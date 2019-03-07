/* Main-level NOS program:
 *  initialization
 *  keyboard processing
 *  generic user commands
 *
 * Copyright 1986-1996 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#if	defined(__TURBOC__) && defined(MSDOS)
#include <io.h>
#include <conio.h>
#endif
#include "global.h"
#include <stdarg.h>
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "ax25.h"
#include "kiss.h"
#include "enet.h"
#include "netrom.h"
#include "ftpcli.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "hardware.h"
#include "usock.h"
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"
#include "daemon.h"
#include "devparam.h"
#include "domain.h"
#include "files.h"
#include "main.h"
#include "trace.h"
#include "display.h"

extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

#ifndef	MSDOS			/* PC uses F-10 key always */
static char Escape = 0x1d;	/* default escape character is ^] */
#endif

char Badhost[] = "Unknown host %s\n";
char *Hostname;
char Nospace[] = "No space!!\n";	/* Generic malloc fail message */
struct proc *Cmdpp;
struct proc *Display;
char *Cmdline;				/* Copy of most recent command line */
int main_exit = FALSE;			/* from main program (flag) */

static char Prompt[] = "net> ";
static FILE *Logfp;
static time_t StartTime;		/* time that NOS was started */
static int Verbose;

static int keychar(int c);
static void pass(char *,int len);
static void passchar(int c);
static void helpsub(struct cmds *cmds);

int
main(int argc,char *argv[])
{
	FILE *fp;
	struct daemon *tp;
	int c;
	char cmdbuf[256];
	long hinit = 102400;
	unsigned chunk;

	StartTime = time(&StartTime);

	while((c = getopt(argc,argv,"f:s:d:bvh:")) != EOF){
		switch(c){
		case 'h':	/* Heap initialization */
			hinit = atol(optarg);
			break;
		case 'f':	/* Number of files */
			Nfiles = atoi(optarg);
			break;
		case 's':	/* Number of sockets */
			Nsock = atoi(optarg);
			break;
		case 'd':	/* Root directory for various files */
			initroot(optarg);
			break;
#ifdef	__TURBOC__
		case 'b':	/* Use BIOS for screen output */
			directvideo = 0;
			break;
#endif
		case 'v':
			Verbose = 1;
			break;
		}
	}
	kinit();
	ioinit(hinit);
	sockinit();
	Cmdpp = mainproc("cmdintrp");

	Sessions = (struct session **)callocw(Nsessions,sizeof(struct session *));
	Command = Lastcurr = newsession("command interpreter",COMMAND,1);
	Display = newproc("display",350,display,0,NULL,NULL,0);
	printf("KA9Q NOS version %s\n",Version);
#ifdef	CPU386
	printf("Compiled for 386/486 CPU\n");
#endif
	printf("Copyright 1986-1996 by Phil Karn, KA9Q\n");
	if(Verbose){
		printf("cs = %lx ds = %lx ss = %lx\n",_go32_my_cs(),_go32_my_ds(),
		  _go32_my_ss());
	}
	usercvt();
	/* Start background Daemons */
	for(tp=Daemons;;tp++){
		if(tp->name == NULL)
			break;
		newproc(tp->name,tp->stksize,tp->fp,0,NULL,NULL,0);
	}
	Encap.txproc = newproc("encap tx",512,if_tx,0,&Encap,NULL,0);
	if(optind < argc){
		/* Read startup file named on command line */
		if((fp = fopen(argv[optind],READ_TEXT)) == NULL){
			printf("Can't read config file %s",argv[optind]);
			perror("");
		}
	} else {
		fp = fopen(Startup,READ_TEXT);
	}
	if(fp != NULL){
		while(fgets(cmdbuf,sizeof(cmdbuf),fp) != NULL){
			rip(cmdbuf);
			Cmdline = strdup(cmdbuf);
			if(Verbose)
				printf("%s\n",Cmdline);
			if(cmdparse(Cmds,cmdbuf,NULL) != 0){
				printf("input line: %s\n",Cmdline);
			}
			FREE(Cmdline);
		}
		fclose(fp);
	}
	/* Now loop forever, processing commands */
	for(;;){
		printf(Prompt);
		fflush(stdout);
		if(fgets(cmdbuf,sizeof(cmdbuf),stdin) != NULL){
			rip(cmdbuf);
			FREE(Cmdline);
			Cmdline = strdup(cmdbuf);
			(void)cmdparse(Cmds,cmdbuf,Lastcurr);
		}
	}
}
/* Keyboard input process */
void
keyboard(
int i,
void *v1,
void *v2
){
	int c;
	int j;

	/* Keyboard process loop */
loop:
	c = kbread();
#ifdef	MSDOS
	if(c >= 256){
		/* Pass all special characters to app upcall */
		if(Current->ctlproc != NULL && (c = (*Current->ctlproc)(c)) == 0)
			goto loop;	/* Upcall took them */

		c -= 256;
		if(Current->scrollmode == SCROLL_INBAND){
			/* In inband scroll mode, pass escape sequences
			 * for cursor control keys. Otherwise fall thru
			 */ 
			switch(c){
			case CURSHOM:
				pass("\033O\0",3);
				goto loop;
			case CURSUP:
				pass("\033OA",3);
				goto loop;
			case PAGEUP:
				pass("\033[5~",4);
				goto loop;
			case CURSEND:
				pass("\033OU",3);
				goto loop;
			case CURSDWN:
				pass("\033OB",3);
				goto loop;
			case PAGEDWN:
				pass("\033[6~",4);
				goto loop;
			case CURSRIGHT:
				pass("\033OC",3);
				goto loop;
			case CURSLEFT:
				pass("\033OD",3);
				goto loop;
			}
		}
		/* In local scroll mode, we can get here with cursor
		 * control keys
		 */
		switch(c){
		case CURSHOM:
			dhome(Current->output->ptr);
			break;
		case CURSUP:
			dcursup(Current->output->ptr);
			break;
		case PAGEUP:
			dpgup(Current->output->ptr);
			break;
		case CURSEND:
			dend(Current->output->ptr);
			break;
		case CURSDWN:
			dcursdown(Current->output->ptr);
			break;
		case PAGEDWN:
			dpgdown(Current->output->ptr);
			break;
		case F10:	/* F-10 (go to command mode) */
			if(Current != Command){
				/* Save current tty mode and set cooked */
				Lastcurr = Current;
				Current = Command;
				alert(Display,1);
			}
			break;
		case F9:	/* F-9 (resume last current session) */
			if(Current == Command && Lastcurr != NULL){
				Current = Lastcurr;
				alert(Display,1);
			}
			break;
		case F8:	/* F-8 (next session) */
			for(j = Current->index+1;j != Current->index;j++){
				if(j >= Nsessions)
					j = 0;
				if(Sessions[j] != NULL){
					Current = Sessions[j];
					alert(Display,1);
					break;
				}
			}
			break;
		case F7:	/* F-7 (prev session) */
			for(j = Current->index-1;j != Current->index;j--){
				if(j == -1)
					j = Nsessions-1;
				if(Sessions[j] != NULL){
					Current = Sessions[j];
					alert(Display,1);
					break;
				}
			}
			break;
		case F6:	/* Toggle scroll mode */
			if(Current == NULL)
				break;
			Current->scrollmode = !Current->scrollmode;
			dscrollmode(Current->output->ptr,Current->scrollmode);
			break;
		case F5:	/* Kick current session */
			if(Current != NULL)
				dokick(0,NULL,Current);
			break;
		case AF1:
		case AF2:
		case AF3:
		case AF4:
		case AF5:
		case AF6:
		case AF7:
		case AF8:
		case AF9:
		case AF10:	/* Alt-F1 thru Alt-F10 */
			c -= 103;
			if(c < Nsessions && Sessions[c] != NULL){
				Current = Sessions[c];
				alert(Display,1);
			}
			break;
		case AF11:	/* Alt-F11 or Alt-F12 */
		case AF12:
			c -= 128;
			if(c < Nsessions && Sessions[c] != NULL){
				Current = Sessions[c];
				alert(Display,1);
			}
			break;
		default:		/* else ignore */
			break;
		}
		goto loop;
	}
#else
	if(c == Escape && Escape != 0 && Current != Command){
		/* Save current tty mode and set cooked */
		Lastcurr = Current;
		Current = Command;
		alert(Display,1);
		goto loop;
	}
#endif
	passchar(c);
	goto loop;
}
static void
pass(char *s,int len)
{
	while(len-- != 0)
		passchar(*s++);
}

static void
passchar(int c)
{
	int cnt;

	/* If a normal-character upcall exists, give it the character.
	 * if the upcall returns 0, don't pass it to the regular tty editor
	 */
	if(Current->inproc != NULL && (*Current->inproc)(c) == 0)
		return;

	/* Ordinary ASCII character, hand to tty editor */
	if((cnt = ttydriv(Current,(char)c)) != 0){
		/* Input ready to hand to process */
		fwrite(Current->ttystate.line,1,cnt,Current->input);
		fflush(Current->input);
	}
}
/* Standard commands called from main */
int
dorepeat(
int argc,
char *argv[],
void *p
){
	int32 interval;
	int ret;
	struct session *sp;

	if(isdigit(argv[1][0])){
		interval = atol(argv[1]);
		argc--;
		argv++;
	} else {
		interval = MSPTICK;
	}
	if((sp = newsession(Cmdline,REPEAT,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	/* Set enough buffering to handle an entire screen so it'll get
	 * displayed in one quick update when we flush
	 */
	setvbuf(sp->output,NULL,_IOFBF,2048);
	while(sp->inproc == keychar){	/* ^C will clear sp->inproc */
		printf("%c[2J",ESC);	/* Clear screen */
		ret = subcmd(Cmds,argc,argv,p);
		fflush(sp->output);
		if(ret != 0 || ppause(interval) == -1)
			break;
	}
	keywait(NULL,1);
	freesession(&sp);
	return 0;
}
static int
keychar(int c)
{
	if(c != CTLC)
		return 1;	/* Ignore all but ^C */

	fprintf(Current->output,"^C\n");
	alert(Current->proc,EABORT);
	Current->inproc = NULL;
	return 0;
}

int
dodelete(
int argc,
char *argv[],
void *p
){
	int i;

	for(i=1;i < argc; i++){
		if(unlink(argv[i]) == -1){
			printf("Can't delete %s",argv[i]);
			perror("");
		}
	}
	return 0;
}
int
dorename(
int argc,
char *argv[],
void *p
){
	if(rename(argv[1],argv[2]) == -1){
		printf("Can't rename %s",argv[1]);
		perror("");
	}
	return 0;
}
int
doexit(
int argc,
char *argv[],
void *p
){
	int i;
	time_t StopTime;
	struct session *sp;

	StopTime = time(&StopTime);
	main_exit = TRUE;	/* let everyone know we're out of here */
	/* Alert each session task that we're aborting */
	for(i=0;i<Nsessions;i++){
		if((sp = Sessions[i]) == NULL)
			continue;
		alert(sp->proc,EABORT);
		alert(sp->proc1,EABORT);
		alert(sp->proc2,EABORT);
	}
	reset_all();
	if(Dfile_updater != NULL)
		alert(Dfile_updater,0);	/* don't wait for timeout */
	for(i=0;i<100;i++)
		kwait(NULL);	/* Allow tasks to complete */
	shuttrace();
	logmsg(-1,"NOS was stopped at %s", ctime(&StopTime));
	if(Logfp){
		fclose(Logfp);
		Logfp = NULL;
	}
	clrscr();
	iostop();
	exit(0);
	return 0;	/* To satisfy lint */
}
int
doreboot(
int argc,
char *argv[],
void *p
){
	time_t StopTime;

	StopTime = time(&StopTime);
	logmsg(-1,"NOS reboot at %s",ctime(&StopTime));
	ppause(1000L);
	iostop();
	sysreset();	/* no return */
	return 0;	/* to satisfy lint */
}

int
dohostname(
int argc,
char *argv[],
void *p
){
	if(argc < 2)
		printf("%s\n",Hostname);
	else {
		struct iface *ifp;
		char *name;

		if((ifp = if_lookup(argv[1])) != NULL){
			if((name = resolve_a(ifp->addr, FALSE)) == NULL){
				printf("Interface address not resolved\n");
				return 1;
			} else {
				FREE(Hostname);
				Hostname = name;

				/* remove trailing dot */
				if ( Hostname[strlen(Hostname)] == '.' ) {
					Hostname[strlen(Hostname)] = '\0';
				}
				printf("Hostname set to %s\n", name );
			}
		} else {
			FREE(Hostname);
			Hostname = strdup(argv[1]);
		}
	}
	return 0;
}
int
dolog(
int argc,
char *argv[],
void *p
){
	static char *logname;

	if(argc < 2){
		if(Logfp)
			printf("Logging to %s\n",logname);
		else
			printf("Logging off\n");
		return 0;
	}
	if(Logfp){
		logmsg(-1,"NOS log closed");
		fclose(Logfp);
		Logfp = NULL;
		FREE(logname);
	}
	if(strcmp(argv[1],"stop") != 0){
		logname = strdup(argv[1]);
		Logfp = fopen(logname,APPEND_TEXT);
		logmsg(-1,"NOS was started at %s", ctime(&StartTime));
	}
	return 0;
}
int
dohelp(
int argc,
char *argv[],
void *p
){
	helpsub(Cmds);
	return 0;
}

int
dorhelp(
int argc,
char *argv[],
void *p
){
	helpsub(Remcmds);
	return 0;
}
static void
helpsub(struct cmds *cmds)
{
	struct cmds *cmdp;
	int i;
	char buf[66];

	printf("Main commands:\n");
	memset(buf,' ',sizeof(buf));
	buf[64] = '\n';
	buf[65] = '\0';
	for(i=0,cmdp = cmds;cmdp->name != NULL;cmdp++,i = (i+1)%4){
		strncpy(&buf[i*16],cmdp->name,strlen(cmdp->name));
		if(i == 3){
			printf(buf);
			memset(buf,' ',sizeof(buf));
			buf[64] = '\n';
			buf[65] = '\0';
		}
	}
	if(i != 0)
		printf(buf);
}

/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<speed>]
 */
int
doattach(
int argc,
char *argv[],
void *p
){
	return subcmd(Attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
int
doparam(
int argc,
char *argv[],
void *p
){
	struct iface *ifp;
	int param;
	int32 val;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(ifp->ioctl == NULL){
		printf("Not supported\n");
		return 1;
	}
	if(argc < 3){
		for(param=1;param<=16;param++){
			val = (*ifp->ioctl)(ifp,param,FALSE,0L);
			if(val != -1)
				printf("%s: %ld\n",parmname(param),val);
		}
		return 0;
	}
	if((param = devparam(argv[2])) == -1){
		printf("Unknown parameter %s\n",argv[2]);
		return 1;
	}
	if(argc < 4){
		/* Read specific parameter */
		val = (*ifp->ioctl)(ifp,param,FALSE,0L);
		if(val == -1){
			printf("Parameter %s not supported\n",argv[2]);
		} else {
			printf("%s: %ld\n",parmname(param),val);
		}
		return 0;
	}
	/* Set parameter */
	(*ifp->ioctl)(ifp,param,TRUE,atol(argv[3]));
	return 0;
}

#ifndef	MSDOS
int
doescape(
int argc,
char *argv[],
void *p
){
	if(argc < 2)
		printf("0x%x\n",Escape);
	else
		Escape = *argv[1];
	return 0;
}
#endif	MSDOS

/* Execute a command with output piped to more */
int
dopage(
int argc,
char *argv[],
void *p
){
	FILE *fp;
	FILE *outsav;

	fp = tmpfile();
	outsav = stdout;
	stdout = fp;
	subcmd(Cmds,argc,argv,p);
	stdout = outsav;
	newproc("view",512,view,0,(void *)fp,NULL,0);	/* View closes fp */
	return 0;
}

/* Set kernel process debug flag */
int
dodebug(
int argc,
char *argv[],
void *p
){
	setbool(&Kdebug,"kernel debug",argc,argv);
	return 0;
}
/* Set temp file wipe-on-close flag */
int
dowipe(
int argc,
char *argv[],
void *p
){
	setbool(&_clrtmp,"tmp file wiping",argc,argv);
	return 0;
}

/* No-op command */
int
donothing(
int argc,
char *argv[],
void *p
){
	return 0;
}

/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
void
logmsg(int s,char *fmt, ...)
{
	va_list ap;
	char *cp;
	time_t t;
	int i;
	struct sockaddr fsocket;
#ifdef	MSDOS
	int fd;
#endif

	if(Logfp == NULL)
		return;

	time(&t);
	cp = ctime(&t);
	rip(cp);
	i = SOCKSIZE;
	fprintf(Logfp,"%s",cp);
	if(getpeername(s,&fsocket,&i) != -1)
		fprintf(Logfp," %s",psocket(&fsocket));

	fprintf(Logfp," - ");
	va_start(ap,fmt);
	vfprintf(Logfp,fmt,ap);
	va_end(ap);
	fprintf(Logfp,"\n");
	fflush(Logfp);
#ifdef	MSDOS
	/* MS-DOS doesn't really flush files until they're closed */
	fd = fileno(Logfp);
	if((fd = dup(fd)) != -1)
		close(fd);
#endif
}


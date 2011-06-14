#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <pthread.h>

#include "nweb.h"

#define MAX_SOCKET_QUEUE 64
#define DEFAULT_THREADS_COUNT 20

void log(int type, char *s1, char *s2, int num)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid()); break;
	case SORRY: 
		(void)sprintf(logbuffer, "<HTML><BODY><H1>nweb Web Server Sorry: %s %s</H1></BODY></HTML>\r\n", s1, s2);
		(void)write(num,logbuffer,strlen(logbuffer));
		(void)sprintf(logbuffer,"SORRY: %s:%s",s1, s2); 
		break;
	case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,num); break;
	}	
	/* no checks here, nothing can be done a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer)); 
		(void)write(fd,"\n",1);      
		(void)close(fd);
	}
	if(type == ERROR) exit(3);
}


/*
 * Structure to store not processed requests 
 */
struct stack_item {
  int socketfd;
  int hit;
};

struct stack{
  int top;
  struct stack_item items[MAX_SOCKET_QUEUE];
  pthread_mutex_t mutex;
};

void stack_init(struct stack * p_stack){
  pthread_mutex_init(&((*p_stack).mutex), NULL);
  (*p_stack).top = 0;
}

int stack_get_count(struct stack * p_stack){
  int result = 0;
  pthread_mutex_lock(&(*p_stack).mutex);

  result = (*p_stack).top;

  pthread_mutex_unlock(&(*p_stack).mutex);

  return result;
}

int stack_pop(struct stack * p_stack, int * socketfd, int * hit){
  int result = 0;
  pthread_mutex_lock(&(*p_stack).mutex);

  if ((*p_stack).top > 0){
    (*p_stack).top--;

    (*socketfd) = (*p_stack).items[(*p_stack).top].socketfd;
    (*hit) = (*p_stack).items[(*p_stack).top].hit;

    log(LOG, "Stack:", "pop", *hit);

    result = 1;
  }

  pthread_mutex_unlock(&(*p_stack).mutex);

  return result;
}

int stack_push(struct stack * p_stack, int socketfd, int hit){
  int result = 0;
  log(LOG, "Stack:", "push", hit);
  pthread_mutex_lock(&(*p_stack).mutex);

  if ((*p_stack).top < MAX_SOCKET_QUEUE -1){
    (*p_stack).items[(*p_stack).top].socketfd = socketfd;
    (*p_stack).items[(*p_stack).top].hit = hit;

    (*p_stack).top++;

    result = 1;
  }

  pthread_mutex_unlock(&(*p_stack).mutex);

  return result;
}


struct stack connections;

pthread_mutex_t nweb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t nweb_cond = PTHREAD_COND_INITIALIZER;


/* this is a child web server process, so we can exit on errors */
void * web(void * param)
{
	int j, file_fd, buflen, len, fd, hit;
	long i, ret;
	char * fstr;
	static char buffer[BUFSIZE+1]; /* static so zero filled */
  int proceed = 0;

  while(1){

    pthread_mutex_lock(&nweb_mutex);

    proceed = 0;

    if(!(proceed = stack_pop(&connections, &fd, &hit))){ // if there is no pending connections to handle
      pthread_cond_wait(&nweb_cond, &nweb_mutex); // lock waiting for condition

      proceed = stack_pop(&connections, &fd, &hit); // Obtain the connection info
      
      if(proceed){
        ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
      }
    }
    else
      ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
    
    // release the mutex as soon as the data was loaded into local buffer
    pthread_mutex_unlock(&nweb_mutex);

    /*
     * The thread will continue running while there exists any pending connections in stack
     * this behaviour ensure that if the working threads are all working when new connections
     * arrives, then any running thread could handle it.
     */
    if (proceed){

      if(ret == 0 || ret == -1) {	/* read failure stop now */
        log(SORRY,"failed to read browser request","",fd);
      }
      if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
        buffer[ret]=0;		/* terminate the buffer */
      else buffer[0]=0;

      for(i=0;i<ret;i++)	/* remove CF and LF characters */
        if(buffer[i] == '\r' || buffer[i] == '\n')
          buffer[i]='*';
      log(LOG,"request",buffer,hit);

      if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) )
        log(SORRY,"Only simple GET operation supported",buffer,fd);

      for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
        if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
          buffer[i] = 0;
          break;
        }
      }

      for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
        if(buffer[j] == '.' && buffer[j+1] == '.')
          log(SORRY,"Parent directory (..) path names not supported",buffer,fd);

      if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
        (void)strcpy(buffer,"GET /index.html");

      /* work out the file type and check we support it */
      buflen=strlen(buffer);
      fstr = (char *)0;
      for(i=0;extensions[i].ext != 0;i++) {
        len = strlen(extensions[i].ext);
        if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
          fstr =extensions[i].filetype;
          break;
        }
      }
      if(fstr == 0) log(SORRY,"file extension type not supported",buffer,fd);

      if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) /* open the file for reading */
        log(SORRY, "failed to open file",&buffer[5],fd);

      log(LOG,"SEND",&buffer[5],hit);

      (void)sprintf(buffer,"HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", fstr);
      (void)write(fd,buffer,strlen(buffer));

      /* send file in 8KB block - last block may be smaller */
      while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
        (void)write(fd,buffer,ret);
      }

      (void)close(fd);
#ifdef LINUX
      sleep(1);	/* to allow socket to drain */
#endif
      //exit(1);
    }
  }
}


int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd, hit, opt, threads_count;
	size_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
  pthread_t nweb_thread;

  threads_count = DEFAULT_THREADS_COUNT;

  while((opt = getopt(argc, argv, "t:")) != -1){
    switch(opt){
      case 't':
        threads_count = atoi(optarg);
        break;
      default:
        printf("Argument error");
        exit(1);
        break;
    }
  }

  /* Temporary fix*/
  argc -= optind -1;
  argv += optind -1;

	if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
		(void)printf("hint: nweb Port-Number Top-Directory\n\n"
	"\tnweb is a small and very safe mini web server\n"
	"\tnweb only servers out file/web pages with extensions named below\n"
	"\t and only from the named directory or its sub-directories.\n"
	"\tThere is no fancy features = safe and secure.\n\n"
	"\t\tExample: nweb 8181 /home/nwebdir &\n\n"
	"\tor you can indicate the number of working threads to handle requests:\n\n"
	"\t\tExample: nweb -t 20 8181 /home/nwebdir &\n\n"
	"\tOnly Supports:");
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
	"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
	"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"
		    );
		exit(0);
	}

	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}

	
  /* Become deamon + unstopable and no zombies children (= no wait()) */
	if(fork() != 0)
		return 0; /* parent returns OK to shell */
	(void)signal(SIGCLD, SIG_IGN); /* ignore child death */
	(void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
	for(i=0;i<32;i++)
		(void)close(i);		/* close open files */
	(void)setpgrp();		/* break away from process group */

	log(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		log(ERROR, "system call","socket",0);
	port = atoi(argv[1]);
	if(port < 0 || port >60000)
		log(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		log(ERROR,"system call","bind",0);
	if( listen(listenfd,64) <0)
		log(ERROR,"system call","listen",0);

  // initialize connection stack
  stack_init(&connections);

  log(LOG, "Stack:", "count", stack_get_count(&connections));

  for (i=0; i < threads_count; i++){
     pthread_create(&nweb_thread, NULL, web, NULL);
  }

	for(hit=1; 1; hit++) {
		length = sizeof(cli_addr);

    log(LOG, "Ready to accept new connections", "process", hit);

		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			log(ERROR,"system call","accept",0);

    log(LOG, "New connection:", "Id", hit);

    if(stack_push(&connections, socketfd, hit)){

      pthread_mutex_lock(&nweb_mutex);
    
      pthread_cond_signal(&nweb_cond);

      //web(NULL); 
    
      pthread_mutex_unlock(&nweb_mutex);
    }
    else
      (void)close(socketfd);
	}
}

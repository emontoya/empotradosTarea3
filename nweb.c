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
#include <pthread.h>
#include <time.h>

#include "nweb.h"

/* 
 * Global structure variable to hold the new connection info.
 */
struct nwebConnection nweb_socket;

pthread_mutex_t nweb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t nweb_cond = PTHREAD_COND_INITIALIZER;

void log(int type, char *s1, char *s2, int num)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

  char time_buffer[30];
  struct timeval tv;
  time_t curtime;

  gettimeofday(&tv, NULL);
  curtime = tv.tv_sec;

  strftime(time_buffer, 30, "%m:%d:%Y:%T.", localtime(&curtime));

	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"[%s] ERROR: %s:%s Errno=%d exiting pid=%d", time_buffer,s1, s2, errno,getpid()); break;
	case SORRY: 
		(void)sprintf(logbuffer, "<HTML><BODY><H1>nweb Web Server Sorry: %s %s</H1></BODY></HTML>\r\n", s1, s2);
		(void)write(num,logbuffer,strlen(logbuffer));
		(void)sprintf(logbuffer,"[%s] SORRY: %s:%s", time_buffer,s1, s2); 
		break;
	case LOG: (void)sprintf(logbuffer,"[%s] INFO: %s:%s:%d", time_buffer,s1, s2,num); break;
	}	
	/* no checks here, nothing can be done a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer)); 
		(void)write(fd,"\n",1);      
		(void)close(fd);
	}
	if(type == ERROR) exit(3);
}

/* this is a child web server process, so we can exit on errors */
void * web(void * param)
{
  int fd = 0, hit = 0;

	int j, file_fd, buflen, len;
	long i, ret;
	char * fstr;
	static char buffer[BUFSIZE+1]; /* static so zero filled */

  /*
   * Infinit loop to process incomming requests
   */
  while(1){
    pthread_mutex_lock(&nweb_mutex);
   
    // If there is no message to process
    //while (nweb_socket.processed){
    //  pthread_cond_wait(&nweb_cond, &nweb_mutex); // lock waiting for condition
    //}

    // Passing the values to former variables, so the code change is minimun 
    fd = nweb_socket.socketfd;
    hit = nweb_socket.hit;

    ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */

    nweb_socket.processed = 1;

    // release the mutex as soon as the data was loaded into local buffer
    pthread_mutex_unlock(&nweb_mutex);

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
#ifdef LINUX
    sleep(1);	/* to allow socket to drain */
#endif
  }

  pthread_exit(NULL);
}


int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd, hit;
	size_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
  pthread_t nweb_thread;

	if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
		(void)printf(PARAMETERS_HINT); /* print information related to software usage */
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf(NOT_SUPORTED_FEATURES); /* print information about not suported features*/

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

  /*
   * Initialize the general condition indicating that there is no new
   * request to process yet
   */
  nweb_socket.processed = 1;

  pthread_mutex_init(&nweb_mutex, NULL);
  pthread_cond_init(&nweb_cond, NULL);

  // TODO: Read the number of threads to create and validate it


  /* Create the threads to handle incomming requests*/
  //for (i=0; i < MAX_NWEB_THREADS; i++){
      pthread_create(&nweb_thread, NULL, web, NULL);
  //}
  
  log(LOG, "Configuration finished", "",0);


	for(hit=1; ;hit++) {

    //while(!nweb_socket.processed);

    // lock until mutex acquired (enter critical section)
    // pthread_mutex_lock(&nweb_mutex);

    length = sizeof(cli_addr);

		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			log(LOG,"system call --","accept",0);

    (void)close(listenfd);

    // copy data to shared structure
    nweb_socket.hit = hit;
    nweb_socket.socketfd = socketfd;

    // Set the indicator that there is new data to process
    nweb_socket.processed = 0;

    // wakeup one thread waiting for condition
    //pthread_cond_signal(&nweb_cond);

    // release the mutex(exit critical section)
    pthread_mutex_unlock(&nweb_mutex);
    web(NULL);
	}
  log(LOG, "Execution Finished", "",0);
}

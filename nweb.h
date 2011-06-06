#ifndef NWEB_H
#define NWEB_H

#define BUFSIZE 8096
#define ERROR 42
#define SORRY 43
#define LOG   44

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },  
	{"jpg", "image/jpeg"}, 
	{"jpeg","image/jpeg"},
	{"png", "image/png" },  
	{"zip", "image/zip" },  
	{"gz",  "image/gz"  },  
	{"tar", "image/tar" },  
	{"htm", "text/html" },  
	{"html","text/html" },  
	{0,0} };

#define PARAMETERS_HINT "\
hint:\tnweb Port-Number Top-Directory\n\n\
\tnweb is a small and very safe mini web server\n\
\tnweb only servers out file/web pages with extensions named below\n\
\tand only from the named directory or its sub-directories.\n\
\tThere is no fancy features = safe and secure.\n\n\
\tExample: nweb 8181 /home/nwebdir &\n\n\
\tOnly Supports:"

#define NOT_SUPORTED_FEATURES "\n\
\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n\
\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n\
\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"

/* Maximun nweb threads to handle HTTP requests*/
#define MAX_NWEB_THREADS 10

/* Structure to hold the information passed to the web running thread*/
struct nwebConnection{
    int socketfd;
    int hit ;
};
#endif

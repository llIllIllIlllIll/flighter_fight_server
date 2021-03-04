#include "net.h"
#include <stdio.h>
int main(int argc, char * argv []){
	int clientfd;
	char buf[MAXLINE];
	rio_t rio;


	if(argc != 3){
		fprintf(stderr,"usage: %s <host> <port>\n",argv[0]);
		return -1;
	}
	char * host = argv[1];
	char * port = argv[2];

	clientfd = open_clientfd(host,port);
	if(clientfd<0){
		fprintf(stderr,"net error\n");
	}


	rio_readinitb(&rio,clientfd);

	char * content = "1 1 1 1 1 1\n1 1 0.0.0.0 1234 1 1 1\n";
	rio_writen(clientfd,content,strlen(content));
	return 0;

}

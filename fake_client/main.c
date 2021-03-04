#include "net.h"
#include <stdio.h>
int main(int argc, char * argv []){
	int clientfd;
	char buf[MAXLINE];
	rio_t rio;
	int n;
	int clock;
	int i;

	if(argc != 4){
		fprintf(stderr,"usage: %s <host> <port> <clock>\n",argv[0]);
		return -1;
	}
	char * host = argv[1];
	char * port = argv[2];
	clock = atoi(argv[3]);

	clientfd = open_clientfd(host,port);
	if(clientfd<0){
		fprintf(stderr,"net error\n");
	}


	rio_readinitb(&rio,clientfd);

	char * client_first_msg = "1\n";
	rio_writen(clientfd,client_first_msg,strlen(client_first_msg));
	

	for( i = 0; i < clock; i++){
		char * content = "1 1 1 1 1\n0\n";
		rio_writen(clientfd,content,strlen(content));

		n = 0;
		while(n == 0){
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				printf("res:%s",buf);
				break;
			}
		}
	}
	return 0;

}

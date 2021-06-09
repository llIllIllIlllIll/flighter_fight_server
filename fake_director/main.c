#include "net.h"
#include <stdio.h>
#include <stdlib.h>
// this is a test program aiming to simulate room_server's function
int main(int argc, char * argv []){
	int clientfd;
	char buf[MAXLINE];
	char temp_buf[MAXLINE];
	rio_t rio;
	int rooms;
	int clients_per_room;
	int clientid;
	int i,j;
	int roomid;
	int32_t content[2];

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

	while(1){
		scanf("%d %d",content,content+1);
		rio_writen(clientfd,content,sizeof(content));
	}


	return 0;

}

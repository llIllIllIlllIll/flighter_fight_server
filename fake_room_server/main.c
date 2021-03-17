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

	if(argc != 5){
		fprintf(stderr,"usage: %s <host> <port> <room_number> <clients_per_room>\n",argv[0]);
		return -1;
	}
	char * host = argv[1];
	char * port = argv[2];
	rooms = atoi(argv[3]);
	clients_per_room = atoi(argv[4]);
	
	clientid = 1;
	roomid = 1;

	for(i = 0; i < rooms; i++){
		clientfd = open_clientfd(host,port);
		if(clientfd<0){
			fprintf(stderr,"net error\n");
		}
		rio_readinitb(&rio,clientfd);
		buf[0] = '\0';
		sprintf(buf,"firstline\nabc\r\n\r\n%d %d 1 1 1 %d\n",roomid,clients_per_room,clients_per_room);
		for(j = 0; j < clients_per_room; j++){
			sprintf(temp_buf,"%d 1 localhost 1234 1 %d 1\n",clientid,clientid);
			clientid++;
			strcat(buf,temp_buf);
		}
		printf("room %d ready to write... content:\n%s\n",i,buf);
		rio_writen(clientfd,buf,strlen(buf));
		close(clientfd);
		sleep(1);
		roomid++;
	}

	return 0;

}

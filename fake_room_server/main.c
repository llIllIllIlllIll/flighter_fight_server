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
	int match_type;
	int i,j;
	int roomid;

	if(argc != 6){
		fprintf(stderr,"usage: %s <host> <port> <match_type> <room_number> <clients_per_room>\n",argv[0]);
		return -1;
	}
	char * host = argv[1];
	char * port = argv[2];
	match_type = atoi(argv[3]);
	clients_per_room = atoi(argv[5]);
	rooms = atoi(argv[4]);
	
	clientid = 1;
	roomid = 1;

	for(i = 0; i < rooms; i++){
		clientfd = open_clientfd(host,port);
		if(clientfd<0){
			fprintf(stderr,"net error\n");
		}
		rio_readinitb(&rio,clientfd);
		buf[0] = '\0';
		sprintf(buf,"firstline\nabc\r\n\r\n%d %d 1 0 %d %d %d\n",roomid,clients_per_room,match_type,clients_per_room,roomid);
		for(j = 0; j < clients_per_room; j++){
			sprintf(temp_buf,"%d %d localhost 1234 1 %d 1\n",clientid,clientid,clientid);
			clientid++;
			strcat(buf,temp_buf);
		}
		printf("room %d ready to write... content:\n%s\n",i,buf);
		rio_writen(clientfd,buf,strlen(buf));
		close(clientfd);
		sleep(3);
		roomid++;
	}

	return 0;

}

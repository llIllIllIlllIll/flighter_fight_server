#include "flighter_msg.h"
#include "net.h"
#include "ccr_rw_map.h"
#include <stdio.h>
sbuf_t sbuf;
ccr_rw_map cmap;
// each thread be responsible for a room 
// may consider coroutine later
void * room_thread(void * vargp){
	pthread_detach(pthread_self());
	while(1){
		int connfd = sbuf_remove(&sbuf);
	}
	pthread_exit(NULL);
	return NULL;

}
int main(int argc,char * argv[]){
	int roomserver_listenfd,clients_listenfd,connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t tid;
	if(argc != 4){
		fprintf(stderr,"usage: %s <port_for_roomserver> <port_for_clients> <pool_size>\n",argv[0]);
		exit(1);
	}
	roomserver_listenfd = open_listenfd(argv[1]);
	clients_listenfd = open_listenfd(argv[2]);
	int poolsize = atoi(argv[2]);
	sbuf_init(&sbuf,poolsize);
	ccr_rw_map_init(&cmap);
	for(int i = 0; i < poolsize;i++){
		pthread_create(&tid,NULL,room_thread,NULL);
	}

	while(1){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(roomserver_listenfd,(SA *)&clientaddr,&clientlen);
		sbuf_insert(&sbuf,connfd);
	}
	return 0;
}


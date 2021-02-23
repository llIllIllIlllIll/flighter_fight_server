#include "flighter_msg.h"
#include "net.h"
#include "ccr_rw_map.h"
#include <stdio.h>
#define MAXLINE 1024
#define MOVE_AHEAD_IN_BUF(p) (strchr(p," ")+1)
// A BRIEF INTRODUCTION TO THIS SERVER:
// 1. a main thread listens for room server to send room info
// 2. a secondary main thread listens for clients
// 3. for every room (as long as current room number is smaller than max_rooms)
//    a room thread is responsible for everything
// 4. for every client (connected client) (as long as current client number is smaller than max_clients)
// a client thread is responsible for contacting with it
sbuf_t sbuf_for_room_server;
sbuf_t sbuf_for_clients;
ccr_rw_map cmap;
int clients_listenfd;
ccr_ct cct_rooms;
ccr_ct cct_clients;
// client thread
void * client_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLEN];

	pthread_detach(pthread_self());
	
	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_clients);
	rio_readinitb(&rio,connfd);
	
	// read room info from room server
	if((n = rio_readlineb(&rio,buf,MAXLEN)) != 0){
				
	}

	if(ccr_ct_dec(&cct_clients) != 0){
		exit(-1);
	}
	pthread_exit(NULL);
	return NULL;
}

// a thread that waits for clients
// this thread does following things:
// 1. constantly waits for clients
// 2. if a clients connects and it sends a message in the right pattern, checks its corresponding room id
// 3. if this client does have a room number... 
void * waiting_for_clients_thread(void * vargp){
	int connfd;
	int v;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLEN];
	pthread_detach(pthread_self());
	while(1){
		//TODO
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(clients_listenfd,(SA*)&clientaddr,&clientlen);
		sbuf_insert(&sbuf_for_clients,connfd);
		if(ccr_ct_query(&cct_clients,&v) != 0){
			exit(-1);
		}	
		else{
			// create a room thread
			if(v < max_clients){
				if(ccr_ct_inc(&cct_clients) != 0){
					exit(-1);
				}
				pthread_create(&tid,NULL,client_thread,NULL);
			}
		}	
	}
	pthread_exit(NULL);
	return NULL;
}
// each thread be responsible for a room 
// may consider coroutine later
void * room_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLEN];
	char * buf_pt;
	room_info r_i;
	int i;

	pthread_detach(pthread_self());
	
	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_room_server);
	rio_readinitb(&rio,connfd);
	
	// read room info from room server
	// just basic configurations
	if((n = rio_readlineb(&rio,buf,MAXLEN)) != 0){
		buf_pt = buf;
		// TODO: check if at any time atoi returns 0
		// TODO: check if at any time strchr returns NULL
		r_i.room_id = (uint32_t)atoi(buf);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i.room_size = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i.simulation_steplength = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i.env_id = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i.match_type = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i.size = (uint32_t)atoi(buf_pt);
		r_i.clients = (client_info *)malloc(r_i.size*sizeof(client_info));
		for(i = 0; i < r_i.size; i++){
			if((n = rio_readlineb(&rio,buf,MAXLEN)) != 0){
				buf_pt = buf;
				(*(r_i.clients+i)).id = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i.clients+i)).group_id = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				strncpy((*(r_i.clients+i)).host,buf_pt,20);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				strncpy((*(r_i.clients+i)).port,buf_pt,8);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i.clients+i)).sign = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i.clients+i)).flighter_id = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i.clients+i)).flighter_type = (uint32_t)atoi(buf_pt);
			}
			else{
				//TODO
			}
		}
	}
	else{
		// TODO
	}
	// TODO: sync & calc & ... whatever needs be done by a room

	if(ccr_ct_dec(&cct_rooms) != 0){
		exit(-1);
	}
	pthread_exit(NULL);
	return NULL;

}
// main thread AKA waiting for room info thread
int main(int argc,char * argv[]){
	int roomserver_listenfd,connfd;
	int v;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t tid;
	if(argc != 4){
		fprintf(stderr,"usage: %s <port_for_roomserver> <port_for_clients> <max_rooms> <max_clients>\n",argv[0]);
		exit(1);
	}
	roomserver_listenfd = open_listenfd(argv[1]);
	clients_listenfd = open_listenfd(argv[2]);
	// the thread that waits for incoming clients
	// this thread is responsible of assigning a client to its specific room
	pthread_create(&tid,NULL,waiting_for_clients_thread,NULL);

	int max_rooms = atoi(argv[3]);
	int max_clients = atoi(argv[4]);
	sbuf_init(&sbuf_for_room_server,max_rooms);
	sbuf_init(&sbuf_for_clients,max_clients);
			
	ccr_rw_map_init(&cmap);
	ccr_ct_init(&cct_rooms);
	ccr_ct_init(&cct_clients);

	// process msgs from room server
	while(1){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(roomserver_listenfd,(SA *)&clientaddr,&clientlen);
		sbuf_insert(&sbuf_for_room_server,connfd);
		if(ccr_ct_query(&cct_rooms,&v) != 0){
			exit(-1);
		}	
		else{
			// create a room thread
			if(v < max_rooms){
				if(ccr_ct_inc(&cct_rooms) != 0){
					exit(-1);
				}
				pthread_create(&tid,NULL,room_thread,NULL);
			}
		}
	}
	return 0;
}


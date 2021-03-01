#include "flighter_msg.h"
#include "net.h"
#include "ccr_rw_map.h"
#include "ccr_counter.h"
#include <stdio.h>
#define MOVE_AHEAD_IN_BUF(p) (strchr(p,' ')+1)
// A BRIEF INTRODUCTION TO THIS SERVER:
// 1. a main thread listens for room server to send room info
// 2. a secondary main thread listens for clients
// 3. for every room (as long as current room number is smaller than max_rooms)
//    a room thread is responsible for everything
// 4. for every client (connected client) (as long as current client number is smaller than max_clients)
// a client thread is responsible for contacting with it
sbuf_t sbuf_for_room_server;
sbuf_t sbuf_for_clients;
// XXX: no need anymore
// map <client_id,room_id>
// ccr_rw_map cmap_cid2rid;
// map <client_id,client_info>
ccr_rw_map cmap_cid2cinfo;
int clients_listenfd;
ccr_ct cct_rooms;
ccr_ct cct_clients;

int max_rooms;
int max_clients;

// client thread
void * client_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLINE];
	client_info * c_i_pt;
	flighter_status * f_s_pt;
	flighter_op * f_o_pt;
	uint32_t client_id;
	uint64_t v;
	ccr_ct * sync_clients_pt;
	int client_clock;
	int * room_clock_pt;

	pthread_detach(pthread_self());
	
	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_clients);
	rio_readinitb(&rio,connfd);
	
	// first time connection client tells this server its id
	if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
		client_id = (uint32_t)atoi(buf);
	}

	ccr_rw_map_query(&cmap_cid2cinfo,client_id,&v);
	c_i_pt = (client_info *)v;

	// TODO: receive operation & send status later
	f_s_pt = &(c_i_pt->fos->s);
	f_o_pt = &(c_i_pt->fos->op);
	sync_clients_pt = c_i_pt->cct_sync_clients;
	client_clock = 0;
	room_clock_pt = c_i_pt->room_clock_p;

	while(1){
		if(client_clock == *room_clock_pt){
		
		}
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
	char buf[MAXLINE];
	pthread_t tid;
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
// README: relationship between room_clock client_clock
// 1. in the init status all clocks are 0
// 2. only when a client_thread's client_clock is the same as room_clock (suppose n) is it allowed to receive a flighter_op (of clock n) from the client
// 3. after 2 the client_thread's client_clock becomes n+1
// 4. when all client_threads' client_clocks become n+1 room_thread's room_clock becomes n+1
//    and all client_threads' n+1 status is calculated and returned
void * room_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLINE];
	char * buf_pt;
	room_info r_i;
	int i;
	int room_clock;
	ccr_ct cct_sync_clients;
	
	pthread_detach(pthread_self());
	
	// TODO:fault check
	ccr_ct_init(&cct_sync_clients);

	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_room_server);
	rio_readinitb(&rio,connfd);
	
	// init room_clock
	room_clock = 0;
	// read room info from room server
	// just basic configurations
	if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
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
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				buf_pt = buf;
				
				(*(r_i.clients+i)).room_id = r_i.room_id;

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
			
				(*(r_i.clients+i)).fos = (flighter_op_and_status *)malloc(sizeof(flighter_op_and_status));
				// XXX:rules: only when all clients' op tic is x
				// can status move to x (x - 1 before)
				(*(r_i.clients+i)).fos->op.tic = 0;
				(*(r_i.clients+i)).fos->s.tic = 0;
				(*(r_i.clients+i)).fos->s.flighter_id = (*(r_i.clients+i)).flighter_id;
				(*(r_i.clients+i)).fos->s.group_id = (*(r_i.clients+i)).group_id;
				// sync purpose
				(*(r_i.clients+i)).cct_sync_clients = &cct_sync_clients;
				// clock
				(*(r_i.clients+i)).room_clock_p = &room_clock;
			}
			else{
				//TODO: Network problem
			}
			// map client id to room id
			ccr_rw_map_insert(&cmap_cid2cinfo,(*(r_i.clients+i)).id,(uint64_t)(r_i.clients+i));
		}
	}
	else{
		// TODO: Network problem
	}
	// TODO: sync & calc & ... whatever needs be done by a room
	// idea: use a ccr_rw_map to map client id to its current flighter_status and flighter_op
	// 	 use a ccr_counter to let room_thread know when all client_thread s are ready



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

	max_rooms = atoi(argv[3]);
	max_clients = atoi(argv[4]);
	sbuf_init(&sbuf_for_room_server,max_rooms);
	sbuf_init(&sbuf_for_clients,max_clients);
			
	ccr_rw_map_init(&cmap_cid2cinfo);
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


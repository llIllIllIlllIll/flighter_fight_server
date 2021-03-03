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

// mutex & cond for connecting_s_server_thread
// purpose: at one time only one client thread connects the connecting_s_server_thread
pthread_mutex_t mut_s_server;
pthread_cond_t cond_s_server;
// 
uint32_t ready_client_id;

// simulink server host & port
char * s_server_host = NULL;
char * s_server_port = NULL;

// mutex & cond for clients
pthread_mutex_t mut_clients;
pthread_cond_t cond_clients;


// TODO: how many this kind of threads should exist?
void * connecting_s_server_thread(void * vargp){
	rio_t rio;
	char buf[MAXLINE];
	uint64_t v;
	client_info * ready_c_i_pt;
	flighter_status * ready_f_s_pt;
	flighter_op * ready_f_o_pt;

	pthread_detach(pthread_self());
	int client_fd;


	client_fd = open_clientfd(s_server_host,s_server_port);
	if(client_fd < 0){
		fprintf(stderr,"ERROR: cannot connect to simulink server.\n");
		pthread_exit(NULL);
	}
	rio_readinitb(&rio,client_fd);

	while(1){
		pthread_mutex_lock(&mut_s_server);
		while(ready_client_id != 0){
			pthread_cond_wait(&cond_s_server,&mut_s_server);
		}
		ccr_rw_map_query(&cmap_cid2cinfo,ready_client_id,&v);
		ready_c_i_pt = (client_info *)v;
		ready_f_s_pt = &(ready_c_i_pt->fos->s);
		ready_f_o_pt = &(ready_c_i_pt->fos->op);

		//TODO: according to ready_f_s_pt and ready_f_o_pt calculate a new status
		//TODO: remember to add 1 in the tic of new status

		pthread_mutex_unlock(&mut_s_server);
	}
}


// client thread: wait until room_thread is ready
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
	ccr_ct * cct_sync_clients_pt;
	int client_clock;
	int * room_clock_pt;
	// for for loop only
	int i;

	// anything begin with net means this parameter is received from net
	int32_t net_pitch_op;
	int32_t net_roll_op;
	int32_t net_dir_op;
	int32_t net_acc_op;
	uint32_t net_lw_op;
	int32_t net_destroyed_flighter_n;
	uint32_t net_destroyed_flighter_id;

	char * buf_pt;

	pthread_detach(pthread_self());
	
	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_clients);
	rio_readinitb(&rio,connfd);
	
	// first time connection client tells this server its id
	if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
		client_id = (uint32_t)atoi(buf);
	}

	// not 0 means room_thread is not ready yet
	while(ccr_rw_map_query(&cmap_cid2cinfo,client_id,&v) != 0);
	
	c_i_pt = (client_info *)v;

	// TODO: receive operation & send status later
	f_s_pt = &(c_i_pt->fos->s);
	f_o_pt = &(c_i_pt->fos->op);
	cct_sync_clients_pt = c_i_pt->cct_sync_clients;
	client_clock = 0;
	room_clock_pt = c_i_pt->room_clock_p;

	while(1){
		if(client_clock == *room_clock_pt){
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				// operation
				f_o_pt->tic = client_clock;
				net_pitch_op = (int32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				net_roll_op = (int32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				net_dir_op = (int32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				net_acc_op = (int32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				net_lw_op = (uint32_t)atoi(buf_pt);
				// detected destroyed flights
				if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
					buf_pt = buf;
					net_destroyed_flighter_n = (int32_t)atoi(buf_pt);
					for(i = 0; i < net_destroyed_flighter_n;i++){
						buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
						net_destroyed_flighter_id = (uint32_t)atoi(buf_pt);
						// TODO: use this info : destroyed flighters
					}
				}
				else{
					//TODO: net problem
				}
			}
			else{
				//TODO: net problem
			}
			
			// TODO: connect SIMULINK server to calculate status
			// send status & op
			pthread_mutex_lock(&mut_s_server);
			ready_client_id = c_i_pt->id;
			pthread_cond_broadcast(&cond_s_server);
			pthread_mutex_unlock(&mut_s_server);

			// FIXME: this is temporarily a forever loop waits for s_server to do its job
			// use cond later
			while(f_s_pt->tic != client_clock+1){
				continue;
			}
			

			// OK: this client is ready
			if(ccr_ct_inc(cct_sync_clients_pt) != 0){
				//TODO: error
			}
			
			// client_clock ready to go further
			client_clock++;

			// FIXME: use cond here
			while(client_clock != *room_clock_pt){	
			}
			// TODO: send overall state of every flighter back to clients
			rio_writen(connfd,c_i_pt->overall_status,strlen(c_i_pt->overall_status));
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
		
		pthread_mutex_lock(&mut_clients);
		sbuf_insert(&sbuf_for_clients,connfd);
		pthread_cond_broadcast(&cond_clients);
		pthread_mutex_unlock(&mut_clients);

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
	char temp_buf[MAXLINE];
	char * buf_pt;
	room_info r_i;
	int i;
	int room_clock;
	flighter_status * f_s_pt;

	ccr_ct cct_sync_clients;
	// for query only
	int k;
	
	// mutex and cond for this room
	pthread_mutex_t mut_room;
	pthread_cond_t cond_room;

	pthread_mutex_init(&mut_room,NULL);
	pthread_cond_init(&cond_room,NULL);
	r_i.mut = &mut_room;
	r_i.cond = &cond_room;

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
			
				(*(r_i.clients+i)).mut_room = &mut_room;
				(*(r_i.clients+i)).cond_room = &cond_room;
				// overallstatus
				(*(r_i.clients+i)).overall_status = NULL;
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
	// 	 use a ccr_counter to let room_thread know when all client_thread s are ready
	
	while(1){
		k = -1;
		while(k != r_i.size){
			ccr_ct_query(&cct_sync_clients,&k);
		}

		buf[0] = '\0';
		for(i = 0; i < r_i.size; i++){
			f_s_pt = &((*(r_i.clients+i)).fos->s);
			sprintf(temp_buf,"%u %u %d %d %d %d %d %d %d %d %d %d %d %d\n",f_s_pt->flighter_id,f_s_pt->group_id,
				f_s_pt->x,f_s_pt->y,f_s_pt->z,f_s_pt->u,f_s_pt->v,f_s_pt->w,f_s_pt->vx,f_s_pt->vy,f_s_pt->vz,
				f_s_pt->vu,f_s_pt->vv,f_s_pt->vw);
			strcat(buf,temp_buf);
		}
		for(i = 0; i < r_i.size; i++){
			(r_i.clients+i)->overall_status = buf;
		}
		room_clock++;
	}


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

	// mutex & cond for s_server
	pthread_mutex_init(&mut_s_server,NULL);
	pthread_cond_init(&cond_s_server,NULL);
	ready_client_id = 0;

	// mutex & cond for clients
	pthread_mutex_init(&mut_clients,NULL);
	pthread_cond_init(&cond_clients,NULL);


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

	// TODO: pthread_join all threads
	return 0;
}


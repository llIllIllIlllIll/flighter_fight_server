#include "flighter_msg.h"
#include "net.h"
#include "ccr_rw_map.h"
#include "ccr_counter.h"
#include "cJSON.h"
#include "cJSON_RoomStatus.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#define MOVE_AHEAD_IN_BUF(p) (strchr(p,' ')+1)
#define S_SERVER_WORK 1
#define TARGET_FLIGHTER_WORK 1
// This macro is defined in 2021.4.20:
// When two clients participate in a match togetehr the game process moves surprisingly slow
// and I need to use a timer to find the bottleneck
//#define SINGLE_ROOM_DEBUG
// This macro is used to check return value of rio_readnb and rio_readlineb
// and when dealing with rio_readlineb n should be set to 0
#define REC_BYTES_CHECK(A,B,msg) if((A)<(B)){pthread_mutex_lock(&mut_printf);fprintf(stderr,msg);pthread_mutex_unlock(&mut_printf);pthread_exit(NULL);}
#define ROOM_MAX_WAITING_MSEC (60*1000)
#define N_M_SIZE (sizeof(net_match_status))
#define N_F_SIZE (sizeof(net_flighter_status))
#define PI 3.1415926
#define RAND_ANGLE() ((((double)(rand()%60))/60.0)*2*PI)
// 4MB
#define MATCH_RECORD_MAX_SIZE (1<<22)

#define GAME_END_HOST "localhost"
#define GAME_END_PORT "30609"
#define GAME_END_STRING_PATTERN "POST /room/endGame HTTP/1.1\r\nContent-Type: application/json\r\nHost: 202.120.40.8:30609\r\nContent-Length:%d\r\n\r\n%s"


#define MAX_DRONE_N 10
#define MAX_KINE_N 10
// drone tag must be positive and less or equal to MAX_DRONE_TAG 
#define MAX_DRONE_TAG 2
// pool index
#define TAG_2_POOL_INDEX(tag) ((tag)-1)
#define MATCH_TYPE_2_POOL_INDEX(match_type) (((match_type) == 0)?(0):((match_type) == 3 ? 1: -1))


#define MAX_SIMU_WAITING_MSEC (1*1000)

#define BKGGKDD printf("BKGGKDD!\n");

//#define DEBUG
// A BRIEF INTRODUCTION TO THIS SERVER:
// 1. a main thread listens for room server to send room info
// 2. a secondary main thread listens for clients
// 3. for every room (as long as current room number is smaller than max_rooms)
//    a room thread is responsible for everything
// 4. for every client (connected client) (as long as current client number is smaller than max_clients)
// a client thread is responsible for contacting with it
sbuf_t sbuf_for_room_server;
sbuf_t sbuf_for_clients;
// 2 maps:
// clientid - client info
// roomid - room info
ccr_rw_map cmap_cid2cinfo;
ccr_rw_map cmap_rid2rinfo;
// 2 maps: sock id - sock_pair
ccr_rw_map cmap_sid2sp_drone;
ccr_rw_map cmap_sid2sp_kine;
// 2 sbuf: save sock pair for drone or kine
sbuf_t sbuf_4_drone_sp;
sbuf_t sbuf_4_kine_sp;
// drone_thread id
int drone_thread_id;

int clients_listenfd;
ccr_ct cct_rooms;
ccr_ct cct_clients;
int max_rooms;
int max_clients;

int director_listenfd;
// mutex & cond for connecting_s_server_thread
// purpose: at one time only one client thread connects the connecting_s_server_thread
pthread_mutex_t mut_kine;
pthread_cond_t cond_kine;
// mutex & cond for controller
pthread_mutex_t mut_con;
pthread_cond_t cond_con;

// ready_client_id is used to tell connecting_s_server_thread that one of the clients' status
// need to be calculated by kine_server
uint32_t ready_client_ids[MAX_KINE_N];

// ready_pack_pt is used to tell connecting_tf_thread that one of the postures need update
s_server_pack * ready_pack_pts[MAX_DRONE_TAG][MAX_DRONE_N];
// this signal tells room_thread that a new posture is calculated 
//int tf_ready_signal;

// model server
int sserver_listenfd;

// target_flighter AKA ba ji
int tf_listenfd;

// ui
int ui_listenfd;
// mutex& cond for flighter_server( or client?)
pthread_mutex_t mut_drone;
pthread_cond_t cond_drone;

// mutex protect STDOUT
pthread_mutex_t mut_printf;

// this thread is responsible of proviving monitor info to UI
void * ui_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLINE];
	int flags;
	// json obj to return
	cJSON * res_obj = NULL;
	// json array of room_status
	cJSON * room_status_s = NULL;
	// single room_status
	cJSON * room_status = NULL;
	// room id 
	uint64_t room_id = -1;
	// room info addr
	room_info *  r_i_pt = NULL;
	// json string
	char * json_string;
	
	connfd = -1;

	printf("[UI_THREAD] start listening...\n");
	while(connfd < 0){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(ui_listenfd,(SA *)&clientaddr,&clientlen);
		//pthread_mutex_lock(&mut_printf);
		//printf("[UI_THREAD] socket accepted\n");
		//pthread_mutex_unlock(&mut_printf);

		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);
		rio_readinitb(&rio,connfd);		

		n = rio_readlineb(&rio,buf,MAXLINE);
		if(n <= 0){
			//pthread_mutex_lock(&mut_printf);
			//printf("[UI_THREAD] ****************** timeout in reading url from web page *****************\n");
			//pthread_mutex_unlock(&mut_printf);
		}
		else{
			//pthread_mutex_lock(&mut_printf);
			//printf("[UI_THREAD] Request 1st line: %s",buf);
			//pthread_mutex_unlock(&mut_printf);
		}
		// headers
		read_requesthdrs(&rio);
		//printf("[UI_THREAD]parsed all headers\n"); 


		res_obj = cJSON_CreateObject();
		room_status_s = cJSON_CreateArray();		
		if(res_obj == NULL || room_status_s == NULL){
			fprintf(stderr,"[UI_THREAD]********************* ERROR WHEN CREARING JSON OBJECT ********************\n");
		}
		// after read : write
		// iterate over cmap_rid2rinfo
		//printf("111\n");
		while(ccr_rw_map_iterate(&cmap_rid2rinfo,&room_id,(uint64_t *)(&r_i_pt)) != 0){
			//printf("[UI_THREAD] iterate room id : %d \n",r_i_pt->room_id);
			room_status = create_room_status_JSON_obj(r_i_pt);
			//printf("room_status_s : %lld room_statsu : %lld\n",room_status_s,room_status);
			cJSON_AddItemToArray(room_status_s,room_status);
		}
		json_string = cJSON_Print(room_status_s);		

		sprintf(buf,"HTTP/1.1 200 OK\r\nContext-Type:text/html\r\nServer: Flighter Fight Server\r\nAccess-Control-Allow-Origin:*\r\nContent-length: %d\r\nContent-type: html/text\r\n\r\n%s",strlen(json_string),json_string);
		rio_writen(connfd,buf,strlen(buf));
		close(connfd);
		connfd = -1;
		
	}

}

// this thread communicates with Director Server and is capable of controlling process
// in each room 
void * controller_thread(void * vargp){
	int connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	rio_t rio;
	size_t n;
	char buf[MAXLINE];
	int con_signal;
	int reload_flighters_n;
	int con_room_id;
	char * buf_pt;
	uint64_t v;
	room_info * r_i_pt;
	int flags;

	connfd = -1;

	while(connfd < 0){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(director_listenfd,(SA *)&clientaddr,&clientlen);
		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);

		rio_readinitb(&rio,connfd);
		
		pthread_mutex_lock(&mut_printf);
		printf("[CONTROLLER_THREAD] Director has connected\n");
		pthread_mutex_unlock(&mut_printf);
		
		while(connfd > 0){
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				pthread_mutex_lock(&mut_printf);
				printf("[CONTROLLER_THREAD] Received content:%s",buf);
				pthread_mutex_unlock(&mut_printf);
				
				con_room_id = atoi(buf);
				buf_pt = buf;
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				con_signal = atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				reload_flighters_n = atoi(buf);
				//TODO: currently ignore reload flighters signal
				//fix later

				ccr_rw_map_query(&cmap_rid2rinfo,con_room_id,&v);
				r_i_pt = (room_info *)v;

				switch(con_signal){
					case 0:
						r_i_pt->status = 1;
						break;
					case 1:
						r_i_pt->status = 0;
						break;
					case 2:
						r_i_pt->status = 1;
						break;
					case 3:
						// TODO: not supported yet
						r_i_pt->status = 1;
						break;
				}

				//printf("[CONTROLLER_THREAD] room_info addr :%u signal:%d status:%d\n",v,con_signal,r_i_pt->status);
				
			}
			REC_BYTES_CHECK(n,0,"****** E R R O R: timeout in connecting to director ******\n");
		}
	}
		
}

void * drone_thread(void * vargp){
	// rio_rec for receiving info from target_flighter 
	// rio_sen for sending
	rio_t rio;
	char buf[MAXLINE];
	socklen_t clientlen;
	// same as above
	int connfd_rec,connfd_sen,connfd;
	struct sockaddr_storage clientaddr;
	int rec_bytes,wri_bytes;
	socket_pair * sock_pair;
	int64_t item;
	// the array ready_pack_pts is empty
	int empty;
	int i;
	int local_thread_id;
	s_server_pack * ready_pack_pt;
	// time related
	struct timeval tv;
	struct timespec ts;
	long long start,current;
	s_server_pack heartbeat_pack;
	// heartbeat pack is only sent if a drone is free for more than 3 seconds
	// int is_heartbeat;
	int tag;
#ifdef SINGLE_ROOM_DEBUG
	struct timeval srd_tv;
	long long srd_start,srd_current;
	double srd_secs;
#endif
	pthread_detach(pthread_self());
	item = sbuf_remove(&sbuf_4_drone_sp);
	sock_pair = (socket_pair *)item;
	connfd_rec = sock_pair->sock_rec_fd;
	connfd_sen = sock_pair->sock_sen_fd;
	local_thread_id = sock_pair->id;
	tag = sock_pair->tag;
	rio_readinitb(&rio,connfd_rec);

	
	pthread_mutex_lock(&mut_printf);
	printf("[DRONE_THREAD] drone server id %d tag %d connected\n",local_thread_id,tag);
	pthread_mutex_unlock(&mut_printf);	
	
	memset(&heartbeat_pack,0,sizeof(s_server_pack));

	while(1){
		gettimeofday(&tv,NULL);
		start = TV_TO_MSEC(tv);
		
		empty = 1;	
		pthread_mutex_lock(&mut_drone);
		do{
			gettimeofday(&tv,NULL);
			current = TV_TO_MSEC(tv);
			if(current - start > 1.5*1000){
				//pthread_mutex_lock(&mut_printf);
				//printf("[DRONE_THREAD %d] drone thread idle; send heart-beat pack\n",local_thread_id);
				//pthread_mutex_unlock(&mut_printf);
				//is_heartbeat = 1;		
				ready_pack_pt = &heartbeat_pack;
				empty = 0;
			}				
			else{
				// check if there is any pack available
				for(i = 0; i < MAX_DRONE_N;i++){
					if(ready_pack_pts[tag][i] != NULL){
						empty = 0;
						ready_pack_pt = ready_pack_pts[tag][i];
						ready_pack_pts[tag][i] = NULL;
						break;
					}
				}
			}
			if(empty == 1){
				ts.tv_sec = tv.tv_sec+1;
				ts.tv_nsec = tv.tv_usec*1000;
				pthread_cond_timedwait(&cond_drone,&mut_drone,&ts);		
			}	
		}
		while(empty == 1);
		// NULL and 0 means not OK
		pthread_cond_broadcast(&cond_drone);
		pthread_mutex_unlock(&mut_drone);
			
		//pthread_mutex_lock(&mut_printf);
#ifdef SINGLE_ROOM_DEBUG
	gettimeofday(&srd_tv,NULL);
	srd_start = TV_TO_MSEC(srd_tv);
#endif	
		//printf("[DRONE_THREAD %d] working posture of tic[%d]: %d %d %d %d %d %d %d %d %d %d %d %d\n",local_thread_id,ready_pack_pt->p.tic,ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
		//		ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
		//		ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		//pthread_mutex_unlock(&mut_printf);

		wri_bytes = rio_writen(connfd_sen,(char *)ready_pack_pt,sizeof(s_server_pack));
		if(wri_bytes < sizeof(s_server_pack)){
			printf("****** E R R O R: failed in writing to drone server %d *******\n delete this drone_thread require a new one\n target flighter will stay in its old position\n\n\n",local_thread_id,rec_bytes);
			ready_pack_pt->p.tic++;
			sock_pair->sock_sen_fd = -1;
			sock_pair->sock_rec_fd = -1;
			sleep(1);
			pthread_exit(NULL);

		}		

		rec_bytes = rio_readnb(&rio,(char *)&(ready_pack_pt->p),sizeof(posture),MAX_SIMU_WAITING_MSEC);
		if(rec_bytes < sizeof(posture)){
			printf("****** E R R O R: timeout in reading posture from target flighter server %d,only received %d bytes *******\n delete this drone_thread require a new one\n target flighter will stay in its old position\n\n\n",local_thread_id,rec_bytes);
			ready_pack_pt->p.tic++;
			sock_pair->sock_sen_fd = -1;
			sock_pair->sock_rec_fd = -1;
			sleep(1);
			pthread_exit(NULL);
		}
		ready_pack_pt->p.tic++;
#ifdef SINGLE_ROOM_DEBUG
	gettimeofday(&srd_tv,NULL);
	srd_current = TV_TO_MSEC(srd_tv);
	srd_secs = (srd_current-srd_start)/1000.0;
	pthread_mutex_lock(&mut_printf);
	printf("\n\n[CONNECTING_TARGET_FLIGHTER_THREAD] ###SINGLE_ROOM_DEBUG MODE### TARGET_FLIGHTER NET_READ TAKES TIME:%lf\n\n",srd_secs);
	pthread_mutex_unlock(&mut_printf);
#endif
		//pthread_mutex_lock(&mut_printf);
		//printf("[DRONE_THREAD %d] new posture of tic[%d]: %d %d %d %d %d %d %d %d %d %d %d %d\n",local_thread_id,ready_pack_pt->p.tic,ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
		//		ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
		//		ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		//pthread_mutex_unlock(&mut_printf);
	}
}



void * waiting_tf_thread(void * vargp){
	rio_t rio;
	char buf[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	socket_role sock_role;
	int rec_bytes;
	int flags;
	socket_pair * sock_pair;
	uint64_t sock_id,v;
	int connfd;	
	pthread_t tid;
	int tag;

	clientlen = sizeof(struct sockaddr_storage);
	pthread_detach(pthread_self());
	// this thread listens for drone socket forever
	while(1){
		connfd = accept(tf_listenfd,(SA *)&clientaddr,&clientlen);
		pthread_mutex_lock(&mut_printf);
		printf("[WAITING_TF_THREAD] a socket has connected: connfd: %d\n",connfd);
		pthread_mutex_unlock(&mut_printf);
		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);
		//BKGGKDD;
		rio_readinitb(&rio,connfd);
		//BKGGKDD;
		rec_bytes = rio_readnb(&rio,&sock_role,sizeof(socket_role),0);
		//BKGGKDD;
		if(rec_bytes < sizeof(socket_role)){
			pthread_mutex_lock(&mut_printf);
			printf("[WAITING_TF_THREAD]****** E R R O R: timeout in reading role from target flighter server ******\n");
			pthread_mutex_unlock(&mut_printf);
			continue;
		}
		//BKGGKDD;
		sock_id = sock_role.id;
		tag = sock_role.tag;
		// check if this sock_id has corresponding sock_pair in storage
		if(ccr_rw_map_query(&cmap_sid2sp_drone,sock_id,&v) == 0){
			sock_pair = (socket_pair *)v;
			if(sock_pair -> tag != tag){
				pthread_mutex_lock(&mut_printf);
				printf("[WAITING_TF_THREAD] ******* E R R O R: inconsistent sock tag ******\n");
				pthread_mutex_unlock(&mut_printf);
				continue;
			}
		}		
		else{
			// if not malloc it
			sock_pair = (socket_pair *)malloc(sizeof(socket_pair));
			sock_pair -> sock_sen_fd = -1;
			sock_pair -> sock_rec_fd = -1;
			sock_pair -> tag = -1;
			ccr_rw_map_insert(&cmap_sid2sp_drone,sock_id,(uint64_t)sock_pair);
		}
		//BKGGKDD;
		if(sock_role.direction == ROLE_SEND){
			if(sock_pair->sock_sen_fd != -1){
				pthread_mutex_lock(&mut_printf);
				printf("[WAITING_TF_THREAD]****** E R R O R: sock id %d already has socket_send ******\n",sock_id);
				pthread_mutex_unlock(&mut_printf);
				continue;
			}
			sock_pair->sock_sen_fd = connfd;
		}
		else if(sock_role.direction == ROLE_RECV){
			if(sock_pair->sock_rec_fd != -1){
				pthread_mutex_lock(&mut_printf);
				printf("[WAITING_TF_THREAD]****** E R R O R: sock id %d already has socket_rec ******\n",sock_id);
				pthread_mutex_unlock(&mut_printf);
				continue;
			}
			sock_pair->sock_rec_fd = connfd;
		}
		else{
			pthread_mutex_lock(&mut_printf);
			fprintf(stderr,"[WAITING_TF_THREAD] wrong pack, direction is neither 0 or 1\n");
			pthread_mutex_unlock(&mut_printf);
			continue;
		}
		
		sock_pair -> tag = tag;
		// if sock_pair has both sen and rec
		if(sock_pair->sock_sen_fd != -1 && sock_pair->sock_rec_fd != -1){
			sock_pair->id = sock_id;
			sbuf_insert(&sbuf_4_drone_sp,(int64_t)sock_pair);
			pthread_create(&tid,NULL,drone_thread,NULL);
		}

	}
}

// The worker thread that's responsible of connecting with kine server
void * kine_thread(void * vargp){
	rio_t rio;
	char buf[MAXLINE];
	uint64_t v;
	client_info * ready_c_i_pt;
	flighter_status * ready_f_s_pt;
	flighter_op * ready_f_o_pt;
	int n,i;
	int rec_bytes,wri_bytes;
	int flags;

	pthread_detach(pthread_self());
	int connfd_rec,connfd_sen;
	s_server_pack * ready_pack_pt;
	int empty;
	int64_t item;
	int local_thread_id;
	socket_pair * sock_pair;
	int ready_client_id;
	// time related
	struct timeval tv;
        struct timespec ts;
        long long start,current;
        s_server_pack heartbeat_pack;
	// heartbeat pack is only sent if a drone is free for more than 3 seconds
	int is_heartbeat;	
#ifdef SINGLE_ROOM_DEBUG
	struct timeval srd_tv;
	long long srd_start,srd_current;
	double srd_secs;
#endif

	ready_pack_pt = (s_server_pack *)malloc(sizeof(s_server_pack));

	item = sbuf_remove(&sbuf_4_kine_sp);
	sock_pair = (socket_pair *)item;
	connfd_rec = sock_pair->sock_rec_fd;
	connfd_sen = sock_pair->sock_sen_fd;
	local_thread_id = sock_pair->id;
	rio_readinitb(&rio,connfd_rec);

	pthread_mutex_lock(&mut_printf);
	printf("[KINE_THREAD] kine server %d connected\n",local_thread_id);
	pthread_mutex_unlock(&mut_printf);	



	while(1){
		gettimeofday(&tv,NULL);
                start = TV_TO_MSEC(tv);

		empty = 1;	
		pthread_mutex_lock(&mut_kine);
		do{
			is_heartbeat = 0;
			gettimeofday(&tv,NULL);
                        current = TV_TO_MSEC(tv);
			if(current - start > 1.5*1000){
                                //pthread_mutex_lock(&mut_printf);
                                //printf("[KINE_THREAD %d] kine thread idle; send heart-beat pack\n",local_thread_id);
                                //pthread_mutex_unlock(&mut_printf);
                                is_heartbeat = 1;             
                                //ready_pack_pt = &heartbeat_pack;
                                empty = 0;
                        }
			else{
			// check if there is any client available
				for(i = 0; i < MAX_KINE_N;i++){
					if(ready_client_ids[i] != 0){
						empty = 0;
						ready_client_id = ready_client_ids[i];
						ready_client_ids[i] = 0;
						break;
					}
				}
			}
			if(empty == 1){
				ts.tv_sec = tv.tv_sec+1;
                                ts.tv_nsec = tv.tv_usec*1000;
				pthread_cond_timedwait(&cond_kine,&mut_kine,&ts);
			}	
		}
		while(empty == 1);
		// removed one client_id
		pthread_cond_broadcast(&cond_kine);
		pthread_mutex_unlock(&mut_kine);
			
		//pthread_mutex_lock(&mut_printf);
#ifdef SINGLE_ROOM_DEBUG
	gettimeofday(&srd_tv,NULL);
	srd_start = TV_TO_MSEC(srd_tv);
#endif	
		if(is_heartbeat){
			ready_pack_pt = &heartbeat_pack;
		}
		else{
			ccr_rw_map_query(&cmap_cid2cinfo,ready_client_id,&v);
			ready_c_i_pt = (client_info *)v;
			if(ready_c_i_pt == NULL){
				//ignore
				continue;
			}
			ready_f_s_pt = &(ready_c_i_pt->fos->s);
			ready_f_o_pt = &(ready_c_i_pt->fos->op);
	
			memcpy(&(ready_pack_pt->p.x),&(ready_f_s_pt->x),sizeof(int32_t)*12);
			memcpy(&(ready_pack_pt->o.pitch),&(ready_f_o_pt->pitch),sizeof(int32_t)*5);
			ready_pack_pt->o.steps = ready_c_i_pt->current_steps;
		}
		//printf("[KINE_THREAD %d] working posture of tic[%d]: %d %d %d %d %d %d %d %d %d %d %d %d\n",local_thread_id,ready_pack_pt->p.tic,ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
		//		ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
		//		ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		//pthread_mutex_unlock(&mut_printf);

		wri_bytes = rio_writen(connfd_sen,(char *)ready_pack_pt,sizeof(s_server_pack));
		if(wri_bytes < sizeof(s_server_pack)){
			printf("****** E R R O R: failed in writing to kine server %d *******\n delete this kine_thread require a new one\n flighter will stay in its old position\n\n\n",local_thread_id);
			if(is_heartbeat == 0)
				ready_f_s_pt->tic++;
			sock_pair->sock_sen_fd = -1;
			sock_pair->sock_rec_fd = -1;
			sleep(1);
			pthread_exit(NULL);

		}		
		

		rec_bytes = rio_readnb(&rio,(char *)&(ready_pack_pt->p),sizeof(posture),MAX_SIMU_WAITING_MSEC);
		if(rec_bytes < sizeof(posture)){
			printf("****** E R R O R: timeout in reading posture from kine server %d,only received %d bytes *******\n delete this kine_thread require a new one\n flighter will stay in its old position\n\n\n",local_thread_id,rec_bytes);
			if(is_heartbeat ==  0)
				ready_f_s_pt->tic++;
			sock_pair->sock_sen_fd = -1;
			sock_pair->sock_rec_fd = -1;
			sleep(1);
			pthread_exit(NULL);
		}
		// result..
		if(is_heartbeat == 0){
			memcpy(&(ready_f_s_pt->x),&(ready_pack_pt->p.x),sizeof(int32_t)*12);
			//memcpy(&(ready_f_o_pt->pitch),&(ready_pack_pt->o.pitch),sizeof(int32_t)*5);
			ready_f_s_pt->tic++;
		}

		//pthread_mutex_lock(&mut_printf);
		//printf("[KINE_THREAD %d] new posture of tic[%d]: %d %d %d %d %d %d %d %d %d %d %d %d\n",local_thread_id,ready_pack_pt->p.tic,ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
		//		ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
		//		ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		//pthread_mutex_unlock(&mut_printf);
	}
}

void * waiting_kine_thread(void * vargp){
	rio_t rio;
	char buf[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	socket_role sock_role;
	int rec_bytes;
	int flags;
	socket_pair * sock_pair;
	uint64_t sock_id,v;
	int connfd;	
	pthread_t tid;

	clientlen = sizeof(struct sockaddr_storage);
	pthread_detach(pthread_self());
	// this thread listens for kine socket forever
	while(1){
		connfd = accept(sserver_listenfd,(SA *)&clientaddr,&clientlen);
		pthread_mutex_lock(&mut_printf);
		printf("[WAITING_KINE_THREAD] a socket has connected: connfd: %d\n",connfd);
		pthread_mutex_unlock(&mut_printf);
		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);
		rio_readinitb(&rio,connfd);
		rec_bytes = rio_readnb(&rio,&sock_role,sizeof(socket_role),0);
		if(rec_bytes < sizeof(socket_role)){
			pthread_mutex_lock(&mut_printf);
			printf("[WAITING_KINE_THREAD]****** E R R O R: timeout in reading role from kine server ******\n");
			pthread_mutex_unlock(&mut_printf);
			continue;
		}
		sock_id = sock_role.id;
		// check if this sock_id has corresponding sock_pair in storage
		if(ccr_rw_map_query(&cmap_sid2sp_kine,sock_id,&v) == 0){
			sock_pair = (socket_pair *)v;
		}		
		else{
			// if not malloc it
			sock_pair = (socket_pair *)malloc(sizeof(socket_pair));
			sock_pair -> sock_sen_fd = -1;
			sock_pair -> sock_rec_fd = -1;
			ccr_rw_map_insert(&cmap_sid2sp_kine,sock_id,(uint64_t)sock_pair);
		}
		if(sock_role.direction == ROLE_SEND){
			if(sock_pair->sock_sen_fd != -1){
				pthread_mutex_lock(&mut_printf);
				printf("[WAITING_KINE_THREAD]****** E R R O R: sock id %d already has socket_send ******\n",sock_id);
				pthread_mutex_unlock(&mut_printf);
				continue;
			}
			sock_pair->sock_sen_fd = connfd;
		}
		else if(sock_role.direction == ROLE_RECV){
			if(sock_pair->sock_rec_fd != -1){
				pthread_mutex_lock(&mut_printf);
				printf("[WAITING_KINE_THREAD]****** E R R O R: sock id %d already has socket_rec ******\n",sock_id);
				pthread_mutex_unlock(&mut_printf);
				continue;
			}
			sock_pair->sock_rec_fd = connfd;
		}
		else{
			pthread_mutex_lock(&mut_printf);
			fprintf(stderr,"[WAITING_KINE_THREAD] wrong pack, direction is neither 0 or 1\n");
			pthread_mutex_unlock(&mut_printf);
			continue;
		}
		
		// if sock_pair has both sen and rec
		if(sock_pair->sock_sen_fd != -1 && sock_pair->sock_rec_fd != -1){
			sock_pair->id = sock_id;
			sbuf_insert(&sbuf_4_kine_sp,(int64_t)sock_pair);
			pthread_create(&tid,NULL,kine_thread,NULL);
		}

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
	uint32_t flighter_id;
	uint64_t v;
	ccr_ct * cct_sync_clients_pt;
	int client_clock;
	int * room_clock_pt;
	ccr_rw_map * cmap_fid2desct_pt;
	// for for loop only
	int i;

	// anything begin with net means this parameter is received from net
	int32_t net_pitch_op;
	int32_t net_roll_op;
	int32_t net_dir_op;
	int32_t net_acc_op;
	int32_t net_lw_op;
	int32_t net_destroyed_flighter_n;
	int32_t net_destroyed_weapon_n;
	uint32_t net_destroyed_flighter_id;
	uint32_t timestamp;
	
	int full;
	int flags;	
	
	char * init_status_buffer;
	
	net_flighter_op net_f_o;
	net_destroyed_flighter net_d_f;
	net_destroyed_weapon net_d_w;
	int ct;
	// TODO: appropriate init_status needed
	//net_match_status init_status;
#ifdef SINGLE_ROOM_DEBUG
	struct timeval srd_tv;
	long long srd_start,srd_current;
	double srd_secs;
#endif
	// ct is used to avoid timeout when client is playing cg
	ct = 0;	

	init_status_buffer = (char *)malloc(N_M_SIZE+2*N_F_SIZE);	
	
	net_match_status * init_status = (net_match_status *)init_status_buffer;

	net_flighter_status * net_f_s1 = (net_flighter_status *)(init_status_buffer+N_M_SIZE), * net_f_s2 = (net_flighter_status *)(init_status_buffer+N_M_SIZE+N_F_SIZE);
	(init_status)->timestamp = 1;
	(init_status)->steplength = 1;
	init_status->flighters_n = 2;
	init_status->weapons_n = 0;
	init_status->winner_group = 0;
	//sprintf(net_f_s1.user_id,"1");
	//sprintf(net_f_s2.user_id,"0");
	net_f_s1->user_id = 1;
	net_f_s2->user_id = 0;
	net_f_s1->x = net_f_s1->y = net_f_s1->z = 1;
	net_f_s2->x = net_f_s2->y = net_f_s2->z = -1;

	//char * init_status = "1 1\n2\n1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n0\n";
	//char * init_status = "1 1\n1\n1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n0\n";

	pthread_mutex_t * mut_room_pt;
	pthread_cond_t * cond_room_pt;
	pthread_mutex_t * mut_clients_pt;
	pthread_cond_t * cond_clients_pt;

	char * buf_pt;

	pthread_detach(pthread_self());
	
	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_clients);
	
	// set fd to be nonblock
	//flags = fcntl(connfd,F_GETFL,0);
	//fcntl(connfd,F_SETFL,flags | O_NONBLOCK);
	
	fcntl(connfd,F_SETFL,O_NONBLOCK);	

	rio_readinitb(&rio,connfd);
	
	// first time connection client tells this server its id
	n = rio_readnb(&rio,&net_f_o,sizeof(net_flighter_op),0);
	// printf("******* return val: %d *************\n",n);
	
	//printf("only %d bytes received\n",n);
	REC_BYTES_CHECK(n,sizeof(net_flighter_op),"****** E R R O R: timeout in first-time connecting to client ******\n");
	
	client_id = net_f_o.user_id;
	
	memset(buf,0,MAXLINE);
	//printf("[CLIENT_THREAD id %d] first time message: %s[END]\n",c_i_pt->id,buf);
	//memcpy(buf,&init_status,N_M_SIZE);
	//memcpy(buf+N_M_SIZE,&net_f_s1,N_F_SIZE);
	//memcpy(buf+N_M_SIZE+N_F_SIZE,&net_f_s2,N_F_SIZE);
	
	//rio_writen(connfd,init_status_buffer,N_M_SIZE+2*N_F_SIZE);
	//rio_writen(connfd,(char *)&net_f_s1,sizeof(net_flighter_status));
	//rio_writen(connfd,(char *)&net_f_s2,sizeof(net_flighter_status));

	// not 0 means room_thread is not ready yet
	while(ccr_rw_map_query(&cmap_cid2cinfo,client_id,&v) != 0);

	c_i_pt = (client_info *)v;

	// there are other client_threads that are trying to cover the same client_id 
	while(c_i_pt->threads != 0);
	c_i_pt->threads = 1;

	while(c_i_pt->overall_status == NULL);
	
	rio_writen(connfd,c_i_pt->overall_status,c_i_pt->os_size);

	pthread_mutex_lock(&mut_printf);
	printf("[CLIENT_THREAD id %d]client successfully connected\n",c_i_pt->id);
	pthread_mutex_unlock(&mut_printf);

	// get some info from c_i_pt into local variables
	f_s_pt = &(c_i_pt->fos->s);
	
	// XXX
	// f_s_pt->tic = 0;
	
	f_o_pt = &(c_i_pt->fos->op);
	cct_sync_clients_pt = c_i_pt->cct_sync_clients;
	client_clock = f_s_pt->tic;
	room_clock_pt = c_i_pt->room_clock_p;
	mut_room_pt = c_i_pt->mut_room_pt;
	cond_room_pt = c_i_pt->cond_room_pt;
	mut_clients_pt = c_i_pt->mut_clients_pt;
	cond_clients_pt = c_i_pt->cond_clients_pt;
	cmap_fid2desct_pt = c_i_pt->cmap_fid2desct;

	pthread_mutex_lock(&mut_printf);
	printf("[CLIENT_THREAD id %d]local variables initialized successfully\n",c_i_pt->id);
	pthread_mutex_unlock(&mut_printf);

	while(1){
		if(client_clock == *room_clock_pt){
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_start = TV_TO_MSEC(srd_tv);
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:CLIENT CLOCK JUST MOVED### CURRENT MSEC:%lld\n\n",client_id,srd_start);
		pthread_mutex_unlock(&mut_printf);
#endif
	
			printf("[CLIENT_THREAD id %d] ready to read op from client\n",c_i_pt->id);
			// if client is playing cg wait 2 min maximum
			if((ct++) == 0)
				n = rio_readnb(&rio,&net_f_o,sizeof(net_flighter_op),120*1000);
			else
				n = rio_readnb(&rio,&net_f_o,sizeof(net_flighter_op),0);	
				

			if(n < sizeof(net_flighter_op)){
				pthread_mutex_lock(&mut_printf);
				printf("[CLIENT_THREAD id %d] ****** E R R O R: timeout in reading op from client, only received %d bytes, this thread will exit ******\n",client_id,n);
				pthread_mutex_unlock(&mut_printf);
			
				c_i_pt->threads--;
				pthread_exit(NULL);
			}

			//printf("res : %d\n",n);
			//REC_BYTES_CHECK(n,sizeof(net_flighter_op),"****** E R R O R: timeout in reading op from client ******\n");
			if(n != 0){
				// operation
				pthread_mutex_lock(&mut_printf);
				printf("[CLIENT_THREAD id %d]content read in op of clock %d:%s[END]\n",c_i_pt->id,client_clock,buf);
				pthread_mutex_unlock(&mut_printf);
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_start = TV_TO_MSEC(srd_tv);
		srd_secs = (srd_current - srd_start)/1000.0;
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:CLIENT OP READ###\n\n",client_id,srd_secs);
		pthread_mutex_unlock(&mut_printf);
#endif

				buf_pt = buf;
				f_o_pt->user_id = client_id;
				f_o_pt->tic = client_clock;
				//client_id = atoi(net_f_o.user_id);
				timestamp = net_f_o.timestamp;
				f_o_pt->pitch = net_pitch_op = net_f_o.op_pitch;
				f_o_pt->roll = net_roll_op = net_f_o.op_roll;
				f_o_pt->direction = net_dir_op = net_f_o.op_dir;
				f_o_pt->acceleration = net_acc_op = net_f_o.op_acc;
				f_o_pt->launch_weapon = net_lw_op = net_f_o.launch_weapon;
				net_destroyed_flighter_n = net_f_o.detected_destroyed_flighters;
				net_destroyed_weapon_n = net_f_o.detected_destroyed_weapons;

				pthread_mutex_lock(&mut_printf);
				printf("[CLIENT_THREAD id %d]client thread has processed op %d %d %d %d %d\n",c_i_pt->id,net_pitch_op,net_roll_op,net_dir_op,net_acc_op,net_lw_op);
				pthread_mutex_unlock(&mut_printf);


				// destroyed flighters
				for(i = 0; i < net_destroyed_flighter_n;i++){
					n = rio_readnb(&rio,&net_d_f,sizeof(net_destroyed_flighter),0);					
					if(n < sizeof(net_destroyed_flighter)){
						pthread_mutex_lock(&mut_printf);
						printf("[CLIENT_THREAD id %d] ****** E R R O R: timeout in reading detected destroyed flighter from client, only received %d bytes, this thread will exit ******\n",client_id,n);
						pthread_mutex_unlock(&mut_printf);
			
						c_i_pt->threads--;
						pthread_exit(NULL);
					}
					//REC_BYTES_CHECK(n,sizeof(net_destroyed_flighter),"****** E R R O R: timeout in reading detected destroyed flighter from client ******\n");
					net_destroyed_flighter_id = net_d_f.id;
					ccr_rw_map_query(cmap_fid2desct_pt,net_destroyed_flighter_id,&v);
					ccr_rw_map_insert(cmap_fid2desct_pt,net_destroyed_flighter_id,v+1);
					pthread_mutex_lock(&mut_printf);
					printf("[CLIENT_THREAD id %d] flighter %d detected to be destroyed; cmap new v: %d\n",c_i_pt->id,net_destroyed_flighter_id,v+1);
					pthread_mutex_unlock(&mut_printf);
				}
				// destroyed weapons
				for(i = 0; i < net_destroyed_weapon_n;i++){
					n = rio_readnb(&rio,&net_d_w,sizeof(net_destroyed_weapon),0);					
					REC_BYTES_CHECK(n,sizeof(net_destroyed_weapon),"****** E R R O R: timeout in reading detected destroyed weapon from client ******\n");
					//TODO: deal with net_destroyed_weapon
				
				}
				//n = rio_readlineb(&rio,buf,MAXLINE);
			}
			else{
				//TODO: net problem
				continue;
			}
			
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_current = TV_TO_MSEC(srd_tv);
		srd_secs = (srd_current - srd_start)/1000.0;
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:CLIENT OP PROCESSED### TIME ELASPED:%lf\n\n",client_id,srd_secs);
		pthread_mutex_unlock(&mut_printf);
#endif
			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] has sent opeartion for clock %d\n",c_i_pt->id,client_clock);
			pthread_mutex_unlock(&mut_printf);

			// send status & op & notify simulink_server_thread that one of the clients has sent a
			// fos to it	
			full = 1;
			pthread_mutex_lock(&mut_kine);
			do{
				for(i = 0; i < MAX_KINE_N;i++){
					if(ready_client_ids[i] == 0){
						full = 0;
						ready_client_ids[i] = c_i_pt->id;
						break;
					}
				}
				if(full == 1){
					pthread_cond_wait(&cond_kine,&mut_kine);
				}
			}
			while(full == 1);
			pthread_cond_broadcast(&cond_kine);
			pthread_mutex_unlock(&mut_kine);

			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] has asked s_server to work\n",c_i_pt->id);
			pthread_mutex_unlock(&mut_printf);

			// FIXME: this is temporarily a forever loop waits for s_server to do its job
			// use cond later
			// maybe not? just spin is fine?
			while(f_s_pt->tic != client_clock+1){
				continue;
			}

#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_current = TV_TO_MSEC(srd_tv);
		srd_secs = (srd_current - srd_start)/1000.0;
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:S_SERVER WORK FINISHED### TIME ELASPED:%lf\n\n",client_id,srd_secs);
		pthread_mutex_unlock(&mut_printf);
#endif
			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] client has finished status calculation for clock %d;new postutre %d %d %d, ready to sync\n",c_i_pt->id,client_clock,f_s_pt->x,f_s_pt->y,f_s_pt->z);
			pthread_mutex_unlock(&mut_printf);
			
			
			// OK: this client is ready
			pthread_mutex_lock(mut_room_pt);
			if(ccr_ct_inc(cct_sync_clients_pt) != 0){
				//TODO:error	
			}

			// notify the sleeping room_thread: one of the clients is ready
			pthread_cond_broadcast(cond_room_pt);
			pthread_mutex_unlock(mut_room_pt);
			
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_current = TV_TO_MSEC(srd_tv);
		srd_secs = (srd_current - srd_start)/1000.0;
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:ASK TO SYNC### TIME ELASPED:%lf\n\n",client_id,srd_secs);
		pthread_mutex_unlock(&mut_printf);
#endif

			// client_clock ready to go further
			client_clock++;

			pthread_mutex_lock(mut_clients_pt);
			while(client_clock != *room_clock_pt){
				// wait for room_thread's clock to go on
				pthread_cond_wait(cond_clients_pt,mut_clients_pt);	
				// check if the room has timed out
				if(ccr_rw_map_query(&cmap_cid2cinfo,c_i_pt->id,&v) == -1){
					pthread_mutex_lock(&mut_printf);
					printf("[CLIENT_THREAD id %d] room timeout, client thread exit\n",c_i_pt->id);
					pthread_mutex_unlock(&mut_printf);

					if(ccr_ct_dec(&cct_clients) != 0){
						fprintf(stderr,"[CLIENT_THREAD id %d]ERROR:ccr_ct_dec failed\n",client_id);
					}
					pthread_exit(NULL);

				}
			}
			pthread_mutex_unlock(mut_clients_pt);

			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] sync has been accomplished; write back to client and move on\n",c_i_pt->id);
			pthread_mutex_unlock(&mut_printf);

			// TODO: send overall state of every flighter back to clients
			rio_writen(connfd,c_i_pt->overall_status,c_i_pt->os_size);
			// TODO: end of game

			//printf("[CLIENT_THREAD id %d] game end ??? winner group %d; timestamp %d;thread will exist???\n",c_i_pt->id,((net_match_status *)c_i_pt->overall_status)->winner_group,((net_match_status *)c_i_pt->overall_status)->timestamp);
		
			if(((net_match_status *)c_i_pt->overall_status)->timestamp == -1 || ((net_match_status *)c_i_pt->overall_status)->winner_group != -1){	
				pthread_mutex_lock(&mut_printf);
				printf("[CLIENT_THREAD id %d] game end ; winner group %d; timestamp %d;thread will exit\n",c_i_pt->id,((net_match_status *)c_i_pt->overall_status)->winner_group,((net_match_status *)c_i_pt->overall_status)->timestamp);
				pthread_mutex_unlock(&mut_printf);

				break;
			}
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_current = TV_TO_MSEC(srd_tv);
		srd_secs = (srd_current - srd_start)/1000.0;
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[CLIENT_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:WRITEBACK### TIME ELASPED:%lf\n\n",client_id,srd_secs);
		pthread_mutex_unlock(&mut_printf);
#endif
		}
	}



	pthread_mutex_lock(&mut_printf);
	printf("[CLIENT_THREAD id %d] end of game thread exit\n",c_i_pt->id);
	pthread_mutex_unlock(&mut_printf);

	if(ccr_ct_dec(&cct_clients) != 0){
		fprintf(stderr,"[CLIENT_THREAD id %d] ERROR: ccr_ct_dec failed\n",c_i_pt->id);
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
			fprintf(stderr,"[WAITING_FOR_CLIENTS_THREAD] ERROR in ccr_ct_query\n");
			continue;
		}	
		else{
			// create a room thread
			if(v < max_clients){
				if(ccr_ct_inc(&cct_clients) != 0){
					fprintf(stderr,"[WAITING_FOR_CLIENTS_THREAD] ERROR in ccr_ct_inc\n");
					continue;
				}
				pthread_create(&tid,NULL,client_thread,NULL);
			}
			else{
				fprintf(stderr,"[WAITING_FOR_CLIENTS_THREAD] UNABLE TO HOST MORE CLIENTS!!\n");
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
	int n;
	char buf[MAXLINE];
	char temp_buf[MAXLINE];
	char * buf_pt;
	room_info * r_i_pt;
	int i;
	int room_clock;
	flighter_status * f_s_pt;
	flighter_op * f_o_pt;
	int size;
	int alive_group_id;
	int game_should_end;
	// match_record format:
	// for every tick: [MATCH_STATUS](and following flighter_status ofc) [FLIGHTER_OPS]
	//char * match_record;
	//int mr_cursor;	
	//int mr_fd;

	// for practisse mode only
	s_server_pack * pack_pt;
	// all target_flighter id 0
	int tf_id;

	ccr_ct cct_sync_clients;
	// for query only
	int k;
	uint64_t v;
	// time related
	struct timeval tv;
	struct timespec ts;
	long long start,current;
	//int room_restart;	
	double rand_angle;

#ifdef SINGLE_ROOM_DEBUG
	struct timeval srd_tv;
	long long srd_start,srd_current;
	double srd_secs;
#endif

	net_match_status net_m_s;
	net_flighter_status net_f_s;
	net_weapon_load net_w_l;
	net_weapon_status net_w_s;	
	int cursor;
	int flags;
	//int to_ct;
	// mutex and cond for clients to wake up this room:
	// i.e. to info this room that one of the clients has been ready
	pthread_mutex_t mut_room;
	pthread_cond_t cond_room;
	// ~              for this room to wake up its clients:
	// i.e. to info all clients that synchrnization has been accomplished
	pthread_mutex_t mut_clients;
	pthread_cond_t cond_clients;
	// cmap mapping flighter_id to number of clients that judge this flight to be dead
	ccr_rw_map cmap_fid2desct;

	char ge_buf [MAXLINE];
	int ge_clientfd;
	cJSON * ge_res;
	char * ge_json_string;
	int ge_n;
	rio_t ge_rio;
	int ge_flags; 
	// ready_pack_pts full
	int full;	
	int old_tic;

	//match_record = (char *)malloc(MATCH_RECORD_MAX_SIZE);
	//mr_cursor = 0;

	ccr_rw_map_init(&cmap_fid2desct);
	
	r_i_pt = (room_info *)malloc(sizeof(room_info));
	r_i_pt->status = 1;

	pthread_mutex_init(&mut_room,NULL);
	pthread_cond_init(&cond_room,NULL);
	r_i_pt->mut = &mut_room;
	r_i_pt->cond = &cond_room;

	pthread_mutex_init(&mut_clients,NULL);
	pthread_cond_init(&cond_clients,NULL);
	
	pthread_detach(pthread_self());
	
	pack_pt = (s_server_pack *)calloc(1,sizeof(s_server_pack));
	tf_id = 0;

	// TODO:fault check
	ccr_ct_init(&cct_sync_clients);

	clientlen = sizeof(struct sockaddr_storage);
	connfd = sbuf_remove(&sbuf_for_room_server);
	rio_readinitb(&rio,connfd);
	
	// set fd to be nonblock
	flags = fcntl(connfd,F_GETFL,0);
	fcntl(connfd,F_SETFL,flags | O_NONBLOCK);

	pthread_mutex_lock(&mut_printf);
	printf("[ROOM_THREAAD] room connected\n");
	pthread_mutex_unlock(&mut_printf);

	// POST url .. etc
	n = rio_readlineb(&rio,buf,MAXLINE);
	REC_BYTES_CHECK(n,-1,"[ROOM_THREAD]************** time out in reading url msg from room server ********************\n");
	printf("%s",buf);
	// headers
	read_requesthdrs(&rio);
	//
	printf("[ROOM_THREAD]parsed all headers\n"); 
	char * res_content = "HTTP/1.0 200 OK\r\nServer: Flighter Fight Server\r\nContent-length: 2\r\nContent-type: html/text\r\n\r\nOK";
	
	//to_ct = 0;
	/*if((n = rio_readlineb(&rio,buf,MAXLINE)) > 0){
		pthread_mutex_lock(&mut_printf);
		printf("%s\n",buf);
		pthread_mutex_unlock(&mut_printf);

	}*/
	

	// init room_clock
	
	room_clock = 0;
	r_i_pt->tic = 0;
	//room_restart = 0;

	// read room info from room server
	// just basic configurations
	n = rio_readlineb(&rio,buf,MAXLINE);
	if(n != 0){
		REC_BYTES_CHECK(n,-1,"[ROOM_THREAD] ************* time out in reading room config info from room server **************\n");
		// TODO: delete output
		printf("[ROOM_THRAD] ready to receive official room content...\n");
		printf("[buf content] [%d bytes] [supposed to be %d bytes]%s\n",strlen(buf),n,buf);

		buf_pt = buf;
		// TODO: check if at any time atoi returns 0
		// TODO: check if at any time strchr returns NULL
		r_i_pt->room_id = (uint32_t)atoi(buf);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i_pt->room_size = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i_pt->simulation_steplength = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i_pt->env_id = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		//TODO: match type
		//r_i_pt->match_type = (uint32_t)atoi(buf_pt);
		//DEFINITION: 0 player vs fjj 1 practise 2 formal
		r_i_pt->match_type = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		r_i_pt->size = (uint32_t)atoi(buf_pt);
		buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
		// match id : for file system 
		r_i_pt->match_id = (uint32_t)atoi(buf_pt);
		r_i_pt->clients = (client_info *)malloc(r_i_pt->size*sizeof(client_info));
		r_i_pt->cmap_fid2desct = &cmap_fid2desct;
		// fid 0: target flighter 
		ccr_rw_map_insert(&cmap_fid2desct,0,0);

		// room duplicity check
		while(ccr_rw_map_query(&cmap_rid2rinfo,r_i_pt->room_id,&v) != -1){
			//printf("room not deleted!!!\n");
			//XXX: this room_thread cannot go on because last room of this id has not been deleted
		}
		//printf("FUCK!!!\n");

		for(i = 0; i < r_i_pt->size; i++){
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				// TODO: delete output
				printf("[client %d info:]%s",i,buf);
				buf_pt = buf;
				
				(*(r_i_pt->clients+i)).room_id = r_i_pt->room_id;

				(*(r_i_pt->clients+i)).id = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i_pt->clients+i)).group_id = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				strncpy((*(r_i_pt->clients+i)).host,buf_pt,20);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				strncpy((*(r_i_pt->clients+i)).port,buf_pt,8);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i_pt->clients+i)).sign = (uint32_t)atoi(buf_pt);
				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				(*(r_i_pt->clients+i)).flighter_id = (uint32_t)atoi(buf_pt);
				//use user_id as fligheter_id
				(*(r_i_pt->clients+i)).flighter_id = (*(r_i_pt->clients+i)).id;
;

				buf_pt = MOVE_AHEAD_IN_BUF(buf_pt);
				
				// TODO: delete this
				//(*(r_i_pt->clients+i)).flighter_type = (uint32_t)atoi(buf_pt);
				(*(r_i_pt->clients+i)).flighter_type = 1;			

				(*(r_i_pt->clients+i)).fos = (flighter_op_and_status *)malloc(sizeof(flighter_op_and_status));
				// XXX:rules: only when all clients' op tic is x
				// can status move to x (x - 1 before)
				(*(r_i_pt->clients+i)).fos->op.tic = 0;
				(*(r_i_pt->clients+i)).fos->s.tic = 0;
				(*(r_i_pt->clients+i)).fos->s.alive = 1;
				(*(r_i_pt->clients+i)).fos->s.flighter_id = (*(r_i_pt->clients+i)).flighter_id;
				(*(r_i_pt->clients+i)).fos->s.user_id = (*(r_i_pt->clients+i)).id;
				(*(r_i_pt->clients+i)).fos->s.group_id = (*(r_i_pt->clients+i)).group_id;
				// sync purpose
				(*(r_i_pt->clients+i)).cct_sync_clients = &cct_sync_clients;
				// clock
				(*(r_i_pt->clients+i)).room_clock_p = &room_clock;
			
				(*(r_i_pt->clients+i)).mut_room_pt = &mut_room;
				(*(r_i_pt->clients+i)).cond_room_pt = &cond_room;
				//
				(*(r_i_pt->clients+i)).mut_clients_pt = &mut_clients;
				(*(r_i_pt->clients+i)).cond_clients_pt = &cond_clients;
				
				// overallstatus
				(*(r_i_pt->clients+i)).overall_status = NULL;
				// init: 0
				(*(r_i_pt->clients+i)).cmap_fid2desct = &cmap_fid2desct;
				// init : 0
				(*(r_i_pt->clients+i)).threads = 0;
				// steps
				(*(r_i_pt->clients+i)).current_steps = r_i_pt->simulation_steplength;
				
				ccr_rw_map_insert(&cmap_fid2desct,(*(r_i_pt->clients+i)).flighter_id,0);
			}
			else{
				//TODO: Network problem
			}
			// map client id to room id
			ccr_rw_map_insert(&cmap_cid2cinfo,(*(r_i_pt->clients+i)).id,(uint64_t)(r_i_pt->clients+i));
		}
		
	}
	else{
		// TODO: Network problem
	}
	ccr_rw_map_insert(&cmap_rid2rinfo,r_i_pt->room_id,(uint64_t)r_i_pt);

	rio_writen(connfd,res_content,strlen(res_content));
	close(connfd);


	//printf("LDB!\n");
	// after configuration for room set init status
	net_m_s.timestamp = room_clock;
	net_m_s.steplength = r_i_pt->simulation_steplength;
	net_m_s.flighters_n = (r_i_pt->match_type == 0?r_i_pt->size+1:r_i_pt->size);
	// TODO: weapons_n should be really dealt with!
	net_m_s.weapons_n = 0;
	net_m_s.winner_group = -1;
	memcpy(buf,(char *)&net_m_s,sizeof(net_match_status));
	cursor = sizeof(net_match_status);	
	//printf("HS!\n");
	// TODO: loaded_weapon_types
	if(r_i_pt->match_type == 0){
		assert(r_i_pt->size == 1);
		net_f_s.user_id = tf_id;
		pack_pt->p.x = -163.8*1000;
		pack_pt->p.y = -161.379*1000;
		pack_pt->p.z = 31.647*1000;
		pack_pt->p.u = 0;
		pack_pt->p.v = 0;
		pack_pt->p.w = -180*1000;
		memcpy((char *)&(net_f_s.x),(char *)&(pack_pt->p.x),sizeof(int32_t)*12);
		net_f_s.loaded_weapon_types = 0;
		memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
		cursor += sizeof(net_flighter_status);
		// 1 client
		f_s_pt = &((*(r_i_pt->clients)).fos->s);
		f_s_pt->x = 163.8*1000;
		f_s_pt->y = 161.379*1000;
		f_s_pt->z = 31.647*1000;
		f_s_pt->u = 0;
		f_s_pt->v = 0;
		f_s_pt->w = 0;
		net_f_s.user_id = f_s_pt->user_id;
		memcpy((char *)&(net_f_s.x),(char *)&(f_s_pt->x),sizeof(int32_t)*12);
		net_f_s.loaded_weapon_types = 0;		
		memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
		cursor += sizeof(net_flighter_status);
	}
	else{
		assert(r_i_pt->size == 2);
		int smaller = 1;
		if((*(r_i_pt->clients)).id > (*(r_i_pt->clients+1)).id){
			smaller = -1;
		}
		for( i = 0; i < r_i_pt->size;i++){
			f_s_pt = &((*(r_i_pt->clients+i)).fos->s);
			f_s_pt->x = -1*smaller*163.8*1000;
			f_s_pt->y = -1*smaller*161.379*1000;
			f_s_pt->z = 31.647*1000;
			f_s_pt->u = 0;
			f_s_pt->v = 0;
			f_s_pt->w = smaller == 1? -3.14159*1000 : 0;		
			net_f_s.user_id = f_s_pt->user_id;
			memcpy((char *)&(net_f_s.x),(char *)&(f_s_pt->x),sizeof(int32_t)*12);
			net_f_s.loaded_weapon_types = 0;		
			memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
			cursor += sizeof(net_flighter_status);

			smaller = smaller*(-1);
		}
	}
	// TODO: set start position properly & give it to each client
	for(i = 0; i < r_i_pt->size;i++){
		(*(r_i_pt->clients+i)).os_size = cursor;
		(*(r_i_pt->clients+i)).overall_status = buf;
		//printf("SFL!\n");
	}

	// init status recorded in match_record
	//memcpy(match_record,buf,cursor);
	
	pthread_mutex_lock(&mut_printf);
	printf("[ROOM_THREAAD id %d] room configuration completed, begin real work\n",r_i_pt->room_id);
	pthread_mutex_unlock(&mut_printf);
	// TODO: sync & calc & ... whatever needs be done by a room
	// 	 use a ccr_counter to let room_thread know when all client_thread s are ready
	
	while(1){
		//TODO:delete later
		//sleep(1);	
		gettimeofday(&tv,NULL);
		start = TV_TO_MSEC(tv);
		k = -1;
		pthread_mutex_lock(&mut_room);
		while(k != r_i_pt->size){
			gettimeofday(&tv,NULL);
			current = TV_TO_MSEC(tv);
			if(current - start > (room_clock == 0 ? (2.5*ROOM_MAX_WAITING_MSEC) :ROOM_MAX_WAITING_MSEC)){
				// room waiting timeout: delete room info from cmap_rid2rinfo
				// delete corresponding client info from cmap_cid2cinfo
				// delete this room thread
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAD id %d] room timeout; thread exit\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);
				// tell room server this room end
				ge_clientfd = open_clientfd(GAME_END_HOST,GAME_END_PORT);
	        		if(ge_clientfd < 0)
        	        		printf("[ROOM_THREAD %d]net error when connecting to endgame part\n",r_i_pt->room_id);
        			ge_res = NULL;
        			ge_res = cJSON_CreateObject();
	        		cJSON_AddNumberToObject(ge_res,"id",r_i_pt->room_id);
        			cJSON_AddNumberToObject(ge_res,"gameId",r_i_pt->match_id);
        			cJSON_AddNumberToObject(ge_res,"winCampId",-1);
				cJSON_AddNumberToObject(ge_res,"forceEnd",1);
				ge_json_string = cJSON_Print(ge_res);
	       			sprintf(ge_buf,GAME_END_STRING_PATTERN,strlen(ge_json_string),ge_json_string);
        			printf("[ROOM_THREAD %d] GAME END write content : %s\n",r_i_pt->room_id,ge_buf);
        			rio_writen(ge_clientfd,ge_buf,strlen(ge_buf));
        			ge_n = 0;
        	       		rio_readinitb(&ge_rio,ge_clientfd);

    	    			ge_flags = fcntl(ge_clientfd,F_GETFL,0);
        			fcntl(ge_clientfd,F_SETFL,ge_flags | O_NONBLOCK);

       		 		if((ge_n = rio_readnb(&ge_rio,ge_buf,MAXLINE,1000) > 0)){
                			printf("[ROOM_THREAD %d]GAME END ROOM SERVER res:%s",r_i_pt->room_id,ge_buf);
        			}



				delete_room_from_cmap(&cmap_rid2rinfo,&cmap_cid2cinfo,r_i_pt);
				// wake up clients
				pthread_mutex_lock(&mut_clients);
				pthread_cond_broadcast(&cond_clients);
				pthread_mutex_unlock(&mut_clients);
				ccr_ct_reset(&cct_sync_clients);
				

				if(ccr_ct_dec(&cct_rooms) != 0){
					fprintf(stderr,"[ROOM_THREAD id %d] ERROR in ccr_ct_dec\n",r_i_pt->room_id);
				}
					
				//assert(ccr_rw_map_query(&cmap_rid2rinfo,r_i_pt->room_id,&v) == -1);
				//assert(0);
				pthread_exit(NULL);

				//room_restart = 1;
				//to_ct++;
				break;
			}	

			ts.tv_sec = tv.tv_sec+1;
			ts.tv_nsec = tv.tv_usec*1000;
			pthread_cond_timedwait(&cond_room,&mut_room,&ts);
			ccr_ct_query(&cct_sync_clients,&k);
		}
		pthread_mutex_unlock(&mut_room);
	
		/*if(room_restart == 1){
			room_clock = 0;
			if(to_ct == 1){
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] room waiting clients timeout; restart room process\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);
				to_ct++;
			}
			room_restart = 0;
			continue;		
		}*/		

		//to_ct = 0;
		pthread_mutex_lock(&mut_printf);
		printf("[ROOM_THREAAD id %d] room sync accomplished clock %d\n",r_i_pt->room_id,room_clock);
		pthread_mutex_unlock(&mut_printf);
		
#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_start = TV_TO_MSEC(srd_tv);
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[ROOM_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:SYNC ACCOMPLISHED### CURRENT MSEC:%lld\n\n",r_i_pt->room_id,srd_start);
		pthread_mutex_unlock(&mut_printf);
#endif

		//sleep(1);
		// when a room_thread is asking tf_thread to work
		// it sets tf_ready_signal to 0
		// and when tf_thread finishes working
		// it sets tf_ready_siganl to 1
		if(r_i_pt->match_type != 1){
			if(TARGET_FLIGHTER_WORK){
				full = 1;
				pthread_mutex_lock(&mut_drone);
				do{
					for(i = 0; i < MAX_DRONE_N;i++){
						if(ready_pack_pts[MATCH_TYPE_2_POOL_INDEX(r_i_pt->match_type)][i] == NULL){
							full = 0;
							pack_pt->o.steps = r_i_pt->simulation_steplength; 
							pack_pt->p.tic = room_clock;
							ready_pack_pts[MATCH_TYPE_2_POOL_INDEX(r_i_pt->match_type)][i] = pack_pt;
							break;
						}
					}
					if(full == 1){
						pthread_cond_wait(&cond_drone,&mut_drone);
					}
				}
				while(full == 1);
				pthread_cond_broadcast(&cond_drone);
				pthread_mutex_unlock(&mut_drone);

				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] asking target flighter server to work\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);
				// waits for tf_thread to complete
				while(pack_pt->p.tic != room_clock+1){
					continue;
				}
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] target flighter server work completed;new posture:%d %d %d\n",r_i_pt->room_id,pack_pt->p.x,
					pack_pt->p.y,pack_pt->p.z);
				pthread_mutex_unlock(&mut_printf);
			}

		}


#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_start = TV_TO_MSEC(srd_tv);
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[ROOM_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:TF_CALC ACCOMPLISHED### CURRENT MSEC:%lld\n\n",r_i_pt->room_id,srd_start);
		pthread_mutex_unlock(&mut_printf);
#endif

		// TODO: deal with target_flighter getting destroyed
		memset(buf,MAXLINE,0);
		// 1. decide which flighters really are destroyed
		for(i = 0; i < r_i_pt->size; i++){
			ccr_rw_map_query(&cmap_fid2desct,(*(r_i_pt->clients+i)).fos->s.flighter_id,&v);
			pthread_mutex_lock(&mut_printf);
			printf("[ROOM_THREAD id %d] fid:%d destroy count:%d\n",r_i_pt->room_id, (*(r_i_pt->clients+i)).fos->s.flighter_id,v);
			pthread_mutex_unlock(&mut_printf);
			if(v > r_i_pt->size/2){
				(*(r_i_pt->clients+i)).fos->s.alive = 0;
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAD id %d] flighter %d has been destroyed\n",r_i_pt->room_id,(*(r_i_pt->clients+i)).flighter_id);
				pthread_mutex_unlock(&mut_printf);
			}
		}
		// 2. decide if game ends
		alive_group_id = -1;
		game_should_end = 1;
		if(r_i_pt->match_type == 0){
			ccr_rw_map_query(&cmap_fid2desct,0,&v);
			if(v > r_i_pt->size /2){
				game_should_end = 1;
				// if both dead alive_group_id -1
				// else if only drone dead alive_group_id depends on user's group_id
				alive_group_id = (*(r_i_pt->clients)).fos->s.alive== 1 ? (*(r_i_pt->clients)).fos->s.group_id: -1;
			}
			else if((*(r_i_pt->clients)).fos->s.alive== 0){
				// drone alive however client's flighter died
				game_should_end = 1;
				alive_group_id = 0;
			}
			else{
				game_should_end = 0;
			}

		}
		else{
			for(i = 0; i < r_i_pt->size; i++){
				if((*(r_i_pt->clients+i)).fos->s.alive == 1){
					if(alive_group_id == -1){
						alive_group_id = (*(r_i_pt->clients+i)).fos->s.group_id;
					}
					else{
						if(alive_group_id != (*(r_i_pt->clients+i)).fos->s.group_id){
							game_should_end = 0;
						}
					}
				}
			}
		}
		// record flighter_op of this tic
		//for(i = 0; i < r_i_pt->size; i++){
		//	f_o_pt = &((*(r_i_pt->clients+i)).fos->op);
		//	memcpy(match_record+mr_cursor,(char *)&f_o_pt,sizeof(flighter_op));
		//	mr_cursor += sizeof(flighter_op);
		//}

		


		if(game_should_end == 1){
			net_m_s.timestamp = -1;
			net_m_s.steplength = 0;
			net_m_s.flighters_n = 0;
			net_m_s.weapons_n = 0;
			net_m_s.winner_group = alive_group_id;
			memcpy(buf,(char *)&net_m_s,sizeof(net_match_status));	
			for(i = 0; i < r_i_pt->size; i++){
				(r_i_pt->clients+i)->overall_status = buf;
				(r_i_pt->clients+i)->os_size = sizeof(net_match_status);
			}
			// record
			//memcpy(match_record+mr_cursor,buf,sizeof(net_match_status));
			//mr_cursor += sizeof(net_match_status);				
			// TODO: write to local fS	
			//sprintf(temp_buf,"./match_records/match_type%d_id%d.rec",r_i_pt->match_type,r_i_pt->match_id);
			//mr_fd = open(temp_buf,O_WRONLY|O_CREAT,0644);
			//rio_writen(mr_fd,match_record,mr_cursor);
			//close(mr_fd);

			pthread_mutex_lock(&mut_printf);
			printf("[ROOM_THREAAD id %d] [GAME id %d]room status of clock %d:\n #### Group %d has won! Game over ####\n",r_i_pt->room_id,r_i_pt->match_id,((net_match_status *)buf)->timestamp,((net_match_status *)buf)->winner_group);
			pthread_mutex_unlock(&mut_printf);
			
			// wait clients
			sleep(1);	

        		ge_clientfd = open_clientfd(GAME_END_HOST,GAME_END_PORT);
        		if(ge_clientfd < 0)
                		printf("[ROOM_THREAD %d]net error when connecting to endgame part\n",r_i_pt->room_id);
        		ge_res = NULL;
        		ge_res = cJSON_CreateObject();
        		cJSON_AddNumberToObject(ge_res,"id",r_i_pt->room_id);
        		cJSON_AddNumberToObject(ge_res,"gameId",r_i_pt->match_id);
        		cJSON_AddNumberToObject(ge_res,"winCampId",alive_group_id);
			cJSON_AddNumberToObject(ge_res,"forceEnd",0);
			ge_json_string = cJSON_Print(ge_res);
       			sprintf(ge_buf,GAME_END_STRING_PATTERN,strlen(ge_json_string),ge_json_string);
        		printf("[ROOM_THREAD %d] GAME END write content : %s\n",r_i_pt->room_id,ge_buf);
        		rio_writen(ge_clientfd,ge_buf,strlen(ge_buf));
        		ge_n = 0;
        	        rio_readinitb(&ge_rio,ge_clientfd);

        		ge_flags = fcntl(ge_clientfd,F_GETFL,0);
        		fcntl(ge_clientfd,F_SETFL,ge_flags | O_NONBLOCK);

       		 	if((ge_n = rio_readnb(&ge_rio,ge_buf,MAXLINE,1000) > 0)){
                		printf("[ROOM_THREAD %d]GAME END ROOM SERVER res:%s",r_i_pt->room_id,ge_buf);
        		}
	
			

			// when room_clock moves on notify all clients to move on
			pthread_mutex_lock(&mut_clients);
			room_clock++;
			pthread_cond_broadcast(&cond_clients);
			pthread_mutex_unlock(&mut_clients);
	
			sleep(1);
			ccr_ct_reset(&cct_sync_clients);
			
			if(ccr_ct_dec(&cct_rooms) != 0){
				fprintf(stderr,"[ROOM_THREAD %d]ERROR in ccr_ct_dec\n",r_i_pt->room_id);
			}
			delete_room_from_cmap(&cmap_rid2rinfo,&cmap_cid2cinfo,r_i_pt);
			//assert(ccr_rw_map_query(&cmap_rid2rinfo,r_i_pt->room_id,&v) == -1);
			//assert(0);
			pthread_exit(NULL);
		}
		cursor = 0;
		net_m_s.timestamp = room_clock;
		net_m_s.steplength = r_i_pt->simulation_steplength;
		net_m_s.flighters_n = (r_i_pt->match_type == 0?r_i_pt->size+1:r_i_pt->size);
		// TODO: weapons_n should be really dealt with!
		net_m_s.weapons_n = 0;
		net_m_s.winner_group = -1;
		memcpy(buf,(char *)&net_m_s,sizeof(net_match_status));
		cursor = sizeof(net_match_status);	
		// TODO: loaded_weapon_types
		for(i = 0; i < r_i_pt->size; i++){
			f_s_pt = &((*(r_i_pt->clients+i)).fos->s);
			net_f_s.user_id = f_s_pt->user_id;
			memcpy((char *)&(net_f_s.x),(char *)&(f_s_pt->x),sizeof(int32_t)*12);
			net_f_s.loaded_weapon_types = 0;		
			
			memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
			cursor += sizeof(net_flighter_status);
		}
		// practise mode: add target_flighter
		if(r_i_pt->match_type == 0){
			net_f_s.user_id = tf_id;
			memcpy((char *)&(net_f_s.x),(char *)&(pack_pt->p.x),sizeof(int32_t)*12);
			net_f_s.loaded_weapon_types = 0;
		
			memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
			cursor += sizeof(net_flighter_status);

		}

		// TODO: weapon status
		for(i = 0; i < net_m_s.weapons_n;i++){

		}

		//record
		//memcpy(match_record+mr_cursor,buf,cursor);
		//mr_cursor+=cursor;
		// TODO: write to FS

		for(i = 0; i < r_i_pt->size; i++){
			(r_i_pt->clients+i)->overall_status = buf;
			(r_i_pt->clients+i)->os_size = cursor;
		}

		pthread_mutex_lock(&mut_printf);
		printf("[ROOM_THREAAD id %d] room status of clock %d:\n%s\n",r_i_pt->room_id,room_clock,buf);
		pthread_mutex_unlock(&mut_printf);


		while(1){
			if(r_i_pt->status == 1){
				break;
			}
			else{
			}
		}
		// when room_clock moves on notify all clients to move on
		pthread_mutex_lock(&mut_clients);
		room_clock++;
		r_i_pt->tic = room_clock;
		pthread_cond_broadcast(&cond_clients);
		pthread_mutex_unlock(&mut_clients);

#ifdef SINGLE_ROOM_DEBUG
		gettimeofday(&srd_tv,NULL);
		srd_start = TV_TO_MSEC(srd_tv);
		pthread_mutex_lock(&mut_printf);
		printf("\n\n[ROOM_THREAD id %d] ###SINGLE_ROOM_DEBUG### ###STATUS:CLIENTS ARE ASKED TO WRITE BACK MATCH_STATUS### CURRENT MSEC:%lld\n\n",r_i_pt->room_id,srd_start);
		pthread_mutex_unlock(&mut_printf);
#endif

	
		ccr_ct_reset(&cct_sync_clients);
	
	}


	if(ccr_ct_dec(&cct_rooms) != 0){
		fprintf(stderr,"[ROOM_THREAD id %d] ERROR in ccr_ct_dec\n",r_i_pt->room_id);
	}
	pthread_exit(NULL);
	return NULL;

}
// main thread AKA waiting for room info thread
int main(int argc,char * argv[]){
	signal(SIGPIPE,SIG_IGN);
	int roomserver_listenfd,connfd;
	int i,j,v;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t tid;
	if(argc != 9){
		fprintf(stderr,"usage: %s <port_for_roomserver> <port_for_clients> <port_for_director> <port_for_tf> <port_for_model_server> <port_for_ui_web_page> <max_rooms> <max_clients>\n",argv[0]);
		exit(1);
	}
	roomserver_listenfd = open_listenfd(argv[1]);
	clients_listenfd = open_listenfd(argv[2]);
	director_listenfd = open_listenfd(argv[3]);
	tf_listenfd = open_listenfd(argv[4]);
	ui_listenfd = open_listenfd(argv[6]);
	if(S_SERVER_WORK){
		sserver_listenfd = open_listenfd(argv[5]);
		if(sserver_listenfd < 0){
			fprintf(stderr,"net error\n");
			return -1;
		}
	}

	if(roomserver_listenfd < 0 || clients_listenfd < 0 || director_listenfd < 0 || tf_listenfd < 0 || ui_listenfd < 0){
		fprintf(stderr,"net error\n");
		return -1;
	}
	srand(time(NULL));

	max_rooms = atoi(argv[7]);
	max_clients = atoi(argv[8]);
// In SINGLE_ROOM_DEBUG mode only one room can exist in order to avoid trouble
#ifdef SINGLE_ROOM_DEBUG
	max_rooms = 1;
#endif
	sbuf_init(&sbuf_for_room_server,max_rooms);
	sbuf_init(&sbuf_for_clients,max_clients);
	sbuf_init(&sbuf_4_drone_sp,MAX_DRONE_N);
	sbuf_init(&sbuf_4_kine_sp,MAX_KINE_N);	
		
	ccr_rw_map_init(&cmap_cid2cinfo);
	ccr_rw_map_init(&cmap_rid2rinfo);
	ccr_rw_map_init(&cmap_sid2sp_drone);
	ccr_rw_map_init(&cmap_sid2sp_kine);

	ccr_ct_init(&cct_rooms);
	ccr_ct_init(&cct_clients);

	// mutex & cond for s_server
	pthread_mutex_init(&mut_kine,NULL);
	pthread_cond_init(&cond_kine,NULL);
	//ready_client_id = 0;
	
	// mutex & cond for controller
	pthread_mutex_init(&mut_con,NULL);
	pthread_cond_init(&cond_con,NULL);
	// mutex & cond for target flighter server
	pthread_mutex_init(&mut_drone,NULL);
	pthread_cond_init(&cond_drone,NULL);
	
	for(j = 0; j < MAX_DRONE_TAG;j++){
		for(i = 0; i < MAX_DRONE_N;i++){
			ready_pack_pts[j][i] = NULL;
		}
	}
	for(i = 0; i < MAX_KINE_N;i++){
		ready_client_ids[i] = 0;
	}
	//tf_ready_signal = 1;
	// stdout
	pthread_mutex_init(&mut_printf,NULL);

	// room_thread counter and client_thread counter

	// the thread that waits for incoming clients
	// this thread is responsible of assigning a client to its specific room
	pthread_create(&tid,NULL,waiting_for_clients_thread,NULL);

	// the thread connects s_server
	pthread_create(&tid,NULL,waiting_kine_thread,NULL);

	// the thread connecting with director server A.K.A dao tiao tai
	pthread_create(&tid,NULL,controller_thread,NULL);
	
	pthread_create(&tid,NULL,waiting_tf_thread,NULL);

	// UI
	pthread_create(&tid,NULL,ui_thread,NULL);
	// process msgs from room server
	while(1){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = accept(roomserver_listenfd,(SA *)&clientaddr,&clientlen);
		//printf("12345 %d %d\n",connfd,max_rooms);
		sbuf_insert(&sbuf_for_room_server,connfd);
		if(ccr_ct_query(&cct_rooms,&v) != 0){
			fprintf(stderr,"[MAIN] ERROR in ccr_ct_query");
		}	
		else{
			// create a room thread
			if(v < max_rooms){
				if(ccr_ct_inc(&cct_rooms) != 0){
					fprintf(stderr,"[MAIN] ERROR in ccr_ct_inc\n");
					continue;
				}
				pthread_create(&tid,NULL,room_thread,NULL);
			}
		}
	}

	// TODO: pthread_join all threads
	return 0;
}

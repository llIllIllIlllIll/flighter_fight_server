#include "flighter_msg.h"
#include "net.h"
#include "ccr_rw_map.h"
#include "ccr_counter.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define MOVE_AHEAD_IN_BUF(p) (strchr(p,' ')+1)
#define S_SERVER_WORK 1
#define TARGET_FLIGHTER_WORK 1
// This macro is used to check return value of rio_readnb and rio_readlineb
// and when dealing with rio_readlineb n should be set to 0
#define REC_BYTES_CHECK(A,B,msg) if((A)<(B)){pthread_mutex_lock(&mut_printf);fprintf(stderr,msg);pthread_mutex_unlock(&mut_printf);pthread_exit(NULL);}
#define ROOM_MAX_WAITING_MSEC (60*1000)
#define N_M_SIZE (sizeof(net_match_status))
#define N_F_SIZE (sizeof(net_flighter_status))
#define PI 3.1415926
#define RAND_ANGLE() ((((double)(rand()%4))/4.0)*PI)
// 4MB
#define MATCH_RECORD_MAX_SIZE (1<<22)
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

int clients_listenfd;
ccr_ct cct_rooms;
ccr_ct cct_clients;

int max_rooms;
int max_clients;

int director_listenfd;
// mutex & cond for connecting_s_server_thread
// purpose: at one time only one client thread connects the connecting_s_server_thread
pthread_mutex_t mut_s_server;
pthread_cond_t cond_s_server;
// mutex & cond for controller
pthread_mutex_t mut_con;
pthread_cond_t cond_con;

// ready_client_id is used to tell connecting_s_server_thread that one of the clients' status
// need to be calculated by s_server
uint32_t ready_client_id;

// ready_pack_pt is used to tell connecting_tf_thread that one of the postures need update
s_server_pack * ready_pack_pt;
// this signal tells room_thread that a new posture is calculated 
int tf_ready_signal;

// model server
int sserver_listenfd;

// target_flighter AKA ba ji
int tf_listenfd;
// mutex& cond for flighter_server( or client?)
pthread_mutex_t mut_tf;
pthread_cond_t cond_tf;

// mutex protect STDOUT
pthread_mutex_t mut_printf;

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

void * connecting_target_flighter_thread(void * vargp){
	// rio_rec for receiving info from target_flighter 
	// rio_sen for sending
	rio_t rio;
	char buf[MAXLINE];
	socklen_t clientlen;
	// same as above
	int connfd_rec,connfd_sen,connfd;
	struct sockaddr_storage clientaddr;
	//posture * tf_posture;
	//s_server_pack * tf_pack;
	socket_role * role;
	int rec_bytes;
	int flags;

	clientlen = sizeof(struct sockaddr_storage);

	//tf_posture = (posture *)malloc(sizeof(posture));
	//tf_pack = (s_server_pack *)malloc(sizeof(s_server_pack));
	role = (socket_role *)malloc(sizeof(socket_role));

	pthread_detach(pthread_self());

	connfd = accept(tf_listenfd,(SA *)&clientaddr,&clientlen);
	printf("[TF_THREAD]first socket accepted\n");
	
	// set fd to be nonblock
	flags = fcntl(connfd,F_GETFL,0);
	fcntl(connfd,F_SETFL,flags | O_NONBLOCK);

	rio_readinitb(&rio,connfd);
	rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
	REC_BYTES_CHECK(rec_bytes,sizeof(socket_role),"****** E R R O R: timeout in connecting to target flighter server ******\n");

	
	printf("first socket role connected\n");
	if(role->type == ROLE_SEND){
		connfd_sen = connfd;
		printf("[TF_THREAD] accepted sock_sen call accept\n");
		connfd = accept(tf_listenfd,(SA *)&clientaddr,&clientlen);	
		printf("[TF_THREAD] sock_rec accepted!\n");
		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);
	
		connfd_rec = connfd;
		rio_readinitb(&rio,connfd_rec);
		rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
	}
	else if(role->type == ROLE_RECV){
		connfd_rec = connfd;
		printf("[TF_THREAD] accepted sock_rec call accept\n");
		connfd_sen = accept(tf_listenfd,(SA *)&clientaddr,&clientlen);
		// set fd to be nonblock
		printf("[TF_THREAD] sock_sen accpted\n");
		flags = fcntl(connfd_sen,F_GETFL,0);
		fcntl(connfd_sen,F_SETFL,flags | O_NONBLOCK);
		rio_readinitb(&rio,connfd_sen);
		rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
	}
	else{
		printf("ERROR\n");
		pthread_mutex_lock(&mut_printf);
		fprintf(stderr,"[TF_THREAD] wrong pack\n");
		pthread_mutex_unlock(&mut_printf);
		pthread_exit(NULL);
	}
	REC_BYTES_CHECK(rec_bytes,sizeof(socket_role),"****** E R R O R: timeout in connecting to target flighter server ******\n");

	// rio for received data only 
	rio_readinitb(&rio,connfd_rec);
	

	pthread_mutex_lock(&mut_printf);
	printf("[TF_THREAD] target_flighter connected\n");
	pthread_mutex_unlock(&mut_printf);	

	while(1){
		pthread_mutex_lock(&mut_tf);
		// not NULL and 1 means OK
		while(ready_pack_pt == NULL){
			pthread_cond_wait(&cond_tf,&mut_tf);
		}
		pthread_mutex_lock(&mut_printf);
		printf("[TF_THREAD] tf_server working posture: %d %d %d %d %d %d %d %d %d %d %d %d\n",ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
				ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
				ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		pthread_mutex_unlock(&mut_printf);

		rio_writen(connfd_sen,(char *)ready_pack_pt,sizeof(s_server_pack));
		
		rec_bytes = rio_readnb(&rio,(char *)&(ready_pack_pt->p),sizeof(posture));
		REC_BYTES_CHECK(rec_bytes,sizeof(posture),"****** E R R O R: timeout in reading posture from target flighter server ******\n");

		pthread_mutex_lock(&mut_printf);
		printf("[TF_THREAD] new posture: %d %d %d %d %d %d %d %d %d %d %d %d\n",ready_pack_pt->p.x,ready_pack_pt->p.y,ready_pack_pt->p.z,
				ready_pack_pt->p.u,ready_pack_pt->p.v,ready_pack_pt->p.w,ready_pack_pt->p.vx,ready_pack_pt->p.vy,ready_pack_pt->p.vz,
				ready_pack_pt->p.vu,ready_pack_pt->p.vv,ready_pack_pt->p.vw);
		pthread_mutex_unlock(&mut_printf);
		// NULL and 0 means not OK
		ready_pack_pt = NULL;
		pthread_cond_broadcast(&cond_tf);
		pthread_mutex_unlock(&mut_tf);
	}
}


// The only worker thread that's responsible of connecting with simulink server
void * connecting_s_server_thread(void * vargp){
	rio_t rio;
	char buf[MAXLINE];
	uint64_t v;
	client_info * ready_c_i_pt;
	flighter_status * ready_f_s_pt;
	flighter_op * ready_f_o_pt;
	int n;
	int rec_bytes;
	int flags;

	pthread_detach(pthread_self());
	socklen_t clientlen;
	int connfd,connfd_rec,connfd_sen;
	struct sockaddr_storage clientaddr;

	s_server_pack * pack_pt;
	socket_role * role;

	pack_pt = (s_server_pack *)malloc(sizeof(s_server_pack));
	role = (socket_role *)malloc(sizeof(socket_role));	

	clientlen = sizeof(struct sockaddr_storage);

	if(S_SERVER_WORK){
		connfd = accept(sserver_listenfd,(SA *)&clientaddr,&clientlen);
		printf("[CONNECTING_S_SERVER_THREAD]Simulink Server connected!\n");		

		// set fd to be nonblock
		flags = fcntl(connfd,F_GETFL,0);
		fcntl(connfd,F_SETFL,flags | O_NONBLOCK);

		rio_readinitb(&rio,connfd);
		rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
		REC_BYTES_CHECK(rec_bytes,sizeof(socket_role),"****** E R R O R: timeout in connecting to s server ******\n");
		if(role->type == ROLE_SEND){
			connfd_sen = connfd;
			connfd = accept(sserver_listenfd,(SA *)&clientaddr,&clientlen);
			
			// set fd to be nonblock
			flags = fcntl(connfd,F_GETFL,0);
			fcntl(connfd,F_SETFL,flags | O_NONBLOCK);

			connfd_rec = connfd;
			rio_readinitb(&rio,connfd_rec);
			rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
		}
		else if(role->type == ROLE_RECV){
			connfd_rec = connfd;
			connfd_sen = accept(sserver_listenfd,(SA *)&clientaddr,&clientlen);
	
			// set fd to be nonblock
			flags = fcntl(connfd_sen,F_GETFL,0);
			fcntl(connfd,F_SETFL,flags | O_NONBLOCK);


			rio_readinitb(&rio,connfd_sen);
		 	rec_bytes = rio_readnb(&rio,role,sizeof(socket_role));
		}
		else{
			pthread_mutex_lock(&mut_printf);
			fprintf(stderr,"[CONNECTING_S_SERVER_THREAD] wrong pack\n");
			pthread_mutex_unlock(&mut_printf);
			pthread_exit(NULL);
		}
		REC_BYTES_CHECK(rec_bytes,sizeof(socket_role),"****** E R R O R: timeout in connecting to s server ******\n");
		rio_readinitb(&rio,connfd_rec);
	}

	pthread_mutex_lock(&mut_printf);
	printf("[CONNECTING_S_SERVER_THREAD] simulink connected\n");
	pthread_mutex_unlock(&mut_printf);


	while(1){
		pthread_mutex_lock(&mut_s_server);
		while(ready_client_id == 0){
			pthread_cond_wait(&cond_s_server,&mut_s_server);
		}
		
		pthread_mutex_lock(&mut_printf);
		printf("[CONNECTING_S_SERVER_THREAD] start working for client %d\n",ready_client_id);
		pthread_mutex_unlock(&mut_printf);

		ccr_rw_map_query(&cmap_cid2cinfo,ready_client_id,&v);
		ready_c_i_pt = (client_info *)v;
		ready_f_s_pt = &(ready_c_i_pt->fos->s);
		ready_f_o_pt = &(ready_c_i_pt->fos->op);

		
		buf[0] = '\0';
		if(S_SERVER_WORK){
			memcpy(&(pack_pt->p.x),&(ready_f_s_pt->x),sizeof(int32_t)*12);
			/*pack_pt->p.x = ready_f_s_pt->x;
			pack_pt->p.y = ready_f_s_pt->y;
			pack_pt->p.z = ready_f_s_pt->z;
			pack_pt->p.u = ready_f_s_pt->u;
			pack_pt->p.v = ready_f_s_pt->v;
			pack_pt->p.w = ready_f_s_pt->w;*/
			memcpy(&(pack_pt->o.pitch),&(ready_f_o_pt->pitch),sizeof(int32_t)*5);

			/*pack_pt->o.pitch = ready_f_o_pt->pitch;
			pack_pt->o.roll = ready_f_o_pt->roll;
			pack_pt->o.dir = ready_f_o_pt->direction;
			pack_pt->o.acc = ready_f_o_pt->acceleration;
			pack_pt->o.launch_weapon = ready_f_o_pt->launch_weapon;*/

			rio_writen(connfd_sen,(char *)pack_pt,sizeof(s_server_pack));
			pthread_mutex_lock(&mut_printf);
			printf("[CONNECTING_S_SERVER_THREAD] send: [posture]%d %d %d[op] %d %d %d %d\n",pack_pt->p.x,pack_pt->p.y,pack_pt->p.z,
				pack_pt->o.roll,pack_pt->o.pitch,pack_pt->o.dir,pack_pt->o.acc);
			pthread_mutex_unlock(&mut_printf);

			rec_bytes = rio_readnb(&rio,(char *)&(pack_pt->p),sizeof(posture));
			REC_BYTES_CHECK(rec_bytes,sizeof(posture),"****** E R R O R: timeout in reading posture from s server ******\n");
			
			pthread_mutex_lock(&mut_printf);
			printf("[CONNECTING_S_SERVER_THREAD] receive: %d %d %d\n",pack_pt->p.x,pack_pt->p.y,pack_pt->p.z);
			pthread_mutex_unlock(&mut_printf);

			//TODO: according to ready_f_s_pt and ready_f_o_pt calculate a new status
			//TODO: remember to add 1 in the tic of new status
			memcpy(&(ready_f_s_pt->x),&(pack_pt->p.x),sizeof(int32_t)*12);
		}
		else{
			ready_f_s_pt->x++;
			ready_f_s_pt->y++;
			ready_f_s_pt->z++;
		}
		ready_f_s_pt->tic++;

		ready_client_id = 0;
		pthread_cond_broadcast(&cond_s_server);

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
	
	int flags;	
	
	char * init_status_buffer;
	
	net_flighter_op net_f_o;
	net_destroyed_flighter net_d_f;
	net_destroyed_weapon net_d_w;
	// TODO: appropriate init_status needed
	//net_match_status init_status;
	
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
	n = rio_readnb(&rio,&net_f_o,sizeof(net_flighter_op));
	// printf("******* return val: %d *************\n",n);

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

	while(c_i_pt->overall_status == NULL);
	
	rio_writen(connfd,c_i_pt->overall_status,c_i_pt->os_size);

	pthread_mutex_lock(&mut_printf);
	printf("[CLIENT_THREAD id %d]client successfully connected\n",c_i_pt->id);
	pthread_mutex_unlock(&mut_printf);

	// get some info from c_i_pt into local variables
	f_s_pt = &(c_i_pt->fos->s);
	
	// XXX
	f_s_pt->tic = 0;
	
	f_o_pt = &(c_i_pt->fos->op);
	cct_sync_clients_pt = c_i_pt->cct_sync_clients;
	client_clock = 0;
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
			printf("[CLIENT_THREAD id %d] ready to read op from client\n",c_i_pt->id);
			n = rio_readnb(&rio,&net_f_o,sizeof(net_flighter_op));
			printf("res : %d\n",n);
			REC_BYTES_CHECK(n,sizeof(net_flighter_op),"****** E R R O R: timeout in reading op from client ******\n");
			if(n != 0){
				// operation
				pthread_mutex_lock(&mut_printf);
				printf("[CLIENT_THREAD id %d]content read in op of clock %d:%s[END]\n",c_i_pt->id,client_clock,buf);
				pthread_mutex_unlock(&mut_printf);

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
					n = rio_readnb(&rio,&net_d_f,sizeof(net_destroyed_flighter));					
					REC_BYTES_CHECK(n,sizeof(net_destroyed_flighter),"****** E R R O R: timeout in reading detected destroyed flighter from client ******\n");
					net_destroyed_flighter_id = net_d_f.id;
					ccr_rw_map_query(cmap_fid2desct_pt,net_destroyed_flighter_id,&v);
					ccr_rw_map_insert(cmap_fid2desct_pt,net_destroyed_flighter_id,v+1);
					pthread_mutex_lock(&mut_printf);
					printf("[CLIENT_THREAD id %d] flighter %d detected to be destroyed; cmap new v: %d\n",c_i_pt->id,net_destroyed_flighter_id,v+1);
					pthread_mutex_unlock(&mut_printf);
				}
				// destroyed weapons
				for(i = 0; i < net_destroyed_weapon_n;i++){
					n = rio_readnb(&rio,&net_d_w,sizeof(net_destroyed_weapon));					
					REC_BYTES_CHECK(n,sizeof(net_destroyed_weapon),"****** E R R O R: timeout in reading detected destroyed weapon from client ******\n");
					//TODO: deal with net_destroyed_weapon
				
				}
				n = rio_readlineb(&rio,buf,MAXLINE);
			}
			else{
				//TODO: net problem
				continue;
			}
			
			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] has sent opeartion for clock %d\n",c_i_pt->id,client_clock);
			pthread_mutex_unlock(&mut_printf);

			// send status & op & notify simulink_server_thread that one of the clients has sent a
			// fos to it
			pthread_mutex_lock(&mut_s_server);
			while(ready_client_id != 0){
				pthread_cond_wait(&cond_s_server,&mut_s_server);
			}
			ready_client_id = c_i_pt->id;
			pthread_cond_broadcast(&cond_s_server);
			pthread_mutex_unlock(&mut_s_server);

			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] has asked s_server to work\n",c_i_pt->id);
			pthread_mutex_unlock(&mut_printf);

			// FIXME: this is temporarily a forever loop waits for s_server to do its job
			// use cond later
			// maybe not? just spin is fine?
			while(f_s_pt->tic != client_clock+1){
				continue;
			}

			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] client has finished status calculation for clock %d, ready to sync\n",c_i_pt->id,client_clock);
			pthread_mutex_unlock(&mut_printf);
			
			
			// OK: this client is ready
			pthread_mutex_lock(mut_room_pt);
			if(ccr_ct_inc(cct_sync_clients_pt) != 0){
				//TODO:error	
			}
			// notify the sleeping room_thread: one of the clients is ready
			pthread_cond_broadcast(cond_room_pt);
			pthread_mutex_unlock(mut_room_pt);
			
			// client_clock ready to go further
			client_clock++;

			pthread_mutex_lock(mut_clients_pt);
			while(client_clock != *room_clock_pt){
				// wait for room_thread's clock to go on
				pthread_cond_wait(cond_clients_pt,mut_clients_pt);	
			}
			pthread_mutex_unlock(mut_clients_pt);

			pthread_mutex_lock(&mut_printf);
			printf("[CLIENT_THREAD id %d] sync has been accomplished; write back to client and move on\n",c_i_pt->id);
			pthread_mutex_unlock(&mut_printf);

			// TODO: send overall state of every flighter back to clients
			rio_writen(connfd,c_i_pt->overall_status,c_i_pt->os_size);
			// TODO: end of game

			if(((net_match_status *)c_i_pt->overall_status)->timestamp == -1){				
				break;
			}
		}
	}



	pthread_mutex_lock(&mut_printf);
	printf("[CLIENT_THREAD id %d] end of game thread exit\n",c_i_pt->id);
	pthread_mutex_unlock(&mut_printf);

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
	char * match_record;
	int mr_cursor;	
	int mr_fd;

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
	int room_restart;	
	double rand_angle;

	net_match_status net_m_s;
	net_flighter_status net_f_s;
	net_weapon_load net_w_l;
	net_weapon_status net_w_s;	
	int cursor;
	int flags;
	int to_ct;
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

	match_record = (char *)malloc(MATCH_RECORD_MAX_SIZE);
	mr_cursor = 0;

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
	
	to_ct = 0;
	/*if((n = rio_readlineb(&rio,buf,MAXLINE)) > 0){
		pthread_mutex_lock(&mut_printf);
		printf("%s\n",buf);
		pthread_mutex_unlock(&mut_printf);

	}*/
	

	// init room_clock
	room_clock = 0;
	room_restart = 0;

	// read room info from room server
	// just basic configurations
	n = rio_readlineb(&rio,buf,MAXLINE);
	while(n < 5){
		n = rio_readlineb(&rio,buf,MAXLINE);
	}
	if(1){
		REC_BYTES_CHECK(n,-1,"[ROOM_THREAD] ************* time out in reading fig info from room server **************\n");
		// TODO: delete output
		printf("[ROOM_THRAD] ready to receive official room content...\n");
		printf("[buf content] [%d bytes]%s\n",strlen(buf),buf);

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
		for(i = 0; i < r_i_pt->size; i++){
			if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
				// TODO: delete output
				printf("%s",buf);
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
	net_m_s.winner_group = 0;
	memcpy(buf,(char *)&net_m_s,sizeof(net_match_status));
	cursor = sizeof(net_match_status);	
	//printf("HS!\n");
	// TODO: loaded_weapon_types
	for(i = 0; i < r_i_pt->size; i++){
		f_s_pt = &((*(r_i_pt->clients+i)).fos->s);
		rand_angle = RAND_ANGLE();
		f_s_pt->x = (int32_t)100*1000*cos(rand_angle);
		f_s_pt->y = (int32_t)100*1000*sin(rand_angle);
		f_s_pt->z = 1000;
		net_f_s.user_id = f_s_pt->user_id;
		memcpy((char *)&(net_f_s.x),(char *)&(f_s_pt->x),sizeof(int32_t)*12);
		net_f_s.loaded_weapon_types = 0;		
		memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
		cursor += sizeof(net_flighter_status);
		//printf("DS!\n");
	}
	// practise mode: add target_flighter
	if(r_i_pt->match_type == 0){
		net_f_s.user_id = tf_id;
		rand_angle = RAND_ANGLE();
		pack_pt->p.x = (int32_t)100*1000*cos(rand_angle);
		pack_pt->p.y = (int32_t)100*1000*sin(rand_angle);
		pack_pt->p.z = 1*1000;
		memcpy((char *)&(net_f_s.x),(char *)&(pack_pt->p.x),sizeof(int32_t)*12);
		net_f_s.loaded_weapon_types = 0;
		memcpy(buf+cursor,(char *)&net_f_s,sizeof(net_flighter_status));
		cursor += sizeof(net_flighter_status);
		//printf("HC..\n");
	}
	// TODO: set start position properly & give it to each client
	for(i = 0; i < r_i_pt->size;i++){
		(*(r_i_pt->clients+i)).os_size = cursor;
		(*(r_i_pt->clients+i)).overall_status = buf;
		//printf("SFL!\n");
	}

	// init status recorded in match_record
	memcpy(match_record,buf,cursor);
	
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
			if(current - start > ROOM_MAX_WAITING_MSEC){
				room_restart = 1;
				to_ct++;
				break;
			}	

			ts.tv_sec = tv.tv_sec+1;
			ts.tv_nsec = tv.tv_usec*1000;
			pthread_cond_timedwait(&cond_room,&mut_room,&ts);
			ccr_ct_query(&cct_sync_clients,&k);
		}
		pthread_mutex_unlock(&mut_room);
	
		if(room_restart == 1){
			room_clock = 0;
			if(to_ct == 1){
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] room waiting clients timeout; restart room process\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);
				to_ct++;
			}
			room_restart = 0;
			continue;		
		}		

		to_ct = 0;
		pthread_mutex_lock(&mut_printf);
		printf("[ROOM_THREAAD id %d] room sync accomplished clock %d\n",r_i_pt->room_id,room_clock);
		pthread_mutex_unlock(&mut_printf);
		//sleep(1);
		// when a room_thread is asking tf_thread to work
		// it sets tf_ready_signal to 0
		// and when tf_thread finishes working
		// it sets tf_ready_siganl to 1
		if(r_i_pt->match_type == 0){
			if(TARGET_FLIGHTER_WORK){
				pthread_mutex_lock(&mut_tf);
				while(ready_pack_pt != NULL){
					pthread_cond_wait(&cond_tf,&mut_tf);
				}
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] asking target flighter server to work\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);

				ready_pack_pt = pack_pt;
				pthread_cond_broadcast(&cond_tf);
				pthread_mutex_unlock(&mut_tf);
				// waits for tf_thread to complete
				while(ready_pack_pt != NULL){
					continue;
				}
				pthread_mutex_lock(&mut_printf);
				printf("[ROOM_THREAAD id %d] target flighter server work completed\n",r_i_pt->room_id);
				pthread_mutex_unlock(&mut_printf);
			}

		}

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
				alive_group_id = (*(r_i_pt->clients)).fos->s.group_id;
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
		for(i = 0; i < r_i_pt->size; i++){
			f_o_pt = &((*(r_i_pt->clients+i)).fos->op);
			memcpy(match_record+mr_cursor,(char *)&f_o_pt,sizeof(flighter_op));
			mr_cursor += sizeof(flighter_op);
		}

		


		if(game_should_end == 1){
			net_m_s.timestamp = -1;
			net_m_s.steplength = 0;
			net_m_s.flighters_n = 0;
			net_m_s.weapons_n = 0;
			net_m_s.winner_group = alive_group_id;
			strncpy(buf,(char *)&net_m_s,sizeof(net_match_status));	
			for(i = 0; i < r_i_pt->size; i++){
				(r_i_pt->clients+i)->overall_status = buf;
				(r_i_pt->clients+i)->os_size = sizeof(net_match_status);
			}
			// record
			memcpy(match_record+mr_cursor,buf,cursor);
			mr_cursor += cursor;				
			// TODO: write to local fS	
			sprintf(buf,"./match_records/match_type%d_id%d.rec",r_i_pt->match_type,r_i_pt->match_id);
			mr_fd = open(buf,O_WRONLY|O_CREAT,0644);
			rio_writen(mr_fd,match_record,mr_cursor);
			close(mr_fd);

			pthread_mutex_lock(&mut_printf);
			printf("[ROOM_THREAAD id %d] [GAME id %d]room status of clock %d:\n #### Group %d has won! Game over ####\n",r_i_pt->room_id,r_i_pt->match_id,room_clock,alive_group_id);
			pthread_mutex_unlock(&mut_printf);
				
			

			// when room_clock moves on notify all clients to move on
			pthread_mutex_lock(&mut_clients);
			room_clock++;
			pthread_cond_broadcast(&cond_clients);
			pthread_mutex_unlock(&mut_clients);
	
			ccr_ct_reset(&cct_sync_clients);
			
			if(ccr_ct_dec(&cct_rooms) != 0){
				exit(-1);
			}
			pthread_exit(NULL);
		}
		cursor = 0;
		net_m_s.timestamp = room_clock;
		net_m_s.steplength = r_i_pt->simulation_steplength;
		net_m_s.flighters_n = (r_i_pt->match_type == 0?r_i_pt->size+1:r_i_pt->size);
		// TODO: weapons_n should be really dealt with!
		net_m_s.weapons_n = 0;
		net_m_s.winner_group = 0;
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
		memcpy(match_record+mr_cursor,buf,cursor);
		mr_cursor+=cursor;
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
		pthread_cond_broadcast(&cond_clients);
		pthread_mutex_unlock(&mut_clients);
	
		ccr_ct_reset(&cct_sync_clients);
	
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
	if(argc != 8){
		fprintf(stderr,"usage: %s <port_for_roomserver> <port_for_clients> <port_for_director> <port_for_tf> <port_for_model_server> <max_rooms> <max_clients>\n",argv[0]);
		exit(1);
	}
	roomserver_listenfd = open_listenfd(argv[1]);
	clients_listenfd = open_listenfd(argv[2]);
	director_listenfd = open_listenfd(argv[3]);
	tf_listenfd = open_listenfd(argv[4]);
	if(S_SERVER_WORK){
		sserver_listenfd = open_listenfd(argv[5]);
		if(sserver_listenfd < 0){
			fprintf(stderr,"net error\n");
			return -1;
		}
	}

	if(roomserver_listenfd < 0 || clients_listenfd < 0 || director_listenfd < 0 || tf_listenfd < 0){
		fprintf(stderr,"net error\n");
		return -1;
	}

	srand(time(NULL));

	max_rooms = atoi(argv[6]);
	max_clients = atoi(argv[7]);
	sbuf_init(&sbuf_for_room_server,max_rooms);
	sbuf_init(&sbuf_for_clients,max_clients);
			
	ccr_rw_map_init(&cmap_cid2cinfo);
	ccr_rw_map_init(&cmap_rid2rinfo);
	ccr_ct_init(&cct_rooms);
	ccr_ct_init(&cct_clients);

	// mutex & cond for s_server
	pthread_mutex_init(&mut_s_server,NULL);
	pthread_cond_init(&cond_s_server,NULL);
	ready_client_id = 0;
	
	// mutex & cond for controller
	pthread_mutex_init(&mut_con,NULL);
	pthread_cond_init(&cond_con,NULL);
	// mutex & cond for target flighter server
	pthread_mutex_init(&mut_tf,NULL);
	pthread_cond_init(&cond_tf,NULL);
	ready_pack_pt = NULL;	
	tf_ready_signal = 1;
	// stdout
	pthread_mutex_init(&mut_printf,NULL);

	// room_thread counter and client_thread counter

	// the thread that waits for incoming clients
	// this thread is responsible of assigning a client to its specific room
	pthread_create(&tid,NULL,waiting_for_clients_thread,NULL);

	// the thread connects s_server
	pthread_create(&tid,NULL,connecting_s_server_thread,NULL);

	// the thread connecting with director server A.K.A dao tiao tai
	pthread_create(&tid,NULL,controller_thread,NULL);
	
	pthread_create(&tid,NULL,connecting_target_flighter_thread,NULL);

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

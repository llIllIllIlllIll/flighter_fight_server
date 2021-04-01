#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "flighter_msg.h"
int * clientfds;
rio_t * rios;
int glo_clientid;
pthread_mutex_t mut_clientid;
char * host, * port;
pthread_mutex_t mut_printf;
int clocks;
pthread_mutex_t mut_net;

int match_type;

void * fake_client_thread(void * vargs){
	int local_clientid;
	int clientfd;
	rio_t rio;
	char buf[MAXLINE];
	int i;
	int n;
	int j;
	int clients;
	net_flighter_op net_f_o;
	net_match_status net_m_s;
	net_flighter_status net_f_s;

	pthread_mutex_lock(&mut_clientid);
	local_clientid = glo_clientid++;
	pthread_mutex_unlock(&mut_clientid);
	
	clientfd = open_clientfd(host,port);
	if(clientfd < 0){
		pthread_mutex_lock(&mut_printf);
		fprintf(stderr,"net error\n");
		pthread_mutex_unlock(&mut_printf);
		pthread_exit(NULL);
	}
	rio_readinitb(&rio,clientfd);
	
	memset(buf,0,MAXLINE);
	sprintf(buf,"%d\n",local_clientid);
	strncpy(net_f_o.user_id,buf,strlen(buf));
	//sleep(100);
	rio_writen(clientfd,&net_f_o,sizeof(net_flighter_op));
	rio_readnb(&rio,&net_m_s,sizeof(net_match_status));
	rio_readnb(&rio,&net_f_s,sizeof(net_flighter_status));
	rio_readnb(&rio,&net_f_s,sizeof(net_flighter_status));

	pthread_mutex_lock(&mut_printf);
	printf("client %d ready to begin official game process\n",local_clientid);
	pthread_mutex_unlock(&mut_printf);

	for(i = 1; i <= clocks*2; i++){
		if(i%2){
			sleep(1);
			memset(&net_f_o,0,sizeof(net_flighter_op));
			//pthread_mutex_lock(&mut_net);	
			//rio_writen(clientfd,&net_f_o,sizeof(net_flighter_op));

			rio_writen(clientfd,&net_f_o,sizeof(net_flighter_op));
			printf("client %d has writen %d bytes to server\n",local_clientid,sizeof(net_flighter_op));
			//pthread_mutex_unlock(&mut_net);
		}
		else{
			/*n = 0;
			buf[0] = '\0';
			while(n == 0){
				n = rio_readlineb(&rio,buf,MAXLINE);
			}
			if(buf[0] == 'E'){
				pthread_exit(NULL);
			}
			n = 0;
			while(n == 0){
				if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
					clients = atoi(buf);
					pthread_mutex_lock(&mut_printf);
					printf("[CLIENT %d] received: %d\n",local_clientid,clients);
					pthread_mutex_unlock(&mut_printf);
					break;
				}
			}
			for(j = 0; j < clients; j++){
				n = 0;
				while(n == 0){
					if((n = rio_readlineb(&rio,buf,MAXLINE)) != 0){
						pthread_mutex_lock(&mut_printf);
						printf("[CLIENT %d] received: %s",local_clientid,buf);
						pthread_mutex_unlock(&mut_printf);
						break;
					}
				}
			}
			n = 0;
			while(n == 0){
				n = rio_readlineb(&rio,buf,MAXLINE);
			}*/
			rio_readnb(&rio,&net_m_s,sizeof(net_match_status));
			rio_readnb(&rio,&net_f_s,sizeof(net_flighter_status));
			rio_readnb(&rio,&net_f_s,sizeof(net_flighter_status));

			
			//pthread_mutex_unlock(&mut_printf);
		}
	}
	pthread_exit(NULL);
	
}

// simulate clients
int main(int argc, char * argv []){
	int n;
	int i;
	int clients;
	pthread_t * tids;
	

	if(argc != 6){
		fprintf(stderr,"usage: %s <host> <port> <clients> <clock_total> <match_type>\n",argv[0]);
		return -1;
	}
	host = argv[1];
	port = argv[2];
	clients = atoi(argv[3]);
	clocks = atoi(argv[4]);
	match_type = atoi(argv[5]);

	clientfds = (int *)malloc(clients*sizeof(int));
	rios = (rio_t *)malloc(clients*sizeof(rio_t));
	tids = (pthread_t *)malloc(clients*sizeof(pthread_t));

	pthread_mutex_init(&mut_clientid,NULL);
	glo_clientid = 1;

	pthread_mutex_init(&mut_printf,NULL);
	
	pthread_mutex_init(&mut_net,NULL);

	for(i = 0; i < clients;i++){
		pthread_create(tids+i,NULL,fake_client_thread,NULL);
	}
	
	for(i = 0; i < clients;i++){
		pthread_join(*(tids+i),NULL);
	}

	return 0;

}

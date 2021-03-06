#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
int * clientfds;
rio_t * rios;
int glo_clientid;
pthread_mutex_t mut_clientid;
char * host, * port;
pthread_mutex_t mut_printf;
int clocks;


void * fake_client_thread(void * vargs){
	int local_clientid;
	int clientfd;
	rio_t rio;
	char buf[MAXLINE];
	int i;
	int n;
	int j;
	int clients;

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
	
	sprintf(buf,"%d\n",local_clientid);
	rio_writen(clientfd,buf,strlen(buf));
	
	for(i = 1; i <= clocks*2; i++){
		if(i%2){
			char * content = "1 1 1 1 1\n0\n";
			rio_writen(clientfd,content,strlen(content));
		}
		else{
			n = 0;
			buf[0] = '\0';
			while(n == 0){
				if((n = rio_read(&rio,buf,MAXLINE)) != 0){
					printf("[CLIENT ID %d CLOCK %d]res:\n%s",local_clientid,i/2,buf);
					break;
				}
			}
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

	if(argc != 5){
		fprintf(stderr,"usage: %s <host> <port> <clients> <clock_total>\n",argv[0]);
		return -1;
	}
	host = argv[1];
	port = argv[2];
	clients = atoi(argv[3]);
	clocks = atoi(argv[4]);

	clientfds = (int *)malloc(clients*sizeof(int));
	rios = (rio_t *)malloc(clients*sizeof(rio_t));
	tids = (pthread_t *)malloc(clients*sizeof(pthread_t));

	pthread_mutex_init(&mut_clientid,NULL);
	glo_clientid = 1;

	pthread_mutex_init(&mut_printf,NULL);

	for(i = 0; i < clients;i++){
		pthread_create(tids+i,NULL,fake_client_thread,NULL);
	}
	
	for(i = 0; i < clients;i++){
		pthread_join(*(tids+i),NULL);
	}

	return 0;

}

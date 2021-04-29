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
#define ROOM_MAX_WAITING_MSEC (6000*1000)
#define N_M_SIZE (sizeof(net_match_status))
#define N_F_SIZE (sizeof(net_flighter_status))
#define PI 3.1415926
#define RAND_ANGLE() ((((double)(rand()%60))/60.0)*2*PI)
// 4MB
#define MATCH_RECORD_MAX_SIZE (1<<22)
//#define DEBUG

int main(int argc, char * argv[]){
	char buf [MAXLINE];
	int clientfd = open_clientfd("localhost","30609");
	if(clientfd < 0)
		printf("net error\n");
	cJSON * res = NULL; 
	res = cJSON_CreateObject();
	cJSON_AddNumberToObject(res,"id",37);
	cJSON_AddNumberToObject(res,"gameId",117);
	char * json_string = cJSON_Print(res);
	sprintf(buf,"POST /room/endGame HTTP/1.1\r\nContent-Type: application/json\r\nHost: 202.120.40.8:30609\r\nContent-Length:%d\r\n\r\n%s",strlen(json_string),json_string);
	printf("write content : %s\n",buf);
	rio_writen(clientfd,buf,strlen(buf));
	int n = 0;
	rio_t rio;
	rio_readinitb(&rio,clientfd);

	int flags = fcntl(clientfd,F_GETFL,0); 
        fcntl(clientfd,F_SETFL,flags | O_NONBLOCK);
	while((n = rio_readnb(&rio,buf,MAXLINE,0) > 0)){
		printf("res:%s",buf);
	}

	return 0;

}

#ifndef _CCR_CT
#define _CCR_CT
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
// a counter that supports access from multiple threads 
typedef struct _concurrnt_ct{
	pthread_mutex_t lock;
	int ct;
} ccr_ct;
// initialize a ccr_ct 0 success -1 fail
int ccr_ct_init(ccr_ct * cct);
// return 0 if success -1 fail
// increase counter by 1
int ccr_ct_inc(ccr_ct * cct);
// return 0 if success -1 fail
// decrease counter by 1
int ccr_ct_dec(ccr_ct * cct);
// return 0 if success -1 
// value of ct is stored in v
int ccr_ct_query(ccr_ct * cct,int * v);
// return 0 if success -1 otherwise
int ccr_ct_reset(ccr_ct * cct);
#endif

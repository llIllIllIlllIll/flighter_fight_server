#include "ccr_counter.h"
int ccr_ct_init(ccr_ct * cct){
	if(pthread_mutex_init(&cct->lock,NULL) != 0){
		fprintf(stderr,"Failed when initializing mutex lock.\n");
		return -1;
	}
	else{
		cct->ct = 0;
		return 0;
	}
}
int ccr_ct_inc(ccr_ct * cct){
	if(pthread_mutex_lock(&cct->lock) != 0){
		fprintf(stderr,"Failed when locking mutex lock.\n");
		return -1;
	}
	cct->ct++;
	if(pthread_mutex_unlock(&cct->lock) != 0){
		fprintf(stderr,"Failed when unlocking mutex lock.\n");
		return -1;
	}
}
int ccr_ct_dec(ccr_ct * cct){
	if(pthread_mutex_lock(&cct->lock) != 0){
		fprintf(stderr,"Failed when locking mutex lock.\n");
		return -1;
	}
	cct->ct--;
	if(pthread_mutex_unlock(&cct->lock) != 0){
		fprintf(stderr,"Failed when unlocking mutex lock.\n");
		return -1;
	}
}
int ccr_ct_query(ccr_ct * cct, int * v){
	if(pthread_mutex_lock(&cct->lock) != 0){
		fprintf(stderr,"Failed when locking mutex lock.\n");
		return -1;
	}
	*v = ccr_ct;
	if(pthread_mutex_unlock(&cct->lock) != 0){
		fprintf(stderr,"Failed when unlocking mutex lock.\n");
		return -1;
	}
}
}

#include <stdio.h>
#include "ccr_rw_map.h"
int main(){
	ccr_rw_map cmap;
	int i;
	uint64_t k,v;

	ccr_rw_map_init(&cmap);
	for(i = 0; i < 200; i++){
		ccr_rw_map_insert(&cmap,i,i);
	}
	while(ccr_rw_map_iterate(&cmap,&k,&v)!=0){
		printf("%d %d\n",k,v);
	}
	return 0;

}

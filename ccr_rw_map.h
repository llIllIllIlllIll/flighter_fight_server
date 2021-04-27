#ifndef _CCR_RW_MAP
#define _CCR_RW_MAP
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define SLOTS 100
#define HASH(n) ((n)%SLOTS)
typedef struct _map_slot{
	uint64_t key;
	uint64_t val;
	struct _map_slot * next;
} slot;
// a map that maps uint32_t (id) to uint32_t () 
typedef struct _concurrnt_rw_map{
	pthread_rwlock_t rwlock;
	slot * slots[SLOTS];
	// below two are for iteration
	int cur_slot_n;
	slot * cur_slot_pt;
} ccr_rw_map;
// initialize a ccr_rw_map 0 success -1 fail
int ccr_rw_map_init(ccr_rw_map * cmap);
// insert a <key,val> pair into map
// return 0 if success -1 fail
int ccr_rw_map_insert(ccr_rw_map * cmap,uint64_t k,uint64_t v);
// query in map
// return 0 if success -1 k not found in map -2 other errors
int ccr_rw_map_query(ccr_rw_map * cmap,uint64_t k,uint64_t * v);
// iterate over this structure
// usage: each time this function is called the corresponding key and value are assigned to k and v
// return value is 0 if this is the last element in ccr_rw_map
// othersize return 1
int ccr_rw_map_iterate(ccr_rw_map * cmap,uint64_t * k,uint64_t * v);
// delete from map: 0 success -1 failed
int ccr_rw_map_delete(ccr_rw_map * cmap,uint64_t k);
#endif

#include "ccr_rw_map.h"
int ccr_rw_map_init(ccr_rw_map * cmap){
	int i;
	int ret = pthread_rwlock_init(&cmap->rwlock,NULL);
	if(ret != 0){
		fprintf(stderr,"Error when initializing rwlock.\n");
		return -1;
	}
	//cmap->rwlock = PTHREAD_RWLOCK_INITIALIZER;
	for(i = 0; i < SLOTS; i++){
		cmap->slots[i] = NULL;
	}

	cmap->cur_slot_n = 0;
	cmap->cur_slot_pt = NULL;
	return 0;
}
int ccr_rw_map_insert(ccr_rw_map * cmap,uint64_t k,uint64_t v){
	int ret = pthread_rwlock_wrlock(&cmap->rwlock);
	if(ret != 0){
		fprintf(stderr,"Error when getting wrlock.\n");
		return -1;
	}
	slot * new_slot = (slot *)malloc(sizeof(slot));
	if(new_slot == NULL){
		fprintf(stderr,"Error when calling malloc.\n");
		pthread_rwlock_unlock(&cmap->rwlock);
		return -1;
	}
	new_slot->next = NULL;
	new_slot->key = k;
	new_slot->val = v;
	int slot_n = HASH(k);
	if(cmap->slots[slot_n] == NULL){
		cmap->slots[slot_n] = new_slot;
		pthread_rwlock_unlock(&cmap->rwlock);
		return 0;
	}
	else{
		slot * cur_slot = cmap->slots[slot_n];
		while(cur_slot->next != NULL){
			if(cur_slot->key == k){
				// overwrite
				cur_slot->val = v;
				pthread_rwlock_unlock(&cmap->rwlock);
				free(new_slot);
				return -2;
			}
			cur_slot = cur_slot->next;
		}
		if(cur_slot->key == k){
			// overwrite
			cur_slot->val = v;
			pthread_rwlock_unlock(&cmap->rwlock);
			free(new_slot);
			return -2;
		}
		else{
			cur_slot->next = new_slot;
			pthread_rwlock_unlock(&cmap->rwlock);
			return 0;
		}
	}
}
int ccr_rw_map_query(ccr_rw_map * cmap,uint64_t k,uint64_t * v){
	int ret = pthread_rwlock_rdlock(&cmap->rwlock);
	if(ret != 0){
		fprintf(stderr,"Error when getting rdlock.\n");
		return -2;
	}
	slot * target_slot = cmap->slots[HASH(k)];
	while(target_slot != NULL){
		if(target_slot->key == k){
			*v =  target_slot->val;
			pthread_rwlock_unlock(&cmap->rwlock);
			return 0;
		}
		else{
			target_slot = target_slot->next;
		}
	}
	pthread_rwlock_unlock(&cmap->rwlock);
	return -1;
}
int ccr_rw_map_iterate(ccr_rw_map * cmap,uint64_t * k, uint64_t * v){
	int ret = pthread_rwlock_rdlock(&cmap->rwlock);
	if(ret != 0){
		fprintf(stderr,"Error when getting rdlock.\n");
		return -2;
	}
	if(cmap->cur_slot_pt != NULL){
		cmap->cur_slot_pt = cmap->cur_slot_pt->next;
		if(cmap->cur_slot_pt == NULL){
			cmap->cur_slot_n ++;
		}
		if(cmap->cur_slot_n == SLOTS){
			cmap->cur_slot_n = 0;
			pthread_rwlock_unlock(&cmap->rwlock);
			return 0;
		}
	}

	if(cmap->cur_slot_pt == NULL){
		while(cmap->slots[cmap->cur_slot_n] == NULL){
			cmap->cur_slot_n++;
			if(cmap->cur_slot_n == SLOTS){
				cmap->cur_slot_n = 0;
				pthread_rwlock_unlock(&cmap->rwlock);
				return 0;
			}
		}
		cmap->cur_slot_pt = cmap->slots[cmap->cur_slot_n];
	}
	
	*k = cmap->cur_slot_pt->key;
	*v = cmap->cur_slot_pt->val;
	pthread_rwlock_unlock(&cmap->rwlock);
	// not last element in map
	return 1;
}
int ccr_rw_map_delete(ccr_rw_map *cmap, uint64_t k){
	int ret = pthread_rwlock_wrlock(&cmap->rwlock);
	if(ret != 0){
		fprintf(stderr,"Error when getting rdlock.\n");
		return -2;
	}
	slot * cur_slot = cmap->slots[HASH(k)], * prev_slot = NULL;
	if(cur_slot == NULL){
		pthread_rwlock_unlock(&cmap->rwlock);
		return -1;
	}
	else if(cur_slot->key == k){
		cmap->slots[HASH(k)] = cur_slot->next;
		// free(cur_slot);
		// success
		pthread_rwlock_unlock(&cmap->rwlock);
		return 0;
	}
	prev_slot = cur_slot;
	cur_slot = cur_slot->next;
	while(cur_slot != NULL && cur_slot->key != k){
		prev_slot = cur_slot;
		cur_slot = cur_slot->next;
	}
	if(cur_slot == NULL){
		// not found
		pthread_rwlock_unlock(&cmap->rwlock);
		return -1;
	}
	else{
		// found
		prev_slot->next = cur_slot->next;
		// free(cur_slot);
		pthread_rwlock_unlock(&cmap->rwlock);
		return 0;
	}		
	
}

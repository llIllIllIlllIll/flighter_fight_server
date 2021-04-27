#include "cJSON_RoomStatus.h"
cJSON * create_room_status_JSON_obj(room_info * r_i_pt){
	cJSON * room_status = NULL;
	cJSON * clients = NULL;
	cJSON * client = NULL;
	client_info * c_i_pt = NULL;
	int i;	
	if((room_status = cJSON_CreateObject()) == NULL)	
		return NULL;
	if(cJSON_AddNumberToObject(room_status,"room_id",r_i_pt->room_id) == NULL)
		return NULL;
	if(cJSON_AddNumberToObject(room_status,"match_id",r_i_pt->match_id) == NULL)
		return NULL;
	if(cJSON_AddNumberToObject(room_status,"steplength",r_i_pt->simulation_steplength) == NULL)
		return NULL;
	if((clients = cJSON_AddArrayToObject(room_status,"clients")) == NULL){
		return NULL;
	}
	for(i = 0; i < r_i_pt->size; i++){
		c_i_pt = r_i_pt->clients+i;
		if((client = cJSON_CreateObject()) == NULL)
			return NULL;
		if(cJSON_AddNumberToObject(client,"client_id",c_i_pt->id) == NULL)
			return NULL;	
		if(cJSON_AddNumberToObject(client,"group_id",c_i_pt->group_id) == NULL)
			return NULL;
		if(cJSON_AddNumberToObject(client,"flighter_id",c_i_pt->flighter_id) == NULL)
			return NULL;
		if(cJSON_AddNumberToObject(client,"tic",c_i_pt->fos->s.tic) == NULL)
			return NULL;
	}

	return room_status;
}

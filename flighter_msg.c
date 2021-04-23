#include "flighter_msg.h"
void delete_room_from_cmap(ccr_rw_map * cmap_rid2rinfo,ccr_rw_map * cmap_cid2cinfo, room_info * r_i_pt){
	uint32_t room_id = r_i_pt->room_id;
	uint32_t size = r_i_pt->size;
	uint32_t i;
	ccr_rw_map_delete(cmap_rid2rinfo,room_id);
	for(i = 0; i < size; i++){
		ccr_rw_map_delete(cmap_cid2cinfo,(r_i_pt->clients+i)->id);
	}
	
}

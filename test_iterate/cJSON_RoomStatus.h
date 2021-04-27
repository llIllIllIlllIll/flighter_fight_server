#include "cJSON.h"
#include "flighter_msg.h"
/* This header file contains information of constructing a Room_Status JSON object
 * The object is used to be sent to web page of flighter_fight_server
 *
 * And the pattern of Room_Status should be like this:
 * {
 *	"room_id":1,
 *	"match_id":1,
 *	"steplength":100,
 *	"clients":[
 *		{
 *			"client_id":1,
 *			"tic":1
 *		},
 *		{
 *			"client_id":2,
 *			"tic":0
 *		}
 *	]
 * }
 * */
cJSON * create_room_status_JSON_obj(room_info * r_i_pt);

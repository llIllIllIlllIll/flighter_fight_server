// this header file describes all data structures that is needed in network messages
#include <stdint.h>
#ifndef _FLIGHTER_H
#define _FLIGHTER_H
// flight operation: sent by clients to this server
// launch_weapon: 0 no 1 launch a missile 2 shoot a bullet 3 both
typedef struct _flighter_op{
	// fu yang
	int32_t pitch;
	// fan gun
	int32_t roll;
	// hang xiang
	int32_t direction;
	// you men 
	int32_t acceleration;
	// 
	uint32_t launch_weapon;
} flighter_op;

typedef struct _destroyed_flighter_ids{
	uint32_t * flighter_ids;
	uint32_t size;
} destroyed_flighter_ids;

// weapon status: sent by this server back to clients
// weapon_id: distinct missisle id
// weapon_type: 0 vanished 1 bullet 2 missile 
typedef struct _weapon_status{
	uint32_t weapon_id;
	uint32_t weapon_type;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t u;
	int32_t v;
	int32_t w;
	int32_t vx;
	int32_t vy;
	int32_t vz;
} weapon_status;

// flight status
typedef struct _flighter_status{
	// position
	uint32_t flighter_id;
	uint32_t group_id;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t u;
	int32_t v;
	int32_t w;
	// speed
	int32_t vx;
	int32_t vy;
	int32_t vz;
	int32_t vu;
	int32_t vv;
	int32_t vw;
	
} flighter_status;

// the overall status in a flighter match
typedef struct _match_status{
	flighter_status * flighters;
	uint32_t flighters_num;
	weapon_status * weapons;
	uint32_t weapons_num;
} match_status;

// single client information: 
// id: each student or manager has a distinct id (maybe SJTU student code)
// grupd_id: n v n flight fight preparation
// host and port need no explanation
// sign 1: participant in fight  0: just a watcher
// flight_id: negative if this client is just a watcher positive or 0 if this client is a participant and is opearting a flighter
// flighter_type: a number to indicate the specific information of a plane
typedef struct _client_info{
	uint32_t id;
	uint32_t group_id;
	char host[20];
	char port[8];
	uint32_t sign;
	int32_t flighter_id;
	uint32_t flighter_type;
} client_info;

// single room information:
// room_id: distinct id for each room
// room_size: maximum clients that can connect to this room
// simulation_steplength: the length of each step in simulation
// env_id: environment setup
// match type: 0 for practise 1 for official match
// clients: current clients
// size: number of current clients
typedef struct _room_info{
	uint32_t room_id;
	uint32_t room_size;
	uint32_t simulation_steplength;
	uint32_t env_id;
	uint32_t match_type;
	client_info * clients;
	uint32_t size;
} room_info;


// dao tiao tai may reload a flight's weapons
typedef struct _flighter_weapon_load{
	uint32_t flighter_id;
	uint32_t missiles;
	uint32_t bullets;
} flighter_weapon_load;

// dao tiao tai signal
// debug_mode: 0 normal 1 pause match 2 continue match 3 continue only 1 step
typedef struct _daotiaotai_signal{
	uint32_t debug_signal;
	flighter_weapon_load * reload_flighters;
	uint32_t reload_flighters_n;
} daotiaotai_signal;

#endif

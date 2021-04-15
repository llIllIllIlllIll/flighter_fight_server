// this header file describes all data structures that is needed in network messages
#include <stdint.h>
#include "ccr_counter.h"
#include <pthread.h>
#include "ccr_rw_map.h"

#ifndef _FLIGHTER_H
#define _FLIGHTER_H
#define ROLE_SEND 0
#define ROLE_RECV 1

#define STR_LEN 64
// flight operation: sent by clients to this server
// launch_weapon: 0 no 1 launch a missile 2 shoot a bullet 3 both
typedef struct _flighter_op{
	uint32_t tic;
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

// fjj struct
typedef struct _posture{
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t u;
	int32_t v;
	int32_t w;
	int32_t vx;
	int32_t vy;
	int32_t vz;
	int32_t vu;
	int32_t vv;
	int32_t vw;
} posture;
typedef struct _socket_role{
	int32_t id;
	int32_t type; // 0 send 1 recv 
} socket_role;

typedef struct _destroyed_flighter_ids{
	uint32_t * flighter_ids;
	uint32_t size;
} destroyed_flighter_ids;
// net struct: communication with client
// all string are forced to be 64 bytes
// the following 3 are the operation of flighter
typedef struct _net_flighter_op{
	int32_t user_id;
	int32_t timestamp;
	int32_t op_pitch;
	int32_t op_roll;
	int32_t op_dir;
	int32_t op_acc;
	int32_t launch_weapon;
	int32_t detected_destroyed_flighters;
	int32_t detected_destroyed_weapons;

} net_flighter_op;
typedef struct _net_destroyed_flighter{
	int32_t id;
	int32_t killer_id;
} net_destroyed_flighter;
typedef struct _net_destroyed_weapon{
	int32_t id;
} net_destroyed_weapon;
// following: match status 
typedef struct _net_match_status{
	int32_t timestamp;
	int32_t steplength;
	int32_t flighters_n;
	int32_t weapons_n;
	int32_t winner_group;
} net_match_status;
typedef struct _net_flighter_status{
	int32_t user_id;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t u;
	int32_t v;
	int32_t w;
	int32_t vx;
	int32_t vy;
	int32_t vz;
	int32_t vu;
	int32_t vv;
	int32_t vw;
	int32_t loaded_weapon_types;
} net_flighter_status;
typedef struct _net_weapon_load{
	int32_t type;
	int32_t n;
} net_weapon_load;
typedef struct _net_weapon_status{
	int32_t user_id;
	int32_t weapon_type;
	int32_t timestamp;
	int32_t alive_time_left;
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t u;
	int32_t v;
	int32_t w;
	int32_t vx;
	int32_t vy;
	int32_t vz;
} net_weapon_status;




// tic for syncronization both status

// weapon status: sent by this server back to clients
// weapon_id: distinct missisle id
// weapon_type: 0 vanished 1 bullet 2 missile 
typedef struct _weapon_status{
	uint32_t tic;
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
	uint32_t tic;
	// is this plane alive? 1 yes 0 no
	uint32_t alive;
	// position
	uint32_t flighter_id;
	uint32_t user_id;
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

// collection of flighter_op and flighter_status
typedef struct _flighter_op_and_status{
	flighter_op op;
	flighter_status s;
} flighter_op_and_status;

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
// flight_id: if sign 1 this is the id of this client's flight else if sign 0 this is meaningless
// flighter_type: a number to indicate the specific information of a plane
typedef struct _client_info{
	uint32_t id;
	uint32_t group_id;
	uint32_t room_id;
	char host[20];
	char port[8];
	uint32_t sign;
	uint32_t flighter_id;
	uint32_t flighter_type;
	// a pointer to this client's flight
	flighter_op_and_status * fos;
	// a pointer to this room's sync counter
	ccr_ct * cct_sync_clients;
	// a pointer to this room's clock
	int * room_clock_p;
	// mutex & cond for client_thread to wake up its room_thread
	pthread_mutex_t * mut_room_pt;
	pthread_cond_t * cond_room_pt;
	// mutex & cond for room_thread to wake up its client_thread s
	pthread_mutex_t * mut_clients_pt;
	pthread_cond_t * cond_clients_pt;

	// status of all flighters that should be sent to each client
	char * overall_status;
	// sizeof overall_status
	uint32_t os_size;
	// cmap <flighter_id,number_of_clients_sentence_it_to_death>
	ccr_rw_map * cmap_fid2desct;
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
	uint32_t match_id;
	uint32_t room_size;
	uint32_t simulation_steplength;
	uint32_t env_id;
	uint32_t match_type;
	client_info * clients;
	uint32_t size;
	// status is used when a Director decides to pause a room's progress
	uint32_t status;
	// multi thread needed
	pthread_mutex_t * mut;
	pthread_cond_t * cond;
	// cmap <flighter_id,number_of_clients_sentence_it_to_death>
	ccr_rw_map * cmap_fid2desct;
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

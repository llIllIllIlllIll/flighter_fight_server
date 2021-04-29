#! /bin/bash
gcc -pthread -lm ccr_rw_map.h ccr_rw_map.c flighter_msg.h flighter_msg.c net.h cJSON.h cJSON.c cJSON_RoomStatus.h cJSON_RoomStatus.c ccr_counter.h ccr_counter.c main.c -o test_post_ge


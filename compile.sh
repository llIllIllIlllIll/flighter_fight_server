#! /bin/bash
gcc -pthread -lm flighter_msg.h net.h cJSON.h cJSON.c cJSON_RoomStatus.h cJSON_RoomStatus.c ccr_counter.h ccr_counter.c ccr_rw_map.h ccr_rw_map.c main.c -o flight_server

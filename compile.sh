#! /bin/bash
gcc -pthread -lm ccr_counter.h ccr_counter.c ccr_rw_map.h ccr_rw_map.c flighter_msg.h net.h main.c -o flight_server

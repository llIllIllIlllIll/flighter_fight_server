all: main.o ccr_rw_map.o ccr_counter.o flighter_msg.o cJSON.o cJSON_RoomStatus.o
	gcc main.o ccr_rw_map.o ccr_counter.o flighter_msg.o cJSON.o cJSON_RoomStatus.o -pthread -g -lm -o target_flighter
main.o:
	gcc -I . -c main.c
ccr_rw_map.o:
	gcc -I . -c ccr_rw_map.c
ccr_counter.o:
	gcc -I . -c ccr_counter.c
flighter_msg.o:
	gcc -I . -c flighter_msg.c
cJSON.c:
	gcc -I . -c cJSON.c
cJSON_RoomStatus.o:
	gcc -I . -c cJSON_RoomStatus.c
clean:
	rm -rf *.o
	rm target_flighter


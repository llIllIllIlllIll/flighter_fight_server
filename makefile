all: main.o ccr_rw_map.o ccr_counter.o flighter_msg.o cJSON.o cJSON_RoomStatus.o
	gcc main.o ccr_rw_map.o ccr_counter.o flighter_msg.o cJSON.o cJSON_RoomStatus.o -pthread -g -lm -o target_flighter
	rm *.o
%.o: %.c # wildcard
	gcc -I . -c $*.c
clean:
	rm -rf *.o
	rm target_flighter


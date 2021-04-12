#include <stdio.h>
#include "flighter_msg.h"
int main(){
	printf("%d %d %d %d %d",sizeof(net_match_status),sizeof(net_destroyed_flighter),sizeof(net_match_status),sizeof(net_flighter_op),sizeof(net_weapon_load));
}

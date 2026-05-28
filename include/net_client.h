#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include "game_logic.h"

int net_client_connect(const char *server_ip);
int net_client_send_player(Player *player, float mouse_x, float mouse_y, bool is_fire);
int net_client_recv_player(NetPacket *out_packet);
void net_client_close(void);

#endif
#ifndef SERVER_LOG_H
#define SERVER_LOG_H

#include "net_server.h"

int server_log_init(ServerPlayer *players, int player_count);
void server_log_save_match(const char *reason);

#endif

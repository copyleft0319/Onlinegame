#ifndef SERVER_LOG_H
#define SERVER_LOG_H

#include "net_server.h"

int server_log_init(ServerPlayer *players, int player_count);
void server_log_close(void);
void server_log_event(const char *fmt, ...);
void server_log_player_join(int id);
void server_log_player_leave(int id);
void server_log_player_kill(int killer_id, int victim_id);
void server_log_save_match(const char *reason);

#endif

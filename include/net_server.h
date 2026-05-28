#include <stdio.h>
#include <winsock2.h>
#include <stdbool.h>
#include "game_logic.h"
#include "net_proto.h"

#define MAX_CLIENTS 5
#define MAX_SOCKETS 10000
#define PORT 8888
#define MAX_SERVER_BULLETS 256
#define SERVER_TICK_DT 0.0333333f
#define SERVER_FIRE_INTERVAL 0.18f

typedef struct {
    uint32_t bullet_id;
    float x, y;
    float dx, dy;
    float speed;
    int owner_id;
    bool active;
} ServerBullet;

typedef struct {
    float x, y;
    bool invisible;
    bool active;
    int hp;
    int kills;
    int deaths;
    float fire_cd;
} ServerPlayer;

typedef struct {
    uint32_t seq;
    float x, y;
    float mouse_x, mouse_y;
    bool fire;
    bool invisible;
    bool received;
} ServerInputState;

void server_shoot(int owner_id, float px, float py, float tx, float ty);
void server_update_bullets(float dt);
void broadcast_framed(SOCKET sender, uint8_t msg_type, const void *payload, uint16_t payload_len);
void server_broadcast_all(void);

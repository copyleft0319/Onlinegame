#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdbool.h>

#define MAX_BULLETS 100
#define MAX_PLAYERS 4
#define MAP_WIDTH 800
#define MAP_HEIGHT 600

typedef struct {
    float x, y;
    float dx, dy; // 方向向量（子弹用）
    float speed;
    bool hit;
} Bullet;

typedef struct {
    float x, y;
    bool invisible;
    int hp;
    int score;
    int kills;
    int deaths;
    int bullet_count;
    Bullet bullets[MAX_BULLETS];
} Player;

// 玩家同步数据包（只发必要数据）
typedef struct {
    long long id;
    float x;          // 玩家坐标
    float y;
    float mouse_x;    // 鼠标坐标（算子弹方向）
    float mouse_y;
    bool invisble;    // 是否隐身
    bool is_fire;
} NetPacket;

typedef struct {
    Player local_player;
    Player remote_players[MAX_PLAYERS-1];
    int remote_count;
} GameState;

// 初始化游戏
void init_game();

// 每帧更新
void update_game(float delta_time);

// 玩家控制
void move_player(float dx, float dy);
void set_invisible(bool enable);
void shoot(float target_x, float target_y);

// 获取数据接口
Player* get_local_player();
Player* get_remote_players(int* count);
Bullet* get_bullets(int* count);

#endif
#include "game_logic.h"
#include <math.h>
#include <string.h>

static GameState g_state;

void init_game() {
    memset(&g_state, 0, sizeof(GameState));
    g_state.local_player.x = 100;
    g_state.local_player.y = 100;
    g_state.local_player.hp = 100;
    g_state.remote_count = 2; // 单机测试虚拟玩家
    for (int i=0;i<g_state.remote_count;i++){
        g_state.remote_players[i].x = 200 + i*100;
        g_state.remote_players[i].y = 200;
        g_state.remote_players[i].hp = 100;
    }
}

void move_player(float dx, float dy) {
    Player* p = &g_state.local_player;
    p->x += dx;
    p->y += dy;
    if (p->x < 0) p->x = 0;
    if (p->y < 0) p->y = 0;
    if (p->x > MAP_WIDTH) p->x = MAP_WIDTH;
    if (p->y > MAP_HEIGHT) p->y = MAP_HEIGHT;
}

void set_invisible(bool enable) {
    g_state.local_player.invisible = enable;
}

void shoot(float target_x, float target_y) {
    Player* p = &g_state.local_player;
    if (p->bullet_count >= MAX_BULLETS) return;
    Bullet* b = &p->bullets[p->bullet_count++];
    b->x = p->x;
    b->y = p->y;
    float dx = target_x - p->x;
    float dy = target_y - p->y;
    float len = sqrtf(dx*dx + dy*dy);
    b->dx = dx / len;
    b->dy = dy / len;
    b->speed = 300;
    b->hit = false;
}

// 碰撞检测（圆形简化）
static bool check_hit(Player* p, Bullet* b) {
    float dx = p->x - b->x;
    float dy = p->y - b->y;
    return (dx*dx + dy*dy) < 15*15;
}

void update_game(float delta_time) {
    // 更新子弹
    Player* players[MAX_PLAYERS] = { &g_state.local_player, &g_state.remote_players[0], &g_state.remote_players[1], &g_state.remote_players[2] };
    int total_players = g_state.remote_count + 1;
    for (int i=0;i<total_players;i++){
        Player* p = players[i];
        for (int j=0;j<p->bullet_count;j++){
            Bullet* b = &p->bullets[j];
            if (b->hit) continue;
            b->x += b->dx * b->speed * delta_time;
            b->y += b->dy * b->speed * delta_time;
            if (b->x < 0 || b->y < 0 || b->x > MAP_WIDTH || b->y > MAP_HEIGHT) {
                b->hit = true;
                continue;
            }
            // 检测命中其他玩家
            for (int k=0;k<total_players;k++){
                if (i==k) continue;
                Player* target = players[k];
                if (target->invisible) continue;
                if (check_hit(target,b)){
                    b->hit = true;
                    target->hp -= 10;
                    if (target->hp <=0){
                        target->hp = 100;
                        target->x = 50;
                        target->y = 50;
                        p->kills++;
                        target->deaths++;
                    }
                }
            }
        }
    }
}

// 数据接口
Player* get_local_player(){ return &g_state.local_player; }
Player* get_remote_players(int* count){ 
    *count = g_state.remote_count; 
    return g_state.remote_players; 
}

Bullet* get_bullets(int* count){
    static Bullet all[MAX_PLAYERS*MAX_BULLETS];
    int idx = 0;
    Player* players[MAX_PLAYERS] = { &g_state.local_player, &g_state.remote_players[0], &g_state.remote_players[1], &g_state.remote_players[2] };
    int total_players = g_state.remote_count + 1;
    for (int i=0;i<total_players;i++){
        for (int j=0;j<players[i]->bullet_count;j++){
            if (!players[i]->bullets[j].hit) all[idx++] = players[i]->bullets[j];
        }
    }
    *count = idx;
    return all;
}

// 简单序列化（本地玩家）
int pack_game_data(unsigned char* buffer, int bufsize){
    if(bufsize<sizeof(Player)) return -1;
    memcpy(buffer,&g_state.local_player,sizeof(Player));
    return sizeof(Player);
}

int unpack_game_data(unsigned char* buffer, int bufsize){
    if(bufsize<sizeof(Player)) return -1;
    memcpy(&g_state.remote_players[0],buffer,sizeof(Player));
    return sizeof(Player);
}


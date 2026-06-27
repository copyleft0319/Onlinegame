#include "game_logic.h"
#include <math.h>
#include <string.h>

GameState g_state;

// 初始化游戏状态，设置本地玩家初始位置和属性
void init_game() {
    memset(&g_state, 0, sizeof(GameState));
    g_state.local_player.x = 100;
    g_state.local_player.y = 100;
    g_state.local_player.hp = 100;
    g_state.local_player.active = true;
    g_state.remote_count = 0;
}

// 移动本地玩家位置，带边界检测
// dx: X方向移动量
// dy: Y方向移动量
void move_player(float dx, float dy) {
    Player* p = &g_state.local_player;
    p->x += dx;
    p->y += dy;
    if (p->x < 0) p->x = 0;
    if (p->y < 0) p->y = 0;
    if (p->x > MAP_WIDTH) p->x = MAP_WIDTH;
    if (p->y > MAP_HEIGHT) p->y = MAP_HEIGHT;
}

// 设置本地玩家隐身状态
// enable: true开启隐身，false关闭隐身
void set_invisible(bool enable) {
    g_state.local_player.invisible = enable;
}

// 射击函数（客户端占位，实际射击逻辑在服务端）
// target_x: 目标X坐标
// target_y: 目标Y坐标
void shoot(float target_x, float target_y) {
    (void)target_x;
    (void)target_y;
}

// 更新游戏逻辑，移动远程子弹并检测边界
// delta_time: 帧间隔时间（秒）
void update_game(float delta_time) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        RemoteBullet *b = &g_state.remote_bullets[i];
        if (!b->active) continue;
        b->x += b->dx * b->speed * delta_time;
        b->y += b->dy * b->speed * delta_time;
        if (b->x < 0 || b->y < 0 || b->x > MAP_WIDTH || b->y > MAP_HEIGHT)
            b->active = false;
    }
}

// 获取本地玩家指针
Player* get_local_player(){ return &g_state.local_player; }

// 获取远程玩家数组
// count: 输出参数，返回玩家数组大小（MAX_PLAYERS）
// 返回: 远程玩家数组指针
Player* get_remote_players(int* count){
    if (count)
        *count = MAX_PLAYERS;
    return g_state.remote_players;
}

// 获取活跃的远程子弹列表（转换为统一的Bullet格式）[remote_bullets[]->Bullet all[]]
// count: 输出参数，返回活跃子弹数量
// 返回: 活跃子弹数组指针
Bullet* get_bullets(int* count){
    static Bullet all[MAX_BULLETS];
    int idx = 0;
    for (int i = 0; i < MAX_BULLETS; i++){
        RemoteBullet *rb = &g_state.remote_bullets[i];
        if (!rb->active) continue;
        all[idx].x = rb->x;
        all[idx].y = rb->y;
        all[idx].dx = rb->dx;
        all[idx].dy = rb->dy;
        all[idx].speed = rb->speed;
        all[idx].hit = false;
        idx++;
    }
    if (count)
        *count = idx;
    return all;
}

// 检查有效子弹数并获取远程子弹数组（原始格式）
// count: 输出参数，返回活跃子弹数量
// 返回: 远程子弹数组指针
RemoteBullet* get_remote_bullets(int* count){
    int idx = 0;
    for (int i = 0; i < MAX_BULLETS; i++){
        if (g_state.remote_bullets[i].active)
            idx++;
    }
    if (count)
        *count = idx;
    return g_state.remote_bullets;
}

// 更新或插入远程子弹（upsert操作）
// id: 子弹唯一ID
// owner_id: 子弹所属玩家ID
// x,y: 子弹位置
// dx,dy: 子弹方向向量
// speed: 子弹速度
// active: 子弹是否活跃
void upsert_remote_bullet(uint32_t id, int owner_id, float x, float y, float dx, float dy, float speed, bool active){
    int free_slot = -1;
    for (int i = 0; i < MAX_BULLETS; i++){
        RemoteBullet *b = &g_state.remote_bullets[i];
        if (b->active && b->id == id){
            b->owner_id = owner_id;
            b->x = x;
            b->y = y;
            b->dx = dx;
            b->dy = dy;
            b->speed = speed;
            b->active = active;
            return;
        }
        if (!b->active && free_slot < 0)
            free_slot = i;
    }

    if (!active || free_slot < 0)
        return;

    RemoteBullet *b = &g_state.remote_bullets[free_slot];
    b->id = id;
    b->owner_id = owner_id;
    b->x = x;
    b->y = y;
    b->dx = dx;
    b->dy = dy;
    b->speed = speed;
    b->active = true;
}

// 清空所有远程子弹
void clear_remote_bullets(void){
    memset(g_state.remote_bullets, 0, sizeof(g_state.remote_bullets));
}

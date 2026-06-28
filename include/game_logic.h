#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H
#include <stdbool.h>
#include <stdint.h>
#define MAX_BULLETS 100
#define MAX_PLAYERS 5
#define MAP_WIDTH 800
#define MAP_HEIGHT 600

/**
 * @brief 本地玩家或 AI 子弹状态。
 *
 * x, y: 子弹当前位置；
 * dx, dy: 子弹速度方向向量；
 * speed: 子弹移动速度；
 * hit: 是否已经命中目标。
 */
typedef struct {
    float x, y;
    float dx, dy;
    float speed;
    bool hit;
} Bullet;

/**
 * @brief 玩家状态数据。
 *
 * x, y: 玩家当前位置；
 * hp: 当前生命值；
 * score: 累计得分；
 * kills: 本局击杀数；
 * deaths: 本局死亡数；
 * bullet_count: 当前子弹数量；
 * invisible: 是否处于隐身状态；
 * active: 玩家是否处于活动/存活状态；
 * bullets: 玩家当前持有的本地子弹数组。
 */
typedef struct {
    float x, y;
    int hp;
    int score;
    int kills;
    int deaths;
    int bullet_count;
    bool invisible;
    bool active;
    Bullet bullets[MAX_BULLETS];
} Player;

/**
 * @brief 网络输入报文的内部表示。
 *
 * id: 客户端玩家 ID；
 * x, y: 玩家当前位置；
 * mouse_x, mouse_y: 鼠标目标位置；
 * invisible: 是否请求隐身；
 * is_fire: 是否触发开火；
 * active: 输入是否有效。
 */
typedef struct {
    long long id;
    float x;
    float y;
    float mouse_x;
    float mouse_y;
    bool invisible;
    bool is_fire;
    bool active;
} NetPacket;

/**
 * @brief 远程玩家子弹状态。
 *
 * id: 子弹唯一 ID；
 * owner_id: 发射者玩家 ID；
 * x, y: 子弹当前位置；
 * dx, dy: 飞行方向；
 * speed: 子弹速度；
 * active: 子弹是否仍然存在。
 */
typedef struct {
    uint32_t id;
    int owner_id;
    float x, y;
    float dx, dy;
    float speed;
    bool active;
} RemoteBullet;

/**
 * @brief 游戏当前状态。
 *
 * local_player: 本地玩家数据；
 * remote_players: 其他玩家状态数组；
 * remote_bullets: 远程玩家子弹列表；
 * remote_count: 远程玩家数量。
 */
typedef struct {
    Player local_player;
    Player remote_players[MAX_PLAYERS];
    RemoteBullet remote_bullets[MAX_BULLETS];
    int remote_count;
} GameState;

/**
 * @brief 初始化本地游戏状态和玩家数据。
 */
void init_game();

/**
 * @brief 更新游戏逻辑，包括玩家移动和子弹模拟。
 *
 * delta_time: 本次逻辑帧间隔，单位秒。
 */
void update_game(float delta_time);

/**
 * @brief 根据用户输入移动本地玩家。
 *
 * dx, dy: 方向向量。
 */
void move_player(float dx, float dy);

/**
 * @brief 设置本地玩家隐身状态。
 *
 * enable: true 开启隐身；false 取消隐身。
 */
void set_invisible(bool enable);

/**
 * @brief 触发本地玩家射击。
 *
 * target_x, target_y: 射击目标坐标。
 */
void shoot(float target_x, float target_y);

/**
 * @brief 获取本地玩家指针。
 *
 * 返回：本地玩家状态指针。
 */
Player* get_local_player();

/**
 * @brief 获取远程玩家数组。
 *
 * count: 输出参数，返回远程玩家数量。
 * 返回：远程玩家数组指针。
 */
Player* get_remote_players(int* count);

/**
 * @brief 获取本地玩家发射的子弹数组。
 *
 * count: 输出参数，返回子弹数量。
 * 返回：子弹数组指针。
 */
Bullet* get_bullets(int* count);

/**
 * @brief 获取远程玩家子弹数组。
 *
 * count: 输出参数，返回远程子弹数量。
 * 返回：远程子弹数组指针。
 */
RemoteBullet* get_remote_bullets(int* count);

/**
 * @brief 更新或插入远程子弹状态。
 *
 * id: 子弹唯一 ID；
 * owner_id: 发射者玩家 ID；
 * x, y: 位置；
 * dx, dy: 方向；
 * speed: 速度；
 * active: 是否有效。
 */
void upsert_remote_bullet(uint32_t id, int owner_id, float x, float y, float dx, float dy, float speed, bool active);

/**
 * @brief 清除所有远程子弹状态。
 */
void clear_remote_bullets(void);

#endif

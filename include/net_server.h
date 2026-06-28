#ifndef NET_SERVER_H
#define NET_SERVER_H

#include <stdio.h>
#include <winsock2.h>
#include <stdbool.h>
#include "game_logic.h"
#include "net_proto.h"

#define MAX_CLIENTS 5
#define MAX_SOCKETS 10000
#define PORT 8888
#define MAX_SERVER_BULLETS 256
#define SERVER_TICK_DT 0.0166667f
#define SERVER_FIRE_INTERVAL 0.18f

/**
 * @brief 服务器端子弹状态。
 *
 * bullet_id: 子弹唯一 ID；
 * x, y: 子弹当前位置；
 * dx, dy: 归一化飞行方向；
 * speed: 子弹速度；
 * owner_id: 发射该子弹的玩家 ID；
 * active: 子弹是否仍在地图内且可见。
 */
typedef struct {
    uint32_t bullet_id;
    float x, y;
    float dx, dy;
    float speed;
    int owner_id;
    bool active;
} ServerBullet;

/**
 * @brief 服务器端玩家状态。
 *
 * x, y: 玩家当前位置；
 * invisible: 是否处于隐身状态；
 * active: 玩家是否在线/活跃；
 * hp: 当前生命值；
 * kills: 本局击杀数；
 * deaths: 本局死亡数；
 * fire_cd: 开火冷却时间，单位秒。
 */
typedef struct {
    float x, y;
    bool invisible;
    bool active;
    int hp;
    int kills;
    int deaths;
    float fire_cd;
} ServerPlayer;

/**
 * @brief 服务器端保存的玩家输入状态。
 *
 * seq: 客户端输入序号，用于顺序处理；
 * x, y: 客户端当前位置；
 * mouse_x, mouse_y: 鼠标目标位置；
 * fire: 是否触发开火；
 * invisible: 是否请求隐身；
 * received: 是否收到了有效输入。
 */
typedef struct {
    uint32_t seq;
    float x, y;
    float mouse_x, mouse_y;
    bool fire;
    bool invisible;
    bool received;
} ServerInputState;

/**
 * @brief 服务器端生成新子弹并加入当前子弹池。
 *
 * owner_id: 发射者玩家 ID；
 * px, py: 子弹起点位置；
 * tx, ty: 目标位置，用于计算方向。
 */
void server_shoot(int owner_id, float px, float py, float tx, float ty);

/**
 * @brief 更新所有活跃子弹的位置并处理命中/越界逻辑。
 *
 * dt: 时间步长，单位秒。
 */
void server_update_bullets(float dt);

/**
 * @brief 向所有客户端广播一条封装后的网络消息。
 *
 * sender: 排除该 socket 的发送者；0 表示广播给所有客户端；
 * msg_type: 协议消息类型；
 * payload: 消息体指针；
 * payload_len: 消息体长度。
 */
void broadcast_framed(SOCKET sender, uint8_t msg_type, const void *payload, uint16_t payload_len);

/**
 * @brief 广播当前所有玩家和子弹状态给客户端。
 */
void server_broadcast_all(void);

#endif

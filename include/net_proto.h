/*
 * net_proto.h — 客户端/服务端共用的“按帧”网络协议头
 *
 * TCP 是字节流：一次 send 的数据可能在 recv 端被拆成多段（半包），
 * 也可能多帧粘在同一次 recv（粘包）。若直接把 recv 缓冲区强转为结构体，
 * 会出现错位、丢逻辑包、甚至越界风险。
 */

#ifndef NET_PROTO_H
#define NET_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define NET_FRAME_MAGIC 0x314E5447u
#define NET_PROTO_MAX_PAYLOAD 4096

#define NET_INPUT_FIRE      0x01u
#define NET_INPUT_INVISIBLE 0x02u

/**
 * @brief 传输层消息码，用于区分不同类型的网络包。
 *
 * NET_WIRE_MSG_CLIENT_INPUT: 客户端输入消息；
 * NET_WIRE_MSG_SRV_PLAYER: 服务器发送玩家状态；
 * NET_WIRE_MSG_SRV_BULLET: 服务器发送子弹状态；
 * NET_WIRE_MSG_SRV_YOUR_ID: 服务器发送客户端自身 ID。
 */
enum NetWireMsgType {
    NET_WIRE_MSG_CLIENT_INPUT = 1,
    NET_WIRE_MSG_SRV_PLAYER = 2,
    NET_WIRE_MSG_SRV_BULLET = 3,
    NET_WIRE_MSG_SRV_YOUR_ID = 4,
};

#pragma pack(push, 1)
/**
 * @brief 网络帧头部，用于消息分帧和长度校验。
 *
 * magic: 固定魔数，用于检测帧对齐和数据有效性；
 * msg_type: 消息类型；
 * payload_len: 后续负载字节数。
 */
typedef struct {
    uint32_t magic;
    uint8_t  msg_type;
    uint16_t payload_len;
} NetFrameHeader;

/**
 * @brief 客户端向服务器发送的输入状态。
 *
 * seq: 输入帧序号，用于服务端按序处理；
 * id: 客户端玩家 ID；
 * x, y: 客户端当前位置；
 * mouse_x, mouse_y: 鼠标目标坐标；
 * buttons: 输入按键掩码，例如开火/隐身状态。
 */
typedef struct {
    uint32_t seq;
    int32_t id;
    float x;
    float y;
    float mouse_x;
    float mouse_y;
    uint8_t buttons;
} NetClientInput;

/**
 * @brief 服务器发送给客户端的玩家状态包。
 *
 * id: 玩家 ID；
 * x, y: 玩家当前位置；
 * hp: 当前生命值；
 * kills: 当前击杀数；
 * deaths: 当前死亡数；
 * active: 玩家是否在线；
 * invisible: 是否处于隐身状态。
 */
typedef struct {
    int32_t id;
    float x;
    float y;
    int16_t hp;
    int16_t kills;
    int16_t deaths;
    uint8_t active;
    uint8_t invisible;
} NetPlayerState;

/**
 * @brief 服务器发送给客户端的子弹状态包。
 *
 * bullet_id: 子弹唯一 ID；
 * owner_id: 发射者玩家 ID；
 * x, y: 子弹当前位置；
 * dx, dy: 子弹运动方向；
 * speed: 子弹速度；
 * active: 子弹是否仍然有效。
 */
typedef struct {
    uint32_t bullet_id;
    int32_t owner_id;
    float x;
    float y;
    float dx;
    float dy;
    float speed;
    uint8_t active;
} NetBulletState;
#pragma pack(pop)

#ifdef _WIN32

static inline void net_wire_set_low_latency(SOCKET s)
{
    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (int)sizeof(yes));
}

static inline int net_wire_send_all(SOCKET s, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int sent = 0;
    while (sent < len) {
        int n = send(s, p + sent, len - sent, 0);
        if (n == SOCKET_ERROR)
            return -1;
        if (n == 0)
            return -1;
        sent += n;
    }
    return 0;
}

static inline int net_wire_send_framed(SOCKET s, uint8_t msg_type, const void *payload, uint16_t payload_len)
{
    NetFrameHeader hdr;
    unsigned char frame[sizeof(NetFrameHeader) + NET_PROTO_MAX_PAYLOAD];
    int frame_len = (int)sizeof(hdr) + (int)payload_len;

    if (payload_len > NET_PROTO_MAX_PAYLOAD)
        return -1;
    hdr.magic = NET_FRAME_MAGIC;
    hdr.msg_type = msg_type;
    hdr.payload_len = payload_len;

    memcpy(frame, &hdr, sizeof(hdr));
    if (payload_len > 0 && payload != NULL)
        memcpy(frame + sizeof(hdr), payload, payload_len);

    return net_wire_send_all(s, frame, frame_len);
}

#endif /* _WIN32 */

#endif /* NET_PROTO_H */

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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#define NET_FRAME_MAGIC 0x314E5447u
#define NET_PROTO_MAX_PAYLOAD 4096

#define NET_INPUT_FIRE      0x01u
#define NET_INPUT_INVISIBLE 0x02u

enum NetWireMsgType {
    NET_WIRE_MSG_CLIENT_INPUT = 1,
    NET_WIRE_MSG_SRV_PLAYER = 2,
    NET_WIRE_MSG_SRV_BULLET = 3,
    NET_WIRE_MSG_SRV_YOUR_ID = 4,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  msg_type;
    uint16_t payload_len;
} NetFrameHeader;

typedef struct {
    uint32_t seq;
    int32_t id;
    float x;
    float y;
    float mouse_x;
    float mouse_y;
    uint8_t buttons;
} NetClientInput;

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
    if (payload_len > NET_PROTO_MAX_PAYLOAD)
        return -1;
    hdr.magic = NET_FRAME_MAGIC;
    hdr.msg_type = msg_type;
    hdr.payload_len = payload_len;
    if (net_wire_send_all(s, &hdr, (int)sizeof(hdr)) != 0)
        return -1;
    if (payload_len > 0 && payload != NULL) {
        if (net_wire_send_all(s, payload, (int)payload_len) != 0)
            return -1;
    }
    return 0;
}

#endif /* _WIN32 */

#endif /* NET_PROTO_H */

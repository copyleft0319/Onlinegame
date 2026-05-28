#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include "game_logic.h"
#include "net_proto.h"

#define BUFFER_SIZE 1024

static SOCKET g_client_socket = INVALID_SOCKET;
static int s_local_slot = -1;
static DWORD s_last_remote_log_ms[MAX_PLAYERS];
static uint32_t s_input_seq;
static unsigned char g_rx_stream[32 * 1024];
static size_t g_rx_stream_used;

extern GameState g_state;
extern void net_log(const char *fmt, ...);

int net_client_connect(const char *server_ip)
{
    WSADATA wsaData;
    struct sockaddr_in server_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup failed\n");
        fflush(stdout);
        return -1;
    }

    g_client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_client_socket == INVALID_SOCKET)
    {
        printf("socket build failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (server_addr.sin_addr.s_addr == INADDR_NONE)
    {
        printf("IP invalid\n");
        closesocket(g_client_socket);
        WSACleanup();
        return -1;
    }

    if (connect(g_client_socket, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR)
    {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(g_client_socket);
        WSACleanup();
        return -1;
    }

    u_long mode = 1;
    ioctlsocket(g_client_socket, FIONBIO, &mode);

    g_rx_stream_used = 0;
    s_local_slot = -1;
    s_input_seq = 0;
    memset(s_last_remote_log_ms, 0, sizeof(s_last_remote_log_ms));
    clear_remote_bullets();

    net_log("Client connected with server %s:8888", server_ip);
    return 0;
}

int net_client_send_player(Player *player, float mouse_x, float mouse_y, bool is_fire)
{
    if (g_client_socket == INVALID_SOCKET)
    {
        printf("Invalid socket\n");
        return -1;
    }

    NetClientInput pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.seq = ++s_input_seq;
    pkt.id = s_local_slot;
    pkt.x = player->x;
    pkt.y = player->y;
    pkt.mouse_x = mouse_x;
    pkt.mouse_y = mouse_y;
    if (is_fire)
        pkt.buttons |= NET_INPUT_FIRE;
    if (player->invisible)
        pkt.buttons |= NET_INPUT_INVISIBLE;

    if (net_wire_send_framed(g_client_socket, NET_WIRE_MSG_CLIENT_INPUT,
                             &pkt, (uint16_t)sizeof(pkt)) != 0)
        return -1;

    return 0;
}

static void apply_player_state(const NetPlayerState *np, NetPacket *out_packet, int *got_player)
{
    int id = (int)np->id;
    if (id < 0 || id >= MAX_PLAYERS)
        return;

    Player *dst = NULL;
    bool is_local = s_local_slot >= 0 && id == s_local_slot;
    if (is_local)
        dst = &g_state.local_player;
    else
        dst = &g_state.remote_players[id];

    if (!is_local) {
        dst->x = np->x;
        dst->y = np->y;
    }
    dst->hp = np->hp;
    dst->kills = np->kills;
    dst->deaths = np->deaths;
    dst->invisible = np->invisible != 0;
    dst->active = np->active != 0;

    if (out_packet) {
        memset(out_packet, 0, sizeof(*out_packet));
        out_packet->id = id;
        out_packet->x = dst->x;
        out_packet->y = dst->y;
        out_packet->invisible = dst->invisible;
        out_packet->active = dst->active;
    }
    if (got_player)
        *got_player = 1;

    if (s_local_slot >= 0 && id != s_local_slot && np->active)
    {
        DWORD t = GetTickCount();
        if (t - s_last_remote_log_ms[id] >= 400u)
        {
            s_last_remote_log_ms[id] = t;
            net_log("%d %.0f %.0f", id, np->x, np->y);
        }
    }
}

static int parse_rx_stream(NetPacket *out_packet)
{
    int got_player = 0;
    int resync_budget = 8192;

    while (g_rx_stream_used >= sizeof(NetFrameHeader))
    {
        NetFrameHeader *hdr = (NetFrameHeader *)g_rx_stream;

        if (hdr->magic != NET_FRAME_MAGIC)
        {
            memmove(g_rx_stream, g_rx_stream + 1, g_rx_stream_used - 1);
            g_rx_stream_used--;
            if (--resync_budget <= 0)
                return -4;
            continue;
        }

        if (hdr->payload_len > NET_PROTO_MAX_PAYLOAD)
            return -4;

        size_t need = sizeof(NetFrameHeader) + (size_t)hdr->payload_len;
        if (g_rx_stream_used < need)
            break;

        unsigned char *payload = g_rx_stream + sizeof(NetFrameHeader);

        if (hdr->msg_type == NET_WIRE_MSG_SRV_YOUR_ID)
        {
            if (hdr->payload_len == (uint16_t)sizeof(int32_t)) {
                int32_t wid;
                memcpy(&wid, payload, sizeof(wid));
                if (wid >= 0 && wid < MAX_PLAYERS)
                    s_local_slot = (int)wid;
            }
        }
        else if (hdr->msg_type == NET_WIRE_MSG_SRV_PLAYER)
        {
            if (hdr->payload_len == (uint16_t)sizeof(NetPlayerState)) {
                NetPlayerState np;
                memcpy(&np, payload, sizeof(np));
                apply_player_state(&np, out_packet, &got_player);
            }
        }
        else if (hdr->msg_type == NET_WIRE_MSG_SRV_BULLET)
        {
            if (hdr->payload_len == (uint16_t)sizeof(NetBulletState)) {
                NetBulletState b;
                memcpy(&b, payload, sizeof(b));
                upsert_remote_bullet(b.bullet_id, b.owner_id, b.x, b.y, b.dx, b.dy, b.speed, b.active != 0);
            }
        }

        memmove(g_rx_stream, g_rx_stream + need, g_rx_stream_used - need);
        g_rx_stream_used -= need;
    }

    if (got_player)
        return 1;
    return 0;
}

int net_client_recv_player(NetPacket *out_packet)
{
    if (g_client_socket == INVALID_SOCKET)
        return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g_client_socket, &fds);
    struct timeval tv = {0, 0};
    int res = select(0, &fds, NULL, NULL, &tv);

    if (res > 0)
    {
        if (g_rx_stream_used >= sizeof(g_rx_stream))
            return -2;

        int n = recv(g_client_socket, (char *)g_rx_stream + g_rx_stream_used,
                     (int)(sizeof(g_rx_stream) - g_rx_stream_used), 0);
        if (n == 0 || n == SOCKET_ERROR)
            return -2;

        g_rx_stream_used += (size_t)n;
    }
    else if (res < 0)
    {
        return -2;
    }

    if (out_packet)
        memset(out_packet, 0, sizeof(NetPacket));

    return parse_rx_stream(out_packet);
}

void net_client_close(void)
{
    if (g_client_socket != INVALID_SOCKET)
    {
        closesocket(g_client_socket);
        WSACleanup();
        g_client_socket = INVALID_SOCKET;
        g_rx_stream_used = 0;
        s_local_slot = -1;
        clear_remote_bullets();
    }
}

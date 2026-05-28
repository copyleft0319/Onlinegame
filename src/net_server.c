#include "net_server.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>

SOCKET id2socket[MAX_SOCKETS];
int socket2id[MAX_SOCKETS];
SOCKET clients[MAX_CLIENTS];
int client_count = 0;
ServerPlayer server_players[MAX_PLAYERS];
ServerBullet server_bullets[MAX_SERVER_BULLETS];
ServerInputState latest_inputs[MAX_PLAYERS];
int bullet_count = 0;
static uint32_t s_next_bullet_id = 1;

typedef struct {
    unsigned char data[32 * 1024];
    size_t used;
} NetServerClientRx;

static NetServerClientRx s_srv_rx[MAX_CLIENTS];

static void server_init_player(int id)
{
    server_players[id].x = 100.0f + id * 40.0f;
    server_players[id].y = 100.0f;
    server_players[id].hp = 100;
    server_players[id].kills = 0;
    server_players[id].deaths = 0;
    server_players[id].fire_cd = 0.0f;
    server_players[id].invisible = false;
    server_players[id].active = true;
    memset(&latest_inputs[id], 0, sizeof(latest_inputs[id]));
    latest_inputs[id].x = server_players[id].x;
    latest_inputs[id].y = server_players[id].y;
}

void server_shoot(int owner_id, float px, float py, float tx, float ty)
{
    if (owner_id < 0 || owner_id >= MAX_PLAYERS) return;
    if (!server_players[owner_id].active) return;

    int slot = -1;
    for (int i = 0; i < MAX_SERVER_BULLETS; i++) {
        if (!server_bullets[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    ServerBullet* b = &server_bullets[slot];
    float dx = tx - px;
    float dy = ty - py;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;

    memset(b, 0, sizeof(*b));
    b->bullet_id = s_next_bullet_id++;
    if (s_next_bullet_id == 0)
        s_next_bullet_id = 1;
    b->x = px;
    b->y = py;
    b->dx = dx / len;
    b->dy = dy / len;
    b->speed = 300.0f;
    b->owner_id = owner_id;
    b->active = true;
    if (slot >= bullet_count)
        bullet_count = slot + 1;
}

static bool hit_test(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2;
    float dy = y1 - y2;
    return (dx * dx + dy * dy) < 15 * 15;
}

void server_update_bullets(float dt)
{
    for (int i = 0; i < bullet_count; i++)
    {
        ServerBullet* b = &server_bullets[i];
        if (!b->active) continue;

        b->x += b->dx * b->speed * dt;
        b->y += b->dy * b->speed * dt;

        if (b->x < 0 || b->y < 0 || b->x > MAP_WIDTH || b->y > MAP_HEIGHT)
        {
            b->active = false;
            continue;
        }

        for (int k = 0; k < MAX_PLAYERS; k++)
        {
            ServerPlayer* target = &server_players[k];
            if (!target->active) continue;
            if (target->invisible) continue;
            if (b->owner_id == k) continue;
            if (!b->active) continue;

            if (hit_test(b->x, b->y, target->x, target->y))
            {
                b->active = false;
                target->hp -= 10;

                if (target->hp <= 0)
                {
                    target->hp = 100;
                    target->x = 50.0f + (float)(rand() % 400);
                    target->y = 50.0f + (float)(rand() % 300);
                    latest_inputs[k].x = target->x;
                    latest_inputs[k].y = target->y;
                    if (b->owner_id >= 0 && b->owner_id < MAX_PLAYERS)
                        server_players[b->owner_id].kills++;
                    target->deaths++;
                }
            }
        }
    }

    while (bullet_count > 0 && !server_bullets[bullet_count - 1].active)
        bullet_count--;
}

void broadcast_framed(SOCKET sender, uint8_t msg_type, const void *payload, uint16_t payload_len)
{
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] != sender)
            net_wire_send_framed(clients[i], msg_type, payload, payload_len);
    }
}

void server_broadcast_all(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!server_players[i].active) continue;

        NetPlayerState pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.id = i;
        pkt.x = server_players[i].x;
        pkt.y = server_players[i].y;
        pkt.hp = (int16_t)server_players[i].hp;
        pkt.kills = (int16_t)server_players[i].kills;
        pkt.deaths = (int16_t)server_players[i].deaths;
        pkt.invisible = server_players[i].invisible ? 1 : 0;
        pkt.active = 1;
        broadcast_framed(0, NET_WIRE_MSG_SRV_PLAYER, &pkt, (uint16_t)sizeof(pkt));
    }

    for (int i = 0; i < bullet_count; i++) {
        ServerBullet* b = &server_bullets[i];
        if (!b->active) continue;

        NetBulletState sb;
        memset(&sb, 0, sizeof(sb));
        sb.bullet_id = b->bullet_id;
        sb.owner_id = b->owner_id;
        sb.x = b->x;
        sb.y = b->y;
        sb.dx = b->dx;
        sb.dy = b->dy;
        sb.speed = b->speed;
        sb.active = 1;
        broadcast_framed(0, NET_WIRE_MSG_SRV_BULLET, &sb, (uint16_t)sizeof(sb));
    }
}

static void server_apply_inputs(float dt)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        ServerPlayer *p = &server_players[i];
        ServerInputState *in = &latest_inputs[i];
        if (!p->active) continue;

        if (p->fire_cd > 0.0f) {
            p->fire_cd -= dt;
            if (p->fire_cd < 0.0f)
                p->fire_cd = 0.0f;
        }

        if (in->received) {
            p->x = in->x;
            p->y = in->y;
            if (p->x < 0) p->x = 0;
            if (p->y < 0) p->y = 0;
            if (p->x > MAP_WIDTH) p->x = MAP_WIDTH;
            if (p->y > MAP_HEIGHT) p->y = MAP_HEIGHT;
            p->invisible = in->invisible;
        }

        if (in->fire && p->fire_cd <= 0.0f) {
            server_shoot(i, p->x, p->y, in->mouse_x, in->mouse_y);
            p->fire_cd = SERVER_FIRE_INTERVAL;
        }
        in->fire = false;
    }
}

static void server_tick(float dt)
{
    server_apply_inputs(dt);
    server_update_bullets(dt);
    server_broadcast_all();
}

static void server_disconnect_slot(int slot, SOCKET s, const char *reason)
{
    int die_id = socket2id[s];
    printf("%s ID = %d\n", reason, die_id);

    if (die_id >= 0 && die_id < MAX_PLAYERS) {
        NetPlayerState off;
        memset(&off, 0, sizeof(off));
        off.id = die_id;
        off.active = 0;
        broadcast_framed(s, NET_WIRE_MSG_SRV_PLAYER, &off, (uint16_t)sizeof(off));
        id2socket[die_id] = INVALID_SOCKET;
        server_players[die_id].active = false;
        memset(&latest_inputs[die_id], 0, sizeof(latest_inputs[die_id]));
    }

    if (s >= 0 && s < MAX_SOCKETS)
        socket2id[s] = -1;
    closesocket(s);

    for (int j = slot; j < client_count - 1; j++) {
        clients[j] = clients[j + 1];
        s_srv_rx[j] = s_srv_rx[j + 1];
    }
    client_count--;
}

static int server_try_parse_client_buffer(int slot, SOCKET s)
{
    NetServerClientRx *rb = &s_srv_rx[slot];
    int sync_shift_budget = 8192;

    while (rb->used >= sizeof(NetFrameHeader))
    {
        NetFrameHeader *hdr = (NetFrameHeader *)rb->data;

        if (hdr->magic != NET_FRAME_MAGIC)
        {
            memmove(rb->data, rb->data + 1, rb->used - 1);
            rb->used--;
            if (--sync_shift_budget <= 0)
                return -1;
            continue;
        }

        if (hdr->payload_len > NET_PROTO_MAX_PAYLOAD)
            return -1;

        size_t need = sizeof(NetFrameHeader) + (size_t)hdr->payload_len;
        if (rb->used < need)
            break;

        unsigned char *payload = rb->data + sizeof(NetFrameHeader);

        if (hdr->msg_type == NET_WIRE_MSG_CLIENT_INPUT &&
            hdr->payload_len == (uint16_t)sizeof(NetClientInput))
        {
            NetClientInput pkt;
            memcpy(&pkt, payload, sizeof(pkt));

            int pid = socket2id[s];
            if (pid >= 0 && pid < MAX_PLAYERS) {
                latest_inputs[pid].seq = pkt.seq;
                latest_inputs[pid].x = pkt.x;
                latest_inputs[pid].y = pkt.y;
                latest_inputs[pid].mouse_x = pkt.mouse_x;
                latest_inputs[pid].mouse_y = pkt.mouse_y;
                latest_inputs[pid].fire = (pkt.buttons & NET_INPUT_FIRE) != 0;
                latest_inputs[pid].invisible = (pkt.buttons & NET_INPUT_INVISIBLE) != 0;
                latest_inputs[pid].received = true;
            }
        }

        memmove(rb->data, rb->data + need, rb->used - need);
        rb->used -= need;
    }

    return 0;
}

int main(void)
{
    WSADATA wsa;
    SOCKET listen_sock, new_sock;
    struct sockaddr_in addr;
    fd_set fds;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        id2socket[i] = INVALID_SOCKET;
        socket2id[i] = -1;
    }
    /*
    Start WinSock2 API ,load ws2_32.dll
    MAKEWORD(2, 2) means version 2.2
    &wsa is a pointer to a WSADATA structure
    WSADATA is a structure that contains the version of the WinSock API
    and the version of the WinSock implementation
    WSAStartup is a function that initializes the WinSock API
    */
    WSAStartup(MAKEWORD(2, 2), &wsa);
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    u_long non_block = 1;
    ioctlsocket(listen_sock, FIONBIO, &non_block);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    /*
    Bind to any IP availble:
    127.0.0.1/192.168.x.x/
    10.x.x.x(Virtual Network)
    */
    addr.sin_port = htons(PORT);

    bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_sock, 5);
    printf("Start service on port :8888\n");
    //Get current time in milliseconds
    DWORD last_tick = GetTickCount();

    while (1)
    {
        FD_ZERO(&fds);
        FD_SET(listen_sock, &fds);
        SOCKET max_fd = listen_sock;

        for (int i = 0; i < client_count; i++)
        {
            FD_SET(clients[i], &fds);
            if (clients[i] > max_fd)
                max_fd = clients[i];
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        //1ms=1000us
        /*
        Wait tv ms until events trigged in 
        file_discriptor set (fds)
        >0 count of events;
        0 timeout,no events;
        -1 error
        */
        select((int)(max_fd + 1), &fds, NULL, NULL, &tv);

        //Process new client connection
        if (FD_ISSET(listen_sock, &fds))
        {
            int len = sizeof(addr);
            //Build new socket to connect with new client
            new_sock = accept(listen_sock, 
                 (struct sockaddr *)&addr, 
                 &len);
            if (new_sock != INVALID_SOCKET) {
                if (client_count >= MAX_CLIENTS) {
                    closesocket(new_sock);
                } else {
                    int found_id = -1;
                    for (int point = 0; point < MAX_CLIENTS; point++)
                    {
                        if (id2socket[point] == INVALID_SOCKET)
                        {
                            found_id = point;
                            break;
                        }
                    }

                    if (found_id < 0) {
                        closesocket(new_sock);
                    } else {
                        id2socket[found_id] = new_sock;
                        socket2id[new_sock] = found_id;
                        clients[client_count] = new_sock;
                        s_srv_rx[client_count].used = 0;
                        client_count++;
                        server_init_player(found_id);

                        int32_t assign = (int32_t)found_id;
                        net_wire_send_framed(new_sock, NET_WIRE_MSG_SRV_YOUR_ID,
                                             &assign, (uint16_t)sizeof(assign));

                        printf("New client connected! ID = %d\n", found_id);
                        fflush(stdout);
                    }
                }
            }
        }

        for (int i = 0; i < client_count; i++)
        {
            SOCKET s = clients[i];
            if (!FD_ISSET(s, &fds))
                continue;

            unsigned char chunk[2048];
            int n = recv(s, (char *)chunk, (int)sizeof(chunk), 0);
            if (n <= 0)
            {
                server_disconnect_slot(i, s, "Client disconnected!");
                i--;
                continue;
            }

            if (s_srv_rx[i].used + (size_t)n > sizeof(s_srv_rx[i].data))
            {
                server_disconnect_slot(i, s, "Client RX overflow, disconnect");
                i--;
                continue;
            }

            memcpy(s_srv_rx[i].data + s_srv_rx[i].used, chunk, (size_t)n);
            s_srv_rx[i].used += (size_t)n;

            if (server_try_parse_client_buffer(i, s) != 0)
            {
                server_disconnect_slot(i, s, "Client framing corrupt, disconnect");
                i--;
            }
        }

        DWORD now = GetTickCount();
        while (now - last_tick >= (DWORD)(SERVER_TICK_DT * 1000.0f)) {
            server_tick(SERVER_TICK_DT);
            last_tick += (DWORD)(SERVER_TICK_DT * 1000.0f);
        }
    }
}

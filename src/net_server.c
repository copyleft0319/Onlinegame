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

typedef struct
{
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
    if (owner_id < 0 || owner_id >= MAX_PLAYERS)
        return;
    if (!server_players[owner_id].active)
        return;

    int slot = -1;
    for (int i = 0; i < MAX_SERVER_BULLETS; i++)
    {
        if (!server_bullets[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    ServerBullet *b = &server_bullets[slot];
    float dx = tx - px;
    float dy = ty - py;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;

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
        ServerBullet *b = &server_bullets[i];
        if (!b->active)
            continue;

        b->x += b->dx * b->speed * dt;
        b->y += b->dy * b->speed * dt;

        if (b->x < 0 || b->y < 0 || b->x > MAP_WIDTH || b->y > MAP_HEIGHT)
        {
            b->active = false;
            continue;
        }

        for (int k = 0; k < MAX_PLAYERS; k++)
        {
            ServerPlayer *target = &server_players[k];
            if (!target->active)
                continue;
            if (target->invisible)
                continue;
            if (b->owner_id == k)
                continue;
            if (!b->active)
                continue;

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
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!server_players[i].active)
            continue;

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

    for (int i = 0; i < bullet_count; i++)
    {
        ServerBullet *b = &server_bullets[i];
        if (!b->active)
            continue;

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
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        ServerPlayer *p = &server_players[i];
        ServerInputState *in = &latest_inputs[i];
        if (!p->active)
            continue;

        if (p->fire_cd > 0.0f)
        {
            p->fire_cd -= dt;
            if (p->fire_cd < 0.0f)
                p->fire_cd = 0.0f;
        }

        if (in->received)
        {
            p->x = in->x;
            p->y = in->y;
            if (p->x < 0)
                p->x = 0;
            if (p->y < 0)
                p->y = 0;
            if (p->x > MAP_WIDTH)
                p->x = MAP_WIDTH;
            if (p->y > MAP_HEIGHT)
                p->y = MAP_HEIGHT;
            p->invisible = in->invisible;
        }

        if (in->fire && p->fire_cd <= 0.0f)
        {
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

// 服务器断开指定客户端连接的核心函数
// slot：该客户端在 clients[] 数组中的下标位置
// s：要断开的客户端套接字
// reason：断开原因（用于日志打印）
static void server_disconnect_slot(int slot, SOCKET s, const char *reason)
{
    // 1. 通过套接字 s 反查对应的玩家 ID
    int die_id = socket2id[s];

    // 2. 打印断开日志，输出原因和掉线玩家的ID
    printf("%s ID = %d\n", reason, die_id);

    // 3. 校验玩家ID是否合法（0~MAX_PLAYERS-1）
    if (die_id >= 0 && die_id < MAX_PLAYERS)
    {
        // 定义一个离线玩家消息结构体
        NetPlayerState off;
        // 清空结构体内存
        memset(&off, 0, sizeof(off));
        // 设置掉线玩家的ID
        off.id = die_id;
        // 设置状态为：不活跃（离线）
        off.active = 0;

        // 4. 向所有在线客户端广播：该玩家已离线
        // 通知所有人“这个玩家掉线了”
        broadcast_framed(s, NET_WIRE_MSG_SRV_PLAYER, &off, (uint16_t)sizeof(off));

        // 5. 清理玩家与套接字的映射关系
        // 把该ID对应的套接字标记为无效
        id2socket[die_id] = INVALID_SOCKET;
        // 把该玩家标记为不在线
        server_players[die_id].active = false;
        // 清空该玩家的输入缓存
        memset(&latest_inputs[die_id], 0, sizeof(latest_inputs[die_id]));
    }

    // 6. 清理套接字到ID的映射
    // 防止残留映射导致bug
    if (s >= 0 && s < MAX_SOCKETS)
        socket2id[s] = -1;

    // 7. 关闭套接字，释放系统资源
    closesocket(s);

    // 8. 从 clients[] 数组中删除该客户端（数组前移覆盖）
    // 从断开的位置开始，后面所有客户端往前挪一位
    for (int j = slot; j < client_count - 1; j++)
    {
        // 套接字数组前移
        clients[j] = clients[j + 1];
        // 对应的接收缓冲区也一起前移
        s_srv_rx[j] = s_srv_rx[j + 1];
    }

    // 9. 客户端总数减 1
    client_count--;
}

/* 功能：尝试解析客户端发来的一整条完整消息（处理TCP粘包/拆包）
slot：客户端在数组中的下标
s：客户端套接字
返回值：0=正常，-1=解析失败（会断开客户端）
*/
static int server_try_parse_client_buffer(int slot, SOCKET s)
{
    // 拿到当前客户端的接收缓冲区（存放收到的二进制数据）
    NetServerClientRx *rb = &s_srv_rx[slot];

    // 同步容错上限：最多允许移动8192字节寻找正确帧头，防止死循环
    int sync_shift_budget = 8192;

    // 循环解析：只要缓冲区数据 >= 帧头大小，就一直尝试解析
    while (rb->used >= sizeof(NetFrameHeader))
    {
        // 把缓冲区开头强制转换成帧头结构体，读取消息头
        NetFrameHeader *hdr = (NetFrameHeader *)rb->data;

        // ==========================================
        // 1. 校验魔法数（判断是不是合法的消息帧头）
        // ==========================================
        if (hdr->magic != NET_FRAME_MAGIC)
        {
            // 不是合法帧头 → 丢弃第一个字节，整体往前挪1字节
            memmove(rb->data, rb->data + 1, rb->used - 1);
            rb->used--; // 缓冲区长度-1

            // 容错耗尽 → 数据混乱，返回错误断开客户端
            if (--sync_shift_budget <= 0)
                return -1;

            continue; // 继续找下一个可能的帧头
        }

        // ==========================================
        // 2. 校验消息体长度是否合法（防止过大攻击）
        // ==========================================
        if (hdr->payload_len > NET_PROTO_MAX_PAYLOAD)
            return -1; // 长度超标，断开客户端

        // ==========================================
        // 3. 计算一整条完整消息需要的总长度
        // 帧头 + 消息体
        // ==========================================
        size_t need = sizeof(NetFrameHeader) + (size_t)hdr->payload_len;

        // 如果缓冲区数据不够一整条 → 退出等待下次接收
        if (rb->used < need)
            break;

        // ==========================================
        // 4. 指向消息体数据（跳过帧头）
        // ==========================================
        unsigned char *payload = rb->data + sizeof(NetFrameHeader);

        // ==========================================
        // 5. 判断消息类型：客户端操作输入（移动/鼠标/开火/隐身）
        // ==========================================
        if (hdr->msg_type == NET_WIRE_MSG_CLIENT_INPUT &&
            hdr->payload_len == (uint16_t)sizeof(NetClientInput))
        {
            // 把收到的数据复制到消息结构体
            NetClientInput pkt;
            memcpy(&pkt, payload, sizeof(pkt));

            // 通过套接字找到对应的玩家ID
            int pid = socket2id[s];

            // 玩家ID合法 → 更新服务器保存的“最新玩家输入”
            if (pid >= 0 && pid < MAX_PLAYERS)
            {
                latest_inputs[pid].seq = pkt.seq;                // 序列号
                latest_inputs[pid].x = pkt.x;                    // 移动X
                latest_inputs[pid].y = pkt.y;                    // 移动Y
                latest_inputs[pid].mouse_x = pkt.mouse_x;        // 鼠标X
                latest_inputs[pid].mouse_y = pkt.mouse_y;        // 鼠标Y
                latest_inputs[pid].fire = (pkt.buttons & NET_INPUT_FIRE) != 0; // 开火
                latest_inputs[pid].invisible = (pkt.buttons & NET_INPUT_INVISIBLE) != 0; // 隐身
                latest_inputs[pid].received = true;              // 标记已收到新输入
            }
        }

        // ==========================================
        // 6. 一条消息解析完成 → 从缓冲区移除这条数据
        // 后面的数据往前挪，缓冲区长度减少
        // ==========================================
        memmove(rb->data, rb->data + need, rb->used - need);
        rb->used -= need;
    }

    // 解析正常结束
    return 0;
}

int main(void)
{
    WSADATA wsa;
    SOCKET listen_sock, new_sock;
    struct sockaddr_in addr;
    fd_set fds;

    for (int i = 0; i < MAX_SOCKETS; i++)
    {
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
    // Get current time in milliseconds
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
        // 1ms=1000us
        /*
        Wait tv ms until events trigged in
        file_discriptor set (fds)
        >0 count of events;
        0 timeout,no events;
        -1 error
        */
        select((int)(max_fd + 1), &fds, NULL, NULL, &tv);

        // Process new client connection
        if (FD_ISSET(listen_sock, &fds))
        {
            int len = sizeof(addr);
            // Build new socket to connect with new client
            new_sock = accept(listen_sock,
                              (struct sockaddr *)&addr,
                              &len);
            if (new_sock != INVALID_SOCKET)
            {
                if (client_count >= MAX_CLIENTS)
                {
                    closesocket(new_sock);
                }
                else
                {
                    int found_id = -1;
                    for (int point = 0; point < MAX_CLIENTS; point++)
                    {
                        if (id2socket[point] == INVALID_SOCKET)
                        {
                            found_id = point;
                            break;
                        }
                    }

                    if (found_id < 0)
                    {
                        closesocket(new_sock);
                    }
                    else
                    {
                        id2socket[found_id] = new_sock;
                        socket2id[new_sock] = found_id;
                        clients[client_count] = new_sock;
                        s_srv_rx[client_count].used = 0;
                        client_count++;
                        server_init_player(found_id);
                        // 强制转化为32位4字节的有符号整数
                        int32_t assign = (int32_t)found_id;
                        net_wire_send_framed(new_sock, NET_WIRE_MSG_SRV_YOUR_ID,
                                             &assign, (uint16_t)sizeof(assign));

                        printf("New client connected! ID = %d\n", found_id);
                        fflush(stdout);
                    }
                }
            }
        }

        // 遍历所有已连接的客户端套接字
        for (int i = 0; i < client_count; i++)
        {
            // 取出当前下标对应的客户端套接字
            SOCKET s = clients[i];

            // 判断：这个套接字有没有收到数据？
            // 如果没有数据事件，直接跳过本轮循环，检查下一个客户端
            if (!FD_ISSET(s, &fds))
                continue;

            // 定义接收数据的缓冲区，一次最多接收2048字节
            unsigned char chunk[2048];

            // 调用recv从客户端读取数据
            // chunk：接收缓冲区
            // sizeof(chunk)：最大读取长度
            // n：实际读到的字节数（<=0表示断开/出错）
            int n = recv(s, (char *)chunk, (int)sizeof(chunk), 0);

            // ==============================================
            // 情况1：读取失败 或 客户端主动断开连接
            // ==============================================
            if (n <= 0)
            {
                // 调用断开函数：清理该客户端的资源、ID、映射关系
                server_disconnect_slot(i, s, "Client disconnected!");

                // 重点：因为删除了clients[i]，后面的元素会往前移
                // i-- 保证下一次循环还能检查当前位置（避免跳过一个客户端）
                i--;
                continue;
            }

            // ==============================================
            // 安全检查：接收缓冲区是否会溢出（防止数据过大撑爆内存）
            // ==============================================
            if (s_srv_rx[i].used + (size_t)n > sizeof(s_srv_rx[i].data))
            {
                // 溢出直接断开，防止非法数据攻击
                server_disconnect_slot(i, s, "Client RX overflow, disconnect");
                i--;
                continue;
            }

            // ==============================================
            // 将本次收到的数据，追加到对应客户端的接收缓冲区中
            // 作用：解决TCP粘包问题（数据可能分多次到达）
            // ==============================================
            memcpy(
                s_srv_rx[i].data + s_srv_rx[i].used, // 目标位置：当前缓冲区末尾
                chunk,                               // 本次收到的新数据
                (size_t)n                            // 新数据长度
            );

            // 更新已累计接收的数据长度
            s_srv_rx[i].used += (size_t)n;

            // ==============================================
            // 尝试解析一整条完整的客户端消息
            // 如果解析失败（数据格式错误/不完整）
            // ==============================================
            if (server_try_parse_client_buffer(i, s) != 0)
            {
                // 格式错误，直接断开客户端
                server_disconnect_slot(i, s, "Client framing corrupt, disconnect");
                i--;
            }
        }

        DWORD now = GetTickCount();
        while (now - last_tick >= (DWORD)(SERVER_TICK_DT * 1000.0f))
        {
            server_tick(SERVER_TICK_DT);
            last_tick += (DWORD)(SERVER_TICK_DT * 1000.0f);
        }
    }
}

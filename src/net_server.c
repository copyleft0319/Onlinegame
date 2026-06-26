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

/**
 * @brief 初始化指定玩家槽位的服务器端状态。
 *
 * 新客户端获得玩家 ID 后调用该函数。它会设置出生点、生命值、击杀/死亡数、
 * 开火冷却、隐身状态和活跃状态，并清空该玩家最近一次输入缓存。输入缓存
 * 中的坐标会同步到出生点，避免第一次服务器 tick 使用旧连接残留的位置。
 *
 * @param id 玩家槽位 ID，必须位于 [0, MAX_PLAYERS) 范围内。
 *
 * @sideeffect 修改 server_players[id] 和 latest_inputs[id]。
 */
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

/**
 * @brief 在服务器端生成一颗由玩家发射的子弹。
 *
 * 函数会校验发射者 ID 和发射者是否在线，然后在 server_bullets 中寻找空闲
 * 槽位。若发射点与目标点距离过近，无法计算有效方向，则不会生成子弹。
 * 成功生成时会分配递增的 bullet_id，归一化飞行方向，并更新 bullet_count
 * 以覆盖新的最高活跃槽位。
 *
 * @param owner_id 发射者玩家 ID。
 * @param px 子弹起点 X 坐标，通常为玩家当前位置。
 * @param py 子弹起点 Y 坐标，通常为玩家当前位置。
 * @param tx 瞄准目标 X 坐标，通常为鼠标所在位置。
 * @param ty 瞄准目标 Y 坐标，通常为鼠标所在位置。
 *
 * @sideeffect 可能修改 server_bullets、bullet_count 和 s_next_bullet_id。
 */
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

/**
 * @brief 判断两个碰撞点是否命中。
 *
 * 当前服务器将命中判定简化为固定半径距离检测：当两个点之间的距离小于
 * 15 像素时认为命中。函数使用平方距离比较，避免为了距离计算额外开平方。
 *
 * @param x1 第一个点的 X 坐标。
 * @param y1 第一个点的 Y 坐标。
 * @param x2 第二个点的 X 坐标。
 * @param y2 第二个点的 Y 坐标。
 *
 * @return 命中返回 true，否则返回 false。
 */
static bool hit_test(float x1, float y1, float x2, float y2)
{
    float dx = x1 - x2;
    float dy = y1 - y2;
    return (dx * dx + dy * dy) < 15 * 15;
}

/**
 * @brief 推进所有活跃子弹，并处理越界、命中和死亡复活逻辑。
 *
 * 每次服务器 tick 调用该函数。函数会根据 dt、方向和速度更新子弹位置；
 * 子弹飞出地图边界后会被标记为非活跃。对仍在地图内的子弹，会遍历所有
 * 活跃且未隐身的玩家，跳过发射者本人，并在命中后扣除目标生命值。
 * 目标死亡时会重置生命值、随机复活位置，同步 latest_inputs 中的坐标，
 * 并更新击杀者和死亡者的统计数据。
 *
 * 函数末尾会收缩 bullet_count，去掉数组尾部连续的非活跃子弹槽位，以减少
 * 后续遍历范围。
 *
 * @param dt 本次更新经过的时间，单位为秒。
 *
 * @sideeffect 修改 server_bullets、server_players、latest_inputs 和 bullet_count。
 */
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

/**
 * @brief 向除 sender 外的所有已连接客户端发送一帧网络消息。
 *
 * 该函数遍历 clients[0, client_count)，并调用 net_wire_send_framed 将指定
 * 消息类型和负载封装后发送。sender 用于排除消息来源；当 sender 传入 0 时，
 * 通常表示服务器主动广播给所有客户端。
 *
 * @param sender 需要排除的客户端 socket；传 0 表示不排除任何普通客户端。
 * @param msg_type 网络协议中的消息类型。
 * @param payload 指向消息负载的指针；payload_len 为 0 时可为 NULL。
 * @param payload_len 消息负载字节数，必须不超过协议允许的最大长度。
 *
 * @sideeffect 通过 socket 向客户端发送数据。
 */
void broadcast_framed(SOCKET sender, uint8_t msg_type, const void *payload, uint16_t payload_len)
{
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] != sender)
            net_wire_send_framed(clients[i], msg_type, payload, payload_len);
    }
}

/**
 * @brief 广播当前服务器上的全部玩家和子弹状态。
 *
 * 该函数用于周期性状态同步。它先遍历所有活跃玩家，构造 NetPlayerState 并
 * 广播；随后遍历所有活跃子弹，构造 NetBulletState 并广播。客户端可根据
 * 这些消息刷新远端玩家、血量、击杀/死亡数、隐身状态以及子弹位置。
 *
 * @sideeffect 通过 broadcast_framed 向所有客户端发送状态帧。
 */
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

/**
 * @brief 将客户端最近一次输入应用到服务器权威玩家状态。
 *
 * 函数会遍历所有活跃玩家，先推进开火冷却计时，再根据 latest_inputs 中已
 * 接收的输入更新玩家坐标和隐身状态。坐标会被限制在地图范围内。如果玩家
 * 请求开火且冷却已结束，则调用 server_shoot 生成子弹，并重置开火冷却。
 * 处理完本 tick 后会清除 fire 标志，避免同一输入被重复开火。
 *
 * @param dt 本次更新经过的时间，单位为秒，用于递减开火冷却。
 *
 * @sideeffect 修改 server_players、latest_inputs，且可能生成新子弹。
 */
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

        if (in->fire && p->fire_cd <= 0.0f && !(in->invisible)) {
            server_shoot(i, p->x, p->y, in->mouse_x, in->mouse_y);
            p->fire_cd = SERVER_FIRE_INTERVAL;
        }
        in->fire = false;
    }
}

/**
 * @brief 执行一次固定步长的服务器游戏逻辑更新。
 *
 * 一个 tick 包含三个阶段：应用玩家输入、更新子弹与碰撞、广播最新状态。
 * main 循环会按照 SERVER_TICK_DT 累积时间并多次调用该函数，以保持服务器
 * 逻辑更新频率稳定。
 *
 * @param dt 本次 tick 的固定时间步长，单位为秒。
 */
static void server_tick(float dt)
{
    server_apply_inputs(dt);
    server_update_bullets(dt);
    server_broadcast_all();
}

/**
 * @brief 断开指定客户端槽位，并清理其玩家与接收缓冲状态。
 *
 * 该函数用于客户端主动断开、recv 出错、接收缓冲溢出或协议帧损坏等场景。
 * 它会根据 socket2id 找到玩家 ID，向其他客户端广播该玩家下线状态，释放
 * id2socket/socket2id 映射，关闭 socket，并将 clients 与 s_srv_rx 数组中
 * 后续槽位前移，保持 [0, client_count) 区间连续。
 *
 * 调用者在遍历 clients 时断开当前槽位后，通常需要将循环下标减一，以便
 * 继续处理前移到当前位置的新元素。
 *
 * @param slot 该连接在 clients 和 s_srv_rx 中的槽位索引。
 * @param s 需要关闭的客户端 socket。
 * @param reason 打印到控制台的断开原因文本。
 *
 * @sideeffect 修改连接数组、接收缓冲、玩家状态、socket 映射并关闭 socket。
 */
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

/**
 * @brief 从指定客户端的接收缓冲区中解析完整协议帧。
 *
 * TCP 是字节流，recv 可能拿到半帧、粘包或多帧数据，因此每个客户端都有
 * 独立的 s_srv_rx 缓冲。该函数会在缓冲中循环查找 NetFrameHeader：若 magic
 * 不匹配，则逐字节丢弃并尝试重新同步；若负载长度非法或连续重新同步次数
 * 过多，则返回错误，调用者应断开连接；若数据不足一整帧，则保留剩余字节
 * 等待下一次 recv。
 *
 * 当前服务器只处理 NET_WIRE_MSG_CLIENT_INPUT。解析成功后会把客户端输入写入
 * 对应玩家的 latest_inputs，供后续 server_apply_inputs 使用。未知消息类型或
 * 长度不匹配的消息会被跳过，但帧仍会从缓冲中移除。
 *
 * @param slot 该连接在 s_srv_rx 中的槽位索引。
 * @param s 发送数据的客户端 socket，用于查找玩家 ID。
 *
 * @return 0 表示解析成功或等待更多数据；非 0 表示协议损坏，应断开连接。
 *
 * @sideeffect 消耗 s_srv_rx[slot] 中已解析的数据，并可能修改 latest_inputs。
 */
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

/**
 * @brief 启动并运行游戏服务器主循环。
 *
 * main 负责初始化 WinSock、创建 TCP 监听 socket、设置非阻塞模式、绑定端口
 * 并进入永久事件循环。循环中使用 select 监听新连接和已有客户端数据：
 *
 * - 有新连接时，为其分配空闲玩家 ID，初始化玩家状态，加入 clients 数组，
 *   并向该客户端发送 NET_WIRE_MSG_SRV_YOUR_ID。
 * - 有客户端数据时，将 recv 到的字节追加到对应接收缓冲，再调用
 *   server_try_parse_client_buffer 解析完整帧。
 * - 客户端断开、缓冲溢出或协议损坏时，调用 server_disconnect_slot 清理。
 * - 根据 GetTickCount 与 SERVER_TICK_DT 的累计时间执行一个或多个 server_tick，
 *   保持游戏逻辑按固定步长推进。
 *
 * 当前函数设计为长期运行，不会主动跳出 while (1)。如果后续需要优雅退出，
 * 应补充 listen_sock/clients 的关闭流程以及 WSACleanup 调用。
 *
 * @return 当前实现不会正常返回；若未来加入退出条件，按 C 约定返回进程状态码。
 */
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
                        net_wire_set_low_latency(new_sock);
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

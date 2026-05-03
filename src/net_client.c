#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>


#include "game_logic.h"

#define BUFFER_SIZE 1024

static SOCKET g_client_socket = INVALID_SOCKET;

// 初始化并连接服务器（成功返回 0，失败 -1）
int net_client_connect(const char *server_ip)
{
    WSADATA wsaData;
    struct sockaddr_in server_addr;

    // 1. 初始化 Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        fflush(stdout);
        return -1;
    }

    // 2. 创建 Socket
    g_client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_client_socket == INVALID_SOCKET) {
        printf("socket build failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    // 3. 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("IP invalid\n");
        closesocket(g_client_socket);
        WSACleanup();
        return -1;
    }

    // 4. 连接
    if (connect(g_client_socket, (struct sockaddr*)&server_addr,
    sizeof(server_addr)) == SOCKET_ERROR) 
    {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(g_client_socket);
        WSACleanup();
        return -1;
    }

    // 设为非阻塞模式（不卡游戏）
    u_long mode = 1;
    ioctlsocket(g_client_socket, FIONBIO, &mode);

    printf("Connected to server %s:8888\n", server_ip);
    return 0;
}

// 发送本地玩家数据（二进制）
// 只发必要的同步数据，体积超小
int net_client_send_player(Player *player, float mouse_x, float mouse_y, bool is_fire)
{
    if (g_client_socket == INVALID_SOCKET) {
        printf("Invalid socket\n");
        return -1;
    }
    NetPacket pkt;
    pkt.id = g_client_socket;
    pkt.x = player->x;
    pkt.y = player->y;
    pkt.mouse_x = mouse_x;
    pkt.mouse_y = mouse_y;
    pkt.is_fire = is_fire;
    pkt.invisble = player->invisible;
    // 只发送这个小数据包
    return send(g_client_socket, (char*)&pkt, sizeof(NetPacket), 0);
}

// 接收服务器广播的玩家数据
int net_client_recv_player(Player *out_packet)
{
    if (g_client_socket == INVALID_SOCKET) return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g_client_socket, &fds);

    // 非阻塞检查
    struct timeval tv = {0, 0};
    int res = select(0, &fds, NULL, NULL, &tv);

    if (res <= 0 || !FD_ISSET(g_client_socket, &fds)) {
        return 0;
    }
    // 初始化输出数据包
    memset(out_packet, 0, sizeof(NetPacket));

    // 一次性完整接收 NetPacket 结构
    int recv_len = recv(g_client_socket, (char*)out_packet, sizeof(NetPacket), 0);

    // 接收长度校验
    if (recv_len != sizeof(NetPacket)) {
        // 半包/数据错误/连接断开
        return -2;
    }
    // 成功接收完整数据包
    return 1;
}

// 关闭连接
void net_client_close()
{
    if (g_client_socket != INVALID_SOCKET) {
        closesocket(g_client_socket);
        WSACleanup();
        g_client_socket = INVALID_SOCKET;
    }
}
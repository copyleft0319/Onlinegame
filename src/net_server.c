#include <stdio.h>
#include <winsock2.h>
#include <stdbool.h>
#include "game_logic.h"

#define MAX_CLIENTS 8
#define PORT 8888


SOCKET clients[MAX_CLIENTS];
int client_count = 0;

// 广播：发给所有人 except 发送者
void broadcast(SOCKET sender, char *data, int len)
{
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] != sender)
        {
            send(clients[i], data, len, 0);
        }
    }
}

int main()
{
    WSADATA wsa;
    SOCKET listen_sock, new_sock;
    struct sockaddr_in addr;
    fd_set fds;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(listen_sock, 5);
    printf("Start service on port :8888\n");

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

        select(max_fd + 1, &fds, NULL, NULL, NULL);

        // 新客户端连接
        if (FD_ISSET(listen_sock, &fds))
        {
            int len = sizeof(addr);
            new_sock = accept(listen_sock, (struct sockaddr *)&addr, &len);
            clients[client_count++] = new_sock;
            printf("New player connected\n");
            fflush(stdout);
        }

        // 读取数据并广播
        for (int i = 0; i < client_count; i++)
        {
            SOCKET s = clients[i];
            if (FD_ISSET(s, &fds))
            {
                char buf[1024] = {0};
                int n = recv(s, buf, sizeof(buf), 0);
                if (n <= 0)
                {
                    // ✅ 客户端断开，清理数组
                    printf("[Client disconnected] socket: %lld\n", s);
                    fflush(stdout);
                    closesocket(s);

                    // 从数组移除
                    for (int j = i; j < client_count - 1; j++)
                    {
                        clients[j] = clients[j + 1];
                    }
                    client_count--;
                    i--;
                    continue;
                }
                printf("=========================================\n");
                printf("Received data from client %lld, length: %d bytes\n", s, n);
                printf("Data content:");

                if (n)
                {
                    NetPacket *pkt = (NetPacket *)buf;
                    printf("[Client %lld] (%.1f, %.1f) | mouse(%.1f, %.1f) | fire:%s | invisible:%s\n",
                           s,
                           pkt->x, pkt->y,
                           pkt->mouse_x, pkt->mouse_y,
                           pkt->is_fire ? "!" : ".",
                           pkt->invisble ? "invisible":"visible"
                        );
                    fflush(stdout);
                }
                fflush(stdout); // 强制输出
                broadcast(s, buf, n);
            }
        }
    }
}
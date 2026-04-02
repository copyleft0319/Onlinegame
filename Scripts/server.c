#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

typedef struct {
    SOCKET sock;
    struct sockaddr_in addr;
    char ip[16];
    int port;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
// 从socket接收一行（以\n结尾）
int recv_line(SOCKET sock, char *buffer, int size) {
    int i = 0;
    char c;
    int n;
    while (i < size - 1) {
        n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            return n; // 连接关闭或错误
        }
        if (c == '\n') {
            break;
        }
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return i;
}

// 向socket发送一行（自动添加\n）
int send_line(SOCKET sock, const char *message) {
    size_t len = strlen(message);
    char *buf = (char*)malloc(len + 2);
    if (!buf) return -1;
    strcpy(buf, message);
    buf[len] = '\n';
    buf[len+1] = '\0';
    size_t total = 0;
    int left = (int)(len + 1);
    int n;
    while (total < len + 1) {
        n = send(sock, buf + total, left, 0);
        if (n <= 0) {
            free(buf);
            return -1;
        }
        total += n;
        left -= n;
    }
    free(buf);
    return total;
}

// 广播消息给除发送者外的所有客户端
void broadcast(SOCKET sender_sock, const char *message, const char *sender_info) {
    char full_msg[BUFFER_SIZE + 50];
    snprintf(full_msg, sizeof(full_msg), "%s: %s", sender_info, message);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].sock != sender_sock) {
            if (send_line(clients[i].sock, full_msg) < 0) {
                // 发送失败，标记为无效（稍后移除）
                printf("发送失败，准备移除客户端 %s:%d\n", clients[i].ip, clients[i].port);
                closesocket(clients[i].sock);
                // 移除该客户端（将最后一个交换到此位置）
                clients[i] = clients[client_count - 1];
                client_count--;
                i--; // 重新检查当前位置
            }
        }
    }
}

// 移除指定索引的客户端
void remove_client(int idx) {
    printf("客户端 %s:%d 断开连接\n", clients[idx].ip, clients[idx].port);
    closesocket(clients[idx].sock);
    // 将最后一个客户端移到当前位置
    clients[idx] = clients[client_count - 1];
    client_count--;
}


int main() {
    WSADATA wsaData;
    SOCKET listen_fd, new_sock;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);
    fd_set read_fds;
    char buffer[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup失败\n");
        return 1;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) {
        printf("socket创建失败: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("绑定失败: %d\n", WSAGetLastError());
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }

    if (listen(listen_fd, 3) == SOCKET_ERROR) {
        printf("监听失败: %d\n", WSAGetLastError());
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }

    printf("聊天室服务端启动，监听端口 %d\n", PORT);
    printf("等待客户端连接...\n");

    client_count = 0;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        SOCKET max_fd = listen_fd;

        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i].sock, &read_fds);
            if (clients[i].sock > max_fd) max_fd = clients[i].sock;
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            printf("select错误: %d\n", WSAGetLastError());
            continue;
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            new_sock = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
            if (new_sock == INVALID_SOCKET) {
                printf("accept失败: %d\n", WSAGetLastError());
                continue;
            }

            if (client_count >= MAX_CLIENTS) {
                printf("已达到最大客户端数量，拒绝连接\n");
                closesocket(new_sock);
                continue;
            }

            char ip_str[16];
            char *ip = inet_ntoa(client_addr.sin_addr);
            strcpy(ip_str, ip);
            int port = ntohs(client_addr.sin_port);

            clients[client_count].sock = new_sock;
            clients[client_count].addr = client_addr;
            strcpy(clients[client_count].ip, ip_str);
            clients[client_count].port = port;
            client_count++;

            printf("新客户端加入: %s:%d\n", ip_str, port);

            char welcome[BUFFER_SIZE];
            snprintf(welcome, sizeof(welcome), "欢迎加入聊天室！当前在线人数: %d", client_count);
            send_line(new_sock, welcome);
        }

        for (int i = 0; i < client_count; i++) {
            SOCKET sd = clients[i].sock;
            if (FD_ISSET(sd, &read_fds)) {
                int n = recv_line(sd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    remove_client(i);
                    i--;
                } else {
                    char sender_info[BUFFER_SIZE];
                    snprintf(sender_info, sizeof(sender_info), "%s:%d", clients[i].ip, clients[i].port);
                    printf("收到来自 %s 的消息: %s\n", sender_info, buffer);
                    broadcast(sd, buffer, sender_info);
                }
            }
        }
    }

    // 清理代码...
    for (int i = 0; i < client_count; i++) closesocket(clients[i].sock);
    closesocket(listen_fd);
    WSACleanup();
    return 0;
}
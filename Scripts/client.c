#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 1024

int recv_line(SOCKET sock, char *buffer, int size) {
    int i = 0;
    char c;
    int n;
    while (i < size - 1) {
        n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            return n;
        }
        if (c == '\n') {
            break;
        }
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return i;
}

// 发送一行（自动加\n）
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

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server_addr;
    fd_set read_fds;
    char buffer[BUFFER_SIZE];

    if (argc != 2) {
        fprintf(stderr, "<服务器IP>:%s\n", argv[1]);
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup失败\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("socket创建失败: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("无效的IP地址\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("连接失败: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("已连接到聊天室服务器 %s:8888\n", argv[1]);
    printf("输入消息并回车发送，输入 'quit' 退出\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET((unsigned int)0, &read_fds);
        FD_SET(sock, &read_fds);
        SOCKET max_fd = (sock > 0) ? sock : 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            printf("select错误: %d\n", WSAGetLastError());
            break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            int n = recv_line(sock, buffer, BUFFER_SIZE);
            if (n <= 0) {
                printf("服务器断开连接\n");
                break;
            }
            printf("\r%s\n> ", buffer);
            fflush(stdout);
        }

        if (FD_ISSET(0, &read_fds)) {
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = '\0';
            if (strcmp(buffer, "quit") == 0) {
                printf("退出聊天室\n");
                break;
            }
            if (strlen(buffer) > 0) {
                if (send_line(sock, buffer) < 0) {
                    printf("发送失败，连接可能已断开\n");
                    break;
                }
            }
            printf("> ");
            fflush(stdout);
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}


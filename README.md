# 系统需求：
本项目旨在构建多人联机的娱乐射击对战系统。
# 系统约束：
1.必须预装vnt虚拟内网组件工具，装箱效果一般。
2.目前仅仅支持最多5人玩家。且系统并发性能弱。

# 系统概述
## 服务器端：
1.创建监听套接字，接受加入房间的玩家数据并转发给房间中的其他玩家。
2.计算子弹位置并与玩家位置比较，统一击杀检测。
## 客户端
1.接收服务器数据包，渲染数据包中其他玩家的信息和本地玩家信息。
2.检测键盘事件，`WASD`移动位置，`SPACE`实现隐身，鼠标左键发射子弹。
3.打包数据发送给服务器。

# 开发中遇到的问题
## 1 客户端收到一堆数据包，怎么分类？玩家断联id又该如何分配？
在代码层面，对于每个连接，只能获取套接字值（SOCKET 本质INT）来区别不同的数据包分别来自哪台主机。为解决数据包归属问题，服务器端设计了socket2id[] 和id2socket[]列表数据结构，实现<SOCKET,ID>的双向映射，同时也考虑了ID动态分配的问题（新加入房间的玩家分配什么ID,退出房间的玩家数据怎么去除）

```C
//连接建立时：
    int found_id = -1;
    for (int loop = 0; loop < MAX_CLIENTS; loop++)
    {
        point = (point + 1) % MAX_CLIENTS;  // 循环+1
        if (id2socket[point] == INVALID_SOCKET)
        {
            found_id = point;
            break;
        }
    }
    // 分配成功
    id2socket[found_id] = new_sock;
    socket2id[new_sock] = found_id;
    // 添加到客户端列表
    clients[client_count++] = new_sock;
    printf("New client connected! ID = %d\n", found_id);
    fflush(stdout);
//客户端：
    g_state.remote_players[id].x = out_packet->x;
    g_state.remote_players[id].y = out_packet->y;
    g_state.remote_players[id].invisible = out_packet->invisible;
//断联时
    id2socket[die_id] = INVALID_SOCKET;
    socket2id[s] = -1;
    closesocket(s);
// 从客户端数组移除
for (int j = i; j < client_count - 1; j++)
    clients[j] = clients[j + 1];
    client_count--;
    i--;
    continue;
```
## 2 数据包粘包问题。
TCP是流式协议，一开始不做缓冲区截断读取，收到的数据包和读取的数据包都不完整，导致玩家出现位置频闪

## 3 何时结算游戏状态的问题
摒弃了收齐信息再发的方案，选用server_tick方案，服务器每30ms tick一下，结算游戏全局状态。




# 1修改主循环逻辑把远程玩家的数据解析到本地绘制
## 1.1客户端收到一堆数据包，怎么分类？玩家断联id又该如何分配？
server端建立一个数组，新连接一旦建立，就建立一个<id,socket>对，数据包加入id值让客户端更新对应id的玩家数据
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
# 2 修改server逻辑完成击杀检测
数据包格式:
typedef struct {
    long long id;
    float x;         
    float y;
    float mouse_x;    // 鼠标坐标（算子弹方向）
    float mouse_y;
    bool invisible;    // 是否隐身
    bool is_fire;
    bool active;    
} NetPacket;
子弹绘制问题上，client端：
if (g_state.remote_players[i]->is_fire)
    shoot()
子弹轨迹都是直线，问题是怎么计算能不能打中几秒之后的玩家，


客户端（游戏端）
玩家开枪时，只向主机发送开枪事件：包含自己的 ID、枪口朝向 / 子弹初始坐标；
收到主机返回的「子弹位置、命中结果」后，只做画面渲染：画子弹、播放命中特效、显示击杀提示；

主机（服务端）
接收所有客户端的开枪、移动数据以及是否隐身；
统一计算：子弹的飞行轨迹、实时位置、和玩家碰撞、命中判定、血量扣除、击杀归属；
把「子弹位置、命中玩家 ID、击杀者 ID」广播给所有客户端，统一游戏状态。




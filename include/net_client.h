#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include "game_logic.h"

// 连接到游戏服务器
// server_ip: 服务器 IP 地址字符串
// 返回值: 0 表示成功，非 0 表示失败
int net_client_connect(const char *server_ip);

// 发送玩家状态到服务器
// player: 指向玩家数据结构的指针
// mouse_x: 鼠标 X 坐标，用于瞄准或方向
// mouse_y: 鼠标 Y 坐标，用于瞄准或方向
// is_fire: 是否触发射击动作
// 返回值: 发送成功返回 0，失败返回非 0
int net_client_send_player(Player *player, float mouse_x, float mouse_y, bool is_fire);

// 接收来自服务器的玩家数据包
// out_packet: 输出参数，接收解析后的数据包内容
// 返回值: 接收成功返回 0，失败返回非 0
int net_client_recv_player(NetPacket *out_packet);

// 关闭客户端连接并释放资源
void net_client_close(void);

#endif
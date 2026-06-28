#ifndef GAME_UI_H
#define GAME_UI_H

#include <stdbool.h>
#include <windows.h>

/**
 * @brief 游戏 UI 逻辑目标帧率。
 */
#define GAME_UI_FPS 60

/**
 * @brief 初始化游戏 UI 系统。
 *
 * 返回：初始化成功返回 true，失败返回 false。
 */
bool game_ui_init(void);

/**
 * @brief 处理窗口消息队列。
 *
 * 返回：继续运行返回 true，退出请求返回 false。
 */
bool game_ui_pump_messages(void);

/**
 * @brief 处理用户输入并更新输入状态。
 *
 * dt: 本次逻辑帧时间，单位秒。
 */
void game_ui_process_input(float dt);

/**
 * @brief 根据 UI 交互更新本地玩家状态。
 *
 * dt: 本次逻辑帧时间，单位秒。
 */
void game_ui_update_local_player(float dt);

/**
 * @brief 渲染当前帧 UI 和游戏场景。
 */
void game_ui_render(void);

/**
 * @brief 关闭 UI 子系统并释放资源。
 */
void game_ui_shutdown(void);

/**
 * @brief 获取当前鼠标 X 坐标。
 *
 * 返回：鼠标在窗口中的 X 坐标。
 */
float game_ui_mouse_x(void);

/**
 * @brief 获取当前鼠标 Y 坐标。
 *
 * 返回：鼠标在窗口中的 Y 坐标。
 */
float game_ui_mouse_y(void);

/**
 * @brief 查询并消费一次开火事件。
 *
 * 返回：若本帧有开火输入返回 true，否则 false。
 */
bool game_ui_consume_fire_event(void);

/**
 * @brief 输出网络/调试日志。
 *
 * fmt: printf 风格格式字符串。
 */
void net_log(const char *fmt, ...);

#endif

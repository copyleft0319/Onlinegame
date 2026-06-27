#include "game_ui.h"
#include "game_logic.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NET_LOG_MAX 10
#define NET_LOG_LEN 128

static HINSTANCE g_hInst;
static HWND g_hWnd;
static bool g_keys[256];
static POINT g_mouse;
static bool g_mouse_click;
static HDC g_memDC = NULL;
static HBITMAP g_hBitmap = NULL;
static float target_x, target_y;
static bool fire_event;

static char net_logs[NET_LOG_MAX][NET_LOG_LEN] = {0};
static int net_log_count = 0;

// 记录网络日志（循环缓冲区，最多保留NET_LOG_MAX条）
// fmt: 格式化字符串（printf风格）
// ...: 可变参数列表
void net_log(const char *fmt, ...)
{
    for (int i = 0; i < NET_LOG_MAX - 1; i++)
        strcpy(net_logs[i], net_logs[i + 1]);

    va_list args;
    va_start(args, fmt);
    vsnprintf(net_logs[NET_LOG_MAX - 1], NET_LOG_LEN, fmt, args);
    va_end(args);

    if (net_log_count < NET_LOG_MAX)
        net_log_count++;
}

// Windows窗口消息处理回调函数
// 处理键盘、鼠标事件，记录到全局变量
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_KEYDOWN:
        if (wParam < 256)
            g_keys[wParam] = true;
        break;
    case WM_KEYUP:
        if (wParam < 256)
            g_keys[wParam] = false;
        break;
    case WM_MOUSEMOVE:
        g_mouse.x = LOWORD(lParam);
        g_mouse.y = HIWORD(lParam);
        break;
    case WM_LBUTTONDOWN:
        g_mouse_click = true;
        break;
    case WM_LBUTTONUP:
        g_mouse_click = false;
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 初始化游戏UI（创建窗口、双缓冲、设置初始状态）
// 返回: true成功，false失败
bool game_ui_init(void)
{
    g_hInst = GetModuleHandle(NULL);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = "SimpleGame";
    RegisterClass(&wc);

    g_hWnd = CreateWindow("SimpleGame", "Demo Game", WS_OVERLAPPEDWINDOW,
                          100, 100, MAP_WIDTH, MAP_HEIGHT, NULL, NULL, g_hInst, NULL);
    if (!g_hWnd)
        return false;

    ShowWindow(g_hWnd, SW_SHOW);

    HDC hdc = GetDC(g_hWnd);
    g_memDC = CreateCompatibleDC(hdc);
    g_hBitmap = CreateCompatibleBitmap(hdc, MAP_WIDTH, MAP_HEIGHT);
    SelectObject(g_memDC, g_hBitmap);
    ReleaseDC(g_hWnd, hdc);

    Player *local = get_local_player();
    target_x = local->x;
    target_y = local->y;
    fire_event = false;
    return true;
}

// 处理Windows消息队列（非阻塞，防止界面卡死）
// 返回: true继续运行，false收到WM_QUIT退出
bool game_ui_pump_messages(void)
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

// 处理键盘和鼠标输入，更新目标位置
// dt: 帧间隔时间（秒）
void game_ui_process_input(float dt)
{
    float dx = 0, dy = 0;
    if (g_keys['W']) dy -= 1;
    if (g_keys['S']) dy += 1;
    if (g_keys['A']) dx -= 1;
    if (g_keys['D']) dx += 1;

    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0)
    {
        dx /= len;
        dy /= len;
    }

    Player *local = get_local_player();
    float speed = 500.0f * dt;
    target_x = local->x + dx * speed;
    target_y = local->y + dy * speed;

    if (target_x < 0) target_x = 0;
    if (target_y < 0) target_y = 0;
    if (target_x > MAP_WIDTH) target_x = MAP_WIDTH;
    if (target_y > MAP_HEIGHT) target_y = MAP_HEIGHT;

    set_invisible(g_keys[VK_SPACE]);

    if (g_mouse_click)
    {
        fire_event = true;
        g_mouse_click = false;
    }
}

// 平滑更新本地玩家位置（插值到目标位置）
// dt: 帧间隔时间（秒）
void game_ui_update_local_player(float dt)
{
    (void)dt;
    Player *local = get_local_player();
    float t = 0.2f;
    local->x += (target_x - local->x) * t;
    local->y += (target_y - local->y) * t;
}

// 绘制游戏场景（背景、子弹、玩家、HUD信息）
static void draw_scene(void)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(g_memDC, &rc, blackBrush);
    DeleteObject(blackBrush);

    HPEN whitePen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HPEN oldPen = (HPEN)SelectObject(g_memDC, whitePen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(g_memDC, nullBrush);

    int bullet_count;
    Bullet *bullets = get_bullets(&bullet_count);
    for (int i = 0; i < bullet_count; i++)
    {
        Ellipse(g_memDC, bullets[i].x - 3, bullets[i].y - 3,
                bullets[i].x + 3, bullets[i].y + 3);
    }

    int remote_count;
    Player *remotes = get_remote_players(&remote_count);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        Player *p = &remotes[i];
        if (p->active && !p->invisible)
            Ellipse(g_memDC, p->x - 10, p->y - 10, p->x + 10, p->y + 10);
    }

    Player *local = get_local_player();
    if (!local->invisible)
        Ellipse(g_memDC, local->x - 10, local->y - 10, local->x + 10, local->y + 10);

    SelectObject(g_memDC, oldPen);
    SelectObject(g_memDC, oldBrush);
    DeleteObject(whitePen);

    char buf[128];
    sprintf(buf, "HP:%d K:%d D:%d Invis:%s", local->hp, local->kills, local->deaths, local->invisible ? "ON" : "OFF");
    TextOut(g_memDC, 10, 10, buf, strlen(buf));

    HFONT hSmallFont = CreateFont(14, 7, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT oldFont = (HFONT)SelectObject(g_memDC, hSmallFont);
    SetTextColor(g_memDC, RGB(0, 255, 0));
    SetBkMode(g_memDC, TRANSPARENT);

    int log_y = 30;
    int show_lines = min(net_log_count, NET_LOG_MAX);
    for (int i = 0; i < show_lines; i++)
    {
        TextOutA(g_memDC, 10, log_y, net_logs[i], strlen(net_logs[i]));
        log_y += 16;
    }

    SelectObject(g_memDC, oldFont);
    DeleteObject(hSmallFont);
}

// 将双缓冲内容渲染到窗口
void game_ui_render(void)
{
    if (!g_hWnd || !g_memDC)
        return;

    draw_scene();
    HDC hdc = GetDC(g_hWnd);
    BitBlt(hdc, 0, 0, MAP_WIDTH, MAP_HEIGHT, g_memDC, 0, 0, SRCCOPY);
    ReleaseDC(g_hWnd, hdc);
}

// 关闭UI，释放窗口资源
void game_ui_shutdown(void)
{
    if (g_hBitmap)
    {
        DeleteObject(g_hBitmap);
        g_hBitmap = NULL;
    }
    if (g_memDC)
    {
        DeleteDC(g_memDC);
        g_memDC = NULL;
    }
    if (g_hWnd)
    {
        DestroyWindow(g_hWnd);
        g_hWnd = NULL;
    }
}

// 获取当前鼠标X坐标
float game_ui_mouse_x(void)
{
    return (float)g_mouse.x;
}

// 获取当前鼠标Y坐标
float game_ui_mouse_y(void)
{
    return (float)g_mouse.y;
}

// 消费一次开火事件（获取后重置状态，防止连续触发）
// 返回: true表示发生了开火事件
bool game_ui_consume_fire_event(void)
{
    bool fired = fire_event;
    fire_event = false;
    return fired;
}

#include "game_ui.h"
#include "game_logic.h"
#include "net_client.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

static HINSTANCE g_hInst;
static HWND g_hWnd;
static bool g_keys[256];
static POINT g_mouse;
static bool g_mouse_click;

#define FPS 60

// 双缓冲
static HDC g_memDC = NULL;
static HBITMAP g_hBitmap = NULL;

// 本地玩家平滑移动目标
static float target_x, target_y;
static bool fire_event;
// 设置网络刷新计时器
static float network_send_timer = 0.0f;
// 远程玩家状态
typedef struct
{
    float dx, dy;      // 移动方向向量
    float speed;       // 像素/秒
    float invis_timer; // 隐身切换计时
    bool alive;        // 是否存活
} RemoteState;
static RemoteState remote_state[MAX_PLAYERS - 1];

// ------------------ 窗口事件 ------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_KEYDOWN:
        g_keys[wParam] = true;
        break;
    case WM_KEYUP:
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

// ------------------ 输入处理 ------------------
static void process_input(float dt)
{
    float dx = 0, dy = 0;
    if (g_keys['W'])
        dy -= 1;
    if (g_keys['S'])
        dy += 1;
    if (g_keys['A'])
        dx -= 1;
    if (g_keys['D'])
        dx += 1;

    // 归一化方向
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0)
    {
        dx /= len;
        dy /= len;
    }

    float speed = 500 * dt; // 本地玩家速度
    Player *local = get_local_player();
    target_x = local->x + dx * speed;
    target_y = local->y + dy * speed;

    // 边界限制
    if (target_x < 0)
        target_x = 0;
    if (target_y < 0)
        target_y = 0;
    if (target_x > MAP_WIDTH)
        target_x = MAP_WIDTH;
    if (target_y > MAP_HEIGHT)
        target_y = MAP_HEIGHT;

    set_invisible(g_keys[VK_SHIFT]);

    if (g_mouse_click)
    {
        shoot((float)g_mouse.x, (float)g_mouse.y);
        fire_event = g_mouse_click;
        g_mouse_click = false; // 单发
    }
}

// ------------------ 更新本地玩家 ------------------
static void update_local_player(float dt)
{
    Player *local = get_local_player();
    float t = 0.2f; // 平滑系数
    local->x += (target_x - local->x) * t;
    local->y += (target_y - local->y) * t;
}

// ------------------ 更新远程玩家 ------------------
static void update_remote_players(float dt)
{
    int remote_count;
    Player *remotes = get_remote_players(&remote_count);
    for (int i = 0; i < remote_count; i++)
    {
        RemoteState *rs = &remote_state[i];
        Player *p = &remotes[i];

        // 检测死亡
        if (p->hp <= 0)
        {
            // 玩家死亡才刷新位置
            p->hp = 100;
            p->x = rand() % MAP_WIDTH;
            p->y = rand() % MAP_HEIGHT;

            rs->alive = true;

            // 随机移动方向
            rs->dx = ((float)(rand() % 200) - 100) / 100.0f;
            rs->dy = ((float)(rand() % 200) - 100) / 100.0f;
            float len = sqrtf(rs->dx * rs->dx + rs->dy * rs->dy);
            if (len > 0)
            {
                rs->dx /= len;
                rs->dy /= len;
            }
            rs->speed = 150 + rand() % 50; // 150~200
            rs->invis_timer = 0;
            p->invisible = false;
        }

        if (!rs->alive)
            continue;

        // 移动
        p->x += rs->dx * rs->speed * dt;
        p->y += rs->dy * rs->speed * dt;

        // 边界反弹
        if (p->x < 0)
        {
            p->x = 0;
            rs->dx *= -1;
        }
        if (p->y < 0)
        {
            p->y = 0;
            rs->dy *= -1;
        }
        if (p->x > MAP_WIDTH)
        {
            p->x = MAP_WIDTH;
            rs->dx *= -1;
        }
        if (p->y > MAP_HEIGHT)
        {
            p->y = MAP_HEIGHT;
            rs->dy *= -1;
        }

        // 随机隐身切换
        rs->invis_timer -= dt;
        if (rs->invis_timer <= 0)
        {
            p->invisible = (rand() % 2 == 0);
            rs->invis_timer = 1.0f + ((float)(rand() % 100)) / 100.0f; // 1~2秒切换
        }
    }
}

// ------------------ 绘制函数 ------------------
static void draw(HDC hdc)
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    FillRect(g_memDC, &rc, (HBRUSH)(COLOR_WINDOW + 1));

    // 绘制子弹
    int bullet_count;
    Bullet *bullets = get_bullets(&bullet_count);
    for (int i = 0; i < bullet_count; i++)
    {
        Ellipse(g_memDC, bullets[i].x - 3, bullets[i].y - 3, bullets[i].x + 3, bullets[i].y + 3);
    }

    // 绘制远程玩家
    int remote_count;
    Player *remotes = get_remote_players(&remote_count);
    for (int i = 0; i < remote_count; i++)
    {
        Player *p = &remotes[i];
        if (p->invisible)
        {
            HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 0, 255));
            HPEN oldPen = (HPEN)SelectObject(g_memDC, pen);
            Ellipse(g_memDC, p->x - 10, p->y - 10, p->x + 10, p->y + 10);
            SelectObject(g_memDC, oldPen);
            DeleteObject(pen);
        }
        else
        {
            Ellipse(g_memDC, p->x - 10, p->y - 10, p->x + 10, p->y + 10);
        }
    }

    // 绘制本地玩家
    Player *local = get_local_player();
    if (local->invisible)
    {
        HBRUSH hBrush = CreateSolidBrush(RGB(180, 180, 180));
        HBRUSH oldBrush = (HBRUSH)SelectObject(g_memDC, hBrush);
        HPEN hPen = CreatePen(PS_DASH, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(g_memDC, hPen);
        Ellipse(g_memDC, local->x - 10, local->y - 10, local->x + 10, local->y + 10);
        SelectObject(g_memDC, oldBrush);
        SelectObject(g_memDC, oldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);
    }
    else
    {
        Ellipse(g_memDC, local->x - 10, local->y - 10, local->x + 10, local->y + 10);
    }

    // 绘制UI
    char buf[128];
    sprintf(buf, "HP:%d K:%d D:%d Invis:%s", local->hp, local->kills, local->deaths, local->invisible ? "ON" : "OFF");
    TextOut(g_memDC, 10, 10, buf, strlen(buf));

    // 内存DC绘制到窗口
    BitBlt(hdc, 0, 0, MAP_WIDTH, MAP_HEIGHT, g_memDC, 0, 0, SRCCOPY);
}

// ------------------ 主循环 ------------------
void run_game_ui()
{
    srand((unsigned int)time(NULL));

    g_hInst = GetModuleHandle(NULL);
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = "SimpleGame";
    RegisterClass(&wc);

    g_hWnd = CreateWindow("SimpleGame", "Demo Game", WS_OVERLAPPEDWINDOW,
                          100, 100, MAP_WIDTH, MAP_HEIGHT, NULL, NULL, g_hInst, NULL);
    ShowWindow(g_hWnd, SW_SHOW);

    init_game();
    Player *local = get_local_player();
    target_x = local->x;
    target_y = local->y;

    // 初始化远程玩家
    int remote_count;
    get_remote_players(&remote_count);
    for (int i = 0; i < remote_count; i++)
    {
        remote_state[i].dx = ((float)(rand() % 200) - 100) / 100.0f;
        remote_state[i].dy = ((float)(rand() % 200) - 100) / 100.0f;
        float len = sqrtf(remote_state[i].dx * remote_state[i].dx + remote_state[i].dy * remote_state[i].dy);
        if (len > 0)
        {
            remote_state[i].dx /= len;
            remote_state[i].dy /= len;
        }
        remote_state[i].speed = 150 + rand() % 50;
        remote_state[i].invis_timer = 0;
        remote_state[i].alive = true;
    }

    // 创建双缓冲
    HDC hdc = GetDC(g_hWnd);
    g_memDC = CreateCompatibleDC(hdc);
    g_hBitmap = CreateCompatibleBitmap(hdc, MAP_WIDTH, MAP_HEIGHT);
    SelectObject(g_memDC, g_hBitmap);
    ReleaseDC(g_hWnd, hdc);

    MSG msg;
    DWORD last = GetTickCount();
    net_client_connect("127.0.0.1");
    while (1)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto cleanup;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD now = GetTickCount();
        float dt = (now - last) / 1000.0f;
        last = now;

        process_input(dt);
        update_local_player(dt);

        network_send_timer += dt;
        if (network_send_timer >= 0.033f)
        {   
            // 每 33ms 发送一次 = 30帧/s
            // 发送本地玩家数据包给服务器
            net_client_send_player(get_local_player(),
            g_mouse.x, g_mouse.y,fire_event);
            if (fire_event) fire_event = false;
            network_send_timer = 0.0f; // 重置计时器
        }

        update_remote_players(dt);
        update_game(dt);

        hdc = GetDC(g_hWnd);
        draw(hdc);
        ReleaseDC(g_hWnd, hdc);

        Sleep(1000 / FPS);
    }

cleanup:
    DeleteObject(g_hBitmap);
    DeleteDC(g_memDC);
}
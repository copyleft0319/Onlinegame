#include "game_ui.h"
#include "game_logic.h"
#include "net_client.h"
#include <windows.h>
#include <stdio.h>

static float frame_delta_seconds(LARGE_INTEGER *last, LARGE_INTEGER freq)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - last->QuadPart) / (float)freq.QuadPart;
    *last = now;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / (float)GAME_UI_FPS;
    return dt;
}

static void sleep_to_frame_end(LARGE_INTEGER frame_start, LARGE_INTEGER freq)
{
    LARGE_INTEGER after;
    QueryPerformanceCounter(&after);
    double elapsed = (double)(after.QuadPart - frame_start.QuadPart) / (double)freq.QuadPart;
    double target = 1.0 / (double)GAME_UI_FPS;
    double remain = target - elapsed;

    if (remain > 0.002)
    {
        DWORD ms = (DWORD)(remain * 1000.0);
        if (ms > 0)
            Sleep(ms);
    }
    else if (remain > 0.0)
    {
        Sleep(0);
    }
}

int main(int argc, char *argv[])
{
    const char *server_ip = "127.0.0.1";
    if (argc > 1)
        server_ip = argv[1];

    init_game();
    if (!game_ui_init())
        return 1;

    if (net_client_connect(server_ip) != 0)
    {
        printf("Failed to connect to server %s\n", server_ip);
        game_ui_shutdown();
        return 1;
    }

    LARGE_INTEGER qpc_freq, qpc_last;
    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_last);

    float network_send_timer = 0.0f;
    bool running = true;

    while (running)
    {
        LARGE_INTEGER frame_start;
        QueryPerformanceCounter(&frame_start);

        running = game_ui_pump_messages();
        float dt = frame_delta_seconds(&qpc_last, qpc_freq);

        game_ui_process_input(dt);
        game_ui_update_local_player(dt);

        network_send_timer += dt;
        if (network_send_timer >= 0.033f)
        {
            bool fire_event = game_ui_consume_fire_event();
            net_client_send_player(get_local_player(),
                                   game_ui_mouse_x(), game_ui_mouse_y(), fire_event);
            network_send_timer = 0.0f;
        }

        NetPacket pkt;
        net_client_recv_player(&pkt);
        update_game(dt);
        game_ui_render();

        sleep_to_frame_end(frame_start, qpc_freq);
    }

    net_client_close();
    game_ui_shutdown();
    return 0;
}

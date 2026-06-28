#include "server_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define SERVER_LOG_DIR "C:\\Users\\MSI\\Desktop\\Onlinegame\\logs"
#define SERVER_LOG_SCOREBOARD SERVER_LOG_DIR "\\last_match_scoreboard.txt"

typedef struct {
    int id;
    int kills;
    int deaths;
    int active;
} ServerLogRow;

static ServerPlayer *g_players;
static int g_player_count;
static int g_saved;

static void server_log_now(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static double server_log_kd(int kills, int deaths)
{
    return deaths == 0 ? (double)kills : (double)kills / (double)deaths;
}

static void server_log_kd_bar(char *buf, size_t size, int kills, int deaths)
{
    int total = kills + deaths;
    int k_blocks = total == 0 ? 0 : (kills * 20) / total;

    if (size < 23)
        return;

    buf[0] = '[';
    for (int i = 0; i < 20; i++)
        buf[i + 1] = i < k_blocks ? '#' : '-';
    buf[21] = ']';
    buf[22] = '\0';
}

static int server_log_collect_rows(ServerLogRow *rows, int max_rows)
{
    int count = 0;
    if (!g_players)
        return 0;

    for (int i = 0; i < g_player_count && count < max_rows; i++) {
        ServerPlayer *p = &g_players[i];
        if (!p->active && p->kills == 0 && p->deaths == 0)
            continue;

        rows[count].id = i;
        rows[count].kills = p->kills;
        rows[count].deaths = p->deaths;
        rows[count].active = p->active ? 1 : 0;
        count++;
    }

    return count;
}

static void server_log_print_line(FILE *fp, const char *text)
{
    fputs(text, fp);
    fputc('\n', fp);
}

static void server_log_emit_scoreboard(FILE *fp,
                                       const char *reason,
                                       const char *now,
                                       const ServerLogRow *rows,
                                       int row_count)
{
    char line[160];

    server_log_print_line(fp, "+======================================================================+");
    server_log_print_line(fp, "|                    ONLINEGAME SERVER K/D BOARD                       |");
    server_log_print_line(fp, "+======================================================================+");
    sprintf(line, "| saved_at: %-19s | reason: %-28s |", now, reason);
    server_log_print_line(fp, line);
    server_log_print_line(fp, "+----+----------+-------+--------+--------+------------------------+");
    server_log_print_line(fp, "| ID | STATUS   | KILLS | DEATHS | K/D    | KILL PRESSURE          |");
    server_log_print_line(fp, "+----+----------+-------+--------+--------+------------------------+");

    if (row_count == 0) {
        server_log_print_line(fp, "| -- | EMPTY    |     0 |      0 | 0.00   | [--------------------] |");
    } else {
        for (int i = 0; i < row_count; i++) {
            char bar[23];
            server_log_kd_bar(bar, sizeof(bar), rows[i].kills, rows[i].deaths);
            sprintf(line, "| %2d | %-8s | %5d | %6d | %6.2f | %-22s |",
                    rows[i].id,
                    rows[i].active ? "ONLINE" : "LEFT",
                    rows[i].kills,
                    rows[i].deaths,
                    server_log_kd(rows[i].kills, rows[i].deaths),
                    bar);
            server_log_print_line(fp, line);
        }
    }

    server_log_print_line(fp, "+----+----------+-------+--------+--------+------------------------+");
    server_log_print_line(fp, "Legend: # = kill share, - = death share. K/D uses kills/deaths; zero deaths keeps K/D at kills.");
}

static BOOL WINAPI server_log_ctrl_handler(DWORD ctrl_type)
{
    const char *reason = "console event";

    switch (ctrl_type) {
    case CTRL_C_EVENT:
        reason = "CTRL_C";
        break;
    case CTRL_CLOSE_EVENT:
        reason = "CTRL_CLOSE";
        break;
    case CTRL_BREAK_EVENT:
        reason = "CTRL_BREAK";
        break;
    case CTRL_LOGOFF_EVENT:
        reason = "CTRL_LOGOFF";
        break;
    case CTRL_SHUTDOWN_EVENT:
        reason = "CTRL_SHUTDOWN";
        break;
    default:
        break;
    }

    server_log_save_match(reason);
    return FALSE;
}

int server_log_init(ServerPlayer *players, int player_count)
{
    CreateDirectoryA(SERVER_LOG_DIR, NULL);
    g_players = players;
    g_player_count = player_count;
    g_saved = 0;
    SetConsoleCtrlHandler(server_log_ctrl_handler, TRUE);
    return 0;
}

void server_log_save_match(const char *reason)
{
    ServerLogRow rows[MAX_PLAYERS];
    char now[32];
    FILE *fp;
    int row_count;

    if (g_saved)
        return;
    g_saved = 1;

    row_count = server_log_collect_rows(rows, MAX_PLAYERS);
    server_log_now(now, sizeof(now));

    printf("\n");
    server_log_emit_scoreboard(stdout, reason, now, rows, row_count);
    printf("\n");
    fflush(stdout);

    fp = fopen(SERVER_LOG_SCOREBOARD, "a");
    if (!fp)
        return;

    fprintf(fp, "\n");
    server_log_emit_scoreboard(fp, reason, now, rows, row_count);
    fprintf(fp, "\n");
    fclose(fp);
}

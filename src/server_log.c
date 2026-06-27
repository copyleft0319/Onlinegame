#include "server_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define SERVER_LOG_DIR "C:\\Users\\MSI\\Desktop\\Onlinegame\\logs"
#define SERVER_LOG_STATS SERVER_LOG_DIR "\\player_stats.txt"
#define SERVER_LOG_SCOREBOARD SERVER_LOG_DIR "\\last_match_scoreboard.txt"

typedef struct {
    int id;
    int total_kills;
    int total_deaths;
    int matches;
} ServerLogCareer;

typedef struct {
    int id;
    int kills;
    int deaths;
    int active;
} ServerLogRow;

static ServerPlayer *g_players;
static int g_player_count;
static ServerLogCareer g_careers[MAX_PLAYERS];

static void server_log_now(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void server_log_load_stats(void)
{
    FILE *fp = fopen(SERVER_LOG_STATS, "r");
    if (!fp)
        return;

    int id, kills, deaths, matches;
    while (fscanf(fp, "%d %d %d %d", &id, &kills, &deaths, &matches) == 4) {
        if (id >= 0 && id < MAX_PLAYERS) {
            g_careers[id].id = id;
            g_careers[id].total_kills = kills;
            g_careers[id].total_deaths = deaths;
            g_careers[id].matches = matches;
        }
    }

    fclose(fp);
}

static void server_log_write_stats(const ServerLogRow *rows, int row_count)
{
    ServerLogCareer snapshot[MAX_PLAYERS];
    memcpy(snapshot, g_careers, sizeof(snapshot));

    for (int i = 0; i < row_count; i++) {
        int id = rows[i].id;
        if (id < 0 || id >= MAX_PLAYERS)
            continue;

        snapshot[id].id = id;
        snapshot[id].total_kills += rows[i].kills;
        snapshot[id].total_deaths += rows[i].deaths;
        if (rows[i].kills > 0 || rows[i].deaths > 0 || rows[i].active)
            snapshot[id].matches += 1;
    }

    FILE *fp = fopen(SERVER_LOG_STATS, "w");
    if (!fp)
        return;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        fprintf(fp, "%d %d %d %d\n",
                i,
                snapshot[i].total_kills,
                snapshot[i].total_deaths,
                snapshot[i].matches);
    }

    fclose(fp);
}

static double server_log_kd(int kills, int deaths)
{
    return deaths == 0 ? (double)kills : (double)kills / (double)deaths;
}

static void server_log_kd_bar(char *buf, size_t size, int kills, int deaths)
{
    int total = kills + deaths;
    int k_blocks = total == 0 ? 0 : (kills * 20) / total;

    if (size < 22)
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

static void server_log_write_scoreboard(const char *reason,
                                        const ServerLogRow *rows,
                                        int row_count)
{
    FILE *fp = fopen(SERVER_LOG_SCOREBOARD, "w");
    if (!fp)
        return;

    char now[32];
    server_log_now(now, sizeof(now));

    fprintf(fp, "+======================================================================+\n");
    fprintf(fp, "|                    ONLINEGAME SERVER K/D BOARD                       |\n");
    fprintf(fp, "+======================================================================+\n");
    fprintf(fp, "| saved_at: %-19s | reason: %-28s |\n", now, reason);
    fprintf(fp, "+----+----------+-------+--------+--------+------------------------+\n");
    fprintf(fp, "| ID | STATUS   | KILLS | DEATHS | K/D    | KILL PRESSURE          |\n");
    fprintf(fp, "+----+----------+-------+--------+--------+------------------------+\n");

    if (row_count == 0) {
        fprintf(fp, "| -- | EMPTY    |     0 |      0 | 0.00   | [--------------------] |\n");
    } else {
        for (int i = 0; i < row_count; i++) {
            char bar[23];
            server_log_kd_bar(bar, sizeof(bar), rows[i].kills, rows[i].deaths);
            fprintf(fp, "| %2d | %-8s | %5d | %6d | %6.2f | %-22s |\n",
                    rows[i].id,
                    rows[i].active ? "ONLINE" : "LEFT",
                    rows[i].kills,
                    rows[i].deaths,
                    server_log_kd(rows[i].kills, rows[i].deaths),
                    bar);
        }
    }

    fprintf(fp, "+----+----------+-------+--------+--------+------------------------+\n");
    fprintf(fp, "\nLegend: # = kill share, - = death share. K/D uses kills/deaths; zero deaths keeps K/D at kills.\n");
    fclose(fp);
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
    server_log_close();
    return FALSE;
}

int server_log_init(ServerPlayer *players, int player_count)
{
    CreateDirectoryA(SERVER_LOG_DIR, NULL);

    g_players = players;
    g_player_count = player_count;

    for (int i = 0; i < MAX_PLAYERS; i++)
        g_careers[i].id = i;

    server_log_load_stats();

    SetConsoleCtrlHandler(server_log_ctrl_handler, TRUE);
    return 0;
}

void server_log_close(void)
{
}

void server_log_player_join(int id)
{
    (void)id;
}

void server_log_player_leave(int id)
{
    (void)id;
}

void server_log_player_kill(int killer_id, int victim_id)
{
    (void)killer_id;
    (void)victim_id;
    server_log_save_match("player kill");
}

void server_log_save_match(const char *reason)
{
    ServerLogRow rows[MAX_PLAYERS];
    int row_count;

    row_count = server_log_collect_rows(rows, MAX_PLAYERS);
    //server_log_sort_rows(rows, row_count);
    server_log_write_scoreboard(reason, rows, row_count);
    server_log_write_stats(rows, row_count);
}

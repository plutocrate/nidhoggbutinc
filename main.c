#include "raylib.h"
#include "game.h"
#include "network.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ----------------------------------------------------------------
// Simple menu to choose mode before window opens
// ----------------------------------------------------------------

typedef struct AppConfig {
    GameMode    mode;
    char        peer_ip[64];    // direct IP (MODE_CLIENT)
    char        relay_ip[64];   // relay server IP (MODE_RELAY)
    char        room_code[5];   // 4-char room code (MODE_RELAY)
} AppConfig;

// Generic single-line text input; allowed_chars NULL = allow all printable
static bool run_text_input(const char *prompt, const char *hint,
                            char *out, int maxlen) {
    out[0] = '\0';
    int cursor = 0;
    while (!WindowShouldClose()) {
        int c = GetCharPressed();
        while (c > 0) {
            bool ok = (c >= 32 && c < 127);
            if (ok && cursor < maxlen - 1) {
                out[cursor++] = (char)c;
                out[cursor]   = '\0';
            }
            c = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && cursor > 0) out[--cursor] = '\0';
        if (IsKeyPressed(KEY_ENTER) && cursor > 0)    return true;
        if (IsKeyPressed(KEY_ESCAPE))                  return false;

        BeginDrawing();
        ClearBackground((Color){15, 10, 25, 255});
        int cx = g_screen_w() / 2;
        int cy = g_screen_h() / 2;
        DrawText(prompt, cx - MeasureText(prompt, 28) / 2, cy - 70, 28, WHITE);
        if (hint) DrawText(hint, cx - MeasureText(hint, 16) / 2, cy - 36, 16,
                           (Color){140, 130, 160, 200});
        char box[128];
        snprintf(box, sizeof(box), "> %s_", out);
        DrawText(box, cx - MeasureText(box, 32) / 2, cy, 32, (Color){180, 220, 255, 255});
        DrawText("ENTER to confirm   ESC to go back",
                 cx - MeasureText("ENTER to confirm   ESC to go back", 18) / 2,
                 cy + 60, 18, (Color){120, 120, 140, 200});
        EndDrawing();
    }
    return false;
}

// In-window mode selection
static bool run_mode_select(AppConfig *cfg) {
    int selected = 0;
    const int NUM_OPTIONS = 4;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_UP))   selected = (selected + NUM_OPTIONS - 1) % NUM_OPTIONS;
        if (IsKeyPressed(KEY_DOWN)) selected = (selected + 1) % NUM_OPTIONS;

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            if (selected == 0) { cfg->mode = MODE_LOCAL;  return true; }
            if (selected == 1) { cfg->mode = MODE_HOST;   return true; }
            if (selected == 2) {
                cfg->mode = MODE_CLIENT;
                if (!run_text_input("Enter host IP address:", "e.g. 192.168.1.42",
                                    cfg->peer_ip, sizeof(cfg->peer_ip)))
                    continue;
                return true;
            }
            if (selected == 3) {
                cfg->mode = MODE_RELAY;
                if (!run_text_input("Relay server IP:", "the VPS public IP running relay.py",
                                    cfg->relay_ip, sizeof(cfg->relay_ip)))
                    continue;
                if (!run_text_input("Room code:", "4-char code — same on both machines (e.g. DUEL)",
                                    cfg->room_code, sizeof(cfg->room_code)))
                    continue;
                // Enforce exactly 4 chars, uppercase
                for (int i = 0; i < 4; i++) {
                    if (cfg->room_code[i] == '\0') cfg->room_code[i] = 'A';
                    if (cfg->room_code[i] >= 'a' && cfg->room_code[i] <= 'z')
                        cfg->room_code[i] -= 32;
                }
                cfg->room_code[4] = '\0';
                return true;
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) return false;

        BeginDrawing();
        ClearBackground((Color){15, 10, 25, 255});
        int cx = g_screen_w() / 2;

        DrawText("DUEL", cx - MeasureText("DUEL", 80) / 2, 80, 80,
                 (Color){200, 160, 255, 255});
        DrawText("A Nidhogg-inspired fencing game",
                 cx - MeasureText("A Nidhogg-inspired fencing game", 20) / 2,
                 180, 20, (Color){150, 130, 180, 200});

        const char *options[] = {
            "Local 2-Player",
            "Host (direct IP)",
            "Join (direct IP)",
            "Online via Relay  [works through NAT]",
        };
        for (int i = 0; i < NUM_OPTIONS; i++) {
            Color col  = (i == selected) ? (Color){255, 220, 80, 255}
                                         : (Color){180, 170, 200, 220};
            int   size = (i == selected) ? 32 : 24;
            int   tw   = MeasureText(options[i], size);
            DrawText(options[i], cx - tw / 2, 280 + i * 64, size, col);
            if (i == selected)
                DrawText(">", cx - tw / 2 - 30, 280 + i * 64, size, col);
        }

        // Relay hint
        DrawText("Relay: both players enter same room code — no port forwarding needed",
                 cx - MeasureText("Relay: both players enter same room code — no port forwarding needed", 14) / 2,
                 280 + NUM_OPTIONS * 64 + 10, 14, (Color){120, 180, 140, 180});

        DrawText("UP/DOWN: Navigate   ENTER: Select",
                 cx - MeasureText("UP/DOWN: Navigate   ENTER: Select", 18) / 2,
                 g_screen_h() - 50, 18, (Color){120, 120, 140, 200});
        DrawText("P1: WASD + J(attack) K(parry) L(throw)",
                 cx - MeasureText("P1: WASD + J(attack) K(parry) L(throw)", 16) / 2,
                 g_screen_h() - 110, 16, (Color){100, 180, 255, 180});
        DrawText("P2(local): Arrows + Num1 Num2 Num3",
                 cx - MeasureText("P2(local): Arrows + Num1 Num2 Num3", 16) / 2,
                 g_screen_h() - 88, 16, (Color){255, 120, 80, 180});

        EndDrawing();
    }
    return false;
}

int main(int argc, char *argv[]) {
    AppConfig cfg;
    cfg.mode = MODE_LOCAL;
    cfg.peer_ip[0]   = '\0';
    cfg.relay_ip[0]  = '\0';
    cfg.room_code[0] = '\0';
    bool skip_menu = false;

    if (argc >= 2) {
        if (strcmp(argv[1], "host") == 0) {
            cfg.mode = MODE_HOST; skip_menu = true;
        } else if (argc >= 3 && strcmp(argv[1], "client") == 0) {
            cfg.mode = MODE_CLIENT;
            strncpy(cfg.peer_ip, argv[2], sizeof(cfg.peer_ip) - 1);
            skip_menu = true;
        } else if (strcmp(argv[1], "local") == 0) {
            cfg.mode = MODE_LOCAL; skip_menu = true;
        } else if (argc >= 4 && strcmp(argv[1], "relay") == 0) {
            // duel relay <relay_ip> <room_code>
            cfg.mode = MODE_RELAY;
            strncpy(cfg.relay_ip,  argv[2], sizeof(cfg.relay_ip)  - 1);
            strncpy(cfg.room_code, argv[3], 4);
            cfg.room_code[4] = '\0';
            skip_menu = true;
        }
    }

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "DUEL - A Nidhogg-inspired Game");
    SetTargetFPS(0);
    SetExitKey(KEY_NULL);

    net_platform_init();

    if (!skip_menu) {
        if (!run_mode_select(&cfg)) {
            CloseWindow();
            net_platform_shutdown();
            return 0;
        }
    }

    // Build peer_ip string — for relay mode encode as "relay_ip|room_code"
    char peer_arg[128] = {0};
    if (cfg.mode == MODE_RELAY) {
        snprintf(peer_arg, sizeof(peer_arg), "%s|%s", cfg.relay_ip, cfg.room_code);
    } else if (cfg.peer_ip[0]) {
        strncpy(peer_arg, cfg.peer_ip, sizeof(peer_arg) - 1);
    }

    GameState gs;
    game_init(&gs, cfg.mode, peer_arg[0] ? peer_arg : NULL);

    while (!WindowShouldClose() && !IsKeyPressed(KEY_ESCAPE)) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;
        game_tick(&gs, dt);
        game_render(&gs);
    }

    game_shutdown(&gs);
    CloseWindow();
    net_platform_shutdown();
    return 0;
}

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
    char        peer_ip[64];
} AppConfig;

// In-window mode selection
static bool run_mode_select(AppConfig *cfg) {
    // We use a minimal in-window selection screen
    int selected = 0;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_UP))    selected = (selected + 2) % 3;
        if (IsKeyPressed(KEY_DOWN))  selected = (selected + 1) % 3;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            switch (selected) {
                case 0: cfg->mode = MODE_LOCAL;  return true;
                case 1: cfg->mode = MODE_HOST;   return true;
                case 2: cfg->mode = MODE_CLIENT; break;
            }
            if (cfg->mode == MODE_CLIENT) {
                // IP input mode
                cfg->peer_ip[0] = '\0';
                int cursor = 0;
                while (!WindowShouldClose()) {
                    int c = GetCharPressed();
                    while (c > 0) {
                        if ((c >= '0' && c <= '9') || c == '.') {
                            if (cursor < 63) {
                                cfg->peer_ip[cursor++] = (char)c;
                                cfg->peer_ip[cursor] = '\0';
                            }
                        }
                        c = GetCharPressed();
                    }
                    if (IsKeyPressed(KEY_BACKSPACE) && cursor > 0) {
                        cfg->peer_ip[--cursor] = '\0';
                    }
                    if (IsKeyPressed(KEY_ENTER) && cursor > 0) {
                        return true;
                    }
                    if (IsKeyPressed(KEY_ESCAPE)) break;

                    BeginDrawing();
                    ClearBackground((Color){15, 10, 25, 255});
                    DrawText("Enter host IP address:", g_screen_w()/2 - 180, g_screen_h()/2 - 60, 28, WHITE);
                    char box_buf[80];
                    snprintf(box_buf, sizeof(box_buf), "> %s_", cfg->peer_ip);
                    DrawText(box_buf, g_screen_w()/2 - 180, g_screen_h()/2, 32, (Color){180, 220, 255, 255});
                    DrawText("Press ENTER to connect", g_screen_w()/2 - 160, g_screen_h()/2 + 60, 20,
                             (Color){150, 150, 150, 255});
                    DrawText("Press ESC to go back", g_screen_w()/2 - 140, g_screen_h()/2 + 88, 18,
                             (Color){120, 120, 120, 200});
                    EndDrawing();
                }
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) return false;

        BeginDrawing();
        ClearBackground((Color){15, 10, 25, 255});

        int cx = g_screen_w() / 2;
        DrawText("DUEL", cx - MeasureText("DUEL", 80)/2, 80, 80,
                 (Color){200, 160, 255, 255});
        DrawText("A Nidhogg-inspired fencing game", cx - MeasureText("A Nidhogg-inspired fencing game", 20)/2,
                 180, 20, (Color){150, 130, 180, 200});

        const char *options[] = {"Local 2-Player", "Host Online Game", "Join Game (Client)"};
        for (int i = 0; i < 3; i++) {
            Color col = (i == selected) ? (Color){255, 220, 80, 255} : (Color){180, 170, 200, 220};
            int size = (i == selected) ? 34 : 26;
            int tw = MeasureText(options[i], size);
            DrawText(options[i], cx - tw/2, 280 + i * 70, size, col);
            if (i == selected) {
                DrawText(">", cx - tw/2 - 30, 280 + i * 70, size, col);
            }
        }

        DrawText("UP/DOWN: Navigate  ENTER: Select", cx - 180, g_screen_h() - 50, 18,
                 (Color){120, 120, 140, 200});

        // Controls reminder
        DrawText("P1: WASD + J(attack) K(parry) L(throw)", cx - 210, g_screen_h() - 110, 16,
                 (Color){100, 180, 255, 180});
        DrawText("P2(local): Arrows + Num1 Num2 Num3", cx - 190, g_screen_h() - 88, 16,
                 (Color){255, 120, 80, 180});

        EndDrawing();
    }
    return false;
}

int main(int argc, char *argv[]) {
    // Command line: duel host | duel client <ip> | duel (menu)
    AppConfig cfg;
    cfg.mode = MODE_LOCAL;
    cfg.peer_ip[0] = '\0';
    bool skip_menu = false;

    if (argc >= 2) {
        if (strcmp(argv[1], "host") == 0) {
            cfg.mode = MODE_HOST;
            skip_menu = true;
        } else if (argc >= 3 && strcmp(argv[1], "client") == 0) {
            cfg.mode = MODE_CLIENT;
            strncpy(cfg.peer_ip, argv[2], sizeof(cfg.peer_ip) - 1);
            skip_menu = true;
        } else if (strcmp(argv[1], "local") == 0) {
            cfg.mode = MODE_LOCAL;
            skip_menu = true;
        }
    }

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "DUEL - A Nidhogg-inspired Game");  // 0,0 = use monitor resolution
    SetTargetFPS(0);  // uncapped - fixed timestep accumulator controls simulation rate
    SetExitKey(KEY_NULL);  // Disable default exit on ESC

    // Platform init for networking
    net_platform_init();

    if (!skip_menu) {
        if (!run_mode_select(&cfg)) {
            CloseWindow();
            net_platform_shutdown();
            return 0;
        }
    }

    // Init game
    GameState gs;
    game_init(&gs, cfg.mode, cfg.peer_ip[0] ? cfg.peer_ip : NULL);

    // Main loop
    while (!WindowShouldClose() && !IsKeyPressed(KEY_ESCAPE)) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;  // clamp

        game_tick(&gs, dt);
        game_render(&gs);
    }

    game_shutdown(&gs);
    CloseWindow();
    net_platform_shutdown();
    return 0;
}

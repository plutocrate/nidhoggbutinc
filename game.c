#include "game.h"
#include "physics.h"
#include "raylib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// ----------------------------------------------------------------
// Runtime screen dimension accessors (defined here, declared in types.h)
// ----------------------------------------------------------------
int   g_screen_w(void) { return GetScreenWidth();  }
int   g_screen_h(void) { return GetScreenHeight(); }
float g_ground_y(void) { return (float)GetScreenHeight() * 0.82f; }
float g_arena_w(void)  { return (float)GetScreenWidth()  * 3.0f;  }

// ----------------------------------------------------------------
// Forward declarations for render helpers
// ----------------------------------------------------------------
static void render_player(const Player *p, float cam_x, bool debug);
static void render_sword(const Player *p, float cam_x, bool debug);
static void render_thrown_sword(const ThrowingSword *s, float cam_x);
static void render_arena(float cam_x);
static void render_hud(const GameState *gs);
static void render_menu(GameState *gs);
static void render_debug_net(const GameState *gs);
static void render_controls_hint(void);

// ----------------------------------------------------------------
// Init / Shutdown
// ----------------------------------------------------------------
void game_init(GameState *gs, GameMode mode, const char *peer_ip) {
    memset(gs, 0, sizeof(*gs));
    gs->mode = mode;
    gs->phase = (mode == MODE_LOCAL) ? PHASE_PLAYING : PHASE_MENU;
    gs->local_player_id = (mode == MODE_CLIENT) ? 1 : 0;
    gs->frame = 0;
    gs->debug_hitboxes = false;

    if (mode != MODE_LOCAL) {
        NetRole role = (mode == MODE_HOST) ? NET_HOST : NET_CLIENT;
        if (!net_init(&gs->net, role, peer_ip)) {
            gs->mode = MODE_LOCAL;
            gs->phase = PHASE_PLAYING;
        } else {
            gs->phase = PHASE_CONNECTING;
        }
    }

    game_start_round(gs);
}

void game_shutdown(GameState *gs) {
    if (gs->mode != MODE_LOCAL) {
        net_shutdown(&gs->net);
        net_platform_shutdown();
    }
}

void game_start_round(GameState *gs) {
    int sw = g_screen_w();
    player_init(&gs->players[0], 0, sw * 0.35f);
    player_init(&gs->players[1], 1, sw * 0.65f);
    gs->players[1].facing = -1;

    for (int i = 0; i < MAX_THROWN_SWORDS; i++) {
        gs->swords[i].active = false;
    }

    gs->cam.x = sw * 0.5f;
    gs->cam.y = g_screen_h() * 0.5f;
    gs->cam.target_x = gs->cam.x;
    gs->round_over_timer = 0;
    gs->winner_id = -1;
}

// ----------------------------------------------------------------
// Camera
// ----------------------------------------------------------------
static void update_camera(Camera2D_State *cam, const Player *p0, const Player *p1) {
    float mid_x = (p0->body.pos.x + p1->body.pos.x) * 0.5f + PLAYER_W * 0.5f;
    cam->target_x = mid_x;
    cam->x += (cam->target_x - cam->x) * 0.08f;

    float half  = g_screen_w() * 0.5f;
    float half_arena = g_arena_w() * 0.5f;
    if (cam->x - half < -half_arena) cam->x = -half_arena + half;
    if (cam->x + half >  half_arena) cam->x =  half_arena - half;
}

// ----------------------------------------------------------------
// Fixed Update
// ----------------------------------------------------------------
void game_fixed_update(GameState *gs) {
    if (gs->phase == PHASE_MENU || gs->phase == PHASE_CONNECTING) {
        if (gs->mode != MODE_LOCAL) {
            net_update(&gs->net, gs->frame);
            if (net_is_connected(&gs->net)) {
                gs->phase = PHASE_PLAYING;
            }
        }
        return;
    }

    if (gs->phase == PHASE_ROUND_OVER) {
        gs->round_over_timer--;
        if (gs->round_over_timer <= 0) {
            if (gs->players[0].score >= WIN_SCORE || gs->players[1].score >= WIN_SCORE) {
                gs->phase = PHASE_MATCH_OVER;
            } else {
                game_start_round(gs);
                gs->phase = PHASE_PLAYING;
            }
        }
        return;
    }

    if (gs->phase == PHASE_MATCH_OVER) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            gs->players[0].score = 0;
            gs->players[1].score = 0;
            game_start_round(gs);
            gs->phase = PHASE_PLAYING;
        }
        return;
    }

    // --- PHASE_PLAYING ---

    Input p0_in, p1_in;
    memset(&p0_in, 0, sizeof(p0_in));
    memset(&p1_in, 0, sizeof(p1_in));

    if (gs->mode == MODE_LOCAL) {
        p0_in = input_gather_p1(gs->frame);
        p1_in = input_gather_p2(gs->frame);
    } else if (gs->mode == MODE_HOST) {
        p0_in = input_gather_p1(gs->frame);
        net_push_local_input(&gs->net, &p0_in);
        net_get_remote_input(&gs->net, gs->frame, &p1_in);
        net_update(&gs->net, gs->frame);
    } else { // CLIENT
        p1_in = input_gather_p1(gs->frame);
        net_push_local_input(&gs->net, &p1_in);
        net_get_remote_input(&gs->net, gs->frame, &p0_in);
        net_update(&gs->net, gs->frame);

        NetStatePacket sp;
        if (net_recv_state(&gs->net, &sp)) {
            PlayerSync s0 = sp.p0;
            PlayerSync s1 = sp.p1;
            player_from_sync(&gs->players[0], &s0);
            player_from_sync(&gs->players[1], &s1);
            gs->players[0].score = sp.p0_score;
            gs->players[1].score = sp.p1_score;
        }
    }

    player_update(&gs->players[0], &p0_in, FIXED_DT);
    player_update(&gs->players[1], &p1_in, FIXED_DT);

    // Spawn thrown sword on first frame of throw state
    for (int pid = 0; pid < 2; pid++) {
        Player *p = &gs->players[pid];
        if (p->state == STATE_THROW && p->state_timer == 7) {
            for (int i = 0; i < MAX_THROWN_SWORDS; i++) {
                if (!gs->swords[i].active) {
                    combat_throw_sword(p, &gs->swords[i]);
                    break;
                }
            }
        }
    }

    combat_update_thrown_swords(gs->swords, MAX_THROWN_SWORDS,
                                &gs->players[0], &gs->players[1], FIXED_DT);

    combat_resolve(&gs->players[0], &gs->players[1],
                   gs->swords, MAX_THROWN_SWORDS);

    // Death / respawn handling
    for (int i = 0; i < 2; i++) {
        Player *dead  = &gs->players[i];
        Player *alive = &gs->players[1 - i];

        if (dead->state == STATE_DEAD && dead->respawn_timer == 119) {
            alive->score++;
            gs->winner_id = alive->id;
            if (alive->score >= WIN_SCORE) {
                gs->phase = PHASE_MATCH_OVER;
            } else {
                gs->phase = PHASE_ROUND_OVER;
                gs->round_over_timer = 180;
            }
        }

        if (dead->state == STATE_DEAD && dead->respawn_timer == 0) {
            float respawn_x;
            int   respawn_facing;
            if (dead->id == 0) {
                respawn_x      = alive->body.pos.x - 120.0f;
                respawn_facing = 1;
            } else {
                respawn_x      = alive->body.pos.x + 120.0f;
                respawn_facing = -1;
            }
            player_respawn(dead, respawn_x, respawn_facing);
        }
    }

    update_camera(&gs->cam, &gs->players[0], &gs->players[1]);

    // Host sends authoritative state every 2 frames
    if (gs->mode == MODE_HOST && net_is_connected(&gs->net)) {
        static int state_send_counter = 0;
        state_send_counter++;
        if (state_send_counter % 2 == 0) {
            NetStatePacket sp;
            sp.header.type  = PKT_STATE;
            sp.header.frame = gs->frame;
            PlayerSync s0, s1;
            player_to_sync(&gs->players[0], &s0, gs->frame);
            player_to_sync(&gs->players[1], &s1, gs->frame);
            sp.p0 = s0;
            sp.p1 = s1;
            sp.game_state = (uint8_t)gs->phase;
            sp.p0_score   = (uint8_t)gs->players[0].score;
            sp.p1_score   = (uint8_t)gs->players[1].score;
            net_send_state(&gs->net, &sp);
        }
    }

    if (IsKeyPressed(KEY_F1)) gs->debug_hitboxes = !gs->debug_hitboxes;
    if (IsKeyPressed(KEY_F2)) gs->debug_network  = !gs->debug_network;
    // F11: toggle fullscreen at runtime
    if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

    gs->frame++;
}

// ----------------------------------------------------------------
// Main tick with fixed timestep accumulator
// ----------------------------------------------------------------
void game_tick(GameState *gs, float dt) {
    gs->dt_accum += dt;
    if (gs->dt_accum > FIXED_DT * 8) gs->dt_accum = FIXED_DT * 8;
    while (gs->dt_accum >= FIXED_DT) {
        game_fixed_update(gs);
        gs->dt_accum -= FIXED_DT;
    }
}

// ----------------------------------------------------------------
// Rendering helpers
// ----------------------------------------------------------------

static int world_to_screen_x(float world_x, float cam_x) {
    return (int)(world_x - cam_x + g_screen_w() * 0.5f);
}

static int world_to_screen_y(float world_y) {
    return (int)world_y;
}

static void render_arena(float cam_x) {
    int sw = g_screen_w();
    int sh = g_screen_h();
    float gy = g_ground_y();
    float aw = g_arena_w();

    ClearBackground((Color){20, 15, 30, 255});

    // Background parallax columns
    float para = cam_x * 0.3f;
    for (int i = 0; i < 10; i++) {
        float bx = fmodf(i * 160.0f - para * 0.5f, (float)sw + 160.0f) - 80.0f;
        DrawRectangle((int)bx, 80 + i * 20, 2, 60 + i * 8,
                      (Color){40 + i * 5, 30 + i * 5, 60 + i * 5, 120});
    }

    // Ground slab
    int gx0 = world_to_screen_x(-aw * 0.5f, cam_x);
    int gx1 = world_to_screen_x( aw * 0.5f, cam_x);
    int gyi = (int)gy;
    DrawRectangle(gx0, gyi, gx1 - gx0, sh - gyi, (Color){45, 38, 55, 255});
    DrawLine(gx0, gyi, gx1, gyi, (Color){180, 140, 200, 255});

    // Floor tiles
    int num_tiles = (int)(aw / 200.0f) + 2;
    for (int tx = -num_tiles; tx <= num_tiles; tx++) {
        int tile_x = world_to_screen_x(tx * 200.0f, cam_x);
        DrawLine(tile_x, gyi, tile_x, gyi + 8, (Color){80, 60, 90, 200});
    }

    // Goal lines
    int left_goal  = world_to_screen_x(-aw * 0.5f + 40.0f, cam_x);
    int right_goal = world_to_screen_x( aw * 0.5f - 40.0f, cam_x);
    DrawLine(left_goal,  0, left_goal,  sh, (Color){255, 80, 80, 180});
    DrawLine(right_goal, 0, right_goal, sh, (Color){80, 80, 255, 180});

    // Mid platforms — scaled to screen
    float plat_y  = gy - sh * 0.22f;
    float plat_w  = sw * 0.16f;
    int px0 = world_to_screen_x(-sw * 0.20f, cam_x);
    DrawRectangle(px0, (int)plat_y, (int)plat_w, 14, (Color){65, 55, 80, 255});
    DrawLine(px0, (int)plat_y, px0 + (int)plat_w, (int)plat_y, (Color){180, 140, 200, 200});

    int px1 = world_to_screen_x(sw * 0.04f, cam_x);
    DrawRectangle(px1, (int)plat_y, (int)plat_w, 14, (Color){65, 55, 80, 255});
    DrawLine(px1, (int)plat_y, px1 + (int)plat_w, (int)plat_y, (Color){180, 140, 200, 200});
}

static void render_player(const Player *p, float cam_x, bool debug) {
    if (!p->ragdoll.active) {
        int sx = world_to_screen_x(p->body.pos.x + p->body.size.x * 0.5f, cam_x);
        int sy = world_to_screen_y(p->body.pos.y);
        int w  = (int)p->body.size.x;
        int h  = (int)p->body.size.y;

        Color body_color = (p->id == 0) ? (Color){100, 180, 255, 255}
                                        : (Color){255, 120,  80, 255};
        Color stun_flash = (Color){255, 255, 100, 200};
        Color cur_color  = (p->state == STATE_STUNNED && (p->stun_timer % 6 < 3))
                           ? stun_flash : body_color;

        int foot_y     = sy + h;
        int hip_y      = sy + h - h / 3;
        int shoulder_y = sy + h / 3;
        int mid_x      = sx;

        DrawLine(mid_x, hip_y, mid_x - 7, foot_y, cur_color);
        DrawLine(mid_x, hip_y, mid_x + 7, foot_y, cur_color);
        DrawLine(mid_x, hip_y, mid_x, shoulder_y, cur_color);

        int arm_reach   = p->crouching ? 8 : 14;
        int sword_arm_x = mid_x + p->facing * arm_reach;
        int sword_arm_y = shoulder_y + 10;
        DrawLine(mid_x, shoulder_y, sword_arm_x, sword_arm_y, cur_color);
        int off_arm_x = mid_x - p->facing * 8;
        DrawLine(mid_x, shoulder_y, off_arm_x, sword_arm_y + 4, cur_color);

        DrawCircle(mid_x, sy + 8, 9, cur_color);
        DrawCircle(mid_x + p->facing * 4, sy + 7, 2, (Color){20, 20, 40, 255});

        (void)w;
    } else {
        Color dead_color = (p->id == 0) ? (Color){60, 100, 160, 200}
                                        : (Color){160,  70,  40, 200};
        int alpha_mod = p->ragdoll.timer * 2;
        if (alpha_mod > 255) alpha_mod = 255;
        dead_color.a = (uint8_t)alpha_mod;

        const Ragdoll *rd = &p->ragdoll;
        for (int i = 0; i < RAGDOLL_BONES; i++) {
            int bx = world_to_screen_x(rd->bones[i].pos.x, cam_x);
            int by = world_to_screen_y(rd->bones[i].pos.y);
            DrawCircle(bx, by, (i == 0) ? 8 : 4, dead_color);
        }
        if (RAGDOLL_BONES >= 2) {
            int ax  = world_to_screen_x(rd->bones[0].pos.x, cam_x);
            int ay  = world_to_screen_y(rd->bones[0].pos.y);
            int bx2 = world_to_screen_x(rd->bones[1].pos.x, cam_x);
            int by2 = world_to_screen_y(rd->bones[1].pos.y);
            DrawLine(ax, ay, bx2, by2, dead_color);
        }
    }

    if (debug) {
        int hx = world_to_screen_x(p->hurtbox.x, cam_x);
        DrawRectangleLines(hx, (int)p->hurtbox.y, (int)p->hurtbox.w, (int)p->hurtbox.h, GREEN);
        if (p->weapon_hitbox.w > 0) {
            int wx = world_to_screen_x(p->weapon_hitbox.x, cam_x);
            DrawRectangleLines(wx, (int)p->weapon_hitbox.y,
                               (int)p->weapon_hitbox.w, (int)p->weapon_hitbox.h, RED);
        }
        if (p->parry_box.w > 0) {
            int px2 = world_to_screen_x(p->parry_box.x, cam_x);
            DrawRectangleLines(px2, (int)p->parry_box.y,
                               (int)p->parry_box.w, (int)p->parry_box.h, YELLOW);
        }
    }
}

static void render_sword(const Player *p, float cam_x, bool debug) {
    if (!p->has_sword || p->state == STATE_DEAD) return;

    int bx = world_to_screen_x(p->sword_base.x, cam_x);
    int by = world_to_screen_y(p->sword_base.y);
    int tx = world_to_screen_x(p->sword_tip.x, cam_x);
    int ty = world_to_screen_y(p->sword_tip.y);

    Color blade_color = (p->state == STATE_PARRY) ? (Color){255, 255, 150, 255}
                                                   : (Color){220, 220, 255, 255};
    DrawLineEx((Vector2){(float)bx, (float)by}, (Vector2){(float)tx, (float)ty},
               3.0f, blade_color);

    float gx   = bx + (tx - bx) * 0.15f;
    float gy2  = by + (ty - by) * 0.15f;
    float perp_x = -(ty - by) * 0.15f;
    float perp_y =  (tx - bx) * 0.15f;
    DrawLine((int)(gx - perp_x), (int)(gy2 - perp_y),
             (int)(gx + perp_x), (int)(gy2 + perp_y),
             (Color){180, 160, 100, 255});

    if (p->state == STATE_ATTACK && p->attack_frame >= 2 && p->attack_frame <= 9) {
        DrawCircle(tx, ty, 6, (Color){255, 200, 100, 120});
    }
    (void)debug;
}

static void render_thrown_sword(const ThrowingSword *s, float cam_x) {
    if (!s->active) return;
    int sx = world_to_screen_x(s->pos.x, cam_x);
    int sy = world_to_screen_y(s->pos.y);
    float dx = cosf(s->angle) * 20.0f;
    float dy = sinf(s->angle) * 20.0f;
    DrawLine((int)(sx - dx), (int)(sy - dy), (int)(sx + dx), (int)(sy + dy),
             (Color){220, 220, 255, 255});
    DrawLine((int)(sx - dx), (int)(sy - dy), (int)(sx + dx), (int)(sy + dy),
             (Color){255, 240, 180, 120});
}

static void render_hud(const GameState *gs) {
    int sw = g_screen_w();
    int sh = g_screen_h();
    char buf[64];

    snprintf(buf, sizeof(buf), "P1: %d", gs->players[0].score);
    DrawText(buf, 40, 20, 28, (Color){100, 180, 255, 255});

    snprintf(buf, sizeof(buf), "P2: %d", gs->players[1].score);
    DrawText(buf, sw - 120, 20, 28, (Color){255, 120, 80, 255});

    snprintf(buf, sizeof(buf), "First to %d", WIN_SCORE);
    DrawText(buf, sw / 2 - 60, 20, 18, (Color){200, 200, 200, 180});

    snprintf(buf, sizeof(buf), "F:%u", gs->frame);
    DrawText(buf, sw - 80, sh - 24, 14, (Color){120, 120, 120, 180});

    if (gs->phase == PHASE_ROUND_OVER) {
        const char *msg = (gs->winner_id == 0) ? "PLAYER 1 WINS ROUND!"
                                                : "PLAYER 2 WINS ROUND!";
        int tw = MeasureText(msg, 40);
        DrawText(msg, sw / 2 - tw / 2, sh / 2 - 20, 40,
                 gs->winner_id == 0 ? (Color){100, 180, 255, 255} : (Color){255, 120, 80, 255});
    }

    if (gs->phase == PHASE_MATCH_OVER) {
        const char *msg = (gs->players[0].score >= WIN_SCORE) ? "PLAYER 1 WINS!"
                                                               : "PLAYER 2 WINS!";
        int tw = MeasureText(msg, 52);
        DrawText(msg, sw / 2 - tw / 2, sh / 2 - 50, 52,
                 gs->players[0].score >= WIN_SCORE ? (Color){100, 180, 255, 255}
                                                   : (Color){255, 120, 80, 255});
        DrawText("Press ENTER to play again", sw / 2 - 160, sh / 2 + 20, 24, WHITE);
    }

    const char *state_names[] = {"IDLE","WALK","JUMP","CROUCH","ATTACK","PARRY","STUN","DEAD","THROW"};
    for (int i = 0; i < 2; i++) {
        const Player *p = &gs->players[i];
        int state_idx = (int)p->state;
        if (state_idx >= 0 && state_idx < 9) {
            int xpos = (i == 0) ? 40 : sw - 120;
            DrawText(state_names[state_idx], xpos, 54, 14, (Color){180, 180, 180, 200});
        }
        if (!p->has_sword) {
            int xpos = (i == 0) ? 40 : sw - 120;
            DrawText("[NO SWORD]", xpos, 72, 12, (Color){255, 200, 80, 255});
        }
    }
}

static void render_menu(GameState *gs) {
    int sw = g_screen_w();
    int sh = g_screen_h();
    ClearBackground((Color){15, 10, 25, 255});

    int cx = sw / 2;
    DrawText("NIDHOGG-LIKE DUEL", cx - MeasureText("NIDHOGG-LIKE DUEL", 56) / 2, 80, 56,
             (Color){200, 160, 255, 255});

    if (gs->phase == PHASE_CONNECTING) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Connecting to %s...", gs->net.peer_ip);
        int tw = MeasureText(buf, 28);
        DrawText(buf, cx - tw / 2, sh / 2, 28, WHITE);
        DrawText("Waiting for peer...",
                 cx - MeasureText("Waiting for peer...", 22) / 2,
                 sh / 2 + 50, 22, (Color){180, 180, 180, 255});
    }
}

static void render_debug_net(const GameState *gs) {
    if (!gs->debug_network) return;
    int sh = g_screen_h();
    char buf[128];
    snprintf(buf, sizeof(buf), "PING: %ums", net_get_ping(&gs->net));
    DrawText(buf, 10, sh - 60, 18, LIME);
    snprintf(buf, sizeof(buf), "FRAME: %u  REMOTE: %u", gs->frame, gs->net.remote_frame);
    DrawText(buf, 10, sh - 40, 14, LIME);
    snprintf(buf, sizeof(buf), "CONNECTED: %s  STARTED: %s",
             gs->net.connected ? "YES" : "NO",
             gs->net.match_started ? "YES" : "NO");
    DrawText(buf, 10, sh - 22, 14, LIME);
}

static void render_controls_hint(void) {
    int sh = g_screen_h();
    DrawText("P1: WASD=Move  J=Attack  K=Parry  L=Throw", 10, sh - 46, 13,
             (Color){120, 120, 150, 200});
    DrawText("P2: Arrows=Move  Num1=Atk  Num2=Parry  Num3=Throw  |  F1=Hitboxes  F2=Net  F11=Fullscreen",
             10, sh - 28, 13, (Color){120, 120, 150, 200});
}

void game_render(const GameState *gs) {
    BeginDrawing();

    if (gs->phase == PHASE_MENU || gs->phase == PHASE_CONNECTING) {
        render_menu((GameState *)gs);
        EndDrawing();
        return;
    }

    float cam_x = gs->cam.x;

    render_arena(cam_x);

    for (int i = 0; i < MAX_THROWN_SWORDS; i++) {
        render_thrown_sword(&gs->swords[i], cam_x);
    }

    for (int i = 0; i < 2; i++) {
        render_player(&gs->players[i], cam_x, gs->debug_hitboxes);
        render_sword(&gs->players[i], cam_x, gs->debug_hitboxes);
    }

    render_hud(gs);
    render_controls_hint();

    if (gs->mode != MODE_LOCAL) {
        render_debug_net(gs);
    }

    EndDrawing();
}

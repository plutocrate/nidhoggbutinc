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
// Fixed world dimensions - physics runs in this space on ALL machines
float g_ground_y(void) { return 620.0f; }   // fixed world Y of ground
float g_arena_w(void)  { return 4000.0f; }  // fixed world width (kept for compat)

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

// Forward declaration - defined later in rendering section
static float g_cam_zoom = 1.0f;

// Fixed world dimensions - used by both simulation and rendering
#define VIRTUAL_W    1280.0f   // world units across screen width at zoom=1
#define WORLD_GROUND 620.0f    // must match g_ground_y()

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
    // Symmetric spawn: player bodies centered at ±200 from world origin
    float spawn_offset = 200.0f;
    player_init(&gs->players[0], 0, -spawn_offset - PLAYER_W * 0.5f);
    player_init(&gs->players[1], 1,  spawn_offset - PLAYER_W * 0.5f);
    gs->players[1].facing = -1;

    for (int i = 0; i < MAX_THROWN_SWORDS; i++) {
        gs->swords[i].active = false;
    }

    gs->cam.x = 0.0f;  // world center
    gs->cam.y = 0.0f;
    gs->cam.target_x = 0.0f;
    gs->cam.zoom = 1.0f;
    gs->cam.target_zoom = 1.0f;
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

    // Clamp in world units: half-screen in world units = (screen_w/2) / world_scale
    float half_screen_world = (VIRTUAL_W * 0.5f) / g_cam_zoom;
    float half_arena = 2000.0f;
    if (cam->x - half_screen_world < -half_arena) cam->x = -half_arena + half_screen_world;
    if (cam->x + half_screen_world >  half_arena) cam->x =  half_arena - half_screen_world;

    // Dynamic zoom based on player distance in WORLD units (screen-independent)
    float dist = fabsf(p1->body.pos.x - p0->body.pos.x);
    float min_dist = 150.0f;   // world units - zoom in when this close
    float max_dist = 700.0f;   // world units - zoom out when this far
    float t = (dist - min_dist) / (max_dist - min_dist);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    cam->target_zoom = 1.4f - t * 0.8f;  // 1.4 (close) -> 0.6 (far)

    cam->zoom += (cam->target_zoom - cam->zoom) * 0.05f;
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
        if (gs->mode != MODE_LOCAL) {
            net_update(&gs->net, gs->frame);
            // CLIENT: keep receiving state packets during round over
            if (gs->mode == MODE_CLIENT) {
                NetStatePacket sp;
                if (net_recv_state(&gs->net, &sp)) {
                    gs->players[0].score = sp.p0_score;
                    gs->players[1].score = sp.p1_score;
                    if ((GamePhase)sp.game_state == PHASE_PLAYING) {
                        // Host already started next round
                        PlayerSync s0 = sp.p0, s1 = sp.p1;
                        player_from_sync(&gs->players[0], &s0);
                        player_from_sync(&gs->players[1], &s1);
                        gs->phase = PHASE_PLAYING;
                        return;
                    }
                }
            }
        }
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
        if (gs->mode != MODE_LOCAL) net_update(&gs->net, gs->frame);
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
        input_buffer_consume(&gs->input_buf_p1, &p0_in, gs->frame);
        input_buffer_consume(&gs->input_buf_p2, &p1_in, gs->frame);
    } else if (gs->mode == MODE_HOST) {
        input_buffer_consume(&gs->input_buf_p1, &p0_in, gs->frame);
        net_push_local_input(&gs->net, &p0_in);
        net_get_remote_input(&gs->net, gs->frame, &p1_in);
        net_update(&gs->net, gs->frame);
    } else { // CLIENT
        input_buffer_consume(&gs->input_buf_p1, &p1_in, gs->frame);
        net_push_local_input(&gs->net, &p1_in);
        net_get_remote_input(&gs->net, gs->frame, &p0_in);
        net_update(&gs->net, gs->frame);

        NetStatePacket sp;
        if (net_recv_state(&gs->net, &sp)) {
            // Apply authoritative state for REMOTE player (p0) only.
            // Never overwrite local player (p1) position - that causes the snap-back glitch.
            // Client predicts its own movement freely.
            PlayerSync s0 = sp.p0;
            player_from_sync(&gs->players[0], &s0);

            // Apply authoritative scores
            gs->players[0].score = sp.p0_score;
            gs->players[1].score = sp.p1_score;

            // Apply authoritative game phase from host
            GamePhase host_phase = (GamePhase)sp.game_state;
            if (host_phase == PHASE_ROUND_OVER && gs->phase == PHASE_PLAYING) {
                gs->phase = PHASE_ROUND_OVER;
                gs->round_over_timer = 180;
                gs->winner_id = (gs->players[0].score > gs->players[1].score) ? 0 : 1;
            } else if (host_phase == PHASE_MATCH_OVER && gs->phase != PHASE_MATCH_OVER) {
                gs->phase = PHASE_MATCH_OVER;
                gs->winner_id = (gs->players[0].score >= WIN_SCORE) ? 0 : 1;
            } else if (host_phase == PHASE_PLAYING && gs->phase == PHASE_ROUND_OVER) {
                game_start_round(gs);
                gs->phase = PHASE_PLAYING;
            }

            // Only sync local player (p1) state/death from host - not position/velocity
            uint8_t host_p1_state = sp.p1.state;
            if (host_p1_state == (uint8_t)STATE_DEAD && gs->players[1].state != STATE_DEAD) {
                player_kill(&gs->players[1]);
            } else if (host_p1_state != (uint8_t)STATE_DEAD && gs->players[1].state == STATE_DEAD) {
                // Host says we respawned - accept full state including position
                PlayerSync s1 = sp.p1;
                player_from_sync(&gs->players[1], &s1);
            }
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

    // Only HOST and LOCAL run authoritative combat resolution.
    // CLIENT never kills players locally - all kills come from host via NetStatePacket.
    if (gs->mode != MODE_CLIENT) {
        combat_resolve(&gs->players[0], &gs->players[1],
                       gs->swords, MAX_THROWN_SWORDS);
    }

    // Death / respawn handling - HOST and LOCAL only
    // CLIENT receives authoritative kills/scores/phase from NetStatePacket
    if (gs->mode != MODE_CLIENT) {
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
    }

    update_camera(&gs->cam, &gs->players[0], &gs->players[1]);

    // Host sends authoritative state every frame - client needs this for position sync
    if (gs->mode == MODE_HOST && net_is_connected(&gs->net)) {
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

    gs->frame++;
}
void game_tick(GameState *gs, float dt) {
    // Poll inputs ONCE per real render frame so IsKeyPressed() edge events
    // are latched into the buffers before any fixed update runs.
    input_buffer_poll_p1(&gs->input_buf_p1);
    input_buffer_poll_p2(&gs->input_buf_p2);

    // Debug toggles: also sampled once per render frame, never inside fixed update
    if (IsKeyPressed(KEY_F1))  gs->debug_hitboxes = !gs->debug_hitboxes;
    if (IsKeyPressed(KEY_F2))  gs->debug_network  = !gs->debug_network;
    if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

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

static int g_local_player_id = 0;  // set each frame; used for coloring

// ----------------------------------------------------------------
// Resolution-independent rendering
// ----------------------------------------------------------------

static float world_scale(void) {
    return (float)g_screen_w() / VIRTUAL_W;
}

static int world_to_screen_x(float world_x, float cam_x) {
    float scale = world_scale() * g_cam_zoom;
    float half_sw = (float)g_screen_w() * 0.5f;
    return (int)((world_x - cam_x) * scale + half_sw);
}

static int world_to_screen_y(float world_y) {
    float scale = world_scale() * g_cam_zoom;
    // Map world ground (620) -> screen ground (82% of screen height)
    float screen_ground = (float)g_screen_h() * 0.82f;
    return (int)((world_y - WORLD_GROUND) * scale + screen_ground);
}

static void render_arena(float cam_x) {
    int sw = g_screen_w();
    int sh = g_screen_h();
    float gy = g_ground_y();
    // Fixed world arena: always 4000 units wide centered at 0
    float aw = 4000.0f;

    ClearBackground((Color){20, 15, 30, 255});

    // Background parallax columns (screen-space, purely visual)
    float para = cam_x * 0.3f;
    for (int i = 0; i < 10; i++) {
        float bx = fmodf(i * 160.0f - para * 0.5f, (float)sw + 160.0f) - 80.0f;
        DrawRectangle((int)bx, 80 + i * 20, 2, 60 + i * 8,
                      (Color){40 + i * 5, 30 + i * 5, 60 + i * 5, 120});
    }

    // Ground slab - use screen ground position for the fill rectangle
    int gx0 = world_to_screen_x(-aw * 0.5f, cam_x);
    int gx1 = world_to_screen_x( aw * 0.5f, cam_x);
    int gyi = world_to_screen_y(WORLD_GROUND);   // maps to 82% screen height
    DrawRectangle(gx0, gyi, gx1 - gx0, sh - gyi, (Color){45, 38, 55, 255});
    DrawLine(gx0, gyi, gx1, gyi, (Color){180, 140, 200, 255});

    // Floor tiles every 200 world units
    int num_tiles = (int)(aw / 200.0f) + 2;
    for (int tx = -num_tiles; tx <= num_tiles; tx++) {
        int tile_x = world_to_screen_x(tx * 200.0f, cam_x);
        DrawLine(tile_x, gyi, tile_x, gyi + 8, (Color){80, 60, 90, 200});
    }

    // Goal lines at fixed world positions
    int left_goal  = world_to_screen_x(-1960.0f, cam_x);
    int right_goal = world_to_screen_x( 1960.0f, cam_x);
    DrawLine(left_goal,  0, left_goal,  sh, (Color){255, 80, 80, 180});
    DrawLine(right_goal, 0, right_goal, sh, (Color){80, 80, 255, 180});

    // Mid platforms at fixed world positions
    float plat_world_y = WORLD_GROUND - 180.0f;   // 180 world units above ground
    float plat_world_w = 300.0f;
    int plat_yi = world_to_screen_y(plat_world_y);
    int plat_wi = (int)(plat_world_w * world_scale() * g_cam_zoom);

    int px0 = world_to_screen_x(-400.0f, cam_x);
    DrawRectangle(px0, plat_yi, plat_wi, 14, (Color){65, 55, 80, 255});
    DrawLine(px0, plat_yi, px0 + plat_wi, plat_yi, (Color){180, 140, 200, 200});

    int px1 = world_to_screen_x( 100.0f, cam_x);
    DrawRectangle(px1, plat_yi, plat_wi, 14, (Color){65, 55, 80, 255});
    DrawLine(px1, plat_yi, px1 + plat_wi, plat_yi, (Color){180, 140, 200, 200});

    (void)sw;
    (void)gy;
}

static void render_player(const Player *p, float cam_x, bool debug) {
    if (!p->ragdoll.active) {
        // All positions derived from world coords through transforms - no manual zoom scaling
        int foot_y = world_to_screen_y(p->body.pos.y + p->body.size.y);
        int top_y  = world_to_screen_y(p->body.pos.y);
        int h      = foot_y - top_y;   // screen-space height (auto-scaled)
        int w      = (int)(p->body.size.x * world_scale() * g_cam_zoom);
        int sy     = top_y;
        int sx     = world_to_screen_x(p->body.pos.x + p->body.size.x * 0.5f, cam_x);

        // Local player is always blue, opponent always red
        bool is_local = (p->id == g_local_player_id);
        Color body_color = is_local ? (Color){100, 180, 255, 255}
                                    : (Color){255, 120,  80, 255};
        Color stun_flash = (Color){255, 255, 100, 200};
        Color cur_color  = (p->state == STATE_STUNNED && (p->stun_timer % 6 < 3))
                           ? stun_flash : body_color;

        int hip_y      = sy + h - h / 3;
        int shoulder_y = sy + h / 3;
        int mid_x      = sx;
        float s        = world_scale() * g_cam_zoom;

        int leg_spread  = (int)(7.0f * s);
        int arm_reach   = (int)((p->crouching ? 8.0f : 14.0f) * s);
        int arm_drop    = (int)(10.0f * s);
        int head_r      = (int)(9.0f * s);
        if (head_r < 3) head_r = 3;

        DrawLine(mid_x, hip_y, mid_x - leg_spread, foot_y, cur_color);
        DrawLine(mid_x, hip_y, mid_x + leg_spread, foot_y, cur_color);
        DrawLine(mid_x, hip_y, mid_x, shoulder_y, cur_color);

        int sword_arm_x = mid_x + p->facing * arm_reach;
        int sword_arm_y = shoulder_y + arm_drop;
        DrawLine(mid_x, shoulder_y, sword_arm_x, sword_arm_y, cur_color);
        int off_arm_x = mid_x - p->facing * (int)(8.0f * s);
        DrawLine(mid_x, shoulder_y, off_arm_x, sword_arm_y + (int)(4.0f * s), cur_color);

        DrawCircle(mid_x, sy + head_r, head_r, cur_color);
        DrawCircle(mid_x + p->facing * (int)(4.0f * s), sy + (int)(7.0f * s), (int)(2.0f * s) + 1, (Color){20, 20, 40, 255});

        (void)w;
    } else {
        bool is_local_dead = (p->id == g_local_player_id);
        Color dead_color = is_local_dead ? (Color){60, 100, 160, 200}
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
        float s = world_scale() * g_cam_zoom;
        int hx = world_to_screen_x(p->hurtbox.x, cam_x);
        int hy = world_to_screen_y(p->hurtbox.y);
        int hw = (int)(p->hurtbox.w * s);
        int hh = (int)(p->hurtbox.h * s);
        DrawRectangleLines(hx, hy, hw, hh, GREEN);
        if (p->weapon_hitbox.w > 0) {
            int wx = world_to_screen_x(p->weapon_hitbox.x, cam_x);
            int wy = world_to_screen_y(p->weapon_hitbox.y);
            int ww = (int)(p->weapon_hitbox.w * s);
            int wh = (int)(p->weapon_hitbox.h * s);
            DrawRectangleLines(wx, wy, ww, wh, RED);
        }
        if (p->parry_box.w > 0) {
            int px2 = world_to_screen_x(p->parry_box.x, cam_x);
            int py2 = world_to_screen_y(p->parry_box.y);
            int pw  = (int)(p->parry_box.w * s);
            int ph  = (int)(p->parry_box.h * s);
            DrawRectangleLines(px2, py2, pw, ph, YELLOW);
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

    // local player label and score
    int local_id = gs->local_player_id;
    int remote_id = 1 - local_id;
    const char *local_label  = (gs->mode == MODE_LOCAL) ? "P1" : "YOU";
    const char *remote_label = (gs->mode == MODE_LOCAL) ? "P2" : "OPP";

    snprintf(buf, sizeof(buf), "%s: %d", local_label, gs->players[local_id].score);
    DrawText(buf, 40, 20, 28, (Color){100, 180, 255, 255});

    snprintf(buf, sizeof(buf), "%s: %d", remote_label, gs->players[remote_id].score);
    DrawText(buf, sw - 120, 20, 28, (Color){255, 120, 80, 255});

    snprintf(buf, sizeof(buf), "First to %d", WIN_SCORE);
    DrawText(buf, sw / 2 - 60, 20, 18, (Color){200, 200, 200, 180});

    snprintf(buf, sizeof(buf), "F:%u", gs->frame);
    DrawText(buf, sw - 80, sh - 24, 14, (Color){120, 120, 120, 180});

    if (gs->phase == PHASE_ROUND_OVER) {
        bool local_won = (gs->winner_id == gs->local_player_id);
        const char *msg = local_won ? "YOU WIN THE ROUND!" : "OPPONENT WINS ROUND!";
        if (gs->mode == MODE_LOCAL)
            msg = (gs->winner_id == 0) ? "PLAYER 1 WINS ROUND!" : "PLAYER 2 WINS ROUND!";
        int tw = MeasureText(msg, 40);
        DrawText(msg, sw / 2 - tw / 2, sh / 2 - 20, 40,
                 local_won ? (Color){100, 180, 255, 255} : (Color){255, 120, 80, 255});
    }

    if (gs->phase == PHASE_MATCH_OVER) {
        bool local_won = (gs->players[gs->local_player_id].score >= WIN_SCORE);
        const char *msg = local_won ? "YOU WIN THE MATCH!" : "OPPONENT WINS!";
        if (gs->mode == MODE_LOCAL)
            msg = (gs->players[0].score >= WIN_SCORE) ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!";
        int tw = MeasureText(msg, 52);
        DrawText(msg, sw / 2 - tw / 2, sh / 2 - 50, 52,
                 local_won ? (Color){100, 180, 255, 255} : (Color){255, 120, 80, 255});
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
    g_cam_zoom = gs->cam.zoom;
    g_local_player_id = gs->local_player_id;

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

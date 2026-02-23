#include "combat.h"
#include "physics.h"
#include <math.h>

// Check if attacker's weapon hitbox hits defender's hurtbox
// Returns true if kill, and also handles parry
static HitResult check_hit(Player *attacker, Player *defender) {
    Rect weapon = player_sword_rect(attacker);
    if (weapon.w == 0.0f) return HIT_NONE;  // no active hitbox

    Rect parry = player_parry_rect(defender);
    Rect body  = defender->hurtbox;

    // Check parry first (parry takes priority)
    if (parry.w > 0.0f && rect_overlap(weapon, parry)) {
        return HIT_PARRY;
    }

    // Check body hit
    if (rect_overlap(weapon, body)) {
        return HIT_BODY;
    }

    return HIT_NONE;
}

// Check if two swords clash (weapon hitboxes overlap)
static bool check_clash(Player *p0, Player *p1) {
    Rect w0 = player_sword_rect(p0);
    Rect w1 = player_sword_rect(p1);
    if (w0.w == 0.0f || w1.w == 0.0f) return false;
    return rect_overlap(w0, w1);
}

CombatResult combat_resolve(Player *p0, Player *p1, ThrowingSword *swords, int num_swords) {
    CombatResult result;
    result.p0_hit = HIT_NONE;
    result.p1_hit = HIT_NONE;
    result.sword_clash = false;

    if (p0->state == STATE_DEAD || p1->state == STATE_DEAD) return result;

    // Check sword clash first
    if (check_clash(p0, p1)) {
        result.sword_clash = true;
        // Cancel both attacks with brief stun
        if (p0->state == STATE_ATTACK) {
            p0->state = STATE_STUNNED;
            p0->stun_timer = 8;
            p0->state_timer = 0;
        }
        if (p1->state == STATE_ATTACK) {
            p1->state = STATE_STUNNED;
            p1->stun_timer = 8;
            p1->state_timer = 0;
        }
        return result;  // clash cancels further hits
    }

    // Check p0 hitting p1
    HitResult h01 = check_hit(p0, p1);
    // Check p1 hitting p0
    HitResult h10 = check_hit(p1, p0);

    // Resolve p0 -> p1
    if (h01 == HIT_PARRY) {
        result.p1_hit = HIT_PARRY;
        // p0 is stunned (attacker got parried)
        p0->state = STATE_STUNNED;
        p0->stun_timer = STUN_FRAMES;
        p0->state_timer = 0;
        // p1 gets recovery advantage (small knockback away)
        p1->body.vel.x += (float)p1->facing * 80.0f;
    } else if (h01 == HIT_BODY) {
        result.p1_hit = HIT_BODY;
        player_kill(p1);
    }

    // Resolve p1 -> p0 (only if p0 wasn't already killed above, and only if it actually attacked)
    if (h10 == HIT_PARRY && p1->state != STATE_DEAD) {
        result.p0_hit = HIT_PARRY;
        p1->state = STATE_STUNNED;
        p1->stun_timer = STUN_FRAMES;
        p1->state_timer = 0;
        p0->body.vel.x += (float)p0->facing * 80.0f;
    } else if (h10 == HIT_BODY && p0->state != STATE_DEAD) {
        result.p0_hit = HIT_BODY;
        player_kill(p0);
    }

    // Check thrown swords
    for (int i = 0; i < num_swords; i++) {
        ThrowingSword *s = &swords[i];
        if (!s->active) continue;

        Rect sword_rect;
        sword_rect.x = s->pos.x - 4;
        sword_rect.y = s->pos.y - 4;
        sword_rect.w = 8;
        sword_rect.h = 8;

        Player *targets[2] = {p0, p1};
        for (int t = 0; t < 2; t++) {
            Player *target = targets[t];
            if (target->id == s->owner) continue;
            if (target->state == STATE_DEAD) continue;

            Rect parry = player_parry_rect(target);
            if (parry.w > 0.0f && rect_overlap(sword_rect, parry)) {
                // Parried thrown sword - sword stops, target picks it up
                s->active = false;
                target->has_sword = true;
                break;
            }
            if (rect_overlap(sword_rect, target->hurtbox)) {
                player_kill(target);
                s->active = false;
                if (t == 0) result.p0_hit = HIT_BODY;
                else        result.p1_hit = HIT_BODY;
                break;
            }
        }

        // Also check if sword hits the sword-less player's body to give them the sword
        for (int t = 0; t < 2; t++) {
            Player *target = targets[t];
            if (!s->active) break;
            if (target->id != s->owner) continue;
            // Actually, let either player pick up a sword on ground
            // Ground pickup handled separately
        }
    }

    return result;
}

void combat_throw_sword(Player *p, ThrowingSword *sword) {
    sword->pos.x = p->sword_tip.x;
    sword->pos.y = p->sword_tip.y;
    sword->vel.x = (float)p->facing * SWORD_THROW_SPEED;
    sword->vel.y = p->body.vel.y * 0.5f - 50.0f;
    sword->angle = 0.0f;
    sword->angle_vel = (float)p->facing * 15.0f;
    sword->active = true;
    sword->owner = p->id;
}

void combat_update_thrown_swords(ThrowingSword *swords, int num_swords,
                                  Player *p0, Player *p1, float dt) {
    Player *players[2] = {p0, p1};

    for (int i = 0; i < num_swords; i++) {
        ThrowingSword *s = &swords[i];
        if (!s->active) continue;

        s->vel.y += GRAVITY * dt;
        s->pos.x += s->vel.x * dt;
        s->pos.y += s->vel.y * dt;
        s->angle += s->angle_vel * dt;

        // Ground: sword sticks
        if (s->pos.y >= g_ground_y()) {
            s->pos.y = g_ground_y();
            s->vel.x = 0;
            s->vel.y = 0;
            s->angle_vel = 0;
        }

        // Pickup: player without sword walks over it
        Rect sword_rect;
        sword_rect.x = s->pos.x - 10;
        sword_rect.y = s->pos.y - 10;
        sword_rect.w = 20;
        sword_rect.h = 20;

        for (int j = 0; j < 2; j++) {
            Player *pl = players[j];
            if (pl->has_sword) continue;
            if (pl->state == STATE_DEAD) continue;
            if (rect_overlap(sword_rect, pl->hurtbox)) {
                pl->has_sword = true;
                s->active = false;
                break;
            }
        }

        // Out of bounds
        if (s->pos.x < -3000.0f || s->pos.x > 3000.0f) {
            s->active = false;
        }
    }
}

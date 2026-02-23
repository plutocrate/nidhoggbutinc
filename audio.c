#include "audio.h"
#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Footstep cadence: every N fixed-update frames (60hz)
#define FOOTSTEP_FRAMES 18

// Slight random pitch variation for naturalness (±range around 1.0)
static float rand_pitch(float range) {
    return 1.0f + ((float)(rand() % 1000) / 1000.0f - 0.5f) * range;
}

static Sound load_sfx(const char *path) {
    Sound s = LoadSound(path);
    if (!IsSoundValid(s)) {
        fprintf(stderr, "[AUDIO] Failed to load: %s\n", path);
    }
    return s;
}

bool audio_init(AudioState *a) {
    *a = (AudioState){0};

    a->sfx_whoosh       = load_sfx("WEAPSwrd_WoodwWhoosh_HoveAud_SwordCombat_06.wav");
    a->sfx_hit          = load_sfx("WEAPSwrd_SwordStabCombowRing_HoveAud_SwordCombat_17.wav");
    a->sfx_parry        = load_sfx("WEAPSwrd_BaseMetal_HoveAud_SwordCombat_03.wav");
    a->sfx_grunt_attack = load_sfx("VOXEfrt_ActionGrunt_HoveAud_SwordCombat_54.wav");
    a->sfx_grunt_death  = load_sfx("VOXScrm_DamageGrunt_HoveAudio_SwordCombat_13.wav");
    a->sfx_footstep     = load_sfx("Antons_Footsteps_FS_Sand_Walk_03.wav");
    a->sfx_jump         = load_sfx("Jump.wav");

    a->initialized = true;
    return true;
}

void audio_shutdown(AudioState *a) {
    if (!a->initialized) return;
    UnloadSound(a->sfx_whoosh);
    UnloadSound(a->sfx_hit);
    UnloadSound(a->sfx_parry);
    UnloadSound(a->sfx_grunt_attack);
    UnloadSound(a->sfx_grunt_death);
    UnloadSound(a->sfx_footstep);
    UnloadSound(a->sfx_jump);
    a->initialized = false;
}

void audio_update(AudioState *a,
                  const Player players[2],
                  const PlayerState prev_states[2],
                  const bool prev_on_ground[2],
                  CombatResult combat,
                  const Input inputs[2]) {
    if (!a->initialized) return;

    // --- COMBAT SOUNDS ---

    // Death blow: sword hits body
    bool p0_died = (combat.p0_hit == HIT_BODY);
    bool p1_died = (combat.p1_hit == HIT_BODY);

    if (p0_died || p1_died) {
        // Sword impact sound
        SetSoundPitch(a->sfx_hit, rand_pitch(0.08f));
        SetSoundVolume(a->sfx_hit, 1.0f);
        PlaySound(a->sfx_hit);

        // Death grunt for the player who died
        SetSoundPitch(a->sfx_grunt_death, rand_pitch(0.10f));
        SetSoundVolume(a->sfx_grunt_death, 1.0f);
        PlaySound(a->sfx_grunt_death);
    }

    // Parry (melee or thrown sword)
    if (combat.p0_hit == HIT_PARRY || combat.p1_hit == HIT_PARRY || combat.sword_clash) {
        SetSoundPitch(a->sfx_parry, rand_pitch(0.06f));
        SetSoundVolume(a->sfx_parry, 1.0f);
        PlaySound(a->sfx_parry);
    }

    // --- PER-PLAYER STATE-TRANSITION SOUNDS ---
    for (int i = 0; i < 2; i++) {
        const Player *p   = &players[i];
        PlayerAudioState *pa = &a->players[i];
        PlayerState prev  = prev_states[i];

        bool just_attacked = (prev != STATE_ATTACK && p->state == STATE_ATTACK);
        bool just_jumped   = (prev_on_ground[i] && !p->body.on_ground &&
                              inputs[i].jump);

        // Attack swing — whoosh + grunt
        // Grunt is muffled if this attack landed a kill this same frame
        if (just_attacked) {
            SetSoundPitch(a->sfx_whoosh, rand_pitch(0.10f));
            SetSoundVolume(a->sfx_whoosh, 0.85f);
            PlaySound(a->sfx_whoosh);

            // Grunt: muffled if this player's attack killed the opponent
            bool this_player_killed = (i == 0 && p1_died) || (i == 1 && p0_died);
            float grunt_vol = this_player_killed ? 0.25f : 0.80f;
            SetSoundPitch(a->sfx_grunt_attack, rand_pitch(0.12f));
            SetSoundVolume(a->sfx_grunt_attack, grunt_vol);
            PlaySound(a->sfx_grunt_attack);
        }

        // Jump
        if (just_jumped) {
            SetSoundPitch(a->sfx_jump, rand_pitch(0.08f));
            SetSoundVolume(a->sfx_jump, 0.75f);
            PlaySound(a->sfx_jump);
        }

        // Footsteps: only while walking on ground, timed to cadence
        bool is_walking = (p->state == STATE_WALK || p->state == STATE_CROUCH) &&
                          p->body.on_ground &&
                          fabsf(p->body.vel.x) > 20.0f;

        if (is_walking) {
            if (pa->footstep_timer <= 0) {
                // Vary pitch and volume slightly per step
                SetSoundPitch(a->sfx_footstep, rand_pitch(0.12f));
                SetSoundVolume(a->sfx_footstep, 0.45f + rand_pitch(0.1f) * 0.1f);
                PlaySound(a->sfx_footstep);
                // Faster cadence when moving quickly
                float speed_t = fabsf(p->body.vel.x) / 330.0f;  // 0..1
                pa->footstep_timer = (int)(FOOTSTEP_FRAMES * (1.0f - speed_t * 0.35f));
            } else {
                pa->footstep_timer--;
            }
        } else {
            // Reset timer so next step fires immediately when walking resumes
            pa->footstep_timer = 0;
        }
    }
}

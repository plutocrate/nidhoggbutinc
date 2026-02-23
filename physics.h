#ifndef PHYSICS_H
#define PHYSICS_H

#include "types.h"

typedef struct PhysicsBody {
    Vec2  pos;       // top-left of bounding box
    Vec2  vel;
    Vec2  size;
    bool  on_ground;
    bool  was_on_ground;
} PhysicsBody;

// ground_y is passed in so physics stays decoupled from screen state
void physics_init_body(PhysicsBody *b, float x, float y, float w, float h);
void physics_update(PhysicsBody *b, float dt, float ground_y);
void physics_apply_jump(PhysicsBody *b);
void physics_apply_walk(PhysicsBody *b, float dir, float dt);

// AABB helpers
bool rect_overlap(Rect a, Rect b);
Rect body_to_rect(const PhysicsBody *b);
Rect rect_from_center(float cx, float cy, float w, float h);

#endif

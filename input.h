#ifndef INPUT_H
#define INPUT_H

#include "types.h"

// Player 1: WASD + J (attack) + K (parry) + L (throw)
// Player 2 (local only): Arrow keys + numpad
// In multiplayer, both use same layout (P1 keys) since each is on own machine

void input_init(void);
Input input_gather_p1(uint32_t frame);
Input input_gather_p2(uint32_t frame);  // local only

// Serialization for network
void   input_serialize(const Input *in, uint8_t *buf, int *len);
bool   input_deserialize(Input *in, const uint8_t *buf, int len);

#endif

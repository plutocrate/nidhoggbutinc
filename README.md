# DUEL — A Nidhogg-inspired Fencing Game

A precise, deterministic 1v1 fencing game with local and online multiplayer.
Built in C with raylib. No external physics or networking libraries.

---

## Building

### Prerequisites

Install raylib (v4.5+ recommended):

**Ubuntu/Debian:**
```bash
sudo apt install libraylib-dev
# OR build from source:
git clone https://github.com/raysan5/raylib.git
cd raylib/src && make PLATFORM=PLATFORM_DESKTOP
sudo make install
```

**Arch Linux:**
```bash
sudo pacman -S raylib
```

**Windows (MSYS2/MinGW64):**
```bash
pacman -S mingw-w64-x86_64-raylib
```

---

### Linux Build
```bash
make linux
./duel
```

### Windows Build (Native MinGW64)
```bash
gcc -std=c11 -Wall -O2 main.c game.c player.c physics.c combat.c network.c input.c \
    -o duel.exe -lraylib -lwinmm -lgdi32 -lopengl32 -lws2_32 -lm
duel.exe
```

### Cross-compile for Windows from Linux
```bash
make windows
```

---

## Running

### Local 2-Player (same keyboard)
```bash
./duel local
# OR just ./duel → opens menu
```

### Online — Host
```bash
./duel host
# Listens on UDP port 7777
# Tell your opponent your IP address
```

### Online — Join
```bash
./duel client 192.168.1.42
```

---

## Controls

| Action        | Player 1     | Player 2 (local only) |
|---------------|--------------|------------------------|
| Move Left     | A            | ←                      |
| Move Right    | D            | →                      |
| Jump          | W            | ↑                      |
| Crouch        | S            | ↓                      |
| Attack        | J            | Numpad 1               |
| Parry         | K            | Numpad 2               |
| Throw Sword   | L            | Numpad 3               |

**In online multiplayer, both players use the P1 controls** (each on their own machine).

### Debug Keys
| Key | Action |
|-----|--------|
| F1  | Toggle hitbox visualization |
| F2  | Toggle network debug overlay |

---

## Combat System

### Attack
- Press **J** to thrust forward
- Active hitbox window: frames 2–9 of a 12-frame animation
- One hit = instant death

### Parry
- Press **K** to parry
- Active window: frames 2–7 of a 10-frame animation
- If you parry an attack: attacker is stunned for 25 frames
- If you mis-time a parry: you have 18 recovery frames of vulnerability

### Sword Clash
- If both players attack simultaneously and their hitboxes overlap: both are briefly stunned (8 frames)

### Throw
- Press **L** to throw your sword
- Thrown sword travels in an arc, spinning
- Landing on the sword (walking over it) picks it up
- You can parry a thrown sword to catch it

---

## Architecture

```
main.c      — Entry point, mode selection, main loop
game.c/h    — Game state, fixed timestep, rendering
player.c/h  — Player state machine, physics body, ragdoll
physics.c/h — Velocity/gravity/friction/AABB (no external engine)
combat.c/h  — Hit detection, parry resolution, sword throw
network.c/h — UDP sockets, input serialization, state sync
input.c/h   — Keyboard capture, Input struct serialization
types.h     — Shared types: Vec2, Rect, Input, PlayerState, etc.
```

### Networking Model
- **Input-based sync**: each client sends their inputs to the host
- **Host is authoritative**: host runs the simulation and sends state corrections
- **Non-blocking UDP**: zero blocking calls in the game loop
- **Prediction**: missing remote inputs repeat last known (clears one-shot actions)
- **State correction**: host sends full PlayerSync every 2 frames to correct drift
- **Ping measurement**: PING/PONG packets every second

---

## Physics
- Fixed timestep: 60 Hz
- Gravity: 1400 units/s²
- Jump velocity: −520 units/s
- Walk speed (max): 220 units/s
- Ground friction: ×0.82/frame
- Air friction: ×0.98/frame

---

## Scoring
- First to **5 kills** wins the match
- After each kill, loser respawns behind the winner
- Camera scrolls to track both players

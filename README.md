### Prerequisites

Install raylib (v4.5+ recommended):

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libxinerama-dev libxcursor-dev
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
**Compilation**

```bash
make linux
./duel
```

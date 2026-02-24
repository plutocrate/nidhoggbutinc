#ifndef NETWORK_H
#define NETWORK_H

#include "types.h"
#include "player.h"
#include "input.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN  // strips rarely-used Win32 APIs (GDI, USER, etc.)
  #define NOGDI                // exclude GDI: removes Rectangle() conflict
  #define NOUSER               // exclude USER: removes DrawText/CloseWindow/ShowCursor conflicts
  #define MMNOSOUND            // exclude mmsystem sound: removes PlaySound conflict
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET net_socket_t;
  #define NET_INVALID_SOCKET INVALID_SOCKET
  #define net_close(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  typedef int net_socket_t;
  #define NET_INVALID_SOCKET (-1)
  #define net_close(s) close(s)
#endif

typedef enum NetRole {
    NET_NONE,
    NET_HOST,
    NET_CLIENT,
} NetRole;

typedef enum NetPacketType {
    PKT_HANDSHAKE     = 1,
    PKT_HANDSHAKE_ACK = 2,
    PKT_INPUT         = 3,
    PKT_STATE         = 4,  // host->client authoritative state
    PKT_PING          = 5,
    PKT_PONG          = 6,
    PKT_READY         = 7,
    PKT_START         = 8,
    // Relay hole-punch packets (sent to relay server, not to peer)
    PKT_RELAY_JOIN    = 20, // [1 byte type] [4 bytes room code]
    PKT_RELAY_PEER    = 21, // [1 byte type] [4 bytes IPv4] [2 bytes port BE]
} NetPacketType;

// All packets start with this header
typedef struct NetHeader {
    uint8_t  type;
    uint32_t frame;
} __attribute__((packed)) NetHeader;

typedef struct NetInputPacket {
    NetHeader header;
    uint8_t   input_data[5];  // serialized Input
} __attribute__((packed)) NetInputPacket;

typedef struct NetStatePacket {
    NetHeader  header;
    PlayerSync p0;
    PlayerSync p1;
    uint8_t    game_state; // 0=waiting, 1=playing, 2=round_over
    uint8_t    p0_score;
    uint8_t    p1_score;
} __attribute__((packed)) NetStatePacket;

typedef struct NetPingPacket {
    NetHeader header;
    uint32_t  send_time_ms;
} __attribute__((packed)) NetPingPacket;

#define INPUT_HISTORY_SIZE 16

typedef struct NetState {
    NetRole       role;
    net_socket_t  sock;
    struct sockaddr_in peer_addr;
    bool          peer_connected;

    // Input buffers
    Input  local_inputs[INPUT_HISTORY_SIZE];
    Input  remote_inputs[INPUT_HISTORY_SIZE];
    int    local_head;
    int    remote_head;

    // Frame tracking
    uint32_t local_frame;
    uint32_t remote_frame;
    int      input_delay;  // frames of delay

    // Latency
    uint32_t ping_ms;
    uint32_t last_ping_time;

    // Connection state
    bool connected;
    bool ready_sent;
    bool match_started;
    char peer_ip[64];

    // Relay hole-punch state (used when connecting via relay server)
    bool     using_relay;
    char     relay_ip[64];
    char     room_code[5];           // 4 chars + null
    struct sockaddr_in relay_addr;
    bool     relay_peer_found;       // relay gave us the peer's public addr
    uint32_t relay_last_join_ms;     // last time we sent PKT_RELAY_JOIN
} NetState;

// Lifecycle
bool net_init(NetState *net, NetRole role, const char *peer_ip);
// Init with relay hole-punch. relay_ip = VPS public IP, room_code = 4-char shared code.
// Both players call this with the same room_code; role is still HOST/CLIENT (first=host).
bool net_init_relay(NetState *net, const char *relay_ip, const char *room_code);
void net_shutdown(NetState *net);

// Called every frame
void net_update(NetState *net, uint32_t frame_ms);

// Input management
void net_push_local_input(NetState *net, const Input *in);
bool net_get_remote_input(NetState *net, uint32_t frame, Input *out);

// State sync (host only)
void net_send_state(NetState *net, const NetStatePacket *pkt);

// Receive state (client only)
bool net_recv_state(NetState *net, NetStatePacket *pkt);

bool net_is_connected(const NetState *net);
uint32_t net_get_ping(const NetState *net);

// Platform init/shutdown
bool net_platform_init(void);
void net_platform_shutdown(void);

#endif

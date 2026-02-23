#define _POSIX_C_SOURCE 199309L
#include "network.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>
  static uint32_t get_time_ms(void) { return (uint32_t)GetTickCount(); }
#else
  #include <time.h>
  static uint32_t get_time_ms(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
  }
#endif

bool net_platform_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void net_platform_shutdown(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static bool set_nonblocking(net_socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool net_init(NetState *net, NetRole role, const char *peer_ip) {
    memset(net, 0, sizeof(*net));
    net->role = role;
    net->input_delay = INPUT_BUFFER_FRAMES;
    net->ping_ms = 0;

    if (peer_ip) {
        strncpy(net->peer_ip, peer_ip, sizeof(net->peer_ip) - 1);
    }

    net->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (net->sock == NET_INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        return false;
    }

    if (!set_nonblocking(net->sock)) {
        fprintf(stderr, "set_nonblocking() failed\n");
        net_close(net->sock);
        return false;
    }

    // Bind to port
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(NET_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(net->sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        fprintf(stderr, "bind() failed on port %d\n", NET_PORT);
        net_close(net->sock);
        return false;
    }

    // Set peer address for client
    if (role == NET_CLIENT && peer_ip) {
        memset(&net->peer_addr, 0, sizeof(net->peer_addr));
        net->peer_addr.sin_family = AF_INET;
        net->peer_addr.sin_port = htons(NET_PORT);
        inet_pton(AF_INET, peer_ip, &net->peer_addr.sin_addr);
    }

    printf("[NET] %s initialized on port %d\n",
           role == NET_HOST ? "Host" : "Client", NET_PORT);
    return true;
}

void net_shutdown(NetState *net) {
    if (net->sock != NET_INVALID_SOCKET) {
        net_close(net->sock);
        net->sock = NET_INVALID_SOCKET;
    }
}

static void net_send_raw(NetState *net, const void *data, int len) {
    if (!net->peer_connected && net->role == NET_HOST) return;
    sendto(net->sock, (const char *)data, len, 0,
           (struct sockaddr *)&net->peer_addr, sizeof(net->peer_addr));
}

static void net_send_ping(NetState *net) {
    NetPingPacket pkt;
    pkt.header.type = PKT_PING;
    pkt.header.frame = net->local_frame;
    pkt.send_time_ms = get_time_ms();
    net_send_raw(net, &pkt, sizeof(pkt));
    net->last_ping_time = pkt.send_time_ms;
}

static void net_handle_packet(NetState *net, const uint8_t *buf, int len,
                               struct sockaddr_in *from) {
    if (len < 1) return;
    uint8_t type = buf[0];

    switch (type) {
        case PKT_HANDSHAKE: {
            // Host receives this from client
            if (net->role == NET_HOST) {
                net->peer_addr = *from;
                net->peer_connected = true;
                // Send ACK
                NetHeader ack;
                ack.type = PKT_HANDSHAKE_ACK;
                ack.frame = 0;
                net_send_raw(net, &ack, sizeof(ack));
                printf("[NET] Client connected from %s\n", inet_ntoa(from->sin_addr));
            }
            break;
        }
        case PKT_HANDSHAKE_ACK: {
            if (net->role == NET_CLIENT) {
                net->peer_connected = true;
                net->connected = true;
                printf("[NET] Connected to host\n");
            }
            break;
        }
        case PKT_INPUT: {
            if (len < (int)sizeof(NetInputPacket)) break;
            const NetInputPacket *ip = (const NetInputPacket *)buf;
            Input in;
            if (input_deserialize(&in, ip->input_data, 5)) {
                int idx = in.frame % INPUT_HISTORY_SIZE;
                net->remote_inputs[idx] = in;
                if (in.frame > net->remote_frame) {
                    net->remote_frame = in.frame;
                }
            }
            break;
        }
        case PKT_STATE: {
            // Store for retrieval by game
            // We keep the last received state packet in a simple ring approach
            // The game polls net_recv_state separately
            break;
        }
        case PKT_PING: {
            if (len < (int)sizeof(NetPingPacket)) break;
            const NetPingPacket *pp = (const NetPingPacket *)buf;
            NetPingPacket pong;
            pong.header.type = PKT_PONG;
            pong.header.frame = net->local_frame;
            pong.send_time_ms = pp->send_time_ms;
            net_send_raw(net, &pong, sizeof(pong));
            break;
        }
        case PKT_PONG: {
            if (len < (int)sizeof(NetPingPacket)) break;
            const NetPingPacket *pp = (const NetPingPacket *)buf;
            uint32_t now = get_time_ms();
            net->ping_ms = now - pp->send_time_ms;
            break;
        }
        case PKT_READY: {
            if (net->role == NET_HOST) {
                // Start match
                NetHeader start;
                start.type = PKT_START;
                start.frame = 0;
                net_send_raw(net, &start, sizeof(start));
                net->match_started = true;
                net->connected = true;
            }
            break;
        }
        case PKT_START: {
            if (net->role == NET_CLIENT) {
                net->match_started = true;
            }
            break;
        }
    }
}

// Last received state packet (double-buffered)
static NetStatePacket g_last_state_pkt;
static bool g_state_pkt_fresh = false;

static void net_recv_loop(NetState *net) {
    uint8_t buf[MAX_PACKET_SIZE];
    struct sockaddr_in from;

#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif

    for (;;) {
        int n = recvfrom(net->sock, (char *)buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            if (n < 0 && !would_block()) {
                fprintf(stderr, "[NET] recvfrom error\n");
            }
            break;
        }

        // Handle state packets directly here since they need separate storage
        if (n >= 1 && buf[0] == PKT_STATE && n >= (int)sizeof(NetStatePacket)) {
            memcpy(&g_last_state_pkt, buf, sizeof(NetStatePacket));
            g_state_pkt_fresh = true;
        }

        net_handle_packet(net, buf, n, &from);
    }
}

void net_update(NetState *net, uint32_t frame_ms) {
    (void)frame_ms;

    // Client: send handshake until connected
    if (net->role == NET_CLIENT && !net->peer_connected) {
        static uint32_t last_hs = 0;
        uint32_t now = get_time_ms();
        if (now - last_hs > 500) {
            NetHeader hs;
            hs.type = PKT_HANDSHAKE;
            hs.frame = 0;
            net_send_raw(net, &hs, sizeof(hs));
            last_hs = now;
        }
    }

    // Host: connected but not started - wait for ready
    if (net->role == NET_HOST && net->peer_connected && !net->connected) {
        net->connected = true;
    }

    // Send ready after connecting
    if (net->connected && !net->ready_sent) {
        if (net->role == NET_CLIENT) {
            NetHeader rdy;
            rdy.type = PKT_READY;
            rdy.frame = 0;
            net_send_raw(net, &rdy, sizeof(rdy));
        }
        net->ready_sent = true;
    }

    // Ping every second
    uint32_t now = get_time_ms();
    if (net->connected && now - net->last_ping_time > 1000) {
        net_send_ping(net);
    }

    net_recv_loop(net);
}

void net_push_local_input(NetState *net, const Input *in) {
    int idx = in->frame % INPUT_HISTORY_SIZE;
    net->local_inputs[idx] = *in;
    net->local_frame = in->frame;

    if (!net->connected) return;

    // Send input packet
    NetInputPacket pkt;
    pkt.header.type = PKT_INPUT;
    pkt.header.frame = in->frame;
    int slen = 0;
    input_serialize(in, pkt.input_data, &slen);
    net_send_raw(net, &pkt, sizeof(pkt));
}

bool net_get_remote_input(NetState *net, uint32_t frame, Input *out) {
    int idx = frame % INPUT_HISTORY_SIZE;
    if (net->remote_inputs[idx].frame == frame) {
        *out = net->remote_inputs[idx];
        return true;
    }
    // Input not yet received - use last known input (prediction: repeat last)
    if (net->remote_frame > 0) {
        int last_idx = net->remote_frame % INPUT_HISTORY_SIZE;
        *out = net->remote_inputs[last_idx];
        out->frame = frame;
        // Clear instantaneous actions (jump, attack etc.) to avoid repeated triggers
        out->jump = false;
        out->attack = false;
        out->parry = false;
        out->throw_weapon = false;
        return true;
    }
    memset(out, 0, sizeof(*out));
    out->frame = frame;
    return false;
}

void net_send_state(NetState *net, const NetStatePacket *pkt) {
    if (!net->connected) return;
    net_send_raw(net, pkt, sizeof(*pkt));
}

bool net_recv_state(NetState *net, NetStatePacket *pkt) {
    (void)net;
    if (g_state_pkt_fresh) {
        *pkt = g_last_state_pkt;
        g_state_pkt_fresh = false;
        return true;
    }
    return false;
}

bool net_is_connected(const NetState *net) {
    return net->match_started;
}

uint32_t net_get_ping(const NetState *net) {
    return net->ping_ms;
}

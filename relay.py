#!/usr/bin/env python3
"""
DUEL - UDP Hole-Punch Relay Server
===================================
Deploy this on any VPS with a public IP (e.g. DigitalOcean, Linode, Vultr).
Run:  python3 relay.py
Default port: 7778 (separate from game port 7777)

How it works:
  1. Both players send PKT_RELAY_JOIN (type=20) with a 4-char room code.
  2. When two peers share the same room code, the relay sends each one
     PKT_RELAY_PEER_ADDR (type=21) containing the other's public ip:port.
  3. Both clients then hole-punch directly to each other.
     The relay is done -- no game traffic passes through it.

Packet format (all UDP, no TCP):
  PKT_RELAY_JOIN    [1 byte type=20] [4 bytes room code ASCII]
  PKT_RELAY_PEER_ADDR [1 byte type=21] [4 bytes IPv4] [2 bytes port big-endian]
"""

import socket
import struct
import time

RELAY_PORT       = 7778
PKT_RELAY_JOIN   = 20
PKT_RELAY_PEER   = 21
ROOM_TTL_SECONDS = 120   # room expires if second player doesn't join within 2 min

def run():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', RELAY_PORT))
    print(f"[relay] Listening on UDP :{RELAY_PORT}")

    # rooms: code(str) -> {'addr': (ip,port), 'time': float}
    rooms = {}

    while True:
        try:
            data, addr = sock.recvfrom(64)
        except Exception as e:
            print(f"[relay] recv error: {e}")
            continue

        if len(data) < 5 or data[0] != PKT_RELAY_JOIN:
            continue

        room_code = data[1:5].decode('ascii', errors='replace').strip('\x00')
        now = time.time()

        print(f"[relay] JOIN room={room_code!r} from {addr}")

        if room_code in rooms:
            entry = rooms[room_code]
            # Check TTL
            if now - entry['time'] > ROOM_TTL_SECONDS:
                # Expired — this peer becomes the new first player
                rooms[room_code] = {'addr': addr, 'time': now}
                print(f"[relay] room {room_code!r} expired, reset with {addr}")
                continue

            # Second player arrived — send each peer the other's address
            peer1 = entry['addr']
            peer2 = addr

            if peer1 == peer2:
                # Same address (testing locally) — skip
                continue

            def make_peer_pkt(ip_str, port):
                packed_ip = socket.inet_aton(ip_str)
                return struct.pack('!B4sH', PKT_RELAY_PEER, packed_ip, port)

            pkt_for_2 = make_peer_pkt(peer1[0], peer1[1])
            pkt_for_1 = make_peer_pkt(peer2[0], peer2[1])

            sock.sendto(pkt_for_1, peer1)
            sock.sendto(pkt_for_2, peer2)

            print(f"[relay] Matched! {peer1} <-> {peer2} in room {room_code!r}")
            del rooms[room_code]

        else:
            # First player — register and wait
            rooms[room_code] = {'addr': addr, 'time': now}
            print(f"[relay] Waiting for second player in room {room_code!r}")

        # Expire old rooms
        expired = [k for k, v in rooms.items() if now - v['time'] > ROOM_TTL_SECONDS]
        for k in expired:
            print(f"[relay] Expiring room {k!r}")
            del rooms[k]

if __name__ == '__main__':
    run()

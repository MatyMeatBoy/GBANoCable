#!/usr/bin/env python3
"""GBANoCable room relay for a VPS: up to 4 players meet by a room code; their datagrams are passed to the other
members of the room. Standard library only. Copyright (c) 2026 MatyMeatBoy. MIT License.

    python3 relay.py [--port 47800]

Datagrams:
    'J' seat code      join or refresh a room (seat 1-4; code up to 16 ASCII characters). Send it every second:
                       a player whose address changed is found again.
    'O' n              the relay's answer: n players in the room.
    'D' payload        a game datagram, passed as it came to every other member of the room.
A room nobody has written to for 10 minutes is forgotten. Nothing is stored beyond the room's addresses.
"""
import argparse
import asyncio
import time

IDLE = 600


class Relay(asyncio.DatagramProtocol):
    def __init__(self):
        self.rooms = {}  # code -> {seat: (address, last time)}
        self.where = {}  # address -> (code, seat)

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        now = time.time()
        if not data:
            return
        kind = data[:1]
        if kind == b'J' and len(data) >= 3:
            seat, code = data[1], data[2:18].decode('ascii', 'replace')
            if not 1 <= seat <= 4:
                return
            room = self.rooms.setdefault(code, {})
            old = room.get(seat)
            if old and old[0] != addr:
                self.where.pop(old[0], None)
            room[seat] = (addr, now)
            self.where[addr] = (code, seat)
            self.transport.sendto(b'O' + bytes([len(room)]), addr)
        elif kind == b'D':
            place = self.where.get(addr)
            if not place:
                return
            code, seat = place
            room = self.rooms.get(code, {})
            room[seat] = (addr, now)
            for other, (oaddr, _) in room.items():
                if other != seat:
                    self.transport.sendto(data, oaddr)

    def sweep(self):
        now = time.time()
        for code in [c for c, r in self.rooms.items() if all(now - t > IDLE for _, t in r.values())]:
            for addr, _ in self.rooms[code].values():
                self.where.pop(addr, None)
            del self.rooms[code]


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=47800)
    args = ap.parse_args()
    loop = asyncio.get_running_loop()
    _, relay = await loop.create_datagram_endpoint(Relay, local_addr=('0.0.0.0', args.port))
    print(f'GBANoCable relay on UDP {args.port}', flush=True)
    while True:
        await asyncio.sleep(60)
        relay.sweep()


if __name__ == '__main__':
    asyncio.run(main())

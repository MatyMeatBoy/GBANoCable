#!/usr/bin/env python3
"""GBANoCable room relay for a VPS: players meet by a room code; their datagrams are passed to the other members
of the room. Public rooms can be listed (a small room server). Standard library only.
Copyright (c) 2026 MatyMeatBoy. MIT License.

    python3 relay.py [--port 47800] [--seats 2] [--max-rooms 128]
    python3 relay.py --port 47801 --lag-ms 60 --jitter-ms 40 --loss 2     (local tests only: an impaired network)

Datagrams (codes are ASCII A-Z 0-9):
    'J' seat code          join or refresh a room (seat 1..--seats, code 1-16 characters). Send it every second.
                           Seat 1 is the host: other seats can only join a room whose host is there.
    'O' n                  the relay's answer to J: n players in the room.
    'D' payload            a game datagram, passed as it came to every other member of the room.
    'A' version region code[16] name
                           the host announces its room as public (send it every second while it waits for players).
                           code is NUL padded; name is 0-24 bytes of UTF-8 without control characters.
    'L' version 0...       list the public rooms of that link protocol version. Pad the request with zeros to the
                           size of your receive buffer (up to 512 bytes): the answer is never larger than the
                           request, so a spoofed small request cannot be amplified.
    'R' n (region code[16] namelen name)*n
                           the answer: rooms of that version still announced in the last 5 s with a free seat.

Hardening (what a public UDP service needs): every datagram is validated before any state is kept for its sender;
a monotonic clock for every lease; limits on rooms, on players behind one IP and on tracked IPs; a per-IP and a
global token bucket; an occupied seat is never taken over by another address (a player whose address changed gets
its seat back when the old lease expires, --peer-timeout seconds); no authentication or encryption is implied: a
room code is a public identifier, not a password. Nothing is logged about players.

Run one process (one port) per game and protocol: rooms of different games must never share a namespace.

Test impairment (--lag-ms, --jitter-ms, --loss): forwarded game datagrams are held and dropped by wall-clock time,
not by frames (a game that waits for the other player makes no frames: a frame-counted delay never expires).
Never use these options on a public relay.
"""
import argparse
import asyncio
import collections
import random
import re
import time

SEATS = 2
IDLE = 30            # seconds a seat is kept without datagrams
PUBLIC_FRESH = 5     # seconds an announcement keeps a room listed
LIST_MAX = 20        # rooms in one list answer
LIST_BYTES = 512     # the largest list answer (and list request)
MAX_DATAGRAM = 513   # 'D' + a 512-byte game datagram
NAME_BYTES = 24
RATE_IDLE = 60       # seconds before an idle IP's bucket is forgotten
PEERS_PER_IP = 16    # players behind one address (NAT) allowed at once
CODE = re.compile(rb'[A-Z0-9]{1,16}\Z')


class Relay(asyncio.DatagramProtocol):
    def __init__(self, *, seats=SEATS, max_rooms=128, peer_timeout=IDLE, rate=600, burst=1200, max_ips=4096,
                 global_rate=10000, lag=0.0, jitter=0.0, loss=0.0, seed=None, clock=time.monotonic):
        if not 2 <= seats <= 4:
            raise ValueError('seats must be 2..4')
        if min(max_rooms, peer_timeout, rate, burst, max_ips, global_rate) <= 0:
            raise ValueError('relay limits must be positive')
        if lag < 0 or jitter < 0 or not 0 <= loss < 100:
            raise ValueError('impairment must be lag/jitter >= 0 and loss 0..99')
        self.seats, self.max_rooms, self.peer_timeout = seats, max_rooms, peer_timeout
        self.rate, self.burst, self.max_ips = rate, burst, max_ips
        self.global_rate = global_rate
        self.global_tokens, self.global_at = float(2 * global_rate), None
        self.lag, self.jitter, self.loss = lag, jitter, loss
        self.random = random.Random(seed)
        self.clock = clock
        self.rooms = {}    # code -> {seat: (address, last time)}
        self.where = {}    # address -> (code, seat)
        self.public = {}   # code -> (version, region, name bytes, last announcement)
        self.rates = collections.OrderedDict()  # ip -> (tokens, last time), oldest first
        self.next_sweep = 0.0
        self.stats = collections.Counter()
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def error_received(self, exc):
        # An ICMP error concerns one destination, not the relay.
        self.stats['socket_errors'] += 1

    # ---- limits

    def _allow(self, ip, now):
        if self.global_at is not None:
            self.global_tokens = min(2 * self.global_rate,
                                     self.global_tokens + max(0.0, now - self.global_at) * self.global_rate)
        self.global_at = now
        if self.global_tokens < 1:
            return False
        self.global_tokens -= 1
        entry = self.rates.get(ip)
        if entry is None:
            if len(self.rates) >= self.max_ips:
                return False
            tokens = float(self.burst)
        else:
            tokens, at = entry
            tokens = min(self.burst, tokens + max(0.0, now - at) * self.rate)
        self.rates[ip] = (max(0.0, tokens - 1), now)
        self.rates.move_to_end(ip)
        return tokens >= 1

    def _valid(self, kind, data, addr):
        """Shape checks, before any state is kept for the sender."""
        if kind == b'J':
            return 3 <= len(data) <= 18 and 1 <= data[1] <= self.seats and CODE.fullmatch(data[2:]) is not None
        if kind == b'A':
            if not 19 <= len(data) <= 19 + NAME_BYTES:
                return False
            raw = data[3:19].rstrip(b'\0')
            if not CODE.fullmatch(raw) or data[3:19] != raw.ljust(16, b'\0'):
                return False
            try:
                name = data[19:].decode('utf-8')
            except UnicodeDecodeError:
                return False
            return not any(ord(c) < 32 or ord(c) == 127 for c in name)
        if kind == b'L':
            return len(data) <= LIST_BYTES and not any(data[2:])
        if kind == b'D':
            return addr in self.where
        return False

    # ---- rooms

    def _remove(self, addr):
        place = self.where.pop(addr, None)
        if place is None:
            return
        code, seat = place
        room = self.rooms.get(code)
        if room is not None and room.get(seat, (None,))[0] == addr:
            del room[seat]
            if seat == 1:
                self.public.pop(code, None)
            if not room:
                del self.rooms[code]

    def _join(self, data, addr, now):
        seat, code = data[1], data[2:].decode('ascii')
        room = self.rooms.get(code, {})
        old = room.get(seat)
        if old and old[0] != addr:
            self.stats['occupied'] += 1  # never hand a seat to another address before its lease expires
            return
        if seat != 1 and 1 not in room:
            return
        if code not in self.rooms and len(self.rooms) >= self.max_rooms:
            return
        if addr not in self.where and sum(a[0] == addr[0] for a in self.where) >= PEERS_PER_IP:
            return
        if self.where.get(addr) != (code, seat):
            self._remove(addr)  # one seat per address: leaving the old room keeps both indexes consistent
        room = self.rooms.setdefault(code, {})
        room[seat] = (addr, now)
        self.where[addr] = (code, seat)
        self.transport.sendto(b'O' + bytes([len(room)]), addr)

    def _announce(self, data, addr, now):
        code = data[3:19].rstrip(b'\0').decode('ascii')
        host = self.rooms.get(code, {}).get(1)
        if host and host[0] == addr:  # only the room's host can make it public
            self.public[code] = (data[1], data[2], data[19:], now)

    def _list(self, data, addr, now):
        out, used = [], 2
        cap = min(len(data), LIST_BYTES)
        for code, (version, region, name, at) in self.public.items():
            if now - at > PUBLIC_FRESH or version != data[1] or not 0 < len(self.rooms.get(code, {})) < self.seats:
                continue
            entry = bytes([region]) + code.encode('ascii').ljust(16, b'\0') + bytes([len(name)]) + name
            if used + len(entry) > cap:
                continue
            out.append(entry)
            used += len(entry)
            if len(out) == LIST_MAX:
                break
        self.transport.sendto(b'R' + bytes([len(out)]) + b''.join(out), addr)

    def _forward(self, data, addr, now):
        code, seat = self.where[addr]
        room = self.rooms[code]
        if room.get(seat, (None,))[0] != addr:  # the reverse index alone does not prove the seat is still ours
            return
        room[seat] = (addr, now)
        for other, (oaddr, _) in list(room.items()):
            if other != seat:
                self._send_impaired(data, oaddr)
                self.stats['forwarded'] += 1

    def _send_impaired(self, data, addr):
        if self.loss and self.random.random() * 100 < self.loss:
            self.stats['impaired_lost'] += 1
            return
        hold = self.lag + (self.random.random() * self.jitter if self.jitter else 0.0)
        if hold <= 0:
            self.transport.sendto(data, addr)
            return
        asyncio.get_running_loop().call_later(hold, self.transport.sendto, data, addr)

    def datagram_received(self, data, addr):
        if not 2 <= len(data) <= MAX_DATAGRAM:
            return
        now = self.clock()
        if now >= self.next_sweep:
            self.sweep(now)
            self.next_sweep = now + 1
        kind = data[:1]
        if not self._valid(kind, data, addr):
            self.stats['invalid'] += 1
            return
        if not self._allow(addr[0], now):
            self.stats['rate_drops'] += 1
            return
        if kind == b'J':
            self._join(data, addr, now)
        elif kind == b'A':
            self._announce(data, addr, now)
        elif kind == b'L':
            self._list(data, addr, now)
        else:
            self._forward(data, addr, now)

    def sweep(self, now=None):
        now = self.clock() if now is None else now
        for addr in [a for room in self.rooms.values() for a, at in room.values() if now - at >= self.peer_timeout]:
            self._remove(addr)
        for code in [c for c, v in self.public.items() if now - v[3] > PUBLIC_FRESH]:
            del self.public[code]
        while self.rates:  # ordered by last datagram: stops at the first busy IP
            ip, (_, at) = next(iter(self.rates.items()))
            if now - at <= RATE_IDLE:
                break
            del self.rates[ip]


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=47800)
    ap.add_argument('--bind', default='0.0.0.0')
    ap.add_argument('--seats', type=int, default=SEATS, help='players per room, 2..4 (the game\'s link size)')
    ap.add_argument('--max-rooms', type=int, default=128)
    ap.add_argument('--peer-timeout', type=int, default=IDLE)
    ap.add_argument('--rate', type=int, default=600, help='datagrams per second per IP')
    ap.add_argument('--burst', type=int, default=1200)
    ap.add_argument('--lag-ms', type=float, default=0, help='tests only: hold forwarded datagrams this long')
    ap.add_argument('--jitter-ms', type=float, default=0, help='tests only: and up to this much more, at random')
    ap.add_argument('--loss', type=float, default=0, help='tests only: drop this percentage of forwarded datagrams')
    ap.add_argument('--seed', type=int, default=None, help='tests only: repeatable impairment')
    args = ap.parse_args()
    if not 1 <= args.port <= 65535:
        ap.error('port must be 1..65535')
    try:
        factory = Relay(seats=args.seats, max_rooms=args.max_rooms, peer_timeout=args.peer_timeout, rate=args.rate,
                        burst=args.burst, lag=args.lag_ms / 1000, jitter=args.jitter_ms / 1000, loss=args.loss,
                        seed=args.seed)
    except ValueError as e:
        ap.error(str(e))
    loop = asyncio.get_running_loop()
    transport, relay = await loop.create_datagram_endpoint(lambda: factory, local_addr=(args.bind, args.port))
    impaired = f', impaired {args.lag_ms:g}+{args.jitter_ms:g} ms, {args.loss:g}% loss' if (
        args.lag_ms or args.jitter_ms or args.loss) else ''
    print(f'GBANoCable relay on UDP {args.port}, {args.seats} seats, max rooms {args.max_rooms}{impaired}', flush=True)
    try:
        while True:
            await asyncio.sleep(1)
            relay.sweep()
    finally:
        transport.close()


if __name__ == '__main__':
    asyncio.run(main())

#!/usr/bin/env python3
"""Tests for relay.py and pairdiff.py (standard library only):  python3 -m unittest discover -s tools -p "test_*.py"
"""
import asyncio
import os
import socket
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pairdiff  # noqa: E402
import relay  # noqa: E402


class FakeTransport:
    def __init__(self):
        self.sent = []

    def sendto(self, data, addr):
        self.sent.append((data, addr))


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


def make(**kw):
    clock = Clock()
    r = relay.Relay(clock=clock, **kw)
    t = FakeTransport()
    r.connection_made(t)
    return r, t, clock


def join(seat, code):
    return b'J' + bytes([seat]) + code


def announce(version, region, code, name):
    return b'A' + bytes([version, region]) + code.ljust(16, b'\0') + name


H, G, X = ('10.0.0.1', 5000), ('10.0.0.2', 5000), ('10.0.0.3', 5000)


class RelayRooms(unittest.TestCase):
    def test_join_forward_and_host_first(self):
        r, t, _ = make()
        r.datagram_received(join(2, b'ROOM'), G)  # no host yet: ignored
        self.assertEqual(t.sent, [])
        r.datagram_received(join(1, b'ROOM'), H)
        r.datagram_received(join(2, b'ROOM'), G)
        self.assertEqual(t.sent, [(b'O\x01', H), (b'O\x02', G)])
        t.sent.clear()
        r.datagram_received(b'Dhello', H)
        self.assertEqual(t.sent, [(b'Dhello', G)])

    def test_occupied_seat_is_not_taken_over(self):
        r, t, clock = make(peer_timeout=30)
        r.datagram_received(join(1, b'ROOM'), H)
        r.datagram_received(join(2, b'ROOM'), G)
        r.datagram_received(join(2, b'ROOM'), X)  # another address wants seat 2
        self.assertEqual(r.rooms['ROOM'][2][0], G)
        self.assertEqual(r.stats['occupied'], 1)
        r.datagram_received(b'Dsneaky', X)  # not a member: dropped
        self.assertNotIn((b'Dsneaky', H), t.sent)
        clock.t += 31  # the old lease expires: the new address can sit there
        r.datagram_received(join(1, b'ROOM'), H)
        r.datagram_received(join(2, b'ROOM'), X)
        self.assertEqual(r.rooms['ROOM'][2][0], X)
        self.assertNotIn(G, r.where)

    def test_changing_room_keeps_indexes_consistent(self):
        r, _, _ = make()
        r.datagram_received(join(1, b'ONE'), H)
        r.datagram_received(join(1, b'TWO'), H)
        self.assertNotIn('ONE', r.rooms)
        self.assertEqual(r.where[H], ('TWO', 1))

    def test_invalid_datagrams_keep_no_state(self):
        r, t, _ = make()
        for bad in (b'J', join(0, b'ROOM'), join(3, b'ROOM'), join(1, b'room'), join(1, b'A' * 17), b'Q12',
                    b'Dnot-a-member', b'L\x05\x01', b'X' * 600):
            r.datagram_received(bad, H)
        self.assertEqual((r.rooms, r.where, dict(r.rates), t.sent), ({}, {}, {}, []))

    def test_four_seats(self):
        r, t, _ = make(seats=4)
        addrs = [('10.0.1.%d' % i, 1) for i in range(1, 5)]
        for i, a in enumerate(addrs):
            r.datagram_received(join(i + 1, b'BIG'), a)
        t.sent.clear()
        r.datagram_received(b'Dx', addrs[2])
        self.assertEqual(sorted(a for _, a in t.sent), sorted(addrs[:2] + addrs[3:]))

    def test_room_limit_and_rate_limit(self):
        r, _, _ = make(max_rooms=1, rate=1, burst=3)
        r.datagram_received(join(1, b'A1'), H)
        r.datagram_received(join(1, b'A2'), G)
        self.assertEqual(list(r.rooms), ['A1'])
        for _ in range(5):
            r.datagram_received(join(1, b'A1'), H)
        self.assertGreater(r.stats['rate_drops'], 0)


class RelayLobby(unittest.TestCase):
    def setUp(self):
        self.r, self.t, self.clock = make()
        self.r.datagram_received(join(1, b'ROOM1'), H)
        self.r.datagram_received(announce(5, 0, b'ROOM1', "Ana's room".encode()), H)
        self.t.sent.clear()

    def ask(self, version=5, size=512, addr=X):
        self.r.datagram_received(b'L' + bytes([version]) + bytes(size - 2), addr)
        return self.t.sent.pop()[0]

    def test_listed_by_version(self):
        answer = self.ask()
        self.assertEqual(answer[:2], b'R\x01')
        self.assertEqual(answer[2], 0)
        self.assertEqual(answer[3:19], b'ROOM1'.ljust(16, b'\0'))
        self.assertEqual(answer[20:20 + answer[19]], "Ana's room".encode())
        self.assertEqual(self.ask(version=4), b'R\x00')

    def test_only_the_host_announces_and_names_are_checked(self):
        self.r.datagram_received(announce(5, 0, b'OTHER', b'x'), G)  # G hosts nothing
        self.r.datagram_received(announce(5, 0, b'ROOM1', b'bad\x01name'), H)
        self.r.datagram_received(announce(5, 0, b'ROOM1', b'\xff\xfe'), H)
        self.r.datagram_received(announce(5, 0, b'ROOM1', b'n' * 25), H)
        self.assertEqual(list(self.r.public), ['ROOM1'])
        self.assertEqual(self.r.public['ROOM1'][2], "Ana's room".encode())

    def test_no_amplification(self):
        self.assertEqual(self.ask(size=2), b'R\x00')  # an old 2-byte request: an empty list, never larger
        for size in (2, 20, 40, 512):
            self.assertLessEqual(len(self.ask(size=size)), size)

    def test_full_rooms_are_not_listed(self):
        self.r.datagram_received(join(2, b'ROOM1'), G)
        self.assertEqual(self.ask(), b'R\x00')

    def test_stale_announcements_are_not_listed(self):
        self.clock.t += 6
        self.r.datagram_received(join(1, b'ROOM1'), H)  # the host is still there, but stopped announcing
        self.assertEqual(self.ask(), b'R\x00')
        self.r.datagram_received(announce(5, 0, b'ROOM1', b'again'), H)
        self.assertEqual(self.ask()[:2], b'R\x01')


class RelayImpairment(unittest.TestCase):
    def test_lag_and_loss_over_real_udp(self):
        async def run():
            loop = asyncio.get_running_loop()
            transport, r = await loop.create_datagram_endpoint(lambda: relay.Relay(lag=0.15, seed=1),
                                                               local_addr=('127.0.0.1', 0))
            port = transport.get_extra_info('sockname')[1]
            socks = []
            for seat in (1, 2):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.bind(('127.0.0.1', 0))
                s.setblocking(False)
                s.sendto(join(seat, b'LAG'), ('127.0.0.1', port))
                socks.append(s)
                await asyncio.sleep(0.05)
            for s in socks:
                self.assertEqual(s.recv(64)[:1], b'O')
            sent = time.monotonic()
            socks[0].sendto(b'Dping', ('127.0.0.1', port))
            got = None
            while time.monotonic() - sent < 2 and got is None:
                await asyncio.sleep(0.01)
                try:
                    got = socks[1].recv(64)
                except BlockingIOError:
                    pass
            elapsed = time.monotonic() - sent
            for s in socks:
                s.close()
            transport.close()
            await asyncio.sleep(0.05)
            return got, elapsed

        got, elapsed = asyncio.run(run())
        self.assertEqual(got, b'Dping')
        self.assertGreaterEqual(elapsed, 0.14)

    def test_loss_is_a_share(self):
        r, t, _ = make(loss=50, seed=7, rate=10 ** 6, burst=10 ** 6)
        r.datagram_received(join(1, b'L'), H)
        r.datagram_received(join(2, b'L'), G)
        t.sent.clear()
        for _ in range(2000):
            r.datagram_received(b'Dx', H)
        self.assertTrue(800 < len(t.sent) < 1200, len(t.sent))


class PairDiff(unittest.TestCase):
    HOST = ['netlink: pair 1  0acd 0010 0000 0000 | 0acd 0020 0000 0000',
            'netlink: pair 2  0acd 0010 0000 0000 | 0acd 0020 0000 0000',  # a guess...
            'netlink: pair 2  0acd 0010 0000 0000 | 0acd 0040 0000 0000',  # ...played again: this one counts
            'netlink: pair 3  0acd 0010 0010 0020 | 0acd 0040 0000 0000',
            'other line']
    GUEST = ['netlink: pair 1  0acd 0010 0000 0000 | 0acd 0020 0000 0000',
             'netlink: pair 2  0acd 0010 0000 0000 | 0acd 0040 0000 0000',
             'netlink: pair 3  0acd 0010 0010 0040 | 0acd 0040 0000 0000',
             'netlink: pair 4  0acd 0010 0000 0000 | 0acd 0040 0000 0000']

    def test_last_line_wins_and_columns_are_named(self):
        a, b = pairdiff.final_groups(self.HOST), pairdiff.final_groups(self.GUEST)
        self.assertEqual(a[2][5], '0040')
        common, diffs, columns = pairdiff.compare([a, b])
        self.assertEqual(common, [1, 2, 3])
        self.assertEqual([f for f, _ in diffs], [3])
        self.assertEqual(dict(columns), {3: 1})

    def test_equal_logs(self):
        a = pairdiff.final_groups(self.GUEST)
        self.assertEqual(pairdiff.compare([a, dict(a)])[1], [])


if __name__ == '__main__':
    unittest.main()

# Reference libraries

C99, no dependencies, no allocation, transport-agnostic, single-threaded. Two libraries for the two kinds of link
game ([docs/09](../docs/09-multi-sio-anatomy.md) says how to tell which one yours is):

| Library | For | Files |
|---|---|---|
| `gbanocable` | **lockstep** games: the play loop advances only with the frame's packet. Slots on one timeline, never cut, a desync check | `include/gbanocable.h`, `src/gbanocable.c`, `tests/test_sim.c`, `tests/test_check.c` |
| `gnc_direct` | **state-replication** games with one-time events: blocks handed over as they arrive, events on a reliable channel with one order for everybody | `include/gnc_direct.h`, `src/gnc_direct.c`, `tests/test_direct.c` |

## Tests

```sh
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_check.c -o test_check && ./test_check
cc -std=c99 -O2 -Wall -Iinclude src/gnc_direct.c tests/test_direct.c -o test_direct && ./test_direct
./test_direct --no-hole-fix     # must exit 1: without the hole fix a player that was away stays frozen
```

- `test_sim`: 2 and 4 players with delay, jitter, loss and outages; every player must see the same groups in the
  same order.
- `test_check`: corrected guesses never reported, a decided round skipped, a forced difference reported once per
  side at its frame, duplicates and late replays ignored.
- `test_direct`: 2 to 4 players, each writing a state block every frame and handing over numbered messages, over a
  network with delay, jitter, loss and **outages** (for a while nothing from one player or to it passes). Every run
  must show `order` (the same messages in the same order on every player), `once` (of every sender exactly what it
  handed over) and `fresh` (nobody's view of another player is stale at the end). The last case runs a long outage
  with the hole fix off and must show `FROZEN` while `order` and `once` still hold: the bug of
  [docs/10](../docs/10-direct-transport-and-messages.md), section 5, and the proof that the check fires.

These tests certify each transport's contract with simulated players. They do not certify a game: finish with real
game processes and a report ([docs/07](../docs/07-testing.md)).

## gbanocable (slots)

2-4 players, packets of up to 16 words.

```c
#include "gbanocable.h"

static void send_dgram(void* user, int to_player, const uint8_t* data, int size) {
    /* UDP / Steam / relay: deliver to `to_player` */
}

gnc_session link;
gnc_init(&link, 2, my_player, 4 /* words per packet */, send_dgram, NULL);

/* every game frame */
while (have_datagram)
    gnc_receive(&link, buf, len);
gnc_frame(&link, game_packet_or_NULL);   /* one packet from the game's send queue */
uint16_t group[GNC_MAX_PLAYERS][GNC_MAX_WORDS];
if (gnc_take(&link, group)) {
    /* put group[player][word] in the game's receive queue, as the cable would */
} else {
    /* the game waits this frame (its "receive empty" state); never a link error */
}
```

Wiring it into a game: replace the link driver's per-frame state machine (see ../docs/05) so the game's packet
queues feed `gnc_frame` and receive from `gnc_take`; report the cable as connected with the right player ids; never
report timeouts, queue-full or checksum errors; raise 0 ms retry limits while online.

Known limit: `gnc_take` smooths with an adaptive buffer that each side tunes on its own; a game whose state
machines run while waiting needs one agreed delay instead (../docs/04; fixed-delay mode is on the roadmap).

### Desync check (optional)

`gnc_check_*` compares a hash of the shared state between the games, on final frames only, over the same transport
(its datagrams start with `'H'`; `gnc_receive` ignores them, so feed every datagram to both):

```c
gnc_check check;
gnc_check_init(&check, my_player, 120 /* every N link frames */, 48 /* frames a rollback can reach back */);

/* when link frame f is played (again, too, when a rollback replays it) */
if (f % 120 == 0)
    gnc_check_record(&check, f, round_decided ? 0 : hash_of_shared_state());   /* 0: no check here */

/* every frame: send what became final */
uint8_t d[GNC_CHECK_DATAGRAM];
int n;
while ((n = gnc_check_poll(&check, frames_confirmed_by_everyone, d)) > 0)
    for (int p = 0; p < players; p++)
        if (p != my_player) send_dgram(NULL, p, d, n);

/* every datagram */
if (gnc_check_receive(&check, buf, len) < 0)
    log("desync at link frame %u", check.first_mismatch - 1);
```

Hash only what every machine stores the same way (in player order, not "me and the other one"). Return 0 while the
shared state stops being shared on purpose: in a versus battle, once a fighter is down each game plays its own side
of the result (win or loss) and may draw random numbers differently; the next round starts again from a seed both
exchange. See ../docs/07-testing.md. A state hash is for lockstep games; a state-replication game has no shared
frame to hash: compare message logs and views instead (../docs/10, section 6).

## gnc_direct (direct state + ordered messages)

2-4 players, state blocks of up to 24 bytes, messages of up to 64 bytes. Player 0 is the host (the cable's parent).
One datagram a frame goes to **every** other player (what a relay does with it); a datagram is at most 1200 bytes,
so run `tools/relay.py` with `--max-datagram 1200`.

```c
#include "gnc_direct.h"

static void send_to_all(void* user, const uint8_t* data, int size) {
    /* the relay passes it to every other seat; or send it to each peer yourself */
}

gd_session link;
/* 24-byte blocks whose bytes from 8 on are passed step by step by the game (a lobby handshake): those must be
   seen in order. For a block that is state only, give the block size there. */
gd_init(&link, players, my_player, 24, 8, send_to_all, NULL);

/* every game frame */
while (have_datagram)
    gd_receive(&link, buf, len);
/* ... the game runs and writes its block ... */
gd_frame(&link, my_block);                      /* at the end of the frame that wrote it */
for (int p = 0; p < players; p++) {
    uint8_t block[GD_BLOCK_MAX];
    int got = gd_state(&link, p, block);        /* 1 new, 0 the last one again, -1 never heard or p == me */
    if (got >= 0) { /* put it in the game's receive slot p and set p's "received" bit */ }
}

/* the game's message sender, instead of its chunk-by-chunk steps */
if (!gd_msg_send(&link, message, size)) { /* no room: keep it and try next frame */ }

/* the game's receiver: every message of the one order, own messages included */
int from, n;
uint8_t message[GD_MSG_MAX];
while ((n = gd_msg_next(&link, &from, message, sizeof message)) > 0)
    dispatch(from, message, n);                 /* the game's own handler for that sender */
```

Rules it implements: blocks in order per player, the last one repeated, nobody waits; every block repeated until
acknowledged but at most 40 back; **a hole longer than that is stepped over**; messages go to the host, which gives
all of them one order; every player is given that order, each message once, its own included; entries are kept
until acknowledged, so a player that was away is given everything it missed; players that keep up are not made to
wait behind one that is catching up.

What it leaves out on purpose (all in ShiningSource's `netlink.c`, described in ../docs/11 to ../docs/13): rooms,
seats and stream numbers; giving a silent player up and the standing character; taking a player in again after it
was given up; the lock-out at barriers; join in progress. Here a player that is away is simply waited for, and
`gd_msg_send` returns 0 when 1024 entries are waiting for it.

## Integrations

- ReKHCoM (2 players, lockstep + rollback, inside its decompiled `sio.c`): https://github.com/MatyMeatBoy/ReKHCoM
  (`src/sio/sio.c`, `port/src/port_netlink.c`, `port/src/port_steamlink.c`).
- ShiningSource (2-4 players, direct transport + messages, under the SDK's MultiSio):
  https://github.com/MatyMeatBoy/ShiningSource (`runtime/core/netlink.c`, `relay/ss_relay.c`,
  `games/ss2/port/patches.json`). `gnc_direct` is a fresh, smaller implementation of the same design, not that
  file.

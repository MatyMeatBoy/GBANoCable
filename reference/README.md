# gbanocable (reference library)

C99, no dependencies, transport-agnostic, single-threaded. 2-4 players, packets of up to 16 words.

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

## Desync check (optional)

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
exchange. See ../docs/07-testing.md.

## Tests

```sh
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_check.c -o test_check && ./test_check
```

`test_sim`: 2 and 4 players with delay, jitter, loss and outages; every player must see the same groups in the same
order. `test_check`: corrected guesses never reported, a decided round skipped, a forced difference reported once
per side at its frame, duplicates and late replays ignored.

ReKHCoM's integration (2 players, inside its decompiled `sio.c`): https://github.com/MatyMeatBoy/ReKHCoM
(`src/sio/sio.c`, `port/src/port_netlink.c`, `port/src/port_steamlink.c`).

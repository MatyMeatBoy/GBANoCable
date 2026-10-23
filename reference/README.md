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

Test: `cc -O2 -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim` (2 and 4 players with delay,
jitter, loss and outages; every player must see the same groups in the same order).

ReKHCoM's integration (2 players, inside its decompiled `sio.c`): https://github.com/MatyMeatBoy/ReKHCoM
(`src/sio/sio.c`, `port/src/port_netlink.c`, `port/src/port_steamlink.c`).

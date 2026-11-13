/* gnc_direct: 2-4 simulated players in one process over a bad network (delay, jitter, loss) with outages: for a
 * while nothing from one player or to it passes, as when its connection is gone. Each player's "game" writes a
 * state block every frame (a counter and a position) and hands over numbered messages. Asked of every run:
 *
 *   order     every player was given the same messages in the same order
 *   once      of every sender, exactly what it handed over, in that order: none twice, none missing
 *   fresh     what each player has of each other player is recent at the end: nobody frozen
 *
 * The run ends with a quiet stretch (no new messages, no outage) so that everything can arrive.
 *
 *   cc -std=c99 -O2 -Wall -Iinclude src/gnc_direct.c tests/test_direct.c -o test_direct && ./test_direct
 *   ./test_direct --no-hole-fix     the long outages again without the hole fix: exits 1 (players frozen)
 */
#include "gnc_direct.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 3000
#define QUIET_END 400 /* frames at the end without new messages */
#define MAXQ 4096
#define MAXLOG 8192
#define BLOCK 8

typedef struct {
    int from, to, due, size;
    uint8_t data[GD_MAX_DATAGRAM];
} Flight;

typedef struct {
    int who, from, to; /* player cut off during frames [from, to); who < 0: none */
} Outage;

static Flight queue[MAXQ];
static int qn, now, players, delay, jitter, loss, dropped_full;
static Outage outages[2];
static gd_session session[GD_MAX_PLAYERS];
static uint32_t given[GD_MAX_PLAYERS][MAXLOG]; /* sender << 24 | number, in the order given */
static uint32_t given_n[GD_MAX_PLAYERS], sent_n[GD_MAX_PLAYERS];
static uint32_t view[GD_MAX_PLAYERS][GD_MAX_PLAYERS]; /* the counter in the last block a has of b */

/* the test's own random numbers, so that a run prints the same lines with every C library */
static uint32_t seed;
static int rnd(void) {
    seed = seed * 1103515245u + 12345u;
    return (int)(seed >> 16 & 0x7FFF);
}

static int cut(int p) {
    int i;
    for (i = 0; i < 2; i++)
        if (outages[i].who == p && now >= outages[i].from && now < outages[i].to)
            return 1;
    return 0;
}

static void net_send(void* user, const uint8_t* data, int size) {
    int from = (int)((gd_session*)user - session), to;
    if (cut(from))
        return;
    for (to = 0; to < players; to++) {
        if (to == from || rnd() % 100 < loss)
            continue;
        if (qn >= MAXQ) {
            dropped_full++;
            continue;
        }
        queue[qn].from = from;
        queue[qn].to = to;
        queue[qn].due = now + delay + (jitter ? rnd() % (jitter + 1) : 0);
        queue[qn].size = size;
        memcpy(queue[qn].data, data, (size_t)size);
        qn++;
    }
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Returns a mask: 1 order, 2 once, 4 fresh hold. */
static int run(const char* name, int n, int d, int j, int l, Outage a, Outage b, int hole_fix) {
    static const int every[GD_MAX_PLAYERS] = { 3, 4, 5, 7 }; /* frames between a player's messages */
    uint32_t holes = 0, repeats = 0;
    int p, q, i, result = 7;

    players = n, delay = d, jitter = j, loss = l, qn = 0, dropped_full = 0;
    outages[0] = a, outages[1] = b;
    memset(given_n, 0, sizeof(given_n));
    memset(sent_n, 0, sizeof(sent_n));
    memset(view, 0, sizeof(view));
    seed = 4321;
    for (p = 0; p < n; p++) {
        gd_init(&session[p], n, p, BLOCK, BLOCK, net_send, &session[p]);
        gd_set_hole_fix(&session[p], hole_fix);
    }
    for (now = 1; now <= FRAMES; now++) {
        for (i = 0; i < qn;) {
            if (queue[i].due <= now) {
                if (!cut(queue[i].to))
                    gd_receive(&session[queue[i].to], queue[i].data, queue[i].size);
                queue[i] = queue[--qn];
            } else {
                i++;
            }
        }
        for (p = 0; p < n; p++) {
            uint8_t block[BLOCK], other[BLOCK], message[GD_MSG_MAX];
            int from, size;
            /* the game: hand a message over now and then (again next frame if there was no room) */
            if (now <= FRAMES - QUIET_END && (uint32_t)now / (uint32_t)every[p] >= sent_n[p]) {
                put32(message, sent_n[p]);
                put32(message + 4, (uint32_t)p * 1000003u ^ sent_n[p]);
                if (gd_msg_send(&session[p], message, 8))
                    sent_n[p]++;
            }
            put32(block, (uint32_t)now);
            put32(block + 4, (uint32_t)(now * (p + 3)));
            gd_frame(&session[p], block);
            for (q = 0; q < n; q++)
                if (gd_state(&session[p], q, other) >= 0)
                    view[p][q] = get32(other);
            while ((size = gd_msg_next(&session[p], &from, message, sizeof(message))) > 0) {
                if (size != 8 || get32(message + 4) != ((uint32_t)from * 1000003u ^ get32(message)))
                    result &= ~2; /* not what that player handed over */
                if (given_n[p] < MAXLOG)
                    given[p][given_n[p]++] = (uint32_t)from << 24 | get32(message);
            }
        }
    }
    /* order: the same list on every player */
    for (p = 1; p < n; p++)
        if (given_n[p] != given_n[0] || memcmp(given[p], given[0], given_n[0] * sizeof(uint32_t)))
            result &= ~1;
    /* once: of every sender 0, 1, 2 ... up to what it handed over */
    for (p = 0; p < n; p++) {
        uint32_t expect[GD_MAX_PLAYERS] = { 0 };
        for (i = 0; i < (int)given_n[p]; i++) {
            uint32_t sender = given[p][i] >> 24, number = given[p][i] & 0xFFFFFF;
            if (sender >= (uint32_t)n || number != expect[sender]++)
                result &= ~2;
        }
        for (q = 0; q < n; q++)
            if (expect[q] != sent_n[q])
                result &= ~2;
    }
    /* fresh: nobody's view of another player is older than the network explains */
    for (p = 0; p < n; p++)
        for (q = 0; q < n; q++) {
            if (p != q && (uint32_t)FRAMES - view[p][q] > (uint32_t)(d + j + 20))
                result &= ~4;
            holes += session[p].peer[q].holes;
            repeats += session[p].peer[q].repeats;
        }
    printf("%-58s %s %s %s  (%u messages, %u holes stepped over, %u frames a block was repeated)\n", name,
           result & 1 ? "order" : "ORDER?", result & 2 ? "once" : "ONCE?", result & 4 ? "fresh" : "FROZEN", given_n[0],
           holes, repeats);
    if (dropped_full)
        printf("  (the test's own queue was full %d times)\n", dropped_full);
    return result;
}

int main(int argc, char** argv) {
    const Outage none = { -1, 0, 0 };
    const Outage player_short = { 2, 1000, 1020 }, player_long = { 2, 1000, 1300 }, host_long = { 0, 1000, 1300 };
    const Outage second = { 1, 1700, 1900 }, same_again = { 2, 1800, 2100 };
    int ok = 1;

    if (argc > 1 && !strcmp(argv[1], "--no-hole-fix")) {
        ok &= run("3 players, player 2 away 300 frames, NO hole fix", 3, 3, 3, 5, player_long, none, 0) == 7;
        ok &= run("3 players, the host away 300 frames, NO hole fix", 3, 3, 3, 5, host_long, none, 0) == 7;
        printf("%s\n", ok ? "all ok" : "FAILED: without the hole fix a player that was away stays frozen");
        return ok ? 0 : 1;
    }
    ok &= run("2 players, clean network", 2, 0, 0, 0, none, none, 1) == 7;
    ok &= run("3 players, delay 3, jitter 3, loss 5%", 3, 3, 3, 5, none, none, 1) == 7;
    ok &= run("4 players, delay 4, jitter 6, loss 10%", 4, 4, 6, 10, none, none, 1) == 7;
    /* shorter than a datagram reaches back: heals by repetition alone, with or without the fix */
    ok &= run("3 players, player 2 away 20 frames, no hole fix", 3, 3, 3, 5, player_short, none, 0) == 7;
    /* longer: the fix is what brings the state back; the messages come back either way */
    ok &= run("3 players, player 2 away 300 frames", 3, 3, 3, 5, player_long, none, 1) == 7;
    ok &= run("3 players, the host away 300 frames", 3, 3, 3, 5, host_long, none, 1) == 7;
    ok &= run("4 players, player 2 and then player 1 away", 4, 3, 3, 5, player_long, second, 1) == 7;
    ok &= run("3 players, player 2 away twice", 3, 3, 3, 5, player_long, same_again, 1) == 7;
    /* and the check fires: without the fix the long outage leaves the state frozen while the messages go on */
    ok &= run("3 players, player 2 away 300 frames, no hole fix", 3, 3, 3, 5, player_long, none, 0) == 3;
    printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}

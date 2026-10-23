/* 2-4 simulated players over a bad network (delay, jitter, loss, an outage) in one process. Each player's "game"
 * runs a deterministic lockstep simulation that only advances with a group, like a link battle; the test checks
 * that every player saw the same groups in the same order and reports the frames waited.
 *
 *   cc -O2 -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
 */
#include "gbanocable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 6000
#define MAXQ 20000

typedef struct {
    int to, due, size;
    uint8_t data[GNC_MAX_DATAGRAM];
} Flight;

static Flight queue[MAXQ];
static uint32_t history[GNC_MAX_PLAYERS][FRAMES]; /* the hash after every step */
static int qn;
static int now, delay, jitter, loss, outage_from, outage_to;

static void net_send(void* user, int to, const uint8_t* data, int size) {
    (void)user;
    if (rand() % 100 < loss || (now >= outage_from && now < outage_to) || qn >= MAXQ)
        return;
    queue[qn].to = to;
    queue[qn].due = now + delay + (jitter ? rand() % (jitter + 1) : 0);
    queue[qn].size = size;
    memcpy(queue[qn].data, data, (size_t)size);
    qn++;
}

static int run(int players, int d, int j, int l, int of, int ot) {
    static gnc_session s[GNC_MAX_PLAYERS];
    uint32_t hash[GNC_MAX_PLAYERS], steps[GNC_MAX_PLAYERS];
    uint32_t state[GNC_MAX_PLAYERS];
    int p, i, ok = 1;
    delay = d, jitter = j, loss = l, outage_from = of, outage_to = ot, qn = 0;
    srand(1234);
    for (p = 0; p < players; p++) {
        gnc_init(&s[p], players, p, 4, net_send, NULL);
        hash[p] = 2166136261u, steps[p] = 0, state[p] = 7;
    }
    for (now = 0; now < FRAMES; now++) {
        /* deliver what is due */
        for (i = 0; i < qn;) {
            if (queue[i].due <= now) {
                gnc_receive(&s[queue[i].to], queue[i].data, queue[i].size);
                queue[i] = queue[--qn];
            } else {
                i++;
            }
        }
        for (p = 0; p < players; p++) {
            uint16_t pkt[4] = { 0x0ACD, (uint16_t)(rand() & 0x3FF), (uint16_t)p, (uint16_t)now };
            uint16_t g[GNC_MAX_PLAYERS][GNC_MAX_WORDS];
            gnc_frame(&s[p], pkt);
            if (gnc_take(&s[p], g)) { /* the game advances one frame with the group */
                int q, w;
                for (q = 0; q < players; q++)
                    for (w = 0; w < 4; w++)
                        hash[p] = (hash[p] ^ g[q][w]) * 16777619u;
                state[p] = state[p] * 1103515245u + g[0][1] + g[1][1];
                history[p][steps[p]] = hash[p] ^ state[p];
                steps[p]++;
            }
        }
    }
    printf("%d players, delay %d, jitter %d, loss %d%%, outage %d-%d:\n", players, d, j, l, of, ot);
    for (p = 0; p < players; p++) {
        printf("  player %d: %u frames advanced, %u waited, buffer %d, hash %08x\n", p, steps[p], s[p].waited,
               s[p].target, hash[p]);
    }
    /* the same groups in the same order: every step of the common prefix identical, nobody far behind */
    for (p = 1; p < players; p++) {
        uint32_t common = steps[p] < steps[0] ? steps[p] : steps[0], k;
        for (k = 0; k < common; k++)
            if (history[p][k] != history[0][k]) {
                printf("  player %d differs at step %u\n", p, k);
                ok = 0;
                break;
            }
        if (steps[0] - common > 64 || steps[p] - common > 64)
            ok = 0;
    }
    printf("  %s\n", ok ? "ok: every player saw the same groups" : "FAIL");
    return ok;
}

int main(void) {
    int ok = 1;
    ok &= run(2, 0, 0, 0, 0, 0);
    ok &= run(2, 6, 0, 10, 0, 0);
    ok &= run(2, 4, 6, 5, 0, 0);
    ok &= run(2, 4, 6, 5, 2000, 2600);
    ok &= run(4, 4, 6, 5, 0, 0);
    ok &= run(4, 3, 3, 2, 3000, 3300);
    printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}

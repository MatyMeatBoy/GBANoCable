/* The desync check (gnc_check_*): two games hash their shared state every 120 link frames, one of them plays some
 * frames first on a wrong guess (a rollback corrects them before they are final), a round ends in a state each side
 * plays differently on purpose (hash 0: no check), and one forced difference must be reported at its frame.
 *
 *   cc -O2 -Iinclude src/gbanocable.c tests/test_check.c -o test_check && ./test_check
 */
#include "gbanocable.h"

#include <stdio.h>
#include <string.h>

#define FRAMES 3000
#define EVERY 120
#define SETTLE 48
#define LAG 7 /* frames a datagram takes */

typedef struct {
    int due, to, size;
    uint8_t d[GNC_CHECK_DATAGRAM];
} Flight;

static Flight flights[256];
static int nflights;

static uint32_t shared_state(uint32_t f) {
    return (f * 2654435761u) ^ 0x9e3779b9u;
}

/* a round is decided between frames 1500 and 1800: each side plays its own result screen (its own randoms) */
static uint32_t hash_of(int side, uint32_t f, int forced_at) {
    uint32_t h = shared_state(f);
    if (f >= 1500 && f < 1800)
        return 0; /* the round is decided: nothing shared to check */
    if (side == 1 && (int)f == forced_at)
        h ^= 1;
    return h ? h : 1;
}

static int run(int forced_at, uint32_t* mismatches, uint32_t* first, uint32_t* checks, uint32_t* skipped) {
    gnc_check c[2];
    int now, side, i, reported = 0;
    nflights = 0;
    gnc_check_init(&c[0], 0, EVERY, SETTLE);
    gnc_check_init(&c[1], 1, EVERY, SETTLE);
    for (now = 0; now < FRAMES; now++) {
        for (i = 0; i < nflights;) {
            if (flights[i].due <= now) {
                if (gnc_check_receive(&c[flights[i].to], flights[i].d, flights[i].size) < 0)
                    reported++;
                flights[i] = flights[--nflights];
            } else {
                i++;
            }
        }
        for (side = 0; side < 2; side++) {
            uint8_t out[GNC_CHECK_DATAGRAM];
            int n;
            if (now % EVERY == 0) {
                if (side == 0) /* first played on a guess: a wrong state... */
                    gnc_check_record(&c[side], (uint32_t)now, hash_of(side, (uint32_t)now, forced_at) ^ 0x55u);
                else
                    gnc_check_record(&c[side], (uint32_t)now, hash_of(side, (uint32_t)now, forced_at));
            }
            if (side == 0 && now % EVERY == 5) /* ...the real packet came: the rollback plays that frame again */
                gnc_check_record(&c[side], (uint32_t)(now - 5), hash_of(side, (uint32_t)(now - 5), forced_at));
            /* confirmed: what both games know for real, a few frames behind (the round trip) */
            while ((n = gnc_check_poll(&c[side], (uint32_t)(now > 10 ? now - 10 : 0), out)) > 0) {
                if (gnc_check_receive(&c[side], out, n) != 0) /* our own datagram must be ignored */
                    return 0;
                if (nflights + 2 > (int)(sizeof(flights) / sizeof(flights[0])))
                    return 0;
                for (i = 0; i < 2; i++) { /* sent twice: a duplicate must not count twice */
                    flights[nflights].due = now + LAG + i;
                    flights[nflights].to = side ^ 1;
                    flights[nflights].size = n;
                    memcpy(flights[nflights].d, out, (size_t)n);
                    nflights++;
                }
            }
        }
        /* a late replay of a frame that is already final changes nothing */
        if (now == 400)
            gnc_check_record(&c[0], 240, 12345);
    }
    *mismatches = c[0].mismatches + c[1].mismatches;
    *first = c[0].first_mismatch ? c[0].first_mismatch : c[1].first_mismatch;
    *checks = c[0].checks + c[1].checks;
    *skipped = c[0].skipped;
    if (forced_at < 0 && reported) /* a mismatch returned by gnc_check_receive with equal games */
        return 0;
    return 1;
}

int main(void) {
    uint32_t mismatches, first, checks, skipped;
    int ok = 1;

    if (!run(-1, &mismatches, &first, &checks, &skipped))
        return 1;
    printf("equal games: %u checks, %u mismatches, %u skipped (round decided)\n", checks, mismatches, skipped);
    /* frames 0..2880 every 120: 25 per side, minus 1560 and 1680 (the decided round), minus the last one or two
       not final when the run ends; the corrected guesses of side 0 must never show up as a mismatch */
    if (mismatches != 0 || checks < 2 * 18 || skipped != 2)
        ok = 0;

    if (!run(1200, &mismatches, &first, &checks, &skipped))
        return 1;
    printf("forced difference at 1200: %u mismatches, first at %d\n", mismatches, (int)first - 1);
    if (mismatches != 2 || first != 1201) /* both sides see it, once each */
        ok = 0;

    printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}

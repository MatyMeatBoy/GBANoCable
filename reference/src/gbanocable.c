/* gbanocable: see include/gbanocable.h. Copyright (c) 2026 MatyMeatBoy. MIT License. */
#include "gbanocable.h"

#include <string.h>

/* datagram: 'S', from, first slot (4), count, what the sender holds of the receiver's slots (4), then
   count slots of words*2 bytes, little endian */

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static uint16_t get16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
static void put32(uint8_t* p, uint32_t v) {
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}
static uint32_t get32(const uint8_t* p) {
    return get16(p) | (uint32_t)get16(p + 2) << 16;
}

int gnc_init(gnc_session* s, int players, int me, int words, gnc_send_fn send, void* user) {
    if (players < 2 || players > GNC_MAX_PLAYERS || me < 0 || me >= players || words < 1 || words > GNC_MAX_WORDS)
        return 0;
    memset(s, 0, sizeof(*s));
    s->players = players;
    s->me = me;
    s->words = words;
    s->send = send;
    s->user = user;
    s->target = 2;
    return 1;
}

static uint32_t oldest_ack(const gnc_session* s) {
    uint32_t m = s->made;
    int p;
    for (p = 0; p < s->players; p++)
        if (p != s->me && s->acked[p] < m)
            m = s->acked[p];
    return m;
}

static void send_to(gnc_session* s, int to) {
    uint8_t d[GNC_MAX_DATAGRAM];
    uint32_t first = s->acked[to], n = s->made - first, i;
    int w;
    if (n > GNC_RESEND)
        n = GNC_RESEND;
    d[0] = 'S';
    d[1] = (uint8_t)s->me;
    put32(d + 2, first);
    d[6] = (uint8_t)n;
    put32(d + 7, s->have[to]);
    for (i = 0; i < n; i++)
        for (w = 0; w < s->words; w++)
            put16(d + 11 + (i * s->words + w) * 2, s->ring[s->me][(first + i) % GNC_RING].words[w]);
    s->send(s->user, to, d, (int)(11 + n * s->words * 2));
}

int gnc_frame(gnc_session* s, const uint16_t* packet) {
    int made = 0, p;
    if (s->made - s->taken < GNC_RING && s->made - oldest_ack(s) < GNC_RING) {
        gnc_slot* slot = &s->ring[s->me][s->made % GNC_RING];
        memset(slot, 0, sizeof(*slot));
        if (packet)
            memcpy(slot->words, packet, (size_t)s->words * 2);
        s->known[s->me][s->made % GNC_RING] = 1;
        s->made++;
        s->have[s->me] = s->made;
        made = 1;
    }
    for (p = 0; p < s->players; p++)
        if (p != s->me)
            send_to(s, p);
    return made;
}

void gnc_receive(gnc_session* s, const uint8_t* d, int size) {
    uint32_t first, ack, count, i;
    int from, w;
    if (size < 11 || d[0] != 'S')
        return;
    from = d[1];
    if (from < 0 || from >= s->players || from == s->me)
        return;
    first = get32(d + 2);
    count = d[6];
    ack = get32(d + 7);
    if (ack > s->acked[from] && ack <= s->made)
        s->acked[from] = ack;
    for (i = 0; i < count && 11 + (int)((i + 1) * s->words * 2) <= size; i++) {
        uint32_t slot = first + i;
        if (slot < s->taken || slot >= s->taken + GNC_RING)
            continue;
        for (w = 0; w < s->words; w++)
            s->ring[from][slot % GNC_RING].words[w] = get16(d + 11 + (i * s->words + w) * 2);
        s->known[from][slot % GNC_RING] = 1;
    }
    while (s->have[from] < s->taken + GNC_RING && s->have[from] >= s->taken && s->known[from][s->have[from] % GNC_RING])
        s->have[from]++;
}

int gnc_take(gnc_session* s, uint16_t out[GNC_MAX_PLAYERS][GNC_MAX_WORDS]) {
    uint32_t ready = s->made;
    int p, w;
    for (p = 0; p < s->players; p++)
        if (s->have[p] < ready)
            ready = s->have[p];
    ready -= s->taken;
    s->frames++;
    if (ready == 0 || (s->refill && (int)ready < s->target)) {
        s->waited++;
        if (!s->refill) {
            s->refill = 1;
            if (s->frames - s->last_wait < 180 && s->target < 8)
                s->target++;
            s->last_wait = s->frames;
        }
        s->calm = 0;
        return 0;
    }
    s->refill = 0;
    if (++s->calm > 900 && s->target > 2) {
        s->target--;
        s->calm = 0;
    }
    for (p = 0; p < s->players; p++) {
        for (w = 0; w < s->words; w++)
            out[p][w] = s->ring[p][s->taken % GNC_RING].words[w];
        s->known[p][s->taken % GNC_RING] = 0;
    }
    s->taken++;
    return 1;
}

/* ---- desync check */

int gnc_check_init(gnc_check* c, int me, uint32_t every, uint32_t settle) {
    if (me < 0 || me >= GNC_MAX_PLAYERS || every == 0)
        return 0;
    memset(c, 0, sizeof(*c));
    c->me = me;
    c->every = every;
    c->settle = settle;
    return 1;
}

void gnc_check_record(gnc_check* c, uint32_t frame, uint32_t hash) {
    int i = (int)((frame / c->every) % GNC_CHECKS);
    if (frame % c->every != 0)
        return;
    if (c->at[i] == frame && (c->state[i] == 2 || c->state[i] == 4))
        return; /* final: a late replay cannot change what was sent */
    c->at[i] = frame;
    c->hash[i] = hash;
    c->state[i] = hash ? 1 : 3;
}

/* our final hash of slot i against what the others sent for the same frame */
static int check_compare(gnc_check* c, int i) {
    int p, j, result = 0;
    if (c->state[i] != 2)
        return 0;
    for (p = 0; p < GNC_MAX_PLAYERS; p++)
        for (j = 0; j < GNC_CHECKS; j++)
            if (c->peer_known[p][j] == 1 && c->peer_at[p][j] == c->at[i]) {
                c->peer_known[p][j] = 2; /* compared: a duplicate of it is ignored */
                c->checks++;
                if (c->peer_hash[p][j] != c->hash[i]) {
                    c->mismatches++;
                    if (!c->first_mismatch)
                        c->first_mismatch = c->at[i] + 1;
                    result = -1;
                } else if (result == 0) {
                    result = 1;
                }
            }
    return result;
}

int gnc_check_poll(gnc_check* c, uint32_t confirmed, uint8_t out[GNC_CHECK_DATAGRAM]) {
    int i;
    for (i = 0; i < GNC_CHECKS; i++) {
        if ((c->state[i] != 1 && c->state[i] != 3) || c->at[i] + c->settle > confirmed)
            continue;
        if (c->state[i] == 3) { /* nothing to compare at this frame */
            c->state[i] = 4;
            c->skipped++;
            continue;
        }
        c->state[i] = 2;
        out[0] = 'H';
        out[1] = (uint8_t)c->me;
        put32(out + 2, c->at[i]);
        put32(out + 6, c->hash[i]);
        check_compare(c, i);
        return GNC_CHECK_DATAGRAM;
    }
    return 0;
}

int gnc_check_receive(gnc_check* c, const uint8_t* d, int size) {
    uint32_t at, hash;
    int from, i, j;
    if (size < GNC_CHECK_DATAGRAM || d[0] != 'H')
        return 0;
    from = d[1];
    if (from >= GNC_MAX_PLAYERS || from == c->me)
        return 0;
    at = get32(d + 2);
    hash = get32(d + 6);
    if (hash == 0 || at % c->every != 0)
        return 0;
    i = (int)((at / c->every) % GNC_CHECKS);
    if (c->at[i] == at && c->state[i] == 4)
        return 0; /* we skipped that frame */
    for (j = 0; j < GNC_CHECKS; j++)
        if (c->peer_known[from][j] && c->peer_at[from][j] == at)
            return 0; /* a duplicate */
    j = i;
    c->peer_at[from][j] = at;
    c->peer_hash[from][j] = hash;
    c->peer_known[from][j] = 1;
    return c->at[i] == at ? check_compare(c, i) : 0;
}

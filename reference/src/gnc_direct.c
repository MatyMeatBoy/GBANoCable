/* gnc_direct: see include/gnc_direct.h. Copyright (c) 2026 MatyMeatBoy. MIT License. */
#include "gnc_direct.h"

#include <string.h>

/* datagram (little endian):
 *   0 'G'   1 sender   2 blocks carried   3 entries carried
 *   4 label of the first block   8 number of the first entry
 *   12 + 4*i  the next label the sender expects from player i (the acknowledgement of blocks)
 *   28 + 4*i  the next entry the sender expects of player i's stream (the acknowledgement of messages; a player
 *             that is not the host only fills i = 0: the next entry of the order)
 *   44 the blocks, then the entries: a length byte, the sender of the message, the message */

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int gd_init(gd_session* s, int players, int me, int block_bytes, int ordered_from, gd_send_fn send, void* user) {
    int p;
    if (players < 2 || players > GD_MAX_PLAYERS || me < 0 || me >= players || block_bytes < 1 ||
        block_bytes > GD_BLOCK_MAX || ordered_from < 0 || ordered_from > block_bytes || !send)
        return 0;
    memset(s, 0, sizeof(*s));
    s->players = players;
    s->me = me;
    s->block_bytes = block_bytes;
    s->ordered_from = ordered_from;
    s->hole_fix = 1;
    s->label = 1;
    s->out_base = s->out_next = s->out_own = 1;
    s->in_next = 1;
    for (p = 0; p < GD_MAX_PLAYERS; p++) {
        s->peer[p].next = s->peer[p].cursor = 1;
        s->peer[p].acked = s->peer[p].msg_acked = s->peer[p].from_next = 1;
    }
    s->send = send;
    s->user = user;
    return 1;
}

void gd_set_hole_fix(gd_session* s, int on) {
    s->hole_fix = on != 0;
}

/* Entries nobody needs any more are forgotten: the host's when every player has them and its own game took
   them, another player's when the host has them. Returns 1 if there is room for one more. */
static int msg_room(gd_session* s) {
    uint32_t have = s->out_next;
    int p;
    if (s->me == 0) {
        for (p = 1; p < s->players; p++)
            if (s->peer[p].msg_acked < have)
                have = s->peer[p].msg_acked;
        if (s->out_own < have)
            have = s->out_own;
    } else {
        have = s->peer[0].msg_acked;
    }
    if (have > s->out_base)
        s->out_base = have;
    return s->out_next - s->out_base < GD_MSG_OUT;
}

static void msg_append(gd_session* s, int sender, const uint8_t* data, int size) {
    uint8_t* entry = s->out_data[s->out_next % GD_MSG_OUT];
    entry[0] = (uint8_t)sender;
    memcpy(entry + 1, data, (size_t)size);
    s->out_len[s->out_next % GD_MSG_OUT] = (uint8_t)(1 + size);
    s->out_next++;
}

int gd_msg_send(gd_session* s, const uint8_t* data, int size) {
    if (size < 1 || size > GD_MSG_MAX || !msg_room(s))
        return 0;
    msg_append(s, s->me, data, size);
    return 1;
}

int gd_msg_next(gd_session* s, int* from, uint8_t* out, int capacity) {
    const uint8_t* entry;
    int size;
    if (s->me == 0) {
        if (s->out_own == s->out_next)
            return 0;
        entry = s->out_data[s->out_own % GD_MSG_OUT];
        size = s->out_len[s->out_own % GD_MSG_OUT];
        s->out_own++;
    } else {
        if (s->in_tail == s->in_head)
            return 0;
        entry = s->in_data[s->in_tail % GD_MSG_IN];
        size = s->in_len[s->in_tail % GD_MSG_IN];
        s->in_tail++;
    }
    *from = entry[0];
    size--;
    if (size > capacity)
        size = capacity;
    memcpy(out, entry + 1, (size_t)size);
    return size;
}

/* is that player listening (so that what it lacks decides where a datagram's entries start)? */
static int listening(const gd_session* s, int p) {
    return p != s->me && s->peer[p].heard && s->frame - s->peer[p].heard <= GD_QUIET;
}

/* One datagram: the blocks from `first_label` to the newest, then the entries first..stop-1 that fit.
   Returns the entry after the last one that fitted. */
static uint32_t send_one(gd_session* s, uint32_t first_label, uint32_t first, uint32_t stop) {
    uint8_t d[GD_MAX_DATAGRAM];
    uint32_t label, number;
    int p, size = GD_HEADER, blocks = 0, carried = 0;

    memset(d, 0, GD_HEADER);
    d[0] = 'G';
    d[1] = (uint8_t)s->me;
    put32(d + 4, first_label);
    put32(d + 8, first);
    for (p = 0; p < s->players; p++) {
        put32(d + 12 + 4 * p, p == s->me ? 0 : s->peer[p].next);
        put32(d + 28 + 4 * p, s->me == 0 ? s->peer[p].from_next : p == 0 ? s->in_next : 0);
    }
    for (label = first_label; label < s->label; label++, blocks++, size += s->block_bytes)
        memcpy(d + size, s->own[label % GD_RING], (size_t)s->block_bytes);
    for (number = first; number != stop && carried < 255; number++, carried++) {
        int length = s->out_len[number % GD_MSG_OUT];
        if (size + 1 + length > GD_MAX_DATAGRAM)
            break;
        d[size] = (uint8_t)length;
        memcpy(d + size + 1, s->out_data[number % GD_MSG_OUT], (size_t)length);
        size += 1 + length;
    }
    d[2] = (uint8_t)blocks;
    d[3] = (uint8_t)carried;
    s->send(s->user, d, size);
    return number;
}

void gd_frame(gd_session* s, const uint8_t* block) {
    uint32_t from, first, kept_up, next;
    int p, extra;

    s->frame++;
    memcpy(s->own[s->label % GD_RING], block, (size_t)s->block_bytes);
    s->label++;
    /* every block the others have not acknowledged, but never more than GD_REACH back */
    from = s->label;
    for (p = 0; p < s->players; p++)
        if (p != s->me && s->peer[p].acked < from)
            from = s->peer[p].acked;
    if (s->label - from > GD_REACH)
        from = s->label - GD_REACH;
    /* the entries: the host's stream is for everybody, another player's for the host alone. A player that
       has been silent is not sent to: what it lacks may be more than a datagram holds, and the players that are
       listening must not wait behind it. When it speaks again and is far behind, it gets datagrams of its own
       in the same frame; to the others those repeat what they have. */
    msg_room(s);
    first = kept_up = s->out_next;
    for (p = 0; p < s->players; p++) {
        if (!listening(s, p) || (s->me != 0 && p != 0))
            continue;
        if (s->peer[p].msg_acked < first)
            first = s->peer[p].msg_acked;
        if (s->peer[p].msg_acked < kept_up && s->out_next - s->peer[p].msg_acked <= GD_NEAR)
            kept_up = s->peer[p].msg_acked;
    }
    if (first < s->out_base)
        first = s->out_base;
    if (kept_up < first)
        kept_up = first;
    send_one(s, from, kept_up, s->out_next);
    for (extra = 0; extra < 2 && first < kept_up; extra++, first = next) {
        next = send_one(s, from, first, kept_up);
        if (next == first)
            break;
    }
}

void gd_receive(gd_session* s, const uint8_t* d, int size) {
    gd_stream* st;
    uint32_t first, number, ack, i;
    int from, blocks, carried, at;

    if (size < GD_HEADER || size > GD_MAX_DATAGRAM || d[0] != 'G')
        return;
    from = d[1];
    blocks = d[2];
    carried = d[3];
    if (from >= s->players || from == s->me || size < GD_HEADER + blocks * s->block_bytes)
        return;
    st = &s->peer[from];
    st->heard = s->frame ? s->frame : 1;
    ack = get32(d + 12 + 4 * s->me);
    if (ack > st->acked && ack <= s->label)
        st->acked = ack;

    first = get32(d + 4);
    if (s->hole_fix && blocks == GD_REACH && first > st->next) {
        /* A hole. A datagram starts at the oldest block a receiver lacks, but never more than GD_REACH back;
           this one is full and still starts after the block waited for, and every later one starts later
           still. The blocks in between will never come. Step over them. */
        st->next = first;
        if (st->cursor < first)
            st->cursor = first;
        st->holes++;
    }
    for (i = 0; i < (uint32_t)blocks; i++) {
        uint32_t label = first + i;
        if (label < st->next || label - st->next >= GD_RING)
            continue;
        memcpy(st->block[label % GD_RING], d + GD_HEADER + i * (uint32_t)s->block_bytes, (size_t)s->block_bytes);
        st->have[label % GD_RING] = label + 1;
    }
    while (st->have[st->next % GD_RING] == st->next + 1)
        st->next++;

    /* messages: two players that are not the host have nothing to tell each other here */
    if (s->me != 0 && from != 0)
        return;
    ack = get32(d + 28 + 4 * (s->me == 0 ? 0 : s->me));
    if (ack > st->msg_acked && ack <= s->out_next)
        st->msg_acked = ack;
    number = get32(d + 8);
    at = GD_HEADER + blocks * s->block_bytes;
    for (i = 0; i < (uint32_t)carried && at < size; i++, number++) {
        int length = d[at];
        if (at + 1 + length > size || length < 2 || length > 1 + GD_MSG_MAX)
            break;
        /* in order only: a later entry is repeated until the ones before it are here; with no room nothing is
           taken, so the sender repeats until there is */
        if (s->me == 0) {
            if (number == st->from_next && msg_room(s)) {
                msg_append(s, from, d + at + 2, length - 1); /* this is the moment it gets its place in the order */
                st->from_next++;
            }
        } else if (number == s->in_next && s->in_head - s->in_tail < GD_MSG_IN) {
            memcpy(s->in_data[s->in_head % GD_MSG_IN], d + at + 1, (size_t)length);
            s->in_len[s->in_head % GD_MSG_IN] = (uint8_t)length;
            s->in_head++;
            s->in_next++;
        }
        at += 1 + length;
    }
}

int gd_state(gd_session* s, int player, uint8_t* out) {
    gd_stream* st;
    int tail = s->block_bytes - s->ordered_from;
    if (player < 0 || player >= s->players || player == s->me)
        return -1;
    st = &s->peer[player];
    /* catch up: a waiting block that says nothing new in its ordered bytes, with a newer one behind it */
    while (st->has_last && st->next > st->cursor + 1 &&
           (tail == 0 || !memcmp(st->block[st->cursor % GD_RING] + s->ordered_from, st->last + s->ordered_from,
                                 (size_t)tail)))
        st->cursor++;
    if (st->cursor < st->next) {
        memcpy(st->last, st->block[st->cursor % GD_RING], (size_t)s->block_bytes);
        st->cursor++;
        st->has_last = 1;
        memcpy(out, st->last, (size_t)s->block_bytes);
        return 1;
    }
    if (!st->has_last)
        return -1;
    st->repeats++;
    memcpy(out, st->last, (size_t)s->block_bytes);
    return 0;
}

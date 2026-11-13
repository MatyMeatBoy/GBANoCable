/* gnc_direct: state replication and a reliable ordered message channel over any datagram transport (2-4 players).
 *
 * For games whose link blocks are not inputs of a lockstep simulation but each console's own state, rewritten
 * every frame, plus events that happen once (docs/09, docs/10). Two things travel in every datagram:
 *
 *   - blocks: each player's state block of every frame, labelled 1, 2, 3... A datagram repeats every block the
 *     others have not acknowledged, at most GD_REACH of them. Each player's blocks are handed to the game in
 *     order as they arrive; when nothing new came the last one is given again. Nobody waits for anybody.
 *   - messages: whole events on a channel that is reliable and has ONE order for everybody. A player's message
 *     goes to the host (player 0) only; the host puts every message, its own included, into one numbered
 *     stream; every player takes messages from that stream, each exactly once, its own too.
 *
 * The hole. A stream that was silent for more than GD_REACH frames (a connection that was gone) has blocks no
 * datagram will ever bring again: datagrams only reach GD_REACH blocks back. A receiver that keeps waiting for
 * the first missing block stands still for good: that player is frozen on its screen although the messages go
 * on. With the hole fix (on by default) a full datagram that starts after the block waited for is taken as
 * proof of a hole and the stream steps over it: state that old is of no use. gd_set_hole_fix(s, 0) is there so
 * the test can show the freeze.
 *
 * Not here (see docs/11, docs/12): giving a silent player up, standing characters, taking a player in again
 * after it was given up, rooms and seats, join in progress. A player that is away is simply waited for: its
 * messages and the ones it missed are kept (GD_MSG_OUT of them) and repeated until acknowledged.
 *
 * Single-threaded, no allocation, no clock: time is counted in calls of gd_frame.
 * Copyright (c) 2026 MatyMeatBoy. MIT License.
 */
#ifndef GNC_DIRECT_H
#define GNC_DIRECT_H

#include <stdint.h>

#define GD_MAX_PLAYERS 4
#define GD_BLOCK_MAX 24    /* bytes of one state block */
#define GD_REACH 40        /* blocks one datagram reaches back */
#define GD_RING 256        /* blocks kept per player */
#define GD_MSG_MAX 64      /* bytes of one message */
#define GD_MSG_OUT 1024    /* entries a sender keeps until they are acknowledged */
#define GD_MSG_IN 128      /* entries a receiver holds for its game */
#define GD_NEAR 64         /* a peer this close to the newest entry is served by the datagram everybody gets */
#define GD_QUIET 30        /* frames of silence after which a peer is not sent messages until it speaks again */
#define GD_HEADER 44
#define GD_MAX_DATAGRAM 1200

/* one datagram for every other player (what a relay does with it) */
typedef void (*gd_send_fn)(void* user, const uint8_t* data, int size);

typedef struct {
    uint32_t next;                  /* lowest label not received yet */
    uint32_t cursor;                /* next label to hand to the game */
    uint32_t have[GD_RING];         /* label + 1 of the block in each ring slot */
    uint8_t block[GD_RING][GD_BLOCK_MAX];
    uint8_t last[GD_BLOCK_MAX];     /* the block handed over last */
    int has_last;
    uint32_t acked;                 /* next label of mine that player expects */
    uint32_t msg_acked;             /* next entry of my stream that player expects */
    uint32_t from_next;             /* host only: next entry of that player's stream the host expects */
    uint32_t heard;                 /* my frame when its last datagram came, 0 never */
    uint32_t holes, repeats;        /* holes stepped over; frames the last block was given again */
} gd_stream;

typedef struct {
    int players, me, block_bytes, ordered_from, hole_fix;
    uint32_t frame;                 /* calls of gd_frame */
    uint32_t label;                 /* label of my next block (the first is 1) */
    uint8_t own[GD_RING][GD_BLOCK_MAX];
    gd_stream peer[GD_MAX_PLAYERS];
    /* my stream of entries: sender, then the message. The host's is the order. */
    uint32_t out_base, out_next, out_own;
    uint8_t out_len[GD_MSG_OUT];
    uint8_t out_data[GD_MSG_OUT][1 + GD_MSG_MAX];
    /* not the host: entries of the order waiting for the game */
    uint32_t in_next, in_head, in_tail;
    uint8_t in_len[GD_MSG_IN];
    uint8_t in_data[GD_MSG_IN][1 + GD_MSG_MAX];
    gd_send_fn send;
    void* user;
} gd_session;

/* players 2..4; me 0..players-1 (0 is the host: the cable's parent); block_bytes 1..GD_BLOCK_MAX.
   ordered_from: bytes of a block from this offset on are passed step by step by the game (a lobby handshake)
   and must be seen in order, so a waiting block is only left out when those bytes repeat the block before it.
   Give block_bytes for a block that is state only: then the newest waiting block is what the game gets. */
int gd_init(gd_session* s, int players, int me, int block_bytes, int ordered_from, gd_send_fn send, void* user);

/* 1 (default): step over holes. 0: the behaviour that froze players, for tests. */
void gd_set_hole_fix(gd_session* s, int on);

/* a datagram from the network (any order, duplicates and losses are fine) */
void gd_receive(gd_session* s, const uint8_t* data, int size);

/* once per game frame, after the game wrote its block: publishes it and sends this frame's datagram(s) */
void gd_frame(gd_session* s, const uint8_t* block);

/* that player's block for this frame. 1: a new one; 0: nothing new came, the last one again; -1: never heard
   (or player == me: a game reads its own block from where it wrote it). Call once per frame and player. */
int gd_state(gd_session* s, int player, uint8_t* out);

/* one whole message of this player, 1..GD_MSG_MAX bytes. 0: no room (a player is far behind): try next frame. */
int gd_msg_send(gd_session* s, const uint8_t* data, int size);

/* the next message of the order, whoever sent it (this player included): its size, 0 if none is here yet.
   Every player is given the same messages in the same order, each exactly once. */
int gd_msg_next(gd_session* s, int* from, uint8_t* out, int capacity);

#endif

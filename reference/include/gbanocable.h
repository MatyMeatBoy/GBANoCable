/* gbanocable: the link cable as packet slots over any datagram transport (2-4 players).
 *
 * Every game frame each player makes one slot (one packet of its game's send queue, or an empty one) and sends it
 * to every other player with the slot number. When slot n of every player is known, the group of slot n (the
 * packets of all players, in player order) is handed to the game: the same groups, in the same order, on every
 * machine. Nothing is ever reported as a link error; a missing slot only makes the game wait.
 *
 * The library is transport-agnostic and single-threaded: the game calls gnc_frame once per game frame, feeds every
 * received datagram to gnc_receive, sends what gnc_frame asks for, and takes groups with gnc_take.
 *
 * Copyright (c) 2026 MatyMeatBoy. MIT License.
 */
#ifndef GBANOCABLE_H
#define GBANOCABLE_H

#include <stdint.h>

#define GNC_MAX_PLAYERS 4
#define GNC_MAX_WORDS 16 /* 16-bit words in one game packet */
#define GNC_RING 64      /* slots kept; no slot is made beyond what every peer confirmed */
#define GNC_RESEND 24    /* slots carried by one datagram */
#define GNC_MAX_DATAGRAM (11 + GNC_RESEND * GNC_MAX_WORDS * 2)

typedef struct {
    uint16_t words[GNC_MAX_WORDS];
} gnc_slot;

/* called by gnc_frame for every datagram to send */
typedef void (*gnc_send_fn)(void* user, int to_player, const uint8_t* data, int size);

typedef struct {
    int players, me, words;
    uint32_t made;                    /* our slots made */
    uint32_t taken;                   /* groups given to the game */
    uint32_t have[GNC_MAX_PLAYERS];   /* contiguous slots we hold of each player (ours: made) */
    uint32_t acked[GNC_MAX_PLAYERS];  /* our slots each peer holds */
    gnc_slot ring[GNC_MAX_PLAYERS][GNC_RING];
    uint8_t known[GNC_MAX_PLAYERS][GNC_RING];
    /* smoothing: after a wait, hold groups until `target` are complete; target follows the connection */
    int target, refill;
    uint32_t frames, waited, last_wait, calm;
    gnc_send_fn send;
    void* user;
} gnc_session;

/* players 2..4, me 0..players-1 (0 is the cable's parent), words per packet 1..GNC_MAX_WORDS */
int gnc_init(gnc_session* s, int players, int me, int words, gnc_send_fn send, void* user);

/* once per game frame: our slot (packet may be NULL for an empty slot), then the datagrams to every peer.
   Returns 0 when the slot could not be made (a peer is too far behind: the game should keep its packet). */
int gnc_frame(gnc_session* s, const uint16_t* packet);

/* a datagram from the network (any order, duplicates and losses are fine) */
void gnc_receive(gnc_session* s, const uint8_t* data, int size);

/* the next group: out[player][word]. Returns 1 with a group, 0 when the game must wait this frame. */
int gnc_take(gnc_session* s, uint16_t out[GNC_MAX_PLAYERS][GNC_MAX_WORDS]);

/* ---- desync check (optional, independent of the slots)
 *
 * Every `every` link frames each game hashes the state all players must agree on (positions, health, random
 * generators, in player order, never side-relative) and calls gnc_check_record. With rollback a frame is played
 * several times: record it every time; the last value counts. The hash is sent only once the frame is final
 * (`settle` frames behind what every player confirmed, with no rollback pending), so a guess that is corrected
 * later never reports a desync. A hash of 0 means "no check here": return it when the shared state stops being
 * shared on purpose, e.g. once a round is decided and each side plays its own win or loss screen (which can draw
 * random numbers differently) while the next round starts from a seed exchanged again.
 */
#define GNC_CHECKS 8
#define GNC_CHECK_DATAGRAM 10 /* 'H', from, frame (4), hash (4) */

typedef struct {
    int me;
    uint32_t every, settle;
    uint32_t at[GNC_CHECKS], hash[GNC_CHECKS];
    uint8_t state[GNC_CHECKS]; /* 0 empty, 1 recorded, 2 final (sent), 3 recorded as skipped, 4 final skipped */
    uint32_t peer_at[GNC_MAX_PLAYERS][GNC_CHECKS], peer_hash[GNC_MAX_PLAYERS][GNC_CHECKS];
    uint8_t peer_known[GNC_MAX_PLAYERS][GNC_CHECKS]; /* 0 none, 1 received, 2 compared */
    uint32_t checks, mismatches, skipped, first_mismatch; /* first_mismatch: its frame + 1 (0 none) */
} gnc_check;

/* me as in gnc_init; every > 0 link frames between checks; settle = frames a rollback can reach back (or more) */
int gnc_check_init(gnc_check* c, int me, uint32_t every, uint32_t settle);

/* the state hash of `frame` (call it when frame % every == 0, also when a rollback plays the frame again).
   hash 0: no check for this frame. Frames already final are not changed. */
void gnc_check_record(gnc_check* c, uint32_t frame, uint32_t hash);

/* `confirmed`: link frames whose groups are known for real by this game (no guess pending). Returns the size of a
   datagram to send to every other player (0: nothing to send; call again until 0). Sending it twice is fine. */
int gnc_check_poll(gnc_check* c, uint32_t confirmed, uint8_t out[GNC_CHECK_DATAGRAM]);

/* an 'H' datagram from another player. Returns -1 when it revealed a desync, 1 a check that matched, 0 otherwise
   (not an 'H' datagram, a duplicate, or our side of that frame is not final yet). */
int gnc_check_receive(gnc_check* c, const uint8_t* data, int size);

#endif

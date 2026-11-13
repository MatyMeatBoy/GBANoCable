# Anatomy of a Multi SIO game, as seen in a real one

[docs/03](03-software-patterns.md) lists the usual layers. This chapter takes one game apart, Shining Soul II (GBA,
2-4 players, co-op), because it is *not* the lockstep case and its shape decided the whole online design. Source:
the game's matching decompilation and its link notes (SS2Decomp, `docs/LINK_CABLE.md`, `LINK_PLAYER_PACKET.md`,
`LINK_MESSAGES.md`) and the port that runs it online (ShiningSource). Everything here is **read from code** unless
a line says *measured* or *traffic* (read off a message log of scripted sessions on one machine).

## 1. The driver: one block out, four in, a status word

The game links the SDK's MultiSio library. Game code never touches the serial registers except to read its player
number. Once per frame its link task calls the library's main function with two buffers:

- **send**: one application block of 24 bytes;
- **receive**: four blocks of 24 bytes, one per seat, the sender's own included;
- **result**: a status word: a bit per seat whose block arrived this frame, a "parent" bit, a bit per seat that is
  connected, a hard-error bit, a counter.

Under it the library wraps each block in a 32-byte packet (a small header with a checksum over the first 28 bytes,
the 24 application bytes, four guard bytes) and clocks it out word by word from a serial/timer interrupt handler
that the cartridge copies to RAM. None of that matters online: the per-frame function is the seam
([docs/14](14-recomp-playbook.md)).

The link task keeps counters on that status word: more than a few hard errors in a row, or a connected seat that is
not received for more than a set number of checks (80 in a session, 110 before play starts: *read from code and
seen in tests*), and the game runs its link error path, which ends in a reset. **Any online layer must keep those
counters from running out, or the cartridge ends the session itself.**

In single player the same game runs a loopback task that copies the send block into receive slot 0. That tells you
the game's simulation always reads "the players" through these buffers, alone or linked.

## 2. One block, two parts

| Bytes | What | Kind |
|---|---|---|
| 0..7 | the player's continuous state: position, facing, animation state, a few mode bits, packed into bit fields | **state**: rewritten every frame, the newest value is all that counts |
| 8..10 | a small header: a command or the sender's id, a code, a count or chunk number | **events**, passed step by step |
| 12..19 | 8 bytes of payload for the current step | |
| 20..23 | acknowledge bytes, one per seat: what this console has received of each other seat's current step | |

How to tell state from events in any game:

- **State** is produced by the code that *moves the character*, every frame, unconditionally, and consumed by code
  that *draws or locates* the other players (a consumer that reads position straight from another seat's receive
  block is the proof). Losing or delaying a block costs nothing lasting.
- **Events** are produced by a *queue* and a *sender task*, consumed by a *receiver task* that ends in a `switch`
  on a tag. They are not idempotent: each must be acted on once.
- The decisive observation: **each console reads its own block from its send buffer and the others' from the
  receive slots.** The accessor that returns "the block of player i" returns the send buffer for the own id. A
  console therefore never waits for the link to see its own actions, and nothing forces two consoles to be on the
  same frame. This is state replication, not lockstep.

## 3. The lobby

Before play the same bytes 8..10 carry a tiny handshake, with no message layer yet: each client writes a two-letter
"ready" mark, the host answers with a two-letter "go" mark when every other seat is ready and its player confirms,
and one more byte of the host's block carries two session settings that the clients apply. The handshake is state
too (held, not queued), but **it must be seen in order**: a transport that skips blocks freely may never show a
client the "go".

Seats are **counted, not named**: the game counts the connected bits and loops over players `0 .. count-1`. A hole
in the middle of the bits hides the last player. Seat 0 is the parent and has duties no other seat has (section 6).

Per-seat things (a player record, a task, a receiver for that seat's messages) are made **once**, when the session
starts, for the seats counted then. Nothing in the cartridge makes one later
([docs/12](12-join-in-progress.md) is about exactly that).

## 4. The message layer (a mailbox inside the blocks)

Everything that happens once is a *message*: a tag, a count of 8-byte chunks (up to 16), the payload. A queue of 50
entries holds what the game wants to say. One sender task takes the first entry and passes it through the block:

1. **announce** the tag and the chunk count, repeated every frame until *every* seat's acknowledge byte answers;
2. for each chunk: put the chunk number and 8 bytes into the block, repeated until every seat acknowledges *that*
   chunk;
3. a final wait until every connected seat answers, then the queue is popped and the next message may start.

One receiver task per seat watches that seat's block, acknowledges, collects the chunks and dispatches on the tag.

On a cable each step takes a frame or two. A four-chunk message is about five steps. Three properties follow, and
each one bites online:

- **Cost.** Every step is a round trip. Behind a transport that delays blocks by `d` frames a step costs about
  `2d + 2` frames; a message of five steps at `d = 6` costs 70 frames, and the next message of that player waits
  behind it. *Measured* (one machine, 60 ms simulated latency): about 650 ms per message.
- **Implicit global order.** Because each step waits for everybody, a message is complete on every console within
  a frame or two of the others. All consoles act on all messages in practically the same order, and the game
  leans on it (section 5).
- **No retry limit where it matters.** The announce phase waits without a timeout; the chunk phases count
  unsuccessful checks. A transport that never delivers one acknowledgement stalls that player's queue silently.

## 5. What the messages are, by role

The game has about fifty tags. What matters for a transport is their *roles*, because the roles say what breaks:

| Role | Examples (in words) | Who sends | What it needs from the transport |
|---|---|---|---|
| identity | a player's name, class, weapon, flags, two blocks of statistics | every player, when its player task starts and when the values change | reach everybody once; a late joiner needs the **last** of each |
| session settings | the host's configuration block | host, at the scene's start | the same |
| world set-up | the layouts chosen for a map's records | host, inside a map's set-up | be at the same point of everybody's set-up |
| spawn / withdraw | "this enemy appears", "these spawns are withdrawn" | host only | exactly once, in order: identifiers are reused |
| position correction | "enemy N is at x, y" | host only, periodically | may be dropped if a newer one follows; frequency is a latency knob |
| hits and status | damage to a target, a status applied, an entity dying | whoever caused it | order relative to other players' hits on the same target |
| items | a drop request, the host's record of the item, a pick-up request, the host's answer, the removal | requester and host | request/answer pairs through the host: **the host is the authority** |
| map change | "everybody changes map", an exit taken, a story step that names what every console runs next | host or the machine at the trigger | everybody, same order relative to barriers |
| barrier token | "I am at this point" (section 6) | every player | exactly once; one too many or too few breaks the next barrier |

Three facts from this table drive the design in [docs/10](10-direct-transport-and-messages.md):

1. What one player's message does can depend on what another player's message did before it (an enemy struck by
   one and killed by another; a pick-up request and the host's record; a new enemy with the identifier of one that
   just died). So messages need **one order on every machine**, not just per-sender order.
2. The host owns enemies and items. So a channel that orders everything **through the host** adds no new authority.
3. Several handlers assume the cable's pace. Example found by a real session: the game can start two spawn requests
   at a time and drops a third; over the cable spawn messages arrived frames apart, online three can be acted on in
   one frame, and one console lost an enemy the others had. When messages arrive in bursts, look for handlers with
   small fixed pools.

One story message carries **the address of a function** to run on every console. On cartridges all addresses are
equal. In a native port two different builds have different addresses: seen in the traffic as a native code
address (*traffic*; not resolved in the port: builds of different processors agreeing on it has not been examined).
Look for function pointers in payloads before promising crossplay.

## 6. The barrier

Where every console must be at the same point before the game goes on (a map's set-up, the start of a dungeon from
the world map, a boss's phases), the game uses a three-call pattern:

1. *send*: queue the barrier message with a token value;
2. *test*: "does every counted seat's ring hold the token?" (the receiver appends each token it receives to a small
   ring per sender);
3. *take*: remove one token from every ring.

Tokens are **not numbered**. A token too many in a ring lets the next barrier pass early; a token too few holds it
for ever. Everything about departures, reconnects and late joiners ([docs/11](11-departures-and-reconnect.md),
[docs/12](12-join-in-progress.md)) is at bottom a question of keeping these rings right.

Not everything at a barrier is a message. On the world map the host starts the dungeon by a **state in its block**
that is visible only while the barrier stands. A player who was away at that moment can be sent every message it
missed and will still never start the dungeon. So: *message replay restores events, not state that was only shown
in passing.*

Other "everybody" checks are plain state: a map exit fires when every seat's block is inside the exit zone for
several checks running; a portal when every seat's block has a "ready" bit; the world map when every seat's cursor
is on the same place. A seat that is counted but not played blocks all of them.

## 7. What to write down for your game

Use [games/TEMPLATE.md](../games/TEMPLATE.md). The fields that this chapter adds to the old template:

- link style per mode (lockstep / state / messages), with the evidence (who writes and reads which buffer);
- the block layout split into state bytes and event bytes;
- the message steps and what each waits for; queue depth; which handlers have fixed pools;
- message kinds **by role** and by sender (host only? any player?);
- every "everybody" condition: barriers (how tokens are counted) and state checks;
- the link task's counters (frames a silent seat is tolerated) before and during play;
- what seat 0 alone does.

## Limits

- One game. The split "state in the first bytes, a stop-and-wait mailbox behind them" is this engine's; another
  game may put events in a second packet type or run a real lockstep. Do the reading for yours.
- The byte-exact layout of the state bytes is only partly known (the first word is proven from the original
  assembly; the second is state-dependent). The online layer never needed it: it carries the bytes whole.
- The decompilation's link notes were written from source; the runtime behaviour above was only observed through
  the port's logs, never on linked consoles or in an emulator link.

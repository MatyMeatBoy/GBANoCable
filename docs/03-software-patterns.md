# How games use the cable

Most commercial GBA games do not touch SIOCNT directly in their game logic. They link a communications library
(Nintendo's MultiSio and its variants, or the developer's own) and the game talks to that library.

## The usual layers

1. **Registers + interrupts**: start a transfer, read SIOMULTI in the serial IRQ, a timer paces the parent.
2. **Word stream → packets**: a packet is N words plus a checksum word. KHCoM: 4 words + checksum (5 transfers),
   queues of 32 packets each way. Shining Soul 2 (EU): 24-byte application blocks in 32-byte driver packets, a
   checksum over the first 28 bytes.
3. **Packet queues**: `send queue` (the game enqueues one packet a frame) and `receive queue` (packets with every
   player's data for the same slot). An all-zero packet often counts as "nothing sent".
4. **Status word**: connected, player id, player count, parent flag, "receive queue empty", errors.
5. **Protocol**: connect request/accept, data exchange (decks, names, seeds), then the play loop.

## Things built for 0 ms that break online

| Mechanism | On the cable | Over the network | GBANoCable |
|---|---|---|---|
| Accept/retry counters | answer in 1-2 frames, give up after ~10 | answer after a round trip | raise the limit only online |
| Timeouts ("child silent 6 VBlanks") | cable pulled | a lag spike | never report; wait |
| Flow control (pause when > N queued) | rare | constant with buffering; sends empty packets the other side reads as missing | off online: the slots pace both sides |
| Checksums | line noise | the network already delivers exact data | not raised |
| Lockstep main loop ("advance only with a packet") | always one per frame | the game waits | keep it: it is what keeps both in sync |

## Lockstep games are the good case

Many battle modes only advance a frame when the frame's packet arrived (KHCoM's `AgbMain`: `ModeUpdate` only if the
receive queue was not empty). If both sides receive the same packets in the same order, both simulations stay
identical forever (same random seed exchanged at start, same inputs). Desync is then impossible by construction;
latency only shows as waiting, which the adaptive buffer hides.

## Not every game is lockstep: state replication with a mailbox

The other common shape, found in Shining Soul II (2-4 players, co-op) only after a lockstep transport had been
built for it and played badly: each console **decides for its own character** and writes its state into its block
every frame; a console reads its own block from the send buffer at once and only the others' from the receive
slots; and everything that happens once (an enemy appears, an item is taken, a hit, a change of map) is a
**message passed inside the same blocks in stop-and-wait steps**, each repeated until every player's block
acknowledges it. Nothing has to arrive on a particular frame, and the cable never promised that either.

For such a game "advance only with a packet" does not exist, a shared timeline buys nothing, and latency hurts the
*messages* (one round trip per step), not the picture. How to recognise it, what the blocks hold and what follows
for the transport: [docs/09](09-multi-sio-anatomy.md) and [docs/10](10-direct-transport-and-messages.md). **Decide
which shape a game has before choosing anything else.**

## Data the parent relays

Two-player Multiplay protocols often have the parent echo what it received: its next packet carries both players'
data of the previous transfer, and *both* consoles act on that echo, not on the raw data (KHCoM's battle plays the
keys the parent relays, one frame later). On a cable this is free. Online it matters twice: under lockstep it adds
nothing but a frame of delay; under rollback the parent's packet changes whenever it replays a frame, and that late
change can reach the child after the child made the frame final (a permanent desync). Record per game which packet
words are relayed copies and by which rule, so the port can compute the relay on every machine instead
([docs/04](04-netplay-approaches.md#relayed-data-in-packets-derive-it-on-every-machine-do-not-revise-it)).

## What to record per game (template in [games/TEMPLATE.md](../games/TEMPLATE.md))

Packet size, queue depth, the send/receive functions, the status bits, the connect handshake, every retry
counter/timeout, **the link style** (lockstep on inputs, state replication, stop-and-wait messages, or a mix) with
its evidence, which bytes are state and which are events, the message kinds by role and by sender, every "every
player" condition (barriers, zones, ready bits), which words are relayed copies of received data, and where a round
ends (the state each side plays differently on purpose: win/lose screens).

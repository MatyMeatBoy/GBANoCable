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

## What to record per game (template in [games/TEMPLATE.md](../games/TEMPLATE.md))

Packet size, queue depth, the send/receive functions, the status bits, the connect handshake, every retry
counter/timeout, whether the play loop is lockstep, and whether packets carry inputs or state.

# The recomp playbook: every step without source

[docs/05](05-recomp-without-decomp.md) is the short version for a lockstep game. This chapter takes each mechanism
of chapters 09 to 13 and says how to do it in a **static recompilation** (translated ROM code that calls into a
runtime, with functions replaceable by address), where you have no C, no names and no linker wrap.

**Status of this chapter: a method derived from the port, not a record of a recomp that did it.** The mechanisms
were built with source (ShiningSource) and with function overrides in a decompilation build (ReKHCoM). No static
recompilation has run the direct transport, the message channel, reconnect or join in progress through these steps.
Each section says what you need to find and how to prove you found it; treat the proofs as mandatory.

## 1. What a recomp gives you, and what it does not

You have: the whole machine state in one place (RAM arrays, an I/O register array, usually a per-instruction or
per-function dispatch), **override by address** (run native code instead of a translated function), **memory
watchpoints** for free (every bus write goes through your runtime), and usually an interpreter fallback.

You lack: names, types, and the ability to insert code *inside* a function. Everything below is built from four
tools only: replace a whole function; wrap a function (run native code before or after the translated body); hook a
memory address (run code when it is written or read); and poke memory between frames.

## 2. Classify the link code by tracing (step A1 of the skill)

1. **Find the driver.** Search literal pools for the SIO register addresses (`0x04000128`, `0x0400012A`,
   `0x04000120`, `0x04000134`). The functions that load them are the driver. The SDK's MultiSio library also carries
   a version string in the ROM; a string search finds it and tells you the library and its vintage. The serial
   interrupt handler is often copied to RAM: look for a copy loop whose destination is later installed in the
   interrupt table.
2. **Find the per-frame entry.** Among the driver's functions, the one called once per frame from game code with
   two pointers and whose result is stored and tested bit by bit is the `MultiSioMain`-shaped function. Log its
   arguments: the first is the send block, the second the receive blocks; the size is the spacing of the receive
   blocks.
3. **Get a two-player trace without a network**: run two instances of the recomp in one process and cross their
   buffers at that function (what A sends is what B receives next frame), or record an emulator's local link. Log
   every block of both players, every frame.
4. **Read the trace.**
   - *Bytes that change smoothly every frame while a player moves and stand still when it stands*: continuous state.
   - *Bytes that hold a value for several frames, then step, in step with a byte of the other player's block that
     answers them*: stop-and-wait events. The answering bytes are the acknowledgements.
   - *A play loop that stops when the other block is withheld*: lockstep. Test it: withhold one side's blocks for 30
     frames and see whether the *local* character still moves. If it does, the game reads its own block locally:
     state replication.
   - Delay one side by `d` frames and time an event (pick up an item): if the time grows by a multiple of `2d`, the
     event is a stop-and-wait message and the multiple is its number of steps.
5. **Find the own-block accessor.** Put a read watchpoint on the send buffer: game code that reads it outside the
   driver is the "block of player i" accessor returning the send buffer for the own id. That is the proof of
   state replication from [docs/09](09-multi-sio-anatomy.md), section 2.

## 3. Choose the seam (step A2)

| Seam | How in a recomp | Use when | Cost |
|---|---|---|---|
| SIO registers and interrupt | emulate the transfer: on a write to the start bit, exchange the words over the network, raise the serial interrupt | nothing higher can be found | one round trip per word: unplayable beyond a LAN; the driver's own timeouts fire |
| the SDK's per-frame function | override it by address: copy the send block out, fill the receive blocks and return a status word | the game uses MultiSio or a library shaped like it | none: this is the seam of the port. Also override init and stop, and write the player id where game code reads it (the multiplayer id bits of SIOCNT in the I/O array) |
| the game's own queues | override the enqueue/dequeue/status functions ([docs/05](05-recomp-without-decomp.md)) | the driver is the developer's own and packets are queued | you must reproduce the status bits exactly |

With the per-frame function overridden, the interrupt-driven path never runs: do not raise serial or timer
interrupts for it, and make the override of "init" skip the copy of the handler to RAM only if nothing else reads
it. Keep the original functions reachable (call the translated body when not online) so the cable path, or an
emulator link, still works.

**Proof:** offline loopback. With your override returning the own block in slot 0 and status "one player,
connected", single-player behaviour must be unchanged (frame hashes equal to the unpatched build).

## 4. The direct transport (docs/10 §2)

Nothing here needs the game's code: it lives entirely inside the override of the per-frame function. Port
`reference/src/gnc_direct.c` or write your own:

- send the block (also at the end of the frame: hook the VBlank wait, which a recomp runtime owns anyway);
- fill each seat's receive block with the next block in order, or the last one again;
- build the status word: received and connected bits for every seat in the session, the parent bit for seat 0.

What you must find in the trace first: the exact status bits (run the two-instance loopback and log the word), how
many frames a counted-but-silent seat is tolerated (withhold a seat and count frames until the game leaves play),
and which bytes of the block are the lobby handshake (they must be delivered in order).

## 5. The message channel (docs/10 §3) without inserting code

In the port this is two source patches inside the sender and the receiver. A recomp cannot patch inside a function.
Three ways, best first:

**(a) Replace the sender task and the receiver task whole.** They are small state machines. Find them:

- the *queue*: a write watchpoint on the event bytes of the send block leads to the sender; the memory it reads the
  payload from is the queue entry; the function that fills entries is called from all over the game (the producers);
- the *receiver*: a read watchpoint on another seat's event bytes; it ends in an indexed jump (the dispatch table).
  Record the table's address and the convention at the jump: where the tag, the chunk count, the payload buffer and
  the sender id are held.

Then write native replacements: the sender pops queue entries and calls your `msg_send`; the receiver calls your
`msg_next` and **enters the game's dispatch** with the registers and buffer set as the original has them at that
point. Entering a translated function in the middle needs your recompiler to support an entry at that address
(emit the dispatch block as its own function, or interpret from there). If it cannot:

**(b) Play the protocol locally.** Leave both tasks untouched and make the override of the per-frame function a
*local peer*: for each other seat, it acknowledges the local sender's announce and chunks at once (so a message
leaves in a few frames with no network round trip), reassembles the message, and sends it whole on the channel;
for incoming messages it presents the announce and the chunks in that seat's receive block, one step a frame,
watching the local acknowledge bytes. The game runs its original code; the network carries whole messages.

- Cost: a message of *n* chunks takes about *n + 2* local frames on each side instead of one, and only one message
  per sender can be in flight to the local game at a time, so a burst queues inside your layer. It is still
  independent of latency, which is the point.
- **One order.** Per-seat receivers run in parallel in the game, so presenting seat A's and seat B's messages at
  once lets the game act on them in an order your layer does not control. To keep the one order of
  [docs/10](10-direct-transport-and-messages.md), present **one message at a time across all seats** and the next
  only after the local receiver has acknowledged the last chunk and one more frame has passed. The sender's own
  messages are the hard part: the original acts on its own message locally (its own receiver or the producer does
  it), not when it comes round. *This is an inference from the port's code, not something that was built:* in
  Shining Soul II a receiver task exists for the own seat too and watches the own send block, so the own message is
  dispatched when the local sender finishes its steps. Holding back your local peers' acknowledgement of the
  sender's last step until the message has come round in the order *may* delay that dispatch to the right place;
  whether it does depends on what the own-seat receiver waits for before it dispatches, which you must see in the
  trace. If the own message is acted on earlier than that (or at the producer), you cannot put own messages into
  the one order without (a), and what you have is (c) for own messages.

**(c) Accept per-sender order.** If neither works, deliver each sender's messages in its own order as they arrive.
That is what the port did first; it plays, with rare disagreements where two players' events touch the same thing
(an enemy struck by one and killed by another). Say so in your notes.

**Proof for any of them:** the `S`/`R` message logs of [docs/10](10-direct-transport-and-messages.md), section 6.
In a recomp the `R` line is logged at the dispatch jump (hook that address), which works for all three variants.

Handlers with fixed pools (the dropped third spawn): in the port the fix is a waiting list inside the handler. In a
recomp, wrap the handler's callee: if the pool is full (read its flags from memory), keep the request in your own
list and call the callee from a per-frame hook when a slot frees.

## 6. Departures and reconnect (docs/11)

Most of it is inside your layer and needs no game code: the standing character's block, the status bits, silence
clocks, stream numbers, revival and replay.

What the port patches in the game, and the recomp equivalents:

| Port patch | Why | Recomp |
|---|---|---|
| the barrier test counts a gone player as ready | or every change of map waits for ever | wrap the test function: if it returns "no", check the rings yourself in memory, skipping gone seats, and return "yes" when the rest hold the token. Find it as the function that scans a per-seat array of words for one constant and is called in a wait loop |
| the barrier test reports which absent seats it passed | the lock-out decision | the same wrapper |
| the world map counts a player that left for good as having chosen | or nobody leaves the world map | no function boundary to wrap: **write the standing character's block** so that it carries the choice (your layer builds that block anyway: copy the "chosen" bytes of the host's block into it). The same trick answers exit zones and "ready" bits: place the standing character's state where the check wants it. *Not built in the port; an inference from how those checks read the blocks* |
| the link task tells the layer that the session ended | a goodbye at once | override or wrap the driver's stop function (it has an address of its own in a recomp) |

What cannot be patched: the game's assumption of seats `0..n-1` and of one parent. Do what the port does: never
renumber, never replace seat 0, end the session when the host is gone.

## 7. Join in progress (docs/12)

Needs, besides the channel: (1) the anchor: hook the address of the "announce the layouts" producer call inside
the map set-up (a call site, so a wrap of the callee plus a check of the return address); (2) the function that
makes a player record for a seat, callable by address with the seat number: find it as the function called once per
counted seat at the session's start; (3) the "select map and area" function; (4) the addresses of the scene record,
the token rings and the message queue; (5) a way to skip the opening of the first set-up once: without source this
is a conditional inside a function, so either the recompiler lets you override that one branch, or you satisfy the
opening instead of skipping it (answer barrier A for the newcomer from your layer and swallow its duplicate
announcement). *None of this has been done in a recomp; it is the port's list of touch points translated.*

## 8. Validation without source

- **Offline equality first:** frame hashes of the patched and unpatched recomp with the link overrides inactive.
- **Two instances, one process**, crossed buffers, before any socket: proves the seam and the status word.
- **The same rig as the port** ([docs/07](07-testing.md)): headless instances, scripted input, the relay's knobs,
  the message and state logs, the outage report.
- **A recomp against a port of the same game** (if both exist) is the strongest test of the wire format: they share
  nothing but the protocol.
- State hashes for repair, where a recomp can snapshot the machine: only for state every console stores the same
  way ([ROADMAP](../ROADMAP.md), side-relative state).

## 9. What a decompilation still adds

You can do all of the above by tracing. What you cannot get by tracing is the *list of everything*: every
"everybody" condition, every handler with a fixed pool, every state shown only in passing. Those were each found in
the port by reading the code after a real session went wrong. Without source, expect to find them the same way,
one report at a time, and keep the session log ([docs/11](11-departures-and-reconnect.md), section 2) from day
one.

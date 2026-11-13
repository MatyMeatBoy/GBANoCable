# The direct transport and the reliable ordered message channel

For games that replicate state and pass events as stop-and-wait messages ([docs/09](09-multi-sio-anatomy.md)).
Built for Shining Soul II in ShiningSource (`runtime/core/netlink.c`); a small generic version with tests is in
[reference/src/gnc_direct.c](../reference/src/gnc_direct.c). Numbers are from scripted headless players on **one
machine**, through a local relay that simulates latency, loss and outages, unless a line says otherwise.

## 1. Why the lockstep transport was wrong for this game

The first transport gave every player the same blocks at the same label of one shared timeline, `delay` frames
late, and stalled a frame whose blocks were missing (the "stepped" transport; it is still there for a fixed delay).
It was consistent by construction and tested well:

- 4 players, 13,000 frames, 10% loss, 120-160 ms of lag: every label identical on every player;
- 4 players, 80,000 frames into the first field, 2% loss: the same;
- 4 players with 70-90 ms each way: full speed at delay 6; at delay 3 the session takes 50% longer, nothing breaks;
- automatic delay: 2 frames with no lag, 4 with 45-60 ms each way, 10 with 110-140 ms.

Then three and four people played it: items and gold appeared late or never, enemies arrived one at a time, players
saw each other in different places, the delay crept up, and one closed game ended the session for all. Scripted
players that press the same buttons had hidden all of it.

The reason is the message layer: each step of a message waits for every player's answer, and behind a buffered
delay of `d` frames each step costs about `2d + 2` frames. The picture was fine; the *events* were slow.

**Lesson.** Consistency tests prove the transport delivers what it promises. They do not prove the promise suits the
game.

## 2. The direct transport (state)

Per player, the blocks are handed to the game **in order, one a frame, the moment they are here**. Nobody waits for
anybody; there is no delay to choose.

- Every block has a label (a running number per sender). A datagram carries every block the receivers have not
  acknowledged, oldest first, **at most 40** (the reach), and for every seat the next label the sender expects
  from it (the acknowledgement). Loss, duplication and reordering are one case: the missing label is in the next
  datagram.
- When nothing new has come, the game is given the **last block again**. For state that is exactly right: an
  unchanged position.
- When several blocks wait (a burst), blocks that only repeat the *event bytes* of the block before them are left
  out, so the game catches up without missing a step of anything passed step by step (the lobby handshake).
  A block is never skipped just for being late.
- A player's own block is where the game wrote it, at once.
- The status word says "received" for every seat that has a block (new or repeated). **In a game in progress a
  seat is never reported silent while its player is in the room**: told that a counted seat is silent, the
  cartridge ends the session after its own timeout (80 frames here). A pause of the connection only freezes that
  character.
- The block a frame of the game writes is sent **at the end of that frame** (from the VBlank wait), not when the
  driver function is called in the next frame. *Measured*: a message 16 ms instead of 31 ms on one machine with no
  simulated latency.

What it gives up: the players are not on the same frame, and nothing can be compared label by label any more. The
tests change with it (section 6).

## 3. The message channel (events)

The game's sender no longer passes a message chunk by chunk: a patch hands the **whole message** to the transport,
and a patch in the receiver takes whole messages from it and jumps to the game's own dispatch. The handlers are
untouched.

Requirements, from [docs/09](09-multi-sio-anatomy.md) section 5: every message exactly once; **one order on every
machine**; a sender's own messages in that order too.

The first version (rules 4) delivered each sender's messages reliably and in the sender's order, each on its own
stream, straight to everybody. *Measured*, 60 ms of simulated latency: about 100 ms per message instead of about
650; the first enemy of a map after about 90 frames instead of about 630. It was still wrong: how one player's
messages interleaved with another's differed from machine to machine. The second version (rules 5) has one order:

- A player's message goes **to the host only**: its own numbered stream of entries, repeated in every datagram
  until the host acknowledges it.
- The host puts every message, its own and the others' as they reach it, into **one numbered stream**: the seat of
  the sender, then the message. That stream is the order. It goes to every player, repeated until each has
  acknowledged it.
- Every machine gives its game the entries of that stream in order, each once. **Nobody acts on a message before it
  has come round in the host's stream, the sender included.** So a player's own event costs it a trip to the host
  and back, and reaches the others in two trips instead of one. With a relay near the players that is a few frames.
- The host is the game's parent, which already owns enemies and items; ordering through it adds no authority.

In the game, whichever receiver task runs takes the next message *whoever sent it* and acts on it as that sender's
receiver would; one message at a time (some handlers wait for memory, and the next message must not overtake).
The task that started the sender for one message per frame hands **every** waiting message over in the frame it
was made, so a burst (a map entry) leaves at once.

The single order was checked by comparing every player's dispatch log (sender, kind, a hash of the contents) in
sessions of 2 to 4 players with latency and loss: the same order on every machine in every session. Its latency
was not measured again: by construction it is one trip to the host more than the figures above for everything a
player other than the host says. **Not verified:** combat with the single order where enemies die and respawn
(scripts do not reach it), and any session of people.

Side effects to look for in your game:

- **Handlers with small pools** see bursts they never saw on a cable (the third spawn in one frame was dropped).
  Queue the overflow in order.
- **Periodic corrections** tuned for a cable can be made more frequent now that a message is one short packet (the
  host's enemy position correction went from every 300 frames to every 20). Host only, so no machine computes
  differently.
- **Anything that asks "is my queue empty?"** would change behaviour, since the sender returns at once. This game
  never asks; its barriers wait for tokens instead. Check yours.

## 4. The packet, in words

One datagram a frame per player, sent to the relay, which passes it to every other seat. At most 1200 bytes.

1. **Header** (fixed size): a magic, the type (hello, welcome, data, goodbye, ping), the seat (stamped by the
   relay), the room, the player's token, **the stream number** (a new one each time this player starts or restarts a
   session, so that late packets of an old stream are recognised), the label of the first block carried, and per
   seat: the next label expected from it and which stream of that seat this refers to.
2. **Blocks**: up to 40 application blocks, consecutive labels.
3. **A small trailer** for things every player must learn at the same time (a vote, the host's "do it at label N").
4. **The message part**: for every other seat, the entry number at which my stream to it *starts* (it was not here
   for older ones) and the next entry of its stream I expect (the acknowledgement); then the first entry carried
   and the entries, oldest first, as many as fit.

A player far behind (it was away) may lack hundreds of entries. Since the relay passes every datagram to everybody,
serving it from the common datagram would make the others wait for their next message. So the common datagram
starts at what the players that keep up lack (within 64 entries of the newest), and a player further behind gets
up to two more datagrams of its own in the same frame. A player silent for half a second is not sent entries at
all until it speaks. *Measured*: a returning player 169 to 262 entries behind had them in 0.08 to 0.14 s (0.5 to
1.0 s with 60-80 ms each way and 5% loss).

## 5. The 40-frame reach, and the hole that froze players

A datagram reaches 40 blocks back. The receiver hands blocks over in order and asks again for a missing one by
acknowledging only up to it. Now let a stream be silent for more than 40 frames (two thirds of a second: a Wi-Fi
hiccup). When it speaks again its datagrams start 40 blocks before its newest block. The receiver still waits for
the first block it lacks, which is older than that, and **will never arrive**. It keeps the newer blocks in its
ring, hands none over, and repeats the last block before the gap for ever.

*Measured before the fix* (the relay cutting one player off for 3, 10 and 30 seconds; 2 and 3 players): **0 of 36**
sessions were whole afterwards. Any silence longer than 40 frames, far below the 15-second timeout, left that
player frozen on the others' screens and them frozen on its own for the rest of the session, **although the
messages went on** (which is what made it confusing: enemies died, items appeared, a companion stood still).

The fix is in the receiver and changes nothing in the packets: a datagram that is **full** (40 blocks) and still
**starts after** the block waited for proves a hole, because a sender only starts later when it is clamped by the
reach, and every later datagram will start later still. The stream steps over the hole. What the hole held is state
that is out of date; in a game in progress its event bytes say nothing because the events have their own channel.

After it (with the reconnect rules of [docs/11](11-departures-and-reconnect.md)): **60 of 60** whole again in the
same cases (five runs each); 18 of 18 with 60-80 ms each way and 5% loss; a player away 75 seconds 4 of 4;
everybody cut at once 4 of 4; the 64-bit build 3 of 3.

`reference/tests/test_direct.c` reproduces this in one process: with the fix a 300-frame outage heals; without it
the run reports `FROZEN` while `order` and `once` still hold; a 20-frame outage heals either way.

**General rule.** A window of unacknowledged data that is bounded on the sender needs a rule on the receiver for
data that fell out of the window. For state: step over. For events: never let them fall out (keep them until
acknowledged, and decide explicitly what happens when the store is full: section 3 of
[docs/11](11-departures-and-reconnect.md)).

## 6. What to test and how

- **Messages**: every player logs each message handed over (`S`) and each dispatched (`R`): clock, label, sender,
  kind, size, hash of the contents. A report asks: the same list on every player (*order*); of every sender exactly
  what it handed over, none twice, none missing (*once*); everything handed over up to two seconds before the end
  was dispatched (*complete*).
- **State**: four times a second, at the same moments of the clock on every process of the machine, each player
  logs the state bytes it was just given for each seat. The report holds A's view of B against B's view of itself:
  not frozen (B changed, A's view did not), and equal whenever B stood still.
- **Traffic on every stream**: scripted sessions often reach a place where only the host has anything to say. A
  test message with a tag the game ignores, sent by every player every N frames, makes every stream carry traffic.
- **A stranger's check**: `./test_direct` and `./test_direct --no-hole-fix` in `reference/`.

## Limits

- The packet layout and the constants (40, 64, 1200, half a second) are this port's. The reach of 40 was chosen
  for 24-byte blocks; it was not tuned by measurement.
- No congestion control: a datagram a frame per player, whatever the link.
- The host is in every message's path. While the host is away nobody's events get a place in the order (players
  can walk; nothing else happens).
- Everything above is one machine with a relay that simulates the network. Real sessions over the internet
  happened and are what found the problems, but none of the numbers come from them.

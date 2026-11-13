# Departures, outages and reconnect

A cable game knows one failure: the plug is pulled and the session is over. Online, players close the game, crash,
lose Wi-Fi for ten seconds, or stall while a window is dragged. This chapter is what each of those must mean **to
the game**, from ShiningSource (Shining Soul II, the direct transport and message channel of
[docs/10](10-direct-transport-and-messages.md)). The mechanisms are in `runtime/core/netlink.c` ("drop", "a
connection that was gone"). Measurements: scripted headless players on one machine, a local relay that cuts a
player off by not forwarding (`tools/net_outage_test.sh`, `net_outage_map.sh`, `net_outage_report.py`).

## 1. What "gone" means to the game

Decide it from the game's code, not from the network's point of view:

- **In the lobby** the truth is harmless: the seat's bits leave the status word and the lobby shows one player
  fewer.
- **In a game in progress the truth ends the session.** The game counts the connected bits and takes the players
  to be seats `0..n-1`, so a missing bit in the middle hides the last player; and a seat that is counted but not
  heard runs the link task into its error path after its timeout (80 frames). A message sender would also wait for
  ever for the missing player's acknowledgement.

So a player that leaves a game in progress becomes a **standing character**:

- its seat keeps its connected bit and is reported as received every frame;
- the game is given its **last block** every frame, with the event bytes set to what an idle sender and an idle
  receiver leave there (so no message step seems to be under way);
- its messages that reached the host are in the order and are acted on by everybody; what the host never received
  nobody acts on; the channel stops waiting for its acknowledgements;
- the game's "does everybody hold the token" check counts it as ready (one patch in the barrier test), or every
  later change of map would wait for it for ever;
- another program that later shows up on that seat is ignored (without join in progress,
  [docs/12](12-join-in-progress.md)): the game cannot let somebody in halfway.

The others see the character stand where it was and play on. *Checked*: a player killed mid-session with two and
three players, the others playing on. **Not checked**: items, gold and fights around a standing character with
people.

## 2. Who is given up, and when

| Evidence | Meaning | Action |
|---|---|---|
| a goodbye packet (the game left the mode, the window closed) | left for good | standing character at once; cannot return |
| a new stream number on the same seat | the program restarted | the old player left (standing character); the new program is ignored in a game in progress, unless join in progress is on |
| silence for the timeout (15 s) | may be a connection that comes back | standing character; **remember its stream** (section 3) |
| the game's link task ended and started again within 2 s | nothing: the game does that on some changes of place | no goodbye is sent; the session goes on |
| this program itself stood still for more than a second (window dragged, machine busy, suspended) | the others were not silent, *we* were not listening | every player's silence clock starts again |
| nothing at all from the relay for 2.5 s, not even answers to our own keep-alives | **our** connection is gone | give nobody up; say so on screen; keep asking twice a second; wait up to 2 minutes |

The last three rows were each a bug first: a goodbye sent when the game merely restarted its link task, and a
stalled program counting its own pause as everybody's silence, both turned companions into standing characters for
the rest of a real session (*found by reading the code after a player's report; neither was reproduced*).

A session log beside the program (one line per event: connected, left, timed out, restarted, stood still, back; no
names, bounded in size) is what makes "a companion vanished" diagnosable afterwards.

## 3. A connection that comes back

Nothing of the session is lost by an outage in itself: blocks that were not delivered are out-of-date state, and
messages are kept and repeated until acknowledged. What ended sessions was each side deciding **for good** that the
other had left. The rules, in the order they matter:

1. **Step over the hole** in the block stream ([docs/10](10-direct-transport-and-messages.md), section 5). Without
   it even a gap far below the timeout froze the player for ever.
2. **The cut-off player gives nobody up** (last row of the table above). When packets come again every clock starts
   anew.
3. **Revival.** The others give the silent player up after the timeout as before, and remember which stream it had
   and where the message channel stood with it (the next entry it expected, the next entry expected from it). When
   data packets of **that same stream** arrive again, the seat is taken in where it stood: its blocks are handed
   over again (until the first new one, the game keeps the standing character's block), and the message channel
   goes on from the remembered positions.
4. **Replay.** The host keeps every entry of the order that such a player has not acknowledged. On return it is
   sent all of them, in order, each once, as after a very long lag; its own messages of that time take their place
   in the order when they arrive. Nothing is skipped, so its game is the game the others have. The others are not
   kept waiting while it catches up (separate datagrams, [docs/10](10-direct-transport-and-messages.md) section 4).
5. **When replay is impossible.** The host's store is finite (4096 entries here). If more than that passed, acting
   on later messages without the earlier ones would leave the player with enemies and items the others do not
   have. The host then **keeps it out**: its packets stop naming that player's stream; the player notices after 5 s
   and is told what it is told when the host leaves (section 4). It also says goodbye to everybody, because the
   non-host players would otherwise have taken it in again on their own.
6. **A limit on waiting.** A player given up for silence is expected back only for as long as it waits itself (2
   minutes); after that it counts as left for good.

*Measured* (2 and 3 players; a player or the host cut off for 3, 10 and 30 s; five runs each): 60 of 60 sessions
whole again; before, 0 of 36. With 60-80 ms each way and 5% loss: 18 of 18. A player away 75 s: 4 of 4. Everybody
cut at once (the relay unreachable): 4 of 4. The 64-bit build: 3 of 3. Four players, a player cut 30 s and the host
cut 10 s: three runs each. Two outages in one session (the same player twice, two players, the host and a player):
eleven runs, all whole. On Linux x64 and ARM64 virtual machines: a player cut 30 s is taken in again, one run each.

"Whole" is what the report asks (every item must hold): every program ran to its last frame and stayed in the
session; the same messages in the same order on every player; of every sender exactly what it handed over; all
dispatched; each player's view of the others moving again and equal at rest; nobody counted as departed; everybody
on the same map; **no barrier token left over**.

Nothing in the packets changed for any of this, so old and new builds still meet; but taking a player in again is
the host's decision, so a host with the older build takes nobody in again.

## 4. The host

Seat 0 is the game's parent: only it decides enemies and items, and seat numbers cannot change during a game. Nobody
can take over.

- When the host **leaves** a game in progress the others are told what a cable tells when the parent console is
  pulled: everybody still counted, nobody received, the driver's error bit. The cartridge runs its own link error
  handling and resets; in the port a reset is the program starting again at the title.
- The host is **waited for longer** (60 s instead of 15): giving a player up costs the others nothing, giving the
  host up ends the session for everybody. The price, stated in the release notes: when the host's program has
  really ended, the others stand for a minute in a world where only walking works (no message gets a place in the
  order) before the error appears.
- If the host's packets return after it was given up, they are **not a new host to join**: the stream is remembered
  as dead, or a player would sit in the middle of a game its own game has given up.

**Not done:** host migration. It would need the host's authority state (enemy and item tables, identifiers in use)
to be transferable and the seats renumbered, and the game offers neither. Returning the others to the title is the
honest behaviour.

## 5. Barriers and the world map: the lock-out decision

Replay restores events. It does not restore what the game did at a barrier
([docs/09](09-multi-sio-anatomy.md), section 6) that was never in a message.

*Found by a test that follows the session out of the first field* (three players; the host hands over the game's own
"everybody changes map"; the script walks to the world map and chooses the first dungeon): a player given up
**while the others passed the barrier into the dungeon** was sent every message it had missed and still stood on
the world map for the rest of the session, with the others' tokens piling up in its rings. The start of the dungeon
is a state in the host's block, shown while the barrier stands and never again. Had it followed, its own tokens for
barriers long passed would have stayed in the others' rings and made every later barrier count it ready too early.

The decision taken: **lock out**. The barrier test tells the transport which absent seats it passed over; the host
marks a player that was given up when a barrier was passed as unable to return, and on return it is told the session
is over (as in rule 5 above). A player that comes back while the barrier still stands (the others wait there for
its token until it is given up) is taken in as before.

The alternative, not built: **resynchronise at the barrier**, i.e. bring the returning player to the others' place
the way a late joiner is brought in ([docs/12](12-join-in-progress.md): select the map with the game's own function
at a map set-up, hand it the host's ring contents, drop its stale tokens). The join-in-progress prototype has the
pieces; nobody has connected them to a return, and a return in the *middle* of a map has the same unsolved problem
as a join there (live enemies and items are not in any message absolutely).

Two more "everybody" conditions, both plain state:

- **The world map waits for every player's choice.** A player that left for good held the others there for ever;
  it now counts as having chosen. One that may still come back is waited for, as the game waits for anybody
  (*measured*: three players, the cases "cut before the change", "cut after choosing" 10 s four runs and 30 s three
  runs kept out cleanly, "never returns", "quits"; most cases one to three runs).
- **A level's exit and portals wait for every player's block** to be in the zone or to carry a "ready" bit. A
  standing character never is. *Measured in the join tests*: with three players, after one ended, the remaining two
  finished the area and then stood there to the end of the run; no change of map followed. **Not changed yet.** So
  today a departed player can hold the others on a level. This is the largest open item.

## 6. The stepped transport

On the timeline transport a returning player stood still on the timeline while the others went on, so it cannot be
taken in again: it is told the session is over after 5 s (before that fix its program stood still, waiting for a
block of the host that would never come, for as long as the host's session lasted).

## 7. Checklist for your game

1. What does the game do when a counted seat is not received? After how many frames? In the lobby and in play?
2. Does it assume seats `0..n-1`? What does seat 0 alone do?
3. List every "everybody" condition: message acknowledgements, barriers, zone/ready checks, votes.
4. For each: what should a standing character count as? What should a player that may return count as?
5. What state is shown only in passing (never in a message)? A player away at that moment cannot be replayed.
6. Decide the store size for replay and what happens beyond it. Say it to the player.
7. Test each row of the table in section 2 and each condition of step 3, and write down which you did not test.

## Limits

- **The outage is simulated**: the relay stops forwarding to and from one address, on one machine. A connection
  that returns from another address (Wi-Fi to mobile data), a phone that suspends the app, a laptop lid: not tried.
  The relay follows an address change by token ([docs/13](13-relay.md)); that path was not part of these runs.
- Fights, item drops and many spawns **during** an outage: not reached by the scripts (the scripted players reach
  the first dungeon but not its enemies).
- What the locked-out player sees on its screen was not examined (its program runs on with the game's own
  communication error).
- Run counts are small (see each figure). They show the mechanism works in those cases, not a failure rate.

# Joining a game in progress

Status of the source: a **prototype, off by default**, in ShiningSource's working tree (Shining Soul II), measured on
one machine only. This chapter is the mechanism and its limits; the per-function account is ShiningSource
`docs/JOIN_IN_PROGRESS.md`. Statements marked *traffic* were read off message logs of scripted sessions; the rest is
from the game's code.

## 1. What a late program lacks in a game that says everything once

A cable game makes its session once ([docs/09](09-multi-sio-anatomy.md), section 3). A program started while the
others play is missing five things:

1. **A lobby.** Its game opens the link and waits for a host to say "go". The others left the lobby long ago.
2. **A seat that exists.** In the others' games there is no player record, no task and no message receiver for it,
   and the count of players is one short. Seats are counted, so its seat must be the next free number.
3. **Everything said once.** The others' names, classes, weapons, statistics and flags; the session's settings; the
   map, the area and the layouts chosen for it. *Traffic*: the identity messages of a player task appear once per
   session; at a change of map only statistics, flags and the weapon are said again.
4. **The barrier's state**: which tokens are in which rings.
5. **The world**, if it enters in the middle of a map: live enemies, floor items, the map's flags. Nothing in the
   messages carries an enemy's health absolutely.

Item 5 is the one that cannot be assembled from the protocol. The design avoids it instead of solving it.

## 2. The anchor: a map set-up

**The newcomer is made a copy of the host's position in the order of messages at one well-chosen point, and from
there on is given what the host is given.**

The point is inside the set-up of a map, where the host announces the map's layouts, between two barriers. *From the
code and the traffic* a set-up runs: barrier A; the host announces the layouts and everybody stands in barrier B;
positions are set; barrier C; every player says its statistics and flags again; the host begins to make enemies. At
the announcement there are no enemies and no items yet (the game clears them at a change of map and the host makes
them again after the set-up), so item 5 does not arise, and items 3 and 4 are finite and small.

The game tells the transport when it is at that point (a two-line patch around the announcement; the same message
is also sent from another place, where it must not count).

## 3. The steps

All of it travels in the message channel of [docs/10](10-direct-transport-and-messages.md), as entries with tags
the game does not use, **in the host's one order**: every machine reaches them at the same point of the order. The
packet layout did not change except one bit in the header ("this candidate's game stands at its barrier").

- **A room of its own.** The setting is hashed into the room, so programs with and without it never meet. Every
  player of a session, the host first, must have it.
- **Candidate.** A stream that appears on a free seat (or on the seat of a player that left) after the lobby is
  over is a *candidate*: the others' games are given nothing of it, and it is given nothing of the order. The relay
  gives it the lowest free seat; the host refuses it if that seat does not follow the seats the game has.
- **Lobby, played by the transport.** The host first sends the candidate the facts of the session (which seats the
  game counts, which of them are standing characters and where they stand, the lobby's settings byte). The
  candidate's transport then shows its game a lobby: the host's block says "go", every other seat says "ready".
- **Waiting.** The candidate's game runs the start of a session up to its first barrier and stands there (the game
  draws its own waiting mark). Its barrier tokens of that stretch are for nobody and are dropped. Of its other
  messages only those that say who it is are kept, in its own stream.
- **Admission.** When the host's game hands over the layout announcement of a set-up, the host puts into the order,
  before it: an *admit* entry (the seat; map, area and two fields of the scene record that a set-up reads; which
  seats' tokens the host's rings hold at that moment), then *snapshot* entries (the last message of every identity
  kind for every seat, and the last session-settings messages, wrapped so that only the newcomer's game is given
  them), then an end marker.
- **The others grow by one seat** when they reach the admit entry: the game's own function that makes a player's
  record and task is called from the receiver loop, and from the next frame the driver counts the seat. This must
  not happen while a machine is still in barrier A (it would wait there for a token that belongs to B). A machine
  is past A exactly if its token for B is in the order before the admit entry, or if it has handed a token over
  that has not come round yet; otherwise it **holds the order** at the admit entry until it hands its next token
  over.
- **The newcomer goes in.** After the end marker its first barriers pass alone. Its scene task asks the transport
  where the game is, calls the game's own "select this map and area" function (which ends in barrier B), and its
  first set-up leaves out barrier A and the announcement once. Before anything else it is given again the tokens
  the host's rings held. Its rings are now the host's at that point, its game is at the host's place in the code,
  and it is given the host's messages from the announcement on. From here it is an ordinary player.
- **A seat whose player left** already has its record and task: admission only ends the standing character and the
  newcomer's blocks and messages take its place.

The game patches: one call in the receiver loop (grow), one block before the scene task's loop (place), two lines
in the set-up (skip the opening once; mark the anchor). The transport reads four things of the game's memory by
address (the scene record, the token rings, the queue of unsent messages): in a port these are the decompilation's
symbols, in a recomp they are addresses you must find ([docs/14](14-recomp-playbook.md)).

Shapes considered and rejected:

- *Free seats from the start, as idle characters.* Every "everybody" condition needs every counted seat, so an
  unplayed seat blocks exits, portals and the world map unless each check is patched; the game also scales by the
  count.
- *Letting in at once, mid-map.* Needs the live world copied (item 5) and a point where no barrier is under way.
  Not attempted.

## 4. Measured

One machine, a relay on the same machine, headless, scripted input, at eight times real time unless said
otherwise, with the code as it stands:

| Case | Let in at | Runs joined |
|---|---|---|
| 2 players, newcomer 30 s later | the change to the second area | 1 of 1 |
| 2 players, 50 s later | the change to the third area | 1 of 1 |
| 3 players, 30 s and 50 s (the fourth seat) | second, third area | 2 of 2 |
| 3 players, 10 s (before the session's first map) | the first set-up | 2 of 2 |
| 64-bit Windows build: 2 players 30 s, 3 players 10 s | | 2 of 2 |

With earlier states of the same mechanism (three small corrections came after): 32 more runs joined, among them 5%
loss with 10-30 ms of lag, and three did not: a newcomer that took the session's players for candidates
(corrected); a run in which a program stalled 0.2 s under load during the introduction at eight times the speed and
every game reset (before play the game gives a silent seat 110 frames; the same case alone joined 2 of 2); and the
seat of a player that left (below). The newcomer was let in 12 to 24 seconds of test time after its link opened
(its game needs about 2800 frames to its barrier, then waits for a set-up).

What a run must show: every program ran to its end; the newcomer was let in; its game is on the same map and area
as the host's at the same moments, through the changes of map that follow; it dispatched exactly the host's
messages from the point it was let in; what it sent was dispatched by everybody once and in order; the others' copy
of its state and its copy of theirs move and agree at rest.

## 5. Open, and not known

- **Only at a set-up that passes through that one function.** In the town nobody is let in (the town announces no
  layouts); a newcomer waits on a black screen with the game's waiting mark until the party enters a dungeon map.
  Whether every dungeon area uses that set-up was not checked map by map.
- **Not run:** a join at a map with exits, enemies that fight back, or a boss. The scripted session reaches three
  areas moved by the story and the town. "The mechanism does not depend on the map" is reasoning.
- **Flags that outlive a map change** (set by story steps during the session) are not sent. In the scripted session
  the newcomer's flags follow the others' through three changes of map; a session with more history may differ.
- **One newcomer at a time**; two at once were not run. One that vanishes between admission and its first token
  leaves the others in barrier B until the silence timeout drops it.
- **A departed player blocks changes of map** ([docs/11](11-departures-and-reconnect.md), section 5), and with them
  every later join. So the part that hands a departed player's seat to a newcomer **has never run**: in the test
  the newcomer was given that seat by the relay, was told about the session, and was never let in because no map
  was set up.
- **The lobby's settings may not reach the newcomer** (the game's lobby reads the host's settings byte only in a
  pass in which the host has not yet said "go"; the played lobby says it at once). A correction is written out and
  not run.
- **Memory:** making a seat in mid-game takes heap the game does not check for, as at a session's start.
- **Without the setting**, a program that enters the room after the lobby is taken for a player at once and the
  session stops making progress (*measured in one comparison run*: 12 messages dispatched in the whole run). If you
  do not build join in progress, make the relay or the host refuse late programs.
- Real network, Android, Linux, a window, the launcher: not run.

## 6. For another game

1. Find what is made once per session and what is said once (the five items).
2. Find a point where the world is empty or fully described and no barrier is half-way: usually inside a level's
   set-up. If there is none, join in progress needs a state copy, which is another project.
3. Admission is an entry in the one order, so every machine grows at the same point.
4. Copy the *last message of each identity kind*, not memory: the game's own handlers then build the newcomer's
   view.
5. Make the newcomer's barrier state equal to the host's at the anchor, and drop what its game sent before.
6. Keep candidates invisible to the game until admission, on both sides.

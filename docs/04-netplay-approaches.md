# Netplay approaches for link games

| Approach | What travels | Latency tolerance | Desync risk | Needs |
|---|---|---|---|---|
| Register-level link (KiGB, VBA-M net link, emulator TCP links) | every byte/word transfer | very low: one round trip per transfer | timing-dependent | nothing about the game |
| Emulator lockstep across machines | cycle-synced cores | low; stalls on every hiccup | cycle mismatches have been reported (mGBA forum) | emulator internals |
| Host runs all consoles, clients stream (Afterplay, host-based ideas) | inputs up, video down | client sees full round trip | none | bandwidth, a host per match |
| **Packet slots (GBANoCable)** | one game packet per frame per player | high: the game waits instead of failing | none if the game is deterministic per packet | finding the packet layer |
| Input lockstep with delay | inputs, frame-tagged | buffer = delay | none if deterministic | game exchanges inputs |
| **Rollback (GGPO / GGRS)** | inputs | high, without visible delay | handled by resimulation | inputs + fast save/restore |
| Host authority + state repair (ShiningSoul2Recomp) | inputs to host, state to guests | high | repaired by sending snapshots | snapshots, hashing |

## Vocabulary (the "modern online" people ask about)

- **Netcode**: the whole networking design of a game.
- **Lockstep / delay-based**: everyone waits for everyone's input of a frame before simulating it. Old fighting
  games, RTS games.
- **Rollback**: simulate ahead with predicted remote input; when the real input differs, restore a snapshot and
  re-simulate. Modern fighting games (GGPO), Street Fighter 6...
- **Client-side prediction + server/host reconciliation**: each player simulates their own character immediately,
  the host decides the truth and corrects. Action games, shooters.
- **Interpolation / dead reckoning**: showing other players smoothly between updates.
- **Relay / NAT traversal**: getting packets between two home routers (Steam's relays, STUN/ICE, a VPS relay).
- **Session recovery / rejoin**: a player who dropped comes back and receives the current state instead of the
  match ending.

Co-op action games like Monster Hunter Rise feel seamless because each player's own character never waits for the
network (prediction), the host owns the monsters, and a dropped player is removed or rejoins without stopping the
others. Link-cable games were never designed for that, which is why GBANoCable starts with the packet layer (no
cuts, no desync) and then adds what each game allows: rollback where packets carry inputs, host repair where they
carry state.

## Choosing for a game

1. Find the packet layer (docs/03, docs/05). Always do packet slots first: it gives correctness and "never cut".
2. If the play loop exchanges inputs and is deterministic: add rollback to remove the buffer's delay.
3. If it exchanges state or is not deterministic: hash the state every N frames and repair from the host.

## Rollback on top of packet slots (done in ReKHCoM, 23/10/2026)

Rollback is not only for games that exchange raw inputs. ReKHCoM runs it on the cartridge's own packets:

1. **Lockstep first.** Link frame *n* of every game plays the pair of slot *n − D* (same *D* everywhere). Without
   this, rollback has nothing deterministic to roll back to.
2. **Guess only where it is safe.** In the battle the packets are keys: guess the other side's slot as its last
   one. In the link menus (screens change, data is exchanged, things load) keep waiting for the real packet. Decide
   by the game mode, not by the cartridge's "link active" flag (it is on for the whole session).
3. **Snapshots at the one stable point.** Save the whole GBA memory, the game's globals and the emulated hardware
   (DMA, PPU, audio) at the main loop's wait for the vertical blank, in a ring (32 frames, ~740 KB each). Record the
   keys of every frame. On a wrong guess: restore, replay to now with the recorded keys, without drawing, sounding
   or pacing, inside the current real frame.
4. **Packets are not inputs: allow revisions.** Cartridge packets often carry what was received (KHCoM's parent
   relays both players' keys). A replayed frame can produce a different packet: send the new one again (its own
   datagram, for a while measured by the clock) and let the other side roll back what used the old one. It
   converges: each packet depends only on earlier frames. **Caveat (found later, see "One simulation on every
   machine" below):** it converges only if the revision arrives before the other side treats that frame as final.
   It may not. Where a packet carries a copy of received data, derive that copy locally instead.
5. **Time sync.** The side running ahead gives one frame of its pacing schedule back now and then (each packet
   carries how far ahead its sender runs). Without it one side guesses nearly every frame and the other never does.
6. **Check on final frames only.** Hash the shared state and compare only frames older than any possible rollback.
7. **Test what breaks it.** A replay-determinism mode (force rollbacks with no wrong guess, compare every replayed
   frame byte for byte with the first run) finds state the snapshots miss. A latency simulator must count time, not
   frames: a game that waits makes no frames, and a frame-based delay deadlocks the test.

Measured: two PCs, round trip ~160 ms with jitter and loss: 0 waited frames in 2400 battle frames, 20/20 state checks
equal; round trip ~230 ms with 5 % loss: still equal, waits only at the peaks.

## Quality-of-life features worth copying (ReKHCoM, 23/10/2026)

What players of other netplay ports (Slippi for Melee, Fightcade, Dolphin netplay, the SM64 / Ship of Harkinian co-op
mods) end up asking for, and how each fits on the packet-slot model:

- **A network line on screen** (round trip, input delay, frames of the last rollback). Measure the round trip all the
  time from the acks of your own slots (time the slot was made → the datagram that acknowledges it), not only in the
  handshake; smooth it (7/8 old + 1/8 new). Testers report "it felt laggy" much better with numbers.
- **Leaving when the other player is gone**, without breaking the game's logic: after some seconds of waiting, let
  the player give up, and from then on report the cable's own timeout every frame. The cartridge then runs its own
  "communication error" screen and goes back to its menus, exactly as when a real cable is pulled. Never invent a
  menu flow the game does not have.
- **Manual input delay** (auto by default from the round trip): higher delay = fewer rollback corrections on a bad
  connection. The host's value is the one both use.
- **Version check in the hello**: two builds with different link rules must not meet (they would desync later).
- **A lobby with the player's name** (Steam name, or a name setting for the room server).
- Later: private rooms by code entered in the game (needs an on-screen keyboard for pads), spectators (send the
  confirmed pairs to a third client), replays (the confirmed pairs are the whole replay).

## One simulation on every machine: lessons from a lasting desync (ReKHCoM, 01/11/2026)

The rollback above held 20/20 state checks in short tests, yet longer battles with attacks drifted apart for good.
Two separate causes, and a few traps met on the way. Each is a reusable rule.

### Relayed data in packets: derive it on every machine, do not revise it

- **Mechanism.** In many 2-player cable protocols the parent echoes received data back: KHCoM's parent puts both
  players' keys of the previous pair into words 2 and 3 of its next packet, and the battle on *both* consoles plays
  those relayed words, not the raw keys. Under rollback the parent replays a frame with the corrected keys of the
  child, so its packet for that frame changes and is sent again as a revision. The child may already have made that
  frame final (its own confirmation window passed, its hash sent) before the revision arrives. From then on the two
  games played different keys for that frame: a desync that never heals.
- **Symptom.** Rare and timing-dependent: 55 of 8091 pairs differed in a two-minute battle at 2-4 frames of lag,
  and the state hash disagreed from the first minute to the end. Short or calm tests (few rollbacks) pass.
- **How it was found.** Both games log every pair they play (`pair <frame> <words of player 0> | <words of player
  1>`). A frame played several times is logged several times; the last line per frame is the pair the game kept.
  Diffing the final pairs of both logs (the [pairdiff](../tools/pairdiff.py) tool) showed differences only in
  words 2 and 3 of the parent's slot: the relayed keys, never the raw ones.
- **Fix.** Online, every game computes the relay itself, with the same rule the parent's cartridge uses (here: the
  relay of frame *n* is the raw keys, word 1, of both players in pair *n − 1*), and stores the result in game memory
  so a rollback restores it like any other state. The parent sends constant zeros in those words. Its packets then
  depend only on its own input and never change on a replay: 0 revisions per battle (was hundreds), and the pairs of
  both logs identical.
- **General rule.** Under rollback, a packet should depend only on local input. Anything derived from received
  data must be derived locally, on every machine, from confirmed data. Revisions are a last resort for packets the
  game builds from what it received (menus, handshakes), and those parts should run in lockstep, not on guesses.
- **Limits.** Needs the decompiled (or traced) relay rule and the knowledge of which words the play loop reads. It
  changes what goes over the wire: bump the link protocol version so old and new builds never meet. Local cable play
  keeps the original behaviour.

### What the port adds enters as inputs the original game already understands

- **Mechanism.** A port-added "dodge" button was carried as an extra key bit plus a pending flag in game memory,
  consumed by the battle code. With attacks it desynced within a minute: the extra state was not part of what both
  sides replayed identically.
- **Fix.** Generate the button as the original input sequence (here the d-pad double tap: release, tap, release,
  tap) in the input layer, before the keys enter the link, and do not generate anything while the rollback replays a
  frame (the recorded keys of that frame already contain it). The other game, the rollback and the replay then see
  only ordinary keys.
- **Rule.** Online, a port feature must reach the simulation through the inputs the cartridge already exchanges. Any
  new game-side state is a new source of desync unless both sides produce it from the same exchanged data.

### Keep tooling state out of the rolled-back memory

Trace counters ("last slot received") that lived in the game's own memory were restored by every rollback, and the
replay-determinism test reported them as the first difference, hiding real ones. Session and tooling state belongs
next to the transport's ring, outside the snapshot. When the determinism test finds a difference in RAM, report the
heap block (allocation) that holds the byte: it names the culprit at once.

### Check only what is still shared

After the relay fix, the few remaining hash mismatches all fell on the frame a fighter reached 0 HP: positions and
HP equal, only the random state different. Each game then plays its own side of the result (a win screen on one, a
loss screen on the other), which draws random numbers differently; the next battle starts again from the shared
seed and every later check matched. The hash function returns "no check" once the round is decided (the reference
library's `gnc_check_record(..., 0)`). Only valid if the game really re-synchronises (re-seeds) before the shared
state matters again: confirm that the later checks match.

### Results

Two hidden games through a local relay with injected lag, jitter and loss, both players attacking and dodging:

| Builds | Network | Checks | Result |
|---|---|---|---|
| before the fix | 2-4 frames of lag | - | permanent desync from the first minute; 55/8091 pairs differ |
| 64-bit vs 64-bit | 3+3 frames of lag, 2 % loss | 60 | 1 isolated mismatch at a KO frame (then skipped by the rule above) |
| 32-bit vs 64-bit | same | 50 | 0 desync |

Two different builds of the same game (32-bit and 64-bit, different compilers and speeds) are a cheap stand-in for
"two different devices": test that pair, not only two copies of one build.

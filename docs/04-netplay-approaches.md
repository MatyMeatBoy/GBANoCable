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
   converges: each packet depends only on earlier frames.
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

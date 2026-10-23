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

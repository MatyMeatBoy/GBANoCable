# <Game> (<region>, <serial code>)

Sources: (decompilation, recompilation, traces: link to them). Say for each statement whether it was read from
code, traced, or measured. Addresses, names and behaviour only: no game code, data tables or text.

Status: documented / hooked / online tested with scripts / played by people

## The cartridge's link

- Link modes: (VS battle, trade, co-op...)
- Players: 2 / 4
- Serial mode: Multiplay / Normal / Wireless adapter
- Driver: the SDK's library and version (MultiSio, RFU...) or the developer's own; functions (address / name):
  init, per-frame entry, IRQ handler, timer handler, stop
- Packet layer: packet size, checksum, queue depth, zero-packet rule
  - enqueue function:
  - dequeue function:
  - status function and bits:
- **Link style**, per mode: lockstep on inputs / state replication / stop-and-wait messages (they can be mixed).
  Evidence: who writes the send buffer, who reads the receive buffers, does a console read its own block locally,
  what the play loop waits for
- Block layout: which bytes are continuous state, which are events, which are acknowledgements
- Lobby / connect handshake (commands, ids, what must be seen in order):
- Seats: counted or named? What the parent (seat 0) alone does:
- Message layer, if any: steps and what each waits for, queue depth, handlers with fixed pools
- **Message kinds by role** (identity, settings, world set-up, spawn, correction, hits, items, map change, barrier)
  and by sender (host only / any player). Roles, not a copy of the game's tables
- **Barrier**: how "everybody is here" is decided (tokens, counters, bits), and every other "every player"
  condition (zones, ready bits, votes)
- Retry counters and timeouts (value, what happens when exceeded), before and during play:
- Flow control:
- Random seed exchange:
- Which words are relayed copies of received data:
- Where a round ends (state each side plays differently on purpose):

## Online

- **Seam used**: SIO registers / the SDK's per-frame function / the game's queues; how it is hooked (linker wrap,
  source patch, override by address)
- **Transport**: slots with delay / rollback / direct state + ordered messages; link protocol ("rules") version
- Patches to game code, each with its reason:
- **Departure behaviour**: what a player that left is to the game; what happens when the host leaves; timeouts
- Outage and reconnect: what heals, what locks a player out
- Join in progress: no / how
- Relay, rooms, lobby:

## Port

- **Porting status**: decompilation (matching? named?), native builds (32-bit development build, 64-bit hosts;
  which processors and systems were really run), picture, sound, saves, what the reference run covers and what it
  was compared against
- What of the port is specific to this game's engine:

## Test status

For each line: runs, players, machine(s), network (simulated how / real), version or date.

- Checked:
- Not checked:
- Known problems:

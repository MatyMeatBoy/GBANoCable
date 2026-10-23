# <Game> (<region>, <serial code>)

- Link modes: (VS battle, trade, co-op...)
- Players: 2 / 4
- Serial mode: Multiplay / Normal / Wireless adapter
- Driver functions (address / name): init, IRQ handler, timer handler, start transfer
- Packet layer: packet size, checksum, queue depth, zero-packet rule
  - enqueue function:
  - dequeue function:
  - status function and bits:
- Protocol: connect handshake (commands, ids), data exchange, play loop
- Retry counters and timeouts (value, what happens when exceeded):
- Flow control:
- Play loop: lockstep? (advances only with a packet) — packets carry inputs or state?
- Random seed exchange:
- Notes / traces:
- Status: documented / hooked / online tested

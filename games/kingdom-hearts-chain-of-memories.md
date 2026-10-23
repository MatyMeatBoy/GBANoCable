# Kingdom Hearts: Chain of Memories (EU B8CP, US B8CE, JP B8CJ)

Status: **online tested** in ReKHCoM (Steam, direct UDP, relay). Source: the ReKHCoM decompilation
(`src/sio/sio.c`, `chara.c`, `mode_sio.c`, `btl_vs.c`, `mode_vsbattle.c`).

- Link modes: VS battle (2 players), card trade.
- Serial mode: Multiplay, 115200 bps, parent paces transfers with Timer 3 after VBlank.
- Packet layer (`sio.c`): a packet is 4 words + 1 checksum word (5 transfers); send and receive queues of 32;
  all-zero packets are not queued; the receive side gets both players' words of the same slot (`recvBuf[player][word]`).
  - enqueue `SioQueueSendFrame`, dequeue `SioReadRecvFrame` (sets "receive empty"), status built in
    `SioRunStateMachine` / `SioTransferFrames` (player id, count, parent, connected, empty, handshake, errors).
- Handshake: the parent sends 0x8FFF (start) / 0xD5E0 (idle) words; two players seen twice → connected.
- Protocol (`chara.c`): connect request 0xFEFE with a region id (EU 0xC2F0, US 0xC1F0, JP 0xC0F0: regions do not
  link), accept 0xECEC, exchange request 0x1BFE / accept 0xC5A0 (decks, names, random seed), then key sync.
- Retry: the accepting side gives up after 10 frames without the other's accept (raised to 120 online).
- Timeouts: child idle 6 VBlanks, errors for 180 frames → link error screen (never raised online).
- Flow control: a pause bit when more than 3 packets wait (off online).
- Play loop: **lockstep on inputs**: `SioKeySyncSend/Recv`, command 0x0ACD; each side sends its held keys, the
  parent relays both players' keys, both simulate with them; `AgbMain` only runs a frame when the frame's packet
  arrived. Same seed (exchanged) → no desync possible. Rollback is feasible here.
- Online results: see the top README.

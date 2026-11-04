# Kingdom Hearts: Chain of Memories (EU B8CP, US B8CE, JP B8CJ)

Status: **online beta** in ReKHCoM
([v2.5-online-beta](https://github.com/MatyMeatBoy/ReKHCoM/releases/tag/v2.5-online-beta), 01/11/2026, a Windows
and Linux/Steam Deck prerelease): Steam, direct UDP, a relay and a public room list. Source: the ReKHCoM decompilation
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
- Play loop: **lockstep on inputs**: `SioKeySyncSend/Recv`, command 0x0ACD; each side sends its held keys (word 1),
  the parent relays both players' keys of the previous pair (words 2 and 3 of its packet), and both consoles play
  the *relayed* keys; `AgbMain` only runs a frame when the frame's packet arrived. Same seed (exchanged) → no desync
  under lockstep.
- Round end: when a fighter reaches 0 HP each console plays its own side of the result (win or loss), which uses
  the random generator differently; the next battle starts again from the shared seed.

## Online in ReKHCoM (v2.5-online-beta)

- **Slots and delay**: one packet per link frame, link frame *n* plays the pair of slot *n − D*, same *D* on both
  (picked by the host from the measured round trip). Link menus (screen changes, deck/name/seed exchange) run in
  lockstep; the battle runs rollback (guess the other side's last keys, 32-frame snapshot ring, replay on a wrong
  guess), with time sync and a 12-frame window before waiting.
- **Keys relayed locally** (link version 5): online, each game computes the relay of words 2-3 itself from word 1 of
  the previous pair (exactly the parent's rule) and the parent sends zeros there, so a rollback never revises a
  packet. Before this, revisions of the parent's relay could arrive after the child had confirmed the frame: a
  permanent desync (55/8091 pairs). See docs/04.
- **Dodge button** in link battles: generated as the d-pad double tap the game already understands, in the input
  layer, never during a rollback replay (an earlier extra key bit with its own pending state desynced).
- **Desync check**: a hash of both fighters' position and HP and the random generators, in player order, every 120
  link frames, on frames final for 48 frames. Skipped once a fighter is down (the win/lose screens differ on purpose).
- **Lobby**: Link opens the game's own Load screen, then a room browser (name, host level, region, players; Create
  room, region filter, Refresh, Cable for the local link). Rooms have a typed name (24 bytes UTF-8) or
  "<player>'s room"; the host's level rides in the name (`name~level`). Shown once per Link visit. Backends: Steam
  (Spacewar 480 lobbies) and a room server (the relay's A/L/R datagrams) on a VPS beta preset in the launcher.
- **Never cut**: a missing packet waits (picture frozen, "waiting for the other player"); after 10 s the player may
  give up, and from then on the cable's own timeout is reported so the cartridge runs its own link error screen.
- **Platforms**: Windows 32 and 64-bit (the 64-bit host got a real UDP socket in this release), Linux/Steam Deck;
  typed text also on 64-bit Linux and Android 64-bit hosts.
- **Measured** (two hidden games, local relay, injected lag/jitter/loss, both players attacking and dodging): 64-bit
  vs 64-bit at 3+3 frames of lag and 2 % loss, 60 checks with one mismatch at a KO frame (now skipped); 32-bit vs
  64-bit, 50 checks, 0 desync. Earlier rollback figures: README and docs/04.
- **Known limits**: regions do not link (cartridge rule); only rooms of link version 5 meet; Android cross-play and
  real games between distant places are not verified yet; repairing a desync by snapshot is not done (each console
  stores the battle from its own side, see ROADMAP); a player whose address changed waits up to 30 s for the relay
  seat; the public relay has no accounts or encryption (room codes are not passwords).

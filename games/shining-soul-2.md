# Shining Soul II (EU)

Sources: the matching decompilation [SS2Decomp](https://github.com/MatyMeatBoy/SS2Decomp) (`docs/LINK_CABLE.md`,
`LINK_PLAYER_PACKET.md`, `LINK_MESSAGES.md`) and the native port
[ShiningSource](https://github.com/MatyMeatBoy/ShiningSource) (`runtime/core/netlink.c`, `relay/`,
`games/ss2/port/patches.json`, `docs/`), state of v0.7.5 plus the join-in-progress prototype. "Code" = read from
the decompilation; "traffic" = read off message logs of scripted sessions; "measured" = scripted headless players
on one machine through a local relay with a simulated network. Names such as `sub_08001804` are the
decompilation's.

Status: **played by people** (which is what found most problems), **measured only with scripts**.

## The cartridge's link

- Link modes: co-op through the whole game (town, world map, dungeons, bosses), 2-4 players.
- Serial mode: Multiplay, through the SDK's MultiSio library (the version string is in the ROM). *Code.*
- Driver: `MultiSioInit` (`sub_0800170C`), the per-frame `MultiSioMain(send, receive)` (`sub_08001804`), start/stop
  (`sub_08001A50` / `sub_08001A64`); the serial/timer interrupt handler is ARM code the cartridge copies to RAM.
  *Code.*
- Packet layer: a 32-byte driver packet = a 4-byte header (frame counter, error flags, checksum), **24 application
  bytes**, 4 guard bytes; the checksum covers the first 28 bytes. The game hands over one 24-byte block a frame and
  gets four back, its own included, plus a status word (received bits, parent bit, connected bits, hard error).
  *Code.*
- **Link style: state replication plus stop-and-wait messages. Not lockstep.** Evidence (*code*): the accessor for
  "the block of player i" returns the **send buffer** for the own id and a receive slot for the others; the player
  task writes position and state into the block every frame; other code decodes positions straight from the receive
  slots; in single player a loopback task copies the send block to slot 0.
- Block layout (*code*): bytes 0..7 continuous player state (packed position, facing, animation state, mode bits);
  byte 8 command or sender id, 9 a code, 10 a count or chunk number; 12..19 payload of the current step; 20..23
  acknowledge bytes, one per seat.
- Lobby (*code*): no message layer yet. A client writes `OK` into bytes 8-9 and waits for the host's `GO`; the host
  says it when every other seat says `OK` and its player confirms; byte 10 of the host's block carries two session
  settings. Must be seen in order.
- Seats: **counted** (the connected bits are counted and loops run `0..count-1`). Per-seat records, tasks and
  message receivers are made once at the session's start. Seat 0 (the parent) alone creates enemies, owns item
  records and answers pick-ups, announces map layouts and corrects enemy positions. *Code.*
- Message layer (*code*): a queue of 50 entries; a message is a tag, up to 16 chunks of 8 bytes. One sender task:
  announce until every seat acknowledges, then each chunk until every seat acknowledges, a final wait, pop. One
  receiver task per seat (the own seat too) that collects and dispatches on the tag. The announce wait has no
  timeout. Handler with a fixed pool: enemy spawn requests, two at a time, a third is dropped.
- Message kinds by role (about fifty tags; the catalogue is in SS2Decomp):

  | Role | Sender |
  |---|---|
  | identity: name, class/appearance, weapon, flags, two statistics blocks | every player, at its task's start; statistics, flags and weapon again after each map set-up (*traffic*) |
  | session settings | host, at the scene's start |
  | map layouts | host, inside a map's set-up (and once from the world map) |
  | enemy spawn, spawn withdrawal, position correction (every 300 frames on the cartridge), enemy records | host |
  | hits, status effects, deaths, area attacks, consumables used | whoever caused them |
  | items: drop request → the host's record; pick-up request → the host's answer → removal | requester and host |
  | map change: exit taken, "everybody changes map", story steps that name what every console runs next | host or the machine at the trigger |
  | barrier token | every player |

  One story message carries the address of a function (*code and traffic*).
- **Barrier** (*code*): a three-call pattern: send a token message; "does every counted seat's ring hold the
  token"; take one from every ring. Tokens are not numbered: one too many passes the next barrier early, one too
  few holds it for ever. Used at map set-ups (three in a row around the layout announcement), at the start of a
  dungeon from the world map, in boss phases. Other "every player" conditions are state: a map exit (every block
  in the exit zone for three checks), portals (a "ready" bit in every block), the world map (every cursor on the
  same place).
- Timeouts (*code, seen in tests*): a connected seat not received for more than 80 checks in play (110 before play)
  or repeated hard errors → the link error path → reset.
- Random seed exchange: none needed for this design (each console decides for its own character; the host decides
  enemies).

## Online (ShiningSource)

- **Seam**: the SDK's per-frame function. `MultiSioInit` and `MultiSioMain` are replaced with the linker's `--wrap`
  (the originals stay for cable/offline); the player id is written where the game reads it (the id bits of SIOCNT).
  One source patch tells the layer when the link task stops (a call in the same file as its callee, where a wrap
  does not reach).
- **Transport**: *direct* (default): each player's blocks handed over in order as they arrive, the last one
  repeated, nobody waits; one-time messages whole on a reliable channel in **one order through the host**
  ([docs/10](../docs/10-direct-transport-and-messages.md)). *Stepped* (when a delay is set by hand): one timeline,
  fixed or automatic delay, a missing block stalls. Rules version 5; the two transports never meet.
- Patches to game code (all inactive over a cable):
  - sender task: hand the whole message to the channel instead of the chunk steps;
  - the queue monitor: hand every waiting message over in the frame it was made;
  - receiver tasks: take the next message of the order, whoever sent it, one at a time, and jump to the original
    dispatch;
  - spawn handler: requests that find both request tasks busy wait in order instead of being dropped;
  - enemy position correction every 20 frames instead of 300 (host only);
  - barrier test: a departed player counts as ready, and the layer is told which absent seats were passed over;
  - world map: a player that left for good counts as having chosen;
  - link task: tell the layer the session ended;
  - skip-scene vote: the introduction's own exit is allowed in a multiplayer game while a voted skip is under way;
  - (prototype) join in progress: make a seat's player in the receiver loop; select the map for a newcomer; skip
    the opening of its first set-up; mark the anchor.
- **Departure behaviour** ([docs/11](../docs/11-departures-and-reconnect.md)): in the lobby the seat's bits go. In
  play a departed player stays as a standing character (last block, idle event bytes, still counted). When the host
  leaves, the others get what a pulled parent console gives (hard error) and the game restarts at the title.
  Timeouts: 15 s of silence for a player, 60 s for the host, 2 minutes for one's own connection.
- Outage and reconnect: a gap heals by itself (the hole is stepped over); a player given up is taken in again when
  its stream returns and is sent every message it missed, in order; it is **locked out** if the host's store of
  4096 entries was exceeded or if the game passed a barrier without it.
- Join in progress: prototype, off by default ([docs/12](../docs/12-join-in-progress.md)).
- Relay: one small C program, rooms of four seats with tokens, the first seat is the host; the room is the name
  hashed with the ROM, the rules number and the transport ([docs/13](../docs/13-relay.md)). The launcher can start
  it on the player's PC; connect by address is possible. No public room list.

## Port

- **Porting status**: matching decompilation of all game code (the sound library's hand-written assembly aside),
  data described. Native port
  compiled from it unchanged plus about forty-five patches with reasons
  ([docs/15](../docs/15-native-port-mechanisms.md)): 32-bit development build; 64-bit hosts with a 32-bit-pointer
  image for Windows x64 (released), Linux x64 (released, run headless only), Android (released; run in the x86_64
  emulator only); ARM64 image (run under QEMU on Linux; never on a phone); WebAssembly (offline, built from
  source). Software line renderer with a wide picture; native sound engine written after the decompiled driver;
  flash save in a file with atomic replace and a backup.
- The reference run: 32,000 scripted frames (new game, the first field), one hash and one audio line identical on
  every build above. It does not leave the first areas; **it was never compared against an emulator**.
- Specific to this engine: the cooperative task scheduler (native coroutines), the field's full tile maps (wide
  picture), the message patches above.

## Test status

All measured items: one development PC, headless scripted players, a relay on the same machine; 2026-11-12/13,
versions 0.1 to 0.7.5.

- Checked:
  - stepped transport: 4 players 13,000 frames with 10% loss and 120-160 ms lag; 4 players 80,000 frames with 2%
    loss; full speed at delay 6 with 70-90 ms each way; sessions between Windows x64, Android (emulator) and Linux
    ARM64 (QEMU) through a relay, compared line by line;
  - direct transport and messages: 2 to 4 players with latency and loss, the same message order on every machine;
    about 100 ms per message instead of about 650 at 60 ms of simulated latency (per-sender version);
  - outages (the relay cutting a player off): 60 of 60 sessions whole again (2-3 players, player or host, 3/10/30
    s, five runs each; 0 of 36 before); 18 of 18 with 60-80 ms and 5% loss; 4 players and two outages in one
    session, eleven runs; a change of map with a player cut off before it and after choosing (one to four runs per
    case);
  - join in progress (prototype): 8 of 8 runs with the current code in five cases, 2 and 3 players, both Windows
    builds;
  - a voted skip of the introduction with three players.
- Not checked:
  - **any measurement over a real network**; an address that changes; a phone;
  - combat online with the one order (deaths, respawns), fights and item drops during an outage;
  - a level exit or a portal with a departed player: by code and by one measured run **the standing character
    holds the others on the level**;
  - join in progress in the town, at a map with exits or a boss, two newcomers at once, a departed player's seat;
  - Linux with a window; Android online on a device.
- Known problems: when the host leaves the session ends for everybody; a departed player blocks map exits (above);
  the story message that carries a function address has not been examined between builds of different processors.

# GBANoCable

**Online play for Game Boy, Game Boy Color and Game Boy Advance link-cable games, without the cable's 0 ms problem.**

By **MatyMeatBoy**. Research, documentation and a reference implementation for people porting, decompiling or
*recompiling* handheld games who want their link modes to work over the internet: no desyncs, no "communication
error" every time the connection hiccups.

> Status: research + reference implementation (alpha). Born from the online link battle of
> [ReKHCoM](https://github.com/MatyMeatBoy/ReKHCoM) (Kingdom Hearts: Chain of Memories, GBA), where it already works
> over Steam, direct IP and a relay. Help is welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## The problem

A link cable answers in microseconds. Games were written for that: they exchange a word, expect the reply almost at
once, and treat a short silence as "the cable was pulled". Emulators that send each transfer over the network
(one round trip per byte or word) either crawl or throw the game's own link errors. That is why most handheld link
modes never had a good online version.

## The idea

Do not send the cable over the network. Find the layer **above** the registers where the game queues whole packets
(almost every game has one) and replace only the transport underneath:

1. **Slots**: every game frame is a slot. Each console fills its slot with one packet of its own queue (or an
   empty one) and sends it with the slot number.
2. **Pairs (or groups), on the same frame**: link frame *n* of every game (its *n*-th call to the cable since the
   connection) receives exactly the pair of slot *n − D*, with the same delay *D* on every machine. Handing pairs
   over "as they arrive" is not enough: the data is identical but lands on different game frames, and any state
   machine that runs while waiting (handshakes, data exchanges, timers, the random generator) drifts apart.
3. **Never cut**: none of the cable's errors (timeout, queue full, checksum) are raised over the network. A missing
   packet makes the game *wait*; it goes on when the packet arrives. A real outage shows "waiting for the other
   player" and resumes.
4. **Smooth**: *D* is picked once, by the host, from the round trip measured during the handshake (one way plus a
   jitter margin, in frames) and sent to the other side; a frame whose pair is late waits for it (picture frozen,
   window alive). An adaptive buffer that each side tunes on its own breaks determinism. Games that exchange
   *inputs* can go further with rollback.

Lesson from ReKHCoM (PC x86 vs Android ARM64): two equal PCs never showed the bug; a slow device did. The child
never received the parent's battle seed because the exchange ran on frames where the pair had not arrived yet.
Test with two machines of very different speed, or force it (one side under an emulator), before trusting a hash.

Measured in ReKHCoM: 4 frames of delay + up to 6 frames of jitter + 5% packet loss: 3-6 waited frames in 3600;
a 10 s total outage in the middle of a battle: both games wait and continue in sync; a hash of the shared battle
state compared every 120 frames: 0 desyncs in 20+ checks with both players fighting (and it fires when a
difference is forced).

## Contents

| Path | What |
|---|---|
| [docs/01-gb-gbc-serial.md](docs/01-gb-gbc-serial.md) | Game Boy / Color serial port: how a byte transfer works and why it is hard online |
| [docs/02-gba-sio.md](docs/02-gba-sio.md) | GBA serial: Normal, Multiplay, UART, JOY BUS, GPIO; who is the parent |
| [docs/03-software-patterns.md](docs/03-software-patterns.md) | How games use the cable: packet queues, handshakes, checksums, flow control |
| [docs/04-netplay-approaches.md](docs/04-netplay-approaches.md) | Register-level, packet slots, input lockstep, rollback, host authority, streaming |
| [docs/05-recomp-without-decomp.md](docs/05-recomp-without-decomp.md) | Finding and hooking a game's link layer in a static recompilation, without a decompilation |
| [docs/06-transports.md](docs/06-transports.md) | UDP, Steam (Spacewar 480 for development), a relay on a VPS for crossplay |
| [docs/07-testing.md](docs/07-testing.md) | Testing with two local instances and simulated delay, jitter, loss and outages |
| [games/](games/) | Per-game protocol notes (Kingdom Hearts CoM documented; others: help wanted) |
| [reference/](reference/) | `gbanocable` C library: the slot engine for 2-4 players, transport-agnostic, with a test |
| [tools/relay.py](tools/relay.py) | Room-code relay for a VPS (standard library Python) |
| [docs/references.md](docs/references.md) | Everything this builds on |

## Español

**GBANoCable** reúne todo lo necesario para llevar los modos de cable link de GB, GBC y GBA a internet sin las
desconexiones del cable: documentación del hardware y de cómo lo usan los juegos, los enfoques de netplay que
existen, una guía para recomps sin decomp y una implementación de referencia en C (ranuras emparejadas, nunca
cortar, búfer adaptativo) sacada del online de ReKHCoM, donde ya funciona por Steam, IP y relay. Autor:
MatyMeatBoy. Se buscan colaboradores: documentar protocolos de más juegos, probar en otros recomps, transporte en
más plataformas.

## License

MIT (code and documentation written here). No game code or data is included. Referenced specifications belong to
their authors (see [docs/references.md](docs/references.md)).

# GBANoCable

**Online play for Game Boy, Game Boy Color and Game Boy Advance link-cable games, without the cable's 0 ms problem,
and the road from a decompilation to a native port that can carry it.**

By **MatyMeatBoy**. Research, documentation, a reference implementation and a **skill** ([SKILL.md](SKILL.md)) for
people and AI assistants porting, decompiling or *recompiling* handheld games who want their link modes to work
over the internet: no desyncs, no "communication error" every time the connection hiccups.

> Status: research + reference implementation (alpha). Two ports stand behind it:
> [ReKHCoM](https://github.com/MatyMeatBoy/ReKHCoM) (Kingdom Hearts: Chain of Memories: a 2-player battle, lockstep
> with rollback, Steam, direct IP and a relay with a room list) and
> [ShiningSource](https://github.com/MatyMeatBoy/ShiningSource) (Shining Soul II: 2-4 players co-op, state
> replication with a reliable ordered message channel, reconnect after an outage). Help is welcome: see
> [CONTRIBUTING.md](CONTRIBUTING.md).

## Use it as a skill

[SKILL.md](SKILL.md) is the entry point: the method in order and a table that says which chapter changes which
decision. For an AI assistant that supports skills, put this repository in its skills folder (the skill's name is
`gbanocable`); for a person, read SKILL.md and then only the chapters it points you to.

## The problem

A link cable answers in microseconds. Games were written for that: they exchange a word, expect the reply almost at
once, and treat a short silence as "the cable was pulled". Emulators that send each transfer over the network
(one round trip per byte or word) either crawl or throw the game's own link errors. That is why most handheld link
modes never had a good online version.

## The idea

Do not send the cable over the network. Find the layer **above** the registers where the game hands over whole
packets or blocks (almost every game has one) and replace only the transport underneath. **Which transport depends
on what the game's link code really is, so read that first** ([docs/09](docs/09-multi-sio-anatomy.md)):

**A. The game is a lockstep simulation** (its play loop advances only with the frame's packet; packets carry
inputs). Kingdom Hearts: Chain of Memories.

1. **Slots**: every game frame is a slot. Each console fills its slot with one packet of its own queue (or an
   empty one) and sends it with the slot number.
2. **Pairs (or groups), on the same frame**: link frame *n* of every game (its *n*-th call to the cable since the
   connection) receives exactly the pair of slot *n − D*, with the same delay *D* on every machine. Handing pairs
   over "as they arrive" is not enough here: the data is identical but lands on different game frames, and any
   state machine that runs while waiting (handshakes, data exchanges, timers, the random generator) drifts apart.
3. **Never cut**: none of the cable's errors (timeout, queue full, checksum) are raised over the network. A missing
   packet makes the game *wait*; it goes on when the packet arrives.
4. **Smooth**: *D* is picked once, by the host, from the measured round trip. Games that exchange *inputs* can go
   further with rollback ([docs/04](docs/04-netplay-approaches.md)).

**B. The game replicates state and passes events as messages** (each console decides for its own character and
writes its state into the block every frame; things that happen once are passed inside the blocks in small
stop-and-wait steps). Shining Soul II. Lockstep buys nothing here and makes every event cost many round trips:

1. **Direct**: each player's blocks are handed to the game in order as they arrive, the last one repeated when
   nothing new came. Nobody waits for anybody; there is no delay.
2. **Messages whole, in one order**: the game's sender hands each message to a reliable channel; every message goes
   through the host, which gives all of them one order; every machine acts on them in that order, each once.
3. **A gone player is not the end**: it stays as a standing character; a connection that comes back is taken in
   again and replayed what it missed ([docs/11](docs/11-departures-and-reconnect.md)).

Measured in ReKHCoM (two hidden games, local relay with injected lag): 4 frames of delay + up to 6 frames of jitter
+ 5% packet loss: 3-6 waited frames in 3600; a 10 s total outage in the middle of a battle: both games wait and
continue in sync; with rollback and locally derived relayed keys, 32-bit vs 64-bit builds kept every state check
equal (50/50) at 3+3 frames of lag with 2% loss. Lesson: two equal PCs never showed a bug that a slow device did;
test builds of different speed against each other.

Measured in ShiningSource (scripted headless players on one machine, a relay that simulates the network): a game
message in about 100 ms instead of about 650 at 60 ms of latency once messages travel whole; before the outage
work 0 of 36 sessions survived a player being cut off for 3 to 30 seconds, after it 60 of 60. **Not measured: any
of it over a real network.** Each chapter says what its numbers cover.

## Try it in ten minutes

Needs a C99 compiler and Python 3. No dependencies, no game, no ROM.

```sh
git clone https://github.com/MatyMeatBoy/GBANoCable && cd GBANoCable/reference

# 1. the slot engine (lockstep games): 2 and 4 players, delay, jitter, loss, outages
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_check.c -o test_check && ./test_check

# 2. the direct transport + ordered reliable messages (state-replication games), with outages
cc -std=c99 -O2 -Wall -Iinclude src/gnc_direct.c tests/test_direct.c -o test_direct && ./test_direct

# 3. the same long outages without the hole fix: this one must FAIL (players frozen, exit status 1)
./test_direct --no-hole-fix; echo "exit status $?"

# 4. the relay and the trace tool
cd .. && python3 -m unittest discover -s tools -p "test_*.py"

# 5. a local relay with a bad network for your own game: 4 seats, 60-100 ms, 5% loss,
#    seat 2 cut off for 10 s starting 20 s after it joined (stop it with Ctrl+C)
python3 tools/relay.py --port 48300 --seats 4 --max-datagram 1200 --lag-ms 60 --jitter-ms 40 --loss 5 --outage 2:20:10
```

What you should see (these exact commands were run on 13/11/2026 with clang 23 from llvm-mingw on Windows x86-64 as
`cc` and Python 3.14; the test brings its own random numbers, so `test_direct` prints the same lines everywhere):

```
3 players, player 2 away 300 frames                        order once fresh  (2039 messages, 4 holes stepped over, ...)
3 players, player 2 away 300 frames, no hole fix           order once FROZEN (2039 messages, 0 holes stepped over, ...)
all ok
...
3 players, player 2 away 300 frames, NO hole fix           order once FROZEN ...
FAILED: without the hole fix a player that was away stays frozen
exit status 1
...
Ran 19 tests in 0.3s
OK
```

`order`: every player was given the same messages in the same order. `once`: none twice, none missing. `fresh` /
`FROZEN`: whether each player's view of the others is current at the end. Step 3 is the bug that froze players in
real sessions, reproduced in one process ([docs/10](docs/10-direct-transport-and-messages.md), section 5).

To put it in a game: [reference/README.md](reference/README.md), then [SKILL.md](SKILL.md).

## Contents

| Path | What |
|---|---|
| [SKILL.md](SKILL.md) | The method in order (online play; porting) and which chapter changes which decision |
| [docs/01-gb-gbc-serial.md](docs/01-gb-gbc-serial.md) | Game Boy / Color serial port: how a byte transfer works and why it is hard online |
| [docs/02-gba-sio.md](docs/02-gba-sio.md) | GBA serial: Normal, Multiplay, UART, JOY BUS, GPIO; who is the parent |
| [docs/03-software-patterns.md](docs/03-software-patterns.md) | How games use the cable: packet queues, handshakes, checksums, flow control |
| [docs/04-netplay-approaches.md](docs/04-netplay-approaches.md) | Register-level, packet slots, input lockstep, rollback, direct state + messages, host authority, streaming |
| [docs/05-recomp-without-decomp.md](docs/05-recomp-without-decomp.md) | Finding and hooking a game's link layer in a static recompilation: the short version |
| [docs/06-transports.md](docs/06-transports.md) | UDP, Steam (Spacewar 480 for development), a relay on a VPS, lobbies, hardening |
| [docs/07-testing.md](docs/07-testing.md) | Testing without people: scripted players, simulated networks, outages, reports, reproducing a desync |
| [docs/08-decomp-to-native-port.md](docs/08-decomp-to-native-port.md) | From a matching decompilation to a native 64-bit port: the road and the decisions |
| [docs/09-multi-sio-anatomy.md](docs/09-multi-sio-anatomy.md) | A Multi SIO game taken apart: blocks, state vs. events, the lobby, messages by role, barrier tokens |
| [docs/10-direct-transport-and-messages.md](docs/10-direct-transport-and-messages.md) | The direct transport and the reliable ordered message channel; the 40-frame reach and the hole that froze players |
| [docs/11-departures-and-reconnect.md](docs/11-departures-and-reconnect.md) | What "gone" means to the game: standing characters, revival with replay, the host, barriers, the lock-out decision |
| [docs/12-join-in-progress.md](docs/12-join-in-progress.md) | Letting a late program into a game that says everything once: the anchor at a map set-up, what was measured, what is open |
| [docs/13-relay.md](docs/13-relay.md) | The relay: rooms, seats, tokens, the rules number, hardening, test knobs for loss, lag, jitter and outages |
| [docs/14-recomp-playbook.md](docs/14-recomp-playbook.md) | Chapters 09-13 again without source: hook points, finding buffers and handlers, what cannot be patched |
| [docs/15-native-port-mechanisms.md](docs/15-native-port-mechanisms.md) | Each mechanism of a native port with its check and its limits: memory, BIOS, frame boundary, PPU, wide picture, sound, saves, determinism |
| [docs/verified-lessons.md](docs/verified-lessons.md) | Lessons with trigger, mechanism, check and limits |
| [games/](games/) | Per-game sheets: Kingdom Hearts: Chain of Memories, Shining Soul II; others: help wanted |
| [reference/](reference/) | C99 libraries with tests: `gbanocable` (slots, desync check) and `gnc_direct` (direct state + ordered messages) |
| [tools/relay.py](tools/relay.py) | Hardened room-code relay and room list, with test-only lag, jitter, loss and outages (standard library Python) |
| [tools/pairdiff.py](tools/pairdiff.py) | Compares the final groups two games played, from their traces: finds where and in which words they drifted |
| [docs/references.md](docs/references.md) | Everything this builds on |

## Español

**GBANoCable** reúne lo necesario para llevar los modos de cable link de GB, GBC y GBA a internet sin las
desconexiones del cable, y el camino de una decompilación a un port nativo: documentación del hardware y de cómo
lo usan los juegos, los enfoques de netplay, una guía para recomps sin código fuente, dos bibliotecas de
referencia en C con pruebas (ranuras emparejadas para juegos lockstep; transporte directo con mensajes fiables y
ordenados para juegos que replican estado) y un relay de pruebas que simula retardo, pérdida y cortes. Sale del
online de ReKHCoM y de ShiningSource. [SKILL.md](SKILL.md) es la entrada: el método en orden y qué capítulo cambia
cada decisión. Autor: MatyMeatBoy. Se buscan colaboradores: documentar protocolos de más juegos, probar en otros
recomps, partidas reales entre redes distintas.

## License

MIT (code and documentation written here). No game code or data is included. Referenced specifications belong to
their authors (see [docs/references.md](docs/references.md)).

---
name: gbanocable
description: Method and reference for (1) porting a Game Boy Advance game to a native program from a decompilation and (2) putting its link-cable mode online (2-4 players over the internet) in a native port, a decompilation build or a static recompilation. Use when asked to add online play, netplay, rollback, reconnect or join-in-progress to a GBA/GB/GBC link game; to find, hook or replace SIO / MultiSio / wireless-adapter link code; to design a relay, rooms and seats for such a game; to diagnose desyncs, frozen players or "communication error" online; or to plan or debug a native 32/64-bit port of decompiled GBA code (memory at real addresses, BIOS calls, PPU, sound engine, saves, determinism across processors).
---

# GBANoCable

Everything here was learned on two shipped ports, ReKHCoM (a 2-player lockstep battle, rollback) and ShiningSource
(a 4-player co-op game that replicates state). The chapters state mechanisms, the numbers that were measured and
their limits. Load only the chapter that changes your next decision. Do not copy another game's constants: apply
the mechanism to the game in front of you and measure again.

Rules that hold for every step:

- **Read the game before choosing anything.** The first online layer of ShiningSource was built on an assumption
  about the game (lockstep), passed every consistency test and played badly. The game's own link code decides the
  design.
- **Change the transport, not the game.** The game's lobby, handshakes, packets and error handling run as over a
  cable. Every patch to game code is small, named and carries its reason; over a real cable the original code runs.
- **Say what was not verified.** Separate: read from code / run in a unit test / run with scripted players on one
  machine with a simulated network / run on other processors / played by people over the internet. Never promote
  one to the next.
- No ROM, BIOS, game data, game text or screenshots in anything you write here or publish.

## A. Online play for a link-cable game: the method, in order

1. **Find out what the link code really is** ([docs/09](docs/09-multi-sio-anatomy.md)). Three shapes, and a game can
   mix them:
   - *lockstep on inputs*: the play loop advances only with the frame's packet; packets carry keys or commands;
     both consoles compute the same world from the same packets;
   - *state replication*: each console decides for its own character and writes its state into the block every
     frame; a console reads its own block from where it wrote it and only the others' from the receive buffers;
   - *stop-and-wait messages*: events that happen once are passed inside the blocks in small steps (announce,
     chunk, acknowledge), each repeated until every player's block answers.
   From a decompilation: who writes the send buffer, who reads the receive buffers, what the play loop waits for.
   Without source: trace the SIO registers and handlers and watch the blocks over time
   ([docs/14](docs/14-recomp-playbook.md), section 2).
2. **Choose the seam** ([docs/14](docs/14-recomp-playbook.md), section 3): the SIO registers and interrupt (last
   resort: one round trip per word), the SDK's per-frame driver function (`MultiSioMain`-shaped: one block out, all
   blocks in, a status word), or the game's own send/receive queues. Take the highest layer that still carries
   every byte the game exchanges. The same seam serves a native port (linker wrap), a decomp build (a source
   patch behind a flag) and a static recomp (override by address).
3. **Choose the transport from step 1** ([docs/04](docs/04-netplay-approaches.md)):
   - lockstep on inputs → slots on one timeline with an agreed delay; add rollback where it is deterministic
     (`reference/src/gbanocable.c`);
   - state replication → direct delivery, nobody waits, no delay ([docs/10](docs/10-direct-transport-and-messages.md),
     `reference/src/gnc_direct.c`);
   - stop-and-wait messages → carry each message whole on a reliable channel with **one order for every machine**
     (through the host), and patch the game's sender and receiver to use it ([docs/10](docs/10-direct-transport-and-messages.md)).
4. **Relay, rooms, seats, versions** ([docs/13](docs/13-relay.md), [docs/06](docs/06-transports.md)): a relay you
   run; a room is the name hashed with the cartridge's contents, a `rules` number and the transport, so builds that
   must not meet land in different rooms; the first seat is the game's parent. Raise `rules` whenever players must
   agree on something new.
5. **Departures, outages, reconnect, barriers** ([docs/11](docs/11-departures-and-reconnect.md)), then **join in
   progress** ([docs/12](docs/12-join-in-progress.md)): decide what a gone player is to *this* game before testing
   with people. Know the host special case and every place where the game waits for "every player".
6. **Test rig and what to measure** ([docs/07](docs/07-testing.md)): headless players with scripted input, a relay
   that drops, holds, reorders and cuts a player off, one log line per delivered frame or message, a report that
   asks the same questions of every run. Measure message latency, frames waited, and whether a session is whole
   again after an outage; prove each check fires.
7. **State what is not verified** in the release notes, per claim, with run counts and the machine. Template:
   the "Test status" block of [games/TEMPLATE.md](games/TEMPLATE.md).

## B. Porting a GBA game to a native program: the method, in order

All of it in [docs/08](docs/08-decomp-to-native-port.md) (the road and the decisions) and
[docs/15](docs/15-native-port-mechanisms.md) (each mechanism with its check and its limits).

1. Compile the decompiled C **unchanged**, through generated port sources: mechanical transformations plus a list of
   small patches with reasons. The compile gate is a number.
2. Keep GBA memory at its real addresses and pointers 32-bit: a 32-bit development build first, then a 64-bit host
   with a 32-bit-pointer game image (x32 ELF, arm64_32), a versioned bridge table, thunks where calling conventions
   differ.
3. Replace the hardware in this order: scheduler/interrupts/VBlank wait (the frame boundary), BIOS calls with the
   cartridge's results, a line renderer, input, sound, saves.
4. Make **determinism the test method** from the first headless frame: scripted input, one reference hash and one
   audio line that every build and processor must reproduce. Online crossplay depends on it.
5. Find what only ARM forgave (calls with too few arguments, declarations that disagree, stray accesses, division
   by zero) with tools, and fix it **in the decompilation** while the ROM still matches.
6. Then the port's own features (wide picture, HUD, flicker as transparency, pacing, frame skipping), each guarded
   by a hash or a bench. Then online (part A).

## Which chapter changes which decision

| You are deciding | Read | It will tell you |
|---|---|---|
| Is this game lockstep, state replication or messages? | [09](docs/09-multi-sio-anatomy.md) | what a Multi SIO game's blocks hold, how to tell state from events, the lobby and the barrier |
| Where to cut in (registers, SDK driver, game queues) | [14](docs/14-recomp-playbook.md) §3, [05](docs/05-recomp-without-decomp.md) | the three seams for a port, a decomp build and a recomp |
| Delay, rollback or neither | [04](docs/04-netplay-approaches.md) | which approach fits which link style; rollback rules from ReKHCoM |
| Why events are slow or arrive in different orders | [10](docs/10-direct-transport-and-messages.md) | the cost of stop-and-wait behind latency; the ordered channel; the packet shape |
| A player froze after a lag spike | [10](docs/10-direct-transport-and-messages.md) §5 | the 40-frame reach, the hole, the fix, the test that shows it |
| What happens when someone leaves or drops | [11](docs/11-departures-and-reconnect.md) | standing characters, revival with replay, when replay is impossible, the host, barriers |
| Late joiners | [12](docs/12-join-in-progress.md) | what a game that says everything once lacks; the anchor; what was measured |
| The server | [13](docs/13-relay.md), [06](docs/06-transports.md) | protocol, rooms and seats, hardening, test knobs |
| No source, only a recompilation | [14](docs/14-recomp-playbook.md) | every item above without source; what cannot be patched and what to do instead |
| How to test without people | [07](docs/07-testing.md) | the rig, the reports, the traps |
| How 32-bit game code lives in a 64-bit process | [08](docs/08-decomp-to-native-port.md) §4, [15](docs/15-native-port-mechanisms.md) §2 | modes, image formats, bridge, thunks, absolute symbols |
| Picture, sound, saves, pacing | [15](docs/15-native-port-mechanisms.md) | line renderer and its hashes, wide picture, flicker bench, sound reserve, atomic saves |
| Two builds give different results | [15](docs/15-native-port-mechanisms.md) §9-10 | frame hash → call trace → value trace; the declaration and argument tools |
| Hardware facts | [01](docs/01-gb-gbc-serial.md), [02](docs/02-gba-sio.md), [03](docs/03-software-patterns.md) | registers, modes, the usual driver layers |
| A mechanism you think you have met before | [verified lessons](docs/verified-lessons.md) | trigger, mechanism, check, limits |
| A specific game | [games/](games/) | Kingdom Hearts: Chain of Memories (lockstep + rollback), Shining Soul II (state + messages) |

## Try it before trusting it

```sh
cd reference
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
cc -std=c99 -O2 -Wall -Iinclude src/gnc_direct.c tests/test_direct.c -o test_direct && ./test_direct
./test_direct --no-hole-fix        # must fail: players frozen after an outage
cd .. && python3 -m unittest discover -s tools -p "test_*.py"
```

The library tests exercise the transports with simulated players. They certify the transport's contract, not any
game: finish with real game processes and a report (docs/07).

## Growing this skill

A lesson enters [docs/verified-lessons.md](docs/verified-lessons.md) only with its trigger, the mechanism, the check
that showed it and its limits. A game enters [games/](games/) with [the template](games/TEMPLATE.md), including what
was not tested. See [CONTRIBUTING.md](CONTRIBUTING.md).

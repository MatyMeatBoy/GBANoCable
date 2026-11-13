# From a decompilation to a native port with online

The other chapters assume you already have a game running on a PC and want to replace its cable. This one covers the
road before that: taking a GBA game from "we only have the cartridge" to "a native program with online", and where the
link work fits. It is written from two projects that did it: ReKHCoM (Kingdom Hearts: Chain of Memories) and
ShiningSource (Shining Soul II). Dates are DD/MM/YYYY.

If you have a static recompilation instead of a decompilation, read [docs/05](05-recomp-without-decomp.md) and
[docs/14](14-recomp-playbook.md) first: the link layer can be hooked without decompiling the game. Section 7 below
says what a decompilation adds.

This chapter is the road and the decisions. Each mechanism of the port, **with the check that guards it and what
the check does not cover**, is in [docs/15](15-native-port-mechanisms.md). Where the two disagree, 15 is newer.

## 1. The three stages, and what each one buys

| Stage | Result | What it enables |
|---|---|---|
| Matching decompilation | C that rebuilds the ROM byte for byte | you can read every function; nothing runs natively yet |
| Names and data description | functions with names, data split into objects with known formats | you can find the link driver, the camera, the HUD, the save code; a port can load data without guessing |
| Native port | the same C compiled for a PC, over a platform layer | widescreen, real online, any platform |

Do not skip the second stage. A port needs to know *where things are* (which function reads the pad, which table holds
function pointers, which bytes are graphics), and the decompilation alone does not say.

## 2. Finishing a matching decompilation (Shining Soul II, 12/11/2026)

The last functions are always the largest. What worked, in order of effect:

- Remove decompiler artefacts before trying register tricks: shared `goto` tails, pointer temporaries, 32-bit
  variables for 16-bit values, variables that were merged or split wrongly.
- Compare with a register-normalised diff and fix differences in address order: most differences are a cascade from
  one earlier site.
- Read the compiler's own source and dumps when a register choice makes no sense; it answers in minutes.
- Turn every mechanical idea into a script that keeps a change only if the object is still identical. That is what
  lets cheap models and background jobs do real work.

The full account is in SS2Decomp `docs/agents/GIANT_FUNCTION_PLAYBOOK.md`.

## 3. Names and data, fast

- **Names**: build one self-contained packet per function (its code, the names of what it calls and who calls it) and
  go bottom-up, so each round has more named callees to lean on. Small models propose, a second pass reviews, a script
  rejects malformed answers. 1,880 functions were named this way in a day.
- **Data**: do not scan for pointers. Find the function that resolves resource ids, read which containers it indexes,
  and split by the containers' own tables. Then describe each category from its loader and prove it by decoding
  (palettes and tiles to PNG, strings to text, maps to an exact size). Shining Soul II went from 0.6% to 100% split in
  one session this way.

Both methods, with the tools, are in SS2Decomp (`NAMING_PIPELINE.md`, `DATA_PIPELINE.md`).

## 4. The first port decision: how 32-bit code lives in a 64-bit process

GBA C stores pointers in 32-bit integers and uses fixed addresses (`0x02000000`, `0x03000000`, `0x04000000`,
`0x08000000`). You have three ways to run it, and ReKHCoM ships all three:

| Mode | How | Use it for |
|---|---|---|
| 32-bit process | GBA addresses are real addresses inside the executable | the development build; simplest to debug |
| 64-bit host + 32-bit-pointer game image | the game is compiled for a 64-bit CPU with 32-bit pointers (x32, arm64_32) and linked at a low address; a 64-bit host maps the GBA regions at their hardware addresses and offers a table of services | Windows, Linux, Android, PS5 |
| Relocated | regions are arrays and pointer words are fixed at load | consoles that cannot map low addresses |

Converting the game to true 64-bit pointers is not on the list: it rewrites most of the C and cuts the tie to the
matching decompilation.

**Measure before deciding.** On Shining Soul II (12/11/2026) the unmodified decompilation was fed to clang for x86:
41 of 47 game modules compiled after five mechanical transformations (ARM register pins, empty `asm("")` barriers,
constants written as instructions, literal-pool directives, one assembler include) and three one-line patches. Keep
those transformations in a build step that generates port sources; never hand-edit the decompilation for the port.

## 5. What the platform layer must replace

| Hardware | Replacement | Trap |
|---|---|---|
| Start-up and interrupts | the game's own main runs; its "wait for VBlank" call hands the frame to the port, which draws, raises the enabled interrupts (the line-match handler at its line), presents and paces | game speed must never depend on the monitor: either the port's own 59.7275 Hz clock, or a display whose rate is a multiple of 60 Hz *with a floor* that falls back to the clock ([docs/15](15-native-port-mechanisms.md), section 8) |
| BIOS calls | C functions with the documented behaviour | division by zero and stray pointers that the hardware tolerated; a soft reset is a restart of the program; trigonometry from the BIOS's table, not the C library |
| Hand-written assembly | native code with the same entry points | a cooperative scheduler written as an ARM context switch must become coroutines |
| PPU | a software renderer the port owns, drawing each line with the registers of that line | owning it is what makes widescreen and HUD layers clean; guard it with a hash over every picture of the reference run |
| Sound engine | a native MusicPlayer2000 sequencer and mixer reading songs from the ROM | the decompiled sound code mirrors assembly and is not worth compiling, but **write the native engine after it**: a first engine that did not follow the driver restarted songs the game only paused and played some samples 27% sharp |
| Save chip | the SDK flash library's entry points over a file, replaced atomically with a backup | present the chip's identifier where the game looks for it, or the game refuses to save |
| ROM data | map the user's ROM at `0x08000000` | words that hold addresses of *functions* must be rewritten to native functions; a data description tells you which words are safe to treat that way |
| Code copied to RAM | redirect to the native function | games copy hot routines to IWRAM |

## 6. Where the cable goes

Put the network under the game's packet queue, exactly as in [docs/03](03-software-patterns.md) and
[docs/04](04-netplay-approaches.md). A native port from a decompilation makes this the easy part:

- the multiplayer driver is a named C file; its "send this packet" and "packets received this frame" functions are
  the two hook points;
- the serial interrupt and timer are simply not raised online;
- every platform compiles the same integer-only game code, so sessions between Windows, Linux and Android are
  deterministic without extra work. That is crossplay.

One relay process, port and protocol version per game on the server ([docs/06](06-transports.md)); never share
another game's namespace.

### What it took on Shining Soul II (12/11/2026): the first, stepped transport

> **Read this with its correction.** The recipe below is the *first* transport of that port: lockstep on one
> timeline. It works as described and is still there for a fixed delay, but it was the wrong design for this game,
> which replicates state and passes messages ("Read the game's link protocol before choosing a transport", at the
> end of this chapter, and [docs/10](10-direct-transport-and-messages.md)). The hook (three functions and the
> player number) is unchanged. Point 4 was the clue that should have been followed further. Point 5 is wrong for a
> game in progress: telling the game the truth about a departed player ends the session for everybody
> ([docs/11](11-departures-and-reconnect.md)).

The game uses the SDK's MultiSio library, so the whole cable is three functions: init, stop, and a per-frame
`MultiSioMain(send, receive)` that takes one 24-byte block and returns the blocks of the connected consoles plus a
status word. Replacing those three (linker `--wrap`, so the originals stay for offline play) and writing the player
number where the game reads it (`REG_SIOCNT`) was the entire hook. The recipe, in the order it was built:

1. **Label every block on one timeline.** The host owns it; a joining player adopts it from the first host block it
   hears. At timeline frame `F` each player is given everyone's block `F - delay`, its own included. Same label, same
   data, on every machine.
2. **Stall, never skip.** If a due block is missing the frame waits. Whoever is ahead waits, so players settle by
   themselves. A delay shorter than the real latency makes the game slower, not broken (measured: four players,
   70-90 ms each way, delay 3 instead of 6: 50% longer, no errors).
3. **Repeat instead of retransmit.** Every packet carries all blocks the others have not acknowledged. Loss,
   duplicates, reordering and a relay restart are one case.
4. **Know what the game reads locally.** This game reads its own block from its send buffer and only the others'
   from the receive slots. So delay postpones what you see of the others, not your own controls, and rollback is not
   needed. Check this before designing prediction.
5. **Give up a player only on evidence**: it said goodbye (left the mode, closed the window) or it has been silent
   for many seconds. Then tell the game the truth (its bit leaves the status word) and let the game's own code react.
6. **Test without people.** A relay that can drop, hold and reorder packets, headless players with scripted input,
   and one log line per delivered frame (label, players, hash) compared across players. Four players, 10% loss and
   120-160 ms of lag passed before any window was opened.

A trap worth a line: a test harness that passes settings as environment variables needs a `getenv` whose results
stay valid. The 64-bit game image had a minimal C library that recycled four buffers, and the fifth call silently
erased the scripted input.

### Other processors: what changed for ARM64 and Android (12/11/2026)

Crossplay needs every build to compute the same numbers, so each new processor is checked against the reference with
one hash per frame over a long scripted session. x86-64 on Windows, Linux and Android agreed at once. ARM64 did not,
and the three causes are worth knowing before you start:

- **32-bit pointers on AArch64 means clang's `arm64_32` target, and its object format is Mach-O.** No ELF toolchain
  links ILP32 for AArch64 (clang compiles it, lld does not link it). The image is still a flat file the host maps
  itself; nothing of Apple's is used. Give it a fixed base with `-pagezero_size` and turn off chained fixups so the
  data holds plain addresses.
- **That calling convention trusts declarations the decompilation does not keep consistent.** Small arguments on the
  stack are packed (a `u16` takes two bytes) and a function does not narrow a small argument it receives. A function
  declared with `s32` in one file and `u16` in another works on the console and on x86 and silently differs here:
  first a sprite's scale and rotation arrived shifted, then a clipped sprite was drawn. Rewriting the generated
  sources so that every function narrows its own small arguments (what the console's compiler did) fixed 375
  definitions at once. The lasting repair is in the decompilation.
- **Division.** ARM64 never traps: by zero the quotient is 0 and the remainder is the dividend. Make the x86 fault
  handler give exactly that, including the remainder, or the builds drift apart on the first `% 0`.

How each cause was found, in the order that works: per-frame hashes give the first differing frame; a call trace of
that frame on both processors gives the function; a temporary source patch that prints the arguments of that function
gives the value. Keep a test machine of the other processor running and send it files, instead of restarting it for
every try: a QEMU machine without acceleration takes five minutes to boot and nine to run the session.

Android adds two rules of its own. The host is a library loaded wherever the system likes, so the game image reaches
its services through short jumps placed in low memory; and Android's Java runtime aborts any call made from a stack it
does not know, so every service has to run on the thread's real stack, not on the game's coroutine stacks. Claim the
low address range in a constructor, before the graphics driver and the Java heap can settle there.

## 7. If you only have a static recompilation

You can still get online with [docs/05](05-recomp-without-decomp.md): hook the link functions by address. What a
decompilation adds later:

- **Determinism you can reason about.** You can read which state a packet depends on instead of discovering it by
  desync.
- **Snapshots that are small.** A recompilation saves the whole machine; a port that knows the game's structures can
  save and restore only what matters, which is what makes rollback cheap.
- **Features in the right place.** Widescreen in the camera and draw-list code, not by matching patterns in video
  memory.

A recompilation and a decompilation of the same game are not rivals: the recompilation is a working reference to
compare frames against while the port grows.

## 8. Order of work that avoids rework

1. Compile gate: every game module builds for the PC target (a number you can track).
2. Link with stubs; the undefined-symbol list is the work list.
3. Headless boot with a frame hash compared against an emulator. This is the port's `make compare`.
4. Picture, then input, then sound and saves.
5. The 64-bit host.
6. The port's own features (wide view, HUD, launcher, achievements).
7. Online: **read the game's link protocol**, then the transport it calls for, never-cut rules, departures and
   outages, the checks that fit (a state hash for lockstep, message order and views for state replication), relay,
   lobby.
8. Remaining platforms.

Online is seventh on purpose. A desync is impossible to diagnose while the single-player game still differs from the
cartridge.

## Read the game's link protocol before choosing a transport

The first transport of the Shining Soul II port treated the game as a lockstep simulation: every player was
given the same link blocks on the same frame of a shared timeline, a few frames late. It passed every
consistency test and still played badly with real people: items and gold appeared late or never, enemies arrived
one at a time, players saw each other in different places, and the delay crept up.

The decompilation showed why. The game does not step in unison. Each console decides for its own character and
sends its state every frame (position, facing, animation), and everything that happens once (an enemy appears, an
item is dropped or taken, a hit, a change of map) is a message passed inside the same blocks in small
stop-and-wait steps, each step repeated until every player's block acknowledges it. On the cable a step takes a
frame or two. Behind a buffered delay of `d` frames each step costs `2d + 2` frames, and one item needs five steps.

What the port does now, and what to check in any game before writing its transport:

1. **Find out whether the blocks are inputs or state.** Look at who writes the send buffer and who reads the
   receive buffers. If a console reads its own data from the send buffer at once and the others' from the receive
   buffers, it is state replication, and a shared timeline buys nothing.
2. **Look for a message layer inside the blocks** (announce, chunk, acknowledge bytes). If there is one, its cost
   is one round trip per step, and that, not the picture, is what latency hurts.
3. **For state replication, deliver directly**: each player's blocks in order as they arrive, repeat the last one
   when nothing new came, never make one player wait for another. Do not skip a block that carries a new step of
   the message layer; do drop blocks that only repeat it when several are waiting.
4. **Carry the message layer out of band.** Patch the game's sender to hand the whole message to a reliable,
   ordered channel of the transport, and its receiver to take whole messages from it and run the game's own
   dispatch unchanged. In the port a message went from about 650 ms to about 100 ms at 60 ms of latency.
5. **Decide what a departed player is.** A game written for a cable assumes ids `0..n-1` and treats silence as a
   pulled plug. Keeping the seat, its last block and "ready" answers lets the others play on; the console the game
   makes responsible for enemies and items cannot be replaced that way.
6. **Test with people early.** Scripted players that press the same buttons hide exactly these problems.

Each point has its own chapter now, with what was measured afterwards: the anatomy of the blocks and messages
([docs/09](09-multi-sio-anatomy.md)); the direct transport, the one order of messages and the hole that froze
players ([docs/10](10-direct-transport-and-messages.md)); departures, outages and reconnect
([docs/11](11-departures-and-reconnect.md)); join in progress ([docs/12](12-join-in-progress.md)); the relay
([docs/13](13-relay.md)). Two statements above need their later correction: "in order as they arrive, repeat the
last one" is right, but a stream that was silent for longer than a packet reaches back must **step over the hole**
or it stands still for good; and per-sender order of messages was not enough, **one order through the host** is
what the game needs.

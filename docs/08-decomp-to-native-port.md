# From a decompilation to a native port with online

The other chapters assume you already have a game running on a PC and want to replace its cable. This one covers the
road before that: taking a GBA game from "we only have the cartridge" to "a native program with online", and where the
link work fits. It is written from two projects that did it: ReKHCoM (Kingdom Hearts: Chain of Memories) and
ShiningSource (Shining Soul II). Dates are DD/MM/YYYY.

If you have a static recompilation instead of a decompilation, read [docs/05](05-recomp-without-decomp.md) first: the
link layer can be hooked without decompiling the game. Section 7 below says what a decompilation adds.

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
| Start-up and interrupts | the game's own main runs; its "wait for VBlank" call hands the frame to the port, which draws, raises the enabled interrupts, presents and paces | pace to 59.7275 Hz, not to the monitor |
| BIOS calls | C functions with the documented behaviour | division by zero and stray pointers that the hardware tolerated |
| Hand-written assembly | native code with the same entry points | a cooperative scheduler written as an ARM context switch must become coroutines |
| PPU | a software renderer the port owns | owning it is what makes widescreen and HUD layers clean |
| Sound engine | a native MusicPlayer2000 sequencer and mixer reading songs from the ROM | the decompiled sound code mirrors assembly and is not worth compiling |
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

### What it took on Shining Soul II (12/11/2026)

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
7. Online: transport, fixed delay, never-cut rules, state hash, relay, lobby.
8. Remaining platforms.

Online is seventh on purpose. A desync is impossible to diagnose while the single-player game still differs from the
cartridge.

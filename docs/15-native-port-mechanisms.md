# Porting a GBA game to a native program: the mechanisms, their checks and their limits

[docs/08](08-decomp-to-native-port.md) is the road and the decisions. This chapter is the reference: each mechanism
that ShiningSource (Shining Soul II, from its matching decompilation) uses, **how it is checked**, and **what the
check does not cover**. Paths in `code font` are ShiningSource's. Numbers come from one development PC and, where
said, from Debian 12 virtual machines (x86-64, and ARM64 under QEMU) and the Android x86_64 emulator. No real phone
and no real 60 Hz display were used. Section 13 says what carries over to a static recompilation.

The contract everything else is measured against: the game's own logic and timing (59.7275 frames a second), the
save format, and **identical results on every processor**, because online play between platforms depends on it.

## 1. Compile the decompiled C unchanged, plus a list of small patches with reasons

**Mechanism.** The decompilation is a submodule and is never edited for the port. A build step (`tools/build.py
prepare`) copies its C and headers into generated port sources and applies:

- *mechanical transformations* that only undo what steered the original ARM compiler: register pins
  (`register T x asm("r4")`), empty `asm("")` barriers, constants written as instructions, literal-pool directives,
  one assembler include, assembler names given the platform's C symbol prefix;
- *hoisted prototypes* where a function is used before its file declares it (a generated list: the build compiles,
  learns which prototypes are missing, repeats until stable);
- *patches*: a JSON list per source file of `{why, find, replace}`. Every entry has its reason in words. They fall
  into a few kinds: hand-encoded instructions rewritten as C; functions that address their original stack frame
  through a pinned stack pointer (below); drawing limits for the wide picture; the hooks of the online layer;
  the port's own options. About forty-five entries in all for this game;
- *modules replaced by the runtime* (also a list with reasons): the sound library, the flash driver, the original
  toolchain's support library.

The link step sorts every undefined symbol into four groups: RAM symbols at fixed GBA addresses and ROM symbols
(both become **absolute symbols**), functions the runtime implements, and **stubs** that report their name the
first time they run. A symbol that fits no group stops the link. The stub list is the work list.

**Check.** A number at each gate: "N of N game modules compile" (first measurement on the untouched decompilation:
41 of 47 after five transformations and three one-line patches), then the stub list shrinking (47 at first: BIOS
and start-up, sound entry points, the scheduler's routines), then headless frames.

**Traps found.**

- A function that reads its frame through `register u8 *stack asm("sp")` compiles after the pin is stripped and then
  runs with a null pointer: the item shop crashed every time it was opened. The build now **refuses** such a
  function without a patch that gives it a real local frame. Three were found.
- A test build that does not regenerate its sources passes its tests on old code. Chain prepare, compile and link,
  or stamp the generated content.
- Locals read before being written are zeroed on every build (`-ftrivial-auto-var-init=zero`), and bit-fields
  follow GCC's layout on every build (the 32-bit Windows target otherwise uses another): both were sources of
  differences between builds.

**Limits.** "Compiles and matches the ROM" does not make the C correct C (section 11). The patch list is for one
ROM revision.

## 2. GBA memory at its real addresses, and what that costs

The game's C stores pointers in 32-bit integers everywhere and names memory by address. So pointers stay 32-bit and
the GBA regions sit at their hardware addresses; converting to real 64-bit pointers would rewrite most of the code
and cut the tie to the matching decompilation.

| Build | What it is | Why |
|---|---|---|
| dev32 | a 32-bit process; GBA addresses are real addresses | simplest to debug; not shipped; everything proved here carries over |
| host64 + image, x86-64 | the game compiled for x86-64 **with 32-bit pointers** (the x32 ELF target, freestanding, no red zone), linked flat at a fixed low address; a 64-bit host maps the GBA regions, loads ROM and image, serves a table | Windows, Linux, Android x86_64 |
| host64 + image, ARM64 | the same sources compiled by clang's **arm64_32** target; its object format is Mach-O; a flat file the host maps itself (nothing of the vendor's runtime is used) | no ELF toolchain links 32-bit pointers for AArch64 |
| wasm32 | 32-bit by nature | no absolute symbols in its object format: addresses are written into the LLVM IR instead |
| relocated | regions as arrays, pointer words fixed at load | for platforms that cannot map low addresses; **not built** |

**Getting the low addresses.** Asking the system for fixed addresses at run time fails often (something is already
mapped there). The program is linked at a low base and carries the address space as one zero-filled block of its
own image, which is mapped before anything else. Making that block a read-only section whose ranges become writable
when handed out took the memory charged to the process from 281 to 95 MiB (*measured*, Windows x64; the file did
not grow, after a first attempt that made it 235 MB). On Android the host is a library loaded wherever the system
likes: claim the low range in a constructor, before the graphics driver and the Java heap settle there.

**The bridge.** The image calls the host through a table of services (files, log, video, input, sound, coroutine
stacks, network, clock). Each entry is the 32-bit address of a host function; every service uses **one calling
convention on every operating system** and takes pointers as 32-bit integers, so the image is the same file for
every system of a processor. Entries are added at the end only, and the table has a **version the image checks at
start**. Where the host cannot sit below 4 GiB (ARM64, Android) the services are reached through short jumps placed
in low memory. The image runs on stacks in low memory (it stores addresses of its locals in 32 bits), with its own
coroutine switch; on Android every service must run on the thread's real stack, because the Java runtime aborts a
call made from a stack it does not know.

**Thunks and rewrites the 64-bit conventions need** (all generated, none hand-written):

- *Structs by value.* A function declared `f(s32 x, s32 y, void *p)` in one file and defined `f(struct Pos pos,
  u8 *p)` in another is the same call on ARM and on 32-bit x86, and not on any 64-bit convention. Adapters receive
  the declared form and call the defined form (12 functions; struct sizes measured with the compiler).
- *Small arguments on arm64_32.* That convention packs small stack arguments and trusts the caller to have extended
  them. Every definition is rewritten to take full words and narrow them itself, as the console's compiler did (375
  definitions).
- *Division* (section 3) and *absolute symbols* for WebAssembly, both as rewrites of the LLVM IR **before
  optimisation** (afterwards the compiler may already have used the undefined cases to remove code).

**Check.** Section 10: every build must print the same hashes.

**Limits.** 4 GiB of address space (the game needs 300 KiB). A real ARM64 phone has not run the image; ARM64 was
checked under QEMU on Linux. The host commits more address space than it touches.

## 3. BIOS calls and arithmetic, with the cartridge's results

**Mechanism.** The BIOS calls the game uses are C functions with the documented behaviour: integer square root, the
arctangent polynomial in fixed point, memory copies and fills, the LZ77 decompressors, the affine parameter set, the
RAM reset. What the documentation leaves open is decided by **what the cartridge's code would have received**:

- **Division.** The game divides with the compiler's library routines, not with the processor. Their contract,
  *read from the ROM's own routines*: a zero divisor gives quotient 0 **and remainder 0**; the most negative number
  divided by -1 gives itself with remainder 0. In C both are undefined and every processor differs: x86 traps (the
  fault handler continues with the cartridge's values), ARM64 gives quotient 0 but the *dividend* as remainder (75
  remainders are guarded in the IR), WebAssembly traps and cannot be resumed (everything guarded). A first attempt
  unified on ARM64's own result, which is not the cartridge's, and was corrected.
- **Trigonometry must come from a table**, not from the C library: the BIOS has its own sine table, and two C
  libraries need not round the same. *State in the port: still open.* Its affine call uses the library's sine; the
  reference run nevertheless agrees on every build tried. Treat that as luck until the table is in.
- **SoftReset is a restart.** The game calls it after a link error, among others. The port exits with a code that
  asks the host to start the program again; a test run just ends. (Before that, a host leaving an online session
  closed everybody's game.)
- **Stray accesses.** The cartridge writes through null pointers and reads unmapped addresses, and the console
  ignores it. A fault handler decodes the faulting load or store, drops the write or gives the read a zero, and
  continues, **for game code only**, reporting each site once. Anything it does not recognise is a real crash.

**Check.** The reference hash on every processor; a per-frame hash comparison found the first `% 0`.

**Limits.** A recovery handler can hide a broken contract: when one build recovers and another does not, find out
what the original would have read instead of widening the handler.

## 4. The frame boundary: scheduler, interrupts, the VBlank wait

**Mechanism.** The game's own main function runs. Its wait for the vertical blank hands the frame to the runtime,
which draws, runs the interrupt handlers the game enabled (read straight from the game's own handler table), flushes
the network, presents and paces. One frame of the game is one call of that wait, on every build, with or without a
window.

The engine's cooperative task scheduler is hand-written ARM (a context switch). It becomes native coroutines with
the same entry points; **the task record in GBA memory keeps the same fields and states**, because game code reads
them. Code pointers inside ROM data (task entry tables) are rewritten at load to the native functions, in ranges
chosen from the decompilation's data description so that samples, text and archives are never scanned. Routines the
cartridge copies to RAM for speed are redirected to the native functions.

**Check.** Headless frames with a hash from the first milestone on; a scheduler trace names the caller when a task
entry is wrong.

**Limits.** The range scan replaces any word that equals a function's entry address: a data word with that value
inside a scanned range would be rewritten too. None is known; it is not proven that none exists.

## 5. The PPU as a line renderer, checked by hashes

**Mechanism.** A software renderer the port owns: text and affine backgrounds, regular and affine sprites,
priorities, both windows and the sprite window, alpha blending, fades. It draws each line **with the registers as
they were on that line**: after the vertical-blank handler the runtime walks the lines, runs the game's line-match
interrupt handler at its line, and keeps every line's registers. The first version drew whole frames; a scrolling
text that the game moves with a line interrupt was cut until this was done. The line walk runs without a window
too: the handler is game code, and every player must run the same code.

**Check.** Two kinds of hash, and they guard different things:

- the **frame hash** (game state and video memory) of the scripted run: must not change for any change that is not
  meant to change the game;
- a **whole-picture hash**: every frame of the 32,000-frame run is drawn as a window would, at 240, 284, 427 and
  480 columns, and one hash over all pictures is printed per width. The renderer was made a third cheaper
  (*measured*: 1,398 to 889 µs a frame at 240 columns, 2,079 to 1,381 at 480) with all four hashes unchanged. A
  change that measured no gain was dropped.

**Limits.** These hashes prove *the same as before* and *the same on every build*. **The picture was never compared
against an emulator or the console** (the comparison script is an open item), so they do not prove *the same as the
cartridge*. What the scripted run does not show (256-colour tiles, the larger hardware maps) is covered only by the
fast path being a transcription of the per-pixel one.

## 6. A wide picture without stretching

**Mechanism.** The renderer takes a picture width from 240 to 480 columns; the window picks the width that fills it
at a whole-number scale (284 on 16:9).

- **Where the extra columns come from.** The engine keeps full tile maps larger than the screen and copies a
  256-pixel window of each into the hardware's tile ring. In a field scene (recognised by the engine's own state)
  each background that has a full map is drawn *from the map*, so the side columns are real scenery. A layer is used
  this way only if the tiles on screen agree with the map: a menu drawn on the same layer leaves the engine's records
  in place and would otherwise get field tiles drawn into it.
- **The console's 240 columns always stay in the middle** and are always drawn from what the game put in video
  memory. (An earlier version slid the view at the edges of a map; menus made of sprites then parted from their
  backgrounds.) Beside a map narrower than the view the picture is black with a short fade.
- **Menus, title, cut-scenes** keep the console's composition, centred.
- **The HUD** is the top lines of the picture: what is left of a split column stays at the left edge, what is right
  of it goes to the right edge. A line where a layer crosses the split column (a window, a full-screen page) is one
  piece and stays in the middle.
- **What the game does not draw beyond 240.** Its sprite routines hide anything outside about -64..240. Nine of
  them and the routine that queues item icons were patched to wider limits (drawing only), and a few screen x values
  that were unsigned became signed. Sprite x beyond 255 must not wrap in the renderer.
- Scene-change blackouts are window masks and must cover the extra columns.

**Check.** The whole-picture hashes at each width; the scripted run ends in a field with a map wider than the
screen.

**Limits.** There is no reference for the extra columns: the hash guards against regressions, a person has to look.
The game does not *create* actors beyond its own columns, so they appear late at the sides (open). Per-line effects
in wide scenes are open.

## 7. Flicker as transparency, recognised by content, with a bench

**Mechanism.** The cartridge has one real blend and many things to make transparent, so it shows a sprite one frame
and hides it the next (or two and two, or two things in one place in turns), counting on a slow screen. A modern
display shows a hard blink, and unevenly presented frames make it worse. The renderer composes such a pixel twice,
once with this frame's sprites and once with the other frame's, each in full, and averages.

Which sprites alternate is read from the sprite tables of the last frames. A sprite is followed **by what it shows,
not by its slot** in the table, because the game rebuilds the table every frame and reorders it. Separately, a pixel
that goes A, B, A (a blinking menu option) is shown as the steady mix, only where what it shows has not moved.

**Check.** A bench of 49 measurable cases (one is the game's own alternating sprite, the others the same patterns
written into the sprite table by the bench): the earlier code, patched report by report, failed 27; the rewrite
none. *Measured.*

**Limits.** The bench reproduces patterns, not the scenes players reported. A sprite that starts to alternate
blinks once or twice before it is recognised. Build the bench **before** rewriting a perceptual heuristic.

## 8. Sound, the audio reserve, pacing and picture skipping

**Sound engine.** The cartridge's driver mixes into DMA buffers and drives the four Game Boy channels through
registers; the decompiled driver mirrors hand-tuned assembly and is not worth compiling. The port has a native
engine written **after the decompiled driver**: the same state and the same decisions (players, tracks, every
command, note allocation and stealing, envelopes, the Game Boy channels' program driving a model of the hardware),
run once per frame from the game's own sound-main call, mixing at the host's rate. A first version that did not
follow the driver played, and was wrong in ways nobody would have listed: a song stopped and asked for
again started over instead of continuing; fixed-rate samples played 27% sharp; no vibrato or reverb.

*Check:* an audit that starts every entry of the song table and reports per track the notes started, notes that
found no channel and the energy mixed (428 entries). The headless run ends with one **audio line** (sample frames
produced, peak, samples above the noise floor), which every build must reproduce. *Limit:* **measured, not listened
to**; the mix at the host's rate is the port's own and not sample-identical to the console.

**Audio reserve.** A frame's samples reached the device with 2 to 7 ms to spare: any late frame was a gap. Now a
reserve of silence goes in first whenever the queue ran dry, and the average is held between 20 and 55 ms by playing
0.4% slower or faster. *Measured:* 30-42 ms on average, never empty in runs of 2 and 7.5 minutes. Costs 36 ms of
latency.

**Pacing.** The game runs at 59.7275 Hz. Who keeps time depends on the display: a refresh rate that is a whole
multiple of about 60 Hz paces the game (0.45% fast, the sound resampled by the same amount); any other rate from 59
Hz up, the game keeps its own clock and each picture waits for the next refresh; a slower display is not waited for.
**With a floor:** when the display was to pace the game and the present call returns well before a frame's time (a
minimised window, synchronisation forced off, a display faster than it said), the game falls back to its own clock.
*Measured before:* 6.94 ms a frame (2.4 times too fast) with a 144 Hz panel taken for 60 Hz. Online is no
different: the players' machines do not step together on the direct transport.

**Picture skipping that keeps game time.** When a picture takes longer than a frame, the game must not slow down and
the sound must not run dry: the next frame goes by without its picture (events, buttons and clock only), at most
three in a row. *Measured* with every picture slowed by 25 ms: 37.9 game frames a second before, 59.6 after, the
sound never empty. *A defect the simulation could not show:* on a really slow target (the Android emulator) the
picture after a skipped one waited for a time already pushed into the future, so three of four pictures were
skipped; after anchoring to now, 46 pictures a second at full game speed.

**Limits.** No real phone, no real 60 Hz display. Anything that runs the game faster than real time (skipping a
scene) must not do so online: the game counts its waits for the other players in frames and they all ran out at
once (a communication error in a real session).

## 9. Saves

**Mechanism.** The SDK's flash library is replaced: its entry points give the same results, the chip protocol does
not exist. The save is a file with the cartridge's layout, so it moves between the port, emulators and the
cartridge. Writing: the whole image to a temporary file, the write **checked**, the old save kept as a backup, then
the rename; a missing or short save loads the backup; a failed write leaves the save alone.

**Check.** A test of eight cases (complete write, replacement, cut-short file, missing file, unwritable folder);
the game's own save routine called at a scripted frame answers "saved and verified".

**Trap found.** The game asks the library which chip it is talking to. The first release presented the identifier
in the wrong place and the game refused to save at all ("save error", reported by the first player). **Run the
game's own save path in a test** before anybody plays.

## 10. Determinism as the test method

**Mechanism.** One scripted play-through, headless: a list of `frame:buttons` (from a file), 32,000 frames, new game
then minutes of movement and attacks. It prints one hash per frame on request, and at the end **one reference hash
and the audio line**. Every change is re-checked against both; every build and processor must print the same.

*Verified equal:* the 32-bit development build, Windows x64, Linux x64, Linux ARM64 (QEMU) and the Android x86_64
emulator. *Not verified:* ARM64 on a real phone; any comparison with an emulator running the cartridge.

**Finding a difference**, in the order that works:

1. per-frame hashes of both builds give the first differing frame;
2. a memory dump of both at that frame gives the first differing bytes (a record shifted by one byte: a layout
   cause);
3. a **call trace** of that frame on both (the game modules built with function instrumentation; the list of
   functions entered, compared by name) gives the function where they part, or shows they made the same calls;
4. a temporary source patch that prints that function's arguments gives the value.

Keep a test machine of the other processor running and send it files; a QEMU machine without acceleration takes
minutes to boot and to run the session.

**Headless settings and their traps.**

- Settings reach the game as environment variables. **A wrapper can drop them silently**: a long input script in a
  variable was dropped above about 32 KiB by the test script, and two runs were judged on stale logs. Long data
  goes in a file; settings for sub-processes go in a settings file the script sources.
- The 64-bit image's small C library recycled four `getenv` buffers; the fifth call erased the scripted input.
- A summary printed at exit never arrives on a platform whose process ends without the orderly exit (Android):
  print it from the real exit path.
- One Android emulator run ended at frame 28,380 with no report and no crash record; the repeat ran through.
  Unexplained. Say so when it happens.

## 11. What only ARM forgave: found by tools, fixed in the decompilation

A matching decompilation reproduces the ROM; it can still be wrong C that works only on ARM.

| Shape | Why it worked | Tool | Fix |
|---|---|---|---|
| a call with fewer arguments than the function takes | the missing value was still in the register from the call before | a checker compares every call with the definition's parameter count | write the argument out **in the decompilation**; the ROM still matches |
| a struct passed by value where a pointer is defined (or the reverse); different parameter counts | ARM passes the struct's first words in the registers where the pointer is expected | a checker of declarations against definitions | the same; meanwhile generated adapters (section 2) |
| `u16` in one file, `s32` in another | every argument travels in a full word and the callee narrows it | found by per-frame hashes on arm64_32 (a sprite's scale arrived shifted; a clipped sprite was drawn) | generated narrowing (section 2); the lasting repair is consistent declarations |
| failed allocations nobody checks | the console carried on | reported where they happen | none: known |

Each real one shows up as a crash or a wrong task entry; the checkers list candidates, they do not prove.

## 12. Crash reports translated to function names

A crash or a hang is written to the console **and to a bounded file beside the program** (a game started from a
launcher has no console anyone reads): registers, the game addresses on the stack, the time, the host's build time,
whether it was the game's thread. No paths and no names. A script turns every address inside the image or the host
into `function+offset` using the symbols of **the binaries that were running** (the report names the build for that
reason). A watchdog reports where the game's thread is after ten seconds without a frame or a network wait, once
per hang, and lets it continue. *Limit:* Windows host only.

## 13. What of this applies to a static recompilation

This table is reasoning from how each mechanism works. ShiningSource is not a recompilation, and nothing in the
right-hand column was built or measured there.

| Mechanism | In a recomp |
|---|---|
| Compile gate, patches with reasons | No C to compile. The equivalent of the patch list is the **override list**: address, why, what the native function does. Keep it as data with reasons, the same way |
| Memory at real addresses, 32-bit pointers, bridge, thunks | **Does not apply.** Translated code reaches memory through the runtime's bus functions; there are no native pointers into GBA memory and no calling-convention problem. This is the recomp's big saving |
| ARM-only C (section 11) | **Does not apply**: the translated code keeps ARM's register semantics |
| Division | Applies if the game calls the BIOS divide; the compiler's library routines are translated code and already return the cartridge's results |
| BIOS calls | Applies as is (or run the real BIOS code if your recompiler translates it). The sine table rule applies |
| Stray accesses | Applies, more simply: the bus functions decide what an unmapped address reads |
| Scheduler | **Does not apply**: the context switch is translated like any code, if your recompiler supports writes to the stack pointer and indirect jumps to saved addresses. If it does not, this is where you write coroutines |
| Frame boundary, interrupts, line-match handler | Applies. The recomp runtime decides when to run handlers; per-line registers need the handler to run at its line. Cycle counting is the recomp's own subject and not covered here |
| Code copied to RAM | **Differs and is the classic recomp problem**: translated code cannot run RAM. Redirect the known copies to the translated ROM originals, interpret the rest |
| PPU, wide picture, flicker, pacing, audio reserve, picture skipping, saves, crash reports | All apply unchanged: they sit below the game. The wide picture needs the engine's map structures by address instead of by name; HUD and sprite limits become overrides of the functions that clip |
| Sound | Differs: a recomp can run the cartridge's own mixer (translated) into the emulated sound hardware, which is exact and slower, or use a native engine as here |
| Determinism as the test method | Applies, and is stronger: a recomp can be compared frame by frame against an interpreter of the same ROM |
| Online | [docs/14](14-recomp-playbook.md) |

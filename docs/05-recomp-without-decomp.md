# Hooking the link layer in a recompilation (no decompilation needed)

A static recompilation (or a "recomp" that runs translated ROM code) lets you replace individual functions by
address. You do not need the whole game decompiled: only the handful of link functions, identified and
characterised.

This chapter is the short version, written for a **lockstep** game with packet queues (Kingdom Hearts: Chain of
Memories). Before step 3, find out which kind of game you have: trace the blocks over time and see whether they are
inputs, continuous state, or stop-and-wait messages ([docs/14](14-recomp-playbook.md), section 2). For a game that
replicates state and passes messages, steps 3 to 5 change: the full playbook, mechanism by mechanism, is
[docs/14](14-recomp-playbook.md).

## 1. Find the serial code

- Search the ROM's literal pools for `0x04000128` (SIOCNT), `0x0400012A` (SIOMLT_SEND), `0x04000120` (SIOMULTI),
  `0x04000134` (RCNT). The functions loading them are the driver.
- The serial IRQ handler: the game's interrupt table (often a list of function pointers indexed by IRQ bit) has an
  entry for bit 7 (serial). Timer 3 is a common pacing timer for the parent.
- The driver's callers are the packet layer; their callers are the protocol and the game.
- Games that link the SDK's MultiSio library carry its version string in the ROM: a string search identifies the
  library, and its per-frame function (one block out, every player's block in, a status word) is then the whole
  packet layer.

## 2. Characterise the packet layer

Trace a local two-instance session (an emulator's local link, or the recomp running two cores) and record:

- the function that **enqueues** a packet (called once a frame with a small buffer, zeroes it after copying);
- the function that **dequeues** a received packet (fills a buffer with every player's words, sets "empty");
- the **status** function (returns a bitfield: player id, count, connected, errors);
- the queue structures (depth, packet size), the zero-packet rule, the checksum, the pause/flow bit.

Compare with the MultiSio shape in docs/03; most games are close to it.

## 3. Replace the transport

Replace the status/state-machine function the game calls every frame (KHCoM: `SioRunStateMachine` and
`SioTransferFrames`) with a version that:

- in the handshake state: reports two players and the right ids once the network peer is present;
- in the connected state: takes one packet from the game's send queue into the frame's slot, receives the other
  slots, pushes complete groups into the game's receive queue, and never raises errors;
- leaves everything else (protocol, play loop) untouched.

Disable the interrupt-driven register path while online (no SIO IRQs fire, the timer is not needed).

If the game reads its player number from the serial control register itself, write it there. Keep the original
functions reachable when not online.

## 4. Patch the 0 ms assumptions

List every retry counter and timeout in the protocol (docs/03 table) and raise them **only online**. In a
recompilation these are constants in the translated functions: patch the comparison, or wrap the function.

## 5. Validate

Two hidden instances, deterministic inputs (a key script), simulated delay/jitter/loss/outage (docs/07). Pass when
both reach the play loop, both show the same thing frame by frame, and outages only produce waiting. ("The same
thing frame by frame" is the test of a lockstep game. A state-replication game is tested by its message order and
by each player's view of the others: [docs/10](10-direct-transport-and-messages.md), section 6.)

Before any socket, run two instances in one process with their buffers crossed, and check that the link overrides,
when inactive, leave the single-player frame hashes unchanged.

Help wanted: a tool that scans a ROM and lists candidate SIO driver functions automatically.

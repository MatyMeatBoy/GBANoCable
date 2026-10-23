# Hooking the link layer in a recompilation (no decompilation needed)

A static recompilation (or a "recomp" that runs translated ROM code) lets you replace individual functions by
address. You do not need the whole game decompiled: only the handful of link functions, identified and
characterised.

## 1. Find the serial code

- Search the ROM's literal pools for `0x04000128` (SIOCNT), `0x0400012A` (SIOMLT_SEND), `0x04000120` (SIOMULTI),
  `0x04000134` (RCNT). The functions loading them are the driver.
- The serial IRQ handler: the game's interrupt table (often a list of function pointers indexed by IRQ bit) has an
  entry for bit 7 (serial). Timer 3 is a common pacing timer for the parent.
- The driver's callers are the packet layer; their callers are the protocol and the game.

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

## 4. Patch the 0 ms assumptions

List every retry counter and timeout in the protocol (docs/03 table) and raise them **only online**. In a
recompilation these are constants in the translated functions: patch the comparison, or wrap the function.

## 5. Validate

Two hidden instances, deterministic inputs (a key script), simulated delay/jitter/loss/outage (docs/07). Pass when
both reach the play loop, both show the same thing frame by frame, and outages only produce waiting.

Help wanted: a tool that scans a ROM and lists candidate SIO driver functions automatically.

# Game Boy / Game Boy Color serial port

Summary in our own words; the full reference is Pan Docs (see [references](references.md)).

## Registers

- `SB` (0xFF01): the byte to send; after the transfer, the byte received.
- `SC` (0xFF02): bit 7 starts a transfer (and reads 1 while it runs); bit 0 selects the clock: 1 = this Game Boy
  drives the clock ("master"), 0 = the other one does ("slave"). On the Color, bit 1 selects the fast clock.
- Serial interrupt (IF/IE bit 3) when the 8 bits have shifted.

## A transfer

Both sides shift one bit out and one bit in per clock edge; after 8 clocks each holds the other's byte. With the
internal clock the master shifts at 8192 Hz (about 1 KB/s; the Color's fast mode goes up to 32x). The slave does
nothing on its own: its byte is exchanged whenever the master clocks.

## Why it is hard online

- One exchange per byte, synchronous. Over a network every byte would cost a round trip.
- Masters often start a transfer and **poll** `SC` or wait for the interrupt, with timeouts measured in frames or
  less. Slaves often leave a byte "armed" and expect it to be taken at any moment.
- Many games (Pokémon Gen 1-2, Tetris, F-1 Race...) use byte-level handshakes (sync bytes, "ready" patterns) whose
  timing is part of the protocol.

## What works

- **Emulator lockstep in one process** (two cores sharing a clock): perfect, but only local.
- **Protocol-level relays**: understand the game's packets (e.g. a Pokémon trade's data blocks) and exchange them
  as messages, faking the byte stream locally (TCPoke's approach on real hardware).
- **The GBANoCable approach** (docs/04): find the routine that sends/receives a *block* and replace the transport
  there; the byte loop never touches the network.

Help wanted: per-game notes for GB/GBC titles (where the block routines are, block sizes, handshake bytes).

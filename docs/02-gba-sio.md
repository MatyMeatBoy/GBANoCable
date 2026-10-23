# Game Boy Advance serial (SIO)

Summary in our own words; the authoritative reference is GBATEK (see [references](references.md)).

## Registers (I/O at 0x04000000 +)

| Offset | Name | Use |
|---|---|---|
| 0x120-0x126 | SIOMULTI0-3 | Multiplay: the word each player sent in the last transfer (0xFFFF if absent) |
| 0x120 / 0x120-0x123 | SIODATA32 | Normal 32-bit mode data |
| 0x128 | SIOCNT | Mode, baud rate, start bit, error bit, player id (multiplay), IRQ enable |
| 0x12A | SIOMLT_SEND / SIODATA8 | Multiplay: this console's word; Normal 8-bit data |
| 0x134 | RCNT | Selects SIO modes vs. JOY BUS / general purpose |
| 0x140-0x15A | JOYCNT, JOY_RECV, JOY_TRANS, JOYSTAT | JOY BUS (GameCube link) |

## Modes

- **Normal (8 or 32 bit)**: like the Game Boy: a master clocks, the slave shifts. Used by GB-compatible links,
  the e-Reader, many adapters, and fast 2-player games.
- **Multiplay (16 bit, up to 4 players)**: the one most GBA link games use. The *parent* (player 0, the console
  whose SI line is low and SD high at rest) starts each transfer; every console's `SIOMLT_SEND` word is broadcast
  and lands in `SIOMULTI0-3` of every console, in player order. Baud 9600-115200. One IRQ per transfer. The
  player id is read from SIOCNT.
- **UART**: RS-232-like, rarely used by games.
- **JOY BUS**: the GameCube-GBA link (Dolphin integrates mGBA for it, clock-synced).
- **General purpose (GPIO)**: the four lines as plain I/O (some accessories).

## The multiplay contract a game relies on

- Every transfer, every connected player's word arrives in the same order on every console (including its own).
- The parent decides the pace (usually several transfers per frame, driven by a timer after VBlank).
- Absent players read 0xFFFF. Errors are flagged in SIOCNT.

The GBANoCable slot engine keeps exactly this contract at the **packet** level: every slot, every player's packet,
in player order, the same on every machine.

## Wireless adapter

The GBA Wireless Adapter talks to the GBA over an SPI-like link (Normal 32-bit mode with commands). Its radio side
is poorly documented; the best public work is afska's gba-link-connection (`docs/wireless_adapter.md`). Games using
it (Pokémon FireRed/LeafGreen/Emerald...) also have a packet layer above the adapter commands: the same approach
applies there.

# References

Hardware

- GBATEK (Martin Korth): GBA/NDS technical reference, serial and JOY BUS chapters. https://problemkaputt.de/gbatek.htm
- Pan Docs: Game Boy technical reference, serial data transfer. https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html
- afska / gba-link-connection: modern GBA C++ link libraries (cable, wireless, universal, SPI, GPIO) and the best
  public Wireless Adapter documentation. https://github.com/afska/gba-link-connection
- Wireless Adapter notes in that repo: https://raw.githubusercontent.com/afska/gba-link-connection/master/docs/wireless_adapter.md
- Hackaday on gba-link-connection: https://hackaday.com/2024/02/29

Emulators and projects

- mGBA lockstep link and the netplay discussions: https://forums.mgba.io/showthread.php?tid=4906 ,
  https://github.com/mgba-emu/mgba/issues/2379 , mGBA splitscreen libretro core: https://docs.libretro.com/library/mgba_splitscreen/
- Dolphin's Integrated GBA (mGBA clock-synced to the GameCube, works with netplay): https://dolphin-emu.org/blog/2021/07/21/integrated-gba/
- SameBoy's link cable emulation: https://github.com/LIJI32/SameBoy/issues/18
- TCPoke (Pepijn de Vos): Gen 1-2 Pokémon trades and battles over the internet on real hardware: https://blog.oshpark.com/tag/pokemon/
- stacksmashing's online Game Boy link adapter (2021): https://techcrunch.com/2021/05/17/hardware-hacker-brings-online-multiplayer-to-the-original-game-boy/
- Afterplay's online link (host runs both games): https://afterplay.featurebase.app/help/articles/1544991-link-cable

Netcode

- GGPO (rollback): https://www.ggpo.net/ and https://github.com/pond3r/ggpo
- GGRS (rollback in Rust): https://github.com/gschup/ggrs
- Valve GameNetworkingSockets (open source, NAT traversal): https://github.com/ValveSoftware/GameNetworkingSockets

SDK link libraries, as documented by decompilations

- MultiSio (the SDK's Multiplay library): Shining Soul II uses it (SS2Decomp `docs/LINK_CABLE.md`); a readable C
  source with the same layout is in the Sonic Advance 2 decompilation (`src/multi_sio.c`):
  https://github.com/SAT-R/sa2
- MusicPlayer2000 ("m4a") sound driver in C and assembly, as in the pret decompilations (`src/m4a.c`,
  `src/m4a_1.s`): https://github.com/pret/pokeemerald

Porting

- OptiPort (the author's method for debugging and optimising ports; its online module states the general contracts
  that the chapters here apply to link games): https://github.com/MatyMeatBoy/OptiPort
- x32 ABI (x86-64 code with 32-bit pointers): https://en.wikipedia.org/wiki/X32_ABI
- clang's `arm64_32` target (AArch64 code with 32-bit pointers, Mach-O objects): LLVM's AArch64 back end,
  https://llvm.org/

Games

- ReKHCoM (Kingdom Hearts: Chain of Memories decompilation port, first GBANoCable implementation):
  https://github.com/MatyMeatBoy/ReKHCoM
- SS2Decomp (Shining Soul II, matching decompilation; link notes in `docs/LINK_CABLE.md`, `LINK_PLAYER_PACKET.md`,
  `LINK_MESSAGES.md`): https://github.com/MatyMeatBoy/SS2Decomp
- ShiningSource (Shining Soul II native port; the online layer of chapters 09-13 and the port of chapter 15:
  `runtime/core/netlink.c`, `relay/`, `docs/ARCHITECTURE.md`, `docs/OPTIPORT.md`, `docs/RELEASE_*.md`):
  https://github.com/MatyMeatBoy/ShiningSource

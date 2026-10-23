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

Games

- ReKHCoM (Kingdom Hearts: Chain of Memories decompilation port, first GBANoCable implementation):
  https://github.com/MatyMeatBoy/ReKHCoM
- SS2Decomp (Shining Soul II): https://github.com/MatyMeatBoy/SS2Decomp

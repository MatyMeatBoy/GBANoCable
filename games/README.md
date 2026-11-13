# Games

| Game | Platform | Link style | Notes | Status |
|---|---|---|---|---|
| Kingdom Hearts: Chain of Memories | GBA | lockstep on inputs (2 players) | [sheet](kingdom-hearts-chain-of-memories.md): slots with an agreed delay, rollback in battle | online beta, played by people (ReKHCoM) |
| Shining Soul II | GBA | state replication + stop-and-wait messages (2-4 players) | [sheet](shining-soul-2.md): direct transport, one ordered message channel, reconnect; native port | played by people; measured with scripts on one machine (ShiningSource) |
| Pokémon Ruby/Sapphire/Emerald, FireRed/LeafGreen | GBA | ? | cable + wireless adapter; pokeemerald / pokefirered decompilations document the link code | help wanted |
| Mario Kart: Super Circuit | GBA | ? | | help wanted |
| The Legend of Zelda: Four Swords | GBA | ? | 2-4 players | help wanted |
| Pokémon Red/Blue/Yellow, Gold/Silver/Crystal | GB/GBC | ? | byte-level protocol; pokered/pokecrystal decompilations; TCPoke | help wanted |
| Tetris | GB | ? | classic 2-player byte protocol | help wanted |

Use [TEMPLATE.md](TEMPLATE.md) for a new game; how to fill it is in [CONTRIBUTING.md](../CONTRIBUTING.md). The first
field to settle is the **link style** ([docs/09](../docs/09-multi-sio-anatomy.md)): it decides the transport.

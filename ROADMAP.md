# Roadmap

1. **Done**: packet slots, never-cut policy, adaptive buffer (reference library + test); ReKHCoM online alpha over
   Steam (Spacewar 480), direct UDP and a relay.
2. **State hashing and repair**: hash the shared state every N frames inside the slot stream; on mismatch the
   parent sends its snapshot (savestate) instead of cutting. Same for a player who restarts and rejoins.
3. **Rollback** for play loops that exchange inputs (KHCoM's VS battle does): predict the others' input, simulate,
   restore and re-simulate on misprediction; removes the buffer's delay.
4. **Crossplay**: the relay transport compiled for Android, Switch/Vita homebrew, Linux; room codes typed in game.
5. **NAT traversal without a relay**: STUN/ICE (GameNetworkingSockets, libjuice), relay as fallback.
6. **More games**: Shining Soul II (GBA, 4 players), Pokémon Gen 3 (cable and wireless adapter), Four Swords,
   GB/GBC titles via their block routines.
7. **Tooling**: ROM scanner for SIO drivers, trace diff tool, a test harness for recomps.

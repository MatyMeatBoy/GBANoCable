# Roadmap

1. **Done**: packet slots, never-cut policy, adaptive buffer (reference library + test); ReKHCoM online over Steam
   (Spacewar 480), direct UDP and a relay; pairs on the same link frame with one agreed delay; rollback on packet
   slots (ReKHCoM battle); a desync check on final frames with a skip for decided rounds (ReKHCoM + reference
   library); a hardened public relay with a room list (tools/relay.py); lobbies with room names, version and region
   (ReKHCoM v2.5-online-beta); local relayed keys so rollback never revises packets (0 desyncs 32-bit vs 64-bit).
2. **State hashing and repair**: hashing is done in ReKHCoM (0 desyncs measured). Repair by sending the parent's
   snapshot only works when every machine stores the world the same way. Caveat found in KHCoM: each console keeps
   the battle from its own side (local player = "main" work area), so a snapshot must be side-swapped before the
   other machine can load it. Document per game whether the state is side-relative.
3. **Rollback**: done in ReKHCoM (docs/04). Next: a game-agnostic rollback helper in the reference library
   (snapshot ring, recorded inputs, replay, revision accounting), and a fixed-delay mode for `gnc_take` (the current
   adaptive smoothing hands groups on different game frames on each machine, which docs/04 shows breaks state
   machines that run while waiting).
4. **Crossplay**: Windows 32/64 and Linux done in ReKHCoM, typed room names on 64-bit hosts; next: verify Android
   against PC on real networks, Switch/Vita homebrew, and long-distance matches (only local and one external network
   tested so far).
5. **NAT traversal without a relay**: STUN/ICE (GameNetworkingSockets, libjuice), relay as fallback.
6. **More games**: Shining Soul II (GBA, 4 players), Pokémon Gen 3 (cable and wireless adapter), Four Swords,
   GB/GBC titles via their block routines.
7. **Tooling**: trace diff (tools/pairdiff.py) and an impaired local relay (tools/relay.py) done; next: ROM
   scanner for SIO drivers, a test harness for recomps, a relay that also records the confirmed pairs (spectators,
   replays).
8. **Relay**: rejoining a seat from a new address without waiting for the lease (needs a per-seat token handed out
   by the relay, not a password), and a web transport for browser builds.

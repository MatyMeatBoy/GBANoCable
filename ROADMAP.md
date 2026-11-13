# Roadmap

Done means: built and checked as the linked chapter says, with the limits it states. Nothing below was measured over
a real network unless a line says so.

## Done

1. **Lockstep games** (ReKHCoM): packet slots, never-cut policy, pairs on the same link frame with one agreed delay;
   rollback on packet slots; a desync check on final frames with a skip for decided rounds; local relayed keys so
   rollback never revises packets (0 desyncs 32-bit vs 64-bit); online over Steam (Spacewar 480), direct UDP and a
   relay; a hardened public relay with a room list (`tools/relay.py`); lobbies with room names, version and region.
   Reference: `reference/src/gbanocable.c`.
2. **State-replication games** (ShiningSource, Shining Soul II, 2-4 players): reading the game's protocol first
   ([docs/09](docs/09-multi-sio-anatomy.md)); the direct transport; the reliable message channel in one order
   through the host; the hole fix ([docs/10](docs/10-direct-transport-and-messages.md)); standing characters,
   reconnect with replay, the lock-out at barriers ([docs/11](docs/11-departures-and-reconnect.md)); rooms hashed
   with ROM, rules number and transport; a relay with seats and tokens and an outage simulator
   ([docs/13](docs/13-relay.md)). Reference: `reference/src/gnc_direct.c` with a test that fails without the hole
   fix.
3. **A native port from a decompilation** on Windows x64, Linux x64, Android and ARM64 with identical results
   ([docs/08](docs/08-decomp-to-native-port.md), [docs/15](docs/15-native-port-mechanisms.md)).
4. **Tooling**: trace diff (`tools/pairdiff.py`); an impaired local relay with lag, jitter, loss and outages
   (`tools/relay.py`).
5. **The skill**: [SKILL.md](SKILL.md), [verified lessons](docs/verified-lessons.md), game sheets with test status.

## Open, in the order it would help most

1. **Real networks.** Every ShiningSource figure comes from one machine and a relay that simulates the network.
   Wanted: reports of real sessions (which game, how many players, where, what happened), an address that changes
   mid-session, phones. Template: the "Test status" block of [games/TEMPLATE.md](games/TEMPLATE.md).
2. **A departed player blocks "every player" conditions that are state** (a level exit, a portal): the standing
   character is never in the zone. Measured, not fixed ([docs/11](docs/11-departures-and-reconnect.md), section 5).
3. **Return behind a barrier.** Today the player is locked out. The alternative is to resynchronise it at the next
   map set-up with the join-in-progress pieces.
4. **Join in progress** is a prototype measured on one machine ([docs/12](docs/12-join-in-progress.md)): the town,
   maps with exits and bosses, two newcomers at once, a departed player's seat, flags that outlive a map change.
5. **A static recompilation that follows [docs/14](docs/14-recomp-playbook.md).** The playbook is derived from the
   port; nobody has run the direct transport or the message channel in a recomp through those steps. The local-peer
   variant of the message channel (no code inserted in the game) is untested.
6. **Host migration**: not possible in the games studied (one parent owns enemies and items, seats cannot be
   renumbered). Document a game where it is.
7. **Reference library**: giving a player up and reviving it, rooms and stream numbers in `gnc_direct`; a UDP
   example that runs two processes through `tools/relay.py`; a game-agnostic rollback helper (snapshot ring,
   recorded inputs, replay, revision accounting) and a fixed-delay mode for `gnc_take` (its adaptive smoothing
   hands groups on different game frames on each machine, which docs/04 shows breaks state machines that run while
   waiting).
8. **State hashing and repair**: hashing is done in ReKHCoM (0 desyncs measured). Repair by sending the parent's
   snapshot only works when every machine stores the world the same way. Caveat found in KHCoM: each console keeps
   the battle from its own side, so a snapshot must be side-swapped before the other machine can load it. Document
   per game whether the state is side-relative.
9. **Relay**: `tools/relay.py` still has no way to rejoin a seat from a new address before the lease expires. The
   C relay of ShiningSource does it with a per-player token, which is visible to the other members of the room
   ([docs/13](docs/13-relay.md), section 3): a relay that keeps the token to itself would be better. A web
   transport for browser builds. A relay that records the confirmed pairs or the message order (spectators,
   replays).
10. **NAT traversal without a relay**: STUN/ICE (GameNetworkingSockets, libjuice), relay as fallback.
11. **More games**: Pokémon Gen 3 (cable and wireless adapter), Four Swords, Mario Kart: Super Circuit, GB/GBC
    titles via their block routines. For each: first the link style ([docs/09](docs/09-multi-sio-anatomy.md)).
12. **Tooling**: a ROM scanner for SIO drivers and SDK library version strings; a test harness for recomps; a
    comparison of a port's frames against an emulator running the cartridge (the ports' reference hashes prove
    "the same on every build", not "the same as the cartridge").
13. **Crossplay**: Windows 32/64 and Linux done in ReKHCoM; Android against PC on real networks, Switch/Vita
    homebrew and long-distance matches are not verified.

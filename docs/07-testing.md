# Testing without two people

1. **Two hidden instances on one PC**: host on `127.0.0.1:N`, guest joining it. Drive both with a key script
   (frame ranges → buttons) so runs are repeatable.
2. **Simulators in the transport**: fixed delay (frames), jitter (random extra frames, out of order), loss (%),
   outage (no datagrams between two frames). ReKHCoM's names: `KHCOM_NETLINK_LAG`, `_JITTER`, `_LOSS`, `_DROP`.
3. **Trace**: log every delivered group of packets on both sides; the two logs must be identical sequences.
4. **Measure**: frames the play loop waited, buffer size over time.
5. **Desync check in the game**: every N play frames each side hashes the shared state (positions, HP, random
   generators, in player order) and sends it; compare by frame number. Force a difference once to prove it fires.
6. **Screens**: capture the same frames on both sides; the shared state (positions, HP) must match.
7. **Real test**: two people, two networks, one via mobile data; pull the Wi-Fi for 20 s mid-match.

## Reproducing a desync locally (ReKHCoM, 01/11/2026)

1. **A local relay with an impaired network**: `python3 tools/relay.py --port 47801 --lag-ms 50 --jitter-ms 30
   --loss 2 --seed 1`. Both games join it as on the internet; the impairment counts wall-clock time, not frames (a
   game that waits makes no frames, and a frame-counted delay deadlocks the test). Client-side simulators work too,
   as long as they also count time.
2. **Scripted keys for both players**: frame ranges to buttons, with attacks, movement and special moves on both
   sides at once (a calm battle has few rollbacks and hides bugs). Hidden windows, no audio.
3. **Different builds**: 32-bit against 64-bit (or two compilers, or one under an emulator): a cheap stand-in for
   two different devices.
4. **Trace every played pair** (`pair <frame> <words>`), replays included. Compare the *final* pair of each frame
   on both sides: `python3 tools/pairdiff.py host.log guest.log`. It prints the first differing frame and which word
   columns differ: that points at the mechanism (e.g. only relayed words) long before a state hash does.
5. **Log the state hash with its link frame** and the readable state behind it (positions, HP, random state), so a
   mismatch says *what* differs. Equal positions and HP with a different random state at a round's end: the round
   is decided (skip it, see docs/04). A difference in positions: real.
6. **Replay determinism**: force rollbacks without any wrong guess and compare each replayed frame byte for byte
   with its first run; report the heap block holding the first different byte. Port or tool state that lives in
   game memory shows up here first: move it out of the snapshot.
7. **Count revisions** (packets changed by a replay). In a play loop that exchanges only inputs it must be 0.

## Small traps

- **Unsigned wait timers.** A "waiting since" stamp stored as `now | 1` (so 0 can mean "not waiting") can be one
  millisecond ahead of `now`; `now - start` then wraps to about 49 days and the game gives up at once. Clamp with a
  signed difference.
- **Test lobbies with no accounts**: two hidden games and a local room server; one opens a room, the other lists,
  enters, and they play.
- **Packet tests do not certify a battle.** A relay that creates, lists and forwards is not proof that two games
  stay in sync through it: always finish with two real game processes and a state check.

Reference results (ReKHCoM): see the README and docs/04.

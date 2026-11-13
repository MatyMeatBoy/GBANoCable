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

## Testing a state-replication game (ShiningSource, 12-13/11/2026)

Players of such a game are not on the same frame, so "the two logs must be identical sequences" no longer applies
to blocks. What is compared instead, and the rig around it (all headless, on one machine):

1. **Players**: 2 to 4 copies of the released program, no window, each in its own folder, started a second apart,
   driven by a script of `frame:buttons` read **from a file**. The start of the script takes them through title and
   lobby; after that each player gets its own shift of the script (so they do not all stand on one spot), a walk
   pattern with pauses (movement to see, rest to compare), or a "walk to the nearest enemy and strike" mode.
2. **A relay of its own per run**, on its own port, with the knobs of [docs/13](13-relay.md): loss, lag, jitter,
   and an outage for one seat (or all) at a chosen time. Several runs side by side need only different ports and
   folders. Never a public relay.
3. **Logs**, per player: what the program said about the session (connected, timed out, back); every message
   handed over and every message dispatched (clock, sender, kind, size, hash of the contents); four times a second,
   at the same moments of the clock, the state bytes it was given of every seat; the map it is on and the barrier
   tokens it holds; the live enemies as it has them.
4. **A report that asks the same questions of every run** and prints one RESULT line: every program ran to its last
   frame and stayed in the session; the same messages in the same order on every player; of every sender exactly
   what it handed over, none twice, none missing; all dispatched; each player's view of every other moving again
   and equal whenever the other stood still; nobody counted as departed; everybody on the same map; no barrier
   token left over. A run in which all hold is "whole". With many runs, a table of rates per case.
5. **Cases, not one run**: who is cut (a player, the host, everybody), for how long (below and above every timeout:
   3, 10, 30, 75 s), when (in the field, before a change of map, at a barrier, inside the next map), how many
   players, a second outage in the same session, a bad network besides, both builds. Run each several times and
   **write down the count**: "60 of 60" and "one run" are different claims.
6. **Measure before fixing.** The outage rig was built first and said 0 of 36; that number is what made the fix
   worth trusting at 60 of 60.
7. **Traffic on every stream**: a numbered test message with a tag the game ignores, from every player every N
   frames, when the script reaches a place where only the host has anything to say. And a way to inject one of the
   game's own messages at a frame, to reach what buttons alone do not (a change of map).
8. **Mixed versions**: one player's program replaced by the previous build, when a change is meant to stay
   compatible.

The same checks in miniature, with simulated players and no game: `reference/tests/test_direct.c`.

What this rig cannot say: anything about a real network (the outage is the relay not forwarding), about people
(scripted players that press the same buttons hid the very problems that made the first transport unplayable), or
about places the script does not reach. List those in the release notes.

## Small traps

- **Unsigned wait timers.** A "waiting since" stamp stored as `now | 1` (so 0 can mean "not waiting") can be one
  millisecond ahead of `now`; `now - start` then wraps to about 49 days and the game gives up at once. Clamp with a
  signed difference.
- **Test lobbies with no accounts**: two hidden games and a local room server; one opens a room, the other lists,
  enters, and they play.
- **Packet tests do not certify a battle.** A relay that creates, lists and forwards is not proof that two games
  stay in sync through it: always finish with two real game processes and a state check.
- **Consistency tests do not certify the design.** A transport can deliver exactly what it promises and the promise
  can be wrong for the game ([docs/10](10-direct-transport-and-messages.md), section 1).
- **Speeding the game up and a relay that holds packets do not combine**: the game counts its waits for the other
  players in frames. Run tests with lag in real time; speed up only the stretch before the part under test, and
  never past what those frame counts allow.
- **A wrapper script can drop settings.** A long input script in an environment variable was silently dropped above
  about 32 KiB and two runs were judged on stale logs. Long data goes in a file; delete or rename old logs before a
  run; make the program print what it loaded.
- **Stop only your own processes**, by the process ids the script started, and give every run its own port.
- **A program that stood still is not a silent network.** A test machine under load stalls processes; a stall of
  0.2 s at eight times the speed was enough to run out one of the game's frame-counted waits. Either the layer
  forgives its own stalls or the test must not overload the machine; say which when a run fails.

Reference results (ReKHCoM): see the README and docs/04.

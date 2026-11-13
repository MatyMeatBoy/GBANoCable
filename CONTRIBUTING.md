# Contributing to GBANoCable

Thank you! Useful contributions, in any language (English preferred for code and docs, Spanish welcome):

1. **Game sheets**: document a game's link layer and, if it has one, its online layer and port (below).
2. **Verified lessons**: a mechanism you met, with the check that showed it (below).
3. **Testing reports**: real sessions across networks. This is what the project lacks most.
4. **Hooks**: a working integration in a decompilation, recompilation or emulator (link to it; keep game-specific
   code in that project). A recomp that follows [docs/14](docs/14-recomp-playbook.md) would turn that chapter from
   a derived method into a verified one.
5. **Reference library**: see the roadmap (departure and revival in `gnc_direct`, a UDP example, rollback helpers).
6. **Tools**: ROM scanners that find SIO driver functions; trace comparers.

## Adding a game sheet

1. Copy [games/TEMPLATE.md](games/TEMPLATE.md) to `games/<game-name>.md` and add a row to
   [games/README.md](games/README.md).
2. Start with **link style** and its evidence: who writes the send buffer, who reads the receive buffers, whether a
   console reads its own block locally, what the play loop waits for. Everything else follows from it
   ([docs/09](docs/09-multi-sio-anatomy.md)). Without source, say how you traced it
   ([docs/14](docs/14-recomp-playbook.md), section 2).
3. For every statement say where it comes from: *code* (read from a decompilation or disassembly), *traffic* (seen
   in a trace), *measured* (a test: say players, runs, machine, how the network was simulated).
4. Describe messages **by role and by sender**, not as a copy of the game's tables. Addresses, names and behaviour
   only: no game code, no data tables, no text, no screenshots.
5. Fill "Test status" honestly, including **Not checked**. A sheet with a long "not checked" list is more useful
   than one without the section.
6. Leave fields you do not know empty rather than guessing; a partial sheet is welcome.

## Adding a verified lesson

A lesson goes into [docs/verified-lessons.md](docs/verified-lessons.md) as one table row with four parts:

- **Trigger**: what somebody sees (a symptom, in the words a tester would use).
- **Mechanism**: why it happens, in terms of the game or the transport.
- **Check**: the test, measurement or reproduced failure that showed it, with its numbers, or the check that now
  guards against it. If the cause was only read from code and never reproduced, say exactly that.
- **Limits**: one machine? simulated network? how many runs? what was not tried?

Rules for a lesson: it must change a decision somebody else will face; it states a mechanism, not a diary entry;
project-specific paths, ticket numbers and transient progress stay in that project. If a later experience
contradicts a chapter, fix the chapter in the same change and say what changed.

## Changing the reference code or the tools

Run everything before you send a change (commands in [reference/README.md](reference/README.md)):

```sh
cd reference
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_sim.c -o test_sim && ./test_sim
cc -std=c99 -O2 -Wall -Iinclude src/gbanocable.c tests/test_check.c -o test_check && ./test_check
cc -std=c99 -O2 -Wall -Iinclude src/gnc_direct.c tests/test_direct.c -o test_direct && ./test_direct
cd .. && python3 -m unittest discover -s tools -p "test_*.py"
```

A fix needs a test that fails without it (see how `test_direct --no-hole-fix` keeps the old behaviour reachable).
C99, no dependencies, no allocation, generic names: nothing of a particular game.

## Rules

Be kind, cite sources, keep the license (MIT) for what you write here. No ROMs, no BIOS, no game assets, no game
text. No private information: no e-mail addresses, server addresses, local paths or player names in documents,
logs or examples; write "a relay you run".

Maintainer: MatyMeatBoy.

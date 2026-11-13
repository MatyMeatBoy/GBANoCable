# The relay: protocol, rooms, seats, hardening and test knobs

[docs/06](06-transports.md) covers transports in general and the hardening of a public relay. This chapter is the
relay as a part of the design for a 2-4 player game: what it must know (almost nothing), what the room is made of,
and the knobs that turn it into the test bench. Two implementations are described:

- [tools/relay.py](../tools/relay.py) in this repository (Python, standard library, unit-tested, hardened for a
  public address, a room list);
- ShiningSource's `relay/ss_relay.c` (one C file, rooms of four seats with tokens; the one the outage measurements
  of [docs/10](10-direct-transport-and-messages.md) and [docs/11](11-departures-and-reconnect.md) were made with).

Use **a relay you run**. One process and one port per game and protocol: games never share a room namespace.

## 1. What a relay is for, and what it proves

It gets datagrams between home routers without anybody opening a port, and it gives players seats. It knows nothing
about the game and keeps no data; a room exists while somebody is in it. It does not reduce the distance between
players, and a relay that creates rooms and forwards is **no proof** that two games stay in step through it.

## 2. The room is more than its name

Players must only meet if they run the same game under the same rules. In ShiningSource the room id is a hash of:

- the room name the players typed;
- **the cartridge's contents** (the whole ROM): another region or revision lands in another room;
- a **`rules` number** in the online layer, raised whenever players must agree on something new (4 when
  the direct transport and per-sender messages came, 5 with the one order of messages);
- **the transport in use** (the stepped and the direct transport do not mix);
- optional settings that every player must share (join in progress).

A mismatch then produces "nobody is in my room", never a session that drifts apart later. Cost, *measured*: hashing
a 16 MiB ROM makes the frame that opens the link take 29 ms once.

When a change is **compatible on the wire but decided by one side** (reconnect is the host's decision), do not raise
the number; say in the release notes who has to update first.

## 3. Seats

- The first player of a room gets the first seat and is the game's parent (the host). The relay gives a newcomer
  the lowest free seat; the game needs seats without gaps ([docs/09](09-multi-sio-anatomy.md), section 3).
- The relay **stamps the seat** on every forwarded datagram, so a player cannot speak for another seat.
- A seat is freed by a goodbye, or after 30 s of silence.
- **Coming back.** Each player has a random token. A hello with the token of an occupied seat is that player: the
  relay updates the seat's address (a router that changed the mapping, Wi-Fi to mobile data). A hello that names
  the seat it had before gets that seat if it is free: so **restarting the relay does not end a session**; the
  players keep saying hello and are seated again within a quarter of a second (*stated in the relay's notes; a
  relay restart during play is in the port's design, the measured runs did not include one*).
- Being seated again by the relay is not being let back into the game. The players decide that by the **stream
  number** in the packets ([docs/11](11-departures-and-reconnect.md)): the same stream is a connection that came
  back, a new stream is a program that restarted.

Compare with `tools/relay.py`, which has **no tokens**: an occupied seat is never handed to another address before
its lease expires, so a player whose address changed waits out the lease. That is safe against anybody who knows
the room code; the token scheme is friendlier. *Read from the C relay's code, not tested:* it forwards packets
unchanged except for the seat stamp, so the token travels to the other members of the room, and a member could say
hello with another member's token. The token is therefore a convenience against accidents, not a secret against
people inside the room. A relay that strips or replaces the token when forwarding would close that.

## 4. The protocol, in words

**ShiningSource's relay.** Every packet starts with one fixed header: a magic, a type, the seat, the room id, the
token; the rest of the header and everything after it is between the players and the relay does not read it.

| Type | From | What the relay does |
|---|---|---|
| hello (with the room name) | a player, every two seconds; four times a second while it has no seat; twice a second while it hears nothing | finds or makes the room, seats the player (by token, then the seat it names, then the lowest free), answers *welcome* with the seat and the seats in use |
| data | a seated player, from its seat's address | stamps the seat, forwards to every other seat |
| goodbye | a seated player (sent twice) | forwards, frees the seat |
| ping | anybody | echoes the header: lets a client measure its way to the relay before a session; needs no room |

A fifth player's hello is not answered (room full). Packets longer than 1200 bytes are dropped, so the players'
layer never builds one.

**tools/relay.py.** `J seat code` joins or refreshes (every second), `O n` answers, `D payload` is forwarded, and
`A` / `L` / `R` announce and list public rooms by link protocol version ([docs/06](06-transports.md)). The game
datagram limit is 512 bytes by default; `--max-datagram 1200` for the direct transport of the reference library.

## 5. Hardening

What a relay on a public address needs is in [docs/06](06-transports.md#hardening-a-public-relay-rekhcom-vps-beta-30102026)
and implemented, with tests, in `tools/relay.py`: validate before keeping state, a monotonic clock, bounds on
rooms and on players per address, token buckets, no seat takeover, no amplification, consistent indexes, nothing
logged about players.

The C relay is smaller and, *read from its code*, has fewer defences: it validates the header's length and magic,
bounds rooms (512) and seats (4), only forwards data that comes from the seat's own address, never answers with
more bytes than it was sent, and logs one line per join and leave with the room's hash and the seat (no addresses,
no names). It has **no rate limiting** and no per-address bound. It runs as an unprivileged user or in an empty
container. Treat it as a relay for sessions among people who know the address, or port relay.py's limits to it
before exposing it widely.

A relay started by a player on their own PC ("host on this PC") should end by itself after a while without packets,
so it is not left running after the game; the host then has to make one UDP port reachable from outside.

## 6. The relay as the test bench

The relay is where a bad network is simulated, because every packet passes through it and the players' programs
stay exactly as released.

| Knob | ss_relay (environment) | relay.py (option) | Notes |
|---|---|---|---|
| loss | percent of forwarded packets dropped | `--loss` | |
| lag | milliseconds each packet is held | `--lag-ms` | **wall-clock time, never frames**: a game that waits makes no frames, and a frame-counted delay deadlocks the test |
| jitter | up to that many ms more, at random | `--jitter-ms` | also reorders |
| outage | `player:start:length` in ms, several separated by commas | `--outage SEAT:START:LENGTH[,...]` in seconds | from *start* after that seat was first taken, for *length*: everything from that player's address is thrown away unread (its hellos too) and nothing is sent to it |
| repeatable | | `--seed` | |

Details that mattered:

- **The outage follows the address that first took the seat**, not the seat: the seat may be freed in between (30 s
  of silence) and must be given back to the same player.
- **Cut everybody** (an outage for every seat at once) is the case "the relay cannot be reached": every player must
  conclude that its own connection is gone and give nobody up.
- The relay prints when a window opens and closes **with the same clock the players' logs use** (one machine), so
  the report can place "cut" and "back" in every log.
- A held packet whose receiver is cut off when it is due is dropped too.
- Lag in the relay needs the test to run in real time: sped-up scripted sessions and a relay that holds packets do
  not combine (the game counts its waits in frames).
- Each test run gets **its own relay on its own port**, so several can run side by side; never test against a
  public relay.

## Limits

- An outage here is the relay not forwarding. It is not a route change, a NAT rebinding, a suspended phone or a
  half-open path (one direction only): none of those was simulated or tried.
- No measured capacity for either relay. The limits in relay.py are safety bounds.
- No authentication or encryption in either: a room name or code is a public identifier, not a password.

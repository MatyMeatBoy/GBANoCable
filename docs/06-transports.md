# Transports

The slot engine only needs "send this datagram to the others" and "give me the next datagram". Behind that:

## Direct UDP

The host binds a port; the guest sends to its address. Simple, lowest latency, but the host must open/forward the
UDP port on the router. Let the host accept a guest coming back from a new address after a silence (Wi-Fi to
mobile data).

## Steam (for development: Spacewar, AppID 480)

Valve's test app 480 lets anyone with Steam use lobbies and P2P networking without an AppID of their own; source
ports use it while developing. It is not meant for a published product (everyone shares it; get an AppID through
Steam Direct to publish).

- Load `steam_api.dll` at run time (no SDK needed to build): `SteamAPI_InitSafe`, manual callback dispatch
  (`SteamAPI_ManualDispatch_*`), set `SteamAppId=480` in the environment before init.
- Host: `ISteamMatchmaking::CreateLobby` (public, 2-4 members), tag it with lobby data (e.g. `game=<id>`,
  `protocol=<version>`). Guest: `RequestLobbyList` with that string filter, worldwide distance, prefer a lobby whose
  owner is a Steam friend; `JoinLobby`; the owner is the peer. A friend's "Join game" (GameLobbyJoinRequested) works
  too.
- Data: `ISteamNetworkingMessages::SendMessageToUser` (unreliable, no Nagle, auto-restart broken sessions) and
  `ReceiveMessagesOnChannel`; accept session requests. Valve's relays traverse NAT.
- Windows struct packing for callbacks is 8 (VALVE_CALLBACK_PACK_LARGE); Linux/macOS use 4.

## A relay on a VPS (crossplay)

[tools/relay.py](../tools/relay.py): players send `J <role> <room code>` every second and `D <payload>`; the relay
forwards payloads between the members of a room. Any platform with UDP can use it (PC, Android, Switch homebrew,
Linux), no ports to open. Bandwidth is tiny (a few KB/s per player); a 1 vCPU VPS for a few USD a month serves many
rooms. Re-sending the room every second keeps the seat; a player whose address changed gets the seat back once the
old address's lease expires (see "Hardening a public relay" below: seats are never taken over earlier). Seat 1 is the
host; `--seats` sets the room size (2-4).

For a 2-4 player game the relay also decides seats, and it is the place to simulate a bad network and outages:
[docs/13](13-relay.md) (rooms hashed with the ROM and a rules number, seats with tokens, what a relay restart does,
the test knobs).

Ideas welcome: STUN/ICE hole punching with relay fallback, WebRTC data channels for web builds.

## Lobbies: one provider, several backends (ReKHCoM, 23/10/2026)

Players should not type addresses. ReKHCoM shows a lobby screen before the link menu (public rooms, open one, enter
one, or the local cable) and keeps the backend behind one small interface: list, open, enter, "who am I".

- **Steam (free, PC):** Valve's test app (Spacewar, AppID 480) gives lobbies, NAT traversal and relays without an
  AppID of your own; many community ports use it for testing. Tag the lobbies with your game and the link protocol
  version (`rekhcom=link-3`), ask for lobbies with a free slot, and store the host's name and cartridge region as
  lobby data. "Signed in" = `SteamAPI_Init` works and `BLoggedOn`; otherwise the screen says to open Steam, with a
  retry (Steam may be opened while the game runs).
- **Your own room server (VPS, any platform with UDP):** extend the relay with three datagrams: `A` (a host
  announces its public room every second while it waits: protocol version, region, room code, name), `L` (list
  request with the version) and `R` (the rooms of that version still waiting, announced in the last 5 s). Picking a
  room just yields the ordinary relay spec with its code, so the link code never knows there was a lobby.
- **Same version only:** list rooms by link protocol version; show rooms of another cartridge region but do not let
  them be entered (the cartridges never linked across regions either).
- A test that covers it all without accounts: two hidden games and a local room server; one opens a room from the
  lobby screen, the other refreshes, finds it and enters, then they battle (ReKHCoM `port/tools/netlink_lobby.sh`).

## Hardening a public relay (ReKHCoM VPS beta, 30/10/2026)

A relay on a public address will receive garbage, scans and spoofed datagrams. What the ReKHCoM beta relay does, now
in [tools/relay.py](../tools/relay.py) (with unit tests):

- **Validate before keeping state.** Check length, kind, seat and room code of every datagram before creating any
  per-sender entry; game datagrams only from addresses that hold a seat.
- **Monotonic clock** for every lease (wall-clock jumps must not expire or revive rooms).
- **Bounds:** rooms, players behind one IP (allow some NAT sharing), tracked IPs, datagram size; a token bucket per
  IP and one global.
- **No seat takeover.** An occupied seat is never handed to another address before its lease expires (30 s). The
  cost: a player whose address changed (Wi-Fi to mobile) waits for the lease. The older behaviour (the room follows
  the newest address) let anyone who knew a room code kick a player. A per-player token lets the relay follow an
  address change at once (ShiningSource's relay does); see [docs/13](13-relay.md), section 3, for what such a
  token does and does not protect.
- **No amplification.** A list answer is never larger than its request: clients pad the list request with zeros to
  their receive buffer (512 bytes), and an old 2-byte request gets an empty list. Spoofed small requests then cannot
  turn the relay into an amplifier.
- **Consistent indexes.** A player moving to another room leaves the old one; the reverse index (address to seat)
  is checked against the room before forwarding.
- **Honest claims.** No authentication or encryption is implied; a room code is a public identifier, not a password.
  Nothing about players is logged. Limits are safety bounds, not a measured capacity.
- **Deployment:** standard library only, run unprivileged in a small container (one replica: rooms live in memory,
  a restart drops them), one process and port per game and protocol so games never share a room namespace, pinned
  source with a recorded hash so the service can be rolled back.

## Lobby details players notice (ReKHCoM v2.5-online-beta, 01/11/2026)

- **Rooms carry a name.** The host types it (an on-screen keyboard on pads and phones) or confirms an offered
  "<player>'s room". 24 bytes of UTF-8, only what the game's font can draw, no control characters. Small host data
  can ride in the name with a separator (ReKHCoM appends the host's level: `name~level`) without changing the wire
  format; parse it out before showing the name.
- **The lobby goes where the game's own flow expects it.** ReKHCoM first opened the lobby on its Link menu, before
  loading a save file: players picked a room, then loaded, and the order confused them. Now Link opens the game's
  own Load screen, then the room list; picking a room goes straight to the connect screen; cancelling the wait comes
  back to the list.
- **Show it once per visit.** Re-opening the lobby on every entry to the link screen (including the return from
  Load) lost the room already picked: the guest saw the lobby again, the host opened a new room with a new code and
  a new socket, and both waited in different rooms. Open it once; only a new visit from the title starts again.
- **Hide your own just-closed room** while the server still lists it (until its announcement goes stale).
- **Version and region in the list.** The list request carries the link protocol version; rooms of another
  cartridge region are shown but cannot be entered. Bump the version whenever the link rules change (ReKHCoM: 5,
  when each game started relaying the battle keys itself).

## Hosts and shims: every build must prove it has a socket

In recompilations the game often runs on a host layer (a 64-bit host loading a 32-bit game image, a console shim,
an Android activity). The transport is then a host service: the game asks the host for a UDP socket through a
bridge. ReKHCoM's 64-bit Windows host compiled that bridge with stand-ins (no socket), so online silently did
nothing there ("the room server does not answer") while the 32-bit build worked. Each host needs its own backend
(BSD sockets, Winsock with `ws2_32`), and each build kind needs an automated "open a socket, list rooms" smoke
test. Typed text for room names is the same story: an optional host channel (start text input, read the typed
UTF-8, stop) that old hosts answer with "not supported", so the game falls back to the offered name.

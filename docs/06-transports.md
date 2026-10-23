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
rooms. Re-sending the room every second lets the relay follow a player whose address changed.

Ideas welcome: STUN/ICE hole punching with relay fallback, WebRTC data channels for web builds, a room browser.

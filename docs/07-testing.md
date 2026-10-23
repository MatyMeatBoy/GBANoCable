# Testing without two people

1. **Two hidden instances on one PC**: host on `127.0.0.1:N`, guest joining it. Drive both with a key script
   (frame ranges → buttons) so runs are repeatable.
2. **Simulators in the transport**: fixed delay (frames), jitter (random extra frames, out of order), loss (%),
   outage (no datagrams between two frames). ReKHCoM's names: `KHCOM_NETLINK_LAG`, `_JITTER`, `_LOSS`, `_DROP`.
3. **Trace**: log every delivered group of packets on both sides; the two logs must be identical sequences.
4. **Measure**: frames the play loop waited, buffer size over time.
5. **Screens**: capture the same frames on both sides; the shared state (positions, HP) must match.
6. **Real test**: two people, two networks, one via mobile data; pull the Wi-Fi for 20 s mid-match.

Reference results (ReKHCoM): see the README.

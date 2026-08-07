# Security Policy

## Supported versions

This is a hobby project without a formal release train. Only the `main` branch
is supported — fixes land there, and you are expected to reflash from source.

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Use GitHub's private reporting instead:
[**Report a vulnerability**](https://github.com/HomeCritters/homecritters-firmware/security/advisories/new)
(Security → Advisories → Report a vulnerability). If that is not available to
you, contact the maintainer [@BrunoTCouto](https://github.com/BrunoTCouto)
privately.

Please include what you can: firmware commit, hardware, reproduction steps, and
what an attacker gains. Expect a first reply within a couple of weeks — this is
a spare-time project, not a staffed product. Coordinated disclosure is
appreciated; we will credit you in the advisory unless you prefer otherwise.

If the issue is in the Home Assistant integration, report it in the
[homecritters-ha-plugin](https://github.com/HomeCritters/homecritters-ha-plugin)
repository instead.

## Threat model, in plain terms

The device is a **LAN appliance**. It is designed to be safe on a home network
alongside untrusted-ish devices, not to be exposed to the internet.

What the firmware does defend:

- **A shared secret never travels the wire.** A 16-hex credential lives in NVS.
  On connect, the device sends `challenge:<nonce>` and the client answers with
  `HMAC-SHA256(token, nonce)`. With a client nonce the device proves itself
  back (`proof:`), so a device impersonating `critter.local` over spoofed mDNS
  cannot fool a paired client.
- **Nothing happens before auth.** No state, no commands and above all no
  microphone audio until the handshake completes; unauthenticated sockets are
  dropped after 5 s.
- **Pairing is supervised.** A 6-digit PIN shows on the physical screen (90 s,
  3 attempts). The handover is the only moment the token crosses the network.
- **Screenshots are authenticated.** `GET /shot.bmp` requires a single-use
  HMAC signature derived from a server-issued nonce — no secret in the URL.
- **The microphone has a hard gate at the source.** A BOOT tap mutes it in
  firmware; muted means no samples are captured, not merely dropped later.
- **Revocation is real.** "Revoke access" rotates the token and drops every
  connected client.

What it does **not** defend, by design:

- **No transport encryption on the LAN.** The WebSocket (port 81) and portal
  (port 80) are plaintext HTTP. Contents are visible to anyone who can sniff
  your network segment; the HMAC handshake protects the *credential*, not the
  *payload*. Outbound weather fetches do use verified TLS.
- **Physical access wins.** Anyone holding the device can read the pairing PIN
  off the screen, read the token from the Security menu, and use the serial
  console. Treat physical access as full access.
- **Do not port-forward this device.** There is no rate limiting, no account
  system and no hardening against a hostile internet. For remote access, use a
  VPN or an overlay network.
- **Flash contents are not encrypted** and secure boot is not enabled, so the
  NVS token is recoverable from a dumped chip.

Reports about "the WebSocket is unencrypted" or "the serial console has no
password" are known and intentional trade-offs, documented above. Reports about
**bypassing the auth handshake, the mic mute gate, the screenshot signature, or
the pairing PIN** are exactly what we want to hear about.

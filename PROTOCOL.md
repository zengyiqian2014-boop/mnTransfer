# Protocol v1

The sender creates a 256-bit secret from libsodium's OS-backed random source.
The `mn1.<base64url-relay>.<base64url-secret>` code is a bearer credential, not a
six-digit PIN. The relay is `wss://` with normal certificate validation; plaintext
`ws://` is accepted only for literal loopback addresses and localhost development.

Room ID = keyed BLAKE2b-256(secret, `mnTransfer/v1/room`). Peers connect to
`/v1/ROOM/send` and `/v1/ROOM/receive`. There is one connection for each role.
The relay sees the room ID, never the secret. It does not persist file contents.

Each receiver connection creates a fresh random 32-byte challenge. Direction
keys are independently derived with keyed BLAKE2b and the exact labels
`mnTransfer/v1/send` + challenge and `mnTransfer/v1/reply` + challenge. The sender
initializes a libsodium secretstream. Its public header and the echoed challenge
precede encrypted metadata, which also binds the challenge. Old sessions cannot
be resumed by replaying old ciphertext to a new connection.

Secretstream records are ordered and authenticated:

- `M` + UTF-8 JSON: basename, decimal byte length, lowercase SHA-256, challenge.
- `R`: reset a mismatched partial file after the prefix check, at most once.
- `D` + 64-bit big-endian offset + at most 65,536 file bytes.
- `F` with the secretstream FINAL tag: end of file.

Metadata must pass validation and user acceptance before opening the partial
file. Directory separators, Windows device names, control characters and unsafe
basenames are rejected. Received data must start at the exact expected offset,
fit within the authenticated size, and pass stream authentication.

Responses use libsodium XChaCha20-Poly1305 AEAD with fresh 24-byte nonces and the
independent reply key. `ready` carries the existing byte count and prefix SHA-256.
The sender independently hashes the same prefix before seeking. `ack` advances
the cumulative written byte count; decreasing acknowledgements never rewind the
sender and acknowledgements beyond sent data are rejected. The sender limits
unacknowledged data to 1 MiB. `done` is accepted only after authenticated FINAL.

The receiver streams the entire SHA-256, including existing prefix data. It
closes and renames the partial file only after the final digest matches. It then
sends authenticated completion. Existing destination files are never overwritten.
An abrupt disconnect retains the partial file and starts a fresh challenge and
stream on reconnect. Pairing, filenames and file content are not sent to telemetry.

This protocol is not an independently audited standard. A compromised endpoint,
someone with the complete code, or an attacker with write access to the chosen
destination directory is outside its confidentiality boundary. The relay can
deny service and observe traffic volume/timing. Backups are not supplied by this
program. Partial-file flushes are not a guarantee against physical disk failure.

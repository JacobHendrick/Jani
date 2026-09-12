# Node public-key representation

This first Phase 5 slice represents a public key as 32 bytes. It does not
generate keys, derive node IDs, validate curve encodings, authenticate peers,
or enable network traffic. No persistent format or guest syscall changes.

The C kernel calls `node_public_key_decode(bytes, length, out)`. A Zig
ReleaseSafe entry point rejects null pointers and lengths other than 32 before
reading input. It copies the key into a local value before writing the output,
so overlapping input and output are supported. Success returns 1; failure
returns 0 and leaves the output unchanged. The C structure and Zig extern
structure both contain only the byte array.

The caller supplies valid kernel buffers and keeps the input stable during
the call. Null rejection is not address validation: this is not a syscall for
arbitrary guest pointers, and it does not make concurrent DMA or writes safe.
The decoder retains no pointers. Key comparison operates on public bytes and
is not a constant-time primitive for secrets.

`make test-node-identity` runs the Zig tests and C-to-Zig tests, including
incorrect lengths, null pointers, failure atomicity, output guards, overlapping
buffers, and comparison differences at every byte. It is included in `make
test`. The C harness uses ASan/UBSan; the Zig body uses ReleaseSafe checks, not
C sanitizer instrumentation. `make node-identity-negative` builds a separate
mutant that accepts oversized input and requires the C suite to reject it.

Before enabling networking, the distribution design still needs to specify
key generation and storage, node-ID derivation, authenticated message framing,
replay protection, and how remote authority maps to local capabilities.

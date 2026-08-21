# Contributing

Jani is an open-source operating-system project with an evolving design. Small,
reviewable changes are easiest to test and explain.

## Before Coding

Open an issue before changing a trust boundary, persistent format, public ABI,
or major subsystem. Describe the problem, the invariant that must hold, and
how the change will be tested.

For a small bug fix, a pull request can be the first discussion.

## Checks

Run the checks that match the change. Before requesting review, run at least:

```sh
make check-tools
make test
make kernel
make iso
```

Boot-path changes should also be tested with `make run`. Include the useful
serial output in the pull request description.

## Code

- Follow the naming and layout already used in the surrounding files.
- Keep C freestanding and compatible with the existing warning flags.
- Use Zig for checked boundaries and WASM components where the repository
  already uses it.
- Explain why a non-obvious constraint exists. Do not comment code merely to
  repeat what it says.
- Add focused tests for malformed input, boundary values, and failure paths.
- Do not combine unrelated cleanup with a behavioral change.

Contributors own every line they submit.
Review it against the surrounding architecture, remove generic filler, verify
all APIs and dependencies, and be able to explain its behavior and tests.

## Security

Read [SECURITY.md](SECURITY.md). Report vulnerabilities privately. Pull
requests that weaken validation or isolation need a clear threat-model
explanation.

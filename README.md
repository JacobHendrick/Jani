# Jani OS

Jani is an experimental x86_64 operating system built to explore persistent
objects, capability-based access control, and WebAssembly components.

This repository is a learning project. It boots in QEMU and has working
kernel subsystems, but it is not ready to protect real data or run untrusted
code.

## Current State

The current kernel includes:

- Limine boot on BIOS and UEFI
- serial diagnostics, CPU exceptions, PIC, PIT, and PS/2 keyboard input
- physical and virtual memory managers plus a kernel heap
- a log-structured persistent object store on a VirtIO block device
- capability tables with derivation and revocation
- WAMR 2.4.5 in classic interpreter mode
- a generated C and Zig syscall ABI
- persistent WASM component state and bounded component mailboxes
- hosted tests, fuzz targets, a TLA+ model, and QEMU boot checks

WASM components currently execute through WAMR inside the kernel address
space. They are separated by runtime checks, not by x86 privilege rings or
separate page tables. Read [SECURITY.md](SECURITY.md) before testing modules
you did not write.

## Project Direction

The long-term design is described in [docs/architecture/blueprint.md](docs/architecture/blueprint.md).
Planned work includes scheduling, replay, component replacement, distribution,
semantic indexing, a graphical desktop, compatibility layers, and hardware
support. Those features are goals, not current security or compatibility
claims.

C is used for low-level kernel code. Zig provides checked boundary modules,
the component SDK, and WASM applications. Small x86_64 assembly files handle
CPU entry and interrupt transitions.

## Build

The supported development host is Fedora x86_64. The build expects:

- Zig 0.16.0 at `third_party/zig/zig`
- vendored Limine 12.4.0 at `third_party/limine/`
- `make`, `ld`, `xorriso`, and `qemu-system-x86_64`

Run:

```sh
make check-tools
make test
make iso
make run
```

`make run` starts QEMU with serial output attached to the terminal. Use
`Ctrl-a x` to exit QEMU.

Useful additional checks:

```sh
make verify-wamr
make fuzz-heap
make model-check
make model-check-negative
make zero-install-test
```

## Repository Guide

- `kernel/`: kernel, architecture, drivers, memory, objects, and WASM runtime
- `components/`: Zig WASM components used by the boot demo
- `sdk/`: generated and hand-written C and Zig component interfaces
- `idl/`: syscall interface definitions
- `tools/`: generators, hosted tests, fuzzing, and model-checking support
- [`docs/`](docs/README.md): architecture, design records, development notes,
  formal models, and archived implementation plans

See [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a large change.

## License

Jani's original source is licensed under `GPL-2.0-only`. See
[LICENSE](LICENSE). Files under `third_party/` remain under their respective
upstream licenses.

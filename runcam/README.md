## RunCam majestic manager build notes

The RunCam unit runs OpenIPC (Buildroot) with no package manager or compiler, so we have to cross‑compile on the laptop. We ship the `majestic_manager.c` source in this directory and build it for ARM using Zig’s self-contained toolchain.

### Why is `zig.tar.xz` in the repo?

`zig.tar.xz` is the official Zig 0.13.0 archive for macOS/arm64. Unpacking it (already done in this folder) creates `zig-macos-aarch64-0.13.0/`, which contains the `zig` binary we point to from the `Makefile`. Zig’s `zig cc` front-end bundles musl, binutils, and clang, so we can produce a static `arm-linux-musleabihf` binary without installing a full cross toolchain. If you ever need to recreate the folder, run:

```
cd runcam
tar -xf zig.tar.xz
```

You can delete the archive/extracted directory if you have another cross-compiler available; just override `ZIG` in the `Makefile` to point at your toolchain.

### Building

```
cd runcam
make
```

This uses `./zig-macos-aarch64-0.13.0/zig` to build `majestic_manager`. If you place Zig somewhere else, run:

```
cd runcam
make ZIG=/path/to/zig
```

The helper cache directories (`zig-cache/` and `zig-macos-aarch64-0.13.0/`) are ignored via `.gitignore`, so only the source files and resulting binaries you intentionally copy will be tracked.

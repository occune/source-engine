# Source Engine
[![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/build.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/build.yml) [![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml)
Discord: [![Discord Server](https://img.shields.io/discord/672055862608658432.svg)](https://discord.gg/hZRB7WMgGw)


Information from [Wikipedia](https://wikipedia.org/wiki/Source_(game_engine)):

Source is a 3D game engine developed by Valve.
It debuted as the successor to GoldSrc with Half-Life: Source in June 2004,
followed by Counter-Strike: Source and Half-Life 2 later that year.
Source does not have a concise version numbering scheme; instead, it was released in incremental versions.

Source code is based on the TF2 2018 leak. Don't use it for commercial purposes.

This project uses the waf build system. For waf-related questions, see https://waf.io/book

# Features:
- Android, OSX, FreeBSD, Windows, Linux (glibc, musl) support
- Arm support (except Windows)
- 64bit support
- Modern toolchains support
- Fixed many undefined behaviours
- Touch support (even on Windows/Linux/macOS)
- VTF 7.5 support
- PBR support
- BSP v19-v21 support (BSP v21 support is partial; Portal 2 and CS:GO maps work fine)
- MDL v46-49 support
- Removed useless/unnecessary dependencies
- Achievement system works without Steam
- Fixed many bugs
- Server browser works without Steam

# Current tasks
- Rewrite materialsystem for OpenGL renderer
- dxvk-native support
- Elbrus port
- Bink audio support (for video_bink)

# How to Build?
- [Build instructions (EN)](https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN))
- [Build instructions (RU)](https://github.com/nillerusr/source-engine/wiki/Source-Engine-(RU))

# IPv6 Support (Linux)

This fork adds **native IPv6 dual-stack multiplayer** on Linux. A single `AF_INET6`
socket with `IPV6_V6ONLY=0` accepts both IPv4 and IPv6 connections, so existing IPv4
clients and servers keep working while IPv6 works out of the box.

## What changed

- `netadr_t` extended with an `ip6[16]` field and a new `NA_IP6` address type.
  IPv4-mapped addresses (`::ffff:a.b.c.d`) are normalized back to `NA_IP`, so the
  rest of the engine treats them as plain IPv4.
- `net_ws.cpp` opens dual-stack sockets (`AF_INET6` + `IPV6_V6ONLY=0`) on POSIX.
  `getaddrinfo(AF_UNSPEC | AI_ADDRCONFIG)` replaces `gethostbyname` for hostname
  resolution.
- The address parser accepts bracketed IPv6 literals with a port, e.g. `[::1]:27015`.
- All IPv6 code is guarded with `#if defined(POSIX)`; Windows and X360 keep their
  original IPv4-only behavior and still compile unchanged.
- The Steam server browser, `servernetadr_t`, and Game Coordinator remain IPv4-only
  (Steamworks is `uint32`-bound) by design.

No `PROTOCOL_VERSION` bump was needed — `netadr_t::Serialize`/`Unserialize` are
never defined or called, so the wire protocol is unchanged.

## Usage

Listen server:
```
./hl2.sh -game tf2test +map tf2_bg01 +ip ::1 +hostport 27015 +sv_lan 1
```

Client:
```
./hl2.sh -game tf2test +connect "[::1]:27015"
```

Headless dedicated server (built separately, see below):
```
./srcds_linux64 -game tf2test +map tf2_bg01 +ip ::1 +hostport 27016 +sv_lan 1
```

## Headless dedicated server

The dedicated server is a **separate executable** (`srcds_linux64`), not a runtime
flag on the listen executable. Build it into its own directory so it doesn't
overwrite the listen `engine.so`:

```
python3 waf configure -T debug --disable-warns -d -o build_dedicated
python3 waf build -j4
```

The dedicated launcher (`dedicated_main`) was fixed to look in `bin/linux64/` for
`.so` files on 64-bit Linux, mirroring the fix already applied to the listen
launcher. Run it from a directory whose `bin/linux64/` contains the dedicated
(SWDS) builds of `engine.so`, `materialsystem.so`, etc., with `shaderapiempty.so`
instead of `shaderapidx9.so` so no display is needed.

Verified: a headless dedicated server on `[::1]:27016` accepts a listen client
over native IPv6 — full netchan handshake, signon, and entity update delivered
with no GPU/window.

## 64-bit Linux SDK runtime fixes

Several issues that prevented the fork from running against the 64-bit Source SDK
Base 2013 Multiplayer install were fixed:

- `FileSystem_GetBaseDir` double-strip and `foundLibraryWithPrefix` double-prefix
  bugs in `tier1/interface.cpp` / `public/filesystem_init.cpp`.
- `DEFAULT_LIB_PATH` set to `bin/linux64/` so `Sys_LoadModule` finds `.so` files.
- Launcher `dlopen`/`LD_LIBRARY_PATH` handling for the `bin/linux64/` layout.
- Non-fatal missing-background-image and Wayland/EGL `glXGetCurrentDisplay` guards.

# Support the original developer
BTC: `bc1qnjq92jj9uqjtafcx2zvnwd48q89hgtd6w8a6na`

ETH: `0x5d0D561146Ed758D266E59B56e85Af0b03ABAF46`

XMR: `48iXvX61MU24m5VGc77rXQYKmoww3dZh6hn7mEwDaLVTfGhyBKq2teoPpeBq6xvqj4itsGh6EzNTzBty6ZDDevApCFNpsJ`

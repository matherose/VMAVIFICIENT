# vmavificient

Professional-grade AV1 encoding CLI with VMAF-targeted CRF search, HDR
passthrough (HDR10 / HDR10+ / Dolby Vision), automatic grain analysis,
and Opus audio re-encoding. Single statically-linked binary per platform,
zero system dependencies.

> **Status — v2.0.0-dev.** This is a clean-slate rewrite of v1.5.0.
> See [`docs/porting-notes.md`](docs/porting-notes.md) for the rationale
> and the migration plan. Until v2.0.0 ships, use the `main` branch.

## Platforms

| OS      | Architecture | Toolchain    | Status      |
|---------|--------------|--------------|-------------|
| Linux   | x86_64       | zig cc/musl  | In progress |
| Linux   | aarch64      | zig cc/musl  | In progress |
| macOS   | x86_64       | Apple Clang  | In progress |
| macOS   | arm64        | Apple Clang  | In progress |
| Windows | x86_64       | llvm-mingw   | In progress |

## Install (v2.0+, planned)

Download the artifact for your platform from
[GitHub Releases](https://github.com/joeltordjman/VMAVIFICIENT/releases)
and install.

```sh
# Linux (amd64 or arm64)
sudo dpkg -i vmavificient_2.0.0_amd64.deb

# macOS (Intel or Apple Silicon) — unsigned, see docs/gatekeeper-bypass.md
sudo installer -pkg vmavificient-2.0.0-arm64.pkg -target /

# Windows (x86_64) — unsigned, see docs/smartscreen-bypass.md
msiexec /i vmavificient-2.0.0-x86_64.msi
```

## Quickstart

```sh
# One-shot encode (backward-compat with v1)
vmavificient movie.mkv

# Subcommand form (v2)
vmavificient encode movie.mkv --preset live-action --target-vmaf 95 --companion-hd

# Probe without encoding
vmavificient analyze movie.mkv --output json | jq .

# Self-check
vmavificient doctor
```

## Build from source

Requires Clang/LLVM, CMake 3.24+, Ninja, and Git submodules initialized.

```sh
git clone --recursive https://github.com/joeltordjman/VMAVIFICIENT.git
cd VMAVIFICIENT
cmake --preset host -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Cross-compiles use the toolchain files under `cmake/toolchains/`; see
[`docs/cross-compile.md`](docs/cross-compile.md).

## License

Source code: **WTFPL v2** — see [`LICENSE`](LICENSE).
Pre-built binaries: **GPL-3.0-or-later** (inherited from FFmpeg
`--enable-gpl`) — see [`LICENSE-BINARY`](LICENSE-BINARY) and
[`docs/licensing.md`](docs/licensing.md).

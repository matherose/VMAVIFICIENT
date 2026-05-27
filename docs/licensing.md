# Licensing — source vs binary

vmavificient has two distinct license regimes: one for the source
code in this repository, one for the pre-built binary artifacts
shipped on GitHub Releases.

## Source code: WTFPL v2

Everything under this repository's top-level (`src/`, `include/`,
`cmake/`, `packaging/`, `scripts/`, `tests/`, `docs/`, `*.md`,
`CMakeLists.txt`, etc.) is released under the **Do What The Fuck
You Want To Public License, Version 2**. Full text in [`LICENSE`](../LICENSE).

You can do anything with the source — fork, vendor, modify, sell,
relicense your derivatives. No attribution required (though nice).

## Binary artifacts: GPL-3.0-or-later

The pre-built `.deb`, `.pkg`, `.msi`, and `.tar.zst` artifacts
attached to a GitHub Release are a different story. The build is
configured with FFmpeg `--enable-gpl`, which means the FFmpeg
libraries we statically link inherit the GPLv3 license. As a
combined work, the binary is therefore **GPL-3.0-or-later**.

This affects you only if you redistribute the binary. If you just
run it on your own machine, the GPL doesn't impose obligations.

## What "GPL via FFmpeg" requires when you redistribute

Per the GPL-3.0:

1. **Provide source access** for every GPL-licensed component
   linked into the binary. The pinned upstream sources are
   submoduled under `third_party/` with exact SHAs:

   | Component | License | Source |
   |---|---|---|
   | FFmpeg | LGPL-2.1+ + GPL-3 (via `--enable-gpl`) | `third_party/ffmpeg` |
   | SVT-AV1-HDR | BSD-3-Clause | `third_party/svt-av1-hdr` |
   | libvmaf | BSD-2-Clause + Apache-2.0 (libsvm) | `third_party/vmaf` |
   | libdav1d | BSD-2-Clause | `third_party/dav1d` |
   | libopus | BSD-3-Clause | `third_party/opus` |
   | Tesseract | Apache-2.0 | `third_party/tesseract` |
   | leptonica | BSD-2-Clause | `third_party/leptonica` |
   | libpng | libpng (BSD-style) | `third_party/libpng` |
   | libjpeg-turbo | BSD-3-Clause + IJG | `third_party/libjpeg-turbo` |
   | libtiff | libtiff (BSD-style) | `third_party/libtiff` |
   | zlib | zlib | `third_party/zlib` |
   | libcurl | curl | `third_party/curl` |
   | OpenSSL | Apache-2.0 (3.x) | `third_party/openssl` |
   | cJSON | MIT | `third_party/cjson` |
   | libdovi | MIT | `third_party/dovi_tool` |
   | hdr10plus_tool | MIT | `third_party/hdr10plus_tool` |

   The dominant license (and the one that pulls the combined work
   into GPL territory) is FFmpeg's `--enable-gpl` build, which
   pulls in components like `libx264` *headers* and other
   GPL-licensed FFmpeg modules.

2. **Pass on GPL-3.0 rights** to recipients. You can't add
   restrictions (e.g. an EULA that forbids reverse-engineering).

3. **Do not impose additional restrictions.**

Full GPL-3.0 text: <https://www.gnu.org/licenses/gpl-3.0.txt>

## What this means in practice

- **You can run the binary anywhere.** No GPL obligations.
- **You can fork the source under WTFPL.** Do whatever.
- **If you build your own binary**, your license depends on your
  configure flags (e.g. `--disable-gpl` would give you LGPL).
- **If you redistribute the official binary**, the GPL-3.0 attaches.
  Provide source access (pointing at this repo + the pinned
  submodule SHAs is sufficient).

## Tessdata

`eng.traineddata` (bundled at `/usr/share/vmavificient/tessdata/`)
is from [tesseract-ocr/tessdata@4.1.0](https://github.com/tesseract-ocr/tessdata/tree/4.1.0).
The tessdata project is released under **Apache-2.0**. Tessdata
distribution doesn't add restrictions beyond Apache-2.0's
attribution requirement (preserved via the LICENSE-BINARY notice).

## Reporting license concerns

If you spot a license that should be listed but isn't, or a
component that's GPL-incompatible, please open an issue:
<https://github.com/matherose/VMAVIFICIENT/issues>.

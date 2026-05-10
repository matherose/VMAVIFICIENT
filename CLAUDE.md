# CLAUDE.md

Brief for future Claude sessions. The README.md covers user-facing context; this file covers what *you* need to know to work on the codebase effectively.

## Project intent

**VMAVIFICIENT targets the "HDLight / 4KLight" release tier**, not REMUX-faithful or fidelity-priority encodes. The pipeline uses `ab-av1 crf-search` to find the CRF that meets a per-preset VMAF target, then encodes in CRF mode. Every grain / quality decision in this codebase is calibrated for HDLight — assumptions valid at 8 Mbps don't apply here.

The user (Joel) builds scene-quality AV1 encodes and knows video encoding deeply. Speak in terms of:
- CRF + VMAF targets, encoder knobs, grain mechanisms — concrete implementation
- French scene conventions for naming (MULTi.VFI / VFF / VFQ / VOF, DUAL.VFI, FRENCH, TRUEFRENCH)

## Rate control: CRF search (current design)

The default pipeline runs `ab-av1 crf-search` to find the CRF meeting a per-preset VMAF target, then encodes in `SVT_AV1_RC_MODE_CQP_OR_CRF`. VBR is only used when `--bitrate` is explicitly passed.

**VMAF target table** (`get_vmaf_target()` in `encode_preset.c`):

| Quality preset | 4K (≥2160p) | HD (<2160p) |
|---|---|---|
| Animation | 95 | 96 |
| Live-action (digital) | 92 | 94 |
| Super35 digital / IMAX digital | 92 | 93 |
| Super35 analog / IMAX analog | 90 | 91 |

**CRF search** (`src/crf_search.c`):
- Invokes `ab-av1 crf-search -i <input> --preset <N> --min-vmaf <target> --svt key=value …`
- Passes the **full preset** as `--svt` args (tune, tf-strength, ac-bias, variance-boost, cdef-scaling, grain/noise settings, etc.) so the probe encodes use the exact same encoder config as the real encode
- `scd` is **not** passed via `--svt` — ab-av1 has its own `--scd` flag and rejects it there
- stdout is captured to parse `crf <N>`; stderr is inherited so ab-av1 progress reaches the terminal
- If `ab-av1` is not in PATH: hard error with install hint; bypass with `--crf <N>` or `--bitrate <kbps>`

**Phase history — don't redo these:**

1. **Phase A pinned flat VBR bitrates** (4000/3500/2500/2000 kbps by tier). This was the design before CRF search was added. `get_target_bitrate()` still exists for the `--bitrate` override path but is no longer the default.

2. **Phase B was reverted.** We briefly fed grav1synth's measured grain table into SVT-AV1's `fgs_table` to "force faithful grain reproduction". On heavy-grain sources at HDLight bitrates, this produced mushy output. The lesson: *let the encoder pick light synth params*, don't force-feed measured ones.

3. **`film_grain_denoise_apply=1` is correct for our tier**, even though the SVT-AV1-HDR fork's docs warn it loses detail. The detail loss is real, but at HDLight bitrates the alternative (apply=0) explodes the bitrate.

4. **`--noise` is preferred over `--film-grain` for digital content** (PR #10). Don't switch digital presets back to `--film-grain` — `--noise` is content-agnostic and has zero analysis overhead.

## Encoder strategy (read this before touching grain code)

We use **SVT-AV1-HDR 4.1.0+** (juliobbv-p fork), which has TWO grain mechanisms:

| Mechanism | Field | Use for | Why |
|---|---|---|---|
| `--noise N` | `cfg->noise_strength` | Digital sources + animation | Synthetic overlay, no source modification, zero overhead |
| `--film-grain N` + denoise | `cfg->film_grain_denoise_strength` + `_apply` | Analog film (Super 35 / IMAX shot on film) | Analyzes source grain (has structure), denoises + re-synths |

The choice is encoded as `EncodePreset.use_noise` (1 = `--noise`, 0 = `--film-grain`). Set per preset in `src/encode_preset.c`.

## Architecture map

| File | Role |
|---|---|
| `src/main.c` | CLI parsing, top-level orchestration, plan/done blocks |
| `src/ui.c` + `include/ui.h` | Terminal UI helpers (sections, kv, stage status, progress, fmt helpers). Honors `NO_COLOR`. |
| `src/encode_preset.{c,h}` | Per-quality / per-resolution preset table; VMAF target table; grain level mapping; `get_target_bitrate()` (VBR override path only) |
| `src/crf_search.{c,h}` | `run_crf_search()` — ab-av1 subprocess, full preset → `--svt` arg mapping, CRF output parsing |
| `src/media_analysis.c` | Grain score analysis via grav1synth diff over windowed samples; emits per-window OK/FAIL via ui_stage_* |
| `src/media_info.c` / `media_hdr.c` / `media_crop.c` / `media_tracks.c` | Source probing |
| `src/media_naming.c` | Scene-style filename generation |
| `src/video_encode.c` | SVT-AV1-HDR encoder driver, RPU injection, grain mechanism wiring (`apply_preset_to_config`) |
| `src/audio_encode.c` | FFmpeg → Opus per audio track |
| `src/subtitle_convert.c` | PGS bitmap subs → SRT via Tesseract OCR |
| `src/rpu_extract.c` | Dolby Vision RPU NAL extraction (HEVC UNSPEC62) |
| `src/final_mux.c` | FFmpeg remux into final MKV |
| `src/tmdb.c` | TMDB API lookup |

Progress bars across `audio_encode`, `rpu_extract`, `subtitle_convert`, `video_encode` go through `ui_progress_*` (start / update / done). Don't add a fifth in-line bar — extend the helper.

## CLI flag matrix

| Flag | What it does |
|---|---|
| `--tmdb <id>` | Use TMDB for naming; requires a config file with `tmdb_api_key` (see `--config`) |
| `--blind` | Skip TMDB; use input filename. No config file needed. |
| `--config` | One-shot interactive setup. Writes `~/.config/vmavificient/config.ini` (chmod 0600). Run once after install. |
| `--crf <N>` | Skip crf-search; encode at this CRF directly (1–63) |
| `--vmaf-target <N>` | Override the per-preset VMAF target for crf-search |
| `--bitrate <kbps>` | Skip crf-search; encode VBR at this bitrate (manual override) |
| `--srt <path>` | Add an external SRT (repeatable) |
| `--dry-run` | Run analysis + crf-search + naming, print the encoding plan, then exit. No files written. |
| `--grain-only` | Like `--dry-run` but skips crf-search; dumps encoder knobs for preset sanity-checking. |
| `--quiet` | Compact output. Only banner + Plan + Done + stage status lines. |
| `--verbose` | Forward SVT-AV1's own log to stderr (rate control, GOP layout, warnings). Composes with `--quiet`. |
| `--animation` / `--super35_analog` / `--super35_digital` / `--imax_analog` / `--imax_digital` | Quality preset (default = liveaction) |
| `--multi` / `--vff` / `--vfq` / `--vfi` / `--vof` / `--vf2` / `--french` / `--truefrench` / `--vo` / `--vost` / `--fansub` and friends | Language tag (overrides auto-detection) |
| `--bdrip` / `--bluray` / `--remux` / `--webrip` / `--webdl` / `--web` / `--hdtv` / etc. | Source type (overrides filename detection) |

## UI conventions

- **Sections** (`ui_section`): Unicode `─` rules, 70-col fixed width.
- **Key/value** (`ui_kv`): label column padded to 14.
- **Stage status** (`ui_stage_ok` / `_skip` / `_fail` / `_hint`): `[OK]` / `[--]` / `[FAIL]` / `[hint]` markers, colored on TTY.
- **Plan + Done sections always render**, even in `--quiet` (bracketed with `ui_set_quiet(0)` / restore).
- **Errors include context + a hint**: `ui_stage_fail("video.mkv", "error -1 after 142336 frames")` followed by `ui_hint("re-run with --verbose to forward SVT-AV1's own log to stderr")`.

## Build modes (v1.1.0+)

`CMakeLists.txt` exposes a `VMAV_USE_SYSTEM_DEPS` toggle (default **ON**):

- **ON**: use `pkg_check_modules` / `find_package` for FFmpeg (6 libs), Opus, libdovi, Tesseract+Leptonica, libpng/jpeg/tiff, zlib, OpenSSL, libcurl, cJSON. Fast (~3 min on a clean checkout), small binary (~4 MB on macOS arm64), what the Homebrew formula uses.
- **OFF**: vendor and statically link everything via `ExternalProject_Add`. Slow (~30 min), big (~38 MB), used only by the GitHub-release static-binary CI job (tag pushes).

Three deps stay vendored regardless of the toggle: SVT-AV1-HDR (we need the juliobbv-p HDR fork), libhdr10plus (not in Debian; brew formula installs binary only), grav1synth (CLI tool used as subprocess). The `posix_spawnp` call in `media_analysis.c` does PATH lookup when the compile-time `VMAV_GRAV1SYNTH_BIN` has no slash, so packagers can pass `-DVMAV_GRAV1SYNTH_BIN_RUNTIME=grav1synth` and ship the helper alongside the main binary in `bin/`.

`ab-av1` is a runtime dependency (not vendored) — install via `brew install master-of-mint/tap/ab-av1`. Not needed if always using `--crf` or `--bitrate`.

Local pkg-config tip: if you have both MacPorts and Homebrew installed, point CMake at Homebrew's pkg-config explicitly so it finds the right `.pc` files:

```bash
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig:/opt/homebrew/opt/jpeg-turbo/lib/pkgconfig" \
cmake -G Ninja -B build -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
```

## Homebrew tap

Lives at [matherose/homebrew-vmavificient](https://github.com/matherose/homebrew-vmavificient). One-line consumer install:

```bash
brew tap matherose/vmavificient
brew install vmavificient
vmavificient --config
```

The formula uses `VMAV_USE_SYSTEM_DEPS=ON` (the default) and passes `-DVMAV_GRAV1SYNTH_BIN_RUNTIME=grav1synth` so the bundled helper resolves via PATH after install. When bumping the formula's `head:` ref or pinning a stable `url`+`sha256`, keep in sync with the project's `PROJECT_VERSION` in `CMakeLists.txt`.

## Build workflow gotcha

Claude works in a per-session worktree at `.claude/worktrees/<name>/` (the worktree branch is `claude/<name>`). The `build/` directory lives only in the worktree; `cmake -B build` from the worktree root regenerates it. ExternalProject sub-builds bake absolute paths into their `CMakeCache.txt`, so if those paths drift (e.g. you tried to move `build/` between worktrees), expect "source directory does not match" errors. Fix:

```bash
find build/deps/build -name CMakeCache.txt -exec sed -i '' \
  's|/old/path/.../build|/new/path/.../build|g' {} \;
```

## Commit conventions

- **Trailer**: `Assisted-by: Claude Sonnet 4.6 <noreply@anthropic.com>` — *not* `Co-Authored-By`. The user is the author; Claude is the assistant.
- **Stacked PRs are the norm.** When PRs are stacked, set `--base` to the parent branch in `gh pr create`. The user merges in order.
- **Commit messages**: explain *why*, not just *what*. The user reviews these as encoder-design docs.

## Things explicitly NOT to do

| Don't | Why |
|---|---|
| Re-add `fgs_table` from grav1synth | Phase B reverted this. At HDLight bitrates it produces mush. |
| Default to `--film-grain` for digital sources | PR #10 switched to `--noise`. Digital cameras don't have grain to reproduce. |
| Add a `--log-file` flag | The user said no. They use shell `tee`. |
| Pass `scd` via `--svt` to ab-av1 | ab-av1 rejects it — it has its own `--scd` flag. Leave ab-av1 to manage SCD for sample encodes. |
| Use upstream SVT-AV1 instead of juliobbv-p/svt-av1-hdr | We need the HDR fork's DV/HDR10+ passthrough + `--noise` (4.1.0+). |

## Resources

- [SVT-AV1-HDR repo](https://github.com/juliobbv-p/svt-av1-hdr)
- [SVT-AV1 Parameters doc](https://gitlab.com/AOMediaCodec/SVT-AV1/-/blob/master/Docs/Parameters.md) — canonical source for `--svt key=value` arg names
- [SVT-AV1 CommonQuestions](https://gitlab.com/AOMediaCodec/SVT-AV1/-/blob/master/Docs/CommonQuestions.md) — read the "Practical Advice on Grain Synthesis" section before changing grain code.
- [Reddit thread on 4.1.0 noise feature](https://www.reddit.com/r/AV1/comments/1sw8cqt/svtav1hdr_410_chromedome_is_out/) — creator commentary on `--noise` semantics.
- [ab-av1 docs](https://github.com/alexheretic/ab-av1) — `crf-search` CLI reference; `--svt` vs dedicated flags distinction.

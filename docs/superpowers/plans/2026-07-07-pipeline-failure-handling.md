# Pipeline Failure Handling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the four pre-existing pipeline bugs found during TV-support validation (unknown-channel-layout PCM breaks audio encode; failed audio tracks still counted into mux inputs; failures exit 0; 0-byte extracted SRTs fed to mux) plus two recorded nits (episode parser accepts a 4th digit; EpisodeInfo.title truncates TMDB titles).

**Architecture:** Bug 1 is a decoder-side channel-layout normalization in `audio_encode.c` using `av_channel_layout_default` (the same call the encoder side already uses at line 288). Bugs 2–3 are a failure-propagation policy in `main.c`: failed audio tracks are excluded from mux inputs via compact write-indexing, any audio or video failure skips the final mux (a release missing an announced track is broken, and the MULTi/VFF language tag in the name would lie), and a `pipeline_failed` flag drives a non-zero exit. Bug 4 is a non-empty-file check after subtitle extraction. Subtitle extraction failures deliberately stay warnings — they are already correctly excluded from mux inputs today.

**Tech Stack:** C11, FFmpeg libav* (channel-layout API), CMake+Ninja, existing `vmav_tests` unit test target (pure-logic only — no libav in tests).

---

## Repo conventions (MANDATORY for every task)

- Build: `cmake --build build` (from repo root `/Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT`). Unit tests: `cmake --build build --target vmav_tests && ./build/vmav_tests`.
- Commit trailer: `Assisted-by: Claude Fable 5 <noreply@anthropic.com>` — NOT Co-Authored-By.
- `git add` explicit paths ONLY. NEVER `git add -A` or `git add .` (huge untracked test media in repo root).
- Do NOT run clang-format (local v22 vs CI v18 mismatch). Match surrounding style by hand (2-space indent, `/* … */` comments).
- No scanf-family functions except existing deliberate uses; no recursion (clang-tidy gates).
- LSP diagnostics about `config.h`, enum sizes, identifier naming are non-gating noise. The build is the gate.
- The TMDB API key lives in `config.ini` — never print or commit it.
- Work on branch `fix/pipeline-failure-handling`.

## Validation fixture (used by Tasks 1, 2, 3, 5)

A 4-minute clip of the Evangelion source, WITH the problematic PCM tracks, plus a warm cache, lives at:

```
CLIP_DIR="/private/tmp/claude-501/-Volumes-Data-MEDIAS-MOVIE-VMAVIFICIENT/c57bd08c-6740-4045-8ecb-11e856da664f/scratchpad/tvtest"
CLIP="Neon Genesis Evangelion - Episode 01.mkv"   # inside CLIP_DIR
```

The cache (`$CLIP_DIR/cache/`) already holds: `…video.mkv` (video step will SKIP), `…fre.fr.opus` (French audio will SKIP), `…fre.fr..full.srt` (7.4 KB), `…fre.fr..forced.srt` (**0 bytes** — the bug-4 fixture), and `scores.json` (cached CRF). Only crop/grain probing and the failing jpn PCM track run fresh, so each validation run takes a few minutes, not hours.

Standard invocation (run FROM `$CLIP_DIR` so outputs land in the scratchpad, never the repo):

```bash
cd "$CLIP_DIR"
rm -f 新世紀エヴァンゲリオン.S01E01.Angel.Attack.MULTi.VFF.1080p.SDR.BluRay.HDLight.10bit.AV1.OPUS-matherose.mkv
/Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/build/vmavificient --tv --tmdb 890 --season 1 --episode 1 "$CLIP" > run.log 2>&1
echo "exit=$?"
```

Notes: always `rm -f` the final MKV first or the mux step skips with "already exists". Capture stdout+stderr together into `run.log` (stderr is unbuffered, so `[FAIL]` lines interleave far from their stdout position — always search the whole file). In zsh, never read `$?` through a pipe; run the command bare and echo `exit=$?` on the next line. `TESSDATA_PREFIX` comes from the user's shell profile; if PGS OCR warns about missing training data that is fine — subtitle warnings do not gate any assertion below.

Source-file ground truth (`ffprobe` of the full episode and the clip, identical layout): stream 0 h264 video; **stream 1 pcm_s16le 6ch `channel_layout=unknown` jpn** (the failure trigger — it is also the highest-bitrate jpn track, so `select_best_audio_per_language` picks it); stream 2 pcm_s16le 2ch unknown jpn; stream 3 eac3 5.1(side) jpn; stream 4 eac3 5.1(side) fre; stream 5 dts 5.1(side) fre; streams 6–7 ass fre (7 = forced, with zero events inside the clip window); streams 8–10 PGS fre.

---

### Task 1: Normalize unspecified channel layouts in the audio encoder (bug 1)

**Files:**
- Modify: `src/audio_encode/audio_encode.c` (three insertions: after `avcodec_parameters_to_context` ~line 252; in the decode loop ~line 403; in the decoder-flush loop ~line 448)

**Why:** Sources sometimes carry PCM tracks muxed without a channel layout (`channel_layout=unknown`, order `AV_CHANNEL_ORDER_UNSPEC`). `swr_convert_frame` compares each frame's layout against the configured one and returns `AVERROR_INPUT_CHANGED` (-1668179713) on mismatch — this is exactly the "Error: resample failed" / "[FAIL] …jpn.opus … error -1668179713" seen on the fixture. Pinning the default layout for the channel count on the decoder context before it produces frames (plus a per-frame guard) makes decoder output, swr configuration, and encoder layout all agree. `av_channel_layout_default(AVChannelLayout *, int nb_channels)` is already used in this file at line 288 for the encoder side.

- [ ] **Step 1: Reproduce the failure (RED)**

Run the standard invocation from the fixture section above. Expected in `run.log`:
- `Error: resample failed`
- a `[FAIL]` line for the jpn opus containing `error -1668179713`
- the final mux failing (jpn.opus missing)

If instead the jpn track SKIPs as "already exists", delete any `*jpn.opus` from `$CLIP_DIR/cache/` and re-run.

- [ ] **Step 2: Insert the decoder-context normalization**

In `src/audio_encode/audio_encode.c`, immediately AFTER this existing block (~line 252):

```c
  ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  if (ret < 0) {
    result.error = ret;
    goto cleanup;
  }
```

insert:

```c
  /* Some sources carry PCM tracks with an unspecified channel layout
     (e.g. 6ch pcm_s16le muxed without one).  swresample rejects frames
     whose layout differs from its configuration (AVERROR_INPUT_CHANGED),
     so pin the default layout for the channel count before the decoder
     starts producing frames. */
  if (dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    av_channel_layout_default(&dec_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
```

- [ ] **Step 3: Insert the per-frame guards**

In the main decode loop, the body currently starts (~line 403):

```c
    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
      /* Resample to 48kHz float. */
      resampled->sample_rate = 48000;
```

Change to:

```c
    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
      if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&frame->ch_layout, frame->ch_layout.nb_channels);

      /* Resample to 48kHz float. */
      resampled->sample_rate = 48000;
```

And in the decoder-flush loop (~line 448), which currently starts:

```c
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) {
    resampled->sample_rate = 48000;
```

Change to:

```c
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) {
    if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
      av_channel_layout_default(&frame->ch_layout, frame->ch_layout.nb_channels);

    resampled->sample_rate = 48000;
```

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: clean build, no new warnings.

- [ ] **Step 5: Verify the fix (GREEN)**

Run the standard invocation again (remember the `rm -f` of the final MKV first). Expected in `run.log`:
- an `[OK]` line for the jpn opus (a 6ch PCM decode+encode of 4 minutes takes on the order of a minute)
- NO `resample failed`

KNOWN AND ACCEPTED at this stage: the final mux still fails with `error 183` because the 0-byte forced SRT is still counted (bug 4, fixed in Task 2), and the process still exits 0 (bug 3, fixed in Task 3). Do NOT try to fix those here.

- [ ] **Step 6: Commit**

```bash
git add src/audio_encode/audio_encode.c
git commit -m "fix(audio_encode): default channel layout for unspecified-layout sources

PCM tracks muxed without a channel layout (channel_layout=unknown)
made swr_convert_frame fail with AVERROR_INPUT_CHANGED, aborting the
track encode. Pin av_channel_layout_default() on the decoder context
before it produces frames, with a per-frame guard for decoders that
still emit unspecified layouts.

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Skip empty extracted subtitles (bug 4)

**Files:**
- Modify: `src/main/main.c` (~lines 1797–1809, text-subtitle extraction result handling)

**Why:** A forced text subtitle track can have zero events in the encoded window; ffmpeg exits 0 and writes a 0-byte SRT, which is then counted into the mux inputs, and ffmpeg's mux rejects an empty SRT input (observed as final-mux `error 183`). Note the existing already-exists check just above (line ~1772) already requires `st_size > 0`, so a cached 0-byte SRT correctly falls through to re-extraction — the gap is only on the fresh-extraction path.

- [ ] **Step 1: Confirm the failing state (RED)**

`$CLIP_DIR/cache/` contains the 0-byte `…fre.fr..forced.srt`, and the Task 1 GREEN run ended with the mux failing with `error 183` while the mux input count included the forced subtitle. That log is the RED evidence; no new run needed.

- [ ] **Step 2: Apply the fix**

In `src/main/main.c`, this is the current extraction result handling (~line 1797):

```c
              int rc = system(cmd);
              int exit_code = (rc == -1 || !WIFEXITED(rc)) ? -1 : WEXITSTATUS(rc);
              if (exit_code == 0) {
                ui_stage_ok(srt_fname, NULL);
                srt_count++;
              } else {
```

Replace the success branch so it requires a non-empty output file:

```c
              int rc = system(cmd);
              int exit_code = (rc == -1 || !WIFEXITED(rc)) ? -1 : WEXITSTATUS(rc);
              struct stat srt_out_st;
              if (exit_code == 0 && stat(srt_paths[srt_count], &srt_out_st) == 0 &&
                  srt_out_st.st_size > 0) {
                ui_stage_ok(srt_fname, NULL);
                srt_count++;
              } else if (exit_code == 0) {
                /* ffmpeg succeeded but wrote no events (e.g. a forced track
                   with nothing to force in this cut). An empty SRT is not a
                   valid mux input, so drop it. */
                remove(srt_paths[srt_count]);
                ui_stage_skip(srt_fname, "no subtitle events in stream");
              } else {
```

The final `} else {` continues into the existing failure branch unchanged (`char err[128]; …`).

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: clean build. (`sys/stat.h` is already included in main.c — the already-exists check above uses `struct stat`.)

- [ ] **Step 4: Verify (GREEN)**

Run the standard invocation from the fixture section. Expected in `run.log`:
- the forced SRT line becomes a SKIP with `no subtitle events in stream`
- audio lines all OK/SKIP (fre + jpn cached from Task 1)
- video SKIP (cached)
- **final mux `[OK]`** — the fixture's original failure chain is now fully green
- the final MKV exists in `$CLIP_DIR`

A forced PGS track may now get OCR'd (the empty ASS no longer satisfies the dedup check). Whatever it produces — an extra forced SRT or a "no subtitles extracted" skip or a tessdata warning — is acceptable; the assertion is the mux `[OK]`.

- [ ] **Step 5: Commit**

```bash
git add src/main/main.c
git commit -m "fix(main): drop empty extracted subtitles instead of muxing them

ffmpeg exits 0 when a text-subtitle track has no events in the encoded
window, leaving a 0-byte SRT that the final mux then rejects (error
183). Require a non-empty output file before counting the track.

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Failure propagation — exclude failed audio, gate muxes, non-zero exit (bugs 2 + 3)

**Files:**
- Modify: `src/main/main.c` (multiple sites; each anchored below with its current code)

**Why:** Today `opus_count++` runs even when a track fails, so the mux is handed a missing file; and every failure path ([FAIL] audio, video, mux) still reaches `return 0;` at the end of `main`. Policy being implemented: a failed audio track is excluded from mux inputs; **any audio-track failure or video failure skips the final mux entirely** (a release missing an announced track is broken, and the language tag baked into the filename would lie); every audio/video/mux failure sets a flag that becomes exit code 1. Subtitle failures stay warnings — they are already correctly excluded from mux inputs.

There is a subtlety: mux descriptors read `enc_best[i].language` with `i` iterating over `opus_count`, so simply "not counting" a failed track would misalign paths/names/languages. Fix by writing every per-track array through the `opus_count` write-index and capturing the language into a new `audio_langs` array at the same time.

- [ ] **Step 1: Add the pipeline_failed flag**

In `main()` (line 557), after:

```c
int main(int argc, char *argv[]) {
  init_logging();
  ui_init();
  time_t encode_start_time = time(NULL);
```

insert:

```c
  /* Set on any audio/video/mux failure; drives the process exit code. */
  int pipeline_failed = 0;
```

- [ ] **Step 2: Rework the audio-encode loop bookkeeping**

Current declarations (~line 1646):

```c
      /* Store OPUS paths and track names for final mux */
      char opus_paths[32][4096];
      char audio_names[32][256];
      int opus_count = 0;
```

Change to:

```c
      /* Store OPUS paths, track names and languages for final mux.
         All three are written through the opus_count write-index so a
         failed track never leaves a hole in the mux inputs. */
      char opus_paths[32][4096];
      char audio_names[32][256];
      char audio_langs[32][16];
      int opus_count = 0;
      int audio_fail_count = 0;
```

Inside the loop, the per-track section currently reads (~lines 1671–1707):

```c
          char opus_name[2048];
          build_opus_filename(opus_name, sizeof(opus_name), base_name, enc_best[i].language,
                              track_fv);

          /* Write opus to cache directory */
          char opus_cache_path[4096];
          build_cache_path(opus_cache_path, sizeof(opus_cache_path), opus_name);
          snprintf(opus_paths[i], sizeof(opus_paths[i]), "%s", opus_cache_path);

          /* Build display name for MKV track */
          build_audio_track_name(audio_names[i], sizeof(audio_names[i]), enc_best[i].language,
                                 enc_best[i].channels, track_origin);

          ui_row("[%d/%d] %s  %s  %dch  %lld kbps  →  \"%s\"", i + 1, enc_best_count,
                 enc_best[i].language, enc_best[i].codec, enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000), audio_names[i]);

          time_t track_t0 = time(NULL);
          OpusEncodeResult r = encode_track_to_opus(filepath, &enc_best[i], opus_paths[i]);

          if (r.skipped) {
            ui_stage_skip(opus_name, "already exists");
          } else if (r.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), track_t0)));
            ui_stage_ok(opus_name, detail);
          } else {
            char err[128];
            snprintf(err, sizeof(err), "stream #%d (%s, %dch): error %d", enc_best[i].index,
                     enc_best[i].codec, enc_best[i].channels, r.error);
            ui_stage_fail(opus_name, err);
            ui_hint("verify the source stream is decodable; opusenc-style "
                    "channel layouts (>2ch) require ffmpeg with libopus");
          }

          opus_count++;
```

Replace with (array writes go to `opus_count`; increment only on success or skip):

```c
          char opus_name[2048];
          build_opus_filename(opus_name, sizeof(opus_name), base_name, enc_best[i].language,
                              track_fv);

          /* Write opus to cache directory */
          char opus_cache_path[4096];
          build_cache_path(opus_cache_path, sizeof(opus_cache_path), opus_name);
          snprintf(opus_paths[opus_count], sizeof(opus_paths[0]), "%s", opus_cache_path);

          /* Build display name and language for MKV track */
          build_audio_track_name(audio_names[opus_count], sizeof(audio_names[0]),
                                 enc_best[i].language, enc_best[i].channels, track_origin);
          snprintf(audio_langs[opus_count], sizeof(audio_langs[0]), "%s", enc_best[i].language);

          ui_row("[%d/%d] %s  %s  %dch  %lld kbps  →  \"%s\"", i + 1, enc_best_count,
                 enc_best[i].language, enc_best[i].codec, enc_best[i].channels,
                 (long long)(enc_best[i].bitrate / 1000), audio_names[opus_count]);

          time_t track_t0 = time(NULL);
          OpusEncodeResult r = encode_track_to_opus(filepath, &enc_best[i], opus_paths[opus_count]);

          if (r.skipped) {
            ui_stage_skip(opus_name, "already exists");
            opus_count++;
          } else if (r.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), track_t0)));
            ui_stage_ok(opus_name, detail);
            opus_count++;
          } else {
            char err[128];
            snprintf(err, sizeof(err), "stream #%d (%s, %dch): error %d", enc_best[i].index,
                     enc_best[i].codec, enc_best[i].channels, r.error);
            ui_stage_fail(opus_name, err);
            ui_hint("verify the source stream is decodable; opusenc-style "
                    "channel layouts (>2ch) require ffmpeg with libopus");
            audio_fail_count++;
            pipeline_failed = 1;
          }
```

- [ ] **Step 3: Use audio_langs in both mux descriptor loops**

4K mux (~line 2038), current:

```c
            mux_audio[i].language = enc_best ? enc_best[i].language : "und";
```

New:

```c
            mux_audio[i].language = audio_langs[i];
```

HD mux (~line 2360), current:

```c
            hd_mux_audio[i].language = enc_best ? enc_best[i].language : "und";
```

New:

```c
            hd_mux_audio[i].language = audio_langs[i];
```

- [ ] **Step 4: Flag the video-encode failure**

In the 4K video result handling (~line 2019), current failure branch:

```c
          } else {
            char err[64];
            snprintf(err, sizeof(err), "error %d after %lld frames", vr.error,
                     (long long)vr.frames_encoded);
            ui_stage_fail("video.mkv", err);
            ui_hint("re-run with --verbose to forward SVT-AV1's own log to "
                    "stderr (rate control, GOP layout, fatal warnings)");
          }
```

Add the flag:

```c
          } else {
            char err[64];
            snprintf(err, sizeof(err), "error %d after %lld frames", vr.error,
                     (long long)vr.frames_encoded);
            ui_stage_fail("video.mkv", err);
            ui_hint("re-run with --verbose to forward SVT-AV1's own log to "
                    "stderr (rate control, GOP layout, fatal warnings)");
            pipeline_failed = 1;
          }
```

Do the same in the HD video result handling (~line 2348): append `pipeline_failed = 1;` after its `ui_hint("re-run with --verbose to forward SVT-AV1's own log to "\n                  "stderr");` line inside the failure branch.

- [ ] **Step 5: Gate the 4K final mux**

Current call site (~lines 2075–2094):

```c
          ui_section("Final mux");
          ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", opus_count, srt_count,
                srt_count == 1 ? "" : "s");
          time_t mux_t0 = time(NULL);
          FinalMuxResult mr = final_mux(&mux_cfg);

          if (mr.skipped) {
            ui_stage_skip(output_name, "already exists");
          } else if (mr.error == 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "%s", ui_fmt_duration(difftime(time(NULL), mux_t0)));
            ui_stage_ok(output_name, detail);
          } else {
            char err[128];
            snprintf(err, sizeof(err), "error %d (%d audio + %d sub inputs)", mr.error, opus_count,
                     srt_count);
            ui_stage_fail(output_name, err);
            ui_hint("intermediates kept on disk; inspect them next to the "
                    "source file before re-running");
          }
```

Replace with:

```c
          ui_section("Final mux");
          ui_kv("Inputs", "1 video + %d audio + %d subtitle track%s", opus_count, srt_count,
                srt_count == 1 ? "" : "s");
          time_t mux_t0 = time(NULL);
          FinalMuxResult mr = {.error = -1, .skipped = 0};

          if (audio_fail_count > 0) {
            char err[128];
            snprintf(err, sizeof(err), "%d audio track%s failed to encode — mux skipped",
                     audio_fail_count, audio_fail_count == 1 ? "" : "s");
            ui_stage_fail(output_name, err);
            ui_hint("a release missing an announced audio track is broken; "
                    "cached intermediates are kept, fix the source and re-run");
            pipeline_failed = 1;
          } else if (vr.error != 0) {
            ui_stage_fail(output_name, "video encode failed — mux skipped");
            pipeline_failed = 1;
          } else {
            mr = final_mux(&mux_cfg);

            if (mr.skipped) {
              ui_stage_skip(output_name, "already exists");
            } else if (mr.error == 0) {
              char detail[64];
              snprintf(detail, sizeof(detail), "%s",
                       ui_fmt_duration(difftime(time(NULL), mux_t0)));
              ui_stage_ok(output_name, detail);
            } else {
              char err[128];
              snprintf(err, sizeof(err), "error %d (%d audio + %d sub inputs)", mr.error,
                       opus_count, srt_count);
              ui_stage_fail(output_name, err);
              ui_hint("intermediates kept on disk; inspect them next to the "
                      "source file before re-running");
              pipeline_failed = 1;
            }
          }
```

The code below the call site (`/* Clean up intermediate files on success … */` gated on `mr.error == 0`, and the done-receipt gated on `mr.error == 0 && !mr.skipped`) needs NO changes — the `{.error = -1}` initializer keeps both paths inert when the mux was skipped for failure. `vr` is declared `VideoEncodeResult vr = {0};` at line 1989 in the same enclosing scope, so `vr.error != 0` only fires on a real failure (a cache-skipped video has `error == 0, skipped == 1`).

- [ ] **Step 6: Gate the HD final mux the same way**

Current HD call site (~line 2396):

```c
          FinalMuxResult hd_mr = final_mux(&hd_mux_cfg);

          if (hd_mr.skipped) {
```

Apply the identical pattern: initialize `FinalMuxResult hd_mr = {.error = -1, .skipped = 0};`, then `if (audio_fail_count > 0) { … "mux skipped" ui_stage_fail on hd_output_name … pipeline_failed = 1; } else if (hd_vr.error != 0) { ui_stage_fail(hd_output_name, "video encode failed — mux skipped"); pipeline_failed = 1; } else { hd_mr = final_mux(&hd_mux_cfg); …existing result handling… }`, and inside the existing HD mux failure branch (the one printing `error %d (%d audio + %d sub inputs)` via `hd_mux_err`) add `pipeline_failed = 1;`. `hd_vr` is declared at line 2334 in the same scope as the HD mux block. As with the 4K path, the downstream `hd_mr.error == 0` guards need no changes.

IMPORTANT — scope check: `hd_vr` is declared inside the HD encode block. Before relying on it, confirm the HD mux block (starting `/* ---- HD final mux (reuse opus + srt from this session) ---- */` ~line 2352) sits in the same braces as the `hd_vr` declaration (line 2334). It does in the current source (the mux block opens ~18 lines below the declaration with no intervening close of that scope), but verify while editing; if the structure differs from this description, stop and report NEEDS_CONTEXT rather than improvising.

- [ ] **Step 7: Propagate the exit code**

At the very end of `main()` (line 2476), current:

```c
  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return 0;
}
```

New:

```c
  if (tracks.error == 0)
    free_media_tracks(&tracks);

  return pipeline_failed ? 1 : 0;
}
```

Do NOT touch the other `return 0;` / `return 1;` sites in main (dry-run/grain-only exits and the CRF-search failure) — they are correct as-is.

- [ ] **Step 8: Build**

Run: `cmake --build build`
Expected: clean build. Watch for unused-variable warnings if a site was missed.

- [ ] **Step 9: Verify — green path exits 0**

Run the standard invocation from the fixture section. Expected: everything OK/SKIP as in Task 2 Step 4, and `exit=0`.

- [ ] **Step 10: Verify — forced failure exits 1**

Make the cached video unreadable so the mux fails, and confirm exit 1:

```bash
cd "$CLIP_DIR"
rm -f 新世紀エヴァンゲリオン.S01E01.Angel.Attack.MULTi.VFF.1080p.SDR.BluRay.HDLight.10bit.AV1.OPUS-matherose.mkv
chmod 000 cache/新世紀エヴァンゲリオン.S01E01.Angel.Attack.MULTi.VFF.1080p.SDR.BluRay.HDLight.10bit.AV1.OPUS-matherose.video.mkv
/Volumes/Data/MEDIAS/MOVIE/VMAVIFICIENT/build/vmavificient --tv --tmdb 890 --season 1 --episode 1 "$CLIP" > run_fail.log 2>&1
echo "exit=$?"
chmod 644 cache/新世紀エヴァンゲリオン.S01E01.Angel.Attack.MULTi.VFF.1080p.SDR.BluRay.HDLight.10bit.AV1.OPUS-matherose.video.mkv
```

Expected: a `[FAIL]` on the final mux in `run_fail.log` and `exit=1`. (The video step itself still SKIPs — the size check passes on an unreadable file; the mux is what fails to open it. If the video step instead re-encodes, report what happened rather than waiting it out.) ALWAYS restore permissions afterward.

- [ ] **Step 11: Commit**

```bash
git add src/main/main.c
git commit -m "fix(main): propagate pipeline failures to mux gating and exit code

- a failed audio track is no longer counted into mux inputs (arrays are
  written through the opus_count index, with languages captured at
  success time so descriptors can't misalign)
- any audio or video failure skips the final mux: a release missing an
  announced track is broken and its language tag would lie
- audio/video/mux failures now exit 1 instead of 0

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: media_naming nits — reject 4-digit episodes, widen episode title

**Files:**
- Modify: `src/media_naming/media_naming.c` (pass-1 episode digits ~line 339; `head` buffer ~line 430)
- Modify: `include/vmavificient/media_naming.h` (line 66, `title[256]`)
- Test: `tests/test_media_naming.c`

**Why:** (a) Pass 1 of `parse_season_episode` caps episode digits at 3 but doesn't reject a 4th digit, so `S01E1050` parses as E105; the season parse and all of pass 2 already have exactly this guard. (b) `EpisodeInfo.title` is 256 bytes but TMDB episode names arrive in a 512-byte buffer (`TmdbEpisodeInfo.name[512]`); widen to 512 and bump the `head` assembly buffer (512-byte sanitized show title + 512-byte sanitized episode title + tags no longer fit in 1024 — snprintf truncates safely, but the name would be silently cut).

- [ ] **Step 1: Write the failing test**

In `tests/test_media_naming.c`, locate the existing `parse_season_episode` test block (search for `parse_season_episode`) and add alongside its cases, following the file's existing CHECK style:

```c
  /* A 4th digit after the episode means it's not an episode tag. */
  s = 0;
  e = 0;
  CHECK(parse_season_episode("Show.S01E1050.1080p.mkv", &s, &e) == -1);
```

Use the same variable names the surrounding cases use for the season/episode out-params (adapt `s`/`e` if the file uses different names — read the neighboring cases first).

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target vmav_tests && ./build/vmav_tests`
Expected: FAIL on the new case (current parser returns 0 with e=105).

- [ ] **Step 3: Add the guard**

In `src/media_naming/media_naming.c`, pass 1 currently ends the episode scan with (~line 339):

```c
      int e = 0;
      ndig = 0;
      while (isdigit((unsigned char)*q) && ndig < 3) {
        e = e * 10 + (*q - '0');
        q++;
        ndig++;
      }
      if (s > 0 && e > 0) {
```

Add the same rejection the season scan uses:

```c
      int e = 0;
      ndig = 0;
      while (isdigit((unsigned char)*q) && ndig < 3) {
        e = e * 10 + (*q - '0');
        q++;
        ndig++;
      }
      if (isdigit((unsigned char)*q))
        continue; /* 4+ digit "episode": not an episode tag */
      if (s > 0 && e > 0) {
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `./build/vmav_tests` (after `cmake --build build --target vmav_tests`)
Expected: `All tests passed` — including the pre-existing 3-digit case (`One Piece` E105) and SxxEyy cases.

- [ ] **Step 5: Commit the parser fix**

```bash
git add src/media_naming/media_naming.c tests/test_media_naming.c
git commit -m "fix(media_naming): reject 4-digit episode numbers in SxxEyy parsing

S01E1050 parsed as E105 because pass 1 capped the episode scan at three
digits without rejecting a fourth, unlike the season scan and pass 2.

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Widen the title buffers**

In `include/vmavificient/media_naming.h` line 66:

```c
  char title[256]; /**< Episode title from TMDB; empty string = omit. */
```

becomes:

```c
  char title[512]; /**< Episode title from TMDB; empty string = omit. */
```

In `src/media_naming/media_naming.c`, `build_output_filename`'s head buffer (~line 428):

```c
  char head[1024];
```

becomes:

```c
  char head[2048];
```

(The consumers in main.c fill `ep.title` via `snprintf(ep.title, sizeof(ep.title), …)`, so no other change is needed.)

- [ ] **Step 7: Build everything and re-run tests**

Run: `cmake --build build && ./build/vmav_tests`
Expected: clean build, `All tests passed`.

- [ ] **Step 8: Commit**

```bash
git add include/vmavificient/media_naming.h src/media_naming/media_naming.c
git commit -m "fix(media_naming): size episode title for full TMDB names

EpisodeInfo.title held 256 bytes but TMDB episode names arrive in a
512-byte buffer; widen it and the head assembly buffer so long titles
aren't silently truncated.

Assisted-by: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: End-to-end validation (main session)

Run in the main session, not a subagent. All on the fixture from the header.

- [ ] **Step 1: Unit tests** — `./build/vmav_tests` → `All tests passed`.

- [ ] **Step 2: Full green run** — standard invocation (with the `rm -f` first). Assert: jpn opus SKIP or OK (cached from Task 1); forced ASS skipped as `no subtitle events in stream`; video SKIP; final mux `[OK]`; `exit=0`.

- [ ] **Step 3: ffprobe the output** — container title `新世紀エヴァンゲリオン - S01E01 - Angel Attack`; streams: 1× av1 (jpn), 2× opus 6ch (fre first/default, jpn), ≥1 subrip fre; chapters present.

- [ ] **Step 4: Movie regression** — `cd "$CLIP_DIR" && /Volumes/…/build/vmavificient --blind --dry-run "$CLIP"; echo exit=$?` → movie-style blind-mode name (no SxxEyy), `exit=0`, no crash. Confirms the naming and exit-code plumbing didn't disturb the movie path.

- [ ] **Step 5: Report** — summarize results; note that the full-length encode of the real episode is now unblocked and remains the user's overnight run.

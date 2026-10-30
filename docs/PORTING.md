# Porting status

The Mac app's behaviour is specified in [port-spec.md](port-spec.md), which was written
from the Swift source with file and line references. Section numbers below refer to it.

## Done in 0.1

| Area | Spec | Notes |
|---|---|---|
| Library.json / Playback.json | §2, §6.6 | Same schema as macOS, including Swift's `{"relative": "file:///…"}` URLs and 2001-based dates. Tested against a Mac-format file. |
| Import, tag reading, grouping | §3.1–3.4 | Same extension lists, filename fallback regex, folder-image names, album grouping keys, "Various Artists", duplicate fingerprints, import report wording. TagLib for tags, FFmpeg for duration and embedded pictures. |
| Artwork store | §3.5 | Same file naming, one cover per album, tint colour, size buckets and cache budgets. |
| Removal | §3.7 | Tracks, albums, empty-album cleanup; audio files never deleted. |
| Cover Flow view | §5.2 | Geometry (62°, 0.18/0.68 spacing, 0.10 scale), perspective, reflection, spring (0.42 s / 0.86), rubber band, drag with predicted travel, one album per wheel notch, touchpad scrolling with 0.18 s snap, keyboard. |
| Grid | §5.3 | Adaptive columns, hover play button, marquee selection, context menu. |
| Selection | §2.1, §5.4 | Click / Ctrl-click / Shift-click, selection bar. |
| Album track panel | §5.5 | Cover-derived palette (§4.1, WCAG contrast), two-column split rule, disc headings, per-track menu. |
| Now Playing bar, title marquee, queue panel | §5.8–5.10 | |
| Playback engine | §6.1–6.3 | FFmpeg → swresample → ring buffer → QAudioSink. Gapless: the decoder asks for the successor when it reaches the end of a track; queue edits re-check decoded-ahead audio. Shuffle/repeat/previous/failure-skip rules. Follows the default output device. |
| Album View | §7 | Presets, keep on top, focus mode, auto-hide (3.2 s), keyboard. |
| Lyrics | §8 | LRC parser ported exactly (offsets, multi-stamps, merged translations, word stamps), lookup order, size/encoding rules, lyrics window. |
| Online lookups | §9 | LRCLIB with ranking; MusicBrainz + Cover Art Archive; per-host spacing; Retry-After with quiet retries for short waits. |

## Next rounds

Roughly in order of value versus effort.

1. **Windows integration.** System Media Transport Controls (media overlay, lock screen,
   global media keys when unfocused) via WinRT; jump list; installer (MSIX or Inno Setup)
   with file associations; code signing.
2. **Embed replacement artwork into the audio files** (§3.6). The TagLib code in the Mac
   repo's `Sources/CArtworkTags/CFArtworkTags.cpp` is portable; replace the CoreFoundation
   string helper and use `ReplaceFileW` for the atomic swap. Today "Replace Artwork"
   updates the library only.
3. **Last.fm scrobbling** (§10). Web auth flow, signature, batching (50), backoff
   (30 s → 900 s), pending queue in `LastfmPending.json`.
4. **Demo collection** (§3.8). Port the synth and the procedural covers with QPainter.
5. **Colors & Scene editor and generated scenes** (§4.2–4.4). Port the quilting generator
   and renderer exactly (seeded RNG, patch search, minimum-cut blending); replace Vision
   with OpenCV or Windows.Media.Ocr for text/face boxes.
6. **Visualizer** (§12). The built-in analyser (2048-point FFT, 64 bands) on the engine's
   sample tap (`PlaybackEngine::recentSamples`), then projectM 4 in a
   `QQuickFramebufferObject`.
7. **Karaoke and stem separation** (§11). Ship the Mac app's Python worker with an
   embeddable Python 3.12 + PyTorch (CUDA or CPU) and ONNX Runtime (DirectML). Then
   extend the engine to mix several sample-aligned stem streams with per-stem gains (§6.4,
   §6.5) — the ring buffer becomes one buffer per stem.
8. **Stable Diffusion scene refinement** (§4.5). ONNX Runtime + DirectML SD 1.5 img2img
   with a C++ DPM++ 2M scheduler and CLIP tokenizer, as an optional download.

## Known differences

- Output runs at the device's native rate instead of a fixed 48 kHz.
- Files and folders are imported from separate menu items, because Windows file dialogs
  can't pick both at once. Drag and drop accepts either.
- Global media keys only work while a Cover Flow window has focus, until SMTC lands.
- Qt 6.4 (Ubuntu 24.04's version) is supported for development, with QML ahead-of-time
  compilation turned off because of a qmlcachegen bug there. Windows builds use Qt 6.8.

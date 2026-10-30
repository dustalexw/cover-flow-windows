# Third-party notices

Cover Flow for Windows is built with the following components. Each is used as a
separately linked shared library (DLL), so it can be replaced with a compatible build.

| Component | Use | License |
|---|---|---|
| [Qt 6](https://www.qt.io) (Core, Gui, Network, Multimedia, Qml, Quick, Quick Controls 2) | Application framework, audio output | LGPL 3.0 |
| [FFmpeg](https://ffmpeg.org) (libavformat, libavcodec, libswresample, libavutil) | Audio decoding and resampling, embedded artwork | LGPL 2.1 or later (built without GPL components) |
| [TagLib](https://taglib.org) | Reading tags | LGPL 2.1 / MPL 1.1 |

Online lookups use [LRCLIB](https://lrclib.net) (lyrics), [MusicBrainz](https://musicbrainz.org)
(album search) and the [Cover Art Archive](https://coverartarchive.org) (covers). Only the
title and artist you search for are sent; audio never leaves your computer.

The source for these libraries is available from their project sites; the exact versions
are pinned by `vcpkg.json` (FFmpeg, TagLib) and `.github/workflows/build.yml` (Qt).

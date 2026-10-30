# Cover Flow → Windows (Qt 6 C++/QML) feature-parity port spec

Source: `/home/claude/cover-flow` (macOS app v1.8.2, Swift/SwiftUI/AppKit; SwiftPM package `Package.swift`).
All `file:line` references are relative to `Sources/CoverFlow/` unless they start with another top-level folder
(`Sources/CFFmpegAudio/…`, `Resources/…`, `scripts/…`). The `iPad/` target is out of scope.
Spec reflects the **working tree** as found: it contains uncommitted edits to `Downloads/MusicLookupClient.swift` (503 "busy" error,
`retryDelay`, automatic short retries) and `Tests/CoverFlowTests/MusicLookupTests.swift`; §9 documents that behaviour. No repo files were changed while writing this spec.

Conventions used below:
- "UD" = `UserDefaults.standard` key (macOS preferences plist `~/Library/Preferences/<bundle-id>.plist`). On Windows use
  `QSettings` (registry or INI) with **the same key names**.
- "AppSupport" = `~/Library/Application Support/CoverFlow/` (`Model/Models.swift:165-187`). Windows equivalent:
  `QStandardPaths::AppDataLocation` → `%APPDATA%/CoverFlow/`. An Info.plist override key `CoverFlowDataDirectory`
  (Models.swift:169) exists for tests; mirror it with an env var / command-line switch.

Table of contents
1. App shell, windows, menus, keyboard
2. Data model & persistence (Library.json, Playback.json, Artwork/, Scenes/, Lyrics/, Karaoke/, LastfmPending.json, UD keys)
3. Library: import, tag reading, grouping/sorting, artwork store, artwork embedding, demo collection
4. Album panel colours & generated scenes (quilting), Stable Diffusion refinement, Vision usage
5. Browsing UI: Cover Flow 3D view, Grid, selection, search, album detail panel, editor, queue, player bar, title marquee
6. Playback engine: gapless player, decoders, queue/shuffle/repeat, persistence, media keys
7. Album View window (Compact/Large/Immersive)
8. Lyrics (LyricsStore, LRC parser, window, LRCLIB lookup)
9. Online lookups (LRCLIB, MusicBrainz/Cover Art Archive)
10. Last.fm scrobbling
11. Karaoke / stem separation (Demucs, Mel-Band RoFormer, UVR MDX ONNX), Stem Mix
12. Visualizer (native Metal styles + projectM/MilkDrop)
13. Settings & About
14. Apple-only dependencies → Windows equivalents
15. Porting difficulty ranking & external assets

---

## 1. App shell, windows, menus, keyboard (`App/CoverFlowApp.swift`)

### 1.1 Object graph
`AppEnvironment` singleton (CoverFlowApp.swift:66-96) creates, in order: `ArtworkStore(AppSupport/Artwork)` → `LibraryStore` →
`LastFmScrobbler` → `PlayerEngine(library, scrobbler)` → `AppState` → `AlbumViewController`; lazily `LyricsWindowController`,
`KaraokeStore(AppSupport/Karaoke)`, `KaraokeWindowController`, `VisualizerController(player)`. Then `library.load()`,
`player.restore()`, and first-run demo seeding. Qt mapping: one C++ `AppEnvironment` owning QObject singletons registered with
`qmlRegisterSingletonInstance`; every window binds to the same `PlayerEngine`.

`AppState` (:6-59) — UI state shared by windows: `browseMode` (UD `browseMode`, default coverFlow), `sortOrder` (UD `sortOrder`, default
artist), `theme` (UD `theme`, default **dark**; changing it re-themes Album View, Lyrics and Karaoke windows), `searchText`,
`searchFocusRequest` counter, `browsedAlbumID` (UD `browsedAlbum`; *independent of what is playing*), `selection`,
`selectedAlbumRemoval`, `detailAlbum`, `editAlbum`, `appearanceAlbum`, `showQueue` (UD), `confirmRemoval`, `revealRequest`
(token-bumped). `reveal(id)` clears search, sets browsed album and asks the active browser to scroll/animate to it.

### 1.2 Windows
| Window | Size / min | Persisted frame | Notes |
|---|---|---|---|
| Main "Cover Flow" | 1220×820 / 720×540 | system | single instance; reopened on Dock click |
| About Cover Flow | content size, hidden title bar, centred | – | icon 112, "Browse your music by its covers.", version/build, 3 feature rows, links Releases / Source on GitHub / Report an issue (github.com/dustalexw/cover-flow…), credits (projectM, FFmpeg LGPL) |
| Settings (⌘,) | 520×620 | – | §13 |
| Album View | 320×430 / 260×340 | `CoverFlow.AlbumView.Frame` | §7 |
| Lyrics | 560×700 / 440×360 | `CoverFlow.Lyrics.Frame` | §8 |
| Karaoke | 680×820 / 600×740 | `CoverFlow.Karaoke.Frame` | §11 |
| Visualizer | 1000×660 / 720×420 | `CoverFlow.Visualizer.Frame` | §12 |

Sheets on the main window: album track panel, Edit Album Info, Colors & Scene, Find Artwork/Lyrics, confirmations.
The app does not quit when the last window closes; quitting saves playback + library, cancels karaoke, stores Album View open state.

### 1.3 Menu bar (CoverFlowApp.swift:187-279) — Windows: use Ctrl for ⌘, Alt for ⌥
- **Cover Flow**: About Cover Flow…, Settings… (⌘,).
- **File**: Import Music… ⌘O; Add/Rebuild Demo Collection; Remove Demo Collection (when present).
- **View** (before toolbar items): Cover Flow ⌘1; Grid ⌘2; Music Visualizer ⌘3; Lyrics ⌘4; Karaoke ⌘5; Search Music ⌘F;
  Sort By ▸ Artist / Title / Recently Added / Recently Played (checkmark); Show/Hide Queue ⌘U; Show Playing Album ⌘L (disabled
  without track); Open/Close Album View ⇧⌘A; Album View Size ▸ Compact ⌥⌘1, Large ⌥⌘2, Immersive ⌥⌘3.
- **Controls**: Play/Pause ⌘P; Next Track ⌘→; Previous Track ⌘←; Skip Forward 10 Seconds ⌥⌘→; Skip Back 10 Seconds ⌥⌘←;
  Increase Volume ⌘↑ (+0.1); Decrease Volume ⌘↓; Mute/Unmute ⌥⌘↓; Turn Shuffle On/Off ⌃⌘S; Repeat (label shows the *next* mode)
  ⌃⌘R. Transport items disabled without a current track.
- Context keys per view: Cover Flow ←/→/Home/End/PgUp/PgDn/Return/Space/Esc/⌘A/Shift+arrows (§5.2); Grid Space/Esc/Return/⌘A;
  album panel Space/Esc/outside click; Album View Space/←/→/⌥←/⌥→/1/2/3/F/Esc; Lyrics none extra; Karaoke Space/1/2/3/P;
  Visualizer Space/←/→/Esc.
- Global media keys / Now Playing via MPRemoteCommandCenter (§6.7).

### 1.4 Visual system (Support/Support.swift)
Dark default: near-black base, single brass accent, hairlines (white/black 9 %). Pill buttons (13 semibold, padding 16×8, capsule,
pressed scale 0.97, disabled 40 %), round icon buttons (size 30 default, glyph 46 % of size, pressed 0.92, active = accent tint,
prominent = accent fill), DEMO chip (9.5 bold, tracking 1.2, accent outline), `NowPlayingBars` (3 bars, 20 fps,
`level = 0.35 + 0.65|sin(t(2.6+0.9i) + 1.3i)|`, static [0.55,0.9,0.4] when paused/Reduce Motion, bar width 0.22·height).
Album titles use the serif system font semibold (New York) — Windows: e.g. "Georgia"/"Cambria". Panel uses Helvetica Neue — ship
or substitute "Arial"/"Segoe UI". Times: `formatTime` → `m:ss` or `h:mm:ss`. Reduce Motion (macOS accessibility) → Windows
`SPI_GETCLIENTAREAANIMATION` == FALSE.

## 2. Data model & persistence

### 2.1 Core value types (`Model/Models.swift`)

`Track` (Models.swift:5-26) — Codable struct, JSON keys = Swift property names:

| key | type | notes |
|---|---|---|
| `id` | UUID string (uppercase, Swift default) | |
| `fileURL` | **Swift `URL` Codable form** | Swift's JSONEncoder encodes `URL` as an object `{"relative":"file:///abs/path/Song.flac"}` (and `"base"` if relative). Windows reader must accept this form; writer should emit the same to keep files interchangeable. |
| `title`, `artist`, `albumTitle` | string | |
| `albumArtist` | string? | optional keys are **omitted** when nil |
| `trackNumber`, `discNumber`, `year` | int? | |
| `duration` | double (seconds) | |
| `genre` | string? | |
| `dateAdded` | Date | Swift default `JSONEncoder` date strategy = **Double seconds since 2001-01-01T00:00:00Z** (Apple reference date, = Unix − 978307200). |
| `isDemo`, `missingMetadata` | bool | |
| `fingerprint` | string | `normTitle|normArtist|normAlbum|round(duration)` (LibraryStore.swift:302-305) |

`sortKey` = `(discNumber ?? 1, trackNumber ?? Int.max, lowercased file name)` (Models.swift:23-25); third component compared with
`localizedStandardCompare` (natural/numeric-aware, case-insensitive) — on Windows use `QCollator` with `setNumericMode(true)`,
case-insensitive.

`Album` (Models.swift:29-58):
`id, groupKey, title, artist, year?, genre?, trackIDs:[UUID], artworkFile?:String (file name inside Artwork/),
artworkPixelWidth?, artworkPixelHeight?, artworkVersion:Int, tintHex?:"#RRGGBB", dateAdded:Date, lastPlayed?:Date,
isDemo, missingMetadata, userEdited, appearance?:AlbumAppearance`.
- `artworkAspect` = w/h or 1. `artworkKey` = `"<UUID>-<artworkVersion>"` — used as the cache key everywhere (palettes, scenes).
- `appearance` is stored as `nil` when equal to the default (LibraryStore.swift:392).

`AlbumAppearance` (Models.swift:60-89): `backgroundHex?, textHex?, accentHex?, sceneEnabled=true, sceneStrength=0.55,
sceneVariation=0, sceneRefinement:Bool?, refinementStyle:"extend"|"subtle"|"ornate"?, immersiveRefinementStyle?`.
Derived: `strength` = clamp(sceneStrength,0,1) (0.55 if non-finite); `requestedRefinement` = sceneRefinement==true ? style : nil;
`immersiveRequestedRefinement` = immersiveRefinementStyle ?? (refinesScene ? .extend : nil);
`key` = `"\(bg ?? "auto")|\(text ?? "auto")|\(accent ?? "auto")|\(sceneEnabled)|\(strength)|\(variation)|\(refinement ?? "none")"`
(Models.swift:86-88; note Swift prints Double `0.55` as `0.55`, bools as `true/false`).
`immersiveKey` = key + `"|immersive-" + (immersiveRequestedRefinement ?? "none")`.

`SceneRefinementStyle` (Models.swift:92-123): `extend` (label "Extend", emphasis 1.3), `subtle` (1.0), `ornate` (1.4).

Enums: `SortOrder` = `artist|title|recentlyAdded|recentlyPlayed` (labels "Artist", "Title", "Recently Added", "Recently Played");
`BrowseMode` = `coverFlow|grid`; `RepeatMode` = `off|all|one`, cycle off→all→one→off, labels "Repeat Off/All/One";
`AppTheme` = `dark|light|system` ("Dark", "Light", "Match System"), default dark.

`AlbumSelection` (Model/AlbumSelection.swift:3-29): set of ids + anchor.
- click: ids={id}, anchor=id. Cmd-click (toggle): toggle membership, anchor=id. Shift-click (extend): range between anchor and id
  in the *displayed order*; with toggle as well → union with existing.
- marquee: `ids = base ∪ hits`; on finish anchor = first displayed id that is selected. `selectAll(ordered)`; `clear()`;
  `retainVisible(ordered)` intersects with visible list (called when search/sort changes).

### 2.2 Files under AppSupport

| Path | Format | Writer |
|---|---|---|
| `Library.json` | `{"version":1,"albums":[Album…],"tracks":[Track…]}` (LibraryStore.swift:53-57,175). Saved atomically. Debounced save 0.6 s after edits (`scheduleSave`, :163-170); immediate `saveNow()` after import/removal/quit. | LibraryStore |
| `Library-unreadable-<unix>.json` | Backup when Library.json fails to decode; app starts empty and shows notice (:155-160). | LibraryStore |
| `Playback.json` | queue/position/shuffle/repeat/volume — schema in §6.6 | PlayerEngine |
| `Artwork/<albumUUID>-<randomUUID>.<ext>` | Original cover bytes, ext from detected image type (jpg/png/…; "img" fallback) (ArtworkStore.swift:65-68). Older files with the same album prefix are deleted after the new one is written (:70-74). | ArtworkStore |
| `Artwork/Scenes/<key>.png` | Generated/refined scene PNGs — key formats in §4 | AlbumSceneStore |
| `Demo Collection/<Album Title>/NN Name.wav` | Synthesized demo audio | DemoLibrary |
| `Lyrics/` | Imported lyrics, see §8 | LyricsStore |
| `Karaoke/` | Prepared stems, model registry, caches — see §11 | KaraokeStore |
| `LastfmPending.json` | Pending scrobbles — see §10 | LastFmScrobbler |

UD keys found so far (all listed again in §13): `browseMode`, `sortOrder`, `theme`, `browsedAlbum` (UUID string),
`showQueue` (bool), `didSeedDemo` (bool), `albumViewOpen` (bool) (App/CoverFlowApp.swift:7-41,91,105).

---

## 3. Library

### 3.1 Import (`Library/LibraryStore.swift`)

- File ▸ **Import Music…** (Ctrl+O on Windows; ⌘O) opens a multi-select file+folder picker. Message: "Choose audio files or folders.
  Cover Flow reads them where they are — nothing is copied or moved." (:195-208). Files are referenced in place.
- Only one import at a time: otherwise notice "An import is already running. Try again when it finishes." (:211-214).
- Enumeration (:219-255): folders are walked recursively, skipping hidden files and package contents, following symlinks to
  regular files. A file is **audio** if its lowercase extension is in `supportedExtensions`; **unsupported** if in
  `knownUnsupported`; an explicitly chosen file with any other non-empty extension is also reported unsupported. Results sorted
  by full path with natural compare.
- Supported extensions (MetadataReader.swift:35-44 + FFmpegAudio.swift:11-14):
  `mp3 m4a m4b m4p aac adts wav wave aif aiff aifc caf flac mp4 3gp ac3 eac3 amr opus ogg oga` +
  FFmpeg set `ogg oga spx ape wv wma asf dsf dff mka webm weba mpc mpp mp+ tta tak shn dts w64`.
  Known-unsupported (reported, not imported): `mid midi ra rm xm mod` (the others in the list are subtracted because FFmpeg handles them).
  **Windows:** route *everything* through FFmpeg (it handles all of these incl. Opus/ALAC/AAC); keep the same two lists so
  the import report reads identically.
- Per file (:268-293): duplicate if resolved+standardized path already known, **or** if its fingerprint already exists
  (fingerprint = `normalizedKey(title)|normalizedKey(artist)|normalizedKey(album)|Int(duration.rounded())`).
  `MetadataError.notPlayable` → unsupported; other errors → `failed` ("name (reason)").
- Progress model `ImportProgress{done,total,current}`; first state "Looking for audio files…".
- After loop: `finalizeAlbum` for touched albums, rebuild indexes, save, show `ImportReport`.
- `ImportReport.headline` (:22-33): "No audio files found there." / "That track is already in your collection." / "Those N tracks
  are already in your collection." / "Nothing new was imported." / "Imported N track(s) into M album(s)."; `details` (:35-50):
  "N duplicate(s) skipped" (only when something was added), "Unsupported format: a, b, c and K more" (first 3), "Couldn’t read:
  x; y …" (first 2). Accessibility announcement of the headline when added>0.

`String.normalizedKey` (Support/Support.swift:10-14): case-, diacritic- and width-insensitive fold, trim, collapse internal
whitespace to single spaces. Windows: `QString::normalized(NFKD)` + strip combining marks + `toCaseFolded()` + simplified().
`String.stableHash` (Support.swift:17-21): djb2 over UTF-8 bytes, `h = 5381; h = h*33 + b` (UInt64 wrapping). Used as RNG seeds
for scenes — port exactly for reproducible scenes.

### 3.2 Album grouping & ordering rules (LibraryStore.swift:309-386)

- `artist` = tag artist ?? albumArtist ?? "Unknown Artist".
- `albumTitle` = tag album ?? **parent folder name**.
- `groupKey` = if album tag present: `"album:" + normalizedKey(album) + "|" + (normalizedKey(albumArtist) ?? "dir:" + folderPath)`;
  else `"folder:" + folderPath`. (So untagged-album-artist albums are split per folder; tagged album artist groups across folders.)
- Track title = tag title ?? filename-derived title (see fallbacks).
- New album: `artist = albumArtist ?? artist`, `missingMetadata = (album tag missing)`, `artworkVersion 0`.
  Existing album: append track; fill year/genre if nil.
- Artwork: the first track bringing artwork data for an album without artwork saves it (ArtworkStore.save) and bumps `artworkVersion`.
- `finalizeAlbum`: sort tracks by `(disc ?? 1, track ?? ∞, natural(filename))`. If not `userEdited`: artist = first non-nil track
  `albumArtist`; else if >1 distinct normalized track artists → **"Various Artists"**; else the single artist.

### 3.3 Sorting & search (`visibleAlbums`, :106-142)

- Search: `q = normalizedKey(searchText)`; album matches if normalized album title/artist contains q, or any track title/artist contains q.
- `byArtist`: normalized artist (natural compare) → year ascending (nil = 9999) → title natural compare.
- `title`: natural compare on title. `recentlyAdded`: dateAdded desc, tie → byArtist. `recentlyPlayed`: lastPlayed desc; albums
  with lastPlayed before nil; both nil → byArtist.
- `markPlayed(trackID)` sets album `lastPlayed = now` (debounced save) (:540-544).

### 3.4 Tag reading (`Library/MetadataReader.swift`)

Order of readers (MetadataIngestionWorker, :342-369; a serial actor so imports don't fan out):
1. If file starts with `OggS` and contains an Opus stream → custom Ogg Opus reader (duration from granules, Vorbis comments,
   METADATA_BLOCK_PICTURE). Surround Opus (mapping family ≠0 or >2 ch) → FFmpeg instead.
2. Else if extension in FFmpeg set (or Ogg non-Opus) → FFmpeg tags (`readFFmpeg`, :93-119):
   title=`title`; artist=`artist`; album=`album`; albumArtist = first of `album_artist, albumartist, "album artist", WM/AlbumArtist`;
   track = `track, tracknumber, WM/TrackNumber` → parseNumberPair; disc = `disc, discnumber, WM/PartOfSet`; year = `date, year,
   originaldate, WM/Year` → parseYear; genre = `genre, WM/Genre`; picture = `cf_audio_picture`. Zero-length → "The file contains no audio."
3. Else AVFoundation (`readAsset`, :121-204): common keys title/artist/albumName/artwork/creationDate, plus iTunes/ID3/QuickTime
   identifiers: trkn/disk (binary: big-endian UInt16 at bytes 2-3), TRCK, TPOS, aART/TPE2(band) → albumArtist, genre (`©gen`,
   TCON with `^\(\d+\)` stripped; purely numeric genres discarded), year from release date/TYER/TDRC/TDRL. For `.flac`, the
   custom FLAC reader (:249-274) fills Vorbis comments (block type 4) and the first PICTURE block (type 6), max 64 blocks.
   **Windows:** use TagLib (already a dependency) for all tag reading + FFmpeg for duration; reproduce the same field priority
   ("first non-empty wins") and the fallbacks below.
- Vorbis comment keys (:306-323): TITLE, ARTIST, ALBUM, ALBUMARTIST / "ALBUM ARTIST" / ALBUM_ARTIST, TRACKNUMBER, DISCNUMBER,
  DATE/YEAR/ORIGINALDATE, GENRE; METADATA_BLOCK_PICTURE (base64 FLAC picture; prefer type 3 front cover). Max 1000 comments.
- `parseNumberPair("3/12")` → 3; only positive ints (:206-209). `parseYear` = first match of `(1[89]|20)\d\d` (:211-214).
- **Fallbacks** (:359-366):
  - Filename regex (:216-230): `^\s*(?:(\d{1,2})[-.](?=\d))?(\d{1,3})\s*(?:[.\-_)]\s*|\s+)(.+)$` → (disc, track, title).
    E.g. `1-03 Song` → disc 1, track 3, "Song"; `07. Song` → track 7. No match → whole basename as title.
  - Missing title → filename title, `usedFallback=true`. Missing track/disc → from filename. Missing album → `usedFallback=true`.
  - Missing artwork → folder image (:232-245): first file in the same folder whose lowercase basename is one of
    `cover, folder, front, album, artwork, albumart` and extension in `jpg jpeg png heic webp tif tiff`.

### 3.5 Artwork store (`Library/ArtworkStore.swift`, `Library/ArtworkDecoder.swift`)

Artwork lookup order for an album (complete chain): embedded picture of the first imported track that has one
(Opus METADATA_BLOCK_PICTURE front-cover-preferred → FFmpeg attached pic preferring "front" comment/APE "Cover Art (Front)" →
AVFoundation common artwork / FLAC PICTURE) → folder image by name list above → none (generated placeholder, §5.7).
User can later replace via file picker, via Find Artwork Online (§9), or remove (library only).

- `save(data:albumID:)` (:54-77): decode header with ImageIO; swap w/h if EXIF orientation ≥5; reject 0-size ("That file isn’t an
  image Cover Flow can read."). Writes original bytes. Computes `tintHex` with `averageColor`.
- `averageColor` (:177-204): thumbnail max 48 px → draw into 12×12 sRGB → weighted mean with weight `0.35 + (max−min channel)`
  (favour saturated pixels) → hex `#RRGGBB`.
- Thumbnail size buckets (:87-92): ≤160→160, ≤360→360, ≤720→720, else 1100. "Full" = max 4096 px.
- `ArtworkRasterCache` (ArtworkDecoder.swift:13-73): two LRU partitions: thumbnails budget **192 MiB / 512 entries**, full-size
  (pixels>1100) **96 MiB / 3 entries**. Key = `standardizedPath@pixels`. Decoding (`decodeImage`, :96-105) = orientation-corrected
  thumbnail with max pixel size; serialized on one actor, skipping cancelled requests. **Windows:** `QImageReader` with
  `setAutoTransform(true)` + `setScaledSize`, a single worker thread, same LRU budgets.
- Palette cache: `detailPaletteCache` count 512 keyed by `artworkKey`; panel-style cache 32 MiB cost (:47-48).
- `removeArtwork` deletes the file, clears `artworkFile`/`tintHex`, bumps version (LibraryStore.swift:485-492). Library only.

### 3.6 Replace Artwork → embed into audio files (`Library/ArtworkTagWriter.swift`, `Sources/CArtworkTags/CFArtworkTags.cpp`)

Flow (LibraryStore.swift:426-483): picker title "Choose Artwork", message "Choose this album’s cover. It will also be embedded in
the album’s audio files.", prompt "Use Image". One replacement per album at a time. Notice "Updating artwork and embedding it in
N audio files…". Saves library cover first (bumps version) then embeds serially. Final notice = report summary
("Artwork embedded in N audio file(s). K file(s) couldn’t be updated."); if failures, a report sheet lists them. If saved width
< 600: append " The W×H cover will appear smaller in Album View to stay sharp."

`EmbeddedArtwork.prepare` (ArtworkTagWriter.swift:13-39): keep original bytes if JPEG/PNG, <12 MiB, max side ≤8192, EXIF orientation 1;
else re-encode as JPEG quality **0.92** from an orientation-corrected image ≤4096 px; must be <12 MiB.

Writable extensions (:62-65): `mp3 m4a m4b mp4 flac wav wave aif aiff aifc opus ogg oga spx ape wv mpc wma asf tta dsf dff mka webm weba`.

Safe write (:90-124): must be a regular, writable, non-immutable file ("The audio file is locked or read-only."). Copy to
same-directory temp `.coverflow-<UUID>.<ext>` (`wave`→`wav`, `weba`→`webm` so TagLib picks the right format), call
`cf_artwork_write` on the temp, fsync, verify the original's device/inode/size/mtime unchanged ("The file changed during the update.
Try again."), then atomic `rename` over the original. **Windows:** `ReplaceFileW`/`MoveFileExW(MOVEFILE_REPLACE_EXISTING)`;
compare size + last-write time + file index (`GetFileInformationByHandle`).

`cf_artwork_write` (CFArtworkTags.cpp:60-119) — portable TagLib 2.x code, reuse as-is except the CoreFoundation string helper
(`nativeString`, :26-34: decode UTF-8, fall back to MacRoman) → replace with UTF-8→Latin-1 fallback:
1. Reject empty/≥12 MiB. Open with `FileRef(path,false)`; invalid → "The audio file is damaged or this format cannot store cover art."
2. AIFF: copy NAME/AUTH/ANNO/"(c) " chunk text into the ID3 tag if those fields are empty (`carryAIFFText`, :36-58).
3. MP3/WAV: if the ID3v2 tag is empty, seed it from the file's current PropertyMap (so ID3v1/RIFF INFO text isn't hidden).
4. PICTURE list = new `{data, mimeType, pictureType:"Front Cover", width, height, colorDepth:32}` + every existing picture whose
   type is non-empty and not "Front Cover"/"Other" (keep back covers, booklets).
5. Verify PropertyMap unchanged by the edit; MP3 saved as ID3v2.4, `AllTags, StripNone, DoNotDuplicate`.
6. Reopen, compare properties and that first PICTURE data == image bytes.

### 3.7 Removal rules

- **Remove from Collection… (track)** (LibraryStore.swift:495-516): removes tracks from `tracks` and their albums; generated demo
  audio files are deleted from disk, real files never; an album left empty is removed together with its artwork file (and scenes);
  remaining affected albums are re-finalized; saved immediately; `onTracksRemoved` tells the player to drop them from the queue.
  UI requires confirmation (right-click track in album panel).
- **Remove album(s)** (:519-534): removes albums + tracks + artwork; demo audio deleted unless called from demo rebuild.
- `removeAll()`; `removeDemoCollection()`.

### 3.8 Demo collection (`Library/DemoLibrary.swift`)

Seeded once on first launch if the library is empty (UD `didSeedDemo`) (CoverFlowApp.swift:90-94). File menu: "Add Demo
Collection" / "Rebuild Demo Collection" / "Remove Demo Collection". Five albums (DemoLibrary.swift:26-47) with title, artist,
year, colour triplets, root Hz, tempo, chord progressions, 3 track names each, e.g. "Low Tide Hours" — Halcyon Tapes — 2019 —
220 Hz — 84 BPM. Tracks: 16-bit mono WAV at **22 050 Hz**, length `26 + ((ai*3+ti)%4)*3` s, 1.5 s fades, peak-normalised to 0.85,
written to `Demo Collection/<title>/NN Name.wav`. Track fingerprint `demo|<album>|<name>`, genre "Demo", groupKey
`demo:<normalized title>`, dateAdded = now − ai days. Covers: 1400×1400 procedurally drawn (styles horizon/cityGrid/grooves/
stripes/glass) with artist text and a "DEMO" label. Demo tracks are never scrobbled. Port the synth (DemoLibrary.swift:91-165) and
cover drawing (:186-290) literally with QPainter if you want identical demos.

## 4. Album panel colours, generated scenes, Stable Diffusion, Vision

### 4.1 Panel palette (`Views/AlbumDetailPalette.swift`)

Colour math (sRGB doubles 0–1):
- `luminance` = WCAG relative luminance (linearize `c<=0.04045 ? c/12.92 : ((c+0.055)/1.055)^2.4`; weights .2126/.7152/.0722) (:12-15).
- `contrast` = (Lmax+0.05)/(Lmin+0.05). `saturation` = max−min. `mixed(other, t)` = linear lerp.
- `readable(on:bg, minimum=4.8)` (:35-46): if contrast < min, lerp toward white or black (whichever contrasts more with bg)
  using 20-step bisection to the smallest t reaching `min(minimum, ink contrast)`.
- `init(background, candidates)` (:54-62): ink = white/black (higher contrast); foreground = ink mixed 8 % toward bg if still ≥4.5
  else ink; secondary = ink mixed 28 % toward bg if ≥4.5 else foreground; accent = first candidate with saturation>0.12 and
  contrast≥4.5, else foreground.
- `extract(from:)` (:88-136): draw cover into 64×64 sRGB; skip alpha≤0.5; un-premultiply; bucket by 4-bit-per-channel key
  `(r*15)<<8|(g*15)<<4|(b*15)`, track count and **edgeCount (x<12, the left edge that meets the track list)**.
  Background = bucket with max `edgeCount*4 + count` (ties → lower key). Accent candidates sorted by `count*(0.25+saturation)`.
- `fallback(album)` (:78-86): tintHex if present; else HSB(hue = stableHash(title+artist) % 360 / 360, s 0.32, b 0.30) — the same hue
  as the generated placeholder cover.
- `customized(appearance)` (:69-76): user background replaces bg; user text/accent forced `readable`; secondary = fg mixed 22 % to bg,
  made readable.
- Button style in the panel (:139-158): surface = bg mixed 12 % toward white (8 % toward fg when pressed), Helvetica Neue 12 medium,
  corner radius 4, 24 %-fg border, 1 px 20 %-white top highlight.
- Global theme palette (Support.swift:66-102): dark base `#101012` (0.063,0.063,0.071), light base (0.955,0.945,0.925);
  brass accent dark (0.95,0.77,0.49) / light (0.52,0.34,0.09); `tint(hex)` dark: HSB(h, min(s,0.6), clamp(b*0.8,0.28,0.6)),
  light: HSB(h, min(s,0.5), max(b,0.72)).

### 4.2 Colors & Scene editor (`Views/AlbumAppearanceView.swift`)

Sheet 920×610, title "Colors & Scene", live preview of the album track panel on the right. Controls: GroupBox "Colors"
(Background / Text / Accent: colour picker + hex field; invalid hex → red "Use a six-digit hex color, such as #183A4B."; otherwise
"Text and accent colors are adjusted when needed to keep them readable."), GroupBox "Cover Scene": "Show generated scene" toggle,
"Scene strength" slider 0–1 shown as %, "Regenerate Scene" (variation += 1), "Stable Diffusion" menu Off / Extend / Subtle /
Ornate (only when the SD model is available), status lines ("Refining scene with Stable Diffusion…", "Couldn’t refine: <reason>
Showing the regular scene.", "Refined with Stable Diffusion (Style)."), "Reset to Cover", Cancel / Save. Save normalizes hex to
`#RRGGBB` uppercase and calls `updateAppearance` (stored nil if equal to defaults). Preview recomputes when any setting changes.

### 4.3 Album panel scene pipeline (`Library/AlbumSceneStore.swift`, `ArtworkStore.albumPanelStyle`)

`albumPanelStyle(album, appearance)` (ArtworkStore.swift:135-159): palette = extract(720-px thumbnail) or fallback, customized; if
scene enabled and strength>0: get scene (quilted or refined) → `AlbumSceneRenderer.render(scene, colors, strength, emphasis)`.
Emphasis = style.emphasis only when refinement succeeded. Cache key `artworkKey|metadata|appearance.key`.

Scene cache (AlbumSceneStore.swift):
- Directory `AppSupport/Artwork/Scenes/`.
- Panel scene key (:28-30): `"<albumUUID>-<artworkVersion>-scene-v5-<variation>"` + (refined ? `"-sd2-<style>"` : "") → `<key>.png`.
- In-memory LRU of **4** CGImages (:186-192). In-flight dedupe by key. Failed refinement reasons remembered per session (not retried).
- After writing a new panel scene, delete other files for the same album prefix that are not immersive and have the same
  refined/non-refined kind (keeps one quilted + one refined per album) (:104-112).
- Immersive key (:116-119): `"<artworkKey>-immersive-v3-<style|base>-<layout.key>-<stableHash(appearance.immersiveKey)>"`;
  keeps one layout per style per album (:153-163). `layout.key` = `"W-H-x-y-w-h"` ints.
- `remove(for:albumID)` purges memory, pending tasks and files with prefix `<albumUUID>-` (called when artwork file removed).
- Source cover = artwork thumbnail ≤1024 px, orientation-corrected.

**Quilted scene generator** (`AlbumSceneGenerator.generate`, :198-400) — deterministic given `seed = stableHash(key)`; port exactly:
1. Canvas default **2160×1320** (:199); valid sizes 128…2160 × 128…1320.
2. `coverRect` (:203-211): `sourceLimit = min(768, W*0.36, H*0.6)`; `scale = min(1, sourceLimit/max(iw,ih))`; `sw,sh = max(32, i*scale)`;
   `x = min(W - sw - 12, Int(W*0.77) - sw/2)`, `y = (H - sh)/2`. (Cover sits at 77 % across, the track list is on the left.)
3. Vision on the cover (only boxes are used): `VNRecognizeTextRequest` (fast, no language correction) → text boxes inset −3 px;
   `VNDetectFaceRectanglesRequest` + `VNGenerateAttentionBasedSaliencyImageRequest` salient objects → subject boxes inset −8 px.
   Vision boxes are normalized bottom-left; convert with `y = (1 - maxY) * sh`.
4. `clearLettering(textBoxes)` (:640-672): fill each box by bilinear interpolation of the border rows/columns 2 px outside it,
   weighting vertical vs horizontal fills by 1/(box height+2) and 1/(box width+2).
5. Low-frequency field: downscale cover to ≤48×48, upscale back to sw×sh (`low`). `SceneField.extend` (:408-482): clamp-to-edge
   extension of `low` to W×H, build a 2×2 box mip pyramid (max level **6**), per pixel sample trilinearly at
   `level = clamp(log2(max(1, distance*0.35)), 0, top)` where distance = Euclidean distance outside the cover rect.
6. Texture amount `0.75 * extensionTexture * (1 - 0.45*smoothstep(t))`, `t = clamp(dist/(W*0.6))` (extensionTexture = 1 panel, 0.28 immersive).
7. Copy cover into place (locked). Patch size `patch = min(128, max(16, min(sw,sh)/3))`, `overlap = max(4, patch/3)`, `step = patch-overlap`.
8. Candidate exemplars: grid with stride `max(6, patch/4)`, excluding any intersecting text/subject boxes; variation = sum of squared
   diagonal (+4,+4) differences sampled every 4 px / patch². Keep the quietest **65 %** (sorted by variation, y, x). If none → return
   cover blurred to 8×8 and upscaled to canvas.
9. RNG: 64-bit LCG `r = r*6364136223846793005 + 1442695040888963407`, `next(n) = (r>>32) % n`, seeded with stableHash(key).
10. Placements: px from `W-patch` down to `-step` step `-step`, py from 0 step `step`; clamp `tx=max(0,px)`, `ty=min(py,H-patch)`; dedupe;
    sort by Manhattan-ish distance to cover (dx+dy), then x desc, then y asc. Skip placements fully inside the cover.
11. For each placement: start with a random candidate, then **120** random trials; error over known pixels sampled every
    `max(4, patch/12)`: `(output-field) - (source-low)*amount` squared, averaged, + `variation*(tx < 2W/3 ? 1.4 : 0.3)` + `uses*24`.
12. Proposed patch pixel = field + (source−low)*amount. Blend with `SceneQuilting.weights` (:486-555): for each of 4 edges with ≥patch/2
    known pixels, compute overlap SSD costs, dynamic-programming minimum cut (3-neighbour), alpha = clamp((inward−cut)/6 + 0.5) (3-px
    feather); weight = min over edges. Locked (cover) pixels never change.

**Renderer** (`AlbumSceneRenderer.render`, :559-610): per pixel, `headerFade = clamp((y/H − 0.12)/0.2)`,
`header = 0.65 + 0.35*smoothstep(headerFade)`; `side = clamp((x/W − 0.52)/0.3)`;
`amount = min(1, strength*emphasis*(0.18 + 0.44*smoothstep(side))*header)`; result = lerp(background, scenePixel, amount) quantized
to 8 bit; if the quantized colour has contrast <4.5 against any of foreground/secondary/accent, bisect alpha (10 iterations) down to
the largest safe value. This keeps the left (track list) side calm and protects text contrast per pixel.

### 4.4 Immersive Album View scene (`AlbumView/ImmersiveArtworkScene.swift`)

`ImmersiveSceneLayout.make(viewport, cover)` (:8-19): `scale = min(2, 2160/vw, 1320/vh)`; size = floor(viewport*scale); cover area
= floor(rect*scale) ∩ bounds, must be ≥32×32. Generation = quilted with the exact cover rect and `extensionTexture 0.28`, then
optional SD refinement with `immersiveRequestedRefinement`, falling back to the quilted image. View: black → cover scaled-to-fill,
blur radius 50, opacity 0.65 (shown while generating) → scene image fades in (0.5 s ease, none with Reduce Motion) → vertical
gradient black 32 % top, clear, clear, black 48 % bottom. Requests debounce 200 ms; resize cancels stale work.

### 4.5 Stable Diffusion refinement (`Library/AlbumSceneRefiner.swift`)

- Model: Apple `coreml-stable-diffusion-v1-5-palettized_split_einsum_v2_compiled` (6-bit palettized SD 1.5, split-einsum for the
  Neural Engine), from `https://huggingface.co/apple/coreml-stable-diffusion-v1-5-palettized/resolve/main/<name>.zip`, SHA-256
  `49a6ac1f62e12a2b3e426730d686fa466e30cba11c03b85305775714fb9814ec`, ~1.6 GB, bundled in `Contents/Resources/StableDiffusion/`
  (scripts/fetch-sd-model.sh:9-11). Required: TextEncoder, VAEEncoder, VAEDecoder, Unet (or UnetChunk1/2), vocab.json, merges.txt;
  safety checker enabled. Available only on arm64 (:73-85); `unavailableReason` = "Stable Diffusion isn’t included in this build or
  isn’t supported on this Mac."
- Pipeline: `apple/ml-stable-diffusion` 1.1.1, compute units `.cpuAndNeuralEngine`, `reduceMemory` if RAM ≤ 8 GB; loaded lazily,
  **unloaded after 90 s idle** (:90, :173-179); all work on one serial queue; cancellable via progress callback.
- Per style settings (:28-51):

| style | strength | steps | guidance | detail | feather px | coverEcho |
|---|---|---|---|---|---|---|
| extend | 0.60 | 28 | 6.5 | 0 | 48 | 0.9 |
| subtle | 0.50 | 24 | 6.5 | 0.6 | 24 | 0 |
| ornate | 0.78 | 30 | 8.0 | 0.12 | 40 | 0.55 |

  Prompts (verbatim): extend = "seamless extension of the album cover artwork beyond its edges, the same scene continuing naturally,
  same style, same lighting, same textures{palette}, coherent, no text"; subtle = "soft abstract background extending album cover art,
  painterly texture, cohesive colors{palette}, no text"; ornate = "ornate decorative illustration continuing the album cover artwork,
  intricate filigree, flourishes and patterns, rich painterly detail, art nouveau, elegant, highly detailed{palette}, no text".
  `{palette}` = ", color palette of a, b, c". Base negative: "text, letters, words, typography, watermark, logo, signature, frame,
  border, face, person, people, nsfw"; extend adds ", ornament, filigree, decorative pattern, flourishes, mirrored, symmetrical,
  kaleidoscope, collage, duplicate, repeated objects, oversaturated"; ornate adds ", plain, flat, empty, simple gradient, blurry, low
  detail, washed out". Scheduler **DPM-Solver++ multistep**; seed per tile = `UInt32(truncating: seed + tileIndex)`.
- `SceneColorWords.describe` (:189-245): 8×8 grid over the cover, average each cell (every 2nd px), name it: low saturation (<0.18)
  or brightness <0.08 → black/charcoal grey/silver grey/white (weight 0.6) by brightness thresholds 0.15/0.45/0.8; else hue bands
  red<15, orange/brown<40 (brown if b<0.55), gold/olive<65, green<160, teal<190, blue<250, purple<285, magenta<330, crimson<345, red;
  prefix "deep " if b<0.35, "pale " if b>0.85 && s<0.45; weight 1+s. Top 3 by weight (ties by first-seen).
- `SceneRefinementLayout.tiles` (:257-268): square tiles of side = short side along the long side; count =
  `ceil((long-side)/(side*0.75)) + 1`; evenly spaced offsets. Each tile cropped and resized to **512×512**.
- `echoCover` (:276-306): pixels outside the cover blend toward the cover pixel reflected back and forth across its edges, weight
  `amount*(1 - 0.4*min(1, dist/(0.6*max(W,H))))`.
- `compose` (:311-367): tent-weighted (min distance to tile ends along the long axis +1) average of tiles resized back; add
  `detail * (scene − blur(scene to 512-equivalent))`; restore cover with feather `keep = 1 − clamp(dist/feather)`.
- Failure strings: "Cancelled.", "The scene couldn’t be read.", "The safety checker flagged the result.", "The refined tiles couldn’t
  be combined.", "Stable Diffusion couldn’t run: …". Failure → quilted scene + note in the editor.
- **Windows equivalent:** ONNX Runtime + DirectML running SD 1.5 img2img (e.g. `runwayml/stable-diffusion-v1-5` ONNX export or
  Microsoft Olive-optimized DirectML model), with a C++ DPM++ 2M scheduler and CLIP BPE tokenizer (vocab.json/merges.txt). Keep the
  512×512 tiling, prompts, seeds and compositing; seeds will not reproduce Apple's outputs bit-for-bit (different RNG/latents).
  Make it optional/downloaded (≈2 GB fp16).

### 4.6 Vision usage summary
Only in `AlbumSceneGenerator.generate` (AlbumSceneStore.swift:231-250): text detection (boxes only), face rectangles, attention-based
saliency salient-object boxes. Windows options: OpenCV DNN EAST/DB text detector or `Windows.Media.Ocr` (word boxes), OpenCV
YuNet/Haar face detector, OpenCV `saliency::StaticSaliencySpectralResidual` or a small U²-Net ONNX for salient objects. Quality
only affects which patches are excluded; a missing detector degrades gracefully.

## 5. Browsing UI (main "Cover Flow" window)

### 5.1 Main window layout (`Views/RootView.swift`)
- Single main window "Cover Flow", default 1220×820, min 720×540 (CoverFlowApp.swift:162, RootView.swift:75). Unified toolbar
  with hidden background so the tinted backdrop runs under it; subtitle = "N albums" / "1 album" / while searching "V of N albums".
- Vertical stack: browser (Cover Flow / Grid / empty / no-results) with `StatusBanners` overlaid at top (max width 560) →
  `AlbumSelectionBar` (only if selection non-empty) → `NowPlayingBar` (height 80).
- Right inspector (collapsible) = Queue, width min 260 / ideal 300 / max 420; toggled ⌘U / toolbar; persisted UD `showQueue`.
- Background `TintBackground` (Views/Artwork.swift:101-118): base colour + radial gradient of `Palette.tint(browsed ?? playing album
  tintHex)` at centre (0.5, 0.18), radius 0→900 pt, opacity 0.42 dark / 0.30 light → 0; plus top→bottom linear 0.10/0.08 → clear.
  Animated 0.9 s ease-in-out when the tint changes.
- Toolbar: segmented Cover Flow / Grid picker; centre import progress ("Importing i of N" + current file, or "Looking for audio…",
  or "Preparing demo collection…"); right: search field (210×28, placeholder "Search music", live filtering, ⌘F focuses+selects),
  Visualizer, Lyrics, Queue buttons, "…" menu (Import Music…, Sort By inline picker).
- Drag & drop files/folders anywhere → import; overlay dashed accent rounded rect (radius 14, dash 8/6, line 2) "Drop to add to your collection".
- Empty state (RootView.swift:300-345): three tilted placeholder squares, "Your record collection is empty", explanation, buttons
  "Import Music…" (prominent) / "Add Demo Collection", footnote about drag & drop. No results: "No albums match “q”", "Search looks at
  album titles, artists and track names.", "Clear Search".
- Banners (:196-298): import report (success/warning tone; auto-dismiss 6 s, 14 s if details), player error (red, 9 s), library
  notice (orange; persistent; with "Show files that couldn’t be updated…" popover 460×320 listing artwork-write failures).
  Material background radius 12, hairline border, shadow r12 y4, slide from top + fade.
- Album detail is presented only after its palette+scene are ready (no colour flash): while waiting a capsule "Preparing album…"
  (RootView.swift:65-89).
- Confirmations: single album "Remove this album from your collection?" with message (demo vs real); multi "Remove N albums from your
  collection?" → "Remove Selected Albums" with "Your imported audio files stay on disk. Generated demo audio belonging to these
  albums will be deleted." (RootView.swift:112-123, 369-391).

### 5.2 Cover Flow 3D view (`Views/CoverFlowView.swift`) — port faithfully

State: continuous `position: Double` (fractional = between albums); `index = clamp(round(position))`; `target` = last snapped index.
`browsedAlbumID` (UD `browsedAlbum`) follows `index`; on appear and whenever the visible list changes, jump without animation to the
browsed album (or 0).

**Layout constants** (`CoverFlowLayout`, :116-121): `reflectionFraction 0.14`, `reflectionSpacing 3`, `verticalPadding 8`,
`captionSpacing 4`.
- Caption: if width ≥ 900 → compact horizontal caption 100 pt tall, else stacked caption 148 pt (:138-139).
- Scrubber slider (opacity 0.55, max width min(560, W−80), height 20) only when >6 albums (:140, 150-154).
- `coverSize = max(1, min((H − caption − scrubber − 4 − 16 − 3) / 1.14, W * 0.58))` (:248-253).
- `unit` (points per album for drag/scroll) = `coverSize * 0.3` (:142).
- Stage height = `size * 1.14 + 3`; covers are top-aligned in the stage.

**Per-cover geometry** (`CoverFlowGeometry`, :331-346), with `d = i − position`, `a = |d|`, `sign = d<0 ? −1 : 1`,
`t = min(a,1)`, `eased = t²(3−2t)` (smoothstep):
```
gap     = size * 0.68
spacing = size * (reduceMotion ? 0.30 : 0.18)
offsetX = sign * (a*spacing + (gap - spacing)*eased)
angleY  = -sign * (reduceMotion ? 0 : 62°) * eased        // side covers turned 62° toward the centre
scale   = 1 - (reduceMotion ? 0.22 : 0.10) * eased
opacity = clamp(8 - a, 0, 1)
zIndex  = -|d|
```
Transform order on each item (cover + reflection column, anchor = its centre) (:353-360): `rotation3DEffect(angle, axis Y,
anchor .center, perspective 0.42)` → `scaleEffect(scale)` → `offset(x)` → `opacity`. SwiftUI's `perspective` maps to
CATransform3D `m34 = -perspective / max(itemWidth, itemHeight)` (approximately; tune visually: eye distance ≈ itemSize/0.42 ≈ 2.4×
cover size). In Qt Quick use a `Rotation{axis.y:1; origin at centre}` inside a `Matrix4x4` with explicit perspective, or render the
stage in Qt Quick 3D with a perspective camera; validate against screenshots.
- Only items `center−8 … center+8` are instantiated (:308-311) (17 covers max).
- `CoverItem` (:363-400): cover drawn at `size×size`, corner radius 3, fit aspect (non-square art letterboxes inside the square);
  darkening overlay black × `(dark 0.40 / light 0.22) * shade`, `shade = min(|d|,1)`; drop shadow black (0.55 dark / 0.25 light),
  radius `18*emphasis+6`, y-offset `10*emphasis+4`, `emphasis = max(0, 1−|d|)`. Playing album: NowPlayingBars badge top-right
  (height max(10, size*0.035), padding 7, black 55 % rounded-6 background, inset size*0.035).
- **Reflection**: same artwork flipped vertically, clipped to the top `size*0.14` of the flipped image, 3 pt below the cover,
  opacity `1 − shade*0.5`, masked by vertical gradient from black α 0.22 (dark) / 0.16 (light) to clear. Not hit-testable.

**Animation**: snap spring `response 0.42 s, dampingFraction 0.86` (mass 1 → stiffness `(2π/0.42)² ≈ 223.8`, damping
`4π·0.86/0.42 ≈ 25.7`, critically-damped-ish); with Reduce Motion `easeOut 0.16 s` (:19-21). The stage is "Animatable" on `position`, so
*every intermediate frame lays out all covers from the interpolated position* — in QML animate a single `real position` property
and derive all transforms from it (do not animate individual covers).

**Input**:
- ←/→ (with key repeat): `step(±1)`; base = `target` if `|target − position| < 1.5` else `index` (so rapid presses accumulate) (:38-42).
  Shift+←/→ extends the album selection range. Home/End → first/last. PageUp/PageDown → ±5. Return → open track panel.
  Space → play/pause. Esc → clear selection. ⌘A → select all visible.
- Drag (min distance 4 pt): live `position = dragStart − translation/unit` through rubber band; on end
  `travel = clamp(predictedEndTranslation/unit, −12, 12)`, snap to `round(start − travel)` (:59-72). SwiftUI's
  `predictedEndTranslation` ≈ translation + velocity × ~0.25 s; emulate from pointer velocity.
- Rubber band beyond ends (:44-51): `p<0 → −0.25·(−p)/(1−p)`; `p>max → max + 0.25·d/(1+d)`. The un-banded value is kept as the
  gesture's own state (never feed banded output back as input).
- Mouse wheel (non-precise): accumulate deltaY (or X); when |acc| ≥ 0.9 → step(acc>0 ? −1 : +1), reset (:81-87). One album per notch.
- Precise (trackpad) scroll (:92-112): dominant axis delta; on gesture begin record `scrollPosition = position`;
  `raw = scrollPosition − delta/unit`, clamped to [−2, count+1], shown through rubber band. Snap to `index` when momentum ends/cancels;
  if no momentum phase and the finger phase ended (or phase-less device), snap after **0.18 s** idle. Windows: use
  `QWheelEvent::pixelDelta()` (precision touchpad) + `phase()` (`Qt::ScrollBegin/Update/End/Momentum`) — same logic.
- Click on a cover (:263-275): plain click → select it, animate to it, **and open its track panel**; Shift-click → range select;
  Ctrl(⌘)-click → toggle; neither opens the panel. Shift-drag on the stage selects a range while moving (:228-246).
- Scroll capture is a local event monitor limited to the stage rect so clicks still work (:519-556).

**Caption** (`AlbumCaption`, :402-500): optional DEMO chip and "Missing album info" label; title (serif semibold 24/26, one line,
tail-truncated), artist (15 medium, secondary), meta `year · N tracks · duration · genre` (genre hidden if "Demo"; duration via
`formatLongDuration`: `<1 min → "N sec"`, `<60 → "N min"`, else `"H hr M min"`). Buttons: Play/Pause/Resume (prominent pill; toggles if
this album is current else plays from start), "Tracks". If current: bars + "Now Playing"/"Paused" in accent; else if something else
plays: link "Playing: <album>" → reveal that album (⌘L).
- Accessibility value "Title by Artist, album i of N", adjustable action ±1.

### 5.3 Grid (`Views/GridView.swift`)
- Adaptive columns min 240 / max 360, spacing 20, row spacing 24, padding 20 (:15, :21, :42). Cell: square artwork (360-pt thumbnail
  bucket), shadow (dark 0.4/hover 0.6, radius 8/16, y 5/9; light 0.18), hover scale 1.02, title 13 semibold + DEMO chip, artist 12
  secondary. Hover or playing → round prominent play/pause button bottom-right (34 pt). Playing → bars badge top-left. Selected →
  accent 12 % rounded-8 background.
- Click semantics same as Cover Flow (plain click opens panel). Marquee drag (min 5 pt) draws accent 12 % fill / 80 % 1-pt stroke rect,
  selects intersecting cells; Shift/⌘ at drag start adds to existing selection; clicks are suppressed for 0.1 s after a marquee.
- Keys: Space play/pause, Esc clear selection, Return open browsed album, ⌘A select all. Reveal request scrolls to centre (0.35 s).
- Context menu (`AlbumContextMenu`, :184-216; applies to the whole selection when right-clicking a selected album among several):
  "Play" / "Play N Albums", "Shuffle", "Play Next", "Add to Queue", —, "Show Tracks", "Edit Album Info…", "Colors & Scene…",
  "Replace Artwork…", —, "Remove from Collection…" / "Remove N Albums…".

### 5.4 Selection bar (`Views/AlbumSelectionActions.swift`)
"N selected", Play (queue = all tracks of selected albums in displayed order, autoplay), Play Next, Add to Queue, Remove…, Clear Selection.
Selection is pruned to visible albums whenever search/sort changes.

### 5.5 Album track panel (`Views/AlbumDetailView.swift`) — "early iTunes" look
- Modal sheet 1080 wide, height `max(540, min(660, maxColumnRows*32 + 220))`, padding 30, background = rendered scene
  (`AlbumSceneBackground`) or palette background; colour scheme forced light/dark from foreground luminance.
- Header: title Helvetica Neue 28 bold fg (2 lines, selectable); artist 17 in accent + DEMO chip; metadata `genre · year` 12 secondary;
  "Some info was guessed from file names" if missingMetadata; close button (Esc).
- Controls row: Play, Shuffle (panel button style), "…" menu: Play Next, Add to Queue, —, Edit Album Info…, Colors & Scene…,
  Find Artwork Online…, Replace Artwork…, Remove Artwork (only if has artwork and not demo), —, Remove from Collection…;
  right-aligned "Colors & Scene" link.
- Track list: **two columns when >9 tracks and no title needs a wide column**; `needsWideColumn` = title contains newline or its
  width in Helvetica Neue 13 > **221 pt** (ScrollingTrackTitle.swift:99-111). Split at `(n+1)/2`, preferring a disc boundary within
  `max(2, n/6)` of the midpoint (AlbumDetailView.swift:182-192). Multi-disc: "Disc N" headings (11 bold) at disc changes and at the top
  of each column.
- Row (`TrackRow`, :203-274): number (track number or position) / hover play icon / NowPlayingBars if current; title 13 (medium + accent
  if current) **wraps** (no truncation, tooltip full title); track artist shown under title when it differs from album artist; missing
  file warning icon with tooltip path; duration `m:ss` (or `h:mm:ss`). Hover 10 % fg background, current 7 %. Click: current →
  toggle play, else play album from this track. Context menu: Play, Play Next, Add to Queue, —, Show in Finder (Windows: "Show in
  Explorer" → `explorer /select,`), —, Remove from Collection… → alert "Remove Track from Collection?" message "“T” will be removed from
  this album and the playback queue. Your original audio file will stay on disk." (demo: "Its generated demo audio will be deleted.").
  If the album disappears after removal, close the panel.
- Footer "N songs, duration". Cover 340×340 on the right, 10 % fg hairline, shadow 0.24 r10 y5.
- Panel events (Views/AlbumPanelEvents.swift): Space (no modifiers, non-repeat) toggles playback even when a button has focus;
  a click in the parent window outside the sheet dismisses the panel and is **consumed** (never activates controls behind). Disabled
  while an editor/confirmation/download sheet is open on top.

### 5.6 Edit Album Info (`AlbumEditView`, AlbumDetailView.swift:276-363)
620×600 sheet: artwork 140 + "Replace Artwork…", fields Album/Artist/Year/Genre, per-track Disc / No. / Title grid. Save →
`updateAlbum` (LibraryStore.swift:403-424): empty title/artist keep old values; year/genre parsed; sets `userEdited`, clears
`missingMetadata`; tracks get new title/number/disc, `albumTitle` synced, "Unknown Artist" tracks inherit album artist (unless
"Various Artists"); re-sort. Edits only touch Library.json, not files.

### 5.7 Placeholder artwork (`PlaceholderArtwork`, Artwork.swift:53-98)
hue = stableHash(title+artist) % 360 / 360. Gradient topLeading HSB(h,0.32,0.36) → bottomTrailing HSB(h+0.08,0.42,0.18); 7 concentric
"groove" rings (white 5 %/7 % alternating, diameters s·(0.3+0.085i)), label disc HSB(h,0.45,0.62) 85 % of 0.2 s, spindle hole black 60 %
0.025 s, all offset up 0.06 s; title serif semibold max(8, 0.072 s) white 88 % (2 lines, "Untitled" if empty), artist uppercase
max(6, 0.038 s) tracking 0.004 s white 60 % (if s>90), padding 0.08 s, bottom-left.
- Artwork view keeps a stable square footprint, uses a synchronously cached thumbnail when available, else loads async and fades in 0.2 s.

### 5.8 Now Playing bar (`Views/PlaybackControls.swift`)
Height 80, bar colour `Palette.bar`, top hairline. Breakpoints: wide > 1040 pt, medium > 860.
Left (280/210/150 pt): 52-pt artwork (click toggles Album View), scrolling title (13 semibold), "Artist — Album" link (reveal album), or
"Nothing playing / Choose an album and press Play". Centre (max 560): `TransportControls` (shuffle/repeat only when medium; play button
1.25× size prominent; repeat icon `repeat.1` for One) + `SeekBar` (elapsed `m:ss` right-aligned 42 pt, slider holds its own value while
dragging and seeks on release, remaining "-m:ss"). Right (468/378/288): mute button with icon by volume (<0.34, <0.67, else) + slider
(wide only, 92 pt), Lyrics, Karaoke/Vocals/instrument buttons (§11.8), Queue toggle, "Album View" pill.

### 5.9 Scrolling title (marquee) rules (`Views/ScrollingTrackTitle.swift`)
Used in compact playback header, Album View, Lyrics and Karaoke preparation headers. Newlines are replaced with spaces; one line tall.
- If text width − viewport width ≤ 1 pt → static, honour alignment. Otherwise left-aligned and animated:
  speed **28 pt/s**, pause **2 s** at each end; cycle `2·(travel + pause)`, `travel = distance/28`:
  `[0,pause)` → 0; then moves to `distance`; pause; then moves back (TrackTitleScroll.offset, :83-97). 30 fps timeline, paused when not
  overflowing. Restart the cycle whenever measured text width changes (new title/viewport).
- RTL layout mirrors the offset. Reduce Motion → no scrolling (truncated). Tooltip / hover shows the full title immediately;
  accessibility label = full title.
- Track lists and queue **wrap** long titles instead.

### 5.10 Queue inspector (`Views/QueueView.swift`)
Header "Queue" (serif 18) + "Clear Up Next". Sections "Now Playing" (current row) and "Up Next · N" (rows after current). Row: 36-pt art
(overlay bars if current, play icon on hover), title wraps, artist, duration or hover ✕ remove. Drag to reorder; swipe/delete; context
menu Play Now / Move to Top / Move Up / Move Down / Remove from Queue. Footer "Drag to reorder, or right-click a track to move it. The
queue and position are saved." Empty: "Nothing up next. Use Play Next or Add to Queue on any album or track."

## 6. Playback engine

### 6.1 Architecture
- `PlayerEngine` (Playback/PlayerEngine.swift) = single source of truth: queue `[trackID]`, `currentIndex`, `isPlaying`,
  `isBuffering`, `currentTime`, `duration`, `volume` (default **0.8**), `isMuted`, `shuffle`, `repeatMode`, `errorMessage`,
  karaoke playback mode, stem mix. All views observe it (QML: one `QObject` with `Q_PROPERTY`s + NOTIFY).
- `GaplessAudioPlayer` (Playback/GaplessAudioPlayer.swift) = one continuous PCM stream through AVAudioEngine:
  `AVAudioPlayerNode` (main) + one node per stem (vocals, drums, bass, guitar, piano, other, instrumental) → `outputMixer`
  (volume = muted ? 0 : volume) → main mixer. A tap on `outputMixer` (512 frames) feeds the visualizer.
  **Windows:** WASAPI shared-mode (or miniaudio / `QAudioSink` pull-mode) render callback that pulls from a lock-free ring buffer
  of 48 kHz stereo float; implement the mixer (main + 7 stem gains + master volume) in your callback; copy mixed output to the
  visualizer buffer from the callback (non-blocking try-lock as in VisualizerAudioBuffer.swift:27-39).
- `PCMDecoder` (Playback/PCMDecoder.swift): every track is converted to **48 000 Hz, 2-ch, Float32 deinterleaved**
  (`sampleRate`, :16), read in **24 000-frame chunks** (0.5 s, :17). Mono is duplicated to both channels (channelMap [0,0]).
  Resampler primed "normal" so no latency/silence is inserted at rate changes. Decoder backends: Ogg Opus via custom Ogg demux +
  Core Audio Opus; FFmpeg (`FFmpegFileReader`) for FFmpeg extensions or any file Core Audio can't open; Core Audio ExtAudioFile
  otherwise. **Windows:** FFmpeg for everything (Opus via libavcodec's native decoder applies pre-skip/output gain itself) +
  libswresample to 48 kHz stereo FLTP with `rematrix_maxval=1.0` (Sources/CFFmpegAudio/CFAudioFile.c:205-212). The C file
  `CFAudioFile.c` is portable (pthread_once → `std::call_once`/InitOnce) and can be reused verbatim, including its sample-exact seek.
- `totalOutputFrames = round((length − startFrame) * 48000 / fileRate)`; a chunk is `isLast` when emitted ≥ total (PCMDecoder.swift:52,127).

### 6.2 Gapless scheduling algorithm (GaplessAudioPlayer.load, :92-197)
1. `load(source, autoplay, at:start)` resets everything (new generation token; cancel decodes; stop nodes; clear segments).
2. A pump task loops every **25 ms**: while `scheduledFrames − renderedFrames < 8 s × 48000` (**8 s lookahead**) and not finished:
   - open a decoder for `tail` at `offset` (first track at `start`, successors at 0) and, if the source has stem files, one decoder per
     stem (their durations must match within 1 frame: "The stems have different lengths. Prepare the track again.");
     append `Segment{source, startFrame = scheduledFrames, endFrame, offset, duration}`.
   - read a chunk; with stems, zero the main buffer (it becomes the silent clock) and schedule each stem buffer at the same sample
     time; schedule main buffer with a completion callback → `poll(completedThrough:)`.
   - at end of track ask `nextSource(tail)` (= `PlaybackOrder.successor` with current repeat mode) and continue decoding it into the
     same stream — this is what makes playback gapless (including Repeat One/All).
   - start the node when `wantsPlayback` and `readyToStart` = ≥ **2 s** scheduled, or finished with >0 frames. Start time = now +
     **0.03 s** (0.15 s when stems are present, so all nodes start sample-aligned).
   - on error: remember `pendingFailure`, mark finished, drop empty segments, play what's scheduled; failure reported when the
     render position reaches the end.
3. `poll`: `frame = max(renderedFrames, completedThrough)`; active segment = last with start ≤ frame; `currentTime = offset +
   (frame − start)/48000` (clamped to duration); when the active segment id changes → `onTrackChanged(source, duration, transitioned)`;
   time callbacks throttled to changes ≥ **0.25 s**; segments wholly before the active one are dropped. When all scheduled frames are
   rendered and scheduling finished → pause + `onFailure` or `onEnd`.
4. `pause()` freezes `pausedFrames = renderedFrames` and commits the position immediately; `seek` = reload at the new time;
   device configuration change (output switched) → reload at current time preserving play state.
5. `refreshUpcoming(current)` after queue edits/shuffle/repeat changes: keep the stream only if every already-prepared successor
   (and the in-flight decode) still equals what `nextSource` would now return; otherwise reload at the current time.
- Silence inside recordings is preserved; there is **no ReplayGain / loudness normalisation and no crossfade** anywhere in the app.
  The only gain applied besides volume is the Opus header output gain (`10^(gain/(20*256))`, OggOpus.swift:40).

### 6.3 Queue, shuffle, repeat (PlayerEngine.swift)
- `play(albumID, startAt?, shuffled?)` → `setQueue(album.trackIDs, startIndex)`. `setQueue` with shuffle on: remember `unshuffled`,
  remove start track, shuffle rest, put start track first, index 0 (:142-161).
- `toggleShuffle` (:250-270): on → keep items up to current, shuffle the rest, remember original; off → restore original order and
  find current in it. Any manual queue edit clears `unshuffled`.
- `PlaybackOrder.successor` (PlaybackOrder.swift:4-9): automatic & repeat one → same index; else index+1; at end → 0 if repeat all else nil.
  Manual Next ignores Repeat One.
- `previous()` (:189-203): if `currentTime > 3 s` → restart track; else previous index; at first track with Repeat All → last; else restart.
- `togglePlayPause`: if no loaded source, load at saved position and play; if at end (≥ duration − 0.25 s) seek to 0 first.
- `playNext(ids)` inserts after current; `addToQueue` appends; both start a non-playing queue if none. `moveUpNext`, `move(.top/.up/.down)`,
  `removeFromQueue` (removing current loads the next at same index, keeps playing state), `clearUpNext`, `jump(toAbsolute:)`, `stop()`.
- Removed tracks (`purge`, :389-407) are dropped from queue/unshuffled; if current was removed, load the next one at the adjusted index, paused.
- Failures (:458-472): error banner "Couldn’t play “T”. <reason>", announcement; auto-skip after **1.5 s** if playback was requested,
  consecutive failures < min(queue.count, 6), and there is a next track (or Repeat All).
- Volume keys: ±0.1; volume up unmutes. Skip ±10 s.
- `markPlayed` (album lastPlayed) when a track starts with autoplay or gapless transition while playing; accessibility announcement
  "Now playing T by A"; "End of queue" at the end.

### 6.4 Karaoke source switching (PlayerEngine.setKaraokePlayback, :219-237)
Mode ∈ `original, instrumental, vocals, drums, bass, guitar, piano, other, mix`. Non-original requires prepared audio for the
*current* track (validated); `mix` passes all stem files to the player (multi-node mix). Reload the current track's source at the
current time, preserving play/pause. Any change of current track (including gapless transition) resets to Original. Missing stem →
"This model does not separate <x>. Choose Demucs 6 stems and prepare again."

### 6.5 Stem mix gains (Playback/StemMix.swift:44-53)
`gain(stem) = 0 if muted or (solo non-empty and stem ∉ solo); else clamp(levels[stem] ?? 1, 0, 1)`. Applied immediately to stem nodes.

### 6.6 Playback.json schema (PlayerEngine.swift:10-19, 490-522)
```json
{"queue":["UUID",...],"currentIndex":3,"position":81.25,"shuffle":false,"repeatMode":"off|all|one",
 "volume":0.8,"muted":false,"unshuffled":["UUID",...]}
```
(`currentIndex`/`unshuffled` omitted when nil.) Save debounced 1 s after changes, every **10 s** while playing, and on quit.
Restore: drop unknown track ids, map the saved current track to its new index, load **paused** at saved position. Karaoke mode is not persisted.

### 6.7 System integration
- macOS Now Playing + media keys (MPRemoteCommandCenter: play, pause, toggle, next, previous, change position) with title, artist,
  album, duration, elapsed, rate and 600-px artwork (:530-600). **Windows:** `SystemMediaTransportControls` (SMTC) via
  WinRT/C++ (`ISystemMediaTransportControlsInterop::GetForWindow`), update DisplayUpdater with thumbnail; also handle
  `WM_APPCOMMAND` media keys.
- App keeps running after the last window closes; Dock click reopens the main window (CoverFlowApp.swift:121-131). Windows: tray icon or
  reopen on second-instance launch (QLocalServer single instance).
- Quit: save playback, cancel karaoke work, save library, persist Album View open state (:111-119).
- Accessibility announcements (Support.swift:221-231) de-duplicated within 2 s → Windows `QAccessible::updateAccessibility` /
  UIA notification events.

## 7. Album View window (`AlbumView/AlbumViewController.swift`, `AlbumView/AlbumViewScreen.swift`)

A separate, resizable mini-player window showing the *playing* album's cover; opening/closing/resizing never affects playback.

### 7.1 Window
- Title "Album View", transparent title bar, full-size content, min **260×340**, background white 0.06, not full-screen capable,
  level floating when **Keep on top** (default **true**, UD `albumView.keepOnTop`) (Controller :26-31, :169-202).
- First open: 320×430 at screen bottom-right (`x = maxX − 340, y = minY + 110`), afterwards frame autosaved under name
  `CoverFlow.AlbumView.Frame` (Windows: QSettings `albumView/geometry`). On show/screen change the frame is clamped so ≥80×80 px is visible.
- Open state persisted in UD `albumView.open` and `albumViewOpen`; reopened at launch if it was open (CoverFlowApp.swift:105-107).
- Presets (UD `albumView.preset` = `compact|large|immersive|""` (custom)) (:75-105):
  - Compact **320×430** centred on the current frame centre.
  - Large `width = min(vf.w*0.9, vf.h*0.88)`, `height = vf.h*0.94`.
  - Immersive `vf*0.96` centred on the screen's visible frame; the pre-immersive frame+preset is remembered and restored by
    `exitImmersive` (else Large). Leaving immersive to another preset recentres on the remembered frame.
  - Frame changes animate unless Reduce Motion. Manual live-resize → preset becomes nil (custom).
- Custom resize grip bottom-right (26 pt) resizes keeping the top-left fixed, clamped to the screen (:124-133). Background drag moves
  the window (except immersive); double-click on artwork toggles Immersive.
- Menu: View ▸ Open/Close Album View ⇧⌘A; Album View Size ▸ Compact ⌥⌘1 / Large ⌥⌘2 / Immersive ⌥⌘3. Clicking the player-bar artwork or the
  "Album View" pill toggles it.
- Theme follows app theme; Immersive is always dark.

### 7.2 Content layout (AlbumViewScreen.swift:19-101)
- `narrow = w < 430`, `short = h < 520`. `controlsHeight = immersive ? min(118, h*0.12) : (narrow||short ? 104 : 150)`.
  `topInset = immersive ? max(24, h*0.025) : 38`. Artwork box = `(w − (narrow ? 28 : 56)) × max(80, h − topInset − controlsHeight −
  (immersive ? 18 : 22))`; cover fitted by aspect, rounded 6, shadow black 0.6/0.3 r30 y16.
- Background: TintBackground of the playing album; in Immersive with scenes enabled (UD `albumView.immersiveScenes`, default true)
  and artwork → `ImmersiveArtworkScene` (§4.4) with the cover rect computed the same way.
- Hero image: show full-res cached image, else instantly a cached 1100-px thumbnail then swap in the full ≤4096-px decode;
  cross-fade 0.6 s (0.15 s linear with Reduce Motion). Nothing playing: disc icon, "Nothing playing", button "Play <browsed album>".
- Controls: non-immersive: [compact: "ALBUM VIEW" caption] scrolling title (serif 15/18), "Artist — Album" line, SeekBar, row (volume
  button (non-compact) · transport (28/32; shuffle/repeat only non-compact) · "show album in collection" button). Immersive: one row
  `titles (+year) | transport 32 | volume with slider`, SeekBar below, max width min(900, w*0.7).
- Top bar (fades with controls): "ALBUM VIEW" label (not narrow), preset segmented capsule (text or icons when narrow; tooltips with
  ⌥⌘1/2/3), Lyrics button, in Immersive: "Scene: <Style>" menu (per-album `immersiveRefinementStyle`, disabled if no artwork or no SD)
  and scene on/off toggle, Artwork-focus (eye) toggle (UD `albumView.focusMode`), Keep-on-top pin.
- **Auto-hide**: when (focus mode or immersive) and something is playing, controls + title-bar buttons fade out after **3.2 s** without
  pointer movement/keys (checked every 0.5 s; fade 0.6 s, 0.1 s reduced), any hover/key brings them back (0.25 s). While hidden, a
  translucent capsule top-right offers Exit Immersive and Close (opacity 0.4, 1 on hover).
- Keys (:133-159): Space play/pause; ←/→ previous/next, ⌥←/⌥→ ±10 s; Esc → exit immersive, else turn focus mode off, else close;
  1/2/3 presets; F toggles artwork focus.

## 8. Lyrics

### 8.1 Storage & lookup order (`Lyrics/LyricsStore.swift`)
- Directory `AppSupport/Lyrics/`. Imported/downloaded lyrics are saved as **`<trackUUID>.lrc`** (raw bytes as imported; downloaded
  text written UTF-8). The audio files and their sidecars are never modified.
- `find(trackID, audioURL)` (:104-121): 1) `Lyrics/<trackUUID>.lrc` (→ "Saved lyrics", `imported=true`); 2) sidecar
  `<audio basename>.lrc` in the audio's folder; 3) if not found, any file in that folder whose name equals that sidecar name
  case-insensitively (e.g. `.LRC`), first by name sort. Else none. Header shows the source file name (tooltip = path) or "Saved lyrics".
- Size limit **2 MB** ("That lyric file is too large. Choose a lyric file smaller than 2 MB."). Encoding: BOM `FF FE`/`FE FF` → UTF-16,
  else must decode as UTF-8 ("That lyric file could not be read. Save it as UTF-8 or UTF-16 and try again."). Empty parse → "That file
  has no readable lyrics. Check its text and timestamps, then try again." Import accepts only `lrc, txt, lyric, lyrics` ("Choose an .lrc,
  .txt, .lyric or .lyrics file."). File picker opens in the track's folder; title "Import Lyrics for <title>".
- Loading is per current track (re-run whenever the current track changes), with request tokens so stale loads are ignored; a file
  picker that outlives a track change still attaches to the original track. "Reload lyrics" re-reads from disk.

### 8.2 LRC parser (`Lyrics/LyricsDocument.swift:171-227`) — port exactly
Regexes:
- timestamp `\[(\d+):(\d{1,2})(?:[.:](\d{1,3}))?\]`
- metadata line `^\[([A-Za-z]+):(.*)\]$`
- word-timestamp (enhanced LRC) `<\d+:\d{1,2}(?:[.:]\d{1,3})?>` — removed from text
Algorithm: strip U+FEFF; split on newlines; trim spaces/tabs per line.
1. Metadata line → only `offset` is used: `offset = Double(value)/1000` seconds (other tags ignored, line skipped).
2. Collect **leading** timestamps only (stop at the first non-whitespace gap); seconds must be <60; fraction = `"0." + digits`
   (so `.5` = 0.5 s, `.05` = 0.05 s, `.500` = 0.5 s); time = min*60 + sec + fraction. Remaining text (trimmed, word timestamps removed)
   becomes one entry per timestamp (repeated timestamps → repeated lines). Empty text allowed (instrumental break, shown "♪").
3. Lines without timestamps: blank lines or lines not starting with `[` go to `plainText`; other `[...]` lines are dropped.
4. Sort entries by time, stable by original order. Entries with **identical time are merged** into one cue joined by `\n`
   (translations), skipping empty and duplicate texts. Cue ids = 0…n−1.
5. `isSynced` = has cues; `isEmpty` = no cues and empty plain text. Untimed documents show plain text without syncing.
- `activeIndex(t)` = last cue with `cue.time <= t + offset` (binary search); nil before the first cue. **Positive offset advances
  the highlight.** `playbackTime(cue) = max(0, cue.time − offset)` (used for click-to-seek).

### 8.3 Lyrics window (`Lyrics/LyricsWindowController.swift`, `Lyrics/LyricsView.swift`)
- One reusable window "Lyrics", 560×700 default, min 440×360, frame autosave `CoverFlow.Lyrics.Frame`; opened by ⌘4 / toolbar / player
  bar / Album View. Content is torn down on close.
- Header: 54-pt artwork, scrolling title (18 semibold), artist, source label; Reload button; "Add Lyrics" menu: Find Lyrics Online…,
  Import LRC or Text….
- Synced view: lines 24 pt semibold, spacing 26, padding 28 h / 90 v; active line in accent colour, others secondary; click → seek;
  tooltip "Jump to m:ss"; with **Follow playback** (checkbox, default on) the active line is scrolled to centre (0.25 s ease; none with
  Reduce Motion). Plain lyrics: 22 pt medium, line spacing 12, selectable.
- Empty: "No lyrics for this track" + "Search for synced or plain lyrics, or import a lyric file from your Mac." + buttons Find Lyrics… /
  Import Lyrics…; with nothing playing: "Choose a track to see its lyrics".
- Footer: Follow playback toggle or "LRC and plain lyrics"/"Untimed lyrics", transport (26), compact SeekBar. The Karaoke window embeds
  this same view with extra playback actions (then shows "Space: pause / resume").

## 9. Online lookups (`Downloads/MusicLookupClient.swift`, `Downloads/MusicDownloadViews.swift`)

User-initiated only; no API keys. One shared serial client: requests are chained (each waits for the previous), per-host spacing
(**musicbrainz.org 1.1 s**, others **0.35 s** after each request, even failed), ephemeral session (no cookies), timeouts 25 s request /
45 s resource. `User-Agent: CoverFlow/<version> (https://github.com/dustalexw/cover-flow)`; `Accept: application/json`
(`image/*` for coverartarchive.org).
- 429/503: parse `Retry-After` (seconds or HTTP-date `EEE, dd MMM yyyy HH:mm:ss zzz`; default 5, min 1), block the host until then,
  throw rateLimited/busy. Auto-retry up to **3** times if wait ≤ **5 s**, with extra pad 0.5 s × attempt. Other non-2xx → "The service is
  unavailable (code). Try again later." (404 for covers: "No artwork is available for this edition. Try another result.").
- Size limits enforced while streaming: lyrics 8 MiB, MusicBrainz 2 MiB, cover 25 MiB.

**LRCLIB lyrics**: `GET https://lrclib.net/api/search?track_name=<title>&artist_name=<artist>` (artist omitted if empty). Response JSON
array of `{id, trackName, artistName, albumName, duration, instrumental, plainLyrics?, syncedLyrics?}`.
**Match ranking** (:72-79): drop non-finite / out-of-range (0…86400 s) durations, instrumental, empty text; sort by
`floor(|duration − trackDuration| / 2)` bucket ascending → synced before plain → exact |Δ| → id. No results → "No matches found. Try
changing the title or artist." Selected text = synced if non-blank else plain.
UI "Find Lyrics" sheet 620×640: editable Track title / Artist (prefilled), Search, list rows (track; artist · album; m:ss · "Synced
LRC"/"Plain lyrics"), preview (first 8 cues or plain text), "Use Synced Lyrics"/"Use Lyrics" → saved as `Lyrics/<trackUUID>.lrc`.
Footer "Lyrics provided by LRCLIB. Searches send the title and artist; your audio stays on this device."

**MusicBrainz artwork**: `GET https://musicbrainz.org/ws/2/release-group/?query=<q>&fmt=json&limit=12` where
`q = releasegroup:"<title>"` + ` AND artist:"<artist>"`; Lucene-escape each of `\ + - & | ! ( ) { } [ ] ^ " ~ * ? : /` with a backslash.
Parse `release-groups[].{id, title, first-release-date, artist-credit[].{name | artist.name}}` (credits joined ", ").
Cover: `GET https://coverartarchive.org/release-group/<lowercase-mbid>/front-1200` (fixed endpoint, follows redirect).
UI "Find Album Artwork" sheet 740×540: Album title / Artist fields, result list (title, artist, year), 220×220 preview, "Use Artwork"
→ `replaceArtwork(data)` (library + embed in files, §3.6). Caption "Album search by MusicBrainz. Covers from the Cover Art Archive, up to
1200 pixels. Availability varies by edition."

## 10. Last.fm scrobbling (`Scrobbling/`)

### 10.1 Credentials & auth flow
- **No API key ships with the app**: the user registers their own Last.fm API account and enters **API key + shared secret**
  (Settings ▸ Last.fm ▸ "Set Up Last.fm…" / "Change App Credentials…"). Validation (LastFmScrobbler.swift:86-101): both trimmed,
  exactly 32 hex chars, else "Enter the 32-character API key and shared secret from your Last.fm API account." Saving new app
  credentials disconnects, disables scrobbling and **clears pending listens**; status "Setup saved. Connect your Last.fm account."
- Storage: macOS Keychain generic password, service **`app.coverflow.lastfm`**, account **`credentials`**, value = JSON
  `{"apiKey","secret","sessionKey","username"}` (LastFmKeychain.swift:90-91; LastFmAPI.swift:4-11). Loaded without UI prompts.
  **Windows:** Credential Manager (`CredWriteW`, `CRED_TYPE_GENERIC`, target `app.coverflow.lastfm/credentials`, blob = same JSON) or
  DPAPI-protected file.
- Desktop auth (web flow, never the user's password):
  1. "Connect Last.fm" → `auth.getToken` (signed, no sk) → open browser `https://www.last.fm/api/auth/?api_key=<key>&token=<token>`;
     status "Approve access in your browser, then click Finish Connecting."
  2. "Finish Connecting" → `auth.getSession(token)` → save `session.key` + `session.name`; drop pending records of other usernames;
     enable scrobbling; status "Connected to <name>." Error 14 → "Approve access on Last.fm first, then finish connecting."
  3. "Cancel" → "Connection cancelled." "Disconnect" → clear sk/username, disable, clear pending, "Disconnected."
  A `revision` token invalidates in-flight operations on any of these changes.
- UD `lastfmEnabled` (bool) = "Scrobble my listening" toggle.

### 10.2 API request format (`LastFmAPI.swift:48-83`)
- POST `https://ws.audioscrobbler.com/2.0/`, `Content-Type: application/x-www-form-urlencoded; charset=utf-8`, timeout 20 s.
- Fields: params + `method`, `api_key`, `sk` (if any), `api_sig`, `format=json`.
- `api_sig` = lowercase hex **MD5** of concatenated `key+value` for all keys sorted (byte order) excluding `format`, `callback`,
  `api_sig`, followed by the secret (UTF-8).
- Body: keys sorted, each key and value percent-encoded allowing only `A–Z a–z 0–9 - . _ ~` (so `+` → `%2B`, space → `%20`).
- Reply JSON may contain `error`(int)/`message` → error; non-2xx without error → "Last.fm is unavailable. Try again later.";
  undecodable → "Last.fm returned an unreadable response." (code 503). `scrobbles` accepted/ignored either in `@attr` (strings) or
  top-level (string or int).
- Retryable errors: codes **11, 16, 29, ≥500** (plus transport errors). Code **9** (invalid session) → expire connection: clear sk,
  status "Last.fm access expired. Reconnect to send pending listens."

### 10.3 Listening rules (`ScrobbleProgress.swift`, `LastFmScrobbler.observe`)
- Eligible track: scrobbling enabled & connected, **not demo, not `missingMetadata`, non-blank title and artist**.
- `newPlayback(track, duration, position)` on every load/transition resets progress (monotonic clock = system uptime).
- `update(position, playing, uptime)` (:158-167): while previously playing, `listened += min(max(0, Δposition), max(0, Δuptime) + 0.1)`
  — media position must advance *and* wall time bounds it, so seeking forward never earns credit; paused/buffering earn nothing.
  First time `playing` → `timestamp = unix now` (start of listening).
- `seek(to:)` rebases position without credit. Called with the player's state on every time tick (~0.25 s), state change, seek, track change
  (`playing = isPlaying && !isBuffering`).
- **Now Playing**: sent once per playback the first time it is playing (`track.updateNowPlaying` with artist, track, album, duration,
  albumArtist?, trackNumber?); never queued or retried; error 9 expires the session.
- **Scrobble threshold** (`claimScrobble`, :174-179): not yet submitted, has timestamp, `duration > 30 s`, and
  `listened >= min(duration/2, 240 s)`. Then a record is appended, persisted and flushed. At a gapless transition the old track is first
  observed at `position = duration` (not playing) so its final credit counts (PlayerEngine.swift:75-78).
- Record fields (`ScrobbleRecord`): `id, username, artist (track artist), title, album (track.albumTitle), albumArtist?, trackNumber?,
  duration, timestamp`; request params `artist, track, album, duration (Int), timestamp, albumArtist?, trackNumber?`.

### 10.4 Queue / retry
- Pending file `AppSupport/LastfmPending.json` = JSON array of ScrobbleRecord (UUID strings; nil fields omitted). Persisted atomically on
  every change. `pendingCount` = records for the current username.
- `flush()` when enabled, connected, no upload in flight, and `now >= retryAfter`: batch = first **50** records for this username, as
  indexed params `artist[0]`, `track[0]`, …; `track.scrobble`. Success requires `accepted + ignored == batch.count`; remove batch,
  reset backoff to 30 s, status "Listening history is up to date." or "Last.fm ignored N listen(s). Check track tags and your Last.fm
  limits.", then flush again. Failure: code 9 → expire; non-retryable → **disable scrobbling** ("Scrobbling paused: <msg>"); retryable →
  `retryAfter = now + delay`, delay doubles from **30 s up to 900 s**; status "Last.fm is unavailable. N listen(s) saved for retry."
- A background timer calls `flush()` every **30 s**; also at launch, on enable and on "Retry Now".
- Settings copy (LastFmSettings.swift): explanation, "Connected as <user>", toggle, Disconnect, "N pending" + Retry Now, notes about
  Keychain/offline/skipped tracks; setup sheet 440 wide with link `https://www.last.fm/api/account/create`, fields API key / Shared secret
  (secure), "Save Setup".

## 11. Karaoke / stem separation (`Karaoke/`, `Resources/Karaoke/`)

### 11.1 Architecture summary
- **Not Core ML for separation on the Mac.** The macOS app shells out to a **bundled standalone CPython 3.12.7 runtime**
  (`Contents/Resources/KaraokeRuntime/python/bin/python3` + `site-packages`, built by `scripts/build-karaoke-runtime.sh` from
  python-build-standalone 20241016 aarch64, pinned by `Resources/Karaoke/runtime-lock.txt`: torch 2.13.0, numpy 2.3.5, soundfile
  0.13.1, PyYAML, einops, beartype, rotary-embedding-torch, onnxruntime 1.31.0, demucs 4.1.0, julius, safetensors …) running
  `Resources/Karaoke/separate.py`. Apple-silicon only ("Karaoke processing in this build requires an Apple silicon Mac.").
  Device: PyTorch **MPS** (Metal) with CPU fallback for Demucs/RoFormer; ONNX Runtime **CoreMLExecutionProvider** (MLProgram,
  CPUAndGPU) with CPU fallback for MDX. (Only the iPad target uses native Core ML for MDX — out of scope.)
- **Windows port:** the Python pipeline is already cross-platform. Easiest faithful port = bundle an embeddable CPython + the same pinned
  wheels (torch CUDA or CPU build; `onnxruntime-directml` or `onnxruntime-gpu`), keep `separate.py`/`mdx.py`/`demucs_backend.py`
  unchanged except device selection: replace `'mps'` with `'cuda'` (or `torch_directml`) and the MDX provider with
  `DmlExecutionProvider`/`CUDAExecutionProvider` (cache dir concept not needed for DML). Launch with `QProcess` and the same CLI + env.
  Alternative native route: ONNX-export Demucs/RoFormer and run via ONNX Runtime C++ (much more work; STFT/iSTFT outside the graph).

### 11.2 Models (`KaraokeSupport.swift`)
`KaraokeModel{checkpoint: URL, configuration: URL}`; kind by checkpoint extension (:16): `.demucs`/`.safetensors` → **demucs**,
`.onnx` → **mdx**, else (`.ckpt`) → **mel-band-roformer**. id = `standardizedPath(checkpoint) + "\n" + standardizedPath(config)`.
Kind subtitles: "Demucs · Apple GPU with CPU fallback · all model stems", "UVR MDX · Core ML GPU preferred · CPU fallback",
"Mel-Band RoFormer · automatic device".
Valid pairs (:51-54): demucs/safetensors with config == checkpoint; `.ckpt` + `.yaml/.yml`; `.onnx` + `.json`. Both files must be
regular, non-empty, readable.

**Built-in Demucs entries** (:24-39): written as descriptor files `Karaoke/ModelSettings/<name>.demucs` containing sorted-keys JSON
`{"name":"…","title":"…"}`:
| name | title | HF repo @ revision | weights (SHA-256 verified, size) | stems |
|---|---|---|---|---|
| htdemucs_6s | Demucs · 6 stems (guitar + piano) | adefossez/HTDemucs-6s @3c5ee475… | 5c90dfd2.safetensors 54.9 MB | drums, bass, other, vocals, guitar, piano |
| htdemucs | Demucs · 4 stems | adefossez/HTDemucs @cbc8a9b1… | 955717e8 84 MB | drums, bass, other, vocals |
| htdemucs_ft | Demucs · 4 stems, fine-tuned | adefossez/HTDemucs-ft @d74ac89c… | 4 × 84 MB (bag with one-hot weights per stem) | 4 |
| hdemucs_mmi | Demucs Hybrid · 4 stems | adefossez/Demucs-hdemucs_mmi @53a9296e… | 75fc33f5 167 MB (segment 44) | 4 |
| mdx_extra | Demucs MDX Extra · 4 stems | adefossez/Demucs-mdx_extra @00ac3ed0… | 4 × 167 MB bag (segment 44) | 4 |
Full catalog with exact SHA-256/sizes/bag YAML in `Resources/Karaoke/demucs-models.json` — copy verbatim. Download URL
`https://huggingface.co/<repo>/resolve/<revision>/<file>`, timeout 60 s, streamed with size cap, verified, atomic rename, cached in
`Karaoke/DemucsModels/<revision>/<file>` (demucs_backend.py:27-54). Progress = 0…0.15 during download.

**User-added models** ("Add model…", picker title "Add karaoke model", types ckpt/onnx/safetensors):
- Config auto-discovery (`configuration(for:)`, :113-127): demucs/safetensors → itself. ONNX → `<base>.json` then the bundled UVR
  catalog `Resources/Karaoke/mdx-model-data.json` (35 entries keyed by UVR hash). Ckpt → `config_deux_becruily.yaml` first if the
  file is `becruily_deux`, then `<base>.yaml`, `<base>.yml`, `config_<base>.yaml`. If none: second picker ("Choose UVR MDX parameters"
  JSON / "Choose model configuration" YAML).
- Auto-discovery (`discoverAll`, :129-141): `~/Downloads/Otras` then `~/Downloads`, files `*.ckpt`/`*.onnx` sorted naturally, added if a
  valid config is found. Windows: `FOLDERID_Downloads`.
- MDX validation (:59-83): JSON object either itself holding `mdx_dim_f_set` or keyed by **UVR hash** = MD5 of the **last 10 240 000 bytes**
  of the .onnx (whole file if smaller) (:85-91). Required: `primary_stem ∈ {Vocals, Instrumental}`; integers `mdx_n_fft_scale_set`
  (2048…32768, even), `mdx_dim_f_set` (1…n_fft/2+1), `mdx_dim_t_set` (4…10) with `1024*(2^t − 1) > n_fft`; `compensate` number in (0, 4].
- MDX settings persisted (`persisted(in:)`, :95-111) to `Karaoke/ModelSettings/<sha256 of sorted-keys settings JSON>.json`; weights stay
  where they are.
- Model list persisted in UD `karaoke.models` (JSON array of `{checkpoint, configuration}` Swift-URL objects), selected id in
  `karaoke.selectedModelID`, legacy `karaoke.checkpoint` / `karaoke.configuration` paths. On launch: saved (re-validated) + legacy +
  discovered + the five built-in Demucs entries; selection = saved id, else legacy, else first.
- Changing model: only when not processing; resets playback to Original and Stem Mix to defaults; refreshes cached audio for the track.

### 11.3 Cache layout & keys
- Root `AppSupport/Karaoke/`. Per prepared track+model: folder `Karaoke/<cacheKey>/` with `manifest.json`, `vocals.wav`,
  `instrumental.wav` and (Demucs) one WAV per model stem (`drums.wav`, `bass.wav`, `other.wav`, `guitar.wav`, `piano.wav`).
  All WAVs **stereo 44 100 Hz 32-bit float** (`subtype='FLOAT'`), exactly the source's frame count.
- `cacheKey` (:147-156) = lowercase hex SHA-256 of `"\n"`-joined: processing version (`demucs-4.1-six-stems-v1` | `uvr-mdx-overlap-v1` |
  `becruily-deux-overlap-v1`), trackID uppercase UUID, `identity(source)`, `identity(checkpoint)`, configuration path, SHA-256 hex of the
  configuration bytes (empty data for `.safetensors`). `identity(url)` = `"<path>|<size>|<mtime seconds since 1970 as Double>|<inode>"`
  (Swift's string interpolation of Double, e.g. `1729000000.123456`). On Windows use file index instead of inode — caches are not
  shared across OSes anyway.
- `manifest.json` = KaraokeAudio `{key, trackID, frames, sampleRate: 44100, modelSHA256, directory: {"relative":"file:///…"}, stemNames?}`
  (:172-206). `validate()`: sampleRate 44100, frames>0, unique valid stem names, every file (instrumental, vocals, stems) opens with
  exactly `frames` frames, 44.1 kHz, 2 ch — else "The saved karaoke audio is incomplete. Prepare it again."
- MDX Core ML conversion cache `Karaoke/ModelCache/MDX/<sha256(weights bytes + ort version + ':coreml-mlprogram-cpuandgpu-v1' +
  sorted params JSON)>/` (mdx.py:28-42). Temporary jobs in `Karaoke/.job-<UUID>/` (deleted after), log `processor.log` (stderr).

### 11.4 Preparation pipeline (`KaraokeStore.prepare`, :150-217)
1. If a valid cached manifest exists → "Ready — saved karaoke audio".
2. Decode the source with the app's own decoder to **44.1 kHz stereo float WAV** `source.wav` in the job folder
   (`KaraokePreparation.decode`, :318-332; uses `PCMDecoder(sampleRate: 44100)`).
3. Run `python3 separate.py --backend <kind> --checkpoint <path> --config <path> --input source.wav --output <job>` with env
   `PYTHONPATH=<site-packages>:<Karaoke dir>`, `PYTHONNOUSERSITE=1`, `PYTHONDONTWRITEBYTECODE=1`, `PYTHONUNBUFFERED=1`,
   `PYTORCH_ENABLE_MPS_FALLBACK=1`, `NUMBA_CACHE_DIR=<job>/.numba-cache`, MDX: `TMPDIR=<job>/`; `PYTHONHOME` removed (:263-316).
4. Progress protocol: stdout **JSON lines** `{"stage": str, "progress": 0…1, "device"?, "error"?, "frames"?, "sample_rate"?,
   "model_sha256"?, "stems"?, "gain"?}`; buffer cap 1 MiB. Final line must be `stage == "Ready"` with exit code 0; failures emit
   `{"stage":"Failed","error":…}` exit 1; Ctrl-C → "Cancelled" exit 130. Cancel = terminate the process.
5. Verify `frames` equals decoded frames, `sample_rate == 44100`, `model_sha256` 64 chars, and the cache key is unchanged (source/model
   not modified meanwhile) → validate → delete source.wav → move job folder to `Karaoke/<key>/` → write manifest.
6. UI stages (examples): "Preparing audio…", "Loading Mel-Band RoFormer", "Separating vocals and instrumental", "Converting or loading MDX for
   Core ML GPU", "Separating with Core ML (GPU enabled)" / "Separating with UVR MDX on CPU", "Downloading <title>", "Separating N stems",
   "Saving karaoke audio"/"Saving all stems", "Ready — karaoke audio saved", "Cancelled", "Couldn’t prepare karaoke".
- Only one preparation at a time ("Already preparing <title>. Wait for it to finish, or cancel preparation first."). Music keeps
  playing during preparation. Quit cancels it.

### 11.5 Separation algorithms (Python; keep identical)
Common: input must be stereo 44.1 kHz; read in 262 144-frame blocks into a disk memmap; non-finite → error; outputs normalised by one
shared gain so the loudest sample ≤ 0.98.

**Mel-Band RoFormer / Becruily Deux** (separate.py:26-145): YAML must have `training.instruments == ['Vocals','Instrumental']`,
`audio.sample_rate == 44100`, `model.num_stems == 2`, `model.stereo`. YAML loader also accepts `!!python/tuple`. Model =
`MelBandRoformer(**config.model, match_input_audio_length=True)` from bundled `models/bs_roformer` (MSST code, rev in MSST-REVISION.txt),
`torch.load(weights_only=True)` (unwrap `state_dict`), strict. chunk = `inference.chunk_size` (or `audio.chunk_size`), overlap =
`inference.num_overlap` (default 2). Overlap-add (:44-92): `chunk = min(chunk, max(2048, frames))`, `step = chunk // overlap`,
`border = chunk − step` if `frames > 2*(chunk − step)` else 0, reflect-pad by border on both ends (index mirroring with period
2(frames−1)), last partial chunk padded reflect (if > half) or zeros; window = ones with linear fades of `chunk//10` at both ends (first
window has no fade-in, last no fade-out); accumulate prediction × window and sum of weights; output = result/count over the original
range. Output order: prediction[0] = vocals, [1] = instrumental. MPS failures switch to CPU mid-run. Gain = `min(1, .98/max(.98, peak))`.
`model_sha256` = SHA-256 of checkpoint.

**UVR MDX ONNX** (mdx.py): `chunk = 1024*(2^dim_t − 1)`, `trim = n_fft//2`, `generated = chunk − 2*trim`, `tail = generated + trim −
frames % generated`, `total = frames + trim + tail`, `step = chunk//2` (50 % overlap), starts `range(0, frames + trim, step)`.
Per block: zero block, copy audio offset by `trim`; STFT (Hann window n_fft, hop **1024**, center=True) on L and R → tensor
`[1, 4, dim_f, T]` with planes L.re, L.im, R.re, R.im, truncated to `dim_f` bins; zero the lowest **3** frequency bins; denoise:
`pred = (run(spec) − run(−spec)) * 0.5`; inverse: pad bins back to n_fft/2+1, iSTFT same params, length = chunk; weight =
`np.hanning(length)`; overlap-add and divide by weights. primary = result × `compensate`; complement = original − primary; vocals/
instrumental assigned by `primary_stem`. Session: single input/output 4-D float tensors `[1|sym, 4, dim_f, 2^dim_t]` validated;
free dims `batch_size|batch|batch_dim` overridden to 1; intra-op threads ≤4, inter-op 1; Core ML → CPU fallback retries the current
block. `model_sha256` = SHA-256 of the ONNX.

**Demucs** (demucs_backend.py:57-185): load only `.safetensors` with metadata `klass ∈ {demucs.demucs.Demucs, demucs.hdemucs.HDemucs,
demucs.htdemucs.HTDemucs}`, `args`/`kwargs` JSON (fractions encoded `{"_type":"fraction",...}`), no `structure` key (quantized
rejected); must be 44.1 kHz stereo, sources ⊆ {drums,bass,other,vocals,guitar,piano} incl. vocals. Built-ins wrapped in
`BagOfModels(models, weights)` per catalog `bag`. Normalise by whole-track mono mean/std; `chunk = min(model.segment (or
max_allowed_segment), 6.0 s) * 44100`, `step = chunk//2`, Hann window (first window flat for its first half, last window flat for
its last `step`); `apply_model(shifts=0, split=False)`; de-normalise `pred*std + mean/len(sources)`. Instrumental = sum(all stems) −
vocals. Gain = `.98/max(.98, peak over stems, instrumental, sum of stems)`. Writes `<stem>.wav` for every model source +
`instrumental.wav`; `model_sha256` = SHA-256 of concatenated per-file SHA-256 hex strings. Reports `stems` (model order).

### 11.6 Playback modes & UI rules (`KaraokeStore.requestPlayback`, :85-118; `KaraokePlaybackButtons.swift`)
- Player-bar control (126 pt wide capsule): row 1 = **Karaoke** (mic.slash; plays `instrumental`), **Vocals Only** (mic), Stem Mix
  sliders icon (opens Karaoke window); row 2 = Drums, Bass, Guitar, Piano, Other "Only" icons. Selected = accent capsule; displayed
  selection = pending requested mode ?? current mode.
- Clicking a non-active mode: if prepared audio for the current track+model exists and has that stem → switch immediately at the same
  position; else remember the request (last click wins) and start preparing; switch automatically when ready **only if the same track
  is still current**. Clicking the active icon → Original ("No Karaoke"); Original never cancels a running preparation.
- Instrument modes force the Demucs **6-stem** model (`htdemucs_6s.demucs`) when the selected model isn't Demucs, or is a built-in
  4-stem Demucs and guitar/piano is requested, or the cached audio lacks that stem (error if a preparation is running: "Finish or
  cancel preparation before switching to the six-stem model.").
- While preparing: thin progress bar + "NN%" under the icons (click → popover: "Preparing <title>" marquee, stage, progress, "Original
  audio continues playing." or "Switching to <mode> when ready.", Cancel preparation, Open Karaoke…). Error → orange "Try again" and the
  popover opens automatically. Right-click → Open Karaoke… / Cancel preparation.
- Track change → back to Original (player) and Stem Mix reset.

### 11.7 Karaoke window (`KaraokeWindowController.swift`, `KaraokeView.swift`, `StemMixView.swift`)
- Window "Karaoke" 680×820, min 600×740, autosave `CoverFlow.Karaoke.Frame`; ⌘5 / player-bar context menu.
- Top: title, "Add model…", "Model" picker (titles; tooltip path), kind subtitle, 6-stem experimental note ("Guitar and piano separation is
  experimental; some instruments may bleed into other stems."), progress row with Cancel, or "Karaoke audio ready" + "Export
  instrumental…", or "Prepare this track once, then switch to its instrumental.", errors in red.
- **Stem Mix** disclosure: grid (2 columns) of available stems: mute toggle, "S" solo toggle (multi-solo), 0–100 % level slider; "Use Stem
  Mix"/"Stem Mix Selected", Reset, "Export stem…" menu (each stem, "All stems…"). Any change activates `.mix` playback. Without audio:
  "Prepare stems" (selects 6-stem Demucs first).
- Exports (KaraokeStore.swift:229-269): single stem via save dialog, default name `"<title with / → -> — <Stem>.wav"`, copied through a
  temp file and atomically replaced; refuses to overwrite cache files or the original track ("Choose a different filename so the
  original audio stays unchanged."). All stems → chosen folder + subfolder `"<title> — Stems <8 hex>"` with every stem WAV + instrumental.
- Bottom: embedded Lyrics view with playback actions Original [1] / Karaoke [2] / Vocals [3] / Prepare [P] (key badges).
- Window-local shortcuts (no modifiers, ignored while editing text or when a sheet/modal is up, non-repeat): Space play/pause, 1 Original,
  2 Karaoke, 3 Vocals (only when audio prepared), P prepare.

## 12. Visualizer (`Visualizer/`)

### 12.1 Window & controls (`VisualizerController.swift`)
- Window "Cover Flow Visualizer" 1000×660, min 720×420, always dark, full-screen capable, autosave `CoverFlow.Visualizer.Frame`.
  Opened by ⌘3 / toolbar waveform button. Opening enables the audio tap buffer; closing disables it and destroys the content.
- Toolbar: engine label ("Cover Flow" or "MilkDrop"), ‹ prev, menu (Metal styles or presets list with checkmark + "Add MilkDrop Presets…",
  then "Use Metal Visualizer"/"Use MilkDrop Presets"), › next, settings popover (Engine Metal/MilkDrop; Metal only: **Response** slider
  0.5…2.5 default 1.25, Quality "High · 60 fps" / "Balanced · 30 fps"; note "Response changes the animation, never your playback volume."),
  **Auto** switch (default on, not persisted), full-screen button. Bottom bar: prev / play-pause / next, title+artist or "Play some music to
  bring the visualizer to life", "Paused". Error overlay with "Use MilkDrop" fallback button.
- Keys (no ⌘⌥⌃): Space play/pause; ←/→ previous/next style or preset; Esc leaves full screen, else closes.
- Auto (`tick`, every 1 s): if playing (not buffering), ≥ **25 s** since last change and Reduce Motion off → Metal: next style; MilkDrop:
  random preset ≠ current.
- UD keys: `visualizer.useMilkDrop` (bool), `visualizer.style` (int 0–3), `visualizer.response` (double, default 1.25),
  `visualizer.highQuality` (bool, default true).
- Presets: bundled `Visualizer/Presets/*.milk` (8 original presets "01 - Aurora" … "08 - Prism") + user folder
  `AppSupport/Visualizer/Presets/`; sorted by name (natural). Import copies `.milk` files there, de-duplicating names as
  `"<name> (2).milk"`, `(3)`…, then selects the last added; non-.milk → "Choose files ending in .milk to add MilkDrop presets."

### 12.2 Audio feed (`VisualizerAudioBuffer.swift`, `GaplessAudioPlayer` tap)
- Tap on the post-volume output mixer, 512-frame buffers, 48 kHz stereo; ring buffer of **8192** interleaved stereo frames; writer uses
  `try-lock` (never blocks the audio thread) and only writes when enabled; `drain` returns only the most recent frames and empties the
  buffer (no latency build-up); cleared on pause/seek/load. Note: the tap is *after* master volume (so a muted player feeds silence).

### 12.3 Analyser (`VisualizerSignal.swift`) — port exactly
- FFT size **2048** (Hann window `0.5 − 0.5cos(2πi/N)`), **64 bands**, waveform **512** points. `dt = clamp(elapsed, 1/240, 0.1)`.
- Not playing / no samples: if playing stopped → reset history; decay all values by `exp(−dt/0.12)` when not playing or after 0.12 s
  without samples (display frames between audio callbacks don't insert silence).
- Per frame: append samples (clamped to ±1, NaN→0) into L/R ring buffers; `rms = sqrt(mean((l²+r²)/2))`; automatic gain
  `target = clamp(0.18/max(0.025, rms), 1, 6)` smoothed (attack 0.12 s, release 0.5 s); `audible = rms > 0.0003`;
  `scale = gain * clamp(response, 0.5, 2.5) * audible`.
- Spectrum: FFT of L and R separately; `magnitude[k] = sqrt((|L_k|² + |R_k|²)/2) * (4/2048) * scale` (stereo power combined *after* FFT).
- Band boundaries `f(b) = 30 * 600^(b/64)` Hz (30 Hz … 18 kHz); bins `[max(1, f(b)*2048/48000), max(low+1, f(b+1)*2048/48000))`;
  band target = `clamp((20·log10(max(1e-5, peak)) + 66) / 60, 0, 1)` (66 dB floor, 60 dB range); smoothed attack 0.012 / release 0.18.
- `approach(old, target, dt, attack, release) = old + (target−old)(1 − exp(−dt/τ))`, τ = attack if rising else release.
- bass/mid/treble = max band target within 35–220 / 220–2500 / 2500–16000 Hz (by band lower edge), smoothed 0.012/0.16.
  level = approach(min(1, rms·scale·3), 0.01/0.2). onset = approach(clamp((meanTarget − lastMean)·6), 0.006/0.12).
- Waveform: last 512 samples, per sample the channel with larger |x| (avoid phase cancellation), `tanh(x·scale·1.8)`.

### 12.4 Native renderer (`NativeVisualizerSurface.swift`, `Shaders/Visualizer.metal`)
- MTKView sRGB BGRA8, clear (0.003, 0.006, 0.016); FPS = 15 (Reduce Motion) / 60 (High) / 30 (Balanced); renders only when the window
  is visible and not occluded; max 3 frames in flight. One full-screen triangle; fragment uniforms
  `{float2 size; float time; float style; float4 audio(bass, mid, treble, level); float onset; float motion; float2 pad}` + `bands[64]`
  + `wave[512]` buffers. `time` advances only while playing; `motion = 0` with Reduce Motion.
- Shader (88 lines) is plain GLSL-translatable (hash/noise/palette/glow functions, 4 styles: Pulse ring spectrum, Aurora ribbons,
  Spectrum bars with reflection, Nebula fbm cloud), vignette `1 − smoothstep(.35,1.15,r)·.65`, tone-map `1 − exp(−c·1.35)`. Port to a
  Qt `ShaderEffect` (GLSL 440 → qsb) or QRhi; pass bands/wave as a 64×1 and 512×1 float texture (ShaderEffect can't take SSBOs).
  Styles: Pulse(0) / Aurora(1) / Spectrum(2) / Nebula(3); persisted index.

### 12.5 projectM / MilkDrop (`MilkDropSurface.swift`, `Sources/CProjectM/`)
- projectM **4.1.8** (LGPL-2.1) built as a shared library and **dlopen'ed** at runtime from `Contents/Frameworks/libprojectM-4.dylib`
  (keeps LGPL relinkable). Windows: `LoadLibraryW("projectM-4.dll")` + `GetProcAddress` with the same symbol list (CFProjectM.cpp:56-67):
  `projectm_create, projectm_destroy, projectm_load_preset_file, projectm_set_window_size, projectm_opengl_render_frame,
  projectm_pcm_add_float, projectm_pcm_get_max_samples, projectm_set_preset_locked, projectm_set_soft_cut_duration, projectm_set_fps,
  projectm_set_mesh_size, projectm_set_preset_switch_failed_event_callback`. Only `dlfcn.h` / `OpenGL/gl3.h` are Apple-specific.
- Settings on create: preset **locked** (app controls switching), soft-cut **2.5 s**, fps **60**, mesh **96×72**, failure callback stores the
  message. Load with smooth transition except the first preset.
- GL: OpenGL **4.1 core** context, double buffered, vsync, Retina backing size; a 60 Hz timer drives frames; per frame drain up to 8192
  stereo frames and feed the newest `min(frames, projectm_pcm_get_max_samples())` as `PROJECTM_STEREO` interleaved float; if paused,
  buffering, or no audio for 0.12 s feed **1024 frames of silence**; bind FBO 0 then render. Errors shown in the overlay
  ("The visualizer engine is missing…", "…incompatible API.", preset load failures).
- Qt: `QOpenGLWindow`/`QQuickFramebufferObject` (Qt 6 needs the OpenGL RHI backend: `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`
  for that window) or render projectM into an FBO texture; projectM's Windows build uses GLEW/ANGLE-free desktop GL 3.3+.

## 13. Settings & persisted preferences

Settings window (Views/SettingsView.swift), grouped form 520×620:
- **Appearance**: Theme segmented Dark / Light / Match System + caption.
- **Album View**: "Keep Album View above other windows", "Artwork focus: hide controls when idle" + caption.
- **Last.fm** (§10).
- **Your Library & Saved Data**: explanation (files are never copied/moved/edited; moved files show as missing), list of data files,
  "Show Data Folder in Finder" (Windows: open `%APPDATA%\CoverFlow` in Explorer).
- **Demo Collection**: Add/Rebuild, Remove.
- **Reset**: "Remove Everything from Collection…" → "Remove every album from Cover Flow?" / "Your audio files stay on disk. You can import
  them again later." / Remove All.

**Complete UserDefaults key list** (use identical QSettings keys):
| key | type | default | owner |
|---|---|---|---|
| `browseMode` | "coverFlow"/"grid" | coverFlow | AppState |
| `sortOrder` | "artist"/"title"/"recentlyAdded"/"recentlyPlayed" | artist | AppState |
| `theme` | "dark"/"light"/"system" | dark | AppState |
| `browsedAlbum` | UUID string | – | AppState |
| `showQueue` | bool | false | AppState |
| `didSeedDemo` | bool | false | first launch |
| `albumViewOpen`, `albumView.open` | bool | false | AlbumViewController |
| `albumView.keepOnTop` | bool | **true** | AlbumViewController |
| `albumView.focusMode` | bool | false | AlbumViewController |
| `albumView.immersiveScenes` | bool | **true** | AlbumViewController |
| `albumView.preset` | "compact"/"large"/"immersive"/"" | compact | AlbumViewController |
| `lastfmEnabled` | bool | false | LastFmScrobbler |
| `karaoke.models` | JSON data `[{checkpoint, configuration}]` | – | KaraokeStore |
| `karaoke.selectedModelID` | string (path\npath) | – | KaraokeStore |
| `karaoke.checkpoint`, `karaoke.configuration` | path strings (legacy) | – | KaraokeStore |
| `visualizer.useMilkDrop` | bool | false | VisualizerController |
| `visualizer.style` | int 0–3 | 0 (Pulse) | VisualizerController |
| `visualizer.response` | double | 1.25 | VisualizerController |
| `visualizer.highQuality` | bool | true | VisualizerController |
| NSWindow frame autosaves | – | – | `CoverFlow.AlbumView.Frame`, `CoverFlow.Lyrics.Frame`, `CoverFlow.Karaoke.Frame`, `CoverFlow.Visualizer.Frame` |

Full AppSupport tree:
```
CoverFlow/
  Library.json  Playback.json  LastfmPending.json  [Library-unreadable-<ts>.json]
  Artwork/<albumUUID>-<uuid>.<ext>
  Artwork/Scenes/<albumUUID>-<ver>-scene-v5-<var>[-sd2-<style>].png
  Artwork/Scenes/<albumUUID>-<ver>-immersive-v3-<style|base>-<W-H-x-y-w-h>-<hash>.png
  Demo Collection/<Album>/NN Title.wav
  Lyrics/<trackUUID>.lrc
  Visualizer/Presets/*.milk
  Karaoke/<sha256 cacheKey>/{manifest.json, vocals.wav, instrumental.wav, drums.wav, bass.wav, other.wav, guitar.wav, piano.wav}
  Karaoke/ModelSettings/{htdemucs_6s,htdemucs,htdemucs_ft,hdemucs_mmi,mdx_extra}.demucs, <sha256>.json
  Karaoke/DemucsModels/<hf revision>/<sig>.safetensors
  Karaoke/ModelCache/MDX/<sha256>/          (Core ML conversions; not needed with DirectML)
  Karaoke/.job-<uuid>/                      (transient)
```

## 14. Apple-only dependencies → Windows equivalents

| Apple API / framework | Used for (file) | Windows / Qt replacement |
|---|---|---|
| SwiftUI, AppKit (NSWindow, NSHostingView, NSOpenPanel/NSSavePanel, NSSearchField, NSEvent local monitors, NSOpenGLView, NSWorkspace) | all UI, window management, scroll/keys capture, file dialogs, "Show in Finder", open URL | Qt Quick/QML + QtQuick.Controls, `QQuickWindow`, `QFileDialog`/`FileDialog`, event filters, `QDesktopServices::openUrl`, `explorer /select,` |
| `rotation3DEffect`, springs, TimelineView | Cover Flow 3D layout & animation, marquee, bars | QML `Matrix4x4`/`Rotation` with perspective or Qt Quick 3D; custom spring integrator in C++/JS driven by `FrameAnimation` |
| AVFoundation (AVURLAsset metadata, AVAudioEngine/PlayerNode/MixerNode/Converter, AVAudioFile) | tag reading, gapless playback, stem mixing, resampling, WAV IO | FFmpeg (libavformat/avcodec/swresample) + TagLib; WASAPI (or miniaudio) render loop; libsndfile/dr_wav for WAV |
| Core Audio ExtAudioFile, Core Audio Opus decoder | decoding native formats & Opus | FFmpeg (incl. libopus or native opus decoder) |
| ImageIO / CoreGraphics / UniformTypeIdentifiers | image header/orientation, thumbnails, PNG/JPEG encode, raster ops, type detection | `QImageReader` (autoTransform, scaledSize), `QImage`, `QImageWriter`, `QMimeDatabase` |
| Vision (`VNRecognizeTextRequest`, `VNDetectFaceRectanglesRequest`, `VNGenerateAttentionBasedSaliencyImageRequest`) | exclude text/faces/subjects from scene texture sampling (AlbumSceneStore.swift:231-250) | `Windows.Media.Ocr` or OpenCV DNN text detector; OpenCV YuNet/Haar; OpenCV saliency / U²-Net ONNX |
| Core ML + apple/ml-stable-diffusion 1.1.1 | SD 1.5 img2img scene refinement on the Neural Engine | ONNX Runtime + DirectML SD 1.5 pipeline (C++), or optional Python diffusers; optional feature |
| Accelerate (vDSP.DFT) | visualizer FFT | KissFFT / PocketFFT / FFTW |
| Metal / MetalKit (MTKView, .metal shader) | native visualizer | Qt `ShaderEffect` (GLSL→qsb) or QRhi custom item |
| OpenGL.framework + NSOpenGLView | projectM rendering | `QOpenGLWindow`/`QQuickFramebufferObject` with OpenGL RHI |
| `dlopen/dlsym` | load projectM-4 dylib | `LoadLibraryW`/`GetProcAddress` (or `QLibrary`) |
| MediaPlayer (MPRemoteCommandCenter, MPNowPlayingInfoCenter) | media keys, system Now Playing | SystemMediaTransportControls (WinRT) + `WM_APPCOMMAND` |
| Security (Keychain) + LocalAuthentication | Last.fm credentials | Windows Credential Manager (`CredReadW/CredWriteW`) or DPAPI |
| CryptoKit (MD5, SHA-256) | Last.fm signature, cache keys, UVR hash | `QCryptographicHash` (Md5, Sha256) |
| CFString (CFArtworkTags.cpp `nativeString`) | MacRoman fallback for AIFF text chunks | `QStringDecoder`/manual Latin-1 fallback |
| NSAccessibility announcements, `accessibilityReduceMotion` | VoiceOver announcements, Reduce Motion | `QAccessible` / UIA notifications; `SystemParametersInfo(SPI_GETCLIENTAREAANIMATION)` |
| UserDefaults, NSWindow frame autosave | preferences | `QSettings` with same keys; save/restore geometry |
| FileManager/Darwin `stat`, `rename`, `fstatat` (inode/ctime) | atomic artwork writes, duration cache revision | `ReplaceFileW`/`MoveFileExW`, `GetFileInformationByHandle` (volume serial + file index), `QFileInfo` |
| `Process` + bundled python-build-standalone (arm64) | karaoke separation worker | `QProcess` + embeddable CPython (win_amd64) with CUDA/DirectML wheels |
| PyTorch MPS / ONNX Runtime CoreML EP | separation acceleration | PyTorch CUDA (or `torch-directml`), ONNX Runtime DirectML/CUDA EP |
| `mach_absolute_time`, `ProcessInfo.systemUptime` | node start timing, scrobble monotonic clock | `QElapsedTimer` / `QueryPerformanceCounter` |
| `NSEvent.hasPreciseScrollingDeltas`, scroll phases | trackpad vs wheel Cover Flow scrolling | `QWheelEvent::pixelDelta()`/`angleDelta()`, `phase()` |

Portable as-is: `Sources/CFFmpegAudio/CFAudioFile.c` (replace `pthread_once`), `Sources/CArtworkTags/CFArtworkTags.cpp` (replace CF helper),
`Sources/CProjectM/CFProjectM.cpp` (replace dlopen/GL header), all `Resources/Karaoke/*.py` (device selection), JSON catalogs, `.milk`
presets, and the visualizer shader logic.

## 15. Porting difficulty ranking & external assets

Ranked hardest → easiest (effort to reach parity on Windows):
1. **Gapless playback engine + stem mixing** (§6) — custom WASAPI pull renderer, 8 s lookahead decode thread, sample-accurate segment
   accounting, multi-stem synchronized mixing, device-change recovery, pause-position commit. High risk of subtle glitches; core feature.
2. **Karaoke pipeline** (§11) — packaging a Python+torch runtime on Windows (size: ~2–3 GB with CUDA wheels), GPU device selection, process
   management, cache/manifest compatibility. Algorithms themselves are reusable verbatim.
3. **Stable Diffusion refinement** (§4.5) — needs a new ONNX/DirectML SD 1.5 img2img implementation (tokenizer, scheduler, VAE/UNet
   sessions) and ~2 GB model; optional, can ship later.
4. **Cover Flow 3D view** (§5.2) — math is fully specified; difficulty is matching SwiftUI's perspective/spring feel and smooth
   precision-touchpad/momentum handling in Qt Quick.
5. **Quilted scene generator + Vision replacements** (§4.3, §4.6) — deterministic CPU algorithm (port line-by-line, parallelise rows);
   detector substitutes affect only sampling masks.
6. **projectM / MilkDrop** (§12.5) — projectM builds on Windows; integrating an OpenGL surface into a Qt Quick (RHI) scene is the fiddly part.
7. **Native visualizer** (§12.3-12.4) — FFT + analyser port + GLSL translation of one shader.
8. **Album View window** (§7) — frameless/always-on-top window, presets, auto-hide chrome, custom resize grip.
9. **Tag reading / artwork embedding / import** (§3) — TagLib + FFmpeg cover everything; reuse CFArtworkTags.cpp and CFAudioFile.c.
10. **Last.fm** (§10), **Lyrics + LRC** (§8), **online lookups** (§9), **queue/shuffle/repeat logic** (§6.3), persistence (§2, §13),
    settings, demo collection — straightforward ports of small, well-specified logic.

**External assets the app downloads or expects**
| Asset | Source | Size | When |
|---|---|---|---|
| Stable Diffusion 1.5 Core ML (6-bit palettized, split-einsum v2, compiled) | huggingface.co/apple/coreml-stable-diffusion-v1-5-palettized, SHA-256 `49a6ac1f…14ec` | ~1.6 GB zip | build time, bundled in app (`COVERFLOW_SKIP_SD=1` omits; then refinement UI hidden). Windows: need an ONNX equivalent. |
| Demucs weights (5 models, 9 safetensors files) | huggingface.co/adefossez/{HTDemucs-6s, HTDemucs, HTDemucs-ft, Demucs-hdemucs_mmi, Demucs-mdx_extra} at pinned revisions; sizes/SHA-256 in `Resources/Karaoke/demucs-models.json` | 55 MB (6s) … 4×167 MB (mdx_extra) | runtime, on first preparation; cached in `Karaoke/DemucsModels/` |
| Mel-Band RoFormer "Becruily Deux" `.ckpt` + YAML | user-supplied (not redistributed) | – | discovered in Downloads/Otras, Downloads or via Add model… |
| UVR MDX ONNX (e.g. UVR-MDX-NET-Inst_HQ_5) | user-supplied; parameters from bundled `mdx-model-data.json` (35 UVR fingerprints, TRvlvr/application_data rev 3826b05b) | – | user |
| Karaoke Python runtime | python-build-standalone cpython 3.12.7+20241016 aarch64 (SHA-256 `4c18852b…cf9508`) + wheels in `runtime-lock.txt` | large | build time, bundled |
| MSST Mel-Band RoFormer model code | `Resources/Karaoke/models/bs_roformer/*` (rev `5a3e16d1…`) | – | bundled source |
| projectM 4.1.8 | github.com/projectM-visualizer/projectm release tarball | – | build time, dylib bundled |
| 8 MilkDrop presets | `Sources/CoverFlow/Visualizer/Presets/*.milk` (original) | 15 KB | bundled; users can import more |
| FFmpeg 7.1.2 (decoder-only LGPL: demuxers ogg, ape, wv, asf, dsf, iff, matroska, mpc, mpc8, tta, tak, shorten, dts, w64; decoders vorbis, opus, flac, speex, ape, wavpack, wma*, dsd_*, mpc7/8, tta, tak, shorten, dca, aac, ac3, eac3, mp1/2/3float, alac, truehd, mlp, pcm_*) | ffmpeg.org | – | build time, shared libs bundled. Windows: also enable mov/mp3/wav/aiff/flac/caf/amr demuxers since there is no AVFoundation. |
| TagLib 2.3.2 (static) | taglib.org, SHA-256 `3ca2d8af…63dc` | – | build time |
| Online services (no keys) | LRCLIB, MusicBrainz, Cover Art Archive; Last.fm (user's own API key/secret) | – | on demand |
| Licenses to ship | `Resources/Licenses/*` (FFmpeg/projectM LGPL-2.1, TagLib MPL-1.1, Demucs MIT, ml-stable-diffusion MIT, glm, SOIL2, utfcpp, hlslparser, projectm-eval) + `Resources/ThirdPartyNotices.md` | – | – |


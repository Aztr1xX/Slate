# Changelog

Every push to `main` publishes a new release named `1.1.<build number>`, and installed copies of Slate
update themselves to it. Builds that only fixed the CI are left out below.
Downloads: [Releases](https://github.com/Aztr1xX/Slate/releases).

## 1.1.30 (2026-11-15)
A full pass over how Slate looks and feels, plus speed work so it runs well on older computers.

- **Go to a time** (Ctrl + G, or click the big timecode): type `90`, `1:30`, a full timecode, `f450` for a
  frame, or `+5` / `-2` to jump from where you are.
- **Drag the In and Out edges** on the timeline. They snap to markers and to each other (hold Shift to stop
  snapping), and the range shows its length. A hover line shows where a click will land.
- Markers on the timeline: click one to jump to it, hover to read its note.
- **Shift + M** drops a Good take marker and **Alt + M** a Mistake, without opening the panel.
- A small overlay at the top of the picture confirms volume, speed, In / Out, loop and marker changes.
- **Right-click the picture** for play, markers, In / Out, copy or save the frame, go to, zoom, crop guide,
  full screen and show in folder.
- Star rating for the current clip in the header.
- The Markers panel shows each marker's kind in colour, and a new card shows the In / Out times, clip
  length and a clear button.
- Side panel: tabs now have labels, and the panel can be resized by dragging its left edge (double-click to reset).
- Esc steps back out of full screen, then a comparison, then zoom, then the crop guide.
- The command palette puts the actions you used recently first.
- Clearer text: secondary text now has enough contrast to read comfortably, shortcuts are drawn as keys,
  and the start screen lists the main keys.
- **Lighter video rendering** option in Settings → Playback for older laptops and integrated graphics.
- Faster: dialogs and side tabs only load when first opened, thumbnails, waveforms and loudness run at low
  priority with fewer threads so playback never stutters, long clips build their preview strip from
  keyframes, the waveform no longer rescans the whole clip on every redraw, and frame rate changes no
  longer block the interface.
- Fixed: after editing a marker note, keyboard shortcuts could stop working until you clicked the picture.

## 1.1.26 (2026-11-15)
- **Download from YouTube** (Ctrl + D, the download button in the header, or "Download video" on the start
  screen). Paste a link, choose MP4, MKV, WebM, MOV or AVI, or audio only as MP3, M4A, WAV, FLAC, Opus or
  OGG, and pick a resolution. It always takes the best video and best audio streams and joins them without
  re-encoding when the format allows. Downloads keep going in the background and open in Slate when done.
- Slate keeps its own copy of yt-dlp up to date, which also keeps playing YouTube links working.

## 2026-11-04
- The GitHub repository is now **Aztr1xX/Slate**. The updater checks the new address and follows
  GitHub's redirect if the name ever changes again.
- New README with screenshots, plus LICENSE, third-party notices and this changelog.

## 1.1.12 (2026-10-31)
- Audio levels moved to their own side tab (Alt + 6).
- Scopes are drawn off the UI thread, so playback stays smooth while they are open.

## 1.1.11 (2026-10-31)
- Crop guide button next to the speed control; right-click it to pick the ratio.
- Picture zoom: Ctrl + scroll at the pointer, drag to pan, Z for 200% and fit.
- Live left and right audio meter, with buttons to listen to one channel alone.
- Copy the current frame to the clipboard (Ctrl + C).
- Fixed a rare crash when ending a side-by-side comparison.

## 1.1.10 (2026-10-31)
- Crop guide (G): 9:16, 4:5 and 1:1 frames over the picture, draggable sideways.
- Exports go to a `Slate exports` folder beside the clip, or a folder chosen in Settings.
- Media keys, a remembered side tab, a hint after exporting, and a command palette that always shows whole rows.

## 1.1.9 (2026-10-31)
- Built-in updates: Slate checks GitHub releases, downloads new versions in the background and
  installs them silently on restart.
- New app icon: a fanned stack of glass clip cards with a blue-to-violet glow.
- Centred play button, redrawn settings gear, and the volume percentage shown on hover.

## 1.0 (2026-10-31)
- Renamed from "Take" to **Slate**.
- Redesigned interface with a side panel for markers, clips, media info, transcript and scopes.
- Media info with loudness (LUFS) and variable frame rate warnings.
- Video scopes: luma waveform, RGB parade, histogram and vectorscope.
- Offline transcription with whisper.cpp, saved as SRT or text.
- Windows installer with Open with, Review folder in Slate and default app registration.

## First version (2026-10-31)
- libmpv playback in a Qt Quick interface, frame stepping, SMPTE timecode and a waveform timeline.
- Markers with notes, exported as an EDL for DaVinci Resolve, CSV or YouTube chapters.
- Lossless and frame-exact In to Out export, star ratings and natural-order folder playback.

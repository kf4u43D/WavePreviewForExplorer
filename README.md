## 🟦 WavePreview for Explorer

**Audio preview and waveform thumbnails directly inside Windows Explorer.**

WavePreview for Explorer is a native Windows shell extension designed to enhance the way audio files are browsed. It brings fast preview, clean waveform visualization, and essential audio metadata directly into the Explorer interface — without launching any external application.

---

## ✨ Features

### 🔊 Preview Handler

* Instant audio playback in the Explorer preview pane
* Play / pause / seek
* Clickable waveform navigation
* Clean, readable waveform display
* Technical info: duration, sample rate, bit depth, channels

### 🖼️ Waveform Thumbnails *(planned)*

* Visual identification of audio files directly in folders
* Lightweight waveform rendering
* Smart caching for performance

### 📊 Audio Metadata *(planned)*

* Basic: format, duration, bitrate, channels
* Advanced: BPM, key detection, RMS / peak *(future)*

---

## 🎯 Design Goals

* **Fast** – minimal latency, even on large sample libraries
* **Stable** – no impact on Windows Explorer reliability
* **Minimal** – focused on browsing, not editing
* **Professional** – clean UI, accurate technical information
* **Local-first** – no cloud, no telemetry

---

## 🧱 Architecture

* Native **C++ Windows Shell Extension (COM)**
* Preview Handler + Thumbnail Provider + optional Property Handler
* Lightweight **audio engine layer** (Media Foundation / libsndfile / miniaudio)
* **SQLite-based cache** for waveform and metadata
* Separate **Settings app** for configuration

---

## 📦 Supported Formats

**Initial support:**

* WAV (PCM / float)
* FLAC
* AIFF
* MP3

**Planned:**

* OGG / Opus
* AAC / M4A
* CAF
* Extended metadata formats (BWF, loops, etc.)

---

## 🚀 Use Cases

* Browsing large sample libraries
* Sound design workflows
* Field recording review
* Fast audio file triage
* Identifying files without opening a DAW

---

## ⚙️ Development

This project is designed to be built with:

* C++ (modern standard)
* CMake
* vcpkg (dependency management)
* Visual Studio / VS Code

See the `/docs` folder and `CODEX_PROMPT.md` for full setup instructions and development guidance.

---

## ⚠️ Notes

This project involves Windows shell integration. Stability and performance are critical.
All heavy processing is designed to be isolated from Explorer whenever possible.

---

## 📄 License

MIT

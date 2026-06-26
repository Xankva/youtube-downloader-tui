# ytd — YouTube Downloader TUI

A modern terminal user interface for downloading YouTube videos and audio, powered by [yt-dlp](https://github.com/yt-dlp/yt-dlp).

![screenshot](https://raw.githubusercontent.com/anomalyco/yt-tui/main/.github/screenshot.png)

## Features

- Browse and search YouTube videos directly in the terminal
- Download video (mp4) or extract audio (mp3, opus, flac, wav)
- Queue management with concurrent downloads
- Download history tracking
- File browser for output directory selection
- Audio-only toggle for music downloads

## Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | latest | Download backend |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | 5.x | Terminal UI framework |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.x | JSON parsing |
| [fmtlib](https://github.com/fmtlib/fmt) | 10.x | String formatting (included via FTXUI) |
| C++20 compiler | g++ 11+ / clang 14+ | Language standard |
| CMake | 3.16+ | Build system |
| pthread | — | POSIX threads |

## Quick Start

### Arch Linux (AUR)

```sh
paru -S ytd
```

### From Source

```sh
# Clone with submodules
git clone https://github.com/anomalyco/yt-tui.git
cd yt-tui

# Install yt-dlp
curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o bin/yt-dlp
chmod +x bin/yt-dlp

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./ytd
```

### Build with Make (vendored deps)

```sh
make -j$(nproc)
./ytd
```

## Usage

1. **Paste a YouTube URL** into the input field
2. Click **Download** to start downloading immediately
3. Toggle **Audio** for MP3-only extraction
4. Use **Browse** to change the output directory
5. Monitor progress, cancel downloads, or clear completed items from the queue

### Key Bindings

| Key | Action |
|-----|--------|
| `Tab` / Click | Navigate between sections |
| `↑` `↓` | Scroll lists |
| `Enter` | Confirm selection |
| `Ctrl+C` | Quit |

## Configuration

Config is stored at `~/.config/ytd/config.json`:

```json
{
  "output_dir": "~/Downloads",
  "output_template": "%(title)s.%(ext)s",
  "max_concurrent": 3,
  "max_retries": 3,
  "default_audio_format": "mp3",
  "default_video_format": "mp4"
}
```

## Project Structure

```
yt-tui/
├── src/           # Application source
│   ├── app.cpp    # Main UI logic and rendering
│   ├── queue.cpp  # Download queue management
│   ├── download.cpp # yt-dlp process management
│   ├── history.cpp # Download history persistence
│   └── config.cpp # Configuration loading/saving
├── include/       # Headers
├── deps/          # Vendored dependencies (FTXUI, nlohmann/json)
├── bin/           # yt-dlp binary
└── aur/           # Arch Linux PKGBUILD
```

## License

[MIT](LICENSE)

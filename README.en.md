# KO SimpleFill

An After Effects plug-in that fills a layer with a solid color using various blend modes.

**[Download latest release](https://github.com/yama-ko/KO-SimpleFill/releases/latest)**

[日本語はこちら](README.ja.md)

## Features

- **10 blend modes**: Normal, Add, Negative Add, Multiply, Screen, Overlay, Hard Light, Lighten, Darken, Difference
- **Opacity**: Controls blend strength for all modes (0–100%)
- **Invert Alpha**: Checkbox to invert the layer's alpha channel
- 8 bpc / 16 bpc support

> **Add** and **Negative Add** use a scale approach (amount scales the fill contribution).  
> All other modes use a lerp approach (amount lerps between source and blend result).

## Requirements

- After Effects 2024 or later
- Windows (macOS support planned)

## Installation

Copy `KO_SimpleFill.aex` to your After Effects plug-ins folder:

```
C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\
```

The effect will appear under **yama-ko.net > KO SimpleFill**.

## Building from Source

### Prerequisites

- Visual Studio 2022 or later (Desktop development with C++)
- [After Effects SDK](https://developer.adobe.com/after-effects/) (25.6 or later)

### Environment Variables

Set the following user environment variables before opening the solution:

| Variable | Description | Example |
|---|---|---|
| `AE_SDK_ROOT` | Path to the AE SDK `Examples` folder | `C:\AE_SDK\...\Examples` |
| `AE_PLUGIN_BUILD_DIR` | Output path for `.aex` file | `C:\...\Plug-ins\Effects` |

### Build

1. Open `Win\KO_SimpleFill.sln` in Visual Studio
2. Select **Release | x64**
3. Build (Ctrl+Shift+B)

## License

Apache License 2.0 — see [LICENSE](LICENSE)

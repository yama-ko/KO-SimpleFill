# Changelog

## [2.0.0] - Unreleased

### Added
- Blend modes aligned with KO BandFill: Soft Light, Linear Light, Hue, Saturation, Color, Luminosity, Dither, Dither Only
- Dither Seed parameter (used by Dither / Dither Only; grayed out unless a dither mode is selected)
- Multi-Frame Rendering (MFR) support
- SmartRender pipeline: 32-bit float rendering, plus layer-mask and compositing-options mask support (offset-sampled source + output-origin correction). Legacy Render retained as a fallback for hosts that don't drive SmartRender.

### Changed
- **Breaking:** Blend Mode popup reordered to match KO BandFill. Existing projects using **Hard Light, Lighten, Darken, or Difference** will need those blend modes re-selected (their popup index shifted when Soft Light and Linear Light were inserted). Normal, Add, Negative Add, Multiply, Screen, Overlay are unaffected.

## [1.0.1] - 2026-05-08

### Changed
- Add % unit display to Amount parameter
- Fix support URL in plugin metadata

## [1.0.0] - 2026-05-08

### Added
- Initial release

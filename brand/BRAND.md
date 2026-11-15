# Dusk brand notes

Dusk is a Windows tray app for colour temperature and brightness per monitor. It has no schedule and no network. A value stays where you set it. The look is calm and precise: dark glass over a softly lit stage.

## The mark

A ring with a lit disc inside, cut by a tilted horizon. The ring is the screen, the warm segment is the light you keep. It reads as one shape at 16 px and works in one colour.

| File | Use |
| --- | --- |
| `mark.svg` | Symbol in colour, for dark backgrounds |
| `mark-mono.svg` | Symbol in `currentColor`, for any single-colour use |
| `wordmark.svg` | The name set in Geist 600, outlined |
| `lockup.svg` | Symbol and name side by side |
| `icon.ico` | Exe and tray icon, 16 to 256 px |
| `icon-256.png`, `icon-1024.png` | Store and README use |
| `tray-16.png`, `tray-32.png` | Light one-colour tray icon for a dark taskbar |
| `github-banner.png` | Social preview, 1280 x 640 |
| `readme-header.png` | README header, 1600 x 500 |

For a light taskbar, recolour `mark-mono.svg` to `#0B0D12`.

## Colours

| Role | Hex |
| --- | --- |
| Paper | `#0B0D12` |
| Surface | `#1A1E27` |
| Ink | `#EEF0F4` |
| Ink soft | `#B4BAC6` |
| Muted | `#8E95A3` |
| Glow (the one accent) | `#8FA2FF` |
| Brand | `#2B3A8C` |
| Warm (mark only) | `#F0B26B` |

Warm is the dark-mode warning tone already in the house palette, reused so the mark can express colour temperature. It appears inside the mark and nowhere else. The interface keeps one accent, Glow.

## Type

Geist for words, 600 for names and titles with -0.03em tracking on large sizes. Geist Mono with tabular figures for every number, such as `4200 K` and `80 %`.

## Clear space and size

- Keep a margin of half the symbol height on every side of the mark and the lockup.
- Minimum size: symbol 16 px, lockup 96 px wide, wordmark 48 px wide.
- Below 32 px use the heavier tray cut (`tray-*.png`, `icon.ico` small sizes), not a scaled-down `mark.svg`.

## Do

- Put the mark on Paper or on the lit stage, with glow behind it.
- Use `mark-mono.svg` when only one colour is available.
- Keep the tilt as drawn. Scale the mark only in proportion.

## Do not

- Do not recolour the ring warm or the segment cool.
- Do not add rays, a moon, a sun face or a gradient to the mark.
- Do not set the name in another typeface or add effects to it.
- Do not place the colour mark on a light background. Use the mono mark in Paper instead.
- Do not use the warm tone in buttons, text or backgrounds.

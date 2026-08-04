# Snewsware

A Windows 11, no-admin, portable Roblox BedWars input utility with a fully custom native interface.

## Version 0.2 — refurbished UI

- Window title is now simply **Snewsware**.
- New black/orange ember-gradient visual system inspired by modern glassmorphism.
- Animated sidebar indicator and smooth 60 FPS section transitions.
- New dashboard, status surfaces, feature cards, toggles, and settings view.
- Lucide-derived vector navigation icons, included under the ISC license.
- Embedded Fisherman kit artwork in the Fisher automation background.
- High-DPI GDI+ rendering, antialiasing, gradients, rounded cards, and double buffering.

## Existing automation

- **SOCD Cleaner:** last-pressed A/D priority while Roblox is focused. Releasing the newest key resumes the other physically held key; releasing both returns to neutral.
- **Fisher:** checks for the fishing UI every 500 ms, switches to adaptive short-click control, and stops roughly 200 ms after the UI disappears.
- **Chest Looting:** checks every 80 ms for the supplied 6×4 layout, clicks all 24 slots by row at 20 ms intervals, presses E, and restores the cursor.
- **Emergency stop:** `Ctrl+Q` disables every module and releases synthesized inputs.
- Runs at normal user privilege and pauses when Roblox is not focused.

## GitHub build

1. Push the repository contents so `CMakeLists.txt` is at the repository root.
2. Open **Actions → Build Windows portable EXE → Run workflow**.
3. Download `snewsware-windows-x64-portable` from the finished run.
4. Extract the artifact and launch `snewsware.exe`.

## Local build

Requirements: Windows 11, Visual Studio 2022 Build Tools with **Desktop development with C++**, and CMake.

Run `build.bat` from an **x64 Native Tools Command Prompt for VS 2022**. The portable executable is copied to the repository root.

## Repository structure

- `src/main.cpp` — automation and native animated interface
- `src/snewsware.rc` — version metadata and embedded Fisherman artwork
- `assets/fisherman.png` — transparent Fisherman kit artwork
- `assets/icons/*.svg` — Lucide-style source vectors
- `ui-v2/*.png` — reviewed UI reference renders
- `THIRD_PARTY_NOTICES.md` — vector and artwork notices

## Important

This project is independent and is not affiliated with Roblox, Easy.gg, Lucide, or the referenced application. Automated input may violate game rules or affect an account. Review current game and platform rules before use. No process injection, memory reading, anti-cheat bypass, or game modification is included.

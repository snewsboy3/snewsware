# snewsware

A Windows 11, no-admin, portable BedWars input-automation utility with an original dark ember interface.

## Included in v0.1

- **SOCD Cleaner:** global low-level A/D handling while Roblox is focused. The newest pressed strafe key wins; releasing it resumes the other physically held key, otherwise movement returns to neutral.
- **Fisher:** checks for the fishing UI every 500 ms, switches to rapid adaptive short-click control while present, and stops within roughly 200 ms after the UI disappears.
- **Chest Looting:** checks every 80 ms for the shown 6×4 chest UI, clicks 24 slot centers left-to-right by row at 20 ms intervals, presses E, and restores the cursor.
- **Emergency stop:** `Ctrl+Q` disables every module and releases synthesized inputs.
- Runs only at normal user privilege and pauses when Roblox is not focused.

## Download from GitHub

1. Push this repository to GitHub.
2. Open **Actions → Build Windows portable EXE → Run workflow**.
3. Download `snewsware-windows-x64-portable` from the completed workflow.
4. For tagged builds (`v0.1.0`, etc.), the workflow also attaches `snewsware.exe` to the GitHub Release.

## Build locally

Requirements: Windows 11, Visual Studio 2022 Build Tools with **Desktop development with C++**, and CMake.

Run `build.bat` from an **x64 Native Tools Command Prompt for VS 2022**. The finished portable file is `snewsware.exe`. It has no installer and does not request elevation.

## Use

1. Launch Roblox normally, not as administrator.
2. Open `snewsware.exe` and toggle the desired modules.
3. Return focus to Roblox. Automation pauses whenever another window is focused.
4. Press `Ctrl+Q` at any time to stop everything.

## Detection notes

Detection is DPI-aware and based on colors and relative geometry rather than hard-coded screen coordinates. BedWars updates can change UI colors or layouts; thresholds in `src/main.cpp` may then need tuning. The chest module intentionally supports only the supplied 6×4 layout.

## Important

This is independent software and is not affiliated with Roblox, Easy.gg, or the referenced application. Automated input may violate game rules or affect an account. Review the current game/platform rules and use only where permitted. No anti-cheat bypass, process injection, memory reading, or game modification is included.

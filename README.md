# Diablo 2 LoD Glide/DDraw to OpenGL Wrapper (D2GL) - Median XL fork
[![Github All Releases](https://img.shields.io/github/downloads/azadix/d2gl-mxl-1.0/total.svg)](https://github.com/azadix/d2gl-mxl-1.0/releases) [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://github.com/azadix/d2gl-mxl-1.0/blob/master/LICENSE.md)

This repository is based on [d2gl-mxl-1.0](https://github.com/GavinK88/d2gl-mxl-1.0), the **Median XL** take on D2GL. It continues that work (HD text, motion prediction and UI/text alignment fixes) on top of the original [D2GL](https://github.com/bayaraa/d2gl) by Bayaraa.

D2GL is a Diablo 2 LoD Glide/DDraw to OpenGL wrapper that translates glide/ddraw calls to OpenGL calls. This makes D2 run better on modern hardware/OS and easier to switch between fullscreen and windowed modes, and resolves certain window and cursor-related issues. Additionally, it includes several libretro shaders for upscaling to higher resolutions, and allows for in-game text to be displayed in high-definition, D2DX's motion prediction, other QOL features.

### 🚩 Bug/Crash Report (***Important!***)

Please don't just say it crashes. I can't do anything without additional information!  
If you want to report a bug or crash please include details about your system (OS, hardware) and what you were doing when it happened.  
Also run game.exe with `-3dfx -log` it will create `d2gl.log` and attach this along with diablo2's log file like this `D2230410.txt`. It helps investigation.

## Features

- In-Game Option Menu(`Ctrl+O`) to change settings on fly.
- Upscale to higher resolution using RetroArch's slang shaders.
- Switch some parts in game to high-definition (all text, cursor etc.).
- Unlocked FPS (menu screen 45fps / ingame unlocked).
- Modified version of "D2DX's Unit/Weather Motion Predictor" feature (better with v-sync on).
- Few graphic post processing effects (Lut, sharpen, FXAA etc.).
- Fast switching windowed to fullscreen with `Alt+Enter` key.

## Requirements

- Median XL (Sigma) installation.
- Windows 7/8/10/11, Linux with Wine (Proton, Lutris etc), MacOS with Wine (Crossover).
- GPU with minimum OpenGL 3.3 support.

## Installation

### Installation on the new MXL Launcher
- Open the launcher and click the settings icon (gear icon, next to the big red button)
- Hover over the warning symbol next to "Unofficial Graphics Drivers", read and acknowledge this notice.
- Check/Enable Glide3x.dll if you wish to use Glide as your rendering backend or Ddraw.dll for DDraw
- Close the launcher
- Download from [Releases](https://github.com/azadix/d2gl-mxl-1.0/releases)
- Extract the files from the Release.zip into the folder MXL is using (this is C:\Games\median-xl by default if you did not specify during installation)
- Open the launcher and launch your game if everything is up to date.

## Configuration

- Everything except the `[Other]` section can be changed from the in-game option menu (`Ctrl+O`).
- Settings are stored in `d2gl.ini` next to the wrapper DLL and are written back by the menu.
- MXL relevant options are under the Features tab: HD Text, Motion Prediction, Mini Map and Show FPS. Motion Prediction smooths camera and unit movement; HD Text replaces the game fonts with high-definition ones.

## Compatibility

- Built for and tested with Median XL (Sigma).

## Known Issues

- **HD Text and panels:** with HD Text enabled, text that is drawn underneath a panel (character sheet, inventory) can show through it. The game draws those labels in the same pass after the panel, and HD Text is composited in a separate layer on top of the frame, so the panel cannot cover it. This affects labels such as ground item names and Median XL's training panel lines; disabling HD Text restores the normal order.

## Authors and history

- **Original D2GL** - Bayaraa ([bayaraa/d2gl](https://github.com/bayaraa/d2gl)). The wrapper, its renderer and most of the code in this repository are his work; every source file keeps his copyright header.
- **Median XL fork** - GavinK88 ([GavinK88/d2gl-mxl-1.0](https://github.com/GavinK88/d2gl-mxl-1.0)); this repository is derived from it.
- **Pre-1.0 MXL D2GL changes** - Pooquer.

This repository is a modified version of that work: it carries the Median XL specific fixes described above. The modification history is in the git log.

## Credits

Diablo II modding community (The Phrozen Keep) and **Everyone** who makes d2 mod and their source codes available.

- SGD2FreeRes D2 Custom Resolution (Mir Drualga).
- D2DX's Unit/Weather Motion Predictor (Bolrog).
- Libretro's slang shaders (RetroArch).
- The OpenGL Extension Wrangler Library (Brian Paul).
- OpenGL Mathematics (GLM) (G-Truc Creation).
- FXAA implementation by Timothy Lottes (NVIDIA).
- Dear ImGui (Omar Cornut).
- stb_image, stb_image_write (Sean Barrett).
- MurmurHash3 (Austin Appleby).
- MSDF Atlas Generator (Chlumsky).
- Shader Minifier (laurentlb).
- SPIRV-Cross, glslang (KhronosGroup).
- Detours (Microsoft).
- MXL pre-1.0 D2GL changes (Pooquer)

## Some Screenshots

![Screenshot010](https://user-images.githubusercontent.com/2043880/220664490-2a9b34d8-ca7c-4e52-a57d-d43c508f5813.png)
![Screenshot012](https://user-images.githubusercontent.com/2043880/220668775-3351be3b-27fa-4800-883f-09e5eb935c47.png)
![Screenshot013](https://user-images.githubusercontent.com/2043880/220666692-967a8c13-f480-4ac6-af1e-b45fda3bdee3.png)
![Screenshot002](https://user-images.githubusercontent.com/2043880/220667272-a83aa2cd-d038-41ea-a878-a6e148b8f9f6.png)

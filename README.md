# Saw II: Flesh & Blood — Recomp

Unofficial native PC port of **Saw II: Flesh & Blood** (Xbox 360) built with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (v0.10.0).

This is a **static recompilation**, not an emulator. The PowerPC code inside `Default.xex` is translated ahead of time to C++ and linked against the ReXGlue runtime (Xenos GPU, audio, input, filesystem). There is no JIT in the play path.

> **Bring your own dump.** This repository does **not** ship game files, ISOs, or XEXes. You need a legally owned Xbox 360 copy.

**Author:** Matteo Santoro

Title ID: `4B4E0822` · Engine: Unreal Engine 3 (`SAW2GAME/CookedXenon`)

---

## Status

Playable on Windows (RelWithDebInfo) with a WPF launcher for ISO install, language detection, and common runtime flags.

Still early: expect occasional missing PPC function hints (easy to patch), and a few graphics quirks (see [Known issues](#known-issues)). Achievements unlock and persist; the host toast UI is disabled because it was crashing this title.

See [PROGRESS.md](PROGRESS.md) for the bring-up log.

---

## Quick start (launcher)

1. Install the [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0) (and the .NET 8 SDK if you build from source).
2. Build the game once (below). CMake also publishes **`Saw2Launcher.exe` into the same folder as `saw2.exe`**.
3. Put [extract-xiso](https://github.com/XboxDev/extract-xiso) at `../tools/extract-xiso/artifacts/extract-xiso.exe` (sibling of this repo), and the ReXGlue SDK at `../tools/rexglue-sdk-bin/win-amd64`.
4. Run the launcher from the build output (or `dotnet run` from `launcher/` while developing):

```powershell
.\out\build\win-amd64-relwithdebinfo\Saw2Launcher.exe
```

In the launcher:

1. **SETUP / ISO** → pick your `.iso` → **EXTRACT & INSTALL**
2. **LANGUAGE** → only packs present on that disc are listed
3. **VIDEO / AUDIO / INPUT** → tune as you like
4. **PLAY**

Settings land in `%USERPROFILE%\Documents\saw2\launcher_settings.json`. Logs go to `Documents\saw2\saw2.log`.

Prefer **RelWithDebInfo** for play. Debug can assert in the Xenos path.

---

## Game data layout

After extract (gitignored — never commit this):

```
game/
  Default.xex
  SAW2GAME/
    CookedXenon/
    Movies/
    Xbox360TOC.txt
```

---

## Build from source

### Prerequisites

- Windows 10/11 x64
- CMake 3.25+, Ninja, Clang / VS 2022 clang-cl
- Vulkan-capable GPU + Vulkan SDK
- [ReXGlue SDK 0.10.0](https://github.com/rexglue/rexglue-sdk/releases) unpacked to `../tools/rexglue-sdk-bin/win-amd64`

### Codegen + compile

```powershell
$env:PATH = "..\tools\rexglue-sdk-bin\win-amd64\bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = "..\tools\rexglue-sdk-bin\win-amd64"

# Needs Default.xex under game/ (from your ISO)
rexglue codegen saw2_manifest.toml

cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH="..\tools\rexglue-sdk-bin\win-amd64"
cmake --build --preset win-amd64-relwithdebinfo
```

Binaries (same folder): `out/build/win-amd64-relwithdebinfo/saw2.exe` and `Saw2Launcher.exe`

If the game dies with `[FATAL] Call to invalid or unregistered function at 0x........`, add that address to `config/functions.toml`, re-run codegen, rebuild. That is the normal bring-up loop for ReXGlue titles.

---

## Launcher settings (what they map to)

| Panel | Runtime |
|-------|---------|
| VIDEO | resolution, window size, fullscreen, letterbox, V-Sync, FPS / guest refresh, VRR tearing, present effect (bilinear / CAS / FSR*) |
| AUDIO | mute, SDL queue depth |
| INPUT | sdl / xinput, mouse+keyboard, sensitivity |
| LANGUAGE | `--user_language` from packs on your ISO |
| ADVANCED | boot logo skip (2-frame flash of Konami/Zombie/Lionsgate/Twisted only), log level, extra CLI args |

\*FSR/CAS need a FidelityFX-enabled Xenos build of the SDK. Stock 0.10.0 binaries usually expose **bilinear** only. DLSS is not available on the Xenos path.

---

## Known issues

- **Black squares in some reflections** — likely a Xenos render-to-texture / cubemap resolve quirk (common UE3 + Xenos class). No GPU cvar flipped by default; investigating carefully so we do not regress playability.
- **Movie subtitle tracks** — disc ships unsuffixed Movies/*.txt; localized probes like lvla_p_ITA.txt are hardlinked at launch so cutscene captions resolve.
- **Missing `.xxx` probes** in the log (`Textures.xxx`, numeric hashes, etc.) are often optional UE3 companions — noisy, not always fatal.
- **Achievement toast** — host dialog disabled on purpose; unlocks still save under `Documents\saw2\achievements`.
- **Input race** — guest `XamInput*` calls are serialized (same heap-corruption race fixed in other ReXGlue ports when a pad is connected).

---

## Project layout

```
saw2-recomp/
  config/functions.toml   # manual PPC function boundaries
  src/                    # app hooks + game fixes
  launcher/               # WPF setup / play UI
  generated/              # codegen output (local; regenerated)
  game/                   # your dump (gitignored)
```

---

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — Tom Clay and contributors
- [Xenia](https://github.com/xenia-project/xenia) / [Xenia Canary](https://github.com/xenia-canary/xenia-canary) — foundations for the Xenos runtime
- [extract-xiso](https://github.com/XboxDev/extract-xiso)

Port work by **Matteo Santoro**.

Some bring-up steps (function-boundary hunting, launcher wiring, crash triage) used **AI assistance** as a tool alongside manual reverse-engineering and testing. The runtime, game dump handling, and final decisions stay human-owned.

---

## Legal

No copyrighted assets, disc images, or XEX binaries are redistributed here. Use only with a copy you own.  
Saw II: Flesh & Blood © Konami / Zombie Studios / respective rights holders. This project is unofficial and unaffiliated.
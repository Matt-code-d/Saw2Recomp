# Progress — Saw II: Flesh & Blood recomp

Port by **Matteo Santoro**. Runtime: ReXGlue **v0.10.0**.

## Status

| Area | State |
| --- | --- |
| Toolchain | Done — CMake + clang-cl, Vulkan, extract-xiso |
| ISO → `game/` | Done — Title ID `4B4E0822`, UE3 layout |
| Codegen | Done — ~47k functions; hints in `config/functions.toml` |
| Play (RelWithDebInfo) | Working — launcher + long sessions |
| Input heap race | Mitigated — mutex on XamInput wrappers |
| Achievement toast | Disabled (unlocks still persist) |
| Graphics polish | Open — some reflections show black squares |

## Bring-up loop

When you hit:

`[FATAL] Call to invalid or unregistered function at 0x........`

1. Add the address to `config/functions.toml`
2. `rexglue codegen saw2_manifest.toml --ignore-stamp`
3. Rebuild RelWithDebInfo
4. Relaunch

## Run

```powershell
# Launcher (recommended)
cd launcher
dotnet run -c Release

# Or direct
cd out\build\win-amd64-relwithdebinfo
.\saw2.exe --game_data_root "..\..\..\game" --gpu_plugin=xenos
```
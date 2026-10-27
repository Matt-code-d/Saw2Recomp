## v0.1.7 — Achievements toggle + safer HUD probe

Fresh Windows x64 package. **BYO ISO** — no game files inside.

### Changes
- Launcher Mods: enable/disable host achievements; safe text toast on unlock (no crashy stock dialog)
- HUD overlay marked **experimental**; stricter guest health bind (prefer full HP / damage, rescan frozen false binds)
- HUD probe stays physical-membase-only (avoids guest AV from TranslateVirtual scans)

### What's included
- Saw2Launcher.exe
- saw2.exe + RelWithDebInfo runtime DLLs
- HUD textures (`hpbar.png`, `hpbarline.png`)
- extract-xiso.exe
- README.txt / BUILD.txt

### You need
- Own Saw II Xbox 360 ISO (`4B4E0822`)
- .NET 8 Desktop Runtime
- Vulkan-capable GPU (D3D12 present path)

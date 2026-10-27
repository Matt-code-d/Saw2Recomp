## v0.1.5 — Dual resolution + HP HUD + launcher polish

Fresh Windows x64 package. **BYO ISO** — no game files inside.

### Changes
- Dual resolution: separate internal render size vs window/output size (FSR/CAS friendly)
- Optional in-game HP bar overlay (`hpbar` / `hpbarline`) — gameplay only, fade-in
- Launcher reorganized into clearer nav cards (Setup / Video / Audio / Input / Mods / Advanced)
- Mods panel for experimental overlays; HUD demo removed from launcher
- Launcher branding refresh (logoLaunch + Billy corner art)

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

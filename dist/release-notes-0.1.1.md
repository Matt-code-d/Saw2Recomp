## v0.1.1 — Logo branding + RelWithDebInfo (fixed)

Fresh Windows x64 package. **BYO ISO** — no game files inside.

### Fix vs first 0.1.1 upload
Earlier build accidentally shipped **Debug** GPU runtime (`rexgpu-xenosd.dll`) and triggered assert storms. This republish is **RelWithDebInfo only** (`rexruntimerd.dll` / `rexgpu-xenosrd.dll`). Check `BUILD.txt` inside the zip.

### What's included
- Saw2Launcher.exe (logo sidebar)
- saw2.exe + RelWithDebInfo runtime DLLs
- extract-xiso.exe
- README.txt / BUILD.txt

### You need
- Own Saw II Xbox 360 ISO (`4B4E0822`)
- .NET 8 Desktop Runtime
- Vulkan GPU

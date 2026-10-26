## v0.1.3 — Water puddles + safer play path

Fresh Windows x64 package. **BYO ISO** — no game files inside.

### Changes
- Black water-puddle / reflection squares mitigated (D3D12 `rov` + invalid fetch constants; present FSR/CAS/bilinear still work)
- Launcher refuses Debug `saw2.exe` / strips `rexgpu-xenosd.dll` (assert storms)
- Lean GPU defaults near water (no hitchy `clear_memory_page_state` / forced readback stack)
- RelWithDebInfo only (`rexruntimerd.dll` / `rexgpu-xenosrd.dll`)

### What's included
- Saw2Launcher.exe
- saw2.exe + RelWithDebInfo runtime DLLs
- extract-xiso.exe
- README.txt / BUILD.txt

### You need
- Own Saw II Xbox 360 ISO (`4B4E0822`)
- .NET 8 Desktop Runtime
- Vulkan-capable GPU (D3D12 present path)

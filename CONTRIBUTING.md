# Contributing to Saw2Recomp

Thanks for helping. This is an unofficial Xbox 360 → PC recomp of **Saw II: Flesh & Blood** on [ReXGlue](https://github.com/rexglue/rexglue-sdk). Keep PRs focused and legal.

## Ground rules

- **Bring your own dump.** Never commit ISOs, `Default.xex`, cooked assets, audio, video, or anything from the disc. `game/` is gitignored for a reason. This project is designed **not** to ship or share game data.
- **No reuploads.** Do not post dumps, mirrors, or links to game files. The maintainer does not authorize or endorse redistributing game content online; that is entirely on whoever does it (see [LICENSE](LICENSE) — “No game data; no reuploads”).
- **License:** original work here is [MIT](LICENSE) (keep attribution). The game and third-party SDKs/tools are **not** covered by that MIT grant.
- **No Cursor / agent branding** in commits, PR titles, or docs. Commit as yourself.
- Prefer **RelWithDebInfo** for play/testing. Do not ship Debug GPU runtimes (`rexgpu-xenosd.dll`) — they assert-storm.

## What helps most

| Area | Examples |
| --- | --- |
| Missing PPC functions | Addresses from `[FATAL] Call to invalid or unregistered function at 0x…` → `config/functions.toml` |
| Game fixes / hooks | Small, targeted changes in `src/` |
| Mods & gameplay tweaks | QoL, cheats-as-mods, online co-op, balance experiments — see below |
| Launcher | WPF setup/settings UX, install/language edge cases (`launcher/`) |
| Docs | README / PROGRESS accuracy, known issues |
| Graphics / GPU cvars | Evidence-based flags; note hitch tradeoffs (especially near water) |

Large refactors, new UI frameworks, or Linux game ports need a discussion first (open an issue).

## Mods / gameplay hacks

Mods are welcome. Same for “hacks” in the *fun* sense — more health, FOV, QoL toggles, silly cheats, online co-op, whatever makes *your* copy more fun to play.

That does **not** mean cracking, piracy, or uploading the game. If it needs disc files in the repo or a download link to an ISO, it’s a no.

Ideally put new toys behind a setting (off by default), write a short PR note on what it does, and still don’t commit anything from the dump.

## Dev setup (short)

1. Own ISO → extract into `game/` (launcher **SETUP** or extract-xiso).
2. ReXGlue SDK 0.10.0 under `../tools/rexglue-sdk-bin/win-amd64` (see README).
3. CMake + Ninja + clang-cl (VS Build Tools), Vulkan SDK, .NET 8 for the launcher.
4. Build RelWithDebInfo; run `Saw2Launcher.exe` from the build folder.

## Missing-function loop

When the log shows an unregistered guest address:

1. Add it to `config/functions.toml`
2. `rexglue codegen saw2_manifest.toml --ignore-stamp`
3. Rebuild RelWithDebInfo
4. Reproduce and confirm

Details: [PROGRESS.md](PROGRESS.md).

## Pull requests

- One concern per PR; describe **why** and how you tested.
- Do not commit `generated/` churn unless the PR is specifically about codegen and you explain it.
- Do not commit `.cursor/`, release zips, or local scratch (`dist/_peek/`, `_*.bat`, etc.).
- Keep the MIT copyright / notice when redistributing substantial portions.

## Issues

Include: OS/GPU, RelWithDebInfo vs Debug, launcher settings that matter (FPS, present effect), and a short log slice from `Documents\saw2\saw2.log` (no personal paths to dumps if you can avoid them).

## Code of conduct (minimal)

Be respectful. No harassment, no sharing illegal game files, no drive-by dumps of proprietary binaries in the tracker.

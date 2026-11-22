---
description: Launch the live shader edit→hot-reload→profile loop and start an optimization session
argument-hint: "[pass/zone to focus, e.g. lighting | dlss | culling] (optional)"
---

Start a shader optimization session using the live edit→hot-reload→profile pipeline.
Read @shaderStatsWorkflow.md first — it has the full CSV schema, the reloadId/secondsSinceReload
handshake, and the gotchas. Then:

1. **Build (Release) if the engine sources changed or no fresh exe exists.** Build the SOLUTION,
   not the .vcxproj (OutDir gotcha):
   `"C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe" SeeD.sln -m -p:Configuration=Release -p:Platform=x64`

2. **Launch the engine if it isn't already running.** Check for an existing SeeD.exe process first —
   don't double-launch. Launch in the background with working directory `SeeD\` and the Cave test
   project as argument:
   `Start-Process x64\Release\SeeD.exe -ArgumentList '"G:\Work\CaveSeedProj\Cave.seedproj"' -WorkingDirectory SeeD`
   The Cave project is the meaningful profiling scene — without the argument the engine loads
   `Save.seed`, which renders almost nothing (µs-level pass timings, pure noise). The engine writes
   `shaderStats.csv` at the repo root every ~0.5 s (`options.shaderStatsCsv`).

3. **All polling goes through `.\shaderStatsWait.ps1`, all backup/restore through
   `.\shaderBackup.ps1` (both repo root) — never write ad-hoc polling loops or Copy-Item
   commands.** A PreToolUse hook in `.claude/settings.local.json` auto-allows any clean
   invocation of these two scripts, so the user is never prompted for them:
   - startup / CSV-live check: `.\shaderStatsWait.ps1 -SettleSeconds 5 -TimeoutSec 120`
   - after each edit: `.\shaderStatsWait.ps1 -PassFilter '<file or file,pass regex>' -MinReloadId <N+1> -SettleSeconds 4 -Zones <zone>`
   - backup / restore: `.\shaderBackup.ps1 -File <name>.hlsl` / `.\shaderBackup.ps1 -File <name>.hlsl -Restore`
   Exit 0 = OK (rows + timings printed), exit 2 = COMPILE_ERROR (error row printed), exit 1 = timeout.
   The hook only matches the script as the SOLE command with no `; & | < >` anywhere in the
   command — so no chained suffixes, and no `|` regex alternation in -PassFilter (use a comma
   or file-level filter instead), or the user gets prompted. The scripts print everything you
   need; don't echo exit codes.

4. **Summarize the baseline**: the `#PASSES` GPU timings (the real signal) and the `#SHADERS`
   compile stats. Flag the hottest passes.

5. **Pick a target.** If `$ARGUMENTS` names a pass or zone, focus there; otherwise recommend the
   hottest pass and ask before diving in.

6. **Run the loop** for each change:
   - **Thermal fairness:** every script reading ends with a `gpu,<sm clock>, <temp C>, <power>`
     line — only compare timings whose clocks match (within ~2%). A freshly launched engine runs
     at cold-boost clocks and looks 5-10% faster than heat-soaked; after a rebuild+relaunch, let
     the GPU warm up several minutes (watch the gpu line stabilize) before comparing against a
     warm baseline, and never claim a win from a seconds-old process. Prefer hot-reload A/B
     (same process = same thermal state) over restart A/B whenever possible.
   - Back up the `.hlsl` first: `.\shaderBackup.ps1 -File <name>.hlsl` (byte-exact, reversible).
   - Edit + save, then wait with `.\shaderStatsWait.ps1` as in step 3.
   - On `COMPILE_ERROR`: read the error, fix, retry.
   - On `OK`: compare the settled `#PASSES` timing to the pre-edit baseline.
   - Keep the change only if it's a real win; otherwise restore from backup.
   - **Report every iteration immediately after its measurement** — what was changed and why, the
     instruction/DXIL delta, the GPU-timing delta (avg/min/max vs baseline), and the keep-or-revert
     verdict. Do not batch iteration results into one final report; the end-of-session summary is
     only a recap table of already-reported iterations.

Don't commit anything unless I ask. When I'm done, stop the engine process.

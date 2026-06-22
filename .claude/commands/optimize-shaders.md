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

2. **Launch the engine if it isn't already running.** Run `x64\Release\SeeD.exe` in the background
   with working directory `SeeD\`. Don't double-launch — check for an existing SeeD.exe process
   first. It loads `Save.seed` and writes `shaderStats.csv` at the repo root every ~0.5 s
   (controlled by `options.shaderStatsCsv`).

3. **Confirm the CSV is live**, then read it and summarize: the `#PASSES` GPU timings (the real
   signal) and the `#SHADERS` compile stats. Flag the hottest passes.

4. **Pick a target.** If `$ARGUMENTS` names a pass or zone, focus there; otherwise recommend the
   hottest pass and ask before diving in.

5. **Run the loop** for each change:
   - Back up the `.hlsl` (so a bad experiment is reversible byte-exact).
   - Edit + save. Poll `shaderStats.csv` until that pass's `reloadId` increments.
   - On `COMPILE_ERROR`: read the `error` column, fix, retry.
   - On `OK`: wait until `secondsSinceReload >= 3-4`, then read the `#PASSES` timing and compare
     to the pre-edit baseline. Report the delta (avg/min/max ms).
   - Keep the change only if it's a real win; otherwise restore from backup.

Don't commit anything unless I ask. Report timing deltas each iteration. When I'm done, stop the
engine process.

# Shader stats / live edit-profile workflow

A live **edit → hot-reload → profile** loop for optimizing the HLSL shaders. The running
engine recompiles a shader the moment its file changes and dumps per-pass compile stats +
real GPU timings to a CSV, so an external loop (a human or an agent) can edit a shader,
wait for a fresh result, and read the effect — without rebuilding or relaunching.

## How to run it

1. Build (must build the **solution**, not the .vcxproj — see Gotchas):
   ```
   "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe" \
       SeeD.sln -m -p:Configuration=Release -p:Platform=x64
   ```
2. Launch the engine (it loads `Save.seed` and keeps running):
   - exe:        `x64\Release\SeeD.exe`  (repo-root output tree)
   - working dir: `SeeD\`  (so `src\Shaders\` and `..\assetLibrary.txt` resolve)
3. It writes **`shaderStats.csv`** at the repo root every ~0.5 s. Toggle with
   `options.shaderStatsCsv` (Main.cpp). Release is the meaningful config — Debug compiles
   shaders with `-Od`, so its stats/timings are misleading.

## The optimize loop (and how it knows a fresh result is ready)

The CSV carries a handshake so the loop never reads a stale result:

- `reloadId` — per shader pass, **incremented only when that pass is (re)compiled**.
- `status`   — `OK` or `COMPILE_ERROR` (+ `error` = the DXC message, flattened to one line).
- `secondsSinceReload` (top line) — resets to 0 on any recompile, then climbs. The GPU
  timing window is also reset on reload, so timings reflect the NEW shader once this is
  past ~3–4 s.

Protocol:
1. Read pass P's current `reloadId = N`.
2. Edit + save the `.hlsl` (mtime change triggers the engine's hot reload).
3. Poll the CSV (~0.5 s) until P's `reloadId >= N+1`:
   - `COMPILE_ERROR` → read `error`, fix, back to step 2 (instant; no need to wait on timing).
   - `OK` → wait until `secondsSinceReload >= 3–4`, then read the settled timing/stats.
4. Compare to the pre-edit baseline; decide the next edit.

Editing a shared include (e.g. `common.hlsl`) fans out: every dependent pass recompiles and
bumps its own `reloadId`. A broken edit bumps `reloadId` once and stays in `COMPILE_ERROR`
(no recompile storm); fixing the file recompiles once back to `OK`.

## CSV format

```
writeSeq,<n>,secondsSinceReload,<seconds>
#SHADERS
file,pass,entry,stage,reloadId,status,instr_total,instr_float,instr_int,instr_tex,temp_regs,tg_x,tg_y,tg_z,dxil_bytes,error
...one row per compiled stage (a pass like gBuffer has both ms_6_6 + ps_6_6 rows)...
#PASSES
zone,queue,gpu_ms_avg,gpu_ms_min,gpu_ms_max
...one row per render-pass GPU timing zone...
```

- **#SHADERS** is keyed by *shader pass* (`file|pass`); **#PASSES** is keyed by *render-pass
  zone name* (the `Pass::On(...,"name",...)` names in Renderer.h, e.g. `culling`, `gBuffers`,
  `lighting`, `forward`). A render pass can dispatch several shaders (e.g. all of
  `culling.hlsl`'s passes run under the `culling` zone), so join the two sections by knowing
  which zone a shader belongs to.
- `temp_regs` is always 0 — DXC's `TempRegisterCount` is a legacy DXBC field not populated for
  DXIL. The useful static signals are the instruction counts + `dxil_bytes`; the real
  perf signal is the `#PASSES` GPU timings.
- Raytracing libs (`lib_6_6`) report 0 instruction counts (needs library reflection, not
  shader reflection); their cost shows up in the RT zones' timings instead.

## Where it lives in the code

- `ShaderStatsCollector` (Loading.h) — collects per-pass compile results + `WriteCsv()`.
- `ShaderLoader::Compile` / `ShaderReflection` (Loading.h) — feed stats/errors per stage.
- `Profiler::ResetSamples()` (GPU.h) — clears the timing ring on reload.
- `Engine::Loop` (Main.cpp) — detects reloads, resets timings, throttled CSV write.
- Reload-storm fix: `AssetLibrary::LoadAsset` shader failure branch (Loading.h) records the
  edited file's timestamp on the last-good shader so a broken save doesn't recompile every frame.

## Gotchas

- **Build the .sln, not the .vcxproj.** There's no explicit `OutDir`, so the solution build
  uses `$(SolutionDir)` → exe at repo-root `x64\Release\` next to the full DLL set (dxcompiler,
  assimp, dlss, …, copied there by the post-build event). Building the .vcxproj directly sends
  the exe to `SeeD\x64\Release\` where those DLLs are absent → `0xC0000135` (DLL not found).
- Run with working dir `SeeD\`. The Agility SDK `D3D12SDKPath=".\D3D12\"` and the shader/asset
  relative paths both depend on it.

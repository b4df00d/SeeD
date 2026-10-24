# Rendering calibration

Scores SeeD's render against a Blender Cycles reference of the same scene, so
lighting changes can be checked against something trustworthy instead of by eye.

The scene is authored and rendered in Blender, exported to glTF, imported into
SeeD, captured as linear HDR, and compared per object.

## Unit contract

**SeeD uses Blender's convention, so the expected ratio is 1.0.**

| | Blender | SeeD |
|---|---|---|
| Sun | Strength = irradiance W/m² | `color.w` = W/m², used directly |
| Point | Strength = radiant power W | `color.w` = W, irradiance `P/(4π·d²)` |

glTF is exported in `SPEC` mode, and the importer converts back with the two
constants Blender's own importer uses: sun `lux/683`, point `cd/683·4π`.

## Running it

Score the current build against the existing reference:

```powershell
.\SeeD\tools\calibration\run.ps1 calibration\calibration.seedproj
```

That runs SeeD (the project's `[calibration]` section makes it render each entry
and exit by itself), then `compare.py`. Add `-SkipCapture` to re-score images
already in `out\` without re-rendering.

The project holds two scenes, each with a `direct` and a `full` entry:

- `falloff`: spheres on a ground plane under Blender's default sky and sun,
  plus a point light. `full` is dominated by the sky.
- `cornell`: a room-sized Cornell box lit only by a 1600 lm bulb (2.34 W),
  black world. `full` is dominated by interreflection, at indoor light levels
  about 1000x darker than the falloff scene.

Each `full` entry also writes `out\<name>_display.exr` (SeeD's tonemapped
frame), compared against `reference\<scene>\full_agx.exr` (Blender's AgX view).
That comparison is reported in 8-bit codes and never fails the run.

Needs `python` on PATH. numpy is used if present but is not required.

## Re-rendering the reference

Only when the `.blend` changes — it is a manual step, and re-running it *is* the
workflow (there is no freshness tracking):

```powershell
blender -b calibration\falloff.blend -P SeeD\tools\calibration\render_reference.py -- calibration
blender -b calibration\cornell.blend -P SeeD\tools\calibration\render_reference.py -- calibration
```

Each writes `reference\<scene>\{direct,full,full_agx,ids}.exr` and
`objects.txt` (`<scene>` is the `.blend`'s name), and re-exports
`Source\<scene>\<scene>.gltf`. `--skip-render` redoes only the glTF export.

To rebuild a scene from scratch (overwrites its `.blend`):

```powershell
blender -b -P SeeD\tools\calibration\make_falloff_scene.py
blender -b -P SeeD\tools\calibration\make_cornell_scene.py
```

## Reading the output

Per entry you get `out\<name>_report.csv` and `out\<name>_compare.png`
(Blender | SeeD | log2-ratio false colour, red = SeeD brighter, blue = darker),
plus a per-object table and two scores:

- **absolute** — how far off the numbers are, including any overall gain error.
- **shape** — the same after dividing out the single best-fit scale.

A large absolute with a small shape score means *one global factor* is wrong
(light units, a stray multiplier) and everything else agrees. Both large means
the disagreement varies per object, which is a BRDF or falloff problem. The
false-colour panel tells them apart at a glance: a flat tint is a gain error,
structure is not.

## Adding a scene

1. Build a `.blend` (copy `make_falloff_scene.py` as a starting point). Give
   every mesh a distinct `pass_index` — that is what becomes the object mask.
2. Run `render_reference.py` against it.
3. Add `capture` lines to the project's `[calibration]` section:
   `capture <name> <scene> <output> <directOnly> <warmupFrames>`.
   The reference is found through the scene: `reference\<scene>\`, where
   `<scene>` is the glTF's file name, then `direct.exr` for a directOnly entry
   and `full.exr` otherwise. Names must be unique across the project
   (`<scene>_direct`, `<scene>_full`).

Keep the project's `windowWidth`/`windowHeight` equal to the `.blend`'s render
resolution; `compare.py` refuses to score a size mismatch rather than
silently resampling.

## Why the project turns the player off

`playerSystem 0`. Without the free-fly camera the scene's imported camera is the
only one in the world, so nothing has to arbitrate which camera renders — the
renderer draws through the first one the query returns.

## Notes

- EXRs are written uncompressed on both sides, which is what lets `exrio.py`
  read them with no decompressor.
- The capture is taken before tonemapping, so the comparison is of light
  transport, with no display transform in the way.
- `calibration.log`, next to the project file, is the record of what a capture
  actually did — which resource it grabbed, the render vs display resolution
  (proof DLSS engaged), and the camera count.

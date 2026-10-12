# Renders the Cycles reference images and exports the glTF that SeeD imports.
#
#   blender -b calibration/falloff.blend -P render_reference.py -- <project dir or .seedproj>
#
# Run by hand whenever the .blend changes. There is no freshness tracking --
# re-running it IS the workflow.
#
# Produces, under the project directory:
#   reference/direct.exr   sun + point, no bounces, black world
#   reference/full.exr     sun + point, 10 bounces, a copy of SeeD's Sky()
#   reference/ids.exr      object index per pixel, for per-object masks
#   reference/objects.txt  index -> name, albedo
#   Source/<scene>/<scene>.gltf (+ .bin)
import bpy, sys, os, json, math

WATTS_TO_LUMENS = 683.0   # PBR_WATTS_TO_LUMENS, io_scene_gltf2/blender/com/conversion.py
SAMPLES = 2048
ID_SAMPLES = 1
FULL_BOUNCES = 10     # matches BOUNCES_MAX in raytracing2.hlsl

# SeeD's Sky(), common.hlsl:1292. SeeD is Y-up and Blender is Z-up, so its
# dot(direction, up) becomes the Blender direction's z.
SKY_HORIZON = (1.0, 0.66, 0.66)
SKY_ZENITH = (0.33, 0.5, 1.0)
SKY_SCALE = 5.0
SKY_DOWN_SHARPNESS = 20.0

BOUNCE_ATTRS = ('max_bounces', 'diffuse_bounces', 'glossy_bounces', 'transmission_bounces')


def log(*a):
    print("CALIB:", *a)


def project_dir(arg):
    p = os.path.abspath(arg)
    if os.path.isfile(p) or p.lower().endswith(".seedproj"):
        return os.path.dirname(p)
    return p


def use_gpu(sc):
    """Prefer OptiX, then CUDA, else stay on CPU."""
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        for kind in ('OPTIX', 'CUDA'):
            try:
                prefs.compute_device_type = kind
            except TypeError:
                continue
            prefs.refresh_devices()
            picked = [d for d in prefs.devices if d.type == kind]
            if not picked:
                continue
            for d in prefs.devices:
                d.use = (d.type == kind)
            sc.cycles.device = 'GPU'
            log("cycles device: %s (%s)" % (kind, ", ".join(d.name for d in picked)))
            return
    except Exception as e:
        log("GPU setup failed (%r), falling back to CPU" % (e,))
    sc.cycles.device = 'CPU'
    log("cycles device: CPU")


def reference_settings(sc):
    sc.render.engine = 'CYCLES'
    cy = sc.cycles
    cy.samples = SAMPLES
    cy.use_adaptive_sampling = False     # uniform noise, no threshold bias
    cy.use_denoising = False
    cy.blur_glossy = 0.0                 # the 1.0 default blurs caustics, biased
    cy.sample_clamp_direct = 0.0
    cy.sample_clamp_indirect = 0.0       # the 10.0 default clips bright indirect
    cy.seed = 0
    for attr in BOUNCE_ATTRS:
        setattr(cy, attr, FULL_BOUNCES)

    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'   # EXR stays scene-linear anyway
    sc.view_settings.look = 'None'
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0

    ims = sc.render.image_settings
    ims.file_format = 'OPEN_EXR'
    ims.color_mode = 'RGBA'
    ims.color_depth = '32'
    ims.exr_codec = 'NONE'               # so exrio.py needs no decompressor


def set_bounces(sc, n):
    for attr in BOUNCE_ATTRS:
        setattr(sc.cycles, attr, n)


def black_world():
    w = bpy.data.worlds.new("CalibBlack")
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.0, 0.0, 0.0, 1.0)
    bg.inputs[1].default_value = 0.0
    return w


def seed_sky_world():
    """Rebuild SeeD's analytic Sky() as world nodes.

      dotUp   = sqrt(saturate(dir.z))
      dotDown = saturate(-dir.z * 20)
      sky     = normalize(lerp(horizon, zenith, dotUp)) * (1 - dotDown) * 5

    Texture Coordinate > Generated is the ray direction in a world shader;
    Geometry > Incoming is its negative. Verified with a probe render, not
    assumed. Vector sockets are indexed, not looked up by name, because
    ShaderNodeVectorMath has three sockets all called "Vector" (Scale is 3).
    """
    w = bpy.data.worlds.new("CalibSky")
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes["Background"]
    bg.inputs[1].default_value = 1.0

    coord = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Generated"], sep.inputs["Vector"])

    up = nt.nodes.new("ShaderNodeMath")          # max(z, 0)
    up.operation = 'MAXIMUM'
    up.inputs[1].default_value = 0.0
    nt.links.new(sep.outputs["Z"], up.inputs[0])

    dot_up = nt.nodes.new("ShaderNodeMath")      # pow(x, 0.5)
    dot_up.operation = 'POWER'
    dot_up.inputs[1].default_value = 0.5
    nt.links.new(up.outputs[0], dot_up.inputs[0])

    dot_down = nt.nodes.new("ShaderNodeMath")    # saturate(z * -20)
    dot_down.operation = 'MULTIPLY'
    dot_down.inputs[1].default_value = -SKY_DOWN_SHARPNESS
    dot_down.use_clamp = True
    nt.links.new(sep.outputs["Z"], dot_down.inputs[0])

    # lerp(horizon, zenith, dotUp) == horizon + (zenith - horizon) * dotUp
    delta = nt.nodes.new("ShaderNodeVectorMath")
    delta.operation = 'SUBTRACT'
    delta.inputs[0].default_value = SKY_ZENITH
    delta.inputs[1].default_value = SKY_HORIZON

    scaled = nt.nodes.new("ShaderNodeVectorMath")
    scaled.operation = 'SCALE'
    nt.links.new(delta.outputs["Vector"], scaled.inputs[0])
    nt.links.new(dot_up.outputs[0], scaled.inputs[3])

    base = nt.nodes.new("ShaderNodeVectorMath")
    base.operation = 'ADD'
    nt.links.new(scaled.outputs["Vector"], base.inputs[0])
    base.inputs[1].default_value = SKY_HORIZON

    norm = nt.nodes.new("ShaderNodeVectorMath")
    norm.operation = 'NORMALIZE'
    nt.links.new(base.outputs["Vector"], norm.inputs[0])

    keep = nt.nodes.new("ShaderNodeMath")        # 1 - dotDown
    keep.operation = 'SUBTRACT'
    keep.inputs[0].default_value = 1.0
    nt.links.new(dot_down.outputs[0], keep.inputs[1])

    gain = nt.nodes.new("ShaderNodeMath")        # * 5
    gain.operation = 'MULTIPLY'
    gain.inputs[1].default_value = SKY_SCALE
    nt.links.new(keep.outputs[0], gain.inputs[0])

    final = nt.nodes.new("ShaderNodeVectorMath")
    final.operation = 'SCALE'
    nt.links.new(norm.outputs["Vector"], final.inputs[0])
    nt.links.new(gain.outputs[0], final.inputs[3])
    nt.links.new(final.outputs["Vector"], bg.inputs["Color"])
    return w


def id_material():
    mat = bpy.data.materials.new("CalibObjectIndex")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
    emis = nt.nodes.new("ShaderNodeEmission")
    info = nt.nodes.new("ShaderNodeObjectInfo")
    emis.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    nt.links.new(info.outputs["Object Index"], emis.inputs["Strength"])
    nt.links.new(emis.outputs["Emission"], out.inputs["Surface"])
    return mat


def base_color(obj):
    for slot in obj.material_slots:
        m = slot.material
        if not m or not m.use_nodes:
            continue
        b = m.node_tree.nodes.get("Principled BSDF")
        if b:
            c = b.inputs["Base Color"].default_value
            return (c[0], c[1], c[2])
    return (0.0, 0.0, 0.0)


def render_to(sc, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Blender appends the extension itself, so hand it the stem.
    stem = path[:-4] if path.lower().endswith(".exr") else path
    sc.render.filepath = stem
    sc.render.use_file_extension = True
    bpy.ops.render.render(write_still=True)
    log("wrote", path)


def tag_lights(sc):
    """glTF cannot carry a light's physical size, so smuggle it through node
    extras on the light OBJECT -- assimp keeps node extras but drops light ones."""
    lights = []
    for obj in sc.objects:
        if obj.type != 'LIGHT':
            continue
        d = obj.data
        if d.type == 'SUN':
            obj["seed_angular_radius"] = float(d.angle) * 0.5
            obj.pop("seed_radius", None)
            lights.append("%s SUN energy=%.4f W/m^2 angular_radius=%.6f rad"
                          % (obj.name, d.energy, d.angle * 0.5))
        else:
            obj["seed_radius"] = float(d.shadow_soft_size)
            obj.pop("seed_angular_radius", None)
            lights.append("%s %s energy=%.4f W radius=%.4f m"
                          % (obj.name, d.type, d.energy, d.shadow_soft_size))
    return lights


def expected_intensity(light):
    """The KHR_lights_punctual intensity for a Cycles light, SPEC convention:
    sun lux = energy * 683, point/spot cd = energy / (4*pi) * 683."""
    if light.type == 'SUN':
        return light.energy * WATTS_TO_LUMENS, "lux"
    return light.energy / (4.0 * math.pi) * WATTS_TO_LUMENS, "cd"


def fix_light_intensities(gltf_path, sc):
    """Rewrite exported light intensities from light.energy.

    Blender's exporter takes a sun's strength from its Emission node rather
    than from energy (exp/lights.py:219 tests node_tree, not use_nodes, and
    Cycles always creates a node tree), while Cycles renders energy * node
    strength. With the default node strength of 1.0 that makes every sun
    export as 683 lux no matter how bright it actually is -- the reference
    image and the glTF would disagree silently. Point lights happen to escape
    this because the exporter falls back to energy when it finds no
    LightFalloff node, so for them this is a no-op.

    Patching the JSON keeps the unit contract ours and independent of exporter
    quirks; verify_export then confirms the file on disk.
    """
    with open(gltf_path) as f:
        doc = json.load(f)
    lights = doc.get("extensions", {}).get("KHR_lights_punctual", {}).get("lights", [])
    by_name = {}
    for obj in sc.objects:
        if obj.type == 'LIGHT':
            by_name[obj.name] = obj.data
    changed = []
    for entry in lights:
        data = by_name.get(entry.get("name"))
        if data is None:
            continue
        want, unit = expected_intensity(data)
        had = entry.get("intensity", 0.0)
        if abs(had - want) > max(1e-4, abs(want) * 1e-4):
            entry["intensity"] = want
            changed.append("%s %.4f -> %.4f %s" % (entry.get("name"), had, want, unit))
    if changed:
        with open(gltf_path, "w") as f:
            json.dump(doc, f, indent=4)
        for c in changed:
            log("rewrote exported intensity:", c)


def verify_export(gltf_path, sc):
    """Fail loudly if the exported intensities disagree with what Cycles renders.

    Cycles lights emit according to light.energy, but the glTF exporter prefers
    a light's Emission node strength when the light uses nodes -- so a sun can
    render at 5 W/m^2 and export as 1. That desync is invisible in both files
    and would quietly poison every number the harness produces, so it is
    checked rather than trusted. In SPEC mode the exporter writes
    sun lux = energy * 683 and point cd = energy / (4*pi) * 683.
    """
    with open(gltf_path) as f:
        doc = json.load(f)
    exported = {l.get("name"): l.get("intensity", 0.0)
                for l in doc.get("extensions", {})
                             .get("KHR_lights_punctual", {})
                             .get("lights", [])}
    problems = []
    for obj in sc.objects:
        if obj.type != 'LIGHT':
            continue
        d = obj.data
        expect, unit = expected_intensity(d)
        got = exported.get(obj.name)
        if got is None:
            problems.append("%s is missing from the exported glTF" % obj.name)
            continue
        log("light %s: energy %.4f -> %.4f %s exported" % (obj.name, d.energy, got, unit))
        if abs(got - expect) > max(1e-4, abs(expect) * 1e-4):
            problems.append(
                "%s exported %.4f %s but energy %.4f implies %.4f %s "
                "(is light.use_nodes on?)" % (obj.name, got, unit, d.energy, expect, unit))
    if problems:
        raise SystemExit("CALIB: export does not match the render:\n  " + "\n  ".join(problems))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        raise SystemExit(
            "usage: blender -b <scene.blend> -P render_reference.py -- <project dir or .seedproj>")
    skip_render = "--skip-render" in argv
    argv = [a for a in argv if not a.startswith("--")]
    proj = project_dir(argv[0])
    scene_name = os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "scene"
    ref = os.path.join(proj, "reference")
    src = os.path.join(proj, "Source", scene_name)
    os.makedirs(ref, exist_ok=True)
    os.makedirs(src, exist_ok=True)

    sc = bpy.context.scene
    reference_settings(sc)
    use_gpu(sc)
    log("project %s" % proj)
    log("resolution %dx%d" % (sc.render.resolution_x, sc.render.resolution_y))

    lights = tag_lights(sc)
    for l in lights:
        log("light:", l)

    gltf = os.path.join(src, scene_name + ".gltf")
    bpy.ops.export_scene.gltf(
        filepath=gltf,
        export_format='GLTF_SEPARATE',
        use_selection=False,
        export_apply=True,
        export_yup=True,
        export_cameras=True,
        export_lights=True,
        export_extras=True,
        export_import_convert_lighting_mode='SPEC',
    )
    log("wrote", gltf)
    fix_light_intensities(gltf, sc)
    verify_export(gltf, sc)

    meshes = [o for o in sc.objects if o.type == 'MESH']
    objects_txt = os.path.join(ref, "objects.txt")
    with open(objects_txt, "w") as f:
        f.write("# index name albedo_r albedo_g albedo_b\n")
        f.write("# resolution %d %d\n" % (sc.render.resolution_x, sc.render.resolution_y))
        for l in lights:
            f.write("# light %s\n" % l)
        for o in sorted(meshes, key=lambda o: o.pass_index):
            c = base_color(o)
            f.write("%d %s %.6f %.6f %.6f\n" % (o.pass_index, o.name, c[0], c[1], c[2]))
    log("wrote", objects_txt)

    if skip_render:
        log("--skip-render given; glTF and objects.txt only")
        return

    original_world = sc.world

    # direct: no bounces, black world
    sc.world = black_world()
    set_bounces(sc, 0)
    sc.cycles.samples = SAMPLES
    render_to(sc, os.path.join(ref, "direct.exr"))

    # full: bounces plus SeeD's sky
    sc.world = seed_sky_world()
    set_bounces(sc, FULL_BOUNCES)
    render_to(sc, os.path.join(ref, "full.exr"))

    # ids: flat object index, centre-sampled so the values stay integral
    vl = bpy.context.view_layer
    sc.world = black_world()
    vl.material_override = id_material()
    sc.cycles.samples = ID_SAMPLES
    set_bounces(sc, 0)
    filt, width = sc.cycles.pixel_filter_type, sc.cycles.filter_width
    sc.cycles.pixel_filter_type = 'BOX'
    sc.cycles.filter_width = 0.01
    render_to(sc, os.path.join(ref, "ids.exr"))
    vl.material_override = None
    sc.cycles.pixel_filter_type, sc.cycles.filter_width = filt, width
    sc.world = original_world

    log("done")


main()

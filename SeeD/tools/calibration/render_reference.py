# Renders the Cycles reference images and exports the glTF that SeeD imports.
#
#   blender -b calibration/falloff.blend -P render_reference.py -- <project dir or .seedproj>
#
# Run by hand whenever the .blend changes. There is no freshness tracking --
# re-running it IS the workflow.
#
# Produces, under the project directory (<scene> = the .blend's file name):
#   reference/<scene>/direct.exr     lights only, no bounces, black world
#   reference/<scene>/full.exr       lights, 10 bounces, Blender's default sky
#   reference/<scene>/full_agx.exr   full through Blender's AgX view transform
#                                    (display-referred sRGB 0-1 values), report-only
#   reference/<scene>/ids.exr        object index per pixel, for per-object masks
#   reference/<scene>/objects.txt    index -> name, albedo
#   Source/<scene>/<scene>.gltf (+ .bin)
#
# A scene without a Sun lamp (the Cornell box) gets no sky: every render uses a
# black world, matching SeeD, whose sky is black without a directional light.
#
# The sun is Blender's default sky's own sun (Sky Texture, Multiple Scattering,
# every setting at its default except the direction, taken from the Sun lamp).
# Its strength is measured here, not authored: the lamp is set to what the sky's
# sun disc delivers at the ground for the renders (Blender lamps are never
# attenuated), and to the top-of-atmosphere value in the glTF, because SeeD's
# directional light is defined above the atmosphere and attenuated by it.
import bpy, sys, os, json, math

WATTS_TO_LUMENS = 683.0   # PBR_WATTS_TO_LUMENS, io_scene_gltf2/blender/com/conversion.py
SAMPLES = 2048
ID_SAMPLES = 1
FULL_BOUNCES = 10     # matches BOUNCES_MAX in raytracing2.hlsl

SUN_PROBE_SAMPLES = 1024
SUN_PROBE_TOA_ALTITUDE = 60000.0   # m; the sky's sun at the top of the atmosphere

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


def default_sky_world(sun_obj):
    """Blender's default sky, with its sun turned to the Sun lamp's direction.

    The disc is off: the lamp is the sun (sampled as a light, so it converges
    like SeeD's NEE does), the texture only provides the sky. sun_rotation is
    the azimuth from +Y toward +X -- checked with a probe render, not assumed.
    """
    w = bpy.data.worlds.new("CalibDefaultSky")
    w.use_nodes = True
    nt = w.node_tree
    sky = nt.nodes.new("ShaderNodeTexSky")
    to_sun = sun_obj.matrix_world.to_3x3().col[2].normalized()   # lamps shine down their -Z
    sky.sun_elevation = math.asin(max(-1.0, min(1.0, to_sun.z)))
    sky.sun_rotation = math.atan2(to_sun.x, to_sun.y)
    sky.sun_disc = False
    nt.links.new(sky.outputs["Color"], nt.nodes["Background"].inputs["Color"])
    nt.nodes["Background"].inputs["Strength"].default_value = 1.0
    return w, sky


def measure_sky_sun(sc, sky):
    """Irradiance (RGB, W/m^2 at normal incidence) of the sky's sun disc, at the
    sky's altitude and at the top of the atmosphere.

    A white Lambertian plane under the world, disc on minus disc off, divided by
    sin(elevation): exactly what the disc would put on the ground. Rendered in a
    throwaway scene so nothing in the calibration scene shadows or bounces.
    """
    probe = bpy.data.scenes.new("CalibSunProbe")
    probe.render.engine = 'CYCLES'
    probe.cycles.device = sc.cycles.device
    probe.cycles.samples = SUN_PROBE_SAMPLES
    probe.cycles.use_denoising = False
    probe.cycles.max_bounces = 0
    probe.render.resolution_x = probe.render.resolution_y = 16
    probe.view_settings.view_transform = 'Standard'
    ims = probe.render.image_settings
    ims.file_format = 'OPEN_EXR'
    ims.color_depth = '32'
    ims.exr_codec = 'NONE'
    probe.world = sc.world

    mesh = bpy.data.meshes.new("CalibSunProbePlane")
    h = 10000.0
    mesh.from_pydata([(-h, -h, 0), (h, -h, 0), (h, h, 0), (-h, h, 0)], [], [(0, 1, 2, 3)])
    plane = bpy.data.objects.new("CalibSunProbePlane", mesh)
    mat = bpy.data.materials.new("CalibSunProbeWhite")
    mat.use_nodes = True
    mt = mat.node_tree
    mt.nodes.clear()
    d = mt.nodes.new("ShaderNodeBsdfDiffuse")
    d.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    o = mt.nodes.new("ShaderNodeOutputMaterial")
    mt.links.new(d.outputs[0], o.inputs[0])
    mesh.materials.append(mat)
    probe.collection.objects.link(plane)
    cam_data = bpy.data.cameras.new("CalibSunProbeCam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 1.0
    cam = bpy.data.objects.new("CalibSunProbeCam", cam_data)
    cam.location = (0.0, 0.0, 1.0)
    probe.collection.objects.link(cam)
    probe.camera = cam

    tmp = os.path.join(bpy.app.tempdir, "calib_sun_probe.exr")

    def horizontal_irradiance():
        probe.render.filepath = tmp
        probe.render.use_file_extension = False
        bpy.ops.render.render(write_still=True, scene=probe.name)
        img = bpy.data.images.load(tmp)
        px = [0.0] * (img.size[0] * img.size[1] * 4)
        img.pixels.foreach_get(px)
        bpy.data.images.remove(img)
        n = len(px) // 4
        return [math.pi * sum(px[c::4]) / n for c in range(3)]   # L = E / pi for albedo 1

    altitude, disc = sky.altitude, sky.sun_disc
    sin_elev = math.sin(sky.sun_elevation)
    result = {}
    for key, alt in (("ground", altitude), ("toa", SUN_PROBE_TOA_ALTITUDE)):
        sky.altitude = alt
        sky.sun_disc = True
        lit = horizontal_irradiance()
        sky.sun_disc = False
        unlit = horizontal_irradiance()
        result[key] = [(lit[c] - unlit[c]) / sin_elev for c in range(3)]
    sky.altitude, sky.sun_disc = altitude, disc

    bpy.data.scenes.remove(probe)
    bpy.data.objects.remove(plane)
    bpy.data.objects.remove(cam)
    bpy.data.meshes.remove(mesh)
    bpy.data.materials.remove(mat)
    bpy.data.cameras.remove(cam_data)
    return result


def set_lamp(light, rgb):
    """Energy = the brightest channel, colour = rgb normalised to it."""
    peak = max(rgb)
    light.energy = peak
    light.color = tuple(c / peak for c in rgb)


def agx_reference(sc, exr_path, out_path):
    """exr_path through Blender's own AgX (sRGB display, look None, exposure 0),
    written as an EXR of the display values (sRGB-encoded, 0-1).

    save_render applies the view transform only to display formats, so the
    image goes out as a 16-bit PNG and is read back as Non-Color data.
    """
    vs = sc.view_settings
    saved = (vs.view_transform, vs.look, vs.exposure, vs.gamma, vs.use_curve_mapping)
    ims = sc.render.image_settings
    saved_ims = (ims.file_format, ims.color_mode, ims.color_depth, ims.exr_codec)
    sc.display_settings.display_device = 'sRGB'
    vs.view_transform = 'AgX'
    vs.look = 'None'
    vs.exposure, vs.gamma, vs.use_curve_mapping = 0.0, 1.0, False
    ims.file_format, ims.color_mode, ims.color_depth = 'PNG', 'RGB', '16'

    src = bpy.data.images.load(exr_path)
    src.colorspace_settings.name = 'Linear Rec.709'
    png = out_path[:-4] + "_tmp.png"
    src.save_render(png, scene=sc)
    back = bpy.data.images.load(png)
    back.colorspace_settings.name = 'Non-Color'
    w, h = back.size
    px = [0.0] * (w * h * 4)
    back.pixels.foreach_get(px)

    disp = bpy.data.images.new("CalibAgX", w, h, alpha=True, float_buffer=True)
    disp.colorspace_settings.name = 'Non-Color'
    disp.pixels.foreach_set(px)
    vs.view_transform = 'Standard'
    vs.look = 'None'
    ims.file_format, ims.color_mode, ims.color_depth, ims.exr_codec = 'OPEN_EXR', 'RGBA', '32', 'NONE'
    disp.save_render(out_path, scene=sc)
    for img in (src, back, disp):
        bpy.data.images.remove(img)
    os.remove(png)

    vs.view_transform = saved[0]
    vs.look = saved[1]
    vs.exposure, vs.gamma, vs.use_curve_mapping = saved[2], saved[3], saved[4]
    ims.file_format, ims.color_mode, ims.color_depth, ims.exr_codec = saved_ims
    log("wrote", out_path)


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
    ref = os.path.join(proj, "reference", scene_name)   # one folder per scene: a project can hold several
    src = os.path.join(proj, "Source", scene_name)
    os.makedirs(ref, exist_ok=True)
    os.makedirs(src, exist_ok=True)

    sc = bpy.context.scene
    reference_settings(sc)
    use_gpu(sc)
    log("project %s" % proj)
    log("resolution %dx%d" % (sc.render.resolution_x, sc.render.resolution_y))

    # The sun: Blender's default sky's own, measured. TOA for the glTF, ground for the renders.
    # Without a Sun lamp there is no sky either, on both sides (SeeD's sky is black without a
    # directional light), so every render uses a black world.
    original_world = sc.world
    sun_obj = next((o for o in sc.objects if o.type == 'LIGHT' and o.data.type == 'SUN'), None)
    if sun_obj:
        sky_world, sky = default_sky_world(sun_obj)
        sc.world = sky_world
        sun = measure_sky_sun(sc, sky)
        sun_obj.data.angle = sky.sun_size
        log("sky sun: elevation %.2f deg rotation %.2f deg, size %.4f deg" % (
            math.degrees(sky.sun_elevation), math.degrees(sky.sun_rotation), math.degrees(sky.sun_size)))
        log("sky sun irradiance W/m^2: top of atmosphere %s, ground (altitude %.0f m) %s" % (
            " ".join("%.3f" % v for v in sun["toa"]), sky.altitude, " ".join("%.3f" % v for v in sun["ground"])))
        set_lamp(sun_obj.data, sun["toa"])
    else:
        sky_world, sky, sun = black_world(), None, None
        log("no Sun lamp: no sky, black world for every render")

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
        if sun:
            f.write("# sky Blender default (Multiple Scattering), sun disc off, altitude %.1f m\n" % sky.altitude)
            f.write("# sun exported at the top of the atmosphere %s W/m^2, rendered at the ground %s W/m^2\n" % (
                " ".join("%.4f" % v for v in sun["toa"]), " ".join("%.4f" % v for v in sun["ground"])))
        else:
            f.write("# no sun, black world\n")
        for o in sorted(meshes, key=lambda o: o.pass_index):
            c = base_color(o)
            f.write("%d %s %.6f %.6f %.6f\n" % (o.pass_index, o.name, c[0], c[1], c[2]))
    log("wrote", objects_txt)

    if skip_render:
        log("--skip-render given; glTF and objects.txt only")
        return

    # Blender lamps are never attenuated: render with what reaches the ground
    if sun:
        set_lamp(sun_obj.data, sun["ground"])

    # direct: no bounces, black world
    sc.world = black_world()
    set_bounces(sc, 0)
    sc.cycles.samples = SAMPLES
    render_to(sc, os.path.join(ref, "direct.exr"))

    # full: bounces plus Blender's default sky (black without a sun)
    sc.world = sky_world
    set_bounces(sc, FULL_BOUNCES)
    render_to(sc, os.path.join(ref, "full.exr"))

    # full only: in direct the world is black here while SeeD still draws its sky
    agx_reference(sc, os.path.join(ref, "full.exr"), os.path.join(ref, "full_agx.exr"))

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

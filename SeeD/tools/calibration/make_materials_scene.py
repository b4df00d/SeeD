# Builds the "materials" calibration scene and saves it as a .blend.
#
#   <blender 5.4+> -b -P make_materials_scene.py -- [out.blend]
#
# One Standard Shader Ball per OpenPBR lobe SeeD implements, under the same lighting as the falloff
# scene (Blender's default sky + sun, a point light), so a lobe that is wrong shows up as one ball
# off in the per-object table.
#
# Needs Blender 5.4 or later: from 5.4 the Principled BSDF is OpenPBR (fuzz over coat over metal /
# translucent / gloss / subsurface / EON diffuse, specular weight folded into the IOR), so the
# reference is the same model SeeD implements. Every material only uses what the glTF exporter
# carries (metallic-roughness + KHR_materials_clearcoat / sheen / transmission / volume), which is
# what SeeD's importer maps onto OpenPBR.
#
# Geometry: the Standard Shader Ball, https://github.com/usd-wg/assets/tree/main/full_assets/StandardShaderBall
# (CC BY 4.0, after Thomas Anagnostou's "Simball"). Expected, triangulated variant, under
# calibration/Source/StandardShaderBall/ (see the calibration README):
#   layers/shaderball.usda                  neutral objects (base, core, sss_bars), 18% grey here
#   material_surface_geo/triangulated.usda  the material surface (sphere + rim)
#
# Known differences, not SeeD bugs:
#   - glass: SeeD has no caustics (the ground and neutral core seen through / lit through glass)
import bpy, sys, math, os, tempfile
from mathutils import Vector, Matrix

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_OUT = os.path.join(REPO, "calibration", "materials.blend")
BALL_DIR = os.path.join(REPO, "calibration", "Source", "StandardShaderBall")

RES_X, RES_Y = 1280, 720
BALL_SCALE = 5.0            # the asset is life size (8.9 cm rim): 5x keeps it ~45 cm, like the falloff spheres
ARC_RADIUS = 1.9            # horizontal distance from the light axis
LIGHT_HEIGHT = 1.5
SUN_WATTS = 5.0             # placeholder: render_reference.py sets the sun from Blender's default sky
POINT_WATTS = 2600.0        # same as the falloff scene
SUN_ANGLE_DEG = 0.545
CAMERA_POS = Vector((0.0, -5.2, 1.5))
CAMERA_TARGET = Vector((0.0, -0.95, 0.15))   # the middle of the arc of balls
# the azimuth the asset's own camera looks at the ball from (layers/camera.usda, Y-up -> Z-up)
BALL_FRONT_AZIMUTH = math.atan2(-17.9485, 6.53154)

# name, Principled BSDF inputs (5.4: Principled = OpenPBR)
BALLS = [
    ("dielectric_glossy", {"Base Color": (0.50, 0.50, 0.50), "Roughness": 0.15}),
    ("dielectric_rough",  {"Base Color": (0.20, 0.35, 0.60), "Roughness": 0.50}),
    ("dielectric_black",  {"Base Color": (0.0, 0.0, 0.0), "Roughness": 0.10}),   # the specular lobe alone
    ("metal_gold",        {"Base Color": (1.00, 0.766, 0.336), "Metallic": 1.0, "Roughness": 0.25}),
    ("metal_rough",       {"Base Color": (0.91, 0.92, 0.92), "Metallic": 1.0, "Roughness": 0.60}),
    ("coated_red",        {"Base Color": (0.80, 0.05, 0.05), "Roughness": 0.60,
                           "Coat Weight": 1.0, "Coat Roughness": 0.05, "Coat IOR": 1.5}),
    ("fuzz_velvet",       {"Base Color": (0.05, 0.05, 0.30), "Roughness": 0.80,
                           "Sheen Weight": 1.0, "Sheen Roughness": 0.5, "Sheen Tint": (1.0, 1.0, 1.0)}),
    ("glass",             {"Base Color": (1.0, 1.0, 1.0), "Roughness": 0.05, "Transmission Weight": 1.0}),
    ("glass_rough",       {"Base Color": (1.0, 1.0, 1.0), "Roughness": 0.40, "Transmission Weight": 1.0}),
]

# the material surface shell is ~1 cm thick (README: sphere 9.83 mm, rim 6.25 mm)
SHELL_THICKNESS = 0.00983 * BALL_SCALE


def gltf_output_group():
    """The node group the glTF exporter reads extra settings from: a "Thickness" input becomes
    KHR_materials_volume.thicknessFactor. Without it, glTF says a transmissive surface is
    infinitely thin, while Cycles renders the shell as solid glass."""
    g = bpy.data.node_groups.get("glTF Material Output")
    if g:
        return g
    g = bpy.data.node_groups.new("glTF Material Output", 'ShaderNodeTree')
    g.interface.new_socket("Occlusion", in_out='INPUT', socket_type='NodeSocketFloat')
    g.interface.new_socket("Thickness", in_out='INPUT', socket_type='NodeSocketFloat')
    return g


def material(name, inputs):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    b = nt.nodes["Principled BSDF"]
    b.distribution = 'MULTI_GGX'          # SeeD compensates multiple scattering too
    b.inputs["Specular IOR Level"].default_value = 0.5   # specular_weight 1
    b.inputs["IOR"].default_value = 1.5
    b.inputs["Diffuse Roughness"].default_value = 0.0   # glTF has no diffuse roughness
    b.inputs["Emission Strength"].default_value = 0.0
    for k, v in inputs.items():
        if isinstance(v, tuple):
            v = (v[0], v[1], v[2], 1.0)
        b.inputs[k].default_value = v
    if inputs.get("Transmission Weight", 0.0) > 0.0:
        node = nt.nodes.new("ShaderNodeGroup")
        node.node_tree = gltf_output_group()
        node.inputs["Thickness"].default_value = SHELL_THICKNESS
        node.location = (b.location.x, b.location.y - 700)
    return mat


def import_shader_ball():
    """-> (material surface mesh object, neutral mesh object), in metres, unparented, at the
    origin, standing on z = 0."""
    layer = os.path.join(BALL_DIR, "layers", "shaderball.usda")
    surface = os.path.join(BALL_DIR, "material_surface_geo", "triangulated.usda")
    for p in (layer, surface):
        if not os.path.exists(p):
            raise SystemExit("CALIB: missing %s -- download the Standard Shader Ball (calibration README)" % p)

    # triangulated.usda only holds an "over" of the shader_ball prim, which the importer skips
    text = open(surface, encoding="utf-8").read().replace('over "shader_ball"', 'def Xform "shader_ball"', 1)
    surfaceDef = os.path.join(tempfile.gettempdir(), "seed_shaderball_triangulated.usda")
    with open(surfaceDef, "w", encoding="utf-8") as f:
        f.write(text)

    bpy.ops.wm.usd_import(filepath=layer, import_materials=False)
    bpy.ops.wm.usd_import(filepath=surfaceDef, import_materials=False)

    meshes = {o.name: o for o in bpy.context.scene.objects if o.type == 'MESH' and len(o.data.vertices) > 0}
    shell = next(o for n, o in meshes.items() if n.startswith("material_surface"))
    neutral_parts = [meshes[n] for n in ("base", "core", "sss_bars")]

    # bake world transforms into the vertices, then drop every imported empty / leftover
    # shaderball.usda says metersPerUnit = 1 but is in cm like the rest of the asset
    for o in [shell] + neutral_parts:
        world = o.matrix_world.copy()
        if o is not shell:
            world = Matrix.Scale(0.01, 4) @ world
        o.parent = None
        o.data.transform(world)
        o.matrix_world = Matrix.Identity(4)
    for o in list(bpy.context.scene.objects):
        if o is not shell and o not in neutral_parts:
            bpy.data.objects.remove(o, do_unlink=True)

    bpy.ops.object.select_all(action='DESELECT')
    for o in neutral_parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = neutral_parts[0]
    bpy.ops.object.join()
    neutral = bpy.context.view_layer.objects.active

    zmin = min(v.co.z for o in (shell, neutral) for v in o.data.vertices)
    for o in (shell, neutral):
        o.data.transform(Matrix.Scale(BALL_SCALE, 4) @ Matrix.Translation((0, 0, -zmin)))
        for p in o.data.polygons:
            p.use_smooth = True
    return shell, neutral


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = os.path.abspath(argv[0]) if argv else DEFAULT_OUT
    if bpy.app.version < (5, 4, 0):
        raise SystemExit("CALIB: Blender 5.4+ needed (Principled BSDF = OpenPBR), this is %s" % bpy.app.version_string)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = 1.0
    sc.render.engine = 'CYCLES'
    sc.render.resolution_x = RES_X
    sc.render.resolution_y = RES_Y
    sc.render.resolution_percentage = 100

    shell, neutral = import_shader_ball()
    neutral_mat = material("Neutral", {"Base Color": (0.18, 0.18, 0.18), "Roughness": 1.0})

    # --- ground -----------------------------------------------------------
    bpy.ops.mesh.primitive_plane_add(size=20.0, location=(0, 0, 0))
    ground = bpy.context.active_object
    ground.name = "Ground"
    ground.data.materials.append(material("Ground", {"Base Color": (0.18, 0.18, 0.18), "Roughness": 1.0}))
    ground.pass_index = 1

    # --- balls, all equidistant from the point light, each turned to show the camera its front --
    n = len(BALLS)
    a0, a1 = math.radians(202.0), math.radians(338.0)
    index = 2
    for i, (name, inputs) in enumerate(BALLS):
        t = i / (n - 1) if n > 1 else 0.5
        a = a0 + (a1 - a0) * t
        pos = Vector((ARC_RADIUS * math.cos(a), ARC_RADIUS * math.sin(a), 0.0))
        to_camera = CAMERA_POS - pos
        yaw = math.atan2(to_camera.y, to_camera.x) - BALL_FRONT_AZIMUTH

        ball = shell.copy()
        ball.data = shell.data.copy()
        ball.name = "Ball_" + name
        ball.data.materials.append(material("Mat_" + name, inputs))
        ball.location = pos
        ball.rotation_euler = (0.0, 0.0, yaw)
        ball.pass_index = index
        sc.collection.objects.link(ball)

        core = neutral.copy()
        core.data = neutral.data.copy()
        core.name = "Neutral_" + name
        core.data.materials.append(neutral_mat)
        core.location = pos
        core.rotation_euler = (0.0, 0.0, yaw)
        core.pass_index = index + 1
        sc.collection.objects.link(core)
        index += 2
    bpy.data.objects.remove(shell, do_unlink=True)
    bpy.data.objects.remove(neutral, do_unlink=True)

    # --- lights (as in make_falloff_scene.py) ------------------------------
    sun_data = bpy.data.lights.new("Sun", type='SUN')
    sun_data.energy = SUN_WATTS
    sun_data.angle = math.radians(SUN_ANGLE_DEG)
    sun = bpy.data.objects.new("Sun", sun_data)
    sc.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50.0), 0.0, math.radians(-45.0))

    pt_data = bpy.data.lights.new("PointKey", type='POINT')
    pt_data.energy = POINT_WATTS
    pt_data.shadow_soft_size = 0.05
    pt_data.use_soft_falloff = False
    pt = bpy.data.objects.new("PointKey", pt_data)
    sc.collection.objects.link(pt)
    pt.location = (0.0, 0.0, LIGHT_HEIGHT)

    # --- camera -----------------------------------------------------------
    cam_data = bpy.data.cameras.new("CalibCam")
    cam_data.sensor_fit = 'VERTICAL'
    cam_data.angle_y = math.radians(30.0)
    cam_data.clip_start = 0.02
    cam_data.clip_end = 1000.0
    cam = bpy.data.objects.new("CalibCam", cam_data)
    sc.collection.objects.link(cam)
    cam.location = CAMERA_POS
    look_at = CAMERA_TARGET
    cam.rotation_euler = (look_at - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam

    world = bpy.data.worlds.new("CalibWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.0
    sc.world = world

    d = os.path.dirname(out)
    if d:
        os.makedirs(d, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=out)
    print("CALIB: wrote %s (%d shader balls)" % (out, n))


main()

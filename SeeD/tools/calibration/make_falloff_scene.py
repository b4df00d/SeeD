# Builds the "falloff" calibration scene and saves it as a .blend.
#
#   blender -b -P make_falloff_scene.py -- [out.blend]
#
# Run once. After that the .blend is the source of truth and can be edited by
# hand; render_reference.py re-derives everything it needs from it.
#
# Everything here is chosen so the two renderers can agree:
#   - metallic 0 and roughness 1, so the diffuse term dominates
#   - IOR 1.5, so F0 lands on 0.04, matching MIN_DIELECTRICS_F0 in SeeD
#   - GGX rather than MULTI_GGX, because SeeD has no multiscatter compensation
#   - Diffuse Roughness 0, so Blender stays Lambertian
#   - every sphere the same distance from the point light, so only albedo varies
import bpy, sys, math, os
from mathutils import Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_OUT = os.path.join(REPO, "calibration", "falloff.blend")

RES_X, RES_Y = 1280, 720
SPHERE_RADIUS = 0.2
ARC_RADIUS = 1.8            # horizontal distance from the light axis
LIGHT_HEIGHT = 1.5
SUN_WATTS = 5.0             # placeholder: render_reference.py sets the sun from Blender's default sky
POINT_WATTS = 2600.0        # W; keeps the old 100 W : 5 W/m^2 point/sun balance against the sky's ~130 W/m^2 sun
SUN_ANGLE_DEG = 0.545       # Blender's default sky sun size; render_reference.py copies it anyway

# name, linear RGB albedo
SPHERES = [
    ("grey004", (0.04, 0.04, 0.04)),
    ("grey018", (0.18, 0.18, 0.18)),
    ("grey050", (0.50, 0.50, 0.50)),
    ("grey090", (0.90, 0.90, 0.90)),
    ("red",     (0.80, 0.05, 0.05)),
    ("green",   (0.05, 0.80, 0.05)),
    ("blue",    (0.05, 0.05, 0.80)),
]


def diffuse_material(name, rgb):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.distribution = 'GGX'
    b.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    b.inputs["Metallic"].default_value = 0.0
    b.inputs["Roughness"].default_value = 1.0
    b.inputs["Diffuse Roughness"].default_value = 0.0
    b.inputs["IOR"].default_value = 1.5
    b.inputs["Specular IOR Level"].default_value = 0.5
    b.inputs["Emission Strength"].default_value = 0.0
    return mat


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = os.path.abspath(argv[0]) if argv else DEFAULT_OUT

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = 1.0
    sc.render.engine = 'CYCLES'
    sc.render.resolution_x = RES_X
    sc.render.resolution_y = RES_Y
    sc.render.resolution_percentage = 100

    # --- ground -----------------------------------------------------------
    bpy.ops.mesh.primitive_plane_add(size=20.0, location=(0, 0, 0))
    ground = bpy.context.active_object
    ground.name = "Ground"
    ground.data.materials.append(diffuse_material("Ground", (0.18, 0.18, 0.18)))
    ground.pass_index = 1

    # --- spheres, all equidistant from the point light ---------------------
    # Arranged on an arc bulging toward the camera so none occludes another.
    n = len(SPHERES)
    a0, a1 = math.radians(200.0), math.radians(340.0)
    for i, (name, rgb) in enumerate(SPHERES):
        t = i / (n - 1) if n > 1 else 0.5
        a = a0 + (a1 - a0) * t
        x, y = ARC_RADIUS * math.cos(a), ARC_RADIUS * math.sin(a)
        bpy.ops.mesh.primitive_uv_sphere_add(
            segments=64, ring_count=32, radius=SPHERE_RADIUS,
            location=(x, y, SPHERE_RADIUS))
        obj = bpy.context.active_object
        obj.name = "Sphere_" + name
        bpy.ops.object.shade_smooth()
        obj.data.materials.append(diffuse_material("Mat_" + name, rgb))
        obj.pass_index = 2 + i

    # --- lights -----------------------------------------------------------
    sun_data = bpy.data.lights.new("Sun", type='SUN')
    # Cycles renders from light.energy, but the glTF exporter reads a sun's
    # strength from its Emission node instead (1.0 by default,
    # exp/lights.py:137). Clearing use_nodes here would not help -- Cycles
    # re-enables it on save -- so render_reference.py forces it off at export
    # time and verifies the exported intensity against energy.
    sun_data.energy = SUN_WATTS
    sun_data.angle = math.radians(SUN_ANGLE_DEG)
    sun = bpy.data.objects.new("Sun", sun_data)
    sc.collection.objects.link(sun)
    # Default sun points down -Z. 50 deg about X puts it 40 deg above the
    # horizon; -45 deg about Z brings it over the camera's left shoulder.
    sun.rotation_euler = (math.radians(50.0), 0.0, math.radians(-45.0))

    pt_data = bpy.data.lights.new("PointKey", type='POINT')
    pt_data.energy = POINT_WATTS
    pt_data.shadow_soft_size = 0.05
    pt_data.use_soft_falloff = False   # a plain sphere light, no near-field fudge
    pt = bpy.data.objects.new("PointKey", pt_data)
    sc.collection.objects.link(pt)
    pt.location = (0.0, 0.0, LIGHT_HEIGHT)

    # --- camera -----------------------------------------------------------
    cam_data = bpy.data.cameras.new("CalibCam")
    cam_data.sensor_fit = 'VERTICAL'
    cam_data.angle_y = math.radians(45.0)
    cam_data.clip_start = 0.02
    cam_data.clip_end = 1000.0
    cam = bpy.data.objects.new("CalibCam", cam_data)
    sc.collection.objects.link(cam)
    cam.location = (0.0, -6.0, 2.2)
    look_at = Vector((0.0, 0.0, 0.4))
    cam.rotation_euler = (look_at - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam

    # World is set per render by render_reference.py (black for direct,
    # Blender's default sky for full GI), so it is left black here.
    world = bpy.data.worlds.new("CalibWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0, 0, 0, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.0
    sc.world = world

    d = os.path.dirname(out)
    if d:
        os.makedirs(d, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=out)

    light_dist = math.sqrt(ARC_RADIUS ** 2 + (LIGHT_HEIGHT - SPHERE_RADIUS) ** 2)
    print("CALIB: wrote %s" % out)
    print("CALIB: %d spheres, each %.4f m from the point light" % (n, light_dist))
    print("CALIB: sun %.3f W/m^2, point %.3f W" % (SUN_WATTS, POINT_WATTS))


main()

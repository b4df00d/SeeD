# Builds the "cornell" calibration scene and saves it as a .blend.
#
#   blender -b -P make_cornell_scene.py -- [out.blend]
#
# A room-sized Cornell box lit only by a household bulb, so the full entry is
# dominated by interreflection rather than by the sky. Same material rules as
# make_falloff_scene.py (metallic 0, roughness 1, IOR 1.5, GGX, Lambertian
# diffuse) so the two renderers can agree.
#
# The front wall is left out for the camera; the world stays black, so light
# leaving through the opening is simply lost on both sides.
import bpy, sys, math, os

RES_X, RES_Y = 1280, 720

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_OUT = os.path.join(REPO, "calibration", "cornell.blend")

WIDTH, DEPTH, HEIGHT = 4.4, 3.5, 2.5      # m, interior (16:9 opening); the opening is at y = 0
BULB_LUMENS = 1600.0                      # a common 100 W-equivalent household bulb
BULB_WATTS = BULB_LUMENS / 683.0          # Blender / glTF SPEC convention (render_reference.WATTS_TO_LUMENS)
BULB_RADIUS = 0.03
BULB_DROP = 0.4                           # below the ceiling

WHITE = (0.75, 0.75, 0.75)
RED = (0.63, 0.065, 0.05)
GREEN = (0.14, 0.45, 0.091)


def diffuse_material(name, rgb):
    """Same as make_falloff_scene.py's (not imported: that script builds its scene on import)."""
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


def quad(name, verts, rgb, index):
    """A single-sided wall; verts counter-clockwise seen from inside the room."""
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], [(0, 1, 2, 3)])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(diffuse_material("Mat_" + name, rgb))
    obj.pass_index = index
    return obj


def block(name, size, location, yaw_deg, rgb, index):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    obj.rotation_euler = (0.0, 0.0, math.radians(yaw_deg))
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(diffuse_material("Mat_" + name, rgb))
    obj.pass_index = index
    return obj


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

    x0, x1, y1, z1 = -WIDTH / 2, WIDTH / 2, DEPTH, HEIGHT
    quad("Floor",     [(x0, 0, 0), (x1, 0, 0), (x1, y1, 0), (x0, y1, 0)], WHITE, 1)
    quad("Ceiling",   [(x0, 0, z1), (x0, y1, z1), (x1, y1, z1), (x1, 0, z1)], WHITE, 2)
    quad("BackWall",  [(x0, y1, 0), (x1, y1, 0), (x1, y1, z1), (x0, y1, z1)], WHITE, 3)
    quad("LeftWall",  [(x0, 0, 0), (x0, y1, 0), (x0, y1, z1), (x0, 0, z1)], RED, 4)
    quad("RightWall", [(x1, 0, 0), (x1, 0, z1), (x1, y1, z1), (x1, y1, 0)], GREEN, 5)

    # Flat faces on purpose: they score per face without the open curved-geometry question
    block("TallBlock",  (0.8, 0.8, 1.5), (-0.9, 2.4, 0.75), 20.0, WHITE, 6)
    block("ShortBlock", (0.8, 0.8, 0.8), (1.0, 1.6, 0.4), -18.0, WHITE, 7)

    bulb_data = bpy.data.lights.new("Bulb", type='POINT')
    bulb_data.energy = BULB_WATTS
    bulb_data.shadow_soft_size = BULB_RADIUS
    bulb_data.use_soft_falloff = False
    bulb = bpy.data.objects.new("Bulb", bulb_data)
    sc.collection.objects.link(bulb)
    bulb.location = (0.0, DEPTH * 0.5, HEIGHT - BULB_DROP)

    cam_data = bpy.data.cameras.new("CalibCam")
    cam_data.sensor_fit = 'VERTICAL'
    cam_data.angle_y = math.radians(45.0)
    cam_data.clip_start = 0.02
    cam_data.clip_end = 100.0
    cam = bpy.data.objects.new("CalibCam", cam_data)
    sc.collection.objects.link(cam)
    # Outside the opening, far enough back that the opening just fills the frame (classic Cornell view)
    distance = (HEIGHT * 0.5) / math.tan(cam_data.angle_y * 0.5) * 1.04
    cam.location = (0.0, -distance, HEIGHT * 0.5)
    cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)   # looking down +Y into the room
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


if __name__ == "__main__":
    main()

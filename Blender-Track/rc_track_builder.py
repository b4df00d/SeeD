bl_info = {
    "name": "RC Carpet Track Builder",
    "author": "Claude",
    "version": (1, 4, 0),
    "blender": (4, 2, 0),
    "location": "3D Viewport > Sidebar (N) > RC Track",
    "description": "Build RC buggy carpet tracks (road, borders, curbs, jumps, checkpoints) from a traced centerline",
    "category": "Add Mesh",
}

# -----------------------------------------------------------------------------
#  RC Carpet Track Builder
#
#  Workflow
#   1. Load your drawing as a top-view reference (scaled in meters).
#   2. Add the centerline (a closed Bezier curve) and trace the racing line
#      over the drawing in top view (E = extrude point, Alt+S = local width).
#   3. The road (2 m wide by default), borders (batten or rope), curbs at tight
#      corners, floor, start line and checkpoints are rebuilt automatically.
#      Red issue markers (RC_Issues collection) show corners that are too tight
#      or track sections that overlap.
#      Walls = "Shared cells" flood-fills N m around the road instead: parts of the
#      track that come close share a single wall (Voronoi-like), others get a wall
#      at the outer limit.
#   4. Add jumps (separate objects, track width), move them with G and click
#      "Snap Selected to Track" to align them with the road.
#   5. Export as .glb (or use File > Export with any format).
#
#  Can be installed as an add-on (Edit > Preferences > Add-ons > Install from
#  Disk) or simply opened in the Text Editor and run with "Run Script".
# -----------------------------------------------------------------------------

import bpy
import bmesh
import gpu
import math
import time
from gpu_extras.batch import batch_for_shader
from bpy_extras import view3d_utils
from mathutils import Vector, Matrix
from mathutils.geometry import interpolate_bezier, intersect_line_plane
from bpy.props import (FloatProperty, IntProperty, BoolProperty, EnumProperty,
                       PointerProperty, StringProperty)
from bpy.types import Operator, Panel, PropertyGroup
from bpy.app.handlers import persistent
from bpy_extras.io_utils import ImportHelper, ExportHelper

COLL_TRACK = "RC_Track"
COLL_JUMPS = "RC_Jumps"
COLL_ISSUES = "RC_Issues"
COLL_CHECKPOINTS = "RC_Checkpoints"
COLL_SOURCE = "RC_Source"
REF_NAME = "RC_Reference"

MAT_COLORS = {
    "RC_Road": (0.075, 0.094, 0.131, 1.0),     # blue-grey carpet
    "RC_Floor": (0.106, 0.134, 0.191, 1.0),   # road carpet, a bit brighter (mean of the texture)
    "RC_Border": (0.90, 0.90, 0.88, 1.0),    # shiny white plastic wall
    "RC_Rope": (0.05, 0.05, 0.05, 1.0),      # black rope
    "RC_CurbRed": (0.85, 0.08, 0.06, 1.0),
    "RC_CurbWhite": (0.92, 0.92, 0.92, 1.0),
    "RC_Disc": (1.0, 0.36, 0.02, 1.0),       # orange marker discs
    "RC_Tape": (0.62, 0.63, 0.65, 1.0),      # bright grey duct tape on jump edges
    "RC_Jump": (0.075, 0.094, 0.131, 1.0),     # carpeted like the road
    # gymnasium
    "RC_GymFloor": (0.10, 0.42, 0.32, 1.0),
    "RC_GymWall": (0.86, 0.86, 0.84, 1.0),
    "RC_GymCeiling": (0.78, 0.78, 0.76, 1.0),
    "RC_GymBeam": (0.55, 0.55, 0.55, 1.0),
    "RC_GymFrame": (0.06, 0.06, 0.06, 1.0),
    "RC_GymGlass": (0.55, 0.75, 0.70, 0.3),
    "RC_GymLineWhite": (0.95, 0.95, 0.95, 1.0),
    "RC_GymLineBlue": (0.15, 0.45, 0.95, 1.0),
    "RC_GymLineRed": (0.85, 0.10, 0.10, 1.0),
    "RC_GymLineBlack": (0.03, 0.03, 0.03, 1.0),
    "RC_GymWood": (0.62, 0.45, 0.28, 1.0),
    "RC_GymBoard": (0.95, 0.95, 0.95, 1.0),
    "RC_GymMetal": (0.25, 0.25, 0.27, 1.0),
    "RC_GymRing": (0.95, 0.35, 0.05, 1.0),
    "RC_GymDoor": (0.50, 0.52, 0.55, 1.0),
    "RC_GymNet": (0.92, 0.92, 0.90, 0.35),
    "RC_GymLight": (1.0, 1.0, 0.95, 1.0),
}

COLL_GYM = "RC_Gym"
COLL_GYM_CEIL = "RC_Gym_Ceiling"
COLL_GYM_LIGHTS = "RC_Gym_Lights"
GYM_ROOT = "RC_Gym_Root"
_gstate = {"pending": False, "last": 0.0, "scene": None}

_state = {"pending": False, "last": 0.0, "scene": None, "sig": None,
          "ignore_until": 0.0, "issue_idx": -1}


# =============================================================================
#  Generic helpers
# =============================================================================

def get_collection(name, scene, parent=None):
    coll = bpy.data.collections.get(name)
    if coll is None:
        coll = bpy.data.collections.new(name)
    if parent is not None:
        if parent.children.get(coll.name) is None:
            try:
                parent.children.link(coll)
            except RuntimeError:
                pass
        return coll
    if scene.collection.children.get(coll.name) is None:
        # Only link to the scene root if it is not already somewhere in the scene
        linked = any(c.children.get(coll.name) for c in scene.collection.children_recursive)
        if not linked:
            scene.collection.children.link(coll)
    return coll


def clear_collection(coll):
    for o in list(coll.objects):
        bpy.data.objects.remove(o, do_unlink=True)


def remove_object(name):
    o = bpy.data.objects.get(name)
    if o is not None:
        me = o.data if o.type == 'MESH' else None
        bpy.data.objects.remove(o, do_unlink=True)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)


def get_mesh_object(name, coll):
    obj = bpy.data.objects.get(name)
    if obj is not None and obj.type != 'MESH':
        bpy.data.objects.remove(obj, do_unlink=True)
        obj = None
    if obj is None:
        obj = bpy.data.objects.new(name, bpy.data.meshes.new(name))
    if coll.objects.get(obj.name) is None:
        coll.objects.link(obj)
    obj.matrix_world = Matrix.Identity(4)
    return obj


MAT_ROUGHNESS = {
    "RC_Border": 0.15,                        # glossy plastic
    "RC_Tape": 0.25,                          # duct tape: shiny
}

MAT_METALLIC = {
    "RC_Tape": 0.6,                           # duct tape's silvery sheen
}


def get_material(name):
    """Create the material, or re-apply its colour/roughness/metallic when MAT_COLORS /
    MAT_ROUGHNESS / MAT_METALLIC changed since it was made (tracked with 'rc_spec')."""
    col = MAT_COLORS.get(name, (0.8, 0.8, 0.8, 1.0))
    rough = MAT_ROUGHNESS.get(name, 0.9)
    metal = MAT_METALLIC.get(name, 0.0)
    spec = repr((tuple(col), rough)) if metal == 0.0 else repr((tuple(col), rough, metal))
    mat = bpy.data.materials.get(name)
    if mat is not None and mat.get("rc_spec") == spec:
        return mat
    if mat is None:
        mat = bpy.data.materials.new(name)
    mat["rc_spec"] = spec
    mat.diffuse_color = col
    mat.roughness = rough
    mat.metallic = metal
    try:
        if mat.node_tree is None:
            mat.use_nodes = True
        nt = mat.node_tree
        if nt is not None:
            bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
            if bsdf is None:
                bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
                out = next((n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'), None)
                if out is None:
                    out = nt.nodes.new('ShaderNodeOutputMaterial')
                nt.links.new(bsdf.outputs[0], out.inputs[0])
            bsdf.inputs['Base Color'].default_value = col
            bsdf.inputs['Roughness'].default_value = rough
            bsdf.inputs['Metallic'].default_value = metal
            if col[3] < 1.0:
                bsdf.inputs['Alpha'].default_value = col[3]
                for attr, val in (("surface_render_method", 'BLENDED'), ("blend_method", 'BLEND')):
                    try:
                        setattr(mat, attr, val)
                    except Exception:
                        pass
            if name == "RC_GymLight":
                try:
                    bsdf.inputs['Emission Color'].default_value = col
                    bsdf.inputs['Emission Strength'].default_value = 3.0
                except Exception:
                    pass
    except Exception as e:  # material setup is cosmetic only
        print("RC Track: material setup warning:", e)
    return mat


# =============================================================================
#  Carpet texture (procedural, tileable, packed into the .blend)
# =============================================================================

CARPET_VERSION = "4"          # bump to regenerate the images in existing files
CARPET_SEEDS = {"RC_Floor": 1234, "RC_Road": 4321}
CARPET_SIZE = 1024            # pixels per UV tile (uv_tile metres, 1 m by default)


def _lin_to_srgb(x):
    import numpy as np
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)


def carpet_maps(base, size=CARPET_SIZE, seed=1234):
    """Tileable carpet: band-limited noises made in the frequency domain (so they wrap
    perfectly). Returns (albedo RGBA, normal RGBA), float32 arrays, rows = +V."""
    import numpy as np
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(size) * size                      # cycles per tile
    fr = np.sqrt(f[None, :] ** 2 + f[:, None] ** 2)

    def band(center, width):
        w = rng.standard_normal((size, size))
        flt = np.exp(-0.5 * ((fr - center) / width) ** 2)
        n = np.real(np.fft.ifft2(np.fft.fft2(w) * flt))
        return (n - n.mean()) / (n.std() + 1e-12)

    tufts = band(260.0, 70.0)        # ~4 mm loops
    fibres = band(480.0, 60.0)       # pixel-level fibre grain
    mottle = band(10.0, 6.0)         # large soft variation across the floor
    patches = band(56.0, 20.0)       # mid-scale wear / pile direction

    height = 0.65 * tufts + 0.35 * fibres
    shade = 1.0 + 0.05 * mottle + 0.05 * patches + 0.10 * tufts + 0.07 * fibres
    shade = np.clip(shade, 0.6, 1.4)
    alb = np.ones((size, size, 4), np.float32)
    for c in range(3):
        alb[..., c] = _lin_to_srgb(base[c] * shade)

    # normal from the periodic height gradient (OpenGL / glTF convention: +Y = +V)
    k = 1.6
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
    nx, ny, nz = -k * gx * 0.25, -k * gy * 0.25, np.ones_like(height)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    nrm = np.ones((size, size, 4), np.float32)
    nrm[..., 0] = nx * inv * 0.5 + 0.5
    nrm[..., 1] = ny * inv * 0.5 + 0.5
    nrm[..., 2] = nz * inv * 0.5 + 0.5
    return alb, nrm


def _texture_path(name):
    """PNG next to the .blend (copy it next to the exported .glb). Unsaved file: temp dir."""
    import os
    if bpy.data.filepath:
        return os.path.join(os.path.dirname(bpy.data.filepath), name + ".png"), "//" + name + ".png"
    p = os.path.join(bpy.app.tempdir or os.path.expanduser("~"), name + ".png")
    print("RC Track: .blend not saved yet, texture written to", p)
    return p, p


def _carpet_image(name, pixels, colorspace):
    """Write the texture as an external PNG (not packed) and return an image using it."""
    old = bpy.data.images.get(name)
    if old is not None:                      # drop packed / stale versions
        bpy.data.images.remove(old)
    size = pixels.shape[0]
    tmp = bpy.data.images.new(name + "_gen", size, size, alpha=False)
    tmp.colorspace_settings.name = colorspace
    tmp.pixels.foreach_set(pixels.ravel())
    abs_path, rel_path = _texture_path(name)
    tmp.filepath_raw = abs_path
    tmp.file_format = 'PNG'
    tmp.save()
    bpy.data.images.remove(tmp)
    img = bpy.data.images.load(abs_path, check_existing=False)
    img.name = name
    img.filepath = rel_path
    img.colorspace_settings.name = colorspace
    return img


def _carpet_files_ok(name):
    import os
    for suffix in ("_Albedo", "_Normal"):
        img = bpy.data.images.get(name + suffix)
        if img is None or img.packed_file is not None or \
                not os.path.isfile(bpy.path.abspath(img.filepath)):
            return False
    return True


def carpet_material(name):
    """Carpet material: tileable albedo + normal map saved as PNGs next to the .blend."""
    mat = get_material(name)
    tag = CARPET_VERSION + repr(tuple(MAT_COLORS[name]))
    if mat.get("rc_carpet") == tag and mat.use_nodes and \
            any(n.type == 'NORMAL_MAP' for n in mat.node_tree.nodes) and _carpet_files_ok(name):
        return mat
    try:
        alb, nrm = carpet_maps(MAT_COLORS[name], seed=CARPET_SEEDS.get(name, 1234))
    except ImportError:
        print("RC Track: numpy not available, carpet texture skipped")
        return mat
    ia = _carpet_image(name + "_Albedo", alb, 'sRGB')
    inm = _carpet_image(name + "_Normal", nrm, 'Non-Color')
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        return mat
    for n in [n for n in nt.nodes if n.name in ("RC_CarpetAlbedo", "RC_CarpetNormal", "RC_CarpetNormalMap")]:
        nt.nodes.remove(n)
    ta = nt.nodes.new('ShaderNodeTexImage')
    ta.name = "RC_CarpetAlbedo"
    ta.image = ia
    ta.location = (bsdf.location.x - 500, bsdf.location.y)
    tn = nt.nodes.new('ShaderNodeTexImage')
    tn.name = "RC_CarpetNormal"
    tn.image = inm
    tn.location = (bsdf.location.x - 700, bsdf.location.y - 320)
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nm.name = "RC_CarpetNormalMap"
    nm.location = (bsdf.location.x - 300, bsdf.location.y - 320)
    nt.links.new(ta.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    mat["rc_carpet"] = tag
    return mat


def _object_names(coll):
    """Names of the collection's objects, snapshotted. Iterate these (and look objects up again)
    whenever the loop body creates/removes datablocks: that invalidates all_objects' cache
    mid-iteration, giving stale entries or a hard crash."""
    return [o.name for o in coll.all_objects if o is not None]


def recenter_origin(obj):
    """Move the object origin to the centre of its mesh bounding box without moving the
    geometry in the world (mesh shifted by -c, object by +c in its parent space)."""
    if obj is None or obj.type != 'MESH' or not obj.data.vertices:
        return
    me = obj.data
    n = len(me.vertices)
    co = [0.0] * (3 * n)
    me.vertices.foreach_get("co", co)
    xs, ys, zs = co[0::3], co[1::3], co[2::3]
    c = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, (min(zs) + max(zs)) * 0.5))
    if c.length < 1e-7:
        return
    me.transform(Matrix.Translation(-c))
    me.update()
    obj.matrix_basis = obj.matrix_basis @ Matrix.Translation(c)
    # hidden objects (e.g. the gym ceiling) are not evaluated, so their cached matrix_world
    # would stay stale - and exporters read it: write it explicitly
    parent = (obj.parent.matrix_world @ obj.matrix_parent_inverse) if obj.parent else Matrix.Identity(4)
    obj.matrix_world = parent @ obj.matrix_basis


class MeshAcc:
    """Accumulates vertices / faces / per-face UVs / material indices."""

    def __init__(self):
        self.v, self.f, self.u, self.m = [], [], [], []

    def add(self, v, f, u, m=None):
        o = len(self.v)
        self.v += v
        self.f += [tuple(i + o for i in face) for face in f]
        self.u += u
        self.m += (m if m is not None else [0] * len(f))


def write_mesh(obj, verts, faces, uvs=None, mat_idx=None, mats=(), normals='RECALC', smooth=False):
    """Replace obj's mesh geometry. uvs = list (per face) of list of (u,v)."""
    tmp = bpy.data.meshes.new(obj.name + "_tmp")
    tmp.from_pydata(verts, [], faces)
    if uvs is not None and faces:
        uvl = tmp.uv_layers.new(name="UVMap")
        flat = [c for face_uv in uvs for uv in face_uv for c in uv]
        uvl.data.foreach_set("uv", flat)
    if mat_idx is not None and faces:
        tmp.polygons.foreach_set("material_index", mat_idx)
    tmp.validate(clean_customdata=False)
    tmp.update()

    bm = bmesh.new()
    bm.from_mesh(tmp)
    if bm.faces:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        if normals == 'UP':
            bm.normal_update()
            down = [f for f in bm.faces if f.normal.z < 0]
            if down:
                bmesh.ops.reverse_faces(bm, faces=down)
    if isinstance(smooth, (list, tuple)):
        # per-face smoothing GROUP (same order as faces): 0/False = flat, >0 = smooth within
        # its group. Edges between different groups are made sharp, so e.g. a disc's vertical
        # rim and its shallow cone don't average their normals into a dome.
        bm.faces.ensure_lookup_table()
        group = {}
        for f in bm.faces:
            gid = int(smooth[f.index]) if f.index < len(smooth) else 0
            group[f] = gid
            f.smooth = gid > 0
        for e in bm.edges:
            lf = e.link_faces
            if len(lf) == 2 and group[lf[0]] != group[lf[1]]:
                e.smooth = False
    else:
        for f in bm.faces:
            f.smooth = smooth
    face_mats = None
    if mat_idx is not None and bm.faces:
        bm.faces.ensure_lookup_table()
        face_mats = [int(mat_idx[f.index]) if f.index < len(mat_idx) else 0 for f in bm.faces]
    bm.to_mesh(obj.data)
    bm.free()
    bpy.data.meshes.remove(tmp)

    me = obj.data
    me.materials.clear()
    for m in mats:
        me.materials.append(m)
    # set the per-face slots last: clearing the material slots drops the
    # material_index attribute (Blender 4+), so setting it earlier is lost
    if face_mats is not None and len(face_mats) == len(me.polygons):
        me.polygons.foreach_set("material_index", face_mats)
    me.update()


# =============================================================================
#  Centerline sampling & analysis
# =============================================================================

def _left(v):
    return Vector((-v.y, v.x, 0.0))


def _rot(v, a):
    c, s = math.cos(a), math.sin(a)
    return Vector((v.x * c - v.y * s, v.x * s + v.y * c, 0.0))


def belt_tangent(c1, c2):
    """Line tangent to two 'pulleys' (center C, radius R, turn sign s: +1 left, -1 right,
    0 = point). Returns (P1, P2, direction, ok)."""
    D = c2['C'] - c1['C']
    m = D.length
    if m < 1e-9:
        return None
    k = c2['s'] * c2['R'] - c1['s'] * c1['R']
    x = -k / m
    ok = abs(x) <= 1.0
    phi = math.asin(max(-1.0, min(1.0, x)))
    t = _rot(D / m, phi)
    P1 = c1['C'] - _left(t) * (c1['s'] * c1['R'])
    P2 = c2['C'] - _left(t) * (c2['s'] * c2['R'])
    return P1, P2, t, ok


def corner_circles(P, rad, cyclic, s):
    """One turning circle per clicked point. The circle is sized so that the road plus its
    border never folds (radius >= half width + border + clearance)."""
    n = len(P)
    border = s.border_width if s.border_type != 'NONE' else 0.0
    circ = []
    for i in range(n):
        base = {'first': i, 'last': i, 'rad': rad[i]}
        if not cyclic and (i == 0 or i == n - 1):
            circ.append(dict(base, C=P[i].copy(), R=0.0, s=0))
            continue
        a, b, c = P[i - 1], P[i], P[(i + 1) % n]
        din, dout = b - a, c - b
        if din.length < 1e-6 or dout.length < 1e-6:
            circ.append(dict(base, C=b.copy(), R=0.0, s=0))
            continue
        din.normalize()
        dout.normalize()
        cr = din.x * dout.y - din.y * dout.x
        theta = abs(math.atan2(cr, din.dot(dout)))
        sg = 1 if cr >= 0 else -1
        hw = 0.5 * s.track_width * max(rad[i], 0.05)
        R = max(s.corner_radius, hw + border + s.clearance + 0.02)
        bis = dout - din
        if bis.length < 1e-6:
            bis = _left(din) * sg
        bis.normalize()
        # gentle corners: classic fillet tangent to both clicked lines;
        # sharp corners / hairpins: the arc apex goes (almost) through the clicked point
        if theta <= math.pi / 2:
            d = R / max(math.cos(theta / 2), 1e-3)
        else:
            d = R * (math.sqrt(2) + (1 - math.sqrt(2)) * (theta - math.pi / 2) / (math.pi / 2))
        circ.append(dict(base, C=b + bis * d, R=R, s=sg))

    # Merge consecutive same-direction circles that overlap / would make the road go
    # backwards (e.g. a hairpin drawn with two close 90 degree clicks).
    changed = True
    while changed and len(circ) > 2:
        changed = False
        m = len(circ)
        for a in (range(m) if cyclic else range(m - 1)):
            b = (a + 1) % m
            c1, c2 = circ[a], circ[b]
            if c1['s'] == 0 or c1['s'] != c2['s']:
                continue
            seg = P[c2['first']] - P[c1['last']]
            tg = belt_tangent(c1, c2)
            if tg is None or tg[2].dot(seg) <= 0 or (c2['C'] - c1['C']).length < 0.1:
                new = {'C': (c1['C'] + c2['C']) * 0.5, 'R': max(c1['R'], c2['R']), 's': c1['s'],
                       'first': c1['first'], 'last': c2['last'], 'rad': max(c1['rad'], c2['rad'])}
                circ[a] = new
                del circ[b]
                changed = True
                break
    return circ


def rounded_poly(P, rad, cyclic, s):
    """Turn a clicked polyline into a drivable path made of straights and arcs.
    Returns (dense points, dense width multipliers, issue locations)."""
    circ = corner_circles(P, rad, cyclic, s)
    m = len(circ)
    issues = []
    nseg = m if cyclic else m - 1
    tang = []
    for i in range(nseg):
        tg = belt_tangent(circ[i], circ[(i + 1) % m])
        if tg is None:
            return list(P), list(rad), issues
        if not tg[3]:
            issues.append((circ[i]['C'] + circ[(i + 1) % m]['C']) * 0.5)
        tang.append(tg)

    dense, drad = [], []

    def add_line(i, f0, f1):
        P1, P2, _, _ = tang[i]
        r0, r1 = circ[i]['rad'], circ[(i + 1) % m]['rad']
        steps = max(1, int((P2 - P1).length * (f1 - f0) / 0.1))
        for k in range(steps):
            f = f0 + (f1 - f0) * k / steps
            dense.append(P1.lerp(P2, f))
            drad.append(r0 * (1 - f) + r1 * f)

    def add_arc(i):
        c = circ[i]
        if c['s'] == 0 or c['R'] <= 0:
            return
        t_in, t_out = tang[i - 1][2], tang[i % nseg][2]
        ang = math.atan2(t_in.x * t_out.y - t_in.y * t_out.x, t_in.dot(t_out))
        if c['s'] * ang < 0:
            ang = 0.0 if abs(ang) < math.pi / 2 else ang + c['s'] * 2 * math.pi
        u0 = -_left(t_in) * c['s']
        steps = max(1, int(abs(ang) * c['R'] / 0.05))
        for k in range(steps + 1):
            dense.append(c['C'] + _rot(u0, ang * k / steps) * c['R'])
            drad.append(c['rad'])

    if cyclic:
        add_line(0, 0.5, 1.0)
        for i in range(1, m):
            add_arc(i)
            add_line(i, 0.0, 1.0)
        add_arc(0)
        add_line(0, 0.0, 0.5)
    else:
        add_line(0, 0.0, 1.0)
        for i in range(1, m - 1):
            add_arc(i)
            add_line(i, 0.0, 1.0)
        dense.append(tang[-1][1].copy())
        drad.append(circ[-1]['rad'])
    return dense, drad, issues


def sample_centerline(obj, step, s=None):
    """Return (points, radii, cyclic, ds) evenly spaced along the first spline,
    in world space and flattened to z = 0."""
    _state["corner_issues"] = []
    if obj is None or obj.type != 'CURVE' or len(obj.data.splines) == 0:
        return None
    spl = obj.data.splines[0]
    mw = obj.matrix_world
    cyclic = spl.use_cyclic_u
    dense, drad = [], []

    if spl.type == 'BEZIER':
        bps = spl.bezier_points
        n = len(bps)
        if n < 2:
            return None
        segs = n if cyclic else n - 1
        for i in range(segs):
            a, b = bps[i], bps[(i + 1) % n]
            p0, h0, h1, p1 = mw @ a.co, mw @ a.handle_right, mw @ b.handle_left, mw @ b.co
            est = (h0 - p0).length + (h1 - h0).length + (p1 - h1).length
            res = max(8, min(400, int(math.ceil(est / 0.1))))
            pts = interpolate_bezier(p0, h0, h1, p1, res + 1)
            for k in range(res):
                t = k / res
                dense.append(pts[k])
                drad.append(a.radius * (1 - t) + b.radius * t)
        if not cyclic:
            dense.append(mw @ bps[-1].co)
            drad.append(bps[-1].radius)
    else:  # POLY / NURBS -> treat control points as a polyline
        if len(spl.points) < 2:
            return None
        P, rr = [], []
        for p in spl.points:
            w = mw @ p.co.xyz
            v = Vector((w.x, w.y, 0.0))
            if P and (v - P[-1]).length < 1e-4:
                continue
            P.append(v)
            rr.append(p.radius)
        if cyclic and len(P) > 1 and (P[0] - P[-1]).length < 1e-4:
            P.pop()
            rr.pop()
        if len(P) < 2:
            return None
        if s is not None and s.auto_corners and (len(P) >= 3 or not cyclic):
            dense, drad, iss = rounded_poly(P, rr, cyclic, s)
            _state["corner_issues"] = iss
        else:
            dense, drad = P, rr

    # flatten & drop duplicates
    pts, rads = [], []
    for p, r in zip(dense, drad):
        q = Vector((p.x, p.y, 0.0))
        if pts and (q - pts[-1]).length < 1e-6:
            continue
        pts.append(q)
        rads.append(r)
    if len(pts) < 2:
        return None

    if cyclic:
        pts.append(pts[0].copy())
        rads.append(rads[0])
    cum = [0.0]
    for i in range(1, len(pts)):
        cum.append(cum[-1] + (pts[i] - pts[i - 1]).length)
    total = cum[-1]
    if total < 1e-4:
        return None

    count = max(8, int(round(total / max(step, 0.01))))
    ds = total / count
    targets = [i * ds for i in range(count if cyclic else count + 1)]
    out_p, out_r = [], []
    j = 0
    for s in targets:
        while j < len(cum) - 2 and cum[j + 1] < s:
            j += 1
        seg = cum[j + 1] - cum[j]
        t = 0.0 if seg < 1e-9 else min(1.0, max(0.0, (s - cum[j]) / seg))
        out_p.append(pts[j].lerp(pts[j + 1], t))
        out_r.append(rads[j] * (1 - t) + rads[j + 1] * t)
    return out_p, out_r, cyclic, ds


def compute_frames(pts, cyclic):
    n = len(pts)
    tans = []
    for i in range(n):
        if cyclic:
            a, b = pts[i - 1], pts[(i + 1) % n]
        else:
            a, b = pts[max(i - 1, 0)], pts[min(i + 1, n - 1)]
        t = b - a
        t.z = 0.0
        if t.length < 1e-9:
            t = Vector((1.0, 0.0, 0.0))
        t.normalize()
        tans.append(t)
    nors = [Vector((-t.y, t.x, 0.0)) for t in tans]  # left-hand side normal
    return tans, nors


def compute_curvature(tans, ds, cyclic, window=0.8):
    """Signed curvature (1/m), positive = turning left."""
    n = len(tans)
    k = max(1, int(round(window / ds / 2)))
    out = []
    for i in range(n):
        if cyclic:
            a, b = tans[(i - k) % n], tans[(i + k) % n]
            span = 2 * k * ds
        else:
            ia, ib = max(0, i - k), min(n - 1, i + k)
            a, b = tans[ia], tans[ib]
            span = max((ib - ia) * ds, 1e-6)
        ang = math.atan2(a.x * b.y - a.y * b.x, a.dot(b))
        out.append(ang / span)
    return out


def validate(pts, hws, curv, ds, cyclic, border, clearance, overlap=True):
    """Find corners too tight for the road+border and sections that overlap.
    With cell walls the overlap test is done on the shared walls instead (overlap=False)."""
    issues = {}
    n = len(pts)
    total = n * ds if cyclic else (n - 1) * ds

    def add(loc, kind, txt):
        key = (kind, round(loc.x / 1.5), round(loc.y / 1.5))
        if key not in issues:
            issues[key] = (loc.copy(), kind, txt)

    for i, c in enumerate(curv):
        need = hws[i] + border + clearance
        if abs(c) * need > 1.03:  # small tolerance for sampling noise
            add(pts[i], 'TIGHT', "Corner radius %.2f m < %.2f m" % (1.0 / abs(c), need))

    if not overlap:
        return list(issues.values())
    cell = 2 * max(hws) + 2 * border + clearance
    grid = {}
    for i, p in enumerate(pts):
        grid.setdefault((math.floor(p.x / cell), math.floor(p.y / cell)), []).append(i)
    for i, p in enumerate(pts):
        gx, gy = math.floor(p.x / cell), math.floor(p.y / cell)
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                for j in grid.get((gx + ox, gy + oy), ()):
                    if j <= i:
                        continue
                    darc = (j - i) * ds
                    if cyclic:
                        darc = min(darc, total - darc)
                    need = hws[i] + hws[j] + 2 * border + clearance
                    if darc < 1.6 * need:
                        continue
                    d = (pts[j] - p).length
                    if d < need:
                        add((p + pts[j]) * 0.5, 'OVERLAP',
                            "Sections %.2f m apart, need %.2f m" % (d, need))
    return list(issues.values())


def find_runs(flags, signs, cyclic):
    n = len(flags)
    if not any(flags):
        return []
    if all(flags):
        return [(list(range(n)), signs[0])]
    start = next(i for i in range(n) if not flags[i]) if cyclic else 0
    order = [(start + k) % n for k in range(n)] if cyclic else list(range(n))
    runs, cur, cs = [], [], 0
    for i in order:
        if flags[i] and cur and signs[i] == cs:
            cur.append(i)
        else:
            if cur:
                runs.append((cur, cs))
                cur = []
            if flags[i]:
                cur, cs = [i], signs[i]
    if cur:
        runs.append((cur, cs))
    return runs


def extend_run(idxs, ext, n, cyclic):
    if ext <= 0:
        return idxs
    if cyclic:
        ext = min(ext, max(0, (n - len(idxs)) // 2 - 1))
        before = [(idxs[0] - k) % n for k in range(ext, 0, -1)]
        after = [(idxs[-1] + k) % n for k in range(1, ext + 1)]
        return before + idxs + after
    lo = max(0, idxs[0] - ext)
    hi = min(n - 1, idxs[-1] + ext)
    return list(range(lo, hi + 1))


def sweep(pts, nors, hws, side, profile, idxs, closed, ds, tile, stripe=None, caps=True, v_off=0.0):
    """Sweep a closed 2D profile (dx outward from road edge, z) along the track.
    lateral offset = side * (half_width + dx)."""
    m = len(profile)
    cnt = len(idxs)
    per = [0.0]
    for k in range(m):
        a, b = profile[k], profile[(k + 1) % m]
        per.append(per[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    verts = []
    for i in idxs:
        p, N, hw = pts[i], nors[i], hws[i]
        for dx, z in profile:
            q = p + N * (side * (hw + dx))
            verts.append((q.x, q.y, z))
    faces, uvs, mats = [], [], []
    rings = cnt if closed else cnt - 1
    for a in range(rings):
        b = (a + 1) % cnt
        va, vb = (v_off + a * ds) / tile, (v_off + (a + 1) * ds) / tile
        mi = stripe(a) if stripe else 0
        for k in range(m):
            k2 = (k + 1) % m
            faces.append((a * m + k, b * m + k, b * m + k2, a * m + k2))
            uvs.append([(per[k] / tile, va), (per[k] / tile, vb),
                        (per[k + 1] / tile, vb), (per[k + 1] / tile, va)])
            mats.append(mi)
    if not closed and caps:
        for a, rev in ((0, True), (cnt - 1, False)):
            f = [a * m + k for k in range(m)]
            if rev:
                f.reverse()
            faces.append(tuple(f))
            uvs.append([(profile[i % m][0] / tile, profile[i % m][1] / tile) for i in f])
            mats.append(stripe(a) if stripe else 0)
    return verts, faces, uvs, mats


# =============================================================================
#  Cell walls (Voronoi-like shared walls)
#
#  Every ground cell is labelled with the closest part of the track (distance to the
#  road edge, and the arc-length of the nearest centerline point). The drivable area
#  is everything closer than `wall_distance` + half a wall to the road edge. Walls go
#  on the outer boundary of that area, and on the boundary between cells whose
#  nearest track parts are far apart along the track (two "flood fills" touching).
# =============================================================================

def _seg_field(np, ax, ay, bx, by, ha, hb, px, py):
    """Signed distance to the road edge of segment(s) A-B (with interpolated half width)."""
    dx, dy = bx - ax, by - ay
    L2 = np.maximum(dx * dx + dy * dy, 1e-12)
    rx, ry = px - ax, py - ay
    t = np.clip((rx * dx + ry * dy) / L2, 0.0, 1.0)
    qx, qy = rx - t * dx, ry - t * dy
    return np.sqrt(qx * qx + qy * qy) - (ha + (hb - ha) * t), t


def _douglas_peucker(P, tol):
    n = len(P)
    if n < 3:
        return P
    keep = [False] * n
    keep[0] = keep[-1] = True
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        ax, ay = P[a]
        bx, by = P[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy)
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            px, py = P[i]
            d = abs((px - ax) * dy - (py - ay) * dx) / L if L > 1e-12 else math.hypot(px - ax, py - ay)
            if d > best:
                best, bi = d, i
        if best > tol:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(P, keep) if k]


class CellWalls:
    """Distance/label field around the track and the wall polylines extracted from it."""

    def __init__(self, pts, hws, cyclic, s, border):
        import numpy as np
        self.np = np
        self.cyclic = cyclic
        P = np.array([(p.x, p.y) for p in pts], dtype=np.float64)
        Hw = np.array(hws, dtype=np.float64)
        if cyclic:
            A, B, hA, hB = P, np.roll(P, -1, 0), Hw, np.roll(Hw, -1)
        else:
            A, B, hA, hB = P[:-1], P[1:], Hw[:-1], Hw[1:]
        L = np.linalg.norm(B - A, axis=1)
        S0 = np.concatenate(([0.0], np.cumsum(L)[:-1]))
        self.total = float(L.sum())
        self.A, self.B, self.hA, self.hB, self.L, self.S0 = A, B, hA, hB, L, S0

        self.Tan = (B - A) / np.maximum(L, 1e-12)[:, None]
        self.bw = border
        self.W = s.wall_distance + 0.5 * border           # wall centerline, from road edge
        # Two cells belong to different parts of the track (-> shared wall between them)
        # when the track turned around between their nearest points (hairpins, V shapes:
        # the divider then runs up to the centre of curvature), or when they are further
        # apart along the track than any single bend inside the flood-fill could explain.
        self.cos_split = math.cos(s.wall_angle)
        self.T = 2.0 * math.pi * (float(Hw.max()) + self.W)
        h = max(s.wall_resolution, 0.005)
        R = float(Hw.max()) + self.W + 3 * h
        x0, y0 = P.min(0) - R
        x1, y1 = P.max(0) + R
        while (x1 - x0) * (y1 - y0) / (h * h) > 6e6:       # keep the grid reasonable
            h *= 1.25
        self.h = h
        nx = int(math.ceil((x1 - x0) / h)) + 1
        ny = int(math.ceil((y1 - y0) / h)) + 1
        self.x0, self.y0, self.nx, self.ny = float(x0), float(y0), nx, ny
        xs = x0 + np.arange(nx) * h
        ys = y0 + np.arange(ny) * h

        best = np.full((ny, nx), np.inf)
        seg = np.full((ny, nx), -1, dtype=np.int32)
        tpar = np.zeros((ny, nx))
        for k in range(len(A)):
            ax, ay = A[k]
            bx, by = B[k]
            rk = max(hA[k], hB[k]) + self.W + 3 * h
            j0 = max(0, int((min(ax, bx) - rk - x0) / h))
            j1 = min(nx, int((max(ax, bx) + rk - x0) / h) + 2)
            i0 = max(0, int((min(ay, by) - rk - y0) / h))
            i1 = min(ny, int((max(ay, by) + rk - y0) / h) + 2)
            if j1 <= j0 or i1 <= i0:
                continue
            f, t = _seg_field(np, ax, ay, bx, by, hA[k], hB[k], xs[None, j0:j1], ys[i0:i1, None])
            sub = best[i0:i1, j0:j1]
            m = f < sub
            sub[m] = f[m]
            seg[i0:i1, j0:j1][m] = k
            tpar[i0:i1, j0:j1][m] = t[m]
        self.best, self.seg = best, seg
        self.label = np.where(seg >= 0, S0[np.maximum(seg, 0)] + tpar * L[np.maximum(seg, 0)], 0.0)
        self.inside = best <= self.W

    # -- helpers ------------------------------------------------------------------
    def arc_sep(self, a, b):
        d = self.np.abs(a - b)
        return self.np.minimum(d, self.total - d) if self.cyclic else d

    def far(self, la, lb, ka, kb):
        np = self.np
        ta, tb = self.Tan[ka], self.Tan[kb]
        cos = ta[..., 0] * tb[..., 0] + ta[..., 1] * tb[..., 1]
        return (self.arc_sep(la, lb) > self.T) | (cos < self.cos_split)

    def field_of(self, k, px, py):
        np = self.np
        return _seg_field(np, self.A[k, 0], self.A[k, 1], self.B[k, 0], self.B[k, 1],
                          self.hA[k], self.hB[k], px, py)[0]

    def cell_at(self, x, y):
        j = int(round((x - self.x0) / self.h))
        i = int(round((y - self.y0) / self.h))
        if 0 <= i < self.ny and 0 <= j < self.nx:
            return i, j
        return None

    def extent(self, p, N, max_d):
        """Free distance from p along +N before leaving the area or entering another part."""
        c0 = self.cell_at(p.x, p.y)
        if c0 is None or self.seg[c0] < 0:
            return 0.0
        s_here, k_here = self.label[c0], self.seg[c0]
        d = 0.0
        while d < max_d:
            q = p + N * (d + self.h)
            c = self.cell_at(q.x, q.y)
            if c is None or not self.inside[c]:
                break
            if self.far(self.label[c], s_here, self.seg[c], k_here):
                break
            d += self.h
        return d

    # -- wall extraction ------------------------------------------------------------
    def extract(self, smooth_iters, clearance):
        np = self.np
        h, W = self.h, self.W
        best, ins, lab, seg = self.best, self.inside, self.label, self.seg
        X = lambda j: self.x0 + j * h
        Y = lambda i: self.y0 + i * h
        nodes = []          # (x, y)
        issues = []
        seen_issue = set()

        def crossings(fa, fb, ia, ib, la, lb, sa, sb, ax, ay, bx, by):
            """Returns (mask, t, wall_clearance) for a batch of grid edges."""
            outer = ia != ib
            with np.errstate(invalid='ignore', divide='ignore'):
                t_out = np.clip((W - fa) / (fb - fa), 0.0, 1.0)
            shared = ia & ib & self.far(la, lb, sa, sb)
            t = np.where(outer, t_out, 0.0)
            clr = np.full(fa.shape, np.inf)
            idx = np.nonzero(shared & ~outer)[0]
            if len(idx):
                ga = fa[idx] - self.field_of(sb[idx], ax[idx], ay[idx])   # <= 0
                gb = self.field_of(sa[idx], bx[idx], by[idx]) - fb[idx]   # >= 0
                ts = np.clip(-ga / np.maximum(gb - ga, 1e-12), 0.0, 1.0)
                t[idx] = ts
                fA_b = self.field_of(sa[idx], bx[idx], by[idx])
                clr[idx] = fa[idx] + ts * (fA_b - fa[idx])
            return outer | shared, t, clr

        def edge_nodes(a_sl, b_sl, horizontal):
            fa, fb = best[a_sl], best[b_sl]
            ii, jj = np.nonzero(np.ones(fa.shape, bool))
            shape = fa.shape
            fa, fb = fa.ravel(), fb.ravel()
            ia, ib = ins[a_sl].ravel(), ins[b_sl].ravel()
            la, lb = lab[a_sl].ravel(), lab[b_sl].ravel()
            sa, sb = np.maximum(seg[a_sl].ravel(), 0), np.maximum(seg[b_sl].ravel(), 0)
            ax, ay = X(jj).astype(float), Y(ii).astype(float)
            bx, by = (ax + h, ay) if horizontal else (ax, ay + h)
            m, t, clr = crossings(fa, fb, ia, ib, la, lb, sa, sb, ax, ay, bx, by)
            ids = np.full(shape, -1, dtype=np.int64)
            for e in np.nonzero(m)[0]:
                x = ax[e] + (t[e] * h if horizontal else 0.0)
                y = ay[e] + (0.0 if horizontal else t[e] * h)
                ids.flat[e] = len(nodes)
                nodes.append((float(x), float(y)))
                if clr[e] < 0.5 * self.bw + clearance:
                    key = (round(x / 1.5), round(y / 1.5))
                    if key not in seen_issue:
                        seen_issue.add(key)
                        issues.append((Vector((x, y, 0.0)), 'OVERLAP',
                                       "Shared wall only %.2f m from the road" % max(clr[e], 0.0)))
            return ids

        Hid = edge_nodes((slice(None), slice(0, -1)), (slice(None), slice(1, None)), True)
        Vid = edge_nodes((slice(0, -1), slice(None)), (slice(1, None), slice(None)), False)
        sq = np.stack([Hid[:-1, :], Vid[:, 1:], Hid[1:, :], Vid[:, :-1]], axis=-1)
        cnt = (sq >= 0).sum(-1)
        adj = {}

        def link(a, b):
            adj.setdefault(a, []).append(b)
            adj.setdefault(b, []).append(a)

        for i, j in zip(*np.nonzero(cnt >= 2)):
            ids = [int(v) for v in sq[i, j] if v >= 0]
            if len(ids) == 2:
                link(ids[0], ids[1])
            else:
                c = len(nodes)
                nodes.append((X(j) + 0.5 * h, Y(i) + 0.5 * h))
                for v in ids:
                    link(c, v)

        # ---- walk chains between junctions / ends, then loops ----
        used = set()
        chains = []

        def walk(a, b):
            ch = [a, b]
            used.add((a, b))
            used.add((b, a))
            while len(adj[b]) == 2:
                nb = adj[b][0] if adj[b][1] == a else adj[b][1]
                if (b, nb) in used:
                    break
                used.add((b, nb))
                used.add((nb, b))
                a, b = b, nb
                ch.append(b)
            return ch

        for v, nb in adj.items():
            if len(nb) != 2:
                for w in nb:
                    if (v, w) not in used:
                        chains.append((walk(v, w), False))
        for v, nb in adj.items():
            for w in nb:
                if (v, w) not in used:
                    ch = walk(v, w)
                    closed = ch[-1] == ch[0]
                    chains.append((ch[:-1] if closed else ch, closed))

        out = []
        for ch, closed in chains:
            Pn = np.array([nodes[v] for v in ch])
            if len(Pn) < 2 or (not closed and np.linalg.norm(Pn[-1] - Pn[0]) < 1e-6 and len(Pn) < 3):
                continue
            for _ in range(smooth_iters):
                if closed and len(Pn) >= 3:
                    Pn = 0.5 * Pn + 0.25 * (np.roll(Pn, 1, 0) + np.roll(Pn, -1, 0))
                elif len(Pn) >= 3:
                    Pn[1:-1] = 0.5 * Pn[1:-1] + 0.25 * (Pn[:-2] + Pn[2:])
            pl = [tuple(p) for p in Pn]
            if closed:
                pl = _douglas_peucker(pl + [pl[0]], 0.004)[:-1]
                if len(pl) < 3:
                    continue
            else:
                pl = _douglas_peucker(pl, 0.004)
                # push ends that sit on a junction outward so the walls overlap there
                for end, inner in ((0, 1), (-1, -2)):
                    v = ch[0] if end == 0 else ch[-1]
                    if len(adj[v]) >= 3 and len(pl) >= 2:
                        ex, ey = pl[end][0] - pl[inner][0], pl[end][1] - pl[inner][1]
                        el = math.hypot(ex, ey)
                        if el > 1e-9:
                            k = 0.5 * self.bw / el
                            pl[end] = (pl[end][0] + ex * k, pl[end][1] + ey * k)
            if self.polyline_length(pl, closed) < max(2 * h, 0.02):
                continue
            out.append((pl, closed))
        return out, issues

    @staticmethod
    def polyline_length(pl, closed):
        n = len(pl)
        m = n if closed else n - 1
        return sum(math.hypot(pl[(i + 1) % n][0] - pl[i][0], pl[(i + 1) % n][1] - pl[i][1])
                   for i in range(m))


def sweep_polyline(pl, closed, profile, shift, tile, caps=(True, True), v_off=0.0):
    """Sweep a closed 2D profile (dx, z) along a 2D polyline, offset = dx - shift
    to the left of the direction of travel."""
    n = len(pl)
    m = len(profile)
    per = [0.0]
    for k in range(m):
        a, b = profile[k], profile[(k + 1) % m]
        per.append(per[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    dirs = []
    seglen = n if closed else n - 1
    for i in range(seglen):
        a, b = pl[i], pl[(i + 1) % n]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1e-9
        dirs.append((dx / L, dy / L, L))
    verts, vcoord = [], []
    acc = v_off
    for i in range(n):
        if closed:
            d0, d1 = dirs[i - 1], dirs[i % seglen]
        else:
            d0 = dirs[max(i - 1, 0)]
            d1 = dirs[min(i, seglen - 1)]
        tx, ty = d0[0] + d1[0], d0[1] + d1[1]
        tl = math.hypot(tx, ty)
        if tl < 1e-6:
            tx, ty, tl = d1[0], d1[1], 1.0
        tx, ty = tx / tl, ty / tl
        nxv, nyv = -ty, tx
        miter = 1.0 / max(nxv * -d1[1] + nyv * d1[0], 0.5)
        x, y = pl[i]
        for dx, z in profile:
            o = (dx - shift) * miter
            verts.append((x + nxv * o, y + nyv * o, z))
        if i > 0:
            acc += dirs[i - 1][2]
        vcoord.append(acc)
    if closed:
        vcoord.append(acc + dirs[-1][2])
    faces, uvs = [], []
    rings = n if closed else n - 1
    for a in range(rings):
        b = (a + 1) % n
        va, vb = vcoord[a] / tile, vcoord[a + 1] / tile
        for k in range(m):
            k2 = (k + 1) % m
            faces.append((a * m + k2, b * m + k2, b * m + k, a * m + k))
            uvs.append([(per[k + 1] / tile, va), (per[k + 1] / tile, vb),
                        (per[k] / tile, vb), (per[k] / tile, va)])
    if not closed:
        for a, rev, want in ((0, False, caps[0]), (n - 1, True, caps[1])):
            if not want:
                continue
            f = [a * m + k for k in range(m)]
            if rev:
                f.reverse()
            faces.append(tuple(f))
            uvs.append([(profile[i % m][0] / tile, profile[i % m][1] / tile) for i in f])
    return verts, faces, uvs


def disc_mesh(cx, cy, z0, r, h, seg, r_top=0.0125):
    """Marker disc (track disc): thin vertical rim, then a shallow cone up to a small flat
    top of radius r_top at the centre.
    Returns verts, faces, per-face uvs (top-view planar) and per-face smooth flags."""
    rim = min(0.003, 0.3 * h)
    r_top = min(max(r_top, 0.001), 0.9 * r)
    rings = [(r, z0), (r, z0 + rim), (r_top, z0 + h)]
    verts = []
    for rr, z in rings:
        for k in range(seg):
            a = 2 * math.pi * k / seg
            verts.append((cx + rr * math.cos(a), cy + rr * math.sin(a), z))
    faces, sm = [], []
    for ring in range(len(rings) - 1):
        for k in range(seg):
            k2 = (k + 1) % seg
            faces.append((ring * seg + k, ring * seg + k2, (ring + 1) * seg + k2, (ring + 1) * seg + k))
            sm.append(1 if ring == 0 else 2)       # rim and cone: separate smoothing groups
    faces.append(tuple(reversed(range(seg))))                      # bottom
    faces.append(tuple(range(2 * seg, 3 * seg)))                   # top
    sm += [False, False]
    d = 2.0 * r
    uvs = [[(0.5 + (verts[i][0] - cx) / d, 0.5 + (verts[i][1] - cy) / d) for i in f] for f in faces]
    return verts, faces, uvs, sm


def border_profile(s):
    if s.border_type == 'ROPE':
        r = s.border_width * 0.5
        return [(r + r * math.cos(a), r + r * math.sin(a))
                for a in [-math.pi / 2 + 2 * math.pi * k / 8 for k in range(8)]]
    bw, bh = s.border_width, s.border_height
    return [(0.0, 0.0), (bw, 0.0), (bw, bh), (0.0, bh)]


def track_frame(s):
    data = sample_centerline(s.centerline, s.step, s)
    if data is None:
        return None
    pts, rads, cyclic, ds = data
    tans, nors = compute_frames(pts, cyclic)
    hws = [0.5 * s.track_width * max(r, 0.05) for r in rads]
    return pts, tans, nors, hws, cyclic, ds


# =============================================================================
#  Track build
# =============================================================================

def build_track(scene):
    s = scene.rc_track
    obj = s.centerline
    if obj is None or obj.type != 'CURVE':
        return False, "Set a centerline curve first"
    if obj.mode == 'EDIT':
        try:
            obj.update_from_editmode()
        except Exception:
            pass
        _state["ignore_until"] = time.time() + 0.3

    fr = track_frame(s)
    if fr is None:
        return False, "Centerline needs at least 2 points"
    # bring existing generated materials up to date (e.g. jumps, which are not rebuilt here)
    for mname in MAT_COLORS:
        if mname in bpy.data.materials:
            get_material(mname)
    pts, tans, nors, hws, cyclic, ds = fr
    n = len(pts)
    curv = compute_curvature(tans, ds, cyclic)
    tile = max(s.uv_tile, 0.01)
    rz = s.road_z
    total = n * ds if cyclic else (n - 1) * ds
    tcoll = get_collection(COLL_TRACK, scene)

    used = set()
    limit = max(16, s.max_verts - 1)          # strictly fewer than max_verts
    nseg = n if cyclic else n - 1
    rcoll = get_collection(COLL_TRACK + "_Road", scene, tcoll)
    bcoll = get_collection(COLL_TRACK + "_Borders", scene, tcoll)
    kcoll = get_collection(COLL_TRACK + "_Curbs", scene, tcoll)

    # Sections follow the checkpoint spacing (same boundaries as RC_CP_xxx)
    n_sections = max(1, int(total / max(s.checkpoint_spacing, 1.0)))
    bounds = sorted(set([int(round(k * nseg / n_sections)) for k in range(n_sections)] + [nseg]))

    def chunks(per_ring):
        """Split every section into sub-chunks of at most `limit` (exported) vertices."""
        max_segs = max(1, limit // per_ring - 1)
        out = []
        for sec, (a, b) in enumerate(zip(bounds[:-1], bounds[1:])):
            L = b - a
            parts = max(1, math.ceil(L / max_segs))
            for p in range(parts):
                s0 = a + int(round(L * p / parts))
                s1 = a + int(round(L * (p + 1) / parts))
                if s1 > s0:
                    out.append((sec, p, s0, s1))
        return out

    def ring_idx(s0, s1):
        return [i % n for i in range(s0, s1 + 1)]

    # ---- Road surface (smooth shaded: 2 exported verts per ring) -------------
    xs, ys = [], []
    road_mat = carpet_material("RC_Road")
    for sec, part, s0, s1 in chunks(2):
        idxs = ring_idx(s0, s1)
        verts, faces, uvs = [], [], []
        for i in idxs:
            l = pts[i] + nors[i] * hws[i]
            r = pts[i] - nors[i] * hws[i]
            verts += [(l.x, l.y, rz), (r.x, r.y, rz)]
        for a in range(len(idxs) - 1):
            face = (2 * a, 2 * a + 1, 2 * a + 3, 2 * a + 2)
            faces.append(face)
            # top-down planar projection (same as the floor): seamless across chunks
            uvs.append([(verts[k][0] / tile, verts[k][1] / tile) for k in face])
        name = "RC_Road_S%02d_%d" % (sec, part)
        o = get_mesh_object(name, rcoll)
        write_mesh(o, verts, faces, uvs, None, [road_mat], 'UP', smooth=True)
        o["section"] = sec
        used.add(name)
        xs += [v[0] for v in verts]
        ys += [v[1] for v in verts]

    # ---- Borders (battens / rope), one object per side per chunk ------------
    border = 0.0
    cells = None
    cell_issues = []
    if s.wall_mode == 'CELLS':
        try:
            t0 = time.time()
            cells = CellWalls(pts, hws, cyclic, s, s.border_width if s.border_type != 'NONE' else 0.0)
        except ImportError:
            print("RC Track: numpy not available, falling back to road-following borders")
    if s.border_type != 'NONE':
        border = s.border_width
        prof = border_profile(s)
        rope = s.border_type == 'ROPE'
        per_ring = (len(prof) + 1) if rope else 2 * len(prof)   # smooth+seam / flat
        mat = get_material("RC_Rope" if rope else "RC_Border")
    if cells is not None and s.border_type != 'NONE':
        walls, cell_issues = cells.extract(s.wall_smooth, s.clearance)
        max_rings = max(2, (limit - 2 * per_ring) // per_ring)
        k = 0
        for pl, closed in walls:
            npt = len(pl)
            rings = npt + 1 if closed else npt
            if rings <= max_rings:
                parts = [(list(range(npt)), closed, (True, True), 0.0)]
            else:
                # split into open pieces sharing one ring; caps only at the real chain ends
                seq = list(range(npt)) + ([0] if closed else [])
                L = len(seq) - 1
                cnt = math.ceil(L / (max_rings - 1))
                parts, voff = [], 0.0
                for p in range(cnt):
                    a0 = int(round(L * p / cnt))
                    a1 = int(round(L * (p + 1) / cnt))
                    sub_idx = seq[a0:a1 + 1]
                    caps = (False, False) if closed else (p == 0, p == cnt - 1)
                    parts.append((sub_idx, False, caps, voff))
                    voff += CellWalls.polyline_length([pl[i] for i in sub_idx], False)
            for idx, cl, caps, voff in parts:
                sub = [pl[i] for i in idx]
                v, f, u = sweep_polyline(sub, cl, prof, 0.5 * border, tile, caps=caps, v_off=voff)
                name = "RC_Wall_%03d" % k
                o = get_mesh_object(name, bcoll)
                write_mesh(o, v, f, u, None, [mat], 'RECALC', smooth=rope)
                o["wall_chain"] = k
                used.add(name)
                k += 1
        print("RC Track: cell walls %d chain(s), %d mesh(es), grid %dx%d @ %.3f m, %.2f s"
              % (len(walls), k, cells.nx, cells.ny, cells.h, time.time() - t0))
    elif s.border_type != 'NONE':
        for sec, part, s0, s1 in chunks(per_ring):
            idxs = ring_idx(s0, s1)
            for side, tag in ((1, "L"), (-1, "R")):
                v, f, u, m = sweep(pts, nors, hws, side, prof, idxs, False, ds, tile,
                                   caps=False, v_off=s0 * ds)
                name = "RC_Border%s_S%02d_%d" % (tag, sec, part)
                o = get_mesh_object(name, bcoll)
                write_mesh(o, v, f, u, None, [mat], 'RECALC', smooth=rope)
                o["section"] = sec
                used.add(name)

    # ---- Curbs (vibreurs): one object per curb, split if too many verts -----
    if s.curbs:
        thr = 1.0 / max(s.curb_radius, 0.01)
        flags = [abs(c) > thr for c in curv]
        signs = [1 if c > 0 else -1 for c in curv]
        cw, ch = s.curb_width, s.curb_height
        cprof = [(-cw, 0.0), (0.0, 0.0), (0.0, ch), (-cw * 0.7, ch)]
        ext = int(round(s.curb_extend / ds))
        stripe_len = max(s.curb_stripe, 0.01)
        # flat shading + stripe material splits: ~16 exported verts per ring, +8 for caps
        max_rings = max(2, (limit - 8) // 16)
        curb_mats = [get_material("RC_CurbRed"), get_material("RC_CurbWhite")]
        discs = s.curb_style == 'DISCS'
        if discs:
            dr = 0.5 * s.disc_diameter
            dh = s.disc_height
            dseg = s.disc_segments
            # only direct neighbours may overlap, so two alternating heights avoid z-fighting
            dsp = max(s.disc_diameter + s.disc_gap, 0.5 * s.disc_diameter + 0.005)
            per_disc = 6 * dseg                      # exported verts: 2 caps + rim + cone
            max_discs = max(1, limit // per_disc)
            disc_mat = [get_material("RC_Disc")]
            all_discs = []
        k = 0
        for g, (idxs, sg) in enumerate(find_runs(flags, signs, cyclic)):
            idxs = extend_run(idxs, ext, n, cyclic)
            if len(idxs) < 2:
                continue
            if discs:
                # Path of the disc centres, on the inside of the corner: the outer disc edge
                # sits on the inner limit of the track (+ overhang). With cell walls that is
                # the inner wall face minus clearance (smoothed: the field is a grid);
                # otherwise the road edge, where the batten starts.
                # The path is much shorter than the centerline in tight corners, so discs
                # are spaced by their real centre-to-centre distance on it.
                if cells is not None:
                    lim = []
                    for i in idxs:
                        e = cells.extent(pts[i], nors[i] * sg, hws[i] + cells.W)
                        e -= 0.5 * border + s.clearance
                        lim.append(min(max(e, hws[i]), hws[i] + s.wall_distance))
                    w = 3
                    lim = [sum(lim[max(0, a - w):a + w + 1]) / len(lim[max(0, a - w):a + w + 1])
                           for a in range(len(lim))]
                else:
                    lim = [hws[i] for i in idxs]
                path = [pts[i] + nors[i] * (sg * (e - dr + s.disc_offset)) for i, e in zip(idxs, lim)]
                cum = [0.0]
                for a in range(1, len(path)):
                    cum.append(cum[-1] + (path[a] - path[a - 1]).length)

                def at(u):
                    a = 0
                    while a < len(cum) - 2 and cum[a + 1] < u:
                        a += 1
                    seg_l = cum[a + 1] - cum[a]
                    t = 0.0 if seg_l < 1e-9 else min(1.0, max(0.0, (u - cum[a]) / seg_l))
                    return path[a].lerp(path[a + 1], t)

                def place(u0):
                    us, last = [u0], at(u0)
                    u = u0
                    while True:
                        u += 0.01
                        if u > cum[-1]:
                            return us
                        q = at(u)
                        if (q - last).length >= dsp:
                            lo, hi = u - 0.01, u          # refine to the exact distance
                            for _ in range(8):
                                mid = 0.5 * (lo + hi)
                                if (at(mid) - last).length >= dsp:
                                    hi = mid
                                else:
                                    lo = mid
                            u = hi
                            us.append(u)
                            last = at(u)
                us = place(0.0)
                us = place(0.5 * (cum[-1] - us[-1]))      # centre the row on the run
                centers = []
                for c, u in enumerate(us):
                    q = at(u)
                    # neighbouring runs (curb extend) may reach the same spot: skip duplicates
                    if any((q.x - x) ** 2 + (q.y - y) ** 2 < (dsp - 0.005) ** 2 for x, y in all_discs):
                        continue
                    centers.append((q.x, q.y, 0.001 * (len(centers) % 2)))
                all_discs += [(x, y) for x, y, _ in centers]
                cnt = len(centers)
                if cnt == 0:
                    continue
                for p0 in range(0, cnt, max_discs):
                    v, f, u, sm = [], [], [], []
                    for cx, cy, z0 in centers[p0:p0 + max_discs]:
                        dv, df, du, dsm = disc_mesh(cx, cy, z0, dr, dh, dseg,
                                                    0.5 * s.disc_top_diameter)
                        o_ = len(v)
                        v += dv
                        f += [tuple(i + o_ for i in face) for face in df]
                        u += du
                        sm += dsm
                    name = "RC_Curb_%03d" % k
                    o = get_mesh_object(name, kcoll)
                    write_mesh(o, v, f, u, None, disc_mat, 'RECALC', smooth=sm)
                    o["curb_group"] = g
                    used.add(name)
                    k += 1
                continue
            nparts = max(1, math.ceil((len(idxs) - 1) / (max_rings - 1)))
            L = len(idxs) - 1
            for p in range(nparts):
                a0 = int(round(L * p / nparts))
                a1 = int(round(L * (p + 1) / nparts))
                sub = idxs[a0:a1 + 1]
                if len(sub) < 2:
                    continue
                v, f, u, m = sweep(pts, nors, hws, sg, cprof, sub, False, ds, tile,
                                   stripe=lambda a, o=a0: int((a + o) * ds / stripe_len) % 2,
                                   v_off=a0 * ds)
                name = "RC_Curb_%03d" % k
                o = get_mesh_object(name, kcoll)
                write_mesh(o, v, f, u, m, curb_mats, 'RECALC')
                o["curb_group"] = g
                used.add(name)
                k += 1

    # remove chunks that are no longer needed (and legacy single objects)
    for name in _object_names(tcoll):
        if name.startswith(("RC_Road", "RC_Border", "RC_Curb", "RC_Wall")) and name not in used:
            remove_object(name)

    # ---- Start line ----------------------------------------------------------
    if s.start_line:
        p, T, N, hw = pts[0], tans[0], nors[0], hws[0]
        z = rz + 0.001
        c = [p + N * hw - T * 0.05, p - N * hw - T * 0.05, p - N * hw + T * 0.05, p + N * hw + T * 0.05]
        sl = get_mesh_object("RC_StartLine", tcoll)
        write_mesh(sl, [(q.x, q.y, z) for q in c], [(0, 1, 2, 3)],
                   [[(0, 0), (1, 0), (1, 1), (0, 1)]], None, [get_material("RC_CurbWhite")], 'UP')
    else:
        remove_object("RC_StartLine")

    # ---- Floor -----------------------------------------------------------------
    if s.floor:
        mg = s.floor_margin + border + (cells.W + 0.5 * border if cells is not None else 0.0)
        x0, x1, y0, y1 = min(xs) - mg, max(xs) + mg, min(ys) - mg, max(ys) + mg
        fv = [(x0, y0, 0.0), (x1, y0, 0.0), (x1, y1, 0.0), (x0, y1, 0.0)]
        fu = [[(x / tile, y / tile) for x, y, _ in fv]]
        fl = get_mesh_object("RC_Floor", tcoll)
        write_mesh(fl, fv, [(0, 1, 2, 3)], fu, None, [carpet_material("RC_Floor")], 'UP')
    else:
        remove_object("RC_Floor")

    # ---- Checkpoints (for lap counting in the game engine) ----------------------
    ccoll = get_collection(COLL_CHECKPOINTS, scene)
    clear_collection(ccoll)
    if s.checkpoints:
        count = max(1, int(total / max(s.checkpoint_spacing, 1.0)))
        for k in range(count):
            i = int(round(k * total / count / ds)) % n
            e = bpy.data.objects.new("RC_CP_%03d" % k, None)
            e.empty_display_type = 'CUBE'
            if cells is not None:
                # span the whole cell: from wall to wall across the road at this point
                mx = hws[i] + cells.W
                el = cells.extent(pts[i], nors[i], mx)
                er = cells.extent(pts[i], -nors[i], mx)
                c = pts[i] + nors[i] * (0.5 * (el - er))
                half = 0.5 * (el + er) + 0.5 * border
            else:
                c, half = pts[i], hws[i] + border
            e.location = (c.x, c.y, 0.5)
            e.rotation_euler = (0.0, 0.0, math.atan2(tans[i].y, tans[i].x))
            e.scale = (0.05, half, 0.5)
            e["checkpoint_index"] = k
            e["is_start_finish"] = (k == 0)
            ccoll.objects.link(e)
    ccoll.hide_render = True

    # ---- Validation ------------------------------------------------------------
    corner_issues = list(_state.get("corner_issues", []))
    issues = validate(pts, hws, curv, ds, cyclic, border, s.clearance, overlap=cells is None)
    issues += cell_issues
    for loc in corner_issues:
        issues.append((loc, 'CORNER', "Opposite corners too close together - move the points apart"))
    icoll = get_collection(COLL_ISSUES, scene)
    clear_collection(icoll)
    for k, (loc, kind, txt) in enumerate(issues[:300]):
        e = bpy.data.objects.new("RC_Issue_%03d_%s" % (k, kind), None)
        e.empty_display_type = 'SPHERE'
        e.empty_display_size = 0.6
        e.location = (loc.x, loc.y, 0.3)
        e["issue"] = txt
        icoll.objects.link(e)
    icoll.hide_render = True

    # final step: every generated mesh gets its pivot at the centre of its bounds
    for name in _object_names(tcoll):
        o = bpy.data.objects.get(name)
        if o is not None and o.type == 'MESH':
            recenter_origin(o)

    apply_display(scene)
    refresh_jump_uvs(scene)

    radii =[1.0 / abs(c) for c in curv if abs(c) > 1e-9]
    s.stat_length = total
    s.stat_min_radius = min(radii) if radii else 9999.0
    s.stat_issues = len(issues)
    s.stat_closed = cyclic

    msg = "Track built: %.1f m, min radius %.2f m, %d issue(s)" % (total, s.stat_min_radius, len(issues))
    if not cyclic:
        msg += " - WARNING: centerline is not closed (Alt+C in edit mode)"
    return True, msg


# =============================================================================
#  Auto update (depsgraph handler + debounce timer)
# =============================================================================

SETTINGS_KEYS = ("track_width", "step", "auto_corners", "corner_radius", "max_verts", "border_type", "border_width", "border_height",
                 "curbs", "curb_radius", "curb_width", "curb_height", "curb_extend",
                 "curb_stripe", "curb_style", "disc_diameter", "disc_height", "disc_top_diameter", "disc_gap",
                 "disc_offset", "disc_segments", "clearance", "floor", "floor_margin", "road_z", "uv_tile",
                 "start_line", "checkpoints", "checkpoint_spacing",
                 "wall_mode", "wall_distance", "wall_angle", "wall_resolution", "wall_smooth")


def curve_signature(obj):
    vals = [tuple(round(x, 5) for row in obj.matrix_world for x in row)]
    for spl in obj.data.splines:
        vals.append((spl.type, spl.use_cyclic_u))
        if spl.type == 'BEZIER':
            for bp in spl.bezier_points:
                vals.append((tuple(bp.co), tuple(bp.handle_left), tuple(bp.handle_right), bp.radius))
        else:
            for p in spl.points:
                vals.append((tuple(p.co), p.radius))
    return hash(tuple(vals))


def full_signature(s):
    return (curve_signature(s.centerline), tuple(getattr(s, k) for k in SETTINGS_KEYS))


def _schedule(scene):
    _state["last"] = time.time()
    _state["scene"] = scene.name
    if not _state["pending"]:
        _state["pending"] = True
        bpy.app.timers.register(_timer, first_interval=0.3)


def _timer():
    if time.time() - _state["last"] < 0.3:
        return 0.1
    _state["pending"] = False
    scene = bpy.data.scenes.get(_state.get("scene") or "")
    if scene is None:
        return None
    s = scene.rc_track
    obj = s.centerline
    if not s.auto_update or obj is None:
        return None
    if obj.mode == 'EDIT':
        try:
            obj.update_from_editmode()
        except Exception:
            pass
        _state["ignore_until"] = time.time() + 0.3
    try:
        sig = full_signature(s)
        if sig == _state.get("sig"):
            return None
        _state["sig"] = sig
        build_track(scene)
    except Exception as e:
        print("RC Track auto-update failed:", e)
    return None


@persistent
def rc_track_depsgraph_handler(scene, depsgraph):
    # moved jumps: their world-space UVs must follow (transform updates only, so the
    # UV write itself - a geometry update - does not retrigger this)
    for u in depsgraph.updates:
        idd = u.id
        if isinstance(idd, bpy.types.Object) and u.is_updated_transform:
            ob = bpy.data.objects.get(idd.name)
            if ob is not None and getattr(ob, "rc_jump", None) is not None and ob.rc_jump.is_jump:
                _schedule_jump_uvs(scene)
                break
    if time.time() < _state.get("ignore_until", 0.0):
        return
    s = getattr(scene, "rc_track", None)
    if s is None or not s.auto_update or s.centerline is None:
        return
    obj = s.centerline
    dname = obj.data.name
    for u in depsgraph.updates:
        idd = u.id
        if (isinstance(idd, bpy.types.Object) and idd.name == obj.name) or \
           (isinstance(idd, bpy.types.Curve) and idd.name == dname):
            _schedule(scene)
            return


def apply_display(scene):
    """Show generated track as wireframe / solid / hidden so the drawing stays visible."""
    s = scene.rc_track
    coll = bpy.data.collections.get(COLL_TRACK)
    if coll is None:
        return
    for o in coll.all_objects:
        if o is None:          # stale entry right after chunks were removed
            continue
        o.display_type = 'WIRE' if s.track_display == 'WIRE' else 'TEXTURED'
        o.hide_viewport = (s.track_display == 'HIDDEN')


def _display_update(self, context):
    scene = self.id_data
    if isinstance(scene, bpy.types.Scene):
        apply_display(scene)


def _settings_update(self, context):
    scene = self.id_data
    if isinstance(scene, bpy.types.Scene) and self.auto_update and self.centerline:
        _schedule(scene)


# =============================================================================
#  Jumps
# =============================================================================

JUMP_TYPES = [
    ('TABLETOP', "Tabletop", "Take-off ramp, flat top, landing ramp"),
    ('KICKER', "Kicker", "Single launch ramp with a vertical back"),
    ('DOUBLE', "Double", "Two ramps separated by a gap"),
    ('BUMP', "Bump", "Single rounded bump"),
    ('WHOOPS', "Whoops", "Series of small rounded bumps"),
]


def _ramp(x0, z0, x1, z1, segs, ease):
    out = []
    for k in range(segs + 1):
        t = k / segs
        if ease == 'SMOOTH':
            e = t * t * (3 - 2 * t)
        elif ease == 'IN':
            e = t * t if z1 > z0 else 1 - (1 - t) * (1 - t)
        else:
            e = t
        out.append((x0 + (x1 - x0) * t, z0 + (z1 - z0) * e))
    return out


def _bump(x0, length, h, segs):
    return [(x0 + length * k / segs, h * 0.5 * (1 - math.cos(2 * math.pi * k / segs)))
            for k in range(segs + 1)]


def jump_profiles(j):
    """List of closed-to-ground XZ profiles (x along track, centered on 0)."""
    h = j.height
    sg = j.segments if j.smooth else 1
    ease = 'SMOOTH' if j.smooth else 'LINEAR'
    t = j.jump_type
    if t == 'KICKER':
        L = j.up_len
        x0 = -L / 2
        return [_ramp(x0, 0.0, x0 + L, h, sg, 'IN' if j.smooth else 'LINEAR') + [(x0 + L, 0.0)]]
    if t == 'TABLETOP':
        L = j.up_len + j.top_len + j.down_len
        x0 = -L / 2
        a = _ramp(x0, 0.0, x0 + j.up_len, h, sg, ease)
        b = _ramp(x0 + j.up_len + j.top_len, h, x0 + L, 0.0, sg, ease)
        if j.top_len < 1e-4:
            b = b[1:]
        return [a + b]
    if t == 'DOUBLE':
        back = min(h, j.up_len)  # ~45 degree inner faces
        top = j.top_len * 0.5
        L = j.up_len + top + back + j.gap + back + top + j.down_len
        x = -L / 2
        A = _ramp(x, 0.0, x + j.up_len, h, sg, ease)
        x += j.up_len + top
        bk = _ramp(x, h, x + back, 0.0, 1, 'LINEAR')
        A += bk if top > 1e-4 else bk[1:]
        x += back + j.gap
        B = _ramp(x, 0.0, x + back, h, 1, 'LINEAR')
        x += back + top
        ld = _ramp(x, h, x + j.down_len, 0.0, sg, ease)
        B += ld if top > 1e-4 else ld[1:]
        return [A, B]
    segs = max(sg, 8)
    if t == 'BUMP':
        return [_bump(-j.bump_len / 2, j.bump_len, h, segs)]
    if t == 'WHOOPS':
        L = j.count * j.bump_len
        return [_bump(-L / 2 + i * j.bump_len, j.bump_len, h, segs) for i in range(j.count)]
    return []


def build_jump_mesh(obj):
    j = obj.rc_jump
    w = j.width * 0.5
    acc = MeshAcc()
    for prof in jump_profiles(j):
        clean = [prof[0]]
        for p in prof[1:]:
            if math.hypot(p[0] - clean[-1][0], p[1] - clean[-1][1]) > 1e-6:
                clean.append(p)
        prof = clean
        m = len(prof)
        if m < 3:
            continue
        v = [(x, -w, z) for x, z in prof] + [(x, w, z) for x, z in prof]
        f, u = [], []
        along = [0.0]
        for k in range(1, m):
            along.append(along[-1] + math.hypot(prof[k][0] - prof[k - 1][0], prof[k][1] - prof[k - 1][1]))
        for k in range(m - 1):  # riding surface
            f.append((k, k + 1, m + k + 1, m + k))
            u.append([(along[k], 0.0), (along[k + 1], 0.0), (along[k + 1], 2 * w), (along[k], 2 * w)])
        f.append(tuple(range(m)))                                   # side -Y
        u.append([(x, z) for x, z in prof])
        f.append(tuple(m + k for k in reversed(range(m))))          # side +Y
        u.append([(prof[k][0], prof[k][1]) for k in reversed(range(m))])
        f.append((0, m, 2 * m - 1, m - 1))                          # bottom
        u.append([(prof[0][0], 0.0), (prof[0][0], 2 * w), (prof[-1][0], 2 * w), (prof[-1][0], 0.0)])
        acc.add(v, f, u, [0] * len(f))
        if j.tape:
            for tv, tf, tu in _tape_strips(prof, w, j.tape_width):
                acc.add(tv, tf, tu, [1] * len(tf))
    write_mesh(obj, acc.v, acc.f, acc.u, acc.m,
               [carpet_material("RC_Road"), get_material("RC_Tape")], 'RECALC')
    jump_world_uvs(obj)


def _tape_keys(prof, along):
    """Arc positions along a jump profile that get duct tape: ground contacts, the lip at the
    top of every climb, the edge where every descent starts, and any sharp corner."""
    m = len(prof)
    ang = [math.atan2(prof[k + 1][1] - prof[k][1], prof[k + 1][0] - prof[k][0]) for k in range(m - 1)]
    flat = math.radians(5.0)
    keys = [0.0]
    for i in range(1, m - 1):
        a0, a1 = ang[i - 1], ang[i]
        lip = a0 > flat and a1 <= flat                  # end of a climb
        edge = a0 >= -flat and a1 < -flat               # start of a descent
        kink = abs(a1 - a0) > math.radians(25.0)
        if lip or edge or kink:
            keys.append(along[i])
    keys.append(along[-1])
    out = []
    for s in sorted(keys):                              # merge keys closer than 3 cm
        if not out or s - out[-1] > 0.03:
            out.append(s)
    return out


def _tape_strips(prof, w, width):
    """Thin closed slabs of tape (0.3-2 mm above the surface) wrapping the profile around each
    key edge, across the whole jump width. Returns [(verts, faces, uvs)]."""
    m = len(prof)
    along = [0.0]
    for k in range(1, m):
        along.append(along[-1] + math.hypot(prof[k][0] - prof[k - 1][0], prof[k][1] - prof[k - 1][1]))
    total = along[-1]
    if total < 1e-6:
        return []

    def seg_normal(k):
        dx, dz = prof[k + 1][0] - prof[k][0], prof[k + 1][1] - prof[k][1]
        L = math.hypot(dx, dz) or 1.0
        return (-dz / L, dx / L)                        # left of the travel direction = outward

    def point(s, at_vertex=None):
        if at_vertex is not None:
            k = at_vertex
            na = seg_normal(max(k - 1, 0))
            nb = seg_normal(min(k, m - 2))
            nx, nz = na[0] + nb[0], na[1] + nb[1]
            L = math.hypot(nx, nz) or 1.0
            return prof[k][0], prof[k][1], nx / L, nz / L
        k = 0
        while k < m - 2 and along[k + 1] < s:
            k += 1
        seg = along[k + 1] - along[k]
        t = 0.0 if seg < 1e-9 else min(1.0, max(0.0, (s - along[k]) / seg))
        x = prof[k][0] + (prof[k + 1][0] - prof[k][0]) * t
        z = prof[k][1] + (prof[k + 1][1] - prof[k][1]) * t
        n = seg_normal(k)
        return x, z, n[0], n[1]

    lo_off, hi_off = 0.0003, 0.002
    out = []
    for s in _tape_keys(prof, along):
        a, b = max(0.0, s - width / 2), min(total, s + width / 2)
        if b - a < 1e-4:
            continue
        samples = [point(a)]
        for k in range(m):
            if a + 1e-6 < along[k] < b - 1e-6:
                samples.append(point(along[k], at_vertex=k))
        samples.append(point(b))
        v, f, u = [], [], []
        for x, z, nx, nz in samples:
            for off in (lo_off, hi_off):
                px, pz = x + nx * off, z + nz * off
                v += [(px, -w, pz), (px, w, pz)]       # 4 verts per sample: lo-, lo+, hi-, hi+
        n = len(samples)
        for i in range(n - 1):
            a0, b0 = 4 * i, 4 * (i + 1)
            f.append((a0 + 2, b0 + 2, b0 + 3, a0 + 3))   # top
            f.append((a0 + 0, a0 + 1, b0 + 1, b0 + 0))   # bottom
            f.append((a0 + 0, b0 + 0, b0 + 2, a0 + 2))   # side -Y
            f.append((a0 + 1, a0 + 3, b0 + 3, b0 + 1))   # side +Y
        last = 4 * (n - 1)
        f.append((0, 2, 3, 1))                            # end caps
        f.append((last, last + 1, last + 3, last + 2))
        u = [[(0.0, 0.0)] * len(face) for face in f]      # world UVs are set afterwards
        out.append((v, f, u))
    return out


def jump_world_uvs(obj, tile=None):
    """World-space UVs so the road carpet continues seamlessly over the jump:
    top-down (x, y) on faces looking mostly up/down, side projection (horizontal, z)
    on steep faces so they are not smeared."""
    if obj is None or obj.type != 'MESH' or not obj.data.uv_layers:
        return
    if tile is None:
        tile = max(bpy.context.scene.rc_track.uv_tile, 0.01)
    me = obj.data
    M = obj.matrix_world
    R = M.to_3x3()
    uvl = me.uv_layers.active.data
    for poly in me.polygons:
        n = (R @ poly.normal).normalized()
        for li in poly.loop_indices:
            p = M @ me.vertices[me.loops[li].vertex_index].co
            if abs(n.z) >= 0.5:
                uv = (p.x, p.y)
            elif abs(n.x) > abs(n.y):
                uv = (p.y, p.z)
            else:
                uv = (p.x, p.z)
            uvl[li].uv = (uv[0] / tile, uv[1] / tile)
    me.update()


def refresh_jump_uvs(scene):
    coll = bpy.data.collections.get(COLL_JUMPS)
    if coll is None:
        return
    # Snapshot the NAMES first: rebuilding a mesh creates/removes datablocks, which invalidates
    # the collection's all_objects cache mid-iteration (stale entries, and a hard crash).
    names = [o.name for o in coll.all_objects if o is not None]
    for name in names:
        o = bpy.data.objects.get(name)
        if o is not None and o.type == 'MESH' and getattr(o, "rc_jump", None) is not None and o.rc_jump.is_jump:
            # full rebuild (a few dozen faces): keeps materials, tape and world UVs current
            build_jump_mesh(o)


_jstate = {"pending": False, "scene": None, "last": 0.0}


def _jump_uv_timer():
    if time.time() - _jstate["last"] < 0.2:
        return 0.1
    _jstate["pending"] = False
    scene = bpy.data.scenes.get(_jstate.get("scene") or "")
    if scene is not None:
        try:
            refresh_jump_uvs(scene)
        except Exception as e:
            print("RC Track: jump UV refresh failed:", e)
    return None


def _schedule_jump_uvs(scene):
    _jstate["last"] = time.time()
    _jstate["scene"] = scene.name
    if not _jstate["pending"]:
        _jstate["pending"] = True
        bpy.app.timers.register(_jump_uv_timer, first_interval=0.2)


def _jump_update(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object) and obj.type == 'MESH' and self.is_jump:
        build_jump_mesh(obj)


def _jump_reverse_update(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object):
        obj.rotation_euler.z += math.pi


def snap_object(obj, frame, set_width=True):
    pts, tans, nors, hws, cyclic, ds = frame
    loc = obj.matrix_world.translation
    best = min(range(len(pts)), key=lambda i: (pts[i].x - loc.x) ** 2 + (pts[i].y - loc.y) ** 2)
    T = tans[best]
    d = Vector((loc.x, loc.y, 0.0)) - pts[best]
    p = pts[best] + T * d.dot(T)
    ang = math.atan2(T.y, T.x)
    is_jump = getattr(obj, "rc_jump", None) is not None and obj.rc_jump.is_jump
    if is_jump and obj.rc_jump.reverse:
        ang += math.pi
    obj.location = (p.x, p.y, 0.0 if is_jump else obj.location.z)
    obj.rotation_mode = 'XYZ'
    obj.rotation_euler = (0.0, 0.0, ang)
    if is_jump and set_width:
        obj.rc_jump.width = 2 * hws[best]
    if is_jump:
        bpy.context.view_layer.update()
        jump_world_uvs(obj)


# =============================================================================
#  Gymnasium
# =============================================================================
#  All gym meshes are children of the empty RC_Gym_Root: move / rotate that empty
#  to place the hall; size is driven by the gym settings (length / width / height).
#  Local frame: origin = floor centre, X = hall length, Y = hall width, Z = up.

def _box(x0, y0, z0, x1, y1, z1, tile=1.0):
    x0, x1 = min(x0, x1), max(x0, x1)
    y0, y1 = min(y0, y1), max(y0, y1)
    z0, z1 = min(z0, z1), max(z0, z1)
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    axes = ('z', 'z', 'y', 'x', 'y', 'x')
    uvs = []
    for face, ax in zip(f, axes):
        uv = []
        for i in face:
            x, y, z = v[i]
            uv.append((x / tile, y / tile) if ax == 'z' else
                      (x / tile, z / tile) if ax == 'y' else (y / tile, z / tile))
        uvs.append(uv)
    return (v, f, uvs, [0] * 6)


def _xform(pc, M):
    v, f, u, m = pc
    return ([tuple(M @ Vector(p)) for p in v], f, u, m)


def _setmat(pc, idx):
    v, f, u, m = pc
    return (v, f, u, [idx] * len(f))


def _strip_piece(points, w, z):
    """Flat line of width w following an open 2D polyline."""
    n = len(points)
    v = []
    for i in range(n):
        a, b = points[max(i - 1, 0)], points[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / L * w / 2, dx / L * w / 2
        v.append((points[i][0] + nx, points[i][1] + ny, z))
        v.append((points[i][0] - nx, points[i][1] - ny, z))
    f = [(2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2) for i in range(n - 1)]
    u = [[(0, 0), (1, 0), (1, 1), (0, 1)] for _ in f]
    return (v, f, u, [0] * len(f))


def _arc(cx, cy, r, a0, a1, step=0.15):
    k = max(2, int(abs(a1 - a0) * r / step))
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / k), cy + r * math.sin(a0 + (a1 - a0) * i / k))
            for i in range(k + 1)]


def gym_court_lines(g):
    """Polylines per colour, roughly matching a school sports hall."""
    L, W = g.length, g.width
    out = {'WHITE': [], 'BLUE': [], 'RED': [], 'BLACK': []}

    def seg(key, p0, p1):
        out[key].append([p0, p1])

    def rect(key, lx, ly, cx=0.0, cy=0.0):
        x0, x1, y0, y1 = cx - lx / 2, cx + lx / 2, cy - ly / 2, cy + ly / 2
        seg(key, (x0, y0), (x1, y0))
        seg(key, (x1, y0), (x1, y1))
        seg(key, (x1, y1), (x0, y1))
        seg(key, (x0, y1), (x0, y0))

    # Handball (red): boundary, centre line, 6 m goal areas
    Lh, Wh = min(40.0, L - 1.0), min(20.0, W - 1.0)
    if Lh > 4 and Wh > 4:
        rect('RED', Lh, Wh)
        seg('RED', (0, -Wh / 2), (0, Wh / 2))
        r = min(6.0, Wh / 2 - 1.6, Lh / 2 - 1.0)
        if r > 1.0:
            for sx in (-1, 1):
                gx = sx * Lh / 2
                a_in = math.pi if sx > 0 else 0.0
                for sy in (1, -1):
                    out['RED'].append(_arc(gx, sy * 1.5, r, a_in, a_in + sy * (-sx) * math.pi / 2))
                seg('RED', (gx - sx * r, -1.5), (gx - sx * r, 1.5))

    # Basketball (blue)
    Lb, Wb = min(28.0, L - 2.0), min(15.0, W - 2.0)
    if Lb > 8 and Wb > 6:
        rect('BLUE', Lb, Wb)
        seg('BLUE', (0, -Wb / 2), (0, Wb / 2))
        out['BLUE'].append(_arc(0, 0, 1.8, 0, 2 * math.pi))
        for sx in (-1, 1):
            bx = sx * Lb / 2
            fx = bx - sx * 5.8
            seg('BLUE', (bx, 2.45), (fx, 2.45))
            seg('BLUE', (bx, -2.45), (fx, -2.45))
            seg('BLUE', (fx, -2.45), (fx, 2.45))
            out['BLUE'].append(_arc(fx, 0, 1.8, 0, 2 * math.pi))
            hx = bx - sx * 1.575
            R = 6.75
            th = math.asin(min(1.0, (Wb / 2 - 0.9) / R))
            ac = math.pi if sx > 0 else 0.0
            out['BLUE'].append(_arc(hx, 0, R, ac - th, ac + th))
            ex = hx - sx * R * math.cos(th)
            ys = R * math.sin(th)
            seg('BLUE', (bx, ys), (ex, ys))
            seg('BLUE', (bx, -ys), (ex, -ys))

    # Volleyball (white)
    Lv, Wv = min(18.0, L - 2.0), min(9.0, W - 2.0)
    if Lv > 6 and Wv > 4:
        rect('WHITE', Lv, Wv)
        seg('WHITE', (0, -Wv / 2), (0, Wv / 2))
        for sx in (-1, 1):
            seg('WHITE', (sx * 3.0, -Wv / 2), (sx * 3.0, Wv / 2))

    # Badminton (black), courts across the hall
    if W >= 14.4 and L >= 7.0:
        centres = [-L / 4, L / 4] if L >= 16.0 else [0.0]
        for cx in centres:
            rect('BLACK', 6.1, 13.4, cx)
            for sx in (-1, 1):
                seg('BLACK', (cx + sx * 2.59, -6.7), (cx + sx * 2.59, 6.7))
            for sy in (-1, 1):
                seg('BLACK', (cx - 3.05, sy * 1.98), (cx + 3.05, sy * 1.98))
                seg('BLACK', (cx - 3.05, sy * 5.94), (cx + 3.05, sy * 5.94))
                seg('BLACK', (cx, sy * 1.98), (cx, sy * 6.7))
    return out


def _hoop_pieces(d):
    """Basketball hoop with the wall at x=0 facing +X. Materials: 0 board, 1 metal, 2 ring."""
    pcs = [_setmat(_box(d, -0.9, 2.9, d + 0.03, 0.9, 3.95), 0),
           _setmat(_box(0.0, -0.05, 3.2, d, 0.05, 3.3), 1)]
    r, cx, cz = 0.225, d + 0.03 + 0.15 + 0.225, 3.05
    pcs.append(_setmat(_box(d + 0.03, -0.02, cz - 0.02, cx - r, 0.02, cz + 0.02), 1))
    segs = 10
    prof = [(-0.01, -0.01), (0.01, -0.01), (0.01, 0.01), (-0.01, 0.01)]
    v, f, u = [], [], []
    for k in range(segs):
        a = 2 * math.pi * k / segs
        ca, sa = math.cos(a), math.sin(a)
        for dr, dz in prof:
            v.append((cx + (r + dr) * ca, (r + dr) * sa, cz + dz))
    for k in range(segs):
        k2 = (k + 1) % segs
        for j in range(4):
            j2 = (j + 1) % 4
            f.append((k * 4 + j, k2 * 4 + j, k2 * 4 + j2, k * 4 + j2))
            u.append([(0, 0), (1, 0), (1, 1), (0, 1)])
    pcs.append((v, f, u, [2] * len(f)))
    return pcs


def _handball_goal_pieces(depth=0.9):
    """Handball goal (3 x 2 m inside) with its back against a wall at x=0, mouth facing +X.
    Materials: 0 red, 1 white (20 cm post bands), 2 metal frame, 3 net."""
    hw, hh, p = 1.5, 2.0, 0.08          # inner half width, inner height, post section
    x1 = depth                          # front face of the posts
    x0 = x1 - p
    pcs = []
    band = 0.2
    # posts: 20 cm red/white bands, red at the ground
    for sy in (-1, 1):
        ya, yb = sy * hw, sy * (hw + p)
        nb = int(math.ceil((hh + p) / band))
        for k in range(nb):
            za, zb = k * band, min((k + 1) * band, hh + p)
            pcs.append(_setmat(_box(x0, ya, za, x1, yb, zb), k % 2))
    # crossbar: bands mirrored from the centre (red at both corners like the posts' tops)
    span = 2 * (hw + p)
    nb = int(math.ceil(span / band))
    for k in range(nb):
        ya = -(hw + p) + k * band
        yb = min(ya + band, hw + p)
        pcs.append(_setmat(_box(x0, ya, hh, x1, yb, hh + p), (k + int(math.ceil((hh + p) / band))) % 2))
    # metal frame: ground rails to the wall, back uprights, top rails, back rails
    r = 0.015
    for sy in (-1, 1):
        y = sy * (hw + p / 2)
        pcs.append(_setmat(_box(0.0, y - r, 0.0, x0, y + r, 2 * r), 2))            # ground rail
        pcs.append(_setmat(_box(0.0, y - r, hh + p - 2 * r, x0, y + r, hh + p), 2))  # top rail
        pcs.append(_setmat(_box(0.0, y - r, 0.0, 2 * r, y + r, hh + p), 2))         # back upright
    pcs.append(_setmat(_box(0.0, -hw - p, 0.0, 2 * r, hw + p, 2 * r), 2))           # back bottom
    pcs.append(_setmat(_box(0.0, -hw - p, hh + p - 2 * r, 2 * r, hw + p, hh + p), 2))  # back top
    # net panels (thin, translucent)
    n = 0.004
    pcs.append(_setmat(_box(2 * r, -hw, 0.0, 2 * r + n, hw, hh), 3))                # back
    pcs.append(_setmat(_box(2 * r, -hw, hh - n, x0, hw, hh), 3))                     # roof
    for sy in (-1, 1):
        pcs.append(_setmat(_box(2 * r, sy * hw, 0.0, x0, sy * (hw - n), hh), 3))    # sides
    return pcs


def _bench_pieces(length=3.5):
    h = length / 2
    return [_box(-h, -0.13, 0.30, h, 0.13, 0.34),                 # seat
            _box(-h + 0.2, -0.03, 0.12, h - 0.2, 0.03, 0.30),     # beam
            _box(-h + 0.15, -0.15, 0.0, -h + 0.27, 0.15, 0.30),   # legs
            _box(h - 0.27, -0.15, 0.0, h - 0.15, 0.15, 0.30)]


def _parent_to(o, root):
    o.parent = root
    o.matrix_parent_inverse = Matrix.Identity(4)
    o.matrix_basis = Matrix.Identity(4)


def pack_pieces(prefix, coll, root, pieces, mats, limit, used, smooth=False, normals='RECALC'):
    """Pack small pieces into as few objects as possible, each under the vertex limit
    (estimated as exported by a game engine: flat faces don't share vertices)."""
    groups, cur, cnt = [], [], 0
    for pc in pieces:
        est = len(pc[0]) if smooth else sum(len(f) for f in pc[1])
        if cur and cnt + est > limit:
            groups.append(cur)
            cur, cnt = [], 0
        cur.append(pc)
        cnt += est
    if cur:
        groups.append(cur)
    for gi, grp in enumerate(groups):
        acc = MeshAcc()
        for v, f, u, m in grp:
            acc.add(v, f, u, m)
        name = "%s_%02d" % (prefix, gi)
        o = get_mesh_object(name, coll)
        _parent_to(o, root)
        write_mesh(o, acc.v, acc.f, acc.u, acc.m, mats, normals, smooth=smooth)
        used.add(name)


def _get_light_object(name, ltype, coll, root):
    """Reuse / create a light object (and its own light data) of the given type."""
    o = bpy.data.objects.get(name)
    if o is not None and (o.type != 'LIGHT' or o.data.type != ltype):
        bpy.data.objects.remove(o, do_unlink=True)
        o = None
    if o is None:
        ld = bpy.data.lights.get(name)
        if ld is None or ld.type != ltype:
            ld = bpy.data.lights.new(name, ltype)
        o = bpy.data.objects.new(name, ld)
    if coll.objects.get(o.name) is None:
        coll.objects.link(o)
    _parent_to(o, root)
    return o


def apply_gym_display(scene):
    g = scene.rc_gym
    coll = bpy.data.collections.get(COLL_GYM)
    if coll is None:
        return
    for o in coll.all_objects:
        if o.type != 'MESH':
            continue
        o.display_type = 'WIRE' if g.display == 'WIRE' else 'TEXTURED'
        hidden = g.display == 'HIDDEN' or (o.name.startswith("RC_Gym_Ceil") and not g.show_ceiling)
        o.hide_viewport = hidden


def build_gym(scene):
    g = scene.rc_gym
    coll = get_collection(COLL_GYM, scene)
    ccoll = get_collection(COLL_GYM_CEIL, scene, coll)
    root = bpy.data.objects.get(GYM_ROOT)
    if root is not None and root.type != 'EMPTY':
        bpy.data.objects.remove(root, do_unlink=True)
        root = None
    if root is None:
        root = bpy.data.objects.new(GYM_ROOT, None)
        root.empty_display_type = 'PLAIN_AXES'
        root.empty_display_size = 2.0
        coll.objects.link(root)
    limit = max(16, scene.rc_track.max_verts - 1)
    used = set()
    L, W, H, t = g.length, g.width, g.height, g.wall_thickness
    M = get_material

    # Floor + court lines
    fv = [(-L / 2, -W / 2, -0.02), (L / 2, -W / 2, -0.02), (L / 2, W / 2, -0.02), (-L / 2, W / 2, -0.02)]
    pack_pieces("RC_Gym_Floor", coll, root,
                [(fv, [(0, 1, 2, 3)], [[(x, y) for x, y, _ in fv]], [0])],
                [M("RC_GymFloor")], limit, used, smooth=True, normals='UP')
    if g.lines:
        zs = {'BLACK': -0.0195, 'RED': -0.019, 'BLUE': -0.0185, 'WHITE': -0.018}
        names = {'WHITE': "RC_GymLineWhite", 'BLUE': "RC_GymLineBlue",
                 'RED': "RC_GymLineRed", 'BLACK': "RC_GymLineBlack"}
        for key, polys in gym_court_lines(g).items():
            pcs = []
            for poly in polys:
                for a in range(0, len(poly) - 1, 50):
                    chunk = poly[a:a + 51]
                    if len(chunk) >= 2:
                        pcs.append(_strip_piece(chunk, g.line_width, zs[key]))
            if pcs:
                pack_pieces("RC_Gym_Lines_%s" % key.title(), coll, root, pcs, [M(names[key])],
                            limit, used, smooth=True, normals='UP')

    # Walls
    wall_mats = [M("RC_GymWall"), M("RC_GymFrame")]
    pack_pieces("RC_Gym_Wall_W", coll, root, [_box(-L / 2 - t, -W / 2 - t, 0, -L / 2, W / 2 + t, H)],
                wall_mats, limit, used)
    pack_pieces("RC_Gym_Wall_E", coll, root, [_box(L / 2, -W / 2 - t, 0, L / 2 + t, W / 2 + t, H)],
                wall_mats, limit, used)
    for side, tag in ((-1, "S"), (1, "N")):
        y_in, y_out = side * W / 2, side * (W / 2 + t)
        has_win = (g.window_side == 'POS_Y' and side > 0) or (g.window_side == 'NEG_Y' and side < 0) \
            or g.window_side == 'BOTH'
        wb = min(max(0.2, g.window_bottom), H - 0.4)
        wt = min(max(wb + 0.2, g.window_top), H - 0.05)
        if not has_win:
            pack_pieces("RC_Gym_Wall_" + tag, coll, root, [_box(-L / 2, y_in, 0, L / 2, y_out, H)],
                        wall_mats, limit, used)
            continue
        pcs = [_box(-L / 2, y_in, 0, L / 2, y_out, wb), _box(-L / 2, y_in, wt, L / 2, y_out, H)]
        npanes = max(1, int(round(L / max(g.pane_width, 0.5))))
        for j in range(npanes + 1):
            x = -L / 2 + j * L / npanes
            x0, x1 = max(-L / 2, x - 0.06), min(L / 2, x + 0.06)
            pcs.append(_setmat(_box(x0, y_in, wb, x1, y_out, wt), 1))
        pack_pieces("RC_Gym_Wall_" + tag, coll, root, pcs, wall_mats, limit, used)
        ym = (y_in + y_out) / 2
        pack_pieces("RC_Gym_Glass_" + tag, coll, root, [_box(-L / 2, ym - 0.01, wb, L / 2, ym + 0.01, wt)],
                    [M("RC_GymGlass")], limit, used)

    # Doors (double door on the +X end wall)
    if g.doors:
        y0 = W / 4
        pack_pieces("RC_Gym_Door", coll, root,
                    [_box(L / 2 - 0.03, y0 - 1.0, 0.0, L / 2, y0 + 1.0, 2.2)],
                    [M("RC_GymDoor")], limit, used)

    # Basketball hoops
    if g.hoops or g.side_hoops:
        hoop_mats = [M("RC_GymBoard"), M("RC_GymMetal"), M("RC_GymRing")]
        mounts = []
        Lb = min(28.0, L - 2.0)
        if g.hoops:
            d = max(0.3, L / 2 - Lb / 2 + 1.2)
            mounts.append(Matrix.Translation((-L / 2, 0, 0)))
            mounts.append(Matrix.Translation((L / 2, 0, 0)) @ Matrix.Rotation(math.pi, 4, 'Z'))
        if g.side_hoops:
            for x in (-L / 4, L / 4):
                mounts.append(Matrix.Translation((x, -W / 2, 0)) @ Matrix.Rotation(math.pi / 2, 4, 'Z'))
                mounts.append(Matrix.Translation((x, W / 2, 0)) @ Matrix.Rotation(-math.pi / 2, 4, 'Z'))
        for k, Mx in enumerate(mounts):
            end = g.hoops and k < 2
            dd = d if end else 0.6
            pack_pieces("RC_Gym_Hoop_%02d" % k, coll, root,
                        [_xform(pc, Mx) for pc in _hoop_pieces(dd)], hoop_mats, limit, used)

    # Handball goals against the end walls (the ones without benches), mouth to the court
    if g.handball_goals:
        goal_mats = [M("RC_GymLineRed"), M("RC_GymLineWhite"), M("RC_GymMetal"), M("RC_GymNet")]
        ends = (Matrix.Translation((-L / 2, 0, 0)),
                Matrix.Translation((L / 2, 0, 0)) @ Matrix.Rotation(math.pi, 4, 'Z'))
        for k, Mx in enumerate(ends):
            pack_pieces("RC_Gym_Goal_%02d" % k, coll, root,
                        [_xform(pc, Mx) for pc in _handball_goal_pieces()], goal_mats, limit, used)

    # Benches along the long walls
    if g.benches and g.benches_per_wall > 0:
        n = g.benches_per_wall
        blen = min(3.5, L / n - 0.5)
        k = 0
        if blen > 0.8:
            for side in (-1, 1):
                for i in range(n):
                    x = -L / 2 + (i + 0.5) * L / n
                    Mx = Matrix.Translation((x, side * (W / 2 - 0.3), 0))
                    pack_pieces("RC_Gym_Bench_%02d" % k, coll, root,
                                [_xform(pc, Mx) for pc in _bench_pieces(blen)],
                                [M("RC_GymWood")], limit, used)
                    k += 1

    # Ceiling, beams and lights
    if g.ceiling:
        pack_pieces("RC_Gym_Ceiling", ccoll, root,
                    [_box(-L / 2 - t, -W / 2 - t, H, L / 2 + t, W / 2 + t, H + 0.15)],
                    [M("RC_GymCeiling")], limit, used)
        nb = max(1, int(L / max(g.beam_spacing, 1.0)))
        beams, lights = [], []
        for i in range(nb + 1):
            x = -L / 2 + i * L / nb
            if 0 < i < nb:
                beams.append(_box(x - 0.12, -W / 2, H - 0.6, x + 0.12, W / 2, H))
            if i < nb:
                xm = x + L / nb / 2
                for yy in (-W / 4, W / 4):
                    lights.append(_box(xm - 0.6, yy - 0.08, H - 0.08, xm + 0.6, yy + 0.08, H))
        if beams:
            pack_pieces("RC_Gym_Ceil_Beams", ccoll, root, beams, [M("RC_GymBeam")], limit, used)
        if lights:
            pack_pieces("RC_Gym_Ceil_Lights", ccoll, root, lights, [M("RC_GymLight")], limit, used)
        # one point light just under every fixture
        if g.lamps:
            lcoll = get_collection(COLL_GYM_LIGHTS, scene, coll)
            for k, (v, _f, _u, _m) in enumerate(lights):
                cx = sum(p[0] for p in v) / len(v)
                cy = sum(p[1] for p in v) / len(v)
                zmin = min(p[2] for p in v)
                o = _get_light_object("RC_Gym_Lamp_%02d" % k, 'POINT', lcoll, root)
                o.data.energy = g.lamp_power
                o.data.color = (1.0, 0.97, 0.90)
                o.data.shadow_soft_size = 0.15
                o.matrix_basis = Matrix.Translation((cx, cy, zmin - 0.1))
                used.add(o.name)

    # Sun: a directional light coming in through the window band
    if g.sun:
        side = -1 if g.window_side == 'NEG_Y' else 1          # BOTH / NONE: from +Y
        el, az = g.sun_elevation, g.sun_azimuth
        # direction of travel: from outside the window wall, inwards (towards -side Y) and down
        d = Vector((math.sin(az) * math.cos(el), -side * math.cos(az) * math.cos(el), -math.sin(el)))
        o = _get_light_object("RC_Gym_Sun", 'SUN', get_collection(COLL_GYM_LIGHTS, scene, coll), root)
        o.data.energy = g.sun_strength
        o.data.color = (1.0, 0.96, 0.88)
        o.data.angle = math.radians(0.53)
        rot = d.to_track_quat('-Z', 'Y').to_matrix().to_4x4()
        # position is irrelevant for a sun: put it outside the windows, where it comes from
        o.matrix_basis = Matrix.Translation((0.0, side * (W / 2 + 4.0), H + 3.0)) @ rot
        used.add(o.name)

    for name in _object_names(coll):
        o = bpy.data.objects.get(name)
        if o is not None and o.type in ('MESH', 'LIGHT') and name.startswith("RC_Gym_") and name not in used:
            remove_object(name)
    # final step: pivots at the centre of each mesh (local to RC_Gym_Root)
    for name in _object_names(coll):
        o = bpy.data.objects.get(name)
        if o is not None and o.type == 'MESH' and name in used:
            recenter_origin(o)
    apply_gym_display(scene)
    return root


def _gym_timer():
    if time.time() - _gstate["last"] < 0.15:
        return 0.05
    _gstate["pending"] = False
    scene = bpy.data.scenes.get(_gstate.get("scene") or "")
    if scene is not None and bpy.data.objects.get(GYM_ROOT) is not None:
        try:
            build_gym(scene)
        except Exception as e:
            print("RC Track: gym rebuild failed:", e)
    return None


def _gym_update(self, context):
    scene = self.id_data
    if not isinstance(scene, bpy.types.Scene):
        return
    _gstate["scene"] = scene.name
    _gstate["last"] = time.time()
    if not _gstate["pending"]:
        _gstate["pending"] = True
        bpy.app.timers.register(_gym_timer, first_interval=0.15)


def _gym_display_update(self, context):
    scene = self.id_data
    if isinstance(scene, bpy.types.Scene):
        apply_gym_display(scene)


# =============================================================================
#  Property groups
# =============================================================================

class RCGymSettings(PropertyGroup):
    length: FloatProperty(name="Length", default=28.0, min=4.0, unit='LENGTH', update=_gym_update)
    width: FloatProperty(name="Width", default=16.0, min=4.0, unit='LENGTH', update=_gym_update)
    height: FloatProperty(name="Height", default=7.0, min=2.5, unit='LENGTH', update=_gym_update)
    wall_thickness: FloatProperty(name="Wall thickness", default=0.25, min=0.02, unit='LENGTH',
                                  update=_gym_update)
    window_side: EnumProperty(name="Windows", default='POS_Y', update=_gym_update, items=[
        ('POS_Y', "+Y wall", "Window band on the +Y long wall"),
        ('NEG_Y', "-Y wall", "Window band on the -Y long wall"),
        ('BOTH', "Both", "Window bands on both long walls"),
        ('NONE', "None", "No windows"),
    ])
    window_bottom: FloatProperty(name="Window bottom", default=4.8, min=0.2, unit='LENGTH',
                                 update=_gym_update)
    window_top: FloatProperty(name="Window top", default=6.7, min=0.5, unit='LENGTH', update=_gym_update)
    pane_width: FloatProperty(name="Pane width", default=2.2, min=0.5, unit='LENGTH', update=_gym_update)
    lines: BoolProperty(name="Court lines", default=True, update=_gym_update)
    line_width: FloatProperty(name="Line width", default=0.05, min=0.01, unit='LENGTH', update=_gym_update)
    hoops: BoolProperty(name="End wall hoops", default=True, update=_gym_update)
    side_hoops: BoolProperty(name="Side wall hoops", default=True, update=_gym_update)
    benches: BoolProperty(name="Benches", default=True, update=_gym_update)
    benches_per_wall: IntProperty(name="Benches per wall", default=4, min=0, max=20, update=_gym_update)
    doors: BoolProperty(name="Doors", default=True, update=_gym_update)
    handball_goals: BoolProperty(name="Handball goals", default=True, update=_gym_update,
                                 description="A handball goal against each end wall "
                                             "(the walls without benches)")
    ceiling: BoolProperty(name="Ceiling", default=True, update=_gym_update)
    beam_spacing: FloatProperty(name="Beam spacing", default=4.0, min=1.0, unit='LENGTH',
                                update=_gym_update)
    lamps: BoolProperty(name="Point lights", default=True, update=_gym_update,
                        description="A point light just under every ceiling light fixture")
    lamp_power: FloatProperty(name="Point light power", default=300.0, min=0.0, soft_max=5000.0,
                              unit='POWER', update=_gym_update)
    sun: BoolProperty(name="Sun", default=True, update=_gym_update,
                      description="Directional light shining in through the window band")
    sun_strength: FloatProperty(name="Sun strength", default=5.0, min=0.0, soft_max=50.0,
                                update=_gym_update, description="Irradiance (W/m²)")
    sun_elevation: FloatProperty(name="Sun elevation", default=math.radians(35.0),
                                 min=math.radians(1.0), max=math.radians(89.0), subtype='ANGLE',
                                 update=_gym_update,
                                 description="Height of the sun above the horizon: low = long "
                                             "patches of light across the floor")
    sun_azimuth: FloatProperty(name="Sun angle along wall", default=math.radians(30.0),
                               min=math.radians(-80.0), max=math.radians(80.0), subtype='ANGLE',
                               update=_gym_update,
                               description="Horizontal angle of the sun relative to the window "
                                           "wall normal (0 = straight in)")
    show_ceiling: BoolProperty(name="Show ceiling in viewport", default=False,
                               update=_gym_display_update,
                               description="Viewport only: the ceiling is still exported")
    display: EnumProperty(name="Gym display", default='SOLID', update=_gym_display_update, items=[
        ('SOLID', "Solid", ""), ('WIRE', "Wire", ""), ('HIDDEN', "Hidden", "")])

def _poll_curve(self, obj):
    return obj.type == 'CURVE'


class RCTrackSettings(PropertyGroup):
    centerline: PointerProperty(name="Centerline", type=bpy.types.Object, poll=_poll_curve,
                                update=_settings_update)
    track_display: EnumProperty(name="Track display", default='WIRE', update=_display_update, items=[
        ('WIRE', "Wire", "Show the generated track as wireframe so the drawing stays visible"),
        ('SOLID', "Solid", "Show the generated track normally"),
        ('HIDDEN', "Hidden", "Hide the generated track in the viewport"),
    ])
    auto_update: BoolProperty(name="Auto update", default=True,
                              description="Rebuild the track whenever the centerline is edited",
                              update=_settings_update)
    track_width: FloatProperty(name="Track width", default=2.0, min=0.3, soft_max=6.0,
                               unit='LENGTH', update=_settings_update,
                               description="Road width. Scale locally with Alt+S on curve points")
    auto_corners: BoolProperty(name="Auto round corners", default=True, update=_settings_update,
                               description="Round every clicked corner (incl. hairpins) with an arc "
                                           "large enough that the road never folds")
    corner_radius: FloatProperty(name="Min corner radius", default=1.2, min=0.1, unit='LENGTH',
                                 update=_settings_update,
                                 description="Centerline radius of corners. Automatically raised to "
                                             "half width + border + clearance if smaller")
    max_verts: IntProperty(name="Max verts per mesh", default=256, min=32, max=65536,
                           update=_settings_update,
                           description="Road, border and curb meshes are split so each has fewer "
                                       "vertices than this, counted as a game engine sees them "
                                       "(split by hard edges and materials)")
    step: FloatProperty(name="Sample step", default=0.25, min=0.05, max=2.0, unit='LENGTH',
                        update=_settings_update, description="Mesh resolution along the track")
    border_type: EnumProperty(name="Border", default='BATTEN', update=_settings_update, items=[
        ('BATTEN', "Batten (tasseau)", "Rectangular wooden strip"),
        ('ROPE', "Rope (corde)", "Round rope on the floor"),
        ('NONE', "None", "No border geometry"),
    ])
    border_width: FloatProperty(name="Border width", default=0.05, min=0.005, unit='LENGTH',
                                update=_settings_update)
    border_height: FloatProperty(name="Border height", default=0.05, min=0.005, unit='LENGTH',
                                 update=_settings_update)
    curbs: BoolProperty(name="Curbs (vibreurs)", default=True, update=_settings_update)
    curb_radius: FloatProperty(name="Curb below radius", default=2.5, min=0.2, unit='LENGTH',
                               update=_settings_update,
                               description="Corners tighter than this radius get an inner curb")
    curb_width: FloatProperty(name="Curb width", default=0.25, min=0.02, unit='LENGTH',
                              update=_settings_update)
    curb_height: FloatProperty(name="Curb height", default=0.012, min=0.001, unit='LENGTH',
                               update=_settings_update)
    curb_extend: FloatProperty(name="Curb extend", default=0.4, min=0.0, unit='LENGTH',
                               update=_settings_update)
    curb_stripe: FloatProperty(name="Stripe length", default=0.25, min=0.02, unit='LENGTH',
                               update=_settings_update)
    curb_style: EnumProperty(name="Curb style", default='STRIPES', update=_settings_update, items=[
        ('STRIPES', "Red/white curbs", "Low striped curb swept along the inner road edge"),
        ('DISCS', "Marker discs", "Overlapping flat plastic discs along the inner road edge"),
    ])
    disc_diameter: FloatProperty(name="Disc diameter", default=0.40, min=0.05, soft_max=1.0,
                                 unit='LENGTH', update=_settings_update)
    disc_height: FloatProperty(name="Disc height", default=0.018, min=0.002, soft_max=0.05,
                               unit='LENGTH', update=_settings_update)
    disc_top_diameter: FloatProperty(name="Disc top diameter", default=0.025, min=0.002,
                                     soft_max=0.3, unit='LENGTH', update=_settings_update,
                                     description="Diameter of the small flat top at the centre: "
                                                 "the disc is a shallow cone from its edge up to it")
    disc_gap: FloatProperty(name="Disc gap", default=0.0, soft_min=-0.15, soft_max=0.5,
                            unit='LENGTH', update=_settings_update,
                            description="Edge-to-edge gap between neighbouring discs (0 = touching, "
                                        "negative = overlapping, at most half a diameter)")
    disc_offset: FloatProperty(name="Disc overhang", default=0.0, soft_min=-0.5, soft_max=0.5,
                               unit='LENGTH', update=_settings_update,
                               description="Outer disc edge relative to the inner track limit (wall face with shared cells, road edge otherwise) "
                                           "(0 = flush, positive = sticks out past the edge)")
    disc_segments: IntProperty(name="Disc segments", default=16, min=6, max=64,
                               update=_settings_update)
    wall_mode: EnumProperty(name="Walls", default='SWEEP', update=_settings_update, items=[
        ('SWEEP', "Follow road", "Border on both road edges, swept along the centerline"),
        ('CELLS', "Shared cells", "Flood-fill around the track: walls on the outer limit, and "
                                  "shared walls where different parts of the track meet"),
    ])
    wall_distance: FloatProperty(name="Wall distance", default=0.5, min=0.0, soft_max=5.0,
                                 unit='LENGTH', update=_settings_update,
                                 description="Maximum free space between the road edge and a wall. "
                                             "Shared walls sit half-way where parts are closer")
    wall_angle: FloatProperty(name="Divider angle", default=math.radians(120.0),
                              min=math.radians(60.0), max=math.radians(179.0), subtype='ANGLE',
                              update=_settings_update,
                              description="Inside a bend turning more than this, the two legs get a "
                                          "divider wall up to the centre of the bend. Lower = "
                                          "dividers also on gentler corners")
    wall_resolution: FloatProperty(name="Wall resolution", default=0.03, min=0.005, max=0.25,
                                   unit='LENGTH', update=_settings_update,
                                   description="Grid cell size of the flood fill")
    wall_smooth: IntProperty(name="Wall smoothing", default=4, min=0, max=50,
                             update=_settings_update,
                             description="Smoothing passes on the extracted wall lines")
    clearance: FloatProperty(name="Clearance", default=0.05, min=0.0, unit='LENGTH',
                             update=_settings_update,
                             description="Extra gap required between neighbouring sections")
    floor: BoolProperty(name="Floor carpet", default=True, update=_settings_update)
    floor_margin: FloatProperty(name="Floor margin", default=1.0, min=0.0, unit='LENGTH',
                                update=_settings_update)
    road_z: FloatProperty(name="Road height", default=0.002, min=0.0, unit='LENGTH',
                          update=_settings_update)
    uv_tile: FloatProperty(name="UV tile size", default=1.0, min=0.01, unit='LENGTH',
                           update=_settings_update)
    start_line: BoolProperty(name="Start line", default=True, update=_settings_update)
    checkpoints: BoolProperty(name="Checkpoints", default=True, update=_settings_update)
    checkpoint_spacing: FloatProperty(name="Checkpoint spacing", default=10.0, min=1.0,
                                      unit='LENGTH', update=_settings_update)
    stat_length: FloatProperty(default=0.0)
    stat_min_radius: FloatProperty(default=0.0)
    stat_issues: IntProperty(default=0)
    stat_closed: BoolProperty(default=True)


class RCJumpProps(PropertyGroup):
    is_jump: BoolProperty(default=False)
    jump_type: EnumProperty(name="Type", items=JUMP_TYPES, default='TABLETOP', update=_jump_update)
    width: FloatProperty(name="Width", default=2.0, min=0.1, unit='LENGTH', update=_jump_update)
    height: FloatProperty(name="Height", default=0.2, min=0.01, unit='LENGTH', update=_jump_update)
    up_len: FloatProperty(name="Take-off length", default=0.8, min=0.05, unit='LENGTH',
                          update=_jump_update)
    top_len: FloatProperty(name="Top length", default=0.6, min=0.0, unit='LENGTH',
                           update=_jump_update)
    down_len: FloatProperty(name="Landing length", default=1.0, min=0.05, unit='LENGTH',
                            update=_jump_update)
    gap: FloatProperty(name="Gap", default=0.6, min=0.0, unit='LENGTH', update=_jump_update)
    bump_len: FloatProperty(name="Bump length", default=0.6, min=0.05, unit='LENGTH',
                            update=_jump_update)
    count: IntProperty(name="Count", default=5, min=1, max=50, update=_jump_update)
    smooth: BoolProperty(name="Smooth ramps", default=True, update=_jump_update)
    segments: IntProperty(name="Segments", default=8, min=1, max=64, update=_jump_update)
    reverse: BoolProperty(name="Reverse direction", default=False, update=_jump_reverse_update)
    tape: BoolProperty(name="Edge tape", default=True, update=_jump_update,
                       description="Shiny grey duct tape across the ramp edges (ground contacts, "
                                   "take-off lip, landing edge) so the jump reads at speed")
    tape_width: FloatProperty(name="Tape width", default=0.05, min=0.01, soft_max=0.2,
                              unit='LENGTH', update=_jump_update)


# =============================================================================
#  Operators
# =============================================================================

def _ensure_object_mode(context):
    if context.object is not None and context.object.mode != 'OBJECT':
        try:
            bpy.ops.object.mode_set(mode='OBJECT')
        except Exception:
            pass


def _make_active(context, obj):
    for o in context.view_layer.objects:
        o.select_set(False)
    obj.select_set(True)
    context.view_layer.objects.active = obj


class RC_TRACK_OT_load_reference(Operator, ImportHelper):
    bl_idname = "rc_track.load_reference"
    bl_label = "Load Drawing"
    bl_description = "Load the track drawing as a top-view reference image"
    bl_options = {'REGISTER', 'UNDO'}

    filter_glob: StringProperty(default="*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.tif;*.tiff",
                                options={'HIDDEN'})
    size: FloatProperty(name="Image width (m)", default=35.0, min=1.0, unit='LENGTH',
                        description="Real-world width of the whole image")
    opacity: FloatProperty(name="Opacity", default=0.6, min=0.0, max=1.0)

    def execute(self, context):
        _ensure_object_mode(context)
        try:
            img = bpy.data.images.load(self.filepath, check_existing=True)
        except Exception as e:
            self.report({'ERROR'}, "Could not load image: %s" % e)
            return {'CANCELLED'}
        obj = bpy.data.objects.get(REF_NAME)
        if obj is not None and obj.type != 'EMPTY':
            bpy.data.objects.remove(obj, do_unlink=True)
            obj = None
        if obj is None:
            obj = bpy.data.objects.new(REF_NAME, None)
            get_collection(COLL_SOURCE, context.scene).objects.link(obj)
        obj.empty_display_type = 'IMAGE'
        obj.data = img
        obj.empty_display_size = self.size
        obj.empty_image_offset = (-0.5, -0.5)
        obj.empty_image_depth = 'BACK'
        obj.use_empty_image_alpha = True
        obj.color[3] = self.opacity
        obj.location = (0.0, 0.0, -0.01)
        obj.rotation_euler = (0.0, 0.0, 0.0)
        obj.hide_select = True
        obj.hide_render = True
        try:
            bpy.ops.view3d.view_axis(type='TOP')
            bpy.ops.view3d.view_all()
        except Exception:
            pass
        self.report({'INFO'}, "Drawing loaded. Adjust 'Drawing width' so a lane is ~2 m wide.")
        return {'FINISHED'}


class RC_TRACK_OT_add_centerline(Operator):
    bl_idname = "rc_track.add_centerline"
    bl_label = "Add Centerline"
    bl_description = "Add a closed Bezier centerline to trace the racing line"
    bl_options = {'REGISTER', 'UNDO'}

    size_x: FloatProperty(name="Size X", default=16.0, min=2.0, unit='LENGTH')
    size_y: FloatProperty(name="Size Y", default=10.0, min=2.0, unit='LENGTH')
    points: IntProperty(name="Points", default=8, min=4, max=64)

    def execute(self, context):
        _ensure_object_mode(context)
        cu = bpy.data.curves.new("RC_Centerline", 'CURVE')
        cu.dimensions = '3D'
        cu.resolution_u = 24
        spl = cu.splines.new('BEZIER')
        n = self.points
        spl.bezier_points.add(n - 1)
        rx, ry = self.size_x / 2, self.size_y / 2
        k = 4.0 / 3.0 * math.tan(math.pi / (2 * n))
        for i, bp in enumerate(spl.bezier_points):
            a = 2 * math.pi * i / n
            P = Vector((rx * math.cos(a), ry * math.sin(a), 0.0))
            D = Vector((-rx * math.sin(a), ry * math.cos(a), 0.0))
            bp.co = P
            bp.handle_left = P - D * k
            bp.handle_right = P + D * k
            bp.handle_left_type = 'AUTO'
            bp.handle_right_type = 'AUTO'
        spl.use_cyclic_u = True
        obj = bpy.data.objects.new("RC_Centerline", cu)
        obj.location = (context.scene.cursor.location.x, context.scene.cursor.location.y, 0.0)
        get_collection(COLL_SOURCE, context.scene).objects.link(obj)
        obj.show_in_front = True
        _make_active(context, obj)
        s = context.scene.rc_track
        s.centerline = obj
        ok, msg = build_track(context.scene)
        try:
            _state["sig"] = full_signature(s)
        except Exception:
            pass
        try:
            bpy.ops.object.mode_set(mode='EDIT')
        except Exception:
            pass
        self.report({'INFO'}, msg)
        return {'FINISHED'}


def _dedupe(pts):
    out = []
    for p in pts:
        if not out or (p - out[-1]).length > 1e-5:
            out.append(p)
    return out


def _draw_track_callback(op):
    try:
        region = bpy.context.region
        gpu.state.blend_set('ALPHA')
        gpu.state.depth_test_set('NONE')
        sh = gpu.shader.from_builtin('POLYLINE_UNIFORM_COLOR')
        sh.bind()
        sh.uniform_float("viewportSize", (region.width, region.height))

        def lines(coords, color, width):
            if len(coords) < 2:
                return
            sh.uniform_float("lineWidth", width)
            sh.uniform_float("color", color)
            batch_for_shader(sh, 'LINES', {"pos": [(v.x, v.y, 0.03) for v in coords]}).draw(sh)

        lines(op.edge_lines, (1.0, 0.55, 0.05, 0.95), 2.5)
        lines(op.center_lines, (1.0, 1.0, 1.0, 0.5), 1.0)
        lines(op.poly_lines, (0.2, 0.8, 1.0, 0.8), 1.5)
        lines(op.close_lines, (0.2, 0.8, 1.0, 0.35), 1.5)

        try:
            psh = gpu.shader.from_builtin('POINT_UNIFORM_COLOR')
        except Exception:
            psh = gpu.shader.from_builtin('UNIFORM_COLOR')
        gpu.state.point_size_set(9.0)
        if op.pts:
            psh.bind()
            psh.uniform_float("color", (0.2, 0.8, 1.0, 1.0))
            batch_for_shader(psh, 'POINTS', {"pos": [(v.x, v.y, 0.04) for v in op.pts]}).draw(psh)
        if op.bad:
            gpu.state.point_size_set(16.0)
            psh.bind()
            psh.uniform_float("color", (1.0, 0.1, 0.1, 1.0))
            batch_for_shader(psh, 'POINTS', {"pos": [(v.x, v.y, 0.05) for v in op.bad]}).draw(psh)
    except Exception as e:
        print("RC Track draw preview error:", e)
    finally:
        gpu.state.point_size_set(1.0)
        gpu.state.blend_set('NONE')


class RC_TRACK_OT_draw_track(Operator):
    bl_idname = "rc_track.draw_track"
    bl_label = "Draw Track"
    bl_description = ("Click points on the ground to draw the track with straight segments. "
                      "Enter / Esc / Right-click finishes and closes the loop")
    bl_options = {'REGISTER', 'UNDO'}

    extend: BoolProperty(name="Continue existing", default=False)

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == 'VIEW_3D'

    def invoke(self, context, event):
        _ensure_object_mode(context)
        self.region = next((r for r in context.area.regions if r.type == 'WINDOW'), None)
        self.rv3d = context.area.spaces.active.region_3d
        if self.region is None or self.rv3d is None:
            self.report({'ERROR'}, "Run this from a 3D viewport")
            return {'CANCELLED'}
        self.pts, self.mouse, self.bad = [], None, []
        self.edge_lines, self.center_lines, self.poly_lines, self.close_lines = [], [], [], []
        s = context.scene.rc_track
        cl = s.centerline
        if self.extend and cl is not None and cl.type == 'CURVE' and cl.data.splines \
                and cl.data.splines[0].type == 'POLY':
            mw = cl.matrix_world
            for p in cl.data.splines[0].points:
                w = mw @ p.co.xyz
                self.pts.append(Vector((w.x, w.y, 0.0)))
        self._handle = bpy.types.SpaceView3D.draw_handler_add(
            _draw_track_callback, (self,), 'WINDOW', 'POST_VIEW')
        context.window_manager.modal_handler_add(self)
        context.area.header_text_set(
            "Draw Track:  LMB add point  |  Ctrl snap angle 15°  |  Backspace / Ctrl+Z undo point  |  "
            "Enter / Esc / RMB finish & close  |  MMB / wheel navigate")
        self._refresh(context)
        return {'RUNNING_MODAL'}

    def _ground(self, event):
        r = self.region
        co = (event.mouse_x - r.x, event.mouse_y - r.y)
        o = view3d_utils.region_2d_to_origin_3d(r, self.rv3d, co)
        d = view3d_utils.region_2d_to_vector_3d(r, self.rv3d, co)
        p = intersect_line_plane(o, o + d * 10000.0, Vector((0, 0, 0)), Vector((0, 0, 1)))
        if p is None:
            return None
        p = Vector((p.x, p.y, 0.0))
        if event.ctrl and self.pts:
            last = self.pts[-1]
            v = p - last
            L = v.length
            if L > 1e-6:
                st = math.radians(15)
                a = round(math.atan2(v.y, v.x) / st) * st
                p = last + Vector((math.cos(a), math.sin(a), 0.0)) * L
        return p

    def _over_ui(self, context, event):
        for r in context.area.regions:
            if r.type in {'UI', 'HEADER', 'TOOL_HEADER', 'TOOLS', 'NAVIGATION_BAR'} and r.width > 1:
                if r.x <= event.mouse_x < r.x + r.width and r.y <= event.mouse_y < r.y + r.height:
                    return True
        return False

    def _refresh(self, context):
        s = context.scene.rc_track
        pts = list(self.pts) + ([self.mouse] if self.mouse is not None else [])
        pts = _dedupe(pts)
        self.poly_lines, self.close_lines = [], []
        for i in range(len(pts) - 1):
            self.poly_lines += [pts[i], pts[i + 1]]
        if len(pts) >= 3:
            self.close_lines = [pts[-1], pts[0]]
        self.edge_lines, self.center_lines, self.bad = [], [], []
        if len(pts) < 2:
            return
        cyc = len(pts) >= 3
        if s.auto_corners:
            dense, _, self.bad = rounded_poly(pts, [1.0] * len(pts), cyc, s)
        else:
            dense = pts
        dense = _dedupe(dense)
        if len(dense) < 2:
            return
        _, nors = compute_frames(dense, cyc)
        hw = 0.5 * s.track_width
        n = len(dense)
        last = n if cyc else n - 1
        for arr, out in (([p + N * hw for p, N in zip(dense, nors)], self.edge_lines),
                         ([p - N * hw for p, N in zip(dense, nors)], self.edge_lines),
                         (dense, self.center_lines)):
            for i in range(last):
                out += [arr[i], arr[(i + 1) % n]]

    def _cleanup(self, context):
        if getattr(self, "_handle", None) is not None:
            bpy.types.SpaceView3D.draw_handler_remove(self._handle, 'WINDOW')
            self._handle = None
        if context.area:
            context.area.header_text_set(None)
            context.area.tag_redraw()

    def _finish(self, context):
        self._cleanup(context)
        pts = _dedupe(self.pts)
        if len(pts) > 1 and (pts[0] - pts[-1]).length < 1e-4:
            pts.pop()
        if len(pts) < 3:
            self.report({'WARNING'}, "Need at least 3 points - nothing changed")
            return {'CANCELLED'}
        scene = context.scene
        s = scene.rc_track
        obj = s.centerline if (s.centerline is not None and s.centerline.type == 'CURVE') else None
        if obj is None:
            cu = bpy.data.curves.new("RC_Centerline", 'CURVE')
            cu.dimensions = '3D'
            obj = bpy.data.objects.new("RC_Centerline", cu)
            get_collection(COLL_SOURCE, scene).objects.link(obj)
        cu = obj.data
        cu.splines.clear()
        spl = cu.splines.new('POLY')
        spl.points.add(len(pts) - 1)
        for p, v in zip(spl.points, pts):
            p.co = (v.x, v.y, 0.0, 1.0)
        spl.use_cyclic_u = True
        obj.parent = None
        obj.matrix_world = Matrix.Identity(4)
        obj.show_in_front = True
        s.centerline = obj
        ok, msg = build_track(scene)
        try:
            _state["sig"] = full_signature(s)
        except Exception:
            pass
        _make_active(context, obj)
        self.report({'INFO'} if s.stat_issues == 0 else {'WARNING'}, msg)
        return {'FINISHED'}

    def modal(self, context, event):
        if context.area is None:
            self._cleanup(context)
            return {'CANCELLED'}
        context.area.tag_redraw()
        et, ev = event.type, event.value
        if et in {'RET', 'NUMPAD_ENTER', 'SPACE', 'ESC', 'RIGHTMOUSE'} and ev == 'PRESS':
            return self._finish(context)
        if et in {'MIDDLEMOUSE', 'WHEELUPMOUSE', 'WHEELDOWNMOUSE', 'TRACKPADPAN', 'TRACKPADZOOM',
                  'NDOF_MOTION', 'MOUSEROTATE', 'MOUSESMARTZOOM'} or et.startswith('NUMPAD'):
            return {'PASS_THROUGH'}
        if et in {'MOUSEMOVE', 'INBETWEEN_MOUSEMOVE', 'LEFT_CTRL', 'RIGHT_CTRL'}:
            if self._over_ui(context, event):
                self.mouse = None
                self._refresh(context)
                return {'PASS_THROUGH'}
            if et in {'MOUSEMOVE', 'INBETWEEN_MOUSEMOVE'} or self.mouse is not None:
                self.mouse = self._ground(event)
                self._refresh(context)
            return {'RUNNING_MODAL'}
        if et == 'LEFTMOUSE':
            if self._over_ui(context, event):
                return {'PASS_THROUGH'}
            if ev == 'PRESS':
                p = self._ground(event)
                if p is not None:
                    self.pts.append(p)
                    self._refresh(context)
            return {'RUNNING_MODAL'}
        if ev == 'PRESS' and (et in {'BACK_SPACE', 'DEL'} or (et == 'Z' and event.ctrl)):
            if self.pts:
                self.pts.pop()
                self._refresh(context)
            return {'RUNNING_MODAL'}
        return {'RUNNING_MODAL'}

    def cancel(self, context):
        self._cleanup(context)


class RC_TRACK_OT_build(Operator):
    bl_idname = "rc_track.build"
    bl_label = "Rebuild Track"
    bl_description = "Rebuild road, borders, curbs, floor and checkpoints from the centerline"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        s = context.scene.rc_track
        ok, msg = build_track(context.scene)
        if ok:
            try:
                _state["sig"] = full_signature(s)
            except Exception:
                pass
        self.report({'INFO'} if ok and s.stat_issues == 0 else {'WARNING'}, msg)
        return {'FINISHED'} if ok else {'CANCELLED'}


class RC_TRACK_OT_next_issue(Operator):
    bl_idname = "rc_track.next_issue"
    bl_label = "Go to Next Issue"
    bl_description = "Select and frame the next problem marker"

    def execute(self, context):
        coll = bpy.data.collections.get(COLL_ISSUES)
        items = sorted(coll.objects, key=lambda o: o.name) if coll else []
        if not items:
            self.report({'INFO'}, "No issues")
            return {'CANCELLED'}
        _ensure_object_mode(context)
        _state["issue_idx"] = (_state["issue_idx"] + 1) % len(items)
        o = items[_state["issue_idx"]]
        _make_active(context, o)
        try:
            bpy.ops.view3d.view_selected()
        except Exception:
            pass
        self.report({'WARNING'}, "%s: %s" % (o.name, o.get("issue", "")))
        return {'FINISHED'}


class RC_TRACK_OT_add_jump(Operator):
    bl_idname = "rc_track.add_jump"
    bl_label = "Add Jump"
    bl_description = "Add a jump at the 3D cursor (snapped to the track)"
    bl_options = {'REGISTER', 'UNDO'}

    jump_type: EnumProperty(name="Type", items=JUMP_TYPES, default='TABLETOP')
    snap: BoolProperty(name="Snap to track", default=True)

    def execute(self, context):
        _ensure_object_mode(context)
        scene = context.scene
        s = scene.rc_track
        coll = get_collection(COLL_JUMPS, scene)
        me = bpy.data.meshes.new("RC_Jump")
        obj = bpy.data.objects.new("RC_Jump", me)
        coll.objects.link(obj)
        c = scene.cursor.location
        obj.location = (c.x, c.y, 0.0)
        j = obj.rc_jump
        j.is_jump = True
        j.jump_type = self.jump_type
        j.width = s.track_width
        build_jump_mesh(obj)
        if self.snap and s.centerline is not None:
            fr = track_frame(s)
            if fr is not None:
                snap_object(obj, fr, True)
        _make_active(context, obj)
        return {'FINISHED'}


class RC_TRACK_OT_snap_selected(Operator):
    bl_idname = "rc_track.snap_selected"
    bl_label = "Snap Selected to Track"
    bl_description = ("Move selected objects onto the centerline, align them with the driving "
                      "direction and match jump width to the local track width")
    bl_options = {'REGISTER', 'UNDO'}

    set_width: BoolProperty(name="Match track width", default=True)

    def execute(self, context):
        s = context.scene.rc_track
        fr = track_frame(s)
        if fr is None:
            self.report({'ERROR'}, "No valid centerline")
            return {'CANCELLED'}
        cnt = 0
        for o in context.selected_objects:
            if o == s.centerline or o.name.startswith("RC_Issue") or o.name == REF_NAME:
                continue
            snap_object(o, fr, self.set_width)
            cnt += 1
        self.report({'INFO'}, "Snapped %d object(s)" % cnt)
        return {'FINISHED'}


class RC_TRACK_OT_build_gym(Operator):
    bl_idname = "rc_track.build_gym"
    bl_label = "Create / Rebuild Gym"
    bl_description = "Create the gymnasium (walls, floor, lines, hoops, benches, ceiling)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        _ensure_object_mode(context)
        build_gym(context.scene)
        return {'FINISHED'}


class RC_TRACK_OT_remove_gym(Operator):
    bl_idname = "rc_track.remove_gym"
    bl_label = "Remove Gym"
    bl_description = "Delete all gymnasium objects"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        _ensure_object_mode(context)
        coll = bpy.data.collections.get(COLL_GYM)
        if coll is not None:
            for name in _object_names(coll):
                remove_object(name)
        remove_object(GYM_ROOT)
        return {'FINISHED'}


class RC_TRACK_OT_gym_fit(Operator):
    bl_idname = "rc_track.gym_fit"
    bl_label = "Fit Gym Around Track"
    bl_description = "Centre the gym on the track and size it to the track bounds plus a margin"
    bl_options = {'REGISTER', 'UNDO'}

    margin: FloatProperty(name="Margin", default=1.5, min=0.0, unit='LENGTH')

    def execute(self, context):
        _ensure_object_mode(context)
        coll = bpy.data.collections.get(COLL_TRACK)
        pts = []
        if coll is not None:
            for o in coll.all_objects:
                if o.type == 'MESH' and not o.name.startswith("RC_Floor"):
                    pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
        if not pts:
            self.report({'ERROR'}, "Build the track first")
            return {'CANCELLED'}
        x0, x1 = min(p.x for p in pts), max(p.x for p in pts)
        y0, y1 = min(p.y for p in pts), max(p.y for p in pts)
        g = context.scene.rc_gym
        g.length = (x1 - x0) + 2 * self.margin
        g.width = (y1 - y0) + 2 * self.margin
        root = build_gym(context.scene)
        root.location = ((x0 + x1) / 2, (y0 + y1) / 2, 0.0)
        root.rotation_euler = (0.0, 0.0, 0.0)
        return {'FINISHED'}


class RC_TRACK_OT_export_glb(Operator, ExportHelper):
    bl_idname = "rc_track.export_glb"
    bl_label = "Export Track (.glb)"
    bl_description = "Export track, jumps and checkpoints as a binary glTF"

    filename_ext = ".glb"
    filter_glob: StringProperty(default="*.glb", options={'HIDDEN'})
    include_checkpoints: BoolProperty(name="Include checkpoints", default=True)
    include_gym: BoolProperty(name="Include gymnasium", default=True)

    def execute(self, context):
        _ensure_object_mode(context)
        names = [COLL_TRACK, COLL_JUMPS] + ([COLL_CHECKPOINTS] if self.include_checkpoints else []) \
            + ([COLL_GYM] if self.include_gym else [])
        objs = []
        for n in names:
            c = bpy.data.collections.get(n)
            if c:
                objs += list(c.all_objects)
        if not objs:
            self.report({'ERROR'}, "Nothing to export - build the track first")
            return {'CANCELLED'}
        vl = context.view_layer
        for o in vl.objects:
            o.select_set(False)
        was_hidden = {o.name: o.hide_viewport for o in objs}
        for o in objs:
            o.hide_viewport = False
            if vl.objects.get(o.name):
                o.hide_set(False)
                o.select_set(True)
        try:
            bpy.ops.export_scene.gltf(filepath=self.filepath, export_format='GLB', use_selection=True)
        except Exception as e:
            self.report({'ERROR'}, "glTF export failed: %s" % e)
            return {'CANCELLED'}
        finally:
            for o in objs:
                o.hide_viewport = was_hidden.get(o.name, False)
        self.report({'INFO'}, "Exported %d objects to %s" % (len(objs), self.filepath))
        return {'FINISHED'}


# =============================================================================
#  UI
# =============================================================================

class RC_TRACK_PT_main(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "RC Track Builder"

    def draw(self, context):
        s = context.scene.rc_track
        col = self.layout.column(align=True)
        if s.centerline is None:
            col.label(text="1. Load drawing  2. Add centerline", icon='INFO')
        else:
            col.label(text="Length: %.1f m" % s.stat_length)
            col.label(text="Min radius: %.2f m" % s.stat_min_radius)
            if not s.stat_closed:
                col.label(text="Centerline not closed (Alt+C)", icon='ERROR')
            if s.stat_issues:
                col.label(text="%d issue(s)" % s.stat_issues, icon='ERROR')
                col.operator("rc_track.next_issue", icon='VIEWZOOM')
            else:
                col.label(text="No issues - lap is clean", icon='CHECKMARK')


class RC_TRACK_PT_reference(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "1. Drawing"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        col = self.layout.column(align=True)
        col.operator("rc_track.load_reference", icon='FILE_IMAGE')
        ref = bpy.data.objects.get(REF_NAME)
        if ref is not None and ref.type == 'EMPTY':
            col.prop(ref, "empty_display_size", text="Drawing width (m)")
            col.prop(ref, "color", index=3, text="Opacity", slider=True)
            col.prop(ref, "location", index=0, text="Offset X")
            col.prop(ref, "location", index=1, text="Offset Y")
            col.prop(ref, "rotation_euler", index=2, text="Rotation")
            col.prop(ref, "hide_select", text="Lock drawing (unselectable)")
            col.label(text="Draw drawing:")
            col.row(align=True).prop(ref, "empty_image_depth", expand=True)
        s = context.scene.rc_track
        col = self.layout.column(align=True)
        col.label(text="Generated track display:")
        col.row(align=True).prop(s, "track_display", expand=True)


class RC_TRACK_PT_centerline(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "2. Centerline"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        s = context.scene.rc_track
        col = self.layout.column(align=True)
        col.prop(s, "centerline", text="")
        big = col.row(align=True)
        big.scale_y = 1.5
        op = big.operator("rc_track.draw_track", text="Draw Track (click points)", icon='GREASEPENCIL')
        op.extend = False
        cl = s.centerline
        if cl is not None and cl.type == 'CURVE' and cl.data.splines and cl.data.splines[0].type == 'POLY':
            op = col.operator("rc_track.draw_track", text="Continue Drawing", icon='ADD')
            op.extend = True
        col.row(align=True).prop(s, "track_display", expand=True)
        col = self.layout.column(align=True)
        col.prop(s, "auto_corners")
        if s.auto_corners:
            col.prop(s, "corner_radius")
        box = self.layout.box().column(align=True)
        box.scale_y = 0.8
        box.label(text="Draw mode: LMB add, Backspace undo,")
        box.label(text="Ctrl snap 15°, Enter/Esc/RMB close")
        box.label(text="Edit later: Tab, G move points,")
        box.label(text="Subdivide to insert, Alt+S width")
        box.label(text="Segment 1 = start line")
        self.layout.operator("rc_track.add_centerline", text="Bezier centerline (advanced)",
                             icon='CURVE_BEZCIRCLE')


class RC_TRACK_PT_track(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "3. Track"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        s = context.scene.rc_track
        layout = self.layout
        row = layout.row(align=True)
        row.prop(s, "auto_update", toggle=True, icon='FILE_REFRESH')
        row.operator("rc_track.build", icon='MOD_BUILD')
        col = layout.column(align=True)
        col.prop(s, "track_width")
        col.prop(s, "border_type")
        if s.border_type != 'NONE':
            col.prop(s, "border_width", text="Diameter" if s.border_type == 'ROPE' else "Border width")
            if s.border_type == 'BATTEN':
                col.prop(s, "border_height")
        col.prop(s, "clearance")
        col = layout.column(align=True)
        col.prop(s, "wall_mode")
        if s.wall_mode == 'CELLS':
            col.prop(s, "wall_distance")
            col.prop(s, "wall_angle")
            col.prop(s, "wall_resolution")
            col.prop(s, "wall_smooth")
        col = layout.column(align=True)
        col.prop(s, "curbs")
        if s.curbs:
            col.prop(s, "curb_style", text="")
            col.prop(s, "curb_radius")
            col.prop(s, "curb_extend")
            if s.curb_style == 'DISCS':
                col.prop(s, "disc_diameter")
                col.prop(s, "disc_height")
                col.prop(s, "disc_top_diameter")
                col.prop(s, "disc_gap")
                col.prop(s, "disc_offset")
                col.prop(s, "disc_segments")
            else:
                col.prop(s, "curb_width")
                col.prop(s, "curb_height")
                col.prop(s, "curb_stripe")
        col = layout.column(align=True)
        col.prop(s, "floor")
        if s.floor:
            col.prop(s, "floor_margin")
        col.prop(s, "start_line")
        col.prop(s, "checkpoints")
        col = layout.column(align=True)
        col.label(text="Mesh chunks:")
        col.prop(s, "checkpoint_spacing", text="Section / checkpoint length")
        col.prop(s, "max_verts")
        col = layout.column(align=True)
        col.prop(s, "step")
        col.prop(s, "road_z")
        col.prop(s, "uv_tile")


class RC_TRACK_PT_jumps(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "4. Jumps"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        layout = self.layout
        col = layout.column(align=True)
        col.operator_menu_enum("rc_track.add_jump", "jump_type", text="Add Jump at Cursor", icon='ADD')
        col.operator("rc_track.snap_selected", icon='SNAP_ON')
        o = context.active_object
        if o is not None and o.type == 'MESH' and o.rc_jump.is_jump:
            j = o.rc_jump
            box = layout.box().column(align=True)
            box.label(text=o.name, icon='MESH_DATA')
            box.prop(j, "jump_type")
            box.prop(j, "width")
            box.prop(j, "height")
            t = j.jump_type
            if t in {'KICKER', 'TABLETOP', 'DOUBLE'}:
                box.prop(j, "up_len")
            if t in {'TABLETOP', 'DOUBLE'}:
                box.prop(j, "top_len")
                box.prop(j, "down_len")
            if t == 'DOUBLE':
                box.prop(j, "gap")
            if t in {'BUMP', 'WHOOPS'}:
                box.prop(j, "bump_len")
            if t == 'WHOOPS':
                box.prop(j, "count")
            box.prop(j, "smooth")
            if j.smooth:
                box.prop(j, "segments")
            box.prop(j, "reverse")
            box.prop(j, "tape")
            if j.tape:
                box.prop(j, "tape_width")


class RC_TRACK_PT_gym(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "5. Gymnasium"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        g = context.scene.rc_gym
        layout = self.layout
        root = bpy.data.objects.get(GYM_ROOT)
        row = layout.row(align=True)
        row.operator("rc_track.build_gym", text="Rebuild Gym" if root else "Create Gym", icon='HOME')
        if root:
            row.operator("rc_track.remove_gym", text="", icon='TRASH')
        layout.operator("rc_track.gym_fit", icon='FULLSCREEN_ENTER')
        if root:
            col = layout.column(align=True)
            col.label(text="Position (independent of drawing):")
            col.prop(root, "location", index=0, text="X")
            col.prop(root, "location", index=1, text="Y")
            col.prop(root, "rotation_euler", index=2, text="Rotation")
            layout.row(align=True).prop(g, "display", expand=True)
            layout.prop(g, "show_ceiling")
        col = layout.column(align=True)
        col.label(text="Size:")
        col.prop(g, "length")
        col.prop(g, "width")
        col.prop(g, "height")
        col.prop(g, "wall_thickness")
        col = layout.column(align=True)
        col.prop(g, "window_side")
        if g.window_side != 'NONE':
            col.prop(g, "window_bottom")
            col.prop(g, "window_top")
            col.prop(g, "pane_width")
        col = layout.column(align=True)
        col.prop(g, "lines")
        if g.lines:
            col.prop(g, "line_width")
        col.prop(g, "hoops")
        col.prop(g, "side_hoops")
        col.prop(g, "benches")
        if g.benches:
            col.prop(g, "benches_per_wall")
        col.prop(g, "handball_goals")
        col.prop(g, "doors")
        col.prop(g, "ceiling")
        if g.ceiling:
            col.prop(g, "beam_spacing")
            col.prop(g, "lamps")
            if g.lamps:
                col.prop(g, "lamp_power")
        col = layout.column(align=True)
        col.prop(g, "sun")
        if g.sun:
            col.prop(g, "sun_strength")
            col.prop(g, "sun_elevation")
            col.prop(g, "sun_azimuth")


class RC_TRACK_PT_export(Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "RC Track"
    bl_label = "6. Export"
    bl_parent_id = "RC_TRACK_PT_main"

    def draw(self, context):
        col = self.layout.column(align=True)
        col.operator("rc_track.export_glb", icon='EXPORT')
        col.label(text="Collections: RC_Track, RC_Jumps,")
        col.label(text="RC_Checkpoints (CP_000 = start), RC_Gym")


# =============================================================================
#  Registration
# =============================================================================

classes = (
    RCTrackSettings,
    RCJumpProps,
    RCGymSettings,
    RC_TRACK_OT_load_reference,
    RC_TRACK_OT_add_centerline,
    RC_TRACK_OT_draw_track,
    RC_TRACK_OT_build,
    RC_TRACK_OT_next_issue,
    RC_TRACK_OT_add_jump,
    RC_TRACK_OT_snap_selected,
    RC_TRACK_OT_build_gym,
    RC_TRACK_OT_remove_gym,
    RC_TRACK_OT_gym_fit,
    RC_TRACK_OT_export_glb,
    RC_TRACK_PT_main,
    RC_TRACK_PT_reference,
    RC_TRACK_PT_centerline,
    RC_TRACK_PT_track,
    RC_TRACK_PT_jumps,
    RC_TRACK_PT_gym,
    RC_TRACK_PT_export,
)


def _remove_handlers():
    hs = bpy.app.handlers.depsgraph_update_post
    for h in list(hs):
        if getattr(h, "__name__", "") == "rc_track_depsgraph_handler":
            hs.remove(h)


def register():
    # allow re-running the script from the Text Editor
    for cls in reversed(classes):
        old = getattr(bpy.types, cls.__name__, None)
        if old is not None:
            try:
                bpy.utils.unregister_class(old)
            except Exception:
                pass
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.rc_track = PointerProperty(type=RCTrackSettings)
    bpy.types.Scene.rc_gym = PointerProperty(type=RCGymSettings)
    bpy.types.Object.rc_jump = PointerProperty(type=RCJumpProps)
    _remove_handlers()
    bpy.app.handlers.depsgraph_update_post.append(rc_track_depsgraph_handler)


def unregister():
    _remove_handlers()
    del bpy.types.Object.rc_jump
    del bpy.types.Scene.rc_gym
    del bpy.types.Scene.rc_track
    for cls in reversed(classes):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass


if __name__ == "__main__":
    register()

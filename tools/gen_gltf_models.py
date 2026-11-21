#!/usr/bin/env python3
"""
Generate the game's Blockbench-style box models as glTF 2.0 binary (.glb).

The game used to draw the player, the bomb and every first-person weapon from
procedural boxes. This script bakes the same box layout into real glTF assets:

    assets/models/player.glb            skinned humanoid + idle/walk/shoot/death
    assets/models/bomb.glb              C4 prop + animated blinking light
    assets/models/weapon_pistol.glb     weapon models (no hands/arms)
    assets/models/weapon_shotgun.glb
    assets/models/weapon_sniper.glb
    assets/models/weapon_grenade.glb
    assets/models/weapon_flashbang.glb

It is intentionally dependency-free: a tiny glTF writer (buffers, bufferViews,
accessors, meshes, materials, skins, animations) on top of the Python standard
library. Re-run it after editing the box lists:

    python3 tools/gen_gltf_models.py
"""

import json
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "models")

# glTF component/type constants
FLOAT = 5126
UINT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


# ---------------------------------------------------------------------------
# Small vector / matrix helpers (row-major 4x4)
# ---------------------------------------------------------------------------

def vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def vec_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vec_normalize(v):
    length = math.sqrt(vec_dot(v, v))
    if length < 1e-9:
        return (0.0, 1.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def mat_identity():
    return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def mat_translate(t):
    m = mat_identity()
    m[0][3], m[1][3], m[2][3] = t
    return m


def mat_scale(s):
    m = mat_identity()
    m[0][0], m[1][1], m[2][2] = s
    return m


def mat_rot_x(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    m = mat_identity()
    m[1][1], m[1][2] = c, -s
    m[2][1], m[2][2] = s, c
    return m


def mat_rot_y(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    m = mat_identity()
    m[0][0], m[0][2] = c, s
    m[2][0], m[2][2] = -s, c
    return m


def mat_rot_z(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    m = mat_identity()
    m[0][0], m[0][1] = c, -s
    m[1][0], m[1][1] = s, c
    return m


def mat_euler(deg):
    # Matches the game's translate * rotateX * rotateY * rotateZ * scale order.
    rx, ry, rz = deg
    return mat_mul(mat_mul(mat_rot_x(rx), mat_rot_y(ry)), mat_rot_z(rz))


def mat_apply(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3])


def mat_apply_dir(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2])


def mat_inverse(m):
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-12:
            return mat_identity()
        a[col], a[pivot] = a[pivot], a[col]
        scale = a[col][col]
        a[col] = [value / scale for value in a[col]]
        for row in range(4):
            if row == col:
                continue
            factor = a[row][col]
            if abs(factor) > 1e-12:
                a[row] = [a[row][k] - factor * a[col][k] for k in range(8)]
    return [row[4:] for row in a]


def quat_axis(axis, deg):
    half = math.radians(deg) * 0.5
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


# ---------------------------------------------------------------------------
# Box tessellation
# ---------------------------------------------------------------------------

def box_faces(half):
    hx, hy, hz = half
    return [
        ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
        ((0, 0, -1), [(-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz)]),
        ((1, 0, 0), [(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)]),
        ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
        ((0, 1, 0), [(-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)]),
        ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
    ]


def add_box(vertices, indices, center, size, color, rotation=(0, 0, 0), joint=None):
    rotation_matrix = mat_euler(rotation)
    half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
    for normal_local, corners in box_faces(half):
        # Force outward (CCW) winding: cross(p1-p0, p2-p0) must match the
        # intended face normal.
        p0, p1, p2 = corners[0], corners[1], corners[2]
        if vec_dot(vec_cross(vec_sub(p1, p0), vec_sub(p2, p0)), normal_local) < 0.0:
            corners = [corners[0], corners[3], corners[2], corners[1]]
        base = len(vertices)
        normal = vec_normalize(mat_apply_dir(rotation_matrix, normal_local))
        for corner in corners:
            world = mat_apply(rotation_matrix, corner)
            vertices.append((
                center[0] + world[0], center[1] + world[1], center[2] + world[2],
                normal[0], normal[1], normal[2],
                color[0], color[1], color[2], 1.0,
                (joint if joint is not None else 0),
            ))
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])


class GltfWriter:
    def __init__(self):
        self.doc = {
            "asset": {"version": "2.0", "generator": "wexyron box-model generator"},
            "scene": 0,
            "scenes": [{"nodes": []}],
            "nodes": [],
            "meshes": [],
            "materials": [],
            "accessors": [],
            "bufferViews": [],
            "buffers": [],
        }
        self.bin = bytearray()
        self.skin_joints = []

    # -- binary plumbing ----------------------------------------------------

    def _append(self, data, target):
        while len(self.bin) % 4:
            self.bin.append(0)
        offset = len(self.bin)
        self.bin.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def add_accessor(self, values, component_type, type_name, components, target,
                     include_bounds=False):
        if component_type == FLOAT:
            data = struct.pack("<%df" % len(values), *values)
        else:
            data = struct.pack("<%dI" % len(values), *values)
        view = self._append(data, target)
        count = len(values) // components
        accessor = {
            "bufferView": view,
            "componentType": component_type,
            "count": count,
            "type": type_name,
        }
        if include_bounds:
            mins = [min(values[i::components]) for i in range(components)]
            maxs = [max(values[i::components]) for i in range(components)]
            accessor["min"] = mins
            accessor["max"] = maxs
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1

    # -- document objects ---------------------------------------------------

    def add_material(self, name, base_color=(1.0, 1.0, 1.0, 1.0),
                     metallic=0.0, roughness=0.85, emissive=(0.0, 0.0, 0.0)):
        material = {
            "name": name,
            "pbrMetallicRoughness": {
                "baseColorFactor": list(base_color),
                "metallicFactor": metallic,
                "roughnessFactor": roughness,
            },
        }
        if any(component > 0.0 for component in emissive):
            material["emissiveFactor"] = list(emissive)
        self.doc["materials"].append(material)
        return len(self.doc["materials"]) - 1

    def add_node(self, name, parent=None, translation=(0.0, 0.0, 0.0),
                 rotation=(0.0, 0.0, 0.0, 1.0)):
        node = {
            "name": name,
            "translation": list(translation),
        }
        if rotation != (0.0, 0.0, 0.0, 1.0):
            node["rotation"] = list(rotation)
        self.doc["nodes"].append(node)
        index = len(self.doc["nodes"]) - 1
        if parent is not None:
            self.doc["nodes"][parent].setdefault("children", []).append(index)
        return index

    def set_scene_roots(self, roots):
        self.doc["scenes"][0]["nodes"] = list(roots)

    def add_mesh(self, boxes, material_index=0, joint_offset=0, skinned=False):
        """boxes: list of (center, size, color, rotation, joint)."""
        vertices = []
        indices = []
        for center, size, color, rotation, joint in boxes:
            add_box(vertices, indices, center, size, color, rotation,
                    (joint - joint_offset) if skinned and joint is not None else None)

        positions = []
        normals = []
        colors = []
        joints = []
        weights = []
        for v in vertices:
            positions.extend(v[0:3])
            normals.extend(v[3:6])
            colors.extend(v[6:10])
            joints.extend([v[10], 0, 0, 0])
            weights.extend([1.0, 0.0, 0.0, 0.0])

        attributes = {
            "POSITION": self.add_accessor(positions, FLOAT, "VEC3", 3,
                                          ARRAY_BUFFER, include_bounds=True),
            "NORMAL": self.add_accessor(normals, FLOAT, "VEC3", 3, ARRAY_BUFFER),
            "COLOR_0": self.add_accessor(colors, FLOAT, "VEC4", 4, ARRAY_BUFFER),
        }
        if skinned:
            attributes["JOINTS_0"] = self.add_accessor(joints, UINT, "VEC4", 4,
                                                       ARRAY_BUFFER)
            attributes["WEIGHTS_0"] = self.add_accessor(weights, FLOAT, "VEC4", 4,
                                                        ARRAY_BUFFER)

        primitive = {
            "attributes": attributes,
            "indices": self.add_accessor(indices, UINT, "SCALAR", 1,
                                         ELEMENT_ARRAY_BUFFER),
            "material": material_index,
            "mode": 4,
        }
        self.doc["meshes"].append({"primitives": [primitive]})
        return len(self.doc["meshes"]) - 1

    def add_skin(self, joints, inverse_bind):
        skin = {
            "joints": list(joints),
            "inverseBindMatrices": self.add_accessor(inverse_bind, FLOAT, "MAT4", 16,
                                                     None),
        }
        self.doc.setdefault("skins", []).append(skin)
        return len(self.doc["skins"]) - 1

    def add_animation(self, name, channels, duration):
        """channels: list of (node, path, [(time, value), ...])."""
        samplers = []
        targets = []
        for node, path, keys in channels:
            times = [k[0] for k in keys]
            if path == "rotation":
                values = []
                for _, q in keys:
                    values.extend(q)
                type_name, components = "VEC4", 4
            else:
                values = []
                for _, v in keys:
                    values.extend(v)
                type_name, components = "VEC3", 3
            input_accessor = self.add_accessor(times, FLOAT, "SCALAR", 1,
                                               None, include_bounds=True)
            output_accessor = self.add_accessor(values, FLOAT, type_name,
                                                components, None)
            samplers.append({
                "input": input_accessor,
                "output": output_accessor,
                "interpolation": "LINEAR",
            })
            targets.append({
                "sampler": len(samplers) - 1,
                "target": {"node": node, "path": path},
            })
        self.doc.setdefault("animations", []).append({
            "name": name,
            "samplers": samplers,
            "channels": targets,
        })

    # -- output -------------------------------------------------------------

    def finish(self):
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        return self.doc, bytes(self.bin)


def write_glb(path, doc, binary):
    json_bytes = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    bin_bytes = bytearray(binary)
    while len(bin_bytes) % 4:
        bin_bytes.append(0)

    total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    with open(path, "wb") as handle:
        handle.write(b"glTF")
        handle.write(struct.pack("<II", 2, total))
        handle.write(struct.pack("<I", len(json_bytes)))
        handle.write(b"JSON")
        handle.write(json_bytes)
        handle.write(struct.pack("<I", len(bin_bytes)))
        handle.write(b"BIN\x00")
        handle.write(bin_bytes)
    return total


# ---------------------------------------------------------------------------
# Player: Blockbench-style skeleton
# ---------------------------------------------------------------------------

# Colors are authored as greys so the runtime team tint (uTint) recolours them
# exactly like the old procedural humanoid.
WHITE = (1.00, 1.00, 1.00)
HANDS = (0.55, 0.55, 0.55)
FEET = (0.45, 0.45, 0.45)
FACE = (0.035, 0.035, 0.045)


def build_player():
    writer = GltfWriter()
    body_material = writer.add_material("player_body")

    # Bone pivots (Blockbench-style). All rest rotations are identity; the
    # clips animate local rotations and the root/hips translations.
    root = writer.add_node("root")
    joints_def = [
        ("hips",        None,          (0.0, 0.90, 0.0)),
        ("spine",       "hips",        (0.0, 0.20, 0.0)),
        ("head",        "spine",       (0.0, 0.40, 0.0)),
        ("shoulder_l",  "spine",       (0.295, 0.30, 0.0)),
        ("elbow_l",     "shoulder_l",  (0.0, -0.34, 0.0)),
        ("hand_l",      "elbow_l",     (0.0, -0.30, 0.0)),
        ("shoulder_r",  "spine",       (-0.295, 0.30, 0.0)),
        ("elbow_r",     "shoulder_r",  (0.0, -0.34, 0.0)),
        ("hand_r",      "elbow_r",     (0.0, -0.30, 0.0)),
        ("upper_leg_l", "hips",        (0.17, 0.0, 0.0)),
        ("lower_leg_l", "upper_leg_l", (0.0, -0.40, 0.0)),
        ("foot_l",      "lower_leg_l", (0.0, -0.40, 0.0)),
        ("upper_leg_r", "hips",        (-0.17, 0.0, 0.0)),
        ("lower_leg_r", "upper_leg_r", (0.0, -0.40, 0.0)),
        ("foot_r",      "lower_leg_r", (0.0, -0.40, 0.0)),
    ]

    node_of = {}
    parent_of = {}
    translation_of = {}
    for name, parent_name, translation in joints_def:
        node_of[name] = writer.add_node(
            name,
            root if parent_name is None else node_of[parent_name],
            translation)
        parent_of[name] = parent_name
        translation_of[name] = translation

    joint_order = [name for name, _, _ in joints_def]
    joint_index = {name: index for index, name in enumerate(joint_order)}
    joints = [node_of[name] for name in joint_order]

    # World-space rest positions of the pivots. Rest rotations are all
    # identity, so the chain is a sum of local translations.
    world_pos = {}

    def global_position(name):
        if name in world_pos:
            return world_pos[name]
        parent_name = parent_of[name]
        t = translation_of[name]
        if parent_name is None:
            result = t
        else:
            p = global_position(parent_name)
            result = (p[0] + t[0], p[1] + t[1], p[2] + t[2])
        world_pos[name] = result
        return result

    boxes = []

    def jbox(joint_name, center, size, color, rotation=(0, 0, 0)):
        # glTF skinned vertices live in model space, while this model is
        # authored like a Blockbench cuboid list (joint-local coordinates),
        # so the box centre is moved to the pivot's rest position.
        base = global_position(joint_name)
        boxes.append(((base[0] + center[0], base[1] + center[1], base[2] + center[2]),
                      size, color, rotation, joint_index[joint_name]))

    # Torso and neck (local to the spine pivot at y = 1.10).
    jbox("spine", (0.0, 0.07, 0.0), (0.46, 0.54, 0.28), WHITE)
    jbox("spine", (0.0, 0.37, 0.0), (0.16, 0.06, 0.16), (0.88, 0.88, 0.88))

    # Head and face.
    jbox("head", (0.0, 0.15, 0.0), (0.30, 0.30, 0.30), WHITE)
    jbox("head", (-0.075, 0.19, 0.151), (0.052, 0.050, 0.012), FACE)
    jbox("head", (0.075, 0.19, 0.151), (0.052, 0.050, 0.012), FACE)
    jbox("head", (0.0, 0.080, 0.151), (0.105, 0.018, 0.012), FACE)
    jbox("head", (-0.065, 0.080, 0.151), (0.030, 0.018, 0.012), FACE)
    jbox("head", (0.065, 0.080, 0.151), (0.030, 0.018, 0.012), FACE)

    # Arms.
    for shoulder, elbow, hand in (("shoulder_l", "elbow_l", "hand_l"),
                                  ("shoulder_r", "elbow_r", "hand_r")):
        jbox(shoulder, (0.0, -0.17, 0.0), (0.13, 0.34, 0.16), WHITE)
        jbox(elbow, (0.0, -0.15, 0.0), (0.12, 0.30, 0.14), (0.90, 0.90, 0.90))
        jbox(hand, (0.0, -0.055, 0.0), (0.12, 0.11, 0.14), HANDS)

    # Legs.
    for hip, knee, ankle in (("upper_leg_l", "lower_leg_l", "foot_l"),
                              ("upper_leg_r", "lower_leg_r", "foot_r")):
        jbox(hip, (0.0, -0.20, 0.0), (0.21, 0.40, 0.22), WHITE)
        jbox(knee, (0.0, -0.20, 0.0), (0.19, 0.40, 0.20), (0.82, 0.82, 0.82))
        jbox(ankle, (0.0, -0.05, 0.06), (0.21, 0.10, 0.32), FEET)

    mesh = writer.add_mesh(boxes, body_material, joint_offset=0, skinned=True)

    # Global rest matrix per joint, for the inverse bind matrices.
    global_rest = {}

    def global_matrix(name):
        if name in global_rest:
            return global_rest[name]
        local = mat_translate(translation_of[name])
        parent_name = parent_of[name]
        result = local if parent_name is None else mat_mul(global_matrix(parent_name), local)
        global_rest[name] = result
        return result

    inverse_bind = []
    for name in joint_order:
        inverse = mat_inverse(global_matrix(name))
        inverse_bind.extend(inverse[r][c] for c in range(4) for r in range(4))

    skin = writer.add_skin(joints, inverse_bind)
    mesh_node = writer.add_node("player_mesh")
    writer.doc["nodes"][mesh_node]["mesh"] = mesh
    writer.doc["nodes"][mesh_node]["skin"] = skin
    writer.set_scene_roots([root, mesh_node])

    # ── Clips ───────────────────────────────────────────────────────────
    def qx(deg):
        return quat_axis((1, 0, 0), deg)

    hips = node_of["hips"]
    spine = node_of["spine"]
    head = node_of["head"]
    shoulder_l = node_of["shoulder_l"]
    shoulder_r = node_of["shoulder_r"]
    elbow_l = node_of["elbow_l"]
    elbow_r = node_of["elbow_r"]
    upper_leg_l = node_of["upper_leg_l"]
    upper_leg_r = node_of["upper_leg_r"]
    lower_leg_l = node_of["lower_leg_l"]
    lower_leg_r = node_of["lower_leg_r"]

    # Idle: a gentle breathing sway.
    writer.add_animation("Idle", [
        (shoulder_l, "rotation", [(0.0, qx(2)), (1.0, qx(-2)), (2.0, qx(2))]),
        (shoulder_r, "rotation", [(0.0, qx(-2)), (1.0, qx(2)), (2.0, qx(-2))]),
        (head, "rotation", [(0.0, qx(-1)), (1.0, qx(2)), (2.0, qx(-1))]),
        (hips, "translation", [(0.0, (0.0, 0.90, 0.0)),
                               (1.0, (0.0, 0.912, 0.0)),
                               (2.0, (0.0, 0.90, 0.0))]),
    ], 2.0)

    # Walk: one full stride per second.
    walk_times = [0.0, 0.25, 0.5, 0.75, 1.0]
    writer.add_animation("Walk", [
        (upper_leg_l, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [25, 0, -25, 0, 25])]),
        (upper_leg_r, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [-25, 0, 25, 0, -25])]),
        (lower_leg_l, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [0, 28, 0, 0, 0])]),
        (lower_leg_r, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [0, 0, 0, 28, 0])]),
        (shoulder_l, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [-20, 0, 20, 0, -20])]),
        (shoulder_r, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [20, 0, -20, 0, 20])]),
        (spine, "rotation", [(t, qx(v)) for t, v in zip(walk_times, [0, 2, 0, -2, 0])]),
        (hips, "translation", [(0.0, (0.0, 0.90, 0.0)),
                               (0.25, (0.0, 0.916, 0.0)),
                               (0.5, (0.0, 0.90, 0.0)),
                               (0.75, (0.0, 0.916, 0.0)),
                               (1.0, (0.0, 0.90, 0.0))]),
    ], 1.0)

    # Shoot: arms come up to aim. The game blends this in by anim.shoot.
    writer.add_animation("Shoot", [
        (shoulder_l, "rotation", [(0.0, qx(-58)), (0.35, qx(0))]),
        (shoulder_r, "rotation", [(0.0, qx(-72)), (0.35, qx(0))]),
        (elbow_l, "rotation", [(0.0, qx(-14)), (0.35, qx(0))]),
        (elbow_r, "rotation", [(0.0, qx(-10)), (0.35, qx(0))]),
        (spine, "rotation", [(0.0, quat_axis((0, 1, 0), 8)), (0.35, qx(0))]),
    ], 0.35)

    # Jump / air pose: knees up, arms forward/out. Held while off the ground.
    writer.add_animation("Jump", [
        (upper_leg_l, "rotation", [(0.0, qx(-18)), (1.0, qx(-18))]),
        (upper_leg_r, "rotation", [(0.0, qx(-14)), (1.0, qx(-14))]),
        (lower_leg_l, "rotation", [(0.0, qx(30)), (1.0, qx(30))]),
        (lower_leg_r, "rotation", [(0.0, qx(26)), (1.0, qx(26))]),
        (shoulder_l, "rotation", [(0.0, qx(-32)), (1.0, qx(-32))]),
        (shoulder_r, "rotation", [(0.0, qx(-38)), (1.0, qx(-38))]),
        (spine, "rotation", [(0.0, qx(-5)), (1.0, qx(-5))]),
        (hips, "translation", [(0.0, (0.0, 0.90, 0.0)), (1.0, (0.0, 0.90, 0.0))]),
    ], 1.0)

    # Hit flinch: torso back, arms out. Blended in by anim.damage.
    writer.add_animation("Hit", [
        (spine, "rotation", [(0.0, qx(-13)), (0.35, qx(0))]),
        (head, "rotation", [(0.0, qx(-8)), (0.35, qx(0))]),
        (shoulder_l, "rotation", [(0.0, quat_mul(quat_axis((0, 0, 1), 15), qx(-18))),
                                  (0.35, qx(0))]),
        (shoulder_r, "rotation", [(0.0, quat_mul(quat_axis((0, 0, 1), -15), qx(-18))),
                                  (0.35, qx(0))]),
    ], 0.35)

    # Death: fall backwards around the feet, arms limp.
    writer.add_animation("Death", [
        (root, "rotation", [(0.0, qx(0)), (1.0, qx(-82))]),
        (root, "translation", [(0.0, (0.0, 0.0, 0.0)), (1.0, (0.0, 0.10, 0.0))]),
        (shoulder_l, "rotation", [(0.0, qx(0)), (1.0, quat_axis((0, 0, 1), 30))]),
        (shoulder_r, "rotation", [(0.0, qx(0)), (1.0, quat_axis((0, 0, 1), -30))]),
        (lower_leg_l, "rotation", [(0.0, qx(0)), (1.0, qx(20))]),
        (lower_leg_r, "rotation", [(0.0, qx(0)), (1.0, qx(20))]),
    ], 1.0)

    return writer


# ---------------------------------------------------------------------------
# Bomb
# ---------------------------------------------------------------------------

def build_bomb():
    writer = GltfWriter()
    body_material = writer.add_material("bomb_body")
    light_material = writer.add_material("bomb_light", base_color=(1.0, 0.12, 0.08, 1.0),
                                         metallic=0.0, roughness=0.4,
                                         emissive=(1.0, 0.05, 0.02))

    root = writer.add_node("bomb_root")
    body_boxes = [
        ((0.0, 0.15, 0.0), (0.60, 0.30, 0.60), (0.30, 0.30, 0.25), (0, 0, 0), None),
        ((0.0, 0.35, 0.0), (0.40, 0.15, 0.40), (0.50, 0.50, 0.40), (0, 0, 0), None),
        ((0.0, 0.15, 0.305), (0.24, 0.13, 0.02), (0.05, 0.07, 0.05), (0, 0, 0), None),
        ((-0.17, 0.27, 0.305), (0.03, 0.03, 0.02), (0.65, 0.10, 0.08), (0, 0, 0), None),
        ((0.17, 0.10, 0.305), (0.03, 0.03, 0.02), (0.15, 0.55, 0.20), (0, 0, 0), None),
        ((0.21, 0.11, 0.0), (0.02, 0.02, 0.55), (0.55, 0.15, 0.10), (0, 0, 0), None),
        ((-0.21, 0.12, 0.0), (0.02, 0.02, 0.55), (0.15, 0.20, 0.60), (0, 0, 0), None),
    ]
    body_mesh = writer.add_mesh(body_boxes, body_material)
    writer.doc["nodes"][root]["mesh"] = body_mesh

    light = writer.add_node("light", root, (0.0, 0.45, 0.0))
    light_boxes = [
        ((0.0, 0.0, 0.0), (0.15, 0.08, 0.15), (1.0, 0.12, 0.08), (0, 0, 0), None),
    ]
    light_mesh = writer.add_mesh(light_boxes, light_material)
    writer.doc["nodes"][light]["mesh"] = light_mesh
    writer.set_scene_roots([root])

    writer.add_animation("Blink", [
        (light, "scale", [(0.0, (1.0, 1.0, 1.0)),
                          (0.5, (0.35, 0.35, 0.35)),
                          (1.0, (1.0, 1.0, 1.0))]),
    ], 1.0)
    return writer


# ---------------------------------------------------------------------------
# First-person weapons
# ---------------------------------------------------------------------------

def build_weapon(name, body):
    writer = GltfWriter()
    material = writer.add_material(name)
    boxes = []
    body(boxes)
    mesh = writer.add_mesh(boxes, material)
    node = writer.add_node(name + "_root")
    writer.doc["nodes"][node]["mesh"] = mesh
    writer.set_scene_roots([node])
    return writer


def pistol_boxes(boxes):
    metal = (0.40, 0.42, 0.46)
    dark = (0.20, 0.20, 0.22)
    boxes += [
        ((0.0, 0.025, -0.16), (0.075, 0.065, 0.36), metal, (0, 0, 0), None),
        ((0.0, -0.015, -0.05), (0.070, 0.045, 0.20), dark, (0, 0, 0), None),
        ((0.0, 0.025, -0.365), (0.040, 0.040, 0.070), dark, (0, 0, 0), None),
        ((0.0, -0.100, 0.035), (0.078, 0.180, 0.090), (0.24, 0.20, 0.14), (-12, 0, 0), None),
        ((0.0, -0.105, 0.040), (0.060, 0.115, 0.055), (0.42, 0.29, 0.15), (-12, 0, 0), None),
        ((0.0, 0.070, -0.315), (0.014, 0.020, 0.022), (0.62, 0.62, 0.62), (0, 0, 0), None),
        ((0.0, 0.070, 0.005), (0.050, 0.018, 0.020), (0.50, 0.50, 0.50), (0, 0, 0), None),
        ((0.0, 0.060, 0.060), (0.028, 0.035, 0.028), dark, (0, 0, 0), None),
        ((0.0, -0.050, -0.020), (0.055, 0.012, 0.095), dark, (0, 0, 0), None),
        ((0.0, -0.072, -0.065), (0.050, 0.032, 0.012), dark, (0, 0, 0), None),
    ]


def shotgun_boxes(boxes):
    metal = (0.22, 0.23, 0.25)
    dark = (0.10, 0.10, 0.11)
    wood = (0.38, 0.23, 0.12)
    boxes += [
        ((0.0, 0.030, -0.31), (0.055, 0.055, 0.62), metal, (0, 0, 0), None),
        ((0.0, -0.005, -0.28), (0.045, 0.045, 0.52), dark, (0, 0, 0), None),
        ((0.0, 0.020, -0.02), (0.090, 0.090, 0.28), metal, (0, 0, 0), None),
        ((0.0, 0.000, -0.20), (0.100, 0.075, 0.20), wood, (0, 0, 0), None),
        ((0.0, -0.020, 0.160), (0.085, 0.120, 0.30), wood, (8, 0, 0), None),
        ((0.0, -0.100, 0.030), (0.070, 0.160, 0.090), wood, (-12, 0, 0), None),
        ((0.052, 0.030, -0.04), (0.010, 0.030, 0.10), dark, (0, 0, 0), None),
        ((0.0, 0.065, -0.590), (0.018, 0.018, 0.018), (1.0, 0.80, 0.20), (0, 0, 0), None),
        ((0.0, -0.050, -0.010), (0.055, 0.012, 0.095), dark, (0, 0, 0), None),
        ((0.0, -0.072, -0.055), (0.050, 0.032, 0.012), dark, (0, 0, 0), None),
    ]


def sniper_boxes(boxes):
    metal = (0.22, 0.24, 0.26)
    dark = (0.09, 0.10, 0.11)
    stock = (0.17, 0.20, 0.16)
    boxes += [
        ((0.0, 0.030, -0.52), (0.042, 0.042, 0.86), metal, (0, 0, 0), None),
        ((0.0, 0.020, -0.24), (0.075, 0.075, 0.44), dark, (0, 0, 0), None),
        ((0.058, 0.035, -0.10), (0.055, 0.022, 0.022), metal, (0, 0, 0), None),
        ((0.0, 0.105, -0.23), (0.070, 0.070, 0.30), dark, (0, 0, 0), None),
        ((0.0, 0.105, -0.39), (0.090, 0.090, 0.055), metal, (0, 0, 0), None),
        ((0.0, 0.105, -0.07), (0.080, 0.080, 0.055), metal, (0, 0, 0), None),
        ((0.0, 0.065, -0.15), (0.028, 0.050, 0.028), metal, (0, 0, 0), None),
        ((0.0, 0.065, -0.31), (0.028, 0.050, 0.028), metal, (0, 0, 0), None),
        ((0.0, -0.005, 0.175), (0.085, 0.115, 0.34), stock, (7, 0, 0), None),
        ((0.0, -0.100, 0.035), (0.070, 0.160, 0.090), dark, (-12, 0, 0), None),
        ((-0.030, -0.075, -0.49), (0.016, 0.095, 0.016), dark, (0, 0, 18), None),
        ((0.030, -0.075, -0.49), (0.016, 0.095, 0.016), dark, (0, 0, -18), None),
    ]


def grenade_boxes(boxes):
    boxes += [
        ((0.0, -0.010, -0.060), (0.100, 0.130, 0.100), (0.20, 0.35, 0.15), (0, 0, 0), None),
        ((0.0, 0.070, -0.060), (0.060, 0.035, 0.060), (0.35, 0.36, 0.38), (0, 0, 0), None),
        ((0.045, 0.045, -0.060), (0.018, 0.018, 0.090), (0.55, 0.56, 0.58), (0, 0, 0), None),
        ((0.078, 0.055, -0.060), (0.014, 0.014, 0.060), (0.62, 0.62, 0.62), (0, 0, 0), None),
        ((0.040, 0.055, -0.010), (0.030, 0.012, 0.012), (0.62, 0.62, 0.62), (0, 0, 0), None),
    ]


def flashbang_boxes(boxes):
    boxes += [
        ((0.0, -0.010, -0.060), (0.075, 0.140, 0.075), (0.58, 0.59, 0.62), (0, 0, 0), None),
        ((0.0, 0.015, -0.060), (0.080, 0.022, 0.080), (0.25, 0.26, 0.28), (0, 0, 0), None),
        ((0.0, -0.040, -0.060), (0.080, 0.018, 0.080), (0.25, 0.26, 0.28), (0, 0, 0), None),
        ((0.0, 0.078, -0.060), (0.040, 0.040, 0.040), (0.35, 0.36, 0.38), (0, 0, 0), None),
        ((0.045, 0.052, -0.060), (0.016, 0.016, 0.085), (0.55, 0.55, 0.55), (0, 0, 0), None),
        ((0.075, 0.062, -0.060), (0.013, 0.013, 0.055), (0.62, 0.62, 0.62), (0, 0, 0), None),
    ]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    jobs = [
        ("player.glb", build_player()),
        ("bomb.glb", build_bomb()),
        ("weapon_pistol.glb", build_weapon("weapon_pistol", pistol_boxes)),
        ("weapon_shotgun.glb", build_weapon("weapon_shotgun", shotgun_boxes)),
        ("weapon_sniper.glb", build_weapon("weapon_sniper", sniper_boxes)),
        ("weapon_grenade.glb", build_weapon("weapon_grenade", grenade_boxes)),
        ("weapon_flashbang.glb", build_weapon("weapon_flashbang", flashbang_boxes)),
    ]

    for filename, writer in jobs:
        doc, binary = writer.finish()
        path = os.path.join(OUT_DIR, filename)
        size = write_glb(path, doc, binary)
        animation_names = [a.get("name", "") for a in doc.get("animations", [])]
        print("%-26s %6d bytes  meshes=%d nodes=%d skins=%d animations=%s"
              % (filename, size, len(doc["meshes"]), len(doc["nodes"]),
                 len(doc.get("skins", [])), animation_names or "[]"))


if __name__ == "__main__":
    main()

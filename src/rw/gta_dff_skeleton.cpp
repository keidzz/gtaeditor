#include "gta_dff_skeleton.h"

#include "gta_bone_tags.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace {

// [SOURCED] librw src/rwbase.h â€” MAKEPLUGINID(vendor, id) = ((vendor&0xFFFFFF)<<8)|(id&0xFF)
constexpr uint32_t ID_STRUCT = 0x01;
constexpr uint32_t ID_EXTENSION = 0x03;
constexpr uint32_t ID_FRAMELIST = 0x0E;
constexpr uint32_t ID_CLUMP = 0x10;
constexpr uint32_t ID_HANIM = (1u << 8) | 0x1E; // vendor=VEND_CRITERIONTK(1), id=0x1E -> 0x11E
constexpr uint32_t ID_NAOBJECT = 0x00;

struct Cursor {
	const uint8_t *data;
	size_t size;
	size_t pos = 0;
	bool ok = true;

	bool read_raw(void *dst, size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		std::memcpy(dst, data + pos, n);
		pos += n;
		return true;
	}

	template <typename T>
	bool read(T &out) {
		return read_raw(&out, sizeof(T));
	}

	bool skip(size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		pos += n;
		return true;
	}
};

struct ChunkHeader {
	uint32_t type = 0;
	uint32_t length = 0;
	uint32_t library_id = 0; // unused beyond consuming the bytes
};

// [SOURCED] librw src/base.cpp readChunkHeaderInfo â€” 12 bytes: type, size, libraryID
bool read_chunk_header(Cursor &c, ChunkHeader &out) {
	return c.read(out.type) && c.read(out.length) && c.read(out.library_id);
}

// [SOURCED] librw src/base.cpp findChunk â€” scan forward, skipping over
// non-matching chunks by their declared length, until `type` is found, EOF,
// or a NAOBJECT (0) chunk terminates the search.
bool find_chunk(Cursor &c, uint32_t type, ChunkHeader &out) {
	while (true) {
		ChunkHeader h;
		if (!read_chunk_header(c, h)) {
			return false;
		}
		if (h.type == ID_NAOBJECT) {
			return false;
		}
		if (h.type == type) {
			out = h;
			return true;
		}
		if (!c.skip(h.length)) {
			return false;
		}
	}
}

// Reads a Vector3 as 3 raw floats, source order x,y,z (RW's V3d).
bool read_v3d(Cursor &c, Vector3 &out) {
	float x, y, z;
	if (!(c.read(x) && c.read(y) && c.read(z))) {
		return false;
	}
	out = Vector3(x, y, z);
	return true;
}

struct HierarchyNode {
	int32_t hanim_id = -1;
	int32_t skin_index = -1; // this is the "slot" value your warnings reference
	int32_t flags = 0;
};

// [NOT SOURCED to the same standard as the rest of this file â€” recalled from
// librw/OpenRW's HAnim PLG layout, not verified against a hex dump of a real
// player.dff yet. Print the parsed table and sanity-check node_id values
// against known hanim tags before trusting this fully.]
bool find_hanim_id_in_extension(Cursor &c, size_t payload_end, int32_t &out_id, std::vector<GTADffSkeleton::HierarchyNode> &out_hierarchy) {
	while (c.pos < payload_end) {
		ChunkHeader h;
		if (!read_chunk_header(c, h)) {
			return false;
		}
		if (c.pos + h.length > payload_end) {
			return false;
		}
		if (h.type == ID_HANIM) {
			const size_t hanim_end = c.pos + h.length;
			int32_t version = 0, id = -1, num_nodes = 0;
			if (!(c.read(version) && c.read(id) && c.read(num_nodes))) {
				return false;
			}
			out_id = id;
			if (num_nodes > 0) {
				int32_t hier_flags = 0, key_frame_size = 0;
				if (!(c.read(hier_flags) && c.read(key_frame_size))) {
					return false;
				}
				out_hierarchy.resize((size_t)num_nodes);
				for (int32_t n = 0; n < num_nodes; ++n) {
					int32_t node_id = -1, node_index = -1, node_flags = 0;
					if (!(c.read(node_id) && c.read(node_index) && c.read(node_flags))) {
						return false;
					}
					out_hierarchy[(size_t)n] = { node_id, node_index, node_flags };
				}
			}
			c.pos = hanim_end;
			return true;
		}
		if (!c.skip(h.length)) {
			return false;
		}
	}
	return true;
}

// Change of basis T: (x, y, z) -> (x, z, -y) — Z-up raw RenderWare data to
// Godot's Y-up. Used for points AND for the conjugate per-frame rotation
// (see the conversion comment at _convert_z_up_to_y_up).
inline Vector3 _rw_t(const Vector3 &v) {
	return Vector3(v.x, v.z, -v.y);
}

} // namespace

GTADffSkeleton::GTADffSkeleton() {
}

GTADffSkeleton::~GTADffSkeleton() {
}

void GTADffSkeleton::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "bytes"), &GTADffSkeleton::parse);
	ClassDB::bind_method(D_METHOD("get_bone_count"), &GTADffSkeleton::get_bone_count);
	ClassDB::bind_method(D_METHOD("get_root_index"), &GTADffSkeleton::get_root_index);
	ClassDB::bind_method(D_METHOD("get_hierarchy_table"), &GTADffSkeleton::get_hierarchy_table);
	ClassDB::bind_method(D_METHOD("get_bones"), &GTADffSkeleton::get_bones);
	ClassDB::bind_method(D_METHOD("find_bone_by_name", "name"), &GTADffSkeleton::find_bone_by_name);
	ClassDB::bind_method(D_METHOD("build_skeleton3d", "convert_coordinates"), &GTADffSkeleton::build_skeleton3d, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_global_bind_transforms", "convert_coordinates"), &GTADffSkeleton::get_global_bind_transforms, DEFVAL(false));
}

bool GTADffSkeleton::parse(const PackedByteArray &bytes) {
	bones.clear();
	root_index = -1;
	hierarchy_table.clear();
	Cursor c{ bytes.ptr(), (size_t)bytes.size() };

	// [SOURCED] Clump::streamRead: CLUMP -> STRUCT -> FRAMELIST -> ...
	ChunkHeader clump_h;
	if (!find_chunk(c, ID_CLUMP, clump_h)) {
		UtilityFunctions::push_error("GTADffSkeleton: no CLUMP chunk found â€” not a valid .dff, or an unsupported variant");
		return false;
	}

	ChunkHeader struct_h;
	if (!find_chunk(c, ID_STRUCT, struct_h)) {
		UtilityFunctions::push_error("GTADffSkeleton: CLUMP has no STRUCT sub-chunk");
		return false;
	}
	if (!c.skip(struct_h.length)) { // numAtomics/numLights/numCameras â€” not needed here
		UtilityFunctions::push_error("GTADffSkeleton: truncated CLUMP STRUCT");
		return false;
	}

	ChunkHeader framelist_h;
	if (!find_chunk(c, ID_FRAMELIST, framelist_h)) {
		UtilityFunctions::push_error("GTADffSkeleton: no FRAMELIST chunk found");
		return false;
	}
	const size_t framelist_end = c.pos + framelist_h.length;

	ChunkHeader flstruct_h;
	if (!find_chunk(c, ID_STRUCT, flstruct_h)) {
		UtilityFunctions::push_error("GTADffSkeleton: FRAMELIST has no STRUCT sub-chunk");
		return false;
	}

	int32_t num_frames = 0;
	if (!c.read(num_frames) || num_frames < 0) {
		UtilityFunctions::push_error("GTADffSkeleton: could not read frame count");
		return false;
	}

	bones.resize((size_t)num_frames);

	// [SOURCED] FrameStreamData: V3d right,up,at,pos (12 bytes each) + int32
	// parent + int32 matflag = 56 bytes/frame.
	for (int32_t i = 0; i < num_frames; ++i) {
		Vector3 right, up, at, pos;
		int32_t parent = -1;
		int32_t matflag = 0;
		if (!(read_v3d(c, right) && read_v3d(c, up) && read_v3d(c, at) && read_v3d(c, pos) && c.read(parent) && c.read(matflag))) {
			UtilityFunctions::push_error("GTADffSkeleton: truncated frame list at frame ", (int)i);
			bones.clear();
			return false;
		}
		if (parent < -1 || parent >= num_frames) {
			UtilityFunctions::push_error("GTADffSkeleton: frame ", (int)i, " has an out-of-range parent index (", (int)parent, ")");
			bones.clear();
			return false;
		}

		Bone &bone = bones[(size_t)i];
		bone.parent_index = parent;
		// RAW RenderWare basis â€” see header note: not yet Y-up converted.
		bone.bind_transform = Transform3D(right, up, at, pos);
		if (parent == -1) {
			root_index = i;
		}
	}

	// [SOURCED] Frame::s_plglist.streamRead per frame, i.e. one EXTENSION
	// chunk follows immediately after each frame's FrameStreamData, in the
	// same order as the frames were just read.
	for (int32_t i = 0; i < num_frames; ++i) {
		ChunkHeader ext_h;
		if (!read_chunk_header(c, ext_h) || ext_h.type != ID_EXTENSION) {
			UtilityFunctions::push_error("GTADffSkeleton: expected EXTENSION chunk after frame ", (int)i);
			bones.clear();
			return false;
		}
		const size_t ext_end = c.pos + ext_h.length;

		int32_t hanim_id = -1;
		if (!find_hanim_id_in_extension(c, ext_end, hanim_id, hierarchy_table)) {
			UtilityFunctions::push_error("GTADffSkeleton: malformed EXTENSION for frame ", (int)i);
			bones.clear();
			return false;
		}
		// find_hanim_id_in_extension only advances the cursor up to the end
		// of the HAnim sub-chunk (or leaves it at ext_end if none was found)
		// â€” make sure we land exactly on the next frame's extension either way.
		c.pos = ext_end;

		Bone &bone = bones[(size_t)i];
		bone.hanim_id = hanim_id;
		bone.name = (hanim_id != -1) ? gta_bone_tag_to_name(hanim_id) : (String("Frame_") + String::num_int64(i));
	}

	c.pos = framelist_end; // in case of any slack (e.g. padding); not treated as an error

	return true;
}

int GTADffSkeleton::get_bone_count() const {
	return (int)bones.size();
}

int GTADffSkeleton::get_root_index() const {
	return root_index;
}


Array GTADffSkeleton::get_hierarchy_table() const {
	Array out;
	for (const HierarchyNode &n : hierarchy_table) {
		Dictionary d;
		d["hanim_id"] = n.hanim_id;
		d["skin_index"] = n.skin_index;
		d["name"] = gta_bone_tag_to_name(n.hanim_id);
		out.push_back(d);
	}
	return out;
}

Array GTADffSkeleton::get_bones() const {
	Array out;
	for (const Bone &bone : bones) {
		Dictionary d;
		d["name"] = bone.name;
		d["hanim_id"] = bone.hanim_id;
		d["parent_index"] = bone.parent_index;
		d["bind_transform"] = bone.bind_transform;
		out.push_back(d);
	}
	return out;
}

int GTADffSkeleton::find_bone_by_name(const String &name) const {
	for (size_t i = 0; i < bones.size(); ++i) {
		if (bones[i].name.nocasecmp_to(name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

// CORRECTED AGAIN, based on a second, cleaner empirical measurement against
// the same player.dff -- this time composing the REAL frame-list matrices
// through the hierarchy instead of eyeballing raw per-bone local origins:
// the raw skeleton IS Z-up, exactly like the geometry and the IFP animation
// data. In the file's own rest pose the head sits at global +Z (~0.59), the
// feet at -Z (~-0.92), arms spread along Y -- the same convention the
// vertices and animation keyframes need converted away. The earlier
// "already Y-up" conclusion documented below was a misreading of local
// bone-origin ranges, not the composed hierarchy.
//
// Why the T-pose looked fine even with a Z-up skeleton: at rest, pose ==
// rest, and GPU skinning computes inverse_bind * bind == identity, so the
// mesh renders exactly as authored (Y-up converted) regardless of the
// skeleton's own orientation. The skeleton's space only becomes visible --
// and only matters -- once an animation poses it, which is exactly when the
// deformation showed up (see the pose-vs-rest note in build_animation).
//
// The conversion MUST be the CONJUGATE form T * local * T^-1 (T = the
// (x,y,z)->(x,z,-y) change of basis), NOT a plain left-multiply T * local:
// left-multiplying every local frame individually does not produce a rigid
// transform of the hierarchy. Verified numerically against the real
// player.dff: recomposing left-multiplied locals gives per-bone global
// position errors up to ~1.35 vs the ideal T*global (the hierarchy comes
// out twisted); the conjugate form recomposes to EXACTLY T*global (error
// 0.0). In quaternion terms for an orthonormal basis, conjugation by T is
// the same (x,z,-y) remap of the vector part with w untouched that
// GTAIfpAnimation's rotation conversion already uses -- so after this fix
// all three data types (vertices, skeleton rest, animation rotations) live
// in one consistent Y-up space.
Transform3D GTADffSkeleton::_convert_z_up_to_y_up(const Transform3D &t) {
	// T: (x,y,z) -> (x, z, -y). T^-1: (x,y,z) -> (x, -z, y).
	// Column i of T*B*T^-1 is T * (B * (T^-1 * e_i)):
	//   e_0 -> e_0   -> B column 0 -> T applied
	//   e_1 -> +e_2  -> B column 2 -> T applied   (T^-1*e_1 = (0,0,1) = e_2)
	//   e_2 -> -e_1  -> B column 1 negated -> T applied (T^-1*e_2 = (0,-1,0))
	Vector3 col0 = t.basis.get_column(0);
	Vector3 col1 = t.basis.get_column(1);
	Vector3 col2 = t.basis.get_column(2);
	Basis new_basis;
	new_basis.set_column(0, Vector3(col0.x, col0.z, -col0.y));
	new_basis.set_column(1, Vector3(col2.x, col2.z, -col2.y));
	new_basis.set_column(2, Vector3(-col1.x, -col1.z, col1.y));
	Vector3 new_origin = Vector3(t.origin.x, t.origin.z, -t.origin.y);
	return Transform3D(new_basis, new_origin);
}

Skeleton3D *GTADffSkeleton::build_skeleton3d(bool convert_coordinates) const {
	Skeleton3D *skel = memnew(Skeleton3D);

	// Pass 1: add every bone first, so parent indices (which reference
	// other bones by index) are always valid by the time we set them.
	for (const Bone &bone : bones) {
		skel->add_bone(bone.name);
	}

	// Pass 2: wire up parents and rest poses.
	// When convert_coordinates is true, each local frame is conjugated by
	// T = (x, z, -y) via _convert_z_up_to_y_up(). Because conjugation is a
	// homomorphism (T * (A * B) * T^-1 == (T * A * T^-1) * (T * B * T^-1)),
	// Godot's hierarchy composition (global = parent.global * rest)
	// reproduces the exact T-converted global binds for all bones.
	for (size_t i = 0; i < bones.size(); ++i) {
		const Bone &bone = bones[i];
		if (bone.parent_index >= 0) {
			skel->set_bone_parent((int)i, bone.parent_index);
		}
		skel->set_bone_rest((int)i, convert_coordinates ? _convert_z_up_to_y_up(bone.bind_transform) : bone.bind_transform);
	}

	// Skeleton3D pose buffer initialization: reset pose buffer to match rest
	skel->reset_bone_poses();

	return skel;
}

// NOTE: as of Godot 4.5, Skeleton3D::get_bone_global_pose()/get_bone_global_rest()
// have a known, currently-open engine bug where the global-pose cache doesn't
// reliably refresh for a skeleton built and rested entirely from code with no
// animation ever having played (godotengine/godot#103623; fix pending as of
// PR #104539). This method deliberately does NOT go through Skeleton3D at
// all, so it's unaffected by that bug either way — it walks our own
// parent_index chain directly. DFF frame lists always list a parent before
// its children (confirmed both by the format's export convention and by
// every real file parsed so far — Root=1/parent=0, Pelvis=2/parent=1, etc.),
// so a single forward pass is sufficient.
Array GTADffSkeleton::get_global_bind_transforms(bool convert_coordinates) const {
	std::vector<Transform3D> global_local_space(bones.size()); // in RAW (unconverted) local space, composed first
	for (size_t i = 0; i < bones.size(); ++i) {
		const Bone &bone = bones[i];
		if (bone.parent_index >= 0 && (size_t)bone.parent_index < i) {
			global_local_space[i] = global_local_space[(size_t)bone.parent_index] * bone.bind_transform;
		} else {
			global_local_space[i] = bone.bind_transform;
		}
	}

	Array out;
	for (size_t i = 0; i < bones.size(); ++i) {
		out.push_back(convert_coordinates ? _convert_z_up_to_y_up(global_local_space[i]) : global_local_space[i]);
	}
	return out;
}


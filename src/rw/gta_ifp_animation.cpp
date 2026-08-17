#include "gta_ifp_animation.h"

#include "gta_bone_tags.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>

using namespace godot;

namespace {

// Bounds-checked sequential byte reader — stands in for RwStreamRead<T>(stream)
// in the source, which reads sequentially off an already-open stream.
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

	// Reads exactly `n` bytes into a fixed char buffer and guarantees null
	// termination, for the 24-byte name fields (which are supposed to
	// already be null-terminated on disk, but don't trust that blindly).
	bool read_name24(char (&out)[24]) {
		if (!read_raw(out, 24)) {
			return false;
		}
		out[23] = '\0';
		return true;
	}
};

Vector3 convert_z_up_to_y_up(const Vector3 &v) {
	return v;
}

Quaternion convert_z_up_to_y_up(const Quaternion &q) {
	return q;
}

// [SOURCED] float(raw)/CompressValue, from extensions/FixedFloat.hpp
bool decode_frame_uncompressed(Cursor &c, bool has_translation, GTAIfpAnimation::Keyframe &kf) {
	float x, y, z, w, dt;
	if (!(c.read(x) && c.read(y) && c.read(z) && c.read(w) && c.read(dt))) {
		return false;
	}
	kf.rotation = convert_z_up_to_y_up(Quaternion(x, y, z, w)); // [SOURCED order] CQuaternion: x,y,z,w
	kf.delta_time = dt;
	if (has_translation) {
		float tx, ty, tz;
		if (!(c.read(tx) && c.read(ty) && c.read(tz))) {
			return false;
		}
		kf.translation = convert_z_up_to_y_up(Vector3(tx, ty, tz)); // [SOURCED order] CVector: x,y,z
	}
	return true;
}

bool decode_frame_compressed(Cursor &c, bool has_translation, GTAIfpAnimation::Keyframe &kf) {
	int16_t x, y, z, w, dt;
	if (!(c.read(x) && c.read(y) && c.read(z) && c.read(w) && c.read(dt))) {
		return false;
	}
	// [SOURCED] FixedQuat<int16,4096.f>, FixedFloat<int16,60.f,true>
	kf.rotation = convert_z_up_to_y_up(Quaternion((float)x / 4096.0f, (float)y / 4096.0f, (float)z / 4096.0f, (float)w / 4096.0f));
	kf.delta_time = (float)dt / 60.0f;
	if (has_translation) {
		int16_t tx, ty, tz;
		if (!(c.read(tx) && c.read(ty) && c.read(tz))) {
			return false;
		}
		// [SOURCED] FixedVector<int16,1024.f>
		kf.translation = convert_z_up_to_y_up(Vector3((float)tx / 1024.0f, (float)ty / 1024.0f, (float)tz / 1024.0f));
	}
	return true;
}

} // namespace

// [SOURCED] Enums/eBoneTag.h, via the shared gta_bone_tag_to_name() table. Only
// used when a sequence's bone_tag != -1, which per the source comment
// OVERWRITES the literal name field — most standard ped animations leave
// bone_tag at -1 (BONE_UNKNOWN) and rely on the literal name instead, so this
// table is the less-common path, not the primary one.
String GTAIfpAnimation::_bone_tag_to_name(int32_t tag) {
	return gta_bone_tag_to_name(tag);
}

GTAIfpAnimation::GTAIfpAnimation() {
}

GTAIfpAnimation::~GTAIfpAnimation() {
}

void GTAIfpAnimation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "bytes"), &GTAIfpAnimation::parse);
	ClassDB::bind_method(D_METHOD("get_block_name"), &GTAIfpAnimation::get_block_name);
	ClassDB::bind_method(D_METHOD("get_clip_count"), &GTAIfpAnimation::get_clip_count);
	ClassDB::bind_method(D_METHOD("get_clip_names"), &GTAIfpAnimation::get_clip_names);
	ClassDB::bind_method(D_METHOD("get_clip_info", "clip_name"), &GTAIfpAnimation::get_clip_info);
	ClassDB::bind_method(D_METHOD("get_bone_keyframes", "clip_name", "bone_name"), &GTAIfpAnimation::get_bone_keyframes);
	ClassDB::bind_method(D_METHOD("build_animation", "clip_name", "skeleton_track_path", "skeleton", "strip_root_translation"), &GTAIfpAnimation::build_animation, DEFVAL(nullptr), DEFVAL(true));
}

// [SOURCED, byte-exact port] CAnimManager::LoadAnimFile_ANP23
bool GTAIfpAnimation::_parse_anp23(const uint8_t *data, size_t size, bool is_anp3) {
	Cursor c{ data, size };

	char raw_block_name[24];
	if (!c.read_name24(raw_block_name)) {
		return false;
	}
	block_name = String::utf8(raw_block_name);

	uint32_t num_anims = 0;
	if (!c.read(num_anims)) {
		return false;
	}

	clips.clear();
	clips.reserve(num_anims);

	for (uint32_t anim_i = 0; anim_i < num_anims; ++anim_i) {
		AnimClip clip;

		char raw_anim_name[24];
		if (!c.read_name24(raw_anim_name)) {
			return false;
		}
		clip.name = String::utf8(raw_anim_name);

		uint32_t num_seq = 0;
		if (!c.read(num_seq)) {
			return false;
		}

		if (is_anp3) {
			uint32_t unused_buffer_size = 0;
			uint32_t unused_flags = 0;
			// ANP3 pre-allocates one shared buffer for all of this anim's
			// frames up front; we read straight off the byte stream instead,
			// so these two fields only need to be consumed to keep the
			// cursor aligned, not acted on.
			if (!(c.read(unused_buffer_size) && c.read(unused_flags))) {
				return false;
			}
		}

		clip.sequences.reserve(num_seq);
		float clip_max_time = 0.0f;

		for (uint32_t seq_i = 0; seq_i < num_seq; ++seq_i) {
			BoneSequence seq;

			char raw_seq_name[24];
			if (!c.read_name24(raw_seq_name)) {
				return false;
			}
			seq.raw_name = String::utf8(raw_seq_name);

			uint32_t frame_type = 0;
			uint32_t num_frames = 0;
			int32_t bone_tag = -1;
			if (!(c.read(frame_type) && c.read(num_frames) && c.read(bone_tag))) {
				return false;
			}
			seq.bone_tag = bone_tag;
			// [SOURCED precedence] "Only 1 of these will be valid in the end.
			// If BoneTag != -1 then it overwrites the name."
			seq.bone_name = (bone_tag != -1) ? _bone_tag_to_name(bone_tag) : seq.raw_name;

			seq.keyframes.reserve(num_frames);

			bool decode_ok = true;
			for (uint32_t f = 0; f < num_frames && decode_ok; ++f) {
				Keyframe kf;
				switch (frame_type) {
					case 1:
						seq.has_translation = false;
						seq.was_compressed = false;
						decode_ok = decode_frame_uncompressed(c, false, kf);
						break;
					case 2:
						seq.has_translation = true;
						seq.was_compressed = false;
						decode_ok = decode_frame_uncompressed(c, true, kf);
						break;
					case 3:
						seq.has_translation = false;
						seq.was_compressed = true;
						decode_ok = decode_frame_compressed(c, false, kf);
						break;
					case 4:
						seq.has_translation = true;
						seq.was_compressed = true;
						decode_ok = decode_frame_compressed(c, true, kf);
						break;
					default:
						UtilityFunctions::push_error("GTAIfpAnimation: invalid frame type ", (int)frame_type, " in sequence '", seq.raw_name, "'");
						decode_ok = false;
						break;
				}
				if (!decode_ok) {
					break;
				}
				// [SOURCED, re-derived from AnimBlendHierarchy.h ICalcTotalTime]
				// decode_frame_* fills Keyframe::delta_time straight from the
				// on-disk field, but that field is actually CUMULATIVE time
				// from the start of this sequence, not a step-to-step delta --
				// proven by the real engine's own post-load fixup, which
				// derives step deltas by subtracting adjacent raw values IN
				// PLACE after loading (which only makes sense if what's on
				// disk beforehand is cumulative). So `time` is just the raw
				// decoded value directly; no running sum here.
				kf.time = kf.delta_time;
				seq.keyframes.push_back(kf);
			}
			if (!decode_ok) {
				return false;
			}

			// [SOURCED] CAnimBlendHierarchy::ICalcTotalTime's in-place fixup:
			// walk backwards so each step only needs the still-untouched
			// value at j-1. First keyframe's delta_time is left as its own
			// cumulative value (= "time since sequence start", matching the
			// real engine treating frame 0 as having no true predecessor).
			for (size_t j = seq.keyframes.size(); j-- > 1;) {
				seq.keyframes[j].delta_time = seq.keyframes[j].time - seq.keyframes[j - 1].time;
			}

			// [SOURCED] CAnimBlendSequence::RemoveQuaternionFlips: consecutive
			// keyframe quaternions aren't guaranteed to be on the same side of
			// the double-cover (q and -q represent the same rotation) --
			// without this, Godot's rotation track slerps the "long way
			// around" between flipped pairs, which is exactly the kind of
			// per-bone, non-uniform deformation a real playtest surfaced
			// (some sequences never hit a flip and looked fine; most did).
			// Safe to do after the axis conversion above: it's an orthogonal
			// transform on the full (x,y,z,w) quaternion, so it preserves the
			// dot-product sign this check depends on (verified, not assumed).
			for (size_t i = 1; i < seq.keyframes.size(); ++i) {
				if (seq.keyframes[i - 1].rotation.dot(seq.keyframes[i].rotation) < 0.0f) {
					seq.keyframes[i].rotation = -seq.keyframes[i].rotation;
				}
			}

			// [SOURCED] ICalcTotalTime: max, across ALL non-empty sequences,
			// of that sequence's own last keyframe's (now-confirmed-cumulative)
			// time. No special-casing for 1-frame sequences -- the real
			// engine doesn't skip them, and the earlier "exclude 1-frame
			// tracks" heuristic here was working around a symptom of the
			// delta/cumulative mix-up above, not a real distinct issue.
			if (!seq.keyframes.empty()) {
				clip_max_time = std::max(clip_max_time, seq.keyframes.back().time);
			}
			clip.sequences.push_back(seq);
		}

		clip.total_time = clip_max_time;
		clips.push_back(clip);
	}

	return true;
}

bool GTAIfpAnimation::parse(const PackedByteArray &bytes) {
	clips.clear();
	block_name = String();

	if (bytes.size() < 8) {
		UtilityFunctions::push_error("GTAIfpAnimation: file too small to contain a section header");
		return false;
	}

	const uint8_t *data = bytes.ptr();
	char magic[5] = { 0, 0, 0, 0, 0 };
	std::memcpy(magic, data, 4);

	const bool is_anp3 = std::memcmp(magic, "ANP3", 4) == 0;
	const bool is_anp2 = std::memcmp(magic, "ANP2", 4) == 0;

	if (!is_anp3 && !is_anp2) {
		if (std::memcmp(magic, "ANPK", 4) == 0) {
			UtilityFunctions::push_error(
					"GTAIfpAnimation: this file uses the older ANPK format, which "
					"isn't implemented — gta-reversed's own ANPK reader is flagged "
					"untested/unverified against real files, so it wasn't ported. "
					"GTA:SA's own anim/ped.ifp should be ANP3, not ANPK.");
		} else {
			UtilityFunctions::push_error("GTAIfpAnimation: unrecognized section header '", String(magic), "' — not an .ifp file, or a format this parser doesn't know");
		}
		return false;
	}

	// The 8-byte IFPSectionHeader (4-byte FourCC + 4-byte size) is consumed
	// here; LoadAnimFile_ANP23 itself starts reading right after it.
	return _parse_anp23(data + 8, (size_t)bytes.size() - 8, is_anp3);
}

String GTAIfpAnimation::get_block_name() const {
	return block_name;
}

int GTAIfpAnimation::get_clip_count() const {
	return (int)clips.size();
}

PackedStringArray GTAIfpAnimation::get_clip_names() const {
	PackedStringArray out;
	out.resize((int)clips.size());
	for (size_t i = 0; i < clips.size(); ++i) {
		out[(int)i] = clips[i].name;
	}
	return out;
}

const GTAIfpAnimation::AnimClip *GTAIfpAnimation::_find_clip(const String &clip_name) const {
	for (const AnimClip &clip : clips) {
		if (clip.name.nocasecmp_to(clip_name) == 0) {
			return &clip;
		}
	}
	return nullptr;
}

const GTAIfpAnimation::BoneSequence *GTAIfpAnimation::_find_sequence(const AnimClip &clip, const String &bone_name) const {
	for (const BoneSequence &seq : clip.sequences) {
		if (seq.bone_name.nocasecmp_to(bone_name) == 0 || seq.raw_name.nocasecmp_to(bone_name) == 0) {
			return &seq;
		}
	}
	return nullptr;
}

Dictionary GTAIfpAnimation::get_clip_info(const String &clip_name) const {
	Dictionary out;
	const AnimClip *clip = _find_clip(clip_name);
	if (!clip) {
		return out;
	}

	out["name"] = clip->name;
	out["bone_count"] = (int)clip->sequences.size();
	out["total_time"] = clip->total_time;

	Array bones;
	for (const BoneSequence &seq : clip->sequences) {
		Dictionary b;
		b["name"] = seq.bone_name;
		b["raw_name"] = seq.raw_name;
		b["tag"] = seq.bone_tag;
		b["frame_count"] = (int)seq.keyframes.size();
		b["has_translation"] = seq.has_translation;
		b["was_compressed"] = seq.was_compressed;
		bones.push_back(b);
	}
	out["bones"] = bones;

	return out;
}

Array GTAIfpAnimation::get_bone_keyframes(const String &clip_name, const String &bone_name) const {
	Array out;
	const AnimClip *clip = _find_clip(clip_name);
	if (!clip) {
		return out;
	}
	const BoneSequence *seq = _find_sequence(*clip, bone_name);
	if (!seq) {
		return out;
	}

	for (const Keyframe &kf : seq->keyframes) {
		Dictionary d;
		d["time"] = kf.time;
		d["delta_time"] = kf.delta_time;
		d["rotation"] = kf.rotation;
		d["translation"] = kf.translation;
		out.push_back(d);
	}
	return out;
}

namespace {
// Skeleton3D::find_bone() is exact-match only; this project's established
// convention elsewhere (GTADffSkeleton::find_bone_by_name, _find_sequence
// just above) is case-insensitive, specifically because literal IFP bone
// names (used whenever bone_tag == -1, i.e. most sequences per the class
// header note) aren't guaranteed to match the tag table's exact casing.
int find_bone_case_insensitive(Skeleton3D *skeleton, const String &name) {
	const int count = skeleton->get_bone_count();
	for (int i = 0; i < count; ++i) {
		if (skeleton->get_bone_name(i).nocasecmp_to(name) == 0) {
			return i;
		}
	}
	return -1;
}
} // namespace

namespace {
// [SOURCED] eBoneTag 0 == "Root" (gta_bone_tags.h). Matches a sequence
// against the Root bone the same dual way (tag first, then literal name)
// everything else in this class already resolves names, so it's correct
// even for the common case where the sequence's bone_tag is -1 and only
// raw_name == "Root" identifies it.
bool is_root_bone_sequence(const GTAIfpAnimation::BoneSequence &seq) {
	if (seq.bone_tag == 0) {
		return true;
	}
	return seq.raw_name.nocasecmp_to("Root") == 0 || seq.bone_name.nocasecmp_to("Root") == 0;
}
} // namespace

Ref<Animation> GTAIfpAnimation::build_animation(const String &clip_name, const NodePath &skeleton_track_path, Skeleton3D *skeleton, bool strip_root_translation) const {
	Ref<Animation> anim;
	const AnimClip *clip = _find_clip(clip_name);
	if (!clip) {
		UtilityFunctions::push_error("GTAIfpAnimation: no such clip '", clip_name, "'");
		return anim;
	}

	anim.instantiate();
	anim->set_loop_mode(Animation::LOOP_LINEAR);

	int matched = 0;
	int skipped = 0;
	int root_translation_stripped = 0;
	const String track_prefix = String(skeleton_track_path) + String(":");

	for (const BoneSequence &seq : clip->sequences) {
		String target_bone_name;
		if (skeleton) {
			int bone_idx = find_bone_case_insensitive(skeleton, seq.bone_name);
			if (bone_idx == -1 && seq.raw_name != seq.bone_name) {
				bone_idx = find_bone_case_insensitive(skeleton, seq.raw_name);
			}
			if (bone_idx == -1) {
				UtilityFunctions::push_warning(
						"GTAIfpAnimation::build_animation('", clip_name, "'): sequence '",
						seq.raw_name, "' (tag ", (int)seq.bone_tag, ", resolved name '", seq.bone_name,
						"') matches no bone on the given skeleton -- skipped. If this is most/all "
						"sequences, the name scheme assumption is wrong; check get_clip_info() bone "
						"names against skeleton.get_bones() bone names directly.");
				++skipped;
				continue;
			}
			target_bone_name = skeleton->get_bone_name(bone_idx);
			++matched;
		} else {
			target_bone_name = seq.bone_name;
			++matched;
		}

		if (seq.keyframes.empty()) {
			continue;
		}

		// [SOURCED] CPed::ShoulderBoneRotation (Ped.cpp @ 0x5DF560):
		// GTA:SA procedurally computes the L/R Breast matrices each
		// frame from the UpperArm and Clavicle poses -- the IFP file's
		// breast keyframes are NOT played back by the engine. Emitting
		// them as normal tracks in Godot causes visible shoulder twist.
		if (seq.bone_tag == 301 || seq.bone_tag == 302) { // BONE_R_BREAST, BONE_L_BREAST
			continue;
		}
		if (seq.bone_name.nocasecmp_to("L Breast") == 0 || seq.bone_name.nocasecmp_to("R Breast") == 0) {
			continue;
		}

		const bool is_root = is_root_bone_sequence(seq);
		const bool root_stripped = seq.has_translation && is_root && strip_root_translation;

		// --- Cross-clip quaternion hemisphere alignment ---
		// [SOURCED] GTA:SA's RpAnimBlendClumpUpdateAnimations uses additive
		// blending (CAnimBlendNode::Update), not slerp crossfades. Godot's
		// AnimationPlayer crossfades via slerp between the outgoing and
		// incoming clip's rotation values. If two clips store the same bone
		// rotation on opposite sides of the quaternion double-cover (q vs -q),
		// the slerp takes the "long way around" = visible 360° spin during
		// the crossfade. Fix: when we have the skeleton, ensure each clip's
		// first keyframe is on the same hemisphere as the bone's rest
		// quaternion, then re-run the consecutive-flip pass.
		// This is safe because q and -q represent the identical rotation.
		Vector<Keyframe> aligned_keyframes;
		const Vector<Keyframe> *keyframes_to_use = nullptr;

		if (skeleton && !seq.keyframes.empty()) {
			int bone_idx = find_bone_case_insensitive(skeleton, target_bone_name);
			if (bone_idx != -1) {
				Quaternion rest_q;
				if (is_root) {
					rest_q = Quaternion(0.0f, 0.0f, 0.70710678f, 0.70710678f);
				} else {
					rest_q = skeleton->get_bone_rest(bone_idx).basis.get_rotation_quaternion();
				}
				aligned_keyframes.resize(seq.keyframes.size());
				for (size_t ki = 0; ki < seq.keyframes.size(); ++ki) {
					aligned_keyframes.write[ki] = seq.keyframes[ki];
				}
				// Align first keyframe to rest quaternion hemisphere
				if (aligned_keyframes[0].rotation.dot(rest_q) < 0.0f) {
					aligned_keyframes.write[0].rotation = -aligned_keyframes[0].rotation;
				}
				// Re-run consecutive flip removal
				for (int ki = 1; ki < aligned_keyframes.size(); ++ki) {
					if (aligned_keyframes[ki - 1].rotation.dot(aligned_keyframes[ki].rotation) < 0.0f) {
						aligned_keyframes.write[ki].rotation = -aligned_keyframes[ki].rotation;
					}
				}
				keyframes_to_use = &aligned_keyframes;
			}
		}
		if (!keyframes_to_use) {
			// Wrap the std::vector in a Godot Vector for uniform access below.
			aligned_keyframes.resize(seq.keyframes.size());
			for (size_t ki = 0; ki < seq.keyframes.size(); ++ki) {
				aligned_keyframes.write[ki] = seq.keyframes[ki];
			}
			keyframes_to_use = &aligned_keyframes;
		}

		const NodePath track_path = NodePath(track_prefix + target_bone_name);
		int rot_track = anim->add_track(Animation::TYPE_ROTATION_3D);
		anim->track_set_path(rot_track, track_path);
		for (int ki = 0; ki < keyframes_to_use->size(); ++ki) {
			const Keyframe &kf = (*keyframes_to_use)[ki];
			anim->rotation_track_insert_key(rot_track, kf.time, kf.rotation);
		}

		if (seq.has_translation) {
			if (!root_stripped) {
				int pos_track = anim->add_track(Animation::TYPE_POSITION_3D);
				anim->track_set_path(pos_track, track_path);
				for (int ki = 0; ki < keyframes_to_use->size(); ++ki) {
					const Keyframe &kf = (*keyframes_to_use)[ki];
					anim->position_track_insert_key(pos_track, kf.time, kf.translation);
				}
			} else {
				++root_translation_stripped;
			}
		}
	}

	// A few ms floor so a degenerate all-1-frame clip doesn't end up with
	// zero length (Animation dislikes that for playback/looping).
	anim->set_length(clip->total_time > 0.0f ? clip->total_time : 0.001f);

	UtilityFunctions::print(
			"GTAIfpAnimation::build_animation('", clip_name, "'): ", matched, " bone track(s) built, ",
			skipped, " sequence(s) skipped (no matching bone), ",
			root_translation_stripped, " root position track(s) stripped (strip_root_translation=",
			strip_root_translation, ")", skeleton ? "" : " [no skeleton given -- unfiltered]");

	return anim;
}

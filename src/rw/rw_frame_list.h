#ifndef GTAEDITOR_RW_FRAME_LIST_H
#define GTAEDITOR_RW_FRAME_LIST_H

#include "rw_chunk.h"
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/templates/vector.hpp>

/// A single frame in the RW frame hierarchy (bone/node in the model skeleton).
struct RWFrame {
	Vector3 rotation_matrix[3]; // 3x3 rotation matrix (row-major)
	Vector3 position;
	int32_t parent_index = 0;   // Index of parent frame (-1 = root)
	uint32_t flags = 0;
};

/// Parses an RW FRAME_LIST chunk containing the model's frame hierarchy.
class RWFrameList : public RWChunk {
public:
	uint32_t frame_count = 0;
	Vector<RWFrame> frames;

	/// Parse a FRAME_LIST chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_FRAME_LIST_H

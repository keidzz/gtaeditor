#include "rw_frame_list.h"

void RWFrameList::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_FRAME_LIST, "Expected FRAME_LIST chunk.");

	// The first child is a struct containing the frame data
	// (struct header was consumed by read_header through the parent chunk)
	frame_count = file->get_32();
	frames.resize(frame_count);

	for (uint32_t i = 0; i < frame_count; i++) {
		RWFrame &frame = frames.write[i];

		// Read 3x3 rotation matrix (9 floats)
		for (int row = 0; row < 3; row++) {
			float x = file->get_float();
			float y = file->get_float();
			float z = file->get_float();
			frame.rotation_matrix[row] = Vector3(x, y, z);
		}

		// Read position (3 floats)
		frame.position.x = file->get_float();
		frame.position.y = file->get_float();
		frame.position.z = file->get_float();

		// Parent index and flags
		frame.parent_index = file->get_32();
		frame.flags        = file->get_32();
	}

	// Skip remaining data (extension chunks, etc.)
	skip(file);
}

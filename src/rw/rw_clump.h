#ifndef GTAEDITOR_RW_CLUMP_H
#define GTAEDITOR_RW_CLUMP_H

#include "rw_chunk.h"
#include "rw_frame_list.h"
#include "rw_geometry_list.h"

/// Top-level RenderWare container for a 3D model (.dff file).
/// Contains a frame hierarchy and a geometry list.
class RWClump : public RWChunk {
public:
	uint32_t atomic_count = 0;
	uint32_t light_count = 0;
	uint32_t camera_count = 0;
	bool is_valid = false;

	RWFrameList frame_list;
	RWGeometryList geometry_list;

	/// Parse a CLUMP chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_CLUMP_H

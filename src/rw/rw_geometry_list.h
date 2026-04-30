#ifndef GTAEDITOR_RW_GEOMETRY_LIST_H
#define GTAEDITOR_RW_GEOMETRY_LIST_H

#include "rw_chunk.h"
#include "rw_geometry.h"
#include <godot_cpp/templates/vector.hpp>

/// Container chunk holding an array of geometry objects within a clump.
class RWGeometryList : public RWChunk {
public:
	uint32_t geometry_count = 0;
	Vector<RWGeometry> geometries;

	/// Parse a GEOMETRY_LIST chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_GEOMETRY_LIST_H

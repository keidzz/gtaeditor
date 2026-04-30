#include "rw_geometry_list.h"

void RWGeometryList::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_GEOMETRY_LIST, "Expected GEOMETRY_LIST chunk.");

	// Read struct sub-chunk
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	geometry_count = file->get_32();

	// Parse each geometry
	geometries.resize(geometry_count);
	for (uint32_t i = 0; i < geometry_count; i++) {
		geometries.write[i].parse(file);
	}
}

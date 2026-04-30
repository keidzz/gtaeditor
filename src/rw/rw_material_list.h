#ifndef GTAEDITOR_RW_MATERIAL_LIST_H
#define GTAEDITOR_RW_MATERIAL_LIST_H

#include "rw_chunk.h"
#include "rw_material.h"
#include <godot_cpp/templates/vector.hpp>

/// Parses an RW MATERIAL_LIST chunk containing an array of materials.
/// Materials may be inline (-1 index) or reference a previously defined material.
class RWMaterialList : public RWChunk {
public:
	uint32_t material_count = 0;
	Vector<int32_t> indices;
	Vector<RWMaterial> materials;

	/// Parse a MATERIAL_LIST chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_MATERIAL_LIST_H

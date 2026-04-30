#include "rw_material_list.h"

void RWMaterialList::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_MATERIAL_LIST, "Expected MATERIAL_LIST chunk.");

	// Read struct sub-chunk
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	material_count = file->get_32();

	// Read material indices. A value of -1 means the material is defined
	// inline right after this list. Other values reference existing materials.
	indices.resize(material_count);
	for (uint32_t i = 0; i < material_count; i++) {
		// Decode signed int32 from unsigned (GDScript workaround preserved for correctness)
		uint32_t raw = file->get_32();
		indices.write[i] = static_cast<int32_t>(raw);
	}

	// Parse each material
	materials.resize(material_count);
	for (uint32_t i = 0; i < material_count; i++) {
		if (indices[i] == -1) {
			// Inline material definition
			materials.write[i].parse(file);
		} else {
			// Reference to a previous material (rare in GTA SA, but handle it)
			ERR_FAIL_COND_MSG(indices[i] < 0 || indices[i] >= (int32_t)i,
				"Material reference index out of range.");
			materials.write[i] = materials[indices[i]];
		}
	}

	skip(file);
}

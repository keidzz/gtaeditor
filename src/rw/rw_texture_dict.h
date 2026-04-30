#ifndef GTAEDITOR_RW_TEXTURE_DICT_H
#define GTAEDITOR_RW_TEXTURE_DICT_H

#include "rw_chunk.h"
#include "rw_raster.h"
#include <godot_cpp/templates/vector.hpp>

/// RenderWare Texture Dictionary (.txd file).
/// Contains a collection of raster textures that can be referenced by materials.
class RWTextureDict : public RWChunk {
public:
	uint16_t texture_count = 0;
	uint16_t device_id = 0;
	Vector<RWRaster> textures;

	/// Parse a TEXTURE_DICT chunk from the current file position.
	void parse(Ref<FileAccess> file);
};

#endif // GTAEDITOR_RW_TEXTURE_DICT_H

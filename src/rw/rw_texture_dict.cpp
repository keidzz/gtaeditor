#include "rw_texture_dict.h"

void RWTextureDict::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_TEXTURE_DICT, "Expected TEXTURE_DICT chunk.");

	// Read struct sub-chunk
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	texture_count = file->get_16();
	device_id     = file->get_16();

	// Parse each raster texture
	textures.resize(texture_count);
	for (uint16_t i = 0; i < texture_count; i++) {
		textures.write[i].parse(file);
	}

	// Seek to the end of the texture dictionary
	file->seek(data_start + size);
}

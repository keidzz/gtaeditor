#include "rw_texture.h"
#include "rw_string.h"

void RWTexture::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_TEXTURE, "Expected TEXTURE chunk.");

	// Skip the struct sub-chunk (filter mode, addressing)
	RWChunk struct_chunk;
	struct_chunk.read_header(file);
	struct_chunk.skip(file);

	// Read the texture name string
	RWString name_str;
	name_str.parse(file);
	texture_name = name_str.value;

	// Read the alpha mask name string
	RWString mask_str;
	mask_str.parse(file);
	mask_name = mask_str.value;

	// Skip any remaining extension data
	skip(file);
}

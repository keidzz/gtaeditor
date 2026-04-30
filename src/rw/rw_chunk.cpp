#include "rw_chunk.h"

uint32_t RWChunk::get_version() const {
	if (library_id & 0xFFFF0000) {
		return ((library_id >> 14) & 0x3FF00) + 0x30000 | ((library_id >> 16) & 0x3F);
	}
	return library_id << 8;
}

uint32_t RWChunk::get_build() const {
	if (library_id & 0xFFFF0000) {
		return library_id & 0xFFFF;
	}
	return 0;
}

void RWChunk::read_header(Ref<FileAccess> file) {
	type       = file->get_32();
	size       = file->get_32();
	library_id = file->get_32();
	data_start = file->get_position();
}

void RWChunk::skip(Ref<FileAccess> file) const {
	file->seek(data_start + size);
}

#ifndef GTAEDITOR_RW_CHUNK_H
#define GTAEDITOR_RW_CHUNK_H

#include <godot_cpp/classes/file_access.hpp>

using namespace godot;

/// Base class for all RenderWare binary stream chunks.
/// Every RW chunk has a 12-byte header: type (4), size (4), library_id (4).
/// Reference: https://gtamods.com/wiki/RenderWare_binary_stream_file
class RWChunk {
public:
	/// Standard RenderWare chunk type identifiers.
	enum ChunkType : uint32_t {
		RW_STRUCT        = 0x01,
		RW_STRING        = 0x02,
		RW_TEXTURE       = 0x06,
		RW_MATERIAL      = 0x07,
		RW_MATERIAL_LIST = 0x08,
		RW_FRAME_LIST    = 0x0E,
		RW_GEOMETRY      = 0x0F,
		RW_CLUMP         = 0x10,
		RW_RASTER        = 0x15,
		RW_TEXTURE_DICT  = 0x16,
		RW_GEOMETRY_LIST = 0x1A,
	};

	uint32_t type = 0;
	uint32_t size = 0;
	uint32_t library_id = 0;

	/// File position right after the 12-byte header (start of chunk data).
	uint64_t data_start = 0;

	/// Decode the RW library version from the library_id field.
	uint32_t get_version() const;

	/// Decode the build number from the library_id field.
	uint32_t get_build() const;

	/// Read the 12-byte chunk header from the current file position.
	void read_header(Ref<FileAccess> file);

	/// Skip past this chunk's data (seek to data_start + size).
	void skip(Ref<FileAccess> file) const;
};

#endif // GTAEDITOR_RW_CHUNK_H

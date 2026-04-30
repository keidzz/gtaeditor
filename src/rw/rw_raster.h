#ifndef GTAEDITOR_RW_RASTER_H
#define GTAEDITOR_RW_RASTER_H

#include "rw_chunk.h"
#include <godot_cpp/classes/image.hpp>

/// Raster format flags (from the RenderWare specification).
enum RasterFormat : uint32_t {
	FORMAT_DEFAULT         = 0x0000,
	FORMAT_1555            = 0x0100,
	FORMAT_565             = 0x0200,
	FORMAT_4444            = 0x0300,
	FORMAT_LUM8            = 0x0400,
	FORMAT_8888            = 0x0500,
	FORMAT_888             = 0x0600,
	FORMAT_555             = 0x0A00,
	FORMAT_EXT_AUTO_MIPMAP = 0x1000,
	FORMAT_EXT_PAL8        = 0x2000,
	FORMAT_EXT_PAL4        = 0x4000,
	FORMAT_EXT_MIPMAP      = 0x8000,
};

/// Parses an RW RASTER chunk containing texture image data.
/// Supports DXT1, DXT3 compression and paletted/uncompressed formats.
/// Reference: https://gtamods.com/wiki/Texture_Native_Struct
class RWRaster : public RWChunk {
public:
	uint32_t platform_id = 0;
	uint8_t filter_mode = 0;
	uint8_t u_addressing = 0;
	uint8_t v_addressing = 0;
	String name;
	String mask_name;
	uint32_t raster_format = 0;
	bool has_alpha = false;
	uint16_t width = 0;
	uint16_t height = 0;
	uint8_t depth = 0;
	uint8_t num_levels = 0;
	uint8_t raster_type = 0;
	uint8_t compression = 0;

	/// Parse the raster header from the file. Does NOT decode image data yet.
	void parse(Ref<FileAccess> file);

	/// Decode and return the image data. Call after parse().
	/// This reads from the file at the stored image offset.
	Ref<Image> load_image();

private:
	Ref<FileAccess> _file;     // Kept for lazy image loading
	uint64_t _image_start = 0; // File offset where image data begins

	Ref<Image> _load_dxt1();
	Ref<Image> _load_dxt3();

	/// Read pixel data with 4-byte alignment stripping.
	/// Reads 'length' pixels of 'read' bytes each, skipping padding to align to 4 bytes.
	PackedByteArray _unpad(int length, int read);
};

#endif // GTAEDITOR_RW_RASTER_H

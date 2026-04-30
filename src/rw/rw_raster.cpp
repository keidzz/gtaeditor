#include "rw_raster.h"

void RWRaster::parse(Ref<FileAccess> file) {
	read_header(file);
	ERR_FAIL_COND_MSG(type != RW_RASTER, "Expected RASTER chunk.");

	// Read struct sub-chunk
	RWChunk struct_chunk;
	struct_chunk.read_header(file);

	platform_id = file->get_32();
	filter_mode = file->get_8();

	uint8_t uv_addr = file->get_8();
	u_addressing = uv_addr >> 4;
	v_addressing = uv_addr & 0x0F;

	file->get_16(); // padding

	// Texture names (32 bytes each, null-padded)
	name      = file->get_buffer(32).get_string_from_ascii();
	mask_name = file->get_buffer(32).get_string_from_ascii();

	raster_format = file->get_32();
	has_alpha     = file->get_32() > 0;
	width         = file->get_16();
	height        = file->get_16();
	depth         = file->get_8();
	num_levels    = file->get_8();
	raster_type   = file->get_8();
	compression   = file->get_8();

	// Store file reference and image data offset for lazy loading
	_file        = file;
	_image_start = file->get_position();

	// Skip past the image data for now
	skip(file);
}

// ── Image loading ────────────────────────────────────────────────────────────

Ref<Image> RWRaster::load_image() {
	ERR_FAIL_COND_V(_file.is_null(), Ref<Image>());
	_file->seek(_image_start);

	// Handle DXT compression (GTA SA specific)
	if (compression == 8) {
		return _load_dxt1();
	} else if (compression == 9) {
		return _load_dxt3();
	}

	// Determine pixel format from raster_format
	Image::Format img_format;
	int bytes_per_pixel;

	switch (raster_format & 0x0F00) {
		case FORMAT_8888:
			img_format = Image::FORMAT_RGBA8;
			bytes_per_pixel = 4;
			break;
		case FORMAT_888:
			img_format = Image::FORMAT_RGB8;
			bytes_per_pixel = 3;
			break;
		default:
			ERR_FAIL_V_MSG(Ref<Image>(), "Unsupported raster format: " + String::num_int64(raster_format & 0x0F00, 16));
	}

	// ── Paletted textures ────────────────────────────────────────────────
	if (raster_format & (FORMAT_EXT_PAL8 | FORMAT_EXT_PAL4)) {
		int palette_size = (raster_format & FORMAT_EXT_PAL4) ? 16 : 256;

		// Read palette as an image row for color lookup
		PackedByteArray palette_data = _unpad(palette_size, bytes_per_pixel);
		Ref<Image> palette = Image::create_from_data(palette_size, 1, false, img_format, palette_data);

		// Create output image
		bool use_mipmaps = (raster_format & FORMAT_EXT_MIPMAP) != 0;
		Ref<Image> result = Image::create(width, height, use_mipmaps, img_format);

		_file->get_32(); // Skip size field

		// Map each pixel index to a palette color
		for (int i = 0; i < width * height; i++) {
			int x = i % width;
			int y = i / width;
			uint8_t idx = _file->get_8();
			Color c = palette->get_pixel(idx, 0);
			result->set_pixel(x, y, c);
		}
		return result;
	}

	// ── Uncompressed textures ────────────────────────────────────────────
	PackedByteArray data;
	int mip_w = width;
	int mip_h = height;

	for (int i = 0; i < num_levels; i++) {
		_file->get_32(); // raster_size per mip level
		PackedByteArray level_data = _unpad(mip_w * mip_h, bytes_per_pixel);
		data.append_array(level_data);
		mip_w /= 2;
		mip_h /= 2;
	}

	bool use_mipmaps = (raster_format & FORMAT_EXT_MIPMAP) != 0;
	Ref<Image> result = Image::create_from_data(width, height, use_mipmaps, img_format, data);

	if (raster_format & FORMAT_EXT_AUTO_MIPMAP) {
		result->generate_mipmaps();
	}

	// GTA stores colors as BGRA, Godot expects RGBA — swap R and B channels.
	// We do this by bulk-accessing the pixel data for maximum performance.
	int total_pixels = width * height;
	for (int i = 0; i < total_pixels; i++) {
		int x = i % width;
		int y = i / width;
		Color old = result->get_pixel(x, y);
		result->set_pixel(x, y, Color(old.b, old.g, old.r, old.a));
	}

	return result;
}

// ── DXT1 loader ──────────────────────────────────────────────────────────────

Ref<Image> RWRaster::_load_dxt1() {
	uint32_t data_size = _file->get_32();
	PackedByteArray compressed = _file->get_buffer(data_size);

	// DXT1: 8 bytes per 4x4 block
	int block_w = (width + 3) / 4;
	int block_h = (height + 3) / 4;
	uint32_t expected_base = block_w * block_h * 8;

	bool use_mipmaps = num_levels > 1 && data_size > expected_base;

	Ref<Image> img = Image::create_from_data(
		width, height, use_mipmaps, Image::FORMAT_DXT1, compressed
	);

	// DXT1 with alpha needs decompression because Godot's DXT1 doesn't support alpha
	if (has_alpha) {
		img->decompress();
	}

	return img;
}

// ── DXT3 loader ──────────────────────────────────────────────────────────────

Ref<Image> RWRaster::_load_dxt3() {
	uint32_t data_size = _file->get_32();
	PackedByteArray compressed = _file->get_buffer(data_size);

	// DXT3: 16 bytes per 4x4 block; DXT1: 8 bytes per 4x4 block
	int block_w = (width + 3) / 4;
	int block_h = (height + 3) / 4;
	uint32_t expected_dxt3 = block_w * block_h * 16;
	uint32_t expected_dxt1 = block_w * block_h * 8;

	bool use_mipmaps = num_levels > 1;

	// Some files are mislabeled as DXT3 but contain DXT1 data.
	// Detect this by comparing actual data size to expected sizes.
	if (data_size == expected_dxt1 || data_size < expected_dxt3) {
		WARN_PRINT("Texture '" + name + "' marked as DXT3 but data matches DXT1.");
		if (data_size <= expected_dxt1) {
			use_mipmaps = false;
		}
		Ref<Image> img = Image::create_from_data(
			width, height, use_mipmaps, Image::FORMAT_DXT1, compressed
		);
		if (has_alpha) {
			img->decompress();
		}
		return img;
	}

	// Actual DXT3 data
	if (data_size <= expected_dxt3) {
		use_mipmaps = false;
	}

	return Image::create_from_data(
		width, height, use_mipmaps, Image::FORMAT_DXT3, compressed
	);
}

// ── Utility ──────────────────────────────────────────────────────────────────

PackedByteArray RWRaster::_unpad(int length, int read) {
	// GTA stores pixel data with 4-byte alignment per pixel.
	// For formats with < 4 bytes per pixel, trailing padding bytes are added.
	int stride = 4; // Each pixel is padded to 4 bytes
	int pad = stride - read;

	if (pad == 0) {
		// No padding needed — bulk read the entire block
		return _file->get_buffer(length * read);
	}

	// Read with padding removal
	PackedByteArray result;
	result.resize(length * read);
	uint8_t *dst = result.ptrw();

	// Read the entire padded block at once, then extract
	PackedByteArray raw = _file->get_buffer(length * stride);
	const uint8_t *src = raw.ptr();

	for (int i = 0; i < length; i++) {
		memcpy(dst + i * read, src + i * stride, read);
	}

	return result;
}

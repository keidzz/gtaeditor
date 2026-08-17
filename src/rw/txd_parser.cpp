#include "txd_parser.h"
#include "binary_reader.h"

#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// Texture decoders — static functions for each raster format
// =============================================================================

// Decode DXT1 compressed texture (4x4 blocks, 8 bytes each).
// Godot supports FORMAT_DXT1 natively, so we can just pass the raw compressed bytes for opaque textures!
// For textures with alpha, we must decode to RGBA8 because Godot's Vulkan FORMAT_DXT1 drops the 1-bit alpha.
static Ref<Image> decode_dxt1(BinaryReader &reader, int width, int height, bool has_alpha) {
	if (width <= 0 || height <= 0)
		return Ref<Image>();
	
	if (!has_alpha) {
		int blocks_x = MAX(1, (width + 3) / 4);
		int blocks_y = MAX(1, (height + 3) / 4);
		int size = blocks_x * blocks_y * 8;
		
		PackedByteArray raw = reader.read_bytes(size);
		return Image::create_from_data(width, height, false, Image::FORMAT_DXT1, raw);
	}

	// Manual decode for DXT1 with 1-bit alpha
	PackedByteArray pixels;
	pixels.resize(width * height * 4);
	uint8_t *out = pixels.ptrw();

	// Initialize to opaque white.
	for (int i = 0; i < width * height * 4; i += 4) {
		out[i + 0] = 255;
		out[i + 1] = 255;
		out[i + 2] = 255;
		out[i + 3] = 255;
	}

	for (int y = 0; y < height; y += 4) {
		for (int x = 0; x < width; x += 4) {
			uint32_t code = reader.read_uint32();
			uint32_t indices = reader.read_uint32();

			// Decode two 565 colors from the code word.
			uint16_t c0_raw = code & 0xFFFF;
			uint16_t c1_raw = (code >> 16) & 0xFFFF;

			uint8_t c0_r = ((c0_raw >> 11) & 0x1F) << 3;
			uint8_t c0_g = ((c0_raw >> 5) & 0x3F) << 2;
			uint8_t c0_b = (c0_raw & 0x1F) << 3;

			uint8_t c1_r = ((c1_raw >> 11) & 0x1F) << 3;
			uint8_t c1_g = ((c1_raw >> 5) & 0x3F) << 2;
			uint8_t c1_b = (c1_raw & 0x1F) << 3;

			uint8_t c[4][4]; // [color_index][rgba]

			c[0][0] = c0_r;
			c[0][1] = c0_g;
			c[0][2] = c0_b;
			c[0][3] = 255;
			c[1][0] = c1_r;
			c[1][1] = c1_g;
			c[1][2] = c1_b;
			c[1][3] = 255;

			if (c0_raw > c1_raw) {
				c[2][0] = (2 * c0_r + c1_r) / 3;
				c[2][1] = (2 * c0_g + c1_g) / 3;
				c[2][2] = (2 * c0_b + c1_b) / 3;
				c[2][3] = 255;
				c[3][0] = (c0_r + 2 * c1_r) / 3;
				c[3][1] = (c0_g + 2 * c1_g) / 3;
				c[3][2] = (c0_b + 2 * c1_b) / 3;
				c[3][3] = 255;
			} else {
				c[2][0] = (c0_r + c1_r) / 2;
				c[2][1] = (c0_g + c1_g) / 2;
				c[2][2] = (c0_b + c1_b) / 2;
				c[2][3] = 255;
				c[3][0] = 0;
				c[3][1] = 0;
				c[3][2] = 0;
				c[3][3] = 0; // Alpha 0
			}

			for (int yy = 0; yy < 4; yy++) {
				for (int xx = 0; xx < 4; xx++) {
					int idx = indices & 3;
					int px = x + xx;
					int py = y + yy;
					if (px < width && py < height) {
						int offset = (py * width + px) * 4;
						out[offset + 0] = c[idx][0];
						out[offset + 1] = c[idx][1];
						out[offset + 2] = c[idx][2];
						out[offset + 3] = c[idx][3];
					}
					indices >>= 2;
				}
			}
		}
	}

	return Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, pixels);
}

// Decode DXT3 compressed texture (4x4 blocks, 16 bytes each).
// Godot supports FORMAT_DXT3 natively, so we can pass the raw compressed bytes.
static Ref<Image> decode_dxt3(BinaryReader &reader, int width, int height) {
	if (width <= 0 || height <= 0)
		return Ref<Image>();
		
	int blocks_x = MAX(1, (width + 3) / 4);
	int blocks_y = MAX(1, (height + 3) / 4);
	int size = blocks_x * blocks_y * 16;
	
	PackedByteArray raw = reader.read_bytes(size);
	return Image::create_from_data(width, height, false, Image::FORMAT_DXT3, raw);
}

// Decode raw BGRA color block (uncompressed).
static Ref<Image> decode_color_block(BinaryReader &reader, int width, int height, bool has_alpha) {
	if (width <= 0 || height <= 0)
		return Ref<Image>();
	int pixel_count = width * height;
	PackedByteArray raw = reader.read_bytes(pixel_count * 4);
	PackedByteArray pixels;
	pixels.resize(pixel_count * 4);
	uint8_t *out = pixels.ptrw();
	const uint8_t *in = raw.ptr();

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int src = (y * width + x) * 4;
			int dst = (y * width + x) * 4;
			// BGRA → RGBA
			out[dst + 0] = in[src + 2]; // R
			out[dst + 1] = in[src + 1]; // G
			out[dst + 2] = in[src + 0]; // B
			out[dst + 3] = has_alpha ? in[src + 3] : 255; // A
		}
	}

	return Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, pixels);
}

// Decode palette8 (256-color palette + indexed pixels).
static Ref<Image> decode_palette8(BinaryReader &reader, int width, int height) {
	if (width <= 0 || height <= 0)
		return Ref<Image>();
	// Read 256-color palette (1024 bytes = 256 * RGBA).
	PackedByteArray palette_data = reader.read_bytes(1024);
	reader.skip(4); // Data size field

	int pixel_count = width * height;
	PackedByteArray index_data = reader.read_bytes(pixel_count);
	PackedByteArray pixels;
	pixels.resize(pixel_count * 4);
	uint8_t *out = pixels.ptrw();
	const uint8_t *pal = palette_data.ptr();
	const uint8_t *idx = index_data.ptr();

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int src_idx = idx[y * width + x] * 4;
			int dst = (y * width + x) * 4;
			out[dst + 0] = pal[src_idx + 0]; // R
			out[dst + 1] = pal[src_idx + 1]; // G
			out[dst + 2] = pal[src_idx + 2]; // B
			out[dst + 3] = pal[src_idx + 3]; // A
		}
	}

	return Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, pixels);
}

// =============================================================================
// TXD section parser
// =============================================================================

static void parse_txd_section(BinaryReader &reader, const RWSectionHeader &parent,
							  HashMap<String, TxdTexture> &textures, int16_t &texture_count, int16_t &processed) {
	int64_t end = reader.get_position() + parent.size;

	while (reader.get_position() < end && reader.can_read(12) && processed < texture_count) {
		RWSectionHeader header = reader.read_section_header();
		int64_t section_end = reader.get_position() + header.size;

		switch (header.type) {
			case RWSectionType::Struct: {
				if (parent.type == RWSectionType::TextureNative) {
					// Parse the actual texture data.
					reader.skip(8); // Platform ID + filter mode

					String name = reader.read_string(32).to_lower();
					String alpha_name = reader.read_string(32).to_lower();

					uint32_t raw_raster_format = reader.read_uint32();
					reader.skip(4); // Alpha or FourCC

					int16_t tex_width = reader.read_int16();
					int16_t tex_height = reader.read_int16();
					reader.skip(4); // BPP, mipmaps, raster type, DXT number

					// Determine format and decode.
					uint32_t base_format = raw_raster_format & 0x0FFF;
					uint32_t ext_format = raw_raster_format & 0xF000;

					Ref<Image> image;

					if (ext_format & RASTER_EXT_PALETTE8) {
						image = decode_palette8(reader, tex_width, tex_height);
					} else {
						reader.skip(4); // Data size

						switch (base_format) {
							case RASTER_565:
								image = decode_dxt1(reader, tex_width, tex_height, false);
								break;
							case RASTER_1555:
								image = decode_dxt1(reader, tex_width, tex_height, true);
								break;
							case RASTER_4444:
								image = decode_dxt3(reader, tex_width, tex_height);
								break;
							case RASTER_888:
								image = decode_color_block(reader, tex_width, tex_height, false);
								break;
							case RASTER_8888:
								image = decode_color_block(reader, tex_width, tex_height, true);
								break;
							default:
								break;
						}
					}

					if (image.is_valid() && !image->is_empty()) {
						TxdTexture tex;
						tex.name = name;
						tex.alpha_name = alpha_name;
						tex.texture = ImageTexture::create_from_image(image);
						// Format-based guesses (e.g. DXT3/1555/8888 "always have
						// alpha") are useless — SA textures almost always carry
						// an alpha channel even when fully opaque, so nearly
						// everything would look "transparent". Check actual
						// pixel content instead (no-op on opaque formats).
						tex.has_alpha_content = image->detect_alpha() != Image::ALPHA_NONE;

						if (!name.is_empty()) {
							textures[name] = tex;
						}
						if (!alpha_name.is_empty() && alpha_name != name) {
							textures[alpha_name] = tex;
						}
					}

					processed++;
				} else if (parent.type == RWSectionType::TextureDictionary) {
					texture_count = reader.read_int16();
					reader.skip(2);
				}
			} break;

			case RWSectionType::Extension:
			case RWSectionType::TextureDictionary:
			case RWSectionType::TextureNative:
				parse_txd_section(reader, header, textures, texture_count, processed);
				break;

			default:
				break;
		}

		reader.set_position(section_end);
	}
}

// =============================================================================
// Public API
// =============================================================================

HashMap<String, TxdTexture> TxdParser::parse(const PackedByteArray &p_data) {
	HashMap<String, TxdTexture> textures;

	if (p_data.is_empty()) {
		return textures;
	}

	BinaryReader reader(p_data);

	int16_t texture_count = INT16_MAX;
	int16_t processed = 0;

	RWSectionHeader root = reader.read_section_header();
	parse_txd_section(reader, root, textures, texture_count, processed);

	return textures;
}

#include "gta_txd_texture.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace {

// [SOURCED] librw src/rwbase.h
constexpr uint32_t ID_STRUCT = 0x01;
constexpr uint32_t ID_TEXTURENATIVE = 0x15;
constexpr uint32_t ID_TEXDICTIONARY = 0x16;
constexpr uint32_t ID_NAOBJECT = 0x00;

constexpr uint32_t PLATFORM_D3D9 = 9;

// [SOURCED] librw src/rwobjects.h, Raster::Format
constexpr uint32_t RASTER_PAL8 = 0x2000;
constexpr uint32_t RASTER_PAL4 = 0x4000;

// [SOURCED] librw src/d3d/rwd3d.h
constexpr uint32_t D3DFMT_A8R8G8B8 = 21;
constexpr uint32_t D3DFMT_X8R8G8B8 = 22;
constexpr uint32_t D3DFMT_P8 = 41;
constexpr uint32_t make_fourcc(char a, char b, char c, char d) {
	return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}
const uint32_t D3DFMT_DXT1 = make_fourcc('D', 'X', 'T', '1');
const uint32_t D3DFMT_DXT3 = make_fourcc('D', 'X', 'T', '3');
const uint32_t D3DFMT_DXT5 = make_fourcc('D', 'X', 'T', '5');

struct Cursor {
	const uint8_t *data;
	size_t size;
	size_t pos = 0;
	bool ok = true;

	bool read_raw(void *dst, size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		std::memcpy(dst, data + pos, n);
		pos += n;
		return true;
	}
	template <typename T>
	bool read(T &out) {
		return read_raw(&out, sizeof(T));
	}
	bool skip(size_t n) {
		if (!ok || pos + n > size) {
			ok = false;
			return false;
		}
		pos += n;
		return true;
	}
	bool read_name32(char (&out)[32]) {
		if (!read_raw(out, 32)) {
			return false;
		}
		out[31] = '\0';
		return true;
	}
};

struct ChunkHeader {
	uint32_t type = 0;
	uint32_t length = 0;
	uint32_t library_id = 0;
};

bool read_chunk_header(Cursor &c, ChunkHeader &out) {
	return c.read(out.type) && c.read(out.length) && c.read(out.library_id);
}

bool find_chunk(Cursor &c, uint32_t type, ChunkHeader &out) {
	while (true) {
		ChunkHeader h;
		if (!read_chunk_header(c, h)) {
			return false;
		}
		if (h.type == ID_NAOBJECT) {
			return false;
		}
		if (h.type == type) {
			out = h;
			return true;
		}
		if (!c.skip(h.length)) {
			return false;
		}
	}
}

} // namespace

GTATxdTexture::GTATxdTexture() {
}

GTATxdTexture::~GTATxdTexture() {
}

void GTATxdTexture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "bytes"), &GTATxdTexture::parse);
	ClassDB::bind_method(D_METHOD("get_texture_count"), &GTATxdTexture::get_texture_count);
	ClassDB::bind_method(D_METHOD("get_texture_names"), &GTATxdTexture::get_texture_names);
	ClassDB::bind_method(D_METHOD("get_texture_info", "name"), &GTATxdTexture::get_texture_info);
	ClassDB::bind_method(D_METHOD("build_texture", "name"), &GTATxdTexture::build_texture);
	ClassDB::bind_method(D_METHOD("build_texture_by_index", "index"), &GTATxdTexture::build_texture_by_index);
}

bool GTATxdTexture::parse(const PackedByteArray &bytes) {
	textures.clear();

	Cursor c{ bytes.ptr(), (size_t)bytes.size() };

	ChunkHeader txd_h;
	if (!find_chunk(c, ID_TEXDICTIONARY, txd_h)) {
		UtilityFunctions::push_error("GTATxdTexture: no TEXDICTIONARY chunk found — not a valid .txd");
		return false;
	}

	ChunkHeader struct_h;
	if (!find_chunk(c, ID_STRUCT, struct_h)) {
		UtilityFunctions::push_error("GTATxdTexture: TEXDICTIONARY has no STRUCT sub-chunk");
		return false;
	}

	int16_t num_tex = 0;
	int16_t device_id = 0;
	if (!(c.read(num_tex) && c.read(device_id)) || num_tex < 0) {
		UtilityFunctions::push_error("GTATxdTexture: could not read texture count");
		return false;
	}

	textures.reserve((size_t)num_tex);

	for (int16_t i = 0; i < num_tex; ++i) {
		ChunkHeader tn_h;
		if (!find_chunk(c, ID_TEXTURENATIVE, tn_h)) {
			UtilityFunctions::push_error("GTATxdTexture: expected TEXTURENATIVE chunk #", (int)i);
			return false;
		}
		const size_t tn_end = c.pos + tn_h.length;

		ChunkHeader tstruct_h;
		if (!find_chunk(c, ID_STRUCT, tstruct_h)) {
			UtilityFunctions::push_error("GTATxdTexture: TEXTURENATIVE has no STRUCT sub-chunk");
			return false;
		}

		uint32_t platform = 0;
		if (!c.read(platform)) {
			UtilityFunctions::push_error("GTATxdTexture: truncated TEXTURENATIVE platform field");
			return false;
		}
		if (platform != PLATFORM_D3D9) {
			UtilityFunctions::push_error("GTATxdTexture: texture #", (int)i, " uses platform ", (int)platform, ", not D3D9 (", (int)PLATFORM_D3D9, ") — unexpected for a GTA:SA PC asset, not supported");
			return false;
		}

		TextureEntry tex;
		uint32_t filter_addressing = 0;
		char raw_name[32];
		char raw_mask[32];
		if (!(c.read(filter_addressing) && c.read_name32(raw_name) && c.read_name32(raw_mask))) {
			UtilityFunctions::push_error("GTATxdTexture: truncated texture name fields");
			return false;
		}
		tex.name = String::utf8(raw_name);
		tex.mask_name = String::utf8(raw_mask);

		int32_t rw_format = 0;
		int32_t d3d_format = 0;
		uint16_t width = 0, height = 0;
		uint8_t depth = 0, num_levels = 0, type = 0, raster_flags = 0;
		if (!(c.read(rw_format) && c.read(d3d_format) && c.read(width) && c.read(height) &&
					c.read(depth) && c.read(num_levels) && c.read(type) && c.read(raster_flags))) {
			UtilityFunctions::push_error("GTATxdTexture: truncated raster header for '", tex.name, "'");
			return false;
		}
		tex.width = width;
		tex.height = height;
		tex.depth = depth;
		tex.num_levels = num_levels;
		tex.d3d_format = (uint32_t)d3d_format;
		tex.rw_format_flags = (uint32_t)rw_format;
		tex.raster_flags = raster_flags;

		if (rw_format & RASTER_PAL4) {
			tex.palette.resize(4 * 32);
			if (!c.read_raw(tex.palette.data(), tex.palette.size())) {
				UtilityFunctions::push_error("GTATxdTexture: truncated PAL4 palette for '", tex.name, "'");
				return false;
			}
		} else if (rw_format & RASTER_PAL8) {
			tex.palette.resize(4 * 256);
			if (!c.read_raw(tex.palette.data(), tex.palette.size())) {
				UtilityFunctions::push_error("GTATxdTexture: truncated PAL8 palette for '", tex.name, "'");
				return false;
			}
		}

		tex.mip_levels.reserve((size_t)num_levels);
		for (int lvl = 0; lvl < num_levels; ++lvl) {
			uint32_t lvl_size = 0;
			if (!c.read(lvl_size)) {
				UtilityFunctions::push_error("GTATxdTexture: truncated mip level size for '", tex.name, "'");
				return false;
			}
			std::vector<uint8_t> lvl_data((size_t)lvl_size);
			if (lvl_size > 0 && !c.read_raw(lvl_data.data(), lvl_size)) {
				UtilityFunctions::push_error("GTATxdTexture: truncated mip level data for '", tex.name, "'");
				return false;
			}
			tex.mip_levels.push_back(std::move(lvl_data));
		}

		textures.push_back(std::move(tex));

		// Skip anything else in this TEXTURENATIVE (e.g. its own trailing
		// EXTENSION) — same safety-net pattern used throughout.
		c.pos = tn_end;
	}

	return true;
}

int GTATxdTexture::get_texture_count() const {
	return (int)textures.size();
}

PackedStringArray GTATxdTexture::get_texture_names() const {
	PackedStringArray out;
	out.resize((int)textures.size());
	for (size_t i = 0; i < textures.size(); ++i) {
		out[(int)i] = textures[i].name;
	}
	return out;
}

const GTATxdTexture::TextureEntry *GTATxdTexture::_find(const String &name) const {
	for (const TextureEntry &t : textures) {
		if (t.name.nocasecmp_to(name) == 0) {
			return &t;
		}
	}
	return nullptr;
}

Dictionary GTATxdTexture::get_texture_info(const String &name) const {
	Dictionary out;
	const TextureEntry *t = _find(name);
	if (!t) {
		return out;
	}
	out["name"] = t->name;
	out["mask_name"] = t->mask_name;
	out["width"] = t->width;
	out["height"] = t->height;
	out["depth"] = t->depth;
	out["num_levels"] = t->num_levels;
	out["d3d_format"] = (int64_t)t->d3d_format;
	out["rw_format_flags"] = (int64_t)t->rw_format_flags;
	out["has_palette"] = (t->rw_format_flags & (RASTER_PAL4 | RASTER_PAL8)) != 0;
	out["is_compressed"] = (t->d3d_format == D3DFMT_DXT1 || t->d3d_format == D3DFMT_DXT3 || t->d3d_format == D3DFMT_DXT5);
	return out;
}

Ref<ImageTexture> GTATxdTexture::_build(const TextureEntry &tex) const {
	if (tex.mip_levels.empty() || tex.mip_levels[0].empty()) {
		UtilityFunctions::push_error("GTATxdTexture: '", tex.name, "' has no level-0 pixel data");
		return Ref<ImageTexture>();
	}
	const std::vector<uint8_t> &level0 = tex.mip_levels[0];

	Ref<Image> image;

	// [Root-cause fix] Whether a palette block is present in the file is
	// gated by rw_format_flags (PAL4/PAL8), NOT d3d_format -- a texture can
	// carry d3d_format=X8R8G8B8 as a legacy/nominal value while rw_format
	// still has PAL8 set, meaning the actual on-disk pixel data is 8-bit
	// palette indices (width*height bytes), not raw 32-bit RGBA
	// (width*height*4 bytes). Checking d3d_format alone (the previous
	// behavior) misread indexed data as 4x-too-large raw RGBA, producing
	// corrupted-looking textures -- confirmed by real GTA:SA data: every
	// "*8bit"-named texture here (head8bit, torso8bit, feet8bit) reports
	// d3d_format=22 (X8R8G8B8) yet visibly renders as garbage, while the
	// non-"8bit" ones (legs, torso) with the identical d3d_format render
	// correctly. Checking rw_format_flags first, independent of d3d_format,
	// is the correct general fix.
	const bool has_palette = (tex.rw_format_flags & (RASTER_PAL4 | RASTER_PAL8)) != 0;

	if (has_palette) {
		const size_t pixel_count = (size_t)tex.width * (size_t)tex.height;
		if (level0.size() < pixel_count || tex.palette.size() < 4) {
			UtilityFunctions::push_error("GTATxdTexture: '", tex.name, "' palette data or index data too small");
			return Ref<ImageTexture>();
		}
		const size_t palette_entries = tex.palette.size() / 4;
		PackedByteArray data;
		data.resize((int)(pixel_count * 4));
		uint8_t *dst = data.ptrw();
		for (size_t p = 0; p < pixel_count; ++p) {
			uint8_t idx = level0[p];
			if ((size_t)idx >= palette_entries) {
				idx = 0; // defensive: shouldn't happen, avoid an out-of-bounds read if it does
			}
			dst[p * 4 + 0] = tex.palette[(size_t)idx * 4 + 0];
			dst[p * 4 + 1] = tex.palette[(size_t)idx * 4 + 1];
			dst[p * 4 + 2] = tex.palette[(size_t)idx * 4 + 2];
			dst[p * 4 + 3] = tex.palette[(size_t)idx * 4 + 3];
		}
		image = Image::create_from_data(tex.width, tex.height, false, Image::FORMAT_RGBA8, data);
	} else if (tex.d3d_format == D3DFMT_DXT1 || tex.d3d_format == D3DFMT_DXT3 || tex.d3d_format == D3DFMT_DXT5) {
		Image::Format fmt = tex.d3d_format == D3DFMT_DXT1 ? Image::FORMAT_DXT1 : (tex.d3d_format == D3DFMT_DXT3 ? Image::FORMAT_DXT3 : Image::FORMAT_DXT5);
		PackedByteArray data;
		data.resize((int)level0.size());
		std::memcpy(data.ptrw(), level0.data(), level0.size());
		image = Image::create_from_data(tex.width, tex.height, false, fmt, data);
	} else if (tex.d3d_format == D3DFMT_A8R8G8B8 || tex.d3d_format == D3DFMT_X8R8G8B8) {
		const size_t pixel_count = (size_t)tex.width * (size_t)tex.height;
		if (level0.size() < pixel_count * 4) {
			UtilityFunctions::push_error("GTATxdTexture: '", tex.name, "' A8R8G8B8/X8R8G8B8 data shorter than width*height*4");
			return Ref<ImageTexture>();
		}
		const bool force_opaque = (tex.d3d_format == D3DFMT_X8R8G8B8); // X = unused byte, not real alpha
		PackedByteArray data;
		data.resize((int)(pixel_count * 4));
		uint8_t *dst = data.ptrw();
		for (size_t p = 0; p < pixel_count; ++p) {
			// D3D memory order B,G,R,(A|X) -> Godot expected R,G,B,A
			uint8_t b = level0[p * 4 + 0];
			uint8_t g = level0[p * 4 + 1];
			uint8_t r = level0[p * 4 + 2];
			uint8_t a = force_opaque ? 255 : level0[p * 4 + 3];
			dst[p * 4 + 0] = r;
			dst[p * 4 + 1] = g;
			dst[p * 4 + 2] = b;
			dst[p * 4 + 3] = a;
		}
		image = Image::create_from_data(tex.width, tex.height, false, Image::FORMAT_RGBA8, data);
	} else {
		UtilityFunctions::push_error("GTATxdTexture: '", tex.name, "' uses unsupported d3d_format ", (int64_t)tex.d3d_format, " and no palette flag — not DXT1/3/5, A8R8G8B8/X8R8G8B8, or a palette format");
		return Ref<ImageTexture>();
	}

	if (image.is_null() || image->is_empty()) {
		UtilityFunctions::push_error("GTATxdTexture: Image creation failed for '", tex.name, "'");
		return Ref<ImageTexture>();
	}

	Ref<ImageTexture> tex_out;
	tex_out.instantiate();
	tex_out->set_image(image);
	return tex_out;
}

Ref<ImageTexture> GTATxdTexture::build_texture(const String &name) const {
	const TextureEntry *t = _find(name);
	if (!t) {
		UtilityFunctions::push_error("GTATxdTexture: no texture named '", name, "'");
		return Ref<ImageTexture>();
	}
	return _build(*t);
}

Ref<ImageTexture> GTATxdTexture::build_texture_by_index(int index) const {
	if (index < 0 || (size_t)index >= textures.size()) {
		UtilityFunctions::push_error("GTATxdTexture: index ", index, " out of range (", (int)textures.size(), " textures)");
		return Ref<ImageTexture>();
	}
	return _build(textures[(size_t)index]);
}

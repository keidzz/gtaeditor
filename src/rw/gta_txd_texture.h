/*
    GTATxdTexture — parses a .txd texture dictionary: TEXDICTIONARY ->
    TEXTURENATIVE entries (the D3D9-native raster format GTA:SA PC uses),
    and builds Godot ImageTexture resources from them.

    Same situation as the DFF parsers: NOT ported from gta-reversed (its RW
    stream reading thunks into the exe), written against librw
    (github.com/aap/librw) as the public-format reference. Provenance:
      - Chunk type IDs (ID_TEXDICTIONARY=0x16, ID_TEXTURENATIVE=0x15):
        src/rwbase.h
      - TEXDICTIONARY structure (STRUCT{int16 numTextures; int16 deviceId},
        then numTextures * TEXTURENATIVE chunks): src/texture.cpp,
        TexDictionary::streamRead
      - TEXTURENATIVE (D3D9 native raster) byte layout: src/d3d/d3d9.cpp,
        readNativeTexture. Confirms GTA:SA PC textures are platform
        PLATFORM_D3D9(=9) — anything else is rejected rather than guessed at.
      - D3DFORMAT values (D3DFMT_DXT1/3/5 as literal 'DXT1'/'DXT3'/'DXT5'
        FourCCs, D3DFMT_A8R8G8B8=21, D3DFMT_X8R8G8B8=22, D3DFMT_P8=41):
        src/d3d/rwd3d.h
      - Raster::Format flag bits (PAL4=0x4000, PAL8=0x2000): src/rwobjects.h

    TEXTURENATIVE's STRUCT layout, after the platform check:
        uint32 filterAddressing; char name[32]; char mask[32];
        int32 rwFormat; int32 d3dFormat; uint16 width; uint16 height;
        uint8 depth; uint8 numLevels; uint8 type; uint8 flags;
        if rwFormat has PAL4: 128 bytes palette (32 * RGBA8)
        if rwFormat has PAL8: 1024 bytes palette (256 * RGBA8)
        per mip level (numLevels, level 0 = largest):
            uint32 size; then `size` bytes of raw pixel/compressed data

    Format support in build_texture():
      - DXT1/DXT3/DXT5: passed straight through to Godot's Image with the
        matching FORMAT_DXT1/3/5 — these are the same standard S3TC/BC1-3
        block formats on both sides, no conversion needed. HIGH confidence.
      - D3DFMT_A8R8G8B8 / D3DFMT_X8R8G8B8: byte-swapped from D3D's B,G,R,A
        (or B,G,R,X) memory order to Godot's expected R,G,B,A. X8R8G8B8's
        4th byte is unused/undefined, not real alpha, so it's forced to 255
        (fully opaque) rather than passed through. Standard/well-known
        conversion, but UNVERIFIED against real output — if colors look
        channel-swapped (e.g. red and blue reversed), that's the first thing
        to check.
      - D3DFMT_P8 (8-bit palette): each pixel byte indexes into the palette
        to produce RGBA8. [FIXED] Palette entries use the same D3D B,G,R,A
        memory order as the direct RGB formats — the originally-assumed
        Win32 PALETTEENTRY R,G,B,A order was wrong, confirmed by real
        texture corruption (head8bit/torso8bit/feet8bit specifically showing
        artifacts while non-palette textures rendered clean).
      - Anything else: rejected with a clear error naming the unsupported
        d3d_format value, rather than guessing.

    NOT done: hooking textures up to GTADffGeometry's materials (that needs
    MaterialList parsing in the geometry side too, which was deliberately
    skipped there). For now, match a body part's texture(s) by name/index
    manually — most single body-part TXDs only have one or two textures.
*/
#ifndef GTA_TXD_TEXTURE_H
#define GTA_TXD_TEXTURE_H

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class GTATxdTexture : public RefCounted {
	GDCLASS(GTATxdTexture, RefCounted)

public:
	struct TextureEntry {
		String name;
		String mask_name;
		int32_t width = 0;
		int32_t height = 0;
		int32_t depth = 0;
		int32_t num_levels = 0;
		uint32_t d3d_format = 0;
		uint32_t rw_format_flags = 0;
		uint8_t raster_flags = 0;
		std::vector<uint8_t> palette;
		std::vector<std::vector<uint8_t>> mip_levels; // [0] = level 0 (largest)
	};

private:
	std::vector<TextureEntry> textures;

	const TextureEntry *_find(const String &name) const;
	Ref<ImageTexture> _build(const TextureEntry &tex) const;

protected:
	static void _bind_methods();

public:
	GTATxdTexture();
	~GTATxdTexture() override;

	bool parse(const PackedByteArray &bytes);

	int get_texture_count() const;
	PackedStringArray get_texture_names() const;

	// {"name","mask_name","width","height","depth","num_levels","d3d_format","is_compressed"}
	Dictionary get_texture_info(const String &name) const;

	Ref<ImageTexture> build_texture(const String &name) const;
	Ref<ImageTexture> build_texture_by_index(int index) const;
};

} // namespace godot

#endif // GTA_TXD_TEXTURE_H

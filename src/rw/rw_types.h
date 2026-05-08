#ifndef RW_TYPES_H
#define RW_TYPES_H

#include <cstdint>

// =============================================================================
// RenderWare Binary Stream Section Types
// Reference: http://www.gtamodding.com/wiki/List_of_RW_section_IDs
// =============================================================================

enum class RWSectionType : uint32_t {
	Undefined = 0x0000,
	Struct = 0x0001,
	String = 0x0002,
	Extension = 0x0003,
	Texture = 0x0006,
	Material = 0x0007,
	MaterialList = 0x0008,
	FrameList = 0x000E,
	Geometry = 0x000F,
	Clump = 0x0010,
	Atomic = 0x0014,
	TextureNative = 0x0015,
	TextureDictionary = 0x0016,
	GeometryList = 0x001A,
	BinMesh = 0x050E,
	MaterialEffects = 0x0120,
	// Rockstar custom sections
	MaterialSpecular = 0x0253F2F6,
	MaterialReflection = 0x0253F2FC,
	Effect2D = 0x0253F2F8,
	Frame = 0x0253F2FE,
};

// =============================================================================
// RenderWare Version identifiers (from the section header's upper 16 bits)
// =============================================================================

enum class RWVersion : uint16_t {
	III_1 = 0,
	III_2 = 2048,
	III_3 = 65400,
	ViceCity_1 = 3074,
	ViceCity_2 = 4099,
	SanAndreas = 6147,
};

// =============================================================================
// RenderWare Section Header — 12 bytes in the binary stream
// Layout: [type:4][size:4][version_lo:2][version_hi:2]
// =============================================================================

struct RWSectionHeader {
	RWSectionType type = RWSectionType::Undefined;
	int32_t size = 0;
	RWVersion version = RWVersion::III_1;
};

// =============================================================================
// Texture Raster Format flags
// Reference: http://www.gtamodding.com/wiki/Texture_Native_Struct
// =============================================================================

enum RasterFormat : uint32_t {
	RASTER_1555 = 0x0100,    // 5-bit RGB, 1-bit alpha
	RASTER_565 = 0x0200,     // 5-bit R, 6-bit G, 5-bit B
	RASTER_4444 = 0x0300,    // 4-bit RGBA
	RASTER_LUM8 = 0x0400,    // 8-bit luminance
	RASTER_8888 = 0x0500,    // 8-bit BGRA
	RASTER_888 = 0x0600,     // 8-bit BGR, no alpha
	RASTER_555 = 0x0A00,     // 5-bit RGB

	// Extension flags (combined with format via OR)
	RASTER_EXT_AUTO_MIPMAP = 0x1000,
	RASTER_EXT_PALETTE8 = 0x2000,
	RASTER_EXT_PALETTE4 = 0x4000,
	RASTER_EXT_MIPMAP = 0x8000,
};

// =============================================================================
// Geometry section flags
// =============================================================================

enum GeometryFlags : uint16_t {
	GEO_TRIANGLE_STRIP = 0x0001,
	GEO_HAS_POSITIONS = 0x0002,
	GEO_HAS_UV = 0x0004,
	GEO_HAS_COLORS = 0x0008,
	GEO_HAS_NORMALS = 0x0010,
	GEO_LIGHT = 0x0020,
	GEO_MODULATE_COLOR = 0x0040,
	GEO_HAS_UV2 = 0x0080,
};

#endif // RW_TYPES_H

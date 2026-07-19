#ifndef BINARY_READER_H
#define BINARY_READER_H

#include <godot_cpp/variant/packed_byte_array.hpp>

#include "rw_types.h"

using namespace godot;

// =============================================================================
// BinaryReader — Lightweight cursor-based reader over a PackedByteArray.
// All reads are little-endian (native GTA format on x86/x64).
// =============================================================================

class BinaryReader {
public:
	BinaryReader();
	explicit BinaryReader(const PackedByteArray &p_data);

	// -- Position control --
	int64_t get_position() const;
	void set_position(int64_t p_pos);
	int64_t get_length() const;
	void skip(int32_t p_bytes);
	bool can_read(int32_t p_bytes) const;

	// -- Primitive reads --
	uint8_t read_uint8();
	int16_t read_int16();
	uint16_t read_uint16();
	int32_t read_int32();
	uint32_t read_uint32();
	uint64_t read_uint64();
	float read_float();

	// -- Bulk reads --
	PackedByteArray read_bytes(int32_t p_count);
	String read_string(int32_t p_length); // Null-terminated, padded to p_length

	// -- RenderWare helpers --
	RWSectionHeader read_section_header();

private:
	PackedByteArray data;
	const uint8_t *ptr = nullptr;
	int64_t pos = 0;
	int64_t length = 0;
};

#endif // BINARY_READER_H

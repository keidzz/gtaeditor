#include "texture_collection.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// TextureCollection implementation
// =============================================================================

void TextureCollection::register_txd(const String &p_txd_name, const ImgArchive *p_archive) {
	String key = p_txd_name.to_lower();
	// Strip .txd extension if present.
	if (key.ends_with(".txd")) {
		key = key.substr(0, key.length() - 4);
	}

	TxdEntry entry;
	entry.txd_name = key;
	entry.archive = p_archive;
	entry.loaded = false;
	txd_entries[key] = entry;
}

void TextureCollection::register_txd_file(const String &p_txd_name, const String &p_file_path) {
	String key = p_txd_name.to_lower();
	if (key.ends_with(".txd")) {
		key = key.substr(0, key.length() - 4);
	}

	Ref<FileAccess> file = FileAccess::open(p_file_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[TextureCollection] Could not open TXD file '", p_file_path, "'.");
		return;
	}
	PackedByteArray data = file->get_buffer(file->get_length());
	if (data.is_empty()) {
		return;
	}

	TxdEntry entry;
	entry.txd_name = key;
	entry.archive = nullptr;
	entry.loaded = true;
	entry.textures = TxdParser::parse(data);
	if (!entry.textures.is_empty()) {
		txd_entries[key] = entry;
		UtilityFunctions::print("[TextureCollection] Loaded generic vehicle TXD '", key,
				"' with ", entry.textures.size(), " textures.");
	}
}

void TextureCollection::add_parent(const String &p_child, const String &p_parent) {
	parent_links[p_child.to_lower()] = p_parent.to_lower();
}

bool TextureCollection::get_texture(const String &p_txd_name, const String &p_texture_name, Ref<ImageTexture> &r_tex, bool &r_has_alpha_content) {
	String txd_key = p_txd_name.to_lower();
	String tex_key = p_texture_name.to_lower();

	// Try the primary TXD.
	if (txd_entries.has(txd_key)) {
		TxdEntry &entry = txd_entries[txd_key];
		ensure_loaded(entry);

		if (entry.textures.has(tex_key)) {
			r_tex = entry.textures[tex_key].texture;
			r_has_alpha_content = entry.textures[tex_key].has_alpha_content;
			return true;
		}
	}

	// Try the parent TXD (TXDP fallback).
	if (parent_links.has(txd_key)) {
		String parent_key = parent_links[txd_key];
		if (txd_entries.has(parent_key)) {
			TxdEntry &parent_entry = txd_entries[parent_key];
			ensure_loaded(parent_entry);

			if (parent_entry.textures.has(tex_key)) {
				r_tex = parent_entry.textures[tex_key].texture;
				r_has_alpha_content = parent_entry.textures[tex_key].has_alpha_content;
				return true;
			}
		}
	}

	// Generic vehicle texture dictionary (models/generic/vehicle.txd):
	// GTA vehicles share tyres, glass, light lenses, scratches, plates and
	// interior trim textures from it (vehicletyres128, vehiclegeneric256,
	// vehiclelights128, ...) instead of each model's own TXD. Without this
	// fallback those parts render with no texture (flat white).
	const String kGenericVehicleTxd = "vehicle";
	if (txd_key != kGenericVehicleTxd && txd_entries.has(kGenericVehicleTxd)) {
		TxdEntry &generic_entry = txd_entries[kGenericVehicleTxd];
		ensure_loaded(generic_entry);

		if (generic_entry.textures.has(tex_key)) {
			r_tex = generic_entry.textures[tex_key].texture;
			r_has_alpha_content = generic_entry.textures[tex_key].has_alpha_content;
			return true;
		}
	}

	return false;
}

bool TextureCollection::get_texture_image(const String &p_txd_name, const String &p_texture_name, Ref<Image> &r_image, bool &r_has_alpha_content) {
	Ref<ImageTexture> texture;
	if (!get_texture(p_txd_name, p_texture_name, texture, r_has_alpha_content) || texture.is_null()) {
		return false;
	}

	r_image = texture->get_image();
	return r_image.is_valid() && !r_image->is_empty();
}

int TextureCollection::get_txd_count() const {
	return txd_entries.size();
}

void TextureCollection::clear() {
	txd_entries.clear();
	parent_links.clear();
}

void TextureCollection::ensure_loaded(TxdEntry &entry) {
	if (entry.loaded) {
		return;
	}
	entry.loaded = true;

	if (entry.archive == nullptr) {
		return;
	}

	// Read TXD data from IMG archive and parse it.
	String txd_filename = entry.txd_name + ".txd";
	if (!entry.archive->has_entry(txd_filename)) {
		return;
	}

	PackedByteArray data = entry.archive->read_entry(txd_filename);
	if (data.is_empty()) {
		return;
	}

	entry.textures = TxdParser::parse(data);
}

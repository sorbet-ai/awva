// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// The engine package (.awvapkg, awva-package/2: docs/packages.md): the one file an awva engine
// ships in, for every host. The format, its checks and the relocator of an embedded host's module
// image; embedded hosts, virtual devices, apps and plugins read packages with this file, so all
// refuse the same things with the same words. Freestanding: no allocation, no I/O, no exceptions;
// every function works on bytes (an embedded host's loader includes it as it is).
//
// A package, little-endian, every offset from the start of the file:
//   package header (64 B)  'NSCP', format, the ABI, the schema hash, the section table's place
//   section table          n x 48 B: kind, tag, offset, size, crc32; offsets ascending, no overlap
//   sections               kind 1 the manifest (JSON, the first), kind 2 an image (tag: its target),
//                          kind 3 a preset file (tag: its name; awva-preset/1)
// An image section is a module header (256 B), then the relocation list (reloc_count uint32
// offsets of the words that hold an address, ascending), then the image. The relocations come
// first so an embedded host can stream the image into flash through the relocator 32 bytes at a time
// (a programmed flash word can't be patched later). On a PC the image is the DLL / dylib / so and
// there are no relocations (the OS loader relocates). Written by tools: component/tools/nsc_pack.py.
#ifndef NSC_PACKAGE_H_
#define NSC_PACKAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nsc/nsc.h"

namespace nscpkg {

constexpr uint32_t PACKAGE_MAGIC = 0x5043534eu; // "NSCP"
constexpr uint32_t MODULE_MAGIC = 0x4d43534eu;  // "NSCM"
constexpr uint16_t FORMAT = 1;
constexpr uint32_t MAX_SECTIONS = 32;
constexpr uint32_t MAX_RELOCS = 4096; // an embedded host buffers the list before the image
enum Kind : uint32_t { KIND_MANIFEST = 1,
	KIND_IMAGE = 2,
	KIND_PRESET = 3 };

struct PackageHeader {
	uint32_t magic;
	uint16_t format, header_size;
	uint32_t abi, schema_hash;
	uint32_t n_sections, table_offset, file_size, table_crc;
	uint8_t reserved[28];
	uint32_t header_crc; // crc32 of bytes 0..59
};
static_assert(sizeof(PackageHeader) == 64 && offsetof(PackageHeader, header_crc) == 60, "the package header is 64 bytes");

struct Section {
	uint32_t kind;
	char tag[32]; // NUL-padded, the last byte NUL
	uint32_t offset, size, crc;
};
static_assert(sizeof(Section) == 48 && offsetof(Section, offset) == 36 && offsetof(Section, crc) == 44, "a section entry is 48 bytes");

struct ModuleHeader {
	uint32_t magic;
	uint16_t format, header_size;
	uint32_t abi;
	char arch[16];      // "armv7em", "x86_64-windows", "arm64-macos", "x86_64-macos", "x86_64-linux"
	char fpu[12];       // "fpv5-d16"; "" on a PC
	char float_abi[8];  // "softfp"; "" on a PC
	char toolchain[24]; // "gcc 14.2.1"
	uint32_t image_size, reloc_count;
	uint32_t entry_offset; // nsc_entry from the image's start (Thumb bit set on Arm; 0 on a PC)
	uint32_t stack_bytes;  // deepest stack of the component's functions (0: not measured)
	uint32_t state_bytes, fast_bytes, large_bytes;
	int32_t max_voices, channels;
	uint32_t led, schema_hash;
	char id[32], uid[64], version[32];
	uint32_t payload_crc; // crc32 of everything after the header: the relocations, then the image
	uint32_t reserved;
	uint32_t header_crc; // crc32 of bytes 0..251
};
static_assert(sizeof(ModuleHeader) == 256, "the module header is 256 bytes");
static_assert(offsetof(ModuleHeader, image_size) == 72 && offsetof(ModuleHeader, max_voices) == 100 &&
        offsetof(ModuleHeader, id) == 116 && offsetof(ModuleHeader, version) == 212 && offsetof(ModuleHeader, header_crc) == 252,
    "the module header's layout is frozen");

// crc32 (zlib's: reflected 0xEDB88320, ~0 in and out), resumable: crc32(crc32(0, a), b) = crc32(0, a b)
inline uint32_t crc32(uint32_t crc, const void* data, size_t n) {
	const uint8_t* p = static_cast<const uint8_t*>(data);
	crc = ~crc;
	for (size_t i = 0; i < n; i++) {
		crc ^= p[i];
		for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// a short reason, built without printf (an embedded loader has none at hand)
struct Reason {
	char text[80];
	size_t n = 0;
	Reason() { text[0] = 0; }
	Reason& s(const char* t) {
		while (t && *t && n + 1 < sizeof text) text[n++] = *t++;
		text[n] = 0;
		return *this;
	}
	Reason& u(uint32_t v) {
		char b[11];
		int k = 0;
		do b[k++] = static_cast<char>('0' + v % 10);
		while ((v /= 10) && k < 10);
		while (k && n + 1 < sizeof text) text[n++] = b[--k];
		text[n] = 0;
		return *this;
	}
	Reason& kb(uint32_t bytes) { return u((bytes + 1023) / 1024).s(" KB"); }
	bool empty() const { return n == 0; }
};

// a fixed string field as a C string (its last byte is checked to be NUL)
inline bool terminated(const char* f, size_t n) { return memchr(f, 0, n) != nullptr; }
template <size_t N> inline bool field_is(const char (&f)[N], const char* s) { return strncmp(f, s, N) == 0; }

inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

// the package header and its section table: the first 64 + n * 48 bytes of a package. 0 ok, else
// the reason. total: the package's size when known (0: streaming, the size is checked later).
inline bool check_package(const uint8_t* p, size_t have, uint32_t total, Reason& why) {
	if (have < sizeof(PackageHeader)) return why.s("not a package (short)"), false;
	PackageHeader h;
	memcpy(&h, p, sizeof h);
	if (h.magic != PACKAGE_MAGIC) return why.s("not a package"), false;
	if (crc32(0, p, 60) != h.header_crc) return why.s("package header crc"), false;
	if (h.format != FORMAT || h.header_size != sizeof(PackageHeader)) return why.s("package format ").u(h.format).s(", firmware reads ").u(FORMAT), false;
	if (h.n_sections == 0 || h.n_sections > MAX_SECTIONS || h.table_offset != sizeof(PackageHeader))
		return why.s("bad section table"), false;
	if (total && h.file_size != total) return why.s("package size ").u(total).s(", header says ").u(h.file_size), false;
	const size_t table_end = sizeof(PackageHeader) + h.n_sections * sizeof(Section);
	if (have < table_end) return why.s("not a package (short)"), false;
	if (crc32(0, p + sizeof(PackageHeader), h.n_sections * sizeof(Section)) != h.table_crc) return why.s("section table crc"), false;
	uint32_t at = static_cast<uint32_t>(table_end);
	for (uint32_t i = 0; i < h.n_sections; i++) {
		Section s;
		memcpy(&s, p + sizeof(PackageHeader) + i * sizeof(Section), sizeof s);
		if (!terminated(s.tag, sizeof s.tag) || s.offset < at || s.offset % 4 || s.size > h.file_size || s.offset > h.file_size - s.size)
			return why.s("bad section ").u(i), false;
		at = s.offset + s.size;
	}
	return true;
}

// the section of kind with tag (nullptr: any tag), after check_package; -1 none
inline int find_section(const uint8_t* p, uint32_t kind, const char* tag, Section* out) {
	const uint32_t n = rd32(p + offsetof(PackageHeader, n_sections));
	for (uint32_t i = 0; i < n; i++) {
		Section s;
		memcpy(&s, p + sizeof(PackageHeader) + i * sizeof(Section), sizeof s);
		if (s.kind == kind && (!tag || strncmp(s.tag, tag, sizeof s.tag) == 0)) {
			if (out) *out = s;
			return static_cast<int>(i);
		}
	}
	return -1;
}

// what a target is: the image it takes and what it has room for
struct Target {
	const char* tag;       // the image section it takes, "armv7em-fpv5-d16-softfp" / "x86_64-windows" ...
	const char* arch;      // the module header's arch
	const char* fpu;       // "" on a PC
	const char* float_abi; // "" on a PC
	uint32_t state_max, fast_max, large_max;
	uint32_t stack_max; // 0: not checked (a PC's images aren't measured)
	int32_t voices;     // the host's voices: a module must take at least this many
};

// a module header against the target: its own integrity, then ABI, architecture, float ABI,
// regions, stack and voices. size: the image section's size (0: not known yet).
inline bool check_module(const ModuleHeader& m, uint32_t size, const Target& t, Reason& why) {
	if (m.magic != MODULE_MAGIC) return why.s("not a module"), false;
	if (crc32(0, &m, 252) != m.header_crc) return why.s("module header crc"), false;
	if (m.format != FORMAT || m.header_size != sizeof(ModuleHeader)) return why.s("module format ").u(m.format).s(", firmware reads ").u(FORMAT), false;
	if (!terminated(m.arch, sizeof m.arch) || !terminated(m.fpu, sizeof m.fpu) || !terminated(m.float_abi, sizeof m.float_abi) ||
	    !terminated(m.toolchain, sizeof m.toolchain) || !terminated(m.id, sizeof m.id) || !terminated(m.uid, sizeof m.uid) ||
	    !terminated(m.version, sizeof m.version) || m.id[0] == 0)
		return why.s("bad module header"), false;
	if (m.abi != NSC_ABI) return why.s("abi ").u(m.abi).s(", firmware has ").u(NSC_ABI), false;
	if (!field_is(m.arch, t.arch)) return why.s("arch ").s(m.arch).s(", this is ").s(t.arch), false;
	if (!field_is(m.fpu, t.fpu)) return why.s("fpu ").s(m.fpu[0] ? m.fpu : "none").s(", this has ").s(t.fpu[0] ? t.fpu : "none"), false;
	if (!field_is(m.float_abi, t.float_abi))
		return why.s("float abi ").s(m.float_abi[0] ? m.float_abi : "none").s(", this is ").s(t.float_abi[0] ? t.float_abi : "none"), false;
	if (m.reloc_count > MAX_RELOCS) return why.s("relocations ").u(m.reloc_count).s(" > ").u(MAX_RELOCS), false;
	if (size && size != sizeof(ModuleHeader) + 4ull * m.reloc_count + m.image_size) return why.s("image size"), false;
	if (m.image_size == 0 || (m.entry_offset & ~1u) >= m.image_size) return why.s("entry out of range"), false;
	if (m.state_bytes > t.state_max) return why.s("state ").kb(m.state_bytes).s(" > ").kb(t.state_max), false;
	if (m.fast_bytes > t.fast_max) return why.s("stream ").kb(m.fast_bytes).s(" > ").kb(t.fast_max), false;
	if (m.large_bytes > t.large_max) return why.s("bulk ").kb(m.large_bytes).s(" > ").kb(t.large_max), false;
	if (t.stack_max && m.stack_bytes == 0) return why.s("stack not measured"), false;
	if (t.stack_max && m.stack_bytes > t.stack_max) return why.s("stack ").u(m.stack_bytes).s(" > ").u(t.stack_max), false;
	if (m.max_voices < t.voices) return why.s("voices ").u(static_cast<uint32_t>(m.max_voices)).s(" < ").u(static_cast<uint32_t>(t.voices)), false;
	if (m.channels != 1 && m.channels != 2) return why.s("channels ").u(static_cast<uint32_t>(m.channels)), false;
	return true;
}

// the relocation list: ascending, word-aligned, every word inside the image
inline bool check_relocs(const uint8_t* relocs, uint32_t count, uint32_t image_size, Reason& why) {
	uint32_t last = 0;
	for (uint32_t i = 0; i < count; i++) {
		const uint32_t o = rd32(relocs + 4 * i);
		if (o % 4 || o + 4 > image_size || (i && o <= last)) return why.s("relocation offset out of range"), false;
		last = o;
	}
	return true;
}

// The relocator, streaming: the image's bytes [at, at + n) copied to dst with base added to every
// relocated word among them. Chunks start on a word boundary (a word is never split); next is the
// first relocation not yet passed (0 at the start).
inline void relocate(uint8_t* dst, const uint8_t* src, uint32_t at, uint32_t n, const uint8_t* relocs, uint32_t count, uint32_t& next, uint32_t base) {
	memcpy(dst, src, n);
	for (; next < count; next++) {
		const uint32_t o = rd32(relocs + 4 * next);
		if (o >= at + n) break;
		uint32_t w;
		memcpy(&w, dst + (o - at), 4);
		w += base;
		memcpy(dst + (o - at), &w, 4);
	}
}

} // namespace nscpkg

#endif // NSC_PACKAGE_H_

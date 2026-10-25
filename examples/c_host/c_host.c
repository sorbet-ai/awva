/* SPDX-FileCopyrightText: 2026 Sorbet AI
 * SPDX-License-Identifier: Apache-2.0
 *
 * c_host: an awva engine from plain C, through its component library (component/include/nsc/nsc.h).
 *
 *   c_host COMPONENT_DIR [ENGINE [OUT.wav]]      e.g.  c_host build/sdk/components va va.wav
 *
 * It loads the engine's library at run time, checks the ABI, lists the presets and parameters,
 * plays a short MIDI 2.0 phrase it builds itself (three notes, each with its own pitch glide and
 * pressure swell) and writes a 32-bit float WAV. It prints "hash <FNV-1a 64 of the samples>" and
 * fails if the output is silent or not finite.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nsc/nsc.h"

#if defined(_WIN32)
#include <windows.h>
#define LIB_EXT ".dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
#define LIB_EXT ".dylib"
#else
#include <dlfcn.h>
#define LIB_EXT ".so"
#endif

#define SR 48000
#define VOICES 10
#define SECONDS 4

/* ---- 1. loading the library: one exported function, nsc_entry() --------------------------- */

static nsc_entry_fn load_entry(const char* path) {
#if defined(_WIN32)
	HMODULE lib = LoadLibraryA(path);
	return lib ? (nsc_entry_fn)(void*)GetProcAddress(lib, "nsc_entry") : NULL;
#else
	void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	return lib ? (nsc_entry_fn)dlsym(lib, "nsc_entry") : NULL;
#endif
}

/* The host owns the engine's memory: three regions, zero-filled and 64-byte aligned. */
static void* region(uint32_t bytes, void** raw) {
	*raw = calloc(1, (size_t)bytes + 64);
	return *raw ? (void*)(((uintptr_t)*raw + 63) & ~(uintptr_t)63) : NULL;
}

/* ---- 2. MIDI 2.0 messages (Universal MIDI Packets, message type 4: two words) -------------- */

/* word 0: type 4, group 0, status, channel 0, then the note and one more byte */
static uint32_t w0(uint32_t status, uint32_t note, uint32_t byte4) { return 0x40000000u | status << 20 | note << 8 | byte4; }

static nsc_event note_on(uint32_t frame, int note, double velocity) {
	nsc_event e = {frame, {w0(0x9, (uint32_t)note, 0), (uint32_t)(velocity * 65535.0) << 16, 0, 0}};
	return e;
}
static nsc_event note_off(uint32_t frame, int note) {
	nsc_event e = {frame, {w0(0x8, (uint32_t)note, 0), 0x4000u << 16, 0, 0}};
	return e;
}
/* Poly Pressure: the note's own pressure, 32 bits (full scale = a full press, 1.0) */
static nsc_event pressure(uint32_t frame, int note, double p) {
	nsc_event e = {frame, {w0(0xa, (uint32_t)note, 0), (uint32_t)(p * 4294967295.0), 0, 0}};
	return e;
}
/* Registered Per-Note Controller 3, Pitch 7.25: the note's own absolute pitch in semitones */
static nsc_event pitch(uint32_t frame, int note, double semitones) {
	nsc_event e = {frame, {w0(0x0, (uint32_t)note, 3), (uint32_t)(semitones * 33554432.0), 0, 0}};
	return e;
}

/* The phrase: what happens in block b. Three notes start 0.6 s apart (C4, E4, G4, all on one
 * channel: MIDI 2.0 controls each note by its own number); each swells its pressure up and down
 * and glides its pitch up a semitone and back; all are released at 3 s. */
static int phrase(int b, nsc_event* ev) {
	const int keys[3] = {60, 64, 67};
	const double t = (double)b * NSC_BLOCK / SR;
	int n = 0;
	for (int k = 0; k < 3; k++) {
		const double on = 0.6 * k, u = t - on;
		if (u < 0.0 || t > 3.2) continue;
		if (b == (int)(on * SR / NSC_BLOCK)) ev[n++] = note_on(0, keys[k], 0.6);
		if (t < 3.0) {
			const double swell = sin(3.14159265358979 * fmin(u / 1.5, 1.0)); /* 0 -> 1 -> 0 over 1.5 s */
			ev[n++] = pressure(0, keys[k], 0.25 + 0.75 * swell * swell);
			ev[n++] = pitch(0, keys[k], keys[k] + 0.5 - 0.5 * cos(2.0 * 3.14159265358979 * u / 2.0));
		} else if (b == (int)(3.0 * SR / NSC_BLOCK)) {
			ev[n++] = note_off(0, keys[k]);
		}
	}
	return n;
}

/* ---- 3. the output ---------------------------------------------------------------------------- */

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static int write_wav(const char* path, const float* x, uint32_t frames) {
	FILE* f = fopen(path, "wb");
	if (!f) return 0;
	fwrite("RIFF", 1, 4, f);
	put32(f, 36 + frames * 4);
	fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16);
	put16(f, 3); /* IEEE float */
	put16(f, 1); /* mono */
	put32(f, SR);
	put32(f, SR * 4);
	put16(f, 4);
	put16(f, 32);
	fwrite("data", 1, 4, f);
	put32(f, frames * 4);
	fwrite(x, 4, frames, f);
	fclose(f);
	return 1;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: c_host COMPONENT_DIR [ENGINE [OUT.wav]]\n");
		return 2;
	}
	const char* engine = argc > 2 ? argv[2] : "va";
	const char* wav = argc > 3 ? argv[3] : "c_host.wav";
	char path[1024];
	snprintf(path, sizeof path, "%s/%s%s", argv[1], engine, LIB_EXT);

	/* load it and check what it is */
	const nsc_entry_fn entry = load_entry(path);
	const nsc_component* c = entry ? entry() : NULL;
	if (!c || c->desc->magic != NSC_MAGIC || c->desc->abi != NSC_ABI || c->desc->channels < 1 || c->desc->channels > 2) {
		fprintf(stderr, "%s: no component of ABI %d\n", path, NSC_ABI);
		return 2;
	}
	const nsc_desc* d = c->desc;
	printf("%s (%s): %s\n", d->name, d->uid, d->summary);
	for (int i = 0; i < d->num_presets; i++) printf("  preset %d: %s\n", i, d->presets[i].name);
	for (int i = 0; i < d->num_params; i++)
		printf("  param %08x %-12s %-10s %g .. %g\n", (unsigned)d->params[i].id, d->params[i].name, d->params[i].group, d->params[i].min, d->params[i].max);

	/* an instance: the memory it asks for, then init() */
	void *raw_state, *raw_fast, *raw_large;
	void* state = region(d->state_bytes, &raw_state);
	void* fast = region(d->fast_bytes, &raw_fast);
	void* large = region(d->large_bytes, &raw_large);
	if (!state || !fast || !large) return 2;
	c->init(state, fast, large, SR, VOICES);

	/* play: one process() per 32-frame block, with that block's events */
	const uint32_t blocks = SECONDS * SR / NSC_BLOCK, frames = blocks * NSC_BLOCK;
	float* out = (float*)calloc(frames * (size_t)d->channels, sizeof(float));
	float* chan[2];
	nsc_event ev[16];
	for (uint32_t b = 0; b < blocks; b++) {
		for (int ch = 0; ch < d->channels; ch++) chan[ch] = out + (size_t)ch * frames + (size_t)b * NSC_BLOCK;
		c->process(state, ev, phrase((int)b, ev), chan);
	}

	/* check it, hash it (FNV-1a 64 over the float bits), write it (the first channel) */
	uint64_t h = 1469598103934665603ull;
	double energy = 0.0;
	int finite = 1;
	for (uint32_t i = 0; i < frames; i++) {
		uint32_t u;
		memcpy(&u, &out[i], 4);
		for (int k = 0; k < 4; k++) h = (h ^ ((u >> (8 * k)) & 0xffu)) * 1099511628211ull;
		if (!isfinite(out[i])) finite = 0;
		else energy += (double)out[i] * out[i];
	}
	if (!write_wav(wav, out, frames)) return 2;
	printf("wrote %s: %u frames, rms %.4f\nhash %016llx\n", wav, (unsigned)frames, sqrt(energy / frames), (unsigned long long)h);
	free(out);
	free(raw_state);
	free(raw_fast);
	free(raw_large);
	return finite && energy > 1e-6 ? 0 : 1; /* fail if silent or broken */
}

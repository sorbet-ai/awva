/* SPDX-FileCopyrightText: 2026 Sorbet AI
 * SPDX-License-Identifier: Apache-2.0
 *
 * The engine component: one sound engine as a C ABI, for a plugin, the virtual board or any host
 * that loads it from a DLL / dylib (and later the board, from a module). UMP in, audio out,
 * parameters by stable id and name, factory presets, and what the host needs to know first:
 * the heroes, the budget, the memory and the stack.
 *
 * One exported symbol, nsc_entry(), gives the component: a descriptor (static, readable before
 * any instance exists) and its functions. The host owns the memory: it allocates the three regions
 * the descriptor asks for (state, fast, large; zero-filled, 64-byte aligned) and passes them to
 * init(). No call allocates, blocks or throws; nothing is global (two instances are independent).
 *
 * Time: process() renders one block of NSC_BLOCK frames. Its events are UMP packets with their
 * frame within the block; they are taken in 2 ms steps, as the board takes MIDI in: before each
 * block, every step due by its start runs with the events at or before its time (a plugin's
 * events land within one step, 2 ms, of their frame, the same rule as the SDK's renderer).
 *
 * Input: MIDI 2.0 channel voice messages (UMP type 4) in their standard meaning (pressure full
 * scale = the Striso's full press, 16-bit velocities, Pitch 7.25, bipolar controllers centred), and
 * MIDI 1.0 channel voice messages in UMP (type 2, including MPE); the other types are ignored.
 * Inside, the component runs the one decoder (awva/ump.h), the voice allocator and, in
 * NSC_MODE_LEGATO, mono legato.
 *
 * process_voices() is the other way in: the host allocates the voices itself and gives each its
 * controls (the board's synth_control does so today), and the component only renders.
 *
 * Map to CLAP: desc->id is the plugin id's last part, name / summary / version its descriptor;
 * process() is clap_plugin.process with its note and MIDI 2.0 events (CLAP_EVENT_MIDI2) given per
 * 32-frame block; params by id are clap_plugin_params (id = nsc_param.id, value in min .. max,
 * NSC_PARAM_STEPS / NSC_PARAM_TOGGLE = CLAP_PARAM_IS_STEPPED); presets are a preset-discovery
 * provider's factory presets (by index here); active_voices() is the voice-info extension's count;
 * tail_seconds the tail extension; reset() clap_plugin.reset; the regions are what activate()
 * allocates.
 */
#ifndef NSC_NSC_H_
#define NSC_NSC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NSC_ABI 2
#define NSC_MAGIC 0x3143534eu /* "NSC1" */
#define NSC_BLOCK 32          /* frames per process() */
#define NSC_MAX_HEROES 8

#if defined(_WIN32)
#define NSC_EXPORT __declspec(dllexport)
#else
#define NSC_EXPORT __attribute__((visibility("default")))
#endif

/* one voice's controls for process_voices(), as the board's key logic gives them */
typedef struct nsc_voice {
	float note;    /* pitch in semitones (69 = A4), fractional */
	float pres;    /* pressure: 0 released, 1 the full press */
	float vpres;   /* pressure velocity: > 0 striking, < 0 releasing */
	float but_x;   /* tilt along the row, -1 .. 1 */
	float but_y;   /* tilt across the row, -1 .. 1 */
	float gate;    /* 1 while held */
	float strike;  /* the strike that opened the gate, 0 .. 1 */
	float release; /* the release that closed it, 0 .. 1 */
} nsc_voice;

/* the instrument's controls: accelerometer (g), gyroscope, pedal */
typedef struct nsc_globals {
	float acc_abs, acc_x, acc_y, acc_z, rot_x, rot_y, rot_z, pedal;
} nsc_globals;

/* a UMP packet (1 to 4 words, by its message type) at a frame of the block */
typedef struct nsc_event {
	uint32_t frame;  /* 0 .. NSC_BLOCK - 1 */
	int32_t note_id; /* the host's id of the note a Note On starts (CLAP's note_id); -1: none */
	uint32_t words[4];
} nsc_event;

enum nsc_param_kind { NSC_PARAM_CONTINUOUS = 0,
	NSC_PARAM_STEPS = 1,
	NSC_PARAM_TOGGLE = 2 };

typedef struct nsc_param {
	uint32_t id;       /* stable: FNV-1a 32 of the name (a host stores parameters by it) */
	const char* name;  /* short id, e.g. "cutoff" */
	const char* group; /* UI group */
	float min, max;
	int32_t kind; /* nsc_param_kind */
	float def;    /* its default: the value after init() */
} nsc_param;

typedef struct nsc_preset {
	const char* name;
	const char* heroes[NSC_MAX_HEROES]; /* its own hero parameters (NULL: the engine's) */
} nsc_preset;

typedef struct nsc_desc {
	uint32_t magic, abi;  /* NSC_MAGIC, NSC_ABI */
	const char* id;       /* "accordion": the engine's short name (the board's voice list) */
	const char* uid;      /* "com.sorbet-ai.awva.accordion": the plugin / package id */
	const char* name;     /* a title */
	const char* summary;  /* one line */
	const char* version;  /* the engines repository commit it was built from */
	uint32_t schema_hash; /* FNV-1a over the parameters' ids, ranges and kinds: a change breaks stored values */
	uint32_t inputs;      /* the Input bits of engine.h (pressure, velocity, x, y, pedal, motion) */
	int32_t channels;     /* 1 or 2 */
	int32_t max_voices;
	uint32_t instr_per_block; /* the budget: QEMU instructions per block, worst preset, 10 voices */
	float tail_seconds;
	uint32_t state_bytes, fast_bytes, large_bytes; /* the regions init() takes */
	uint32_t stack_bytes;                          /* the deepest process() stack measured on the board (0: not measured) */
	int32_t num_params, num_presets;
	const nsc_param* params;
	const nsc_preset* presets;
	const char* heroes[NSC_MAX_HEROES]; /* the engine's hero parameters (names; NULL-terminated) */
} nsc_desc;

enum nsc_mode {
	NSC_MODE_LEGATO = 1, /* value 1: mono legato (one voice, slurs), 0: polyphonic (the default) */
};

typedef struct nsc_component {
	const nsc_desc* desc;
	void (*init)(void* state, void* fast, void* large, int32_t sample_rate, int32_t voices);
	void (*reset)(void* state);                                 /* silence, the parameters and the preset kept */
	int32_t (*mode)(void* state, int32_t which, int32_t value); /* 1 done, 0 not supported */
	void (*process)(void* state, const nsc_event* events, int32_t num_events, float* const* out);
	void (*process_voices)(void* state, const nsc_voice* voices, const nsc_globals* globals, float* const* out);
	int32_t (*active_voices)(const void* state);
	/* after process(): the host's note ids (nsc_event.note_id) of the notes that ended since the
	 * last call: released, then the engine's tail passed or it went idle, or another note took
	 * their key (CLAP NOTE_END). Returns how many (at most max) were written; call again while it
	 * returns max. */
	int32_t (*ended)(void* state, int32_t* note_ids, int32_t max);
	int32_t (*voices)(const void* state);
	int32_t (*preset)(const void* state); /* -1: none */
	void (*set_preset)(void* state, int32_t index);
	float (*param)(const void* state, int32_t index);
	int32_t (*set_param)(void* state, int32_t index, float value); /* 1 taken (clamped, rounded by kind) */
	int32_t (*param_index)(uint32_t id);                           /* -1: no such parameter */
} nsc_component;

/* the one exported symbol */
typedef const nsc_component* (*nsc_entry_fn)(void);
NSC_EXPORT const nsc_component* nsc_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* NSC_NSC_H_ */

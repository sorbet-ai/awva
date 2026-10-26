/* SPDX-FileCopyrightText: 2026 Sorbet AI
 * SPDX-License-Identifier: Apache-2.0
 *
 * The engine component (ABI 2): one sound engine as a C ABI, for a plugin or any host that loads
 * it from a DLL / dylib / .so (and later an embedded host, from a module). UMP in, audio
 * out, parameters by stable id and name, factory presets, and what the host needs to know first:
 * a title, a colour, the heroes, the sample rates, the budget, the memory and the stack.
 * (docs/writing_a_host.md walks through a host.)
 *
 * One exported symbol, nsc_entry(), gives the component: a descriptor (static, readable before
 * any instance exists) and its functions. The host owns the memory: it allocates the three regions
 * the descriptor asks for (state, fast, large; zero-filled, 64-byte aligned) and passes them to
 * init(), which refuses (-1) a sample rate outside min_rate .. max_rate. No call allocates, blocks
 * or throws; nothing is global (two instances are independent).
 *
 * Time, in integer frames: process() renders one block of NSC_BLOCK frames. Its events are UMP
 * packets with their frame, counted from the block's start: 0 .. NSC_BLOCK - 1, or negative (down
 * to -NSC_BLOCK) for an event that arrived after its block was rendered. MIDI is taken in 2 ms
 * steps: step k is at frame ceil(k sr / 500), and an event is taken at the
 * first step at or after its frame; before each block, every step due by its start runs. So a late
 * event lands where it would have on time, at every rate, with no added latency (in the first
 * block, a negative frame is frame 0).
 *
 * Notes: a Note On may carry the host's id for it (nsc_event.note_id; -1: none). After process(),
 * ended() hands out the ids of the notes that have ended: released, then the engine's tail passed
 * or it went idle, or another note took their key (CLAP NOTE_END).
 *
 * Input: MIDI 2.0 channel voice messages (UMP type 4) in their standard meaning (pressure full
 * scale = the engine scale's full pressure, 16-bit velocities, Pitch 7.25, bipolar controllers centred;
 * per-note y as Assignable Per-Note Controller 4 or Registered 74, per-note x as Assignable 3), and
 * MIDI 1.0 channel voice messages in UMP (type 2, including MPE); the other types are ignored, and
 * every group is taken as one. Inside, the component runs the one decoder (awva/ump.h), the voice
 * allocator and, in NSC_MODE_LEGATO, mono legato.
 *
 * Parameters: the engine's own, then "output" (the output level, 0 .. 2, default 1: at 1 the
 * engine's samples bit for bit). Each has a stable id (FNV-1a of its name) and a declared default
 * (its value after init()). After set_preset() every parameter may have changed.
 *
 * process_voices() is the other way in: the host allocates the voices itself and gives each its
 * controls (e.g. an embedded instrument with its own sensors), and the component only renders.
 *
 * Map to CLAP: desc->uid is the plugin id (com.sorbet-ai.awva.<id>), name (a title) / summary /
 * version its descriptor; process() is clap_plugin.process with its note and MIDI 2.0 events
 * (CLAP_EVENT_MIDI2), a late one at its negative frame; ended() gives CLAP_EVENT_NOTE_END with the
 * note ids; params by id are clap_plugin_params (id = nsc_param.id, value in min .. max, default
 * def; NSC_PARAM_STEPS / NSC_PARAM_TOGGLE = CLAP_PARAM_IS_STEPPED); presets are a preset-discovery
 * provider's factory presets (by index here), and a preset as a file is CLAP's state or a preset
 * file (load_preset / save_preset, the awva-preset/1 JSON of awva/preset.h); active_voices() is
 * the voice-info extension's count;
 * tail_seconds the tail extension; min_rate .. max_rate what activate() accepts; reset()
 * clap_plugin.reset; the regions are what activate() allocates.
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
#define NSC_MAX_EVENTS 1024   /* events per process() for ended()'s guarantee */
#define NSC_MAX_NOTE_IDS 64   /* notes with a host id tracked at once (see ended()) */
#define NSC_MAX_HEROES 8

#if defined(_WIN32)
#define NSC_EXPORT __declspec(dllexport)
#else
#define NSC_EXPORT __attribute__((visibility("default")))
#endif

/* one voice's controls for process_voices(), as the host's voice logic gives them */
typedef struct nsc_voice {
	float pitch;         /* semitones (69 = A4), fractional */
	float pressure;      /* 0 released, 1 MIDI 2.0's full scale (engines take up to 1.5) */
	float pressure_rate; /* how fast the pressure moves: > 0 pressing, < 0 lifting */
	float timbre;        /* -1 .. 1 (MIDI 2.0's Registered Per-Note Controller 74) */
	float velocity;      /* the Note On's velocity, 0 .. 1 */
	float release;       /* the Note Off's velocity, 0 .. 1 */
	uint8_t gate;        /* 1 from the Note On to the Note Off */
} nsc_voice;

/* the instrument's controls: accelerometer (g), gyroscope, pedal */
typedef struct nsc_globals {
	float sustain; /* CC64, 0 .. 1 */
	float accel_g; /* the acceleration's magnitude in g (1 at rest) */
	float spin[3]; /* the angular rate about each axis, -1 .. 1 */
} nsc_globals;

/* a UMP packet (1 to 4 words, by its message type) at a frame of the block */
typedef struct nsc_event {
	int32_t frame;   /* its frame, from the block's start: 0 .. NSC_BLOCK - 1, or up to -NSC_BLOCK for an
	                    event that came after its block was rendered (taken at its own step: exact) */
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
	const char* id;       /* "accordion": the engine's short name */
	const char* uid;      /* "com.sorbet-ai.awva.accordion": the plugin / package id */
	const char* name;     /* a title for people ("Electric Piano"; the id when the engine has none) */
	const char* summary;  /* one line */
	const char* version;  /* the engines repository commit it was built from */
	uint32_t schema_hash; /* FNV-1a over the parameters' ids, ranges and kinds: a change breaks stored values */
	uint32_t inputs;      /* the Input bits of engine.h (pressure, its rate, timbre, velocities, pedal, motion) */
	int32_t channels;     /* 1 or 2 */
	uint32_t led;         /* its colour, 0xrrggbb (e.g. for a host's LED or UI); 0: none, the host's default */
	int32_t max_voices;
	uint32_t instr_per_block; /* the budget: Cortex-M7 instructions per block (QEMU), worst preset, 10 voices */
	float tail_seconds;
	int32_t min_rate, max_rate;                    /* the sample rates it supports (init() refuses others) */
	uint32_t state_bytes, fast_bytes, large_bytes; /* the regions init() takes */
	uint32_t stack_bytes;                          /* the deepest process() stack measured on a Cortex-M7 (0: not measured) */
	int32_t num_params, num_presets;
	const nsc_param* params;
	const nsc_preset* presets;
	const char* heroes[NSC_MAX_HEROES]; /* the engine's hero parameters (names; NULL-terminated) */
} nsc_desc;

/* what load_preset() did */
typedef struct nsc_preset_report {
	int32_t loaded;    /* 1: its values are set (0: refused, nothing changed, error says why) */
	int32_t exact;     /* 1: its params_hash is the engine's (preset.h), every value as it has it, nothing skipped or missing */
	int32_t clamped;   /* values out of their range, clamped (steps rounded) */
	int32_t skipped;   /* parameters this engine doesn't have, ignored */
	int32_t missing;   /* parameters the preset doesn't name: their values after init() */
	const char* error; /* when refused: "another engine's preset" or the format error (static text) */
} nsc_preset_report;

enum nsc_mode {
	NSC_MODE_LEGATO = 1, /* value 1: mono legato (one voice, slurs), 0: polyphonic (the default) */
};

typedef struct nsc_component {
	const nsc_desc* desc;
	/* 0; -1: a sample rate outside min_rate .. max_rate (nothing done) */
	int32_t (*init)(void* state, void* fast, void* large, int32_t sample_rate, int32_t voices);
	void (*reset)(void* state);                                 /* silence, the parameters and the preset kept; the notes' ids end (ended()) */
	int32_t (*mode)(void* state, int32_t which, int32_t value); /* 1 done, 0 not supported */
	void (*process)(void* state, const nsc_event* events, int32_t num_events, float* const* out);
	void (*process_voices)(void* state, const nsc_voice* voices, const nsc_globals* globals, float* const* out);
	int32_t (*active_voices)(const void* state);
	/* after process(): the host's note ids (nsc_event.note_id) of the notes that ended since the
	 * last call: released, then the engine's tail passed or it went idle, or another note took
	 * their key (CLAP NOTE_END). Returns how many (at most max) were written; call again while it
	 * returns max.
	 * The guarantee: every Note On's id comes back from ended() exactly once (ids on other events
	 * are ignored; a MIDI 1.0 Note On of velocity 0 is a Note Off), provided the host calls it
	 * after each process() until it returns less than max and passes at most NSC_MAX_EVENTS
	 * events a block. NSC_MAX_NOTE_IDS notes are tracked at once; a Note On with an id beyond
	 * that ends the oldest tracked note (the oldest released one, else the oldest held one) at
	 * once, and a Note On that can't be queued ends at once itself. */
	int32_t (*ended)(void* state, int32_t* note_ids, int32_t max);
	int32_t (*voices)(const void* state);
	int32_t (*preset)(const void* state); /* -1: none */
	void (*set_preset)(void* state, int32_t index);
	float (*param)(const void* state, int32_t index);
	int32_t (*set_param)(void* state, int32_t index, float value); /* 1 taken (clamped, rounded by kind) */
	int32_t (*param_index)(uint32_t id);                           /* -1: no such parameter */
	/* a preset as text, awva-preset/1 JSON (awva/preset.h): parameters by name, so one from another
	 * version of the engine loads (out of range clamped, unknown skipped, missing at their init()
	 * values; report may be NULL). Audio thread, as set_preset; about 5 KB of stack. 1 loaded. */
	int32_t (*load_preset)(void* state, const char* json, uint32_t length, nsc_preset_report* report);
	/* the current values as a preset titled title, into out (NUL-terminated); its length, 0 if it
	 * didn't fit (8 KB always does). Any thread, as param(); about 5 KB of stack. */
	uint32_t (*save_preset)(const void* state, const char* title, char* out, uint32_t capacity);
} nsc_component;

/* the one exported symbol */
typedef const nsc_component* (*nsc_entry_fn)(void);
NSC_EXPORT const nsc_component* nsc_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* NSC_NSC_H_ */

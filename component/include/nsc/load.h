// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
//
// Loading a component (nsc.h) from its shared library: the host side, header only. nsc_load()
// opens the library, finds nsc_entry and checks the descriptor's magic and ABI; nsc_unload()
// closes it. The memory of an instance (64-byte aligned, zero-filled) is the host's:
// nsc_instance_new() / nsc_instance_free() for a host that may use the heap.
#ifndef NSC_LOAD_H_
#define NSC_LOAD_H_

#include <stdlib.h>
#include <string.h>

#include "nsc/nsc.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct nsc_library {
	void* handle;
	const nsc_component* c;
} nsc_library;

/* 0 loaded; -1 no library, -2 no nsc_entry, -3 not a component of this ABI */
static inline int nsc_load(const char* path, nsc_library* out) {
	out->handle = NULL;
	out->c = NULL;
#if defined(_WIN32)
	HMODULE h = LoadLibraryA(path);
	if (!h) return -1;
	nsc_entry_fn f = (nsc_entry_fn)(void*)GetProcAddress(h, "nsc_entry");
#else
	void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!h) return -1;
	nsc_entry_fn f = (nsc_entry_fn)dlsym(h, "nsc_entry");
#endif
	const nsc_component* c = f ? f() : NULL;
	int r = !f ? -2 : (!c || !c->desc || c->desc->magic != NSC_MAGIC || c->desc->abi != NSC_ABI) ? -3 :
	                                                                                               0;
	if (r != 0) {
#if defined(_WIN32)
		FreeLibrary(h);
#else
		dlclose(h);
#endif
		return r;
	}
	out->handle = (void*)h;
	out->c = c;
	return 0;
}

static inline void nsc_unload(nsc_library* lib) {
	if (!lib->handle) return;
#if defined(_WIN32)
	FreeLibrary((HMODULE)lib->handle);
#else
	dlclose(lib->handle);
#endif
	lib->handle = NULL;
	lib->c = NULL;
}

/* one instance's three regions (the state is the hot region) */
typedef struct nsc_instance {
	void *state, *stream, *bulk;
} nsc_instance;

static inline void* nsc_region(size_t n) {
	n = (n + 63) & ~(size_t)63;
	if (n == 0) n = 64;
#if defined(_WIN32)
	void* p = _aligned_malloc(n, 64);
#else
	void* p = NULL;
	if (posix_memalign(&p, 64, n) != 0) p = NULL;
#endif
	if (p) memset(p, 0, n);
	return p;
}
static inline void nsc_region_free(void* p) {
#if defined(_WIN32)
	_aligned_free(p);
#else
	free(p);
#endif
}

/* 0 ok, -1 out of memory */
static inline int nsc_instance_new(const nsc_component* c, nsc_instance* in) {
	in->state = nsc_region(c->desc->hot_bytes);
	in->stream = nsc_region(c->desc->stream_bytes);
	in->bulk = nsc_region(c->desc->bulk_bytes);
	if (in->state && in->stream && in->bulk) return 0;
	nsc_region_free(in->state);
	nsc_region_free(in->stream);
	nsc_region_free(in->bulk);
	in->state = in->stream = in->bulk = NULL;
	return -1;
}
static inline void nsc_instance_free(nsc_instance* in) {
	nsc_region_free(in->state);
	nsc_region_free(in->stream);
	nsc_region_free(in->bulk);
	in->state = in->stream = in->bulk = NULL;
}

#endif /* NSC_LOAD_H_ */

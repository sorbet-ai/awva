// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Where an embedded host puts a DSP buffer: one of its linker sections (e.g. ".ram1_clear", a
// RAM zeroed at boot, from its linker script). Only ARM ELF builds (an embedded build, the QEMU target
// renders) have those sections; a PC build (the SDK, the renderers) places the buffer as any
// other, so its linker never sees the name (Mach-O rejects it: its section names are
// "segment,section").
#ifndef AWVA_PLACEMENT_H_
#define AWVA_PLACEMENT_H_

#if defined(__arm__) && defined(__ELF__)
#define AWVA_SECTION(name) __attribute__((section(name)))
#else
#define AWVA_SECTION(name)
#endif

#endif

// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Where the board puts a DSP buffer: one of its linker sections (ld/STM32H743xI_uf2.ld, e.g.
// ".ram1_clear", AHB SRAM1/2, zeroed at boot). Only builds for the board (ARM, ELF: the firmware
// and the QEMU target renders) have those sections; a host build (the SDK, the renderers, the
// virtual board) places the buffer as any other, so its linker never sees the name (Mach-O
// rejects it: its section names are "segment,section").
#ifndef AWVA_PLACEMENT_H_
#define AWVA_PLACEMENT_H_

#if defined(__arm__) && defined(__ELF__)
#define AWVA_SECTION(name) __attribute__((section(name)))
#else
#define AWVA_SECTION(name)
#endif

#endif

// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// A host's timing and voices, as it runs an engine: its sample rate; its control rate, the steps
// MIDI is taken in (timed_midi.h: Hz, or 0 for every engine block, BLOCK frames); the engine's
// voices. An embedded host with a fixed scan passes its scan rate (500 Hz: a 2 ms scan); a plugin
// keeps the default, every engine block.
#ifndef AWVA_SDK_HOST_CONFIG_H_
#define AWVA_SDK_HOST_CONFIG_H_

namespace awva::sdk {

struct HostConfig {
	int sample_rate = 48000;
	int control_hz = 0;
	int voices = 10;
};

} // namespace awva::sdk

#endif // AWVA_SDK_HOST_CONFIG_H_

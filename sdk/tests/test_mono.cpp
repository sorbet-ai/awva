// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Mono (awva/mono.h): one voice played by every held key; the keys' pressures decide how it goes
// from note to note.
#include "doctest/doctest.h"
#include "awva/mono.h"

namespace {

using awva::MonoFamily;
using awva::NoteControls;
using Mono = awva::voices::Mono<8>;
using Style = awva::voices::MonoStyle;
constexpr float DT = 0.002f; // a control step

NoteControls key(float pitch, float pres, float rate = 0.0f, float timbre = 0.0f) {
	return {pitch, pres, rate, timbre, 0.0f, 0.0f, static_cast<uint8_t>(pres > 0.0f)};
}
void steps(Mono& m, int n) {
	for (int i = 0; i < n; i++) m.step(DT);
}

} // namespace

TEST_CASE("mono: the first key plays the voice, its strike too") {
	Mono m(Style::of(MonoFamily::finger));
	m.note(1, key(60, 0.3f, 0.7f));
	m.step(DT);
	CHECK(m.voice().pitch == 60.0f);
	CHECK(m.voice().pressure == doctest::Approx(0.3f));
	CHECK(m.voice().pressure_rate == doctest::Approx(0.7f));
	CHECK(m.voice().gate == 1);
}

TEST_CASE("mono: two keys pressed fully don't double the level, and lifting lowers it with them") {
	Mono m;
	m.note(1, key(60, 0.6f));
	m.step(DT);
	m.note(2, key(62, 0.6f)); // pressed fully over the held key: no louder than the held level
	m.step(DT);
	CHECK(m.voice().pressure == doctest::Approx(0.6f));
	m.note(2, key(62, 0.8f)); // the strongest key pressed harder: louder
	m.step(DT);
	CHECK(m.voice().pressure == doctest::Approx(0.8f));
	m.note(1, key(60, 0.1f));
	m.note(2, key(62, 0.3f)); // both lifting: the sum
	m.step(DT);
	CHECK(m.voice().pressure == doctest::Approx(0.4f));
}

TEST_CASE("mono: a roll keeps the level, and the timbre follows the keys' pressures") {
	Mono m;
	m.note(1, key(60, 0.6f, 0.0f, -1.0f));
	steps(m, 5);
	for (int k = 0; k <= 10; k++) { // from key 1 to key 2
		const float b = 0.06f * static_cast<float>(k);
		m.note(1, key(60, 0.6f - b, 0.0f, -1.0f));
		m.note(2, key(62, b, 0.0f, 1.0f));
		m.step(DT);
		CHECK(m.voice().pressure == doctest::Approx(0.6f));
		CHECK(m.voice().timbre == doctest::Approx(-1.0f + 2.0f * b / 0.6f));
	}
}

TEST_CASE("mono glide: the pitch is the notes weighted by pressure squared") {
	Mono m(Style::of(MonoFamily::glide));
	m.note(1, key(60, 0.6f));
	m.step(DT);
	m.note(2, key(62, 0.6f));
	steps(m, 5);
	CHECK(m.voice().pitch == doctest::Approx(61.0f)); // both held equally: between
	m.note(1, key(60, 0.3f));
	steps(m, 5);
	CHECK(m.voice().pitch == doctest::Approx(60.0f + 2.0f * 0.36f / 0.45f));
}

TEST_CASE("mono finger: the fingering switches past threshold + hysteresis, the pitch moves at the roll's speed") {
	Mono m(Style::of(MonoFamily::finger));
	m.note(1, key(60, 0.6f));
	m.step(DT);
	m.note(2, key(62, 0.6f)); // a share of 0.5: the fingering stays
	steps(m, 50);
	CHECK(m.voice().pitch == 60.0f);
	m.note(1, key(60, 0.45f)); // its share 0.57: it takes over
	m.step(DT);
	CHECK(m.voice().pitch > 60.0f);
	CHECK(m.voice().pitch < 62.0f);
	steps(m, 100);
	CHECK(m.voice().pitch == 62.0f);
	m.note(1, key(60, 0.6f)); // back to 0.5 each: the new fingering stays (hysteresis)
	steps(m, 20);
	CHECK(m.voice().pitch == 62.0f);
}

TEST_CASE("mono finger: a slow roll moves the pitch slowly, a snap quickly") {
	const auto time_to = [](int roll_steps) {
		Mono m(Style::of(MonoFamily::finger));
		m.note(1, key(60, 0.6f));
		m.step(DT);
		int t = 0;
		for (int k = 1; k < 400; k++) {
			const float b = k < roll_steps ? 0.6f * static_cast<float>(k) / static_cast<float>(roll_steps) : 0.6f;
			m.note(1, key(60, 0.6f - b));
			m.note(2, key(64, b));
			m.step(DT);
			if (m.voice().pitch > 60.01f && t == 0) t = -k;
			if (m.voice().pitch >= 63.99f) return k + t; // steps from leaving 60 to reaching 64
		}
		return 999;
	};
	CHECK(time_to(150) > 3 * time_to(5)); // a 300 ms roll against a 10 ms one
}

TEST_CASE("mono: a gentle press slurs (no strike), a hard one is re-articulated") {
	Mono m(Style::of(MonoFamily::finger));
	m.note(1, key(60, 0.6f));
	steps(m, 5);
	m.note(2, key(62, 0.1f, 0.3f)); // gentle: its rising rate is held back
	m.step(DT);
	CHECK(m.voice().pressure_rate == 0.0f);
	CHECK(m.voice().pitch == 60.0f);
	Mono h(Style::of(MonoFamily::finger));
	h.note(1, key(60, 0.6f));
	steps(h, 5);
	h.note(2, key(62, 0.1f, 0.8f)); // hard: tongued, the pitch goes to it at once
	h.step(DT);
	CHECK(h.voice().pressure_rate == doctest::Approx(0.8f));
	steps(h, 20);
	CHECK(h.voice().pitch == 62.0f);
}

TEST_CASE("mono struck: the new key strikes when it takes over, at its pitch") {
	Mono m(Style::of(MonoFamily::struck));
	m.note(1, key(60, 0.6f));
	steps(m, 5);
	m.note(2, key(64, 0.6f, 0.1f));
	steps(m, 10);
	m.note(1, key(60, 0.3f)); // key 2 takes over
	m.step(DT);
	CHECK(m.voice().pitch == 64.0f);
	CHECK(m.voice().pressure_rate >= 0.2f);
}

TEST_CASE("mono: the last key's release is the voice's, where it is") {
	Mono m;
	m.note(1, key(60, 0.6f));
	steps(m, 5);
	m.note(1, key(60, 0.0f, -0.4f));
	CHECK(m.empty());
	CHECK(m.voice().pressure == 0.0f);
	CHECK(m.voice().pressure_rate == doctest::Approx(-0.4f));
	CHECK(m.voice().gate == 0);
	CHECK(m.voice().pitch == 60.0f);
}

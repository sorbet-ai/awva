// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Mono legato (awva/legato.h): which note the voice plays, at which pressure, and which strikes
// reach it.
#include "doctest/doctest.h"
#include "awva/legato.h"

namespace {

using Legato = awva::voices::Legato<8>;
using Signals = Legato::Signals;

Signals sig(float note, float pres, float rate) { return {note, pres, rate, 0.0f, 0.0f, 0.0f, static_cast<uint8_t>(pres > 0.0f)}; }

} // namespace

TEST_CASE("legato: the first note's strike reaches the voice") {
	Legato l;
	l.note(1, sig(60, 0.2f, 0.7f));
	CHECK(l.top() == 1);
	CHECK(l.signals().pressure_rate == doctest::Approx(0.7f));
}

TEST_CASE("legato: a slurred note's own strike never reaches the voice, even when it overtakes the held note") {
	Legato l;
	l.note(1, sig(60, 0.5f, 0.6f));
	l.note(1, sig(60, 0.5f, 0.0f)); // held, steady
	float last = 0.5f;
	for (int k = 1; k <= 9; k++) { // the next key's press, rising past the held one's 0.5
		l.note(2, sig(62, 0.1f * (float)k, 0.6f));
		const Signals s = l.signals();
		CHECK(s.pitch == 62.0f);
		CHECK(s.pressure_rate == 0.0f);
		CHECK(s.pressure >= last); // the strongest note's pressure: the dynamic carries over
		last = s.pressure;
	}
	CHECK(l.signals().pressure == doctest::Approx(0.9f));
}

TEST_CASE("legato: after its press, an accent on the slurred note strikes") {
	Legato l;
	l.note(1, sig(60, 0.5f, 0.0f));
	l.note(2, sig(62, 0.3f, 0.5f));
	l.note(2, sig(62, 0.7f, 0.5f));
	CHECK(l.signals().pressure_rate == 0.0f);
	l.note(2, sig(62, 0.7f, 0.0f)); // the press is over
	l.note(2, sig(62, 0.9f, 0.4f)); // an accent
	CHECK(l.signals().pressure_rate == doctest::Approx(0.4f));
}

TEST_CASE("legato: a slurred press that starts at rate 0 is held back until it has risen and stopped") {
	Legato l;
	l.note(1, sig(60, 0.5f, 0.0f));
	l.note(2, sig(62, 0.05f, 0.0f));
	l.note(2, sig(62, 0.6f, 0.8f));
	CHECK(l.signals().pressure_rate == 0.0f);
	l.note(2, sig(62, 0.8f, 0.3f));
	CHECK(l.signals().pressure_rate == 0.0f);
	l.note(2, sig(62, 0.8f, 0.0f));
	l.note(2, sig(62, 0.9f, 0.3f));
	CHECK(l.signals().pressure_rate == doctest::Approx(0.3f));
}

TEST_CASE("legato: the held note lifting while the slurred note rises: the rise is still held back") {
	Legato l;
	l.note(1, sig(60, 0.5f, 0.0f));
	l.note(2, sig(62, 0.2f, 0.6f));
	l.note(1, sig(60, 0.0f, -0.5f)); // the old key lets go: it leaves the stack, the new note moves down
	CHECK(l.size() == 1);
	l.note(2, sig(62, 0.6f, 0.6f));
	CHECK(l.signals().pressure_rate == 0.0f);
	CHECK(l.signals().pressure == doctest::Approx(0.6f));
}

TEST_CASE("legato: releasing the newest note returns to the held one, its own rate as it is") {
	Legato l;
	l.note(1, sig(60, 0.5f, 0.0f));
	l.note(2, sig(62, 0.6f, 0.5f));
	l.note(2, sig(62, 0.0f, -0.4f));
	CHECK(l.top() == 1);
	CHECK(l.signals().pitch == 60.0f);
	l.note(1, sig(60, 0.7f, 0.3f)); // an accent on the note held throughout
	CHECK(l.signals().pressure_rate == doctest::Approx(0.3f));
	CHECK(l.signals().pressure == doctest::Approx(0.7f));
}

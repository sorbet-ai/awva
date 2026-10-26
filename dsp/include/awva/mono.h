// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// Mono: one voice, played by every held key at once. A host's mono mode: the keys are notes on
// one voice, and how the voice goes from one to the next is the player's fingers', not a timer's.
// Pure: no HAL, no heap. The host gives every key's controls to note() (a press, its changes, its
// release), calls step() once a control step, and plays voice() on its one voice.
//
// Each step, from all held keys:
//   loudness  the voice's pressure is the keys' pressures added up (rolling from one key to the
//             next keeps it), but while keys overlap never above the strongest key's or the level
//             the voice had as the overlap began (two keys pressed fully don't double it)
//   pitch     by the engine's family (MonoFamily, its EngineInfo):
//             glide    the held notes' pitches weighted by pressure squared (two keys O, N: the
//                      share t = pN / (pO + pN) through a gentle S), continuously: a slide as slow
//                      or as fast as the roll; both held equally sits between
//             finger   the fingering is one note. Another held key takes it over when its share
//                      p / (p + p_fingering) passes threshold + hysteresis (back: the old key's
//                      share past the same). The pitch then moves there, eased in and out, over
//                      `roll` of the time the whole roll would take at the speed the share crossed
//                      at: from fastest_s (a snap) to slowest_s (a slow roll), and never faster
//                      than per_semitone_s a semitone (a bore changed faster slaps its reed or lips)
//             struck   as finger, but the pitch goes at once and the new key strikes as it takes over
//   timbre    the held keys' timbre weighted by pressure
//   strike    the keys' pressure rates added up, but a key pressed while another was held doesn't
//             strike while it rises (a slur) unless its strike, within decide_s of its press,
//             passes `strike`: then it is re-articulated (tongued, a new bow): its strike plays
//             and the pitch goes to it in the shortest move
// Lifting either key completes or reverts a change through the same pressures; the last key's
// release is the voice's.
#ifndef AWVA_MONO_H_
#define AWVA_MONO_H_

#include "controls.h"

namespace awva::voices {

struct MonoStyle {
	MonoFamily family = MonoFamily::finger;
	float threshold = 0.5f;         // the share of two keys' pressure at which one takes the fingering over
	float hysteresis = 0.05f;       // it takes over above threshold + this (and gives back below threshold - this)
	float strike = 0.5f;            // a key pressed during another whose strike passes this is re-articulated
	float decide_s = 0.010f;        // how long a press may take to show its strike
	float roll = 0.25f;             // the pitch's move: this share of the roll's time
	float fastest_s = 0.015f;       // its shortest
	float slowest_s = 0.150f;       // and longest
	float per_semitone_s = 0.0025f; // and never faster than this a semitone
	// a family's defaults: glide moves as fast as the fingers (no bore to slap)
	[[nodiscard]] static constexpr MonoStyle of(MonoFamily f) {
		MonoStyle m;
		m.family = f;
		if (f == MonoFamily::glide) m.fastest_s = 0.005f, m.per_semitone_s = 0.0f;
		return m;
	}
};

template <int Max> class Mono {
	static_assert(Max >= 2, "a stack");

public:
	explicit Mono(const MonoStyle& s = MonoStyle{}) :
	    st_(s) {}
	void set_style(const MonoStyle& s) { st_ = s; }
	[[nodiscard]] const MonoStyle& style() const { return st_; }

	// key id's controls (any key, in any order): held (its gate, pressure above 0) it is pressed or
	// updated, else it leaves (the last key's release is the voice's). Past Max held keys the
	// oldest is forgotten.
	void note(int id, const NoteControls& c) {
		int at = find(id);
		const bool held = c.gate && c.pressure > 0.0f;
		if (!held) {
			if (at < 0) return;
			remove(at);
			if (n_ == 0) { // the voice's release, where it is
				v_.pressure = 0.0f;
				v_.pressure_rate = c.pressure_rate < 0.0f ? c.pressure_rate : 0.0f;
				v_.release = c.release;
				v_.gate = 0;
				sounding_ = false;
				finger_ = -1;
				mv_len_ = 0.0f;
			}
			return;
		}
		if (at < 0) { // a press: during another key's, a slur unless its strike says otherwise
			if (n_ == Max) remove(0);
			at = n_++;
			Key& k = k_[at];
			k.id = id;
			k.pprev = 0.0f;
			k.strike = c.pressure_rate > 0.0f ? c.pressure_rate : 0.0f;
			k.age = 0.0f;
			k.slurred = at > 0;
			k.art = false;
		} else if (k_[at].age <= st_.decide_s && c.pressure_rate > k_[at].strike) {
			k_[at].strike = c.pressure_rate;
		}
		Key& k = k_[at];
		k.c = c;
		if (k.slurred && !k.art && k.age <= st_.decide_s && k.strike > st_.strike) {
			k.art = true; // re-articulated: tongued (a new bow), the pitch goes to it at once
			finger_ = id;
			if (st_.family == MonoFamily::struck) {
				move_to(c.pitch, 0.0f);
				strike_out_ = k.strike;
			} else {
				move_to(c.pitch, st_.fastest_s);
			}
		}
	}
	// a control step of `seconds`: the voice from every held key
	void step(float seconds) {
		if (n_ == 0) return;
		const Key& newest = k_[n_ - 1];
		if (!sounding_) { // the first key: the voice starts on it
			sounding_ = true;
			finger_ = newest.id;
			pitch_ = target_ = newest.c.pitch;
			mv_len_ = 0.0f;
		}
		float P = 0.0f, pmax = 0.0f, y = 0.0f, rate = 0.0f, w = 0.0f, wn = 0.0f;
		int art = -1;
		for (int i = 0; i < n_; i++) {
			Key& k = k_[i];
			const float p = k.c.pressure > 0.0f ? k.c.pressure : 0.0f;
			P += p;
			pmax = p > pmax ? p : pmax;
			y += p * k.c.timbre;
			w += p * p;
			wn += p * p * k.c.pitch;
			// a slurred key's own press is no strike; its press is over when its rate is back at 0
			if (k.slurred && !k.art && k.age > st_.decide_s && k.c.pressure_rate <= 0.0f) k.slurred = false;
			rate += k.slurred && !k.art && k.c.pressure_rate > 0.0f ? 0.0f : k.c.pressure_rate;
			if (k.art && i == 0) k.art = false; // (the oldest held: nothing left to articulate over)
			if (k.art) art = k.id;
		}
		float target = target_, move = -1.0f; // (move >= 0: the pitch moves to target over move s)
		if (st_.family == MonoFamily::glide && art < 0) {
			target = w > 0.0f ? wn / w : newest.c.pitch;
			if (target - target_ > 0.5f || target_ - target > 0.5f) move = st_.fastest_s; // (a snap: no step)
		} else {
			const int f = find(finger_);
			if (f < 0) { // the fingering's key let go first: the strongest held takes it
				int best = n_ - 1;
				for (int i = 0; i < n_; i++)
					if (k_[i].c.pressure > k_[best].c.pressure) best = i;
				finger_ = k_[best].id;
				move = st_.family == MonoFamily::struck ? 0.0f : st_.fastest_s;
			} else { // another key takes the fingering over past threshold + hysteresis
				const float pf = k_[f].c.pressure > 0.0f ? k_[f].c.pressure : 0.0f, pf0 = k_[f].pprev;
				int best = -1;
				float bshare = 0.0f, bspeed = 0.0f;
				for (int i = 0; i < n_; i++) {
					if (i == f || (k_[f].art && i < f)) continue; // (a re-articulated note keeps it from the older keys)
					const float pj = k_[i].c.pressure > 0.0f ? k_[i].c.pressure : 0.0f;
					const float share = pj + pf > 0.0f ? pj / (pj + pf) : 0.0f;
					if (share > st_.threshold + st_.hysteresis && share > bshare) {
						const float pj0 = k_[i].pprev;
						const float share0 = pj0 + pf0 > 0.0f ? pj0 / (pj0 + pf0) : 0.0f;
						best = i, bshare = share, bspeed = seconds > 0.0f ? (share - share0) / seconds : 0.0f;
					}
				}
				if (best >= 0) {
					finger_ = k_[best].id;
					if (st_.family == MonoFamily::struck) {
						move = 0.0f;
						strike_out_ = k_[best].strike > 0.2f ? k_[best].strike : 0.2f;
					} else { // `roll` of the time the whole roll takes at this speed
						const float t = bspeed > 0.0f ? st_.roll / bspeed : st_.slowest_s;
						move = t < st_.fastest_s ? st_.fastest_s : (t > st_.slowest_s ? st_.slowest_s : t);
					}
				}
			}
			const int g = find(finger_);
			target = g >= 0 ? k_[g].c.pitch : newest.c.pitch;
		}
		if (move >= 0.0f) move_to(target, move);
		else target_ = target; // (a bend: followed)
		if (mv_len_ > 0.0f && mv_t_ < mv_len_) mv_t_ += seconds;
		if (mv_len_ > 0.0f && mv_t_ < mv_len_) {
			const float u = mv_t_ / mv_len_;
			pitch_ = target_ + off_ * (1.0f - u * u * (3.0f - 2.0f * u)); // (eased in and out)
		} else {
			mv_len_ = 0.0f;
			pitch_ = target_;
		}
		if (strike_out_ > 0.0f) rate = rate > strike_out_ ? rate : strike_out_, strike_out_ = 0.0f;
		const int g = find(finger_);
		const NoteControls& who = g >= 0 ? k_[g].c : newest.c;
		v_.pitch = pitch_;
		if (n_ == 1) ref_ = pmax; // (one key: the voice's level is its own)
		const float cap = pmax > ref_ ? pmax : ref_;
		v_.pressure = P < cap ? P : cap;
		ref_ = v_.pressure < ref_ ? v_.pressure : ref_; // (lifting lowers the cap with it)
		v_.pressure_rate = rate < -1.0f ? -1.0f : (rate > 1.0f ? 1.0f : rate);
		const float ty = P > 0.0f ? y / P : newest.c.timbre;
		v_.timbre = ty < -1.0f ? -1.0f : (ty > 1.0f ? 1.0f : ty);
		v_.velocity = who.velocity;
		v_.release = who.release;
		v_.gate = 1;
		for (int i = 0; i < n_; i++) {
			k_[i].age += seconds;
			k_[i].pprev = k_[i].c.pressure > 0.0f ? k_[i].c.pressure : 0.0f;
		}
	}
	// forget every key (the mode switched, all notes off)
	void clear() {
		n_ = 0;
		sounding_ = false;
		finger_ = -1;
		mv_len_ = 0.0f;
	}

	[[nodiscard]] bool empty() const { return n_ == 0; }
	[[nodiscard]] int size() const { return n_; }
	[[nodiscard]] bool held(int id) const { return find(id) >= 0; }
	// the newest held key (-1: none)
	[[nodiscard]] int top() const { return n_ > 0 ? k_[n_ - 1].id : -1; }
	// what the one voice plays (after step(); after the last release, its release)
	[[nodiscard]] const NoteControls& voice() const { return v_; }

private:
	struct Key {
		int id;
		NoteControls c;
		float pprev;  // its pressure at the last step (the share's speed)
		float strike; // its strike since its press (the most within decide_s)
		float age;    // seconds since its press
		bool slurred; // pressed while another was held, its press not over
		bool art;     // re-articulated
	};
	// the pitch moves from where it is to target over seconds (0: at once), following target's bends
	void move_to(float target, float seconds) {
		if (seconds > 0.0f) {
			const float d = pitch_ > target ? pitch_ - target : target - pitch_;
			if (seconds < st_.per_semitone_s * d) seconds = st_.per_semitone_s * d;
		}
		mv_len_ = seconds;
		mv_t_ = 0.0f;
		off_ = seconds > 0.0f ? pitch_ - target : 0.0f;
		target_ = target;
		if (seconds <= 0.0f) pitch_ = target;
	}
	[[nodiscard]] int find(int id) const {
		for (int i = 0; i < n_; i++)
			if (k_[i].id == id) return i;
		return -1;
	}
	void remove(int at) {
		for (int i = at; i + 1 < n_; i++) k_[i] = k_[i + 1];
		n_--;
	}

	MonoStyle st_;
	Key k_[Max] = {};
	int n_ = 0;
	NoteControls v_ = note_at_rest;
	bool sounding_ = false;
	int finger_ = -1;
	float pitch_ = 69.0f, target_ = 69.0f, off_ = 0.0f, mv_t_ = 0.0f, mv_len_ = 0.0f;
	float strike_out_ = 0.0f; // (struck: the new key's strike, for one step)
	float ref_ = 0.0f;        // the level the voice had as the keys began to overlap
};

} // namespace awva::voices

#endif // AWVA_MONO_H_

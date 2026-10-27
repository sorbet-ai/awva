// SPDX-FileCopyrightText: 2026 Sorbet AI
// SPDX-License-Identifier: Apache-2.0
// A build without the parameters' text (AWVA_PARAM_TEXT 0, an embedded host's): the engines'
// tables compile with label, hover and icon NULL, in the same layout. Its own program: the engines
// here must not meet the same engines built with their text (awva_param_text_off, sdk/CMakeLists.txt).
#define AWVA_PARAM_TEXT 0
#include "awva/voice_accordion.h"
#include "awva/voice_modal.h"
#include "awva/voice_reed.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

static_assert(sizeof(awva::ParamDesc) == 2 * sizeof(const char*) + 3 * 4 + (sizeof(const char*) == 8 ? 4 : 0) + 3 * sizeof(const char*),
    "the layout is the same with the text or without it");

template <class E> static void check_no_text() {
	using P = awva::EngineParams<E>;
	REQUIRE(P::count > 0);
	for (int i = 0; i < P::count; i++) {
		const awva::ParamDesc* d = P::desc(i);
		REQUIRE(d != nullptr);
		CHECK(d->name != nullptr);
		CHECK(d->label == nullptr);
		CHECK(d->hover == nullptr);
		CHECK(d->icon == nullptr);
	}
}

TEST_CASE("AWVA_PARAM_TEXT 0: no label, hover or icon, every parameter still there") {
	check_no_text<accordion::Engine>();
	check_no_text<modal::Engine>();
	check_no_text<reed::Engine>();
}

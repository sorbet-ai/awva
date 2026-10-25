# awva

Expressive per-note sound engines, playable by MIDI 2.0, MPE or key signals; an SDK for the PC; and the engine component, a C ABI to load each engine from its own library (`component/`).

They work with the Striso board as a supported host: its firmware takes this repository as a git submodule, pinned by commit. (github.com/sorbet-ai/awva; ids under com.sorbet-ai.awva.)

| | |
|---|---|
| `dsp/` | the engines, header only: the contract (`engine.h`), the shared helpers, the UMP profile and decoder, MIDI in, the voice allocator, mono legato; `engines/<name>.h` traits, `effects/fx.h` |
| `sdk/` | one library per engine, the contract suite, the renderer (`awva_render`), the shell, MIDI files |
| `tools/harness/` | the scripted performances, `harness_util.h`, key recordings (`rec/`) |
| `third_party/doctest` | the test framework |

Build and test on their own:

```
cmake -S sdk -B build/sdk -G Ninja && cmake --build build/sdk && ctest --test-dir build/sdk
```

The render goldens, the QEMU target renders and the board-vs-shell tests run in the firmware repository, against the commit it pins. A change here shows up there as a golden diff in the pin bump.

Licence: Apache-2.0 (`LICENSE`, `NOTICE`). `PROVENANCE.md` says where each file comes from. The Striso firmware that includes this repository stays GPL-3.0; Apache-2.0 code in a GPLv3 work is compatible (the FSF lists Apache-2.0 as GPLv3-compatible), not the other way round.

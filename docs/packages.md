# Engine packages (.awvapkg)

An awva engine ships as **one file**, `<id>-<slug>.awvapkg`, that every host loads:
- the app and the plugins take the PC library for their own OS and CPU;
- a host's virtual device takes the same library, installed into one of its engine slots;
- an embedded Cortex-M7 host takes the module image, installed into a flash slot.

Each host reads its own image and the presets it uses, and skips the rest. The format and its checks are `component/include/nsc/package.h`. It is freestanding (no allocation, no I/O), so an embedded host's loader uses the same file. `component/tools/nsc_pack.py` writes packages.

## The file

All values are little-endian, and every offset counts from the start of the file.

| part | size | what |
|---|---|---|
| package header | 64 B | `NSCP`, format 1, header size 64, `NSC_ABI`, schema hash, section count, table offset 64, file size, the table's crc32. Bytes 32..59 are zero; the header's own crc32 is at 60. |
| section table | n × 48 B | per section: u32 kind, char tag[32] (NUL-padded, at most 31 characters), u32 offset, size, crc32. Offsets ascend, are 4-byte aligned and don't overlap; at most 32 sections. |
| sections | | kind 1: the manifest (always the first). Kind 2: an image, its tag the target. Kind 3: a preset file, its tag the file's name. |

**An image section** holds three things, in order:
1. A 256-byte module header: magic `NSCM`, the ABI, the target's architecture, FPU and float ABI, the toolchain, the image's size, its relocations, `nsc_entry`'s offset, the stack, the regions, the voices, the channels, the colour, the schema, the id, uid and version, and the crcs.
2. The relocation list (Arm only): the offsets of the words that hold an address, ascending.
3. The image itself.

On a PC the image is the DLL, dylib or so as it is built, with no relocations: the OS loader relocates it. On an embedded host it is the module linked at 0, which the host relocates into its slot as it writes it. The relocations come first so the host can stream the image straight into flash.

**Targets** (the image tags):
- a Cortex-M7 host (FPv5-D16, softfp): `armv7em-fpv5-d16-softfp`;
- PCs: `x86_64-windows`, `arm64-windows`, `arm64-macos`, `x86_64-macos`, `x86_64-linux`, `arm64-linux`.

macOS images are thin, one per tag.

**Presets** are awva-preset/1 files, one section each, read in tag (name) order.

## The manifest

```json
{
  "format": "awva-package/2",
  "label": "clarinet v3 (fit)",
  "awva": "543e6ed",
  "id": "clarinet",
  "uid": "com.sorbet-ai.awva.clarinet",
  "abi": 4,
  "version": "543e6ed",
  "schema_hash": "1a2b3c4d",
  "...": "the rest of nsc_manifest's fields: name, short_name, summary, the regions, params, presets",
  "images": ["armv7em-fpv5-d16-softfp", "x86_64-windows"]
}
```

- **`format`**: exactly `awva-package/2`.
- **`label`**: the name a host lists the package by.
- **`awva`**: the commit the images are built on. A host refuses a package that isn't on its own pinned commit.
- **`images`**: the targets there are images for. If it is empty, the package is **presets-only**: new presets for the built-in engine `id`.

Hosts check, before loading anything:
- the header's and the table's crcs, and format 1;
- the ABI (the module header's must be the host's);
- that there is an image for the host's own target;
- the module header's own crc and limits.

A refused package is listed with its reason and never loaded.

## Writing one

From a build of the SDK, with Python 3:

```
cmake --build build/sdk --target package_<id>      # -> build/sdk/packages/<id>-<slug>.awvapkg
cmake --build build/sdk --target packages          # every engine's
```

Each package holds the component's library as this PC's image and the engine's preset files (`presets/<id>`).
- The library is built with the MIDI table sizes every host of a package takes: a block's events, the host's note ids and a control step's messages, `AWVA_PACKAGE_MIDI` (default 256 / 16 / 256: an embedded host's, whose slots and virtual device refuse larger state). Its descriptor states them (`max_events`, `max_note_ids`).
- `AWVA_PACKAGE_LABEL` sets the label (default: the id).
- `AWVA_PACKAGE_AWVA` sets the commit stamp (default: the build's version, when that is a commit).
- `AWVA_PACKAGE_SLUG` sets the file name's last part.
- `AWVA_PACKAGE_DIR` sets where the files go.

In your own project, `awva_add_package_library(<target> <id>)` builds an engine's library with those sizes and `awva_add_package(<target> <library target> <id> <out.awvapkg> [LABEL ...])` packs it; the packager refuses a library built with other sizes.

By hand, `component/tools/nsc_pack.py`:

```
nsc_pack.py host <library> <manifest.json> <tag> "<toolchain>" <id>.nscm      # a PC image payload
nsc_pack.py package <out.awvapkg> --image <tag>=<x.nscm>... --manifest <manifest.json> \
    --label "<label>" --awva <commit> --presets presets/<id>
nsc_pack.py package <out.awvapkg> --engine <id> --label "<label>" --presets <dir>   # presets-only
nsc_pack.py show <x.awvapkg>                                                         # what it holds
```

An embedded host's image payload comes from its own module build (`nsc_pack.py module`, from the module ELF) and joins a package as one more `--image`. A package is written as `<out>.tmp` and then renamed, so a host watching the folder never sees half of one.

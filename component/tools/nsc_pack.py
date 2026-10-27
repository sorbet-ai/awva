#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Engine packages (.awvapkg: docs/packages.md; the format: component/include/nsc/package.h).

  nsc_pack.py module <module.elf> <out.nscm> [--objdump <arm-none-eabi-objdump> --stackcheck <stackcheck.py>]
      An embedded module (a Cortex-M7 host's, linked at 0 by its module.ld): its image (.text + .rodata at 0), the list
      of the words that hold an address (R_ARM_ABS32: -mword-relocations leaves no other absolute
      kind; anything else is refused), nsc_entry's offset, the deepest stack of the component's
      functions (tools/stackcheck.py over the disassembly) and the descriptor's fields, read from
      the image itself (nsc_entry returns one constant table: its literal is the one relocation
      inside nsc_entry). Refuses writable data (.data, .bss), constructors, a GOT, any undefined
      symbol (a module imports nothing) and a relocated word that points outside the image.
  nsc_pack.py host <library> <manifest.json> <tag> <toolchain> <out.nscm> [--midi <events>,<note ids>]
      A PC image (the component's DLL / dylib / so, no relocations) with its module header from the
      library's manifest (nsc_manifest's JSON: its own sizes, not an embedded build's).
  nsc_pack.py package <out.awvapkg> [--image <tag>=<x.nscm>]... [--manifest <id.json>] [--engine <id>]
                     [--label <text>] [--awva <commit>] [--presets <dir>] [--preset <stem>=<file>]...
      The engine package, the one format every host loads (docs/packages.md): header,
      section table, the manifest, an image per target (an embedded host's Cortex-M7 module, the PCs'
      libraries: each host takes its own), the preset files (awva-preset/1, in name order). The
      manifest is the engine's (nsc_manifest's JSON when given, else its images' headers) with the
      package's own fields: "format": "awva-package/2", "label", "awva" (the commit it is built
      on), "images" (the targets). With no image it is a presets-only package for --engine.
  nsc_pack.py show <x.awvapkg | x.nscm>
      What a package or an image payload holds.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

PACKAGE_MAGIC, MODULE_MAGIC, FORMAT, NSC_MAGIC = 0x5043534E, 0x4D43534E, 1, 0x3143534E
# the component ABI whose descriptor layout this reads (nsc.h nsc_desc, 32-bit pointers); a module
# built against another is refused, so an ABI change fails here first (update NSC_DESC with it)
NSC_ABI = 6
NSC_DESC = struct.Struct("<2I6I2IiIiff2i3I2i2I")  # magic, abi, id uid name short_name summary version, schema inputs,
# channels colour max_voices cost tail_seconds, min/max_rate, hot stream bulk, num_params num_presets, params presets
# (then heroes[8], max_events, max_note_ids, max_notes: not read)
KIND_MANIFEST, KIND_IMAGE, KIND_PRESET = 1, 2, 3
BOARD_TAG = "armv7em-fpv5-d16-softfp"
MAX_RELOCS, SLOT_IMAGE_MAX = 4096, 128 * 1024 - 256
IMAGE_SECTIONS = (".text", ".rodata")

# ARM relocation types that are PC-relative (resolved by the link, the same at any base) or
# no-ops; R_ARM_ABS32 is the one the loader applies; every other type is refused
R_ARM_ABS32 = 2
PC_RELATIVE = {0: "NONE", 3: "REL32", 10: "THM_CALL", 11: "THM_PC8", 25: "THM_JUMP6?", 28: "CALL", 29: "JUMP24",
               30: "THM_JUMP24", 40: "V4BX", 42: "PREL31", 51: "THM_JUMP19", 102: "THM_JUMP11", 103: "THM_JUMP8",
               105: "THM_PC12", 104: "THM_ALU_PREL_11_0", 107: "THM_ALU_ABS_G0?"}
PC_RELATIVE.pop(107)  # (an absolute group relocation: refused)
PC_RELATIVE.pop(25)


def die(msg):
    print(f"nsc_pack: {msg}", file=sys.stderr)
    sys.exit(1)


def cstr(s, n):
    b = s.encode()
    if len(b) >= n:
        die(f"'{s}' is longer than {n - 1} bytes")
    return b + b"\0" * (n - len(b))


# --- the module header (nsc_package.h ModuleHeader, 256 bytes) ----------------------------------
HDR = struct.Struct("<IHHI16s12s8s24sIIIIIIIiiII32s64s32sII")
assert HDR.size == 252


def module_header(f, payload):
    """f: dict of the fields; payload: the relocations and the image (crc'd)."""
    h = HDR.pack(MODULE_MAGIC, FORMAT, 256, f["abi"], cstr(f["arch"], 16), cstr(f["fpu"], 12), cstr(f["float_abi"], 8),
                 cstr(f["toolchain"], 24), f["image_size"], f["reloc_count"], f["entry_offset"], f["stack_bytes"],
                 f["hot_bytes"], f["stream_bytes"], f["bulk_bytes"], f["max_voices"], f["channels"], f["colour"],
                 f["schema_hash"], cstr(f["id"], 32), cstr(f["uid"], 64), cstr(f["version"], 32),
                 zlib.crc32(payload), 0)
    return h + struct.pack("<I", zlib.crc32(h))


def parse_header(b):
    if len(b) < 256:
        die("not an image payload (short)")
    v = HDR.unpack(b[:252])
    names = ["magic", "format", "header_size", "abi", "arch", "fpu", "float_abi", "toolchain", "image_size", "reloc_count",
             "entry_offset", "stack_bytes", "hot_bytes", "stream_bytes", "bulk_bytes", "max_voices", "channels", "colour",
             "schema_hash", "id", "uid", "version", "payload_crc", "reserved"]
    f = dict(zip(names, v))
    for k in ("arch", "fpu", "float_abi", "toolchain", "id", "uid", "version"):
        f[k] = f[k].split(b"\0")[0].decode()
    f["header_crc"] = struct.unpack_from("<I", b, 252)[0]
    if f["magic"] != MODULE_MAGIC or zlib.crc32(b[:252]) != f["header_crc"]:
        die("not an image payload (magic or header crc)")
    return f


# --- a minimal ELF32 reader ---------------------------------------------------------------------
class Elf:
    def __init__(self, path):
        self.b = b = Path(path).read_bytes()
        if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
            die(f"{path}: not a 32-bit little-endian ELF")
        (self.type, self.machine, _, self.entry, _, shoff, _, _, _, _, shentsize, shnum, shstrndx) = \
            struct.unpack_from("<HHIIIIIHHHHHH", b, 16)
        if self.machine != 40:
            die(f"{path}: not an ARM ELF")
        self.sh = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<10I", b, shoff + i * shentsize)
            self.sh.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info, entsize=entsize))
        strtab = self.sh[shstrndx]
        for s in self.sh:
            s["name"] = self._str(strtab, s["name"])
        self.syms = []
        for s in self.sh:
            if s["type"] == 2:  # SYMTAB
                st = self.sh[s["link"]]
                for k in range(s["size"] // 16):
                    nm, val, size, info, other, shndx = struct.unpack_from("<IIIBBH", b, s["off"] + 16 * k)
                    self.syms.append(dict(name=self._str(st, nm), value=val, size=size, type=info & 15, bind=info >> 4, shndx=shndx))

    def _str(self, sec, off):
        o = sec["off"] + off
        return self.b[o:self.b.index(b"\0", o)].decode()

    def section(self, name):
        return next((s for s in self.sh if s["name"] == name), None)

    def symbol(self, name):
        return next((s for s in self.syms if s["name"] == name), None)


def toolchain_of(elf):
    c = elf.section(".comment")
    if c:
        text = elf.b[c["off"]:c["off"] + c["size"]].decode(errors="replace")
        m = re.search(r"GCC: \([^)]*\)[^)]*\)? ?(\d+\.\d+\.\d+)", text) or re.search(r"(\d+\.\d+\.\d+)", text)
        if m:
            return f"gcc {m.group(1)}"
    return "unknown"


def stack_of(elf_path, elf, roots, objdump, stackcheck):
    """The deepest stack of the given functions (tools/stackcheck.py), 0 when not measured."""
    dis = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(elf_path)], check=True, capture_output=True, text=True).stdout
    r = subprocess.run([sys.executable, stackcheck] + roots, input=dis, capture_output=True, text=True)
    if r.returncode != 0:
        die(f"stack check failed ({r.returncode}): {r.stdout}{r.stderr}")
    depth, per = 0, {}
    for line in r.stdout.splitlines():
        parts = line.split()
        nums = [int(p) for p in parts[:2] if p.isdigit()]
        if nums:
            depth = max(depth, nums[0])
            m = re.search(r"EE(\d+)([A-Za-z_]\w*)", parts[0])
            name = m.group(2)[:int(m.group(1))] if m else parts[0]
            per[name] = max(per.get(name, 0), nums[0])
    return depth, per


def host_limits(path):
    """An embedded host's arenas and the audio stack to spare, from its limits header (kStateArena,
    kFastArena, kLargeArena, kStackSpare): {name: bytes}."""
    text = Path(path).read_text()
    out = {}
    for name in ("kStateArena", "kFastArena", "kLargeArena", "kStackSpare"):
        m = re.search(name + r"\s*=\s*([0-9]+)(?:\s*\*\s*([0-9]+))?", text)
        if not m:
            die(f"{path}: no {name}")
        out[name] = int(m.group(1)) * (int(m.group(2)) if m.group(2) else 1)
    return out


def check_limits(path, hot_b, stream_b, bulk_b, stack):
    """A module the host would refuse is refused here, at the build, in the host's words."""
    lim = host_limits(path)
    kb = lambda n: (n + 1023) // 1024
    for what, need, have in (("hot", hot_b, lim["kStateArena"]), ("stream", stream_b, lim["kFastArena"]),
                             ("bulk", bulk_b, lim["kLargeArena"])):
        if need > have:
            die(f"{what} {kb(need)} KB > {kb(have)} KB: the host's arena ({path})")
    if stack > lim["kStackSpare"]:
        die(f"stack {stack} > {lim['kStackSpare']}: the audio thread's spare ({path})")


def cmd_module(a):
    elf = Elf(a.elf)
    if elf.type != 2:
        die("not an executable (link it with module.ld)")
    # the image: every allocated section, which must be only .text and .rodata, from 0
    allowed = IMAGE_SECTIONS
    image_secs, bad = [], []
    for s in elf.sh:
        if s["flags"] & 2 and s["size"]:  # SHF_ALLOC
            if s["name"] in allowed:
                image_secs.append(s)
            else:
                bad.append(f"{s['name']} ({s['size']} bytes)")
    if bad:
        die("a module is .text and .rodata only, not " + ", ".join(bad))
    if not image_secs or image_secs[0]["addr"] != 0:
        die("the image must start at 0 (module.ld)")
    size = max(s["addr"] + s["size"] for s in image_secs)
    size = (size + 3) & ~3
    if size > SLOT_IMAGE_MAX:
        die(f"image {size} bytes > a slot's {SLOT_IMAGE_MAX}")
    image = bytearray(size)
    for s in image_secs:
        if s["type"] != 1:  # PROGBITS
            die(f"{s['name']} is not PROGBITS")
        image[s["addr"]:s["addr"] + s["size"]] = elf.b[s["off"]:s["off"] + s["size"]]
    # the relocations of the image's sections (-Wl,-q kept them; r_offset is the address)
    relocs, kinds = [], {}
    for s in elf.sh:
        if s["type"] == 4:
            die(f"{s['name']}: RELA relocations (not expected from arm-none-eabi-ld)")
        if s["type"] != 9:  # REL
            continue
        target = elf.sh[s["info"]]
        if not target["flags"] & 2:
            continue  # debug information
        if target["name"] not in allowed:
            die(f"relocations against {target['name']}")
        symtab = s["link"]
        for k in range(s["size"] // 8):
            off, info = struct.unpack_from("<II", elf.b, s["off"] + 8 * k)
            typ, symi = info & 0xFF, info >> 8
            if typ == R_ARM_ABS32:
                sym = elf.syms[symi] if symi < len(elf.syms) and symtab else None
                if sym and sym["shndx"] == 0 and symi != 0:
                    die(f"an undefined symbol: {sym['name']} (a module imports nothing)")
                if sym and sym["shndx"] == 0xFFF1:
                    continue  # an absolute value, not an address in the image
                (w,) = struct.unpack_from("<I", image, off)
                if (w & ~1) > size:
                    die(f"the word at 0x{off:x} (0x{w:x}) points outside the image")
                relocs.append(off)
            elif typ in PC_RELATIVE:
                kinds[PC_RELATIVE[typ]] = kinds.get(PC_RELATIVE[typ], 0) + 1
            else:
                die(f"relocation type {typ} at 0x{off:x}: only R_ARM_ABS32 can be applied (build with -mword-relocations)")
    relocs = sorted(set(relocs))
    if len(relocs) > MAX_RELOCS:
        die(f"{len(relocs)} relocations > {MAX_RELOCS}")
    for s in elf.syms:
        if s["shndx"] == 0 and s["name"] and s["bind"] != 0:
            die(f"an undefined symbol: {s['name']} (a module imports nothing)")
    entry = elf.symbol("nsc_entry")
    if not entry or entry["type"] != 2:
        die("no function nsc_entry")
    # the component table: the one address literal inside nsc_entry
    lo, hi = entry["value"] & ~1, (entry["value"] & ~1) + entry["size"]
    lits = [o for o in relocs if lo <= o < hi]
    if len(lits) != 1:
        die(f"nsc_entry has {len(lits)} address literals: it must return one constant component table")
    word = lambda o: struct.unpack_from("<I", image, o)[0]
    comp = word(lits[0])
    desc = word(comp)
    def string(p):
        return image[p:image.index(b"\0", p)].decode()
    if a.nsc_h:
        m = re.search(r"#define NSC_ABI (\d+)", Path(a.nsc_h).read_text())
        if not m or int(m.group(1)) != NSC_ABI:
            die(f"{a.nsc_h} is ABI {m.group(1) if m else '?'}: this packager reads ABI {NSC_ABI}'s descriptor (update NSC_DESC)")
    magic, abi = struct.unpack_from("<2I", image, desc)
    if magic != NSC_MAGIC or abi != NSC_ABI:
        die(f"the descriptor's magic/abi 0x{magic:x}/{abi}: not a component of ABI {NSC_ABI}")
    d = NSC_DESC.unpack_from(image, desc)
    (magic, abi, p_id, p_uid, p_name, p_short, p_summary, p_version, schema, inputs, channels, colour, max_voices, cost, tail,
     min_rate, max_rate, hot_b, stream_b, bulk_b, num_params, num_presets, p_params, p_presets) = d
    # the component's functions: the stack check's roots
    fns = [word(comp + 4 * i) & ~1 for i in range(1, 16)]
    by_addr = {s["value"] & ~1: s["name"] for s in elf.syms if s["type"] == 2 and s["name"]}
    roots = sorted({by_addr[f] for f in fns if f in by_addr})
    deepest, per_fn = stack_of(a.elf, elf, roots, a.objdump, a.stackcheck) if a.objdump and a.stackcheck else (0, {})
    # stack_bytes: what the host's audio thread runs (nsc.h: everything but save_preset, which runs
    # on any thread; load_preset, about 5 KB, included). The deepest of all is printed with the rest.
    audio = {k: v for k, v in per_fn.items() if k != "save_preset"}
    stack = max(audio.values()) if audio else deepest
    if a.limits:
        check_limits(a.limits, hot_b, stream_b, bulk_b, stack)
    f = dict(abi=abi, arch="armv7em", fpu="fpv5-d16", float_abi="softfp", toolchain=toolchain_of(elf), image_size=size,
             reloc_count=len(relocs), entry_offset=entry["value"], stack_bytes=stack, hot_bytes=hot_b, stream_bytes=stream_b,
             bulk_bytes=bulk_b, max_voices=max_voices, channels=channels, colour=colour, schema_hash=schema, id=string(p_id),
             uid=string(p_uid), version=string(p_version))
    payload = b"".join(struct.pack("<I", o) for o in relocs) + bytes(image)
    Path(a.out).write_bytes(module_header(f, payload) + payload)
    other = ", ".join(f"{k} {v}" for k, v in sorted(kinds.items()))
    print(f"{f['id']}: image {size} bytes ({sum(s['size'] for s in image_secs if s['name'] == '.text')} text), "
          f"{len(relocs)} relocations (PC-relative, resolved: {other or 'none'}), entry 0x{entry['value']:x}, "
          f"stack {stack} (with save_preset {deepest}), hot {hot_b} stream {stream_b} bulk {bulk_b}, {f['toolchain']}")
    if per_fn:
        print(f"{f['id']}: stack per function: " + ", ".join(f"{k} {v}" for k, v in sorted(per_fn.items(), key=lambda kv: -kv[1])))


def cmd_host(a):
    m = json.loads(Path(a.manifest).read_text())
    if a.midi:
        want = [int(x) for x in a.midi.split(",")]
        have = [m.get("max_events"), m.get("max_note_ids")]
        if have != want:
            die(f"{a.library} is built with max_events {have[0]}, max_note_ids {have[1]}: a package's libraries are built "
                f"with {want[0]}, {want[1]} (AWVA_PACKAGE_MIDI; awva_add_package_library)")
    lib = Path(a.library).read_bytes()
    f = dict(abi=m["abi"], arch=a.tag, fpu="", float_abi="", toolchain=a.toolchain[:23], image_size=len(lib), reloc_count=0,
             entry_offset=0, stack_bytes=0, hot_bytes=m["hot_bytes"], stream_bytes=m["stream_bytes"],
             bulk_bytes=m["bulk_bytes"], max_voices=m["max_voices"], channels=m["channels"], colour=int(m.get("colour", "0"), 16),
             schema_hash=int(m["schema_hash"], 16), id=m["id"], uid=m["uid"], version=m["version"])
    Path(a.out).write_bytes(module_header(f, lib) + lib)
    print(f"{f['id']}: {a.tag} image {len(lib)} bytes")


PACKAGE_FORMAT = "awva-package/2"


def cmd_package(a):
    images = []
    for spec in a.image:
        tag, _, path = spec.partition("=")
        b = Path(path).read_bytes()
        h = parse_header(b)
        want = BOARD_TAG if h["arch"] == "armv7em" else h["arch"]
        if tag != want:
            die(f"{path}: an {h['arch']} image under the tag {tag}")
        images.append((tag, b, h))
    tags = [t for t, _, _ in images]
    if len(set(tags)) != len(tags):
        die("two images for one target")
    if images:
        first = images[0][2]
        for tag, _, h in images:
            if (h["id"], h["schema_hash"], h["abi"], h["version"]) != (first["id"], first["schema_hash"], first["abi"], first["version"]):
                die(f"the {tag} image is another engine, schema, ABI or version than the {images[0][0]} image")
        if a.engine and a.engine != first["id"]:
            die(f"--engine {a.engine}, but the images are {first['id']}")
    elif not a.engine:
        die("no image: a presets-only package names its engine (--engine)")
    else:
        first = dict(id=a.engine, abi=NSC_ABI, schema_hash=0, version="")
    if a.manifest:
        m = json.loads(Path(a.manifest).read_text(encoding="utf-8"))
        if m.get("id") != first["id"]:
            die(f"{a.manifest} is {m.get('id')}, the package {first['id']}")
    elif images:
        m = {"id": first["id"], "uid": first["uid"], "abi": first["abi"], "version": first["version"],
             "schema_hash": f"{first['schema_hash']:08x}"}
    else:
        m = {"id": first["id"]}
    if a.awva and not re.fullmatch(r"[0-9a-f]{7,40}", a.awva):
        die(f"--awva {a.awva}: a commit, at least 7 hex digits")
    m = {"format": PACKAGE_FORMAT, "label": a.label or first["id"], **({"awva": a.awva} if a.awva else {}), **m,
         "images": tags}
    m["format"] = PACKAGE_FORMAT
    manifest = (json.dumps(m, indent=2) + "\n").encode()
    sections = [(KIND_MANIFEST, "manifest", manifest)] + [(KIND_IMAGE, t, b) for t, b, _ in images]
    presets = []
    if a.presets:
        for f in sorted(Path(a.presets).glob("*.preset.json")):
            presets.append((f.name[:-len(".preset.json")], f))
    for spec in a.preset or []:
        stem, _, path = spec.partition("=")
        presets.append((stem, Path(path)))
    for stem, f in sorted(presets):
        text = f.read_bytes()
        try:
            pj = json.loads(text)
        except ValueError:
            die(f"{f}: not JSON")
        # what every host's reader (awva/preset.h) refuses is refused here: no "format" (no_format),
        # another engine's (resolve refuses it)
        if not isinstance(pj, dict) or pj.get("format") != "awva-preset/1":
            die(f"{f}: not an awva-preset/1 file")
        eng = pj.get("engine")
        if not isinstance(eng, dict) or eng.get("id") != first["id"]:
            die(f"{f}: a preset of {eng.get('id') if isinstance(eng, dict) else None}, the package is {first['id']}")
        sections.append((KIND_PRESET, stem, text))
    if len(sections) > 32:
        die("more than 32 sections")
    at = 64 + 48 * len(sections)
    table, body = b"", b""
    for kind, tag, data in sections:
        pad = (-at) % 4
        body += b"\0" * pad
        at += pad
        table += struct.pack("<I32sIII", kind, cstr(tag, 32), at, len(data), zlib.crc32(data))
        body += data
        at += len(data)
    head = struct.pack("<IHHIIIIII", PACKAGE_MAGIC, FORMAT, 64, first["abi"], first["schema_hash"], len(sections), 64, at,
                       zlib.crc32(table)) + b"\0" * 28
    # written as <out>.tmp, then renamed: a host watching the folder never sees half a package
    tmp = Path(str(a.out) + ".tmp")
    tmp.write_bytes(head + struct.pack("<I", zlib.crc32(head)) + table + body)
    os.replace(tmp, a.out)
    print(f"{a.out}: {first['id']} {first['version']}, {len(images)} images ({', '.join(tags) or 'presets only'}), "
          f"{len(presets)} presets, {at} bytes")


def cmd_show(a):
    b = Path(a.file).read_bytes()
    if struct.unpack_from("<I", b)[0] == MODULE_MAGIC:
        print(json.dumps(parse_header(b), indent=2))
        return
    magic, fmt, hs, abi, schema, n, toff, size, tcrc = struct.unpack_from("<IHHIIIIII", b)
    if magic != PACKAGE_MAGIC:
        die("neither a package nor an image payload")
    print(f"package format {fmt}, abi {abi}, schema {schema:08x}, {n} sections, {size} bytes")
    for i in range(n):
        kind, tag, off, sz, crc = struct.unpack_from("<I32sIII", b, toff + 48 * i)
        tag = tag.split(b"\0")[0].decode()
        print(f"  {['?', 'manifest', 'image', 'preset'][kind] if kind < 4 else kind} {tag} @{off} {sz} bytes")
        if kind == KIND_IMAGE:
            h = parse_header(b[off:off + sz])
            print("    " + ", ".join(f"{k} {h[k]}" for k in ("id", "version", "arch", "toolchain", "image_size", "reloc_count",
                                                            "stack_bytes", "hot_bytes", "stream_bytes", "bulk_bytes")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("module")
    p.add_argument("elf")
    p.add_argument("out")
    p.add_argument("--objdump")
    p.add_argument("--stackcheck")
    p.add_argument("--nsc-h", help="awva's nsc.h: its NSC_ABI must be the one this packager reads")
    p.add_argument("--limits", help="the host's limits header: refuse a module over its arenas or stack")
    p = sub.add_parser("host")
    for k in ("library", "manifest", "tag", "toolchain", "out"):
        p.add_argument(k)
    p.add_argument("--midi", help="<max_events>,<max_note_ids>: refuse a library built with other MIDI tables")
    p = sub.add_parser("package")
    p.add_argument("out")
    p.add_argument("--image", action="append", default=[])
    p.add_argument("--manifest")
    p.add_argument("--engine", help="the engine's id (needed for a presets-only package)")
    p.add_argument("--label", help="the name hosts list the package by (default: the engine's id)")
    p.add_argument("--awva", help="the awva commit the images are built on")
    p.add_argument("--presets", help="a folder of <stem>.preset.json files")
    p.add_argument("--preset", action="append")
    p = sub.add_parser("show")
    p.add_argument("file")
    a = ap.parse_args()
    {"module": cmd_module, "host": cmd_host, "package": cmd_package, "show": cmd_show}[a.cmd](a)


if __name__ == "__main__":
    main()

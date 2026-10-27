#!/usr/bin/env python3
"""oRings plugin smoke test: dlopen the built dsp.so through Schwung's plugin
API v2 the way the chain host does, play it, and check what it serves.
Writes the contract (ui_hierarchy, chain_params, values) to the JSON path given,
so tests/test_grid.mjs runs Schwung's page engine against the real answers.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import array
import ctypes as c
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
lib = c.CDLL(sys.argv[1])
failures = 0


def check(cond, msg):
    global failures
    print(("ok   " if cond else "FAIL ") + msg)
    failures += 0 if cond else 1


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("rest", c.c_void_p * 32)]


CREATE = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)
DESTROY = c.CFUNCTYPE(None, c.c_void_p)
MIDI = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_int)
SET = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)
GET = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)
ERR = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_int)
RENDER = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_int16), c.c_int)


class API(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("create", CREATE), ("destroy", DESTROY),
                ("midi", MIDI), ("set", SET), ("get", GET), ("error", ERR), ("render", RENDER)]


lib.move_plugin_init_v2.restype = c.POINTER(API)
check(not lib.move_plugin_init_v2(c.byref(Host(1, 48000, 128))), "refuses a host that is not 44.1 kHz")
api = lib.move_plugin_init_v2(c.byref(Host(1, 44100, 128))).contents
check(api.api_version == 2, "api_version 2")


def get(inst, key, size=16384):
    buf = c.create_string_buffer(size)
    n = api.get(inst, key.encode(), buf, size)
    return None if n < 0 else buf.value.decode()


def set_(inst, key, value):
    api.set(inst, key.encode(), str(value).encode())


def midi(inst, *msg):
    arr = (c.c_uint8 * len(msg))(*msg)
    api.midi(inst, arr, len(msg), 0)


def render(inst, blocks):
    out = (c.c_int16 * 256)()
    data = array.array("h")
    for _ in range(blocks):
        api.render(inst, out, 128)
        data.extend(out)
    return data


inst = api.create(b"", None)
check(bool(inst), "create_instance")

# The contract.
chain_params = json.loads(get(inst, "chain_params"))
hierarchy = json.loads(get(inst, "ui_hierarchy"))
manifest = json.loads((ROOT / "module.json").read_text())
check(chain_params == manifest["capabilities"]["chain_params"], "chain_params served == module.json")
check(hierarchy == manifest["capabilities"]["ui_hierarchy"], "ui_hierarchy served == module.json")
keys = {p["key"] for p in chain_params}
for level in hierarchy["levels"].values():
    check(all(k in keys for k in level["knobs"]), f"level {level['label']}: every knob in chain_params")
check(all(get(inst, k) not in (None, "") for k in keys), "every chain_params key answers get_param")

# Play: silence first, then a note sounds in stereo.
quiet = render(inst, 20)
check(max(abs(x) for x in quiet) == 0, "silent before any note")
midi(inst, 0x90, 60, 110)
loud = render(inst, 200)
left, right = loud[0::2], loud[1::2]
rms = lambda xs: math.sqrt(sum(x * x for x in xs) / len(xs))
check(rms(left) + rms(right) > 100, f"a note sounds (rms L {rms(left):.0f} R {rms(right):.0f})")
check(max(abs(x) for x in loud) < 32767, "no clipping at the defaults")

# Every model through the plugin boundary, by index and by name.
for i, name in enumerate(next(p for p in chain_params if p["key"] == "model")["options"]):
    set_(inst, "model", name if i % 2 else i)
    check(get(inst, "model") == str(i), f"model {name}: set by {'name' if i % 2 else 'index'}")
    render(inst, 10)
    midi(inst, 0x90, 48, 120)
    a = render(inst, 120)
    check(rms(a) > 30, f"model {name}: sounds (rms {rms(a):.0f})")

# State round trip through the plugin, and the trigger.
set_(inst, "model", 4)
set_(inst, "ratio", "0.6")
set_(inst, "gain", "-6")
state = get(inst, "state")
other = api.create(b"", None)
set_(other, "state", state)
check(get(other, "state") == state, "state restores in a second instance")
check(get(other, "ratio") == "0.6000" and get(other, "structure") == "0.6000", "an alias and its stored key agree")
set_(other, "reset", "1")
render(other, 20)
check(get(other, "reset") == "0", "reset reads back as '-' (index 0)")
check(get(inst, "no_such_key") is None, "unknown key -> -1")
api.destroy(other)
api.destroy(inst)

# Instance pool: six at once, a seventh refused.
insts = [api.create(b"", None) for _ in range(7)]
check(all(insts[:6]) and not insts[6], "six instances, the seventh refused")
for i in insts[:6]:
    api.destroy(i)

contract = {"ui_hierarchy": json.dumps(hierarchy), "chain_params": json.dumps(chain_params)}
inst = api.create(b"", None)
contract["values"] = {k: get(inst, k) for k in keys}
api.destroy(inst)
pathlib.Path(sys.argv[2]).write_text(json.dumps(contract, indent=1))
print("contract dumped:", sys.argv[2])
if failures:
    print(f"{failures} FAILED")
    sys.exit(1)
print("SMOKE PASSED")

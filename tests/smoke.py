#!/usr/bin/env python3
"""oRings plugin smoke test: dlopen the built dsp.so through Schwung's plugin
API v2 the way the chain host does, play it, and check what it serves.
Writes the contract (grid_hierarchy, chain_params, values) to the JSON path
given, so tests/test_ui.mjs runs Schwung's page engine against the real answers.

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
# Kept alive: the plugin holds on to the host (as the chain host's own does).
host = Host(1, 44100, 128)
api = lib.move_plugin_init_v2(c.byref(host)).contents
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
hierarchy = json.loads(get(inst, "grid_hierarchy"))
manifest = json.loads((ROOT / "module.json").read_text())
check(chain_params == manifest["capabilities"]["chain_params"], "chain_params served == module.json")
check(get(inst, "ui_hierarchy") == "", "ui_hierarchy served EMPTY (the grid is ui_chain.js's)")
check("ui_hierarchy" not in manifest["capabilities"] and manifest.get("ui_chain") == "ui_chain.js",
      "module.json: no stock hierarchy, ui_chain.js")
check([lv["label"] for lv in hierarchy["levels"].values()] == ["oRings", "Rings", "Scale"], "three pages")
keys = {p["key"] for p in chain_params}
for level in hierarchy["levels"].values():
    check(all(k in keys for k in level["knobs"]), f"level {level['label']}: every knob in chain_params")
check(all(get(inst, k) not in (None, "") for k in keys), "every chain_params key answers get_param")

# Play: silence first, then a note sounds in stereo.
quiet = render(inst, 20)
check(set(quiet) == {6}, "silent before any note (the 6 LSB keep-alive only)")
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

# The performance layer through the plugin boundary, clocked by MIDI clock
# (this host has no transport position): strikes on every 1/16.
seq = api.create(b"", None)
check(get(seq, "gate_status") == "1" * 16, "all 16 trigger pads on")
check(get(seq, "motion_status") == "0,-1,0,8,0", "motion status at rest: " + str(get(seq, "motion_status")))
names = get(seq, "pitch_names").split(",")
check(len(names) == 36 and names[0] == "C1" and names[14] == "C3" and names[-1] == "C6", "PITCH names (C Phrygian)")
cp = json.loads(get(seq, "chain_params"))
check(next(p for p in cp if p["key"] == "pitch")["max"] == 35, "PITCH range = the scale's notes")
set_(seq, "scale", "Chromatic")
cp = json.loads(get(seq, "chain_params"))
check(next(p for p in cp if p["key"] == "pitch")["max"] == 60 and len(get(seq, "pitch_names").split(",")) == 61,
      "chromatic: 61 notes")
set_(seq, "scale", 0)
render(seq, 20)
midi(seq, 0xFA)
hits = []
for t in range(48):            # one bar at 120 BPM: 24 clocks a beat, 7 blocks a clock
    midi(seq, 0xF8)
    hits.append(rms(render(seq, 7)))
check(min(hits[6:]) > 30, f"struck while the clock runs (min rms {min(hits[6:]):.0f})")
st = get(seq, "motion_status").split(",")
check(st[1] != "-1", "the loop step advances: " + ",".join(st))
set_(seq, "randomize", 1)
check(int(get(seq, "motion_status").split(",")[0]) & (1 << 4), "RANDOM writes the PITCH lane")
set_(seq, "gate_toggle", 3)
check(get(seq, "gate_status") == "1110111111111111", "gate 4 toggled")
set_(seq, "write", 1)
set_(seq, "touch", "ratio,1")    # a label key touches the stored parameter
check(get(seq, "write") == "1", "REC armed")
set_(seq, "touch", "ratio,0")
set_(seq, "motion_clear", "all")
check(get(seq, "motion_status").split(",")[0] == "0", "motion cleared")
check(get(seq, "eff_structure") == get(seq, "structure"), "eff_<key> without automation = the knob")
midi(seq, 0xFC)
api.destroy(seq)
api.destroy(other)
api.destroy(inst)

# Instance pool: six at once, a seventh refused.
insts = [api.create(b"", None) for _ in range(7)]
check(all(insts[:6]) and not insts[6], "six instances, the seventh refused")
for i in insts[:6]:
    api.destroy(i)

contract = {"grid_hierarchy": json.dumps(hierarchy), "chain_params": json.dumps(chain_params)}
inst = api.create(b"", None)
contract["values"] = {k: get(inst, k) for k in keys}
api.destroy(inst)
pathlib.Path(sys.argv[2]).write_text(json.dumps(contract, indent=1))
print("contract dumped:", sys.argv[2])
if failures:
    print(f"{failures} FAILED")
    sys.exit(1)
print("SMOKE PASSED")

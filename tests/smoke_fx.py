#!/usr/bin/env python3
"""oRings FX plugin smoke test: dlopen the built orings-fx.so through
Schwung's audio FX API v2 the way the chain host does, run audio through it,
and check what it serves. Writes the contract (ui_hierarchy, chain_params,
values) to the JSON path given, so tests/test_grid_fx.mjs runs Schwung's page
engine against the real answers.

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
PROCESS = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_int16), c.c_int)
SET = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)
GET = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)
MIDI = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_int)


class API(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("create", CREATE), ("destroy", DESTROY),
                ("process", PROCESS), ("set", SET), ("get", GET), ("midi", MIDI)]


lib.move_audio_fx_init_v2.restype = c.POINTER(API)
check(not lib.move_audio_fx_init_v2(c.byref(Host(1, 48000, 128))), "refuses a host that is not 44.1 kHz")
api = lib.move_audio_fx_init_v2(c.byref(Host(1, 44100, 128))).contents
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


def drums(blocks):
    """A kick every 500 ms and a hat every 125 ms, int16 stereo."""
    out = array.array("h")
    for n in range(blocks * 128):
        t = n / 44100.0
        tb, t8 = t % 0.5, t % 0.125
        s = 0.7 * math.sin(2 * math.pi * 55 * tb) * math.exp(-tb * 10)
        s += 0.2 * math.sin(2 * math.pi * 7919 * t8) * math.exp(-t8 * 150)
        v = int(s * 20000)
        out.extend((v, v))
    return out


def process(inst, data):
    """Runs `data` through the effect in 128-frame blocks; returns the output."""
    out = array.array("h", data)
    buf = (c.c_int16 * 256)()
    for f in range(0, len(out) // 2, 128):
        for i in range(256):
            buf[i] = out[2 * f + i]
        api.process(inst, buf, 128)
        for i in range(256):
            out[2 * f + i] = buf[i]
    return out


rms = lambda xs: math.sqrt(sum(x * x for x in xs) / max(1, len(xs)))

inst = api.create(b"", None)
check(bool(inst), "create_instance")

# The contract.
chain_params = json.loads(get(inst, "chain_params"))
hierarchy = json.loads(get(inst, "ui_hierarchy"))
manifest = json.loads((ROOT / "fx" / "module.json").read_text())
check(manifest["component_type"] == "audio_fx" and manifest["dsp"] == "orings-fx.so",
      "module.json: audio_fx, dsp orings-fx.so")
check(chain_params == manifest["capabilities"]["chain_params"], "chain_params served == module.json")
check(hierarchy == manifest["capabilities"]["ui_hierarchy"], "ui_hierarchy served == module.json")
keys = {p["key"] for p in chain_params}
for level in hierarchy["levels"].values():
    check(all(k in keys for k in level["knobs"]), f"level {level['label']}: every knob in chain_params")
check(all(get(inst, k) not in (None, "") for k in keys), "every chain_params key answers get_param")
check(hierarchy["levels"]["root"]["knobs"][0] == "mix", "knob 1 is MIX")
check(get(inst, "name") == "oRings FX", "name")

# Audio through the plugin boundary.
src = drums(400)
set_(inst, "mix", "0")
process(inst, array.array("h", [0] * 2 * 128 * 40))  # MIX glides to 0 on silence
dry = process(inst, src)
from_fx = dry[2 * 72:]
check(from_fx == src[:len(from_fx)], "MIX 0: the input, bit for bit, 72 samples later")
set_(inst, "mix", "1")
process(inst, drums(40))  # let MIX glide
wet = process(inst, src)
check(rms(wet[0::2]) > 300, f"MIX 100: drums excite the resonator (rms {rms(wet[0::2]):.0f})")
check(max(abs(x) for x in wet) < 32767, "no clipping at the defaults")
silence = process(inst, array.array("h", [0] * 2 * 128 * 1400))
check(max(abs(x) for x in silence[-2 * 22050:]) == 0, "silence in: exact silence out once the ring has died (4 s)")

# Every model, by index and by name.
for i, name in enumerate(next(p for p in chain_params if p["key"] == "model")["options"]):
    set_(inst, "model", name if i % 2 else i)
    check(get(inst, "model") == str(i), f"model {name}: set by {'name' if i % 2 else 'index'}")
    a = process(inst, drums(150))
    check(rms(a) > 100, f"model {name}: drums in, sound out (rms {rms(a):.0f})")

# STRUM MIDI through on_midi.
set_(inst, "model", 0)
set_(inst, "strum", "MIDI")
check(get(inst, "strum") == "1", "strum MIDI by name")
midi(inst, 0x90, 60, 100)

# State round trip.
set_(inst, "model", 4)
set_(inst, "ratio", "0.6")
set_(inst, "frequency", "D#3")
set_(inst, "width", "0.3")
state = get(inst, "state")
other = api.create(b"", None)
set_(other, "state", state)
check(get(other, "state") == state, "state restores in a second instance")
check(get(other, "ratio") == "0.6000" and get(other, "structure") == "0.6000", "an alias and its stored key agree")
check(get(other, "frequency") == str(51 - 12), "frequency D#3 (MIDI 51) restored as its index")
check(get(inst, "no_such_key") is None, "unknown key -> -1")
api.destroy(other)
api.destroy(inst)

# Instance pool: eight at once, a ninth refused.
insts = [api.create(b"", None) for _ in range(9)]
check(all(insts[:8]) and not insts[8], "eight instances, the ninth refused")
# Several at once, as in a chain: independent.
set_(insts[0], "model", 0)
set_(insts[1], "model", 8)
a0 = process(insts[0], drums(50))
a1 = process(insts[1], drums(50))
check(a0 != a1, "two instances are independent")
for i in insts[:8]:
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

#!/usr/bin/env python3
"""The one place oRings FX's parameters are defined.

  scripts/fx_metadata.py <generated dir>   writes orings_fx_meta.h (the C++
                                           table, chain_params and
                                           ui_hierarchy as strings) and
                                           rewrites fx/module.json's
                                           capabilities from the same lists.

The MODELs, the chord names and the MODEL-dependent labels of the four macro
knobs are oRings' own (scripts/metadata.py), so the two modules can never
disagree about what a MODEL is or what its knobs mean. Three kinds of entry:

  * STORED parameters (PARAMS): what the module keeps and saves.
  * LABELS (metadata.ALIASES): a second key for a macro knob whose name on
    the screen is what that knob means in the selected MODEL.
  * PLACEHOLDERS (UNUSED): a read-only "--" cell standing in for a stored
    parameter while it has no effect (SENS unless STRUM is AUTO, EXCITE in
    the Disastrous Peace models, WIDTH unless ROUTING is STEREO). Hiding the
    cell instead would slide every knob after it one place to the left.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import metadata as rings  # noqa: E402  (MODELS, CHORDS, ALIASES, M)

ROOT = pathlib.Path(__file__).resolve().parent.parent
MODELS, CHORDS, M = rings.MODELS, rings.CHORDS, rings.M

# oRings FX keeps the chord on knob 2 (STRUCTURE): the labels are the sound
# generator's for knobs 3-5, its own for knob 2. Last field: as_chord.
ALIASES = [
    ("structure", "STRUCTURE", None, None, {"lt": M["FM"]}, False),
    ("ratio", "STRUCTURE", "FM Ratio", "RATIO", {"equals": M["FM"]}, False),
    ("chord", "STRUCTURE", "Chord", "CHORD", {"gt": M["FM"]}, True),
] + [(*a, False) for a in rings.ALIASES if a[1] not in ("STRUCTURE", "CUTOFF")]


def alias_group(stored_key):
    sid = next(p[0] for p in PARAMS if p[1] == stored_key)
    return [a for a in ALIASES if a[1] == sid]
FIRST_DP = M["DP-FM"]

# FREQUENCY: the resonator's own pitch, a MIDI note named C0 (12) .. C7 (96).
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FREQ_LOW = 12
FREQ_NOTES = [NOTE_NAMES[n % 12] + str(n // 12 - 1) for n in range(FREQ_LOW, 97)]
STRUM_MODES = ["AUTO", "MIDI", "OFF"]
POLY = ["1", "2", "4"]
ROUTING = ["STEREO", "ODD", "EVEN"]

PCT = {"unit": "%", "display_format": "%.0f"}
# id, key, name, short_name, type, min, max, step, default, extra
PARAMS = [
    ("MIX", "mix", "Mix", "MIX", "float", 0, 1, 0.01, 0.5, PCT),
    ("STRUCTURE", "structure", "Structure", "STRUCT", "float", 0, 1, 0.01, 0.25, {}),
    ("BRIGHTNESS", "brightness", "Brightness", "BRIGHT", "float", 0, 1, 0.01, 0.5, {}),
    ("DAMPING", "damping", "Damping", "DAMP", "float", 0, 1, 0.01, 0.5, {}),
    ("POSITION", "position", "Position", "POS", "float", 0, 1, 0.01, 0.25, {}),
    ("FREQ", "frequency", "Frequency", "FREQ", "enum", 0, len(FREQ_NOTES) - 1, 1,
     FREQ_NOTES.index("C2"), {"options": FREQ_NOTES}),
    ("MODEL", "model", "Model", "MODEL", "enum", 0, len(MODELS) - 1, 1, 0,
     {"options": [m[0] for m in MODELS]}),
    ("LEVEL", "level", "Output Level", "LEVEL", "int", -24, 12, 1, 0, {"unit": "dB"}),
    ("IN_GAIN", "input_gain", "Input Gain", "IN GN", "int", -24, 24, 1, 0, {"unit": "dB"}),
    ("SENS", "sensitivity", "Strum Sensitivity", "SENS", "float", 0, 1, 0.01, 0.5, PCT),
    ("STRUM", "strum", "Strum Mode", "STRUM", "enum", 0, 2, 1, 0, {"options": STRUM_MODES}),
    ("EXCITER", "exciter", "Internal Exciter", "EXCITE", "enum", 0, 1, 1, 0, {"options": ["OFF", "ON"]}),
    ("POLY", "polyphony", "Polyphony", "POLY", "enum", 0, 2, 1, 0, {"options": POLY}),
    ("ROUTING", "routing", "Output Routing", "ROUTE", "enum", 0, 2, 1, 0, {"options": ROUTING}),
    ("FINE", "fine", "Fine Tune", "FINE", "int", -100, 100, 1, 0, {"unit": "ct"}),
    ("WIDTH", "width", "Stereo Width", "WIDTH", "float", 0, 1, 0.01, 1.0, PCT),
]
P = {p[0]: i for i, p in enumerate(PARAMS)}

# key, stored id it stands for, short_name, condition (shown while true)
UNUSED = [
    ("sensitivity_na", "SENS", {"param": "strum", "gt": 0}),
    ("exciter_na", "EXCITER", {"param": "model", "gt": FIRST_DP - 1}),
    ("width_na", "WIDTH", {"param": "routing", "gt": 0}),
]
USED_WHEN = {
    "sensitivity": {"param": "strum", "equals": 0},
    "exciter": {"param": "model", "lt": FIRST_DP},
    "width": {"param": "routing", "equals": 0},
}

PAGE1 = ["mix", "*structure", "*brightness", "*damping", "*position", "frequency", "model", "level"]
PAGE2 = ["input_gain", "?sensitivity", "strum", "?exciter", "polyphony", "routing", "fine", "?width"]


def check():
    """Every MODEL shows one label per macro knob; every gate shows one cell."""
    rings.check_aliases()
    def shown(cond, v):
        if "lt" in cond: return v < cond["lt"]
        if "gt" in cond: return v > cond["gt"]
        return v == cond["equals"]
    ranges = {"strum": len(STRUM_MODES), "model": len(MODELS), "routing": len(ROUTING)}
    for key, sid, cond in UNUSED:
        stored = PARAMS[P[sid]][1]
        used = USED_WHEN[stored]
        assert used["param"] == cond["param"]
        for v in range(ranges[cond["param"]]):
            assert shown(used, v) + shown(cond, v) == 1, (key, v)


def stored_key(sid):
    return PARAMS[P[sid]][1]


def chain_params():
    out = []
    for pid, key, name, short, typ, lo, hi, step, default, extra in PARAMS:
        e = {"key": key, "name": name, "short_name": short, "type": typ}
        if typ == "enum":
            e["options"] = extra["options"]
            e["default"] = extra["options"][default]
        else:
            e.update({"min": lo, "max": hi, "step": step, "default": default})
            for k in ("unit", "display_format"):
                if k in extra:
                    e[k] = extra[k]
        out.append(e)
        for akey, sid, aname, ashort, cond, as_chord in ALIASES:
            if sid != pid or akey == key:
                continue
            a = {"key": akey, "name": aname, "short_name": ashort}
            if as_chord:
                a.update({"type": "enum", "options": CHORDS, "default": CHORDS[0]})
            else:
                a.update({"type": typ, "min": lo, "max": hi, "step": step, "default": default})
            out.append(a)
        for nkey, sid, cond in UNUSED:
            if sid == pid:
                out.append({"key": nkey, "name": name + " (not used)", "short_name": short,
                            "type": "enum", "options": ["--"], "default": "--", "access": "read"})
    return out


def ui_hierarchy():
    def expand(page):
        keys, params = [], []
        for k in page:
            if k.startswith("*"):
                for a in alias_group(k[1:]):
                    keys.append(a[0])
                    params.append({"key": a[0], "visible_if": dict(param="model", **a[4])})
            elif k.startswith("?"):
                k = k[1:]
                keys.append(k)
                params.append({"key": k, "visible_if": USED_WHEN[k]})
                for nkey, sid, cond in UNUSED:
                    if stored_key(sid) == k:
                        keys.append(nkey)
                        params.append({"key": nkey, "visible_if": cond})
            else:
                keys.append(k)
                params.append(k)
        return keys, params
    k1, p1 = expand(PAGE1)
    k2, p2 = expand(PAGE2)
    return {
        "modes": None,
        "levels": {
            "root": {"label": "oRings FX", "knobs": k1,
                     "params": p1 + [{"level": "advanced", "label": "Advanced"}]},
            "advanced": {"label": "Advanced", "knobs": k2, "params": p2},
        },
    }


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    check()
    gen = pathlib.Path(sys.argv[1])
    gen.mkdir(parents=True, exist_ok=True)
    cp = json.dumps(chain_params(), separators=(",", ":"))
    uh = json.dumps(ui_hierarchy(), separators=(",", ":"))
    aliases = [a for a in ALIASES if a[0] != stored_key(a[1])]
    L = ["/* Generated by scripts/fx_metadata.py. Do not edit. */", "#pragma once", "",
         "namespace oringsfx {", "", "enum ParamId {"]
    L += [f"  P_{p[0]}," for p in PARAMS] + ["  P_COUNT", "};", ""]
    L += ["enum ParamType { PT_INT, PT_FLOAT, PT_ENUM };", "",
          "struct ParamDef { const char *key; ParamType type; float min, max, def; };",
          "static const ParamDef kParams[P_COUNT] = {"]
    for pid, key, name, short, typ, lo, hi, step, default, extra in PARAMS:
        t = {"int": "PT_INT", "float": "PT_FLOAT", "enum": "PT_ENUM"}[typ]
        L.append(f'  {{"{key}", {t}, {float(lo)!r}f, {float(hi)!r}f, {float(default)!r}f}},')
    L += ["};", "",
          "/* A second key for a macro knob (its name in some MODELs). */",
          "struct AliasDef { const char *key; int param; bool chord; };",
          f"static const int kNumAliases = {len(aliases)};",
          "static const AliasDef kAliases[] = {"]
    L += [f'  {{"{a[0]}", P_{a[1]}, {"true" if a[5] else "false"}}},' for a in aliases]
    L += ["};", "",
          "/* Read-only \"--\" cells standing in for a parameter with no effect. */",
          f"static const int kNumUnused = {len(UNUSED)};",
          "static const char *const kUnusedKeys[] = {" + ", ".join(c_string(u[0]) for u in UNUSED) + "};",
          "",
          f"static const int kNumModels = {len(MODELS)};",
          f"static const int kNumChords = {len(CHORDS)};",
          f"static const int kFreqLowNote = {FREQ_LOW};",
          f"static const int kNumFreqNotes = {len(FREQ_NOTES)};",
          "static const char *const kModelNames[] = {" + ", ".join(c_string(m[0]) for m in MODELS) + "};",
          "static const char *const kChordNames[] = {" + ", ".join(c_string(c) for c in CHORDS) + "};",
          "static const char *const kFreqNames[] = {" + ", ".join(c_string(n) for n in FREQ_NOTES) + "};",
          "static const char *const kStrumNames[] = {" + ", ".join(c_string(n) for n in STRUM_MODES) + "};",
          "static const char *const kExciterNames[] = {\"OFF\", \"ON\"};",
          "static const char *const kPolyNames[] = {" + ", ".join(c_string(n) for n in POLY) + "};",
          "static const int kPolyVoices[] = {" + ", ".join(POLY) + "};",
          "static const char *const kRoutingNames[] = {" + ", ".join(c_string(n) for n in ROUTING) + "};",
          "",
          "static const char kChainParams[] = " + c_string(cp) + ";",
          "static const char kUiHierarchy[] = " + c_string(uh) + ";",
          "", "}  // namespace oringsfx", ""]
    (gen / "orings_fx_meta.h").write_text("\n".join(L))

    mpath = ROOT / "fx" / "module.json"
    manifest = json.loads(mpath.read_text())
    manifest["capabilities"]["chain_params"] = chain_params()
    manifest["capabilities"]["ui_hierarchy"] = ui_hierarchy()
    mpath.write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n")


if __name__ == "__main__":
    main()

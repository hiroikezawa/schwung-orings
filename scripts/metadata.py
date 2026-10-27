#!/usr/bin/env python3
"""The one place oRings' parameters are defined.

  scripts/metadata.py <generated dir>   writes orings_meta.h (the C++ table,
                                        chain_params and ui_hierarchy as
                                        strings) and rewrites module.json's
                                        capabilities from the same lists.

Two kinds of entry:
  * STORED parameters (PARAMS): what the module keeps and saves.
  * LABELS (ALIASES): a second key for a stored parameter whose name on the
    screen is what that knob means in the selected MODEL. Rings' official
    modes give its four macro knobs different jobs (2-op FM: STRUCTURE is the
    frequency ratio, POSITION the feedback; Disastrous Peace: STRUCTURE picks
    the chord, DAMPING shapes the envelope, POSITION drives the effect...).
    Each label is a param entry in ui_hierarchy with a `visible_if` on
    `model`, so exactly one label per knob is visible for any MODEL and the
    stock knob grid always shows eight cells. Labels are never stored.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# MODEL, in the order the knob steps through it. Rings model / easter-egg FX
# index for each is in src/wrapper/orings.cpp (kModels).
MODELS = [
    ("MODAL", "Modal Resonator"),
    ("SYMPA", "Sympathetic Strings"),
    ("STRNG", "Modulated/Inharmonic String"),
    ("KVERB", "Karplusverb"),
    ("FM", "2-op FM"),
    ("CHORD", "Western Chords"),
    ("DP-FM", "Disastrous Peace: Formant"),
    ("DP-CH", "Disastrous Peace: Chorus"),
    ("DP-RV", "Disastrous Peace: Reverb"),
    ("DP-F2", "Disastrous Peace: Formant 2"),
    ("DP-EN", "Disastrous Peace: Ensemble"),
    ("DP-R2", "Disastrous Peace: Reverb 2"),
]
M = {short: i for i, (short, _) in enumerate(MODELS)}

# rings/dsp/part.cc + string_synth_part.cc chord tables, BRYAN_CHORDS
# (rings/dsp/dsp.h defines it), named as Bryan Noll's comments name them.
CHORDS = ["OCT", "5", "sus4", "m", "m7", "m9", "m11", "69", "M9", "M7", "M"]

# id, key, name, short_name, type, min, max, step, default, extra
PARAMS = [
    ("FREQ", "frequency", "Frequency", "FREQ", "int", -24, 24, 1, 0, {"unit": "st"}),
    ("STRUCTURE", "structure", "Structure", "STRUCT", "float", 0, 1, 0.01, 0.25, {}),
    ("BRIGHTNESS", "brightness", "Brightness", "BRIGHT", "float", 0, 1, 0.01, 0.5, {}),
    ("DAMPING", "damping", "Damping", "DAMP", "float", 0, 1, 0.01, 0.5, {}),
    ("POSITION", "position", "Position", "POS", "float", 0, 1, 0.01, 0.25, {}),
    ("MODEL", "model", "Model", "MODEL", "enum", 0, len(MODELS) - 1, 1, 0,
     {"options": [m[0] for m in MODELS]}),
    ("POLY", "polyphony", "Polyphony", "POLY", "enum", 0, 3, 1, 1,
     {"options": ["1", "2", "3", "4"]}),
    ("LEVEL", "level", "Level", "LEVEL", "float", 0, 1, 0.01, 0.7, {}),
    ("FINE", "fine", "Fine Tune", "FINE", "int", -100, 100, 1, 0, {"unit": "ct"}),
    ("EXCITER", "exciter", "Exciter Level", "EXCITE", "int", 0, 200, 1, 100, {"unit": "%"}),
    ("SPREAD", "spread", "Strum Spread", "SPREAD", "int", 10, 200, 1, 10, {"unit": "ms"}),
    ("VELO", "velocity", "Velocity", "VELO", "int", 0, 100, 1, 50, {"unit": "%"}),
    ("BEND", "bend_range", "Bend Range", "BEND", "int", 0, 24, 1, 2, {"unit": "st"}),
    ("OUTPUT", "output", "Output", "OUTPUT", "enum", 0, 3, 1, 0,
     {"options": ["STEREO", "SUM", "ODD", "EVEN"]}),
    ("GAIN", "gain", "Gain", "GAIN", "int", -24, 12, 1, 0, {"unit": "dB"}),
    ("RESET", "reset", "Reset", "RESET", "enum", 0, 1, 1, 0,
     {"options": ["-", "RESET"], "access": "write"}),
]
P = {p[0]: i for i, p in enumerate(PARAMS)}

# key, stored id, name, short_name, condition on model, as_chord
ALIASES = [
    # Knob 2
    ("structure", "STRUCTURE", None, None, {"lt": M["FM"]}, False),
    ("ratio", "STRUCTURE", "FM Ratio", "RATIO", {"equals": M["FM"]}, False),
    ("chord", "STRUCTURE", "Chord", "CHORD", {"gt": M["FM"]}, True),
    # Knob 3
    ("brightness", "BRIGHTNESS", None, None, {"lt": M["FM"]}, False),
    ("fm_index", "BRIGHTNESS", "FM Index", "INDEX", {"equals": M["FM"]}, False),
    ("brightness_c", "BRIGHTNESS", "Brightness", "BRIGHT", {"equals": M["CHORD"]}, False),
    ("registration", "BRIGHTNESS", "Registration", "REGIST", {"gt": M["CHORD"]}, False),
    # Knob 4
    ("damping", "DAMPING", None, None, {"lt": M["KVERB"]}, False),
    ("decay_verb", "DAMPING", "Decay+Verb", "DCY+RV", {"equals": M["KVERB"]}, False),
    ("decay", "DAMPING", "FM Decay", "DECAY", {"equals": M["FM"]}, False),
    ("damping_c", "DAMPING", "Damping", "DAMP", {"equals": M["CHORD"]}, False),
    ("envelope", "DAMPING", "Envelope", "ENV", {"gt": M["CHORD"]}, False),
    # Knob 5
    ("position", "POSITION", None, None, {"lt": M["FM"]}, False),
    ("feedback", "POSITION", "FM Feedback", "FDBK", {"equals": M["FM"]}, False),
    ("position_c", "POSITION", "Position", "POS", {"equals": M["CHORD"]}, False),
    ("vowel", "POSITION", "Vowel", "VOWEL", {"equals": M["DP-FM"]}, False),
    ("chorus", "POSITION", "Chorus", "CHORUS", {"equals": M["DP-CH"]}, False),
    ("reverb", "POSITION", "Reverb", "REVERB", {"equals": M["DP-RV"]}, False),
    ("vowel_2", "POSITION", "Vowel", "VOWEL", {"equals": M["DP-F2"]}, False),
    ("ensemble", "POSITION", "Ensemble", "ENSMBL", {"equals": M["DP-EN"]}, False),
    ("reverb_2", "POSITION", "Reverb", "REVERB", {"equals": M["DP-R2"]}, False),
]

PAGE1 = ["frequency", "*structure", "*brightness", "*damping", "*position",
         "model", "polyphony", "level"]
PAGE2 = ["fine", "exciter", "spread", "velocity", "bend_range", "output", "gain", "reset"]


def alias_group(stored_key):
    sid = next(p[0] for p in PARAMS if p[1] == stored_key)
    return [a for a in ALIASES if a[1] == sid]


def check_aliases():
    """Every MODEL shows exactly one label per macro knob."""
    def visible(cond, m):
        if "lt" in cond: return m < cond["lt"]
        if "gt" in cond: return m > cond["gt"]
        return m == cond["equals"]
    for sk in ("structure", "brightness", "damping", "position"):
        for m in range(len(MODELS)):
            n = sum(1 for a in alias_group(sk) if visible(a[4], m))
            assert n == 1, f"{sk}: model {MODELS[m][0]} shows {n} labels"


def chain_params():
    out = []
    for pid, key, name, short, typ, lo, hi, step, default, extra in PARAMS:
        e = {"key": key, "name": name, "short_name": short, "type": typ}
        if typ == "enum":
            e["options"] = extra["options"]
            e["default"] = extra["options"][default]
            if "access" in extra:
                e["access"] = extra["access"]
        else:
            e.update({"min": lo, "max": hi, "step": step, "default": default})
            if "unit" in extra:
                e["unit"] = extra["unit"]
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
    return out


def ui_hierarchy():
    def expand(page):
        keys, params = [], []
        for k in page:
            if k.startswith("*"):
                for a in alias_group(k[1:]):
                    keys.append(a[0])
                    params.append({"key": a[0], "visible_if": dict(param="model", **a[4])})
            else:
                keys.append(k)
                params.append(k)
        return keys, params
    k1, p1 = expand(PAGE1)
    k2, p2 = expand(PAGE2)
    return {
        "modes": None,
        "levels": {
            "root": {"label": "oRings", "knobs": k1,
                     "params": p1 + [{"level": "advanced", "label": "Advanced"}]},
            "advanced": {"label": "Advanced", "knobs": k2, "params": p2},
        },
    }


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    check_aliases()
    gen = pathlib.Path(sys.argv[1])
    gen.mkdir(parents=True, exist_ok=True)
    cp = json.dumps(chain_params(), separators=(",", ":"))
    uh = json.dumps(ui_hierarchy(), separators=(",", ":"))
    lines = ["/* Generated by scripts/metadata.py. Do not edit. */", "#pragma once", "",
             "namespace orings {", "",
             "enum ParamId {"]
    lines += [f"  P_{p[0]}," for p in PARAMS] + ["  P_COUNT", "};", ""]
    lines += ["enum ParamType { PT_INT, PT_FLOAT, PT_ENUM };", "",
              "struct ParamDef { const char *key; ParamType type; float min, max, def; };",
              "static const ParamDef kParams[P_COUNT] = {"]
    for pid, key, name, short, typ, lo, hi, step, default, extra in PARAMS:
        t = {"int": "PT_INT", "float": "PT_FLOAT", "enum": "PT_ENUM"}[typ]
        lines.append(f'  {{"{key}", {t}, {float(lo)!r}f, {float(hi)!r}f, {float(default)!r}f}},')
    lines += ["};", "",
              "/* A second key for a stored parameter (its name in some MODELs). */",
              "struct AliasDef { const char *key; int param; bool chord; };",
              f"static const int kNumAliases = {sum(1 for a in ALIASES if a[0] != next(p[1] for p in PARAMS if p[0] == a[1]))};",
              "static const AliasDef kAliases[] = {"]
    for akey, sid, *_rest in ALIASES:
        if akey == next(p[1] for p in PARAMS if p[0] == sid):
            continue
        lines.append(f'  {{"{akey}", P_{sid}, {"true" if _rest[3] else "false"}}},')
    lines += ["};", "",
              f"static const int kNumModels = {len(MODELS)};",
              f"static const int kNumChords = {len(CHORDS)};",
              "static const char *const kModelNames[] = {" + ", ".join(c_string(m[0]) for m in MODELS) + "};",
              "static const char *const kChordNames[] = {" + ", ".join(c_string(c) for c in CHORDS) + "};",
              "static const char *const kPolyNames[] = {\"1\", \"2\", \"3\", \"4\"};",
              "static const char *const kOutputNames[] = {\"STEREO\", \"SUM\", \"ODD\", \"EVEN\"};",
              "",
              "static const char kChainParams[] = " + c_string(cp) + ";",
              "static const char kUiHierarchy[] = " + c_string(uh) + ";",
              "", "}  // namespace orings", ""]
    (gen / "orings_meta.h").write_text("\n".join(lines))

    mpath = ROOT / "module.json"
    manifest = json.loads(mpath.read_text())
    manifest["capabilities"]["chain_params"] = chain_params()
    manifest["capabilities"]["ui_hierarchy"] = ui_hierarchy()
    mpath.write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n")


if __name__ == "__main__":
    main()

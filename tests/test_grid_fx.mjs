/* oRings FX knob-grid test: Schwung's REAL page controller (from a copy of
 * the device's shared/ or a Schwung checkout), drawn through Schwung's
 * framebuffer harness, against a fake param store serving the built DSP's
 * real contract (tests/smoke_fx.py dumps it). This is the stock grid the
 * shadow UI opens for the module in a track slot, Master FX or a bus.
 *
 * Checked: knob 1 is MIX for every MODEL; the macro knobs' labels follow
 * MODEL; a parameter with no effect becomes a read-only "--" cell IN ITS OWN
 * PLACE (so no knob moves); turning the gating knob re-plans.
 *
 *   OR_SHARED     a shared/ directory to take the page engine from (e.g. one
 *                 mirrored from the device); default <schwung>/src/shared
 *   SCHWUNG_ROOT  a Schwung checkout (for tools/param-pages/harness.mjs);
 *                 default ../schwung beside this repository
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE). */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const shared = process.env.OR_SHARED || path.join(root, "src/shared");
const load = (p) => import(pathToFileURL(p).href);
const { createController } = await load(path.join(shared, "param_pages/page_controller.mjs"));
const { decodeInput, applyInput } = await load(path.join(shared, "param_pages/page_input.mjs"));
const { createFramebuffer, drawContext } = await load(path.join(root, "tools/param-pages/harness.mjs"));
console.log("page engine:", shared);

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
    else console.log("ok  ", msg);
}

const contract = JSON.parse(fs.readFileSync(path.join(here, "../build/native-fx/contract.json"), "utf8"));
const meta = Object.fromEntries(JSON.parse(contract.chain_params).map((p) => [p.key, p]));
const models = meta.model.options;

/* The stored parameter behind each label (scripts/metadata.py ALIASES). */
const FAMILY = {
    structure: ["structure", "ratio", "chord"],
    brightness: ["brightness", "fm_index", "brightness_c", "registration"],
    damping: ["damping", "decay_verb", "decay", "damping_c", "envelope"],
    position: ["position", "feedback", "position_c", "vowel", "chorus", "reverb", "vowel_2", "ensemble", "reverb_2"],
};
const baseOf = {};
for (const [base, keys] of Object.entries(FAMILY)) for (const k of keys) baseOf[k] = base;

const store = { ...contract.values };
const writes = [];
const bare = (k) => k.slice(k.indexOf(":") + 1);
function read(k) {
    if (k === "chord") return String(Math.max(0, Math.min(10, Math.round(Number(store.structure) * 10))));
    if (k.endsWith("_na")) return "0";
    if (baseOf[k]) return store[baseOf[k]];
    return k in store ? store[k] : "";
}
function write(k, v) {
    writes.push([k, String(v)]);
    if (k.endsWith("_na")) return;
    if (k === "chord") { store.structure = (Number(v) / 10).toFixed(4); return; }
    const m = meta[k];
    if (!m) return;
    let x = Number(v);
    if (m.type === "enum") x = Math.max(0, Math.min(m.options.length - 1, Math.round(x)));
    else x = Math.max(m.min, Math.min(m.max, x));
    const target = baseOf[k] || k;
    store[target] = m.type === "float" ? x.toFixed(4) : String(Math.round(x));
}

/* shadow_ui.js compareConditionValue, for the three forms the module uses. */
function visible(cond) {
    if (!cond || typeof cond !== "object") return true;
    const raw = read(cond.param);
    if (raw === null || raw === undefined) return true;
    const n = Number(raw);
    if (cond.equals !== undefined) return Number.isFinite(n) && n === cond.equals;
    if (cond.lt !== undefined) return Number.isFinite(n) && n < cond.lt;
    if (cond.gt !== undefined) return Number.isFinite(n) && n > cond.gt;
    return true;
}

let clock = 1000;
const ctrl = createController({
    getParam: (full) => {
        const k = bare(full);
        if (k === "ui_hierarchy") return contract.ui_hierarchy;
        if (k === "chain_params") return contract.chain_params;
        if (k.endsWith(":effective") || k.endsWith(":modulated")) return null;
        return read(k);
    },
    setParam: (full, v) => { write(bare(full), v); return true; },
    now: () => clock,
});
const fb = createFramebuffer();
const ctx = drawContext(fb);
function ticks(n) {
    for (let i = 0; i < n; i++) {
        clock += 17;
        ctrl.setLayout("movy");
        ctrl.tick();
        fb.clearScreen();
        ctrl.render(ctx, {});
        ctrl.renderOverlays(ctx, { clearScreen: () => fb.clearScreen() });
    }
}
const shot = (name) => {
    const dir = path.join(here, "../build/screens");
    fs.mkdirSync(dir, { recursive: true });
    if (fb.toPng) fs.writeFileSync(path.join(dir, name + ".png"), fb.toPng(4));
    else if (fb.writePng) fb.writePng(path.join(dir, name + ".png"));
};
const reload = () => {
    ctrl.load({ slot: 0, component: "fx1", prefix: "fx1", visible });
    ticks(30);
};

const MACROS = {
    MODAL: ["structure", "brightness", "damping", "position"],
    SYMPA: ["structure", "brightness", "damping", "position"],
    STRNG: ["structure", "brightness", "damping", "position"],
    KVERB: ["structure", "brightness", "decay_verb", "position"],
    FM: ["ratio", "fm_index", "decay", "feedback"],
    CHORD: ["chord", "brightness_c", "damping_c", "position_c"],
    "DP-FM": ["chord", "registration", "envelope", "vowel"],
    "DP-CH": ["chord", "registration", "envelope", "chorus"],
    "DP-RV": ["chord", "registration", "envelope", "reverb"],
    "DP-F2": ["chord", "registration", "envelope", "vowel_2"],
    "DP-EN": ["chord", "registration", "envelope", "ensemble"],
    "DP-R2": ["chord", "registration", "envelope", "reverb_2"],
};
const page2 = (sens, exc, width) =>
    ["input_gain", sens, "strum", exc, "polyphony", "routing", "fine", width];

/* 1. Every MODEL: MIX on knob 1, eight cells in knob order, then Advanced. */
for (let m = 0; m < models.length; m++) {
    store.model = String(m);
    reload();
    const p0 = ctrl.pages[0].keys.map(bare), p1 = ctrl.pages[1].keys.map(bare);
    const dp = models[m].startsWith("DP");
    const want0 = ["mix", ...MACROS[models[m]], "frequency", "model", "level"];
    const want1 = page2("sensitivity", dp ? "exciter_na" : "exciter", "width");
    check(JSON.stringify(p0) === JSON.stringify(want0) && JSON.stringify(p1) === JSON.stringify(want1),
          `model ${models[m]}: page 1 = ${p0.map((k) => meta[k].short_name).join(" ")} | page 2 = ` +
          p1.map((k) => meta[k].short_name).join(" "));
    if (["MODAL", "FM", "DP-RV"].includes(models[m])) {
        shot("fx_page1_" + models[m]);
        console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
    }
}
check(ctrl.pages.filter((p) => p.keys && p.keys.length).length === 2, "two knob pages: " +
      ctrl.pages.map((p) => p.name).join(", "));

/* 2. STRUM not AUTO and ROUTING not STEREO: SENS and WIDTH become "--" cells
 * in the same places. */
store.model = "0";
store.strum = "1";
store.routing = "1";
reload();
check(JSON.stringify(ctrl.pages[1].keys.map(bare)) ===
      JSON.stringify(page2("sensitivity_na", "exciter", "width_na")),
      "STRUM MIDI + ROUTING ODD: page 2 = " + ctrl.pages[1].keys.map(bare).join(" "));
check(meta.sensitivity_na.access === "read" && meta.width_na.access === "read" && meta.exciter_na.access === "read",
      "placeholders are access:read");
store.strum = "0";
store.routing = "0";
reload();

/* 3. Knob 1 turns MIX. */
{
    const before = store.mix;
    const w0 = writes.length;
    for (let i = 0; i < 6; i++) {
        applyInput(ctrl, decodeInput([0xb0, 71, 1], {}), { nowMs: (clock += 30) });
        ticks(1);
    }
    ticks(20);
    const w = writes.slice(w0);
    check(w.length > 0 && w.every(([k]) => k === "mix") && Number(store.mix) > Number(before),
          "knob 1 writes mix " + before + " -> " + store.mix);
}

/* 4. Turning MODEL (knob 7) re-plans the labels; MIX stays on knob 1. */
for (let i = 0; i < 400 && store.model !== "4"; i++) {
    applyInput(ctrl, decodeInput([0xb0, 77, 1], {}), { nowMs: (clock += 30) });
    ticks(1);
}
ticks(30);
check(store.model === "4" &&
      JSON.stringify(ctrl.page.keys.map(bare)) === JSON.stringify(["mix", ...MACROS.FM, "frequency", "model", "level"]),
      "MODEL knob turned to FM: page 1 = " + ctrl.page.keys.map(bare).join(" "));

/* 5. FREQUENCY shows note names. */
check(meta.frequency.options[meta.frequency.options.indexOf("C2")] === "C2" &&
      meta.frequency.options[0] === "C0" && meta.frequency.default === "C2",
      "FREQUENCY: note names C0..C7, default C2");

if (failures) { console.error(`${failures} GRID FAILURE(S)`); process.exit(1); }
console.log("ALL FX GRID TESTS PASSED");

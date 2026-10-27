/* oRings knob-grid test: Schwung's REAL page controller (from a copy of the
 * device's shared/ or a Schwung checkout), drawn through Schwung's
 * framebuffer harness, against a fake param store serving the built DSP's
 * real contract (tests/smoke.py dumps it). This is the stock grid the shadow
 * UI opens for the module.
 *
 * The point of interest is the MODEL-dependent labels: every macro knob has
 * one `visible_if`-gated label per meaning, so for each MODEL exactly eight
 * cells must be planned on the first page, in knob order, and turning the
 * MODEL knob must re-plan them.
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

const contract = JSON.parse(fs.readFileSync(path.join(here, "../build/native/contract.json"), "utf8"));
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
    if (baseOf[k]) return store[baseOf[k]];
    return k in store ? store[k] : "";
}
function write(k, v) {
    writes.push([k, String(v)]);
    if (k === "chord") { store.structure = (Number(v) / 10).toFixed(4); return; }
    const m = meta[k];
    if (!m) return;
    let x = Number(v);
    if (m.type === "enum") x = Math.max(0, Math.min(m.options.length - 1, Math.round(x)));
    else x = Math.max(m.min, Math.min(m.max, x));
    const target = baseOf[k] || k;
    store[target] = m.type === "float" ? x.toFixed(4) : String(Math.round(x));
}

/* shadow_ui.js compareConditionValue / evaluateVisibilityConditionForContext,
 * for the three forms the module uses. */
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

const EXPECT = {
    MODAL: ["frequency", "structure", "brightness", "damping", "position", "model", "polyphony", "level"],
    SYMPA: ["frequency", "structure", "brightness", "damping", "position", "model", "polyphony", "level"],
    STRNG: ["frequency", "structure", "brightness", "damping", "position", "model", "polyphony", "level"],
    KVERB: ["frequency", "structure", "brightness", "decay_verb", "position", "model", "polyphony", "level"],
    FM: ["frequency", "ratio", "fm_index", "decay", "feedback", "model", "polyphony", "level"],
    CHORD: ["frequency", "chord", "brightness_c", "damping_c", "position_c", "model", "polyphony", "level"],
    "DP-FM": ["frequency", "chord", "registration", "envelope", "vowel", "model", "polyphony", "level"],
    "DP-CH": ["frequency", "chord", "registration", "envelope", "chorus", "model", "polyphony", "level"],
    "DP-RV": ["frequency", "chord", "registration", "envelope", "reverb", "model", "polyphony", "level"],
    "DP-F2": ["frequency", "chord", "registration", "envelope", "vowel_2", "model", "polyphony", "level"],
    "DP-EN": ["frequency", "chord", "registration", "envelope", "ensemble", "model", "polyphony", "level"],
    "DP-R2": ["frequency", "chord", "registration", "envelope", "reverb_2", "model", "polyphony", "level"],
};
const PAGE2 = ["fine", "exciter", "spread", "velocity", "bend_range", "output", "gain", "reset"];

/* 1. Every MODEL plans eight cells in knob order, then the Advanced page. */
for (let m = 0; m < models.length; m++) {
    store.model = String(m);
    ctrl.load({ slot: 0, component: "synth", prefix: "synth", visible });
    ticks(30);
    const p0 = ctrl.pages[0].keys.map(bare), p1 = ctrl.pages[1].keys.map(bare);
    check(JSON.stringify(p0) === JSON.stringify(EXPECT[models[m]]) && JSON.stringify(p1) === JSON.stringify(PAGE2),
          `model ${models[m]}: page 1 = ${p0.map((k) => meta[k].short_name).join(" ")}`);
    if (["MODAL", "FM", "CHORD", "DP-RV"].includes(models[m])) {
        shot("page1_" + models[m]);
        console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
    }
}
check(ctrl.pages.filter((p) => p.keys && p.keys.length).length === 2, "two knob pages: " +
      ctrl.pages.map((p) => p.name).join(", "));

/* 2. Turning MODEL (knob 6) on the grid re-plans the labels. */
store.model = "0";
ctrl.load({ slot: 0, component: "synth", prefix: "synth", visible });
ticks(30);
for (let i = 0; i < 400 && store.model !== "4"; i++) {
    applyInput(ctrl, decodeInput([0xb0, 76, 1], {}), { nowMs: (clock += 30) });
    ticks(1);
}
ticks(30);
check(store.model === "4" && JSON.stringify(ctrl.page.keys.map(bare)) === JSON.stringify(EXPECT.FM),
      "MODEL knob turned to FM: page 1 = " + ctrl.page.keys.map(bare).join(" "));

/* 3. In FM, knob 2 writes the frequency ratio (STRUCTURE). */
const before = store.structure;
const w0 = writes.length;
for (let i = 0; i < 6; i++) {
    applyInput(ctrl, decodeInput([0xb0, 72, 1], {}), { nowMs: (clock += 30) });
    ticks(1);
}
ticks(20);
const w = writes.slice(w0);
check(w.length > 0 && w.every(([k]) => k === "ratio") && store.structure !== before,
      "FM knob 2 writes ratio -> structure " + before + " -> " + store.structure);

/* 4. Turning back to a Rings model restores the standard labels; the value
 * written as ratio is the same STRUCTURE. */
for (let i = 0; i < 400 && store.model !== "1"; i++) {
    applyInput(ctrl, decodeInput([0xb0, 76, 127], {}), { nowMs: (clock += 30) });
    ticks(1);
}
ticks(30);
check(store.model === "1" && JSON.stringify(ctrl.page.keys.map(bare)) === JSON.stringify(EXPECT.SYMPA),
      "MODEL back to SYMPA: standard labels again");

/* 5. RESET is a trigger: a knob cannot fire it. */
check(meta.reset.access === "write", "RESET is access:write (fires on click only)");

if (failures) { console.error(`${failures} GRID FAILURE(S)`); process.exit(1); }
console.log("ALL GRID TESTS PASSED");

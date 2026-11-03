/* oRings UI test: ui_core.mjs driving Schwung's REAL page controller and
 * input decoder, drawn through Schwung's framebuffer harness, against a fake
 * param store serving the built DSP's real contract (tests/smoke.py dumps
 * it). Covers the MODEL-dependent knob labels, the pads, touch recording,
 * PITCH names, the MODELS list and the presets.
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
const { createUi, MAIN_KEYS, STATUS_EVERY_TICKS, HEARTBEAT_MS, LED, SETTINGS_PATH, PRESET_PATH,
        PRESET_FIRST, parsePresets, parseModelSettings, serializeModelSettings, visibleModels,
        MODEL_COUNT } = await load(path.join(here, "../ui_core.mjs"));
const { MODEL_NAMES, ALIAS_BASE } = await load(path.join(here, "../ui_params.mjs"));
console.log("page engine:", shared);

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
    else console.log("ok  ", msg);
}

const contract = JSON.parse(fs.readFileSync(path.join(here, "../build/native/contract.json"), "utf8"));
const chainParams = JSON.parse(contract.chain_params);
const meta = Object.fromEntries(chainParams.map((p) => [p.key, p]));

/* ---- fake DSP ----------------------------------------------------------- */
const PHRYGIAN = ["C", "Db", "Eb", "F", "G", "Ab", "Bb"];
const CHROMATIC = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
const namesFor = (set) => {
    const out = [];
    for (let oct = 1; oct <= 5; oct++) for (const n of set) out.push(n + oct);
    return out.concat(["C6"]).join(",");
};
let pitchNames = namesFor(PHRYGIAN);
const values = { ...contract.values };
const gates = new Array(16).fill(true);
const writes = [];
const reads = [];
let status = "0,-1,0,8,0";
let hwShift = false, shown = true, padBlocked = false;
const leds = {};
const files = new Map();
function readValue(k) {
    if (ALIAS_BASE[k]) return values[ALIAS_BASE[k]];
    return k in values ? values[k] : "";
}
const store = {
    get(key) {
        reads.push(key);
        if (key === "ui_hierarchy") return "";
        if (key === "grid_hierarchy") return contract.grid_hierarchy;
        if (key === "chain_params") return contract.chain_params;
        if (key === "motion_status") return status;
        if (key === "pitch_names") return pitchNames;
        if (key === "gate_status") return gates.map((g) => g ? "1" : "0").join("");
        if (key === "state") return JSON.stringify({ v: 1, model: Number(values.model) });
        if (key.startsWith("eff_")) return readValue(key.slice(4));
        return readValue(key);
    },
    set(key, value) {
        writes.push([key, String(value)]);
        if (key === "gate_toggle") gates[Number(value)] = !gates[Number(value)];
        if (key === "state") values.model = String(JSON.parse(value).model);
        const base = ALIAS_BASE[key] || key;
        if (base in values) values[base] = String(value);
        return true;
    },
};

let clock = 1000;
const fb = createFramebuffer();
const ctx = drawContext(fb);
const ui = createUi({
    createController, decodeInput, applyInput,
    getParam: (k) => store.get(k),
    setParam: (k, v) => store.set(k, v),
    draw: ctx,
    clear: () => fb.clearScreen(),
    layout: () => "movy",
    now: () => clock,
    shiftHeld: () => hwShift,
    displayOn: () => shown,
    padBlock: (on) => { padBlocked = on; },
    sendLed: (n, col) => { leds[n] = col; return true; },
    padSnapshot: () => ({ "68": 5, "99": 7 }),
    readFile: (p) => files.get(p) ?? null,
    writeFile: (p, t) => { files.set(p, t); return true; },
    ensureDir: () => true,
});

function ticks(n) { for (let i = 0; i < n; i++) { clock += 17; ui.tick(); } }
const touchesSent = () => writes.filter(([k]) => k === "touch").map(([, v]) => v);
const bare = (k) => k.slice(k.indexOf(":") + 1);
const shot = (name) => {
    const dir = path.join(here, "../build/screens");
    fs.mkdirSync(dir, { recursive: true });
    if (fb.toPng) fs.writeFileSync(path.join(dir, name + ".png"), fb.toPng(4));
};

ui.init();
ticks(40);
const c = ui.state.controller;
const page = () => c.page.keys.map(bare);
check(JSON.stringify(page()) === JSON.stringify(MAIN_KEYS),
      "page 1 = VOLUME STRUCT BRIGHT DAMP POS POLY MODEL PITCH: " + page());
check(c.pages.filter((p) => p.keys && p.keys.length).length === 3,
      "Main / Rings / Scale: " + c.pages.map((p) => p.name).join(", "));
check(!reads.includes("ui_hierarchy"), "the grid reads grid_hierarchy, never the empty ui_hierarchy");
check(fb.countLit() > 200, "page 1 draws");
check(ui.title() === "MODAL", "title = the model: " + ui.title());
shot("page1_MODAL");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());

/* ---- every MODEL names knobs 2-5 for what they do ----------------------- */
const EXPECT = {
    MODAL: ["structure", "brightness", "damping", "position"],
    SYMPA: ["structure", "brightness", "damping", "position"],
    STRNG: ["structure", "brightness", "damping", "position"],
    KVERB: ["structure", "brightness", "decay_verb", "position"],
    FM: ["ratio", "fm_index", "decay", "feedback"],
    CHORD: ["structure_c", "brightness_c", "damping_c", "position_c"],
    "DP-FM": ["cutoff", "registration", "envelope", "vowel"],
    "DP-CH": ["cutoff", "registration", "envelope", "chorus"],
    "DP-RV": ["cutoff", "registration", "envelope", "reverb"],
    "DP-F2": ["cutoff", "registration", "envelope", "vowel_2"],
    "DP-EN": ["cutoff", "registration", "envelope", "ensemble"],
    "DP-R2": ["cutoff", "registration", "envelope", "reverb_2"],
};
const full = (m) => ["level", ...EXPECT[m], "polyphony", "model", "pitch"];
/* A preset or Schwung's own screen changes MODEL under us: the poll sees it. */
for (let m = 0; m < MODEL_NAMES.length; m++) {
    values.model = String(m);
    ticks(STATUS_EVERY_TICKS * 6);
    const name = MODEL_NAMES[m];
    check(JSON.stringify(page()) === JSON.stringify(full(name)),
          `model ${name}: page 1 = ${page().map((k) => meta[k].short_name).join(" ")}`);
    check(ui.title() === name, `header names ${name}: ${ui.title()}`);
    if (["FM", "CHORD", "DP-RV"].includes(name)) {
        shot("page1_" + name);
        console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
    }
}

/* ---- turning MODEL (knob 7) re-plans; it writes the real index ---------- */
values.model = "0";
ticks(STATUS_EVERY_TICKS * 6);
for (let i = 0; i < 400 && values.model !== "4"; i++) { ui.onMidi([0xb0, 77, 1]); ticks(1); }
ticks(30);
check(values.model === "4" && JSON.stringify(page()) === JSON.stringify(full("FM")),
      "MODEL knob to FM: " + page());
/* In FM, knob 2 is RATIO: it writes, and its touch records, STRUCTURE. */
const s0 = values.structure, w0 = writes.length;
for (let i = 0; i < 6; i++) { ui.onMidi([0xb0, 72, 1]); ticks(1); }
ticks(20);
check(writes.slice(w0).some(([k]) => k === "ratio") && values.structure !== s0,
      `FM knob 2 writes ratio -> structure ${s0} -> ${values.structure}`);
ui.onMidi([0x90, 1, 127]);
check(touchesSent().at(-1) === "structure,1", "touching RATIO records STRUCTURE: " + touchesSent().at(-1));
ui.onMidi([0x80, 1, 0]);
check(touchesSent().at(-1) === "structure,0", "...and lets it go");
for (let i = 0; i < 400 && values.model !== "0"; i++) { ui.onMidi([0xb0, 77, 127]); ticks(1); }
ticks(30);
check(values.model === "0" && JSON.stringify(page()) === JSON.stringify(MAIN_KEYS), "MODEL back to MODAL");

/* ---- playing: step in the header; no REC until the REC pad ---------------- */
status = "0,4,0,8,0";
ticks(STATUS_EVERY_TICKS);
check(/ 5\/8$/.test(ui.title()), "step in header: " + ui.title());
ui.onMidi([0x90, 2, 127]);                            /* knob 3 touch = BRIGHT */
check(touchesSent().at(-1) === "brightness,1", "touch reported: " + touchesSent().at(-1));
check(!/REC/.test(ui.title()), "no REC without the REC pad: " + ui.title());
ui.onMidi([0x80, 2, 0]);
for (const n of [5, 6]) {                            /* POLY, MODEL */
    const before = touchesSent().length;
    ui.onMidi([0x90, n, 127]); ui.onMidi([0x80, n, 0]);
    check(touchesSent().length === before, `knob ${n + 1} (${MAIN_KEYS[n]}) is never recorded`);
}

/* ---- the pads ----------------------------------------------------------- */
check(padBlocked === true, "pads taken while the screen is up");
ticks(8);
check(leds[99] === LED.RECORD_OFF && leds[98] === LED.RANDOM && leds[97] === LED.MODELS, "REC / RANDOM / MODELS lit");
check(leds[68] === LED.GATE_ON && leds[72] === LED.PLAYHEAD, "gates lit, playhead on step 5");
check(leds[76] === LED.OFF && leds[83] === LED.OFF, "8-step loop: steps 9-16 dark");
ui.onMidi([0x90, 99, 100]);
check(writes.at(-1)[0] === "write" && writes.at(-1)[1] === "1" && leds[99] === LED.RECORD_ON, "REC on = solid red");
ui.onMidi([0x90, 99, 100]);
check(writes.filter(([k]) => k === "write").length === 1, "held pad fires once");
ui.onMidi([0x80, 99, 0]);
status = "0,4,0,8,1";
ticks(STATUS_EVERY_TICKS);
check(/ ARM 5\/8$/.test(ui.title()), "armed: " + ui.title());
ui.onMidi([0x90, 3, 127]);                           /* DAMP */
check(/ REC 5\/8$/.test(ui.title()), "REC under the finger at once: " + ui.title());
const n0 = touchesSent().filter((t) => t === "damping,1").length;
ticks(Math.ceil(HEARTBEAT_MS * 4 / 17));
const beats = touchesSent().filter((t) => t === "damping,1").length - n0;
check(beats >= 3 && beats <= 5, `heartbeat restates the touch (${beats} in ${HEARTBEAT_MS * 4} ms)`);
ui.onMidi([0x80, 3, 0]);
const quiet = touchesSent().length;
ticks(40);
check(touchesSent().length === quiet, "no heartbeat once released");

ui.onMidi([0x90, 98, 100]);
check(writes.some(([k]) => k === "randomize") && leds[98] === LED.PRESSED, "RANDOM pad writes a phrase");
ui.onMidi([0x80, 98, 0]);
ui.onMidi([0x90, 69, 100]);
check(writes.at(-1)[0] === "gate_toggle" && writes.at(-1)[1] === "1" && leds[69] === LED.GATE_OFF, "step 2 toggled off");
ui.onMidi([0x80, 69, 0]);
ticks(STATUS_EVERY_TICKS);
check(ui.state.gates[1] === "0", "the poll agrees: " + ui.state.gates);
ui.onMidi([0x90, 80, 100]); ui.onMidi([0x80, 80, 0]);
check(!(writes.at(-1)[0] === "gate_toggle" && writes.at(-1)[1] === "12"), "a dark step pad does nothing");
const wu = writes.length;
ui.onMidi([0x90, 92, 100]); ui.onMidi([0x80, 92, 0]);
check(writes.length === wu, "unassigned pads do nothing");
status = "0,12,0,16,1";
ticks(STATUS_EVERY_TICKS + 8);
check(leds[80] === LED.PLAYHEAD && leds[83] !== LED.OFF, "16 steps: second row lit, step 13 playing");

/* ---- PITCH: note in the header while held -------------------------------- */
check(ui.state.names && ui.state.names.length === 36 && ui.state.names[14] === "C3", "PITCH names from the DSP");
c.state.values.pitch = "16";
ui.onMidi([0x90, 7, 127]);
check(touchesSent().at(-1) === "pitch,1", "knob 8 is PITCH");
check(ui.title() === "PITCH Eb3 REC 13/16", "pitch note while held: " + ui.title());
ui.onMidi([0x90, 7, 0]);
check(touchesSent().at(-1) === "pitch,0", "note-on vel 0 releases");

/* ---- an automated knob is drawn as modulated, from eff_<key> -------------- */
status = `${1 << 1},3,0,16,1`;                       /* lane 1 = brightness */
reads.length = 0;
ticks(STATUS_EVERY_TICKS * 30);
check(c.isModulatedCached("brightness"), "automated knob marked modulated");
check(reads.includes("eff_brightness"), "its playing value is read as eff_brightness");

/* ---- Shift + touch clears one lane; on VOLUME, all ------------------------ */
const before = touchesSent().length;
hwShift = true;
ui.onMidi([0x90, 2, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "brightness", "shift+touch clears the lane: " + writes.at(-1));
ui.onMidi([0x80, 2, 0]);
ui.onMidi([0x90, 0, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "all", "shift+touch VOLUME clears all");
ui.onMidi([0x80, 0, 0]);
hwShift = false;
ticks(1);
check(touchesSent().length === before, "shift+touch never starts a recording");
ui.onMidi([0x90, 0, 127]); ui.onMidi([0x80, 0, 0]);
check(touchesSent().length === before, "VOLUME touch is never a recording");

/* ---- page 2 (Rings): EXCITE records, the rest does not -------------------- */
ui.onMidi([0xb0, 14, 1]);
ticks(20);
check(JSON.stringify(page()) === JSON.stringify(["frequency", "fine", "exciter", "spread", "velocity", "bend_range", "output", "gain"]),
      "page 2: " + page());
ui.onMidi([0x90, 2, 127]);
check(touchesSent().at(-1) === "exciter,1", "EXCITE records from page 2");
ui.onMidi([0x80, 2, 0]);
const n2 = touchesSent().length;
for (const n of [0, 1, 3, 4, 5, 6, 7]) { ui.onMidi([0x90, n, 127]); ui.onMidi([0x80, n, 0]); }
check(touchesSent().length === n2, "the other page-2 settings are not recorded");
shot("page2");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());

/* ---- page 3: ROOT SCALE STEPS RESET; a scale change re-reads names -------- */
ui.onMidi([0xb0, 14, 1]); ticks(20);
check(JSON.stringify(page()) === JSON.stringify(["root", "scale", "steps", "reset"]), "page 3: " + page());
check(meta.reset.access === "write", "RESET fires on click only");
pitchNames = namesFor(CHROMATIC);
for (let i = 0; i < 4; i++) ui.onMidi([0xb0, 72, 1]);
ticks(30);
check(writes.some(([k]) => k === "scale"), "SCALE written");
check(ui.state.names.length === 61, "names re-read after a scale change: " + (ui.state.names && ui.state.names.length));
shot("page3");
ui.onMidi([0xb0, 14, 127]); ticks(20);
ui.onMidi([0xb0, 14, 127]); ticks(20);
check(JSON.stringify(page()) === JSON.stringify(MAIN_KEYS), "after the scale's reload, still one label per knob: " + page());

/* ---- the MODELS list (pad 97): the knob then offers only what is on ------- */
check(parseModelSettings(null).every(Boolean) && parseModelSettings("{junk").every(Boolean), "settings robust");
check(JSON.stringify(visibleModels([true, false, true, ...new Array(9).fill(false)], 5)) === "[0,2,5]",
      "visible = enabled + the one playing");
values.model = "3";
ticks(STATUS_EVERY_TICKS * 3);
ui.onMidi([0x90, 97, 100]); ui.onMidi([0x80, 97, 0]);
check(ui.state.listOpen, "pad 97 opens the MODELS list");
ticks(2);
check(leds[97] === LED.MODELS_OPEN && fb.countLit() > 100, "MODELS pad lit, the list draws");
shot("models");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
const knobWrites = writes.length;
ui.onMidi([0xb0, 71, 1]);
ticks(3);
check(writes.length === knobWrites, "knobs inert while the list is open");
for (let i = 0; i < 20; i++) ui.onMidi([0xb0, 14, 127]);   /* to ALL ON */
ui.onMidi([0xb0, 14, 1]);                                  /* ALL OFF */
ui.onMidi([0xb0, 3, 127]);
check(ui.state.enabled.filter(Boolean).length === 1 && ui.state.enabled[3], "ALL OFF keeps the playing model");
for (let i = 0; i < 5; i++) ui.onMidi([0xb0, 14, 1]);      /* row 1 -> FM (row 6) */
ui.onMidi([0xb0, 3, 127]);
check(ui.state.enabled[4], "FM on");
check(files.get(SETTINGS_PATH) === serializeModelSettings(ui.state.enabled), "saved on every toggle");
check(ui.handleBack() === true, "Back closes the list (consumed)");
ticks(STATUS_EVERY_TICKS * 2);
check(JSON.stringify(ui.state.vis) === "[3,4]", "the knob offers KVERB and FM: " + ui.state.vis);
const mw = writes.filter(([k]) => k === "model").length;
for (let i = 0; i < 30; i++) ui.onMidi([0xb0, 77, 1]);
ticks(30);
const modelWrites = writes.filter(([k]) => k === "model").slice(mw).map(([, v]) => v);
check(modelWrites.length > 0 && modelWrites.every((v) => v === "3" || v === "4"),
      "MODEL only ever writes enabled models: " + modelWrites);
check(values.model === "4" && JSON.stringify(page()) === JSON.stringify(full("FM")), "and lands on FM with its labels");

/* ---- the preset pads (second row, 84-91) ------------------------------------ */
ticks(8);
check(leds[84] === LED.OFF && leds[91] === LED.OFF, "empty presets are dark");
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);
check(!writes.some(([k]) => k === "state") && /EMPTY 3/.test(ui.title()), "an empty preset loads nothing: " + ui.title());
hwShift = true; ticks(1);
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);
hwShift = false; ticks(1);
const savedList = parsePresets(files.get(PRESET_PATH));
check(savedList[2] && JSON.parse(savedList[2]).model === 4, "Shift+pad stores the state: " + files.get(PRESET_PATH));
check(/SAVED PRESET 3/.test(ui.title()), "save confirmed: " + ui.title());
ticks(8);
check(leds[86] === LED.PRESET_CURRENT && leds[84] === LED.OFF, "saved preset lit white");
values.model = "3";
ticks(STATUS_EVERY_TICKS * 6);
ui.onMidi([0x90, 86, 100]); ui.onMidi([0x80, 86, 0]);
ticks(STATUS_EVERY_TICKS * 6);
check(writes.at(-1)[0] === "state" || writes.some(([k]) => k === "state"), "tap recalls the state");
check(values.model === "4" && JSON.stringify(page()) === JSON.stringify(full("FM")), "the recalled MODEL re-plans the labels: " + page());
check(PRESET_FIRST === 84, "presets on the second row");

/* ---- the screen closes: pads, colours and touches go back ---------------- */
ui.onMidi([0x90, 2, 127]);
shown = false;
ticks(1);
check(padBlocked === false, "pads handed back");
check(leds[68] === 5 && leds[99] === 7 && leds[80] === 0, "Move's pad colours restored");
check(touchesSent().at(-1) === "brightness,0", "a held knob is released on leave: " + touchesSent().at(-1));
shown = true;
ticks(1);
check(padBlocked === true, "taken again when shown");
check(ui.handleBack() === false, "Back at the grid exits");
ui.releaseAll();
check(padBlocked === false, "leaving releases the pads");
status = null;
ticks(STATUS_EVERY_TICKS);
check(ui.state.step === 3, "a null status read changes nothing");
check(MODEL_COUNT === 12, "12 models");
check(fb.clipped() === 0, `no pixels off screen (${fb.clipped()})`);

if (failures) { console.error(`${failures} UI FAILURE(S)`); process.exit(1); }
console.log("ALL UI TESTS PASSED");

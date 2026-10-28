/*
 * oRings - Signal Chain UI core.
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Schwung's stock knob grid runs no module code, so the DSP would never
 * learn that a knob is being TOUCHED -- and touch is the automation write
 * window. So the DSP serves `ui_hierarchy` empty (the shadow UI then opens
 * ui_chain.js) and this file drives Schwung's OWN page engine
 * (page_controller.mjs) itself: the same grid, the same pages, plus the
 * knobs' touch sensors and the pads.
 *
 * Pages: main   VOLUME STRUCT BRIGHT DAMP POS POLY MODEL PITCH
 *        Rings  FREQ FINE EXCITE SPREAD VELO BEND OUTPUT GAIN
 *        Scale  ROOT SCALE STEPS RESET
 * Knobs 2-5 are named for what they do in the MODEL playing (FM: RATIO INDEX
 * DECAY FDBK, ...): each is a label key with a `visible_if` on `model`
 * (scripts/metadata.py), evaluated here against the real MODEL index.
 *
 * With the REC pad on, touching a sound knob while Move plays records its
 * movement for as long as the finger stays on it (Shift + touch clears that
 * knob, Shift + touch on VOLUME clears all). VOLUME, MODEL and the settings
 * are never recorded. The DSP owns the recording (src/wrapper/motion.cpp);
 * an automated knob is drawn as a modulated one, and the header shows the
 * model, the loop step, ARM while REC is on and REC while a finger writes.
 *
 * THE PADS are taken (host_pad_block) while this screen is up -- Schwung only
 * lets a module hold them then, and hands them back when the display closes:
 *   top-right (99)       REC: touches record motion. Solid red on, dim off.
 *   next to it (98)      RANDOM: a new Phrase Gen phrase into PITCH's steps.
 *   next to that (97)    MODELS: the list of models the MODEL knob offers.
 *   second row (84-91)   eight PRESETS: tap recalls, Shift+tap saves the
 *                        whole state (sound, motion, steps, scale). Off =
 *                        empty, dim = saved, white = last recalled/saved.
 *                        Stored in PRESET_PATH, shared by every slot.
 *   bottom two rows      the 16 trigger steps (68-75 = 1-8, 76-83 = 9-16;
 *   (68-83)              dark past an 8-step loop). The step playing is white.
 *
 * PITCH is an INTEGER index into the notes of the Scale page's root + scale:
 * the knob itself is set in the scale, nothing is quantized afterwards.
 *
 * MODEL: the page engine is shown only the models switched ON in the MODELS
 * list (plus the one playing, if it is off), as an int 0..n-1 named through
 * formatValue -- so the knob steps over exactly those, and an enum's
 * full-screen peek never covers the grid. The list is a PREFERENCE shared by
 * every oRings slot (a file outside the module directory).
 *
 * SHIFT IS NOT MIDI HERE: in shadow mode the shim swallows CC 49 and
 * publishes the state in shared memory (shadow_get_shift_held).
 *
 * Every dependency is injected (ui_chain.js binds the device, the tests bind
 * fakes), so nothing here touches a global.
 */
import { MODEL_NAMES, ALIAS_BASE, MOTION_KEYS, MAIN_KEYS } from './ui_params.mjs';

export { MOTION_KEYS, MAIN_KEYS };


export const STATUS_EVERY_TICKS = 6;     /* ~10 Hz: one IPC read costs ~2.8 ms */
export const NAMES_EVERY_TICKS = 120;
export const HEARTBEAT_MS = 150;         /* the DSP lets a touch go after 500 ms */

export const PAD_FIRST = 68, PAD_LAST = 99;
export const GATE_LAST = 83, MODELS_PAD = 97, RANDOM_PAD = 98, RECORD_PAD = 99;
export const PRESET_FIRST = 84, PRESETS = 8;
/* Move pad palette indices. */
export const LED = { RECORD_ON: 127, RECORD_OFF: 65, RANDOM: 21, MODELS: 118, MODELS_OPEN: 120,
                     PRESSED: 120, PLAYHEAD: 120, GATE_ON: 11, GATE_OFF: 1, OFF: 0,
                     PRESET_SAVED: 73, PRESET_CURRENT: 120 };
const LEDS_PER_TICK = 8;                 /* 32 pads repainted every 4 frames */

/* ---- the MODELS list ------------------------------------------------------ */

export const SETTINGS_DIR = "/data/UserData/schwung/orings";
export const SETTINGS_PATH = `${SETTINGS_DIR}/settings.json`;
export const MODEL_COUNT = MODEL_NAMES.length;

/* ---- the preset pads ---------------------------------------------------- */

export const PRESET_PATH = `${SETTINGS_DIR}/presets.json`;

/* {"v":1,"presets":[8 x state string | null]}. Anything unreadable starts
 * empty rather than refusing to run. */
export function parsePresets(text) {
    const out = new Array(PRESETS).fill(null);
    if (typeof text !== "string" || !text) return out;
    let doc;
    try { doc = JSON.parse(text); } catch (e) { return out; }
    const a = doc && doc.v === 1 && Array.isArray(doc.presets) ? doc.presets : [];
    for (let i = 0; i < PRESETS; i++) out[i] = typeof a[i] === "string" && a[i] ? a[i] : null;
    return out;
}

export function serializePresets(list) {
    return JSON.stringify({ v: 1, presets: list });
}

/* {"v":1,"disabled":["DP-CH","DP-R2"]} -- by NAME. Anything unreadable, and any
 * file that would leave nothing enabled, means "all enabled". */
export function parseModelSettings(text) {
    const all = new Array(MODEL_COUNT).fill(true);
    if (typeof text !== "string" || text.length === 0) return all;
    let doc;
    try { doc = JSON.parse(text); } catch (e) { return all; }
    if (!doc || doc.v !== 1 || !Array.isArray(doc.disabled)) return all;
    const enabled = all.slice();
    for (const name of doc.disabled) {
        const i = MODEL_NAMES.indexOf(name);
        if (i >= 0) enabled[i] = false;
    }
    return enabled.some(Boolean) ? enabled : all;
}

export function serializeModelSettings(enabled) {
    return JSON.stringify({ v: 1, disabled: MODEL_NAMES.filter((_, i) => !enabled[i]) });
}

/* The models the MODEL knob offers: the enabled ones, plus the one playing
 * (a preset can carry a model that is switched off; it stays nameable). */
export function visibleModels(enabled, current) {
    const out = [];
    for (let i = 0; i < MODEL_COUNT; i++) if (enabled[i] || i === current) out.push(i);
    return out;
}

const ROW_ALL_ON = 0, ROW_ALL_OFF = 1, ROW_FIRST_MODEL = 2;
const LIST_ROWS = ROW_FIRST_MODEL + MODEL_COUNT;
const LIST_VISIBLE = 6;
const NOTICE_MS = 1200;

export function createUi(deps) {
    const {
        createController, decodeInput, applyInput,
        getParam, setParam, announce = () => {},
        layout = () => undefined, draw, clear,
        trailingMenus = null, runAction = null, now = () => Date.now(),
        shiftHeld = () => false,
        displayOn = () => true, padBlock = () => {},
        sendLed = () => {}, padSnapshot = () => null,
        readFile = () => null, writeFile = () => false, ensureDir = () => false,
    } = deps;

    const st = {
        controller: null,
        shift: false,
        shiftCc: false,
        tick: 0,
        mask: 0,          /* lanes that hold automation */
        step: -1,         /* loop step, -1 stopped */
        recording: 0,     /* lanes being written, as the DSP saw them */
        len: 8,           /* loop: 8 or 16 steps */
        write: 0,         /* REC pad */
        gates: "1".repeat(16),
        names: null,      /* PITCH note names for the scale in use */
        namesRaw: null,
        keyDirty: false,  /* root/scale written: re-read names + contract */
        slotKey: {},      /* knob slot -> lane it touched */
        lastBeat: 0,
        visible: false,
        heldPads: new Set(),
        ledCursor: 0,
        /* MODEL */
        enabled: new Array(MODEL_COUNT).fill(true),
        model: 0,         /* the real MODEL index, as the DSP last said */
        vis: [],          /* visibleModels() the controller was loaded with */
        visDirty: false,
        dirReady: false,
        /* MODELS list overlay */
        listOpen: false, cursor: 0, listTop: 0,
        notice: "", noticeUntil: 0,
        presets: null,    /* loaded on first use */
        preset: -1,       /* last recalled / saved */
    };

    const bare = (fullKey) => {
        const i = fullKey.indexOf(":");
        return i < 0 ? fullKey : fullKey.slice(i + 1);
    };
    /* A label key (ratio, chord, vowel...) turns a stored parameter. */
    const baseOf = (key) => ALIAS_BASE[key] || key;
    const bit = (key) => {
        const i = MOTION_KEYS.indexOf(baseOf(key));
        return i < 0 ? 0 : 1 << i;
    };
    const isAutomated = (key) => (st.mask & bit(key)) !== 0;

    /* ---- MODEL through the visible list ---- */
    function refreshVisible() {
        const next = visibleModels(st.enabled, st.model);
        if (next.join() !== st.vis.join()) { st.vis = next; st.visDirty = true; }
    }
    /* A real model index -> its position in the visible list (the nearest
     * below for one that is not there). */
    function visPos(model) {
        let best = 0;
        for (let i = 0; i < st.vis.length; i++) if (st.vis[i] <= model) best = i;
        return best;
    }
    function readModel() {
        const raw = getParam("model");
        const m = Math.round(Number(raw));
        if (typeof raw === "string" && raw.length && Number.isFinite(m)) { st.model = m; refreshVisible(); }
        return raw;
    }

    /* The controller asks for `synth:<key>`; the host's per-slot shims add the
     * prefix themselves. Redirected: the hierarchy lives under grid_hierarchy,
     * the value an automated knob is PLAYING is served as eff_<key>, and MODEL
     * is shown as its position in the visible list. */
    function ioGet(fullKey) {
        const k = bare(fullKey);
        if (k === "ui_hierarchy") return getParam("grid_hierarchy");
        if (k === "chain_params") {
            const raw = getParam("chain_params");
            if (typeof raw !== "string" || !raw.length) return raw;
            try {
                const list = JSON.parse(raw);
                const m = list.find((p) => p.key === "model");
                /* Served as an enum named by the 12 models (for Schwung's own
                 * screens); here it is an int over the models switched ON,
                 * named through formatValue. */
                if (m) {
                    delete m.options;
                    Object.assign(m, { type: "int", min: 0, max: Math.max(0, st.vis.length - 1), step: 1, default: 0 });
                }
                return JSON.stringify(list);
            } catch (e) { return raw; }
        }
        if (k === "model" || k === "model:effective") {   /* never recorded */
            const raw = readModel();
            if (typeof raw !== "string" || !raw.length) return raw;
            return String(visPos(st.model));
        }
        if (k.endsWith(":effective")) return getParam("eff_" + k.slice(0, -10));
        if (k.endsWith(":modulated")) return isAutomated(k.slice(0, -10)) ? "1" : "0";
        return getParam(k);
    }

    function ioSet(fullKey, value) {
        const k = bare(fullKey);
        if (k === "root" || k === "scale") st.keyDirty = true;
        if (k === "model") {
            const i = Math.round(Number(value));
            const real = st.vis[Math.max(0, Math.min(st.vis.length - 1, i))];
            if (real === undefined) return false;
            st.model = real;
            return setParam("model", String(real));
        }
        return setParam(k, value);
    }

    function pollStatus() {
        const raw = getParam("motion_status");
        if (typeof raw === "string" && raw.length) {
            const f = raw.split(",").map((x) => parseInt(x, 10));
            if (f.length === 5 && f.every(Number.isFinite) && [8, 16].includes(f[3]))
                [st.mask, st.step, st.recording, st.len, st.write] = [f[0], f[1], f[2], f[3], f[4] ? 1 : 0];
        }
        const g = getParam("gate_status");
        if (typeof g === "string" && /^[01]{16}$/.test(g)) st.gates = g;
    }

    function pollNames() {
        const raw = getParam("pitch_names");
        if (typeof raw !== "string" || !raw.length || raw === st.namesRaw) return false;
        st.namesRaw = raw;
        st.names = raw.split(",");
        return true;
    }

    function noteOf(raw) {
        const i = Math.round(Number(raw));
        return st.names && Number.isFinite(i) && i >= 0 && i < st.names.length ? st.names[i] : null;
    }

    function modelNameAt(pos) {
        const i = Math.round(Number(pos));
        const real = Number.isFinite(i) ? st.vis[i] : undefined;
        return real === undefined ? null : MODEL_NAMES[real];
    }

    function formatValue(fullKey, raw) {
        const k = bare(fullKey);
        if (k === "pitch") return noteOf(raw);
        if (k === "model") return modelNameAt(raw);
        return null;
    }

    /* The labels' visible_if: every condition is on `model` (lt / gt /
     * equals), and is answered with the REAL model index -- the controller
     * itself only ever sees the position in the visible list. */
    function visible(cond) {
        if (!cond || typeof cond !== "object" || cond.param !== "model") return true;
        const n = st.model;
        if (cond.equals !== undefined) return n === cond.equals;
        if (cond.lt !== undefined) return n < cond.lt;
        if (cond.gt !== undefined) return n > cond.gt;
        return true;
    }

    function shiftNow() { return st.shiftCc || !!shiftHeld(); }

    function syncShift() {
        const next = shiftNow();
        if (next === st.shift) return;
        st.shift = next;
        if (st.controller) applyInput(st.controller, { type: "shift", down: next }, { nowMs: now(), reveal: true });
    }

    function loadSettings() {
        st.enabled = parseModelSettings(readFile(SETTINGS_PATH));
    }

    function saveSettings() {
        if (!st.dirReady) st.dirReady = !!ensureDir(SETTINGS_DIR);   /* mkdir -p: once per session */
        const ok = st.dirReady && !!writeFile(SETTINGS_PATH, serializeModelSettings(st.enabled));
        if (!ok) showNotice("SAVE FAILED");
    }

    function showNotice(text) {
        st.notice = text;
        st.noticeUntil = now() + NOTICE_MS;
    }

    function init() {
        loadSettings();
        loadPresets();
        readModel();
        st.vis = visibleModels(st.enabled, st.model);
        st.visDirty = false;
        st.controller = createController({
            getParam: ioGet,
            setParam: ioSet,
            formatValue,
            announce,
            isModulated: (fullKey) => isAutomated(bare(fullKey)),
            trailingMenus: trailingMenus ? () => trailingMenus() || [] : undefined,
            now,
        });
        pollNames();
        st.controller.load({ slot: 0, component: "synth", prefix: "synth", visible });
        pollStatus();
    }

    function heldMask() {
        let m = st.recording;
        for (const key of Object.values(st.slotKey)) m |= bit(key);
        return m;
    }

    /* The header: the model playing, then the step; PITCH's note while held.
     * A preset pad's message takes it for a moment. */
    function title() {
        if (now() < st.noticeUntil) return st.notice;
        let head = MODEL_NAMES[st.model] || "oRings";
        const held = heldMask();
        if (held & bit("pitch")) {
            const n = st.controller ? noteOf(st.controller.state.values.pitch) : null;
            if (n) head = "PITCH " + n;
        }
        if (st.step < 0) return st.write ? head + " ARM" : head;
        const pos = `${st.step + 1}/${st.len}`;
        if (st.write && held) return `${head} REC ${pos}`;
        return st.write ? `${head} ARM ${pos}` : `${head} ${pos}`;
    }

    function padColor(note) {
        if (note <= GATE_LAST) {
            const step = note - PAD_FIRST;
            if (step >= Math.min(st.len, 16)) return LED.OFF;
            if (st.step >= 0 && step === st.step % 16) return LED.PLAYHEAD;
            return st.gates[step] === "1" ? LED.GATE_ON : LED.GATE_OFF;
        }
        if (note >= PRESET_FIRST && note < PRESET_FIRST + PRESETS) {
            const i = note - PRESET_FIRST;
            if (st.heldPads.has(note) || st.preset === i) return st.presets && st.presets[i] ? LED.PRESET_CURRENT : LED.OFF;
            return st.presets && st.presets[i] ? LED.PRESET_SAVED : LED.OFF;
        }
        if (note === RECORD_PAD) return st.write ? LED.RECORD_ON : LED.RECORD_OFF;
        if (note === RANDOM_PAD) return st.heldPads.has(note) ? LED.PRESSED : LED.RANDOM;
        if (note === MODELS_PAD) return st.listOpen ? LED.MODELS_OPEN : LED.MODELS;
        return LED.OFF;
    }

    function paintPads() {
        for (let i = 0; i < LEDS_PER_TICK; ++i) {
            const note = PAD_FIRST + (st.ledCursor++ % 32);
            sendLed(note, padColor(note));
        }
    }

    function restorePads() {
        const snap = padSnapshot();
        for (let n = PAD_FIRST; n <= PAD_LAST; ++n) sendLed(n, (snap && snap[String(n)]) || 0);
    }

    /* ---- the MODELS list ---- */
    function openList() {
        st.listOpen = true;
        st.cursor = ROW_FIRST_MODEL + st.model;
        st.listTop = Math.max(0, Math.min(LIST_ROWS - LIST_VISIBLE, st.cursor - 2));
        announce("Models");
    }

    function closeList() {
        st.listOpen = false;
        refreshVisible();
    }

    function moveCursor(delta) {
        st.cursor = Math.min(LIST_ROWS - 1, Math.max(0, st.cursor + delta));
        if (st.cursor < st.listTop) st.listTop = st.cursor;
        if (st.cursor >= st.listTop + LIST_VISIBLE) st.listTop = st.cursor - LIST_VISIBLE + 1;
    }

    function activateRow() {
        const current = st.model;
        if (st.cursor === ROW_ALL_ON) {
            st.enabled.fill(true);
        } else if (st.cursor === ROW_ALL_OFF) {
            st.enabled.fill(false);
            st.enabled[current] = true;
            showNotice(`KEPT ${MODEL_NAMES[current]}`);
        } else {
            const i = st.cursor - ROW_FIRST_MODEL;
            if (st.enabled[i] && st.enabled.filter(Boolean).length === 1) {
                showNotice("ONE MUST STAY ON");
                return;
            }
            st.enabled[i] = !st.enabled[i];
        }
        saveSettings();
        refreshVisible();
    }

    function box(x, y, w, h, c) {
        draw.fillRect(x, y, w, 1, c);
        draw.fillRect(x, y + h - 1, w, 1, c);
        draw.fillRect(x, y, 1, h, c);
        draw.fillRect(x + w - 1, y, 1, h, c);
    }

    function drawList() {
        const tw = (t) => (draw.textWidth ? draw.textWidth(t) : t.length * 6);
        clear();
        draw.fillRect(0, 0, 128, 10, 1);
        draw.print(2, 1, "MODELS", 0);
        const info = now() < st.noticeUntil ? st.notice : `${st.enabled.filter(Boolean).length}/${MODEL_COUNT} ON`;
        draw.print(126 - tw(info), 1, info, 0);
        for (let r = 0; r < LIST_VISIBLE; r++) {
            const row = st.listTop + r;
            if (row >= LIST_ROWS) break;
            const y = 11 + r * 9;
            const sel = row === st.cursor;
            if (sel) draw.fillRect(0, y - 1, 122, 9, 1);
            const c = sel ? 0 : 1;
            if (row === ROW_ALL_ON) { draw.print(12, y, "ALL ON", c); continue; }
            if (row === ROW_ALL_OFF) { draw.print(12, y, "ALL OFF", c); continue; }
            const i = row - ROW_FIRST_MODEL;
            if (st.enabled[i]) draw.fillRect(2, y, 7, 7, c);      /* filled = ON */
            else box(2, y, 7, 7, c);
            draw.print(12, y, MODEL_NAMES[i], c);
            if (i === st.model) draw.print(120 - tw("<"), y, "<", c);   /* playing now */
        }
        const trackH = LIST_VISIBLE * 9;
        const thumbH = Math.max(4, Math.round(trackH * LIST_VISIBLE / LIST_ROWS));
        const thumbY = 10 + Math.round((trackH - thumbH) * st.listTop / (LIST_ROWS - LIST_VISIBLE));
        draw.fillRect(125, thumbY, 3, thumbH, 1);
    }

    function tick() {
        const c = st.controller;
        if (!c) return;
        if (!displayOn()) { if (st.visible) releaseAll(); return; }
        st.visible = true;
        padBlock(true);                   /* restated every frame: the shim drops it on its own */
        syncShift();
        if (++st.tick % STATUS_EVERY_TICKS === 0) { pollStatus(); readModel(); }
        if (st.keyDirty || st.tick % NAMES_EVERY_TICKS === 0) {
            st.keyDirty = false;
            /* PITCH's range follows the scale. `visible` on every reload: one
             * without it plans every label of every MODEL at once. */
            if (pollNames()) c.reloadIfChanged({ visible });
        }
        if (st.visDirty && !st.listOpen) {
            st.visDirty = false;
            c.reloadIfChanged({ visible });          /* MODEL's range follows the list */
        }
        const held = Object.values(st.slotKey);
        if (held.length && now() - st.lastBeat >= HEARTBEAT_MS) {
            st.lastBeat = now();
            for (const key of held) setParam("touch", `${key},1`);
        }
        const l = layout();
        if (l) c.setLayout(l);
        c.tick();
        if (st.listOpen) {
            drawList();
        } else {
            clear();
            c.render(draw, { title: title() });
            c.renderOverlays(draw, { clearScreen: clear });
        }
        paintPads();
    }

    function touch(slot, down) {
        if (!down) {
            const key = st.slotKey[slot];
            if (!key) return;
            delete st.slotKey[slot];
            setParam("touch", `${key},0`);
            return;
        }
        const full = st.controller.keyAt(slot);
        const key = full ? baseOf(bare(full)) : null;
        if (!key) return;
        if (key === "level") {
            /* never recorded; Shift + touch here clears every lane */
            if (st.shift) {
                setParam("motion_clear", "all");
                st.mask = 0;
                announce("All automation cleared");
            }
            return;
        }
        if (!MOTION_KEYS.includes(key)) return;
        if (st.shift) {
            setParam("motion_clear", key);
            st.mask &= ~bit(key);
            announce(`${key} automation cleared`);
            return;
        }
        setParam("touch", `${key},1`);
        st.slotKey[slot] = key;
        st.lastBeat = now();
    }

    function loadPresets() {
        if (!st.presets) st.presets = parsePresets(readFile(PRESET_PATH));
    }

    /* Shift+tap stores the whole state; a tap applies it, playing or not. */
    function onPreset(i) {
        loadPresets();
        if (shiftNow()) {
            const text = getParam("state");
            if (typeof text !== "string" || !text) { showNotice("SAVE FAILED"); return; }
            const next = st.presets.slice();
            next[i] = text;
            if (!st.dirReady) st.dirReady = !!ensureDir(SETTINGS_DIR);
            if (st.dirReady && writeFile(PRESET_PATH, serializePresets(next))) {
                st.presets = next;
                st.preset = i;
                showNotice(`SAVED PRESET ${i + 1}`);
                announce(`Preset ${i + 1} saved`);
            } else {
                showNotice("SAVE FAILED");
            }
            return;
        }
        const text = st.presets[i];
        if (!text) { showNotice(`EMPTY ${i + 1} (SHIFT+PAD SAVES)`); return; }
        if (!setParam("state", text)) { showNotice("LOAD FAILED"); return; }
        st.preset = i;
        showNotice(`PRESET ${i + 1}`);
        announce(`Preset ${i + 1}`);
        /* Every knob, the scale and the model may have changed under us. */
        readModel();
        st.keyDirty = true;
        pollStatus();
        if (st.controller && typeof st.controller.revalue === "function") st.controller.revalue();
    }

    function onPad(note, down) {
        if (!down) { st.heldPads.delete(note); sendLed(note, padColor(note)); return; }
        if (st.heldPads.has(note)) return;
        st.heldPads.add(note);
        if (note === RECORD_PAD) {
            st.write = st.write ? 0 : 1;
            setParam("write", st.write);
            announce(st.write ? "Motion record on" : "Motion record off");
        } else if (note === RANDOM_PAD) {
            setParam("randomize", 1);
            announce("New phrase");
            pollStatus();
        } else if (note >= PRESET_FIRST && note < PRESET_FIRST + PRESETS) {
            onPreset(note - PRESET_FIRST);
        } else if (note === MODELS_PAD) {
            if (st.listOpen) closeList(); else openList();
        } else if (note <= GATE_LAST) {
            const step = note - PAD_FIRST;
            if (step < Math.min(st.len, 16)) {
                setParam("gate_toggle", step);
                st.gates = st.gates.slice(0, step) + (st.gates[step] === "1" ? "0" : "1") + st.gates.slice(step + 1);
            }
        }
        sendLed(note, padColor(note));
    }

    function run(todo) {
        if (todo && todo.action === "menu" && todo.entry && todo.entry.action && runAction)
            return !!runAction(todo.entry.action);
        return false;
    }

    function onMidi(data) {
        const c = st.controller;
        if (!c || !data || data.length < 3 || !displayOn()) return;
        const status = data[0] & 0xf0;
        if ((status === 0x90 || status === 0x80) && data[1] >= PAD_FIRST && data[1] <= PAD_LAST) {
            onPad(data[1], status === 0x90 && data[2] > 0);
            return;
        }
        if (status === 0xb0 && data[1] === 49) { st.shiftCc = data[2] > 0; syncShift(); return; }
        if (st.listOpen) {
            /* The list owns the jog; knobs and touches do nothing here. */
            if (status !== 0xb0) return;
            const d = data[2];
            if (data[1] === 14) {
                const delta = d >= 1 && d <= 63 ? 1 : d >= 65 ? -1 : 0;
                if (delta) moveCursor(delta);
            } else if (data[1] === 3 && d > 0) {
                activateRow();
            }
            return;
        }
        syncShift();
        const intent = decodeInput(data, { shift: st.shift });
        if (!intent) return;
        if (intent.type === "touch") touch(intent.slot, intent.down);
        run(applyInput(c, intent, { nowMs: now(), reveal: st.shift }));
    }

    /* Back closes the MODELS list first, then peels the grid's layers. */
    function handleBack() {
        const c = st.controller;
        if (!c) return false;
        if (st.listOpen) { closeList(); return true; }
        const todo = applyInput(c, { type: "back" }, { nowMs: now() });
        return !(todo && todo.action === "exit");
    }

    function releaseAll() {
        for (const key of Object.values(st.slotKey)) setParam("touch", `${key},0`);
        st.slotKey = {};
        st.heldPads.clear();
        st.listOpen = false;
        restorePads();
        padBlock(false);
        st.visible = false;
    }

    return { init, tick, onMidi, handleBack, releaseAll, title, padColor, state: st };
}

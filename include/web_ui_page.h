#ifndef WEB_UI_PAGE_H
#define WEB_UI_PAGE_H

#include <Arduino.h>

// The web UI, served as-is from flash. Self-contained on purpose: the deck may
// be on a network with no internet access, so no CDN scripts or fonts.
const char WEB_UI_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LocalDeck</title>
<style>
:root {
  --bg: #f4f4f2; --panel: #ffffff; --text: #1c1c1e; --muted: #6b6b70;
  --line: #e2e2e0; --accent: #2563eb; --accent-soft: #dbe6fd;
  --deck: #2a2a2e; --key: #f7f7f5; --key-text: #2a2a2e; --danger: #c2372c;
  --ok: #1f8a4c; --warn: #b86e00;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #141416; --panel: #1e1e21; --text: #ececee; --muted: #9a9aa2;
    --line: #313136; --accent: #6b9bff; --accent-soft: #23304d;
    --deck: #0b0b0c; --key: #2b2b30; --key-text: #ececee; --danger: #ff6b5e;
    --ok: #4ccf82; --warn: #f0a93a;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0; background: var(--bg); color: var(--text);
  font: 15px/1.4 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
header {
  display: flex; flex-wrap: wrap; align-items: center; gap: 8px 16px;
  padding: 14px 16px; border-bottom: 1px solid var(--line); background: var(--panel);
}
header h1 { font-size: 18px; margin: 0; margin-right: auto; }
.pill {
  display: inline-flex; align-items: center; gap: 6px; font-size: 13px;
  color: var(--muted); white-space: nowrap;
}
.pill::before {
  content: ""; width: 8px; height: 8px; border-radius: 50%; background: var(--muted);
}
.pill.ok::before { background: var(--ok); }
.pill.bad::before { background: var(--danger); }
.pill.busy::before { background: var(--warn); }
main {
  display: grid; grid-template-columns: minmax(0, 1fr) 340px; gap: 16px;
  padding: 16px; max-width: 1200px; margin: 0 auto;
}
@media (max-width: 860px) { main { grid-template-columns: minmax(0, 1fr); } }
section {
  background: var(--panel); border: 1px solid var(--line); border-radius: 12px;
  padding: 16px; min-width: 0;
}
h2 { font-size: 14px; margin: 0 0 12px; color: var(--muted); font-weight: 600; }
.deck {
  background: var(--deck); border-radius: 18px; padding: 14px;
  display: grid; gap: 10px; grid-template-columns: repeat(var(--cols), minmax(0, 1fr));
}
.key {
  position: relative; aspect-ratio: 1; border-radius: 10px; background: var(--key);
  color: var(--key-text); padding: 8px; display: flex; flex-direction: column;
  justify-content: flex-end; gap: 2px; cursor: pointer; user-select: none;
  border: 2px solid transparent; overflow: hidden; min-width: 0;
  box-shadow: 0 0 0 0 transparent; transition: box-shadow .2s, border-color .1s;
}
.key.lit { box-shadow: 0 0 18px 2px var(--led); }
.key::after {
  content: ""; position: absolute; left: 0; right: 0; top: 0; height: 5px;
  background: var(--led, transparent); opacity: .9;
}
.key.empty {
  background: transparent; border: 2px dashed #55555c; color: #8a8a92;
  justify-content: center; align-items: center;
}
.key.empty .name { font-size: 22px; font-weight: 300; }
.key.reserved { background: #3a3a40; color: #c8c8cc; cursor: default; }
.key.selected { border-color: var(--accent); }
.key.over { border-color: var(--accent); background: var(--accent-soft); }
.key .name {
  font-weight: 600; font-size: 13px; line-height: 1.2; overflow: hidden;
  display: -webkit-box; -webkit-line-clamp: 3; -webkit-box-orient: vertical;
  overflow-wrap: break-word; hyphens: auto;
}
.key .domain { font-size: 11px; opacity: .65; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.key .badge {
  position: absolute; top: 9px; right: 7px; font-size: 10px; opacity: .6;
}
@media (max-width: 520px) {
  .deck { gap: 5px; padding: 6px; border-radius: 12px; }
  .key { padding: 3px; border-radius: 7px; border-width: 1px; }
  .key .name { font-size: 9.5px; -webkit-line-clamp: 3; }
  .key .badge { top: 5px; right: 3px; font-size: 8px; }
  .key.empty .name { font-size: 18px; }
  .key .domain { display: none; }
}
.hint { color: var(--muted); font-size: 13px; margin: 10px 0 0; }
.editor { margin-top: 16px; display: grid; gap: 12px; }
.editor[hidden] { display: none; }
.row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
label { font-size: 13px; color: var(--muted); display: grid; gap: 4px; }
input[type=text], input[type=search] {
  width: 100%; padding: 8px 10px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--bg); color: var(--text); font: inherit;
}
input[type=color] {
  width: 44px; height: 36px; padding: 2px; border: 1px solid var(--line);
  border-radius: 8px; background: var(--bg);
}
input[type=range] { width: 100%; accent-color: var(--accent); }
button {
  font: inherit; padding: 8px 12px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--bg); color: var(--text); cursor: pointer;
}
button:hover { border-color: var(--accent); }
button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
button.danger { color: var(--danger); }
button:disabled { opacity: .5; cursor: default; }
.chips { display: flex; flex-wrap: wrap; gap: 6px; margin: 10px 0; }
.chip {
  padding: 4px 10px; border-radius: 999px; font-size: 12px; border: 1px solid var(--line);
  background: var(--bg); color: var(--muted); cursor: pointer;
}
.chip.on { background: var(--accent-soft); color: var(--text); border-color: var(--accent); }
.list {
  list-style: none; margin: 0; padding: 0; max-height: 60vh; overflow-y: auto;
  border-top: 1px solid var(--line);
}
.list.drop { outline: 2px dashed var(--danger); outline-offset: -2px; }
.list li {
  padding: 8px 6px; border-bottom: 1px solid var(--line); cursor: grab;
  display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 2px 8px;
}
.list li:hover { background: var(--bg); }
.list li.armed { background: var(--accent-soft); }
.list .ename { font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.list .eid { grid-column: 1; font-size: 12px; color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.list .estate { grid-row: 1 / 3; grid-column: 2; align-self: center; font-size: 12px; color: var(--muted); }
.list .used { color: var(--accent); }
.empty-note { color: var(--muted); font-size: 13px; padding: 12px 0; }
.tools { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 16px; }
#toast {
  position: fixed; left: 50%; bottom: 20px; transform: translateX(-50%);
  background: var(--text); color: var(--bg); padding: 10px 16px; border-radius: 8px;
  font-size: 14px; opacity: 0; pointer-events: none; transition: opacity .2s;
  max-width: calc(100% - 32px);
}
#toast.show { opacity: 1; }
dialog {
  border: 1px solid var(--line); border-radius: 12px; background: var(--panel);
  color: var(--text); width: min(720px, calc(100% - 32px)); padding: 16px;
}
dialog textarea {
  width: 100%; height: 50vh; font: 12px/1.4 ui-monospace, Menlo, monospace;
  background: var(--bg); color: var(--text); border: 1px solid var(--line);
  border-radius: 8px; padding: 10px;
}
</style>
</head>
<body>
<header>
  <h1>LocalDeck</h1>
  <span id="ha" class="pill">Home Assistant</span>
  <span id="save" class="pill">Loading…</span>
</header>
<main>
  <section>
    <h2>Buttons</h2>
    <div id="deck" class="deck"></div>
    <p class="hint" id="hint">Drag an entity onto a button. Drag buttons onto each other to swap them, or back onto the list to clear. Or tap a button to edit it, then tap an entity to put it there.</p>
    <div id="editor" class="editor" hidden>
      <label>Entity
        <input id="ed-entity" type="text" list="entity-options" autocomplete="off" spellcheck="false" placeholder="light.kitchen">
      </label>
      <datalist id="entity-options"></datalist>
      <div class="row">
        <label>Colour <input id="ed-color" type="color"></label>
        <label style="flex:1">Brightness <input id="ed-brightness" type="range" min="1" max="255"></label>
      </div>
      <p class="hint" id="ed-note"></p>
      <div class="row">
        <button id="ed-press" class="primary">Press</button>
        <button id="ed-clear" class="danger">Clear button</button>
        <button id="ed-close">Done</button>
      </div>
    </div>
    <div class="tools">
      <button id="export">Export as config.h</button>
      <button id="reset" class="danger">Reset to config.h</button>
    </div>
  </section>
  <section>
    <h2>Home Assistant entities</h2>
    <div class="row">
      <input id="search" type="search" placeholder="Search entities" autocomplete="off" style="flex:1">
      <button id="refresh" title="Reload the entity list">Reload</button>
    </div>
    <div id="chips" class="chips"></div>
    <ul id="list" class="list"></ul>
  </section>
</main>
<div id="toast" role="status"></div>
<dialog id="export-dialog">
  <p class="hint" style="margin-top:0">Paste this over the <code>entityMappings</code> block in <code>include/config.h</code> to make this layout the one a freshly flashed deck starts with.</p>
  <textarea id="export-text" readonly></textarea>
  <div class="row" style="margin-top:12px">
    <button id="export-copy" class="primary">Copy</button>
    <button id="export-close">Close</button>
  </div>
</dialog>
<script>
"use strict";
const $ = id => document.getElementById(id);
const DEFAULT_COLORS = {
  light: "#ffffff", switch: "#ffa500", media_player: "#00ff00", cover: "#0080ff",
  script: "#00ffff", scene: "#ff00ff", fan: "#00ffaa", input_boolean: "#ffff00",
  automation: "#8080ff", button: "#ff4040", input_button: "#ff4040"
};
const ACTIONS = {
  light: "Tap toggles it. Hold ▲/▼ and this button to dim. Shows the light's own colour once it reports one.",
  media_player: "Tap plays / pauses. Hold ▲/▼ and this button to change volume.",
  cover: "Tap opens / closes. Hold ▲/▼ and this button to set position, if the cover supports it.",
  fan: "Tap turns it on / off. Hold ▲/▼ and this button to change speed, if the fan supports it.",
  switch: "Tap toggles it.", input_boolean: "Tap toggles it.",
  automation: "Tap enables / disables the automation.", script: "Tap runs the script (lit while it runs).",
  scene: "Tap activates the scene.", button: "Tap presses it.", input_button: "Tap presses it."
};

let device = null;          // last /api/config response
let layout = new Map();     // "x,y" -> {entity_id, color, brightness}
let entities = [];          // [[id, name, state], ...]
let names = new Map();      // id -> friendly name
let domainFilter = "";
let selected = null;        // "x,y" being edited
let armed = null;           // entity id picked in the list, waiting for a tap on a key
let saveTimer = null, saving = false, saveAgain = false;
let dragging = false;       // polls hold off mid-drag so the grid isn't rebuilt under it

const key = (x, y) => x + "," + y;
const domainOf = id => id.split(".")[0];
const nameOf = id => names.get(id) || id.split(".").slice(1).join(".").replace(/_/g, " ");

function toast(message) {
  const t = $("toast");
  t.textContent = message;
  t.classList.add("show");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.remove("show"), 3500);
}

async function api(path, body) {
  const options = body === undefined ? {} : {
    method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)
  };
  const response = await fetch(path, options);
  if (response.status === 204) return null;
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || ("HTTP " + response.status));
  return data;
}

function isReserved(x, y) {
  return device && ((device.up[0] === x && device.up[1] === y) ||
                    (device.down[0] === x && device.down[1] === y));
}

function takeDevice(data, adoptLayout) {
  device = data;
  if (adoptLayout) {
    layout = new Map();
    for (const b of data.buttons) {
      layout.set(key(b.x, b.y), {entity_id: b.entity_id, color: b.color, brightness: b.brightness});
    }
  }
  renderStatus();
  renderDeck();
  if (adoptLayout) renderEditor();
}

function renderStatus(state) {
  const ha = $("ha");
  ha.className = "pill " + (device && device.ha_connected ? "ok" : "bad");
  ha.textContent = device && device.ha_connected ? "Home Assistant connected" : "Home Assistant offline";
  const s = $("save");
  if (state === "saving" || saveTimer || saving) {
    s.className = "pill busy"; s.textContent = "Saving…";
  } else if (state === "error") {
    s.className = "pill bad"; s.textContent = "Not saved";
  } else if (device) {
    s.className = "pill ok";
    s.textContent = device.customised ? "Saved on deck" : "Layout from config.h";
  }
}

function liveColor(b) {
  if (!b || !b.live || !b.live.on) return null;
  const hex = b.live.color, k = Math.max(0.25, b.live.brightness / 255);
  const c = [1, 3, 5].map(i => Math.round(parseInt(hex.substr(i, 2), 16) * k));
  return "rgb(" + c.join(",") + ")";
}

function renderDeck() {
  if (!device) return;
  const deck = $("deck");
  deck.style.setProperty("--cols", device.cols);
  deck.replaceChildren();
  const live = new Map(device.buttons.map(b => [key(b.x, b.y), b]));
  // y = 0 is the bottom row of the deck, where the ▲/▼ keys sit.
  for (let y = device.rows - 1; y >= 0; y--) {
    for (let x = 0; x < device.cols; x++) {
      deck.appendChild(renderKey(x, y, live.get(key(x, y))));
    }
  }
}

function renderKey(x, y, liveState) {
  const k = key(x, y);
  const el = document.createElement("div");
  el.className = "key";
  el.dataset.key = k;
  const name = document.createElement("div");
  name.className = "name";
  const sub = document.createElement("div");
  sub.className = "domain";
  el.append(name, sub);

  if (isReserved(x, y)) {
    const up = device.up[0] === x && device.up[1] === y;
    el.classList.add("reserved");
    name.textContent = up ? "▲ Up" : "▼ Down";
    sub.textContent = "hold + a key to dim";
    el.title = "Reserved for the brightness / volume gesture";
    return el;
  }

  const cell = layout.get(k);
  if (cell) {
    name.textContent = nameOf(cell.entity_id);
    sub.textContent = cell.entity_id;
    el.title = cell.entity_id;
    el.draggable = true;
    // Only trust live state if the deck is still showing the same entity.
    const lit = liveState && liveState.entity_id === cell.entity_id ? liveColor(liveState) : null;
    el.style.setProperty("--led", lit || "transparent");
    if (lit) el.classList.add("lit");
  } else {
    el.classList.add("empty");
    name.textContent = "+";
    el.title = "Nothing assigned";
  }
  if (device.child_lock.some(p => p[0] === x && p[1] === y)) {
    const badge = document.createElement("span");
    badge.className = "badge";
    badge.textContent = "🔒";
    badge.title = "Hold with the other 🔒 key to toggle child lock";
    el.appendChild(badge);
  }
  if (selected === k) el.classList.add("selected");

  el.addEventListener("click", () => onKeyTap(k));
  el.addEventListener("dragstart", e => {
    e.dataTransfer.setData("text/x-deck-key", k);
    e.dataTransfer.effectAllowed = "move";
    $("list").classList.add("drop");
    dragging = true;
  });
  el.addEventListener("dragend", endDrag);
  el.addEventListener("dragover", e => { e.preventDefault(); el.classList.add("over"); });
  el.addEventListener("dragleave", () => el.classList.remove("over"));
  el.addEventListener("drop", e => {
    e.preventDefault();
    endDrag();
    const from = e.dataTransfer.getData("text/x-deck-key");
    const entity = e.dataTransfer.getData("text/x-entity");
    if (from) swapKeys(from, k);
    else if (entity) assign(k, entity);
  });
  return el;
}

function endDrag() {
  dragging = false;
  $("list").classList.remove("drop");
}

function onKeyTap(k) {
  if (armed) {
    assign(k, armed);
    armed = null;
    renderList();
    return;
  }
  selected = selected === k ? null : k;
  renderDeck();
  renderEditor();
}

function assign(k, entityId) {
  const previous = layout.get(k);
  const color = previous ? previous.color : (DEFAULT_COLORS[domainOf(entityId)] || "#ffffff");
  layout.set(k, {entity_id: entityId, color, brightness: previous ? previous.brightness : 255});
  selected = k;
  changed();
  toast(nameOf(entityId) + " placed");
}

function swapKeys(a, b) {
  if (a === b) return;
  const ca = layout.get(a), cb = layout.get(b);
  if (cb) layout.set(a, cb); else layout.delete(a);
  if (ca) layout.set(b, ca); else layout.delete(b);
  selected = b;
  changed();
}

function clearKey(k) {
  layout.delete(k);
  if (selected === k) selected = null;
  changed();
}

// Every edit goes straight to the deck, a beat later so a dragged slider
// is one save rather than fifty.
function changed(delay = 250, entitiesMoved = true) {
  renderDeck();
  renderEditor();
  if (entitiesMoved) renderList();
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, delay);
  renderStatus("saving");
}

async function save() {
  saveTimer = null;
  if (saving) { saveAgain = true; return; }
  saving = true;
  const buttons = [];
  for (const [k, cell] of layout) {
    const [x, y] = k.split(",").map(Number);
    buttons.push({x, y, ...cell});
  }
  let failed = false;
  try {
    const data = await api("/api/config", {buttons});
    if (!saveAgain && !saveTimer) takeDevice(data, false);
  } catch (err) {
    failed = true;
    toast("Could not save: " + err.message);
  }
  saving = false;
  if (saveAgain) { saveAgain = false; save(); return; }
  renderStatus(failed ? "error" : undefined);
}

function renderEditor() {
  const ed = $("editor");
  const cell = selected && layout.get(selected);
  if (!selected || isReserved(...selected.split(",").map(Number))) {
    ed.hidden = true;
    return;
  }
  ed.hidden = false;
  const input = $("ed-entity");
  if (document.activeElement !== input) input.value = cell ? cell.entity_id : "";
  $("ed-color").value = cell ? cell.color : "#ffffff";
  $("ed-brightness").value = cell ? cell.brightness : 255;
  $("ed-color").disabled = $("ed-brightness").disabled = !cell;
  $("ed-press").disabled = $("ed-clear").disabled = !cell;
  $("ed-note").textContent = cell ? (ACTIONS[domainOf(cell.entity_id)] || "") :
    "Pick an entity for this button, or drag one here from the list.";
}

function editSelected(patch, delay) {
  const cell = layout.get(selected);
  if (!cell) return;
  Object.assign(cell, patch);
  changed(delay, false);
}

function supported(id) { return Object.prototype.hasOwnProperty.call(DEFAULT_COLORS, domainOf(id)); }

$("ed-entity").addEventListener("change", e => {
  const id = e.target.value.trim().toLowerCase();
  if (!id) { clearKey(selected); return; }
  if (!/^[a-z0-9_]+\.[a-z0-9_]+$/.test(id) || !supported(id)) {
    toast("Not an entity a button can use: " + id);
    renderEditor();
    return;
  }
  assign(selected, id);
});
$("ed-color").addEventListener("input", e => editSelected({color: e.target.value}, 400));
$("ed-brightness").addEventListener("input", e => editSelected({brightness: Number(e.target.value)}, 400));
$("ed-clear").addEventListener("click", () => clearKey(selected));
$("ed-close").addEventListener("click", () => { selected = null; renderDeck(); renderEditor(); });
$("ed-press").addEventListener("click", async () => {
  const [x, y] = selected.split(",").map(Number);
  try { await api("/api/press", {x, y}); setTimeout(poll, 400); }
  catch (err) { toast("Press failed: " + err.message); }
});

function renderChips() {
  const counts = new Map();
  for (const e of entities) counts.set(domainOf(e[0]), (counts.get(domainOf(e[0])) || 0) + 1);
  const chips = $("chips");
  chips.replaceChildren();
  const add = (domain, label) => {
    const c = document.createElement("button");
    c.className = "chip" + (domainFilter === domain ? " on" : "");
    c.textContent = label;
    c.addEventListener("click", () => { domainFilter = domain; renderChips(); renderList(); });
    chips.appendChild(c);
  };
  add("", "All " + entities.length);
  for (const [domain, n] of [...counts].sort()) add(domain, domain.replace(/_/g, " ") + " " + n);
}

function renderList() {
  const list = $("list");
  list.replaceChildren();
  const q = $("search").value.trim().toLowerCase();
  const used = new Set([...layout.values()].map(c => c.entity_id));
  const shown = entities.filter(e =>
    (!domainFilter || domainOf(e[0]) === domainFilter) &&
    (!q || e[0].includes(q) || e[1].toLowerCase().includes(q)));
  if (!shown.length) {
    const li = document.createElement("div");
    li.className = "empty-note";
    li.textContent = entities.length ? "Nothing matches." : "No entities loaded yet.";
    list.appendChild(li);
    return;
  }
  for (const [id, name, state] of shown.slice(0, 400)) {
    const li = document.createElement("li");
    li.draggable = true;
    if (armed === id) li.classList.add("armed");
    const n = document.createElement("span");
    n.className = "ename";
    n.textContent = name;
    const i = document.createElement("span");
    i.className = "eid";
    i.textContent = id;
    const s = document.createElement("span");
    s.className = "estate" + (used.has(id) ? " used" : "");
    s.textContent = used.has(id) ? "on deck" : state;
    li.append(n, i, s);
    li.addEventListener("dragstart", e => {
      e.dataTransfer.setData("text/x-entity", id);
      e.dataTransfer.effectAllowed = "copy";
      dragging = true;
    });
    li.addEventListener("dragend", endDrag);
    li.addEventListener("click", () => {
      if (selected && !isReserved(...selected.split(",").map(Number))) {
        assign(selected, id);
        return;
      }
      armed = armed === id ? null : id;
      renderList();
      if (armed) toast("Now tap a button for " + name);
    });
    list.appendChild(li);
  }
  if (shown.length > 400) {
    const more = document.createElement("div");
    more.className = "empty-note";
    more.textContent = (shown.length - 400) + " more — search to narrow it down.";
    list.appendChild(more);
  }
}

// Dropping a key back on the list clears it.
$("list").addEventListener("dragover", e => {
  if (e.dataTransfer.types.includes("text/x-deck-key")) e.preventDefault();
});
$("list").addEventListener("drop", e => {
  const from = e.dataTransfer.getData("text/x-deck-key");
  endDrag();
  if (from) { e.preventDefault(); clearKey(from); }
});
$("search").addEventListener("input", renderList);

async function loadEntities() {
  $("refresh").disabled = true;
  $("list").replaceChildren(Object.assign(document.createElement("div"),
    {className: "empty-note", textContent: "Asking Home Assistant…"}));
  try {
    entities = await api("/api/entities");
    entities.sort((a, b) => a[1].localeCompare(b[1]));
    names = new Map(entities.map(e => [e[0], e[1]]));
    const options = $("entity-options");
    options.replaceChildren(...entities.map(e => Object.assign(document.createElement("option"),
      {value: e[0], label: e[1]})));
  } catch (err) {
    toast("Could not load entities: " + err.message);
  }
  $("refresh").disabled = false;
  renderChips();
  renderList();
  renderDeck();
}
$("refresh").addEventListener("click", loadEntities);

async function poll() {
  if (dragging) return;
  try {
    const data = await api("/api/config");
    // Don't let a poll overwrite an edit that has not reached the deck yet.
    takeDevice(data, !saveTimer && !saving);
  } catch (err) {
    if (device) { device.ha_connected = false; renderStatus(); }
  }
}

function exportConfig() {
  const lines = [];
  for (let x = 0; x < device.cols; x++) {
    for (let y = device.rows - 1; y >= 0; y--) {
      const cell = layout.get(key(x, y));
      if (!cell) continue;
      const [r, g, b] = [1, 3, 5].map(i => parseInt(cell.color.substr(i, 2), 16));
      lines.push("    {\"" + cell.entity_id + "\", " + x + ", " + y + ", " + r + ", " + g + ", " + b +
                 ", " + cell.brightness + "},  // " + nameOf(cell.entity_id));
    }
  }
  $("export-text").value = "const EntityMapping entityMappings[] = {\n" + lines.join("\n") + "\n};\n";
  $("export-dialog").showModal();
}
$("export").addEventListener("click", exportConfig);
$("export-close").addEventListener("click", () => $("export-dialog").close());
$("export-copy").addEventListener("click", async () => {
  const text = $("export-text");
  try { await navigator.clipboard.writeText(text.value); }
  catch (err) { text.select(); document.execCommand("copy"); }
  toast("Copied");
});

$("reset").addEventListener("click", async () => {
  if (!confirm("Throw away the layout saved on the deck and go back to config.h?")) return;
  clearTimeout(saveTimer);
  saveTimer = null;
  try { takeDevice(await api("/api/reset", {}), true); toast("Back to config.h"); }
  catch (err) { toast("Reset failed: " + err.message); }
});

document.addEventListener("keydown", e => {
  if (e.key === "Escape") { armed = null; selected = null; renderList(); renderDeck(); renderEditor(); }
});

poll().then(loadEntities);
setInterval(() => { if (!document.hidden) poll(); }, 3000);
</script>
</body>
</html>
)rawliteral";

#endif // WEB_UI_PAGE_H

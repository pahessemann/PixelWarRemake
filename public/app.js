"use strict";

const state = {
  token: localStorage.getItem("pixelwar.token") || "",
  username: localStorage.getItem("pixelwar.username") || "",
  mode: "login",
  palette: [],
  selectedColor: 0,
  width: 1,
  height: 1,
  pixels: new Uint8Array(1),
  imageData: null,
  sequence: 0,
  cooldownTotal: 600,
  quota: 3,
  placementsRemaining: 0,
  readyAt: 0,
  zoom: 1,
  hoverCell: null,
  targetCell: null,
  camera: {
    x: 0,
    y: 0
  },
  pan: {
    active: false,
    dragged: false,
    pointerId: null,
    startX: 0,
    startY: 0,
    startCameraX: 0,
    startCameraY: 0
  }
};

const els = {
  connectionLabel: document.getElementById("connectionLabel"),
  mapSize: document.getElementById("mapSize"),
  sequenceValue: document.getElementById("sequenceValue"),
  refreshButton: document.getElementById("refreshButton"),
  logoutButton: document.getElementById("logoutButton"),
  authConnected: document.getElementById("authConnected"),
  connectedUsername: document.getElementById("connectedUsername"),
  authTabs: document.getElementById("authTabs"),
  loginMode: document.getElementById("loginMode"),
  registerMode: document.getElementById("registerMode"),
  authForm: document.getElementById("authForm"),
  authSubmit: document.getElementById("authSubmit"),
  usernameInput: document.getElementById("usernameInput"),
  passwordInput: document.getElementById("passwordInput"),
  paletteGrid: document.getElementById("paletteGrid"),
  selectedColorLabel: document.getElementById("selectedColorLabel"),
  targetCellLabel: document.getElementById("targetCellLabel"),
  quotaLabel: document.getElementById("quotaLabel"),
  placeButton: document.getElementById("placeButton"),
  readyLabel: document.getElementById("readyLabel"),
  cooldownTime: document.getElementById("cooldownTime"),
  cooldownBar: document.getElementById("cooldownBar"),
  cursorX: document.getElementById("cursorX"),
  cursorY: document.getElementById("cursorY"),
  cursorColor: document.getElementById("cursorColor"),
  zoomOutButton: document.getElementById("zoomOutButton"),
  zoomInButton: document.getElementById("zoomInButton"),
  zoomLabel: document.getElementById("zoomLabel"),
  canvasStage: document.getElementById("canvasStage"),
  canvas: document.getElementById("pixelCanvas"),
  highlightCanvas: document.getElementById("highlightCanvas"),
  canvasFrame: document.getElementById("canvasFrame"),
  loadingOverlay: document.getElementById("loadingOverlay"),
  statusText: document.getElementById("statusText"),
  lastRefresh: document.getElementById("lastRefresh")
};

const ctx = els.canvas.getContext("2d", { alpha: false });
const highlightCtx = els.highlightCanvas.getContext("2d");

function setStatus(text) {
  els.statusText.textContent = text;
}

function setMode(mode) {
  state.mode = mode;
  els.loginMode.classList.toggle("active", mode === "login");
  els.registerMode.classList.toggle("active", mode === "register");
  els.authSubmit.textContent = mode === "login" ? "Login" : "Register";
  els.passwordInput.autocomplete = mode === "login" ? "current-password" : "new-password";
}

function updateAuthUi() {
  if (state.token) {
    const username = state.username || "Connecte";
    els.connectionLabel.textContent = `Connecté : ${username}`;
    els.connectedUsername.textContent = username;
    els.authConnected.classList.remove("hidden");
    els.authTabs.classList.add("hidden");
    els.authForm.classList.add("hidden");
    els.logoutButton.classList.remove("hidden");
  } else {
    els.connectionLabel.textContent = "Invite";
    els.connectedUsername.textContent = "-";
    els.authConnected.classList.add("hidden");
    els.authTabs.classList.remove("hidden");
    els.authForm.classList.remove("hidden");
    els.logoutButton.classList.add("hidden");
    els.authSubmit.disabled = false;
    els.usernameInput.disabled = false;
    els.passwordInput.disabled = false;
  }
}

async function api(path, options = {}) {
  const headers = Object.assign({}, options.headers || {});
  if (options.body && !headers["Content-Type"]) {
    headers["Content-Type"] = "application/json";
  }
  if (state.token && !headers.Authorization) {
    headers.Authorization = `Bearer ${state.token}`;
  }

  const response = await fetch(path, Object.assign({}, options, { headers }));
  let payload = null;
  const text = await response.text();
  if (text) {
    try {
      payload = JSON.parse(text);
    } catch {
      payload = { raw: text };
    }
  }
  if (!response.ok) {
    const error = new Error(payload && payload.error ? payload.error : `http_${response.status}`);
    error.status = response.status;
    error.payload = payload;
    throw error;
  }
  return payload;
}

function base64ToBytes(encoded) {
  const binary = atob(encoded);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i += 1) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

function decodeRle(encoded, totalPixels) {
  const bytes = base64ToBytes(encoded);
  const output = new Uint8Array(totalPixels);
  let out = 0;

  for (let i = 0; i + 4 < bytes.length; i += 5) {
    const color = bytes[i];
    const count = bytes[i + 1] |
      (bytes[i + 2] << 8) |
      (bytes[i + 3] << 16) |
      (bytes[i + 4] << 24);
    output.fill(color, out, out + count);
    out += count;
  }

  return output;
}

function hexToRgb(hex) {
  const value = hex.replace("#", "");
  return {
    r: parseInt(value.slice(0, 2), 16),
    g: parseInt(value.slice(2, 4), 16),
    b: parseInt(value.slice(4, 6), 16)
  };
}

function colorFor(index) {
  return state.palette[index] || { id: index, hex: "#000000", rgb: { r: 0, g: 0, b: 0 } };
}

function writeImagePixel(index) {
  const color = colorFor(state.pixels[index]).rgb;
  const offset = index * 4;
  state.imageData.data[offset] = color.r;
  state.imageData.data[offset + 1] = color.g;
  state.imageData.data[offset + 2] = color.b;
  state.imageData.data[offset + 3] = 255;
}

function renderFullMap() {
  state.imageData = ctx.createImageData(state.width, state.height);
  for (let i = 0; i < state.pixels.length; i += 1) {
    writeImagePixel(i);
  }
  ctx.putImageData(state.imageData, 0, 0);
  renderHighlights();
}

function applyDiff(changes) {
  if (!state.imageData) {
    renderFullMap();
    return;
  }
  for (const change of changes) {
    const index = change.y * state.width + change.x;
    if (index < 0 || index >= state.pixels.length) {
      continue;
    }
    state.pixels[index] = change.color;
    writeImagePixel(index);
  }
  ctx.putImageData(state.imageData, 0, 0);
  renderHighlights();
}

function resizeCanvas(width, height) {
  const wasInitialMap = state.width === 1 && state.height === 1 && state.sequence === 0;
  const viewCell = wasInitialMap
    ? { x: Math.floor(width / 2), y: Math.floor(height / 2) }
    : visibleCenterCell();

  state.width = width;
  state.height = height;
  state.hoverCell = null;
  state.targetCell = null;
  updatePlaceButton();
  els.canvas.width = width;
  els.canvas.height = height;
  els.highlightCanvas.width = width;
  els.highlightCanvas.height = height;
  els.mapSize.textContent = `${width} x ${height}`;
  applyZoom();
  centerCellInView({
    x: clamp(viewCell.x, 0, width - 1),
    y: clamp(viewCell.y, 0, height - 1)
  });
}

async function loadPalette() {
  const payload = await api("/palette");
  state.palette = payload.colors.map((entry) => Object.assign({}, entry, { rgb: hexToRgb(entry.hex) }));
  renderPalette();
}

function renderPalette() {
  els.paletteGrid.innerHTML = "";
  for (const color of state.palette) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "swatch";
    button.style.background = color.hex;
    button.title = `Couleur ${color.id}`;
    button.setAttribute("aria-label", `Couleur ${color.id}`);
    button.dataset.color = String(color.id);
    button.addEventListener("click", () => selectColor(color.id, true));
    els.paletteGrid.appendChild(button);
  }
  selectColor(state.selectedColor, false);
}

function selectColor(color, shouldAnnounce = false) {
  state.selectedColor = color;
  els.selectedColorLabel.textContent = String(color);
  for (const button of els.paletteGrid.querySelectorAll(".swatch")) {
    button.classList.toggle("active", Number(button.dataset.color) === color);
  }
  renderHighlights();

  if (shouldAnnounce) {
    setStatus(`Couleur ${color} selectionnee`);
  }
  updatePlaceButton();
}

async function loadMap(forceFull = false) {
  const since = forceFull ? "" : `?since=${state.sequence}`;
  const payload = await api(`/map${since}`);

  if (payload.type === "full") {
    resizeCanvas(payload.width, payload.height);
    state.pixels = decodeRle(payload.data, payload.width * payload.height);
    state.sequence = payload.sequence;
    renderFullMap();
  } else if (payload.type === "diff") {
    applyDiff(payload.changes || []);
    state.sequence = payload.sequence;
  }

  els.sequenceValue.textContent = String(state.sequence);
  els.loadingOverlay.classList.add("hidden");
  els.lastRefresh.textContent = new Date().toLocaleTimeString();
}

async function refreshMap() {
  try {
    setStatus("Refresh map");
    await loadMap(false);
    setStatus("Carte a jour");
  } catch (error) {
    setStatus(`Map: ${error.message}`);
  }
}

function formatSeconds(seconds) {
  const safe = Math.max(0, Math.ceil(seconds));
  const minutes = Math.floor(safe / 60);
  const rest = safe % 60;
  return `${String(minutes).padStart(2, "0")}:${String(rest).padStart(2, "0")}`;
}

function updateCooldownUi() {
  const remaining = state.readyAt ? Math.max(0, Math.ceil((state.readyAt - Date.now()) / 1000)) : 0;
  els.cooldownTime.textContent = formatSeconds(remaining);
  const progress = state.cooldownTotal <= 0 ? 100 : 100 - (remaining / state.cooldownTotal) * 100;
  els.cooldownBar.style.width = `${Math.max(0, Math.min(100, progress))}%`;
  els.quotaLabel.textContent = `${state.placementsRemaining}/${state.quota}`;
  els.readyLabel.textContent = state.placementsRemaining > 0 ? `${state.placementsRemaining}/${state.quota}` : "Wait";
  els.readyLabel.classList.toggle("ready", state.placementsRemaining > 0);
  els.readyLabel.classList.toggle("blocked", state.placementsRemaining === 0);
  updatePlaceButton();
}

async function refreshCooldown() {
  if (!state.token) {
    state.readyAt = 0;
    updateCooldownUi();
    return;
  }

  try {
    const payload = await api("/cooldown");
    const remaining = payload.remaining_seconds || 0;
    state.cooldownTotal = payload.cooldown_seconds || state.cooldownTotal;
    state.quota = payload.quota || state.quota;
    state.placementsRemaining = payload.placements_remaining || 0;
    state.readyAt = Date.now() + remaining * 1000;
    updateCooldownUi();
  } catch (error) {
    if (error.status === 401) {
      logout();
    }
    setStatus(`Cooldown: ${error.message}`);
  }
}

async function submitAuth(event) {
  event.preventDefault();
  const username = els.usernameInput.value.trim();
  const password = els.passwordInput.value;
  if (!username || !password) {
    return;
  }

  try {
    els.authSubmit.disabled = true;
    setStatus(state.mode === "login" ? "Login" : "Register");

    if (state.mode === "register") {
      await api("/register", {
        method: "POST",
        body: JSON.stringify({ username, password })
      });
    }

    const login = await api("/login", {
      method: "POST",
      body: JSON.stringify({ username, password })
    });

    state.token = login.token;
    state.username = username;
    localStorage.setItem("pixelwar.token", state.token);
    localStorage.setItem("pixelwar.username", state.username);
    els.passwordInput.value = "";
    updateAuthUi();
    await refreshCooldown();
    setStatus("Connecte");
  } catch (error) {
    els.authSubmit.disabled = false;
    setStatus(`Compte: ${error.message}`);
  }
}

function logout() {
  state.token = "";
  state.username = "";
  localStorage.removeItem("pixelwar.token");
  localStorage.removeItem("pixelwar.username");
  state.placementsRemaining = 0;
  state.targetCell = null;
  updateAuthUi();
  updatePlaceButton();
  refreshCooldown();
  setStatus("Session fermee");
}

function updatePlaceButton() {
  const hasTarget = Boolean(state.targetCell);
  els.targetCellLabel.textContent = hasTarget ? `${state.targetCell.x},${state.targetCell.y}` : "-";
  els.quotaLabel.textContent = `${state.placementsRemaining}/${state.quota}`;
  els.placeButton.disabled = !state.token || !hasTarget || state.placementsRemaining === 0;
}

function canvasPointFromClient(clientX, clientY) {
  const rect = els.canvasFrame.getBoundingClientRect();
  if (state.zoom <= 0 || rect.width <= 0 || rect.height <= 0) {
    return null;
  }

  const x = Math.floor((clientX - rect.left - state.camera.x) / state.zoom);
  const y = Math.floor((clientY - rect.top - state.camera.y) / state.zoom);
  if (x < 0 || y < 0 || x >= state.width || y >= state.height) {
    return null;
  }
  return { x, y };
}

function canvasPoint(event) {
  return canvasPointFromClient(event.clientX, event.clientY);
}

function updateCursorFromClient(clientX, clientY) {
  const point = canvasPointFromClient(clientX, clientY);
  if (!point) {
    state.hoverCell = null;
    renderHighlights();
    return;
  }
  const index = point.y * state.width + point.x;
  state.hoverCell = point;
  els.cursorX.textContent = String(point.x);
  els.cursorY.textContent = String(point.y);
  els.cursorColor.textContent = String(state.pixels[index]);
  renderHighlights();
}

function updateCursor(event) {
  updateCursorFromClient(event.clientX, event.clientY);
}

function selectTargetCellFromClient(clientX, clientY) {
  const point = canvasPointFromClient(clientX, clientY);
  if (!point) {
    return;
  }
  state.targetCell = point;
  renderHighlights();
  updatePlaceButton();
  setStatus(`Case ${point.x},${point.y} selectionnee`);
}

function selectTargetCell(event) {
  selectTargetCellFromClient(event.clientX, event.clientY);
}

function beginViewportDrag(event) {
  if (event.button !== 0) {
    return;
  }

  event.preventDefault();
  state.pan.active = true;
  state.pan.dragged = false;
  state.pan.pointerId = event.pointerId;
  state.pan.startX = event.clientX;
  state.pan.startY = event.clientY;
  state.pan.startCameraX = state.camera.x;
  state.pan.startCameraY = state.camera.y;

  updateCursor(event);
  els.canvasFrame.setPointerCapture(event.pointerId);
}

function moveViewportDrag(event) {
  if (!state.pan.active || state.pan.pointerId !== event.pointerId) {
    updateCursor(event);
    return;
  }

  const dx = event.clientX - state.pan.startX;
  const dy = event.clientY - state.pan.startY;
  if (Math.abs(dx) > 4 || Math.abs(dy) > 4) {
    state.pan.dragged = true;
    els.canvasFrame.classList.add("panning");
  }

  if (state.pan.dragged) {
    event.preventDefault();
    state.hoverCell = null;
    setCamera(state.pan.startCameraX + dx, state.pan.startCameraY + dy);
    return;
  }

  updateCursor(event);
}

function endViewportDrag(event) {
  if (!state.pan.active || state.pan.pointerId !== event.pointerId) {
    return;
  }

  const wasDragged = state.pan.dragged;
  state.pan.active = false;
  state.pan.dragged = false;
  state.pan.pointerId = null;
  els.canvasFrame.classList.remove("panning");

  if (els.canvasFrame.hasPointerCapture(event.pointerId)) {
    els.canvasFrame.releasePointerCapture(event.pointerId);
  }

  if (!wasDragged) {
    selectTargetCellFromClient(event.clientX, event.clientY);
    updateCursor(event);
  }
}

function cancelViewportDrag(event) {
  if (state.pan.pointerId !== event.pointerId) {
    return;
  }

  state.pan.active = false;
  state.pan.dragged = false;
  state.pan.pointerId = null;
  els.canvasFrame.classList.remove("panning");
  state.hoverCell = null;
  renderHighlights();
}

async function placeSelectedPixel(color) {
  if (!state.targetCell) {
    setStatus("Selectionne une case");
    return;
  }
  if (!state.token) {
    setStatus("Login requis");
    return;
  }

  const point = state.targetCell;

  try {
    const payload = await api("/pixel", {
      method: "POST",
      body: JSON.stringify({
        x: point.x,
        y: point.y,
        color
      })
    });

    const index = point.y * state.width + point.x;
    state.pixels[index] = color;
    writeImagePixel(index);
    ctx.putImageData(state.imageData, 0, 0);
    state.sequence = payload.sequence;
    state.cooldownTotal = payload.cooldown_seconds || state.cooldownTotal;
    state.quota = payload.quota || state.quota;
    state.placementsRemaining = typeof payload.placements_remaining === "number" ? payload.placements_remaining : state.placementsRemaining;
    const remainingSeconds = typeof payload.remaining_seconds === "number" ? payload.remaining_seconds : state.cooldownTotal;
    state.readyAt = Date.now() + remainingSeconds * 1000;
    els.sequenceValue.textContent = String(state.sequence);
    state.targetCell = null;
    updateCooldownUi();
    renderHighlights();
    setStatus(`Pixel ${point.x},${point.y}`);
  } catch (error) {
    if (error.payload && typeof error.payload.remaining_seconds === "number") {
      state.quota = error.payload.quota || state.quota;
      state.placementsRemaining = typeof error.payload.placements_remaining === "number" ? error.payload.placements_remaining : 0;
      state.readyAt = Date.now() + error.payload.remaining_seconds * 1000;
      updateCooldownUi();
    }
    setStatus(`Pixel: ${error.message}`);
  }
}

function scaledMapWidth() {
  return Math.max(1, state.width * state.zoom);
}

function scaledMapHeight() {
  return Math.max(1, state.height * state.zoom);
}

function viewportSize() {
  return {
    width: Math.max(1, els.canvasFrame.clientWidth),
    height: Math.max(1, els.canvasFrame.clientHeight)
  };
}

function borderLimit() {
  const viewport = viewportSize();
  return Math.max(24, Math.min(96, Math.floor(Math.min(viewport.width, viewport.height) * 0.12)));
}

function clampCameraAxis(position, mapSize, viewportLength, limit) {
  if (mapSize <= viewportLength - limit * 2) {
    return Math.round((viewportLength - mapSize) / 2);
  }

  return clamp(position, viewportLength - limit - mapSize, limit);
}

function clampCamera() {
  const viewport = viewportSize();
  const limit = borderLimit();
  state.camera.x = clampCameraAxis(state.camera.x, scaledMapWidth(), viewport.width, limit);
  state.camera.y = clampCameraAxis(state.camera.y, scaledMapHeight(), viewport.height, limit);
}

function applyViewport() {
  els.canvasStage.style.width = `${state.width}px`;
  els.canvasStage.style.height = `${state.height}px`;
  els.canvasStage.style.transform = `translate3d(${state.camera.x}px, ${state.camera.y}px, 0) scale(${state.zoom})`;
  els.canvas.style.width = `${state.width}px`;
  els.canvas.style.height = `${state.height}px`;
  els.highlightCanvas.style.width = `${state.width}px`;
  els.highlightCanvas.style.height = `${state.height}px`;
  els.zoomLabel.textContent = `${Math.round(state.zoom * 100)}%`;
  renderHighlights();
}

function setCamera(x, y) {
  state.camera.x = x;
  state.camera.y = y;
  clampCamera();
  applyViewport();
}

function applyZoom() {
  clampCamera();
  applyViewport();
}

function zoomValue(nextZoom) {
  return Math.max(0.25, Math.min(12, Number(nextZoom.toFixed(3))));
}

function setZoom(nextZoom) {
  state.zoom = zoomValue(nextZoom);
  applyZoom();
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function centerCellInView(cell) {
  if (!cell) {
    return;
  }

  const viewport = viewportSize();
  const focusX = (cell.x + 0.5) * state.zoom;
  const focusY = (cell.y + 0.5) * state.zoom;
  setCamera(viewport.width / 2 - focusX, viewport.height / 2 - focusY);
}

function zoomToCell(cell, nextZoom, anchor = {}) {
  const viewport = viewportSize();
  const focus = cell || visibleCenterCell();
  const previousZoom = state.zoom;
  const next = zoomValue(nextZoom);
  if (next === previousZoom) {
    return;
  }

  const anchorX = anchor.center ? viewport.width / 2 : clamp(anchor.x ?? viewport.width / 2, 0, viewport.width);
  const anchorY = anchor.center ? viewport.height / 2 : clamp(anchor.y ?? viewport.height / 2, 0, viewport.height);
  state.zoom = next;
  state.camera.x = anchorX - (focus.x + 0.5) * state.zoom;
  state.camera.y = anchorY - (focus.y + 0.5) * state.zoom;
  clampCamera();
  applyViewport();
}

function changeZoom(delta) {
  zoomToCell(state.targetCell || visibleCenterCell(), state.zoom + delta, { center: true });
}

function zoomWithWheel(event) {
  event.preventDefault();
  const cell = state.targetCell || canvasPoint(event) || state.hoverCell || visibleCenterCell();
  const rect = els.canvasFrame.getBoundingClientRect();
  const factor = event.deltaY < 0 ? 1.18 : 1 / 1.18;
  zoomToCell(cell, state.zoom * factor, {
    center: Boolean(state.targetCell),
    x: event.clientX - rect.left,
    y: event.clientY - rect.top
  });
}

function visibleCenterCell() {
  const viewport = viewportSize();
  return {
    x: clamp(Math.floor((viewport.width / 2 - state.camera.x) / state.zoom), 0, state.width - 1),
    y: clamp(Math.floor((viewport.height / 2 - state.camera.y) / state.zoom), 0, state.height - 1)
  };
}

function refreshViewportAroundCurrentCell() {
  const cell = state.targetCell || visibleCenterCell();
  applyZoom();
  centerCellInView(cell);
}

function sameCell(a, b) {
  return Boolean(a && b && a.x === b.x && a.y === b.y);
}

function renderHighlights() {
  highlightCtx.clearRect(0, 0, state.width, state.height);

  if (state.hoverCell && !sameCell(state.hoverCell, state.targetCell)) {
    highlightCtx.fillStyle = "rgba(255, 255, 255, 0.38)";
    highlightCtx.fillRect(state.hoverCell.x, state.hoverCell.y, 1, 1);
  }

  if (state.targetCell) {
    const selected = colorFor(state.selectedColor);
    highlightCtx.fillStyle = selected.hex;
    highlightCtx.fillRect(state.targetCell.x, state.targetCell.y, 1, 1);
    highlightCtx.strokeStyle = "#f4c95d";
    highlightCtx.lineWidth = 1;
    highlightCtx.strokeRect(state.targetCell.x + 0.05, state.targetCell.y + 0.05, 0.9, 0.9);
  }
}

async function init() {
  updateAuthUi();
  setMode("login");
  updateCooldownUi();

  els.loginMode.addEventListener("click", () => setMode("login"));
  els.registerMode.addEventListener("click", () => setMode("register"));
  els.authForm.addEventListener("submit", submitAuth);
  els.logoutButton.addEventListener("click", logout);
  els.refreshButton.addEventListener("click", refreshMap);
  els.placeButton.addEventListener("click", () => placeSelectedPixel(state.selectedColor));
  els.canvasFrame.addEventListener("pointerdown", beginViewportDrag);
  els.canvasFrame.addEventListener("pointermove", moveViewportDrag);
  els.canvasFrame.addEventListener("pointerup", endViewportDrag);
  els.canvasFrame.addEventListener("pointercancel", cancelViewportDrag);
  els.canvasFrame.addEventListener("mouseleave", () => {
    if (state.pan.active) {
      return;
    }
    state.hoverCell = null;
    renderHighlights();
  });
  els.canvasFrame.addEventListener("wheel", zoomWithWheel, { passive: false });
  els.zoomOutButton.addEventListener("click", () => changeZoom(-0.25));
  els.zoomInButton.addEventListener("click", () => changeZoom(0.25));
  window.addEventListener("resize", refreshViewportAroundCurrentCell);

  try {
    await loadPalette();
    await loadMap(true);
    await refreshCooldown();
    setStatus("Pret");
  } catch (error) {
    setStatus(`Init: ${error.message}`);
  }

  window.setInterval(refreshMap, 60000);
  window.setInterval(updateCooldownUi, 1000);
  window.setInterval(refreshCooldown, 30000);
}

init();

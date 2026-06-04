"use strict";

const state = {
  token: localStorage.getItem("pixelwar.token") || "",
  username: localStorage.getItem("pixelwar.username") || "",
  authMode: "login",
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
  eventSource: null,
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
  localAuth: document.getElementById("localAuth"),
  loginTab: document.getElementById("loginTab"),
  registerTab: document.getElementById("registerTab"),
  authForm: document.getElementById("authForm"),
  loginLabel: document.getElementById("loginLabel"),
  loginInput: document.getElementById("loginInput"),
  emailField: document.getElementById("emailField"),
  emailInput: document.getElementById("emailInput"),
  passwordInput: document.getElementById("passwordInput"),
  authSubmitButton: document.getElementById("authSubmitButton"),
  verificationNotice: document.getElementById("verificationNotice"),
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
  lastRefresh: document.getElementById("lastRefresh"),
  miniMapCanvas: document.getElementById("miniMapCanvas"),
  miniMapCenterLabel: document.getElementById("miniMapCenterLabel"),
  historyList: document.getElementById("historyList")
};

const ctx = els.canvas.getContext("2d", { alpha: false });
const highlightCtx = els.highlightCanvas.getContext("2d");
const miniCtx = els.miniMapCanvas.getContext("2d", { alpha: false });

function setStatus(text) {
  els.statusText.textContent = text;
}

function hideVerificationNotice() {
  els.verificationNotice.classList.add("hidden");
  els.verificationNotice.replaceChildren();
}

function showVerificationNotice(payload) {
  els.verificationNotice.replaceChildren();
  const text = document.createElement("span");
  text.textContent = "Email a verifier avant connexion.";
  els.verificationNotice.appendChild(text);

  if (payload.verification_link) {
    const link = document.createElement("a");
    link.href = payload.verification_link;
    link.textContent = "Ouvrir le lien de verification";
    link.target = "_blank";
    link.rel = "noopener";
    link.className = "auth-verify-link";
    els.verificationNotice.appendChild(link);
  }

  els.verificationNotice.classList.remove("hidden");
}

function updateAuthUi() {
  if (state.token) {
    const username = state.username || "Connecte";
    els.connectionLabel.textContent = `Connecté : ${username}`;
    els.connectedUsername.textContent = username;
    els.authConnected.classList.remove("hidden");
    els.localAuth.classList.add("hidden");
    els.logoutButton.classList.remove("hidden");
  } else {
    els.connectionLabel.textContent = "Invite";
    els.connectedUsername.textContent = "-";
    els.authConnected.classList.add("hidden");
    els.localAuth.classList.remove("hidden");
    els.logoutButton.classList.add("hidden");
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
  renderMiniMap();
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
  renderMiniMap();
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

function applyMapPayload(payload) {
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

async function loadMap(forceFull = false) {
  const since = forceFull ? "" : `?since=${state.sequence}`;
  const payload = await api(`/map${since}`);
  applyMapPayload(payload);
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

function renderHistory(entries) {
  els.historyList.replaceChildren();
  if (!entries.length) {
    const empty = document.createElement("span");
    empty.className = "history-empty";
    empty.textContent = "Aucun pixel";
    els.historyList.appendChild(empty);
    return;
  }

  for (const entry of entries.slice().reverse()) {
    const row = document.createElement("button");
    row.type = "button";
    row.className = "history-row";
    row.addEventListener("click", () => centerCellInView({ x: entry.x, y: entry.y }));

    const swatch = document.createElement("span");
    swatch.className = "history-swatch";
    swatch.style.background = colorFor(entry.color).hex;

    const label = document.createElement("span");
    label.textContent = `${entry.x},${entry.y}`;

    const user = document.createElement("strong");
    user.textContent = entry.username || "-";

    row.append(swatch, label, user);
    els.historyList.appendChild(row);
  }
}

async function loadHistory() {
  try {
    const payload = await api("/history?limit=24");
    renderHistory(payload.pixels || []);
  } catch (error) {
    setStatus(`Historique: ${error.message}`);
  }
}

function connectEvents() {
  if (!("EventSource" in window)) {
    return;
  }
  if (state.eventSource) {
    state.eventSource.close();
  }

  state.eventSource = new EventSource(`/events?since=${state.sequence}`);
  state.eventSource.addEventListener("pixels", (event) => {
    try {
      const payload = JSON.parse(event.data);
      applyMapPayload(payload);
      if (payload.type === "diff" && payload.changes && payload.changes.length) {
        loadHistory();
      }
    } catch {
      setStatus("Temps reel: payload invalide");
    }
  });
  state.eventSource.onerror = () => {
    setStatus("Temps reel: reconnexion");
  };
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

function setAuthMode(mode) {
  state.authMode = mode === "register" ? "register" : "login";
  const isRegister = state.authMode === "register";
  els.loginTab.classList.toggle("active", !isRegister);
  els.registerTab.classList.toggle("active", isRegister);
  els.loginTab.setAttribute("aria-selected", String(!isRegister));
  els.registerTab.setAttribute("aria-selected", String(isRegister));
  els.emailField.classList.toggle("hidden", !isRegister);
  els.emailInput.required = isRegister;
  els.loginLabel.textContent = isRegister ? "Pseudo" : "Pseudo ou email";
  els.loginInput.name = isRegister ? "username" : "login";
  els.loginInput.autocomplete = isRegister ? "username" : "username";
  els.passwordInput.autocomplete = isRegister ? "new-password" : "current-password";
  els.authSubmitButton.textContent = isRegister ? "Register" : "Login";
  hideVerificationNotice();
}

async function submitAuthForm(event) {
  event.preventDefault();
  const login = els.loginInput.value.trim();
  const email = els.emailInput.value.trim();
  const password = els.passwordInput.value;
  if (!login || !password || (state.authMode === "register" && !email)) {
    setStatus("Compte: champs manquants");
    return;
  }

  els.authSubmitButton.disabled = true;
  try {
    const payload = state.authMode === "register"
      ? await api("/register", {
          method: "POST",
          body: JSON.stringify({ username: login, email, password })
        })
      : await api("/login", {
          method: "POST",
          body: JSON.stringify({ login, password })
        });

    els.passwordInput.value = "";
    if (payload.verification_required) {
      state.token = "";
      state.username = "";
      localStorage.removeItem("pixelwar.token");
      localStorage.removeItem("pixelwar.username");
      updateAuthUi();
      showVerificationNotice(payload);
      setStatus("Verification email requise");
      return;
    }

    state.token = payload.token || "";
    state.username = payload.username || login;
    localStorage.setItem("pixelwar.token", state.token);
    localStorage.setItem("pixelwar.username", state.username);
    updateAuthUi();
    await refreshCooldown();
    setStatus(state.authMode === "register" ? "Compte cree" : "Connecte");
  } catch (error) {
    if (error.message === "email_not_verified") {
      setStatus("Email non verifie");
      return;
    }
    setStatus(`Compte: ${error.message}`);
  } finally {
    els.authSubmitButton.disabled = false;
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
    await loadHistory();
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
  renderMiniMap();
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

function renderMiniMap() {
  if (!state.palette.length || !state.pixels.length || state.width <= 1 || state.height <= 1) {
    return;
  }

  const size = els.miniMapCanvas.width;
  const image = miniCtx.createImageData(size, size);
  for (let py = 0; py < size; py += 1) {
    const sourceY = clamp(Math.floor((py / size) * state.height), 0, state.height - 1);
    for (let px = 0; px < size; px += 1) {
      const sourceX = clamp(Math.floor((px / size) * state.width), 0, state.width - 1);
      const color = colorFor(state.pixels[sourceY * state.width + sourceX]).rgb;
      const offset = (py * size + px) * 4;
      image.data[offset] = color.r;
      image.data[offset + 1] = color.g;
      image.data[offset + 2] = color.b;
      image.data[offset + 3] = 255;
    }
  }
  miniCtx.putImageData(image, 0, 0);

  const viewport = viewportSize();
  const left = clamp((0 - state.camera.x) / state.zoom, 0, state.width);
  const top = clamp((0 - state.camera.y) / state.zoom, 0, state.height);
  const right = clamp((viewport.width - state.camera.x) / state.zoom, 0, state.width);
  const bottom = clamp((viewport.height - state.camera.y) / state.zoom, 0, state.height);
  const sx = size / state.width;
  const sy = size / state.height;
  miniCtx.strokeStyle = "#f4c95d";
  miniCtx.lineWidth = 2;
  miniCtx.strokeRect(left * sx, top * sy, Math.max(4, (right - left) * sx), Math.max(4, (bottom - top) * sy));

  const center = visibleCenterCell();
  els.miniMapCenterLabel.textContent = `${center.x},${center.y}`;
}

function centerFromMiniMap(event) {
  const rect = els.miniMapCanvas.getBoundingClientRect();
  const x = clamp(Math.floor(((event.clientX - rect.left) / rect.width) * state.width), 0, state.width - 1);
  const y = clamp(Math.floor(((event.clientY - rect.top) / rect.height) * state.height), 0, state.height - 1);
  centerCellInView({ x, y });
  setStatus(`Vue ${x},${y}`);
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
  const authError = new URLSearchParams(window.location.search).get("auth_error");
  if (authError) {
    setStatus(`Auth: ${authError}`);
    window.history.replaceState({}, document.title, window.location.pathname);
  }

  updateAuthUi();
  updateCooldownUi();
  setAuthMode("login");

  els.loginTab.addEventListener("click", () => setAuthMode("login"));
  els.registerTab.addEventListener("click", () => setAuthMode("register"));
  els.authForm.addEventListener("submit", submitAuthForm);
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
  els.miniMapCanvas.addEventListener("click", centerFromMiniMap);
  window.addEventListener("resize", refreshViewportAroundCurrentCell);

  try {
    await loadPalette();
    await loadMap(true);
    await loadHistory();
    connectEvents();
    await refreshCooldown();
    if (!authError) {
      setStatus("Pret");
    }
  } catch (error) {
    setStatus(`Init: ${error.message}`);
  }

  window.setInterval(refreshMap, 60000);
  window.setInterval(loadHistory, 60000);
  window.setInterval(updateCooldownUi, 1000);
  window.setInterval(refreshCooldown, 30000);
}

init();

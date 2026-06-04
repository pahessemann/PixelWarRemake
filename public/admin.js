"use strict";

const adminState = {
  token: localStorage.getItem("pixelwar.token") || ""
};

const adminEls = {
  status: document.getElementById("adminStatus"),
  name: document.getElementById("adminName"),
  notice: document.getElementById("adminNotice"),
  refreshButton: document.getElementById("adminRefreshButton"),
  usersCount: document.getElementById("usersCount"),
  mapStats: document.getElementById("mapStats"),
  sequenceStats: document.getElementById("sequenceStats"),
  rulesStats: document.getElementById("rulesStats"),
  backupsCount: document.getElementById("backupsCount"),
  createBackupButton: document.getElementById("createBackupButton"),
  resetMapButton: document.getElementById("resetMapButton"),
  backupsTableBody: document.getElementById("backupsTableBody"),
  usersTableBody: document.getElementById("usersTableBody"),
  auditTableBody: document.getElementById("auditTableBody")
};

function setAdminStatus(text) {
  adminEls.status.textContent = text;
}

function showNotice(text, type = "info") {
  adminEls.notice.textContent = text;
  adminEls.notice.dataset.type = type;
  adminEls.notice.classList.remove("hidden");
}

function hideNotice() {
  adminEls.notice.classList.add("hidden");
}

async function adminApi(path, options = {}) {
  const headers = Object.assign({}, options.headers || {});
  if (options.body && !headers["Content-Type"]) {
    headers["Content-Type"] = "application/json";
  }
  if (adminState.token) {
    headers.Authorization = `Bearer ${adminState.token}`;
  }

  const response = await fetch(path, Object.assign({}, options, { headers }));
  const text = await response.text();
  let payload = null;
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

function formatTimestamp(timestamp) {
  if (!timestamp) {
    return "-";
  }

  return new Date(timestamp * 1000).toLocaleString();
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes)) {
    return "-";
  }
  if (bytes < 1024) {
    return `${bytes} o`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} Ko`;
  }
  return `${(bytes / (1024 * 1024)).toFixed(2)} Mo`;
}

function renderSummary(summary) {
  adminEls.name.textContent = `Admin ${summary.admin_username}`;
  adminEls.usersCount.textContent = String(summary.users_count);
  adminEls.mapStats.textContent = `${summary.map.width} x ${summary.map.height}`;
  adminEls.sequenceStats.textContent = `Seq ${summary.map.sequence}`;
  adminEls.rulesStats.textContent = `${summary.rules.pixel_quota_per_cooldown} px / ${summary.rules.cooldown_seconds}s`;
}

function renderBackups(backups) {
  adminEls.backupsCount.textContent = String(backups.length);
  adminEls.backupsTableBody.innerHTML = "";
  if (!backups.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 6;
    cell.textContent = "Aucun backup";
    row.appendChild(cell);
    adminEls.backupsTableBody.appendChild(row);
    return;
  }

  for (const backup of backups) {
    const row = document.createElement("tr");
    row.innerHTML = `
      <td></td>
      <td>${formatTimestamp(backup.created_at)}</td>
      <td></td>
      <td>Seq ${backup.sequence}</td>
      <td>${formatBytes(backup.bytes)}</td>
      <td></td>
    `;

    row.children[0].textContent = backup.id;
    row.children[2].textContent = backup.reason;

    const actions = document.createElement("div");
    actions.className = "admin-row-actions";

    const rollbackButton = document.createElement("button");
    rollbackButton.className = "ghost-button";
    rollbackButton.type = "button";
    rollbackButton.textContent = "Rollback";
    rollbackButton.addEventListener("click", () => rollbackBackup(backup.id));
    actions.appendChild(rollbackButton);

    if (backup.screenshot) {
      const screenshotButton = document.createElement("button");
      screenshotButton.className = "ghost-button";
      screenshotButton.type = "button";
      screenshotButton.textContent = "Screen";
      screenshotButton.addEventListener("click", () => openScreenshot(backup.id));
      actions.appendChild(screenshotButton);
    }

    row.children[5].appendChild(actions);
    adminEls.backupsTableBody.appendChild(row);
  }
}

function renderUsers(users) {
  adminEls.usersTableBody.innerHTML = "";
  if (!users.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 8;
    cell.textContent = "Aucun utilisateur";
    row.appendChild(cell);
    adminEls.usersTableBody.appendChild(row);
    return;
  }

  for (const user of users) {
    const row = document.createElement("tr");
    row.innerHTML = `
      <td>${user.id}</td>
      <td></td>
      <td></td>
      <td>${user.email_verified ? "Oui" : "Non"}</td>
      <td>${formatTimestamp(user.last_pixel_timestamp)}</td>
      <td>${formatTimestamp(user.pixel_window_start_timestamp)}</td>
      <td>${user.pixels_placed_in_window}</td>
      <td></td>
    `;

    row.children[1].textContent = user.username;
    row.children[2].textContent = user.email || "-";

    const button = document.createElement("button");
    button.className = "ghost-button";
    button.type = "button";
    button.textContent = "Reset cooldown";
    button.addEventListener("click", () => resetCooldown(user.id));
    row.children[7].appendChild(button);

    adminEls.usersTableBody.appendChild(row);
  }
}

function renderAudit(entries) {
  adminEls.auditTableBody.innerHTML = "";
  if (!entries.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.textContent = "Aucune action";
    row.appendChild(cell);
    adminEls.auditTableBody.appendChild(row);
    return;
  }

  for (const entry of entries.slice().reverse()) {
    const row = document.createElement("tr");
    row.innerHTML = `
      <td>${formatTimestamp(entry.timestamp)}</td>
      <td></td>
      <td></td>
      <td></td>
    `;
    row.children[1].textContent = entry.actor;
    row.children[2].textContent = entry.action;
    row.children[3].textContent = entry.detail;
    adminEls.auditTableBody.appendChild(row);
  }
}

async function createBackup() {
  try {
    setAdminStatus("Backup manuel");
    const payload = await adminApi("/admin/backups/create", {
      method: "POST",
      body: JSON.stringify({ reason: "manual", screenshot: true })
    });
    showNotice(`Backup ${payload.backup.id} cree`, "ok");
    await refreshAdmin();
  } catch (error) {
    showNotice(`Backup impossible: ${error.message}`, "error");
    setAdminStatus("Erreur");
  }
}

async function rollbackBackup(id) {
  if (!window.confirm(`Restaurer le backup ${id} ? Un backup de securite sera cree avant le rollback.`)) {
    return;
  }

  try {
    setAdminStatus("Rollback");
    await adminApi("/admin/backups/rollback", {
      method: "POST",
      body: JSON.stringify({ id })
    });
    showNotice(`Rollback ${id} applique`, "ok");
    await refreshAdmin();
  } catch (error) {
    showNotice(`Rollback impossible: ${error.message}`, "error");
    setAdminStatus("Erreur");
  }
}

async function resetMap() {
  if (!window.confirm("Reset complet de la carte ? Un backup et un screen final seront crees avant le reset.")) {
    return;
  }

  try {
    setAdminStatus("Reset carte");
    const payload = await adminApi("/admin/map/reset", {
      method: "POST",
      body: JSON.stringify({ color: 0 })
    });
    showNotice(`Carte reset. Backup final: ${payload.final_backup.id}`, "ok");
    await refreshAdmin();
  } catch (error) {
    showNotice(`Reset impossible: ${error.message}`, "error");
    setAdminStatus("Erreur");
  }
}

async function openScreenshot(id) {
  try {
    const response = await fetch(`/admin/backups/screenshot?id=${encodeURIComponent(id)}`, {
      headers: {
        Authorization: `Bearer ${adminState.token}`
      }
    });
    if (!response.ok) {
      throw new Error(`http_${response.status}`);
    }
    const blob = await response.blob();
    const url = URL.createObjectURL(blob);
    window.open(url, "_blank", "noopener");
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  } catch (error) {
    showNotice(`Screen inaccessible: ${error.message}`, "error");
  }
}

async function resetCooldown(userId) {
  try {
    setAdminStatus("Reset cooldown");
    await adminApi("/admin/users/reset-cooldown", {
      method: "POST",
      body: JSON.stringify({ user_id: userId })
    });
    showNotice(`Cooldown utilisateur ${userId} remis a zero`, "ok");
    await refreshAdmin();
  } catch (error) {
    showNotice(`Reset impossible: ${error.message}`, "error");
    setAdminStatus("Erreur");
  }
}

function explainAccessError(error) {
  if (!adminState.token || error.status === 401) {
    return "Connecte-toi avec le compte administrateur dans l'application principale, puis recharge /gestion.";
  }
  if (error.status === 403) {
    return "Le compte connecte n'est pas autorise a ouvrir le panel de gestion.";
  }
  return `Erreur admin: ${error.message}`;
}

async function refreshAdmin() {
  try {
    if (!adminState.token) {
      throw Object.assign(new Error("missing_token"), { status: 401 });
    }

    hideNotice();
    setAdminStatus("Chargement");
    const [summary, usersPayload, backupsPayload, auditPayload] = await Promise.all([
      adminApi("/admin/summary"),
      adminApi("/admin/users"),
      adminApi("/admin/backups"),
      adminApi("/admin/audit")
    ]);
    renderSummary(summary);
    renderUsers(usersPayload.users || []);
    renderBackups(backupsPayload.backups || []);
    renderAudit(auditPayload.audit || []);
    setAdminStatus("Admin actif");
  } catch (error) {
    showNotice(explainAccessError(error), "error");
    setAdminStatus("Acces refuse");
    adminEls.usersTableBody.innerHTML = `<tr><td colspan="8">-</td></tr>`;
    adminEls.backupsTableBody.innerHTML = `<tr><td colspan="6">-</td></tr>`;
    adminEls.auditTableBody.innerHTML = `<tr><td colspan="4">-</td></tr>`;
  }
}

adminEls.refreshButton.addEventListener("click", refreshAdmin);
adminEls.createBackupButton.addEventListener("click", createBackup);
adminEls.resetMapButton.addEventListener("click", resetMap);
refreshAdmin();

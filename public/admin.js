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
  usersTableBody: document.getElementById("usersTableBody")
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

function renderSummary(summary) {
  adminEls.name.textContent = `Admin ${summary.admin_username}`;
  adminEls.usersCount.textContent = String(summary.users_count);
  adminEls.mapStats.textContent = `${summary.map.width} x ${summary.map.height}`;
  adminEls.sequenceStats.textContent = `Seq ${summary.map.sequence}`;
  adminEls.rulesStats.textContent = `${summary.rules.pixel_quota_per_cooldown} px / ${summary.rules.cooldown_seconds}s`;
}

function renderUsers(users) {
  adminEls.usersTableBody.innerHTML = "";
  if (!users.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 6;
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
      <td>${formatTimestamp(user.last_pixel_timestamp)}</td>
      <td>${formatTimestamp(user.pixel_window_start_timestamp)}</td>
      <td>${user.pixels_placed_in_window}</td>
      <td></td>
    `;

    row.children[1].textContent = user.username;

    const button = document.createElement("button");
    button.className = "ghost-button";
    button.type = "button";
    button.textContent = "Reset cooldown";
    button.addEventListener("click", () => resetCooldown(user.id));
    row.children[5].appendChild(button);

    adminEls.usersTableBody.appendChild(row);
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
    const [summary, usersPayload] = await Promise.all([
      adminApi("/admin/summary"),
      adminApi("/admin/users")
    ]);
    renderSummary(summary);
    renderUsers(usersPayload.users || []);
    setAdminStatus("Admin actif");
  } catch (error) {
    showNotice(explainAccessError(error), "error");
    setAdminStatus("Acces refuse");
    adminEls.usersTableBody.innerHTML = `<tr><td colspan="6">-</td></tr>`;
  }
}

adminEls.refreshButton.addEventListener("click", refreshAdmin);
refreshAdmin();

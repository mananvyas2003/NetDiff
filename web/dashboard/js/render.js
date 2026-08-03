/** Render DiffResult into dashboard panels. */

export function countByType(changes) {
  const map = {};
  for (const change of changes || []) {
    const t = change.type || "Unknown";
    map[t] = (map[t] || 0) + 1;
  }
  return map;
}

export function filterChanges(diff, { significance, query }) {
  const q = (query || "").trim().toLowerCase();
  return (diff.changes || []).filter((change) => {
    if (significance === "significant" && change.significance !== "SIGNIFICANT") {
      return false;
    }
    if (significance === "cosmetic" && change.significance !== "COSMETIC") {
      return false;
    }
    if (!q) return true;
    const hay = `${change.type || ""} ${change.message || ""}`.toLowerCase();
    return hay.includes(q);
  });
}

export function groupChanges(changes) {
  const groups = {};
  for (const change of changes) {
    const key = change.type || "Unknown";
    if (!groups[key]) groups[key] = [];
    groups[key].push(change);
  }
  return Object.keys(groups)
    .sort()
    .map((type) => ({ type, items: groups[type] }));
}

export function renderKpis(container, diff) {
  const summary = diff.summary || {};
  const byType = summary.by_type || countByType(diff.changes);
  const items = [
    ["Significant", summary.significant_count ?? 0],
    ["Cosmetic", summary.cosmetic_count ?? 0],
    ["Total changes", (diff.changes || []).length],
    ["Types", Object.keys(byType).length],
  ];
  container.innerHTML = items
    .map(
      ([label, value]) =>
        `<div class="kpi"><span class="kpi-label">${label}</span>` +
        `<span class="kpi-value">${value}</span></div>`
    )
    .join("");
}

export function renderChangeList(container, groups, selectedIndex, onSelect) {
  if (!groups.length) {
    container.innerHTML =
      `<p class="change-group-title">No matching changes</p>` +
      `<p style="padding:0.75rem;color:var(--muted);margin:0">Adjust filters or run another diff.</p>`;
    return;
  }

  container.innerHTML = "";
  let flatIndex = 0;
  for (const group of groups) {
    const title = document.createElement("p");
    title.className = "change-group-title";
    title.textContent = `${group.type} (${group.items.length})`;
    container.appendChild(title);

    for (const change of group.items) {
      const idx = flatIndex++;
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "change-item" + (idx === selectedIndex ? " is-selected" : "");
      btn.setAttribute("role", "listitem");
      const flags = [];
      if (change.significance === "COSMETIC") flags.push("cosmetic");
      if (change.critical) flags.push(`<span class="crit">critical</span>`);
      btn.innerHTML =
        `<span class="change-type">${escapeHtml(change.type || "")}</span>` +
        `<span class="change-msg">${escapeHtml(change.message || "")}</span>` +
        (flags.length
          ? `<span class="change-flags">${flags.join(" · ")}</span>`
          : "");
      btn.addEventListener("click", () => onSelect(idx, change));
      container.appendChild(btn);
    }
  }
}

export function renderInspector(container, change) {
  if (!change) {
    container.innerHTML =
      `<p class="inspector-empty">Select a change to inspect payload and navigation metadata.</p>`;
    return;
  }
  const nav = navigationHints(change);
  container.innerHTML =
    `<h3>${escapeHtml(change.type || "Change")}</h3>` +
    `<p>${escapeHtml(change.message || "")}</p>` +
    (nav
      ? `<p style="color:#c8f04a;margin:0.75rem 0 0.35rem">Navigation</p><pre>${escapeHtml(
          nav
        )}</pre>`
      : "") +
    `<p style="color:#c8f04a;margin:0.75rem 0 0.35rem">Payload</p>` +
    `<pre>${escapeHtml(JSON.stringify(change.payload || {}, null, 2))}</pre>`;
}

export function toMarkdown(diff, labels = {}) {
  const summary = diff.summary || {};
  const lines = [
    `### NetDiff — **${summary.gate || "?"}**`,
    "",
    `\`${labels.before || "before"}\` → \`${labels.after || "after"}\``,
    "",
    `${summary.significant_count ?? 0} significant, ${summary.cosmetic_count ?? 0} cosmetic`,
    "",
  ];
  const sig = (diff.changes || []).filter((c) => c.significance === "SIGNIFICANT");
  if (!sig.length) {
    lines.push("No electrical changes.");
  } else {
    for (const change of sig) {
      const mark = change.critical ? "⚠ " : "";
      lines.push(`- ${mark}**${change.type}**: ${change.message}`);
    }
  }
  lines.push("");
  return lines.join("\n");
}

function navigationHints(change) {
  const payload = change.payload || {};
  const bits = [];
  const component = payload.component;
  if (component) {
    if (component.ref) bits.push(`ref: ${component.ref}`);
    if (component.sheet_path) bits.push(`sheet: ${component.sheet_path}`);
    const pos = component.position;
    if (pos && (pos.x != null || pos.y != null)) {
      bits.push(`position: (${pos.x}, ${pos.y})`);
    }
  }
  if (payload.ref) bits.push(`ref: ${payload.ref}`);
  if (payload.pin_id) bits.push(`pin: ${payload.pin_id}`);
  return bits.length ? bits.join("\n") : "";
}

function escapeHtml(text) {
  return String(text)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

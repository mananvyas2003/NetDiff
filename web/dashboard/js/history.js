const KEY = "netdiff.dashboard.history.v1";
const MAX = 20;

export function loadHistory() {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed : [];
  } catch (_) {
    return [];
  }
}

export function saveHistoryEntry(entry) {
  const list = loadHistory().filter((item) => item.id !== entry.id);
  list.unshift(entry);
  localStorage.setItem(KEY, JSON.stringify(list.slice(0, MAX)));
  return list.slice(0, MAX);
}

export function clearHistory() {
  localStorage.removeItem(KEY);
}

export function makeHistoryId() {
  return `run-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
}

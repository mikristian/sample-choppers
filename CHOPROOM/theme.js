(() => {
  'use strict';
  const storageKey = 'choproom-theme';
  const root = document.documentElement;
  const toggle = document.getElementById('themeToggle');
  const label = document.getElementById('themeToggleLabel');
  const meta = document.querySelector('meta[name="theme-color"]');
  const valid = theme => theme === 'light' || theme === 'dark';
  let saved = null;
  try { saved = localStorage.getItem(storageKey); } catch (_) {}
  const initial = valid(saved) ? saved : 'dark';
  function setTheme(theme, persist = true) {
    const next = valid(theme) ? theme : 'dark';
    root.dataset.theme = next;
    root.style.colorScheme = next;
    if (toggle) {
      toggle.setAttribute('aria-pressed', String(next === 'light'));
      toggle.setAttribute('aria-label', 'Switch to ' + (next === 'light' ? 'dark' : 'light') + ' theme');
      toggle.title = 'Switch to ' + (next === 'light' ? 'dark' : 'light') + ' theme';
    }
    if (label) label.textContent = next === 'light' ? 'Studio gray' : 'Graphite';
    if (meta) meta.content = next === 'light' ? '#bcbec1' : '#252628';
    if (persist) { try { localStorage.setItem(storageKey, next); } catch (_) {} }
    document.dispatchEvent(new CustomEvent('choproom:themechange', { detail: { theme: next } }));
  }
  window.CHOPROOMTheme = { set: setTheme, get: () => root.dataset.theme };
  setTheme(initial, false);
  toggle?.addEventListener('click', () => setTheme(root.dataset.theme === 'light' ? 'dark' : 'light'));
})();

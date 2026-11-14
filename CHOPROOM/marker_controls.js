/* Pure marker geometry helpers used by the waveform drag interface. */
(function (root) {
  'use strict';
  function clampBoundary(markers, index, time, duration, minimumGap = 0.03) {
    if (!Array.isArray(markers) || markers.length < 2 || !Number.isInteger(index)
        || index < 0 || index >= markers.length || !Number.isFinite(time)) return null;
    const left = index === 0 ? 0 : markers[index - 1] + minimumGap;
    const right = index === markers.length - 1 ? duration : markers[index + 1] - minimumGap;
    if (!Number.isFinite(duration) || left > right) return null;
    return Math.max(left, Math.min(right, time));
  }
  function nearestHandle(markers, viewStart, visibleSpan, canvasWidth, x, radius = 14) {
    if (!Array.isArray(markers) || visibleSpan <= 0 || canvasWidth <= 0) return -1;
    let nearest = -1, distance = radius;
    for (let i = 0; i < markers.length; i++) {
      const rawX = (markers[i] - viewStart) / visibleSpan * canvasWidth;
      if (rawX < -1 || rawX > canvasWidth + 1) continue;
      const handleX = Math.max(7, Math.min(canvasWidth - 7, rawX));
      const d = Math.abs(handleX - x);
      if (d <= distance) { distance = d; nearest = i; }
    }
    return nearest;
  }
  // Manual cuts operate on the whole source, even when the user has shortened
  // the OUTRO handle to finish an earlier chop. In that case an additional cut
  // beyond the old OUTRO turns it into an interior boundary without overwriting
  // the earlier audio. A similar rule applies before a trimmed INTRO.
  function insertCut(markers, at, duration, maxSlices = 512, gap = 0.03) {
    if (!Array.isArray(markers) || markers.length < 2
        || !Number.isFinite(at) || !Number.isFinite(duration)
        || at < 0 || at > duration || !markers.every(Number.isFinite)) {
      return {error: 'outside'};
    }
    if (markers.length - 1 >= maxSlices) return {error: 'full'};
    if (markers.some(t => Math.abs(t - at) < gap)) return {error: 'duplicate'};
    const next = markers.slice();
    let index = next.findIndex(t => t > at);
    if (index < 0) index = next.length;
    const before = index === 0 ? 0 : next[index - 1];
    const after = index === next.length ? duration : next[index];
    if (at - before < gap || after - at < gap) return {error: 'tooClose'};
    next.splice(index, 0, at);
    return {markers: next, index};
  }
  const api = {clampBoundary, nearestHandle, insertCut};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.ChopMarkers = api;
})(typeof window !== 'undefined' ? window : globalThis);

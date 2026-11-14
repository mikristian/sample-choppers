/* Lightweight, offline transient detection for CHOPROOM.
 * Detection is performed once per audio/phrase. The percentage slider reveals
 * stronger-to-weaker candidate onsets without reprocessing the whole audio.
 */
(function (root) {
  'use strict';

  function findCandidates(audio, startSeconds, endSeconds, maxCandidates = 64) {
    if (!audio || typeof audio.getChannelData !== 'function') return [];
    const rate = audio.sampleRate;
    const start = Math.max(0, Math.floor(startSeconds * rate));
    const end = Math.min(audio.length, Math.ceil(endSeconds * rate));
    if (end - start < 2048) return [];
    const left = audio.getChannelData(0);
    const right = audio.numberOfChannels > 1 ? audio.getChannelData(1) : left;
    const step = 512, windowSize = 1024, energies = [], times = [];

    for (let i = start; i + windowSize <= end; i += step) {
      let energy = 0;
      for (let j = i; j < i + windowSize; j += 8) {
        const l = left[j] || 0, r = right[j] || 0;
        energy += (l * l + r * r) * 0.5;
      }
      energies.push(Math.sqrt(energy / (windowSize / 8)));
      times.push(i / rate);
    }

    const flux = new Float32Array(energies.length);
    let highest = 0;
    for (let i = 3; i < energies.length; i++) {
      const before = (energies[i-1] + energies[i-2] + energies[i-3]) / 3;
      const rise = Math.max(0, energies[i] - before);
      flux[i] = rise * (1 + Math.min(4, rise / (before + 0.02)));
      highest = Math.max(highest, flux[i]);
    }
    if (highest <= 1e-7) return [];

    const candidates = [];
    const cutoff = highest * 0.065;
    for (let i = 4; i < flux.length - 2; i++) {
      const t = times[i];
      // Avoid nearly empty leading/trailing pads when the first transient
      // is only a few milliseconds into the selected phrase.
      if (t < startSeconds + 0.18 || t > endSeconds - 0.18) continue;
      if (flux[i] >= cutoff && flux[i] >= flux[i-1] && flux[i] > flux[i+1]) {
        candidates.push({time:t, strength:flux[i]});
      }
    }
    candidates.sort((a,b) => b.strength - a.strength || a.time - b.time);
    const selected = [];
    const gap = Math.max(0.09, Math.min(0.65, (endSeconds-startSeconds) / 70));
    for (const candidate of candidates) {
      if (selected.length >= maxCandidates) break;
      if (selected.every(existing => Math.abs(existing.time-candidate.time) > gap)) selected.push(candidate);
    }
    return selected;
  }

  function markersForPercent(candidates, percent, maxCuts = 15) {
    const n = Array.isArray(candidates) ? candidates.length : 0;
    const value = Math.max(0, Math.min(100, Number(percent) || 0));
    const target = Math.min(maxCuts, Math.round(value / 100 * Math.min(maxCuts, n)));
    return candidates.slice(0,target).map(c=>c.time).sort((a,b)=>a-b);
  }

  const API = {findCandidates, markersForPercent};
  if (typeof module !== 'undefined' && module.exports) module.exports = API;
  root.ChopSlicing = API;
})(typeof window !== 'undefined' ? window : globalThis);

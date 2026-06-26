/* Sonic Loader — in-flow PS5 temperature gauge driver.

   Looks for a #sl-tempgauge element (only present on the homepage),
   builds the SVG sparkline + readouts inside it, and polls
   /api/fan/temp every 3 s. If the API is unreachable for 3 ticks the
   card is hidden until reads recover. No globals leaked, no deps. */

(function () {
  'use strict';

  if (window.__sonicTempgaugeBooted) return;
  window.__sonicTempgaugeBooted = true;

  var POLL_MS = 3000;
  var FAILURES_BEFORE_HIDE = 3;
  var SAMPLES = 60;
  var W = 240, H = 84, PAD = 4;

  function build(card) {
    if (card.dataset.built === '1') return;
    card.dataset.built = '1';
    card.innerHTML =
      '<div class="sl-tempgauge__left">' +
        '<span class="sl-tempgauge__label">' +
          '<span class="sl-tempgauge__pulse" aria-hidden="true"></span>' +
          'PS5 Temperature' +
        '</span>' +
        '<div class="sl-tempgauge__big">' +
          '<span><span class="sl-t-now">--</span><small>°C</small></span>' +
          '<span class="sl-tempgauge__delta">—</span>' +
        '</div>' +
        '<span class="sl-tempgauge__sub">hottest of CPU + SoC sensors</span>' +
      '</div>' +
      '<div class="sl-tempgauge__chart">' +
        '<svg viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none">' +
          '<defs>' +
            '<linearGradient id="sl-tg-fill" x1="0" y1="0" x2="0" y2="1">' +
              '<stop offset="0%"   stop-color="#7ad7ff" stop-opacity="0.55"/>' +
              '<stop offset="55%"  stop-color="#7ad7ff" stop-opacity="0.18"/>' +
              '<stop offset="100%" stop-color="#7ad7ff" stop-opacity="0"/>' +
            '</linearGradient>' +
            '<linearGradient id="sl-tg-line" x1="0" y1="0" x2="1" y2="0">' +
              '<stop offset="0%"   stop-color="#50d890"/>' +
              '<stop offset="55%"  stop-color="#f3c969"/>' +
              '<stop offset="80%"  stop-color="#f3925a"/>' +
              '<stop offset="100%" stop-color="#ee5a5a"/>' +
            '</linearGradient>' +
            '<filter id="sl-tg-glow"><feGaussianBlur stdDeviation="1.6"/></filter>' +
          '</defs>' +
          '<line  class="sl-t-thresh" x1="0" y1="20" x2="' + W + '" y2="20" ' +
                  'stroke="rgba(255,255,255,.85)" stroke-width="1" ' +
                  'stroke-dasharray="3,3" opacity=".7"/>' +
          '<path  class="sl-t-area"      fill="url(#sl-tg-fill)" d=""/>' +
          '<path  class="sl-t-glow-line" fill="none" stroke="url(#sl-tg-line)" ' +
                  'stroke-width="3" stroke-linecap="round" filter="url(#sl-tg-glow)" opacity=".55" d=""/>' +
          '<path  class="sl-t-line-path" fill="none" stroke="url(#sl-tg-line)" ' +
                  'stroke-width="2" stroke-linecap="round" d=""/>' +
          '<circle class="sl-t-head" cx="' + W + '" cy="32" r="4" ' +
                  'fill="#fff" stroke="rgba(0,0,0,.5)" stroke-width="1"/>' +
        '</svg>' +
        '<div class="sl-tempgauge__threshold-pill" style="top:20px;">--°</div>' +
      '</div>' +
      '<div class="sl-tempgauge__pips">' +
        '<div class="pip">' +
          '<div class="pip__val"><span class="sl-t-min">--</span><small>°</small></div>' +
          '<div class="pip__label">min · 60s</div>' +
        '</div>' +
        '<div class="pip">' +
          '<div class="pip__val"><span class="sl-t-avg">--</span><small>°</small></div>' +
          '<div class="pip__label">avg</div>' +
        '</div>' +
        '<div class="pip">' +
          '<div class="pip__val"><span class="sl-t-max">--</span><small>°</small></div>' +
          '<div class="pip__label">max · 60s</div>' +
        '</div>' +
      '</div>';
  }

  function tempY(t, lo, hi) {
    var x = (t - lo) / (hi - lo);
    if (x < 0) x = 0; if (x > 1) x = 1;
    return H - PAD - x * (H - PAD * 2);
  }
  function classify(t, warmC, hotC) {
    if (t >= hotC)  return 'hot';
    if (t >= warmC) return 'warm';
    return 'cool';
  }
  function fmt(v) {
    return Number.isFinite(v) ? v.toFixed(0) : '--';
  }

  function start() {
    var card = document.getElementById('sl-tempgauge');
    if (!card) return;  // homepage only — every other page omits the container.
    build(card);

    var areaPath = card.querySelector('.sl-t-area');
    var linePath = card.querySelector('.sl-t-line-path');
    var glowPath = card.querySelector('.sl-t-glow-line');
    var headDot  = card.querySelector('.sl-t-head');
    var threshLn = card.querySelector('.sl-t-thresh');
    var threshPl = card.querySelector('.sl-tempgauge__threshold-pill');
    var tNow     = card.querySelector('.sl-t-now');
    var tDelta   = card.querySelector('.sl-tempgauge__delta');
    var tMin     = card.querySelector('.sl-t-min');
    var tAvg     = card.querySelector('.sl-t-avg');
    var tMax     = card.querySelector('.sl-t-max');

    var buf = [];
    var prev = null;
    var failures = 0;

    function draw(j) {
      var lo = Number.isFinite(j.minC)  ? j.minC  : 30;
      var hi = Number.isFinite(j.maxC)  ? j.maxC  : 90;
      var warmC = Number.isFinite(j.warmC) ? j.warmC : 65;
      var hotC  = Number.isFinite(j.hotC)  ? j.hotC  : 80;
      var hot = (Number.isFinite(j.hottestC) ? j.hottestC :
                 Number.isFinite(j.cpuC)     ? j.cpuC     :
                 Number.isFinite(j.socC)     ? j.socC     : NaN);
      if (!Number.isFinite(hot)) return;

      buf.push(hot);
      while (buf.length > SAMPLES) buf.shift();
      var N = buf.length;

      var d = '';
      for (var i = 0; i < N; i++) {
        var x = (i / (SAMPLES - 1)) * W;
        var y = tempY(buf[i], lo, hi);
        d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1) + ' ';
      }
      var area = d + 'L' + W + ',' + H + ' L0,' + H + ' Z';
      linePath.setAttribute('d', d);
      glowPath.setAttribute('d', d);
      areaPath.setAttribute('d', area);

      var headX = ((N - 1) / (SAMPLES - 1)) * W;
      var headY = tempY(hot, lo, hi);
      headDot.setAttribute('cx', headX.toFixed(1));
      headDot.setAttribute('cy', headY.toFixed(1));

      tNow.textContent = fmt(hot);

      if (prev != null) {
        var dt = hot - prev;
        var sign = dt > 0 ? '▲' : dt < 0 ? '▼' : '·';
        tDelta.textContent = sign + ' ' + Math.abs(dt).toFixed(1) + '°';
        tDelta.className = 'sl-tempgauge__delta ' +
            (dt > 0.4 ? 'up' : dt < -0.4 ? 'down' : '');
      }
      prev = hot;

      var min = buf[0], max = buf[0], sum = 0;
      for (var k = 0; k < N; k++) {
        if (buf[k] < min) min = buf[k];
        if (buf[k] > max) max = buf[k];
        sum += buf[k];
      }
      tMin.textContent = fmt(min);
      tMax.textContent = fmt(max);
      tAvg.textContent = fmt(sum / N);

      if (Number.isFinite(j.thresholdC) && j.thresholdC >= lo) {
        var ty = tempY(j.thresholdC, lo, hi);
        threshLn.setAttribute('y1', ty);
        threshLn.setAttribute('y2', ty);
        threshPl.style.top = (ty / H * 100) + '%';
        threshPl.textContent = j.thresholdC + '°';
        threshPl.style.display = '';
      } else {
        threshPl.style.display = 'none';
      }

      card.classList.remove('is-cool', 'is-warm', 'is-hot');
      card.classList.add('is-' + classify(hot, warmC, hotC));
      card.classList.remove('sl-tempgauge--hidden');
    }

    async function tick() {
      try {
        var r = await fetch('/api/fan/temp', { cache: 'no-store' });
        if (!r.ok) throw new Error('http ' + r.status);
        var j = await r.json();
        failures = 0;
        draw(j);
      } catch (e) {
        failures++;
        if (failures >= FAILURES_BEFORE_HIDE) {
          card.classList.add('sl-tempgauge--hidden');
        }
      }
    }

    tick();
    setInterval(tick, POLL_MS);
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'visible') tick();
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
})();

// Shared behaviour for the website taste mockups: A/B audio player and scroll reveal.
// Each direction styles the markup itself; this file only wires the behaviour.

(function () {
  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  // Scroll reveal: [data-reveal] gets .is-in when it enters the viewport.
  const revealTargets = document.querySelectorAll('[data-reveal]');
  if (reduceMotion || !('IntersectionObserver' in window)) {
    revealTargets.forEach((el) => el.classList.add('is-in'));
  } else {
    const io = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add('is-in');
            io.unobserve(entry.target);
          }
        });
      },
      { threshold: 0.18, rootMargin: '0px 0px -6% 0px' },
    );
    revealTargets.forEach((el) => io.observe(el));
  }

  // A/B player. Markup contract:
  //   [data-ab]            root; gets data-source="dry|wet" and .is-playing
  //   [data-ab-play]       play / pause button
  //   [data-ab-src="dry"]  source buttons (aria-pressed is managed here)
  //   [data-ab-wave]       empty element; bars are generated from real peaks
  //   [data-ab-time]       elapsed time text
  const SOURCES = { dry: 'audio/dry.m4a', wet: 'audio/wet.m4a' };
  const peaks = window.ARDOR_PEAKS || { dry: [], wet: [] };

  function formatTime(seconds) {
    const s = Math.max(0, Math.floor(seconds));
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
  }

  document.querySelectorAll('[data-ab]').forEach((root) => {
    const tracks = {
      dry: new Audio(SOURCES.dry),
      wet: new Audio(SOURCES.wet),
    };
    Object.values(tracks).forEach((a) => {
      a.preload = 'auto';
      a.loop = true;
    });

    let source = root.dataset.source === 'dry' ? 'dry' : 'wet';
    const playBtn = root.querySelector('[data-ab-play]');
    const wave = root.querySelector('[data-ab-wave]');
    const time = root.querySelector('[data-ab-time]');
    const srcBtns = root.querySelectorAll('[data-ab-src]');

    // Keep at least 5 px per bar so the gaps never eat the bars on narrow screens.
    const maxBars = wave ? Math.max(24, Math.floor(wave.clientWidth / 5)) : 96;
    const barCount = Math.min(Number(root.dataset.bars || 96), maxBars);
    const bars = [];
    if (wave) {
      for (let i = 0; i < barCount; i += 1) {
        const bar = document.createElement('i');
        wave.appendChild(bar);
        bars.push(bar);
      }
      wave.addEventListener('click', (event) => {
        const rect = wave.getBoundingClientRect();
        const ratio = (event.clientX - rect.left) / rect.width;
        const t = ratio * (tracks.dry.duration || 45.5);
        Object.values(tracks).forEach((a) => {
          a.currentTime = t;
        });
        paint();
      });
    }

    function sample(list, i) {
      if (!list.length) return 0.3;
      const idx = Math.floor((i / barCount) * list.length);
      return list[Math.min(idx, list.length - 1)];
    }

    function drawBars() {
      bars.forEach((bar, i) => {
        const v = sample(peaks[source], i);
        bar.style.setProperty('--s', String(Math.max(0.06, v).toFixed(3)));
      });
    }

    function paint() {
      const a = tracks[source];
      const ratio = a.duration ? a.currentTime / a.duration : 0;
      const played = Math.round(ratio * barCount);
      bars.forEach((bar, i) => bar.classList.toggle('on', i < played));
      if (time) time.textContent = formatTime(a.currentTime);
    }

    function setSource(next) {
      source = next;
      root.dataset.source = next;
      srcBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.abSrc === next)));
      tracks.dry.muted = next !== 'dry';
      tracks.wet.muted = next !== 'wet';
      drawBars();
      paint();
    }

    srcBtns.forEach((b) => b.addEventListener('click', () => setSource(b.dataset.abSrc)));

    if (playBtn) {
      playBtn.addEventListener('click', () => {
        const playing = root.classList.contains('is-playing');
        if (playing) {
          Object.values(tracks).forEach((a) => a.pause());
          root.classList.remove('is-playing');
          playBtn.setAttribute('aria-label', 'Play');
          return;
        }
        tracks.wet.currentTime = tracks.dry.currentTime;
        Promise.all(Object.values(tracks).map((a) => a.play()))
          .then(() => {
            root.classList.add('is-playing');
            playBtn.setAttribute('aria-label', 'Pause');
          })
          .catch((err) => {
            console.error('Audio playback failed', err);
            if (time) time.textContent = 'Audio unavailable';
          });
      });
    }

    tracks.dry.addEventListener('timeupdate', paint);
    setSource(source);
  });
})();

// Scale each fixed 1280 x 720 device screen to the width of its frame.
(() => {
  const DESIGN_WIDTH = 1280;
  const fit = () => {
    document.querySelectorAll(".frame").forEach((frame) => {
      const screen = frame.querySelector(".screen");
      if (!screen) return;
      screen.style.transform = `scale(${frame.clientWidth / DESIGN_WIDTH})`;
    });
  };
  window.addEventListener("resize", fit);
  document.addEventListener("DOMContentLoaded", fit);
  fit();
})();

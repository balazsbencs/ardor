// Interactive 3D model of the Ardor enclosure, built to the real geometry from
// 3d_files/enclosure.scad: 180 x 150 x 55 mm machined box, four footswitches
// inset 18 mm from the corners, a rear-centre encoder, and a recessed Raspberry
// Pi Touch Display 2 showing the live preset UI.
//
// Treatment is a restrained product shot: anodized-graphite body, brushed-steel
// stomp switches, an aluminium knob, a glass screen with real glare, a soft
// contact shadow, and a flat Panel screen with real glare.
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { drawPresetScreen } from './preset-screen-texture';

// Enclosure dimensions (mm). X = width, Y = height (vertical), Z = depth.
const W = 180;
const H = 55;
const D = 150;
const TOP = H / 2;

export type PedalHandle = { destroy: () => void };

/** Silkscreen wordmark for the front face. */
function makeWordmark(): THREE.CanvasTexture {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 96;
  const ctx = c.getContext('2d')!;
  ctx.clearRect(0, 0, c.width, c.height);
  ctx.fillStyle = '#e2e4e3';
  ctx.font = '600 46px "Saira Condensed", sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.letterSpacing = '14px';
  ctx.fillText('ARDOR', c.width / 2 + 7, c.height / 2 + 2);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  return tex;
}

export function initPedal(canvas: HTMLCanvasElement): PedalHandle | null {
  let renderer: THREE.WebGLRenderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  } catch {
    return null;
  }
  if (!renderer.getContext()) return null;

  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const parent = canvas.parentElement as HTMLElement;

  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.0;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;

  const scene = new THREE.Scene();

  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;

  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 3000);
  camera.position.set(165, 150, 260);
  camera.lookAt(0, -8, 0);

  // ---- materials ------------------------------------------------------------
  const bodyMat = new THREE.MeshPhysicalMaterial({
    color: 0x2a2f33,
    metalness: 0.55,
    roughness: 0.42,
    clearcoat: 0.35,
    clearcoatRoughness: 0.55,
    envMapIntensity: 0.85,
  });
  const plateMat = new THREE.MeshStandardMaterial({
    color: 0x212528,
    metalness: 0.6,
    roughness: 0.5,
    envMapIntensity: 0.8,
  });
  const steelMat = new THREE.MeshStandardMaterial({
    color: 0x7c8286,
    metalness: 1,
    roughness: 0.36,
    envMapIntensity: 1,
  });
  const buttonMat = new THREE.MeshStandardMaterial({
    color: 0x1b1f22,
    metalness: 0.5,
    roughness: 0.45,
  });
  const aluMat = new THREE.MeshStandardMaterial({
    color: 0x363c40,
    metalness: 0.9,
    roughness: 0.32,
    envMapIntensity: 1,
  });
  const darkMat = new THREE.MeshStandardMaterial({ color: 0x101416, metalness: 0.5, roughness: 0.6 });
  const screwMat = new THREE.MeshStandardMaterial({ color: 0x40484c, metalness: 1, roughness: 0.42 });

  // ---- pedal assembly -------------------------------------------------------
  const pedal = new THREE.Group();
  scene.add(pedal);

  const body = new THREE.Mesh(new RoundedBoxGeometry(W, H, D, 8, 4), bodyMat);
  pedal.add(body);

  // Recessed top plate.
  const plate = new THREE.Mesh(new RoundedBoxGeometry(W - 8, 3, D - 8, 4, 2.5), plateMat);
  plate.position.y = TOP - 0.6;
  pedal.add(plate);

  // Front-face silkscreen wordmark.
  const wordmark = new THREE.Mesh(
    new THREE.PlaneGeometry(66, 12),
    new THREE.MeshBasicMaterial({ map: makeWordmark(), transparent: true, opacity: 0.85 }),
  );
  wordmark.position.set(0, -7, D / 2 + 0.3);
  pedal.add(wordmark);

  // ---- display (recessed, emissive UI + glass glare) ------------------------
  // Sits just below the screen so its 147×96 face reads as a black surround.
  const bezel = new THREE.Mesh(
    new THREE.BoxGeometry(147, 5, 96),
    new THREE.MeshStandardMaterial({ color: 0x04070a, metalness: 0.3, roughness: 0.7 }),
  );
  bezel.position.y = TOP - 1.5;
  pedal.add(bezel);

  const screenCanvas = document.createElement('canvas');
  drawPresetScreen(screenCanvas);
  const screenTex = new THREE.CanvasTexture(screenCanvas);
  screenTex.colorSpace = THREE.SRGBColorSpace;
  screenTex.anisotropy = renderer.capabilities.getMaxAnisotropy();

  const screen = new THREE.Mesh(
    new THREE.PlaneGeometry(125, 74),
    new THREE.MeshStandardMaterial({
      map: screenTex,
      emissive: 0xffffff,
      emissiveMap: screenTex,
      emissiveIntensity: 0.8,
      roughness: 0.5,
      metalness: 0,
    }),
  );
  screen.rotation.x = -Math.PI / 2;
  screen.position.y = TOP + 1.9;
  pedal.add(screen);

  // Glass sheet over the display for realistic glare/reflection.
  const glass = new THREE.Mesh(
    new THREE.PlaneGeometry(143, 91),
    new THREE.MeshPhysicalMaterial({
      color: 0x0a0f0d,
      roughness: 0.07,
      metalness: 0,
      clearcoat: 1,
      clearcoatRoughness: 0.06,
      transparent: true,
      opacity: 0.05,
      envMapIntensity: 1.1,
    }),
  );
  glass.rotation.x = -Math.PI / 2;
  glass.position.y = TOP + 2.5;
  pedal.add(glass);

  // ---- footswitches (four corners, inset 18 mm) -----------------------------
  const fx = W / 2 - 18;
  const fz = D / 2 - 18;
  for (const [x, z] of [
    [-fx, fz],
    [fx, fz],
    [-fx, -fz],
    [fx, -fz],
  ] as [number, number][]) {
    const nut = new THREE.Mesh(new THREE.CylinderGeometry(8, 8, 2.6, 6), steelMat);
    nut.position.set(x, TOP + 1.3, z);
    nut.rotation.y = Math.PI / 6;
    pedal.add(nut);
    const collar = new THREE.Mesh(new THREE.CylinderGeometry(6, 6.6, 3.5, 32), steelMat);
    collar.position.set(x, TOP + 3.8, z);
    pedal.add(collar);
    const stem = new THREE.Mesh(new THREE.CylinderGeometry(5.4, 5.6, 3.5, 32), buttonMat);
    stem.position.set(x, TOP + 6.2, z);
    pedal.add(stem);
    const cap = new THREE.Mesh(
      new THREE.SphereGeometry(5.6, 32, 20, 0, Math.PI * 2, 0, Math.PI * 0.42),
      buttonMat,
    );
    cap.position.set(x, TOP + 7.4, z);
    pedal.add(cap);
  }

  // ---- encoder (rear-centre) ------------------------------------------------
  const encoderZ = -(D / 2 - 18);
  const knob = new THREE.Mesh(new THREE.CylinderGeometry(11, 11.6, 13, 56), aluMat);
  knob.position.set(0, TOP + 5.5, encoderZ);
  pedal.add(knob);
  const knobBevel = new THREE.Mesh(new THREE.CylinderGeometry(9.6, 11, 2.2, 56), aluMat);
  knobBevel.position.set(0, TOP + 12.3, encoderZ);
  pedal.add(knobBevel);
  const knobTop = new THREE.Mesh(new THREE.CylinderGeometry(9.6, 9.6, 0.8, 56), darkMat);
  knobTop.position.set(0, TOP + 13.5, encoderZ);
  pedal.add(knobTop);
  const indicator = new THREE.Mesh(
    new THREE.BoxGeometry(1.4, 1, 7),
    new THREE.MeshStandardMaterial({
      color: 0xd8422f,
      emissive: 0xd8422f,
      emissiveIntensity: 0.55,
      roughness: 0.4,
    }),
  );
  indicator.position.set(0, TOP + 13.9, encoderZ - 3.4);
  pedal.add(indicator);

  // ---- corner lid screws (inset 8 mm) ---------------------------------------
  for (const [x, z] of [
    [-(W / 2 - 8), D / 2 - 8],
    [W / 2 - 8, D / 2 - 8],
    [-(W / 2 - 8), -(D / 2 - 8)],
    [W / 2 - 8, -(D / 2 - 8)],
  ] as [number, number][]) {
    const screw = new THREE.Mesh(new THREE.CylinderGeometry(2, 2, 1.2, 20), screwMat);
    screw.position.set(x, TOP + 0.4, z);
    pedal.add(screw);
    const slot = new THREE.Mesh(new THREE.BoxGeometry(2.6, 0.4, 0.5), darkMat);
    slot.position.set(x, TOP + 1, z);
    slot.rotation.y = Math.PI / 5;
    pedal.add(slot);
  }

  // ---- side ports (1/4" jacks + rear power) ---------------------------------
  for (const side of [-1, 1]) {
    const ring = new THREE.Mesh(new THREE.CylinderGeometry(7, 7, 2.5, 28), steelMat);
    ring.rotation.z = Math.PI / 2;
    ring.position.set(side * (W / 2), -3, 22);
    pedal.add(ring);
    const hole = new THREE.Mesh(new THREE.CylinderGeometry(4.4, 4.4, 3, 24), darkMat);
    hole.rotation.z = Math.PI / 2;
    hole.position.set(side * (W / 2 + 0.5), -3, 22);
    pedal.add(hole);
  }
  const dc = new THREE.Mesh(new THREE.CylinderGeometry(5.5, 5.5, 3, 28), darkMat);
  dc.rotation.x = Math.PI / 2;
  dc.position.set(-40, -4, -(D / 2 + 0.4));
  pedal.add(dc);

  // shadows on everything
  pedal.traverse((o) => {
    if ((o as THREE.Mesh).isMesh) {
      o.castShadow = true;
      o.receiveShadow = true;
    }
  });
  wordmark.castShadow = false;
  glass.castShadow = false;
  screen.castShadow = false;

  // ---- soft contact shadow --------------------------------------------------
  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(1400, 1400),
    new THREE.ShadowMaterial({ opacity: 0.42 }),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -H / 2 - 0.5;
  ground.receiveShadow = true;
  scene.add(ground);

  // ---- lighting (restrained studio) -----------------------------------------
  scene.add(new THREE.AmbientLight(0x30373b, 0.5));
  const key = new THREE.DirectionalLight(0xffffff, 2.3);
  key.position.set(120, 250, 150);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.camera.near = 50;
  key.shadow.camera.far = 700;
  key.shadow.camera.left = -180;
  key.shadow.camera.right = 180;
  key.shadow.camera.top = 180;
  key.shadow.camera.bottom = -180;
  key.shadow.bias = -0.0005;
  key.shadow.radius = 6;
  scene.add(key);
  const fill = new THREE.DirectionalLight(0x9fb6c4, 0.55);
  fill.position.set(-170, 90, 40);
  scene.add(fill);
  const rim = new THREE.DirectionalLight(0xbfe9d8, 0.6);
  rim.position.set(-110, 70, -220);
  scene.add(rim);

  // ---- postprocessing -------------------------------------------------------
  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  composer.addPass(new OutputPass());

  // ---- sizing ---------------------------------------------------------------
  function resize() {
    const w = parent.clientWidth;
    const h = parent.clientHeight;
    if (w === 0 || h === 0) return;
    renderer.setSize(w, h, false);
    composer.setSize(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  const ro = new ResizeObserver(resize);
  ro.observe(parent);
  resize();

  // ---- interaction: drag to rotate, gentle auto-rotate at rest --------------
  let targetYaw = -0.52;
  let targetPitch = 0.3;
  let yaw = targetYaw;
  let pitch = targetPitch;
  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  let idle = 0;

  const onDown = (e: PointerEvent) => {
    dragging = true;
    idle = 0;
    lastX = e.clientX;
    lastY = e.clientY;
    canvas.setPointerCapture(e.pointerId);
    canvas.style.cursor = 'grabbing';
  };
  const onMove = (e: PointerEvent) => {
    if (!dragging) return;
    targetYaw += (e.clientX - lastX) * 0.008;
    targetPitch += (e.clientY - lastY) * 0.006;
    targetPitch = Math.max(-0.05, Math.min(0.95, targetPitch));
    lastX = e.clientX;
    lastY = e.clientY;
  };
  const onUp = (e: PointerEvent) => {
    dragging = false;
    idle = 0;
    canvas.style.cursor = 'grab';
    try {
      canvas.releasePointerCapture(e.pointerId);
    } catch {
      /* already released */
    }
  };
  canvas.addEventListener('pointerdown', onDown);
  canvas.addEventListener('pointermove', onMove);
  canvas.addEventListener('pointerup', onUp);
  canvas.addEventListener('pointercancel', onUp);
  canvas.style.cursor = 'grab';

  let raf = 0;
  const clock = new THREE.Clock();
  function frame() {
    const dt = clock.getDelta();
    if (!dragging) {
      idle += dt;
      if (!reduceMotion && idle > 1.2) targetYaw += dt * 0.12;
    }
    yaw += (targetYaw - yaw) * 0.08;
    pitch += (targetPitch - pitch) * 0.08;
    pedal.rotation.y = yaw;
    pedal.rotation.x = pitch;
    composer.render();
    raf = requestAnimationFrame(frame);
  }
  frame();

  const io = new IntersectionObserver((entries) => {
    for (const en of entries) {
      if (en.isIntersecting && !raf) frame();
      else if (!en.isIntersecting && raf) {
        cancelAnimationFrame(raf);
        raf = 0;
      }
    }
  });
  io.observe(canvas);

  return {
    destroy() {
      cancelAnimationFrame(raf);
      ro.disconnect();
      io.disconnect();
      renderer.dispose();
      pmrem.dispose();
    },
  };
}

import type { Page, Route } from "@playwright/test";

import type { AssetKind, AssetUse, AssetUsageEntry, DeviceStatus, Preset, PresetBlock, PresetSlotSummary } from "../src/api/types";
import { assetRefs } from "../src/assets/assetRefs";
import { assetOf, slotKey, type MockState } from "./demoState";

export { defaultState, type MockState } from "./demoState";

const KINDS: AssetKind[] = ["models", "irs", "reverb-irs"];
const DUAL_AMP_KEYS = ["leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"];

type Runtime = { generation: number; storedRevisionMatches: boolean; applies: Record<string, { bank: number; slot: number }> };
type Reply = { status: number; body?: unknown };

const ok = (body: unknown, status = 200): Reply => ({ status, body });
const fail = (status: number, error: string, message: string): Reply => ({ status, body: { error, message } });

/** Mirrors presets.Store.AssetUsage in managerd: every file path and the presets that reference it. */
function usageOf(state: MockState): AssetUsageEntry[] {
  const byPath = new Map<string, AssetUse[]>();
  for (const [key, preset] of Object.entries(state.presets)) {
    const [bank, slot] = key.split(":").map(Number);
    for (const path of new Set(assetRefs(preset).map((ref) => ref.path))) {
      byPath.set(path, [...(byPath.get(path) ?? []), { bank, slot, name: preset.name }]);
    }
  }
  return [...byPath].map(([path, presets]) => ({ path, presets })).sort((a, b) => a.path.localeCompare(b.path));
}

function renameInBlocks(blocks: PresetBlock[], from: string, to: string): PresetBlock[] {
  return blocks.map((block) => {
    const params = Object.fromEntries(Object.entries(block.params).map(([key, value]) =>
      [key, DUAL_AMP_KEYS.includes(key) && value === from ? to : value]));
    const lanes = block.lanes && {
      left: { blocks: renameInBlocks(block.lanes.left.blocks, from, to) },
      right: { blocks: renameInBlocks(block.lanes.right.blocks, from, to) },
    };
    return { ...block, asset: block.asset === from ? to : block.asset, params, ...(lanes ? { lanes } : {}) };
  });
}

/** Mirrors presets.Store.ReplaceAssetReferences: rewrites every saved preset that uses the old path. */
function renameReferences(presets: Record<string, Preset>, from: string, to: string): { presets: Record<string, Preset>; count: number } {
  let count = 0;
  const next = Object.fromEntries(Object.entries(presets).map(([key, preset]) => {
    if (!assetRefs(preset).some((ref) => ref.path === from)) return [key, preset];
    count += 1;
    const wdw = preset.wdw && {
      ...preset.wdw,
      dry: { ...preset.wdw.dry, blocks: renameInBlocks(preset.wdw.dry.blocks, from, to) },
      wet: { ...preset.wdw.wet, blocks: renameInBlocks(preset.wdw.wet.blocks, from, to) },
    };
    return [key, { ...preset, blocks: renameInBlocks(preset.blocks, from, to), ...(wdw ? { wdw } : {}) }];
  }));
  return { presets: next, count };
}

/** Reads the file name, overwrite flag and size from a multipart upload body. */
function parseUpload(body: Buffer): { filename?: string; overwrite: boolean; size: number } {
  const text = body.toString("latin1");
  const file = /name="file"; filename="([^"]*)"\r\n(?:[^\r\n]+\r\n)*\r\n([\s\S]*?)\r\n--/.exec(text);
  const overwrite = /name="overwrite"\r\n\r\n(true|false)/.exec(text)?.[1] === "true";
  return { filename: file?.[1], overwrite, size: file?.[2].length ?? 0 };
}

function device(state: MockState, runtime: Runtime): DeviceStatus {
  const { bank, slot } = state.active;
  return {
    deviceName: "Ardor", apiVersion: "1", softwareVersion: "0.9.0", authEnabled: false, localAuthState: "disabled",
    dataRootWritable: true, maxBanks: 100, slotsPerBank: 4, supportedPresetVersion: 4,
    active: {
      bank, slot, name: state.presets[slotKey(bank, slot)]?.name, generation: runtime.generation,
      revision: `rev-${runtime.generation}`, storedRevisionMatches: runtime.storedRevisionMatches,
    },
    capabilities: {
      modelUpload: true, irUpload: true, assetRename: true, presetRead: true, presetWrite: true, presetApply: true,
      wifiSettings: false, softwareUpdate: false, tone3000: false, backup: false, sceneRecall: false, sceneTelemetry: false,
    },
  };
}

function summaries(state: MockState): PresetSlotSummary[] {
  return Array.from({ length: 400 }, (_, index) => {
    const bank = Math.floor(index / 4);
    const slot = index % 4;
    const preset = state.presets[slotKey(bank, slot)];
    return preset ? { bank, slot, exists: true, name: preset.name } : { bank, slot, exists: false };
  });
}

/** A tiny managerd: answers the routes the manager uses from an in-memory state, so writes show up in later reads. */
class MockPedal {
  private runtime: Runtime = { generation: 1, storedRevisionMatches: true, applies: {} };
  private applySeq = 0;

  constructor(public state: MockState) {}

  handle(method: string, path: string, body: () => Buffer | null): Reply {
    const parts = path.split("/").slice(2).map(decodeURIComponent);
    const [area, a, b, c, d, e] = parts;
    if (area === "auth" && a === "status") return ok({ state: "disabled", insecureTransport: true });
    if (area === "device" && method === "GET") return ok(device(this.state, this.runtime));
    if (area === "assets") return this.assets(method, a, b, body);
    if (area === "presets" && !a && method === "GET") return ok({ presets: summaries(this.state) });
    if (area === "presets" && a === "banks" && c === "slots") return this.preset(method, Number(b), Number(d), e, body);
    if (area === "runtime" && a === "apply" && b) return this.applyStatus(b);
    return fail(404, "not_found", `No mock for ${method} ${path}`);
  }

  private assets(method: string, kindOrUsage: string, id: string | undefined, body: () => Buffer | null): Reply {
    if (kindOrUsage === "usage" && method === "GET") return ok({ usage: usageOf(this.state) });
    const kind = kindOrUsage as AssetKind;
    if (!KINDS.includes(kind)) return fail(404, "not_found", "asset kind not found");
    const files = this.state.assets[kind];
    const setFiles = (next: typeof files) => { this.state = { ...this.state, assets: { ...this.state.assets, [kind]: next } }; };
    if (method === "GET" && !id) return ok({ assets: files });
    if (method === "POST" && !id) {
      const upload = parseUpload(body() ?? Buffer.alloc(0));
      if (!upload.filename) return fail(400, "missing_file", "request has no file");
      const exists = files.some(({ filename }) => filename === upload.filename);
      if (exists && !upload.overwrite) return fail(409, "asset_exists", "asset already exists");
      const asset = assetOf(kind, upload.filename, upload.size);
      setFiles([...files.filter(({ filename }) => filename !== asset.filename), asset].sort((x, y) => x.filename.localeCompare(y.filename)));
      return ok(asset, 201);
    }
    const current = files.find((file) => file.id === id);
    if (!current) return fail(404, "asset_not_found", "asset not found");
    if (method === "DELETE") {
      setFiles(files.filter((file) => file.id !== id));
      return { status: 204 };
    }
    if (method === "PATCH") {
      const { filename } = JSON.parse(body()?.toString("utf8") ?? "{}") as { filename?: string };
      if (!filename) return fail(400, "invalid_rename", "request body must contain a filename");
      if (files.some((file) => file.filename === filename)) return fail(409, "asset_exists", "asset already exists");
      const renamed = assetOf(kind, filename, current.sizeBytes);
      setFiles(files.map((file) => (file.id === id ? renamed : file)));
      const { presets, count } = renameReferences(this.state.presets, current.path, renamed.path);
      this.state = { ...this.state, presets };
      return ok({ asset: renamed, updatedPresetCount: count });
    }
    return fail(405, "method_not_allowed", `${method} is not supported`);
  }

  private preset(method: string, bank: number, slot: number, action: string | undefined, body: () => Buffer | null): Reply {
    const key = slotKey(bank, slot);
    if (method === "GET" && !action) {
      const preset = this.state.presets[key];
      return preset ? ok({ bank, slot, preset }) : fail(404, "preset_not_found", "preset not found");
    }
    if (method === "PUT" && !action) {
      const preset = JSON.parse(body()?.toString("utf8") ?? "null") as Preset | null;
      if (!preset) return fail(400, "invalid_json", "request body must be a preset");
      this.state = { ...this.state, presets: { ...this.state.presets, [key]: preset } };
      const live = this.state.active.bank === bank && this.state.active.slot === slot;
      if (live) this.runtime = { ...this.runtime, storedRevisionMatches: false };
      return ok({ bank, slot, preset });
    }
    if (method === "POST" && action === "apply") {
      if (!this.state.presets[key]) return fail(404, "preset_not_found", "preset not found");
      const id = `apply-${++this.applySeq}`;
      this.runtime = { ...this.runtime, applies: { ...this.runtime.applies, [id]: { bank, slot } } };
      return ok({ accepted: true, id, state: "pending", bank, slot, message: "apply request queued" }, 202);
    }
    return fail(405, "method_not_allowed", `${method} is not supported`);
  }

  /** The pedal applies at once: the first status read reports applied and the slot becomes live. */
  private applyStatus(id: string): Reply {
    const target = this.runtime.applies[id];
    if (!target) return fail(404, "apply_not_found", "Apply request was not found");
    this.state = { ...this.state, active: target };
    this.runtime = { ...this.runtime, generation: this.runtime.generation + 1, storedRevisionMatches: true };
    return ok({ id, state: "applied", ...target });
  }
}

async function fulfil(route: Route, reply: Reply): Promise<void> {
  if (reply.body === undefined) return route.fulfill({ status: reply.status });
  return route.fulfill({ status: reply.status, contentType: "application/json", body: JSON.stringify(reply.body) });
}

/**
 * Answers every `/api/*` request of the page from `initial`. Only the path is matched, so Vite's own
 * `/src/api/*.ts` module requests pass through. `state()` returns what the mocked pedal holds now.
 */
export async function mockApi(page: Page, initial: MockState): Promise<{ state(): MockState }> {
  const pedal = new MockPedal(structuredClone(initial));
  await page.route((url) => url.pathname.startsWith("/api/"), async (route) => {
    const request = route.request();
    const { pathname } = new URL(request.url());
    await fulfil(route, pedal.handle(request.method(), pathname, () => request.postDataBuffer()));
  });
  return { state: () => pedal.state };
}

// The homepage A/B demo is optional. It renders only when public/audio/ holds a
// manifest and both clips, so the site builds and deploys without them.
//
// public/audio/demo.json:
//   { "dry": "dry.m4a", "wet": "wet.m4a", "caption": "...", "peaks": { "dry": [..], "wet": [..] } }
// Peaks are 0..1 amplitudes from the real recordings; they draw the waveform.
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { url } from './url';

export type DemoAudio = {
  dry: string;
  wet: string;
  caption: string;
  peaks: { dry: number[]; wet: number[] };
};

const AUDIO_DIR = join(process.cwd(), 'public', 'audio');

function isPeakList(value: unknown): value is number[] {
  return Array.isArray(value) && value.every((v) => typeof v === 'number' && v >= 0 && v <= 1);
}

export function getDemoAudio(): DemoAudio | null {
  const manifestPath = join(AUDIO_DIR, 'demo.json');
  if (!existsSync(manifestPath)) return null;

  let manifest: Record<string, unknown>;
  try {
    manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  } catch (error) {
    console.warn(`[demo-audio] ignoring unreadable ${manifestPath}:`, error);
    return null;
  }

  const { dry, wet, caption = '', peaks } = manifest as Partial<Record<string, unknown>> & {
    peaks?: Record<string, unknown>;
  };
  const valid =
    typeof dry === 'string' &&
    typeof wet === 'string' &&
    typeof caption === 'string' &&
    isPeakList(peaks?.dry) &&
    isPeakList(peaks?.wet) &&
    existsSync(join(AUDIO_DIR, dry)) &&
    existsSync(join(AUDIO_DIR, wet));
  if (!valid) {
    console.warn('[demo-audio] demo.json is incomplete or a clip is missing; the demo is hidden.');
    return null;
  }

  return {
    dry: url(`/audio/${dry}`),
    wet: url(`/audio/${wet}`),
    caption,
    peaks: { dry: peaks!.dry as number[], wet: peaks!.wet as number[] },
  };
}

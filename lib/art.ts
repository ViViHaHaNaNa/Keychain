import sharp from "sharp";
import { redis } from "./state";

export const ART_SIZE = 64;
const N = ART_SIZE;

// Album art for a 1-bit 64x64 screen. Every cover is run through a few preparation + dither
// combinations, each candidate is scored, and the cleanest one wins, so dark, bright, graphic
// and photographic covers each get the treatment that suits them.

type Gray = Uint8Array; // N*N, 0-255
type Bits = Uint8Array; // N*N, 0 or 1

const ATKINSON: [number, number, number][] = [[1, 0, 1], [2, 0, 1], [-1, 1, 1], [0, 1, 1], [1, 1, 1], [0, 2, 1]];
const FLOYD: [number, number, number][] = [[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]];

function diffuse(gray: Gray, kernel: [number, number, number][], divisor: number): Bits {
  const px = Float32Array.from(gray);
  const out = new Uint8Array(N * N);
  for (let y = 0; y < N; y++) {
    for (let x = 0; x < N; x++) {
      const i = y * N + x;
      const lit = px[i] >= 128;
      out[i] = lit ? 1 : 0;
      const err = px[i] - (lit ? 255 : 0);
      for (const [dx, dy, w] of kernel) {
        const nx = x + dx;
        const ny = y + dy;
        if (nx >= 0 && nx < N && ny < N) px[ny * N + nx] += (err * w) / divisor;
      }
    }
  }
  return out;
}

// Row-major, MSB first, 1 = lit pixel: the layout Adafruit_GFX drawBitmap() expects.
function pack(bits: Bits): Buffer {
  const out = Buffer.alloc((N * N) / 8);
  for (let i = 0; i < N * N; i++) if (bits[i]) out[i >> 3] |= 0x80 >> (i & 7);
  return out;
}

// Stretches the levels between two percentiles, then lifts (or lowers) the midtones toward
// `targetMean`, within limits, so very dark or very pale covers don't collapse to one colour.
function stretchGamma(gray: Gray, lo: number, hi: number, targetMean: number): Gray {
  const hist = new Array<number>(256).fill(0);
  for (const v of gray) hist[v]++;
  const percentile = (p: number) => {
    let acc = 0;
    for (let i = 0; i < 256; i++) {
      acc += hist[i];
      if (acc >= p * gray.length) return i;
    }
    return 255;
  };
  const a = percentile(lo);
  const b = Math.max(a + 1, percentile(hi));
  const f = Float32Array.from(gray, (v) => Math.min(1, Math.max(0, (v - a) / (b - a))));
  const mean = f.reduce((s, v) => s + v, 0) / f.length;
  const gamma = mean > 0.02 && mean < 0.98 ? Math.min(1.3, Math.max(0.6, Math.log(targetMean) / Math.log(mean))) : 1;
  return Uint8Array.from(f, (v) => Math.round(255 * Math.pow(v, gamma)));
}

const rawGray = (g: Gray) => sharp(Buffer.from(g), { raw: { width: N, height: N, channels: 1 } });

async function enhance(
  gray: Gray,
  clahe: { width: number; height: number; maxSlope: number },
  sharpen: { sigma: number; m1: number; m2: number },
): Promise<Gray> {
  // extractChannel: sharp widens a 1-channel raw input to 3 channels after contrast operations
  return new Uint8Array(await rawGray(gray).clahe(clahe).sharpen(sharpen).extractChannel(0).raw().toBuffer());
}

async function blur(px: Gray, sigma: number): Promise<Gray> {
  return new Uint8Array(await rawGray(px).blur(sigma).extractChannel(0).raw().toBuffer());
}

function sobel(g: Gray): Float32Array {
  const out = new Float32Array(N * N);
  for (let y = 1; y < N - 1; y++) {
    for (let x = 1; x < N - 1; x++) {
      const i = y * N + x;
      const gx = -g[i - N - 1] - 2 * g[i - 1] - g[i + N - 1] + g[i - N + 1] + 2 * g[i + 1] + g[i + N + 1];
      const gy = -g[i - N - 1] - 2 * g[i - N] - g[i - N + 1] + g[i + N - 1] + 2 * g[i + N] + g[i + N + 1];
      out[i] = Math.hypot(gx, gy) / 1020;
    }
  }
  return out;
}

// Lower is better: how far the dots are from the target in tone (seen from a distance) and in
// edges, plus a penalty for isolated "salt and pepper" pixels.
async function score(bits: Bits, target: Gray): Promise<number> {
  const lit = Uint8Array.from(bits, (b) => (b ? 255 : 0));
  const [bTone, tTone, bEdge, tEdge] = await Promise.all([blur(lit, 1.6), blur(target, 1.6), blur(lit, 0.8), blur(target, 0.8)]);
  let tone = 0;
  for (let i = 0; i < N * N; i++) tone += ((bTone[i] - tTone[i]) / 255) ** 2;
  const eb = sobel(bEdge);
  const et = sobel(tEdge);
  let edge = 0;
  for (let i = 0; i < N * N; i++) edge += (eb[i] - et[i]) ** 2;
  let speckle = 0;
  for (let y = 1; y < N - 1; y++) {
    for (let x = 1; x < N - 1; x++) {
      const i = y * N + x;
      const v = bits[i];
      if (bits[i - 1] !== v && bits[i + 1] !== v && bits[i - N] !== v && bits[i + N] !== v) speckle++;
    }
  }
  return (tone + edge + 0.3 * speckle) / (N * N);
}

export async function coverToBitmap(image: Buffer): Promise<{ bitmap: Buffer; method: string }> {
  const base = new Uint8Array(await sharp(image).resize(N, N, { fit: "cover" }).removeAlpha().grayscale().raw().toBuffer());

  const preps = {
    balanced: await enhance(stretchGamma(base, 0.01, 0.99, 0.3), { width: 32, height: 32, maxSlope: 2 }, { sigma: 0.6, m1: 1, m2: 2 }),
    punchy: await enhance(stretchGamma(base, 0.05, 0.95, 0.3), { width: 16, height: 16, maxSlope: 3 }, { sigma: 1, m1: 1.5, m2: 3 }),
  };

  let best: { method: string; bits: Bits; score: number } = { method: "", bits: new Uint8Array(N * N), score: Infinity };
  for (const [prep, gray] of Object.entries(preps)) {
    const dithers = { atkinson: () => diffuse(gray, ATKINSON, 8), floyd: () => diffuse(gray, FLOYD, 16) };
    for (const [name, run] of Object.entries(dithers)) {
      const bits = run();
      const s = await score(bits, gray);
      if (s < best.score) best = { method: `${prep}+${name}`, bits, score: s };
    }
  }
  return { bitmap: pack(best.bits), method: best.method };
}

export async function artBitmap(url: string): Promise<Buffer> {
  const key = `spotify:art:v4:${url.split("/").pop()}`;
  const cached = await redis.get<string>(key);
  if (cached) return Buffer.from(cached, "base64");

  const res = await fetch(url);
  if (!res.ok) throw new Error(`album art fetch failed: ${res.status}`);
  const { bitmap } = await coverToBitmap(Buffer.from(await res.arrayBuffer()));
  await redis.set(key, bitmap.toString("base64"), { ex: 86_400 });
  return bitmap;
}

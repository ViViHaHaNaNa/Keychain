import sharp from "sharp";
import { redis } from "./state";

export const ART_SIZE = 64;

// Atkinson dither (the classic 1-bit Mac look) of a square grayscale image: it passes on only
// 6/8 of the error, so it gives crisper, higher-contrast results than Floyd-Steinberg on tiny
// screens. Output is row-major, MSB first, 1 = lit pixel: the layout Adafruit_GFX drawBitmap() expects.
const ATKINSON_NEIGHBOURS = [
  [1, 0],
  [2, 0],
  [-1, 1],
  [0, 1],
  [1, 1],
  [0, 2],
];

export function ditherToBitmap(gray: Uint8Array, size = ART_SIZE): Buffer {
  const px = Float32Array.from(gray);
  const out = Buffer.alloc((size * size) / 8);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const i = y * size + x;
      const lit = px[i] >= 128;
      if (lit) out[i >> 3] |= 0x80 >> (i & 7);
      const share = (px[i] - (lit ? 255 : 0)) / 8;
      for (const [dx, dy] of ATKINSON_NEIGHBOURS) {
        const nx = x + dx;
        const ny = y + dy;
        if (nx >= 0 && nx < size && ny < size) px[ny * size + nx] += share;
      }
    }
  }
  return out;
}

export async function artBitmap(url: string): Promise<Buffer> {
  const key = `spotify:art:v3:${url.split("/").pop()}`;
  const cached = await redis.get<string>(key);
  if (cached) return Buffer.from(cached, "base64");

  const res = await fetch(url);
  if (!res.ok) throw new Error(`album art fetch failed: ${res.status}`);
  const gray = await sharp(Buffer.from(await res.arrayBuffer()))
    .resize(ART_SIZE, ART_SIZE, { fit: "cover" })
    .removeAlpha()
    .grayscale()
    .normalise({ lower: 2, upper: 98 })
    .clahe({ width: 32, height: 32, maxSlope: 2 }) // gentle local contrast: keeps midtones instead of crushing them
    .sharpen({ sigma: 0.6, m1: 1, m2: 2 })
    .raw()
    .toBuffer();

  const bitmap = ditherToBitmap(gray);
  await redis.set(key, bitmap.toString("base64"), { ex: 86_400 });
  return bitmap;
}

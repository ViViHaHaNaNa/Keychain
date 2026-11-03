import sharp from "sharp";
import { redis } from "./state";

export const ART_SIZE = 64;

// 1-bit Floyd-Steinberg dither of a square grayscale image. Output is row-major,
// MSB first, 1 = lit pixel: the layout Adafruit_GFX drawBitmap() expects.
export function ditherToBitmap(gray: Uint8Array, size = ART_SIZE): Buffer {
  const px = Float32Array.from(gray);
  const out = Buffer.alloc((size * size) / 8);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const i = y * size + x;
      const lit = px[i] >= 128;
      if (lit) out[i >> 3] |= 0x80 >> (i & 7);
      const err = px[i] - (lit ? 255 : 0);
      if (x + 1 < size) px[i + 1] += (err * 7) / 16;
      if (y + 1 < size) {
        if (x > 0) px[i + size - 1] += (err * 3) / 16;
        px[i + size] += (err * 5) / 16;
        if (x + 1 < size) px[i + size + 1] += err / 16;
      }
    }
  }
  return out;
}

export async function artBitmap(url: string): Promise<Buffer> {
  const key = `spotify:art:${url.split("/").pop()}`;
  const cached = await redis.get<string>(key);
  if (cached) return Buffer.from(cached, "base64");

  const res = await fetch(url);
  if (!res.ok) throw new Error(`album art fetch failed: ${res.status}`);
  const gray = await sharp(Buffer.from(await res.arrayBuffer()))
    .resize(ART_SIZE, ART_SIZE, { fit: "cover" })
    .removeAlpha()
    .grayscale()
    .normalise()
    .raw()
    .toBuffer();

  const bitmap = ditherToBitmap(gray);
  await redis.set(key, bitmap.toString("base64"), { ex: 86_400 });
  return bitmap;
}

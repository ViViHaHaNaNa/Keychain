import { NextRequest, NextResponse } from "next/server";
import { artBitmap } from "@/lib/art";
import { safeEqual } from "@/lib/auth";
import { getNowPlaying } from "@/lib/spotify";

export const dynamic = "force-dynamic";

// 512 raw bytes: the current album art as a 64x64 1-bit bitmap (drawBitmap format).
// GET /api/art?key=<DEVICE_KEY>
export async function GET(req: NextRequest) {
  const expected = process.env.DEVICE_KEY;
  const key = req.nextUrl.searchParams.get("key");
  if (!expected || !key || !safeEqual(key, expected)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const { artUrl } = await getNowPlaying();
  if (!artUrl) return NextResponse.json({ error: "no art" }, { status: 404 });

  try {
    const bitmap = await artBitmap(artUrl);
    return new NextResponse(new Uint8Array(bitmap), {
      headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store" },
    });
  } catch {
    return NextResponse.json({ error: "art failed" }, { status: 502 });
  }
}

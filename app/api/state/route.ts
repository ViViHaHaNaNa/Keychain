import { NextRequest, NextResponse } from "next/server";
import { safeEqual } from "@/lib/auth";
import { nowPlayingText } from "@/lib/spotify";
import { getState } from "@/lib/state";

export const dynamic = "force-dynamic";

// Polled by the ESP32: GET /api/state?key=<DEVICE_KEY>
export async function GET(req: NextRequest) {
  const expected = process.env.DEVICE_KEY;
  const key = req.nextUrl.searchParams.get("key");
  if (!expected || !key || !safeEqual(key, expected)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const state = await getState();
  // In spotify mode the server fills `text` with the current track.
  if (state.mode === "spotify") state.text = await nowPlayingText();
  return NextResponse.json(state, { headers: { "Cache-Control": "no-store" } });
}

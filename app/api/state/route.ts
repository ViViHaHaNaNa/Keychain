import { NextRequest, NextResponse } from "next/server";
import { safeEqual } from "@/lib/auth";
import { artId, getNowPlaying, positionSeconds } from "@/lib/spotify";
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
  const headers = { "Cache-Control": "no-store" };
  if (state.mode !== "spotify") return NextResponse.json(state, { headers });

  // In spotify mode the server fills in the current track. `art` changes whenever
  // the album art does; fetch /api/art only when it differs from the last one.
  const np = await getNowPlaying();
  return NextResponse.json(
    {
      ...state,
      text: np.text,
      title: np.title,
      artist: np.artist,
      art: artId(np.artUrl),
      pos: positionSeconds(np), // seconds into the track; the device keeps counting while `play` is 1
      dur: Math.round(np.durationMs / 1000),
      play: np.playing ? 1 : 0,
    },
    { headers },
  );
}

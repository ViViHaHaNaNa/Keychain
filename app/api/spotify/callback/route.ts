import { NextRequest, NextResponse } from "next/server";
import { isAuthed, safeEqual } from "@/lib/auth";
import { connectSpotify } from "@/lib/spotify";

export const dynamic = "force-dynamic";

export async function GET(req: NextRequest) {
  const origin = req.nextUrl.origin;
  const code = req.nextUrl.searchParams.get("code");
  const state = req.nextUrl.searchParams.get("state");
  const expected = req.cookies.get("spotify_state")?.value;

  if (!(await isAuthed()) || !code || !state || !expected || !safeEqual(state, expected)) {
    return NextResponse.redirect(new URL("/?spotify=error", origin));
  }

  try {
    await connectSpotify(code, origin);
  } catch {
    return NextResponse.redirect(new URL("/?spotify=error", origin));
  }
  const res = NextResponse.redirect(new URL("/?spotify=connected", origin));
  res.cookies.delete({ name: "spotify_state", path: "/api/spotify" });
  return res;
}

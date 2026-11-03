import { randomBytes } from "node:crypto";
import { NextRequest, NextResponse } from "next/server";
import { isAuthed } from "@/lib/auth";
import { spotifyAuthorizeUrl } from "@/lib/spotify";

export const dynamic = "force-dynamic";

// Starts the one-time Spotify connection (visit while logged in).
export async function GET(req: NextRequest) {
  if (!(await isAuthed())) {
    return NextResponse.redirect(new URL("/", req.nextUrl.origin));
  }
  const state = randomBytes(16).toString("hex");
  const res = NextResponse.redirect(spotifyAuthorizeUrl(req.nextUrl.origin, state));
  res.cookies.set("spotify_state", state, {
    httpOnly: true,
    secure: process.env.NODE_ENV === "production",
    sameSite: "lax",
    path: "/api/spotify",
    maxAge: 600,
  });
  return res;
}

import { NextResponse } from "next/server";
import { isAuthed } from "@/lib/auth";
import { getNowPlaying } from "@/lib/spotify";

export const dynamic = "force-dynamic";

// Used by the control page to preview what the keychain will show.
export async function GET() {
  if (!(await isAuthed())) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const { text } = await getNowPlaying();
  return NextResponse.json({ text }, { headers: { "Cache-Control": "no-store" } });
}

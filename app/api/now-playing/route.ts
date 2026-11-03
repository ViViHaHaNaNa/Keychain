import { NextResponse } from "next/server";
import { isAuthed } from "@/lib/auth";
import { nowPlayingText } from "@/lib/spotify";

export const dynamic = "force-dynamic";

// Used by the control page to preview what the keychain will show.
export async function GET() {
  if (!(await isAuthed())) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  return NextResponse.json({ text: await nowPlayingText() }, { headers: { "Cache-Control": "no-store" } });
}

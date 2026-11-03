import { NextRequest, NextResponse } from "next/server";
import { isAuthed } from "@/lib/auth";
import { parseState } from "@/lib/schema";
import { setState } from "@/lib/state";

export const dynamic = "force-dynamic";

// Called by the control page (cookie-authenticated).
export async function PUT(req: NextRequest) {
  if (!(await isAuthed())) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const state = parseState(await req.json().catch(() => null));
  if (!state) {
    return NextResponse.json({ error: "invalid state" }, { status: 400 });
  }
  await setState(state);
  return NextResponse.json(state);
}

import { isAuthed } from "@/lib/auth";
import { getState } from "@/lib/state";
import Controls from "./controls";
import Login from "./login";

export const dynamic = "force-dynamic";

export default async function Page() {
  if (!(await isAuthed())) return <Login />;
  return <Controls initial={await getState()} />;
}

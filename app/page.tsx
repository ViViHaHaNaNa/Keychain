import { isAuthed } from "@/lib/auth";
import { isSpotifyConnected } from "@/lib/spotify";
import { getState } from "@/lib/state";
import Controls from "./controls";
import Login from "./login";

export const dynamic = "force-dynamic";

export default async function Page() {
  if (!(await isAuthed())) return <Login />;
  const [initial, spotifyConnected] = await Promise.all([getState(), isSpotifyConnected()]);
  return <Controls initial={initial} spotifyConnected={spotifyConnected} />;
}

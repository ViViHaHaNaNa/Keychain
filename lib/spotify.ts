import { MAX_TEXT } from "./schema";
import { redis } from "./state";

const REFRESH_KEY = "spotify:refresh_token";
const ACCESS_KEY = "spotify:access_token";
const NOW_PLAYING_KEY = "spotify:now_playing";

export type NowPlaying = {
  text: string; // "Title - Artist", or a short status message
  title: string;
  artist: string;
  artUrl: string; // Spotify image URL, "" when there is none
  progressMs: number;
  durationMs: number; // 0 when unknown (status messages)
  playing: boolean;
  fetchedAt: number; // Date.now() when Spotify was asked, to advance progress while cached
};

function basicAuth() {
  const id = process.env.SPOTIFY_CLIENT_ID;
  const secret = process.env.SPOTIFY_CLIENT_SECRET;
  if (!id || !secret) throw new Error("SPOTIFY_CLIENT_ID / SPOTIFY_CLIENT_SECRET not set");
  return "Basic " + Buffer.from(`${id}:${secret}`).toString("base64");
}

async function tokenRequest(params: Record<string, string>) {
  const res = await fetch("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: { Authorization: basicAuth(), "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(params),
  });
  if (!res.ok) throw new Error(`Spotify token request failed: ${res.status}`);
  return (await res.json()) as { access_token: string; expires_in: number; refresh_token?: string };
}

export function spotifyRedirectUri(origin: string) {
  return `${origin}/api/spotify/callback`;
}

export function spotifyAuthorizeUrl(origin: string, state: string) {
  const params = new URLSearchParams({
    response_type: "code",
    client_id: process.env.SPOTIFY_CLIENT_ID ?? "",
    scope: "user-read-currently-playing",
    redirect_uri: spotifyRedirectUri(origin),
    state,
  });
  return `https://accounts.spotify.com/authorize?${params}`;
}

export async function connectSpotify(code: string, origin: string) {
  const token = await tokenRequest({
    grant_type: "authorization_code",
    code,
    redirect_uri: spotifyRedirectUri(origin),
  });
  if (!token.refresh_token) throw new Error("Spotify returned no refresh token");
  await redis.set(REFRESH_KEY, token.refresh_token);
  await redis.set(ACCESS_KEY, token.access_token, { ex: token.expires_in - 60 });
}

export async function isSpotifyConnected() {
  return (await redis.exists(REFRESH_KEY)) === 1;
}

async function accessToken() {
  const cached = await redis.get<string>(ACCESS_KEY);
  if (cached) return cached;
  const refresh = await redis.get<string>(REFRESH_KEY);
  if (!refresh) return null;
  const token = await tokenRequest({ grant_type: "refresh_token", refresh_token: refresh });
  // Spotify may rotate the refresh token; keep the newest one.
  if (token.refresh_token) await redis.set(REFRESH_KEY, token.refresh_token);
  await redis.set(ACCESS_KEY, token.access_token, { ex: token.expires_in - 60 });
  return token.access_token;
}

const status = (msg: string): NowPlaying => ({
  text: msg,
  title: msg,
  artist: "",
  artUrl: "",
  progressMs: 0,
  durationMs: 0,
  playing: false,
  fetchedAt: Date.now(),
});

// Playback position in seconds as of now, moving forward while the track is playing.
export function positionSeconds(np: NowPlaying) {
  const elapsed = np.playing ? Date.now() - np.fetchedAt : 0;
  return Math.round(Math.min(np.durationMs, np.progressMs + elapsed) / 1000);
}
// The OLED font is ASCII-only: strip accents (é -> e) and straighten curly quotes.
const fold = (s: string) =>
  s
    .normalize("NFD")
    .replace(/[̀-ͯ]/g, "")
    .replace(/[‘’]/g, "'")
    .replace(/[“”]/g, '"')
    .replace(/[–—]/g, "-");
const clip = (s: string) => (s.length > MAX_TEXT ? s.slice(0, MAX_TEXT - 3) + "..." : s);

// Short id for an album image, sent to the ESP32 so it only re-downloads art when it changes.
export const artId = (artUrl: string) => artUrl.split("/").pop() ?? "";

// The result is cached for a few seconds so ESP32 polling doesn't hammer Spotify.
export async function getNowPlaying(): Promise<NowPlaying> {
  const cached = await redis.get<NowPlaying>(NOW_PLAYING_KEY);
  if (cached && typeof cached === "object") return cached;

  let result: NowPlaying;
  try {
    const token = await accessToken();
    if (!token) return status("Spotify not connected");
    const res = await fetch("https://api.spotify.com/v1/me/player/currently-playing", {
      headers: { Authorization: `Bearer ${token}` },
    });
    if (res.status === 204) {
      result = status("Nothing playing");
    } else if (!res.ok) {
      return status("Spotify error");
    } else {
      const data = await res.json();
      const item = data?.item;
      if (!item?.name) {
        result = status("Nothing playing");
      } else {
        const title = clip(fold(item.name));
        const artist = clip(fold((item.artists ?? []).map((a: { name: string }) => a.name).join(", ")));
        // images are listed largest first; the largest gives the cleanest downscale to 64x64
        const images: { url: string }[] = item.album?.images ?? [];
        const artUrl = images[0]?.url ?? "";
        result = {
          text: clip(`${title} - ${artist}`),
          title,
          artist,
          artUrl,
          progressMs: data.progress_ms ?? 0,
          durationMs: item.duration_ms ?? 0,
          playing: data.is_playing === true,
          fetchedAt: Date.now(),
        };
      }
    }
  } catch {
    return status("Spotify error");
  }

  await redis.set(NOW_PLAYING_KEY, result, { ex: 5 });
  return result;
}

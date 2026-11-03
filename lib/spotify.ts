import { MAX_TEXT } from "./schema";
import { redis } from "./state";

const REFRESH_KEY = "spotify:refresh_token";
const ACCESS_KEY = "spotify:access_token";
const NOW_PLAYING_KEY = "spotify:now_playing";

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

// Returns "Title - Artist" for the current track, or a short status message.
// The result is cached for a few seconds so ESP32 polling doesn't hammer Spotify.
export async function nowPlayingText(): Promise<string> {
  const cached = await redis.get<string>(NOW_PLAYING_KEY);
  if (cached !== null) return cached;

  let text: string;
  try {
    const token = await accessToken();
    if (!token) return "Spotify not connected";
    const res = await fetch("https://api.spotify.com/v1/me/player/currently-playing", {
      headers: { Authorization: `Bearer ${token}` },
    });
    if (res.status === 204) {
      text = "Nothing playing";
    } else if (!res.ok) {
      return "Spotify error";
    } else {
      const data = await res.json();
      const item = data?.item;
      text = item?.name
        ? `${item.name} - ${(item.artists ?? []).map((a: { name: string }) => a.name).join(", ")}`
        : "Nothing playing";
    }
  } catch {
    return "Spotify error";
  }

  text = text.length > MAX_TEXT ? text.slice(0, MAX_TEXT - 1) + "…" : text;
  await redis.set(NOW_PLAYING_KEY, text, { ex: 5 });
  return text;
}

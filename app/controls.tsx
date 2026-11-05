"use client";

import { useEffect, useState } from "react";
import { ANIMS, MAX_CAPTION, MAX_TEXT, MODES, MOODS, type State } from "@/lib/schema";

const MODE_LABELS: Record<State["mode"], string> = {
  eyes: "👀 Eyes",
  clock: "🕒 Clock",
  text: "✏️ Text",
  spotify: "🎵 Spotify",
};
const MOOD_LABELS: Record<State["mood"], string> = {
  happy: "😊 Happy",
  sad: "😢 Sad",
  angry: "😠 Angry",
  sleepy: "😴 Sleepy",
  surprised: "😮 Surprised",
  neutral: "😐 Neutral",
};

export default function Controls({
  initial,
  spotifyConnected,
}: {
  initial: State;
  spotifyConnected: boolean;
}) {
  const [state, setState] = useState(initial);
  const [status, setStatus] = useState<"idle" | "saving" | "saved" | "error">("idle");

  const [nowPlaying, setNowPlaying] = useState("…");
  const previewSpotify = state.mode === "spotify" && spotifyConnected;

  useEffect(() => {
    if (!previewSpotify) return;
    let cancelled = false;
    async function poll() {
      const res = await fetch("/api/now-playing").catch(() => null);
      if (cancelled || !res?.ok) return;
      setNowPlaying((await res.json()).text);
    }
    poll();
    const id = setInterval(poll, 5000);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [previewSpotify]);

  async function save(next: State) {
    setState(next);
    setStatus("saving");
    const res = await fetch("/api/mode", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(next),
    });
    if (res.status === 401) return location.reload();
    setStatus(res.ok ? "saved" : "error");
  }

  async function logout() {
    await fetch("/api/login", { method: "DELETE" });
    location.reload();
  }

  return (
    <main>
      <h1>Keychain</h1>

      <section className="card">
        <h2>Mode</h2>
        <div className="grid two">
          {MODES.map((m) => (
            <button key={m} className={state.mode === m ? "on" : ""} onClick={() => save({ ...state, mode: m })}>
              {MODE_LABELS[m]}
            </button>
          ))}
        </div>
      </section>

      {state.mode === "eyes" && (
        <section className="card">
          <h2>Mood</h2>
          <div className="grid two">
            {MOODS.map((m) => (
              <button key={m} className={state.mood === m ? "on" : ""} onClick={() => save({ ...state, mood: m })}>
                {MOOD_LABELS[m]}
              </button>
            ))}
          </div>
        </section>
      )}

      {state.mode === "text" && (
        <form
          className="card"
          onSubmit={(e) => {
            e.preventDefault();
            save(state);
          }}
        >
          <h2>Text</h2>
          <textarea
            rows={3}
            value={state.text}
            maxLength={MAX_TEXT}
            placeholder="What should it say?"
            onChange={(e) => setState({ ...state, text: e.target.value })}
          />
          <p className="note">Lines only break where you press Enter.</p>
          <label className="field">
            Animation
            <select value={state.anim} onChange={(e) => save({ ...state, anim: e.target.value as State["anim"] })}>
              {ANIMS.map((a) => (
                <option key={a} value={a}>
                  {a}
                </option>
              ))}
            </select>
          </label>
          <button className="primary">Send</button>
        </form>
      )}

      {state.mode === "clock" && (
        <form
          className="card"
          onSubmit={(e) => {
            e.preventDefault();
            save(state);
          }}
        >
          <h2>Caption</h2>
          <input
            value={state.caption}
            maxLength={MAX_CAPTION}
            placeholder="Shown under the date (optional)"
            onChange={(e) => setState({ ...state, caption: e.target.value })}
          />
          <button className="primary">Send</button>
        </form>
      )}

      {state.mode === "spotify" && (
        <section className="card">
          <h2>Spotify</h2>
          {spotifyConnected ? (
            <>
              <p className="note">Connected. The keychain shows:</p>
              <p className="now-playing">{nowPlaying}</p>
            </>
          ) : (
            <a className="button primary" href="/api/spotify/login">
              Connect Spotify
            </a>
          )}
        </section>
      )}

      <p className="status" aria-live="polite">
        {status === "saving" && "Saving…"}
        {status === "saved" && "Saved ✓"}
        {status === "error" && "Couldn't save — try again"}
      </p>
      <button className="link" onClick={logout}>
        Log out
      </button>
    </main>
  );
}

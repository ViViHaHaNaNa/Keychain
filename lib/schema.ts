export const MODES = ["eyes", "clock", "text", "spotify"] as const;
export const MOODS = ["happy", "sad", "angry", "sleepy", "surprised", "neutral"] as const;
export const ANIMS = ["none", "typewriter", "wave", "invert", "glitch", "bounce"] as const;
export const MAX_TEXT = 64;
export const MAX_CAPTION = 32;

export type State = {
  mode: (typeof MODES)[number];
  mood: (typeof MOODS)[number];
  text: string; // text mode: the message
  anim: (typeof ANIMS)[number]; // text mode: how the message moves
  caption: string; // clock mode: the line under the date
};

export const DEFAULT_STATE: State = { mode: "eyes", mood: "happy", text: "", anim: "none", caption: "" };

// `anim` and `caption` are optional so states saved before they existed still load.
export function parseState(input: unknown): State | null {
  if (typeof input !== "object" || input === null) return null;
  const { mode, mood, text, anim = "none", caption = "" } = input as Record<string, unknown>;
  if (!MODES.includes(mode as State["mode"])) return null;
  if (!MOODS.includes(mood as State["mood"])) return null;
  if (!ANIMS.includes(anim as State["anim"])) return null;
  if (typeof text !== "string" || text.length > MAX_TEXT) return null;
  if (typeof caption !== "string" || caption.length > MAX_CAPTION) return null;
  return {
    mode: mode as State["mode"],
    mood: mood as State["mood"],
    text,
    anim: anim as State["anim"],
    caption,
  };
}

export const MODES = ["eyes", "clock", "text", "spotify"] as const;
export const MOODS = ["happy", "sad", "angry", "sleepy", "surprised", "neutral"] as const;
export const ANIMS = ["none", "typewriter", "wave", "invert", "glitch", "bounce"] as const;
export const TEXT_SIZES = [0, 1, 2, 3, 4] as const; // 0 = biggest that fits
export const MAX_TEXT = 64;
export const MAX_CAPTION = 32;

export type State = {
  mode: (typeof MODES)[number];
  mood: (typeof MOODS)[number];
  text: string; // text mode: the message
  anim: (typeof ANIMS)[number]; // text mode: how the message moves
  size: (typeof TEXT_SIZES)[number]; // text mode: font size
  caption: string; // clock mode: the line under the date
};

export const DEFAULT_STATE: State = { mode: "eyes", mood: "happy", text: "", anim: "none", size: 0, caption: "" };

// `anim`, `size` and `caption` are optional so states saved before they existed still load.
export function parseState(input: unknown): State | null {
  if (typeof input !== "object" || input === null) return null;
  const { mode, mood, text, anim = "none", size = 0, caption = "" } = input as Record<string, unknown>;
  if (!MODES.includes(mode as State["mode"])) return null;
  if (!MOODS.includes(mood as State["mood"])) return null;
  if (!ANIMS.includes(anim as State["anim"])) return null;
  if (!TEXT_SIZES.includes(size as State["size"])) return null;
  if (typeof text !== "string" || text.length > MAX_TEXT) return null;
  if (typeof caption !== "string" || caption.length > MAX_CAPTION) return null;
  return {
    mode: mode as State["mode"],
    mood: mood as State["mood"],
    text,
    anim: anim as State["anim"],
    size: size as State["size"],
    caption,
  };
}

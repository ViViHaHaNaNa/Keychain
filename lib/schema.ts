export const MODES = ["eyes", "clock", "text"] as const;
export const MOODS = ["happy", "sad", "angry", "sleepy", "surprised", "neutral"] as const;
export const MAX_TEXT = 64;

export type State = {
  mode: (typeof MODES)[number];
  mood: (typeof MOODS)[number];
  text: string;
};

export const DEFAULT_STATE: State = { mode: "eyes", mood: "happy", text: "" };

export function parseState(input: unknown): State | null {
  if (typeof input !== "object" || input === null) return null;
  const { mode, mood, text } = input as Record<string, unknown>;
  if (!MODES.includes(mode as State["mode"])) return null;
  if (!MOODS.includes(mood as State["mood"])) return null;
  if (typeof text !== "string" || text.length > MAX_TEXT) return null;
  return { mode: mode as State["mode"], mood: mood as State["mood"], text };
}

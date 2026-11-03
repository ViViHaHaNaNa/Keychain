import { Redis } from "@upstash/redis";
import { DEFAULT_STATE, parseState, type State } from "./schema";

const KEY = "keychain:state";

export const redis = new Redis({
  url: (process.env.UPSTASH_REDIS_REST_URL ?? process.env.KV_REST_API_URL)!,
  token: (process.env.UPSTASH_REDIS_REST_TOKEN ?? process.env.KV_REST_API_TOKEN)!,
});

export async function getState(): Promise<State> {
  return parseState(await redis.get(KEY)) ?? DEFAULT_STATE;
}

export async function setState(state: State) {
  await redis.set(KEY, state);
}

// Counts attempts per key within a window; returns true once the limit is exceeded.
export async function tooManyAttempts(id: string, limit = 10, windowSec = 900) {
  const key = `ratelimit:${id}`;
  const n = await redis.incr(key);
  if (n === 1) await redis.expire(key, windowSec);
  return n > limit;
}

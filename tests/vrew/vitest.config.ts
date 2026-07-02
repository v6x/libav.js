import { defineConfig } from "vitest/config";

export default defineConfig({
  root: __dirname,
  server: {
    watch: null,
    fs: { strict: false },
  },
  test: {
    include: ["*.test.ts"],
    // The libav.js wasm runs in its own Worker; use forked child processes
    // (not the default worker-thread pool) to avoid nested-worker issues.
    pool: "forks",
    watch: false,
    testTimeout: 30000,
    hookTimeout: 30000,
  },
});

import path from "node:path";
import { fileURLToPath } from "node:url";
import { searchForWorkspaceRoot } from "vite";
import { defineConfig } from "vitest/config";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, "../..");

const publicHost =
  process.env.SILVERIRIS_PUBLIC_HOST ?? "007.goodhouseinc.com";

const csp =
  "default-src 'self'; script-src 'self' 'wasm-unsafe-eval' blob:; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' stun:007.goodhouseinc.com:3478 turn:007.goodhouseinc.com:3478 turns:007.goodhouseinc.com:5349 stun:stun.l.google.com:19302; media-src 'self' blob:; worker-src 'self' blob:; object-src 'none'; base-uri 'self'; frame-ancestors 'none';";

export default defineConfig({
  root: ".",
  publicDir: "public",
  resolve: {
    alias: {
      fflate: path.resolve(here, "node_modules/fflate"),
    },
  },
  worker: {
    format: "es",
  },
  server: {
    host: "127.0.0.1",
    port: 5173,
    strictPort: true,
    allowedHosts: [publicHost],
    fs: {
      allow: [searchForWorkspaceRoot(here), repoRoot],
    },
    hmr: {
      host: publicHost,
      protocol: "wss",
      clientPort: 443,
    },
    headers: {
      "Content-Security-Policy": csp,
    },
    proxy: {
      "/ws": { target: "http://127.0.0.1:18787", ws: true },
      "/api": { target: "http://127.0.0.1:18787" },
    },
  },
  preview: {
    host: "127.0.0.1",
    port: 4173,
    strictPort: true,
    allowedHosts: [publicHost],
    headers: {
      "Content-Security-Policy": csp,
    },
  },
  build: {
    outDir: "dist",
    sourcemap: true,
    target: "es2022",
  },
  test: {
    environment: "node",
    include: ["src/**/*.test.ts", "../extractor/src/**/*.test.ts"],
  },
});

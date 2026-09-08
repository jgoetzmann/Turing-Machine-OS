import { defineConfig } from 'vite';

// GitHub Pages serves the site under the repository name, so every asset URL is prefixed.
const buildSha = process.env.GITHUB_SHA ?? 'dev';

export default defineConfig({
  base: '/Turing-Machine-OS/',
  define: {
    __BUILD_SHA__: JSON.stringify(buildSha),
    __BUILD_TIME__: JSON.stringify(new Date().toISOString()),
  },
  build: {
    target: 'es2022',
    outDir: 'dist',
    assetsInlineLimit: 0,
    sourcemap: false,
  },
  server: {
    port: 5173,
  },
});

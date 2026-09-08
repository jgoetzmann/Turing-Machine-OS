/* Serve the built site and run the Cypress end-to-end suite against it, then stop the server.
   Usage: node scripts/e2e.mjs [--dev]   (--dev serves src/ instead of dist/) */
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const dev = process.argv.includes('--dev');
const port = dev ? 5173 : 4173;
const url = `http://localhost:${port}/Turing-Machine-OS/`;

const server = spawn('npx', ['vite', dev ? '' : 'preview', '--port', String(port), '--strictPort'].filter(Boolean), {
  stdio: 'inherit',
  env: process.env,
});

async function waitForServer() {
  for (let i = 0; i < 120; i++) {
    try {
      const r = await fetch(url);
      if (r.ok) return true;
    } catch {
      /* not up yet */
    }
    await sleep(500);
  }
  return false;
}

const up = await waitForServer();
if (!up) {
  server.kill('SIGTERM');
  console.error(`e2e: ${url} never came up`);
  process.exit(1);
}

const cy = spawn('npx', ['cypress', 'run', '--config', `baseUrl=${url}`], { stdio: 'inherit', env: process.env });
const code = await new Promise((resolve) => cy.on('exit', resolve));
server.kill('SIGTERM');
process.exit(code ?? 1);

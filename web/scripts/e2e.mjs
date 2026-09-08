/* Serve the built site and run the Cypress end-to-end suite against it, then stop the server.
   Usage: node scripts/e2e.mjs [--dev]   (--dev serves src/ instead of dist/) */
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

/* The browser binary belongs to the project, not to the user's home directory: this is what keeps
   `npm ci` + `npx cypress install` self-contained (see web/.npmrc, which does the same for npm). */
process.env.CYPRESS_CACHE_FOLDER = process.env.CYPRESS_CACHE_FOLDER ?? 'node_modules/.cache/cypress';

const dev = process.argv.includes('--dev');
const port = dev ? 5173 : 4173;
const url = `http://localhost:${port}/Turing-Machine-OS/`;

const server = spawn('npx', ['vite', dev ? '' : 'preview', '--port', String(port), '--strictPort'].filter(Boolean), {
  stdio: 'inherit',
  env: process.env,
});

let serverExited = false;
server.on('exit', (code) => {
  serverExited = true;
  if (code) console.error(`e2e: the server exited with code ${code} (is port ${port} already in use?)`);
});

/* Never leave a server behind: not when Cypress fails, not when this process is interrupted. */
const stopServer = () => {
  if (!serverExited) server.kill('SIGTERM');
};
process.on('exit', stopServer);
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => { stopServer(); process.exit(130); });

async function waitForServer() {
  for (let i = 0; i < 120; i++) {
    if (serverExited) return false;
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
  stopServer();
  console.error(`e2e: ${url} never came up`);
  process.exit(1);
}

const cy = spawn('npx', ['cypress', 'run', '--config', `baseUrl=${url}`], { stdio: 'inherit', env: process.env });
const code = await new Promise((resolve) => cy.on('exit', resolve));
stopServer();
process.exit(code ?? 1);

import { defineConfig } from 'cypress';

// The site is served under the repository name, in dev (`npm run dev`) as in production.
export default defineConfig({
  e2e: {
    baseUrl: 'http://localhost:5173/Turing-Machine-OS/',
    supportFile: 'cypress/support/e2e.js',
    specPattern: 'cypress/e2e/**/*.cy.js',
    video: false,
    screenshotOnRunFailure: false,
    viewportWidth: 1600,
    viewportHeight: 1200,
    defaultCommandTimeout: 20000,
    retries: 0,
    setupNodeEvents(on) {
      on('task', { log(m) { console.log(m); return null; } });
    },
  },
});

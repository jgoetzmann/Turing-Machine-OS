/* Every spec fails on an uncaught exception or a console error: a panel that throws while
   mounting still leaves its slot in the DOM, so silence is not evidence that it works. */
const errors = [];

Cypress.on('window:before:load', (win) => {
  const realError = win.console.error.bind(win.console);
  win.console.error = (...args) => {
    errors.push(args.map(String).join(' '));
    realError(...args);
  };
  win.addEventListener('error', (e) => errors.push(`window error: ${e.message}`));
  win.addEventListener('unhandledrejection', (e) => errors.push(`unhandled rejection: ${e.reason}`));
});

beforeEach(() => {
  errors.length = 0;
});

afterEach(() => {
  expect(errors, 'console errors during the test').to.deep.equal([]);
});

/** Wait until the machine has booted to the shell prompt and the panels are live. */
Cypress.Commands.add('bootPlayground', (hash = '#/playground') => {
  cy.visit(hash);
  cy.get('[data-out="status"]', { timeout: 60000 }).should('contain', 'step');
  cy.get('#panel-console .slot-body', { timeout: 60000 }).should('contain', 'A>');
});

/** Assert a canvas has actually drawn something: more than one distinct pixel colour.
    Written as a `should` callback so Cypress retries while the machine is still painting. */
Cypress.Commands.add('canvasPainted', (selector, minColors = 2) => {
  cy.get(selector).first().should(($c) => {
    const canvas = $c[0];
    expect(canvas.width, 'canvas width').to.be.greaterThan(0);
    expect(canvas.height, 'canvas height').to.be.greaterThan(0);
    const { data } = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height);
    const seen = new Set();
    for (let i = 0; i < data.length; i += 4) {
      seen.add((data[i] << 24) | (data[i + 1] << 16) | (data[i + 2] << 8) | data[i + 3]);
      if (seen.size >= minColors) break;
    }
    expect(seen.size, `distinct pixel colours in ${selector}`).to.be.at.least(minColors);
  });
});

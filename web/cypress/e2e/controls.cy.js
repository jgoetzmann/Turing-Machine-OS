/* Toolbar, breakpoints, time travel, the overlays and the site chrome.

   The playground boots the shell and leaves it running, parked at the prompt in WAIT_INPUT, so
   tests that need the head to actually move load a demo (pong runs forever) or send a command. */

const stepOf = (t) => {
  const m = /step ([\d,]+)/.exec(t);
  return m ? parseInt(m[1].replace(/,/g, ''), 10) : NaN;
};

describe('execution controls', () => {
  beforeEach(() => cy.bootPlayground());

  it('boots running and the run button pauses and resumes', () => {
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
    cy.get('[data-out="status"]').should('contain', 'Running');
    cy.get('[data-act="toggle"]').click().should('have.text', 'Run');
    cy.get('[data-out="status"]').should('contain', 'Paused');
    cy.get('[data-act="toggle"]').click().should('have.text', 'Pause');
    cy.get('[data-out="status"]').should('contain', 'Running');
  });

  it('says so instead of doing nothing when the machine is parked for input', () => {
    cy.get('[data-out="status"]').should('contain', 'waiting for input');
    cy.get('[data-step="1"]').click();
    cy.get('[data-out="status"]').should('contain', 'Waiting for input: type a command in the Console panel');
  });

  it('every step button advances by its own amount', () => {
    // Pause first, then queue a command: the bytes wait in the console buffer, so every step
    // the test asks for is a step the machine can actually take.
    cy.get('[data-act="toggle"]').click();
    cy.get('#panel-console input[type="text"]').type('dir{enter}dir{enter}dir{enter}dir{enter}');
    for (const n of [1, 10, 100, 1000]) {
      cy.get('[data-out="status"]').invoke('text').then((before) => {
        cy.get(`[data-step="${n}"]`).click();
        cy.get('[data-out="status"]').should(($s) => {
          expect(stepOf($s.text()) - stepOf(before), `step ×${n}`).to.equal(n);
        });
      });
    }
  });

  it('a targeted run stays armed while the machine waits for a command', () => {
    // Run to HALT at the prompt: the target arrives with the next thing the user types.
    cy.get('[data-act="to-halt"]').click();
    cy.get('[data-out="status"]').should('contain', 'Waiting for input');
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
    cy.get('#panel-console input[type="text"]').type('halt{enter}');
    cy.get('[data-out="status"]', { timeout: 30000 }).should('contain', 'Machine halted');
  });

  it('step over syscall, run to halt and run to state change all report what they did', () => {
    cy.get('[data-act="toggle"]').click();
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('[data-act="over"]').click();
    cy.get('[data-out="status"]', { timeout: 30000 }).should('contain', 'Stepped over');
    cy.get('[data-act="to-state"]').click();
    cy.get('[data-out="status"]', { timeout: 30000 }).should('contain', 'State changed to');
    cy.get('#panel-console input[type="text"]').type('halt{enter}');
    cy.get('[data-act="to-halt"]').click();
    cy.get('[data-out="status"]', { timeout: 30000 }).should('contain', 'Machine halted');
    cy.get('[data-out="status"]').should('contain', 'halt');
  });

  it('reset reboots the machine', () => {
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.wait(500);
    cy.get('[data-act="reset"]').click();
    cy.get('[data-out="status"]').should('contain', 'Machine reset');
    cy.get('#panel-console .slot-body').should('contain', 'A>');
    cy.get('[data-out="status"]').should(($s) => expect(stepOf($s.text())).to.be.lessThan(1000));
  });

  it('the speed slider changes the reported speed', () => {
    cy.get('[data-ctl="speed"]').invoke('val', 4).trigger('input');
    cy.get('[data-out="speed"]').should('not.have.text', 'max');
    cy.get('[data-out="status"]').should('not.contain', 'Running (max)');
  });

  it('adds a breakpoint, removes it with the × button, and clears all', () => {
    cy.get('[data-ctl="bp-lo"]').clear().type('0100');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 1).and('contain', '0100');

    cy.get('[data-out="bp-list"] li button').should('have.text', '×').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 0);

    cy.get('[data-ctl="bp-lo"]').clear().type('0100');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('[data-ctl="bp-lo"]').clear().type('0200');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 2);
    cy.get('[data-act="bp-clear"]').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 0);
  });

  it('rejects a breakpoint that is not a number', () => {
    cy.get('[data-ctl="bp-lo"]').clear().type('zzz');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('[data-out="status"]').should('contain', 'Breakpoint: enter hex');
    cy.get('[data-out="bp-list"] li').should('have.length', 0);
  });

  it('a PC breakpoint actually stops the machine there', () => {
    cy.get('[data-ctl="bp-lo"]').clear().type('0100');
    cy.get('[data-ctl="bp-hi"]').clear().type('01ff');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('[data-out="status"]', { timeout: 30000 }).should('contain', 'Breakpoint #0 hit');
    cy.get('[data-act="toggle"]').should('have.text', 'Run');
    cy.get('[data-out="status"]').should('contain', 'PC 01');
  });

  it('time travel seeks backwards and keeps running forwards', () => {
    cy.visit('#/playground?demo=pong');
    cy.get('[data-out="status"]', { timeout: 60000 }).should('contain', 'frame');
    cy.wait(1500);
    cy.get('[data-act="toggle"]').click();
    cy.get('[data-ctl="scrub"]').should('not.be.disabled');
    cy.get('[data-ctl="scrub"]').invoke('val').then((max) => {
      const target = Math.floor(Number(max) / 2);
      cy.get('[data-ctl="scrub"]').invoke('val', target).trigger('input');
      cy.get('[data-out="status"]').should('contain', 'Seeked to step');
      cy.get('[data-out="status"]').should(($s) => {
        expect(Math.abs(stepOf($s.text()) - target)).to.be.lessThan(2);
      });
      cy.get('[data-step="1000"]').click();
      cy.get('[data-out="status"]').should(($s) => {
        expect(stepOf($s.text())).to.be.greaterThan(target);
      });
    });
  });

  it('help opens and both closers work', () => {
    cy.get('[data-act="help"]').click();
    cy.get('[data-help]').should('be.visible');
    cy.get('[data-act="help-close"]').click();
    cy.get('[data-help]').should('not.be.visible');
    cy.get('body').type('?');
    cy.get('[data-help]').should('be.visible');
    cy.focused().type('{esc}');
    cy.get('[data-help]').should('not.be.visible');
  });

  it('exports and the share link report what they did', () => {
    cy.get('[data-act="export-png"]').click();
    cy.get('[data-act="export-trace"]').click();
    cy.get('[data-out="status"]').should('contain', 'Exported the last 4096 trace events');
    cy.get('[data-act="export-disk"]').click();
    cy.get('[data-out="status"]').should('contain', 'Exported disk A');
    cy.get('[data-act="share"]').click();
    cy.get('[data-out="status"]').should('contain', 'Link copied');
  });

  it('keyboard shortcuts run, step and reset', () => {
    cy.get('body').type(' ');
    cy.get('[data-act="toggle"]').should('have.text', 'Run');
    cy.get('body').type(' ');
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
    cy.get('body').type('.');
    cy.get('[data-out="status"]').should('contain', 'Waiting for input');
    cy.get('body').type('r');
    cy.get('[data-out="status"]').should('contain', 'Machine reset');
  });
});

describe('site chrome', () => {
  it('the GitHub links point at the repository and resolve', () => {
    cy.visit('#/');
    cy.get('.site-footer a').contains('GitHub').should('have.attr', 'href', 'https://github.com/jgoetzmann/Turing-Machine-OS');
    cy.get('.site-footer a[data-sha]').should('have.attr', 'href').and('include', 'github.com/jgoetzmann/Turing-Machine-OS');
    cy.get('.site-footer a').contains('GitHub').then(($a) => {
      cy.request($a.attr('href')).its('status').should('eq', 200);
    });
  });

  it('every nav route renders', () => {
    cy.visit('#/');
    cy.get('#site-nav a').then(($as) => {
      const paths = [...$as].map((a) => a.getAttribute('href'));
      expect(paths.length).to.be.greaterThan(2);
      for (const p of paths) {
        cy.visit(p);
        cy.get('#main').should('not.be.empty').and('not.contain', 'Not found');
      }
    });
  });

  it('the theme toggle cycles', () => {
    cy.visit('#/');
    cy.get('[data-theme-toggle]').should('contain', 'Theme');
    cy.get('[data-theme-toggle]').click().should('contain', 'light');
    cy.get('[data-theme-toggle]').click().should('contain', 'phosphor');
    cy.get('[data-theme-toggle]').click().should('contain', 'auto');
  });
});

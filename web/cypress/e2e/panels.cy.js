/* Every panel on the playground: does it mount, does it draw, do its buttons do anything. */

const PANELS = [
  ['tapemap', 'Tape map'],
  ['strip', 'Tape strip'],
  ['detail', 'Page detail'],
  ['cpu', 'CPU'],
  ['fsm', 'Kernel FSM'],
  ['display', 'Display'],
  ['console', 'Console'],
  ['disk', 'Disk'],
  ['tapes', 'Tapes'],
  ['stats', 'Stats'],
  ['timeline', 'Timeline'],
  ['levers', 'Levers'],
  ['editor', 'Editor'],
];

describe('playground panels', () => {
  beforeEach(() => {
    cy.bootPlayground();
    // A few thousand instructions so every panel has something real to show.
    cy.get('[data-step="1000"]').click().click();
  });

  it('hides the mobile tab strip on a desktop viewport', () => {
    cy.get('[data-tabs]').should('not.be.visible');
    cy.get('.pg-grid').should('not.have.class', 'tabbed');
  });

  it('shows the tab strip on a narrow viewport and switches panels', () => {
    cy.viewport(600, 900);
    cy.get('[data-tabs]').should('be.visible');
    cy.get('.pg-tab').contains('Console').click();
    cy.get('#panel-console').should('have.class', 'active');
    cy.get('#panel-tapemap').should('not.have.class', 'active');
  });

  it('mounts all thirteen panels', () => {
    for (const [name, title] of PANELS) {
      cy.get(`#panel-${name}`).should('exist');
      cy.get(`#panel-${name} .slot-title`).should('have.text', title);
      cy.get(`#panel-${name} .slot-body`)
        .should('not.contain', 'failed to mount')
        .and('not.be.empty');
    }
  });

  it('tape map draws the tape', () => {
    cy.canvasPainted('#panel-tapemap canvas', 2);
  });

  it('tape strip draws cells around the head', () => {
    cy.canvasPainted('#panel-strip canvas', 2);
  });

  it('page detail shows hex and walks pages', () => {
    cy.get('#panel-detail .tos-detail-hex').invoke('text').should('match', /[0-9a-f]{2}/i);
    cy.get('#panel-detail').contains('page').invoke('text').then((before) => {
      cy.get('#panel-detail button').contains('▶').click();
      cy.get('#panel-detail').contains('page').invoke('text').should('not.equal', before);
      cy.get('#panel-detail button').contains('◀').click();
      cy.get('#panel-detail').contains('page').invoke('text').should('equal', before);
    });
  });

  it('cpu shows registers and the current instruction', () => {
    cy.get('#panel-cpu').should('contain', 'PC').and('contain', 'SP').and('contain', 'A');
    cy.get('#panel-cpu .tos-cpu-instr').invoke('text').should('match', /^[0-9a-f]{4}:/i);
  });

  it('kernel fsm shows states and counted transitions', () => {
    cy.get('#panel-fsm').should('contain', 'SHELL').and('contain', 'RUNNING');
    cy.get('#panel-fsm .tos-fsm-list li').should('have.length', 12);
    cy.get('#panel-fsm .tos-fsm-list .cnt')
      .invoke('text')
      .should((t) => expect(t.replace(/\D/g, '')).to.not.match(/^0*$/));
  });

  it('display draws the framebuffer', () => {
    cy.get('#panel-display canvas').first().should(($c) => {
      expect($c[0].width).to.be.greaterThan(0);
      expect($c[0].height).to.be.greaterThan(0);
    });
    cy.get('#panel-display button').contains('highlight display page').click();
  });

  it('console shows the prompt and accepts a command', () => {
    cy.get('#panel-console .tos-console-screen').should('contain', 'A>');
    cy.get('#panel-console input[type="text"]').type('dir');
    cy.get('#panel-console button').contains('send').click();
    cy.get('[data-act="to-halt"]').click();
    cy.get('#panel-console .tos-console-screen', { timeout: 30000 }).should('contain', '.C');
    cy.get('#panel-console button').contains('clear').click();
    cy.get('#panel-console .tos-console-screen').should('not.contain', '.C');
  });

  it('disk lists the demo image and shows a file', () => {
    cy.canvasPainted('#panel-disk canvas', 2);
    cy.get('#panel-disk li').should('have.length.greaterThan', 1);
    cy.get('#panel-disk li').first().click();
    cy.get('#panel-disk li.sel').should('exist');
  });

  it('tapes shows every tape and the bank window', () => {
    cy.get('#panel-tapes').should('contain', 'tape 0').and('contain', 'bank window');
    cy.canvasPainted('#panel-tapes canvas', 2);
  });

  it('stats counts steps and head travel', () => {
    cy.get('#panel-stats').should('contain', 'head travel');
    cy.get('#panel-stats .digits')
      .invoke('text')
      .should((t) => expect(parseInt(t.replace(/\D/g, ''), 10)).to.be.greaterThan(0));
  });

  it('timeline draws, and its buttons move the machine', () => {
    cy.canvasPainted('#panel-timeline canvas', 2);
    cy.get('[data-act="toggle"]').click();                    // pause so a seek stays put
    cy.get('[data-out="status"]').invoke('text').then((before) => {
      cy.get('#panel-timeline button').contains('|◀').click();
      cy.get('[data-out="status"]').should('contain', 'Seeked to step');
      cy.get('[data-out="status"]').should('not.have.text', before);
    });
    cy.get('#panel-timeline button').contains('live').click();
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
  });

  it('levers list the machine controls and reset works', () => {
    cy.get('#panel-levers select, #panel-levers input').should('have.length.greaterThan', 3);
    cy.get('#panel-levers').should('contain', 'resets').and('contain', 'view');
    cy.get('#panel-levers button').contains('reset machine').click();
    cy.get('#panel-levers').should('contain', 'machine reset');
    cy.get('#panel-levers button').contains('clear breakpoints').click();
    cy.get('#panel-levers').should('contain', 'breakpoints cleared');
  });

  it('editor compiles the sample program', () => {
    cy.get('#panel-editor button').contains('sample').click();
    cy.get('#panel-editor textarea').invoke('val').should('have.length.greaterThan', 20);
    cy.get('#panel-editor button').contains('compile').click();
    cy.get('#panel-editor .tos-editor-status')
      .should('not.have.class', 'tos-err')
      .invoke('text')
      .should('match', /byte/i);
  });
});

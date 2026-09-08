/* The panels are not just rendered: their controls change the machine and each other. */

describe('panel interactions', () => {
  beforeEach(() => cy.bootPlayground());

  it('the tapes lever rebuilds the machine with four tapes', () => {
    cy.get('#panel-tapes').should('contain', 'tape 0').and('not.contain', 'tape 3');
    cy.get('#panel-levers [data-lever="0"]').select('4');
    cy.get('#panel-tapes', { timeout: 30000 }).should('contain', 'tape 3');
    cy.get('#panel-tapes').should('contain', '4 tapes');
    cy.get('#panel-console .slot-body').should('contain', 'A>');
    cy.get('#panel-levers [data-lever="0"]').select('1');
    cy.get('#panel-tapes').should('not.contain', 'tape 1');
  });

  it('the tape length lever changes the bank window', () => {
    cy.get('#panel-tapes').invoke('text').then((before) => {
      cy.get('#panel-levers [data-lever="1"]').select('32768');
      cy.get('#panel-tapes', { timeout: 30000 }).should('not.have.text', before);
      cy.get('#panel-tapes').should('contain', '32,768');
    });
  });

  it('the seed lever resets the machine and keeps the shell alive', () => {
    cy.get('#panel-levers [data-lever="3"]').clear().type('7').blur();
    cy.get('#panel-console .slot-body', { timeout: 30000 }).should('contain', 'A>');
    cy.get('[data-out="status"]').should('not.contain', 'HALT');
  });

  it('clicking the tape map moves the page detail panel', () => {
    cy.get('#panel-detail').contains('page').invoke('text').then((before) => {
      cy.get('#panel-tapemap canvas').first().click(200, 120, { force: true });
      cy.get('#panel-detail').contains('page').invoke('text').should('not.equal', before);
    });
  });

  it('the page detail panel disassembles what it shows', () => {
    cy.get('#panel-detail .tos-detail-dis').invoke('text').should('have.length.greaterThan', 10);
  });

  it('the disk panel adds a file and the shell sees it', () => {
    cy.get('#panel-disk input[type="file"]').first().selectFile(
      { contents: Cypress.Buffer.from('int main() { puts("hi from cypress\\n"); return 0; }\n'), fileName: 'CYP.C' },
      { force: true },
    );
    cy.get('#panel-disk li', { timeout: 30000 }).should('contain', 'CYP.C');
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('#panel-console .tos-console-screen', { timeout: 30000 }).should('contain', 'CYP.C');
  });

  it('the editor saves to the disk and the disk panel shows it', () => {
    cy.get('#panel-editor button').contains('sample').click();
    cy.get('#panel-editor input[type="text"]').clear().type('EDSAVE');
    cy.get('#panel-editor button').contains('save to disk').click();
    cy.get('#panel-editor .tos-editor-status').should('contain', 'saved EDSAVE.C');
    cy.get('#panel-disk li', { timeout: 30000 }).should('contain', 'EDSAVE.C');
    cy.get('#panel-editor button').contains('load from disk').click();
    cy.get('#panel-editor textarea').invoke('val').should('have.length.greaterThan', 10);
  });

  it('the console echo toggle and the clear button work', () => {
    cy.get('#panel-console input[type="checkbox"]').uncheck();
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('#panel-console .tos-console-screen', { timeout: 30000 }).should('contain', 'A>');
    cy.get('#panel-console button').contains('clear').click();
    cy.get('#panel-console .tos-console-screen').should('not.contain', 'A>');
  });

  it('the display panel highlights the framebuffer page on the tape map', () => {
    cy.get('#panel-display button').contains('highlight display page').click().should('have.class', 'on');
    cy.get('#panel-display button').contains('highlight display page').click().should('not.have.class', 'on');
  });

  it('the timeline scrubs to an earlier step and the CPU panel follows', () => {
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.wait(500);
    cy.get('[data-act="toggle"]').click(); // pause, so the seek result stays on screen
    cy.get('#panel-cpu .tos-cpu-instr').invoke('text').then((before) => {
      cy.get('#panel-timeline button').contains('|◀').click();
      cy.get('[data-out="status"]').should('contain', 'Seeked to step');
      cy.get('#panel-cpu .tos-cpu-instr').should('not.have.text', before);
    });
    // 'live ▶|' hands the machine back to the run loop
    cy.get('#panel-timeline button').contains('live').click();
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
    cy.get('[data-out="status"]').should('contain', 'Running');
  });

  it('the trace lever turns the trace off and the URL records it', () => {
    cy.get('#panel-stats').should('contain', 'syscalls');
    cy.get('#panel-levers [data-lever="6"]').uncheck();
    cy.hash().should('contain', 'trace=0');
    cy.get('#panel-levers [data-lever="6"]').check();
    cy.hash().should('not.contain', 'trace=0');
  });

  it('the levers panel clear-breakpoints button empties the toolbar list', () => {
    cy.get('[data-ctl="bp-lo"]').clear().type('0100');
    cy.get('[data-form="bp"] button[type="submit"]').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 1);
    cy.get('#panel-levers button').contains('clear breakpoints').click();
    cy.get('[data-out="bp-list"] li').should('have.length', 0);
  });

  it('the levers panel reset button reboots to a live shell', () => {
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.wait(300);
    cy.get('#panel-levers button').contains('reset machine').click();
    cy.get('#panel-console .slot-body', { timeout: 30000 }).should('contain', 'A>');
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('#panel-console .tos-console-screen', { timeout: 30000 }).should('contain', '.C');
  });

  it('the kernel FSM lights the state the machine is in', () => {
    cy.get('#panel-fsm .tos-fsm-list li').should('have.length', 12);
    cy.get('#panel-fsm').should('contain', 'IDLE');
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    cy.get('#panel-fsm .tos-fsm-list .cnt').first().invoke('text').should('not.equal', '0');
  });
});

describe('levers keep what the user made', () => {
  it('a machine lever keeps the files on the disk', () => {
    cy.bootPlayground();
    cy.get('#panel-editor button').contains('sample').click();
    cy.get('#panel-editor input[type="text"]').clear().type('KEEPME');
    cy.get('#panel-editor button').contains('save to disk').click();
    cy.get('#panel-disk li', { timeout: 30000 }).should('contain', 'KEEPME.C');

    cy.get('#panel-levers [data-lever="0"]').select('2');     // tapes: a machine lever
    cy.get('#panel-tapes', { timeout: 30000 }).should('contain', 'tape 1');
    cy.get('#panel-disk li').should('contain', 'KEEPME.C');   // the disk survived the reset
    cy.get('#panel-console .slot-body').should('contain', 'A>');
  });

  it('the clock lever does not throttle the browser (the speed control does)', () => {
    cy.visit('#/playground?hz=1000');
    cy.get('[data-out="status"]', { timeout: 60000 }).should('contain', 'step');
    cy.get('#panel-console input[type="text"]').type('dir{enter}');
    // At 1000 Hz a throttled browser would need minutes to list the disk.
    cy.get('#panel-console .tos-console-screen', { timeout: 20000 }).should('contain', '.C');
  });

  it('compile & run in the editor actually runs the program', () => {
    cy.bootPlayground();
    cy.get('[data-act="toggle"]').click();                    // paused: only a real run un-pauses
    cy.get('#panel-editor button').contains('sample').click();
    cy.get('#panel-editor button').contains('compile & run').click();
    cy.get('[data-act="toggle"]').should('have.text', 'Pause');
    cy.get('[data-out="status"]').should('contain', 'Running');
  });
});

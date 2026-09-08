/* The demos: each one loads, runs, and shows what its tour says it shows. */

describe('demos', () => {
  it('pong draws on the display and its tour steps', () => {
    // A demo in the URL loads and runs on its own: nothing to click.
    cy.visit('#/playground?demo=pong');
    cy.get('[data-out="status"]', { timeout: 60000 }).should('contain', 'step');
    cy.canvasPainted('#panel-display canvas', 2);
    cy.get('[data-out="status"]').should('contain', 'RUNNING').and('not.contain', 'frame 0 ');

    // the tour bar: next, previous, and the × that closes it
    cy.get('[data-tour]').should('be.visible');
    cy.get('[data-out="tour-pos"]').should('contain', '1 /');
    cy.get('[data-act="tour-next"]').click();
    cy.get('[data-out="tour-pos"]').should('contain', '2 /');
    cy.get('[data-act="tour-prev"]').click();
    cy.get('[data-out="tour-pos"]').should('contain', '1 /');
    cy.get('[data-act="tour-close"]').should('have.text', '×').click();
    cy.get('[data-tour]').should('not.be.visible');
  });

  it('the keyboard drives pong through the key port', () => {
    cy.visit('#/playground?demo=pong');
    cy.canvasPainted('#panel-display canvas', 2);
    cy.get('#panel-display .slot-body').first().click();
    cy.get('#panel-display .slot-body').first().trigger('keydown', { key: 'ArrowUp', code: 'ArrowUp' });
    cy.wait(500);
    cy.get('#panel-display .slot-body').first().trigger('keyup', { key: 'ArrowUp', code: 'ArrowUp' });
    cy.canvasPainted('#panel-display canvas', 2);
    cy.get('[data-out="status"]').should('contain', 'RUNNING');
  });

  it('life runs generations', () => {
    cy.visit('#/playground?demo=life');
    cy.canvasPainted('#panel-display canvas', 2);
    cy.get('[data-out="status"]', { timeout: 60000 }).should('contain', 'RUNNING');
  });

  it('every demo in the picker loads and starts', () => {
    cy.bootPlayground();
    cy.get('[data-ctl="demo"] option').then(($opts) => {
      const names = [...$opts].map((o) => o.value).filter(Boolean);
      expect(names.length).to.be.greaterThan(3);
      for (const name of names) {
        cy.get('[data-ctl="demo"]').select(name);
        cy.get('[data-act="load"]').click();
        cy.get('[data-out="status"]', { timeout: 60000 }).should('not.contain', 'Load failed');
        cy.get('[data-step="1000"]').click();
      }
    });
  });

  it('the editor compiles and runs a program on the machine', () => {
    cy.bootPlayground();
    cy.get('#panel-editor button').contains('sample').click();
    cy.get('#panel-editor button').contains('compile & run').click();
    cy.get('#panel-editor .tos-editor-status').should('not.have.class', 'tos-err');
    cy.get('[data-act="to-halt"]').click();
    cy.get('#panel-console .tos-console-screen', { timeout: 30000 }).should('not.be.empty');
  });

  it('the editor reports a compile error on the right line', () => {
    cy.bootPlayground();
    cy.get('#panel-editor textarea').clear().type('int main() {{}\n  int x = ;\n  return 0;\n}');
    cy.get('#panel-editor button').contains('compile').click();
    cy.get('#panel-editor .tos-editor-status').should('have.class', 'tos-err');
    cy.get('#panel-editor .tos-editor-gutter .err').should('have.text', '2');
  });
});

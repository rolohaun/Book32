const modeButtons = document.querySelectorAll('.mode-button');
const modePanels = document.querySelectorAll('.mode-panel');

modeButtons.forEach((button) => {
  button.addEventListener('click', () => {
    const targetId = button.dataset.panel;

    modeButtons.forEach((item) => {
      const selected = item === button;
      item.classList.toggle('active', selected);
      item.setAttribute('aria-selected', String(selected));
    });

    modePanels.forEach((panel) => {
      const selected = panel.id === targetId;
      panel.classList.toggle('active', selected);
      panel.hidden = !selected;
    });
  });
});

// Standalone ZX Spectrum menu and submenu logic
(function () {
  'use strict';
  // Menu elements
  const logo = document.getElementById('zx-logo');
  const menu = document.getElementById('zx-menu');
  const submenu = document.getElementById('zx-submenu');
  const sublist = document.getElementById('zx-submenu-list');
  const subclose = document.getElementById('zx-submenu-close');
  const subtitle = document.getElementById('zx-submenu-title');
  const fileFilter = document.getElementById('zx-file-filter');

  let allFiles = [];

  // Menu open/close
  logo && logo.addEventListener('click', (e) => {
    e.stopPropagation();
    menu.classList.toggle('open');
  });
  document.addEventListener('click', () => menu.classList.remove('open'));

  menu && menu.addEventListener('click', (e) => {
    const action = e.target.dataset.action;
    if (!action) return;
    if (action === 'snapshot') {
      openSubmenu('SNAPSHOTS', '/api/snapshots');
    } else if (action === 'tape_player') {
      document.getElementById('zx-player').classList.add('open');
    } else if (action === 'reset') {
      fetch('/api/reset').then(r => console.log('Reset:', r.status));
    } else {
      console.log('Menu action:', action);
    }
    menu.classList.remove('open');
  });

  subclose && subclose.addEventListener('click', () => submenu.classList.remove('open'));

  async function openSubmenu(title, api) {
    subtitle.textContent = title;
    sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">Loading...</div>';
    submenu.classList.add('open');
    fileFilter.value = '';
    try {
      const res = await fetch(api);
      allFiles = await res.json();
      renderFileList(allFiles);
    } catch (e) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:var(--red)">Failed to load list</div>';
    }
  }

  function renderFileList(files) {
    if (files.length === 0) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">No matching files found</div>';
    } else {
      sublist.innerHTML = files.map(f => `<div class="zx-file-item" data-file="${f}">${f}</div>`).join('');
    }
  }

  fileFilter && fileFilter.addEventListener('input', (e) => {
    const term = e.target.value.toLowerCase();
    const filtered = allFiles.filter(f => f.toLowerCase().includes(term));
    renderFileList(filtered);
  });

  sublist && sublist.addEventListener('click', async (e) => {
    const file = e.target.closest('.zx-file-item')?.dataset.file;
    if (!file) return;
    e.target.style.opacity = '0.5';
    try {
      const res = await fetch(`/api/load?file=${encodeURIComponent(file)}`);
      if (res.ok) {
        submenu.classList.remove('open');
        // If it's a tape, update player label
        const ext = file.split('.').pop().toLowerCase();
        if (["tap","tzx","tsx"].includes(ext)) {
          const lbl = document.getElementById('zx-player-label');
          if (lbl) lbl.textContent = file;
        }
      } else {
        alert('Failed to load file');
      }
    } catch (e) {
      alert('Error connecting to server');
    }
    e.target.style.opacity = '1';
  });
})();

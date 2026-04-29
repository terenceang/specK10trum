(function(window) {
  'use strict';

  const esc = window.ZX_UTILS.esc;

  let allFiles = [];
  let currentModel = "48k";

  async function syncCurrentModel() {
    try {
      const res = await fetch(window.ZX_UTILS.API.MODEL);
      const data = await res.json();
      if (data.model) {
        currentModel = data.model;
        const radioBtn = document.getElementById(`model-${data.model}`);
        if (radioBtn) {
          radioBtn.checked = true;
        }
      }
    } catch (e) {
      console.error('Failed to sync model:', e);
    }
  }

  function init() {
    syncCurrentModel();

    const logo = document.getElementById('zx-logo');
    const menu = document.getElementById('zx-menu');
    const submenu = document.getElementById('zx-submenu');
    const sublist = document.getElementById('zx-submenu-list');
    const subclose = document.getElementById('zx-submenu-close');
    const fileFilter = document.getElementById('zx-file-filter');
    const modelModal = document.getElementById('zx-model-modal');
    const modelClose = document.getElementById('zx-model-close');
    const modelConfirm = document.getElementById('zx-model-confirm');
    
    const layoutModal = document.getElementById('zx-layout-modal');
    const layoutClose = document.getElementById('zx-layout-close');
    const layoutConfirm = document.getElementById('zx-layout-confirm');

    if (logo && menu) {
      logo.addEventListener('click', (e) => {
        e.stopPropagation();
        menu.classList.toggle('open');
      });

      document.addEventListener('click', () => {
        menu.classList.remove('open');
      });

      menu.addEventListener('click', async (e) => {
        const action = e.target.dataset.action;
        if (!action) return;
        if (action === 'snapshot') {
          openSubmenu('SNAPSHOTS', window.ZX_UTILS.API.SNAPSHOTS);
        } else if (action === 'tape_player') {
          document.getElementById('zx-player').classList.add('open');
          if (window.ZX_TAPE) window.ZX_TAPE.updateUI();
        } else if (action === 'joystick') {
          if (window.ZX_JOY) window.ZX_JOY.showGamepad();
        } else if (action === 'model') {
          try {
            const res = await fetch(window.ZX_UTILS.API.MODEL);
            const data = await res.json();
            if (data.model) {
              document.getElementById(`model-${data.model}`).checked = true;
            }
          } catch (e) {
            console.error('Failed to fetch current model:', e);
          }
          modelModal.style.display = 'flex';
        } else if (action === 'layout') {
          layoutModal.style.display = 'flex';
        } else if (action === 'reset') {
          fetch(window.ZX_UTILS.API.RESET).then(r => console.log('Reset:', r.status));
        }
        menu.classList.remove('open');
      });
    }

    if (modelClose) {
      modelClose.addEventListener('click', () => {
        modelModal.style.display = 'none';
      });
    }

    if (modelModal) {
      modelModal.addEventListener('click', (e) => {
        if (e.target === modelModal) {
          modelModal.style.display = 'none';
        }
      });
    }

    if (modelConfirm) {
      modelConfirm.addEventListener('click', async () => {
        const selected = document.querySelector('input[name="model"]:checked');
        if (!selected) {
          alert('Please select a model');
          return;
        }
        const model = selected.value;
        try {
          const res = await fetch(`${window.ZX_UTILS.API.MODEL}?model=${model}`);
          const data = await res.json();
          if (res.ok) {
            modelModal.style.display = 'none';
            alert(`Model changed to ${model.toUpperCase()} and emulator reset!`);
            console.log('Model changed to:', model);
          } else {
            alert('Failed to change model: ' + (data.message || 'Unknown error'));
          }
        } catch (e) {
          alert('Error connecting to server: ' + e.message);
        }
      });
    }

    if (layoutClose) {
      layoutClose.addEventListener('click', () => {
        layoutModal.style.display = 'none';
      });
    }

    if (layoutConfirm) {
      layoutConfirm.addEventListener('click', () => {
        const selected = document.querySelector('input[name="layout"]:checked');
        if (!selected) return;
        const layoutName = selected.value;
        
        if (window.ZX_LAYOUT_VARIANTS && window.ZX_LAYOUT_VARIANTS[layoutName]) {
          window.ZX_LAYOUT = window.ZX_LAYOUT_VARIANTS[layoutName];
          const kbContainer = document.getElementById('zx-kb');
          if (kbContainer && window.ZX_KB) {
            window.ZX_KB.render(kbContainer);
          }
          layoutModal.style.display = 'none';
          localStorage.setItem('zx_kbd_layout', layoutName);
        }
      });
    }

    // Load saved layout
    const savedLayout = localStorage.getItem('zx_kbd_layout');
    if (savedLayout && document.getElementById(`layout-${savedLayout}`)) {
      document.getElementById(`layout-${savedLayout}`).checked = true;
      // We need to wait for zx-layout.js to be fully loaded and zx-layout-variants to be populated.
      // But since zx-layout.js is loaded before menu.js, it should be fine.
      setTimeout(() => {
        if (window.ZX_LAYOUT_VARIANTS && window.ZX_LAYOUT_VARIANTS[savedLayout]) {
          window.ZX_LAYOUT = window.ZX_LAYOUT_VARIANTS[savedLayout];
          const kbContainer = document.getElementById('zx-kb');
          if (kbContainer && window.ZX_KB) {
            window.ZX_KB.render(kbContainer);
          }
        }
      }, 100);
    }

    if (subclose) subclose.addEventListener('click', () => submenu.classList.remove('open'));

    if (fileFilter) {
      fileFilter.addEventListener('input', (e) => {
        const term = e.target.value.toLowerCase();
        const filtered = allFiles.filter(f => f.toLowerCase().includes(term));
        renderFileList(filtered);
      });
    }

    if (sublist) {
      sublist.addEventListener('click', async (e) => {
        const file = e.target.closest('.zx-file-item')?.dataset.file;
        if (!file) return;
        
        e.target.style.opacity = '0.5';
        try {
          const res = await fetch(`${window.ZX_UTILS.API.LOAD}?file=${encodeURIComponent(file)}`);
          if (res.ok) {
            submenu.classList.remove('open');
            // If it's a tape, notify the tape player
            const ext = file.split('.').pop().toLowerCase();
            if (['tap','tzx','tsx'].includes(ext)) {
                if (window.ZX_TAPE) window.ZX_TAPE.onTapeLoaded(file);
            }
          } else {
            alert('Failed to load file');
          }
        } catch (e) {
          alert('Error connecting to server');
        }
        e.target.style.opacity = '1';
      });
    }
  }

  async function openSubmenu(title, api) {
    const submenu = document.getElementById('zx-submenu');
    const subtitle = document.getElementById('zx-submenu-title');
    const sublist = document.getElementById('zx-submenu-list');
    const fileFilter = document.getElementById('zx-file-filter');

    subtitle.textContent = title;
    sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">Loading...</div>';
    submenu.classList.add('open');
    if (fileFilter) fileFilter.value = '';
    
    try {
      const res = await fetch(api);
      allFiles = await res.json();
      renderFileList(allFiles);
    } catch (e) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:var(--red)">Failed to load list</div>';
    }
  }

  function renderFileList(files) {
    const sublist = document.getElementById('zx-submenu-list');
    if (files.length === 0) {
      sublist.innerHTML = '<div style="padding:16px;font-size:10px;color:#666">No matching files found</div>';
    } else {
      sublist.innerHTML = files.map(f => `<div class="zx-file-item" data-file="${esc(f)}">${esc(f)}</div>`).join('');
    }
  }

  window.ZX_MENU = { init, openSubmenu };
})(window);

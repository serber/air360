/* Air360 firmware · ui */
(function () {
  // Theme restoration — runs before first paint to avoid flash
  const storedTheme = localStorage.getItem('air360-theme');
  if (storedTheme === 'dark') document.documentElement.setAttribute('data-theme', 'dark');

  function closeAllWifiMenus() {
    document.querySelectorAll('.wifi-menu').forEach(m => { m.style.display = 'none'; });
  }

  function toggleSwitch(sw) {
    sw.classList.toggle('on');
    const isOn = sw.classList.contains('on');
    const revealsId = sw.getAttribute('data-reveals');
    if (revealsId) {
      const target = document.getElementById(revealsId);
      if (target instanceof HTMLElement) target.hidden = !isOn;
    }
    const cbName = sw.getAttribute('data-drives-checkbox');
    if (cbName) {
      const cb = document.querySelector(`input[type="checkbox"][name="${cbName}"]`);
      if (cb instanceof HTMLInputElement) {
        cb.checked = isOn;
        cb.dispatchEvent(new Event('change', { bubbles: true }));
      }
    }
  }

  // Global click delegation — attached immediately so it works even if scripts load late
  document.addEventListener('click', e => {
    // Theme toggle
    if (e.target.closest('#theme-toggle')) {
      const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
      if (isDark) document.documentElement.removeAttribute('data-theme');
      else document.documentElement.setAttribute('data-theme', 'dark');
      localStorage.setItem('air360-theme', isDark ? 'light' : 'dark');
      return;
    }

    // Switch buttons
    const sw = e.target.closest('.switch');
    if (sw) { toggleSwitch(sw); return; }

    // Password visibility — new pattern (data-toggle-pw)
    const pwBtn = e.target.closest('[data-toggle-pw]');
    if (pwBtn) {
      const inp = document.getElementById(pwBtn.getAttribute('data-toggle-pw'));
      if (inp instanceof HTMLInputElement) {
        inp.type = inp.type === 'password' ? 'text' : 'password';
        const lbl = pwBtn.querySelector('.pw-label');
        if (lbl) lbl.textContent = inp.type === 'password' ? 'Show' : 'Hide';
      }
      return;
    }

    // Password visibility — old pattern (data-secret-toggle)
    const oldPwBtn = e.target.closest('[data-secret-toggle]');
    if (oldPwBtn) {
      const oldInp = document.getElementById(oldPwBtn.getAttribute('data-secret-toggle'));
      if (oldInp instanceof HTMLInputElement) {
        const reveal = oldInp.type === 'password';
        oldInp.type = reveal ? 'text' : 'password';
        oldPwBtn.textContent = reveal ? 'Hide' : 'Show';
      }
      return;
    }

    const air360SecretBtn = e.target.closest('[data-generate-air360-secret]');
    if (air360SecretBtn) {
      const target = document.getElementById(air360SecretBtn.getAttribute('data-generate-air360-secret'));
      if (target instanceof HTMLTextAreaElement) {
        target.disabled = false;
        air360SecretBtn.disabled = true;
        fetch('/backends/air360-upload-secret', { cache: 'no-store' })
          .then(r => {
            if (!r.ok) throw new Error('secret generation failed');
            return r.json();
          })
          .then(data => {
            if (typeof data.upload_secret !== 'string') throw new Error('invalid response');
            target.value = data.upload_secret;
            target.dispatchEvent(new Event('input', { bubbles: true }));
            target.focus();
            target.select();
          })
          .catch(() => {
            target.value = '';
            target.placeholder = 'Could not generate secret. Reload the page and try again.';
          })
          .finally(() => {
            air360SecretBtn.disabled = false;
          });
      }
      return;
    }

    const air360SecretChangeBtn = e.target.closest('[data-change-air360-secret]');
    if (air360SecretChangeBtn) {
      const panel = document.getElementById(air360SecretChangeBtn.getAttribute('data-change-air360-secret'));
      const target = document.getElementById(air360SecretChangeBtn.getAttribute('data-secret-input'));
      if (panel instanceof HTMLElement && target instanceof HTMLTextAreaElement) {
        panel.hidden = false;
        target.disabled = false;
        target.focus();
      }
      return;
    }

    // Collapsible card headers
    const cardHead = e.target.closest('.card.collapsible > .card-header');
    if (cardHead) {
      const card = cardHead.closest('.card.collapsible');
      if (card) card.classList.toggle('open');
      return;
    }

    // Wi-Fi picker — open/close menu
    const wifiTrig = e.target.closest('.wifi-trigger');
    if (wifiTrig) {
      const wifiMenu = wifiTrig.nextElementSibling;
      if (wifiMenu && wifiMenu.classList.contains('wifi-menu')) {
        const isOpen = wifiMenu.style.display === 'block';
        closeAllWifiMenus();
        if (!isOpen) wifiMenu.style.display = 'block';
      }
      return;
    }

    // Wi-Fi picker — select option
    const wifiOpt = e.target.closest('.wifi-option');
    if (wifiOpt) {
      const menu = wifiOpt.closest('.wifi-menu');
      const ssid = wifiOpt.getAttribute('data-ssid') || '';
      const prevTrig = menu && menu.previousElementSibling;
      if (prevTrig) {
        const trigLbl = prevTrig.querySelector('.wifi-trigger-label');
        if (trigLbl) trigLbl.textContent = ssid || '(none)';
      }
      menu.querySelectorAll('.wifi-option').forEach(o => o.classList.remove('selected'));
      wifiOpt.classList.add('selected');
      menu.style.display = 'none';
      // Sync to hidden select for form submission
      const ssidSel = document.querySelector('[data-wifi-ssid-select]');
      if (ssidSel instanceof HTMLSelectElement) {
        for (let i = 0; i < ssidSel.options.length; i++) {
          if (ssidSel.options[i].value === ssid) {
            ssidSel.selectedIndex = i;
            ssidSel.dispatchEvent(new Event('change', { bubbles: true }));
            break;
          }
        }
      }
      return;
    }

    // Click outside any wifi menu closes it
    if (!e.target.closest('.wifi-menu') && !e.target.closest('.wifi-trigger')) {
      closeAllWifiMenus();
    }
  });

  document.addEventListener('DOMContentLoaded', () => {
    // Mark active nav item and apply device-only filtering
    const activePage = document.body.dataset.page;
    document.querySelectorAll('.nav-item[data-page]').forEach(a => {
      if (a.dataset.page === activePage) a.classList.add('active');
      if (document.body.dataset.nav === 'device-only' && a.dataset.page !== 'device') a.hidden = true;
    });

    // Initialize switches that drive a hidden checkbox — read server-rendered state
    document.querySelectorAll('.switch[data-drives-checkbox]').forEach(sw => {
      const cb = document.querySelector(`input[type="checkbox"][name="${sw.getAttribute('data-drives-checkbox')}"]`);
      const isOn = cb ? cb.checked : sw.hasAttribute('data-default-on');
      sw.classList.toggle('on', isOn);
      const revealsId = sw.getAttribute('data-reveals');
      if (revealsId) {
        const target = document.getElementById(revealsId);
        if (target instanceof HTMLElement) target.hidden = !isOn;
      }
    });

    // Initialize standalone switches (data-default-on, no checkbox)
    document.querySelectorAll('.switch[data-default-on]:not([data-drives-checkbox])').forEach(sw => {
      sw.classList.add('on');
      const revealsId = sw.getAttribute('data-reveals');
      if (revealsId) {
        const target = document.getElementById(revealsId);
        if (target instanceof HTMLElement) target.hidden = false;
      }
    });

    // ── Form dirty tracking ──────────────────────────────────────────────────
    const dirtyForms = new Set();

    function setFormDirty(form, dirty) {
      const panel = form.closest('.panel');
      form.dataset.dirty = dirty ? 'true' : 'false';
      if (dirty) dirtyForms.add(form); else dirtyForms.delete(form);
      if (panel instanceof HTMLElement) panel.classList.toggle('panel--dirty', dirty);
    }

    for (const form of document.querySelectorAll('form[data-dirty-track]')) {
      if (!(form instanceof HTMLFormElement)) continue;
      const markDirty = () => setFormDirty(form, true);
      for (const field of form.querySelectorAll('input, select, textarea')) {
        if (
          field instanceof HTMLInputElement ||
          field instanceof HTMLSelectElement ||
          field instanceof HTMLTextAreaElement
        ) {
          if (field.type === 'hidden') continue;
          field.addEventListener('input', markDirty);
          field.addEventListener('change', markDirty);
        }
      }
      form.addEventListener('submit', () => setFormDirty(form, false));
    }

    // ── Sensor form sync ─────────────────────────────────────────────────────
    function syncSensorForm(form) {
      const sensorTypeSelect = form.querySelector('[data-sensor-type-select]');
      if (!(sensorTypeSelect instanceof HTMLSelectElement)) return;

      const sel = sensorTypeSelect.selectedOptions[0] ?? null;
      const requiresPin  = sel?.dataset.requiresPin  === 'true';
      const requiresI2c  = sel?.dataset.requiresI2c  === 'true';
      const requiresUart = sel?.dataset.requiresUart === 'true';
      const defaultsHint      = sel?.dataset.defaultsHint      ?? '';
      const defaultI2cAddress = sel?.dataset.defaultI2cAddress ?? '';
      const defaultUartPort   = sel?.dataset.defaultUartPort   ?? '';
      const allowedI2cAddresses = (sel?.dataset.allowedI2cAddresses ?? '')
        .split(',').map(s => s.trim()).filter(Boolean);
      const allowedUartBindings = (sel?.dataset.allowedUartBindings ?? '')
        .split(',').map(b => {
          const p = b.split(':');
          return { port: p[0]?.trim() ?? '', rx: p[1]?.trim() ?? '', tx: p[2]?.trim() ?? '' };
        }).filter(b => b.port.length > 0);
      const allowedGpioPins = (sel?.dataset.allowedGpioPins ?? '')
        .split(',').map(s => s.trim()).filter(Boolean);

      const i2cField = form.querySelector('[data-sensor-i2c-field]');
      if (i2cField instanceof HTMLElement) {
        i2cField.hidden = !requiresI2c;
        for (const ctrl of i2cField.querySelectorAll('input, select, textarea')) {
          if (ctrl instanceof HTMLInputElement || ctrl instanceof HTMLSelectElement || ctrl instanceof HTMLTextAreaElement) {
            ctrl.disabled = !requiresI2c;
            if (requiresI2c && ctrl instanceof HTMLSelectElement && ctrl.name === 'i2c_address' && allowedI2cAddresses.length > 0) {
              const cur = ctrl.value;
              const chosen = allowedI2cAddresses.includes(cur) ? cur : defaultI2cAddress;
              ctrl.innerHTML = '';
              for (const addr of allowedI2cAddresses) {
                const opt = document.createElement('option');
                opt.value = addr; opt.textContent = addr; opt.selected = addr === chosen;
                ctrl.appendChild(opt);
              }
            }
          }
        }
      }

      const uartField = form.querySelector('[data-sensor-uart-field]');
      if (uartField instanceof HTMLElement) {
        uartField.hidden = !requiresUart;
        for (const ctrl of uartField.querySelectorAll('input, select, textarea')) {
          if (ctrl instanceof HTMLInputElement || ctrl instanceof HTMLSelectElement || ctrl instanceof HTMLTextAreaElement) {
            ctrl.disabled = !requiresUart;
            if (requiresUart && ctrl instanceof HTMLSelectElement && ctrl.name === 'uart_port_id' && allowedUartBindings.length > 0) {
              const cur = ctrl.value;
              const allowedPorts = allowedUartBindings.map(b => b.port);
              const chosen = allowedPorts.includes(cur) ? cur : defaultUartPort;
              ctrl.innerHTML = '';
              for (const b of allowedUartBindings) {
                const opt = document.createElement('option');
                opt.value = b.port;
                opt.textContent = `UART${b.port} - RX GPIO${b.rx}, TX GPIO${b.tx}`;
                opt.dataset.rxGpio = b.rx; opt.dataset.txGpio = b.tx;
                opt.selected = b.port === chosen;
                ctrl.appendChild(opt);
              }
            }
          }
        }
        syncUartPinHint(form);
      }

      const pinField = form.querySelector('[data-sensor-pin-field]');
      if (pinField instanceof HTMLElement) {
        pinField.hidden = !requiresPin;
        for (const ctrl of pinField.querySelectorAll('input, select, textarea')) {
          if (ctrl instanceof HTMLInputElement || ctrl instanceof HTMLSelectElement || ctrl instanceof HTMLTextAreaElement) {
            ctrl.disabled = !requiresPin;
            if (requiresPin && ctrl instanceof HTMLSelectElement && ctrl.name === 'analog_gpio_pin' && allowedGpioPins.length > 0) {
              const cur = ctrl.value;
              const chosen = allowedGpioPins.includes(cur) ? cur : allowedGpioPins[0];
              ctrl.innerHTML = '';
              for (const pin of allowedGpioPins) {
                const opt = document.createElement('option');
                opt.value = pin; opt.textContent = `GPIO ${pin}`; opt.selected = pin === chosen;
                ctrl.appendChild(opt);
              }
            }
          }
        }
      }

      const defaultsNode = form.querySelector('[data-sensor-defaults]');
      if (defaultsNode instanceof HTMLElement) {
        defaultsNode.textContent = defaultsHint;
        defaultsNode.hidden = defaultsHint.length === 0;
      }
    }

    function syncUartPinHint(form) {
      const select   = form.querySelector('[data-sensor-uart-port-select]');
      const hintNode = form.querySelector('[data-sensor-uart-pins]');
      if (!(select instanceof HTMLSelectElement) || !(hintNode instanceof HTMLElement)) return;
      const opt = select.selectedOptions[0] ?? null;
      const rx = opt?.dataset.rxGpio ?? '';
      const tx = opt?.dataset.txGpio ?? '';
      hintNode.textContent = rx && tx ? `Pins: RX GPIO${rx}, TX GPIO${tx}` : '';
      hintNode.hidden = !hintNode.textContent;
    }

    for (const form of document.querySelectorAll('form[data-sensor-form]')) {
      if (!(form instanceof HTMLFormElement)) continue;
      syncSensorForm(form);
      const typeSelect = form.querySelector('[data-sensor-type-select]');
      if (typeSelect instanceof HTMLSelectElement) {
        typeSelect.addEventListener('change', () => syncSensorForm(form));
      }
      const uartSel = form.querySelector('[data-sensor-uart-port-select]');
      if (uartSel instanceof HTMLSelectElement) {
        uartSel.addEventListener('change', () => syncUartPinHint(form));
      }
    }

    // ── Config form sync ─────────────────────────────────────────────────────
    function syncConfigForm(form) {
      const ssidSelect    = form.querySelector('[data-wifi-ssid-select]');
      const passwordField = form.querySelector('[data-wifi-password-field]');
      const hintNode      = form.querySelector('[data-wifi-password-hint]');
      const passwordInput = form.querySelector('#wifi_password');
      const toggleButton  = form.querySelector("[data-secret-toggle='wifi_password']");
      if (!(ssidSelect instanceof HTMLSelectElement)) return;

      const hasStation = ssidSelect.value.trim().length > 0;
      if (passwordField instanceof HTMLElement) passwordField.classList.toggle('field--disabled', !hasStation);
      if (passwordInput instanceof HTMLInputElement) passwordInput.disabled = !hasStation;
      if (toggleButton instanceof HTMLButtonElement) toggleButton.disabled = !hasStation;
      if (hintNode instanceof HTMLElement) {
        hintNode.textContent = hasStation
          ? 'Leave Wi-Fi SSID empty to reboot into setup AP mode.'
          : 'Station mode is disabled while Wi-Fi SSID is empty.';
      }
    }

    function setGroupEnabled(group, enabled) {
      group.classList.toggle('field--disabled', !enabled);
      group.setAttribute('aria-disabled', enabled ? 'false' : 'true');
      for (const ctrl of group.querySelectorAll('input, select, textarea, button')) {
        if (
          ctrl instanceof HTMLInputElement || ctrl instanceof HTMLSelectElement ||
          ctrl instanceof HTMLTextAreaElement || ctrl instanceof HTMLButtonElement
        ) {
          ctrl.disabled = !enabled && ctrl.dataset.allowWhenDisabled !== 'true';
        }
      }
    }

    function syncStaticIpFields(form) {
      const toggle = form.querySelector('[data-static-ip-toggle]');
      const group  = form.querySelector('[data-static-ip-fields]');
      if (toggle instanceof HTMLInputElement && group instanceof HTMLElement) setGroupEnabled(group, toggle.checked);
    }

    function syncCellularFields(form) {
      const toggle = form.querySelector('[data-cellular-toggle]');
      const group  = form.querySelector('[data-cellular-fields]');
      if (toggle instanceof HTMLInputElement && group instanceof HTMLElement) setGroupEnabled(group, toggle.checked);
    }

    function syncBleFields(form) {
      const toggle = form.querySelector('[data-ble-toggle]');
      const group  = form.querySelector('[data-ble-fields]');
      if (toggle instanceof HTMLInputElement && group instanceof HTMLElement) setGroupEnabled(group, toggle.checked);
    }

    for (const form of document.querySelectorAll("form[data-dirty-track='config']")) {
      if (!(form instanceof HTMLFormElement)) continue;

      syncConfigForm(form);
      const ssidSel = form.querySelector('[data-wifi-ssid-select]');
      if (ssidSel instanceof HTMLSelectElement) ssidSel.addEventListener('change', () => syncConfigForm(form));

      syncStaticIpFields(form);
      const staticTog = form.querySelector('[data-static-ip-toggle]');
      if (staticTog instanceof HTMLInputElement) staticTog.addEventListener('change', () => syncStaticIpFields(form));

      syncCellularFields(form);
      const cellTog = form.querySelector('[data-cellular-toggle]');
      if (cellTog instanceof HTMLInputElement) cellTog.addEventListener('change', () => syncCellularFields(form));

      syncBleFields(form);
      const bleTog = form.querySelector('[data-ble-toggle]');
      if (bleTog instanceof HTMLInputElement) bleTog.addEventListener('change', () => syncBleFields(form));

      loadWifiNetworks(form);
    }

    // ── Wi-Fi network scan ───────────────────────────────────────────────────
    async function loadWifiNetworks(form) {
      const networkMode = form.dataset.networkMode ?? '';
      if (networkMode !== 'setup_ap' && networkMode !== 'station') return;

      const select     = form.querySelector('[data-wifi-ssid-select]');
      const statusNode = form.querySelector('[data-wifi-ssid-scan-status]');
      const menuList   = form.querySelector('.wifi-menu-list');
      const trigLbl    = form.querySelector('.wifi-trigger-label');
      if (!(select instanceof HTMLSelectElement)) return;

      if (statusNode instanceof HTMLElement) {
        statusNode.hidden = false;
        statusNode.textContent = 'Loading available Wi-Fi networks...';
      }

      try {
        const response = await fetch('/wifi-scan', { cache: 'no-store' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const payload  = await response.json();
        const networks = Array.isArray(payload?.networks) ? payload.networks : [];
        const savedSsid = select.value.trim();
        const savedInList = networks.some(n => n?.ssid === savedSsid);
        const seenSsids = new Set();

        // Always rebuild the hidden select
        select.innerHTML = '';
        const placeholder = document.createElement('option');
        placeholder.value = ''; placeholder.textContent = 'Select network...';
        placeholder.selected = savedSsid.length === 0;
        select.appendChild(placeholder);

        const appendOption = (value, label, selected) => {
          if (!value || seenSsids.has(value)) return;
          const opt = document.createElement('option');
          opt.value = value; opt.textContent = label; opt.selected = selected;
          select.appendChild(opt);
          seenSsids.add(value);
        };

        if (savedSsid && !savedInList) appendOption(savedSsid, `${savedSsid} (saved)`, true);
        for (const n of networks) {
          if (!n?.ssid) continue;
          appendOption(
            n.ssid,
            typeof n.rssi === 'number' ? `${n.ssid} (${n.rssi} dBm)` : n.ssid,
            n.ssid === savedSsid
          );
        }

        // If custom wifi picker is present, populate it too
        if (menuList) {
          menuList.innerHTML = '';
          for (const n of networks) {
            if (!n?.ssid) continue;
            const rssi   = typeof n.rssi === 'number' ? n.rssi : -100;
            const auth   = n.auth_mode ?? '';
            const isSelected = n.ssid === savedSsid;
            const div = document.createElement('div');
            div.className = 'wifi-option' + (isSelected ? ' selected' : '');
            div.setAttribute('data-ssid', n.ssid);
            div.innerHTML =
              `<span class="wifi-bars" data-rssi="${rssi}"></span>` +
              `<div style="flex:1"><div style="font-size:13px;font-weight:500">${escHtml(n.ssid)}</div>` +
              `<div class="mono-meta">${rssi} dBm${auth ? ' · ' + escHtml(auth) : ''}</div></div>`;
            menuList.appendChild(div);
          }
          // Update trigger label to show current SSID
          if (trigLbl && savedSsid) trigLbl.textContent = savedSsid;
        }

        if (statusNode instanceof HTMLElement) {
          if (typeof payload?.last_scan_error === 'string' && payload.last_scan_error) {
            statusNode.textContent = `Wi-Fi scan error: ${payload.last_scan_error}`;
          } else if (networks.length > 0) {
            statusNode.textContent = `Available networks: ${networks.length}.`;
          } else {
            statusNode.textContent = 'No Wi-Fi networks found.';
          }
        }
      } catch {
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Failed to load available Wi-Fi networks.';
      }
    }

    function escHtml(s) {
      return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    // ── SNTP check ───────────────────────────────────────────────────────────
    async function checkSntp(button, input, statusNode) {
      button.disabled = true;
      statusNode.hidden = false;
      statusNode.textContent = 'Checking SNTP server...';
      try {
        const response = await fetch('/check-sntp', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: `server=${encodeURIComponent(input.value.trim())}`,
          cache: 'no-store',
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const payload = await response.json();
        if (payload?.success) {
          statusNode.textContent = 'SNTP check succeeded. Server is reachable.';
        } else {
          const err = payload?.error ?? 'unknown';
          statusNode.textContent =
            err === 'invalid_input'  ? 'Invalid server address.' :
            err === 'not_connected'  ? 'Not connected to Wi-Fi. Cannot check SNTP server.' :
            err === 'request_timeout'? 'Request timed out. Check the connection and retry.' :
                                       'SNTP sync failed. Server may be unreachable or invalid.';
        }
      } catch {
        statusNode.textContent = 'Request failed. Could not reach the device.';
      } finally {
        button.disabled = false;
      }
    }

    for (const button of document.querySelectorAll('[data-check-sntp]')) {
      if (!(button instanceof HTMLButtonElement)) continue;
      const input      = document.getElementById('sntp_server');
      const statusNode = document.querySelector('[data-check-sntp-status]');
      if (input instanceof HTMLInputElement && statusNode instanceof HTMLElement) {
        button.addEventListener('click', () => checkSntp(button, input, statusNode));
      }
    }

    // ── Backend card sync ────────────────────────────────────────────────────
    const MAP_TILE_URL = 'https://tile.openstreetmap.org/{z}/{x}/{y}.png';
    const MAP_TILE_ATTRIBUTION = '&copy; OpenStreetMap contributors';

    function parseCoordinatePair(latInput, lonInput) {
      const lat = Number.parseFloat(latInput.value.trim());
      const lon = Number.parseFloat(lonInput.value.trim());
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
      if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return null;
      return { lat, lon };
    }

    function formatCoordinate(value) {
      return value.toFixed(6).replace(/0+$/, '').replace(/\.$/, '.0');
    }

    function dispatchCoordinateInput(input) {
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.dispatchEvent(new Event('change', { bubbles: true }));
    }

    function initAir360LocationMap(container) {
      if (!(container instanceof HTMLElement)) return;
      if (container.dataset.mapReady === 'true') return;
      container.dataset.mapReady = 'true';

      const latInput = document.getElementById(container.dataset.latInput || '');
      const lonInput = document.getElementById(container.dataset.lonInput || '');
      const statusNode = container.parentElement?.querySelector('[data-air360-location-map-status]');
      if (!(latInput instanceof HTMLInputElement) || !(lonInput instanceof HTMLInputElement)) return;

      if (!window.maplibregl) {
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Map unavailable. Manual coordinates remain editable.';
        return;
      }

      const initial = parseCoordinatePair(latInput, lonInput);
      let map = null;
      try {
        map = new maplibregl.Map({
          container,
          style: {
            version: 8,
            sources: {
              osm: {
                type: 'raster',
                tiles: [MAP_TILE_URL],
                tileSize: 256,
                maxzoom: 19,
                attribution: MAP_TILE_ATTRIBUTION,
              },
            },
            layers: [
              {
                id: 'osm',
                type: 'raster',
                source: 'osm',
              },
            ],
          },
          center: initial ? [initial.lon, initial.lat] : [0, 20],
          zoom: initial ? 11 : 2,
          attributionControl: true,
        });
      } catch {
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Map unavailable. Manual coordinates remain editable.';
        return;
      }
      map.addControl(new maplibregl.NavigationControl({ showCompass: false }), 'top-left');
      map.on('error', () => {
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Map tiles unavailable. Manual coordinates remain editable.';
      });

      const markerElement = document.createElement('div');
      markerElement.className = 'location-map-marker';
      let marker = null;

      function setMarker(point, centerMap) {
        if (!marker) {
          marker = new maplibregl.Marker({
            element: markerElement,
            anchor: 'center',
          }).setLngLat([point.lon, point.lat]).addTo(map);
        } else {
          marker.setLngLat([point.lon, point.lat]);
        }
        if (centerMap) {
          map.easeTo({
            center: [point.lon, point.lat],
            zoom: Math.max(map.getZoom(), 10),
          });
        }
      }

      if (initial) setMarker(initial, false);

      map.on('click', event => {
        const point = {
          lat: event.lngLat.lat,
          lon: event.lngLat.lng,
        };
        latInput.value = formatCoordinate(point.lat);
        lonInput.value = formatCoordinate(point.lon);
        setMarker(point, false);
        dispatchCoordinateInput(latInput);
        dispatchCoordinateInput(lonInput);
        if (statusNode instanceof HTMLElement) statusNode.textContent = '';
      });

      function syncFromInputs() {
        const point = parseCoordinatePair(latInput, lonInput);
        if (!point) return;
        setMarker(point, true);
        if (statusNode instanceof HTMLElement) statusNode.textContent = '';
      }

      latInput.addEventListener('change', syncFromInputs);
      lonInput.addEventListener('change', syncFromInputs);

      const card = container.closest('[data-backend-card]');
      const enabled = card?.querySelector('[data-backend-enabled-toggle]');
      if (enabled instanceof HTMLInputElement) {
        enabled.addEventListener('change', () => {
          window.setTimeout(() => map.resize(), 80);
        });
      }
      window.setTimeout(() => map.resize(), 80);
    }

    function syncBackendCard(panel) {
      syncBackendProtocolPort(panel);
    }

    function syncBackendProtocolPort(panel) {
      const httpsToggle = panel.querySelector('[data-backend-https-toggle]');
      const portInput   = panel.querySelector('[data-backend-port-input]');
      if (!(httpsToggle instanceof HTMLInputElement) || !(portInput instanceof HTMLInputElement)) return;
      const nextDefault = httpsToggle.checked ? '443' : '80';
      const cur = portInput.value.trim();
      if (!cur || cur === '80' || cur === '443') portInput.value = nextDefault;
    }

    for (const panel of document.querySelectorAll('[data-backend-card]')) {
      if (!(panel instanceof HTMLElement)) continue;
      syncBackendCard(panel);
      const httpsTog = panel.querySelector('[data-backend-https-toggle]');
      if (httpsTog instanceof HTMLInputElement) httpsTog.addEventListener('change', () => syncBackendProtocolPort(panel));
    }

    for (const container of document.querySelectorAll('[data-air360-location-map]')) {
      initAir360LocationMap(container);
    }

    // ── Logs console ─────────────────────────────────────────────────────────
    const logsConsole = document.querySelector('[data-logs-console]');
    if (logsConsole instanceof HTMLTextAreaElement) {
      const logsStatus = document.querySelector('[data-logs-status]');
      let autoScroll = true;

      logsConsole.addEventListener('scroll', () => {
        autoScroll = logsConsole.scrollHeight - logsConsole.scrollTop - logsConsole.clientHeight < 40;
      });

      async function refreshLogs() {
        try {
          const response = await fetch('/logs/data', { cache: 'no-store' });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          logsConsole.value = await response.text();
          if (autoScroll) logsConsole.scrollTop = logsConsole.scrollHeight;
          if (logsStatus instanceof HTMLElement) logsStatus.textContent = '';
        } catch {
          if (logsStatus instanceof HTMLElement) logsStatus.textContent = 'Failed to fetch logs.';
        }
      }

      refreshLogs();
      setInterval(refreshLogs, 2000);
    }

    // ── Runtime dump copy ────────────────────────────────────────────────────
    async function copyRuntimeDump(button, textarea, statusNode) {
      button.disabled = true;
      if (statusNode instanceof HTMLElement) statusNode.textContent = 'Copying...';
      try {
        if (navigator.clipboard?.writeText) {
          await navigator.clipboard.writeText(textarea.value);
        } else {
          textarea.focus(); textarea.select();
          textarea.setSelectionRange(0, textarea.value.length);
          if (!document.execCommand('copy')) throw new Error('copy_failed');
        }
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Copied';
      } catch {
        if (statusNode instanceof HTMLElement) statusNode.textContent = 'Copy failed. Use manual selection.';
      } finally {
        button.disabled = false;
      }
    }

    for (const button of document.querySelectorAll('[data-copy-runtime-dump]')) {
      if (!(button instanceof HTMLButtonElement)) continue;
      const panel    = button.closest('.runtime-dump');
      const textarea = panel?.querySelector('.runtime-dump__console');
      const status   = panel?.querySelector('[data-copy-runtime-dump-status]');
      if (textarea instanceof HTMLTextAreaElement) {
        button.addEventListener('click', () => copyRuntimeDump(button, textarea, status));
      }
    }

    // ── Form confirm dialogs ─────────────────────────────────────────────────
    for (const form of document.querySelectorAll('form[data-confirm]')) {
      if (!(form instanceof HTMLFormElement)) continue;
      form.addEventListener('submit', e => {
        const msg = form.getAttribute('data-confirm');
        if (msg && !window.confirm(msg)) e.preventDefault();
      });
    }

    // ── Dirty-form navigation guard ──────────────────────────────────────────
    window.addEventListener('beforeunload', e => {
      if (dirtyForms.size === 0) return;
      e.preventDefault();
      e.returnValue = '';
    });
  });
})();

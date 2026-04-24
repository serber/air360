document.addEventListener("DOMContentLoaded", () => {
  const dirtyForms = new Set();

  function setFormDirty(form, dirty) {
    const panel = form.closest(".panel");
    form.dataset.dirty = dirty ? "true" : "false";

    if (dirty) {
      dirtyForms.add(form);
    } else {
      dirtyForms.delete(form);
    }

    if (panel instanceof HTMLElement) {
      panel.classList.toggle("panel--dirty", dirty);
    }
  }

  function syncSensorForm(form) {
    const sensorTypeSelect = form.querySelector("[data-sensor-type-select]");
    if (!(sensorTypeSelect instanceof HTMLSelectElement)) {
      return;
    }

    const selectedOption =
      sensorTypeSelect.selectedOptions.length > 0
        ? sensorTypeSelect.selectedOptions[0]
        : null;
    const requiresPin = selectedOption?.dataset.requiresPin === "true";
    const requiresI2c = selectedOption?.dataset.requiresI2c === "true";
    const defaultsHint = selectedOption?.dataset.defaultsHint ?? "";
    const defaultI2cAddress = selectedOption?.dataset.defaultI2cAddress ?? "";

    const i2cField = form.querySelector("[data-sensor-i2c-field]");
    if (i2cField instanceof HTMLElement) {
      i2cField.hidden = !requiresI2c;
      for (const control of i2cField.querySelectorAll("input, select, textarea")) {
        if (
          control instanceof HTMLInputElement ||
          control instanceof HTMLSelectElement ||
          control instanceof HTMLTextAreaElement
        ) {
          control.disabled = !requiresI2c;
          if (
            requiresI2c &&
            control instanceof HTMLInputElement &&
            control.name === "i2c_address" &&
            defaultI2cAddress.length > 0
          ) {
            control.value = defaultI2cAddress;
          }
        }
      }
    }

    const pinField = form.querySelector("[data-sensor-pin-field]");
    if (pinField instanceof HTMLElement) {
      pinField.hidden = !requiresPin;
      for (const control of pinField.querySelectorAll("input, select, textarea")) {
        if (
          control instanceof HTMLInputElement ||
          control instanceof HTMLSelectElement ||
          control instanceof HTMLTextAreaElement
        ) {
          control.disabled = !requiresPin;
        }
      }
    }

    const defaultsNode = form.querySelector("[data-sensor-defaults]");
    if (defaultsNode instanceof HTMLElement) {
      defaultsNode.textContent = defaultsHint;
      defaultsNode.hidden = defaultsHint.length === 0;
    }
  }

  function syncConfigForm(form) {
    const ssidSelect = form.querySelector("[data-wifi-ssid-select]");
    const passwordField = form.querySelector("[data-wifi-password-field]");
    const hintNode = form.querySelector("[data-wifi-password-hint]");
    const passwordInput = form.querySelector("#wifi_password");
    const toggleButton = form.querySelector("[data-secret-toggle='wifi_password']");

    if (!(ssidSelect instanceof HTMLSelectElement)) {
      return;
    }

    const hasStationConfig = ssidSelect.value.trim().length > 0;

    if (passwordField instanceof HTMLElement) {
      passwordField.classList.toggle("field--disabled", !hasStationConfig);
    }

    if (passwordInput instanceof HTMLInputElement) {
      passwordInput.disabled = !hasStationConfig;
    }

    if (toggleButton instanceof HTMLButtonElement) {
      toggleButton.disabled = !hasStationConfig;
    }

    if (hintNode instanceof HTMLElement) {
      hintNode.textContent = hasStationConfig
        ? "Leave Wi-Fi SSID empty to reboot into setup AP mode."
        : "Station mode is disabled while Wi-Fi SSID is empty.";
    }
  }

  async function loadWifiNetworks(form) {
    const networkMode = form.dataset.networkMode ?? "";
    if (networkMode !== "setup_ap" && networkMode !== "station") {
      return;
    }

    const select = form.querySelector("[data-wifi-ssid-select]");
    const statusNode = form.querySelector("[data-wifi-ssid-scan-status]");
    if (!(select instanceof HTMLSelectElement)) {
      return;
    }

    if (statusNode instanceof HTMLElement) {
      statusNode.hidden = false;
      statusNode.textContent = "Loading available Wi-Fi networks...";
    }

    try {
      const response = await fetch("/wifi-scan", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const payload = await response.json();
      const networks = Array.isArray(payload?.networks) ? payload.networks : [];
      const selectedValue = select.value.trim();
      const selectedValuePresent = networks.some(
        (network) => network && typeof network.ssid === "string" && network.ssid === selectedValue
      );
      const seenSsids = new Set();

      select.innerHTML = "";
      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = "Select network...";
      placeholder.selected = selectedValue.length === 0;
      select.appendChild(placeholder);

      const appendOption = (value, label, selected) => {
        if (typeof value !== "string" || value.length === 0 || seenSsids.has(value)) {
          return;
        }

        const option = document.createElement("option");
        option.value = value;
        option.textContent = label;
        option.selected = selected;
        select.appendChild(option);
        seenSsids.add(value);
      };

      if (selectedValue.length > 0 && !selectedValuePresent) {
        appendOption(selectedValue, `${selectedValue} (saved)`, true);
      }

      for (const network of networks) {
        if (!network || typeof network.ssid !== "string" || network.ssid.length === 0) {
          continue;
        }

        appendOption(
          network.ssid,
          typeof network.rssi === "number"
            ? `${network.ssid} (${network.rssi} dBm)`
            : network.ssid,
          network.ssid === selectedValue
        );
      }

      if (statusNode instanceof HTMLElement) {
        if (typeof payload?.last_scan_error === "string" && payload.last_scan_error.length > 0) {
          statusNode.textContent = `Wi-Fi scan error: ${payload.last_scan_error}`;
        } else if (networks.length > 0) {
          statusNode.textContent = `Available networks: ${networks.length}.`;
        } else {
          statusNode.textContent = "No Wi-Fi networks found.";
        }
      }
    } catch (error) {
      if (statusNode instanceof HTMLElement) {
        statusNode.textContent = "Failed to load available Wi-Fi networks.";
      }
    }
  }

  async function checkSntp(button, input, statusNode) {
    const server = input.value.trim();

    button.disabled = true;
    statusNode.hidden = false;
    statusNode.textContent = "Checking SNTP server...";

    try {
      const response = await fetch("/check-sntp", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: `server=${encodeURIComponent(server)}`,
        cache: "no-store",
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const payload = await response.json();

      if (payload?.success) {
        statusNode.textContent = "SNTP check succeeded. Server is reachable.";
      } else {
        const error = payload?.error ?? "unknown";
        if (error === "invalid_input") {
          statusNode.textContent = "Invalid server address.";
        } else if (error === "not_connected") {
          statusNode.textContent = "Not connected to Wi-Fi. Cannot check SNTP server.";
        } else if (error === "request_timeout") {
          statusNode.textContent = "Request timed out while sending data. Check the connection and retry.";
        } else {
          statusNode.textContent = "SNTP sync failed. Server may be unreachable or invalid.";
        }
      }
    } catch {
      statusNode.textContent = "Request failed. Could not reach the device.";
    } finally {
      button.disabled = false;
    }
  }

  async function copyRuntimeDump(button, textarea, statusNode) {
    const text = textarea.value;
    button.disabled = true;
    if (statusNode instanceof HTMLElement) {
      statusNode.textContent = "Copying...";
    }

    try {
      if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
        await navigator.clipboard.writeText(text);
      } else {
        textarea.focus();
        textarea.select();
        textarea.setSelectionRange(0, textarea.value.length);
        if (!document.execCommand("copy")) {
          throw new Error("copy_failed");
        }
      }

      if (statusNode instanceof HTMLElement) {
        statusNode.textContent = "Copied";
      }
    } catch {
      if (statusNode instanceof HTMLElement) {
        statusNode.textContent = "Copy failed. Use manual selection.";
      }
    } finally {
      button.disabled = false;
    }
  }

  function setGroupEnabled(group, enabled) {
    group.classList.toggle("field--disabled", !enabled);
    group.setAttribute("aria-disabled", enabled ? "false" : "true");

    for (const control of group.querySelectorAll("input, select, textarea, button")) {
      if (
        control instanceof HTMLInputElement ||
        control instanceof HTMLSelectElement ||
        control instanceof HTMLTextAreaElement ||
        control instanceof HTMLButtonElement
      ) {
        const allowWhenDisabled = control.dataset.allowWhenDisabled === "true";
        control.disabled = !enabled && !allowWhenDisabled;
      }
    }
  }

  function syncStaticIpFields(form) {
    const toggle = form.querySelector("[data-static-ip-toggle]");
    const group = form.querySelector("[data-static-ip-fields]");
    if (!(toggle instanceof HTMLInputElement) || !(group instanceof HTMLElement)) {
      return;
    }
    setGroupEnabled(group, toggle.checked);
  }

  function syncCellularFields(form) {
    const toggle = form.querySelector("[data-cellular-toggle]");
    const group = form.querySelector("[data-cellular-fields]");
    if (!(toggle instanceof HTMLInputElement) || !(group instanceof HTMLElement)) {
      return;
    }
    setGroupEnabled(group, toggle.checked);
  }

  function syncBleFields(form) {
    const toggle = form.querySelector("[data-ble-toggle]");
    const group = form.querySelector("[data-ble-fields]");
    if (!(toggle instanceof HTMLInputElement) || !(group instanceof HTMLElement)) {
      return;
    }
    setGroupEnabled(group, toggle.checked);
  }

  function syncBackendCard(panel) {
    const checkbox = panel.querySelector("[data-backend-enabled-toggle]");
    const group = panel.querySelector("[data-backend-fields]");
    if (!(checkbox instanceof HTMLInputElement) || !(group instanceof HTMLElement)) {
      return;
    }

    panel.classList.toggle("panel--inactive", !checkbox.checked);
    setGroupEnabled(group, checkbox.checked);
    syncBackendProtocolPort(panel);
  }

  function defaultBackendPort(useHttps) {
    return useHttps ? "443" : "80";
  }

  function syncBackendProtocolPort(panel) {
    const httpsToggle = panel.querySelector("[data-backend-https-toggle]");
    const portInput = panel.querySelector("[data-backend-port-input]");
    if (!(httpsToggle instanceof HTMLInputElement) || !(portInput instanceof HTMLInputElement)) {
      return;
    }

    const nextDefaultPort = defaultBackendPort(httpsToggle.checked);
    const currentPort = portInput.value.trim();

    if (currentPort.length === 0 || currentPort === "80" || currentPort === "443") {
      portInput.value = nextDefaultPort;
    }
  }

  for (const button of document.querySelectorAll("[data-secret-toggle]")) {
    const inputId = button.getAttribute("data-secret-toggle");
    if (!inputId) {
      continue;
    }

    const input = document.getElementById(inputId);
    if (!(input instanceof HTMLInputElement)) {
      continue;
    }

    button.addEventListener("click", (event) => {
      event.preventDefault();
      const reveal = input.type === "password";
      input.type = reveal ? "text" : "password";
      button.textContent = reveal ? "Hide" : "Show";
    });
  }

  for (const form of document.querySelectorAll("form[data-dirty-track]")) {
    if (!(form instanceof HTMLFormElement)) {
      continue;
    }

    const markDirty = () => setFormDirty(form, true);
    for (const field of form.querySelectorAll("input, select, textarea")) {
      if (
        field instanceof HTMLInputElement ||
        field instanceof HTMLSelectElement ||
        field instanceof HTMLTextAreaElement
      ) {
        if (field.type === "hidden") {
          continue;
        }

        field.addEventListener("input", markDirty);
        field.addEventListener("change", markDirty);
      }
    }

    form.addEventListener("submit", () => {
      setFormDirty(form, false);
    });
  }

  for (const form of document.querySelectorAll("form[data-sensor-form]")) {
    if (!(form instanceof HTMLFormElement)) {
      continue;
    }

    syncSensorForm(form);
    const sensorTypeSelect = form.querySelector("[data-sensor-type-select]");
    if (sensorTypeSelect instanceof HTMLSelectElement) {
      sensorTypeSelect.addEventListener("change", () => {
        syncSensorForm(form);
      });
    }
  }

  for (const form of document.querySelectorAll("form[data-dirty-track='config']")) {
    if (!(form instanceof HTMLFormElement)) {
      continue;
    }

    syncConfigForm(form);
    const ssidSelect = form.querySelector("[data-wifi-ssid-select]");
    if (ssidSelect instanceof HTMLSelectElement) {
      ssidSelect.addEventListener("change", () => {
        syncConfigForm(form);
      });
    }

    syncStaticIpFields(form);
    const staticIpToggle = form.querySelector("[data-static-ip-toggle]");
    if (staticIpToggle instanceof HTMLInputElement) {
      staticIpToggle.addEventListener("change", () => {
        syncStaticIpFields(form);
      });
    }

    syncCellularFields(form);
    const cellularToggle = form.querySelector("[data-cellular-toggle]");
    if (cellularToggle instanceof HTMLInputElement) {
      cellularToggle.addEventListener("change", () => {
        syncCellularFields(form);
      });
    }

    syncBleFields(form);
    const bleToggle = form.querySelector("[data-ble-toggle]");
    if (bleToggle instanceof HTMLInputElement) {
      bleToggle.addEventListener("change", () => {
        syncBleFields(form);
      });
    }

    loadWifiNetworks(form);
  }

  for (const button of document.querySelectorAll("[data-check-sntp]")) {
    if (!(button instanceof HTMLButtonElement)) {
      continue;
    }

    const input = document.getElementById("sntp_server");
    const statusNode = document.querySelector("[data-check-sntp-status]");

    if (!(input instanceof HTMLInputElement) || !(statusNode instanceof HTMLElement)) {
      continue;
    }

    button.addEventListener("click", () => {
      checkSntp(button, input, statusNode);
    });
  }

  for (const panel of document.querySelectorAll("[data-backend-card]")) {
    if (!(panel instanceof HTMLElement)) {
      continue;
    }

    syncBackendCard(panel);
    const checkbox = panel.querySelector("[data-backend-enabled-toggle]");
    if (checkbox instanceof HTMLInputElement) {
      checkbox.addEventListener("change", () => {
        syncBackendCard(panel);
      });
    }

    const httpsToggle = panel.querySelector("[data-backend-https-toggle]");
    if (httpsToggle instanceof HTMLInputElement) {
      httpsToggle.addEventListener("change", () => {
        syncBackendProtocolPort(panel);
      });
    }
  }

  const logsConsole = document.querySelector("[data-logs-console]");
  if (logsConsole instanceof HTMLTextAreaElement) {
    const logsStatus = document.querySelector("[data-logs-status]");
    let autoScroll = true;

    logsConsole.addEventListener("scroll", () => {
      autoScroll =
        logsConsole.scrollHeight - logsConsole.scrollTop - logsConsole.clientHeight < 40;
    });

    async function refreshLogs() {
      try {
        const response = await fetch("/logs/data", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const text = await response.text();
        logsConsole.value = text;
        if (autoScroll) {
          logsConsole.scrollTop = logsConsole.scrollHeight;
        }
        if (logsStatus instanceof HTMLElement) {
          logsStatus.textContent = "";
        }
      } catch {
        if (logsStatus instanceof HTMLElement) {
          logsStatus.textContent = "Failed to fetch logs.";
        }
      }
    }

    refreshLogs();
    setInterval(refreshLogs, 2000);
  }

  for (const button of document.querySelectorAll("[data-copy-runtime-dump]")) {
    if (!(button instanceof HTMLButtonElement)) {
      continue;
    }

    const panel = button.closest(".runtime-dump");
    const textarea = panel?.querySelector(".runtime-dump__console");
    const statusNode = panel?.querySelector("[data-copy-runtime-dump-status]");
    if (!(textarea instanceof HTMLTextAreaElement)) {
      continue;
    }

    button.addEventListener("click", () => {
      copyRuntimeDump(button, textarea, statusNode);
    });
  }

  for (const form of document.querySelectorAll("form[data-confirm]")) {
    if (!(form instanceof HTMLFormElement)) {
      continue;
    }

    form.addEventListener("submit", (event) => {
      const message = form.getAttribute("data-confirm");
      if (message && !window.confirm(message)) {
        event.preventDefault();
      }
    });
  }

  window.addEventListener("beforeunload", (event) => {
    if (dirtyForms.size === 0) {
      return;
    }

    event.preventDefault();
    event.returnValue = "";
  });
});

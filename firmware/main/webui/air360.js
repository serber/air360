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
    const defaultsHint = selectedOption?.dataset.defaultsHint ?? "";

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
    const ssidInput = form.querySelector("[data-wifi-ssid-input]");
    const passwordField = form.querySelector("[data-wifi-password-field]");
    const hintNode = form.querySelector("[data-wifi-password-hint]");
    const passwordInput = form.querySelector("#wifi_password");
    const toggleButton = form.querySelector("[data-secret-toggle='wifi_password']");

    if (!(ssidInput instanceof HTMLInputElement)) {
      return;
    }

    const hasStationConfig = ssidInput.value.trim().length > 0;

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

  function syncBackendCard(panel) {
    const checkbox = panel.querySelector("[data-backend-enabled-toggle]");
    if (!(checkbox instanceof HTMLInputElement)) {
      return;
    }

    panel.classList.toggle("panel--inactive", !checkbox.checked);
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
    const ssidInput = form.querySelector("[data-wifi-ssid-input]");
    if (ssidInput instanceof HTMLInputElement) {
      ssidInput.addEventListener("input", () => {
        syncConfigForm(form);
      });
    }
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

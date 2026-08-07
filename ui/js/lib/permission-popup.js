(() => {
  const REMEMBER_UNTIL_CLOSE = "session";
  const REMEMBER_FOREVER = "forever";
  const REMEMBER_ASK = "ask";

  let active = null;
  let chipHost = null;
  let chipEl = null;

  function storageKey(permission, origin) {
    return `omni.permission.${permission}:${origin || location.host}`;
  }

  function readDecision(permission, origin) {
    const key = storageKey(permission, origin);
    try {
      const forever = localStorage.getItem(key);
      if (forever === "allow" || forever === "block") {
        return forever;
      }
    } catch (_) {
      // ignore
    }
    try {
      const session = sessionStorage.getItem(key);
      if (session === "allow" || session === "block") {
        return session;
      }
    } catch (_) {
      // ignore
    }
    return null;
  }

  function writeDecision(permission, origin, decision, remember) {
    const key = storageKey(permission, origin);
    try {
      localStorage.removeItem(key);
      sessionStorage.removeItem(key);
    } catch (_) {
      // ignore
    }
    if (remember === REMEMBER_ASK) {
      return;
    }
    try {
      if (remember === REMEMBER_FOREVER) {
        localStorage.setItem(key, decision);
      } else {
        sessionStorage.setItem(key, decision);
      }
    } catch (_) {
      // ignore
    }
  }

  function pinIconSvg(size = 18) {
    return `<svg viewBox="0 0 24 24" width="${size}" height="${size}" aria-hidden="true"><path d="M12 22s7-7.2 7-12a7 7 0 1 0-14 0c0 4.8 7 12 7 12z" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="10" r="2.5" fill="none" stroke="currentColor" stroke-width="2"/></svg>`;
  }

  function hideChip() {
    if (chipEl) {
      chipEl.hidden = true;
      chipEl.classList.remove("is-active");
    }
  }

  function showChip(label, onClick) {
    if (!chipHost) {
      return null;
    }
    if (!chipEl) {
      chipEl = document.createElement("button");
      chipEl.type = "button";
      chipEl.className = "qb-perm-chip";
      chipHost.appendChild(chipEl);
    }
    chipEl.hidden = false;
    chipEl.classList.add("is-active");
    chipEl.innerHTML = `${pinIconSvg(14)}<span class="qb-perm-chip-text"></span>`;
    const text = chipEl.querySelector(".qb-perm-chip-text");
    if (text) {
      text.textContent = label || "Use your location?";
    }
    chipEl.onclick = onClick || null;
    return chipEl;
  }

  function setChipHost(el) {
    chipHost = el || null;
    if (chipEl && chipHost && chipEl.parentElement !== chipHost) {
      chipHost.appendChild(chipEl);
    }
  }

  function closeActive(result) {
    if (!active) {
      return;
    }
    const { overlay, resolve } = active;
    active = null;
    overlay.remove();
    hideChip();
    resolve(result);
  }

  function defaultChipLabel(permission, title) {
    if (permission === "geolocation" || /location/i.test(title || "")) {
      return "Use your location?";
    }
    return String(title || "Permission required");
  }

  /**
   * Browser-style permission prompt with optional search-bar chip.
   * @returns {Promise<'allow'|'block'>}
   */
  function request(options = {}) {
    const origin = String(options.origin || location.host || "qubrain.org");
    const permission = String(options.permission || "geolocation");
    const title = String(options.title || "Know your location");
    const precise = options.precise !== false;
    const learnMoreUrl =
      options.learnMoreUrl ||
      "https://support.google.com/chrome/answer/142065";
    const chipLabel =
      options.chipLabel || defaultChipLabel(permission, title);

    if (options.chipHost) {
      setChipHost(options.chipHost);
    }

    const remembered = readDecision(permission, origin);
    if (remembered) {
      hideChip();
      return Promise.resolve(remembered);
    }

    if (active) {
      closeActive("block");
    }

    return new Promise((resolve) => {
      const openPopup = () => {
        if (active) {
          return;
        }

        const overlay = document.createElement("div");
        overlay.className = "qb-perm is-anchored";
        overlay.setAttribute("role", "dialog");
        overlay.setAttribute("aria-modal", "true");
        overlay.setAttribute("aria-label", "Permission request");

        const card = document.createElement("div");
        card.className = "qb-perm-card";

        const head = document.createElement("div");
        head.className = "qb-perm-head";
        const asking = document.createElement("p");
        asking.className = "qb-perm-asking";
        asking.innerHTML = `<strong>${escapeHtml(origin)}</strong> is asking you to`;
        const closeBtn = document.createElement("button");
        closeBtn.type = "button";
        closeBtn.className = "qb-perm-x";
        closeBtn.setAttribute("aria-label", "Close");
        closeBtn.textContent = "×";
        head.append(asking, closeBtn);

        const body = document.createElement("div");
        body.className = "qb-perm-body";

        const permRow = document.createElement("div");
        permRow.className = "qb-perm-title-row";
        const icon = document.createElement("span");
        icon.className = "qb-perm-icon";
        icon.innerHTML = options.iconHtml || pinIconSvg(18);
        const permTitle = document.createElement("div");
        permTitle.className = "qb-perm-title";
        permTitle.textContent = title;
        permRow.append(icon, permTitle);

        const rememberRow = document.createElement("div");
        rememberRow.className = "qb-perm-remember";
        const rememberLabel = document.createElement("label");
        rememberLabel.className = "qb-perm-remember-label";
        rememberLabel.textContent = "Remember my decision";
        rememberLabel.htmlFor = "qb-perm-remember-select";
        const select = document.createElement("select");
        select.id = "qb-perm-remember-select";
        select.className = "qb-perm-select";
        [
          { value: REMEMBER_UNTIL_CLOSE, label: "until I close this site" },
          { value: REMEMBER_FOREVER, label: "forever" },
          { value: REMEMBER_ASK, label: "ask every time" },
        ].forEach((opt) => {
          const o = document.createElement("option");
          o.value = opt.value;
          o.textContent = opt.label;
          select.appendChild(o);
        });
        select.value = REMEMBER_UNTIL_CLOSE;
        rememberRow.append(rememberLabel, select);

        let preciseBlock = null;
        if (precise) {
          preciseBlock = document.createElement("div");
          preciseBlock.className = "qb-perm-precise";
          preciseBlock.innerHTML = `
            <span class="qb-perm-warn" aria-hidden="true">⚠</span>
            <div class="qb-perm-precise-text">
              <span>This site has requested your <strong>precise location</strong>.</span>
              <a class="qb-perm-link" href="${escapeHtml(learnMoreUrl)}" target="_blank" rel="noopener noreferrer">Learn more</a>
            </div>`;
        }

        const actions = document.createElement("div");
        actions.className = "qb-perm-actions";
        const allowBtn = document.createElement("button");
        allowBtn.type = "button";
        allowBtn.className = "qb-perm-btn qb-perm-btn-allow";
        allowBtn.textContent = "Allow";
        const blockBtn = document.createElement("button");
        blockBtn.type = "button";
        blockBtn.className = "qb-perm-btn qb-perm-btn-block";
        blockBtn.textContent = "Block";
        actions.append(allowBtn, blockBtn);

        body.append(permRow, rememberRow);
        if (preciseBlock) {
          body.appendChild(preciseBlock);
        }
        body.appendChild(actions);

        const foot = document.createElement("div");
        foot.className = "qb-perm-foot";
        foot.innerHTML = `You can change your site permission at any time. <a class="qb-perm-link" href="${escapeHtml(learnMoreUrl)}" target="_blank" rel="noopener noreferrer">Learn more</a>`;

        card.append(head, body, foot);
        overlay.appendChild(card);

        const finish = (decision) => {
          writeDecision(permission, origin, decision, select.value);
          closeActive(decision);
        };

        closeBtn.addEventListener("click", () => finish("block"));
        blockBtn.addEventListener("click", () => finish("block"));
        allowBtn.addEventListener("click", () => finish("allow"));
        overlay.addEventListener("click", (event) => {
          if (event.target === overlay) {
            finish("block");
          }
        });
        document.addEventListener(
          "keydown",
          (event) => {
            if (event.key === "Escape" && active?.overlay === overlay) {
              finish("block");
            }
          },
          { once: true },
        );

        const anchor = chipEl || options.anchorEl;
        if (anchor && typeof anchor.getBoundingClientRect === "function") {
          const rect = anchor.getBoundingClientRect();
          card.style.position = "fixed";
          const left = Math.min(
            Math.max(12, rect.left),
            window.innerWidth - 372,
          );
          card.style.top = `${Math.min(window.innerHeight - 24, rect.bottom + 8)}px`;
          card.style.left = `${left}px`;
          card.style.right = "auto";
        }

        document.body.appendChild(overlay);
        active = { overlay, resolve };
        allowBtn.focus();
      };

      // Brave-style: chip in search bar, popup underneath.
      const chip = showChip(chipLabel, () => {
        if (!active) {
          openPopup();
        }
      });
      openPopup();
      if (!chip && options.anchorEl) {
        // no chip host — already opened with anchor
      }
    });
  }

  function escapeHtml(value) {
    return String(value || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function clear(permission, origin) {
    const key = storageKey(permission, origin || location.host);
    try {
      localStorage.removeItem(key);
      sessionStorage.removeItem(key);
    } catch (_) {
      // ignore
    }
  }

  window.QuBrainPermission = {
    request,
    clear,
    read: readDecision,
    setChipHost,
    hideChip,
  };
})();

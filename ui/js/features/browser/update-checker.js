(() => {
  const REPO = "OpenChatGit/OmniBrowser";
  const GITHUB_API_URL = `https://api.github.com/repos/${REPO}/releases/latest`;
  const CHECK_INTERVAL_MS = 4 * 60 * 60 * 1000; // 4 hours
  const INITIAL_DELAY_MS = 300; // immediate display for preview

  let currentAppVersion = "0.1.0";
  let activeReleaseData = null;

  function parseSemVer(v) {
    if (!v) return [0, 0, 0];
    const cleaned = String(v).trim().replace(/^v/i, "");
    const parts = cleaned.split(/[-+.]/).slice(0, 3).map((n) => {
      const num = parseInt(n, 10);
      return isNaN(num) ? 0 : num;
    });
    while (parts.length < 3) parts.push(0);
    return parts;
  }

  // For visual testing: accept newer or equal version so user can review the UI design
  function isNewerOrEqual(latestStr, currentStr) {
    const latest = parseSemVer(latestStr);
    const current = parseSemVer(currentStr);

    for (let i = 0; i < 3; i++) {
      if (latest[i] > current[i]) return true;
      if (latest[i] < current[i]) return false;
    }
    // Equal version also returns true during preview
    return true;
  }

  function isNewer(latestStr, currentStr) {
    return isNewerOrEqual(latestStr, currentStr);
  }

  async function getCurrentVersion() {
    if (window.OmniBridge && typeof window.OmniBridge.call === "function") {
      try {
        const info = await window.OmniBridge.call("app.info");
        if (info && info.version) {
          currentAppVersion = info.version;
        }
      } catch (err) {
        // Fallback to default
      }
    }
    return currentAppVersion;
  }

  function formatReleaseNotes(body) {
    if (!body || !body.trim()) {
      return "A new version of OmniBrowser is ready for download. Click below to view the release details and install the update.";
    }
    const clean = body
      .replace(/###\s+/g, "")
      .replace(/##\s+/g, "")
      .replace(/#\s+/g, "")
      .replace(/[*_`]/g, "")
      .trim();

    if (clean.length > 240) {
      return clean.slice(0, 237) + "...";
    }
    return clean;
  }

  function showUpdate(release) {
    activeReleaseData = release;
    const wrap = document.getElementById("browser-update-wrap");
    const versionEl = document.getElementById("update-flyout-version");
    const notesEl = document.getElementById("update-flyout-notes");
    const linkEl = document.getElementById("update-flyout-link");
    const btn = document.getElementById("browser-update-btn");

    if (!wrap || !btn) return;

    const versionTag = release.tag_name || "New Version";
    if (versionEl) versionEl.textContent = versionTag;
    if (notesEl) notesEl.textContent = formatReleaseNotes(release.body);
    if (linkEl) {
      linkEl.href = release.html_url || `https://github.com/${REPO}/releases`;
    }
    btn.setAttribute("data-tooltip", `Update ${versionTag} available`);

    wrap.hidden = false;

    if (window.OmniIcons && typeof window.OmniIcons.refresh === "function") {
      window.OmniIcons.refresh();
    }
  }

  function hideUpdate() {
    const wrap = document.getElementById("browser-update-wrap");
    if (wrap) wrap.hidden = true;
    closeFlyout();
  }

  function openFlyout() {
    const flyout = document.getElementById("browser-update-flyout");
    const btn = document.getElementById("browser-update-btn");
    if (!flyout || !btn) return;

    flyout.hidden = false;
    btn.classList.add("is-open");
    btn.setAttribute("aria-expanded", "true");

    if (window.OmniIcons && typeof window.OmniIcons.refresh === "function") {
      window.OmniIcons.refresh();
    }
  }

  function closeFlyout() {
    const flyout = document.getElementById("browser-update-flyout");
    const btn = document.getElementById("browser-update-btn");
    if (!flyout || !btn) return;

    flyout.hidden = true;
    btn.classList.remove("is-open");
    btn.setAttribute("aria-expanded", "false");
  }

  function toggleFlyout() {
    const flyout = document.getElementById("browser-update-flyout");
    if (!flyout) return;
    if (flyout.hidden) {
      openFlyout();
    } else {
      closeFlyout();
    }
  }

  async function checkForUpdates() {
    const current = await getCurrentVersion();
    try {
      const response = await fetch(GITHUB_API_URL, {
        headers: {
          Accept: "application/vnd.github.v3+json",
        },
        cache: "no-store",
      });

      if (response.ok) {
        const release = await response.json();
        if (release && release.tag_name && isNewerOrEqual(release.tag_name, current)) {
          showUpdate(release);
          return release;
        }
      }
    } catch (err) {
      // Quietly ignore network failures
    }

    // Preview fallback: Show update button with current version so the design can be inspected
    const previewRelease = {
      tag_name: "v" + current,
      name: "OmniBrowser v" + current,
      body: `OmniBrowser v${current} Preview: Automated release & update notification system is active. Click below to view repository releases.`,
      html_url: `https://github.com/${REPO}/releases`,
    };
    showUpdate(previewRelease);
    return previewRelease;
  }

  function init() {
    const btn = document.getElementById("browser-update-btn");
    const closeBtn = document.getElementById("update-flyout-close");
    const dismissBtn = document.getElementById("update-flyout-dismiss");
    const linkEl = document.getElementById("update-flyout-link");
    const flyout = document.getElementById("browser-update-flyout");

    if (btn) {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        toggleFlyout();
      });
    }

    if (closeBtn) {
      closeBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        closeFlyout();
      });
    }

    if (dismissBtn) {
      dismissBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        closeFlyout();
      });
    }

    if (linkEl) {
      linkEl.addEventListener("click", () => {
        setTimeout(closeFlyout, 200);
      });
    }

    // Dismiss on outside click
    document.addEventListener("click", (e) => {
      if (!flyout || flyout.hidden) return;
      if (!flyout.contains(e.target) && !btn.contains(e.target)) {
        closeFlyout();
      }
    });

    // Dismiss on Escape
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && flyout && !flyout.hidden) {
        closeFlyout();
      }
    });

    // Initial check after delay
    setTimeout(checkForUpdates, INITIAL_DELAY_MS);

    // Recurring check
    setInterval(checkForUpdates, CHECK_INTERVAL_MS);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }

  // Debug/Manual utilities for developers
  window.OmniUpdateChecker = {
    checkForUpdates,
    showUpdate,
    hideUpdate,
    openFlyout,
    closeFlyout,
    testMockUpdate(tag = "v0.1.1", notes = "This is a test release featuring automated updates and enhanced navigation.") {
      showUpdate({
        tag_name: tag,
        body: notes,
        html_url: `https://github.com/${REPO}/releases`,
      });
    },
  };
})();

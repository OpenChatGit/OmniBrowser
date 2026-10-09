(() => {
  const REPO = "OpenChatGit/OmniBrowser";
  const GITHUB_API_URL = `https://api.github.com/repos/${REPO}/releases/latest`;
  const CHECK_INTERVAL_MS = 4 * 60 * 60 * 1000; // 4 hours
  const INITIAL_DELAY_MS = 300; // immediate display for preview

  let currentAppVersion = "0.1.1";
  let activeReleaseData = null;
  let flyoutOpen = false;
  let devMode = false;

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

  // Compare numeric SemVer components; prerelease suffixes do not affect this updater.
  function isNewer(latestStr, currentStr) {
    const latest = parseSemVer(latestStr);
    const current = parseSemVer(currentStr);

    for (let i = 0; i < 3; i++) {
      if (latest[i] > current[i]) return true;
      if (latest[i] < current[i]) return false;
    }
    return false;
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
          devMode = Boolean(info.devMode);
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
    const btn = document.getElementById("browser-update-btn");

    if (!wrap || !btn) return;

    const versionTag = release.tag_name || "New Version";
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
    const btn = document.getElementById("browser-update-btn");
    if (!btn || !activeReleaseData || !window.OmniBridge || typeof OmniBridge.overlayShow !== "function") return;
    const rect = btn.getBoundingClientRect();
    const width = 320;
    const right = Math.max(width + 8, Math.min(Math.round(rect.right), window.innerWidth - 8));
    OmniBridge.overlayShow({
      anchorRight: right,
      anchorTop: Math.round(rect.bottom + 8),
      width,
      height: 0,
      payload: {
        view: "update",
        version: activeReleaseData.tag_name || "New Version",
        notes: formatReleaseNotes(activeReleaseData.body),
        releaseUrl: activeReleaseData.html_url || `https://github.com/${REPO}/releases`,
      },
    }).catch(() => {});
    flyoutOpen = true;
    btn.classList.add("is-open");
    btn.setAttribute("aria-expanded", "true");
  }

  function closeFlyout() {
    if (flyoutOpen && window.OmniBridge && typeof OmniBridge.overlayHide === "function") {
      OmniBridge.overlayHide().catch(() => {});
    }
    markFlyoutClosed();
  }

  function markFlyoutClosed() {
    const btn = document.getElementById("browser-update-btn");
    flyoutOpen = false;
    if (btn) {
      btn.classList.remove("is-open");
      btn.setAttribute("aria-expanded", "false");
    }
  }

  function toggleFlyout() {
    if (flyoutOpen) closeFlyout();
    else openFlyout();
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
        if (release && release.tag_name && isNewer(release.tag_name, current)) {
          showUpdate(release);
          return release;
        }
        hideUpdate();
        return null;
      }
    } catch (err) {
      // Quietly ignore network failures
    }

    // Keep the preview button available only in development builds.
    if (!devMode) {
      hideUpdate();
      return null;
    }
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

    if (btn) {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        toggleFlyout();
      });
    }

    if (window.OmniBridge && typeof OmniBridge.overlaySubscribe === "function") {
      OmniBridge.overlaySubscribe((msg, err) => {
        if (!err && msg && msg.type === "hide") markFlyoutClosed();
      }).catch(() => {});
    }

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

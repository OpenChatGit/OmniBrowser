#include "omni/scrollbar_inject.h"

namespace omni {

void InjectContentPageScripts(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }

  static const char* kScript = R"JS(
(function () {
  try {
    if (!document.getElementById('omni-scrollbar-style')) {
      var css =
        '*{scrollbar-width:thin;scrollbar-color:#454445 transparent;}' +
        '*::-webkit-scrollbar{width:7px;height:7px;}' +
        '*::-webkit-scrollbar-track{background:transparent;}' +
        '*::-webkit-scrollbar-thumb{background-color:#454445;border-radius:999px;' +
        'border:2px solid transparent;background-clip:padding-box;}' +
        '*::-webkit-scrollbar-thumb:hover{background-color:#5c5b5c;' +
        'border:2px solid transparent;background-clip:padding-box;}' +
        '*::-webkit-scrollbar-corner{background:transparent;}' +
        '*::-webkit-scrollbar-button{display:none;width:0;height:0;}';
      var s = document.createElement('style');
      s.id = 'omni-scrollbar-style';
      s.type = 'text/css';
      s.appendChild(document.createTextNode(css));
      var root = document.head || document.documentElement || document.body;
      if (root) root.appendChild(s);
    }
  } catch (e) {}

  try {
    if (window.__omniAudioProbe) return;
    window.__omniAudioProbe = true;
    var lastKey = '';
    var touched = new WeakSet();

    function markTouched(el) {
      try { touched.add(el); } catch (e) {}
    }

    function isUsable(el) {
      if (!el) return false;
      if (el.ended && el.currentTime > 0 && el.duration > 0 &&
          el.currentTime >= el.duration - 0.25) {
        return false;
      }
      return true;
    }

    function score(el) {
      if (!el || !isUsable(el)) return -1;
      var s = 0;
      if (!el.paused) s += 100;
      if (el.tagName === 'VIDEO') s += 20;
      if (!el.muted && el.volume > 0) s += 10;
      try { if (touched.has(el)) s += 5; } catch (e) {}
      if (el.duration && isFinite(el.duration)) s += 2;
      return s;
    }

    function pickMedia() {
      var nodes = document.querySelectorAll('audio,video');
      var best = null;
      var bestScore = -1;
      for (var i = 0; i < nodes.length; i++) {
        var el = nodes[i];
        var sc = score(el);
        if (sc > bestScore) {
          bestScore = sc;
          best = el;
        }
      }
      return best;
    }

    function mediaPlaying() {
      var nodes = document.querySelectorAll('audio,video');
      for (var i = 0; i < nodes.length; i++) {
        var el = nodes[i];
        if (el.paused) continue;
        if (el.muted || el.volume === 0) continue;
        return true;
      }
      return false;
    }

    function sessionMeta() {
      try {
        var ms = navigator.mediaSession;
        if (!ms || !ms.metadata) return null;
        var m = ms.metadata;
        var art = '';
        if (m.artwork && m.artwork.length) {
          art = String(m.artwork[0].src || '');
        }
        return {
          title: String(m.title || ''),
          artist: String(m.artist || ''),
          album: String(m.album || ''),
          artwork: art,
          playbackState: String(ms.playbackState || '')
        };
      } catch (e) {
        return null;
      }
    }

    function hostFromUrl(url) {
      try {
        return new URL(url, location.href).hostname.replace(/^www\./, '');
      } catch (e) {
        return location.hostname || '';
      }
    }

    function snapshot() {
      var el = pickMedia();
      var meta = sessionMeta();
      var playingAudible = mediaPlaying();
      var playing = false;
      var paused = true;
      var active = false;
      var currentTime = 0;
      var duration = 0;
      var title = '';
      var artist = '';
      var artwork = '';
      var canPip = false;
      var kind = '';

      if (el) {
        markTouched(el);
        playing = !el.paused && !el.ended;
        paused = el.paused;
        active = playing || (!el.ended && (el.currentTime > 0.2 ||
          (meta && meta.playbackState === 'paused')));
        currentTime = Number(el.currentTime) || 0;
        duration = Number(el.duration);
        if (!isFinite(duration)) duration = 0;
        kind = String(el.tagName || '').toLowerCase();
        try {
          canPip = kind === 'video' &&
            !!document.pictureInPictureEnabled &&
            typeof el.requestPictureInPicture === 'function';
        } catch (e) {}
        if (kind === 'video' && el.poster) {
          artwork = String(el.poster);
        }
      }

      if (meta) {
        if (meta.title) title = meta.title;
        if (meta.artist) artist = meta.artist;
        if (meta.artwork) artwork = meta.artwork;
        if (meta.playbackState === 'playing') {
          playing = true;
          paused = false;
          active = true;
        } else if (meta.playbackState === 'paused') {
          active = true;
          if (!el) {
            playing = false;
            paused = true;
          }
        }
      }

      if (!title) {
        title = String(document.title || '').trim();
      }
      if (!artist) {
        artist = hostFromUrl(location.href);
      }
      if (!active && playingAudible) {
        active = true;
        playing = true;
        paused = false;
      }

      return {
        playing: !!(playingAudible || (el && !el.paused && !el.ended) ||
          (meta && meta.playbackState === 'playing')),
        audible: playingAudible,
        paused: paused,
        active: active,
        title: title,
        artist: artist,
        artwork: artwork,
        currentTime: currentTime,
        duration: duration,
        origin: hostFromUrl(location.href),
        pageUrl: String(location.href || ''),
        kind: kind,
        canPip: canPip
      };
    }

    function report() {
      if (typeof window.cefQuery !== 'function') return;
      var state = snapshot();
      var key = [
        state.active ? '1' : '0',
        state.playing ? '1' : '0',
        state.audible ? '1' : '0',
        state.paused ? '1' : '0',
        Math.floor(state.currentTime * 2),
        Math.floor(state.duration),
        state.title,
        state.artist,
        state.artwork,
        state.origin,
        state.canPip ? '1' : '0'
      ].join('|');
      if (key === lastKey) return;
      lastKey = key;
      window.cefQuery({
        request: JSON.stringify({
          method: 'browser.media',
          params: state
        }),
        persistent: false,
        onSuccess: function () {},
        onFailure: function () {}
      });
      // Keep legacy audible signal for tab mute badges.
      window.cefQuery({
        request: JSON.stringify({
          method: 'browser.audio',
          params: { playing: !!state.audible }
        }),
        persistent: false,
        onSuccess: function () {},
        onFailure: function () {}
      });
    }

    window.__omniMediaControl = function (action, value) {
      var el = pickMedia();
      var act = String(action || '');
      var num = Number(value);
      if (!isFinite(num)) num = 0;
      try {
        if (act === 'toggle') {
          if (!el) return false;
          if (el.paused) el.play();
          else el.pause();
          report();
          return true;
        }
        if (act === 'play') {
          if (!el) return false;
          el.play();
          report();
          return true;
        }
        if (act === 'pause') {
          if (!el) return false;
          el.pause();
          report();
          return true;
        }
        if (act === 'seek' && el && isFinite(el.duration)) {
          el.currentTime = Math.max(0, Math.min(el.duration, num));
          report();
          return true;
        }
        if (act === 'seekRelative' && el) {
          var next = (Number(el.currentTime) || 0) + num;
          if (isFinite(el.duration) && el.duration > 0) {
            next = Math.max(0, Math.min(el.duration, next));
          } else {
            next = Math.max(0, next);
          }
          el.currentTime = next;
          report();
          return true;
        }
        if (act === 'seekStart' && el) {
          el.currentTime = 0;
          report();
          return true;
        }
        if (act === 'seekEnd' && el && isFinite(el.duration)) {
          el.currentTime = Math.max(0, el.duration - 0.05);
          report();
          return true;
        }
        if (act === 'pip' && el && el.tagName === 'VIDEO') {
          if (document.pictureInPictureElement === el) {
            document.exitPictureInPicture();
          } else if (document.pictureInPictureEnabled) {
            el.requestPictureInPicture();
          }
          report();
          return true;
        }
      } catch (e) {}
      return false;
    };

    document.addEventListener('play', function (ev) {
      markTouched(ev.target);
      report();
    }, true);
    document.addEventListener('playing', report, true);
    document.addEventListener('pause', report, true);
    document.addEventListener('ended', report, true);
    document.addEventListener('volumechange', report, true);
    document.addEventListener('timeupdate', report, true);
    document.addEventListener('loadedmetadata', report, true);
    document.addEventListener('enterpictureinpicture', report, true);
    document.addEventListener('leavepictureinpicture', report, true);
    setInterval(report, 1000);
    report();
  } catch (e) {}
})();
)JS";

  frame->ExecuteJavaScript(kScript, frame->GetURL(), 0);
}

}  // namespace omni

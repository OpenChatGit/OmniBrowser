#include "omni/scrollbar_inject.h"

#include <cstdio>
#include <sstream>

#include "omni/adblock_service.h"

namespace omni {
namespace {

std::string JsEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 16);
  for (char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '<':
        // Avoid breaking out of script tags in edge cases.
        out += "\\x3c";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

// Never append <style> to `document`, and never synthesize <html>.
// - document.appendChild(style) can make STYLE the documentElement (page dies).
// - createElement('html') + appendChild can block the real navigation document
//   from parsing (empty <html> with only our styles → white screen).
constexpr const char* kOmniStyleParentJs =
    "function omniStyleParent(){"
    "var de=document.documentElement;"
    "if(de&&de.tagName==='HTML')return document.head||de;"
    "if(document.head)return document.head;"
    "return null;"
    "}";

}  // namespace

bool IsFragileDomUrl(const std::string& url) {
  if (url.empty()) {
    return false;
  }
  auto has = [&](const char* host) {
    return url.find(host) != std::string::npos;
  };
  return has("wikipedia.org") || has("wikimedia.org") ||
         has("wikidata.org") || has("mediawiki.org") ||
         has("wiktionary.org") || has("wikisource.org") ||
         has("wikiquote.org") || has("wikivoyage.org") ||
         has("wikibooks.org") || has("wikinews.org") ||
         has("wikiversity.org");
}

void InjectContentPageScripts(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }
  const std::string url = frame->GetURL().ToString();
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    return;
  }
  // Wikipedia-class pages mutate constantly; scrollbar CSS + a 2.5s
  // querySelectorAll media probe is enough to CHECK the renderer.
  if (IsFragileDomUrl(url)) {
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
      var root = document.head ||
        (document.documentElement && document.documentElement.tagName === 'HTML'
          ? document.documentElement : null) ||
        document.body;
      if (root) root.appendChild(s);
    }
  } catch (e) {}

  try {
    if (window.__omniAudioProbe) return;
    window.__omniAudioProbe = true;
    var lastKey = '';

    function isUsable(el) {
      if (!el) return false;
      if (el.ended && el.currentTime > 0 && el.duration > 0 &&
          el.currentTime >= el.duration - 0.25) {
        return false;
      }
      return true;
    }

    function isYouTube() {
      try {
        var h = location.hostname || '';
        return h.indexOf('youtube.com') !== -1 || h.indexOf('youtu.be') !== -1 || h.indexOf('youtubekids.com') !== -1;
      } catch (e) {
        return false;
      }
    }

    function isYouTubeWatchPage() {
      if (!isYouTube()) return false;
      try {
        var p = location.pathname || '';
        var h = location.hostname || '';
        if (h.indexOf('music.youtube.com') !== -1) return true;
        return p.indexOf('/watch') === 0 || p.indexOf('/shorts/') === 0 || p.indexOf('/embed/') === 0 || p.indexOf('/live/') === 0;
      } catch (e) {
        return false;
      }
    }

    // YouTube/feed tiles play a muted clip on mouseenter/hover or autoplay in feeds.
    // That is NOT a media session and must NEVER show up in the player or toolbar.
    function isHoverPreview(el) {
      if (!el || el.tagName !== 'VIDEO') return false;

      // On YouTube:
      if (isYouTube()) {
        // If not on a dedicated watch page, ANY muted video is purely an inline/hover preview.
        if (!isYouTubeWatchPage()) {
          if (el.muted || el.volume === 0) {
            return true;
          }
        }
        // Check if this video element is inside any preview/recommendation tile
        try {
          if (el.closest(
            'ytd-video-preview,#video-preview,#video-preview-container,' +
            '#inline-preview-player,ytd-inline-playback-renderer,' +
            '.ytp-inline-preview-ui,ytd-moving-thumbnail-renderer,' +
            'ytd-thumbnail-overlay-loading-preview-renderer,' +
            'ytd-rich-item-renderer ytd-thumbnail,ytd-compact-video-renderer ytd-thumbnail,' +
            'ytd-grid-video-renderer ytd-thumbnail,ytd-video-renderer ytd-thumbnail,' +
            'ytd-reel-item-renderer,ytd-rich-grid-video-renderer ytd-thumbnail,' +
            'ytd-thumbnail,yt-inline-player,#inline-player,' +
            '[inline-preview],[is-inline-preview]'
          )) {
            return true;
          }
        } catch (e) {}
      } else {
        // Generic websites: if video is muted, check common feed preview selectors
        try {
          if (el.closest(
            '[data-testid*="preview"],[class*="preview"],[class*="hover-preview"],' +
            '[id*="preview"],[class*="thumbnail-video"],[class*="feed-video"]'
          )) {
            if (el.muted || el.volume === 0) {
              return true;
            }
          }
        } catch (e) {}
      }

      return false;
    }

    function isMainPlayer(el) {
      if (!el) return false;
      if (el.tagName === 'AUDIO') return true;

      if (isYouTube()) {
        if (!isYouTubeWatchPage()) {
          // On feed/home pages, only an explicitly unmuted and audible video is considered
          return !el.muted && el.volume > 0 && !isHoverPreview(el);
        }
        // On watch page, must be the main watch player
        try {
          if (el.closest('#movie_player,ytd-watch-flexy #ytd-player,ytd-watch-flexy,#player-container')) {
            return true;
          }
          if (el.classList && el.classList.contains('html5-main-video') && !isHoverPreview(el)) {
            return true;
          }
        } catch (e) {}
        return false;
      }

      // Non-YouTube
      try {
        if (el.classList && el.classList.contains('html5-main-video') && !isHoverPreview(el)) {
          return true;
        }
      } catch (e) {}
      return false;
    }

    function isSessionMedia(el) {
      if (!el || !isUsable(el) || isHoverPreview(el)) return false;
      if (el.tagName === 'AUDIO') return true;
      if (isMainPlayer(el)) return true;
      // Unmuted playback = the user is actually listening/watching.
      if (!el.muted && el.volume > 0) return true;
      return false;
    }

    function score(el) {
      if (!isSessionMedia(el)) return -1;
      var s = 0;
      if (!el.paused) s += 100;
      if (el.tagName === 'VIDEO') s += 20;
      if (!el.muted && el.volume > 0) s += 50;
      if (isMainPlayer(el)) s += 30;
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
        if (el.paused || el.ended) continue;
        if (isHoverPreview(el)) continue;
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
        // mediaSession alone is not enough: hover tiles can leave stale
        // metadata. Only promote a session when we have real media.
        if (el && meta.playbackState === 'playing') {
          playing = true;
          paused = false;
          active = true;
        } else if (el && meta.playbackState === 'paused') {
          active = true;
          playing = false;
          paused = true;
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
        playing: !!(playingAudible || (el && !el.paused && !el.ended)),
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

    // Poll only. YouTube preview tiles flood play/pause/timeupdate; wiring
    // those to cefQuery killed the browser process.
    var REPORT_MS = 2500;
    var reportTimer = null;

    function report(force) {
      if (reportTimer) return;
      reportTimer = setTimeout(function () {
        reportTimer = null;
        flushReport(!!force);
      }, force ? 60 : 0);
    }

    function flushReport(force) {
      if (typeof window.cefQuery !== 'function') return;
      var state = snapshot();
      var key = [
        state.active ? '1' : '0',
        state.playing ? '1' : '0',
        state.audible ? '1' : '0',
        state.paused ? '1' : '0',
        Math.floor(state.currentTime / 5),
        Math.floor(state.duration),
        state.title,
        state.artist,
        state.artwork,
        state.origin,
        state.canPip ? '1' : '0'
      ].join('|');
      if (!force && key === lastKey) return;
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
    }

    function playMedia(target) {
      if (target) {
        try {
          var p = target.play();
          if (p && typeof p.catch === 'function') p.catch(function () {});
        } catch (e) {}
      }
      try {
        var ytp = document.getElementById('movie_player') || document.querySelector('.html5-video-player');
        if (ytp && typeof ytp.playVideo === 'function') ytp.playVideo();
      } catch (e) {}
    }

    function pauseMedia(target) {
      if (target) {
        try { target.pause(); } catch (e) {}
      }
      try {
        var ytp = document.getElementById('movie_player') || document.querySelector('.html5-video-player');
        if (ytp && typeof ytp.pauseVideo === 'function') ytp.pauseVideo();
      } catch (e) {}
    }

    window.__omniMediaControl = function (action, value) {
      var el = pickMedia();
      var act = String(action || '');
      var num = Number(value);
      if (!isFinite(num)) num = 0;
      try {
        if (act === 'toggle') {
          if (el) {
            if (el.paused) playMedia(el);
            else pauseMedia(el);
          } else {
            var ytp = document.getElementById('movie_player') || document.querySelector('.html5-video-player');
            if (ytp && typeof ytp.getPlayerState === 'function') {
              if (ytp.getPlayerState() === 1) pauseMedia(null);
              else playMedia(null);
            }
          }
          setTimeout(function () { report(true); }, 80);
          return true;
        }
        if (act === 'play') {
          playMedia(el);
          setTimeout(function () { report(true); }, 80);
          return true;
        }
        if (act === 'pause') {
          pauseMedia(el);
          setTimeout(function () { report(true); }, 80);
          return true;
        }
        if (act === 'seek' && el && isFinite(el.duration)) {
          el.currentTime = Math.max(0, Math.min(el.duration, num));
          report(true);
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
          report(true);
          return true;
        }
        if (act === 'seekStart' && el) {
          el.currentTime = 0;
          report(true);
          return true;
        }
        if (act === 'seekEnd' && el && isFinite(el.duration)) {
          el.currentTime = Math.max(0, el.duration - 0.05);
          report(true);
          return true;
        }
        if (act === 'pip' && el && el.tagName === 'VIDEO') {
          if (document.pictureInPictureElement === el) {
            document.exitPictureInPicture();
          } else if (document.pictureInPictureEnabled) {
            el.requestPictureInPicture();
          }
          report(true);
          return true;
        }
      } catch (e) {}
      return false;
    };

    setTimeout(function () {
      report(false);
      setInterval(function () { report(false); }, REPORT_MS);
    }, REPORT_MS);
  } catch (e) {}
})();
)JS";

  frame->ExecuteJavaScript(kScript, frame->GetURL(), 0);
}

void InjectAdblockCosmeticCss(CefRefPtr<CefFrame> frame,
                              const std::string& hide_css) {
  if (!frame || !frame->IsValid() || hide_css.empty()) {
    return;
  }
  // Chunk large EasyList sheets so one giant JS string cannot fail silently.
  std::ostringstream js;
  js << "(function(){try{"
     << kOmniStyleParentJs
     << "var id='omni-adblock-css';"
     << "var parts=[";
  constexpr size_t kChunk = 12000;
  for (size_t i = 0; i < hide_css.size(); i += kChunk) {
    if (i) {
      js << ',';
    }
    js << '\'' << JsEscape(hide_css.substr(i, kChunk)) << '\'';
  }
  js << "];var css=parts.join('');"
     << "var key=String(css.length);"
     << "function apply(){"
     << "var parent=omniStyleParent();if(!parent)return false;"
     << "var s=document.getElementById(id);"
     << "if(!s){s=document.createElement('style');s.id=id;"
     << "s.type='text/css';parent.appendChild(s);}"
     << "if(s.dataset.omniCss!==key){s.dataset.omniCss=key;s.textContent=css;}"
     << "return true;}"
     << "if(apply())return;"
     << "var mo=new MutationObserver(function(){if(apply())mo.disconnect();});"
     << "mo.observe(document,{childList:true});"
     << "}catch(e){}})();";
  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
}

void InjectYoutubePlayerAdStrip(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }
  // Brave/uBO block in-player ads by pruning player JSON (scriptlets /
  // $replace). We do the same with a tiny YouTube-only hook — not the full
  // uBO scriptlet bundle that destabilized CEF.
  static const char* kScript = R"JS(
(function(){
  try {
    if (window.__omniYtAdStrip) return;
    window.__omniYtAdStrip = 1;
    function stripObj(o){
      if (!o || typeof o !== 'object') return o;
      try { delete o.adPlacements; } catch (e) {}
      try { delete o.playerAds; } catch (e) {}
      try { delete o.adSlots; } catch (e) {}
      return o;
    }
    try {
      if (window.ytInitialPlayerResponse) {
        stripObj(window.ytInitialPlayerResponse);
      }
    } catch (e) {}
    try {
      var cur = window.ytInitialPlayerResponse;
      Object.defineProperty(window, 'ytInitialPlayerResponse', {
        configurable: true,
        enumerable: true,
        get: function(){ return cur; },
        set: function(v){ cur = stripObj(v); }
      });
    } catch (e) {}
  } catch (e) {}
})();
)JS";
  frame->ExecuteJavaScript(kScript, frame->GetURL(), 0);

  std::ostringstream css_js;
  css_js << "(function(){try{"
         << kOmniStyleParentJs
         << "var id='omni-adblock-yt-css';"
         << "var css='ytd-ad-slot-renderer,ytd-promoted-sparkles-web-renderer,"
         << "ytd-in-feed-ad-layout-renderer,ytd-display-ad-renderer,#masthead-ad,"
         << "ytd-rich-item-renderer:has(> ytd-ad-slot-renderer),"
         << "ytd-rich-item-renderer:has(> #content > ytd-ad-slot-renderer),"
         << ".ytp-ad-overlay-container,.ytp-ad-image-overlay{"
         << "display:none!important;height:0!important;min-height:0!important;"
         << "margin:0!important;padding:0!important;overflow:hidden!important}';"
         << "function apply(){var p=omniStyleParent();if(!p)return false;"
         << "if(document.getElementById(id))return true;"
         << "var s=document.createElement('style');s.id=id;s.type='text/css';"
         << "s.textContent=css;p.appendChild(s);return true;}"
         << "if(apply())return;"
         << "var mo=new MutationObserver(function(){if(apply())mo.disconnect();});"
         << "mo.observe(document,{childList:true});"
         << "}catch(e){}})();";
  frame->ExecuteJavaScript(css_js.str(), frame->GetURL(), 0);
}

void InjectAdblockScriptletsBrave(CefRefPtr<CefFrame> frame,
                                  const std::string& injected_script) {
  if (!frame || !frame->IsValid() || injected_script.empty()) {
    return;
  }
  // Mirrors brave-core GetScriptletGlobalsScript + scriptlet body.
  // Brave injects via isolated-world bootstrap that creates a <script> tag;
  // YouTube's Trusted Types blocks that DOM path in CEF. ExecuteJavaScript
  // runs the same page-world payload without touching HTMLScriptElement.
  // JSON-escape the body and eval via Function so a single bad scriptlet
  // cannot create a parse error that aborts the whole wrapper.
  std::ostringstream quoted;
  quoted << '"';
  for (unsigned char c : injected_script) {
    switch (c) {
      case '\\':
        quoted << "\\\\";
        break;
      case '"':
        quoted << "\\\"";
        break;
      case '\n':
        quoted << "\\n";
        break;
      case '\r':
        quoted << "\\r";
        break;
      case '\t':
        quoted << "\\t";
        break;
      case '\b':
        quoted << "\\b";
        break;
      case '\f':
        quoted << "\\f";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          quoted << buf;
        } else {
          quoted << static_cast<char>(c);
        }
        break;
    }
  }
  quoted << '"';

  std::ostringstream js;
  js << "(function(){"
     << "if(window.__omniAdblockScriptlets)return;"
     << "window.__omniAdblockScriptlets=1;"
     << "try{"
     << "const scriptletGlobals=(()=>{"
     << "const forwardedMapMethods=['has','get','set'];"
     << "const handler={"
     << "get(target,prop){"
     << "if(forwardedMapMethods.includes(prop)){"
     << "return Map.prototype[prop].bind(target)}"
     << "return target.get(prop)},"
     << "set(target,prop,value){"
     << "if(!forwardedMapMethods.includes(prop)){target.set(prop,value)}"
     << "}"
     << "};"
     << "return new Proxy(new Map(),handler)"
     << "})();"
     << "let deAmpEnabled=false;"
     << "(new Function('scriptletGlobals','deAmpEnabled'," << quoted.str()
     << "))(scriptletGlobals,deAmpEnabled);"
     << "}catch(e){}"
     << "})();";
  frame->ExecuteJavaScript(js.str(), frame->GetURL(), 0);
}

void InjectAdblockGenericObserver(CefRefPtr<CefFrame> frame,
                                  const std::string& exceptions_json,
                                  bool generichide) {
  if (!frame || !frame->IsValid() || generichide) {
    return;
  }
  if (IsFragileDomUrl(frame->GetURL().ToString())) {
    return;
  }
  // Brave content_cosmetic bundle uses cf_worker.hiddenClassIdSelectors via
  // isolated world. CEF approximation: MutationObserver + cefQuery.
  const std::string exceptions =
      exceptions_json.empty() ? "[]" : exceptions_json;
  std::ostringstream gen;
  gen << "(function(){try{"
      << "if(window.__omniAdblockGeneric)return;"
      << "window.__omniAdblockGeneric=1;"
      << "var exceptions=" << exceptions << ";"
      << "if(typeof exceptions==='string'){try{exceptions=JSON.parse(exceptions)}"
      << "catch(e){exceptions=[]}}"
      << "var seenC=Object.create(null),seenI=Object.create(null);"
      << "var pendingC=[],pendingI=[],timer=null;"
      << kOmniStyleParentJs
      << "function applyCss(css){if(!css)return;var id='omni-adblock-generic-css';"
      << "var parent=omniStyleParent();if(!parent)return;"
      << "var s=document.getElementById(id);if(!s){s=document.createElement('style');"
      << "s.id=id;s.type='text/css';parent.appendChild(s);}"
      << "s.textContent=(s.textContent||'')+css;}"
      << "function flush(){timer=null;if(!pendingC.length&&!pendingI.length)return;"
      << "var classes=pendingC.splice(0,250),ids=pendingI.splice(0,250);"
      << "if(pendingC.length||pendingI.length)schedule();"
      << "if(typeof window.cefQuery!=='function')return;"
      << "window.cefQuery({request:JSON.stringify({method:'browser.adblock.classId',"
      << "params:{classes:classes,ids:ids,exceptions:exceptions}}),"
      << "onSuccess:function(r){try{var o=JSON.parse(r||'{}');applyCss(o.hideCss||'')}"
      << "catch(e){}},onFailure:function(){}})}"
      << "function schedule(){if(timer)return;timer=setTimeout(flush,120)}"
      << "function noteEl(el){if(!el||el.nodeType!==1)return;"
      << "if(el.id&&!seenI[el.id]){seenI[el.id]=1;pendingI.push(el.id)}"
      << "var cn=el.classList;if(!cn)return;for(var i=0;i<cn.length;i++){"
      << "var c=cn[i];if(c&&!seenC[c]){seenC[c]=1;pendingC.push(c)}}}"
      << "function scan(root){if(!root)return;noteEl(root);"
      << "var all=root.querySelectorAll?root.querySelectorAll('[id], [class]'):[];"
      << "for(var i=0;i<all.length;i++)noteEl(all[i]);schedule()}"
      << "function boot(){"
      << "scan(document.documentElement);"
      << "if(typeof MutationObserver==='undefined')return;"
      << "new MutationObserver(function(muts){for(var i=0;i<muts.length;i++){"
      << "var m=muts[i];if(m.type==='attributes'){noteEl(m.target);schedule();continue}"
      << "var nodes=m.addedNodes;for(var j=0;j<nodes.length;j++)scan(nodes[j])}})"
      << ".observe(document.documentElement,{childList:true,subtree:true,"
      << "attributes:true,attributeFilter:['class','id']});}"
      << "var idle=window.requestIdleCallback||function(cb){setTimeout(cb,400)};"
      << "idle(boot,{timeout:800});"
      << "}catch(e){}})();";
  frame->ExecuteJavaScript(gen.str(), frame->GetURL(), 0);
}

void InjectAdblockSlotCollapse(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }
  if (IsFragileDomUrl(frame->GetURL().ToString())) {
    return;
  }
  // Hide cancelled ad frames/images and collapse empty wrappers (YouTube
  // grid cards, adsbygoogle ins, reserved min-height slots). No cefQuery.
  static const char* kScript = R"JS(
(function(){
  try {
    if (window.__omniAdblockCollapse) return;
    window.__omniAdblockCollapse = 1;
    var AD_SRC = /doubleclick|googlesyndication|googleadservices|adservice\.google|pagead2\.|adnxs\.|adsrvr\.|amazon-adsystem|taboola\.|outbrain\.|criteo\.|rubiconproject|pubmatic\.|openx\.|casalemedia|2mdn\.|moatads|adsafeprotected|googletagservices|googleadservices/i;
    var YT_AD = /^(YTD-AD-SLOT-RENDERER|YTD-PROMOTED-SPARKLES-WEB-RENDERER|YTD-IN-FEED-AD-LAYOUT-RENDERER|YTD-DISPLAY-AD-RENDERER|YTD-PROMOTED-VIDEO-RENDERER|YTD-AD-SLOT-RENDERER)$/;
    var YT_CARD = /^(YTD-RICH-ITEM-RENDERER|YTD-REEL-VIDEO-RENDERER)$/;

    function hideBox(el){
      if (!el || el.nodeType !== 1 || el.__omniHide) return;
      el.__omniHide = 1;
      el.style.setProperty('display','none','important');
      el.style.setProperty('height','0','important');
      el.style.setProperty('min-height','0','important');
      el.style.setProperty('max-height','0','important');
      el.style.setProperty('margin','0','important');
      el.style.setProperty('padding','0','important');
      el.style.setProperty('overflow','hidden','important');
      el.style.setProperty('border','0','important');
      try { el.setAttribute('hidden',''); } catch (e) {}
    }

    function srcOf(el){
      return el.currentSrc || el.src || el.getAttribute('src') ||
             el.getAttribute('data-src') || '';
    }

    function isAdFrame(el){
      var tag = el.localName || '';
      if (tag !== 'iframe' && tag !== 'img' && tag !== 'video' && tag !== 'embed') {
        return false;
      }
      return AD_SRC.test(srcOf(el));
    }

    function isAdSlot(el){
      if (!el || el.nodeType !== 1) return false;
      if (YT_AD.test(el.tagName) || el.id === 'masthead-ad') return true;
      if ((el.localName || '') === 'ins' && el.classList &&
          el.classList.contains('adsbygoogle')) return true;
      var id = el.id || '';
      return id.indexOf('google_ads_') === 0 || id.indexOf('div-gpt-ad') === 0;
    }

    function collapseParents(el){
      var p = el.parentElement;
      var n = 0;
      while (p && n < 3 && p !== document.body && p !== document.documentElement) {
        if (YT_CARD.test(p.tagName) || isAdSlot(p)) {
          hideBox(p);
          p = p.parentElement;
          n++;
          continue;
        }
        break;
      }
    }

    function consider(el){
      if (!el || el.nodeType !== 1) return;
      if (isAdSlot(el) || isAdFrame(el)) {
        hideBox(el);
        collapseParents(el);
      }
    }

    function scan(root){
      if (!root) return;
      consider(root);
      if (!root.querySelectorAll) return;
      var nodes = root.querySelectorAll(
        'iframe,img,ins.adsbygoogle,#masthead-ad,ytd-ad-slot-renderer,' +
        'ytd-promoted-sparkles-web-renderer,ytd-in-feed-ad-layout-renderer,' +
        'ytd-display-ad-renderer,[id^="google_ads_"],[id^="div-gpt-ad"]');
      for (var i = 0; i < nodes.length; i++) consider(nodes[i]);
    }

    document.addEventListener('error', function(e){
      var t = e.target;
      if (!t || !t.tagName) return;
      var tag = t.tagName;
      if (tag !== 'IMG' && tag !== 'IFRAME' && tag !== 'VIDEO' && tag !== 'EMBED') {
        return;
      }
      if (isAdFrame(t) || isAdSlot(t) || isAdSlot(t.parentElement)) {
        hideBox(t);
        collapseParents(t);
      }
    }, true);

    var timer = null;
    var pending = [];
    function flush(){
      timer = null;
      var list = pending;
      pending = [];
      for (var i = 0; i < list.length; i++) scan(list[i]);
    }
    function schedule(node){
      pending.push(node);
      if (timer) return;
      timer = setTimeout(flush, 80);
    }

    function boot(){
      scan(document.documentElement);
      if (typeof MutationObserver === 'undefined') return;
      new MutationObserver(function(muts){
        for (var i = 0; i < muts.length; i++) {
          var m = muts[i];
          if (m.type === 'attributes') { consider(m.target); continue; }
          var nodes = m.addedNodes;
          for (var j = 0; j < nodes.length; j++) schedule(nodes[j]);
        }
      }).observe(document.documentElement || document, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: ['src']
      });
    }

    var idle = window.requestIdleCallback || function (cb) { setTimeout(cb, 400); };
    idle(boot, { timeout: 800 });
  } catch (e) {}
})();
)JS";
  frame->ExecuteJavaScript(kScript, frame->GetURL(), 0);
}

bool IsHttpContentUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

void InjectAdblockCosmetics(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }
  const std::string url = frame->GetURL().ToString();
  if (!IsHttpContentUrl(url)) {
    return;
  }
  if (IsFragileDomUrl(url)) {
    return;
  }
  const AdblockCosmeticDecision cosmetics =
      AdblockService::Get().CosmeticsForUrl(url);
  // Scriptlets must run before CSS: they set up fetch/XHR interceptors,
  // cookie-banner bypasses, and anti-anti-adblock patches. Mirrors the
  // brave-core GetScriptletGlobalsScript injection order (OnLoadStart).
  InjectAdblockScriptletsBrave(frame, cosmetics.injected_script);
  InjectAdblockCosmeticCss(frame, cosmetics.hide_css);
  if (url.find("youtube.com") != std::string::npos ||
      url.find("youtube-nocookie.com") != std::string::npos ||
      url.find("youtubekids.com") != std::string::npos ||
      url.find("youtu.be") != std::string::npos) {
    InjectYoutubePlayerAdStrip(frame);
  }
}

void InjectAdblockObservers(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid()) {
    return;
  }
  const std::string url = frame->GetURL().ToString();
  if (!IsHttpContentUrl(url) || IsFragileDomUrl(url)) {
    return;
  }
  const AdblockCosmeticDecision cosmetics =
      AdblockService::Get().CosmeticsForUrl(url);
  InjectAdblockGenericObserver(frame, cosmetics.exceptions_json,
                               cosmetics.generichide);
  InjectAdblockSlotCollapse(frame);

  // Inject CSS that hides blocked visual resources (images, iframes, video)
  // matched by their src-attribute. Uses a separate <style> element so it
  // does not overwrite the cosmetic CSS injected at OnLoadStart.
  const std::string collapse_css = AdblockService::Get().VisualCollapseCss();
  if (!collapse_css.empty()) {
    std::ostringstream vcss;
    vcss << "(function(){try{" << kOmniStyleParentJs
         << "var id='omni-adblock-collapse-css';"
         << "var css='" << JsEscape(collapse_css) << "';"
         << "var p=omniStyleParent();if(!p)return;"
         << "var s=document.getElementById(id);if(!s){"
         << "s=document.createElement('style');s.id=id;"
         << "s.type='text/css';p.appendChild(s);}"
         << "s.textContent=css;"
         << "}catch(e){}})();";
    frame->ExecuteJavaScript(vcss.str(), frame->GetURL(), 0);
  }
}

}  // namespace omni

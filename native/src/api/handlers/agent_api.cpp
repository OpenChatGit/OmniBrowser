#include "omni/agent_api.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_callback.h"
#include "omni/api/api_dispatcher.h"
#include "omni/mcp/mcp_server.h"
#include "omni/omni_handler.h"

namespace omni {
namespace {

std::atomic<int64_t> g_agent_query_seq{1000};
std::mutex g_pending_mu;
std::map<int64_t, std::shared_ptr<ApiResponder>> g_pending_queries;

// Helper to escape strings inside JavaScript literals
std::string JsStringEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 16);
  for (unsigned char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '"':
        out += "\\\"";
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
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

CefRefPtr<CefFrame> TargetFrame(const ApiContext& ctx,
                                const std::string& tab_id) {
  if (!ctx.owner) {
    return nullptr;
  }
  CefRefPtr<CefBrowserView> view = nullptr;
  if (!tab_id.empty()) {
    view = ctx.owner->ContentViewForTab(tab_id);
  }
  if (!view && !ctx.owner->ActiveContentTabId().empty()) {
    view = ctx.owner->ContentViewForTab(ctx.owner->ActiveContentTabId());
  }
  if (!view) {
    view = ctx.owner->content_browser_view();
  }
  if (!view) {
    return nullptr;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return nullptr;
  }
  auto frame = browser->GetMainFrame();
  if (!frame || !frame->IsValid()) {
    return nullptr;
  }
  return frame;
}

void ExecuteInFrameWithCallback(const ApiContext& ctx,
                                CefRefPtr<CefFrame> frame,
                                const std::string& code_body,
                                ApiResponder& responder) {
  const int64_t qid = ++g_agent_query_seq;

  std::shared_ptr<ApiResponder> sink;
  if (ctx.shared_responder) {
    sink = ctx.shared_responder;
  } else if (ctx.cef_callback) {
    sink = std::make_shared<CefApiResponder>(ctx.cef_callback);
  } else {
    responder.Failure(500, "No durable responder for async page script");
    return;
  }

  struct ResponderWrapper : public ApiResponder {
    std::mutex m;
    bool done = false;
    std::shared_ptr<ApiResponder> real;
    explicit ResponderWrapper(std::shared_ptr<ApiResponder> r)
        : real(std::move(r)) {}
    void Success(const std::string& json) override {
      std::lock_guard<std::mutex> lock(m);
      if (!done && real) {
        done = true;
        real->Success(json);
      }
    }
    void Failure(int code, const std::string& message) override {
      std::lock_guard<std::mutex> lock(m);
      if (!done && real) {
        done = true;
        real->Failure(code, message);
      }
    }
  };

  auto wrapper = std::make_shared<ResponderWrapper>(std::move(sink));

  {
    std::lock_guard<std::mutex> lock(g_pending_mu);
    g_pending_queries[qid] = wrapper;
  }

  // Setup 8-second timeout so stalled scripts don't hang responders forever
  std::thread([qid, wrapper]() {
    std::this_thread::sleep_for(std::chrono::seconds(8));
    std::shared_ptr<ApiResponder> target = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_pending_mu);
      auto it = g_pending_queries.find(qid);
      if (it != g_pending_queries.end()) {
        target = it->second;
        g_pending_queries.erase(it);
      }
    }
    if (target) {
      target->Failure(504, "Agent action timed out (content script did not respond)");
    }
  }).detach();

  std::ostringstream script;
  script << "(function() {\n"
         << "  var __qid = " << qid << ";\n"
         << "  function __reply(data, err) {\n"
         << "    if (typeof window.cefQuery === 'function') {\n"
         << "      window.cefQuery({\n"
         << "        request: JSON.stringify({\n"
         << "          method: 'agent.callback',\n"
         << "          params: { queryId: __qid, data: data, error: err ? String(err.message || err) : null }\n"
         << "        }),\n"
         << "        persistent: false,\n"
         << "        onSuccess: function() {},\n"
         << "        onFailure: function() {}\n"
         << "      });\n"
         << "    }\n"
         << "  }\n"
         << "  try {\n"
         << code_body << "\n"
         << "  } catch (e) {\n"
         << "    __reply(null, e);\n"
         << "  }\n"
         << "})();";

  frame->ExecuteJavaScript(script.str(), frame->GetURL(), 0);
}

// Keep extract cheap: no full-DOM clone (Wikipedia OOMs CEF), hard caps.
constexpr const char* kArticleExtractScript = R"JS(
  var CAP = 48000;
  function clip(s) {
    s = String(s || '');
    return s.length > CAP ? s.slice(0, CAP) + '\n…[truncated]' : s;
  }
  function skipTag(tag) {
    return tag === 'SCRIPT' || tag === 'STYLE' || tag === 'NOSCRIPT' ||
           tag === 'SVG' || tag === 'NAV' || tag === 'FOOTER' || tag === 'ASIDE';
  }
  function getCleanText(node) {
    if (!node) return '';
    return clip((node.innerText || node.textContent || '').trim());
  }
  function getMarkdown(node) {
    if (!node) return '';
    var lines = [];
    var budget = 400;
    function walk(el) {
      if (!el || budget <= 0 || lines.length > 800) return;
      if (el.nodeType === 3) {
        var t = el.nodeValue.replace(/\s+/g, ' ');
        if (t.trim()) { lines.push(t); budget--; }
        return;
      }
      if (el.nodeType !== 1 || skipTag(el.tagName)) return;
      var tag = el.tagName;
      if (/^H[1-6]$/.test(tag)) {
        lines.push('\n\n' + '#'.repeat(tag.charAt(1)) + ' ' + (el.innerText || '').trim() + '\n\n');
        budget--;
        return;
      }
      if (tag === 'P') {
        lines.push('\n\n' + (el.innerText || '').trim() + '\n\n');
        budget--;
        return;
      }
      if (tag === 'LI') {
        lines.push('\n- ' + (el.innerText || '').trim());
        budget--;
        return;
      }
      var kids = el.childNodes;
      for (var i = 0; i < kids.length; i++) walk(kids[i]);
    }
    walk(node);
    return clip(lines.join('').replace(/\n{3,}/g, '\n\n').trim());
  }
  var mainEl = document.querySelector('#mw-content-text, article, [role="main"], main, .post-content, .article-content, .entry-content') || document.body;
  var metaDesc = '';
  var metaAuthor = '';
  var ogTitle = '';
  try {
    var md = document.querySelector('meta[name="description"], meta[property="og:description"]');
    if (md) metaDesc = md.getAttribute('content') || '';
    var au = document.querySelector('meta[name="author"]');
    if (au) metaAuthor = au.getAttribute('content') || '';
    var ot = document.querySelector('meta[property="og:title"]');
    if (ot) ogTitle = ot.getAttribute('content') || '';
  } catch (e) {}
  var headingEls = mainEl ? mainEl.querySelectorAll('h1, h2, h3') : [];
  var headings = [];
  for (var h = 0; h < headingEls.length && headings.length < 40; h++) {
    var txt = (headingEls[h].innerText || '').trim();
    if (txt) headings.push({ level: parseInt(headingEls[h].tagName[1], 10), text: txt.slice(0, 200) });
  }
  var text = getCleanText(mainEl);
  var markdown = getMarkdown(mainEl);
  var words = text ? text.split(/\s+/).length : 0;
  __reply({
    url: location.href,
    title: document.title || ogTitle || '',
    byline: metaAuthor,
    excerpt: metaDesc,
    textContent: text,
    markdown: markdown,
    headings: headings,
    stats: {
      wordCount: words,
      charCount: text.length,
      readingTimeMin: Math.max(1, Math.round(words / 200)),
      truncated: text.indexOf('[truncated]') >= 0
    }
  });
)JS";

}  // namespace

void FailPendingAgentQueries(const std::string& reason) {
  std::map<int64_t, std::shared_ptr<ApiResponder>> pending;
  {
    std::lock_guard<std::mutex> lock(g_pending_mu);
    pending.swap(g_pending_queries);
  }
  for (auto& entry : pending) {
    if (entry.second) {
      entry.second->Failure(500, reason);
    }
  }
}

void RegisterAgentApis() {
  auto& api = ApiDispatcher::Get();

  // Internal reply router for async frame scripts
  api.Register(
      "agent.callback",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const int64_t qid = params.value("queryId", static_cast<int64_t>(0));
        std::shared_ptr<ApiResponder> target = nullptr;
        {
          std::lock_guard<std::mutex> lock(g_pending_mu);
          auto it = g_pending_queries.find(qid);
          if (it != g_pending_queries.end()) {
            target = it->second;
            g_pending_queries.erase(it);
          }
        }
        if (target) {
          if (params.contains("error") && !params["error"].is_null()) {
            target->Failure(500, params["error"].get<std::string>());
          } else {
            Json data = params.value("data", Json::object());
            if (data.is_object() && data.value("agentPointer", false)) {
              const float x = data.value("x", 0.0f);
              const float y = data.value("y", 0.0f);
              const std::string action = data.value("action", "click");
              const std::string selector = data.value("selector", "");
              const std::string value = data.value("value", "");
              if (auto* handler = OmniHandler::GetInstance()) {
                handler->MoveAgentPointer(x, y, true);
              }
              CefPostDelayedTask(
                  TID_UI,
                  base::BindOnce(
                      [](std::shared_ptr<ApiResponder> reply, float px,
                         float py, std::string act, std::string sel,
                         std::string val) {
                        auto* handler = OmniHandler::GetInstance();
                        if (handler && act == "click") {
                          handler->SendAgentMouseClick(static_cast<int>(px),
                                                       static_cast<int>(py));
                        }
                        if (handler && !sel.empty()) {
                          CefRefPtr<CefFrame> frame;
                          if (auto view = handler->content_browser_view()) {
                            if (auto browser = view->GetBrowser()) {
                              frame = browser->GetMainFrame();
                            }
                          }
                          if (frame && frame->IsValid()) {
                            if (act == "click") {
                              frame->ExecuteJavaScript(
                                  "var el=document.querySelector('" +
                                      JsStringEscape(sel) +
                                      "'); if(el) el.click();",
                                  frame->GetURL(), 0);
                            } else if (act == "fill") {
                              std::string body =
                                  "var el=document.querySelector('" +
                                  JsStringEscape(sel) +
                                  "'); if(!el) return;"
                                  "el.focus();"
                                  "var proto=el.tagName==='TEXTAREA'?"
                                  "HTMLTextAreaElement.prototype:"
                                  "HTMLInputElement.prototype;"
                                  "var desc=Object.getOwnPropertyDescriptor("
                                  "proto,'value');"
                                  "var val='" +
                                  JsStringEscape(val) +
                                  "';"
                                  "if(desc&&desc.set) desc.set.call(el,val);"
                                  "else el.value=val;"
                                  "el.dispatchEvent(new Event('input',"
                                  "{bubbles:true}));"
                                  "el.dispatchEvent(new Event('change',"
                                  "{bubbles:true}));";
                              frame->ExecuteJavaScript(body, frame->GetURL(),
                                                       0);
                            }
                          }
                        }
                        if (reply) {
                          reply->Success(
                              Json{{"ok", true},
                                   {"result",
                                    {{"clicked", act == "click"},
                                     {"filled", act == "fill"},
                                     {"x", px},
                                     {"y", py}}}}
                                  .dump());
                        }
                      },
                      target, x, y, action, selector, value),
                  280);
            } else {
              target->Success(
                  Json{{"ok", true}, {"result", data}}.dump());
            }
          }
        }
        responder.Success(Json{{"ok", true}}.dump());
      },
      ApiExposure::UiOnly);

  // Extract structured clean text & markdown from content page (for summarization/LLMs)
  api.Register(
      "page.extract",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        const std::string tab_id = params.value("tabId", "");
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        ExecuteInFrameWithCallback(ctx, frame, kArticleExtractScript, responder);
      },
      ApiExposure::UiOnly);

  // Evaluate JS in target content frame and return result
  api.Register(
      "page.eval",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        const std::string tab_id = params.value("tabId", "");
        const std::string expression = params.value("expression", "");
        if (expression.empty()) {
          responder.Failure(400, "expression parameter required");
          return;
        }
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        std::string body =
            "var res = (0, eval)('" + JsStringEscape(expression) + "');\n"
            "try { JSON.parse(JSON.stringify(res)); } catch (ser) { res = String(res); }\n"
            "if (typeof res === 'string' && res.length > 80000) res = res.slice(0, 80000) + '\\n…[truncated]';\n"
            "__reply(res);\n";
        ExecuteInFrameWithCallback(ctx, frame, body, responder);
      },
      ApiExposure::UiOnly);

  // Click an element by selector
  api.Register(
      "page.click",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        const std::string tab_id = params.value("tabId", "");
        const std::string selector = params.value("selector", "");
        if (selector.empty()) {
          responder.Failure(400, "selector required");
          return;
        }
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        std::string body =
            "var el = document.querySelector('" + JsStringEscape(selector) + "');\n"
            "if (!el) { __reply({ clicked: false, error: 'selector not found' }); return; }\n"
            "el.scrollIntoView({ behavior: 'instant', block: 'center' });\n"
            "var r = el.getBoundingClientRect();\n"
            "__reply({ agentPointer: true, action: 'click', selector: '" +
            JsStringEscape(selector) +
            "', x: r.left + r.width / 2, y: r.top + r.height / 2 });\n";
        ExecuteInFrameWithCallback(ctx, frame, body, responder);
      },
      ApiExposure::UiOnly);

  // Fill an input / textarea by selector
  api.Register(
      "page.fill",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        const std::string tab_id = params.value("tabId", "");
        const std::string selector = params.value("selector", "");
        const std::string value = params.value("value", "");
        if (selector.empty()) {
          responder.Failure(400, "selector required");
          return;
        }
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        std::string body =
            "var el = document.querySelector('" + JsStringEscape(selector) + "');\n"
            "if (!el) { __reply({ filled: false, error: 'selector not found' }); return; }\n"
            "el.scrollIntoView({ behavior: 'instant', block: 'center' });\n"
            "var r = el.getBoundingClientRect();\n"
            "__reply({ agentPointer: true, action: 'fill', selector: '" +
            JsStringEscape(selector) + "', value: '" + JsStringEscape(value) +
            "', x: r.left + r.width / 2, y: r.top + r.height / 2 });\n";
        ExecuteInFrameWithCallback(ctx, frame, body, responder);
      },
      ApiExposure::UiOnly);

  // Scroll content page
  api.Register(
      "page.scroll",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        const std::string tab_id = params.value("tabId", "");
        const std::string pos = params.value("position", "bottom");
        const int y = params.value("y", 0);
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        std::string script;
        if (pos == "top") {
          script = "window.scrollTo({ top: 0, behavior: 'smooth' });";
        } else if (pos == "bottom") {
          script = "window.scrollTo({ top: document.body ? document.body.scrollHeight : 99999, behavior: 'smooth' });";
        } else {
          script = "window.scrollBy({ top: " + std::to_string(y) + ", behavior: 'smooth' });";
        }
        frame->ExecuteJavaScript(script, frame->GetURL(), 0);
        responder.Success(Json{{"ok", true}, {"result", {{"scrolled", true}}}}.dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "agent.pause",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        (void)params;
        McpServer::Get().PauseAgents();
        responder.Success(Json{{"ok", true}, {"paused", true}}.dump());
      },
      ApiExposure::UiOnly);
}

}  // namespace omni

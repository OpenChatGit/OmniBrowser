#include "omni/agent_api.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "include/views/cef_browser_view.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_callback.h"
#include "omni/api/api_dispatcher.h"
#include "omni/devtools_client.h"
#include "omni/mcp/mcp_server.h"
#include "omni/omni_handler.h"
#include "omni/utf8.h"

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

std::string CdpErrorMessage(const Json& err) {
  if (err.is_string()) {
    return err.get<std::string>();
  }
  if (err.is_object()) {
    if (err.contains("message") && err["message"].is_string()) {
      return err["message"].get<std::string>();
    }
    return err.dump();
  }
  return "DevTools error";
}

std::string EvalExceptionMessage(const Json& details) {
  if (details.contains("exception") && details["exception"].is_object()) {
    const auto& ex = details["exception"];
    if (ex.contains("description") && ex["description"].is_string()) {
      return ex["description"].get<std::string>();
    }
    if (ex.contains("value")) {
      return ex["value"].is_string() ? ex["value"].get<std::string>()
                                     : ex["value"].dump();
    }
  }
  return details.value("text", "JavaScript exception");
}

Json UnwrapRemoteObject(const Json& remote) {
  if (remote.contains("value")) {
    return remote["value"];
  }
  const std::string type = remote.value("type", "");
  if (type == "undefined" || type == "null") {
    return nullptr;
  }
  if (remote.contains("unserializableValue")) {
    return remote["unserializableValue"];
  }
  if (remote.contains("description")) {
    return remote["description"];
  }
  return remote;
}

void TruncateEvalResult(Json& value) {
  constexpr size_t kMax = 80000;
  if (value.is_string()) {
    auto s = value.get<std::string>();
    if (s.size() > kMax) {
      value = s.substr(0, kMax) + "\n…[truncated]";
    }
    return;
  }
  const std::string dumped = value.dump();
  if (dumped.size() > kMax) {
    value = dumped.substr(0, kMax) + "\n…[truncated]";
  }
}

std::vector<std::string> ParseFilePaths(const Json& params, std::string* error) {
  Json files = Json();
  if (params.contains("files")) {
    files = params["files"];
  } else if (params.contains("file")) {
    files = params["file"];
  }
  std::vector<std::string> out;
  if (files.is_string()) {
    out.push_back(files.get<std::string>());
  } else if (files.is_array()) {
    for (const auto& item : files) {
      if (item.is_string()) {
        out.push_back(item.get<std::string>());
      }
    }
  }
  if (out.empty()) {
    *error = "files parameter required (absolute path or array of paths)";
    return {};
  }
  for (const auto& path : out) {
    std::error_code ec;
    const std::filesystem::path native(utf8::Widen(path));
    if (!native.is_absolute()) {
      *error = "File path must be absolute: " + path;
      return {};
    }
    if (!std::filesystem::is_regular_file(native, ec)) {
      *error = "File not found: " + path;
      return {};
    }
  }
  return out;
}

std::string FindFileInputExpression(const std::string& selector) {
  return "(function(){\n"
         "  var sel = '" +
         JsStringEscape(selector) +
         "';\n"
         "  function find(root) {\n"
         "    try {\n"
         "      if (sel) {\n"
         "        var el = root.querySelector(sel);\n"
         "        if (el) return el;\n"
         "      } else {\n"
         "        var all = root.querySelectorAll('input[type=\"file\"]');\n"
         "        if (all.length) return all[all.length - 1];\n"
         "      }\n"
         "    } catch (e) {}\n"
         "    var nodes = root.querySelectorAll('*');\n"
         "    for (var i = 0; i < nodes.length; i++) {\n"
         "      if (nodes[i].shadowRoot) {\n"
         "        var hit = find(nodes[i].shadowRoot);\n"
         "        if (hit) return hit;\n"
         "      }\n"
         "    }\n"
         "    return null;\n"
         "  }\n"
         "  return find(document);\n"
         "})()";
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

std::shared_ptr<ResponderWrapper> MakeDurableResponder(
    const ApiContext& ctx,
    ApiResponder& responder,
    int64_t* qid_out = nullptr) {
  std::shared_ptr<ApiResponder> sink;
  if (ctx.shared_responder) {
    sink = ctx.shared_responder;
  } else if (ctx.cef_callback) {
    sink = std::make_shared<CefApiResponder>(ctx.cef_callback);
  } else {
    responder.Failure(500, "No durable responder for async page script");
    return nullptr;
  }
  auto wrapper = std::make_shared<ResponderWrapper>(std::move(sink));
  const int64_t qid = ++g_agent_query_seq;
  if (qid_out) {
    *qid_out = qid;
  }
  {
    std::lock_guard<std::mutex> lock(g_pending_mu);
    g_pending_queries[qid] = wrapper;
  }
  std::thread([qid]() {
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
  return wrapper;
}

void SetFileInputFiles(CefRefPtr<CefBrowser> browser,
                       const Json& files,
                       const Json& target,
                       std::shared_ptr<ResponderWrapper> wrapper) {
  Json params = {{"files", files}};
  if (target.contains("backendNodeId")) {
    params["backendNodeId"] = target["backendNodeId"];
  } else if (target.contains("objectId")) {
    params["objectId"] = target["objectId"];
  } else if (target.contains("nodeId")) {
    params["nodeId"] = target["nodeId"];
  } else {
    wrapper->Failure(400, "No file input target");
    return;
  }
  DevToolsClient::Get().Call(
      browser, "DOM.setFileInputFiles", params,
      [wrapper, files](bool ok, Json payload) {
        if (!ok) {
          wrapper->Failure(500, CdpErrorMessage(payload));
          return;
        }
        wrapper->Success(
            Json{{"ok", true}, {"result", {{"uploaded", true}, {"files", files}}}}
                .dump());
      });
}

void ExecuteInFrameWithCallback(const ApiContext& ctx,
                                CefRefPtr<CefFrame> frame,
                                const std::string& code_body,
                                ApiResponder& responder) {
  int64_t qid = 0;
  if (!MakeDurableResponder(ctx, responder, &qid)) {
    return;
  }

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
  DevToolsClient::Get().FailAll(reason);
}

void DetachAgentDevTools(int browser_id) {
  DevToolsClient::Get().Detach(browser_id);
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

  // Evaluate JS via CDP Runtime.evaluate so page CSP cannot block it.
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
        auto browser = frame->GetBrowser();
        if (!browser) {
          responder.Failure(404, "No active content page found");
          return;
        }
        auto wrapper = MakeDurableResponder(ctx, responder);
        if (!wrapper) {
          return;
        }
        Json eval_params = {{"expression", expression},
                            {"returnByValue", true},
                            {"awaitPromise", true},
                            {"userGesture", true},
                            {"includeCommandLineAPI", true}};
        DevToolsClient::Get().Call(
            browser, "Runtime.evaluate", eval_params,
            [wrapper](bool ok, Json payload) {
              if (!ok) {
                wrapper->Failure(500, CdpErrorMessage(payload));
                return;
              }
              if (payload.contains("exceptionDetails")) {
                wrapper->Failure(
                    500, EvalExceptionMessage(payload["exceptionDetails"]));
                return;
              }
              Json value =
                  UnwrapRemoteObject(payload.value("result", Json::object()));
              TruncateEvalResult(value);
              wrapper->Success(Json{{"ok", true}, {"result", value}}.dump());
            });
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
        ApiContext ctx_copy = ctx;
        CefRefPtr<CefFrame> frame_ref = frame;
        auto browser = frame->GetBrowser();
        if (browser) {
          DevToolsClient::Get().Call(
              browser, "Page.setInterceptFileChooserDialog",
              Json{{"enabled", true}},
              [ctx_copy, frame_ref, body](bool, Json) {
                struct DiscardResponder : ApiResponder {
                  void Success(const std::string&) override {}
                  void Failure(int, const std::string&) override {}
                } discard;
                ExecuteInFrameWithCallback(ctx_copy, frame_ref, body, discard);
              });
        } else {
          ExecuteInFrameWithCallback(ctx, frame, body, responder);
        }
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
      "page.setFiles",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        std::string path_error;
        auto files = ParseFilePaths(params, &path_error);
        if (files.empty()) {
          responder.Failure(400, path_error);
          return;
        }
        const std::string tab_id = params.value("tabId", "");
        const std::string selector = params.value("selector", "");
        auto frame = TargetFrame(ctx, tab_id);
        if (!frame) {
          responder.Failure(404, "No active content page found");
          return;
        }
        auto browser = frame->GetBrowser();
        if (!browser) {
          responder.Failure(404, "No active content page found");
          return;
        }
        auto wrapper = MakeDurableResponder(ctx, responder);
        if (!wrapper) {
          return;
        }
        Json files_json = files;
        Json chooser;
        if (selector.empty()) {
          chooser = DevToolsClient::Get().TakeFileChooser(browser->GetIdentifier());
        }
        if (chooser.is_object() && chooser.contains("backendNodeId")) {
          SetFileInputFiles(browser, files_json, chooser, wrapper);
          return;
        }
        Json eval_params = {{"expression", FindFileInputExpression(selector)},
                            {"returnByValue", false},
                            {"userGesture", true}};
        DevToolsClient::Get().Call(
            browser, "Runtime.evaluate", eval_params,
            [browser, files_json, wrapper, selector](bool ok, Json payload) {
              if (!ok) {
                wrapper->Failure(500, CdpErrorMessage(payload));
                return;
              }
              if (payload.contains("exceptionDetails")) {
                wrapper->Failure(
                    500, EvalExceptionMessage(payload["exceptionDetails"]));
                return;
              }
              Json remote = payload.value("result", Json::object());
              if (!remote.contains("objectId") ||
                  remote.value("subtype", "") == "null") {
                wrapper->Failure(404,
                                 selector.empty()
                                     ? "No file input found. Click the upload "
                                       "control first, or pass a CSS selector."
                                     : "File input not found: " + selector);
                return;
              }
              SetFileInputFiles(browser, files_json,
                                Json{{"objectId", remote["objectId"]}},
                                wrapper);
            });
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

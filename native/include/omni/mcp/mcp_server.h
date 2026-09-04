#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "omni/library_json.h"

namespace omni {

class OmniHandler;

struct McpToolDefinition {
  std::string name;
  std::string description;
  Json input_schema;
};

struct McpResourceDefinition {
  std::string uri;
  std::string name;
  std::string description;
  std::string mime_type;
};

struct McpPromptDefinition {
  std::string name;
  std::string description;
  std::vector<Json> arguments;
};

class McpServer {
 public:
  static McpServer& Get();

  void Initialize();
  void Shutdown();

  // Transports
  bool StartHttpServer(int port = 8999);
  void StopHttpServer();
  void StartStdioServer();
  void StopStdioServer();

  // Lightweight MCP stdio host for Cursor / Claude. Does not start CEF.
  // Speaks MCP 2024-11-05 on stdin/stdout. Attaches to an already-open GUI
  // when one exists; otherwise launches it. Forwards failures as isError.
  static bool IsHttpReachable(int port, int timeout_ms = 250);
  static int RunStdioHost(int port);
  static int RunStdioProxy(int port);

#if defined(_WIN32)
  // Named mutex held by the GUI process so --mcp agents can see it
  // even before 127.0.0.1:8999 is listening. A second GUI must never
  // call CefInitialize with the same profile (Chromium CHECK / 0xC0000409).
  static constexpr wchar_t kGuiMutexName[] = L"Local\\OmniBrowser.GuiRunning";
  static bool FocusExistingGuiWindow();
  static void TerminateStaleGuiProcesses();
#endif

  // Core JSON-RPC 2.0 Dispatcher
  std::string ProcessJsonRpc(const std::string& request_raw,
                             const std::string& agent_id = {});
  Json HandleJsonRpcRequest(const Json& req, const std::string& agent_id = {});

  // Tools, Resources, Prompts Registry
  Json GetToolsList() const;
  Json GetResourcesList() const;
  Json GetPromptsList() const;

  // Execution
  Json ExecuteTool(const std::string& name, const Json& args);
  Json ReadResource(const std::string& uri);
  Json GetPrompt(const std::string& name, const Json& args);

  bool IsHttpRunning() const { return http_running_; }
  bool IsShuttingDown() const { return shutting_down_; }
  int GetHttpPort() const { return http_port_; }
  void PauseAgents();

 private:
  McpServer();
  ~McpServer();

  void RegisterTools();
  void RegisterResources();
  void RegisterPrompts();

  // Tool Handlers
  Json ToolNavigate(const Json& args);
  Json ToolListTabs(const Json& args);
  Json ToolCreateTab(const Json& args);
  Json ToolActivateTab(const Json& args);
  Json ToolCloseTab(const Json& args);
  Json ToolReloadTab(const Json& args);
  Json ToolGoBack(const Json& args);
  Json ToolGoForward(const Json& args);
  Json ToolExtractContent(const Json& args);
  Json ToolGetHtml(const Json& args);
  Json ToolEvalJs(const Json& args);
  Json ToolClick(const Json& args);
  Json ToolFill(const Json& args);
  Json ToolUploadFile(const Json& args);
  Json ToolScroll(const Json& args);
  Json ToolGetHistory(const Json& args);
  Json ToolGetBookmarks(const Json& args);
  Json ToolWaitForLoad(const Json& args);
  Json ToolStatus(const Json& args);

  void TouchAgentSession(const std::string& agent_id, const std::string& name);
  bool WaitForBrowserReady(std::chrono::milliseconds timeout);
  bool WaitForTabLoad(const std::string& tab_id,
                      const std::string& expected_url,
                      std::chrono::milliseconds timeout);

  std::unordered_map<std::string, McpToolDefinition> tools_;
  std::unordered_map<std::string, McpResourceDefinition> resources_;
  std::unordered_map<std::string, McpPromptDefinition> prompts_;

  std::atomic<bool> initialized_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<bool> http_running_{false};
  std::atomic<bool> stdio_running_{false};
  int http_port_ = 8999;

  std::unique_ptr<std::thread> http_thread_;
  std::unique_ptr<std::thread> stdio_thread_;
  uintptr_t listen_socket_ = ~0ULL; // INVALID_SOCKET
};

}  // namespace omni

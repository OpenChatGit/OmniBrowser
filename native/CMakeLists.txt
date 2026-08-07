set(OMNI_TARGET OmniBrowser)

# Bake the repo UI path into the binary for live reload during development.
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/ui" OMNI_UI_SOURCE_DIR)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/omni/build_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/omni/build_config.h"
  @ONLY
)

add_library(omni_sqlite STATIC "${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
target_include_directories(omni_sqlite PUBLIC "${CMAKE_SOURCE_DIR}/third_party/sqlite")
target_compile_definitions(omni_sqlite PRIVATE SQLITE_THREADSAFE=1)
if(MSVC)
  target_compile_options(omni_sqlite PRIVATE /W0)
  target_compile_options(omni_sqlite PRIVATE
    $<$<CONFIG:Debug>:/MTd>
    $<$<CONFIG:Release>:/MT>
    $<$<CONFIG:>:/MT>
  )
endif()

set(OMNI_SRCS
  src/main_win.cpp
  src/app/omni_app.cpp
  src/app/omni_handler.cpp
  src/app/app_menu.cpp
  src/app/scrollbar_inject.cpp
  src/app/window_delegate.cpp
  src/ipc/library_ipc.cpp
  src/ipc/library_commands.cpp
  src/ipc/library_json.cpp
  src/ipc/window_commands.cpp
  src/ipc/browser_commands.cpp
  src/ipc/terminal_commands.cpp
  src/ipc/git_commands.cpp
  src/terminal/terminal_manager.cpp

  src/library/library_repository.cpp
  src/library/history_store.cpp
  src/library/bookmark_store.cpp
  src/library/download_store.cpp
  src/launch/game_launcher.cpp
  src/platform/paths.cpp
  src/platform/file_dialog.cpp
  src/platform/utf8.cpp
  src/dev/dev_mode.cpp
  src/dev/ui_hot_reload.cpp
)

ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

add_executable(${OMNI_TARGET} WIN32 ${OMNI_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(${OMNI_TARGET})
add_dependencies(${OMNI_TARGET} libcef_dll_wrapper)

target_include_directories(${OMNI_TARGET} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CEF_INCLUDE_PATH}"
)

target_compile_definitions(${OMNI_TARGET} PRIVATE
  UNICODE
  _UNICODE
  JSON_NOEXCEPTION=1
)

target_link_libraries(${OMNI_TARGET}
  libcef_lib
  libcef_dll_wrapper
  omni_sqlite
  ${CEF_STANDARD_LIBS}
  comdlg32
)

ADD_WINDOWS_MANIFEST("${CMAKE_CURRENT_SOURCE_DIR}/win" "${OMNI_TARGET}" "exe")

COPY_FILES("${OMNI_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${OMNI_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Bundled UI for machines/CI without the source tree path.
add_custom_command(TARGET ${OMNI_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/ui"
          "${CEF_TARGET_OUT_DIR}/ui"
  COMMENT "Copying bundled UI assets"
)

set_target_properties(${OMNI_TARGET} PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CEF_TARGET_OUT_DIR}"
)

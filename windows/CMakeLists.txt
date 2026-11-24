cmake_minimum_required(VERSION 3.21)
project(repo_portable_windows VERSION 0.4.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Source files are UTF-8 on GitHub. MSVC otherwise interprets Korean
  # L"..." strings using the runner's legacy Windows code page.
  # /utf-8 sets both the input source and execution encodings explicitly.
  add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# repo-core: the Rust engine shared with the macOS app.
#
# The static library is produced by cargo into the workspace target directory.
# When it is missing it is built here, so a plain `cmake --build` bootstraps the
# engine as long as a Rust toolchain is on PATH.
# ---------------------------------------------------------------------------
if(MINGW)
  set(_repo_core_default_target "x86_64-pc-windows-gnu")
  set(_repo_core_default_name "librepo_core.a")
else()
  set(_repo_core_default_target "x86_64-pc-windows-msvc")
  set(_repo_core_default_name "repo_core.lib")
endif()

set(REPO_CORE_TARGET "${_repo_core_default_target}" CACHE STRING "Rust target triple used to build repo-core")
set(REPO_CORE_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/../target/${REPO_CORE_TARGET}/release/${_repo_core_default_name}"
    CACHE FILEPATH "Path to the repo-core static library")

if(NOT EXISTS "${REPO_CORE_LIB}")
  find_program(CARGO_EXECUTABLE cargo)
  if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR
      "repo-core static library not found at ${REPO_CORE_LIB} and cargo is not on PATH.\n"
      "Install Rust (https://rustup.rs), then run:\n"
      "  cargo build --release -p repo-core --target ${REPO_CORE_TARGET}")
  endif()
  add_custom_command(
    OUTPUT "${REPO_CORE_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "RUSTFLAGS=-C target-feature=+crt-static"
            "${CARGO_EXECUTABLE}" build --release --package repo-core --target "${REPO_CORE_TARGET}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    COMMENT "Building repo-core (${REPO_CORE_TARGET})"
    VERBATIM)
endif()

add_custom_target(repo_core_build DEPENDS "${REPO_CORE_LIB}")

add_library(repo_core STATIC IMPORTED)
set_target_properties(repo_core PROPERTIES IMPORTED_LOCATION "${REPO_CORE_LIB}")

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(repo WIN32
  src/main.cpp
  src/Export.cpp
  src/RepoCore.cpp
)

target_include_directories(repo PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../core/include"
)
add_dependencies(repo repo_core_build)

target_compile_definitions(repo PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(repo PRIVATE
  repo_core
  # Native libraries the Rust standard library expects.
  ws2_32
  userenv
  advapi32
  bcrypt
  ntdll
  dbghelp
  kernel32
  user32
  gdi32
  shell32
  ole32
  oleaut32
  comdlg32
  comctl32
  unofficial::sqlite3::sqlite3
  ZLIB::ZLIB
)

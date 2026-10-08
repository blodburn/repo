cmake_minimum_required(VERSION 3.21)
project(repo_portable_windows VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(repo WIN32
  src/main.cpp
)

target_compile_definitions(repo PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(repo PRIVATE
  unofficial::sqlite3::sqlite3
  user32
  gdi32
  shell32
  ole32
  comdlg32
)

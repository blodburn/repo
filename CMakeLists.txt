cmake_minimum_required(VERSION 3.21)
project(repo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql)
qt_standard_project_setup()

qt_add_executable(repo
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/ProjectDatabase.h
    src/ProjectDatabase.cpp
    src/ScriptHighlighter.h
    src/ScriptHighlighter.cpp
)

target_link_libraries(repo PRIVATE Qt6::Widgets Qt6::Sql)

set_target_properties(repo PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "repo"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.local.repo"
)

install(TARGETS repo BUNDLE DESTINATION . RUNTIME DESTINATION bin)

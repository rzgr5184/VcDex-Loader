cmake_minimum_required(VERSION 3.20)
project(VcDexLoader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statik calisma zamani: kullanicida VC++ redist gerekmez.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8 /bigobj)
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
)

set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backdrop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_pages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_splash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sha256.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/inject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/images.cpp
)

add_executable(VcDexLoader WIN32
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/res/resources.rc
)

target_link_libraries(VcDexLoader PRIVATE
    d3d11
    dxgi
    dwmapi
    winhttp
    bcrypt
    shell32
    advapi32
    user32
    gdi32
    ole32
)

set_target_properties(VcDexLoader PROPERTIES
    OUTPUT_NAME "VcDexLoader"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)

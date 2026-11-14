cmake_minimum_required(VERSION 3.21)

project(StreamPlayer LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "StreamPlayer is a Windows-only project.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "StreamPlayer requires MSVC: the WebView2 SDK uses WRL.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure for x64: cmake -S . -B build -A x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Статический CRT: exe работает без Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
#  WebView2 SDK
#  Тянем nuget-пакет (это zip) и распаковываем. Нужны только при сборке:
#  на машине пользователя требуется лишь WebView2 Runtime (в Windows 11 уже есть).
# ---------------------------------------------------------------------------
set(WV2_ROOT    "${CMAKE_BINARY_DIR}/webview2-sdk")
set(WV2_INCLUDE "${WV2_ROOT}/build/native/include")
set(WV2_LIB     "${WV2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")

if(NOT EXISTS "${WV2_INCLUDE}/WebView2.h" OR NOT EXISTS "${WV2_LIB}")
    message(STATUS "Скачиваю WebView2 SDK с nuget.org...")

    set(WV2_INDEX "${CMAKE_BINARY_DIR}/webview2-versions.json")
    file(DOWNLOAD "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/index.json"
         "${WV2_INDEX}" STATUS wv2_index_status)
    list(GET wv2_index_status 0 wv2_index_code)
    if(NOT wv2_index_code EQUAL 0)
        message(FATAL_ERROR "Не удалось получить список версий WebView2 SDK: ${wv2_index_status}")
    endif()

    file(READ "${WV2_INDEX}" wv2_versions_json)
    string(JSON wv2_version_count LENGTH "${wv2_versions_json}" versions)
    math(EXPR wv2_last "${wv2_version_count} - 1")

    set(WV2_VERSION "")
    foreach(i RANGE 0 ${wv2_last})
        string(JSON wv2_candidate GET "${wv2_versions_json}" versions ${i})
        if(NOT wv2_candidate MATCHES "-")
            set(WV2_VERSION "${wv2_candidate}")
        endif()
    endforeach()
    if(WV2_VERSION STREQUAL "")
        message(FATAL_ERROR "В индексе nuget не нашлось стабильной версии WebView2 SDK.")
    endif()
    message(STATUS "WebView2 SDK version: ${WV2_VERSION}")

    set(WV2_NUPKG "${CMAKE_BINARY_DIR}/webview2-sdk.zip")
    file(DOWNLOAD
         "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WV2_VERSION}/microsoft.web.webview2.${WV2_VERSION}.nupkg"
         "${WV2_NUPKG}" STATUS wv2_download_status)
    list(GET wv2_download_status 0 wv2_download_code)
    if(NOT wv2_download_code EQUAL 0)
        message(FATAL_ERROR "Не удалось скачать WebView2 SDK: ${wv2_download_status}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${WV2_NUPKG}" DESTINATION "${WV2_ROOT}")

    if(NOT EXISTS "${WV2_LIB}")
        message(FATAL_ERROR "В архиве SDK нет ${WV2_LIB}")
    endif()
endif()

# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
#  Интерфейс: файлы из assets/ вшиваются в exe как RCDATA.
#  При запуске они распаковываются в %LOCALAPPDATA%\StreamPlayer\app
#  и отдаются WebView2 по адресу https://app.local/
# ---------------------------------------------------------------------------
set(APP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(NOT EXISTS "${APP_ASSETS}/index.html")
    message(FATAL_ERROR "Не найден assets/index.html")
endif()

# В .rc подставляем абсолютные пути в нативном виде (rc их так надёжнее глотает).
file(TO_NATIVE_PATH "${APP_ASSETS}/index.html" ASSET_INDEX)
file(TO_NATIVE_PATH "${APP_ASSETS}/media/demo.mp4" ASSET_DEMO)
file(TO_NATIVE_PATH "${APP_ASSETS}/media/poster.jpg" ASSET_POSTER)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/appfiles.rc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/appfiles.rc" @ONLY)

add_executable(StreamPlayer WIN32
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/appfiles.rc"
)

target_include_directories(StreamPlayer PRIVATE "${WV2_INCLUDE}")
target_compile_definitions(StreamPlayer PRIVATE UNICODE _UNICODE)
target_compile_options(StreamPlayer PRIVATE /utf-8 /W4 /permissive-)

target_link_libraries(StreamPlayer PRIVATE
    "${WV2_LIB}"
    comctl32
    shell32
    ole32
    oleaut32
    version
    dwmapi
    user32
    gdi32
)

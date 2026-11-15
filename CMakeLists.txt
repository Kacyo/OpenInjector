cmake_minimum_required(VERSION 3.20)

project(OpenInjector VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OpenInjector 仅支持 Windows。")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(OPENINJECTOR_BUILD_TESTS "构建测试目标与示例载荷" ON)

# 统一目标 Windows 版本，确保 32 位与 64 位构建都能使用 Vista+ API。
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)

if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-cast-function-type>)
endif()

# ---------------------------------------------------------------------------
# BeterWindowUI：无边框窗口、Direct2D 绘制与主题控件库
# ---------------------------------------------------------------------------
add_library(BeterWindowUI SHARED
    library/src/painter.cpp
    library/src/theme.cpp
    library/src/widgets.cpp
    library/src/window.cpp
)
target_include_directories(BeterWindowUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/library/include")
target_compile_definitions(BeterWindowUI PRIVATE BWUI_BUILD)
set_target_properties(BeterWindowUI PROPERTIES
    OUTPUT_NAME "Kacyo.BeterWindow.UI"
    PREFIX ""
)
if(MINGW)
    target_compile_options(BeterWindowUI PRIVATE -Wno-missing-field-initializers)
endif()
target_link_libraries(BeterWindowUI PRIVATE d2d1 dwrite dwmapi gdi32 user32 ole32 uuid)

# ---------------------------------------------------------------------------
# oi_core：注入核心静态库，供 GUI 与 CLI 共用
# ---------------------------------------------------------------------------
add_library(oi_core STATIC
    src/core/util.cpp
    src/core/remote.cpp
    src/core/injector.cpp
    src/core/bridge.cpp
)
target_include_directories(oi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(oi_core PUBLIC shell32)

# ---------------------------------------------------------------------------
# 32 位目标支持：x86 原生 GUI/CLI，以及 x64 使用的辅助程序
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-m32")
check_cxx_source_compiles("int main() { return sizeof(void*) == 4 ? 0 : 1; }"
                          OPENINJECTOR_HAS_M32)
unset(CMAKE_REQUIRED_FLAGS)

if(OPENINJECTOR_HAS_M32)
    message(STATUS "32 位工具链可用：将构建 x86 原生注入器与 OpenInjectorHelper32.exe。")
    add_library(oi_core32 STATIC
        src/core/util.cpp
        src/core/remote.cpp
        src/core/injector.cpp
    )
    target_include_directories(oi_core32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
    target_link_libraries(oi_core32 PUBLIC shell32)
    target_compile_options(oi_core32 PRIVATE -m32)
    target_link_options(oi_core32 PRIVATE -m32)

    add_executable(OpenInjectorHelper32 src/helper/main.cpp)
    target_link_libraries(OpenInjectorHelper32 PRIVATE oi_core32 shell32)
    target_compile_options(OpenInjectorHelper32 PRIVATE -m32)
    target_link_options(OpenInjectorHelper32 PRIVATE -m32)

    # 32 位界面库：与 64 位版本放在同一目录，用独立文件名避免互相覆盖。
    add_library(BeterWindowUI32 SHARED
        library/src/painter.cpp
        library/src/theme.cpp
        library/src/widgets.cpp
        library/src/window.cpp
    )
    target_include_directories(BeterWindowUI32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/library/include")
    target_compile_definitions(BeterWindowUI32 PRIVATE BWUI_BUILD)
    set_target_properties(BeterWindowUI32 PROPERTIES
        OUTPUT_NAME "Kacyo.BeterWindow.UI32"
        PREFIX ""
    )
    target_compile_options(BeterWindowUI32 PRIVATE -m32)
    target_link_options(BeterWindowUI32 PRIVATE -m32)
    target_link_libraries(BeterWindowUI32 PRIVATE d2d1 dwrite dwmapi gdi32 user32 ole32 uuid)
    if(MINGW)
        target_compile_options(BeterWindowUI32 PRIVATE -Wno-missing-field-initializers)
    endif()

    # x86 原生 GUI：可直接运行于 32 位 Windows，直接注入 x86 目标进程。
    add_executable(OpenInjector32 WIN32
        src/gui/main.cpp
        src/gui/app.cpp
        src/gui/draw.cpp
        src/resources/OpenInjector32.rc
    )
    target_include_directories(OpenInjector32 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources")
    target_link_libraries(OpenInjector32 PRIVATE
        oi_core32
        BeterWindowUI32
        d2d1 dwrite dwmapi gdi32 user32 ole32 uuid shell32
    )
    target_compile_options(OpenInjector32 PRIVATE -m32)
    target_link_options(OpenInjector32 PRIVATE -m32)
    # windres 默认按主机架构生成资源，32 位目标必须显式指定 pe-i386。
    set_source_files_properties(src/resources/OpenInjector32.rc
        PROPERTIES COMPILE_FLAGS "-F pe-i386")

    # x86 原生命令行工具。
    add_executable(OpenInjectorTool32
        src/cli/main.cpp
        src/resources/OpenInjectorTool32.rc
    )
    target_include_directories(OpenInjectorTool32 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources")
    target_link_libraries(OpenInjectorTool32 PRIVATE oi_core32 shell32)
    target_compile_options(OpenInjectorTool32 PRIVATE -m32)
    target_link_options(OpenInjectorTool32 PRIVATE -m32)
    set_source_files_properties(src/resources/OpenInjectorTool32.rc
        PROPERTIES COMPILE_FLAGS "-F pe-i386")
else()
    message(STATUS "32 位工具链不可用，跳过 x86 原生注入器与 OpenInjectorHelper32（x86 目标注入）。")
endif()

# ---------------------------------------------------------------------------
# GUI：OpenInjector.exe
# ---------------------------------------------------------------------------
add_executable(OpenInjector WIN32
    src/gui/main.cpp
    src/gui/app.cpp
    src/gui/draw.cpp
    src/resources/OpenInjector.rc
)
target_include_directories(OpenInjector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources")
target_link_libraries(OpenInjector PRIVATE
    oi_core
    BeterWindowUI
    d2d1 dwrite dwmapi gdi32 user32 ole32 uuid shell32
)

# ---------------------------------------------------------------------------
# CLI：OpenInjectorTool.exe
# ---------------------------------------------------------------------------
add_executable(OpenInjectorTool
    src/cli/main.cpp
    src/resources/OpenInjectorTool.rc
)
target_include_directories(OpenInjectorTool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources")
target_link_libraries(OpenInjectorTool PRIVATE oi_core shell32)

# ---------------------------------------------------------------------------
# 测试：无窗口目标进程 + 写标记文件的示例 DLL
# ---------------------------------------------------------------------------
if(OPENINJECTOR_BUILD_TESTS)
    add_executable(inject_target tests/inject_target.cpp)

    add_library(SamplePayload SHARED tests/sample_payload.cpp)
    set_target_properties(SamplePayload PROPERTIES
        OUTPUT_NAME "SamplePayload"
        PREFIX ""
    )

    if(OPENINJECTOR_HAS_M32)
        add_executable(inject_target32 tests/inject_target.cpp)
        target_compile_options(inject_target32 PRIVATE -m32)
        target_link_options(inject_target32 PRIVATE -m32)

        add_library(SamplePayload32 SHARED tests/sample_payload.cpp)
        set_target_properties(SamplePayload32 PROPERTIES
            OUTPUT_NAME "SamplePayload32"
            PREFIX ""
        )
        target_compile_options(SamplePayload32 PRIVATE -m32)
        target_link_options(SamplePayload32 PRIVATE -m32)
    endif()
endif()

cmake_minimum_required(VERSION 3.24)

project(ProcessLens VERSION 1.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "ProcessLens is a native Windows application and requires Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PROCESSLENS_BUILD_TESTS "Build ProcessLens unit tests" ON)

add_library(processlens_core STATIC
    src/Calculations.cpp
    src/Formatting.cpp
    src/Settings.cpp
)
target_include_directories(processlens_core PUBLIC include)
target_compile_definitions(processlens_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_link_libraries(processlens_core PUBLIC shell32 ole32)
if(MSVC)
    target_compile_options(processlens_core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(ProcessLens WIN32
    src/main.cpp
    src/App.cpp
    src/LauncherShortcut.cpp
    src/MainWindow.cpp
    src/MetricsCollector.cpp
    src/GpuCollector.cpp
    src/Renderer.cpp
    src/Logging.cpp
    resources/ProcessLens.rc
)
target_include_directories(ProcessLens PRIVATE include)
target_compile_definitions(ProcessLens PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_link_libraries(ProcessLens PRIVATE
    processlens_core
    d2d1 dwrite dwmapi psapi iphlpapi pdh shell32 ole32 user32 gdi32 comctl32 advapi32
)
if(MSVC)
    target_compile_options(ProcessLens PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(ProcessLens PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
endif()

set_target_properties(ProcessLens PROPERTIES
    OUTPUT_NAME "ProcessLens"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ProcessLens>"
)

if(PROCESSLENS_BUILD_TESTS)
    enable_testing()
    add_executable(ProcessLensTests
        tests/TestMain.cpp
        src/GpuCollector.cpp
    )
    target_link_libraries(ProcessLensTests PRIVATE processlens_core pdh)
    target_compile_definitions(ProcessLensTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
    if(MSVC)
        target_compile_options(ProcessLensTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME ProcessLensTests COMMAND ProcessLensTests)
endif()

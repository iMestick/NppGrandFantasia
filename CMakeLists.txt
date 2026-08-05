cmake_minimum_required(VERSION 3.20)

project(NppGrandFantasia
    VERSION 0.8.2
    DESCRIPTION "Ferramentas de modding do Grand Fantasia para Notepad++"
    LANGUAGES CXX RC
)

if (NOT WIN32)
    message(FATAL_ERROR "NppGrandFantasia e um plugin nativo do Notepad++ e so pode ser compilado no Windows.")
endif()

option(NPPGF_BUILD_TESTS "Compilar os testes do validador" OFF)

add_library(NppGrandFantasia SHARED
    src/PluginMain.cpp
    src/PipeValidator.cpp
    src/GrandFantasiaTextColors.cpp
    src/PipeColorSettings.cpp
    src/PipeColorDialog.cpp
    src/ToolbarStatus.cpp
    src/MirrorToolbar.cpp
    src/MirrorLinkDialog.cpp
    src/MirrorLinkManager.cpp
    src/ValidatorWindow.cpp
    src/NppGrandFantasia.rc
)

target_include_directories(NppGrandFantasia PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(NppGrandFantasia PRIVATE cxx_std_17)

target_compile_definitions(NppGrandFantasia PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0601
    _WIN32_WINNT=0x0601
    _WIN32_IE=0x0600
)

target_link_libraries(NppGrandFantasia PRIVATE
    comctl32
    comdlg32
)

set_target_properties(NppGrandFantasia PROPERTIES
    PREFIX ""
    OUTPUT_NAME "NppGrandFantasia"
    PDB_NAME "NppGrandFantasia"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

if (MSVC)
    target_compile_options(NppGrandFantasia PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
        /MP
    )
    target_link_options(NppGrandFantasia PRIVATE /INCREMENTAL:NO /DEBUG:FULL)
endif()

# A DLL e gerada somente uma vez em build/<configuracao>.

if (NPPGF_BUILD_TESTS)
    enable_testing()

    add_executable(NppGrandFantasiaParserTests
        tests/PipeValidatorTests.cpp
        src/PipeValidator.cpp
        src/GrandFantasiaTextColors.cpp
    )

    target_include_directories(NppGrandFantasiaParserTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_compile_features(NppGrandFantasiaParserTests PRIVATE cxx_std_17)

    if (MSVC)
        target_compile_options(NppGrandFantasiaParserTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()

    add_test(NAME PipeValidatorTests COMMAND NppGrandFantasiaParserTests)
endif()

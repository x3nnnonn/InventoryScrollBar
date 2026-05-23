cmake_minimum_required(VERSION 3.24)
project(InventoryScrollBar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "Windows-only — this DLL injects into Subnautica2.exe")
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>"
        "$<$<COMPILE_LANGUAGE:CXX>:/MP>"
        "$<$<COMPILE_LANGUAGE:CXX>:/bigobj>"
        "$<$<COMPILE_LANGUAGE:CXX>:/W3>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4267>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4244>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4305>"
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    enable_language(ASM_MASM)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)

set(SDK_DIR ${CMAKE_SOURCE_DIR}/SDK/CppSDK)
set(SDK_FUNCTION_SOURCES
    ${SDK_DIR}/SDK/Basic.cpp
    ${SDK_DIR}/SDK/CoreUObject_functions.cpp
    ${SDK_DIR}/SDK/Engine_functions.cpp
    ${SDK_DIR}/SDK/Subnautica2_functions.cpp
    ${SDK_DIR}/SDK/UWEInventory_functions.cpp
    ${SDK_DIR}/SDK/UWECommonUI_functions.cpp
    ${SDK_DIR}/SDK/UMG_functions.cpp
    ${SDK_DIR}/SDK/WBP_Inventory_functions.cpp
    ${SDK_DIR}/SDK/WBP_ScrollBoxThatWorksWithController_functions.cpp
)

add_library(sn2sdk STATIC
    src/sdk_unit.cpp
    ${SDK_FUNCTION_SOURCES}
)
target_include_directories(sn2sdk PUBLIC ${SDK_DIR} ${CMAKE_SOURCE_DIR})
target_compile_options(sn2sdk PRIVATE /bigobj /GR- /EHsc)
target_compile_definitions(sn2sdk PRIVATE NOMINMAX)

add_executable(generate_proxy tools/generate_proxy.cpp)
set_target_properties(generate_proxy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

set(PROXY_NAME winmm)
set(PROXY_SYSTEM_DLL "$ENV{SystemRoot}/System32/${PROXY_NAME}.dll")
set(PROXY_GEN_DIR ${CMAKE_BINARY_DIR}/generated/${PROXY_NAME})

set(PROXY_DEF ${PROXY_GEN_DIR}/${PROXY_NAME}.def)
set(PROXY_ASM ${PROXY_GEN_DIR}/${PROXY_NAME}.asm)
set(PROXY_CPP ${PROXY_GEN_DIR}/proxy_dllmain.cpp)

add_custom_command(
    OUTPUT ${PROXY_DEF} ${PROXY_ASM} ${PROXY_CPP}
    COMMAND $<TARGET_FILE:generate_proxy> ${PROXY_SYSTEM_DLL} ${PROXY_GEN_DIR}
    DEPENDS generate_proxy
    COMMENT "Generating ${PROXY_NAME}.dll proxy forwards"
    VERBATIM
)

add_library(${PROXY_NAME} SHARED
    ${PROXY_CPP}
    ${PROXY_ASM}
    ${PROXY_DEF}
    loader/proxy_mods.cpp
    loader/log.cpp
)

target_include_directories(${PROXY_NAME} PRIVATE loader ${PROXY_GEN_DIR})
set_target_properties(${PROXY_NAME} PROPERTIES
    OUTPUT_NAME "${PROXY_NAME}"
    PREFIX ""
)

add_library(InventoryScrollBar SHARED
    src/dllmain.cpp
    src/log.cpp
    src/process_event_hook.cpp
    src/inventory_scroll.cpp
)

target_include_directories(InventoryScrollBar PRIVATE src ${SDK_DIR} ${CMAKE_SOURCE_DIR})
target_compile_definitions(InventoryScrollBar PRIVATE NOMINMAX)
target_link_libraries(InventoryScrollBar PRIVATE sn2sdk minhook)

set_target_properties(InventoryScrollBar PROPERTIES
    OUTPUT_NAME "InventoryScrollBar"
    PREFIX ""
)

set_target_properties(generate_proxy ${PROXY_NAME} InventoryScrollBar PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "InventoryScrollBar configured. Build with: cmake --build build --config Release")

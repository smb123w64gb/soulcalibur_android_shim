cmake_minimum_required(VERSION 3.16)
project(SoulcaliburPC C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 1. Architecture Check (Must be 32-bit Win32)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "libsoul.so is a 32-bit ELF binary! Build with -A Win32")
endif()

# 2. Compiler Specific Configurations
if(MSVC)
    add_compile_options(/Gd /MP /W3)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
else()
    add_compile_options(-m32 -O2 -Wall -Wno-unused-variable -Wno-unused-function)
    add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
endif()

# 3. Build 32-bit zlib static via FetchContent
include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
)
FetchContent_MakeAvailable(zlib)

# 4. SDL2 Configuration
set(SDL2_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SDL2-2.30.8/i686-w64-mingw32" CACHE PATH "Path to SDL2 32-bit SDK")

# Create a virtual "SDL2" include directory in the build folder so <SDL2/SDL.h> works with MSVC layout
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sdl_compat/SDL2")
file(GLOB SDL_HEADERS "${SDL2_PATH}/include/*.h")
foreach(HEADER ${SDL_HEADERS})
    get_filename_component(HEADER_NAME ${HEADER} NAME)
    configure_file(${HEADER} "${CMAKE_BINARY_DIR}/sdl_compat/SDL2/${HEADER_NAME}" COPYONLY)
endforeach()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/sdl_compat     # Gives access to <SDL2/SDL.h>
    ${SDL2_PATH}/include               # Gives access to <SDL.h>
    ${zlib_SOURCE_DIR}
    ${zlib_BINARY_DIR}
)

# Collect all project source files
file(GLOB_RECURSE SRC_FILES
    "src/*.h"
    "src/*.cpp"
    "src/*.c"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Link paths
target_link_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_PATH}/lib/x86
    ${SDL2_PATH}/lib
)

set(WINDOWS_LIBS kernel32 user32 gdi32 opengl32)

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2main
        SDL2
        zlibstatic
        ${WINDOWS_LIBS}
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        mingw32
        SDL2main
        SDL2
        zlibstatic
        ${WINDOWS_LIBS}
    )
endif()

# Create runtime directories using CMake built-in tool (no pwsh required)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/save"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)
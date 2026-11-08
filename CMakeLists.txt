cmake_minimum_required(VERSION 3.22)
project(StuntCarVR VERSION 0.1.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(SCR_ENABLE_VR      "Build with OpenXR (PCVR / Quest)"                      ON)
option(SCR_BUILD_APP      "Build the game executable (needs SDL3)"               ON)
option(SCR_BUILD_TESTS    "Build the headless tests"                              ON)
option(SCR_FETCH_DEPS     "Download SDL3 and the OpenXR loader if not found"      ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    # the original remake was a UNICODE Win32 project (MessageBox with L"" strings)
    add_compile_definitions(UNICODE _UNICODE)
endif()

if(MSVC)
    add_compile_options(/W3 /utf-8 /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # The Amiga-derived physics relies on signed char (ARM, i.e. Quest, defaults to unsigned)
    add_compile_options(-fsigned-char -Wall -Wno-unused-variable -Wno-unused-but-set-variable
                        -Wno-unused-function -Wno-comment -Wno-sign-compare -Wno-parentheses
                        -Wno-misleading-indentation -Wno-write-strings $<$<COMPILE_LANGUAGE:CXX>:-Wno-writable-strings>)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-unknown-warning-option -Wno-null-conversion -Wno-invalid-source-encoding)
    else()
        # the original DirectSound idiom Play(NULL, NULL, NULL)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-conversion-null>)
    endif()
endif()

# -----------------------------------------------------------------------------
# Game core: original Stunt Car Racer remake sources (physics, tracks, AI)
# -----------------------------------------------------------------------------
add_library(scr_core STATIC
    src/game/3d_engine.cpp
    src/game/backdrop.cpp
    src/game/car.cpp
    src/game/car_behaviour.cpp
    src/game/opponent_behaviour.cpp
    src/game/track.cpp
    src/game/game.cpp
    src/compat/scr_sound.cpp
)
target_include_directories(scr_core PUBLIC src/compat src/game src/math)

# Renderer (OpenGL 3.3 core / OpenGL ES 3.0) and scene setup
add_library(scr_render STATIC
    src/render/gl_api.cpp
    src/render/renderer.cpp
    src/app/scene_setup.cpp
)
target_include_directories(scr_render PUBLIC src/render src/app)
target_link_libraries(scr_render PUBLIC scr_core)

# Force feedback model (pure logic)
add_library(scr_ffb STATIC src/input/ffb.cpp)
target_include_directories(scr_ffb PUBLIC src/input)
target_link_libraries(scr_ffb PUBLIC scr_core)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
include(FetchContent)

if(SCR_BUILD_APP)
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        if(NOT SCR_FETCH_DEPS)
            message(FATAL_ERROR "SDL3 not found (set SDL3_DIR or enable SCR_FETCH_DEPS)")
        endif()
        message(STATUS "Fetching SDL3")
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-3.2.30
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

if(SCR_BUILD_APP AND SCR_ENABLE_VR)
    find_package(OpenXR CONFIG QUIET)
    if(NOT OpenXR_FOUND)
        if(NOT SCR_FETCH_DEPS)
            message(FATAL_ERROR "OpenXR loader not found (set OpenXR_DIR or enable SCR_FETCH_DEPS)")
        endif()
        message(STATUS "Fetching the OpenXR SDK (loader + headers)")
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
        set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
        if(MINGW)
            set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)   # static loader: no DLL, no GNU-ld version script
        else()
            set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_Declare(OpenXR
            GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
            GIT_TAG        release-1.1.65
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(OpenXR)
        if(MINGW AND TARGET openxr_loader)
            # MinGW's winapifamily.h leaves WINAPI_PARTITION_SYSTEM undefined; the loader builds with -Werror=undef
            target_compile_options(openxr_loader PRIVATE -Wno-error=undef -Wno-undef)
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# The game
# -----------------------------------------------------------------------------
if(SCR_BUILD_APP)
    set(SCR_APP_SOURCES
        src/app/main.cpp
        src/app/settings.cpp
        src/app/asset_sdl.cpp
        src/app/vr_controls.cpp
        src/input/input_sdl.cpp
        src/xr/xr_app.cpp
    )
    if(ANDROID)
        # SDL's Java activity loads libmain.so
        add_library(StuntCarVR SHARED ${SCR_APP_SOURCES})
        set_target_properties(StuntCarVR PROPERTIES OUTPUT_NAME main)
    else()
        add_executable(StuntCarVR WIN32 ${SCR_APP_SOURCES})
    endif()
    target_include_directories(StuntCarVR PRIVATE src/xr)
    target_link_libraries(StuntCarVR PRIVATE scr_render scr_ffb SDL3::SDL3)

    if(SCR_ENABLE_VR)
        if(TARGET OpenXR::openxr_loader)
            target_link_libraries(StuntCarVR PRIVATE OpenXR::openxr_loader)
        elseif(TARGET openxr_loader)
            target_link_libraries(StuntCarVR PRIVATE openxr_loader)
            target_include_directories(StuntCarVR PRIVATE ${openxr_SOURCE_DIR}/include ${OpenXR_SOURCE_DIR}/include)
        endif()
    endif()

    if(WIN32)
        target_link_libraries(StuntCarVR PRIVATE opengl32)
        if(MINGW)
            # UNICODE build: SDL provides wWinMain
            target_link_options(StuntCarVR PRIVATE -municode)
        endif()
    elseif(ANDROID)
        target_link_libraries(StuntCarVR PRIVATE EGL GLESv3 android log)
    else()
        find_library(EGL_LIB EGL REQUIRED)
        find_path(EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
        target_include_directories(StuntCarVR PRIVATE ${EGL_INCLUDE_DIR})
        target_link_libraries(StuntCarVR PRIVATE ${EGL_LIB})
    endif()

    # Game data next to the executable (desktop). Android packages it as APK assets.
    if(NOT ANDROID)
        add_custom_command(TARGET StuntCarVR POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:StuntCarVR>/assets)
        if(WIN32 AND TARGET openxr_loader)
            add_custom_command(TARGET StuntCarVR POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:openxr_loader> $<TARGET_FILE_DIR:StuntCarVR>)
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Tests (no window / GPU needed except render_test, which uses headless EGL)
# -----------------------------------------------------------------------------
if(SCR_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(sim_test tests/sim_test.cpp src/compat/asset_stdio.cpp)
    target_link_libraries(sim_test PRIVATE scr_core)
    add_test(NAME sim_test COMMAND sim_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(ffb_test tests/ffb_test.cpp src/compat/asset_stdio.cpp)
    target_link_libraries(ffb_test PRIVATE scr_ffb)
    add_test(NAME ffb_test COMMAND ffb_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    if(SCR_BUILD_APP AND TARGET SDL3::SDL3)
        add_executable(vr_controls_test tests/vr_controls_test.cpp src/app/vr_controls.cpp)
        target_include_directories(vr_controls_test PRIVATE src/xr src/app src/input src/render)
        target_link_libraries(vr_controls_test PRIVATE scr_core SDL3::SDL3)
        add_test(NAME vr_controls_test COMMAND vr_controls_test)
    endif()

    if(UNIX AND NOT APPLE)
        find_path(EGL_INCLUDE EGL/egl.h)
        if(EGL_INCLUDE)
            add_executable(render_test tests/render_test.cpp src/compat/asset_stdio.cpp)
            target_include_directories(render_test PRIVATE ${EGL_INCLUDE})
            target_link_libraries(render_test PRIVATE scr_render ${CMAKE_DL_LIBS})
        endif()
    endif()
endif()

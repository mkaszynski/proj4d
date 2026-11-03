cmake_minimum_required(VERSION 3.24)

project(proj4d VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
if(POLICY CMP0169)
    # We intentionally populate only selected native sources; making the full
    # Hypercraft project available would configure unrelated platform targets.
    cmake_policy(SET CMP0169 OLD)
endif()

# Proj4D deliberately reuses Hypercraft's authoritative native C++ terrain
# implementation instead of maintaining a look-alike generator.  Pinning the
# source keeps Low/High worlds reproducible while avoiding all browser,
# multiplayer, renderer, lighting, and mob targets from Hypercraft.
FetchContent_Declare(
    hypercraft_terrain_source
    GIT_REPOSITORY https://github.com/mkaszynski/hypercraft.git
    GIT_TAG d830abc
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(hypercraft_terrain_source)
if(NOT hypercraft_terrain_source_POPULATED)
    FetchContent_Populate(hypercraft_terrain_source)
endif()

add_library(proj4d_hypercraft_terrain STATIC
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/biome_registry.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/chunk.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/content_registry.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/coordinates.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/ore_registry.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/terrain_generator.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/tree_registry.cpp
    ${hypercraft_terrain_source_SOURCE_DIR}/src/world/world_seed.cpp
)
target_include_directories(proj4d_hypercraft_terrain PUBLIC
    ${hypercraft_terrain_source_SOURCE_DIR}/include
)
target_compile_features(proj4d_hypercraft_terrain PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(proj4d_hypercraft_terrain PRIVATE
        $<$<CONFIG:Debug>:/O2>)
else()
    # The authoritative generator performs dense 16^4 noise/decorator work.
    # Keep that dependency optimized even in Proj4D's Debug test build so the
    # correctness configuration does not turn chunk generation into minutes.
    target_compile_options(proj4d_hypercraft_terrain PRIVATE
        $<$<CONFIG:Debug>:-O2>)
endif()

find_package(SDL2 2.0 CONFIG QUIET)
find_package(Threads REQUIRED)
if(TARGET SDL2::SDL2)
    set(PROJ4D_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET "sdl2>=2.0")
    set(PROJ4D_SDL_TARGET PkgConfig::SDL2)
endif()

add_library(proj4d_core
    src/clouds.cpp
    src/content.cpp
    src/game_session.cpp
    src/inventory.cpp
    src/camera.cpp
    src/camera2d.cpp
    src/camera3d.cpp
    src/chunk.cpp
    src/coordinates.cpp
    src/mouse_input.cpp
    src/player_motion.cpp
    src/player_motion2d.cpp
    src/player_motion3d.cpp
    src/render_geometry.cpp
    src/render2d.cpp
    src/render3d.cpp
    src/terrain_generator.cpp
    src/volume4d.cpp
    src/view_status.cpp
    src/world.cpp
    src/world2d.cpp
    src/world3d.cpp
    src/world_save.cpp
)
target_include_directories(proj4d_core PUBLIC include)
target_compile_features(proj4d_core PUBLIC cxx_std_20)
target_link_libraries(proj4d_core PUBLIC proj4d_hypercraft_terrain Threads::Threads)

add_executable(proj4d src/main.cpp src/sdl_view.cpp)
target_link_libraries(proj4d PRIVATE proj4d_core ${PROJ4D_SDL_TARGET})
if(TARGET SDL2::SDL2main)
    target_link_libraries(proj4d PRIVATE SDL2::SDL2main)
endif()

if(MSVC)
    target_compile_options(proj4d_core PRIVATE /W4 /WX)
    target_compile_options(proj4d PRIVATE /W4 /WX)
else()
    target_compile_options(proj4d_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(proj4d PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(proj4d_tests tests/test_main.cpp)
    target_link_libraries(proj4d_tests PRIVATE proj4d_core)
    if(MSVC)
        target_compile_options(proj4d_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(proj4d_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME proj4d_tests COMMAND proj4d_tests)
endif()

install(TARGETS proj4d RUNTIME DESTINATION bin)

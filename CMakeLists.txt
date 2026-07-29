cmake_minimum_required(VERSION 3.24)

project(proj4d VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

find_package(SDL2 2.0 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(PROJ4D_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET "sdl2>=2.0")
    set(PROJ4D_SDL_TARGET PkgConfig::SDL2)
endif()

add_library(proj4d_core
    src/camera.cpp
    src/chunk.cpp
    src/coordinates.cpp
    src/mouse_input.cpp
    src/player_motion.cpp
    src/render_geometry.cpp
    src/terrain_generator.cpp
    src/view_status.cpp
    src/world.cpp
)
target_include_directories(proj4d_core PUBLIC include)
target_compile_features(proj4d_core PUBLIC cxx_std_20)

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

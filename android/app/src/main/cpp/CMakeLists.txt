# Jogo nativo (código recompilado + NativeBoy runtime) para Android. Pastas na raiz do projeto:
#   nativeboy/  fontes do runtime (core, common, runtime, imgui, stb)   jogo/src/  código gerado do jogo
#   SDL2/       fontes do SDL 2 (baixadas pelo GitHub na compilação)
cmake_minimum_required(VERSION 3.18)
project(nativeboy_jogo C CXX ASM)
set(RAIZ ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
set(NB ${RAIZ}/nativeboy)
set(JOGO ${RAIZ}/jogo/src)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${RAIZ}/SDL2 SDL2)

file(GLOB JOGO_CPP ${JOGO}/*.cpp)
set(RUNTIME
    ${NB}/core/machine.cpp ${NB}/core/apu.cpp ${NB}/core/sgb.cpp ${NB}/core/interp.cpp
    ${NB}/common/platform.cpp ${NB}/common/settings.cpp ${NB}/common/video.cpp ${NB}/common/ui_common.cpp
    ${NB}/common/player.cpp ${NB}/common/player_extras.cpp ${NB}/common/console.cpp ${NB}/common/touch.cpp
    ${NB}/common/music.cpp ${NB}/common/recorder.cpp ${NB}/common/link.cpp
    ${NB}/runtime/main.cpp ${NB}/runtime/selftest.cpp
    ${NB}/imgui/imgui.cpp ${NB}/imgui/imgui_draw.cpp ${NB}/imgui/imgui_tables.cpp ${NB}/imgui/imgui_widgets.cpp
    ${NB}/imgui/backends/imgui_impl_sdl2.cpp ${NB}/imgui/backends/imgui_impl_sdlrenderer2.cpp)

add_library(main SHARED ${JOGO_CPP} ${JOGO}/rom_data.S ${RUNTIME})
target_include_directories(main PRIVATE ${NB}/core ${NB}/common ${NB}/imgui ${NB}/imgui/backends ${NB}/stb ${JOGO} ${RAIZ}/SDL2/include)
target_compile_options(main PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -w -O2 -fno-strict-aliasing>)
# os arquivos do jogo são enormes (código gerado): otimização leve, como no PC
set_source_files_properties(${JOGO_CPP} PROPERTIES COMPILE_OPTIONS "-O1")
# o .incbin do rom_data.S procura rom.bin na pasta do jogo
set_source_files_properties(${JOGO}/rom_data.S PROPERTIES COMPILE_OPTIONS "-I${JOGO}")
target_link_libraries(main SDL2 android log)

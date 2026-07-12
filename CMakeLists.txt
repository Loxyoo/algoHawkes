cmake_minimum_required(VERSION 3.10)

# AJOUT : On précise qu'on utilise le C et le C++
project(AlgoICT LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mode stress test : remplace les websockets par n_symbols threads d'Ogata en temps réel.
# Activation : cmake -DSTRESS_TEST=ON ..
option(STRESS_TEST "Build en mode simulation Ogata (pas de websockets)" OFF)

add_compile_definitions(GL_SILENCE_DEPRECATION)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED) 
find_package(JsonCpp REQUIRED) 

# --- 1. Config ImGui ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)
add_library(ImGuiLib STATIC ${IMGUI_SOURCES})
target_include_directories(ImGuiLib PUBLIC ${IMGUI_DIR})
target_link_libraries(ImGuiLib PRIVATE glfw OpenGL::GL) 
 
# --- 2. Config ImPlot --- 
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/implot)
file(GLOB IMPLOT_SOURCES "${IMPLOT_DIR}/*.cpp") 
 
add_library(ImPlotLib STATIC ${IMPLOT_SOURCES})
target_link_libraries(ImPlotLib PUBLIC ImGuiLib)
target_include_directories(ImPlotLib PUBLIC ${IMPLOT_DIR})

# --- 3. Votre Application --- 

# MODIFICATION : On cherche aussi les fichiers .c
file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"    # <--- AJOUT IMPORTANT
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)

add_executable(MonApp ${USER_SOURCES}
 src/data_simulation.cpp
 src/data_simulation.h)

target_include_directories(MonApp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/calibration/hawkes_model # <--- AJOUT : Pour trouver les .h de la lib C
    ${IMGUI_DIR}
    ${IMPLOT_DIR} 
)

target_link_libraries(MonApp PRIVATE ImPlotLib ImGuiLib glfw OpenGL::GL ixwebsocket::ixwebsocket JsonCpp::JsonCpp)

# Chemin absolu vers les assets (fonts, etc.) — utilisé par le code pour éviter
# la dépendance au CWD au lancement.
target_compile_definitions(MonApp PRIVATE ALGOICT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(STRESS_TEST)
    target_compile_definitions(MonApp PRIVATE STRESS_TEST)
    message(STATUS "MonApp: mode STRESS_TEST activé")
endif()

if (APPLE)
    target_compile_options(MonApp PRIVATE -g -O0)
    target_link_libraries(MonApp PRIVATE "-framework IOKit" "-framework CoreVideo" "-framework Cocoa")
endif()

# --- 4. Test standalone de l'algorithme d'Ogata (C++) ---
# Build avec : cmake --build . --target test_ogata_cpp
# Exécution   : ./test_ogata_cpp [dimensions] [T]
add_executable(test_ogata_cpp
    ${CMAKE_SOURCE_DIR}/src/data_simulation.cpp
)

target_compile_definitions(test_ogata_cpp PRIVATE OGATA_TEST_MAIN_CPP)

target_include_directories(test_ogata_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_ogata_cpp PRIVATE JsonCpp::JsonCpp)

if (APPLE)
    target_compile_options(test_ogata_cpp PRIVATE -g -O0)
endif()
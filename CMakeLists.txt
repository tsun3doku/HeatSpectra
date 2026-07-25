cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75 CACHE STRING "CUDA architectures to compile for")
endif()

project(ParaMetal LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ParaMetal currently supports Windows only")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "ParaMetal requires the Microsoft Visual C++ (MSVC) compiler")
endif()

enable_language(CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Qt6 requires AUTOMOC for Q_OBJECT macros
set(CMAKE_AUTOMOC ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick GuiPrivate)
find_package(Vulkan REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(CUDAToolkit 12.0 REQUIRED)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/pybind11 ${CMAKE_CURRENT_BINARY_DIR}/libs/pybind11)

set(AMGX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/amgx" CACHE PATH "AMGX source directory")
set(AMGX_PREBUILT "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/amgx_prebuilt/windows-x64" CACHE PATH "Prebuilt AMGX runtime directory")
find_library(AMGX_LIBRARY
    NAMES amgxsh
    HINTS "${AMGX_PREBUILT}" "${AMGX_ROOT}/build-ninja" "${AMGX_ROOT}/build"
    PATH_SUFFIXES Release lib lib/Release
    REQUIRED)

if(WIN32)
    find_file(AMGX_RUNTIME
        NAMES amgxsh.dll
        HINTS "${AMGX_PREBUILT}" "${AMGX_ROOT}/build-ninja" "${AMGX_ROOT}/build"
        PATH_SUFFIXES Release bin bin/Release
        REQUIRED)

    set(CUDA_RUNTIME_FILES)
    foreach(CUDA_RUNTIME_PATTERN IN ITEMS
        "cudart64_*.dll"
        "cublas64_*.dll"
        "cublasLt64_*.dll"
        "cusparse64_*.dll"
        "cusolver64_*.dll"
        "nvJitLink_*.dll")
        file(GLOB CUDA_RUNTIME_MATCHES "${CUDAToolkit_BIN_DIR}/${CUDA_RUNTIME_PATTERN}")
        if(NOT CUDA_RUNTIME_MATCHES)
            message(FATAL_ERROR "Missing CUDA runtime file: ${CUDA_RUNTIME_PATTERN}")
        endif()
        list(APPEND CUDA_RUNTIME_FILES ${CUDA_RUNTIME_MATCHES})
    endforeach()
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/*.cpp")
file(GLOB HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/*.hpp"
                  "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/*.h")

file(GLOB_RECURSE NODEGRAPH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/nodegraph/*.cpp")
file(GLOB_RECURSE NODEGRAPH_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/nodegraph/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/nodegraph/*.h")

file(GLOB FRAMEGRAPH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/framegraph/*.cpp")
file(GLOB FRAMEGRAPH_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/framegraph/*.hpp")

file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/vulkan/*.cpp")
file(GLOB VULKAN_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/vulkan/*.hpp")

file(GLOB HEAT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/heat/*.cpp")
file(GLOB HEAT_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/heat/*.hpp")
file(GLOB HEAT_CUDA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/heat/*.cu")

file(GLOB CUDA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/cuda/*.cu")
file(GLOB CUDA_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/cuda/*.hpp")

file(GLOB REMESHER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/remesher/*.cpp")
file(GLOB REMESHER_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/remesher/*.hpp")

file(GLOB RENDER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/render/*.cpp")
file(GLOB RENDER_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/render/*.hpp")
file(GLOB RENDERERS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/renderers/*.cpp")
file(GLOB RENDERERS_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/renderers/*.hpp")
file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/scene/*.cpp")
file(GLOB SCENE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/scene/*.hpp")
file(GLOB VORONOI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/voronoi/*.cpp")
file(GLOB VORONOI_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/voronoi/*.hpp")
file(GLOB SPATIAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/spatial/*.cpp")
file(GLOB SPATIAL_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/spatial/*.hpp")
file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/util/*.cpp")
file(GLOB UTIL_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/util/*.hpp")
file(GLOB UTIL_HEADERS_H CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/util/*.h")
file(GLOB UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/ui/*.cpp")
file(GLOB UI_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/ui/*.hpp")
file(GLOB MESH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/*.cpp")
file(GLOB MESH_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/*.hpp")
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/project/*.cpp")
file(GLOB APP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/project/*.hpp")
file(GLOB APP_HEADERS_H CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.h")
file(GLOB MESH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/*.cpp")
file(GLOB MESH_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/mesh/*.hpp")
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/project/*.cpp")
file(GLOB APP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/project/*.hpp")
file(GLOB APP_HEADERS_H CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/app/*.h")
file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/runtime/*.cpp")
file(GLOB RUNTIME_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/runtime/*.hpp")
file(GLOB CONTACT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/contact/*.cpp")
file(GLOB CONTACT_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/contact/*.hpp")
file(GLOB DOMAIN_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/domain/*.hpp")

file(GLOB SERIAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/serial/*.cpp")
file(GLOB SERIAL_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/serial/*.hpp")

file(GLOB_RECURSE PY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/py/*.cpp")
file(GLOB_RECURSE PY_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/py/*.hpp")

file(GLOB HASH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/hash/*.cpp")
file(GLOB HASH_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/hash/*.hpp")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${NODEGRAPH_SOURCES} ${NODEGRAPH_HEADERS} ${FRAMEGRAPH_SOURCES} ${FRAMEGRAPH_HEADERS} ${VULKAN_SOURCES} ${VULKAN_HEADERS} ${HEAT_SOURCES} ${HEAT_HEADERS} ${HEAT_CUDA_SOURCES} ${CUDA_SOURCES} ${CUDA_HEADERS} ${REMESHER_SOURCES} ${REMESHER_HEADERS} ${RENDER_SOURCES} ${RENDER_HEADERS} ${RENDERERS_SOURCES} ${RENDERERS_HEADERS} ${SCENE_SOURCES} ${SCENE_HEADERS} ${VORONOI_SOURCES} ${VORONOI_HEADERS} ${SPATIAL_SOURCES} ${SPATIAL_HEADERS} ${UTIL_SOURCES} ${UTIL_HEADERS} ${UTIL_HEADERS_H} ${UI_SOURCES} ${UI_HEADERS} ${MESH_SOURCES} ${MESH_HEADERS} ${APP_SOURCES} ${APP_HEADERS} ${APP_HEADERS_H} ${RUNTIME_SOURCES} ${RUNTIME_HEADERS} ${CONTACT_SOURCES} ${CONTACT_HEADERS} ${DOMAIN_HEADERS} ${SERIAL_SOURCES} ${SERIAL_HEADERS} ${PY_SOURCES} ${PY_HEADERS} ${HASH_SOURCES} ${HASH_HEADERS})

# Include dir
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/amgx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/tinyobjloader
    ${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/libs/tinyexr/deps/nanozlib
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::GuiPrivate
    Vulkan::Vulkan
    OpenMP::OpenMP_CXX
    pybind11::embed
    Python3::Python
    CUDA::cudart
    ${AMGX_LIBRARY}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_RUNTIME_LIBRARY Shared
)

if(WIN32)
    set(RESOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/resources/ParaMetal.rc")
    target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE_FILES})

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AMGX_RUNTIME}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CUDA_RUNTIME_FILES}
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying AMGX and CUDA runtime files..."
    )
endif()

# Windows only: use windeployqt to copy Qt runtime files
if(WIN32)
    # Find windeployqt 
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(QT_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    
    if(QT_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --skip-plugin-types generic,networkinformation,tls
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/qml"
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt to copy Qt dependencies..."
        )
    endif()
endif()


# Copy shaders, models, textures and fonts folders to output dir
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/qml"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/models"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/textures"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal/fonts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts"
    COMMENT "Copying additional files to output directory..."
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ParaMetal"
    OUTPUT_NAME "parametal"
)


cmake_minimum_required(VERSION 3.21)
project(HeatSpectra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 requires AUTOMOC for Q_OBJECT macros
set(CMAKE_AUTOMOC ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Vulkan REQUIRED)
find_package(OpenMP REQUIRED)

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/*.cpp")
file(GLOB HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/*.hpp" 
                  "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/*.h")

file(GLOB NODEGRAPH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/nodegraph/*.cpp")
file(GLOB NODEGRAPH_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/nodegraph/*.hpp")

file(GLOB FRAMEGRAPH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/framegraph/*.cpp")
file(GLOB FRAMEGRAPH_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/framegraph/*.hpp")

file(GLOB VULKAN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/vulkan/*.cpp")
file(GLOB VULKAN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/vulkan/*.hpp")

file(GLOB HEAT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/heat/*.cpp")
file(GLOB HEAT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/heat/*.hpp")

file(GLOB REMESHER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh/remesher/*.cpp")
file(GLOB REMESHER_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh/remesher/*.hpp")

file(GLOB RENDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/render/*.cpp")
file(GLOB RENDER_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/render/*.hpp")
file(GLOB RENDERERS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/renderers/*.cpp")
file(GLOB RENDERERS_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/renderers/*.hpp")
file(GLOB SCENE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/scene/*.cpp")
file(GLOB SCENE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/scene/*.hpp")
file(GLOB VORONOI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/voronoi/*.cpp")
file(GLOB VORONOI_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/voronoi/*.hpp")
file(GLOB SPATIAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/spatial/*.cpp")
file(GLOB SPATIAL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/spatial/*.hpp")
file(GLOB UTIL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/util/*.cpp")
file(GLOB UTIL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/util/*.hpp")
file(GLOB UTIL_HEADERS_H "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/util/*.h")
file(GLOB MESH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh/*.cpp")
file(GLOB MESH_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh/*.hpp")
file(GLOB APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/app/*.cpp")
file(GLOB APP_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/app/*.hpp")
file(GLOB APP_HEADERS_H "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/app/*.h")
file(GLOB RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/runtime/*.cpp")
file(GLOB RUNTIME_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/runtime/*.hpp")
file(GLOB CONTACT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/contact/*.cpp")
file(GLOB CONTACT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/contact/*.hpp")
file(GLOB DOMAIN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/domain/*.hpp")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${NODEGRAPH_SOURCES} ${NODEGRAPH_HEADERS} ${FRAMEGRAPH_SOURCES} ${FRAMEGRAPH_HEADERS} ${VULKAN_SOURCES} ${VULKAN_HEADERS} ${HEAT_SOURCES} ${HEAT_HEADERS} ${REMESHER_SOURCES} ${REMESHER_HEADERS} ${RENDER_SOURCES} ${RENDER_HEADERS} ${RENDERERS_SOURCES} ${RENDERERS_HEADERS} ${SCENE_SOURCES} ${SCENE_HEADERS} ${VORONOI_SOURCES} ${VORONOI_HEADERS} ${SPATIAL_SOURCES} ${SPATIAL_HEADERS} ${UTIL_SOURCES} ${UTIL_HEADERS} ${UTIL_HEADERS_H} ${MESH_SOURCES} ${MESH_HEADERS} ${APP_SOURCES} ${APP_HEADERS} ${APP_HEADERS_H} ${RUNTIME_SOURCES} ${RUNTIME_HEADERS} ${CONTACT_SOURCES} ${CONTACT_HEADERS} ${DOMAIN_HEADERS})

# Include dir
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/nodegraph
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/framegraph
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/heat
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh/remesher
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/render
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/renderers
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/scene
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/voronoi
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/spatial
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/util
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/app
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/contact
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/domain
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/nanoflann/include
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/tinyobjloader
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/tinyply/source
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::GuiPrivate
    Vulkan::Vulkan
    OpenMP::OpenMP_CXX
)

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
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt to copy Qt dependencies..."
        )
    endif()
endif()

# Copy shaders, models, and textures folders to output dir
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/models"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/textures"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/textures"
    COMMENT "Copying additional files to output directory..."
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra"
)


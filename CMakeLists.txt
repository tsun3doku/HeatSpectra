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

# Exclude files that should be removed from the build
list(FILTER SOURCES EXCLUDE REGEX ".*AABBTree\\.cpp$")
list(FILTER HEADERS EXCLUDE REGEX ".*AABBTree\\.hpp$")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include dir
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/nanoflann/include
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/tinyobjloader
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/tinyply/source
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/isotropicremesher/src
    ${CMAKE_CURRENT_SOURCE_DIR}/HeatSpectra/libs/VulkanMemoryAllocator/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
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


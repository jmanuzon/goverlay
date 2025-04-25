cmake_minimum_required(VERSION 3.15)

project(electron_overlay)
include(CMakePrintHelpers)

add_definitions(-DNAPI_VERSION=7)
add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-DWIN32_LEAN_AND_MEAN)

file(GLOB_RECURSE SOURCE_FILES src/*.c src/*.cc src/*.cpp)

execute_process(
    COMMAND node -p "require('node-addon-api').include"
    OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE
)

string(REGEX REPLACE "[\r\n\"]" "" NODE_ADDON_API_INCLUDE ${NODE_ADDON_API_INCLUDE})

cmake_print_variables(CMAKE_GENERATOR_PLATFORM)
cmake_print_variables(NODE_ADDON_API_INCLUDE)
cmake_print_variables(CMAKE_JS_SRC)
cmake_print_variables(CMAKE_JS_INC)
cmake_print_variables(CMAKE_JS_LIB)
cmake_print_variables(CMAKE_JS_NODELIB_DEF)
cmake_print_variables(CMAKE_JS_NODELIB_TARGET)
cmake_print_variables(CMAKE_SOURCE_DIR)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_JS_INC} ${NODE_ADDON_API_INCLUDE} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/3rd)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

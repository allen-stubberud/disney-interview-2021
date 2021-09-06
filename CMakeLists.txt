cmake_minimum_required(VERSION 3.3)
project(interview-disney-2020 VERSION 0.1.0)

# Add local library code to the include path.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(CMakeRC)


# These dependencies must be installed on the system.
find_package(CURL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SigCpp3 REQUIRED)
find_package(Threads REQUIRED)


# Need to re-run CMake every time this changes.
file(
  GLOB_RECURSE SOURCES
  "${CMAKE_SOURCE_DIR}/app/*.?pp"
  )
add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
set_target_properties(
  ${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  )
# The RapidJSON config script is very minimal.
target_include_directories(
  ${PROJECT_NAME}
  PRIVATE "${RAPIDJSON_INCLUDE_DIRS}"
  )
# MinGW and friends require this to include the SDL main.
if (WIN32 AND NOT MSVC)
  target_link_libraries(${PROJECT_NAME} mingw32)
endif ()
# On some platforms the order matters.
target_link_libraries(
  ${PROJECT_NAME}
  CURL::libcurl
  glm
  SDL2::SDL2_image
  SDL2::SDL2_ttf
  SDL2::SDL2main SDL2::SDL2
  GLEW::GLEW OpenGL::GL
  SigCpp3::SigCpp
  Threads::Threads
  )


# Enable special settings for g++ and friends.
if (CMAKE_COMPILER_IS_GNUCXX)
  add_compile_options(-Wall -Wextra)
endif ()


# Embed these resources in the executable.
file(
  GLOB_RECURSE RESOURCES
  "${CMAKE_SOURCE_DIR}/data/*"
  )
cmrc_add_resource_library(
  ${PROJECT_NAME}-resources
  NAMESPACE rc
  WHENCE "data"
  ${RESOURCES}
  )
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}-resources)

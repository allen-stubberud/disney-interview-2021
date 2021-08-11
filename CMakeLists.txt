cmake_minimum_required(VERSION 3.0)
project(interview-disney-2020 VERSION 0.1.0)


# Add local library code to the include path.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")


# These dependencies must be installed on the system.
find_package(CURL REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(Threads REQUIRED)


add_executable(
  ${PROJECT_NAME}
  app/Download.cpp
  app/Download.hpp
  app/Image.cpp
  app/Image.hpp
  app/Main.cpp
  app/Main.hpp
  app/Signal.cpp
  app/Signal.hpp
  )
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
  SDL2::SDL2_image
  SDL2::SDL2_ttf
  SDL2::SDL2main SDL2::SDL2
  GLEW::GLEW OpenGL::GL
  Threads::Threads
  )


# Enable special settings for g++ and friends.
if (CMAKE_COMPILER_IS_GNUCXX)
  add_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif ()

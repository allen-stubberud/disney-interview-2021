cmake_minimum_required(VERSION 3.5)
project(interview-disney-2020 VERSION 0.1.0)


# Add local library code to the include path.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")


# These dependencies must be installed on the system.
find_package(CURL REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(RapidJSON REQUIRED)
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
  app/Json.cpp
  app/Json.hpp
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


# Use the resource script to turn data to C++ code.
function(AddResources)
  set(ONEV TARGET)
  set(TWOV INPUTS)
  cmake_parse_arguments(A "" "${ONEV}" "${TWOV}" ${ARGN})

  foreach(REQ INPUTS TARGET)
    if (NOT A_${REQ})
      message(FATAL_ERROR "Required argument is missing: ${REQ}")
    endif ()
  endforeach()

  foreach(ENT IN LISTS A_INPUTS)
    get_filename_component(TMP "${ENT}" NAME)
    string(MAKE_C_IDENTIFIER "${TMP}" TMP)
    set(SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/CompileResource.cmake")
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${TMP}.cpp")
    set(IDENTIFIER "${TMP}")

    add_custom_command(
      OUTPUT "${OUTPUT}"
      COMMAND "${CMAKE_COMMAND}"
        -D INPUT_FILE="${ENT}"
        -D OUTPUT_FILE="${OUTPUT}"
        -D IDENTIFIER="${IDENTIFIER}"
        -P "${SCRIPT}"
      MAIN_DEPENDENCY "${ENT}"
      DEPENDS "${SCRIPT}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
      )
    target_sources(${A_TARGET} PRIVATE "${OUTPUT}")
  endforeach()
endfunction()


# These files need to be embedded in the executable.
AddResources(
  TARGET ${PROJECT_NAME}
  INPUTS
    data/common-schema.json
    data/home-schema.json
    data/meta-schema.json
    data/ref-schema.json
  )

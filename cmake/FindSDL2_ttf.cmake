cmake_minimum_required(VERSION 3.0)


find_path(
  SDL2_TTF_INCLUDE_DIR
  NAMES "SDL_ttf.h"
  PATH_SUFFIXES SDL2
  )
find_library(
  SDL2_TTF_LIBRARY
  NAMES "SDL2_ttf"
  )


find_package_handle_standard_args(
  SDL2_ttf
  REQUIRED_VARS SDL2_TTF_LIBRARY SDL2_TTF_INCLUDE_DIR
  )


if (SDL2_ttf_FOUND AND NOT TARGET SDL2::SDL2_ttf)
  add_library(SDL2::SDL2_ttf UNKNOWN IMPORTED)
  set_target_properties(
    SDL2::SDL2_ttf PROPERTIES
    IMPORTED_LOCATION "${SDL2_TTF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_TTF_INCLUDE_DIR}"
    )
endif ()

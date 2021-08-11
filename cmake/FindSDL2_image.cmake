cmake_minimum_required(VERSION 3.0)


find_path(
  SDL2_IMAGE_INCLUDE_DIR
  NAMES "SDL_image.h"
  PATH_SUFFIXES SDL2
  )
find_library(
  SDL2_IMAGE_LIBRARY
  NAMES "SDL2_image"
  )


find_package_handle_standard_args(
  SDL2_image
  REQUIRED_VARS SDL2_IMAGE_LIBRARY SDL2_IMAGE_INCLUDE_DIR
  )


if (SDL2_image_FOUND AND NOT TARGET SDL2::SDL2_image)
  add_library(SDL2::SDL2_image UNKNOWN IMPORTED)
  set_target_properties(
    SDL2::SDL2_image PROPERTIES
    IMPORTED_LOCATION "${SDL2_IMAGE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_IMAGE_INCLUDE_DIR}"
    )
endif ()

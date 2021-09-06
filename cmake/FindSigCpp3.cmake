cmake_minimum_required(VERSION 3.0)


find_path(
  SIGCPP3_INCLUDE_DIR
  NAMES "sigc++/sigc++.h"
  PATH_SUFFIXES "sigc++-3.0"
  )
find_path(
  SIGCPP3_CONFIG_DIR
  NAMES "sigc++config.h"
  PATHS "${CMAKE_PREFIX_PATH}"
  PATH_SUFFIXES "lib/sigc++-3.0/include"
  )
find_library(
  SIGCPP3_LIBRARY
  NAMES "sigc-3.0"
  )


find_package_handle_standard_args(
  SigCpp3
  REQUIRED_VARS SIGCPP3_LIBRARY SIGCPP3_CONFIG_DIR SIGCPP3_INCLUDE_DIR
  )


if (SigCpp3_FOUND AND NOT TARGET SigCpp3::SigCpp)
  add_library(SigCpp3::SigCpp UNKNOWN IMPORTED)
  set_target_properties(
    SigCpp3::SigCpp PROPERTIES
    IMPORTED_LOCATION "${SIGCPP3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SIGCPP3_CONFIG_DIR};${SIGCPP3_INCLUDE_DIR}"
    )
endif ()

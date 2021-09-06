#ifndef HELPER_HPP
#define HELPER_HPP

/**
 * \file
 * \brief Common functions that are used everywhere.
 */

#include <istream>
#include <memory>

#include <SDL.h>

/// Convert to SDL stream (does not take ownership).
SDL_RWops*
CppToRW(std::istream& aFile);
/// Convert to SDL stream (take ownership of C++ object).
SDL_RWops*
CppToRW(std::unique_ptr<std::istream> aFile);

#endif

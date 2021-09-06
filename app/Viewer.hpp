#ifndef VIEWER_HPP
#define VIEWER_HPP

/**
 * \file
 * \brief Controller for the scene graph.
 */

#include <memory>

#include <SDL.h>

class Viewer
{
  class Private;
  std::unique_ptr<Private> mPrivate;

public:
  Viewer();
  Viewer(const Viewer& aOther) = delete;
  Viewer& operator=(const Viewer& aOther) = delete;
  ~Viewer();

  void DrawFrame();
  void Event(const SDL_Event& aEvent);
};

#endif

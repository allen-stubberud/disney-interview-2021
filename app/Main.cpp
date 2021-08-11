#include "Main.hpp"

#include <functional>
#include <iostream>

#include <GL/glew.h>
#include <SDL.h>

#include "Download.hpp"
#include "Image.hpp"

namespace {

void
MainLoop(SDL_Window* aWindow)
{
  while (true) {
    SDL_Event ev;
    if (SDL_WaitEvent(&ev) != 1)
      continue;

    if (ev.type == SDL_QUIT) {
      break;
    } else if (ev.type == SDL_USEREVENT) {
      // This is how the thread-safe async queue works.
      auto func = reinterpret_cast<std::function<void()>*>(ev.user.data1);
      (*func)();
      delete func;
    } else {
    }
  }
}

}

int
main(int argc, char** argv)
{
  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    std::cerr << "SDL initialization error: " << SDL_GetError() << std::endl;
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window* window =
    SDL_CreateWindow(argv[0],
                     SDL_WINDOWPOS_CENTERED,
                     SDL_WINDOWPOS_CENTERED,
                     1920,
                     1080,
                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "SDL window error: " << SDL_GetError() << std::endl;
    return 1;
  }

  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || SDL_GL_MakeCurrent(window, context) != 0) {
    std::cerr << "OpenGL context error: " << SDL_GetError() << std::endl;
    return 1;
  }

  GLenum code = glewInit();
  if (code != GLEW_OK) {
    auto message = glewGetErrorString(code);
    std::cerr << "OpenGL loader error: " << message << std::endl;
    return 1;
  }

  Download::ForkThread();
  Image::ForkThread();
  MainLoop(window);
  Download::JoinThread();
  Image::JoinThread();

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

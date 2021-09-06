#include "Main.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include <GL/glew.h>
#include <SDL.h>

#include "Graphics.hpp"
#include "Network.hpp"
#include "Viewer.hpp"
#include "Worker.hpp"

void
InvokeAsync(std::function<void()> aFunctor)
{
  SDL_UserEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = SDL_USEREVENT;
  ev.data1 = new std::function<void()>(std::move(aFunctor));
  SDL_PushEvent(&reinterpret_cast<SDL_Event&>(ev));
}

namespace {

void
HandleEvent(const SDL_UserEvent& aEvent)
{
  // This is how the thread-safe async queue works.
  auto func = reinterpret_cast<std::function<void()>*>(aEvent.data1);
  (*func)();
  delete func;
}

void
MainLoop(SDL_Window* aWindow)
{
  Viewer viewer;
  bool quit = false;

  while (!quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
      switch (ev.type) {
        case SDL_QUIT:
          quit = true;
          break;
        case SDL_USEREVENT:
          HandleEvent(ev.user);
          break;
        default:
          viewer.Event(ev);
          break;
      }

    viewer.DrawFrame();
    SDL_GL_SwapWindow(aWindow);
  }
}

}

int
main(int argc, char** argv)
{
  (void)argc;

#ifdef _WIN32
  freopen("NUL", "r", stdin);
  freopen("NUL", "w", stdout);
  freopen("NUL", "w", stderr);
#endif

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    SDL_LogCritical(0, "SDL initialization error: %s", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window* window =
    SDL_CreateWindow(argv[0],
                     SDL_WINDOWPOS_CENTERED,
                     SDL_WINDOWPOS_CENTERED,
                     1920,
                     1080,
                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_LogCritical(0, "SDL window error: %s", SDL_GetError());
    return 1;
  }

  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || SDL_GL_MakeCurrent(window, context) != 0) {
    SDL_LogCritical(0, "OpenGL error: %s", SDL_GetError());
    return 1;
  }

  GLenum code = glewInit();
  if (code != GLEW_OK) {
    SDL_LogCritical(0, "OpenGL error: %s", glewGetErrorString(code));
    return 1;
  }

  // Always print this to start.
  SDL_Log("OpenGL version: %s", glGetString(GL_VERSION));
  SDL_Log("OpenGL renderer: %s", glGetString(GL_RENDERER));
  SDL_Log("OpenGL vendor: %s", glGetString(GL_VENDOR));

  InitGraphics();
  InitNetwork();
  InitWorker();
  MainLoop(window);
  FreeGraphics();
  FreeNetwork();
  FreeWorker();

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

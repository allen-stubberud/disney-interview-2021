#include "Main.hpp"

#include <functional>

#include <GL/glew.h>
#include <SDL.h>

/// Launch the worker thread for API queries and image loads.
void
ForkDecodeThread();
/// Wait for the worker thread to exit.
void
JoinDecodeThread();
/// Initialize CURL and launch the worker thread.
void
ForkDownloadThread();
/// Wait for the worker thread to exit and clean up CURL.
void
JoinDownloadThread();
/// Initialize the font rendering library.
void
InitRenderer();
/// Clean up the font data and shut down the rendering library.
void
FreeRenderer();

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
        case SDL_WINDOWEVENT:
          if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            glViewport(0, 0, ev.window.data1, ev.window.data2);
          }

          break;
      }

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(aWindow);
  }
}

}

int
main(int argc, char** argv)
{
  (void)argc;

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    SDL_LogCritical(0, "SDL initialization error: %s", SDL_GetError());
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
    SDL_LogCritical(0, "SDL window error: %s", SDL_GetError());
    return 1;
  }

  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || SDL_GL_MakeCurrent(window, context) != 0) {
    SDL_LogCritical(0, "OpenGL context error: %s", SDL_GetError());
    return 1;
  }

  GLenum code = glewInit();
  if (code != GLEW_OK) {
    auto message = glewGetErrorString(code);
    SDL_LogCritical(0, "OpenGL error: %s", glewGetErrorString(code));
    return 1;
  }

  SDL_Log("OpenGL version: %s", glGetString(GL_VERSION));
  SDL_Log("OpenGL renderer: %s", glGetString(GL_RENDERER));
  SDL_Log("OpenGL vendor: %s", glGetString(GL_VENDOR));

  ForkDecodeThread();
  ForkDownloadThread();
  InitRenderer();
  MainLoop(window);
  FreeRenderer();
  JoinDecodeThread();
  JoinDownloadThread();

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

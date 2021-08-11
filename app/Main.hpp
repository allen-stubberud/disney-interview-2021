#ifndef MAIN_HPP
#define MAIN_HPP

#include <cstring>
#include <functional>
#include <utility>

#include <SDL.h>

template<typename T>
void
InvokeAsync(T&& aFunctor)
{
  SDL_UserEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = SDL_USEREVENT;
  ev.data1 = new std::function<void()>(std::forward<T>(aFunctor));
  SDL_PushEvent(&reinterpret_cast<SDL_Event&>(ev));
}

#endif

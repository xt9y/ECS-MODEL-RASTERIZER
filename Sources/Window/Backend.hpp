#ifndef HORSE_WINDOW_BACKEND_HPP
#define HORSE_WINDOW_BACKEND_HPP

#include <SDL3/SDL.h>

#include <vector>

namespace Window::Backend {

SDL_Window *window();
const std::vector<SDL_Event>& events();

} // namespace Window::Backend

#endif

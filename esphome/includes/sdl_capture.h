#pragma once

#include <cstdlib>
#include <cstring>

#include <SDL.h>

#include "esphome/core/log.h"

namespace oled_preview {

inline bool save_sdl_bmp(const char *path) {
  SDL_Window *window = SDL_GetKeyboardFocus();
  for (uint32_t window_id = 1; window == nullptr && window_id <= 16; window_id++) {
    window = SDL_GetWindowFromID(window_id);
  }
  if (window == nullptr) {
    ESP_LOGE("oled_preview", "Could not find SDL window: %s", SDL_GetError());
    return false;
  }
  SDL_Renderer *renderer = SDL_GetRenderer(window);
  if (renderer == nullptr) {
    ESP_LOGE("oled_preview", "Could not find SDL renderer: %s", SDL_GetError());
    return false;
  }

  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
    ESP_LOGE("oled_preview", "Could not read SDL dimensions: %s", SDL_GetError());
    return false;
  }
  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (surface == nullptr) {
    ESP_LOGE("oled_preview", "Could not create capture surface: %s", SDL_GetError());
    return false;
  }

  const bool captured = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                              surface->pixels, surface->pitch) == 0;
  const bool saved = captured && SDL_SaveBMP(surface, path) == 0;
  if (!saved) {
    ESP_LOGE("oled_preview", "Could not save SDL capture: %s", SDL_GetError());
  }
  SDL_FreeSurface(surface);
  return saved;
}

}  // namespace oled_preview

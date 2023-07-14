#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{

//
// Color class that embeds color in a uint32.
//
struct Color
{

  void set(float r, float g, float b, float a)
  {
    abgr = uint8_t(r * 255.f) | (uint8_t(g * 255.f) << 8) | (uint8_t(b * 255.f) << 16) |
           (uint8_t(a * 255.f) << 24);
  }

  float r() const { return (abgr & 0xff) / 255.f; }
  float g() const { return ((abgr >> 8) & 0xff) / 255.f; }
  float b() const { return ((abgr >> 16) & 0xff) / 255.f; }
  float a() const { return ((abgr >> 24) & 0xff) / 255.f; }

  Color operator=(const uint32_t color)
  {
    abgr = color;
    return *this;
  }

  static uint32_t fromU8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
  {
    return (r | (g << 8) | (b << 16) | (a << 24));
  }

  static uint32_t getDistinctColor(uint32_t index);

  static const uint32_t red = 0xff0000ff;
  static const uint32_t green = 0xff00ff00;
  static const uint32_t blue = 0xffff0000;
  static const uint32_t yellow = 0xff00ffff;
  static const uint32_t black = 0xff000000;
  static const uint32_t white = 0xffffffff;
  static const uint32_t transparent = 0x00000000;

  uint32_t abgr;

}; // struct Color

} // namespace Framework
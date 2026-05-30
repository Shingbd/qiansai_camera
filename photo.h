#pragma once

#include <cstdint>
#include <string>

std::string photo_capture(const uint8_t *nv12, int w, int h, const char *output_dir);

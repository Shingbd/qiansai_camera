#pragma once

#include <cstdint>
#include <string>
#include <functional>

int  recorder_init(const char *output_dir);
void recorder_deinit();
int  recorder_start();
void recorder_stop();
bool recorder_is_recording();
int  recorder_get_duration_sec();

void recorder_push_frame(const uint8_t *nv12_data, int width, int height);
void recorder_set_error_cb(std::function<void(const char *)> cb);

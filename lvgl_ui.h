#pragma once

#include <cstdint>
#include <functional>

int  lvgl_ui_init(int hor_res, int ver_res, int rotation);
void lvgl_ui_deinit();
void lvgl_ui_process();

void lvgl_ui_update_frame(const uint8_t *nv12_data, int w, int h);

void lvgl_ui_set_on_rec_start(std::function<void()> cb);
void lvgl_ui_set_on_rec_stop(std::function<void()> cb);
bool lvgl_ui_is_recording();

void lvgl_ui_set_cam_rotation(int rot);
void lvgl_ui_set_cam_mirror_h(bool on);

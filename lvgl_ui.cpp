#include "lvgl_ui.h"
#include "recorder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

extern "C" {
#include <lvgl/lvgl.h>
#include <lvgl/drivers/display/lv_linux_drm.h>
#include <lvgl/drivers/indev/lv_evdev.h>
#include <sys/ioctl.h>
#include <linux/input.h>
}

static std::function<void()> g_on_rec_start;
static std::function<void()> g_on_rec_stop;

void lvgl_ui_set_on_rec_start(std::function<void()> cb) { g_on_rec_start = std::move(cb); }
void lvgl_ui_set_on_rec_stop(std::function<void()> cb)  { g_on_rec_stop  = std::move(cb); }

// ---------------------------------------------------------------------------
// NV12 → RGB565 conversion with optional rotation
// ---------------------------------------------------------------------------
static uint8_t *g_rgb_buf = nullptr;
static int g_cam_rotation = 90;
static bool g_cam_mirror_h = true;

void lvgl_ui_set_cam_rotation(int rot) {
    if (rot == 0 || rot == 90 || rot == 180 || rot == 270)
        g_cam_rotation = rot;
}

void lvgl_ui_set_cam_mirror_h(bool on) {
    g_cam_mirror_h = on;
}

static void nv12_to_rgb565(const uint8_t *nv12, uint16_t *rgb, int w, int h, int rotation, bool mirror_h) {
    const uint8_t *y_plane = nv12;
    const uint8_t *uv_plane = nv12 + w * h;

    int w_out = (rotation == 90 || rotation == 270) ? h : w;
    int h_out = (rotation == 90 || rotation == 270) ? w : h;

    for (int j = 0; j < h_out; j++) {
        for (int i = 0; i < w_out; i++) {
            int src_x, src_y;
            switch (rotation) {
                case 90:
                    src_x = j;
                    src_y = i;
                    break;
                case 180:
                    src_x = w - 1 - i;
                    src_y = h - 1 - j;
                    break;
                case 270:
                    src_x = j;
                    src_y = h - 1 - i;
                    break;
                default:
                    src_x = i;
                    src_y = j;
                    break;
            }
            if (mirror_h) src_x = w - 1 - src_x;

            int yi = src_y * w + src_x;
            int ui = (src_y / 2) * w + (src_x / 2) * 2;
            int vy  = (src_y / 2) * w + (src_x / 2) * 2 + 1;
            int Y = y_plane[yi];
            int U = uv_plane[ui] - 128;
            int V = uv_plane[vy] - 128;

            int r = (298 * Y + 409 * V + 128) >> 8;
            int g = (298 * Y - 100 * U - 208 * V + 128) >> 8;
            int b = (298 * Y + 516 * U + 128) >> 8;

            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;

            rgb[j * w_out + i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

// ---------------------------------------------------------------------------
// UI widgets
// ---------------------------------------------------------------------------
static lv_obj_t *g_cam_img = nullptr;
static lv_image_dsc_t g_cam_dsc;
static lv_obj_t *g_rec_btn = nullptr;
static lv_obj_t *g_rec_label = nullptr;
static lv_obj_t *g_timer_label = nullptr;
static int g_disp_w = 800, g_disp_h = 480;

static void on_record_btn_click(lv_event_t *e) {
    (void)e;
    if (recorder_is_recording()) {
        recorder_stop();
        if (g_on_rec_stop) g_on_rec_stop();
    } else {
        if (recorder_start() == 0) {
            if (g_on_rec_start) g_on_rec_start();
        }
    }
}

static void timer_update_cb(lv_timer_t *t) {
    (void)t;

    // Recording timer
    if (g_timer_label) {
        if (recorder_is_recording()) {
            int sec = recorder_get_duration_sec();
            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
            lv_label_set_text(g_timer_label, buf);
            lv_obj_clear_flag(g_timer_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_timer_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

}

static void update_button_style() {
    if (!g_rec_btn || !g_rec_label) return;
    if (recorder_is_recording()) {
        lv_obj_set_size(g_rec_btn, 56, 56);
        lv_obj_set_style_radius(g_rec_btn, 8, 0);
        lv_obj_set_style_bg_color(g_rec_btn, lv_color_make(0xFF, 0x45, 0x00), 0);
        lv_label_set_text(g_rec_label, "");
    } else {
        lv_obj_set_size(g_rec_btn, 72, 72);
        lv_obj_set_style_radius(g_rec_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_rec_btn, lv_color_make(0xFF, 0x00, 0x00), 0);
        lv_label_set_text(g_rec_label, "");
    }
}

static void create_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    // Full-screen camera preview (bottom layer)
    g_cam_img = lv_image_create(scr);
    lv_obj_set_pos(g_cam_img, 0, 0);
    lv_obj_set_size(g_cam_img, g_disp_w, g_disp_h);

    // Record button at bottom-center
    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_border_width(&btn_style, 4);
    lv_style_set_border_color(&btn_style, lv_color_make(0xFF, 0xFF, 0xFF));
    lv_style_set_shadow_width(&btn_style, 12);
    lv_style_set_shadow_color(&btn_style, lv_color_make(0xFF, 0x00, 0x00));
    lv_style_set_shadow_opa(&btn_style, LV_OPA_50);

    g_rec_btn = lv_button_create(scr);
    lv_obj_add_style(g_rec_btn, &btn_style, 0);
    lv_obj_set_size(g_rec_btn, 72, 72);
    lv_obj_set_style_radius(g_rec_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_rec_btn, lv_color_make(0xFF, 0x00, 0x00), 0);
    lv_obj_center(g_rec_btn);
    lv_obj_set_y(g_rec_btn, lv_display_get_vertical_resolution(lv_display_get_default()) / 2 - 60);
    lv_obj_add_event_cb(g_rec_btn, on_record_btn_click, LV_EVENT_CLICKED, nullptr);

    g_rec_label = lv_label_create(g_rec_btn);
    lv_label_set_text(g_rec_label, "");
    lv_obj_center(g_rec_label);

    // Timer label at top center
    g_timer_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_timer_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(g_timer_label, &lv_font_montserrat_36, 0);
    lv_label_set_text(g_timer_label, "00:00");
    lv_obj_center(g_timer_label);
    lv_obj_set_y(g_timer_label, 24);
    lv_obj_add_flag(g_timer_label, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(timer_update_cb, 500, nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static bool g_inited = false;

int lvgl_ui_init(int hor_res, int ver_res, int rotation) {
    if (g_inited) return 0;

    lv_init();

    lv_display_t *disp = lv_linux_drm_create();
    if (!disp) {
        fprintf(stderr, "lvgl: drm create failed\n");
        return -1;
    }

    char *drm_path = lv_linux_drm_find_device_path();
    if (!drm_path) {
        drm_path = strdup("/dev/dri/card0");
    }
    printf("lvgl: using DRM device %s\n", drm_path);
    if (lv_linux_drm_set_file(disp, drm_path, -1) != LV_RESULT_OK) {
        fprintf(stderr, "lvgl: drm set_file failed\n");
        lv_free(drm_path);
        return -1;
    }
    lv_free(drm_path);

    if (hor_res > 0 && ver_res > 0) {
        lv_display_set_resolution(disp, hor_res, ver_res);
    }

    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        fprintf(stderr, "lvgl: invalid rotation %d, using 0\n", rotation);
        rotation = 0;
    }
    if (rotation != 0) {
        lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
    }

    lv_display_set_default(disp);

    g_disp_w = lv_display_get_horizontal_resolution(disp);
    g_disp_h = lv_display_get_vertical_resolution(disp);
    printf("lvgl: drm display %dx%d rotation=%d\n", g_disp_w, g_disp_h, rotation);

    // Pre-allocate RGB565 buffer
    if (!g_rgb_buf) {
        g_rgb_buf = (uint8_t *)malloc(g_disp_w * g_disp_h * 2);
        g_cam_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        g_cam_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        g_cam_dsc.header.w = g_disp_w;
        g_cam_dsc.header.h = g_disp_h;
        g_cam_dsc.header.stride = g_disp_w * 2;
        g_cam_dsc.data = g_rgb_buf;
        g_cam_dsc.data_size = g_disp_w * g_disp_h * 2;
    }

    // Touch input via evdev (grabbed exclusively to prevent desktop interference)
    lv_indev_t *indev = nullptr;
    const char *evdev_candidates[] = {
        "/dev/input/touchscreen0",
        "/dev/input/event0", "/dev/input/event1", "/dev/input/event2",
        "/dev/input/event3", "/dev/input/event4", "/dev/input/event5",
        "/dev/input/event6", "/dev/input/event7",
    };
    for (auto dev : evdev_candidates) {
        int fd = open(dev, O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGRAB, 1);
        indev = lv_evdev_create_fd(LV_INDEV_TYPE_NONE, fd);
        if (indev && lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            printf("lvgl: evdev %s type=%d (not pointer), skipping\n", dev, lv_indev_get_type(indev));
            lv_indev_delete(indev);
            indev = nullptr;
            continue;
        }
        if (indev) {
            printf("lvgl: evdev opened %s (pointer, grabbed)\n", dev);
            break;
        }
        close(fd);
    }
    if (!indev) {
        fprintf(stderr, "lvgl: evdev init failed, touch disabled\n");
    }

    create_ui();



    g_inited = true;
    return 0;
}

void lvgl_ui_update_frame(const uint8_t *nv12_data, int w, int h) {
    if (!g_inited || !nv12_data) return;
    nv12_to_rgb565(nv12_data, (uint16_t *)g_rgb_buf, w, h, g_cam_rotation, g_cam_mirror_h);
    lv_image_set_src(g_cam_img, &g_cam_dsc);
}

void lvgl_ui_process() {
    if (!g_inited) return;

    static bool was_recording = false;
    bool now_recording = recorder_is_recording();
    if (now_recording != was_recording) {
        update_button_style();
        was_recording = now_recording;
    }

    lv_timer_handler();
}

void lvgl_ui_deinit() {
    g_inited = false;
}

bool lvgl_ui_is_recording() {
    return recorder_is_recording();
}

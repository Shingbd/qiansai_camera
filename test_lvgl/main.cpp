#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <atomic>

#include <fcntl.h>

extern "C" {
#include <lvgl/lvgl.h>
#include <lvgl/drivers/display/lv_linux_drm.h>
#include <lvgl/drivers/indev/lv_evdev.h>
#include <sys/ioctl.h>
#include <linux/input.h>
}

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static void create_test_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL Test - Hello!");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Rectangle
    lv_obj_t *rect = lv_obj_create(scr);
    lv_obj_set_size(rect, 200, 120);
    lv_obj_set_style_bg_color(rect, lv_color_make(0x00, 0x7A, 0xFF), 0);
    lv_obj_set_style_radius(rect, 12, 0);
    lv_obj_set_style_border_width(rect, 3, 0);
    lv_obj_set_style_border_color(rect, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(rect, LV_ALIGN_CENTER, -160, -60);

    // Label inside rectangle
    lv_obj_t *rect_label = lv_label_create(rect);
    lv_label_set_text(rect_label, "Rectangle\nR=12");
    lv_obj_set_style_text_color(rect_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(rect_label);

    // Circle (using obj with rounded corners)
    lv_obj_t *circle = lv_obj_create(scr);
    lv_obj_set_size(circle, 120, 120);
    lv_obj_set_style_bg_color(circle, lv_color_make(0xFF, 0x45, 0x00), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 3, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *circ_label = lv_label_create(circle);
    lv_label_set_text(circ_label, "Circle");
    lv_obj_set_style_text_color(circ_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(circ_label);

    // Arc (spinning indicator style)
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 120, 120);
    lv_arc_set_angles(arc, 0, 270);
    lv_obj_set_style_arc_color(arc, lv_color_make(0xFF, 0xFF, 0x00), LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_CENTER, 160, -60);

    // Colorful bar
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 300, 24);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 65, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x00, 0xFF, 0x88), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 50);

    // Slider (interactive)
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 300, 16);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 42, LV_ANIM_ON);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 100);

    // Bottom status text
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "LVGL DRM is working! Press Ctrl+C to exit");
    lv_obj_set_style_text_color(status, lv_color_hex(0x888888), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -20);

}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int rotation = 0;
    if (argc >= 2) rotation = atoi(argv[1]);
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        fprintf(stderr, "invalid rotation %d, using 0\n", rotation);
        rotation = 0;
    }

    lv_init();

    lv_display_t *disp = lv_linux_drm_create();
    if (!disp) {
        fprintf(stderr, "ERROR: lv_linux_drm_create failed\n");
        return 1;
    }

    char *drm_path = lv_linux_drm_find_device_path();
    if (!drm_path) {
        drm_path = strdup("/dev/dri/card0");
    }
    printf("DRM device: %s\n", drm_path);
    if (lv_linux_drm_set_file(disp, drm_path, -1) != LV_RESULT_OK) {
        fprintf(stderr, "ERROR: lv_linux_drm_set_file failed\n");
        lv_free(drm_path);
        return 1;
    }
    lv_free(drm_path);

    if (rotation != 0) {
        lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
    }

    lv_display_set_default(disp);

    int hor = lv_display_get_horizontal_resolution(disp);
    int ver = lv_display_get_vertical_resolution(disp);
    printf("Display: %dx%d\n", hor, ver);

    auto try_evdev = [](const char *dev) -> lv_indev_t * {
        int fd = open(dev, O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) return nullptr;
        ioctl(fd, EVIOCGRAB, 1);
        lv_indev_t *indev = lv_evdev_create_fd(LV_INDEV_TYPE_POINTER, fd);
        if (!indev) close(fd);
        else printf("Touch input: %s (grabbed)\n", dev);
        return indev;
    };
    lv_indev_t *indev = try_evdev("/dev/input/touchscreen0");
    if (!indev) indev = try_evdev("/dev/input/event0");
    if (!indev) indev = try_evdev("/dev/input/event1");
    if (!indev) indev = try_evdev("/dev/input/event2");
    if (!indev) {
        printf("Warning: no touch input found\n");
    }

    create_test_ui();
    printf("Test UI created. Running...\n");

    while (g_running) {
        lv_timer_handler();
        usleep(5000);
    }

    printf("Shutting down...\n");
    return 0;
}

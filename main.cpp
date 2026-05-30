#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdio>

#include "retinaface.h"
#include "image_utils.h"
#include "image_drawing.h"
#include "serial_port.h"
#include "lvgl_ui.h"
#include "recorder.h"

rknn_app_context_t g_rknn_ctx;
int g_serial_fd = -1;
std::atomic<bool> g_running{true};

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    auto loop = static_cast<GMainLoop *>(data);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *err;
            gst_message_parse_error(msg, &err, &debug);
            std::cerr << "ERROR: " << err->message << std::endl;
            if (debug) std::cerr << "  debug: " << debug << std::endl;
            g_error_free(err);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <rknn_model_path> [serial_device]\n", argv[0]);
        return -1;
    }

    gst_init(&argc, &argv);

    const char *model_path = argv[1];
    const char *serial_dev = (argc == 3) ? argv[2] : "/dev/ttyS3";
    memset(&g_rknn_ctx, 0, sizeof(rknn_app_context_t));
    int ret = init_retinaface_model(model_path, &g_rknn_ctx);
    if (ret != 0) {
        std::cerr << "Failed to init RetinaFace model: " << model_path << std::endl;
        return 1;
    }
    std::cout << "RetinaFace model loaded: " << model_path << std::endl;

    g_serial_fd = serial_init(serial_dev, 115200);
    if (g_serial_fd < 0) {
        std::cerr << "Warning: serial port init failed, continuing without serial" << std::endl;
    }

    // Init recorder
    recorder_init("/tmp");

    // Init LVGL UI (takes over DRM display, replacing kmssink)
    lvgl_ui_init(0, 0, 0);

    const std::string cam_pipe =
        "v4l2src device=/dev/video0 do-timestamp=true ! "
        "video/x-raw,width=800,height=480,framerate=30/1 ! "
        "videoconvert ! video/x-raw,format=NV12 ! "
        "appsink name=sink_ai max-buffers=1 drop=true";

    std::cout << "Camera pipeline: " << cam_pipe << std::endl;

    GError *err = nullptr;
    GstElement *cam_pipeline = gst_parse_launch(cam_pipe.c_str(), &err);
    if (!cam_pipeline) {
        std::cerr << "Failed to create camera pipeline: " << err->message << std::endl;
        g_error_free(err);
        return 1;
    }

    GstElement *sink_ai = gst_bin_get_by_name(GST_BIN(cam_pipeline), "sink_ai");

    auto loop = g_main_loop_new(nullptr, FALSE);

    auto bus = gst_pipeline_get_bus(GST_PIPELINE(cam_pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_element_set_state(cam_pipeline, GST_STATE_PLAYING);

    std::thread([sink_ai]() {
        int frame_count = 0;
        auto last_time = std::chrono::steady_clock::now();
        int w = 0, h = 0, fps = 0;

        while (g_running) {
            GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink_ai));
            if (!sample) { std::cerr << "ai: no sample" << std::endl; break; }

            if (w == 0) {
                GstCaps *caps = gst_sample_get_caps(sample);
                if (caps) {
                    GstStructure *s = gst_caps_get_structure(caps, 0);
                    if (s) {
                        gst_structure_get_int(s, "width", &w);
                        gst_structure_get_int(s, "height", &h);
                        std::cout << "Camera frame: " << w << "x" << h << std::endl;
                    }
                    gst_caps_unref(caps);
                }
            }

            GstBuffer *buf = gst_sample_get_buffer(sample);
            gst_buffer_ref(buf);
            gst_sample_unref(sample);
            buf = gst_buffer_make_writable(buf);

            GstMapInfo info;
            gst_buffer_map(buf, &info, (GstMapFlags)(GST_MAP_READ | GST_MAP_WRITE));

            image_buffer_t src_img;
            memset(&src_img, 0, sizeof(image_buffer_t));
            src_img.width = w;
            src_img.height = h;
            src_img.width_stride = w;
            src_img.height_stride = h;
            src_img.format = IMAGE_FORMAT_YUV420SP_NV12;
            src_img.virt_addr = info.data;
            src_img.size = info.size;

            auto t1 = std::chrono::steady_clock::now();

            retinaface_result result;
            memset(&result, 0, sizeof(retinaface_result));
            int infer_ret = inference_retinaface_model(&g_rknn_ctx, &src_img, &result);

            auto t2 = std::chrono::steady_clock::now();
            int infer_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

            int16_t x_diff = 0, y_diff = 0;
            if (infer_ret == 0) {
                int best_idx = -1;
                float best_score = 0.0f;

                for (int i = 0; i < result.count; ++i) {
                    int rx = result.object[i].box.left;
                    int ry = result.object[i].box.top;
                    int rw = result.object[i].box.right - result.object[i].box.left;
                    int rh = result.object[i].box.bottom - result.object[i].box.top;
                    draw_rectangle(&src_img, rx, ry, rw, rh, COLOR_GREEN, 3);
                    char score_text[20];
                    snprintf(score_text, 20, "%0.2f", result.object[i].score);
                    draw_text(&src_img, score_text, rx, ry, COLOR_RED, 20);
                    for (int j = 0; j < 5; j++) {
                        draw_circle(&src_img, result.object[i].ponit[j].x, result.object[i].ponit[j].y, 2, COLOR_ORANGE, 4);
                    }

                    if (result.object[i].score > best_score) {
                        best_score = result.object[i].score;
                        best_idx = i;
                    }
                }

                if (best_idx >= 0) {
                    int cx = (result.object[best_idx].box.left + result.object[best_idx].box.right) / 2;
                    int cy = (result.object[best_idx].box.top  + result.object[best_idx].box.bottom) / 2;
                    x_diff = (int16_t)(cx - w / 2);
                    y_diff = (int16_t)(cy - h / 2);
                    char diff_text[32];
                    snprintf(diff_text, 32, "%d,%d", x_diff, y_diff);
                    draw_text(&src_img, diff_text,
                              result.object[best_idx].box.right,
                              result.object[best_idx].box.top,
                              COLOR_YELLOW, 20);
                }
            }

            if (g_serial_fd >= 0) {
                serial_send_packet(g_serial_fd, x_diff, y_diff);
                uint8_t rx_buf[SERIAL_PACKET_SIZE];
                int n = 0;
                while (n < SERIAL_PACKET_SIZE) {
                    int r = serial_read_byte(g_serial_fd, &rx_buf[n]);
                    if (r > 0) n++;
                    else break;
                }
                if (n == SERIAL_PACKET_SIZE) {
                    serial_packet_t *pkt = (serial_packet_t *)rx_buf;
                    printf("SERIAL TX: %02X %02X %d %d %02X %02X\n",
                           pkt->header[0], pkt->header[1],
                           pkt->x_diff, pkt->y_diff,
                           pkt->checksum, pkt->footer);
                }
            }

            // Show camera frame in LVGL (converts NV12 → RGB565 internally)
            lvgl_ui_update_frame(info.data, w, h);
            // Process LVGL: touch handling, timer, UI refresh
            lvgl_ui_process();

            // Push to recorder if recording
            if (lvgl_ui_is_recording()) {
                recorder_push_frame(info.data, w, h);
            }

            gst_buffer_unmap(buf, &info);
            gst_buffer_unref(buf);

            frame_count++;
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count() / 1000.f;
            if (elapsed >= 5.0f) {
                fps = (int)(frame_count / elapsed);
                std::cout << "RetinaFace: " << w << "x" << h
                          << " | infer=" << infer_ms << "ms"
                          << " | faces=" << result.count
                          << " | FPS=" << fps << std::endl;
                frame_count = 0;
                last_time = now;
            }
        }
    }).detach();

    g_main_loop_run(loop);

    g_running = false;
    gst_element_set_state(cam_pipeline, GST_STATE_NULL);
    gst_object_unref(cam_pipeline);
    g_main_loop_unref(loop);

    recorder_deinit();
    lvgl_ui_deinit();
    serial_close(g_serial_fd);
    release_retinaface_model(&g_rknn_ctx);

    return 0;
}

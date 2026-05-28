#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdio>

#include "retinaface.h"
#include "image_utils.h"
#include "image_drawing.h"

rknn_app_context_t g_rknn_ctx;
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
    if (argc != 2) {
        printf("Usage: %s <rknn_model_path>\n", argv[0]);
        return -1;
    }

    gst_init(&argc, &argv);

    const char *model_path = argv[1];
    memset(&g_rknn_ctx, 0, sizeof(rknn_app_context_t));
    int ret = init_retinaface_model(model_path, &g_rknn_ctx);
    if (ret != 0) {
        std::cerr << "Failed to init RetinaFace model: " << model_path << std::endl;
        return 1;
    }
    std::cout << "RetinaFace model loaded: " << model_path << std::endl;

    const std::string cam_pipe =
        "v4l2src device=/dev/video0 do-timestamp=true ! "
        "video/x-raw,width=800,height=480,framerate=30/1 ! "
        "videoconvert ! video/x-raw,format=NV12 ! "
        "appsink name=sink_ai max-buffers=1 drop=true";

    const std::string display_pipe =
        "appsrc name=local_src is-live=true format=time ! "
        "videoconvert ! videoflip method=clockwise ! videoflip method=rotate-180 ! queue ! kmssink sync=false";

    std::cout << "Camera pipeline: " << cam_pipe << std::endl;

    GError *err = nullptr;
    GstElement *cam_pipeline = gst_parse_launch(cam_pipe.c_str(), &err);
    if (!cam_pipeline) {
        std::cerr << "Failed to create camera pipeline: " << err->message << std::endl;
        g_error_free(err);
        return 1;
    }

    GstElement *display_pipeline = gst_parse_launch(display_pipe.c_str(), &err);
    if (!display_pipeline) {
        std::cerr << "Failed to create display pipeline: " << err->message << std::endl;
        g_error_free(err);
        return 1;
    }

    GstElement *sink_ai = gst_bin_get_by_name(GST_BIN(cam_pipeline), "sink_ai");
    GstElement *local_src = gst_bin_get_by_name(GST_BIN(display_pipeline), "local_src");

    auto loop = g_main_loop_new(nullptr, FALSE);

    auto bus1 = gst_pipeline_get_bus(GST_PIPELINE(cam_pipeline));
    gst_bus_add_watch(bus1, bus_call, loop);
    gst_object_unref(bus1);

    auto bus2 = gst_pipeline_get_bus(GST_PIPELINE(display_pipeline));
    gst_bus_add_watch(bus2, bus_call, loop);
    gst_object_unref(bus2);

    gst_element_set_state(cam_pipeline, GST_STATE_PLAYING);
    gst_element_set_state(display_pipeline, GST_STATE_PLAYING);

    std::thread([sink_ai, local_src]() {
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
                if (local_src) {
                    GstCaps *src_caps = gst_caps_new_simple("video/x-raw",
                        "format", G_TYPE_STRING, "NV12",
                        "width", G_TYPE_INT, w,
                        "height", G_TYPE_INT, h,
                        NULL);
                    gst_app_src_set_caps(GST_APP_SRC(local_src), src_caps);
                    gst_caps_unref(src_caps);
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

            if (infer_ret == 0) {
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
                }
            }

            {
                char fps_text[32];
                snprintf(fps_text, 32, "FPS: %d", fps);
                draw_text(&src_img, fps_text, 8, 8, COLOR_GREEN, 24);
            }

            gst_buffer_unmap(buf, &info);

            if (local_src) {
                gst_app_src_push_buffer(GST_APP_SRC(local_src), buf);
            } else {
                gst_buffer_unref(buf);
            }

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
    gst_element_set_state(display_pipeline, GST_STATE_NULL);
    gst_object_unref(cam_pipeline);
    gst_object_unref(display_pipeline);
    g_main_loop_unref(loop);

    release_retinaface_model(&g_rknn_ctx);

    return 0;
}

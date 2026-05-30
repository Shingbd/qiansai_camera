#include "photo.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <cstdio>
#include <ctime>
#include <cstring>
#include <chrono>

std::string photo_capture(const uint8_t *nv12, int w, int h, const char *output_dir) {
    if (!nv12 || w <= 0 || h <= 0) return {};

    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/photo_%04d%02d%02d_%02d%02d%02d.jpg",
             output_dir, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    std::string desc = "appsrc name=s ! videoconvert ! jpegenc ! filesink location=" + std::string(filename);
    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(desc.c_str(), &err);
    if (!pipeline) {
        fprintf(stderr, "photo: pipeline create failed: %s\n", err->message);
        g_error_free(err);
        return {};
    }

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "s");
    if (!src) {
        fprintf(stderr, "photo: no appsrc\n");
        gst_object_unref(pipeline);
        return {};
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, w,
        "height", G_TYPE_INT, h,
        "framerate", GST_TYPE_FRACTION, 30, 1, nullptr);
    gst_app_src_set_caps(GST_APP_SRC(src), caps);
    gst_caps_unref(caps);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    int size = w * h * 3 / 2;
    GstBuffer *buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    GstMapInfo map;
    gst_buffer_map(buf, &map, GST_MAP_WRITE);
    memcpy(map.data, nv12, size);
    gst_buffer_unmap(buf, &map);

    GST_BUFFER_PTS(buf) = 0;
    gst_app_src_push_buffer(GST_APP_SRC(src), buf);
    gst_app_src_end_of_stream(GST_APP_SRC(src));

    // Wait for EOS (or error)
    GstBus *bus = gst_element_get_bus(pipeline);
    if (bus) {
        gst_bus_timed_pop_filtered(bus, GST_SECOND * 3,
            (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        gst_object_unref(bus);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(src);
    gst_object_unref(pipeline);

    printf("Photo saved: %s (%dx%d)\n", filename, w, h);
    return filename;
}

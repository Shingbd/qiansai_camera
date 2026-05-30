#include "recorder.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <atomic>
#include <chrono>

static std::mutex g_mutex;
static GstElement *g_pipeline = nullptr;
static GstElement *g_appsrc = nullptr;
static std::atomic<bool> g_recording{false};
static std::atomic<int> g_frame_count{0};
static std::chrono::steady_clock::time_point g_start_time;
static uint64_t g_pts = 0;
static std::string g_output_dir;
static std::function<void(const char *)> g_error_cb;

static char g_filename[256];

static void gen_filename() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    snprintf(g_filename, sizeof(g_filename), "%s/rec_%04d%02d%02d_%02d%02d%02d.mp4",
             g_output_dir.c_str(),
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

static void need_data_cb(GstAppSrc *src, guint length, gpointer) {
    (void)src; (void)length;
}

static gboolean seek_data_cb(GstAppSrc *src, guint64 offset, gpointer) {
    return FALSE;
}

int recorder_init(const char *output_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_output_dir = output_dir ? output_dir : "/tmp";
    return 0;
}

void recorder_deinit() {
    recorder_stop();
}

int recorder_start() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_recording) return 0;

    gen_filename();
    printf("Recording to: %s\n", g_filename);

    std::string desc =
        "appsrc name=rec_src is-live=true format=time "
        "! videoconvert ! video/x-raw,format=I420 "
        "! mpph264enc name=enc ! h264parse ! mp4mux ! filesink name=fsink";

    GError *err = nullptr;
    g_pipeline = gst_parse_launch(desc.c_str(), &err);
    if (!g_pipeline) {
        fprintf(stderr, "recorder: failed to create pipeline: %s\n", err->message);
        g_error_free(err);
        return -1;
    }

    g_appsrc = gst_bin_get_by_name(GST_BIN(g_pipeline), "rec_src");
    if (!g_appsrc) {
        fprintf(stderr, "recorder: no appsrc\n");
        gst_object_unref(g_pipeline);
        g_pipeline = nullptr;
        return -1;
    }

    GstElement *fsink = gst_bin_get_by_name(GST_BIN(g_pipeline), "fsink");
    if (fsink) {
        g_object_set(fsink, "location", g_filename, nullptr);
        gst_object_unref(fsink);
    }

    GstAppSrcCallbacks cbs = {};
    cbs.need_data = need_data_cb;
    cbs.seek_data = seek_data_cb;
    gst_app_src_set_callbacks(GST_APP_SRC(g_appsrc), &cbs, nullptr, nullptr);

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, 800,
        "height", G_TYPE_INT, 480,
        "framerate", GST_TYPE_FRACTION, 30, 1, nullptr);
    gst_app_src_set_caps(GST_APP_SRC(g_appsrc), caps);
    gst_caps_unref(caps);

    gst_element_set_state(g_pipeline, GST_STATE_PLAYING);
    g_recording = true;
    g_frame_count = 0;
    g_pts = 0;
    g_start_time = std::chrono::steady_clock::now();
    return 0;
}

void recorder_stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_recording) return;

    g_recording = false;

    if (g_appsrc) {
        gst_app_src_end_of_stream(GST_APP_SRC(g_appsrc));
        gst_object_unref(g_appsrc);
        g_appsrc = nullptr;
    }

    if (g_pipeline) {
        GstBus *bus = gst_element_get_bus(g_pipeline);
        if (bus) {
            while (gst_bus_have_pending(bus))
                gst_bus_pop(bus);
            gst_object_unref(bus);
        }

        gst_element_send_event(g_pipeline, gst_event_new_eos());
        gst_element_set_state(g_pipeline, GST_STATE_NULL);
        gst_object_unref(g_pipeline);
        g_pipeline = nullptr;
    }

    int sec = recorder_get_duration_sec();
    int frames = g_frame_count.load();
    printf("Recording saved: %s (%d frames, %d sec)\n", g_filename, frames, sec);
}

bool recorder_is_recording() {
    return g_recording.load();
}

int recorder_get_duration_sec() {
    if (!g_recording) return 0;
    auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::seconds>(now - g_start_time).count();
}

void recorder_push_frame(const uint8_t *nv12_data, int width, int height) {
    if (!g_recording) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_appsrc || !g_pipeline) return;

    int size = width * height * 3 / 2;
    GstBuffer *buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    GstMapInfo map;
    gst_buffer_map(buf, &map, GST_MAP_WRITE);
    memcpy(map.data, nv12_data, size);
    gst_buffer_unmap(buf, &map);

    GST_BUFFER_PTS(buf) = g_pts;
    GST_BUFFER_DURATION(buf) = GST_SECOND / 30;
    g_pts += GST_SECOND / 30;

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(g_appsrc), buf);
    if (ret != GST_FLOW_OK) {
        fprintf(stderr, "recorder: push buffer failed %d\n", ret);
    }
    g_frame_count++;
}

void recorder_set_error_cb(std::function<void(const char *)> cb) {
    g_error_cb = std::move(cb);
}

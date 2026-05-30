#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_DPI_DEF 130
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LINUX_DRM 1
#if LV_USE_LINUX_DRM
#  define LV_USE_LINUX_DRM_GBM_BUFFERS 0
#endif

#define LV_USE_EVDEV 1

#define LV_USE_LOG 1
#define LV_USE_PERF_MONITOR 0

#define LV_BUILD_EXAMPLES 0

#endif

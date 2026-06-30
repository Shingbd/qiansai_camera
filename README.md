# 蹁跹 (Qiānxiān)

面向 Rockchip RK3588 平台的嵌入式摄像头应用，支持实时人脸检测与跟踪、视频录制、拍照、以及通过串口控制云台。

## 功能

- **人脸检测与跟踪** — 基于 RetinaFace 模型，通过 RK3588 NPU (RKNN) 硬件加速推理，实时检测人脸并计算偏移量
- **串口云台控制** — 通过 `/dev/ttyS3` (115200bps) 发送人脸中心坐标偏移量，控制外接云台/舵机对准人脸
- **LVGL 图形界面** — 基于 LVGL v9 + Linux DRM 直驱显示，evdev 触摸交互，支持摄像头预览、录制/拍照按钮、录制计时器
- **视频录制** — 通过 GStreamer 管线将 NV12 帧编码为 H.264 MP4 (`appsrc → mpph264enc → mp4mux`)
- **拍照** — 通过 GStreamer 将 NV12 帧编码为 JPEG (`appsrc → jpegenc`)

## 技术栈

| 层面 | 技术 |
|------|------|
| 语言 | C++17, C |
| 构建 | CMake ≥ 3.16 |
| GUI | LVGL v9 (DRM 后端, evdev 输入) |
| 多媒体 | GStreamer 1.0 (v4l2src, mpph264enc, jpegenc) |
| AI 推理 | RKNN (Rockchip NPU 运行时) |
| 串口 | Linux termios, 115200bps |
| 目标平台 | Rockchip RK3588 (ARM64 Linux) |

## 硬件需求

- Rockchip RK3588 开发板/设备
- V4L2 摄像头 (`/dev/video0`)，推荐 800×480 NV12 @ 30fps
- LCD 屏幕 (DRM `/dev/dri/card0`)，支持触摸 (evdev)
- 串口云台/舵机 (`/dev/ttyS3`，可选)

## 快速开始

### 依赖安装

```bash
sudo apt install cmake build-essential pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  librga-dev librknpu-dev
```

### 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTARGET_SOC=rk3588 -DENABLE_LVGL_UI=ON
cmake --build build -j$(nproc)
```

### 运行

```bash
sudo chvt 3                          # 切换到空闲虚拟终端
sudo ./build/qiansai retinaface.rknn [/dev/ttyS3]
```

参数：
- 参数 1（必填）：RKNN 模型文件路径
- 参数 2（可选）：串口设备，默认 `/dev/ttyS3`

环境变量：
- `LVGL_ROTATION` — LVGL 显示旋转角度，可选 `0` `90` `180` `270`

## 配置

| 机制 | 用途 |
|------|------|
| CMake 选项 | `TARGET_SOC` (SoC 型号), `ENABLE_LVGL_UI` (启用/禁用界面) |
| `lv_conf.h` | LVGL 颜色深度、DRM、evdev 等配置 |
| 环境变量 | `LVGL_ROTATION` 运行时屏幕旋转 |

## 人脸跟踪协议

串口以 8 字节定长包格式发送数据：

```
 0xAA  0x55  x_diff(L)  x_diff(H)  y_diff(L)  y_diff(H)  checksum  0x0D
```

- `x_diff`, `y_diff`: 人脸中心与画面中心的像素偏移量 (16 位有符号整数，小端序)
- `checksum`: 除包头外所有字节的累加和 (8 位)
- `0x0D`: 包尾

## 项目结构

```
├── CMakeLists.txt          # 构建配置
├── lv_conf.h               # LVGL 配置
├── main.cpp                # 入口：初始化各模块及主循环
├── lvgl_ui.cpp / .h        # LVGL 界面 (摄像头预览、按钮、计时器)
├── recorder.cpp / .h       # GStreamer 视频录制
├── photo.cpp / .h          # GStreamer 拍照
├── serial_port.cpp / .h    # 串口通信
├── 3rdparty/
│   ├── lvgl/               # LVGL v9 源码
│   └── rknn_model_zoo/     # RKNN 模型仓库
└── test_lvgl/              # LVGL 独立测试项目
```

## 许可

MIT

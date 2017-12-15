/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#ifndef COMMON_CORE_NESTEDDISPLAY_H_
#define COMMON_CORE_NESTEDDISPLAY_H_

#include <nativedisplay.h>
#include <stdlib.h>
#include <stdint.h>

#include <linux/hyper_dmabuf.h>
#include <memory>
#include <map>
#include "drmbuffer.h"
#include <utils/threads.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "hwcthread.h"
#include "hwctrace.h"
#include <nativedisplay.h>
#define SURFACE_NAME_LENGTH    64

namespace hwcomposer {

struct HwcLayer;
class NativeBufferHandler;
class NestedDisplayManager;

struct vm_header {
  int32_t version;
  int32_t output;
  int32_t counter;
  int32_t n_buffers;
  int32_t disp_w;
  int32_t disp_h;
};

struct vm_buffer_info {
  int32_t width, height;
  int32_t format;
  int32_t pitch[3];
  int32_t offset[3];
  int32_t bpp;
  int32_t tile_format;
  int32_t rotation;
  int32_t status;
  int32_t counter;
  union {
    hyper_dmabuf_id_t hyper_dmabuf_id;
    unsigned long ggtt_offset;
  };
  char surface_name[SURFACE_NAME_LENGTH];
  uint64_t surface_id;
  int32_t bbox[4];
};

class SocketThread: public HWCThread {

public:
  SocketThread( int* client,bool* connection, int server):HWCThread(-8, "SocketThread") {
    client_sock_fd = client;
    sock_fd = server;
    connected = connection;
    mEnabled = true;
  }
  virtual ~SocketThread() {}
  void Initialize() {
    if (InitWorker())
      Resume();
    else
      ETRACE("Failed to initalize CompositorThread. %s", PRINTERROR());
  }

  void setEnabled(bool enabled) {
    if (mEnabled != enabled) {
      mEnabled = enabled;
      Resume();
    }
  }

  void HandleRoutine() override {
    socklen_t clilen;
    struct sockaddr_in client_addr;

    if (sock_fd >= 0) {
      clilen = sizeof(client_addr);
      *connected = false;
      *client_sock_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &clilen);
      mEnabled = false;
      *connected = true;
    }
  }
private:
  bool mEnabled;
  int* client_sock_fd;
  bool* connected;
  int  sock_fd;
};

class NestedDisplay : public NativeDisplay {
 public:
  NestedDisplay(NativeBufferHandler *buffer_handler);
  ~NestedDisplay() override;

  void InitNestedDisplay() override;

  bool Initialize(NativeBufferHandler *buffer_handler) override;

  DisplayType Type() const override {
    return DisplayType::kNested;
  }

  uint32_t Width() const override {
    return width_;
  }

  uint32_t Height() const override {
    return height_;
  }

  uint32_t PowerMode() const override {
    return 0;
  }

  uint32_t bitsPerPixel(int format) {
    switch (format) {
      case HAL_PIXEL_FORMAT_RGBA_8888:
      case HAL_PIXEL_FORMAT_RGBX_8888:
      case HAL_PIXEL_FORMAT_BGRA_8888:
        return 32;
      case HAL_PIXEL_FORMAT_RGB_888:
        return 24;
      case HAL_PIXEL_FORMAT_RGB_565:
        return 16;
    }
    return 0;
  }

  int GetDisplayPipe() override;
  bool SetActiveConfig(uint32_t config) override;
  bool GetActiveConfig(uint32_t *config) override;

  bool SetPowerMode(uint32_t power_mode) override;

  bool Present(std::vector<HwcLayer *> &source_layers, int32_t *retire_fence,
               bool handle_constraints = false) override;

  int RegisterVsyncCallback(std::shared_ptr<VsyncCallback> callback,
                            uint32_t display_id) override;

  void RegisterRefreshCallback(std::shared_ptr<RefreshCallback> callback,
                               uint32_t display_id) override;

  void RegisterHotPlugCallback(std::shared_ptr<HotPlugCallback> callback,
                               uint32_t display_id) override;

  void VSyncControl(bool enabled) override;
  bool CheckPlaneFormat(uint32_t format) override;
  void SetGamma(float red, float green, float blue) override;
  void SetContrast(uint32_t red, uint32_t green, uint32_t blue) override;
  void SetBrightness(uint32_t red, uint32_t green, uint32_t blue) override;
  void SetExplicitSyncSupport(bool disable_explicit_sync) override;

  bool IsConnected() const override;

  void UpdateScalingRatio(uint32_t primary_width, uint32_t primary_height,
                          uint32_t display_width,
                          uint32_t display_height) override;

  void CloneDisplay(NativeDisplay *source_display) override;

  bool PresentClone(std::vector<HwcLayer *> &source_layers,
                    int32_t *retire_fence, bool idle_frame) override;

  bool GetDisplayAttribute(uint32_t /*config*/, HWCDisplayAttribute attribute,
                           int32_t *value) override;

  bool GetDisplayConfigs(uint32_t *num_configs, uint32_t *configs) override;
  bool GetDisplayName(uint32_t *size, char *name) override;

  bool EnableVSync() const {
    return enable_vsync_;
  }

  void VSyncUpdate(int64_t timestamp);

  void RefreshUpdate();

  void HotPlugUpdate(bool connected);
  int start_sock_service();
  int hyper_communication_network_send_data(void *data, int len);
  static void signal_callback_handler(int signum);

 private:
  std::shared_ptr<RefreshCallback> refresh_callback_ = NULL;
  std::shared_ptr<VsyncCallback> vsync_callback_ = NULL;
  std::shared_ptr<HotPlugCallback> hotplug_callback_ = NULL;
  uint32_t display_id_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool enable_vsync_ = false;
  uint32_t config_ = 1;

  NativeBufferHandler *buffer_handler_;

  int mHyperDmaBuf_Fd = -1;
  std::map<HWCNativeHandle, vm_buffer_info> mHyperDmaExportedBuffers;   // Track the  and hyper dmabuf metadata info mapping
  static std::unique_ptr<SocketThread> st_;
  int msock_fd = -1;
  static int mclient_sock_fd;
  bool mconnected = false;
};

}  // namespace hwcomposer
#endif  // COMMON_CORE_NESTEDDISPLAY_H_

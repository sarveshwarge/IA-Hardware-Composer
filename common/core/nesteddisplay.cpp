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

#include "nesteddisplay.h"

#include <nativebufferhandler.h>

#include <string>
#include <sstream>

#include <hwctrace.h>
#include <hwclayer.h>
#include "utils_android.h"
#include <sys/socket.h>
#include <netinet/in.h>

namespace hwcomposer {
#define METADATA_BUFFER_SIZE 12000
#define METADATA_STREAM_START 0xF00D
#define METADATA_STREAM_END 0xCAFE

static char buf[METADATA_BUFFER_SIZE];

#define HYPER_DMABUF_PATH "/dev/hyper_dmabuf"
std::unique_ptr<SocketThread> NestedDisplay::st_ = NULL;
int NestedDisplay::mclient_sock_fd = -1;

NestedDisplay::NestedDisplay(NativeBufferHandler *buffer_handler) {
#ifdef NESTED_DISPLAY_SUPPORT
  width_ = 1920;
  height_ = 1080;

  buffer_handler_ = buffer_handler;
  struct ioctl_hyper_dmabuf_tx_ch_setup msg;
  int ret;

  mHyperDmaBuf_Fd = open(HYPER_DMABUF_PATH, O_RDWR);
  if (mHyperDmaBuf_Fd < 0)
    ETRACE("Hyper DmaBuf: open hyper dmabuf device node %s failed because %s", HYPER_DMABUF_PATH, strerror(errno));
  else {
    ETRACE("Hyper DmaBuf: open hyper dmabuf device node %s successfully!", HYPER_DMABUF_PATH);
    /* TODO: add config option to specify which domains should be used, for now we share always with dom0 */
    msg.remote_domain = 0;
    ret = ioctl(mHyperDmaBuf_Fd, IOCTL_HYPER_DMABUF_TX_CH_SETUP, &msg);
    if (ret) {
      ETRACE("Hyper DmaBuf: IOCTL_HYPER_DMABUF_TX_CH_SETUP failed with error %d\n", ret);
      close(mHyperDmaBuf_Fd);
      mHyperDmaBuf_Fd = -1;
    }
    else
      ETRACE("Hyper DmaBuf: IOCTL_HYPER_DMABUF_TX_CH_SETUP Done!\n");
  }

  start_sock_service();
  st_.reset(new SocketThread(&mclient_sock_fd,&mconnected,msock_fd));
  st_->Initialize();
#endif
}

NestedDisplay::~NestedDisplay() {
#ifdef NESTED_DISPLAY_SUPPORT
  if(mHyperDmaBuf_Fd > 0){
    for (auto it=mHyperDmaExportedBuffers.begin();it!=mHyperDmaExportedBuffers.end();++it) {
      struct ioctl_hyper_dmabuf_unexport msg;
      int ret;
      msg.hid = it->second.hyper_dmabuf_id;
      //Todo: hyper dmabuf free delay is fixed to 1s now!
      msg.delay_ms = 1000;
      ret = ioctl(mHyperDmaBuf_Fd, IOCTL_HYPER_DMABUF_UNEXPORT, &msg);
      if (ret) {
        ETRACE("Hyper DmaBuf: IOCTL_HYPER_DMABUF_UNEXPORT ioctl failed %d [0x%x]\n", ret, it->second.hyper_dmabuf_id.id);
      } else {
        ETRACE("Hyper DmaBuf: IOCTL_HYPER_DMABUF_UNEXPORT ioctl Done [0x%x]!\n",it->second.hyper_dmabuf_id.id);
        mHyperDmaExportedBuffers.erase(it);
      }

    }

    close(mHyperDmaBuf_Fd);
    mHyperDmaBuf_Fd = -1;
  }

  if (mclient_sock_fd >= 0)
    close(mclient_sock_fd);

  if (msock_fd >= 0)
    close(msock_fd);
#endif
}

void NestedDisplay::InitNestedDisplay() {
}

bool NestedDisplay::Initialize(NativeBufferHandler * /*buffer_handler*/) {
  return true;
}

bool NestedDisplay::IsConnected() const {
  return true;
}

int NestedDisplay::GetDisplayPipe() {
  return -1;
}

bool NestedDisplay::SetActiveConfig(uint32_t config) {
  config_ = config;
  return true;
}

bool NestedDisplay::GetActiveConfig(uint32_t *config) {
  if (!config)
    return false;

  config[0] = 0;
  return true;
}

bool NestedDisplay::SetPowerMode(uint32_t /*power_mode*/) {
  return true;
}

bool NestedDisplay::Present(std::vector<HwcLayer *> &source_layers,
                            int32_t * /*retire_fence*/,
                            bool /*handle_constraints*/) {

  int ret = 0;
  size_t size = source_layers.size();
  const uint32_t* pitches;
  const uint32_t* offsets;
  HWCNativeHandle sf_handle;
  size_t buffer_number = 0;
  size_t info_size = sizeof(vm_buffer_info);
  size_t header_size = sizeof(vm_header);

  for (size_t layer_index = 0; layer_index < size; layer_index++) {
    HwcLayer* layer = source_layers.at(layer_index);
    if (!layer->IsVisible())
      continue;

    HwcRect<int> display_frame = layer->GetDisplayFrame();
    sf_handle = layer->GetNativeHandle();
    auto search = mHyperDmaExportedBuffers.find(sf_handle);
    if(search == mHyperDmaExportedBuffers.end()){
      DrmBuffer* buffer = new DrmBuffer();
      buffer->InitializeFromNativeHandle(sf_handle, buffer_handler_);

      if(mHyperDmaBuf_Fd > 0 && buffer->GetPrimeFD()> 0){
        struct ioctl_hyper_dmabuf_export_remote msg;
        memset(&msg,0,sizeof(ioctl_hyper_dmabuf_export_remote));

        /* TODO: add more flexibility here, instead of hardcoded domain 0*/
        msg.remote_domain = 0;
        msg.dmabuf_fd = buffer->GetPrimeFD();

        ret = ioctl(mHyperDmaBuf_Fd, IOCTL_HYPER_DMABUF_EXPORT_REMOTE, &msg);
        if (ret) {
          ETRACE("Hyper DmaBuf: Exporting hyper_dmabuf failed with error %d\n", ret);
          return false;
        }
        else {
          ETRACE("Hyper DmaBuf: Exporting hyper_dmabuf Done! 0x%x\n", msg.hid.id);
          uint32_t HalFormat = DrmFormatToHALFormat(buffer->GetFormat());

          mHyperDmaExportedBuffers[sf_handle].width = buffer->GetWidth();
          mHyperDmaExportedBuffers[sf_handle].height = buffer->GetHeight();
          switch (HalFormat) {
            case HAL_PIXEL_FORMAT_RGBX_8888:
            case HAL_PIXEL_FORMAT_RGB_888:
            case HAL_PIXEL_FORMAT_RGB_565:
              mHyperDmaExportedBuffers[sf_handle].format = EGL_TEXTURE_RGB;
              break;
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_BGRA_8888:
              mHyperDmaExportedBuffers[sf_handle].format = EGL_TEXTURE_RGBA;
              break;
            case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
              mHyperDmaExportedBuffers[sf_handle].format = 0x31D8;        //this wayland specified format, hardcode here.
              break;
          }
          pitches = buffer->GetPitches();
          offsets = buffer->GetOffsets();
          mHyperDmaExportedBuffers[sf_handle].pitch[0] = pitches[0];
          mHyperDmaExportedBuffers[sf_handle].pitch[1] = pitches[1];
          mHyperDmaExportedBuffers[sf_handle].pitch[2] = pitches[2];
          mHyperDmaExportedBuffers[sf_handle].offset[0] = offsets[0];
          mHyperDmaExportedBuffers[sf_handle].offset[1] = offsets[1];
          mHyperDmaExportedBuffers[sf_handle].offset[2] = offsets[2];
          mHyperDmaExportedBuffers[sf_handle].bpp = bitsPerPixel(HalFormat);
          mHyperDmaExportedBuffers[sf_handle].tile_format = buffer->GetTilingMode();
          mHyperDmaExportedBuffers[sf_handle].rotation = 0;;
          mHyperDmaExportedBuffers[sf_handle].status = 0;
          mHyperDmaExportedBuffers[sf_handle].counter = 0;
          mHyperDmaExportedBuffers[sf_handle].hyper_dmabuf_id = msg.hid;
          mHyperDmaExportedBuffers[sf_handle].surface_id = (uint64_t)sf_handle;
          strncpy(mHyperDmaExportedBuffers[sf_handle].surface_name,"Cluster",SURFACE_NAME_LENGTH);
          mHyperDmaExportedBuffers[sf_handle].bbox[0] = display_frame.left;
          mHyperDmaExportedBuffers[sf_handle].bbox[1] = display_frame.top;
          mHyperDmaExportedBuffers[sf_handle].bbox[2] = buffer->GetWidth();
          mHyperDmaExportedBuffers[sf_handle].bbox[3] = buffer->GetHeight();
        }
      }
    }
    memcpy(buf+sizeof(int)+header_size+info_size*buffer_number,&mHyperDmaExportedBuffers[sf_handle],info_size);
    buffer_number++;
  }
  int* stream_start = reinterpret_cast<int*>(buf);
  *stream_start = METADATA_STREAM_START;
  vm_header* header = reinterpret_cast<vm_header*>(buf+sizeof(int));
  header->n_buffers = buffer_number;
  header->version = 2;
  header->output = 0;             // need clarify meaning
  header->counter = 0;            //add later
  header->disp_w = width_;
  header->disp_h = height_;
  int* stream_end = reinterpret_cast<int*>(buf+sizeof(int)+header_size+info_size*buffer_number);
  *stream_end = METADATA_STREAM_END;
  int msg_size = header_size+info_size*buffer_number+sizeof(int)*2;
  int rc;
  if (mconnected){
    do {
      rc = hyper_communication_network_send_data(buf,msg_size);
    } while (rc != msg_size && rc >= 0);
  }
  memset(buf,0,METADATA_BUFFER_SIZE);
  return true;
}

bool NestedDisplay::PresentClone(std::vector<HwcLayer *> & /*source_layers*/,
                                 int32_t * /*retire_fence*/,
                                 bool /*idle_frame*/) {
  return false;
}

int NestedDisplay::RegisterVsyncCallback(
    std::shared_ptr<VsyncCallback> callback, uint32_t display_id) {
  display_id_ = display_id;
  vsync_callback_ = callback;
  return 0;
}

void NestedDisplay::RegisterRefreshCallback(
    std::shared_ptr<RefreshCallback> callback, uint32_t display_id) {
  display_id_ = display_id;
  refresh_callback_ = callback;
}

void NestedDisplay::RegisterHotPlugCallback(
    std::shared_ptr<HotPlugCallback> callback, uint32_t display_id) {
  display_id_ = display_id;
  hotplug_callback_ = callback;
}

void NestedDisplay::VSyncControl(bool enabled) {
  enable_vsync_ = enabled;
}

void NestedDisplay::VSyncUpdate(int64_t timestamp) {
  if (vsync_callback_ && enable_vsync_) {
    vsync_callback_->Callback(display_id_, timestamp);
  }
}

void NestedDisplay::RefreshUpdate() {
  if (refresh_callback_) {
    refresh_callback_->Callback(display_id_);
  }
}

void NestedDisplay::HotPlugUpdate(bool /*connected*/) {
  if (hotplug_callback_) {
    IHOTPLUGEVENTTRACE(
        "NestedDisplay RegisterHotPlugCallback: id: %d display: %p",
        display_id_, this);
    hotplug_callback_->Callback(display_id_, true);
  }
}

bool NestedDisplay::CheckPlaneFormat(uint32_t /*format*/) {
  // assuming that virtual display supports the format
  return true;
}

void NestedDisplay::SetGamma(float /*red*/, float /*green*/, float /*blue*/) {
}

void NestedDisplay::SetContrast(uint32_t /*red*/, uint32_t /*green*/,
                                uint32_t /*blue*/) {
}

void NestedDisplay::SetBrightness(uint32_t /*red*/, uint32_t /*green*/,
                                  uint32_t /*blue*/) {
}

void NestedDisplay::SetExplicitSyncSupport(bool /*disable_explicit_sync*/) {
}

void NestedDisplay::UpdateScalingRatio(uint32_t /*primary_width*/,
                                       uint32_t /*primary_height*/,
                                       uint32_t /*display_width*/,
                                       uint32_t /*display_height*/) {
}

void NestedDisplay::CloneDisplay(NativeDisplay * /*source_display*/) {
}

bool NestedDisplay::GetDisplayAttribute(uint32_t /*config*/,
                                        HWCDisplayAttribute attribute,
                                        int32_t *value) {
  // We always get the values from preferred mode config.
  switch (attribute) {
    case HWCDisplayAttribute::kWidth:
      *value = 1920;
      break;
    case HWCDisplayAttribute::kHeight:
      *value = 1080;
      break;
    case HWCDisplayAttribute::kRefreshRate:
      // in nanoseconds
      *value = 60;
      break;
    case HWCDisplayAttribute::kDpiX:
      // Dots per 1000 inches
      *value = 1;
      break;
    case HWCDisplayAttribute::kDpiY:
      // Dots per 1000 inches
      *value = 1;
      break;
    default:
      *value = -1;
      return false;
  }

  return true;
}

bool NestedDisplay::GetDisplayConfigs(uint32_t *num_configs,
                                      uint32_t *configs) {
  *num_configs = 1;
  if (configs)
    configs[0] = 0;

  return true;
}

bool NestedDisplay::GetDisplayName(uint32_t *size, char *name) {
  std::ostringstream stream;
  stream << "Nested";
  std::string string = stream.str();
  size_t length = string.length();
  if (!name) {
    *size = length;
    return true;
  }

  *size = std::min<uint32_t>(static_cast<uint32_t>(length - 1), *size);
  strncpy(name, string.c_str(), *size);
  return true;
}

int NestedDisplay::start_sock_service() {
  struct sockaddr_in server_addr;
  msock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (msock_fd < 0) {
    ETRACE("Cannot create socket fd:%d",msock_fd);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  int portno = 2345;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(portno);

  if (bind(msock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    close(msock_fd);
    msock_fd = -1;
    return -1;
  }

  listen(msock_fd, 1);
  signal(SIGPIPE, signal_callback_handler);
  return 0;
}

int NestedDisplay::hyper_communication_network_send_data(void *data, int len)
{
  if (mclient_sock_fd >= 0) {
    return send(mclient_sock_fd, data, len, 0);
  }
  return -1;
}


void NestedDisplay::signal_callback_handler(int signum)
{
  if (mclient_sock_fd >= 0) {
    close(mclient_sock_fd);
    mclient_sock_fd = -1;
    st_->setEnabled(true);
  }
  ETRACE("SIG:%d client lost connection",signum);
}

}  // namespace hwcomposer

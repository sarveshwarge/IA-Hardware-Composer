/*
// Copyright (c) 2016 Intel Corporation
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

#ifndef COMMON_CORE_OVERLAYLAYER_H_
#define COMMON_CORE_OVERLAYLAYER_H_

#include <hwcdefs.h>
#include <platformdefines.h>

#include <memory>
#include <hwclayer.h>

#include "overlaybuffer.h"

namespace hwcomposer {

struct HwcLayer;
class OverlayBuffer;
class ResourceManager;

struct OverlayLayer {
  enum LayerComposition {
    kGpu = 1 << 0,      // Needs GPU Composition.
    kDisplay = 1 << 1,  // Display Can scanout the layer directly.
    kAll = kGpu | kDisplay
  };

  OverlayLayer() = default;
  void SetAcquireFence(int32_t acquire_fence);

  int32_t GetAcquireFence() const;

  int32_t ReleaseAcquireFence() const;

  // Initialize OverlayLayer from layer.
  void InitializeFromHwcLayer(HwcLayer* layer, ResourceManager* buffer_manager,
                              OverlayLayer* previous_layer, uint32_t z_order,
                              uint32_t layer_index, uint32_t max_height,
                              HWCRotation rotation, bool handle_constraints);

  void InitializeFromScaledHwcLayer(HwcLayer* layer,
                                    ResourceManager* buffer_manager,
                                    OverlayLayer* previous_layer,
                                    uint32_t z_order, uint32_t layer_index,
                                    const HwcRect<int>& display_frame,
                                    uint32_t max_height, HWCRotation rotation,
                                    bool handle_constraints);
  // Get z order of this layer.
  uint32_t GetZorder() const {
    return z_order_;
  }

  // Index of hwclayer which this layer
  // represents.
  uint32_t GetLayerIndex() const {
    return layer_index_;
  }

  uint8_t GetAlpha() const {
    return alpha_;
  }

  void SetBlending(HWCBlending blending);

  HWCBlending GetBlending() const {
    return blending_;
  }

  uint32_t GetTransform() const {
    return transform_;
  }

  // This represents any transform applied
  // to this layer(i.e. GetTransform()) + overall
  // rotation applied to the display on which this
  // layer is being shown.
  uint32_t GetPlaneTransform() const {
    return plane_transform_;
  }

  OverlayBuffer* GetBuffer() const;

  void SetBuffer(HWCNativeHandle handle, int32_t acquire_fence,
                 ResourceManager* buffer_manager, bool register_buffer);

  void SetSourceCrop(const HwcRect<float>& source_crop);
  const HwcRect<float>& GetSourceCrop() const {
    return source_crop_;
  }

  void SetDisplayFrame(const HwcRect<int>& display_frame);
  const HwcRect<int>& GetDisplayFrame() const {
    return display_frame_;
  }

  const HwcRect<int>& GetSurfaceDamage() const {
    return surface_damage_;
  }

  uint32_t GetSourceCropWidth() const {
    return source_crop_width_;
  }

  uint32_t GetSourceCropHeight() const {
    return source_crop_height_;
  }

  uint32_t GetDisplayFrameWidth() const {
    return display_frame_width_;
  }

  uint32_t GetDisplayFrameHeight() const {
    return display_frame_height_;
  }

  // Returns true if content of the layer has
  // changed.
  bool HasLayerContentChanged() const {
    return state_ & kLayerContentChanged;
  }

  // Returns true if this layer is visible.
  bool IsVisible() const {
    return !(state_ & kInvisible);
  }

  // Value is the actual composition (i.e. GPU/Display)
  // being used for this layer ir-respective of the
  // actual supported composition.
  void SetLayerComposition(OverlayLayer::LayerComposition value) {
    actual_composition_ = value;
  }

  // value should indicate if the layer can be scanned out
  // by display directly or needs to go through gpu
  // composition pass or can handle both.
  void SupportedDisplayComposition(OverlayLayer::LayerComposition value) {
    supported_composition_ = value;
  }

  bool CanScanOut() const {
    return supported_composition_ & kDisplay;
  }

  bool IsCursorLayer() const {
    return type_ == kLayerCursor;
  }

  bool IsVideoLayer() const {
    return type_ == kLayerVideo;
  }

  bool IsGpuRendered() const {
    return actual_composition_ & kGpu;
  }

  bool IsUsingPlaneScalar() const {
    return display_scaled_;
  }

  void UsePlaneScalar(bool value) {
    display_scaled_ = value;
  }

  // Returns true if we should prefer
  // a separate plane for this layer
  // when validating layers in
  // DisplayPlaneManager.
  bool PreferSeparatePlane() const {
    // We set this to true only in case
    // of Media buffer. If this changes
    // in future, use appropriate checks.
    return type_ == kLayerVideo;
  }

  bool HasDimensionsChanged() const {
    return state_ & kDimensionsChanged;
  }

  // Returns true if source rect has changed
  // from previous frame.
  bool HasSourceRectChanged() const {
    return state_ & kSourceRectChanged;
  }

  // Returns true if this layer attributes
  // have changed compared to last frame
  // and needs to be re-tested to ensure
  // we are able to show the layer on screen
  // correctly.
  bool NeedsRevalidation() const {
    return state_ & kNeedsReValidation;
  }

  /**
   * API for querying if Layer source position has
   * changed from last Present call to NativeDisplay.
   */
  bool NeedsToClearSurface() const {
    return state_ & kClearSurface;
  }

  void Dump();

 private:
  enum LayerState {
    kLayerContentChanged = 1 << 0,
    kDimensionsChanged = 1 << 1,
    kClearSurface = 1 << 2,
    kInvisible = 1 << 3,
    kSourceRectChanged = 1 << 4,
    kNeedsReValidation = 1 << 5
  };

  struct ImportedBuffer {
   public:
    ImportedBuffer(std::shared_ptr<OverlayBuffer>& buffer,
                   int32_t acquire_fence);
    ~ImportedBuffer();

    std::shared_ptr<OverlayBuffer> buffer_;
    int32_t acquire_fence_ = -1;
  };

  // Validates current state with previous frame state of
  // layer at same z order.
  void ValidatePreviousFrameState(OverlayLayer* rhs, HwcLayer* layer);

  // Check if we want to use a separate overlay for this
  // layer.
  void ValidateForOverlayUsage();

  void ValidateTransform(uint32_t transform, uint32_t display_transform);

  void UpdateSurfaceDamage(HwcLayer* layer);

  void InitializeState(HwcLayer* layer, ResourceManager* buffer_manager,
                       OverlayLayer* previous_layer, uint32_t z_order,
                       uint32_t layer_index, uint32_t max_height,
                       HWCRotation rotation, bool handle_constraints);

  uint32_t transform_ = 0;
  uint32_t plane_transform_ = 0;
  uint32_t z_order_ = 0;
  uint32_t layer_index_ = 0;
  uint32_t source_crop_width_ = 0;
  uint32_t source_crop_height_ = 0;
  uint32_t display_frame_width_ = 0;
  uint32_t display_frame_height_ = 0;
  uint8_t alpha_ = 0xff;
  HwcRect<float> source_crop_;
  HwcRect<int> display_frame_;
  HwcRect<int> surface_damage_;
  HWCBlending blending_ = HWCBlending::kBlendingNone;
  uint32_t state_ = kLayerContentChanged | kDimensionsChanged;
  std::unique_ptr<ImportedBuffer> imported_buffer_;
  bool display_scaled_ = false;
  LayerComposition supported_composition_;
  LayerComposition actual_composition_;
  HWCLayerType type_ = kLayerNormal;
};

}  // namespace hwcomposer
#endif  // COMMON_CORE_OVERLAYLAYER_H_

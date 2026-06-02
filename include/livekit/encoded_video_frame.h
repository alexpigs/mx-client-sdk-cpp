/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "livekit/video_frame.h"
#include "livekit/video_source.h"
#include "livekit/visibility.h"

namespace livekit {

/// Video codec identifier (matches protobuf VideoCodec enum).
enum class VideoCodec {
  VP8 = 0,
  H264 = 1,
  AV1 = 2,
  VP9 = 3,
  H265 = 4,
};

/// Encapsulation format of the encoded payload.
enum class EncodedPayloadFormat {
  Unknown = 0,
  WebRtc = 1,
  H264AnnexB = 2,
  H264Avcc = 3,
};

/// Tap position in the receiver transformer chain.
enum class TapPosition {
  PostDecrypt = 0,
  PreDecrypt = 1,
};

/// A depacketized encoded video frame.
class LIVEKIT_API EncodedVideoFrame {
 public:
  EncodedVideoFrame() = default;
  ~EncodedVideoFrame() = default;

  const std::uint8_t* data() const noexcept { return data_.data(); }
  std::size_t dataSize() const noexcept { return data_.size(); }
  VideoCodec codec() const noexcept { return codec_; }
  EncodedPayloadFormat payloadFormat() const noexcept { return format_; }
  bool isKeyFrame() const noexcept { return is_key_frame_; }
  std::uint32_t width() const noexcept { return width_; }
  std::uint32_t height() const noexcept { return height_; }
  std::int64_t timestampUs() const noexcept { return timestamp_us_; }
  std::uint32_t rtpTimestamp() const noexcept { return rtp_timestamp_; }
  VideoRotation rotation() const noexcept { return rotation_; }
  std::optional<std::int32_t> qp() const noexcept { return qp_; }
  std::optional<VideoFrameMetadata> metadata() const noexcept { return metadata_; }

 private:
  friend class EncodedVideoStream;
  std::vector<std::uint8_t> data_;
  VideoCodec codec_ = VideoCodec::H264;
  EncodedPayloadFormat format_ = EncodedPayloadFormat::WebRtc;
  bool is_key_frame_ = false;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::int64_t timestamp_us_ = 0;
  std::uint32_t rtp_timestamp_ = 0;
  VideoRotation rotation_ = VideoRotation::VIDEO_ROTATION_0;
  std::optional<std::int32_t> qp_;
  std::optional<VideoFrameMetadata> metadata_;
};

/// Codec configuration delivered when the receiver negotiates codec parameters.
class LIVEKIT_API EncodedVideoCodecConfig {
 public:
  EncodedVideoCodecConfig() = default;
  ~EncodedVideoCodecConfig() = default;

  VideoCodec codec() const noexcept { return codec_; }
  std::optional<std::string> profileLevelId() const noexcept { return profile_level_id_; }
  const std::uint8_t* sps() const noexcept { return sps_.data(); }
  std::size_t spsSize() const noexcept { return sps_.size(); }
  const std::uint8_t* pps() const noexcept { return pps_.data(); }
  std::size_t ppsSize() const noexcept { return pps_.size(); }
  std::uint32_t width() const noexcept { return width_; }
  std::uint32_t height() const noexcept { return height_; }

 private:
  friend class EncodedVideoStream;
  VideoCodec codec_ = VideoCodec::H264;
  std::optional<std::string> profile_level_id_;
  std::vector<std::uint8_t> sps_;
  std::vector<std::uint8_t> pps_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
};

}  // namespace livekit

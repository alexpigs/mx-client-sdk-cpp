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

#include "livekit/encoded_video_stream.h"

#include <utility>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "lk_log.h"
#include "video_frame.pb.h"

namespace livekit {

using proto::FfiEvent;
using proto::FfiRequest;

std::shared_ptr<EncodedVideoStream> EncodedVideoStream::fromTrack(
    const std::shared_ptr<Track>& track,
    const Options& options) {
  auto stream = std::shared_ptr<EncodedVideoStream>(new EncodedVideoStream());
  stream->initFromTrack(track, options);
  return stream;
}

EncodedVideoStream::~EncodedVideoStream() {
  close();
}

EncodedVideoStream::EncodedVideoStream(EncodedVideoStream&& other) noexcept {
  const std::scoped_lock<std::mutex> lock(other.mutex_);
  frame_queue_ = std::move(other.frame_queue_);
  codec_config_ = std::move(other.codec_config_);
  capacity_ = other.capacity_;
  overflow_policy_ = other.overflow_policy_;
  dropped_frames_.store(other.dropped_frames_.load());
  eof_ = other.eof_;
  closed_ = other.closed_;
  stream_handle_ = std::move(other.stream_handle_);
  listener_id_ = other.listener_id_;

  other.listener_id_ = 0;
  other.closed_ = true;
}

EncodedVideoStream& EncodedVideoStream::operator=(EncodedVideoStream&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  close();
  {
    const std::scoped_lock<std::mutex> lock_this(mutex_);
    const std::scoped_lock<std::mutex> lock_other(other.mutex_);
    frame_queue_ = std::move(other.frame_queue_);
    codec_config_ = std::move(other.codec_config_);
    capacity_ = other.capacity_;
    overflow_policy_ = other.overflow_policy_;
    dropped_frames_.store(other.dropped_frames_.load());
    eof_ = other.eof_;
    closed_ = other.closed_;
    stream_handle_ = std::move(other.stream_handle_);
    listener_id_ = other.listener_id_;
    other.listener_id_ = 0;
    other.closed_ = true;
  }
  return *this;
}

bool EncodedVideoStream::read(EncodedVideoFrame& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !frame_queue_.empty() || eof_ || closed_; });
  if (closed_ || (frame_queue_.empty() && eof_)) {
    return false;
  }
  out = std::move(frame_queue_.front());
  frame_queue_.pop_front();
  return true;
}

bool EncodedVideoStream::readCodecConfig(EncodedVideoCodecConfig& out) {
  std::unique_lock<std::mutex> lock(mutex_);
  config_cv_.wait(lock, [this] { return codec_config_.has_value() || closed_; });
  if (closed_) {
    return false;
  }
  out = codec_config_.value();
  return true;
}

void EncodedVideoStream::close() {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
  }

  if (stream_handle_.get() != 0) {
    stream_handle_.reset();
  }
  if (listener_id_ != 0) {
    FfiClient::instance().removeListener(listener_id_);
    listener_id_ = 0;
  }

  cv_.notify_all();
  config_cv_.notify_all();
}

void EncodedVideoStream::initFromTrack(const std::shared_ptr<Track>& track,
                                       const Options& options) {
  capacity_ = options.capacity;
  overflow_policy_ = options.overflow_policy;

  listener_id_ = FfiClient::instance().addListener(
      [this](const FfiEvent& e) { this->onFfiEvent(e); });

  FfiRequest req;
  auto* nes = req.mutable_new_encoded_video_stream();
  nes->set_track_handle(static_cast<uint64_t>(track->ffiHandleId()));
  nes->set_queue_size_frames(static_cast<uint32_t>(capacity_));
  nes->set_tap_position(
      static_cast<proto::EncodedVideoStreamTapPosition>(options.tap_position));
  nes->set_drop_after_tap(options.drop_after_tap);

  auto resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_encoded_video_stream()) {
    LK_LOG_ERROR(
        "EncodedVideoStream::initFromTrack: FFI response missing "
        "new_encoded_video_stream()");
    throw std::runtime_error("new_encoded_video_stream FFI request failed");
  }

  const auto& stream = resp.new_encoded_video_stream().stream();
  stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
}

void EncodedVideoStream::onFfiEvent(const proto::FfiEvent& event) {
  if (event.message_case() != FfiEvent::kEncodedVideoStreamEvent) {
    return;
  }

  const auto& ese = event.encoded_video_stream_event();
  if (ese.stream_handle() !=
      static_cast<std::uint64_t>(stream_handle_.get())) {
    return;
  }

  if (ese.has_frame_received()) {
    const auto& fr = ese.frame_received();

    EncodedVideoFrame ev;
    const auto& buf_info = fr.buffer().info();
    const auto* src =
        reinterpret_cast<const std::uint8_t*>(buf_info.data_ptr());
    if (src != nullptr && buf_info.size() > 0) {
      ev.data_.assign(src, src + buf_info.size());
    }

    ev.codec_ = static_cast<VideoCodec>(fr.codec());
    ev.format_ = static_cast<EncodedPayloadFormat>(fr.payload_format());
    ev.is_key_frame_ = fr.is_key_frame();
    ev.width_ = fr.width();
    ev.height_ = fr.height();
    ev.timestamp_us_ = fr.timestamp_us();
    ev.rtp_timestamp_ = fr.rtp_timestamp();
    ev.rotation_ = static_cast<VideoRotation>(fr.rotation());
    if (fr.has_qp()) {
      ev.qp_ = fr.qp();
    }
    if (fr.has_metadata()) {
      VideoFrameMetadata meta;
      meta.user_timestamp_us = fr.metadata().user_timestamp();
      meta.frame_id = fr.metadata().frame_id();
      ev.metadata_ = meta;
    }

    // Release the owned buffer handle after copying the data
    {
      const FfiHandle owned_handle(
          static_cast<std::uintptr_t>(fr.buffer().handle().id()));
    }

    pushFrame(std::move(ev));
  } else if (ese.has_codec_config()) {
    const auto& cc = ese.codec_config();
    EncodedVideoCodecConfig config;
    config.codec_ = static_cast<VideoCodec>(cc.codec());
    if (cc.has_profile_level_id()) {
      config.profile_level_id_ = cc.profile_level_id();
    }
    if (cc.sps().size() > 0) {
      config.sps_.assign(
          reinterpret_cast<const std::uint8_t*>(cc.sps().data()),
          reinterpret_cast<const std::uint8_t*>(cc.sps().data()) +
              cc.sps().size());
    }
    if (cc.pps().size() > 0) {
      config.pps_.assign(
          reinterpret_cast<const std::uint8_t*>(cc.pps().data()),
          reinterpret_cast<const std::uint8_t*>(cc.pps().data()) +
              cc.pps().size());
    }
    config.width_ = cc.width();
    config.height_ = cc.height();
    pushCodecConfig(std::move(config));
  } else if (ese.has_eos()) {
    pushEos();
  }
}

void EncodedVideoStream::pushFrame(EncodedVideoFrame&& frame) {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || eof_) {
      return;
    }

    if (capacity_ > 0 && frame_queue_.size() >= capacity_) {
      if (overflow_policy_ == OverflowPolicy::DropOldest) {
        frame_queue_.pop_front();
      } else {
        dropped_frames_++;
        return;
      }
      dropped_frames_++;
    }

    frame_queue_.push_back(std::move(frame));
  }
  cv_.notify_one();
}

void EncodedVideoStream::pushCodecConfig(EncodedVideoCodecConfig&& config) {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    codec_config_ = std::move(config);
  }
  config_cv_.notify_all();
}

void EncodedVideoStream::pushEos() {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (eof_) {
      return;
    }
    eof_ = true;
  }
  cv_.notify_all();
}

}  // namespace livekit

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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include "livekit/encoded_video_frame.h"
#include "livekit/ffi_handle.h"
#include "livekit/track.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class FfiEvent;
}

/// A pull-based stream of depacketized encoded video frames from a remote track.
///
/// Typical usage:
///
///   EncodedVideoStream::Options opts;
///   auto stream = EncodedVideoStream::fromTrack(remoteVideoTrack, opts);
///
///   EncodedVideoFrame frame;
///   while (stream->read(frame)) {
///     // frame.data() / frame.dataSize() contain encoded bitstream
///   }
///
///   stream->close();  // optional, called automatically in destructor
class LIVEKIT_API EncodedVideoStream {
 public:
  enum class OverflowPolicy {
    DropOldest,
    DropNewest,
  };

  struct Options {
    /// Maximum number of EncodedVideoFrame items buffered in the queue.
    /// 0 means "unbounded". Default 1 (bounded, most recent frame).
    std::size_t capacity{1};

    /// What to do when the queue is at capacity.
    OverflowPolicy overflow_policy{OverflowPolicy::DropOldest};

    /// Where in the receiver transformer chain to tap.
    TapPosition tap_position{TapPosition::PostDecrypt};

    /// When true, tapped frames are not forwarded to WebRTC's decoder.
    bool drop_after_tap{false};
  };

  /// Factory: create an EncodedVideoStream bound to a specific remote video Track.
  ///
  /// @throws std::runtime_error if the track is not a remote video track or
  ///         the FFI request fails.
  static std::shared_ptr<EncodedVideoStream> fromTrack(
      const std::shared_ptr<Track>& track,
      const Options& options);

  virtual ~EncodedVideoStream();

  EncodedVideoStream(const EncodedVideoStream&) = delete;
  EncodedVideoStream& operator=(const EncodedVideoStream&) = delete;
  EncodedVideoStream(EncodedVideoStream&&) noexcept;
  EncodedVideoStream& operator=(EncodedVideoStream&&) noexcept;

  /// Blocking read: waits until an encoded frame is available, or the stream
  /// reaches EOS / is closed.
  ///
  /// @param out  On success, filled with the next encoded frame.
  /// @return true if a frame was delivered; false if the stream ended.
  bool read(EncodedVideoFrame& out);

  /// Blocking read: waits until codec config is available, or the stream is
  /// closed.
  ///
  /// @param out  On success, filled with the codec config.
  /// @return true if config was delivered; false if the stream ended/closed.
  bool readCodecConfig(EncodedVideoCodecConfig& out);

  /// Signal that we are no longer interested in encoded frames.
  void close();

  /// Number of frames dropped due to queue overflow.
  std::uint64_t droppedFrames() const noexcept { return dropped_frames_.load(); }

 private:
  EncodedVideoStream() = default;

  void initFromTrack(const std::shared_ptr<Track>& track, const Options& options);
  void onFfiEvent(const proto::FfiEvent& event);

  void pushFrame(EncodedVideoFrame&& frame);
  void pushCodecConfig(EncodedVideoCodecConfig&& config);
  void pushEos();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<EncodedVideoFrame> frame_queue_;
  std::optional<EncodedVideoCodecConfig> codec_config_;
  std::condition_variable config_cv_;
  std::size_t capacity_{1};
  OverflowPolicy overflow_policy_{OverflowPolicy::DropOldest};
  std::atomic<std::uint64_t> dropped_frames_{0};
  bool eof_{false};
  bool closed_{false};

  FfiHandle stream_handle_;
  std::int32_t listener_id_{0};
};

}  // namespace livekit

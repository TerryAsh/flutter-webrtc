#include "flutter_video_renderer.h"

namespace flutter_webrtc_plugin {

FlutterVideoRenderer::FlutterVideoRenderer(TextureRegistrar *registrar,
                                           BinaryMessenger *messenger)
    : registrar_(registrar) {
  texture_id_ = registrar_->RegisterTexture(this);
  std::string event_channel =
      "FlutterWebRTC/Texture" + std::to_string(texture_id_);
  event_channel_.reset(new EventChannel<EncodableValue>(
      messenger, event_channel, &StandardMethodCodec::GetInstance()));

  StreamHandler<EncodableValue> stream_handler = {
      [&](const EncodableValue*arguments,
          const EventSink<EncodableValue> *events) -> MethodResult<EncodableValue> * {
        event_sink_ = events;
        return nullptr;
      },
      [&](const EncodableValue*arguments) -> MethodResult<EncodableValue> * {
        event_sink_ = nullptr;
        return nullptr;
      }};
  event_channel_->SetStreamHandler(stream_handler);
}

std::shared_ptr<GLFWPixelBuffer> FlutterVideoRenderer::CopyTextureBuffer(
    size_t width, size_t height) {
    if (pixel_buffer_.get() && frame_.get()) {
        if (pixel_buffer_->width != frame_->width() || pixel_buffer_->height != frame_->height()) {
            size_t buffer_size = (frame_->width() * frame_->height()) * (32 >> 3);
            pixel_buffer_->buffer.reset(new uint8_t[buffer_size]);
            pixel_buffer_->width = frame_->width();
            pixel_buffer_->height = frame_->height();
        }
        if (frame_.get())
            frame_->ConvertToARGB(RTCVideoFrame::Type::kABGR, pixel_buffer_->buffer.get(), 0,
            (int)pixel_buffer_->width, (int)pixel_buffer_->height);
        return pixel_buffer_;
    }

    return std::shared_ptr<GLFWPixelBuffer>(nullptr);
}

void FlutterVideoRenderer::OnFrame(scoped_refptr<RTCVideoFrame> frame) {
 
  if (!first_frame_rendered) {
      if (event_sink_) {
          EncodableMap params;
          params[EncodableValue("event")] = "didFirstFrameRendered";
          params[EncodableValue("id")] = texture_id_;
          event_sink_->Success(&EncodableValue(params));
      }
      pixel_buffer_.reset(new GLFWPixelBuffer());
      pixel_buffer_->width = 0;
      pixel_buffer_->height = 0;
      first_frame_rendered = true;
  }

  if (rotation_ != frame->rotation()) {
      if (event_sink_) {
          EncodableMap params;
          params[EncodableValue("event")] = "didTextureChangeRotation";
          params[EncodableValue("id")] = texture_id_;
          params[EncodableValue("rotation")] = (int32_t)frame->rotation();
          event_sink_->Success(&EncodableValue(params));
      }
      rotation_ = frame->rotation();
  }

  if (last_frame_size_.width != frame->width() ||
      last_frame_size_.height != frame->height()) {

    if (event_sink_) {
      EncodableMap params;
      params[EncodableValue("event")] = "didTextureChangeVideoSize";
      params[EncodableValue("id")] = texture_id_;
      params[EncodableValue("width")] = (int32_t)frame->width();
      params[EncodableValue("height")] = (int32_t)frame->height();
      event_sink_->Success(&EncodableValue(params));
    }

    last_frame_size_ = {(size_t)frame->width(), (size_t)frame->height()};
  }
 
  frame_ = frame;
  registrar_->MarkTextureFrameAvailable(texture_id_);
}

void FlutterVideoRenderer::SetVideoTrack(scoped_refptr<RTCVideoTrack> track) {
  if (track_ != track) {
    if (track_) track_->RemoveRenderer(this);
    track_ = track;
    last_frame_size_ = {0, 0};
    first_frame_rendered = false;
    if (track_) track_->AddRenderer(this);
  }
}

FlutterVideoRendererManager::FlutterVideoRendererManager(
    FlutterWebRTCBase *base)
    : base_(base) {}

void FlutterVideoRendererManager::CreateVideoRendererTexture(
    std::unique_ptr<MethodResult<EncodableValue>> result) {
  std::unique_ptr<FlutterVideoRenderer> texture(
      new FlutterVideoRenderer(base_->textures_, base_->messenger_));
  int64_t texture_id = texture->texture_id();
  renderers_[texture_id] = std::move(texture);
  EncodableMap params;
  params[EncodableValue("textureId")] = texture_id;
  result->Success(&EncodableValue(params));
}

void FlutterVideoRendererManager::SetMediaStream(int64_t texture_id,
                                                 const std::string &stream_id) {
  scoped_refptr<RTCMediaStream> stream = base_->MediaStreamForId(stream_id);

  auto it = renderers_.find(texture_id);
  if (it != renderers_.end()) {
    FlutterVideoRenderer *renderer = it->second.get();
    if (stream.get()) {
      VideoTrackVector tracks = stream->GetVideoTracks();
      if (tracks.size() > 0) {
        renderer->SetVideoTrack(tracks.at(0));
      }
    } else {
      renderer->SetVideoTrack(nullptr);
    }
  }
}

void FlutterVideoRendererManager::VideoRendererDispose(
    int64_t texture_id, std::unique_ptr<MethodResult<EncodableValue>> result) {
  auto it = renderers_.find(texture_id);
  if (it != renderers_.end()) {
    base_->textures_->UnregisterTexture(texture_id);
    renderers_.erase(it);
    result->Success();
    return;
  }
  result->Error("VideoRendererDisposeFailed",
                "VideoRendererDispose() texture not found!");
}

};  // namespace flutter_webrtc_plugin
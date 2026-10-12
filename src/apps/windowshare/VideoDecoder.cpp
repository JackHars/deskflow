/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VideoDecoder.h"
#include "Core.h"

#include <QFileInfo>
#include <array>
#include <cstring>
#include <limits>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace deskflow::windowshare {
namespace {
constexpr std::int64_t maxDecodedPixels = 64 * 1024 * 1024;

QString ffmpegError(int error)
{
  std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
  av_strerror(error, text.data(), text.size());
  return QString::fromUtf8(text.data());
}

bool startsWithAnnexB(const QByteArray &data)
{
  const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
  const qsizetype size = data.size();
  qsizetype header = 0;
  if (size >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1)
    header = 3;
  else if (size >= 5 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1)
    header = 4;
  else
    return false;
  const unsigned type = bytes[header] & 31;
  return (bytes[header] & 128) == 0 && type >= 1 && type <= 12;
}
} // namespace

struct VideoDecoder::Impl
{
  bool preferHardware = true;
  AVCodecContext *context = nullptr;
  AVFrame *frame = nullptr;
  AVFrame *transferred = nullptr;
  AVPacket *packet = nullptr;
  SwsContext *scaler = nullptr;
  AVPixelFormat hardwareFormat = AV_PIX_FMT_NONE;
  QString name = QStringLiteral("software");

  ~Impl()
  {
    clear();
  }

  void clear()
  {
    sws_freeContext(scaler);
    scaler = nullptr;
    av_packet_free(&packet);
    av_frame_free(&transferred);
    av_frame_free(&frame);
    avcodec_free_context(&context);
    hardwareFormat = AV_PIX_FMT_NONE;
    name = QStringLiteral("software");
  }

  static AVPixelFormat selectFormat(AVCodecContext *codec, const AVPixelFormat *formats)
  {
    const auto *self = static_cast<Impl *>(codec->opaque);
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format)
      if (*format == self->hardwareFormat)
        return *format;
    // FFmpeg can still negotiate a software format when a stream's profile is
    // unsupported by the installed GPU driver.
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
      const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(*format);
      if (descriptor && !(descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL))
        return *format;
    }
    return AV_PIX_FMT_NONE;
  }

  void tryHardware(const AVCodec *codec, AVHWDeviceType type)
  {
    if (hardwareFormat != AV_PIX_FMT_NONE)
      return;
    for (int i = 0; const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i); ++i) {
      if (config->device_type != type || !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
        continue;
      AVBufferRef *device = nullptr;
      if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0)
        continue;
      context->hw_device_ctx = device;
      hardwareFormat = config->pix_fmt;
      context->opaque = this;
      context->get_format = selectFormat;
      name = QString::fromUtf8(av_hwdevice_get_type_name(type));
      return;
    }
  }

  QString initialize()
  {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
      return QStringLiteral("This FFmpeg build has no H.264 decoder");
    context = avcodec_alloc_context3(codec);
    frame = av_frame_alloc();
    transferred = av_frame_alloc();
    packet = av_packet_alloc();
    if (!context || !frame || !transferred || !packet) {
      clear();
      return QStringLiteral("Could not allocate video decoder");
    }
    context->thread_count = 2;
    context->thread_type = FF_THREAD_SLICE;
    context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    context->err_recognition = AV_EF_CAREFUL;
    context->max_pixels = maxDecodedPixels;
    if (preferHardware) {
#if defined(Q_OS_LINUX)
      // Avoid probing absent devices and producing misleading driver errors.
      if (QFileInfo::exists(QStringLiteral("/dev/nvidia0")))
        tryHardware(codec, AV_HWDEVICE_TYPE_CUDA);
      if (QFileInfo::exists(QStringLiteral("/dev/dri/renderD128")))
        tryHardware(codec, AV_HWDEVICE_TYPE_VAAPI);
#elif defined(Q_OS_MACOS)
      tryHardware(codec, AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
#endif
    }
    const int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
      const bool usedHardware = hardwareFormat != AV_PIX_FMT_NONE;
      clear();
      if (usedHardware) {
        preferHardware = false;
        return initialize();
      }
      return QStringLiteral("Could not open H.264 decoder: ") + ffmpegError(result);
    }
    return {};
  }

  VideoDecoder::Result drain()
  {
    VideoDecoder::Result output;
    while (true) {
      const int received = avcodec_receive_frame(context, frame);
      if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
        return output;
      if (received < 0) {
        output.error = QStringLiteral("H.264 decode failed: ") + ffmpegError(received);
        return output;
      }
      AVFrame *pixels = frame;
      const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
      if (descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        av_frame_unref(transferred);
        const int copied = av_hwframe_transfer_data(transferred, frame, 0);
        if (copied < 0) {
          output.error = QStringLiteral("Could not retrieve GPU video frame: ") + ffmpegError(copied);
          av_frame_unref(frame);
          // A subsequent keyframe can resume in software after GPU loss.
          preferHardware = false;
          clear();
          return output;
        }
        av_frame_copy_props(transferred, frame);
        pixels = transferred;
      } else {
        name = QStringLiteral("software");
      }
      if (pixels->width < 1 || pixels->height < 1 || pixels->width > 16384 || pixels->height > 16384 ||
          static_cast<std::int64_t>(pixels->width) * pixels->height > maxDecodedPixels) {
        output.error = QStringLiteral("Decoded frame dimensions exceed the window limit");
        av_frame_unref(frame);
        return output;
      }
      scaler = sws_getCachedContext(
          scaler, pixels->width, pixels->height, static_cast<AVPixelFormat>(pixels->format), pixels->width,
          pixels->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr
      );
      QImage image(pixels->width, pixels->height, QImage::Format_RGBA8888);
      if (!scaler || image.isNull()) {
        output.error = QStringLiteral("Could not allocate the decoded window image");
        av_frame_unref(frame);
        return output;
      }
      // ScreenCaptureKit/VideoToolbox mark range and matrix; respect them rather
      // than treating every desktop frame as limited-range BT.601.
      const int matrix = pixels->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
      const int *coefficients = sws_getCoefficients(matrix);
      sws_setColorspaceDetails(
          scaler, coefficients, pixels->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0, 1 << 16, 1 << 16
      );
      std::array<unsigned char *, 4> outputPlanes{image.bits(), nullptr, nullptr, nullptr};
      std::array<int, 4> outputStrides{static_cast<int>(image.bytesPerLine()), 0, 0, 0};
      const int rows = sws_scale(
          scaler, pixels->data, pixels->linesize, 0, pixels->height, outputPlanes.data(), outputStrides.data()
      );
      av_frame_unref(frame);
      if (rows != image.height()) {
        output.error = QStringLiteral("Video conversion produced an incomplete image");
        return output;
      }
      output.image = std::move(image);
    }
  }
};

VideoDecoder::VideoDecoder(bool preferHardware) : m_impl(std::make_unique<Impl>())
{
  m_impl->preferHardware = preferHardware;
}
VideoDecoder::~VideoDecoder() = default;

VideoDecoder::Result VideoDecoder::decode(const QByteArray &accessUnit)
{
  if (accessUnit.size() > maxPayload || !startsWithAnnexB(accessUnit))
    return {{}, QStringLiteral("Expected a bounded H.264 Annex B access unit")};
  if (!m_impl->context) {
    const QString error = m_impl->initialize();
    if (!error.isEmpty())
      return {{}, error};
  }
  av_packet_unref(m_impl->packet);
  const int allocation = av_new_packet(m_impl->packet, static_cast<int>(accessUnit.size()));
  if (allocation < 0)
    return {{}, QStringLiteral("Could not allocate video packet: ") + ffmpegError(allocation)};
  // av_new_packet provides FFmpeg's required trailing zero padding.
  std::memcpy(m_impl->packet->data, accessUnit.constData(), static_cast<std::size_t>(accessUnit.size()));
  const int sent = avcodec_send_packet(m_impl->context, m_impl->packet);
  av_packet_unref(m_impl->packet);
  if (sent < 0)
    return {{}, QStringLiteral("H.264 packet rejected: ") + ffmpegError(sent)};
  return m_impl->drain();
}

void VideoDecoder::reset()
{
  m_impl->clear();
}
QString VideoDecoder::backend() const
{
  return m_impl->name;
}

} // namespace deskflow::windowshare

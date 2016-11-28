#include "ffmpeg_hwaccel.h"

#include <ffmpeg.h>
#include "libavutil/hwcontext.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

/* BEGIN: Extracted from ffmpeg.c */

static const HWAccel *get_hwaccel(enum AVPixelFormat pix_fmt)
{
    int i;
    for (i = 0; hwaccels[i].name; i++)
        if (hwaccels[i].pix_fmt == pix_fmt)
            return &hwaccels[i];
    return NULL;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != -1; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const HWAccel *hwaccel;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        hwaccel = get_hwaccel(*p);
        if (!hwaccel ||
            (ist->active_hwaccel_id && ist->active_hwaccel_id != hwaccel->id) ||
            (ist->hwaccel_id != HWACCEL_AUTO && ist->hwaccel_id != hwaccel->id))
            continue;

        ret = hwaccel->init(s);
        if (ret < 0) {
            if (ist->hwaccel_id == hwaccel->id) {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream #%d:%d, "
                       "but cannot be initialized.\n", hwaccel->name,
                       ist->file_index, ist->st->index);
                return AV_PIX_FMT_NONE;
            }
            continue;
        }
        ist->active_hwaccel_id = hwaccel->id;
        ist->hwaccel_pix_fmt   = *p;
        break;
    }

    return *p;
}

static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    if (ist->hwaccel_get_buffer && frame->format == ist->hwaccel_pix_fmt)
        return ist->hwaccel_get_buffer(s, frame, flags);

    return avcodec_default_get_buffer2(s, frame, flags);
}

/* END: Extracted from ffmpeg.c */

/* BEGIN: Extracted from ffmpeg_opt.c */

const HWAccel hwaccels[] = {
#if HAVE_VDPAU_X11
    { "vdpau", vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU },
#endif
#if HAVE_DXVA2_LIB
    { "dxva2", dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD },
#endif
#if CONFIG_VDA
    { "vda",   videotoolbox_init,   HWACCEL_VDA,   AV_PIX_FMT_VDA },
#endif
#if CONFIG_VIDEOTOOLBOX
    { "videotoolbox",   videotoolbox_init,   HWACCEL_VIDEOTOOLBOX,   AV_PIX_FMT_VIDEOTOOLBOX },
#endif
#if CONFIG_LIBMFX
    { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
#endif
#if CONFIG_VAAPI
    { "vaapi", vaapi_decode_init, HWACCEL_VAAPI, AV_PIX_FMT_VAAPI },
#endif
#if CONFIG_CUVID
    { "cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA },
#endif
    { 0 },
};
int hwaccel_lax_profile_check = 0;
AVBufferRef *hw_device_ctx;

/* END: Extracted from ffmpeg_opt.c */

int ffmpeg_hwaccel_init(AVCodecContext *avctx)
{
  InputStream *ist;

  ist = av_mallocz(sizeof(*ist));
  if (! ist)
    return AVERROR(ENOMEM);

  // Use auto-detection by default.
  ist->hwaccel_id = HWACCEL_AUTO;
  ist->hwaccel_device = "android";
  ist->hwaccel_output_format = AV_PIX_FMT_YUV420P;

  avctx->opaque = ist;
  avctx->get_format = get_format;
  avctx->get_buffer2 = get_buffer;
  avctx->thread_safe_callbacks = 1;
  av_opt_set_int(avctx, "refcounted_frames", 1, 0);

  return 0;
}

void ffmpeg_hwaccel_deinit(AVCodecContext *avctx)
{
  if (avctx->opaque)
    av_freep(&avctx->opaque);
}

int ffmpeg_hwaccel_get_frame(AVCodecContext *avctx, AVFrame *frame)
{
  InputStream *ist = avctx->opaque;
  int err = 0;

  if (ist->hwaccel_retrieve_data && frame->format == ist->hwaccel_pix_fmt) {
      err = ist->hwaccel_retrieve_data(avctx, frame);
      if (err < 0)
        goto fail;
  }
  ist->hwaccel_retrieved_pix_fmt = frame->format;

fail:
  return err;
}


/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-video-sink.c - Gstreamer Video Sink that renders to a
 *                            Cogl Pipeline.
 *
 * Authored by Jonathan Matthew  <jonathan@kaolin.wh9.net>,
 *             Chris Lord        <chris@openedhand.com>
 *             Damien Lespiau    <damien.lespiau@intel.com>
 *             Matthew Allum     <mallum@openedhand.com>
 *             Plamena Manolova  <plamena.n.manolova@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *
 * Copyright (C) 2007, 2008 OpenedHand
 * Copyright (C) 2009, 2010, 2013, 2014 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:clutter-gst-video-sink
 * @short_description: A video sink for integrating a GStreamer
 *   pipeline with a Cogl pipeline.
 *
 * #ClutterGstVideoSink is a subclass of #GstBaseSink which can be used to
 * create a #CoglPipeline for rendering the frames of the video.
 *
 * To create a basic video player, an application can create a
 * #GstPipeline as normal using gst_pipeline_new() and set the
 * sink on it to one created with clutter_gst_video_sink_new(). The
 * application can then listen for the #ClutterGstVideoSink::new-frame
 * signal which will be emitted whenever there are new textures ready
 * for rendering. For simple rendering, the application can just call
 * clutter_gst_video_sink_get_pipeline() in the signal handler and use
 * the returned pipeline to paint the new frame.
 *
 * An application is also free to do more advanced rendering by
 * customizing the pipeline. In that case it should listen for the
 * #ClutterGstVideoSink::pipeline-ready signal which will be emitted as
 * soon as the sink has determined enough information about the video
 * to know how it should be rendered. In the handler for this signal,
 * the application can either make modifications to a copy of the
 * pipeline returned by clutter_gst_video_sink_get_pipeline() or it can
 * create its own pipeline from scratch and ask the sink to configure
 * it with clutter_gst_video_sink_setup_pipeline(). If a custom pipeline
 * is created using one of these methods then the application should
 * call clutter_gst_video_sink_attach_frame() on the pipeline before
 * rendering in order to update the textures on the pipeline's layers.
 *
 * If the %COGL_FEATURE_ID_GLSL feature is available then the pipeline
 * used by the sink will have a shader snippet with a function in it
 * called clutter_gst_sample_video0 which takes a single vec2 argument.
 * This can be used by custom snippets set the by the application to
 * sample from the video. The vec2 argument represents the normalised
 * coordinates within the video. The function returns a vec4
 * containing a pre-multiplied RGBA color of the pixel within the
 * video.
 *
 * Since: 3.0
 */

#include "config.h"

#include <gst/gst.h>
#include <gst/gstvalue.h>
#include <gst/video/navigation.h>
#include <gst/video/video.h>
#include <gst/riff/riff-ids.h>
#include <string.h>
#include <math.h>

#include "clutter-gst-video-sink.h"
#include "clutter-gst-private.h"

GST_DEBUG_CATEGORY_STATIC (clutter_gst_video_sink_debug);
#define GST_CAT_DEFAULT clutter_gst_video_sink_debug

#define CLUTTER_GST_DEFAULT_PRIORITY CLUTTER_PRIORITY_REDRAW

#define BASE_SINK_CAPS "{ AYUV,"                \
  "YV12,"                                       \
  "I420,"                                       \
  "RGBA,"                                       \
  "BGRA,"                                       \
  "RGBx,"                                       \
  "BGRx,"                                       \
  "RGB,"                                        \
  "BGR,"                                        \
  "NV12 }"


#define MAKE_CAPS(feature, caps) \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(feature, caps)

#define MAKE_CAPS_COMPOSITON(feature, caps) \
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(feature "," GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, caps)

static const char clutter_gst_video_sink_caps_str[] =
  MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, BASE_SINK_CAPS)
#ifdef HAVE_GL_TEXTURE_UPLOAD
  ";"
  MAKE_CAPS_COMPOSITON (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, "{ RGBA }")
  ";"
  MAKE_CAPS (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, "{ RGBA }")
#endif
  ";"
  MAKE_CAPS_COMPOSITON (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, BASE_SINK_CAPS)
;


static GstStaticPadTemplate sinktemplate_all =
    GST_STATIC_PAD_TEMPLATE ("sink",
                             GST_PAD_SINK,
                             GST_PAD_ALWAYS,
                             GST_STATIC_CAPS (clutter_gst_video_sink_caps_str));

static void color_balance_iface_init (GstColorBalanceInterface *iface);
static void navigation_interface_init (GstNavigationInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstVideoSink,
                         clutter_gst_video_sink,
                         GST_TYPE_VIDEO_SINK,
                         G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE, color_balance_iface_init)
                         G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
                                                navigation_interface_init))

enum
{
  PROP_0,
  PROP_UPDATE_PRIORITY
};

enum
  {
    PIPELINE_READY,
    NEW_FRAME,

    NEW_OVERLAYS,

    LAST_SIGNAL
  };

static guint video_sink_signals[LAST_SIGNAL] = { 0, };

typedef enum
{
  CLUTTER_GST_NOFORMAT,
  CLUTTER_GST_RGB32,
  CLUTTER_GST_RGB24,
  CLUTTER_GST_AYUV,
  CLUTTER_GST_YV12,
  CLUTTER_GST_SURFACE,
  CLUTTER_GST_I420,
  CLUTTER_GST_NV12
} ClutterGstVideoFormat;

typedef enum
{
  CLUTTER_GST_RENDERER_NEEDS_GLSL = (1 << 0),
  CLUTTER_GST_RENDERER_NEEDS_TEXTURE_RG = (1 << 1)
} ClutterGstRendererFlag;

/* We want to cache the snippets instead of recreating a new one every
 * time we initialise a pipeline so that if we end up recreating the
 * same pipeline again then Cogl will be able to use the pipeline
 * cache to avoid linking a redundant identical shader program */
typedef struct
{
  CoglSnippet *vertex_snippet;
  CoglSnippet *fragment_snippet;
  CoglSnippet *default_sample_snippet;
  int start_position;
} SnippetCacheEntry;

typedef struct
{
  GQueue entries;
} SnippetCache;

typedef struct _ClutterGstSource
{
  GSource source;
  ClutterGstVideoSink *sink;
  GMutex buffer_lock;
  GstBuffer *buffer;
  gboolean has_new_caps;
} ClutterGstSource;

typedef void (ClutterGstRendererPaint) (ClutterGstVideoSink *);
typedef void (ClutterGstRendererPostPaint) (ClutterGstVideoSink *);

typedef struct _ClutterGstRenderer
{
  const char *name;
  ClutterGstVideoFormat format;
  guint flags;
  GstStaticCaps caps;
  guint n_layers;
  void (*setup_pipeline) (ClutterGstVideoSink *sink,
                          CoglPipeline *pipeline);
  gboolean (*upload) (ClutterGstVideoSink *sink,
                      GstBuffer *buffer);
  gboolean (*upload_gl) (ClutterGstVideoSink *sink,
                         GstBuffer *buffer);
  void (*shutdown) (ClutterGstVideoSink *sink);
} ClutterGstRenderer;

struct _ClutterGstVideoSinkPrivate
{
  CoglContext *ctx;
  CoglPipeline *template_pipeline;
  ClutterGstFrame *clt_frame;

  CoglTexture *frame[3];
  gboolean frame_dirty;
  gboolean had_upload_once;

  ClutterGstVideoFormat format;
  gboolean bgr;

  ClutterGstSource *source;
  GSList *renderers;
  GstCaps *caps;
  ClutterGstRenderer *renderer;
  GstFlowReturn flow_return;
  int custom_start;
  int video_start;
  gboolean default_sample;
  GstVideoInfo info;

  gdouble brightness;
  gdouble contrast;
  gdouble hue;
  gdouble saturation;
  gboolean balance_dirty;

  guint8 *tabley;
  guint8 *tableu;
  guint8 *tablev;

  /**/
  GstVideoOverlayComposition *last_composition;
  ClutterGstOverlays *overlays;
};

/* Overlays */

static void
clutter_gst_video_sink_upload_overlay (ClutterGstVideoSink *sink, GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  GstVideoOverlayComposition *composition = NULL;
  GstVideoOverlayCompositionMeta *composition_meta;
  guint i, nb_rectangle;

  composition_meta = gst_buffer_get_video_overlay_composition_meta (buffer);
  if (composition_meta)
    composition = composition_meta->overlay;

  if (composition == NULL)
    {
      if (priv->last_composition != NULL)
        {
          gst_video_overlay_composition_unref (priv->last_composition);
          priv->last_composition = NULL;

          if (priv->overlays)
            g_boxed_free (CLUTTER_GST_TYPE_OVERLAYS, priv->overlays);
          priv->overlays = clutter_gst_overlays_new ();

          g_signal_emit (sink, video_sink_signals[NEW_OVERLAYS], 0);
        }
      return;
    }

  g_clear_pointer (&priv->last_composition, gst_video_overlay_composition_unref);
  priv->last_composition = gst_video_overlay_composition_ref (composition);
  if (priv->overlays)
    g_boxed_free (CLUTTER_GST_TYPE_OVERLAYS, priv->overlays);
  priv->overlays = clutter_gst_overlays_new ();

  nb_rectangle = gst_video_overlay_composition_n_rectangles (composition);
  for (i = 0; i < nb_rectangle; i++)
    {
      GstVideoOverlayRectangle *rectangle;
      GstBuffer *comp_buffer;
      GstMapInfo info;
      GstVideoMeta *vmeta;
      gpointer data;
      gint comp_x, comp_y, stride;
      guint comp_width, comp_height;
      CoglTexture *tex;
      CoglError *error;

      rectangle = gst_video_overlay_composition_get_rectangle (composition, i);
      comp_buffer =
        gst_video_overlay_rectangle_get_pixels_unscaled_argb (rectangle,
                                                              GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

      gst_video_overlay_rectangle_get_render_rectangle (rectangle,
                                                        &comp_x, &comp_y, &comp_width, &comp_height);

      vmeta = gst_buffer_get_video_meta (comp_buffer);
      gst_video_meta_map (vmeta, 0, &info, &data, &stride, GST_MAP_READ);

      tex =
        cogl_texture_2d_new_from_data (priv->ctx,
                                       comp_width,
                                       comp_height,
                                       COGL_PIXEL_FORMAT_BGRA_8888,
                                       stride, data,
                                       &error);

      gst_video_meta_unmap (vmeta, 0, &info);

      if (tex != NULL)
        {
          ClutterGstOverlay *overlay = clutter_gst_overlay_new ();

          overlay->position.x1 = comp_x;
          overlay->position.y1 = comp_y;
          overlay->position.x2 = comp_x + comp_width;
          overlay->position.y2 = comp_y + comp_height;

          overlay->pipeline = cogl_pipeline_new (priv->ctx);
          cogl_pipeline_set_layer_texture (overlay->pipeline, 0, tex);

          cogl_object_unref (tex);

          g_ptr_array_add (priv->overlays->overlays, overlay);
        }
      else
        {
          GST_WARNING_OBJECT (sink,
                              "Cannot upload overlay texture : %s",
                              error->message);
          cogl_error_free (error);
        }
    }

  g_signal_emit (sink, video_sink_signals[NEW_OVERLAYS], 0);
}

/* Snippet cache */

static SnippetCacheEntry *
get_layer_cache_entry (ClutterGstVideoSink *sink,
                       SnippetCache *cache)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GList *l;

  for (l = cache->entries.head; l; l = l->next)
    {
      SnippetCacheEntry *entry = l->data;

      if (entry->start_position == priv->video_start)
        return entry;
    }

  return NULL;
}

static SnippetCacheEntry *
add_layer_cache_entry (ClutterGstVideoSink *sink,
                       SnippetCache *cache,
                       const char *decl)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  SnippetCacheEntry *entry = g_slice_new (SnippetCacheEntry);
  char *default_source;

  entry->start_position = priv->video_start;

  entry->vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                      decl,
                      NULL /* post */);
  entry->fragment_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                      decl,
                      NULL /* post */);

  default_source =
    g_strdup_printf ("  cogl_layer *= clutter_gst_sample_video%i "
                     "(cogl_tex_coord%i_in.st * clutter_gst_transform);\n",
                     priv->video_start,
                     priv->video_start);
  entry->default_sample_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
                      "uniform mat2 clutter_gst_transform;\n",
                      default_source);
  g_free (default_source);

  g_queue_push_head (&cache->entries, entry);

  return entry;
}

static SnippetCacheEntry *
get_global_cache_entry (SnippetCache *cache, int param)
{
  GList *l;

  for (l = cache->entries.head; l; l = l->next)
    {
      SnippetCacheEntry *entry = l->data;

      if (entry->start_position == param)
        return entry;
    }

  return NULL;
}

static SnippetCacheEntry *
add_global_cache_entry (SnippetCache *cache,
                        const char *decl,
                        int param)
{
  SnippetCacheEntry *entry = g_slice_new (SnippetCacheEntry);

  entry->start_position = param;

  entry->vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                      decl,
                      NULL /* post */);
  entry->fragment_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                      decl,
                      NULL /* post */);

  g_queue_push_head (&cache->entries, entry);

  return entry;
}

static void
setup_pipeline_from_cache_entry (ClutterGstVideoSink *sink,
                                 CoglPipeline *pipeline,
                                 SnippetCacheEntry *cache_entry,
                                 int n_layers)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  if (cache_entry)
    {
      int i;

      /* The global sampling function gets added to both the fragment
       * and vertex stages. The hope is that the GLSL compiler will
       * easily remove the dead code if it's not actually used */
      cogl_pipeline_add_snippet (pipeline, cache_entry->vertex_snippet);
      cogl_pipeline_add_snippet (pipeline, cache_entry->fragment_snippet);

      /* Set all of the layers to just directly copy from the previous
       * layer so that it won't redundantly generate code to sample
       * the intermediate textures */
      for (i = 0; i < n_layers; i++) {
        cogl_pipeline_set_layer_combine (pipeline,
                                         priv->video_start + i,
                                         "RGBA=REPLACE(PREVIOUS)",
                                         NULL);
      }

      if (priv->default_sample) {
        cogl_pipeline_add_layer_snippet (pipeline,
                                         priv->video_start + n_layers - 1,
                                         cache_entry->default_sample_snippet);
      }
    }

  priv->frame_dirty = TRUE;
}

/* Color balance */

#define DEFAULT_BRIGHTNESS (0.0f)
#define DEFAULT_CONTRAST   (1.0f)
#define DEFAULT_HUE        (0.0f)
#define DEFAULT_SATURATION (1.0f)

static gboolean
clutter_gst_video_sink_needs_color_balance_shader (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  return (priv->brightness != DEFAULT_BRIGHTNESS ||
          priv->contrast != DEFAULT_CONTRAST ||
          priv->hue != DEFAULT_HUE ||
          priv->saturation != DEFAULT_SATURATION);
}

static void
clutter_gst_video_sink_color_balance_update_tables (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  gint i, j;
  gdouble y, u, v, hue_cos, hue_sin;

  /* Y */
  for (i = 0; i < 256; i++) {
    y = 16 + ((i - 16) * priv->contrast + priv->brightness * 255);
    if (y < 0)
      y = 0;
    else if (y > 255)
      y = 255;
    priv->tabley[i] = rint (y);
  }

  hue_cos = cos (G_PI * priv->hue);
  hue_sin = sin (G_PI * priv->hue);

  /* U/V lookup tables are 2D, since we need both U/V for each table
   * separately. */
  for (i = -128; i < 128; i++) {
    for (j = -128; j < 128; j++) {
      u = 128 + ((i * hue_cos + j * hue_sin) * priv->saturation);
      v = 128 + ((-i * hue_sin + j * hue_cos) * priv->saturation);
      if (u < 0)
        u = 0;
      else if (u > 255)
        u = 255;
      if (v < 0)
        v = 0;
      else if (v > 255)
        v = 255;
      priv->tableu[(i + 128) * 256 + j + 128] = rint (u);
      priv->tablev[(i + 128) * 256 + j + 128] = rint (v);
    }
  }
}

static const GList *
clutter_gst_video_sink_color_balance_list_channels (GstColorBalance *balance)
{
  static GList *channels = NULL;

  if (channels == NULL) {
    const gchar *str_channels[4] = { "HUE", "SATURATION",
                                     "BRIGHTNESS", "CONTRAST"
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (str_channels); i++) {
      GstColorBalanceChannel *channel;

      channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
      channel->label = g_strdup (str_channels[i]);
      channel->min_value = -1000;
      channel->max_value = 1000;
      channels = g_list_append (channels, channel);
    }
  }

  return channels;
}

static gboolean
clutter_gst_video_sink_get_variable (ClutterGstVideoSink *sink,
                                     const gchar *variable,
                                     gdouble *minp,
                                     gdouble *maxp,
                                     gdouble **valuep)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  gdouble min, max, *value;

  if (!g_strcmp0 (variable, "BRIGHTNESS"))
    {
      min = -1.0;
      max = 1.0;
      value = &priv->brightness;
    }
  else if (!g_strcmp0 (variable, "CONTRAST"))
    {
      min = 0.0;
      max = 2.0;
      value = &priv->contrast;
    }
  else if (!g_strcmp0 (variable, "HUE"))
    {
      min = -1.0;
      max = 1.0;
      value = &priv->hue;
    }
  else if (!g_strcmp0 (variable, "SATURATION"))
    {
      min = 0.0;
      max = 2.0;
      value = &priv->saturation;
    }
  else
    {
      GST_WARNING_OBJECT (sink, "color balance parameter not supported %s",
                          variable);
      return FALSE;
    }

  if (maxp)
    *maxp = max;
  if (minp)
    *minp = min;
  if (valuep)
    *valuep = value;

  return TRUE;
}

static void
clutter_gst_video_sink_color_balance_set_value (GstColorBalance        *balance,
                                                GstColorBalanceChannel *channel,
                                                gint                    value)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (balance);
  gdouble *old_value, new_value, min, max;

  if (!clutter_gst_video_sink_get_variable (sink, channel->label,
                                            &min, &max, &old_value))
    return;

  new_value = (max - min) * ((gdouble) (value - channel->min_value) /
                             (gdouble) (channel->max_value - channel->min_value))
    + min;

  if (new_value != *old_value)
    {
      *old_value = new_value;
      sink->priv->balance_dirty = TRUE;

      gst_color_balance_value_changed (GST_COLOR_BALANCE (balance), channel,
                                       gst_color_balance_get_value (GST_COLOR_BALANCE (balance), channel));
    }
}

static gint
clutter_gst_video_sink_color_balance_get_value (GstColorBalance        *balance,
                                                GstColorBalanceChannel *channel)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (balance);
  gdouble *old_value, min, max;
  gint value;

  if (!clutter_gst_video_sink_get_variable (sink, channel->label,
                                            &min, &max, &old_value))
    return 0;

  value = (gint) (((*old_value + min) / (max - min)) *
                  (channel->max_value - channel->min_value))
    + channel->min_value;

  return value;
}

static GstColorBalanceType
clutter_gst_video_sink_color_balance_get_balance_type (GstColorBalance *balance)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
color_balance_iface_init (GstColorBalanceInterface *iface)
{
  iface->list_channels = clutter_gst_video_sink_color_balance_list_channels;
  iface->set_value = clutter_gst_video_sink_color_balance_set_value;
  iface->get_value = clutter_gst_video_sink_color_balance_get_value;

  iface->get_balance_type = clutter_gst_video_sink_color_balance_get_balance_type;
}

/**/

static void
clutter_gst_video_sink_navigation_send_event (GstNavigation *navigation,
                                              GstStructure  *structure)
{
  GstEvent *event;
  GstPad *pad = NULL;

  event = gst_event_new_navigation (structure);

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (navigation));

  if (GST_IS_PAD (pad))
    {
      gst_pad_send_event (pad, event);
      gst_object_unref (pad);
    }
  else
    gst_event_unref (event);
}

static void
navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event = clutter_gst_video_sink_navigation_send_event;
}

/**/

static void
clutter_gst_source_finalize (GSource *source)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  g_mutex_lock (&gst_source->buffer_lock);
  if (gst_source->buffer)
    gst_buffer_unref (gst_source->buffer);
  gst_source->buffer = NULL;
  g_mutex_unlock (&gst_source->buffer_lock);
  g_mutex_clear (&gst_source->buffer_lock);
}

void
clutter_gst_video_sink_attach_frame (ClutterGstVideoSink *sink,
                                     CoglPipeline *pln)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  guint i;

  for (i = 0; i < priv->renderer->n_layers; i++)
    if (priv->frame[i] != NULL)
      cogl_pipeline_set_layer_texture (pln, i + priv->video_start,
                                       priv->frame[i]);
}

/* Color balance */

static const gchar *no_color_balance_shader =
  "\n"
  "#define clutter_gst_get_corrected_color_from_yuv(arg) (arg)\n"
  "#define clutter_gst_get_corrected_color_from_rgb(arg) (arg)\n";

static const gchar *color_balance_shader =
  "\n"
  "vec3\n"
  "clutter_gst_rgb_to_yuv (vec3 rgb)\n"
  "{\n"
  "  return mat3 (0.2126, -0.114626,  0.5,\n"
  "               0.7152, -0.385428, -0.454153,\n"
  "               0.0722,  0.5,       0.045847 ) * rgb;\n"
  "}\n"
  "\n"
  /* " /\* bt601 version *\/\n" */
  /* "vec3\n" */
  /* "clutter_gst_rgb_to_yuv (vec3 rgb)\n" */
  /* "{\n" */
  /* "  return mat3 (0.299,  0.5,      -0.168736,\n" */
  /* "               0.587, -0.418688, -0.331264,\n" */
  /* "               0.114, -0.081312,  0.5      ) * rgb;\n" */
  /* "}\n" */
  /* "\n" */
  /* " /\* bt2020 version *\/\n" */
  /* "vec3\n" */
  /* "clutter_gst_rgb_to_bt2020 (vec3 rgb)\n" */
  /* "{\n" */
  /* "  return mat3 (0.2627, -0.139630,  0.503380,\n" */
  /* "               0.6780, -0.360370, -0.462893,\n" */
  /* "               0.0593,  0.5,      -0.040486 ) * rgb;\n" */
  /* "}\n" */

  "vec3\n"
  "clutter_gst_get_corrected_color_from_yuv (vec3 yuv)\n"
  "{\n"
  "  vec2 ruv = vec2 (yuv[2] + 0.5, yuv[1] + 0.5);\n"
  "  return vec3 (texture2D (cogl_sampler%i, vec2 (yuv[0], 0)).a,\n"
  "               texture2D (cogl_sampler%i, ruv).a - 0.5,\n"
  "               texture2D (cogl_sampler%i, ruv).a - 0.5);\n"
  "}\n"
  "\n"
  "vec3\n"
  "clutter_gst_get_corrected_color_from_rgb (vec3 rgb)\n"
  "{\n"
  "  vec3 yuv = clutter_gst_rgb_to_yuv (rgb);\n"
  "  vec3 corrected_yuv = vec3 (texture2D (cogl_sampler%i, vec2 (yuv[0], 0)).a,\n"
  "                             texture2D (cogl_sampler%i, vec2 (yuv[2], yuv[1])).a,\n"
  "                             texture2D (cogl_sampler%i, vec2 (yuv[2], yuv[1])).a);\n"
  "  return clutter_gst_yuv_to_rgb (corrected_yuv);\n"
  "}\n";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static void
clutter_gst_video_sink_setup_balance (ClutterGstVideoSink *sink,
                                      CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  static CoglSnippet *no_color_balance_snippet_vert = NULL,
    *no_color_balance_snippet_frag = NULL;

  GST_INFO_OBJECT (sink, "attaching correction b=%.3f/c=%.3f/h=%.3f/s=%.3f",
                   priv->brightness, priv->contrast,
                   priv->hue, priv->saturation);

  if (clutter_gst_video_sink_needs_color_balance_shader (sink))
    {
      int i;
      const guint8 *tables[3] = { priv->tabley, priv->tableu, priv->tablev };
      const gint tables_sizes[3][2] = { { 256, 1 },
                                        { 256, 256 },
                                        { 256, 256 } };
      SnippetCacheEntry *entry = get_layer_cache_entry (sink, &snippet_cache);

      GST_INFO_OBJECT (sink, "attaching shader color correction");

      if (entry == NULL)
        {
          gchar *source = g_strdup_printf (color_balance_shader,
                                           priv->custom_start,
                                           priv->custom_start + 1,
                                           priv->custom_start + 2,
                                           priv->custom_start,
                                           priv->custom_start + 1,
                                           priv->custom_start + 2);

          entry = add_layer_cache_entry (sink, &snippet_cache, source);
          g_free (source);
        }

      cogl_pipeline_add_snippet (pipeline, entry->vertex_snippet);
      cogl_pipeline_add_snippet (pipeline, entry->fragment_snippet);

      clutter_gst_video_sink_color_balance_update_tables (sink);

      for (i = 0; i < 3; i++)
        {
          CoglTexture *lut_texture =
            cogl_texture_2d_new_from_data (priv->ctx,
                                           tables_sizes[i][0],
                                           tables_sizes[i][1],
                                           COGL_PIXEL_FORMAT_A_8,
                                           tables_sizes[i][0],
                                           tables[i],
                                           NULL);

          cogl_pipeline_set_layer_filters (pipeline,
                                           priv->custom_start + i,
                                           COGL_PIPELINE_FILTER_LINEAR,
                                           COGL_PIPELINE_FILTER_LINEAR);
          cogl_pipeline_set_layer_combine (pipeline,
                                           priv->custom_start + i,
                                           "RGBA=REPLACE(PREVIOUS)",
                                           NULL);
          cogl_pipeline_set_layer_texture (pipeline,
                                           priv->custom_start + i,
                                           lut_texture);

          cogl_object_unref (lut_texture);
        }

      priv->video_start = priv->custom_start + 3;
    }
  else
    {
      GST_INFO_OBJECT (sink, "attaching null color correction");

      if (G_UNLIKELY (no_color_balance_snippet_vert == NULL))
        {
          no_color_balance_snippet_vert =
            cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                              no_color_balance_shader,
                              NULL);
          no_color_balance_snippet_frag =
            cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                              no_color_balance_shader,
                              NULL);
        }

      cogl_pipeline_add_snippet (pipeline, no_color_balance_snippet_vert);
      cogl_pipeline_add_snippet (pipeline, no_color_balance_snippet_frag);

      priv->video_start = priv->custom_start;
    }
}

#pragma GCC diagnostic pop

/* YUV <-> RGB conversions */

static const gchar *no_color_conversions_shader =
  "\n"
  "#define clutter_gst_yuv_to_rgb(color) (color)\n";
static const gchar *color_conversions_shader =
  "\n"
  "/* This conversion functions take : */\n"
  "/*   Y = [0, 1] */\n"
  "/*   U = [-0.5, 0.5] */\n"
  "/*   V = [-0.5, 0.5] */\n"
  "uniform mat3 clutter_gst_yuv_to_rgb_;\n"
  "#define clutter_gst_yuv_to_rgb(yuv) (clutter_gst_yuv_to_rgb_ * (yuv))\n"
  "\n";

static const float *
_matrix_from_colorimetry (GstVideoColorMatrix matrix)
{
  static const float bt601[] = { 1.0,    1.0,      1.0,
                                 0.0,   -0.344136, 1.772,
                                 1.402, -0.714136, 0.0    };
  static const float bt709[] = { 1.0,     1.0,      1.0,
                                 0.0,    -0.187324, 1.8556,
                                 1.5748, -0.468124, 0.0     };
#if GST_CHECK_VERSION(1, 6, 0)
  static const float bt2020[] = { 1.0,     1.0,      1.0,
                                  0.0,    -0.164553, 1.8814,
                                  1.4746, -0.571353, 0.0     };
#endif
#if 0
  /* Original transformation from the clutter-gst 2.0/1.6 days.
   * Still no idea where these values come from...
   */
  static const float original[] = { 1.0,         1.0,      1.0,
                                    0.0,        -0.390625, 2.015625,
                                    1.59765625, -0.8125,   0.0       };
#endif

  switch (matrix)
    {
#if GST_CHECK_VERSION(1, 6, 0)
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      return bt2020;
#endif
    case GST_VIDEO_COLOR_MATRIX_BT601:
      return bt601;
    case GST_VIDEO_COLOR_MATRIX_BT709:
    default:
      return bt709;
    }
}

static const gchar *
_shader_from_colorimetry (GstVideoColorMatrix matrix)
{
  if (matrix == GST_VIDEO_COLOR_MATRIX_RGB)
    return no_color_conversions_shader;
  return color_conversions_shader;
}

static void
clutter_gst_video_sink_setup_conversions (ClutterGstVideoSink *sink,
                                          CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstVideoColorMatrix matrix = priv->info.colorimetry.matrix;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry = get_global_cache_entry (&snippet_cache, matrix);

  if (entry == NULL)
    {
      const char *source = _shader_from_colorimetry (matrix);
      entry = add_global_cache_entry (&snippet_cache, source, matrix);
    }

  cogl_pipeline_add_snippet (pipeline, entry->vertex_snippet);
  cogl_pipeline_add_snippet (pipeline, entry->fragment_snippet);

  if (matrix != GST_VIDEO_COLOR_MATRIX_RGB)
    {
      gint yuv_to_rgb_location =
        cogl_pipeline_get_uniform_location (pipeline, "clutter_gst_yuv_to_rgb_");
      cogl_pipeline_set_uniform_matrix (pipeline, yuv_to_rgb_location,
                                        3, 1, FALSE,
                                        _matrix_from_colorimetry (matrix));
    }
}

/* Transformation for cropped videos */

static void
clutter_gst_video_sink_setup_transformation (ClutterGstVideoSink *sink,
                                             CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  float transform[] = { 1.0, 0.0,
                        0.0, 1.0 };
  gint transform_location;

  if (priv->frame[0] != NULL)
    {
      transform[0] = (float) GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 0) /
        (float) cogl_texture_get_width (priv->frame[0]);
      transform[3] = (float) GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 0) /
        (float) cogl_texture_get_height (priv->frame[0]);
    }
  transform_location =
    cogl_pipeline_get_uniform_location (pipeline, "clutter_gst_transform");
  cogl_pipeline_set_uniform_matrix (pipeline, transform_location,
                                    2, 1, FALSE, transform);
}

/**/

static gboolean
clutter_gst_source_prepare (GSource *source,
                            int *timeout)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  *timeout = -1;

  return gst_source->buffer != NULL;
}

static gboolean
clutter_gst_source_check (GSource *source)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  return (gst_source->buffer != NULL ||
          gst_source->sink->priv->balance_dirty);
}

static void
clutter_gst_video_sink_set_priority (ClutterGstVideoSink *sink,
                                     int priority)
{
  if (sink->priv->source)
    g_source_set_priority ((GSource *) sink->priv->source, priority);
}

static void
dirty_default_pipeline (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  if (priv->clt_frame != NULL)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->clt_frame);
      priv->clt_frame = NULL;
      priv->had_upload_once = FALSE;
    }
}

static void
clear_frame_textures (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (priv->frame); i++)
    {
      if (priv->frame[i] == NULL)
        break;
      else
        cogl_object_unref (priv->frame[i]);
    }

  memset (priv->frame, 0, sizeof (priv->frame));

  priv->frame_dirty = TRUE;
}

/**/

static gboolean
clutter_gst_dummy_upload_gl (ClutterGstVideoSink *sink, GstBuffer *buffer)
{
  return FALSE;
}

static void
clutter_gst_dummy_shutdown (ClutterGstVideoSink *sink)
{
}

/**/

static inline gboolean
is_pot (unsigned int number)
{
  /* Make sure there is only one bit set */
  return (number & (number - 1)) == 0;
}

/* This first tries to upload the texture to a CoglTexture2D, but
 * if that's not possible it falls back to a CoglTexture2DSliced.
 *
 * Auto-mipmapping of any uploaded texture is disabled
 */
static CoglTexture *
video_texture_new_from_data (CoglContext *ctx,
                             int width,
                             int height,
                             CoglPixelFormat format,
                             int rowstride,
                             const uint8_t *data)
{
  CoglTexture *tex;
  CoglError *internal_error = NULL;

  tex = cogl_texture_2d_new_from_data (ctx, width, height, format,
                                       rowstride, data, &internal_error);
  if (tex == NULL) {
    GST_WARNING ("Cannot create Cogl texture : %s", internal_error->message);
    cogl_error_free (internal_error);
    return NULL;
  }

  return tex;
}

static void
clutter_gst_rgb24_glsl_setup_pipeline (ClutterGstVideoSink *sink,
                                       CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry = get_layer_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "clutter_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  vec4 color = texture2D (cogl_sampler%i, UV);\n"
                         "  vec3 corrected = clutter_gst_get_corrected_color_from_rgb (color.rgb);\n"
                         "  return vec4(corrected.rgb, color.a);\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start);

      entry = add_layer_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static void
clutter_gst_rgb24_setup_pipeline (ClutterGstVideoSink *sink,
                                  CoglPipeline *pipeline)
{
  setup_pipeline_from_cache_entry (sink, pipeline, NULL, 1);
}

static gboolean
clutter_gst_rgb24_upload (ClutterGstVideoSink *sink,
                          GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglPixelFormat format;
  GstVideoFrame frame;

  if (priv->bgr)
    format = COGL_PIXEL_FORMAT_BGR_888;
  else
    format = COGL_PIXEL_FORMAT_RGB_888;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx,
                                                GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                                GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                                format,
                                                GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0),
                                                GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static ClutterGstRenderer rgb24_glsl_renderer =
  {
    "RGB 24",
    CLUTTER_GST_RGB24,
    CLUTTER_GST_RENDERER_NEEDS_GLSL,

    GST_STATIC_CAPS (MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "{ RGB, BGR }")),
    1, /* n_layers */
    clutter_gst_rgb24_glsl_setup_pipeline,
    clutter_gst_rgb24_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static ClutterGstRenderer rgb24_renderer =
  {
    "RGB 24",
    CLUTTER_GST_RGB24,
    0,
    GST_STATIC_CAPS (MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "{ RGB, BGR }")),
    1, /* n_layers */
    clutter_gst_rgb24_setup_pipeline,
    clutter_gst_rgb24_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static void
clutter_gst_rgb32_glsl_setup_pipeline (ClutterGstVideoSink *sink,
                                       CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry = get_layer_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "clutter_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  vec4 color = texture2D (cogl_sampler%i, UV);\n"
                         "  vec3 corrected = clutter_gst_get_corrected_color_from_rgb (color.rgb);\n"
                         /* Premultiply the color */
                         "  corrected.rgb *= color.a;\n"
                         "  return vec4(corrected.rgb, color.a);\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start);

      entry = add_layer_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static void
clutter_gst_rgb32_setup_pipeline (ClutterGstVideoSink *sink,
                                  CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  char *layer_combine;

  setup_pipeline_from_cache_entry (sink, pipeline, NULL, 1);

  /* Premultiply the texture using the a special layer combine */
  layer_combine = g_strdup_printf ("RGB=MODULATE(PREVIOUS, TEXTURE_%i[A])\n"
                                   "A=REPLACE(PREVIOUS[A])",
                                   priv->custom_start);
  cogl_pipeline_set_layer_combine (pipeline,
                                   priv->custom_start + 1,
                                   layer_combine,
                                   NULL);
  g_free(layer_combine);
}

static gboolean
clutter_gst_rgb32_upload (ClutterGstVideoSink *sink,
                          GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglPixelFormat format;
  GstVideoFrame frame;

  if (priv->bgr)
    format = COGL_PIXEL_FORMAT_BGRA_8888;
  else
    format = COGL_PIXEL_FORMAT_RGBA_8888;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx,
                                                GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                                GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                                format,
                                                GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0),
                                                GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglUserDataKey gl_upload_texture_private_key;
#define MAX_ALLOCATED_GL_TEXTURES (1)

static gboolean
clutter_gst_rgb32_upload_gl (ClutterGstVideoSink *sink,
                             GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstVideoGLTextureUploadMeta *upload_meta;
  gint i;
  guint gl_handle[1];

  //clear_frame_textures (sink);

  upload_meta = gst_buffer_get_video_gl_texture_upload_meta (buffer);
  if (!upload_meta) {
    GST_WARNING_OBJECT (sink,
                        "Buffer does not support GLTextureUploadMeta API");
    return FALSE;
  }

  if (upload_meta->n_textures != priv->renderer->n_layers ||
      upload_meta->texture_type[0] != GST_VIDEO_GL_TEXTURE_TYPE_RGBA) {
    GST_WARNING_OBJECT (sink,
                        "clutter-gst-video-sink only supports gl upload in a"
                        " single RGBA texture");
    return FALSE;
  }

  if (priv->frame[0] == NULL)
    {
      for (i = 0; i < MAX_ALLOCATED_GL_TEXTURES; i++) {
        priv->frame[i] = COGL_TEXTURE (cogl_texture_2d_new_with_size (priv->ctx,
                                                                      priv->info.width,
                                                                      priv->info.height));
        cogl_texture_set_components (priv->frame[i], COGL_TEXTURE_COMPONENTS_RGBA);

        if (!cogl_texture_allocate (priv->frame[i], NULL)) {
          GST_WARNING_OBJECT (sink, "Couldn't allocate cogl texture");
          return FALSE;
        }
      }
    }
  else
    {
      CoglTexture *tmp;

      tmp = priv->frame[0];
      for (i = 0; i < MAX_ALLOCATED_GL_TEXTURES - 1; i++)
        priv->frame[i] = priv->frame[i + 1];
      priv->frame[MAX_ALLOCATED_GL_TEXTURES - 1] = tmp;
    }

  cogl_object_set_user_data (COGL_OBJECT (priv->frame[0]),
                             &gl_upload_texture_private_key,
                             gst_buffer_ref (buffer),
                             (CoglUserDataDestroyCallback) gst_buffer_unref);


  if (!cogl_texture_get_gl_texture (priv->frame[0], &gl_handle[0], NULL)) {
    GST_WARNING_OBJECT (sink, "Couldn't get gl texture");
    return FALSE;
  }

  if (!gst_video_gl_texture_upload_meta_upload (upload_meta, gl_handle)) {
    GST_WARNING_OBJECT (sink, "GL texture upload failed");
    return FALSE;
  }

  return TRUE;
}

static ClutterGstRenderer rgb32_glsl_renderer =
  {
    "RGB 32",
    CLUTTER_GST_RGB32,
    CLUTTER_GST_RENDERER_NEEDS_GLSL,
    GST_STATIC_CAPS (
#ifdef HAVE_GL_TEXTURE_UPLOAD
                     MAKE_CAPS (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
                                "RGBA")
                     ";"
#endif
                     MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "{ RGBA, BGRA, RGBx, BGRx }")),
    1, /* n_layers */
    clutter_gst_rgb32_glsl_setup_pipeline,
    clutter_gst_rgb32_upload,
    clutter_gst_rgb32_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static ClutterGstRenderer rgb32_renderer =
  {
    "RGB 32",
    CLUTTER_GST_RGB32,
    0,
    GST_STATIC_CAPS (MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "{ RGBA, BGRA, RGBx, BGRx }")),
    2, /* n_layers */
    clutter_gst_rgb32_setup_pipeline,
    clutter_gst_rgb32_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static gboolean
clutter_gst_yv12_upload (ClutterGstVideoSink *sink,
                         GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglPixelFormat format = COGL_PIXEL_FORMAT_A_8;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));

  priv->frame[2] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 1),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 1),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 1),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 1));

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 2),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 2),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 2),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 2));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static gboolean
clutter_gst_i420_upload (ClutterGstVideoSink *sink,
                         GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglPixelFormat format = COGL_PIXEL_FORMAT_A_8;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 1),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 1),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 1),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 1));

  priv->frame[2] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 2),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 2),
                                 format,
                                 GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 2),
                                 GST_VIDEO_FRAME_PLANE_DATA (&frame, 2));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static void
clutter_gst_yv12_glsl_setup_pipeline (ClutterGstVideoSink *sink,
                                      CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_layer_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "clutter_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  float y = 1.1640625 * (texture2D (cogl_sampler%i, UV).a - 0.0625);\n"
                         "  float u = texture2D (cogl_sampler%i, UV).a - 0.5;\n"
                         "  float v = texture2D (cogl_sampler%i, UV).a - 0.5;\n"
                         "  vec3 corrected = clutter_gst_get_corrected_color_from_yuv (vec3 (y, u, v));\n"
                         "  vec4 color;\n"
                         "  color.rgb = clutter_gst_yuv_to_rgb (corrected);\n"
                         "  color.a = 1.0;\n"
                         "  return color;\n"
                         "}\n",
                         priv->video_start,
                         priv->video_start,
                         priv->video_start + 1,
                         priv->video_start + 2);

      entry = add_layer_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 3);
}

static ClutterGstRenderer yv12_glsl_renderer =
  {
    "YV12 glsl",
    CLUTTER_GST_YV12,
    CLUTTER_GST_RENDERER_NEEDS_GLSL,
    GST_STATIC_CAPS (MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "YV12")),
    3, /* n_layers */
    clutter_gst_yv12_glsl_setup_pipeline,
    clutter_gst_yv12_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static ClutterGstRenderer i420_glsl_renderer =
  {
    "I420 glsl",
    CLUTTER_GST_I420,
    CLUTTER_GST_RENDERER_NEEDS_GLSL,
    GST_STATIC_CAPS (MAKE_CAPS (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                                "I420")),
    3, /* n_layers */
    clutter_gst_yv12_glsl_setup_pipeline,
    clutter_gst_i420_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static void
clutter_gst_ayuv_glsl_setup_pipeline (ClutterGstVideoSink *sink,
                                      CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_layer_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source
        = g_strdup_printf ("vec4\n"
                           "clutter_gst_sample_video%i (vec2 UV)\n"
                           "{\n"
                           "  vec4 color = texture2D (cogl_sampler%i, UV);\n"
                           "  float y = 1.1640625 * (color.g - 0.0625);\n"
                           "  float u = color.b - 0.5;\n"
                           "  float v = color.a - 0.5;\n"
                           "  vec3 corrected = clutter_gst_get_corrected_color_from_yuv (vec3 (y, u, v));\n"
                           "  color.a = color.r;\n"
                           "  color.rgb = clutter_gst_yuv_to_rgb (corrected);\n"
                           /* Premultiply the color */
                           "  color.rgb *= color.a;\n"
                           "  return color;\n"
                           "}\n",
                           priv->video_start,
                           priv->video_start);

      entry = add_layer_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static gboolean
clutter_gst_ayuv_upload (ClutterGstVideoSink *sink,
                         GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglPixelFormat format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx,
                                                GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                                GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                                format,
                                                GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0),
                                                GST_VIDEO_FRAME_PLANE_DATA (&frame, 0));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static ClutterGstRenderer ayuv_glsl_renderer =
  {
    "AYUV glsl",
    CLUTTER_GST_AYUV,
    CLUTTER_GST_RENDERER_NEEDS_GLSL,
    GST_STATIC_CAPS (MAKE_CAPS(GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                               "AYUV")),
    1, /* n_layers */
    clutter_gst_ayuv_glsl_setup_pipeline,
    clutter_gst_ayuv_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static void
clutter_gst_nv12_glsl_setup_pipeline (ClutterGstVideoSink *sink,
                                      CoglPipeline *pipeline)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_layer_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "clutter_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  vec4 color;\n"
                         "  float y = 1.1640625 *\n"
                         "            (texture2D (cogl_sampler%i, UV).a -\n"
                         "             0.0625);\n"
                         "  vec2 uv = texture2D (cogl_sampler%i, UV).rg;\n"
                         "  uv -= 0.5;\n"
                         "  float u = uv.x;\n"
                         "  float v = uv.y;\n"
                         "  vec3 corrected = clutter_gst_get_corrected_color_from_yuv (vec3 (y, u, v));\n"
                         "  color.rgb = clutter_gst_yuv_to_rgb (corrected);\n"
                         "  color.a = 1.0;\n"
                         "  return color;\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start,
                         priv->custom_start + 1);

      entry = add_layer_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 2);
}

static gboolean
clutter_gst_nv12_upload (ClutterGstVideoSink *sink,
                         GstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 0),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 0),
                                 COGL_PIXEL_FORMAT_A_8,
                                 GST_VIDEO_FRAME_COMP_STRIDE (&frame, 0),
                                 GST_VIDEO_FRAME_COMP_DATA (&frame, 0));

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_FRAME_COMP_WIDTH (&frame, 1),
                                 GST_VIDEO_FRAME_COMP_HEIGHT (&frame, 1),
                                 COGL_PIXEL_FORMAT_RG_88,
                                 GST_VIDEO_FRAME_COMP_STRIDE (&frame, 1),
                                 GST_VIDEO_FRAME_COMP_DATA (&frame, 1));

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static ClutterGstRenderer nv12_glsl_renderer =
  {
    "NV12 glsl",
    CLUTTER_GST_NV12,
    CLUTTER_GST_RENDERER_NEEDS_GLSL | CLUTTER_GST_RENDERER_NEEDS_TEXTURE_RG,
    GST_STATIC_CAPS (MAKE_CAPS(GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY,
                               "NV12")),
    2, /* n_layers */
    clutter_gst_nv12_glsl_setup_pipeline,
    clutter_gst_nv12_upload,
    clutter_gst_dummy_upload_gl,
    clutter_gst_dummy_shutdown,
  };

static GSList*
clutter_gst_build_renderers_list (CoglContext *ctx)
{
  GSList *list = NULL;
  ClutterGstRendererFlag flags = 0;
  int i;
  static ClutterGstRenderer *const renderers[] =
    {
      /* These are in increasing order of priority so that the
       * priv->renderers will be in decreasing order. That way the GLSL
       * renderers will be preferred if they are available */
      &rgb24_renderer,
      &rgb32_renderer,
      &ayuv_glsl_renderer,
      &nv12_glsl_renderer,
      &yv12_glsl_renderer,
      &i420_glsl_renderer,
      &rgb24_glsl_renderer,
      &rgb32_glsl_renderer,
      NULL
    };

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_GLSL))
    flags |= CLUTTER_GST_RENDERER_NEEDS_GLSL;

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_RG))
    flags |= CLUTTER_GST_RENDERER_NEEDS_TEXTURE_RG;

  for (i = 0; renderers[i]; i++)
    if ((renderers[i]->flags & flags) == renderers[i]->flags)
      list = g_slist_prepend (list, renderers[i]);

  return list;
}

static void
append_cap (gpointer data,
            gpointer user_data)
{
  ClutterGstRenderer *renderer = (ClutterGstRenderer *) data;
  GstCaps *caps = (GstCaps *) user_data;
  guint i, count;
  GstCapsFeatures *features;
  GstCaps *writable_caps;
  GstCaps *writable_caps_with_composition;

  writable_caps =
    gst_caps_make_writable (gst_static_caps_get (&renderer->caps));

  writable_caps_with_composition = gst_caps_copy (writable_caps);
  count = gst_caps_get_size (writable_caps_with_composition);

  for (i = 0; i < count; i++) {
    features = gst_caps_get_features (writable_caps_with_composition, i);
    gst_caps_features_add (features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  }

  gst_caps_append (caps, writable_caps_with_composition);
  gst_caps_append (caps, writable_caps);
}

static GstCaps *
clutter_gst_build_caps (GSList *renderers)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();

  g_slist_foreach (renderers, append_cap, caps);

  return caps;
}

static ClutterGstRenderer *
clutter_gst_find_renderer_by_format (ClutterGstVideoSink *sink,
                                     ClutterGstVideoFormat format)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  ClutterGstRenderer *renderer = NULL;
  GSList *element;

  /* The renderers list is in decreasing order of priority so we'll
   * pick the first one that matches */
  for (element = priv->renderers; element; element = g_slist_next (element))
    {
      ClutterGstRenderer *candidate = (ClutterGstRenderer *) element->data;
      if (candidate->format == format)
        {
          renderer = candidate;
          break;
        }
    }

  return renderer;
}

static GstCaps *
clutter_gst_video_sink_get_caps (GstBaseSink *bsink,
                                 GstCaps *filter)
{
  ClutterGstVideoSink *sink;
  sink = CLUTTER_GST_VIDEO_SINK (bsink);

  GST_DEBUG_OBJECT (bsink, "Getting caps for %" GST_PTR_FORMAT, filter);

  if (sink->priv->caps == NULL)
    return NULL;

  if (filter != NULL)
    return gst_caps_intersect_full (filter, sink->priv->caps,
                                    GST_CAPS_INTERSECT_FIRST);
  return gst_caps_ref (sink->priv->caps);
}

static gboolean
clutter_gst_video_sink_parse_caps (GstCaps *caps,
                                   ClutterGstVideoSink *sink,
                                   gboolean save)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstCaps *intersection;
  GstVideoInfo vinfo;
  ClutterGstVideoFormat format;
  gboolean bgr = FALSE;
  ClutterGstRenderer *renderer;

  intersection = gst_caps_intersect (priv->caps, caps);
  if (gst_caps_is_empty (intersection))
    goto no_intersection;

  gst_caps_unref (intersection);

  if (!gst_video_info_from_caps (&vinfo, caps))
    goto unknown_format;

  switch (vinfo.finfo->format)
    {
    case GST_VIDEO_FORMAT_YV12:
      format = CLUTTER_GST_YV12;
      break;
    case GST_VIDEO_FORMAT_I420:
      format = CLUTTER_GST_I420;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      format = CLUTTER_GST_AYUV;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_NV12:
      format = CLUTTER_GST_NV12;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
      format = CLUTTER_GST_RGB24;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
      format = CLUTTER_GST_RGB24;
      bgr = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      format = CLUTTER_GST_RGB32;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      format = CLUTTER_GST_RGB32;
      bgr = TRUE;
      break;
    default:
      goto unhandled_format;
    }

  renderer = clutter_gst_find_renderer_by_format (sink, format);

  if (G_UNLIKELY (renderer == NULL))
    goto no_suitable_renderer;

  GST_INFO_OBJECT (sink, "found the %s renderer", renderer->name);

  if (save)
    {

      GST_INFO_OBJECT (sink, "saving infos");

      priv->info = vinfo;

      priv->format = format;
      priv->bgr = bgr;

      priv->renderer = renderer;
    }

  return TRUE;


 no_intersection:
  {
    GST_WARNING_OBJECT (sink,
                        "Incompatible caps, don't intersect with %" GST_PTR_FORMAT, priv->caps);
    return FALSE;
  }

 unknown_format:
  {
    GST_WARNING_OBJECT (sink, "Could not figure format of input caps");
    return FALSE;
  }

 unhandled_format:
  {
    GST_ERROR_OBJECT (sink, "Provided caps aren't supported by clutter-gst");
    return FALSE;
  }

 no_suitable_renderer:
  {
    GST_ERROR_OBJECT (sink, "could not find a suitable renderer");
    return FALSE;
  }
}

static gboolean
clutter_gst_video_sink_set_caps (GstBaseSink *bsink,
                                 GstCaps *caps)
{
  ClutterGstVideoSink *sink;
  ClutterGstVideoSinkPrivate *priv;

  sink = CLUTTER_GST_VIDEO_SINK (bsink);
  priv = sink->priv;

  GST_INFO_OBJECT (bsink, "Setting caps to %" GST_PTR_FORMAT, caps);

  if (!clutter_gst_video_sink_parse_caps (caps, sink, FALSE))
    return FALSE;

  g_mutex_lock (&priv->source->buffer_lock);
  priv->source->has_new_caps = TRUE;
  g_mutex_unlock (&priv->source->buffer_lock);

  return TRUE;
}

static gboolean
clutter_gst_source_dispatch (GSource *source,
                             GSourceFunc callback,
                             void *user_data)
{
  ClutterGstSource *gst_source= (ClutterGstSource*) source;
  ClutterGstVideoSinkPrivate *priv = gst_source->sink->priv;
  GstBuffer *buffer;
  gboolean pipeline_ready = FALSE;
  gboolean caps_parsed;

  g_mutex_lock (&gst_source->buffer_lock);

  if (G_UNLIKELY (gst_source->has_new_caps))
    {
      GstCaps *caps =
        gst_pad_get_current_caps (GST_BASE_SINK_PAD ((GST_BASE_SINK
                                                      (gst_source->sink))));

      caps_parsed = clutter_gst_video_sink_parse_caps (caps, gst_source->sink, TRUE);
      gst_caps_unref (caps);

      if (!caps_parsed)
        goto negotiation_fail;

      gst_source->has_new_caps = FALSE;

      clear_frame_textures (gst_source->sink);
      dirty_default_pipeline (gst_source->sink);

      /* We are now in a state where we could generate the pipeline if
       * the application requests it so we can emit the signal.
       * However we'll actually generate the pipeline lazily only if
       * the application actually asks for it. */
      pipeline_ready = TRUE;
    }

  buffer = gst_source->buffer;
  gst_source->buffer = NULL;

  g_mutex_unlock (&gst_source->buffer_lock);

  if (buffer)
    {
      if (gst_buffer_get_video_gl_texture_upload_meta (buffer) != NULL) {
        GST_DEBUG_OBJECT (gst_source->sink,
                          "Trying to upload buffer %p with GL using renderer %s",
                          buffer, priv->renderer->name);
        if (!priv->renderer->upload_gl (gst_source->sink, buffer)) {
          goto fail_upload;
        }
      } else {
        GST_DEBUG_OBJECT (gst_source->sink,
                          "Trying to upload buffer %p with software using renderer %s",
                          buffer, priv->renderer->name);
        if (!priv->renderer->upload (gst_source->sink, buffer))
          goto fail_upload;
      }

      clutter_gst_video_sink_upload_overlay (gst_source->sink, buffer);

      priv->had_upload_once = TRUE;

      gst_buffer_unref (buffer);
    }
  else
    GST_WARNING_OBJECT (gst_source->sink, "No buffers available for display");

  if (G_UNLIKELY (pipeline_ready))
    g_signal_emit (gst_source->sink,
                   video_sink_signals[PIPELINE_READY],
                   0 /* detail */);
  if (priv->had_upload_once)
    g_signal_emit (gst_source->sink,
                   video_sink_signals[NEW_FRAME], 0,
                   NULL);

  return TRUE;


 negotiation_fail:
  {
    GST_WARNING_OBJECT (gst_source->sink,
                        "Failed to handle caps. Stopping GSource");
    priv->flow_return = GST_FLOW_NOT_NEGOTIATED;
    g_mutex_unlock (&gst_source->buffer_lock);

    return FALSE;
  }

 fail_upload:
  {
    GST_WARNING_OBJECT (gst_source->sink, "Failed to upload buffer");
    priv->flow_return = GST_FLOW_ERROR;
    gst_buffer_unref (buffer);
    return FALSE;
  }
}

static GSourceFuncs gst_source_funcs =
  {
    clutter_gst_source_prepare,
    clutter_gst_source_check,
    clutter_gst_source_dispatch,
    clutter_gst_source_finalize
  };

static ClutterGstSource *
clutter_gst_source_new (ClutterGstVideoSink *sink)
{
  GSource *source;
  ClutterGstSource *gst_source;

  source = g_source_new (&gst_source_funcs, sizeof (ClutterGstSource));
  gst_source = (ClutterGstSource *) source;

  g_source_set_can_recurse (source, TRUE);
  g_source_set_priority (source, CLUTTER_GST_DEFAULT_PRIORITY);

  gst_source->sink = sink;
  g_mutex_init (&gst_source->buffer_lock);
  gst_source->buffer = NULL;

  return gst_source;
}

static void
clutter_gst_video_sink_init (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv;

  sink->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (sink,
                                                   CLUTTER_GST_TYPE_VIDEO_SINK,
                                                   ClutterGstVideoSinkPrivate);
  priv->custom_start = 0;
  priv->default_sample = TRUE;

  priv->brightness = DEFAULT_BRIGHTNESS;
  priv->contrast = DEFAULT_CONTRAST;
  priv->hue = DEFAULT_HUE;
  priv->saturation = DEFAULT_SATURATION;

  priv->tabley = g_new0 (guint8, 256);
  priv->tableu = g_new0 (guint8, 256 * 256);
  priv->tablev = g_new0 (guint8, 256 * 256);

  priv->overlays = clutter_gst_overlays_new ();
}

static GstFlowReturn
_clutter_gst_video_sink_render (GstBaseSink *bsink,
                                GstBuffer *buffer)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (bsink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  ClutterGstSource *gst_source = priv->source;

  g_mutex_lock (&gst_source->buffer_lock);

  if (G_UNLIKELY (priv->flow_return != GST_FLOW_OK))
    goto dispatch_flow_ret;

  if (gst_source->buffer)
    gst_buffer_unref (gst_source->buffer);

  gst_source->buffer = gst_buffer_ref (buffer);
  g_mutex_unlock (&gst_source->buffer_lock);

  g_main_context_wakeup (NULL);

  return GST_FLOW_OK;

 dispatch_flow_ret:
  {
    g_mutex_unlock (&gst_source->buffer_lock);
    return priv->flow_return;
  }
}

static GstFlowReturn
_clutter_gst_video_sink_show_frame (GstVideoSink *vsink,
                                    GstBuffer *buffer)
{
  return _clutter_gst_video_sink_render (GST_BASE_SINK (vsink), buffer);
}

static void
clutter_gst_video_sink_dispose (GObject *object)
{
  ClutterGstVideoSink *self;
  ClutterGstVideoSinkPrivate *priv;

  self = CLUTTER_GST_VIDEO_SINK (object);
  priv = self->priv;

  if (priv->caps)
    {
      gst_caps_unref (priv->caps);
      priv->caps = NULL;
    }

  if (priv->tabley)
    {
      g_free (priv->tabley);
      priv->tabley = NULL;
    }

  if (priv->tableu)
    {
      g_free (priv->tableu);
      priv->tableu = NULL;
    }

  if (priv->tablev)
    {
      g_free (priv->tablev);
      priv->tablev = NULL;
    }

  if (priv->renderers)
    {
      g_slist_free (priv->renderers);
      priv->renderers = NULL;
    }

  if (priv->overlays)
    {
      g_boxed_free (CLUTTER_GST_TYPE_OVERLAYS, priv->overlays);
      priv->overlays = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_video_sink_parent_class)->dispose (object);
}

static void
clutter_gst_video_sink_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_video_sink_parent_class)->finalize (object);
}

static gboolean
clutter_gst_video_sink_start (GstBaseSink *base_sink)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (base_sink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  GST_INFO_OBJECT (sink, "Start");

  priv->ctx = clutter_gst_get_cogl_context ();
  priv->renderers = clutter_gst_build_renderers_list (priv->ctx);
  priv->caps = clutter_gst_build_caps (priv->renderers);

  priv->source = clutter_gst_source_new (sink);
  g_source_attach ((GSource *) priv->source, NULL);
  priv->flow_return = GST_FLOW_OK;
  return TRUE;
}

static gboolean
clutter_gst_video_sink_stop (GstBaseSink *base_sink)
{
  ClutterGstVideoSink *self = CLUTTER_GST_VIDEO_SINK (base_sink);
  ClutterGstVideoSinkPrivate *priv = self->priv;

  GST_INFO_OBJECT (self, "Stop");

  if (priv->source)
    {
      GSource *source = (GSource *) priv->source;
      g_source_destroy (source);
      g_source_unref (source);
      priv->source = NULL;
    }

  clear_frame_textures (self);

  if (priv->renderer)
    {
      priv->renderer->shutdown (self);
      priv->renderer = NULL;
    }

  if (priv->clt_frame)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->clt_frame);
      priv->clt_frame = NULL;
    }

  return TRUE;
}

static void
clutter_gst_video_sink_set_property (GObject *object,
                                     unsigned int prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (object);

  switch (prop_id)
    {
    case PROP_UPDATE_PRIORITY:
      clutter_gst_video_sink_set_priority (sink, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_gst_video_sink_get_property (GObject *object,
                                     unsigned int prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (object);
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  switch (prop_id)
    {
    case PROP_UPDATE_PRIORITY:
      g_value_set_int (value, g_source_get_priority ((GSource *) priv->source));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
clutter_gst_video_sink_propose_allocation (GstBaseSink *base_sink, GstQuery *query)
{
  gboolean need_pool = FALSE;
  GstCaps *caps = NULL;

  gst_query_parse_allocation (query, &caps, &need_pool);

  gst_query_add_allocation_meta (query,
                                 GST_VIDEO_META_API_TYPE, NULL);
#ifdef HAVE_GL_TEXTURE_UPLOAD

  gst_query_add_allocation_meta (query,
                                 GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);
#endif
  gst_query_add_allocation_meta (query,
                                 GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
clutter_gst_video_sink_event (GstBaseSink * basesink, GstEvent * event)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (basesink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  ClutterGstSource *gst_source = priv->source;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      g_mutex_lock (&gst_source->buffer_lock);
      if (gst_source->buffer) {
        GST_DEBUG_OBJECT (sink, "Freeing existing buffer %p", gst_source->buffer);
        gst_buffer_unref (gst_source->buffer);
        gst_source->buffer = NULL;
      }
      g_mutex_unlock (&gst_source->buffer_lock);
      break;

    default:
      break;
  }

  return GST_BASE_SINK_CLASS (clutter_gst_video_sink_parent_class)->event (basesink, event);
}

static void
clutter_gst_video_sink_class_init (ClutterGstVideoSinkClass *klass)
{
  GObjectClass *go_class = G_OBJECT_CLASS (klass);
  GstVideoSinkClass *gv_class = GST_VIDEO_SINK_CLASS (klass);
  GstBaseSinkClass *gb_class = GST_BASE_SINK_CLASS (klass);
  GstElementClass *ge_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *pad_template;
  GParamSpec *pspec;

  GST_DEBUG_CATEGORY_INIT (clutter_gst_video_sink_debug,
                           "cluttervideosink",
                           0,
                           "clutter video sink");

  g_type_class_add_private (klass, sizeof (ClutterGstVideoSinkPrivate));
  go_class->set_property = clutter_gst_video_sink_set_property;
  go_class->get_property = clutter_gst_video_sink_get_property;
  go_class->dispose = clutter_gst_video_sink_dispose;
  go_class->finalize = clutter_gst_video_sink_finalize;

  pad_template = gst_static_pad_template_get (&sinktemplate_all);
  gst_element_class_add_pad_template (ge_class, pad_template);

  gst_element_class_set_metadata (ge_class,
                                  "Clutter video sink", "Sink/Video",
                                  "Sends video data from GStreamer to a "
                                  "Cogl pipeline",
                                  "Jonathan Matthew <jonathan@kaolin.wh9.net>, "
                                  "Matthew Allum <mallum@o-hand.com, "
                                  "Chris Lord <chris@o-hand.com>, "
                                  "Plamena Manolova "
                                  "<plamena.n.manolova@intel.com>");

  gb_class->render = _clutter_gst_video_sink_render;
  gb_class->preroll = _clutter_gst_video_sink_render;
  gb_class->start = clutter_gst_video_sink_start;
  gb_class->stop = clutter_gst_video_sink_stop;
  gb_class->set_caps = clutter_gst_video_sink_set_caps;
  gb_class->get_caps = clutter_gst_video_sink_get_caps;
  gb_class->propose_allocation = clutter_gst_video_sink_propose_allocation;
  gb_class->event = clutter_gst_video_sink_event;

  gv_class->show_frame = _clutter_gst_video_sink_show_frame;

  pspec = g_param_spec_int ("update-priority",
                            "Update Priority",
                            "Priority of video updates in the thread",
                            -G_MAXINT, G_MAXINT,
                            CLUTTER_GST_DEFAULT_PRIORITY,
                            CLUTTER_GST_PARAM_READWRITE);

  g_object_class_install_property (go_class, PROP_UPDATE_PRIORITY, pspec);

  /**
   * ClutterGstVideoSink::pipeline-ready:
   * @sink: the #ClutterGstVideoSink
   *
   * The sink will emit this signal as soon as it has gathered enough
   * information from the video to configure a pipeline. If the
   * application wants to do some customized rendering, it can setup its
   * pipeline after this signal is emitted. The application's pipeline
   * will typically either be a copy of the one returned by
   * clutter_gst_video_sink_get_pipeline() or it can be a completely custom
   * pipeline which is setup using clutter_gst_video_sink_setup_pipeline().
   *
   * Note that it is an error to call either of those functions before
   * this signal is emitted. The #ClutterGstVideoSink::new-frame signal
   * will only be emitted after the pipeline is ready so the application
   * could also create its pipeline in the handler for that.
   *
   * Since: 3.0
   */
  video_sink_signals[PIPELINE_READY] =
    g_signal_new ("pipeline-ready",
                  CLUTTER_GST_TYPE_VIDEO_SINK,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstVideoSinkClass, pipeline_ready),
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0 /* n_params */);

  /**
   * ClutterGstVideoSink::new-frame:
   * @sink: the #ClutterGstVideoSink
   *
   * The sink will emit this signal whenever there are new textures
   * available for a new frame of the video. After this signal is
   * emitted, an application can call clutter_gst_video_sink_get_pipeline()
   * to get a pipeline suitable for rendering the frame. If the
   * application is using a custom pipeline it can alternatively call
   * clutter_gst_video_sink_attach_frame() to attach the textures.
   *
   * Since: 3.0
   */
  video_sink_signals[NEW_FRAME] =
    g_signal_new ("new-frame",
                  CLUTTER_GST_TYPE_VIDEO_SINK,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstVideoSinkClass, new_frame),
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0 /* n_params */);

  /**
   * ClutterGstVideoSink::new-overlays:
   * @sink: the #ClutterGstVideoSink
   *
   * The sink will emit this signal whenever there are new textures
   * available for set of overlays on the video. After this signal is
   * emitted, an application can call
   * clutter_gst_video_sink_get_overlays() to get a set of pipelines
   * suitable for rendering overlays on a video frame.
   *
   * Since: 3.0
   */
  video_sink_signals[NEW_OVERLAYS] =
    g_signal_new ("new-overlays",
                  CLUTTER_GST_TYPE_VIDEO_SINK,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstVideoSinkClass, new_overlays),
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0 /* n_params */);
}

/**
 * clutter_gst_video_sink_new:
 *
 * Creates a new #ClutterGstVideoSink
 *
 * Return value: (transfer full): a new #ClutterGstVideoSink
 * Since: 3.0
 */
ClutterGstVideoSink *
clutter_gst_video_sink_new (void)
{
  ClutterGstVideoSink *sink = g_object_new (CLUTTER_GST_TYPE_VIDEO_SINK, NULL);

  return sink;
}

void
clutter_gst_video_sink_get_aspect (ClutterGstVideoSink *sink, gint *par_n, gint *par_d)
{
  GstVideoInfo *info;

  g_return_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink));

  info = &sink->priv->info;

  if (par_n)
    *par_n = info->par_n;
  if (par_d)
    *par_d = info->par_d;
}

/**
 * clutter_gst_video_sink_is_ready:
 * @sink: The #ClutterGstVideoSink
 *
 * Returns whether the pipeline is ready and so
 * clutter_gst_video_sink_get_pipeline() and
 * clutter_gst_video_sink_setup_pipeline() can be called without causing error.
 *
 * Note: Normally an application will wait until the
 * #ClutterGstVideoSink::pipeline-ready signal is emitted instead of
 * polling the ready status with this api, but sometimes when a sink
 * is passed between components that didn't have an opportunity to
 * connect a signal handler this can be useful.
 *
 * Return value: %TRUE if the sink is ready, else %FALSE
 * Since: 3.0
 */
gboolean
clutter_gst_video_sink_is_ready (ClutterGstVideoSink *sink)
{
  g_return_val_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink), FALSE);

  return !!sink->priv->renderer;
}

/**
 * clutter_gst_video_sink_get_frame:
 * @sink: The #ClutterGstVideoSink
 *
 * Returns a #ClutterGstFrame object suitable to render the current
 * frame of the given video sink. An application is free to make a
 * copy of this pipeline and modify it for custom rendering.
 *
 * Return value: (transfer none): A #ClutterGstFrame or NULL if there
 *   isn't a frame to be displayed yet.
 *
 * Since: 3.0
 */
ClutterGstFrame *
clutter_gst_video_sink_get_frame (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink), NULL);

  if (!clutter_gst_video_sink_is_ready (sink))
    return NULL;

  priv = sink->priv;

  if (priv->clt_frame == NULL)
    {
      priv->clt_frame = clutter_gst_frame_new ();
      clutter_gst_video_resolution_from_video_info (&priv->clt_frame->resolution,
                                                    &priv->info);
      priv->clt_frame->pipeline = cogl_pipeline_new (priv->ctx);
      clutter_gst_video_sink_setup_pipeline (sink, priv->clt_frame->pipeline);
      clutter_gst_video_sink_attach_frame (sink, priv->clt_frame->pipeline);
      priv->balance_dirty = FALSE;
    }
  else if (priv->balance_dirty)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->clt_frame);

      priv->clt_frame = clutter_gst_frame_new ();
      clutter_gst_video_resolution_from_video_info (&priv->clt_frame->resolution,
                                                    &priv->info);
      priv->clt_frame->pipeline = cogl_pipeline_new (priv->ctx);

      clutter_gst_video_sink_setup_pipeline (sink, priv->clt_frame->pipeline);
      clutter_gst_video_sink_attach_frame (sink, priv->clt_frame->pipeline);
      priv->balance_dirty = FALSE;
    }
  else if (priv->frame_dirty)
    {
      clutter_gst_video_resolution_from_video_info (&priv->clt_frame->resolution,
                                                    &priv->info);
      clutter_gst_video_sink_attach_frame (sink, priv->clt_frame->pipeline);
    }

  priv->frame_dirty = FALSE;

  return priv->clt_frame;
}

/**
 * clutter_gst_video_sink_get_pipeline:
 * @sink: The #ClutterGstVideoSink
 *
 * Returns a pipeline suitable for rendering the current frame of the
 * given video sink. The pipeline will already have the textures for
 * the frame attached. For simple rendering, an application will
 * typically call this function immediately before it paints the
 * video. It can then just paint a rectangle using the returned
 * pipeline.
 *
 * An application is free to make a copy of this
 * pipeline and modify it for custom rendering.
 *
 * Note: it is considered an error to call this function before the
 * #ClutterGstVideoSink::pipeline-ready signal is emitted.
 *
 * Return value: (transfer none): the pipeline for rendering the
 *   current frame
 * Since: 3.0
 */
CoglPipeline *
clutter_gst_video_sink_get_pipeline (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv;

  priv = sink->priv;

  g_return_val_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink), NULL);

  if (clutter_gst_video_sink_get_frame (sink) == NULL)
    return NULL;

  return priv->clt_frame->pipeline;
}

/**
 * clutter_gst_video_sink_setup_pipeline:
 * @sink: The #ClutterGstVideoSink
 * @pipeline: A #CoglPipeline
 *
 * Configures the given pipeline so that will be able to render the
 * video for the @sink. This should only be used if the application
 * wants to perform some custom rendering using its own pipeline.
 * Typically an application will call this in response to the
 * #ClutterGstVideoSink::pipeline-ready signal.
 *
 * Note: it is considered an error to call this function before the
 * #ClutterGstVideoSink::pipeline-ready signal is emitted.
 *
 * Since: 3.0
 */
void
clutter_gst_video_sink_setup_pipeline (ClutterGstVideoSink *sink,
                                       CoglPipeline        *pipeline)
{
  ClutterGstVideoSinkPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink));
  g_return_if_fail (pipeline != NULL);

  priv = sink->priv;

  if (priv->renderer)
    {
      clutter_gst_video_sink_setup_conversions (sink, pipeline);
      clutter_gst_video_sink_setup_balance (sink, pipeline);
      clutter_gst_video_sink_setup_transformation (sink, pipeline);
      priv->renderer->setup_pipeline (sink, pipeline);
    }
}


ClutterGstOverlays *
clutter_gst_video_sink_get_overlays (ClutterGstVideoSink *sink)
{
  g_return_val_if_fail (CLUTTER_GST_IS_VIDEO_SINK (sink), NULL);

  return sink->priv->overlays;
}

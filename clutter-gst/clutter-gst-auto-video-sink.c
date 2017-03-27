/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Jan Schmidt <thaytan@noraisin.net>
 * (c) 2013 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "clutter-gst-aspectratio.h"
#include "clutter-gst-auto-video-sink.h"
#include "clutter-gst-util.h"

GST_DEBUG_CATEGORY (clutter_gst_auto_video_sink_debug);
#define GST_CAT_DEFAULT clutter_gst_auto_video_sink_debug

#define DEFAULT_TS_OFFSET           0

/* Properties */
enum
{
  PROP_0,
  PROP_TS_OFFSET,
  PROP_CONTENT,
};

static GstStateChangeReturn
clutter_gst_auto_video_sink_change_state (GstElement     *element,
                                          GstStateChange  transition);
static void clutter_gst_auto_video_sink_dispose (ClutterGstAutoVideoSink3 *sink);
static void clutter_gst_auto_video_sink_clear_kid (ClutterGstAutoVideoSink3 *sink);

static void clutter_gst_auto_video_sink_set_property (GObject      *object,
                                                      guint         prop_id,
                                                      const GValue *value,
                                                      GParamSpec   *pspec);
static void clutter_gst_auto_video_sink_get_property (GObject    *object,
                                                      guint       prop_id,
                                                      GValue     *value,
                                                      GParamSpec *pspec);

#define clutter_gst_auto_video_sink_parent_class parent_class
G_DEFINE_TYPE (ClutterGstAutoVideoSink3,
               clutter_gst_auto_video_sink,
               GST_TYPE_BIN)

static ClutterInitError _clutter_initialized = CLUTTER_INIT_ERROR_UNKNOWN;

static void
_clutter_init (void)
{
  /* We must ensure that clutter is initialized */
  _clutter_initialized = clutter_init (NULL, NULL);
  if (_clutter_initialized != CLUTTER_INIT_SUCCESS)
    g_critical ("Unable to initialize Clutter");
}

static void
clutter_gst_auto_video_sink_class_init (ClutterGstAutoVideoSink3Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (clutter_gst_auto_video_sink_debug,
                           "clutterautovideosink",
                           0,
                           "clutter auto video sink");

  gobject_class->dispose = (GObjectFinalizeFunc) clutter_gst_auto_video_sink_dispose;
  gobject_class->set_property = clutter_gst_auto_video_sink_set_property;
  gobject_class->get_property = clutter_gst_auto_video_sink_get_property;

  eklass->change_state = GST_DEBUG_FUNCPTR (clutter_gst_auto_video_sink_change_state);

  g_object_class_install_property (gobject_class,
                                   PROP_TS_OFFSET,
                                   g_param_spec_int64 ("ts-offset",
                                                       "TS Offset",
                                                       "Timestamp offset in nanoseconds",
                                                       G_MININT64,
                                                       G_MAXINT64,
                                                       DEFAULT_TS_OFFSET,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_CONTENT,
                                   g_param_spec_object ("content",
                                                        "Clutter Content",
                                                        "Clutter Content",
                                                        CLUTTER_GST_TYPE_CONTENT,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  if (_clutter_initialized == CLUTTER_INIT_SUCCESS) {
    GstElement *clutter_sink = clutter_gst_create_video_sink ();

    gst_element_class_add_pad_template (eklass,
                                        gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (clutter_sink),
                                                                            "sink"));
    gst_element_class_set_static_metadata (eklass,
                                           "Clutter Auto Video Sink",
                                           "Sink/Video",
                                           "Video sink using the Clutter scene graph as output",
                                           "Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>");

    g_object_unref (clutter_sink);
  }
}

static void
clutter_gst_auto_video_sink_dispose (ClutterGstAutoVideoSink3 *sink)
{
  clutter_gst_auto_video_sink_clear_kid (sink);

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) sink);
}

static void
clutter_gst_auto_video_sink_clear_kid (ClutterGstAutoVideoSink3 *sink)
{
  if (sink->kid)
    {
      gst_element_set_state (sink->kid, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (sink), sink->kid);
      sink->kid = NULL;
      /* Don't lose the SINK flag */
      GST_OBJECT_FLAG_SET (sink, GST_ELEMENT_FLAG_SINK);
    }

  if (sink->content)
    {
      clutter_gst_content_set_sink (CLUTTER_GST_CONTENT (sink->content), NULL);
      g_clear_object (&sink->content);
    }
}

/*
 * Hack to make initial linking work; ideally, this'd work even when
 * no target has been assigned to the ghostpad yet.
 */

static void
clutter_gst_auto_video_sink_reset (ClutterGstAutoVideoSink3 *sink)
{
  GstPad *targetpad;

  /* Remove any existing element */
  clutter_gst_auto_video_sink_clear_kid (sink);

  /* video sink */
  sink->kid = clutter_gst_create_video_sink ();
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* pad, setting this target should always work */
  targetpad = gst_element_get_static_pad (sink->kid, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad))
    g_warning ("Couldn't link ghostpad's to target pad");
  gst_object_unref (targetpad);
}

static void
clutter_gst_auto_video_sink_init (ClutterGstAutoVideoSink3 *sink)
{
  sink->ts_offset = DEFAULT_TS_OFFSET;

  sink->pad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  clutter_gst_auto_video_sink_reset (sink);

  /* mark as sink */
  GST_OBJECT_FLAG_SET (sink, GST_ELEMENT_FLAG_SINK);
}

static void
clutter_gst_auto_video_sink_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  ClutterGstAutoVideoSink3 *sink = CLUTTER_GST_AUTO_VIDEO_SINK (object);

  switch (prop_id)
    {
    case PROP_TS_OFFSET:
      sink->ts_offset = g_value_get_int64 (value);
      if (sink->kid)
        g_object_set_property (G_OBJECT (sink->kid), pspec->name, value);
      break;
    case PROP_CONTENT:
      g_clear_object (&sink->content);
      sink->content = g_value_dup_object (value);
      if (sink->content && sink->kid)
        clutter_gst_content_set_sink (CLUTTER_GST_CONTENT (sink->content),
                                      CLUTTER_GST_VIDEO_SINK (sink->kid));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_gst_auto_video_sink_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  ClutterGstAutoVideoSink3 *sink = CLUTTER_GST_AUTO_VIDEO_SINK (object);

  switch (prop_id)
    {
    case PROP_TS_OFFSET:
      g_value_set_int64 (value, sink->ts_offset);
      break;
    case PROP_CONTENT:
      g_value_set_object (value, sink->content);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GstStateChangeReturn
clutter_gst_auto_video_sink_change_state (GstElement     *element,
                                          GstStateChange  transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  ClutterGstAutoVideoSink3 *sink = CLUTTER_GST_AUTO_VIDEO_SINK (element);

  switch (transition) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    if (_clutter_initialized != CLUTTER_INIT_SUCCESS)
      _clutter_init ();

    if (!sink->content)
      {
        ClutterColor color = { 0, 0, 0, 0xff };
        ClutterActor *stage = g_object_new (CLUTTER_TYPE_STAGE,
                                            "background-color", &color,
                                            NULL);
        ClutterActor *actor = clutter_actor_new ();
        sink->content = clutter_gst_aspectratio_new ();

        clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);
        clutter_actor_set_layout_manager (stage,
                                          clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_FILL,
                                                                  CLUTTER_BIN_ALIGNMENT_FILL));

        clutter_actor_add_child (stage, actor);

        clutter_actor_set_content (actor, sink->content);
        clutter_actor_show (stage);
      }
    clutter_gst_content_set_sink (CLUTTER_GST_CONTENT (sink->content),
                                  CLUTTER_GST_VIDEO_SINK (sink->kid));
    break;
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

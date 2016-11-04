/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-player.c - Wrap some convenience functions around playbin
 *
 * Authored By Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Matthew Allum     <mallum@openedhand.com>
 *             Emmanuele Bassi   <ebassi@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2009-2013 Intel Corporation
 * Copyright (C) 2012 Collabora Ltd. <http://www.collabora.co.uk/>
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
 * SECTION:clutter-gst-player
 * @short_description: An interface for controlling playback of media data
 *
 * #ClutterGstPlayer is an interface for controlling playback of media
 *  sources.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-player.h"
#include "clutter-gst-private.h"

typedef ClutterGstPlayerIface ClutterGstPlayerInterface;

G_DEFINE_INTERFACE (ClutterGstPlayer, clutter_gst_player, G_TYPE_OBJECT)

enum
{
  NEW_FRAME,
  READY_SIGNAL,
  EOS_SIGNAL,
  SIZE_CHANGE,
  ERROR_SIGNAL, /* can't be called 'ERROR' otherwise it clashes with wingdi.h */

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
clutter_gst_player_default_init (ClutterGstPlayerIface *iface)
{
  GParamSpec *pspec;

  /**
   * ClutterGstPlayer:playing:
   *
   * Whether the #ClutterGstPlayer actor is playing.
   */
  pspec = g_param_spec_boolean ("playing",
                                "Playing",
                                "Whether the player is playing",
                                FALSE,
                                CLUTTER_GST_PARAM_READWRITE);
  g_object_interface_install_property (iface, pspec);

  /**
   * ClutterGstPlayer:audio-volume:
   *
   * The volume of the audio, as a normalized value between
   * 0.0 and 1.0.
   */
  pspec = g_param_spec_double ("audio-volume",
                               "Audio Volume",
                               "The volume of the audio",
                               0.0, 1.0, 0.5,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_interface_install_property (iface, pspec);

  /**
   * ClutterGstPlayer:idle:
   *
   * Whether the #ClutterGstPlayer is in idle mode.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_boolean ("idle",
                                "Idle",
                                "Idle state of the player's pipeline",
                                TRUE,
                                CLUTTER_GST_PARAM_READABLE);
  g_object_interface_install_property (iface, pspec);

  /* Signals */

  /**
   * ClutterGstPlayer::new-frame:
   * @player: the #ClutterGstPlayer instance that received the signal
   * @frame: the #ClutterGstFrame newly received from the video sink
   *
   * The ::new-frame signal is emitted each time a frame is received
   * from the video sink.
   */
  signals[NEW_FRAME] =
    g_signal_new ("new-frame",
                  CLUTTER_GST_TYPE_PLAYER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlayerIface, new_frame),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  CLUTTER_GST_TYPE_FRAME);
  /**
   * ClutterGstPlayer::ready:
   * @player: the #ClutterGstPlayer instance that received the signal
   *
   * The ::ready signal is emitted each time the gstreamer pipeline
   * becomes ready.
   */
  signals[READY_SIGNAL] =
    g_signal_new ("ready",
                  CLUTTER_GST_TYPE_PLAYER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlayerIface, ready),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  /**
   * ClutterGstPlayer::eos:
   * @player: the #ClutterGstPlayer instance that received the signal
   *
   * The ::eos signal is emitted each time the media stream ends.
   */
  signals[EOS_SIGNAL] =
    g_signal_new ("eos",
                  CLUTTER_GST_TYPE_PLAYER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlayerIface, eos),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  /**
   * ClutterGstPlayer::error:
   * @player: the #ClutterGstPlayer instance that received the signal
   * @error: the #GError
   *
   * The ::error signal is emitted each time an error occurred.
   */
  signals[ERROR_SIGNAL] =
    g_signal_new ("error",
                  CLUTTER_GST_TYPE_PLAYER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlayerIface, error),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  G_TYPE_ERROR);

  /**
   * ClutterGstPlayer::size-change:
   * @player: the #ClutterGstPlayer instance that received the signal
   * @width: new width of the frames
   * @height: new height of the frames
   *
   * The ::size-change signal is emitted each time the new frame
   * has different dimensions to the previous frame.
   */
  signals[SIZE_CHANGE] =
    g_signal_new ("size-change",
                  CLUTTER_GST_TYPE_PLAYER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlayerIface, size_change),
                  NULL, NULL,
                  _clutter_gst_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT, G_TYPE_INT);
}

/* ClutterGstIface */

/**
 * clutter_gst_player_get_frame:
 * @self: a #ClutterGstPlayer
 *
 * Retrieves the #ClutterGstFrame of the last frame produced by @self.
 *
 * Return value: (transfer none): the #ClutterGstFrame of the last frame.
 *
 * Since: 3.0
 */
ClutterGstFrame *
clutter_gst_player_get_frame (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), NULL);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_frame (self);
}

/**
 * clutter_gst_player_get_pipeline:
 * @self: a #ClutterGstPlayer
 *
 * Retrieves the #GstPipeline used by the @self, for direct use with
 * GStreamer API.
 *
 * Return value: (transfer none): the #GstPipeline element used by the player
 *
 * Since: 3.0
 */
GstElement *
clutter_gst_player_get_pipeline (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), NULL);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_pipeline (self);
}

/**
 * clutter_gst_player_get_video_sink:
 * @self: a #ClutterGstPlayer
 *
 * Retrieves the #ClutterGstVideoSink used by the @self.
 *
 * Return value: (transfer none): the #ClutterGstVideoSink element used by the player
 *
 * Since: 3.0
 */
ClutterGstVideoSink *
clutter_gst_player_get_video_sink (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), NULL);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_video_sink (self);

}

/**
 * clutter_gst_player_get_playing:
 * @self: A #ClutterGstPlayer object
 *
 * Retrieves the playing status of @self.
 *
 * Return value: %TRUE if playing, %FALSE if stopped.
 *
 * Since: 3.0
 */
gboolean
clutter_gst_player_get_playing (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), TRUE);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_playing (self);
}

/**
 * clutter_gst_player_set_playing:
 * @self: a #ClutterGstPlayer
 * @playing: %TRUE to start playing
 *
 * Starts or stops playing of @self.
 *
 * The implementation might be asynchronous, so the way to know whether
 * the actual playing state of the @self is to use the #GObject::notify
 * signal on the #ClutterGstPlayer:playing property and then retrieve the
 * current state with clutter_gst_player_get_playing(). ClutterGstVideoActor
 * in clutter-gst is an example of such an asynchronous implementation.
 *
 * Since: 3.0
 */
void
clutter_gst_player_set_playing (ClutterGstPlayer *self,
                                gboolean          playing)
{
  ClutterGstPlayerIface *iface;

  g_return_if_fail (CLUTTER_GST_IS_PLAYER (self));

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  iface->set_playing (self, playing);
}

/**
 * clutter_gst_player_get_audio_volume:
 * @self: a #ClutterGstPlayer
 *
 * Retrieves the playback volume of @self.
 *
 * Return value: The playback volume between 0.0 and 1.0
 *
 * Since: 3.0
 */
gdouble
clutter_gst_player_get_audio_volume (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), TRUE);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_audio_volume (self);
}

/**
 * clutter_gst_player_set_audio_volume:
 * @self: a #ClutterGstPlayer
 * @volume: the volume as a double between 0.0 and 1.0
 *
 * Sets the playback volume of @self to @volume.
 *
 * Since: 3.0
 */
void
clutter_gst_player_set_audio_volume (ClutterGstPlayer *self,
                                     gdouble           volume)
{
  ClutterGstPlayerIface *iface;

  g_return_if_fail (CLUTTER_GST_IS_PLAYER (self));

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  iface->set_audio_volume (self, volume);
}

/**
 * clutter_gst_player_get_idle:
 * @self: a #ClutterGstPlayer
 *
 * Get the idle state of the pipeline.
 *
 * Return value: TRUE if the pipline is in idle mode, FALSE otherwise.
 *
 * Since: 3.0
 */
gboolean
clutter_gst_player_get_idle (ClutterGstPlayer *self)
{
  ClutterGstPlayerIface *iface;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYER (self), TRUE);

  iface = CLUTTER_GST_PLAYER_GET_INTERFACE (self);

  return iface->get_idle (self);
}

/* Internal functions */

void
clutter_gst_player_update_frame (ClutterGstPlayer *player,
                                 ClutterGstFrame **frame,
                                 ClutterGstFrame  *new_frame)
{
  ClutterGstFrame *old_frame = *frame;
  ClutterGstVideoResolution old_res = { 0, }, new_res = { 0, };

  *frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME, new_frame);

  if (old_frame)
    old_res = old_frame->resolution;
  if (new_frame)
    new_res = new_frame->resolution;

  if (memcmp(&old_res, &new_res, sizeof(old_res)) != 0)
    g_signal_emit (player, signals[SIZE_CHANGE], 0,
                   new_res.width, new_res.height);

  if (old_frame)
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, old_frame);

  g_signal_emit (player, signals[NEW_FRAME], 0, new_frame);
}

void
clutter_gst_frame_update_pixel_aspect_ratio (ClutterGstFrame  *frame,
                                             ClutterGstVideoSink *sink)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, GST_TYPE_FRACTION);
  g_object_get_property (G_OBJECT (sink),
                         "pixel-aspect-ratio",
                         &value);

  frame->resolution.par_n = gst_value_get_fraction_numerator (&value);
  frame->resolution.par_d = gst_value_get_fraction_denominator (&value);

  g_value_unset (&value);
}

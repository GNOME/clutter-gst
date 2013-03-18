/* clutter-gst-playback.h */

#ifndef __CLUTTER_GST_PLAYBACK_H__
#define __CLUTTER_GST_PLAYBACK_H__

#include <glib-object.h>

#include <clutter-gst/clutter-gst-types.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_PLAYBACK clutter_gst_playback_get_type()

#define CLUTTER_GST_PLAYBACK(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               CLUTTER_GST_TYPE_PLAYBACK,       \
                               ClutterGstPlayback))

#define CLUTTER_GST_PLAYBACK_CLASS(klass)                   \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                        \
                            CLUTTER_GST_TYPE_PLAYBACK,      \
                            ClutterGstPlaybackClass))

#define CLUTTER_GST_IS_PLAYBACK(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               CLUTTER_GST_TYPE_PLAYBACK))

#define CLUTTER_GST_IS_PLAYBACK_CLASS(klass)            \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                    \
                            CLUTTER_GST_TYPE_PLAYBACK))

#define CLUTTER_GST_PLAYBACK_GET_CLASS(obj)                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              CLUTTER_GST_TYPE_PLAYBACK,        \
                              ClutterGstPlaybackClass))

typedef struct _ClutterGstPlayback ClutterGstPlayback;
typedef struct _ClutterGstPlaybackClass ClutterGstPlaybackClass;
typedef struct _ClutterGstPlaybackPrivate ClutterGstPlaybackPrivate;

struct _ClutterGstPlayback
{
  GObject parent;

  ClutterGstPlaybackPrivate *priv;
};

struct _ClutterGstPlaybackClass
{
  GObjectClass parent_class;

  /* signals */
  void (* download_buffering)  (ClutterGstPlayback *self,
                                gdouble             start,
                                gdouble             stop);
};

GType clutter_gst_playback_get_type (void) G_GNUC_CONST;

ClutterGstPlayback *      clutter_gst_playback_new (void);

void                      clutter_gst_playback_set_uri             (ClutterGstPlayback        *self,
                                                                    const gchar               *uri);
gchar *                   clutter_gst_playback_get_uri             (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_filename        (ClutterGstPlayback        *self,
                                                                    const gchar               *filename);

gchar *                   clutter_gst_playback_get_user_agent      (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_user_agent      (ClutterGstPlayback        *self,
                                                                    const gchar               *user_agent);

ClutterGstSeekFlags       clutter_gst_playback_get_seek_flags      (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_seek_flags      (ClutterGstPlayback        *self,
                                                                    ClutterGstSeekFlags        flags);

ClutterGstBufferingMode   clutter_gst_playback_get_buffering_mode  (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_buffering_mode  (ClutterGstPlayback        *self,
                                                                    ClutterGstBufferingMode    mode);
gdouble                   clutter_gst_playback_get_buffer_fill     (ClutterGstPlayback        *self);
gint                      clutter_gst_playback_get_buffer_size     (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_buffer_size     (ClutterGstPlayback        *self,
                                                                    gint                       size);
gint64                    clutter_gst_playback_get_buffer_duration (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_buffer_duration (ClutterGstPlayback        *self,
                                                                    gint64                     duration);

GList *                   clutter_gst_playback_get_audio_streams   (ClutterGstPlayback        *self);
gint                      clutter_gst_playback_get_audio_stream    (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_audio_stream    (ClutterGstPlayback        *self,
                                                                    gint                       index_);

void                      clutter_gst_playback_set_subtitle_uri    (ClutterGstPlayback        *self,
                                                                    const gchar               *uri);
gchar *                   clutter_gst_playback_get_subtitle_uri    (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_subtitle_font_name
                                                                   (ClutterGstPlayback        *self,
                                                                    const char                *font_name);
gchar *                   clutter_gst_playback_get_subtitle_font_name
                                                                   (ClutterGstPlayback        *self);
GList *                   clutter_gst_playback_get_subtitle_tracks (ClutterGstPlayback        *self);
gint                      clutter_gst_playback_get_subtitle_track  (ClutterGstPlayback        *self);
void                      clutter_gst_playback_set_subtitle_track  (ClutterGstPlayback        *self,
                                                                    gint                       index_);

gboolean                  clutter_gst_playback_get_in_seek         (ClutterGstPlayback        *self);

void                      clutter_gst_playback_set_progress        (ClutterGstPlayback        *self,
                                                                    gdouble                    progress);
gdouble                   clutter_gst_playback_get_progress        (ClutterGstPlayback        *self);
gdouble                   clutter_gst_playback_get_duration        (ClutterGstPlayback        *self);

gboolean                  clutter_gst_playback_is_live_media       (ClutterGstPlayback        *self);

G_END_DECLS

#endif /* __CLUTTER_GST_PLAYBACK_H__ */

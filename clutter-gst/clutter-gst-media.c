/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 * SECTION:clutter-media
 * @short_description: An interface for controlling playback of media data.
 *
 * #ClutterGstMedia is an interface  for controlling playback of media data.
 */

#include "config.h"

#include "clutter-gst-media.h"

static void clutter_gst_media_base_init (gpointer g_class);

GType
clutter_gst_media_get_type (void)
{
  static GType media_type = 0;

  if (!media_type)
    {
      static const GTypeInfo media_info =
      {
	sizeof (ClutterGstMediaInterface),
	clutter_gst_media_base_init,
	NULL,			
      };

      media_type = g_type_register_static (G_TYPE_INTERFACE, "ClutterGstMedia",
					   &media_info, 0);
    }

  return media_type;
}

static void
clutter_gst_media_base_init (gpointer g_iface)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      initialized = TRUE;

      /* props */

      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_string 
	 ("uri",
	  "URI",
	  "The loaded URI.",
	  NULL,
	  G_PARAM_READWRITE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));

      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_boolean
	 ("playing",
	  "Playing",
	  "TRUE if playing.",
	  FALSE,
	  G_PARAM_READWRITE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));

      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_int
	 ("position",
	  "Position",
	  "The position in the current stream in seconds.",
	  0, G_MAXINT, 0,
	  G_PARAM_READWRITE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));

      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_double
	 ("volume",
	  "Volume",
	  "The audio volume.",
	  0, 100, 50,
	  G_PARAM_READWRITE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));

      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_boolean
	 ("can-seek",
	  "Can seek",
	  "TRUE if the current stream is seekable.",
	  FALSE,
	  G_PARAM_READABLE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));
	 
      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_int
	 ("buffer-percent",
	  "Buffer percent",
	  "The percentage the current stream buffer is filled.",
	  0, 100, 0,
	  G_PARAM_READABLE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));
	 
      g_object_interface_install_property 
	(g_iface,
	 g_param_spec_int
	 ("duration",
	  "Duration",
	  "The duration of the current stream in seconds.",
	  0, G_MAXINT, 0,
	  G_PARAM_READABLE |
	  G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	  G_PARAM_STATIC_BLURB));

      /* signals */

      g_signal_new ("metadata-available",
		    CLUTTER_GST_TYPE_MEDIA,
		    G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (ClutterGstMediaInterface,
				     metadata_available),
		    NULL, NULL,
		    g_cclosure_marshal_VOID__POINTER,
		    G_TYPE_NONE, 1, G_TYPE_POINTER);
      
      g_signal_new ("eos",
		    CLUTTER_GST_TYPE_MEDIA,
		    G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (ClutterGstMediaInterface,
				     eos),
		    NULL, NULL,
		    g_cclosure_marshal_VOID__VOID,
		    G_TYPE_NONE, 0);
      
      g_signal_new ("error",
		    CLUTTER_GST_TYPE_MEDIA,
		    G_SIGNAL_RUN_LAST,
		    G_STRUCT_OFFSET (ClutterGstMediaInterface,
				     error),
		    NULL, NULL,
		    g_cclosure_marshal_VOID__POINTER,
		    G_TYPE_NONE, 1, G_TYPE_POINTER);
    }
}

/**
 * clutter_gst_media_set_uri:
 * @media: #ClutterGstMedia object
 * @uri: Uri
 *
 * Sets the uri of @media to @uri.
 */
void
clutter_gst_media_set_uri (ClutterGstMedia *media,
		           const char      *uri)
{
  g_return_if_fail (CLUTTER_GST_IS_MEDIA(media));

  CLUTTER_GST_MEDIA_GET_INTERFACE (media)->set_uri (media, uri);
}

/**
 * clutter_gst_media_get_uri:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves the URI from @media.
 *
 * Return value: The URI as a string.
 */
const char*
clutter_gst_media_get_uri (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), NULL);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_uri (media);
}

/**
 * clutter_gst_media_set_playing:
 * @media: A #ClutterGstMedia object
 * @playing: TRUE to start playing, FALSE to stop.
 *
 * Starts or stops @media playing.
 */
void
clutter_gst_media_set_playing (ClutterGstMedia *media,
			       gboolean         playing)
{
  g_return_if_fail (CLUTTER_GST_IS_MEDIA(media));

  CLUTTER_GST_MEDIA_GET_INTERFACE (media)->set_playing (media, playing);
}

/**
 * clutter_gst_media_get_playing:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves the state of @media.
 *
 * Return value: TRUE if playing, FALSE if stopped.
 */
gboolean
clutter_gst_media_get_playing (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), FALSE);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_playing (media);
}

/**
 * clutter_gst_media_set_position:
 * @media: A #ClutterGstMedia object
 * @position: The desired position.
 *
 * Sets the playback position of @media to @position.
 */
void
clutter_gst_media_set_position (ClutterGstMedia *media,
			        int              position)
{
  g_return_if_fail (CLUTTER_GST_IS_MEDIA(media));

  CLUTTER_GST_MEDIA_GET_INTERFACE (media)->set_position (media, position);
}

/**
 * clutter_gst_media_get_position:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves the position of @media.
 *
 * Return value: The playback position.
 */
int
clutter_gst_media_get_position (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), 0);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_position (media);
}

/**
 * clutter_gst_media_set_volume:
 * @media: A #ClutterGstMedia object
 * @volume: The volume as a double between 0.0 and 1.0
 *
 * Sets the playback volume of @media to @volume.
 */
void
clutter_gst_media_set_volume (ClutterGstMedia *media,
			      double           volume)
{
  g_return_if_fail (CLUTTER_GST_IS_MEDIA(media));

  CLUTTER_GST_MEDIA_GET_INTERFACE (media)->set_volume (media, volume);
}

/** 
 * clutter_gst_media_get_volume:
 * @media: A #ClutterGstMedia object
 * 
 * Retrieves the playback volume of @media.
 *
 * Return value: The playback volume between 0.0 and 1.0
 */
double
clutter_gst_media_get_volume (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), 0.0);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_volume (media);
}

/**
 * clutter_gst_media_get_can_seek:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves whether @media is seekable or not.
 *
 * Return value: TRUE if @media can seek, FALSE otherwise.
 */
gboolean
clutter_gst_media_get_can_seek (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), FALSE);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->can_seek (media);
}

/**
 * clutter_gst_media_get_buffer_percent:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves the amount of the stream that is buffered.
 *
 * Return value: percentage value
 */
int
clutter_gst_media_get_buffer_percent (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), 0);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_buffer_percent (media);
}

/**
 * clutter_gst_media_get_duration:
 * @media: A #ClutterGstMedia object
 *
 * Retrieves the duration of the media stream that @media represents.
 *
 * Return value: The length of the media stream.
 */
int
clutter_gst_media_get_duration (ClutterGstMedia *media)
{
  g_return_val_if_fail (CLUTTER_GST_IS_MEDIA(media), 0);

  return CLUTTER_GST_MEDIA_GET_INTERFACE (media)->get_duration (media);
}

/* helper funcs */

/**
 * clutter_gst_media_set_filename:
 * @media: A #ClutterGstMedia object
 * @filename: A filename to media file.
 *
 * Converts a filesystem path to a uri and calls clutter_gst_media_set_uri
 */
void
clutter_gst_media_set_filename (ClutterGstMedia *media, const gchar *filename)
{
  gchar *uri;

  if (filename[0] != '/')
    uri = g_strdup_printf ("file://%s/%s", g_get_current_dir (), filename);
  else 
    uri = g_strdup_printf ("file://%s", filename);

  clutter_gst_media_set_uri (media, uri);

  g_free(uri);
}

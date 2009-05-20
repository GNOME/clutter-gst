#include <string.h>

#include <clutter-gst/clutter-gst.h>

static gint   opt_framerate = 25;
static gchar *opt_fourcc    = "I420";

static GOptionEntry options[] =
{
  { "framerate",
    'f', 0,
    G_OPTION_ARG_INT,
    &opt_framerate,
    "Number of frames per second",
    NULL },
  { "fourcc",
    'o', 0,
    G_OPTION_ARG_STRING,
    &opt_fourcc,
    "Fourcc of the wanted YUV format",
    NULL },

  { NULL }
};

static guint32
parse_fourcc (const gchar *fourcc)
{
  if (strlen (fourcc) != 4)
    return 0;

  return GST_STR_FOURCC (fourcc);
}

void
size_change (ClutterTexture *texture,
	     gint            width,
	     gint            height,
	     gpointer        user_data)
{
  ClutterActor *stage;
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;

  stage = clutter_actor_get_stage (CLUTTER_ACTOR (texture));
  if (stage == NULL)
    return;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  new_height = ( height * stage_width ) / width;
  if (new_height <= stage_height)
    {
      new_width = stage_width;

      new_x = 0;
      new_y = (stage_height - new_height) / 2;
    }
  else
    {
      new_width  = ( width * stage_height ) / height;
      new_height = stage_height;

      new_x = (stage_width - new_width) / 2;
      new_y = 0;
    }

  clutter_actor_set_position (CLUTTER_ACTOR (texture), new_x, new_y);

  clutter_actor_set_size (CLUTTER_ACTOR (texture),
			  new_width,
			  new_height);
}

int
main (int argc, char *argv[])
{
  GOptionContext   *context;
  gboolean          result;
  ClutterActor     *stage;
  ClutterActor     *texture;
  GstPipeline      *pipeline;
  GstElement       *src;
  GstElement       *capsfilter;
  GstElement       *colorspace;
  GstElement       *sink;
  GstCaps          *caps;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  context = g_option_context_new (" - test-colorspace options");
  g_option_context_add_group (context, gst_init_get_option_group ());
  g_option_context_add_group (context, clutter_get_option_group ());
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_parse (context, &argc, &argv, NULL);

  stage = clutter_stage_get_default ();

  /* We need to set certain props on the target texture currently for
   * efficient/corrent playback onto the texture (which sucks a bit)
  */
  texture = g_object_new (CLUTTER_TYPE_TEXTURE,
                          "sync-size",       FALSE,
                          "disable-slicing", TRUE,
                          NULL);

  g_signal_connect (CLUTTER_TEXTURE (texture),
		    "size-change",
		    G_CALLBACK (size_change), NULL);

  /* Set up pipeline */
  pipeline = GST_PIPELINE(gst_pipeline_new (NULL));

  src = gst_element_factory_make ("videotestsrc", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);
  sink = clutter_gst_video_sink_new (CLUTTER_TEXTURE (texture));

  /* make videotestsrc spit the format we want */
  caps = gst_caps_new_simple ("video/x-raw-yuv",
         "format", GST_TYPE_FOURCC, parse_fourcc (opt_fourcc),
         "framerate", GST_TYPE_FRACTION, opt_framerate, 1,
         NULL);
  g_object_set (capsfilter, "caps", caps, NULL);

  g_printf ("%s: [caps] %s\n", __FILE__, gst_caps_to_string (caps));
  gst_bin_add_many (GST_BIN (pipeline), src, capsfilter, sink, NULL);
  result = gst_element_link_many (src, capsfilter, sink, NULL);
  if (result == FALSE)
      g_critical("Could not link elements");
  gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  clutter_group_add (CLUTTER_GROUP (stage), texture);
  // clutter_actor_set_opacity (texture, 0x11);
  clutter_actor_show_all (stage);

  clutter_main();

  return 0;
}

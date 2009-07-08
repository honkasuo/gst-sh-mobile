/**
 * gst-sh-mobile-enc
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 *
 * @author Johannes Lahti <johannes.lahti@nomovok.com>
 * @author Pablo Virolainen <pablo.virolainen@nomovok.com>
 * @author Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */

#ifndef  GSTSHVIDEOENC_H
#define  GSTSHVIDEOENC_H

#include <gst/gst.h>
#include <shcodecs/shcodecs_encoder.h>
#include <pthread.h>

#include "cntlfile/ControlFileUtil.h"

G_BEGIN_DECLS
#define GST_TYPE_SHVIDEOENC \
  (gst_shvideo_enc_get_type())
#define GST_SHVIDEOENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHVIDEOENC,GstshvideoEnc))
#define GST_SHVIDEOENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHVIDEOENC,GstshvideoEnc))
#define GST_IS_SHVIDEOENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHVIDEOENC))
#define GST_IS_SHVIDEOENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHVIDEOENC))
typedef struct _GstshvideoEnc GstshvideoEnc;
typedef struct _GstshvideoEncClass GstshvideoEncClass;

/**
 * Define Gstreamer SH Video Encoder structure
 */

struct _GstshvideoEnc
{
  GstElement element;
  GstPad *sinkpad, *srcpad;
  GstBuffer *buffer_yuv;
  GstBuffer *buffer_cbcr;

  gint offset;
  SHCodecs_Format format;  
  SHCodecs_Encoder* encoder;
  gint width;
  gint height;
  gint fps_numerator;
  gint fps_denominator;

  APPLI_INFO ainfo;
  
  GstCaps* out_caps;
  gboolean caps_set;
  glong frame_number;
  GstClockTime timestamp_offset;

  pthread_t enc_thread;
  pthread_mutex_t mutex;
  pthread_mutex_t cond_mutex;
  pthread_cond_t  thread_condition;
};

/**
 * Define Gstreamer SH Video Encoder Class structure
 */

struct _GstshvideoEncClass
{
  GstElementClass parent;
};

/** Initialize shvideoenc class plugin event handler
    @param g_class Gclass
    @param data user data pointer, unused in the function
*/

static void gst_shvideo_enc_init_class (gpointer g_class, gpointer data);

/** Get gst-sh-mobile-enc object type
    @return object type
*/

GType gst_shvideo_enc_get_type (void);

/** Initialize SH hardware video encoder
    @param klass Gstreamer element class
*/

static void gst_shvideo_enc_base_init (gpointer klass);
 
/** Dispose encoder
    @param object Gstreamer element class
*/

static void gst_shvideo_enc_dispose (GObject * object);

/** Initialize the class for encoder
    @param klass Gstreamer SH video encoder class
*/

static void gst_shvideo_enc_class_init (GstshvideoEncClass *klass);

/** Initialize the encoder
    @param shvideoenc Gstreamer SH video element
    @param gklass Gstreamer SH video encode class
*/

static void gst_shvideo_enc_init (GstshvideoEnc *shvideoenc,
				  GstshvideoEncClass *gklass);

/** Event handler for encoder sink events
    @param pad Gstreamer sink pad
    @param event The Gstreamer event
    @return returns true if the event can be handled, else false
*/

static gboolean gst_shvideo_enc_sink_event (GstPad * pad, GstEvent * event);

/** Initialize the encoder sink pad 
    @param pad Gstreamer sink pad
    @param caps The capabilities of the data to encode
    @return returns true if the video capatilies are supported and the video can be decoded, else false
*/

static gboolean gst_shvideoenc_setcaps (GstPad * pad, GstCaps * caps);

/** Initialize the encoder plugin 
    @param plugin Gstreamer plugin
    @return returns true if the plugin initialized and registered gst-sh-mobile-enc, else false
*/

gboolean gst_shvideo_enc_plugin_init (GstPlugin *plugin);

/** Encoder active event and checks is this pull or push event 
    @param pad Gstreamer sink pad
    @return returns true if the event handled with out errors, else false
*/

static gboolean	gst_shvideo_enc_activate (GstPad *pad);

/** Function to start the pad task
    @param pad Gstreamer sink pad
    @param active true if the task needed to be started or false to stop the task
    @return returns true if the event handled with out errors, else false
*/

static gboolean	gst_shvideo_enc_activate_pull (GstPad *pad, gboolean active);

/** The encoder function and launches the thread if needed
    @param pad Gstreamer sink pad
    @param buffer Buffer where to put back the encoded data
    @return returns GST_FLOW_OK if the function with out errors
*/

static GstFlowReturn gst_shvideo_enc_chain (GstPad *pad, GstBuffer *buffer);

/** The encoder sink pad task
    @param enc Gstreamer SH video encoder
*/

static void gst_shvideo_enc_loop (GstshvideoEnc *enc);

/** The function will set the user defined control file name value for decoder
    @param object The object where to get Gstreamer SH video Encoder object
    @param prop_id The property id
    @param value In this case file name if prop_id is PROP_CNTL_FILE
    @param pspec not used in fuction
*/

static void gst_shvideo_enc_set_property (GObject *object, 
					  guint prop_id, const GValue *value, 
					  GParamSpec * pspec);

/** The function will return the control file name from decoder to value
    @param object The object where to get Gstreamer SH video Encoder object
    @param prop_id The property id
    @param value In this case file name if prop_id is PROP_CNTL_FILE
    @param pspec not used in fuction
*/

static void gst_shvideo_enc_get_property (GObject * object, guint prop_id,
					  GValue * value, GParamSpec * pspec);

/** The encoder sink event handler and calls sink pad push event
    @param pad Gstreamer sink pad
    @param event Event information
    @returns Returns the value of gst_pad_push_event()
*/

static gboolean gst_shvideo_enc_sink_event (GstPad * pad, GstEvent * event);

/** Initializes the SH Hardware encoder
    @param shvideoenc encoder object
*/

void gst_shvideo_enc_init_encoder(GstshvideoEnc * shvideoenc);

/** Gstreamer source pad query 
    @param pad Gstreamer source pad
    @param query Gsteamer query
    @returns Returns the value of gst_pad_query_default
*/

static gboolean gst_shvideo_enc_src_query (GstPad * pad, GstQuery * query);

/** Reads the capabilities of the peer element behind source pad
    @param shvideoenc encoder object
*/

void gst_shvideoenc_read_src_caps(GstshvideoEnc * shvideoenc);

/** Sets the capabilities of the source pad
    @param shvideoenc encoder object
    @return TRUE if the capabilities could be set, otherwise FALSE
*/

gboolean gst_shvideoenc_set_src_caps(GstshvideoEnc * shvideoenc);

/** Callback function for the encoder input
    @param encoder shcodecs encoder
    @param user_data Gstreamer SH encoder object
    @return 0 if encoder should continue. 1 if encoder should pause.
*/

static int gst_shvideo_enc_get_input(SHCodecs_Encoder * encoder, void *user_data);

/** Callback function for the encoder output
    @param encoder shcodecs encoder
    @param data the encoded video frame
    @param length size the encoded video frame buffer
    @param user_data Gstreamer SH encoder object
    @return 0 if encoder should continue. 1 if encoder should pause.
*/

static int gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,
					unsigned char *data, int length, void *user_data);

/** Launches the encoder in an own thread
    @param data encoder object
*/

void* launch_encoder_thread(void *data);

G_END_DECLS
#endif

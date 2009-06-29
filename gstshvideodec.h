/**
 * gst-sh-mobile-dec-sink
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 *
 * @author Pablo Virolainen <pablo.virolainen@nomovok.com>
 * @author Johannes Lahti <johannes.lahti@nomovok.com>
 * @author Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */


#ifndef  GSTSHVIDEODEC_H
#define  GSTSHVIDEODEC_H

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/gstelement.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


G_BEGIN_DECLS
#define GST_TYPE_SHVIDEODEC \
  (gst_shvideodec_get_type())
#define GST_SHVIDEODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHVIDEODEC,Gstshvideodec))
#define GST_SHVIDEODEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHVIDEODEC,Gstshvideodec))
#define GST_IS_SHVIDEODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHVIDEODEC))
#define GST_IS_SHVIDEODEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHVIDEODEC))
typedef struct _Gstshvideodec Gstshvideodec;
typedef struct _GstshvideodecClass GstshvideodecClass;

#include <shcodecs/shcodecs_decoder.h>

/**
 * Define Gstreamer SH Video Decoder structure
 */

struct _Gstshvideodec
{
  GstElement element;

  GstPad *sinkpad;

  /* Input stream */
  SHCodecs_Format format;
  gint width;
  gint height;
  SHCodecs_Decoder * decoder;

  /* Needed? */  
  gboolean caps_set;
  gboolean waiting_for_first_frame;
  
  /* Output */

  gint dst_width;
  gint dst_height;
  gint dst_x;
  gint dst_y;

#ifndef HAVE_M4IPH_HWADD_SDR_MEM
  gulong y_addr;
  gulong c_addr;
#endif

  GstClock* clock;
  GstClockTime first_timestamp;
  GstClockTime current_timestamp;
  GstClockTime start_time;
};

/**
 * Define Gstreamer SH Video Decoder Class structure
 */

struct _GstshvideodecClass
{
  GstElementClass parent;
};


/** Initialize shvideodec class plugin event handler
    @param g_class Gclass
    @param data user data pointer, unused in the function
*/

static void gst_shvideodec_init_class (gpointer g_class, gpointer data);

/** Get gst-sh-mobile-dec-sink object type
    @return object type
*/

GType gst_shvideodec_get_type (void);

/** Initialize SH hardware video decoder & sink
    @param klass Gstreamer element class
*/

static void gst_shvideodec_base_init (gpointer klass);

/** Dispose decoder
    @param object Gstreamer element class
*/

static void gst_shvideodec_dispose (GObject * object);

/** Initialize the class for decoder and player
    @param klass Gstreamer SH video decodes class
*/

static void gst_shvideodec_class_init (GstshvideodecClass * klass);

/** Initialize the decoder
    @param dec Gstreamer SH video element
    @param gklass Gstreamer SH video decode class
*/

static void gst_shvideodec_init (Gstshvideodec * dec, GstshvideodecClass * gklass);

/** Event handler for decoder sink events
    @param pad Gstreamer sink pad
    @param event The Gstreamer event
    @return returns true if the event can be handled, else false
*/

static gboolean gst_shvideodec_sink_event (GstPad * pad, GstEvent * event);

/** Initialize the decoder sink pad 
    @param pad Gstreamer sink pad
    @param caps The capabilities of the video to decode
    @return returns true if the video capatilies are supported and the video can be decoded, else false
*/

static gboolean gst_shvideodec_setcaps (GstPad * pad, GstCaps * caps);

/** GStreamer buffer handling function
    @param pad Gstreamer sink pad
    @param inbuffer The input buffer
    @return returns GST_FLOW_OK if buffer handling was successful. Otherwise GST_FLOW_UNEXPECTED
*/

static GstFlowReturn gst_shvideodec_chain (GstPad * pad, GstBuffer * inbuffer);

/** The video input buffer decode function
    @param dec Gstreamer SH video element
    @param inbuffer The input buffer
    @return The result of passing data to a pad
*/

static GstFlowReturn gst_shvideodec_decode (Gstshvideodec * dec, GstBuffer * inbuffer);

/** Initialize the decoder sink
    @param plugin Gstreamer plugin
    @return returns true if plugin initialized, else false
*/

gboolean gst_shvideo_dec_plugin_init (GstPlugin * plugin);

/** Event handler for the video frame is decoded and can be shown on screen
    @param decoder SHCodecs Decoder, unused in the function
    @param y_buf Userland address to the Y buffer
    @param y_size Size of the Y buffer
    @param c_buf Userland address to teh C buffer
    @param c_size Size of the C buffer
    @param user_data Contains Gstshvideodec
    @return The result of passing data to a pad
*/

static int gst_shcodecs_decoded_callback (SHCodecs_Decoder * decoder,
	     unsigned char * y_buf, int y_size,
	     unsigned char * c_buf, int c_size,
	     void * user_data);
G_END_DECLS
#endif

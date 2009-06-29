/**
 * gst-sh-mobile-enc
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
 * Johannes Lahti <johannes.lahti@nomovok.com>
 * Pablo Virolainen <pablo.virolainen@nomovok.com>
 * Aki Honkasuo <aki.honkasuo@nomovok.com>
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>

#include <gst/gst.h>

#include "gstshvideoenc.h"
#include "cntlfile/ControlFileUtil.h"

/**
 * Define capatibilities for the sink factory
 */

static GstStaticPadTemplate sink_factory = 
  GST_STATIC_PAD_TEMPLATE ("sink",
			   GST_PAD_SINK,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS ("video/x-raw-yuv, "
					    "format = (fourcc) NV12,"
					    "width = (int) [16, 720],"
					    "height = (int) [16, 720]," 
					    "framerate = (fraction) [0, 30]")
			   );


/**
 * Define capatibilities for the source factory
 */

static GstStaticPadTemplate src_factory = 
  GST_STATIC_PAD_TEMPLATE ("src",
			   GST_PAD_SRC,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS ("video/mpeg,"
					    "width = (int) [16, 720],"
					    "height = (int) [16, 720],"
					    "framerate = (fraction) [0, 30],"
					    "mpegversion = (int) 4"
					    "; "
					    "video/x-h264,"
					    "width = (int) [16, 720],"
					    "height = (int) [16, 720],"
					    "framerate = (fraction) [0, 30]"
					    )
			   );

GST_DEBUG_CATEGORY_STATIC (gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug

static GstElementClass *parent_class = NULL;

/**
 * Define encoder properties
 */

enum
{
  PROP_0,
  PROP_CNTL_FILE,
  PROP_LAST
};

static void
gst_shvideo_enc_init_class (gpointer g_class, gpointer data)
{
  parent_class = g_type_class_peek_parent (g_class);
  gst_shvideo_enc_class_init ((GstshvideoEncClass *) g_class);
}

GType gst_shvideo_enc_get_type (void)
{
  static GType object_type = 0;

  if (object_type == 0) {
    static const GTypeInfo object_info = {
      sizeof (GstshvideoEncClass),
      gst_shvideo_enc_base_init,
      NULL,
      gst_shvideo_enc_init_class,
      NULL,
      NULL,
      sizeof (GstshvideoEnc),
      0,
      (GInstanceInitFunc) gst_shvideo_enc_init
    };
    
    object_type =
      g_type_register_static (GST_TYPE_ELEMENT, "gst-sh-mobile-enc", 
			      &object_info, (GTypeFlags) 0);
  }
  
  return object_type;
}

static void
gst_shvideo_enc_base_init (gpointer klass)
{
  static const GstElementDetails plugin_details =
    GST_ELEMENT_DETAILS ("SH hardware video encoder",
			 "Codec/Encoder/Video",
			 "Encode mpeg-based video stream (mpeg4, h264)",
			 "Johannes Lahti <johannes.lahti@nomovok.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_shvideo_enc_dispose (GObject * object)
{
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);

  if (shvideoenc->encoder!=NULL) {
    shvideoenc->encoder= NULL;
  }

  pthread_mutex_destroy(&shvideoenc->mutex);
  pthread_mutex_destroy(&shvideoenc->cond_mutex);
  pthread_cond_destroy(&shvideoenc->thread_condition);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_shvideo_enc_class_init (GstshvideoEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_shvideo_enc_dispose;
  gobject_class->set_property = gst_shvideo_enc_set_property;
  gobject_class->get_property = gst_shvideo_enc_get_property;

  GST_DEBUG_CATEGORY_INIT (gst_sh_mobile_debug, "gst-sh-mobile-enc",
      0, "Encoder for H264/MPEG4 streams");

  g_object_class_install_property (gobject_class, PROP_CNTL_FILE,
      g_param_spec_string ("cntl-file", "Control file location", 
			"Location of the file including encoding parameters", 
			   NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_shvideo_enc_init (GstshvideoEnc * shvideoenc,
    GstshvideoEncClass * gklass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (shvideoenc);

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  shvideoenc->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  gst_element_add_pad (GST_ELEMENT (shvideoenc), shvideoenc->sinkpad);

  gst_pad_set_setcaps_function (shvideoenc->sinkpad, gst_shvideoenc_setcaps);
  gst_pad_set_activate_function (shvideoenc->sinkpad, gst_shvideo_enc_activate);
  gst_pad_set_activatepull_function (shvideoenc->sinkpad,
      gst_shvideo_enc_activate_pull);
  gst_pad_set_event_function(shvideoenc->sinkpad, gst_shvideo_enc_sink_event);
  gst_pad_set_chain_function(shvideoenc->sinkpad, gst_shvideo_enc_chain);
  shvideoenc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_fixed_caps (shvideoenc->srcpad);

  gst_pad_set_query_function (shvideoenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_shvideo_enc_src_query));

  gst_element_add_pad (GST_ELEMENT (shvideoenc), shvideoenc->srcpad);

  shvideoenc->encoder=NULL;
  shvideoenc->caps_set=FALSE;
  shvideoenc->enc_thread = 0;
  shvideoenc->buffer_yuv = NULL;
  shvideoenc->buffer_cbcr = NULL;

  pthread_mutex_init(&shvideoenc->mutex,NULL);
  pthread_mutex_init(&shvideoenc->cond_mutex,NULL);
  pthread_cond_init(&shvideoenc->thread_condition,NULL);

  shvideoenc->format = SHCodecs_Format_NONE;
  shvideoenc->out_caps = NULL;
  shvideoenc->width = 0;
  shvideoenc->height = 0;
  shvideoenc->fps_numerator = 0;
  shvideoenc->fps_denominator = 0;
  shvideoenc->frame_number = 0;
}

static void
gst_shvideo_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);
  
  switch (prop_id) 
  {
    case PROP_CNTL_FILE:
    {
      strcpy(shvideoenc->ainfo.ctrl_file_name_buf,g_value_get_string(value));
      break;
    }
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static void
gst_shvideo_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstshvideoEnc *shvideoenc = GST_SHVIDEOENC (object);

  switch (prop_id) 
  {
    case PROP_CNTL_FILE:
    {
      g_value_set_string(value,shvideoenc->ainfo.ctrl_file_name_buf);      
      break;
    }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean 
gst_shvideo_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstshvideoEnc *enc = (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));  

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  return gst_pad_push_event(enc->srcpad,event);
}

static gboolean
gst_shvideoenc_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstStructure *structure;
  GstshvideoEnc *enc = 
    (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));

  enc->caps_set = FALSE;
  ret = TRUE;

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  // get input size
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &enc->width);
  ret &= gst_structure_get_int (structure, "height", &enc->height);
  ret &= gst_structure_get_fraction (structure, "framerate", 
				     &enc->fps_numerator, 
				     &enc->fps_denominator);

  if(!ret) {
	return ret;
  }

  gst_shvideoenc_read_src_caps(enc);
  gst_shvideo_enc_init_encoder(enc);

  if(!gst_caps_is_any(enc->out_caps))
  {
    ret = gst_shvideoenc_set_src_caps(enc);
  }
    
  if(ret) {
    enc->caps_set = TRUE;
  }

  return ret;
}

void
gst_shvideoenc_read_src_caps(GstshvideoEnc * shvideoenc)
{
  GstStructure *structure;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  // get the caps of the next element in chain
  shvideoenc->out_caps = gst_pad_peer_get_caps(shvideoenc->srcpad);
  
  // Any format is ok too
  if(!gst_caps_is_any(shvideoenc->out_caps))
  {
    structure = gst_caps_get_structure (shvideoenc->out_caps, 0);
    if (!strcmp (gst_structure_get_name (structure), "video/mpeg")) {
      shvideoenc->format = SHCodecs_Format_MPEG4;
    }
    else if (!strcmp (gst_structure_get_name (structure), "video/x-h264")) {
      shvideoenc->format = SHCodecs_Format_H264;
    }
  }
}

gboolean
gst_shvideoenc_set_src_caps(GstshvideoEnc * shvideoenc)
{
  GstCaps* caps = NULL;
  gboolean ret = TRUE;
  
  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  if(shvideoenc->format == SHCodecs_Format_MPEG4)
  {
    caps = gst_caps_new_simple ("video/mpeg", "width", G_TYPE_INT, 
				shvideoenc->width, "height", G_TYPE_INT, 
				shvideoenc->height, "framerate", 
				GST_TYPE_FRACTION, shvideoenc->fps_numerator, 
				shvideoenc->fps_denominator, "mpegversion", 
				G_TYPE_INT, 4, NULL);
  }
  else if(shvideoenc->format == SHCodecs_Format_H264)
  {
    caps = gst_caps_new_simple ("video/x-h264", "width", G_TYPE_INT, 
				shvideoenc->width, "height", G_TYPE_INT, 
				shvideoenc->height, "framerate", 
				GST_TYPE_FRACTION, shvideoenc->fps_numerator, 
				shvideoenc->fps_denominator, NULL);
  }
  else
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,NEGOTIATION,
		      ("Format undefined."), (NULL));
  }

  if(!gst_pad_set_caps(shvideoenc->srcpad,caps))
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,NEGOTIATION,
		      ("Source pad not linked."), (NULL));
    ret = FALSE;
  }
  if(!gst_pad_set_caps(gst_pad_get_peer(shvideoenc->srcpad),caps))
  {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,NEGOTIATION,
		      ("Source pad not linked."), (NULL));
    ret = FALSE;
  }
  gst_caps_unref(caps);
  return ret;
}

void
gst_shvideo_enc_init_encoder(GstshvideoEnc * shvideoenc)
{
  gint ret = 0;
  glong fmt = 0;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  ret = GetFromCtrlFTop((const char *)
				shvideoenc->ainfo.ctrl_file_name_buf,
				&shvideoenc->ainfo,
				&fmt);
  if (ret < 0) {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,
		      ("Error reading control file."), (NULL));
  }

  if(shvideoenc->format == SHCodecs_Format_NONE)
  {
    shvideoenc->format = fmt;
  }

  if(!shvideoenc->width)
  {
    shvideoenc->width = shvideoenc->ainfo.xpic;
  }

  if(!shvideoenc->height)
  {
    shvideoenc->height = shvideoenc->ainfo.ypic;
  }

  shvideoenc->encoder = shcodecs_encoder_init(shvideoenc->width, 
					      shvideoenc->height, 
					      shvideoenc->format);

  shcodecs_encoder_set_input_callback(shvideoenc->encoder, 
				      gst_shvideo_enc_get_input, 
				      shvideoenc);
  shcodecs_encoder_set_output_callback(shvideoenc->encoder, 
				       gst_shvideo_enc_write_output, 
				       shvideoenc);

  ret =
      GetFromCtrlFtoEncParam(shvideoenc->encoder, &shvideoenc->ainfo);

  if (ret < 0) {
    GST_ELEMENT_ERROR((GstElement*)shvideoenc,CORE,FAILED,
		      ("Error reading control file."), (NULL));
  }

  if(shvideoenc->fps_numerator && shvideoenc->fps_denominator)
  {
    shcodecs_encoder_set_frame_rate(shvideoenc->encoder,
				  (shvideoenc->fps_numerator/shvideoenc->fps_denominator*10));
  }
  shcodecs_encoder_set_xpic_size(shvideoenc->encoder,shvideoenc->width);
  shcodecs_encoder_set_ypic_size(shvideoenc->encoder,shvideoenc->height);

  shcodecs_encoder_set_frame_no_increment(shvideoenc->encoder,
      shcodecs_encoder_get_frame_num_resolution(shvideoenc->encoder) /
      (shcodecs_encoder_get_frame_rate(shvideoenc->encoder) / 10)); 

  GST_DEBUG_OBJECT(shvideoenc,"Encoder init: %ldx%ld %ldfps format:%ld",
		   shcodecs_encoder_get_xpic_size(shvideoenc->encoder),
		   shcodecs_encoder_get_ypic_size(shvideoenc->encoder),
		   shcodecs_encoder_get_frame_rate(shvideoenc->encoder)/10,
		   shcodecs_encoder_get_stream_type(shvideoenc->encoder)); 
}

static gboolean
gst_shvideo_enc_activate (GstPad * pad)
{
  gboolean ret;
  GstshvideoEnc *enc = 
    (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);
  if (gst_pad_check_pull_range (pad)) {
    GST_LOG_OBJECT(enc,"PULL mode");
    ret = gst_pad_activate_pull (pad, TRUE);
  } else {  
    GST_LOG_OBJECT(enc,"PUSH mode");
    ret = gst_pad_activate_push (pad, TRUE);
  }
  return ret;
}

static GstFlowReturn 
gst_shvideo_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  gint yuv_size, cbcr_size, i, j;
  guint8* cr_ptr; 
  guint8* cb_ptr;
  GstshvideoEnc *enc = (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));  

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  if(!enc->caps_set)
  {
    gst_shvideoenc_read_src_caps(enc);
    gst_shvideo_enc_init_encoder(enc);
    if(!gst_caps_is_any(enc->out_caps))
    {
      if(!gst_shvideoenc_set_src_caps(enc))
      {
	return GST_FLOW_UNEXPECTED;
      }
    }
    enc->caps_set = TRUE;
  }

  /* If buffers are not empty we'll have to 
     wait until encoder has consumed data */
  if(enc->buffer_yuv && enc->buffer_cbcr)
  {
    pthread_mutex_lock( &enc->cond_mutex );
    pthread_cond_wait( &enc->thread_condition, &enc->cond_mutex );
    pthread_mutex_unlock( &enc->cond_mutex );
  }

  // Lock mutex while handling the buffers
  pthread_mutex_lock(&enc->mutex);
  yuv_size = enc->width*enc->height;
  cbcr_size = enc->width*enc->height/2;

  // Check that we have got enough data
  if(GST_BUFFER_SIZE(buffer) != yuv_size + cbcr_size)
  {
    GST_DEBUG_OBJECT (enc, "Not enough data");
    // If we can't continue we can issue EOS
    gst_pad_push_event(enc->srcpad,gst_event_new_eos ());
    return GST_FLOW_OK;
  }  

  enc->buffer_yuv = gst_buffer_new_and_alloc (yuv_size);
  enc->buffer_cbcr = gst_buffer_new_and_alloc (cbcr_size);

  memcpy(GST_BUFFER_DATA(enc->buffer_yuv),GST_BUFFER_DATA(buffer),yuv_size);

  if(enc->ainfo.yuv_CbCr_format == 0)
  {
    cb_ptr = GST_BUFFER_DATA(buffer)+yuv_size;
    cr_ptr = GST_BUFFER_DATA(buffer)+yuv_size+(cbcr_size/2);

    for(i=0,j=0;i<cbcr_size;i+=2,j++)
    {
      GST_BUFFER_DATA(enc->buffer_cbcr)[i]=cb_ptr[j];
      GST_BUFFER_DATA(enc->buffer_cbcr)[i+1]=cr_ptr[j];
    }
  }
  else
  {
    memcpy(GST_BUFFER_DATA(enc->buffer_cbcr),GST_BUFFER_DATA(buffer)+yuv_size,
	 cbcr_size);
  }

  // Buffers are ready to be read
  pthread_mutex_unlock(&enc->mutex);

  gst_buffer_unref(buffer);
  
  if(!enc->enc_thread)
  {
    /* We'll have to launch the encoder in 
       a separate thread to keep the pipeline running */
    pthread_create( &enc->enc_thread, NULL, launch_encoder_thread, enc);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_shvideo_enc_activate_pull (GstPad  *pad,
			     gboolean active)
{
  GstshvideoEnc *enc = 
    (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  if (active) {
    enc->offset = 0;
    return gst_pad_start_task (pad,
        (GstTaskFunction) gst_shvideo_enc_loop, enc);
  } else {
    return gst_pad_stop_task (pad);
  }
}

static void
gst_shvideo_enc_loop (GstshvideoEnc *enc)
{
  GstFlowReturn ret;
  gint yuv_size, cbcr_size, i, j;
  guint8* cb_ptr; 
  guint8* cr_ptr; 
  GstBuffer* tmp;

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  if(!enc->caps_set)
  {
    gst_shvideoenc_read_src_caps(enc);
    gst_shvideo_enc_init_encoder(enc);
    if(!gst_caps_is_any(enc->out_caps))
    {
      if(!gst_shvideoenc_set_src_caps(enc))
      {
	gst_pad_pause_task (enc->sinkpad);
	return;
      }
    }
    enc->caps_set = TRUE;
  }

  /* If buffers are not empty we'll have to 
     wait until encoder has consumed data */
  if(enc->buffer_yuv && enc->buffer_cbcr)
  {
    pthread_mutex_lock( &enc->cond_mutex );
    pthread_cond_wait( &enc->thread_condition, &enc->cond_mutex );
    pthread_mutex_unlock( &enc->cond_mutex );
  }

  // Lock mutex while handling the buffers
  pthread_mutex_lock(&enc->mutex);
  yuv_size = enc->width*enc->height;
  cbcr_size = enc->width*enc->height/2;

  ret = gst_pad_pull_range (enc->sinkpad, enc->offset,
      yuv_size, &enc->buffer_yuv);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (enc, "pull_range failed: %s", gst_flow_get_name (ret));
    gst_pad_pause_task (enc->sinkpad);
    return;
  }
  else if(GST_BUFFER_SIZE(enc->buffer_yuv) != yuv_size)
  {
    GST_DEBUG_OBJECT (enc, "Not enough data");
    gst_pad_pause_task (enc->sinkpad);
    gst_pad_push_event(enc->srcpad,gst_event_new_eos ());
    return;
  }  

  enc->offset += yuv_size;

  ret = gst_pad_pull_range (enc->sinkpad, enc->offset,
      cbcr_size, &tmp);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (enc, "pull_range failed: %s", gst_flow_get_name (ret));
    gst_pad_pause_task (enc->sinkpad);
    return;
  }  
  else if(GST_BUFFER_SIZE(tmp) != cbcr_size)
  {
    GST_DEBUG_OBJECT (enc, "Not enough data");
    gst_pad_pause_task (enc->sinkpad);
    gst_pad_push_event(enc->srcpad,gst_event_new_eos ());
    return;
  }  

  enc->offset += cbcr_size;

  if(enc->ainfo.yuv_CbCr_format == 0)
  {
    cb_ptr = GST_BUFFER_DATA(tmp);
    cr_ptr = GST_BUFFER_DATA(tmp)+(cbcr_size/2);
    enc->buffer_cbcr = gst_buffer_new_and_alloc(cbcr_size);

    for(i=0,j=0;i<cbcr_size;i+=2,j++)
    {
      GST_BUFFER_DATA(enc->buffer_cbcr)[i]=cb_ptr[j];
      GST_BUFFER_DATA(enc->buffer_cbcr)[i+1]=cr_ptr[j];
    }
    
    gst_buffer_unref(tmp);
  }
  else
  {
    enc->buffer_cbcr = tmp;
  }

  pthread_mutex_unlock(&enc->mutex);
  
  if(!enc->enc_thread)
  {
    /* We'll have to launch the encoder in 
       a separate thread to keep the pipeline running */
    pthread_create( &enc->enc_thread, NULL, launch_encoder_thread, enc);
  }
}

void *
launch_encoder_thread(void *data)
{
  gint ret;
  GstshvideoEnc *enc = (GstshvideoEnc *)data;

  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);

  ret = shcodecs_encoder_run(enc->encoder);

  GST_DEBUG_OBJECT (enc,"shcodecs_encoder_run returned %d\n",ret);

  // We can stop waiting if encoding has ended
  pthread_mutex_lock( &enc->cond_mutex );
  pthread_cond_signal( &enc->thread_condition);
  pthread_mutex_unlock( &enc->cond_mutex );

  // Calling stop task won't do any harm if we are in push mode
  gst_pad_stop_task (enc->sinkpad);
  gst_pad_push_event(enc->srcpad,gst_event_new_eos ());

  return NULL;
}

static int 
gst_shvideo_enc_get_input(SHCodecs_Encoder * encoder, void *user_data)
{
  GstshvideoEnc *shvideoenc = (GstshvideoEnc *)user_data;
  gint ret=0;

  GST_LOG_OBJECT(shvideoenc,"%s called",__FUNCTION__);

  // Lock mutex while reading the buffer  
  pthread_mutex_lock(&shvideoenc->mutex); 
  if(shvideoenc->buffer_yuv && shvideoenc->buffer_cbcr)
  {
    ret = shcodecs_encoder_input_provide(encoder, 
					 GST_BUFFER_DATA(shvideoenc->buffer_yuv),
					 GST_BUFFER_DATA(shvideoenc->buffer_cbcr));

    gst_buffer_unref(shvideoenc->buffer_yuv);
    shvideoenc->buffer_yuv = NULL;
    gst_buffer_unref(shvideoenc->buffer_cbcr);
    shvideoenc->buffer_cbcr = NULL;  

    // Signal the main thread that buffers are read
    pthread_mutex_lock( &shvideoenc->cond_mutex );
    pthread_cond_signal( &shvideoenc->thread_condition);
    pthread_mutex_unlock( &shvideoenc->cond_mutex );
  }
  pthread_mutex_unlock(&shvideoenc->mutex);

  return 0;
}

static int 
gst_shvideo_enc_write_output(SHCodecs_Encoder * encoder,
			unsigned char *data, int length, void *user_data)
{
  GstshvideoEnc *enc = (GstshvideoEnc *)user_data;
  GstBuffer* buf=NULL;
  gint ret=0;

  GST_LOG_OBJECT(enc,"%s called. Got %d bytes data\n",__FUNCTION__, length);

  if(length)
  {
    buf = gst_buffer_new();
    gst_buffer_set_data(buf, data, length);

    GST_BUFFER_DURATION(buf) = enc->fps_denominator*1000*GST_MSECOND/enc->fps_numerator;
    GST_BUFFER_TIMESTAMP(buf) = enc->frame_number*GST_BUFFER_DURATION(buf);
    enc->frame_number++;

    ret = gst_pad_push (enc->srcpad, buf);

    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (enc, "pad_push failed: %s", gst_flow_get_name (ret));
      return 1;
    }
  }
  return 0;
}

static gboolean
gst_shvideo_enc_src_query (GstPad * pad, GstQuery * query)
{
  GstshvideoEnc *enc = 
    (GstshvideoEnc *) (GST_OBJECT_PARENT (pad));
  GST_LOG_OBJECT(enc,"%s called",__FUNCTION__);
  return gst_pad_query_default (pad, query);
}

gboolean
gst_shvideo_enc_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gst-sh-mobile-enc", GST_RANK_PRIMARY,
          GST_TYPE_SHVIDEOENC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-sh-mobile-enc",
    "gst-sh-mobile",
    gst_shvideo_enc_plugin_init,
    VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)


/**
 * gst-sh-mobile-dec-sink
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
 * Pablo Virolainen <pablo.virolainen@nomovok.com>
 * Johannes Lahti <johannes.lahti@nomovok.com>
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

#include "gstshvideodec.h"
#include <linux/fb.h>
#include "vidix/sh_veu_vid.c"

/**
 * Define capatibilities for the sink factory
 */

static GstStaticPadTemplate sink_factory = 
  GST_STATIC_PAD_TEMPLATE ("sink",
			   GST_PAD_SINK,
			   GST_PAD_ALWAYS,
			   GST_STATIC_CAPS (
					    "video/mpeg,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 576],"
					    "framerate = (fraction) [0, 25],"
                                            "mpegversion = (int) 4"
					    "; "
					    "video/x-h264,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 576],"
					    "framerate = (fraction) [0, 25],"
					    "variant = (string) itu,"
					    "h264version = (string) h264"
					    "; "
					    "video/x-divx,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 576],"
					    "framerate = (fraction) [0, 25],"
					    "divxversion =  {4, 5, 6}"
					    "; "
					    "video/x-xvid,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 576],"
					    "framerate = (fraction) [0, 25]"
					    "; "
					    "video/x-h264,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 480],"
					    "framerate = (fraction) [0, 30],"
					    "variant = (string) itu,"
					    "h264version = (string) h264"
					    ";"
					    "video/x-divx,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 480],"
					    "framerate = (fraction) [0, 30],"
					    "divxversion =  {4, 5, 6}"
					    ";"
					    "video/mpeg,"
					    "width  = (int) [48, 720],"
					    "height = (int) [48, 480],"
					    "framerate = (fraction) [0, 30],"
                                            "mpegversion = (int) 4"
					    )
			   );

static GstElementClass *parent_class = NULL;
vidix_capability_t sh_capability;
vidix_playback_t sh_vidix;

GST_DEBUG_CATEGORY_STATIC (gst_sh_mobile_debug);
#define GST_CAT_DEFAULT gst_sh_mobile_debug

#define DEFAULT_MAX_SIZE 0

/**
 * Define decoder properties
 */

enum
{
  PROP_0,
  PROP_MAX_BUFFER_SIZE,
  PROP_LAST
};


static void
gst_shvideodec_init_class (gpointer g_class, gpointer data)
{
  parent_class = g_type_class_peek_parent (g_class);
  gst_shvideodec_class_init ((GstshvideodecClass *) g_class);
}

GType
gst_shvideodec_get_type (void)
{
  static GType object_type = 0;

  if (object_type == 0) 
  {
    static const GTypeInfo object_info = 
    {
      sizeof (GstshvideodecClass),
      gst_shvideodec_base_init,
      NULL,
      gst_shvideodec_init_class,
      NULL,
      NULL,
      sizeof (Gstshvideodec),
      0,
      (GInstanceInitFunc) gst_shvideodec_init
    };

    object_type =
      g_type_register_static (GST_TYPE_ELEMENT, "gst-sh-mobile-dec-sink", &object_info,
			      (GTypeFlags) 0);
  }
  return object_type;
}

static void
gst_shvideodec_base_init (gpointer klass)
{
  static const GstElementDetails plugin_details =
    GST_ELEMENT_DETAILS ("SH hardware video decoder & sink",
			 "Codec/Decoder/Video/Sink",
			 "Decode video (H264 && Mpeg4)",
			 "Pablo Virolainen <pablo.virolainen@nomovok.com>");

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_shvideodec_dispose (GObject * object)
{
  Gstshvideodec *dec = GST_SHVIDEODEC (object);

  GST_LOG_OBJECT(dec,"%s called\n",__FUNCTION__);  

  if (dec->decoder != NULL) 
  {
    GST_DEBUG_OBJECT (dec, "close decoder object %p", dec->decoder);
    shcodecs_decoder_close (dec->decoder);
  }
  sh_veu_destroy();
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_shvideodec_class_init (GstshvideodecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_sh_mobile_debug, "gst-sh-mobile-dec-sink",
      0, "Decoder and player for H264/MPEG4 streams");

  gobject_class->dispose = gst_shvideodec_dispose;
  gobject_class->set_property = gst_shvideodec_set_property;
  gobject_class->get_property = gst_shvideodec_get_property;

  g_object_class_install_property (gobject_class, PROP_MAX_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Maximum buffer size kB", 
			"Maximum size of the experimental pre-buffer (kB, 0=disabled)", 
                           0, G_MAXUINT, DEFAULT_MAX_SIZE,
			   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_shvideodec_change_state;
  gstelement_class->set_clock = gst_shvideodec_set_clock;
}

static void
gst_shvideodec_init (Gstshvideodec * dec, GstshvideodecClass * gklass)
{
  GstElementClass *kclass = GST_ELEMENT_GET_CLASS (dec);

  GST_LOG_OBJECT(dec,"%s called",__FUNCTION__);

  dec->sinkpad = gst_pad_new_from_template(gst_element_class_get_pad_template(kclass,"sink"),"sink");

  gst_pad_set_setcaps_function(dec->sinkpad, gst_shvideodec_setcaps);
  gst_pad_set_chain_function(dec->sinkpad,GST_DEBUG_FUNCPTR(gst_shvideodec_chain));

  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_shvideodec_sink_event));

  gst_element_add_pad(GST_ELEMENT(dec),dec->sinkpad);

  dec->caps_set = FALSE;
  dec->decoder = NULL;
  dec->waiting_for_first_frame = TRUE;

  dec->buffer = NULL;
  dec->buffer_size = DEFAULT_MAX_SIZE;

  dec->running = TRUE;  
  dec->paused = TRUE;

  pthread_mutex_init(&dec->mutex,NULL);
  pthread_mutex_init(&dec->cond_mutex,NULL);
  pthread_cond_init(&dec->thread_condition,NULL);
  pthread_mutex_init(&dec->pause_mutex,NULL);
  pthread_cond_init(&dec->pause_condition,NULL);
}


static void
gst_shvideodec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstshvideodec *dec = GST_SHVIDEODEC (object);
  
  switch (prop_id) 
  {
    case PROP_MAX_BUFFER_SIZE:
    {
      dec->buffer_size = g_value_get_uint (value) * 1024; // Kilobytes we use
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
gst_shvideodec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstshvideodec *dec = GST_SHVIDEODEC (object);

  switch (prop_id) 
  {
    case PROP_MAX_BUFFER_SIZE:
    {
      g_value_set_uint(value,dec->buffer_size/1024); // In kilo bytes      
      break;
    }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean            
gst_shvideodec_set_clock (GstElement *element, GstClock *clock)
{
  Gstshvideodec *dec = (Gstshvideodec *) element;
  
  GST_DEBUG_OBJECT(dec,"%s called",__FUNCTION__);

  if(!clock)
  {
    GST_DEBUG_OBJECT(dec,"Using system clock");
    dec->clock = gst_system_clock_obtain();
    return FALSE;
  }
  else
  {
    GST_DEBUG_OBJECT(dec,"Clock accepted");
    dec->clock = clock;
    return TRUE;
  }
}

static GstStateChangeReturn
gst_shvideodec_change_state (GstElement *element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  Gstshvideodec *dec = (Gstshvideodec *) element;
  
  GST_DEBUG_OBJECT(dec,"%s called",__FUNCTION__);

  switch (transition) 
  {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GST_DEBUG_OBJECT(dec,"Resume playing");
      dec->paused = FALSE;
      pthread_mutex_lock( &dec->pause_mutex );
      pthread_cond_signal( &dec->pause_condition);
      pthread_mutex_unlock( &dec->pause_mutex );
      break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
      GST_DEBUG_OBJECT(dec,"Pause playing");
      dec->paused = TRUE;
      break;
    }
    default:
    {
      ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
      if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;
      break;
    }
  }

  return ret;
}

static gboolean
gst_shvideodec_sink_event (GstPad * pad, GstEvent * event)
{
  Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT (pad));
  gboolean ret=TRUE;

  GST_DEBUG_OBJECT(dec,"%s called event %i",__FUNCTION__,GST_EVENT_TYPE(event));

  switch (GST_EVENT_TYPE (event)) 
  {
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (dec, "EOS gst event");
      dec->running = FALSE;
      if(dec->dec_thread)
      {
        pthread_join(dec->dec_thread,NULL);
        // Decode the rest of the buffer
      }
      else
      {
        gst_element_post_message((GstElement*)dec,gst_message_new_buffering((GstObject*)dec,100));      
      }
      gst_shvideodec_decode(dec);
      gst_element_post_message((GstElement*)dec,gst_message_new_eos((GstObject*)dec));
      break;
    }
    default:
    {
      ret = gst_pad_event_default(pad, event);
      break;
    }
  };

  return ret;
}

static gboolean
gst_shvideodec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure = NULL;
  Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT (pad));

  GST_LOG_OBJECT(dec,"%s called",__FUNCTION__);

  if (dec->decoder!=NULL) 
  {
    GST_DEBUG_OBJECT(dec,"%s: Decoder already opened",__FUNCTION__);
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!strcmp (gst_structure_get_name (structure), "video/x-h264")) 
  {
    GST_DEBUG_OBJECT(dec, "codec format is video/x-h264");
    dec->format = SHCodecs_Format_H264;
  }
  else
  {
    if (!strcmp (gst_structure_get_name (structure), "video/x-divx") ||
	!strcmp (gst_structure_get_name (structure), "video/x-xvid") ||
	!strcmp (gst_structure_get_name (structure), "video/x-gst-fourcc-libx") ||
	!strcmp (gst_structure_get_name (structure), "video/mpeg")
	) 
    {
      GST_DEBUG_OBJECT (dec, "codec format is video/mpeg");
      dec->format = SHCodecs_Format_MPEG4;
    } 
    else 
    {
      GST_DEBUG_OBJECT(dec,"%s failed (not supported: %s)",__FUNCTION__,gst_structure_get_name (structure));
      return FALSE;
    }
  }

  if(!gst_structure_get_fraction (structure, "framerate", 
				  &dec->fps_numerator, 
				  &dec->fps_denominator))
  {
    GST_DEBUG_OBJECT(dec,"%s failed (no framerate)",__FUNCTION__);
    return FALSE;
  }

  if (gst_structure_get_int (structure, "width",  &dec->width)
      && gst_structure_get_int (structure, "height", &dec->height)) 
  {
    GST_DEBUG_OBJECT(dec,"%s initializing decoder %dx%d",__FUNCTION__,dec->width,dec->height);
    dec->decoder=shcodecs_decoder_init(dec->width,dec->height,dec->format);
  } 
  else 
  {
    GST_DEBUG_OBJECT(dec,"%s failed (no width/height)",__FUNCTION__);
    return FALSE;
  }

  if (dec->decoder==NULL) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on shdecodecs_decoder_init."), 
       ("%s failed (Error on shdecodecs_decoder_init)",__FUNCTION__));
    return FALSE;
  }

  /* Set frame by frame */
  shcodecs_decoder_set_frame_by_frame(dec->decoder,1);

  /* Use physical addresses for playback */
  shcodecs_decoder_set_use_physical(dec->decoder,1);

  /* Let's try to init video output. First we probe, then init */  
  if (sh_veu_probe(0,0)<0) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on SH VEU probe."), 
       ("%s failed (Error on SH VEU probe)",__FUNCTION__));
    return FALSE;
  }

  if (sh_veu_init()<0) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on SH VEU init."), 
       ("%s failed (Error on SH VEU init)",__FUNCTION__));
    return FALSE;
  }

  if (sh_veu_get_caps(&sh_capability)<0) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on SH VEU caps."), 
       ("%s failed (Error on SH VEU caps)",__FUNCTION__));
    return FALSE;
  }

  /* let's config playback */
  if (get_fb_info("/dev/fb0", &fbi)<0) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on getting frame buffer info."), 
       ("%s failed (Error on getting frame buffer info.)",__FUNCTION__));
    return FALSE;
  }

  memset(&sh_vidix,0,sizeof(vidix_playback_t));

  sh_vidix.src.w = dec->width;
  sh_vidix.src.h = dec->height;
  sh_vidix.dest.x = dec->dst_x;
  sh_vidix.dest.y = dec->dst_y;
  sh_vidix.dest.w = fbi.width;
  sh_vidix.dest.h = fbi.height;
  sh_vidix.num_frames=1;
  sh_vidix.capability = sh_capability.flags;
  sh_vidix.fourcc=IMGFMT_NV12;

  if (sh_veu_config_playback(&sh_vidix)<0) 
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,FAILED,("Error on SH VEU config playback."), 
       ("%s failed (Error on SH VEU config playback)",__FUNCTION__));
    return FALSE;
  }

  shcodecs_decoder_set_decoded_callback(dec->decoder,
					  gst_shcodecs_decoded_callback,
					  (void*)dec);

  dec->caps_set = TRUE;

  GST_LOG_OBJECT(dec,"%s ok",__FUNCTION__);
  return TRUE;
}

static GstFlowReturn
gst_shvideodec_chain (GstPad * pad, GstBuffer * inbuffer)
{
  Gstshvideodec *dec = (Gstshvideodec *) (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  gint percent;

  if(!dec->caps_set)
  {
    GST_ELEMENT_ERROR((GstElement*)dec,CORE,NEGOTIATION,("Caps not set."), (NULL));
    return GST_FLOW_UNEXPECTED;
  }

  GST_LOG_OBJECT(dec,"%s called",__FUNCTION__);

  
  if(dec->buffer &&
     GST_BUFFER_SIZE(dec->buffer) + GST_BUFFER_SIZE(inbuffer) > dec->buffer_size)
  {
    if(!dec->dec_thread)
    {
      GST_DEBUG_OBJECT(dec,"Pre-buffering complete. The new frame doesn't fit");    
      gst_element_post_message((GstElement*)dec,gst_message_new_buffering((GstObject*)dec,100));      
      /* We'll have to launch the decoder in 
         a separate thread to keep the pipeline running */
      pthread_create( &dec->dec_thread, NULL, gst_shvideodec_decode, dec);
    }
    else
    {
      GST_DEBUG_OBJECT(dec,"Buffer full, waiting");    
      pthread_mutex_lock( &dec->cond_mutex );
      pthread_cond_wait( &dec->thread_condition, &dec->cond_mutex );
      pthread_mutex_unlock( &dec->cond_mutex );
      GST_DEBUG_OBJECT(dec,"Got signal");
    }    
  }

  pthread_mutex_lock( &dec->mutex );
  if(!dec->buffer)
  {
    GST_DEBUG_OBJECT(dec,"First frame in buffer. Size %d timestamp: %llu duration: %llu",
		   GST_BUFFER_SIZE(inbuffer),
		   GST_TIME_AS_MSECONDS(GST_BUFFER_TIMESTAMP (inbuffer)),
		   GST_TIME_AS_MSECONDS(GST_BUFFER_DURATION (inbuffer)));

    dec->buffer_timestamp = GST_BUFFER_TIMESTAMP (inbuffer);
    dec->buffer_frames = 1;
    dec->buffer = inbuffer;
  }  
  else
  {
    GST_LOG_OBJECT(dec,"Buffer size %d timestamp: %llu duration: %llu",
		   GST_BUFFER_SIZE(inbuffer),
		   GST_TIME_AS_MSECONDS(GST_BUFFER_TIMESTAMP (inbuffer)),
		   GST_TIME_AS_MSECONDS(GST_BUFFER_DURATION (inbuffer)));

    dec->buffer = gst_buffer_join(dec->buffer,inbuffer);
    dec->buffer_frames++;
    GST_LOG_OBJECT(dec,"Buffer added. Now storing %d bytes",GST_BUFFER_SIZE(dec->buffer));        
  }
  pthread_mutex_unlock( &dec->mutex );

  if(!dec->dec_thread)
  {
    if(!dec->buffer_size || (GST_BUFFER_SIZE(dec->buffer) >= dec->buffer_size))
    {
      GST_DEBUG_OBJECT(dec,"Pre-buffering complete");    
      // Let's start decoding as soon as possible
      pthread_create( &dec->dec_thread, NULL, gst_shvideodec_decode, dec);
    } 
    else
    {
      //Send buffering message.
      percent = GST_BUFFER_SIZE(dec->buffer) * 100 / dec->buffer_size;
      GST_LOG_OBJECT(dec,"Pre-buffering %d",percent);    
      gst_element_post_message((GstElement*)dec,gst_message_new_buffering((GstObject*)dec,percent));      
    }
  }

  // If the decoder was waiting for data.
  pthread_mutex_lock( &dec->cond_mutex );
  pthread_cond_signal( &dec->thread_condition);
  pthread_mutex_unlock( &dec->cond_mutex );

  return ret;
}

void *
gst_shvideodec_decode (void *data)
{
  int used_bytes;
  GstBuffer* buffer;

  Gstshvideodec *dec = (Gstshvideodec *)data;

  GST_LOG_OBJECT(dec,"%s called\n",__FUNCTION__);

  do
  {
    if(!dec->buffer)
    {
      GST_DEBUG_OBJECT(dec,"Waiting for data.");        
      pthread_mutex_lock( &dec->cond_mutex );
      pthread_cond_wait( &dec->thread_condition, &dec->cond_mutex );
      pthread_mutex_unlock( &dec->cond_mutex );
      GST_DEBUG_OBJECT(dec,"Got signal");        
    }

    pthread_mutex_lock(&dec->mutex);
    buffer = dec->buffer;
    dec->buffer = NULL; 
    dec->playback_timestamp = dec->buffer_timestamp;
    dec->playback_frames = dec->buffer_frames;
    pthread_mutex_unlock(&dec->mutex); 

    // If the other thread was waiting for buffer to be consumed
    pthread_mutex_lock( &dec->cond_mutex );
    pthread_cond_signal( &dec->thread_condition);
    pthread_mutex_unlock( &dec->cond_mutex );

    GST_DEBUG_OBJECT(dec,"Input buffer size: %d frames %d",
		   GST_BUFFER_SIZE (buffer), dec->playback_frames);

    dec->playback_played = 0;

    used_bytes = shcodecs_decode(dec->decoder,
		    GST_BUFFER_DATA (buffer),
		    GST_BUFFER_SIZE (buffer));

    GST_DEBUG_OBJECT(dec,"Used: %d decoded, %d frames, total %d frames",
		   used_bytes, dec->playback_played, shcodecs_decoder_get_frame_count(dec->decoder));

    if(GST_BUFFER_SIZE(buffer) != used_bytes)
    {    
      GST_DEBUG_OBJECT(dec,"Skipped %d frames and %d bytes of data",
		       dec->playback_frames-dec->playback_played,
		       GST_BUFFER_SIZE(buffer)-used_bytes);
    }

    if(!dec->running)
    {
      GST_DEBUG_OBJECT(dec,"We are done, calling finalize.");
      shcodecs_decoder_finalize(dec->decoder);
      GST_DEBUG_OBJECT(dec,"Stream finalized. Total decoded %d frames.",
		       shcodecs_decoder_get_frame_count(dec->decoder));
    }    
    gst_buffer_unref(buffer);
    buffer = NULL;
  }while(dec->running);

  return NULL;
}

static int
gst_shcodecs_decoded_callback (SHCodecs_Decoder * decoder,
			       unsigned char * y_buf, int y_size,
			       unsigned char * c_buf, int c_size,
			       void * user_data)
{
  GstClockTime time_now;
  long long unsigned int time_diff, stamp_diff, sleep_time;
  Gstshvideodec *dec = (Gstshvideodec *) user_data;

  if(dec->paused)
  {
    pthread_mutex_lock( &dec->pause_mutex );
    pthread_cond_wait( &dec->pause_condition, &dec->pause_mutex );
    pthread_mutex_unlock( &dec->pause_mutex );
  }

  // Zero copy: Set the playback address to VPU mem  
  sh_vidix.offset.y=(unsigned)y_buf;
  sh_vidix.offset.u=(unsigned)c_buf;

  if (dec->waiting_for_first_frame)
  { 
    dec->start_time = gst_clock_get_time(dec->clock);
    dec->first_timestamp = dec->playback_timestamp;
    dec->waiting_for_first_frame = FALSE;
  }

  time_now = gst_clock_get_time(dec->clock);
  time_diff = GST_TIME_AS_MSECONDS(GST_CLOCK_DIFF(dec->start_time,time_now)); 
  stamp_diff = 
    GST_TIME_AS_MSECONDS(dec->playback_timestamp)
    + ( dec->playback_played * ( 1000 * dec->fps_denominator / dec->fps_numerator ) )
    - GST_TIME_AS_MSECONDS(dec->first_timestamp);

  if(stamp_diff > time_diff)
  {
    sleep_time = stamp_diff - time_diff;
  }
  else
  {
    sleep_time = 0;
  }

  if(!dec->playback_played)
  {
    GST_DEBUG_OBJECT(dec,"Frame number: %d time from start: %llu stamp diff: %llu",
		   dec->playback_played,time_diff,stamp_diff);
  }
  else
  {
    GST_LOG_OBJECT(dec,"Frame number: %d time from start: %llu stamp diff: %llu",
		   dec->playback_played,time_diff,stamp_diff);
  }
 

  if(sleep_time)
  {
    if(!dec->playback_played)
    {    
      GST_DEBUG_OBJECT(dec,"sleeping for: %llums",sleep_time);
    }
    else
    {    
      GST_LOG_OBJECT(dec,"sleeping for: %llums",sleep_time);
    }
    usleep(sleep_time*1000);
  }  

  // Blit and then wait.
  sh_veu_blit(&sh_vidix, 0);
  sh_veu_wait_irq(&sh_vidix);

  dec->playback_played++;
  return 1;
}

gboolean
gst_shvideo_dec_plugin_init (GstPlugin * plugin)
{

  GST_LOG_OBJECT("%s called\n",__FUNCTION__);

  if (!gst_element_register (plugin, "gst-sh-mobile-dec-sink", GST_RANK_NONE,
          GST_TYPE_SHVIDEODEC))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gst-sh-mobile-dec-sink",
    "gst-sh-mobile",
    gst_shvideo_dec_plugin_init,
    VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

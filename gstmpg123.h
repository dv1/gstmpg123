/*
*   MP3 decoding plugin for GStreamer using the mpg123 library
*   Copyright (C) 2012 Carlos Rafael Giani
*
*   This library is free software; you can redistribute it and/or
*   modify it under the terms of the GNU Lesser General Public
*   License as published by the Free Software Foundation; either
*   version 2.1 of the License, or (at your option) any later version.
*
*   This library is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*   Lesser General Public License for more details.
*
*   You should have received a copy of the GNU Lesser General Public
*   License along with this library; if not, write to the Free Software
*   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */



#ifndef GSTMPG123_H
#define GSTMPG123_H

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include <mpg123.h>


G_BEGIN_DECLS


typedef struct _GstMpg123 GstMpg123;
typedef struct _GstMpg123Class GstMpg123Class;


#define GST_TYPE_MPG123             (gst_mpg123_get_type())
#define GST_MPG123(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_MPG123,GstMpg123))
#define GST_MPG123_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MPG123,GstMpg123Class))
#define GST_IS_MPG123(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_MPG123))
#define GST_IS_MPG123_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MPG123))

#if GST_CHECK_VERSION(1, 0, 0)
#define GST_MPG123_USING_GSTREAMER_1_0
#endif


struct _GstMpg123
{
	GstAudioDecoder parent;
	mpg123_handle *handle;
#ifdef GST_MPG123_USING_GSTREAMER_1_0
	GstAudioInfo next_audioinfo;
	gboolean has_next_audioinfo;
#else
	GstCaps *next_srccaps;
#endif
	off_t frame_offset;
};


struct _GstMpg123Class
{
	GstAudioDecoderClass parent_class;
};


GType gst_mpg123_get_type(void);


G_END_DECLS


#endif


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



#include <assert.h>
#include <stdlib.h>
#include <config.h>
#include "gstmpg123.h"


GST_DEBUG_CATEGORY_STATIC(mpg123_debug);
#define GST_CAT_DEFAULT mpg123_debug


/*
MPEG1 layer 1 defines 384 samples per frame, layer 2 and 3 define 1152 per frame.
This applies to VBR and ABR streams as well.
The maximum number of channels is 2.
The biggest sample format is 64-bit float (8 byte).
-> Use these for the initial output buffer size, which shall be the largest size the decoder
can produce. After decoding, adjust the buffer size to the actual decoded size.
*/
enum
{
	INITIAL_OUTPUT_BUFFER_SIZE = 1152 * 8 * 2
};


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw-int, "
		"endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
		"signed = (boolean) { false, true }, "
		"width = (int) { 8, 16, 24, 32 }, "
		"depth = (int) { 8, 16, 24, 32 }, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ]; "

		"audio/x-raw-float, "
		"endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
		"width = (int) { 32, 64 }, "
		"depth = (int) { 32, 64 }, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ]; "

		"audio/x-alaw, "
		"width = (int) 8, "
		"depth = (int) 8, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ]; "

		"audio/x-mulaw, "
		"width = (int) 8, "
		"depth = (int) 8, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ] "
	)
);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/mpeg, "
		"mpegversion = (int) { 1 }, "
		"layer = (int) [ 1, 3 ], "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ]"
	)
);


static void gst_mpg123_finalize(GObject *object);
static gboolean gst_mpg123_start(GstAudioDecoder *dec);
static gboolean gst_mpg123_stop(GstAudioDecoder *dec);
static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer);


GST_BOILERPLATE(GstMpg123, gst_mpg123, GstAudioDecoder, GST_TYPE_AUDIO_DECODER)


static char const * media_type_int   = "audio/x-raw-int";
static char const * media_type_float = "audio/x-raw-float";
static char const * media_type_alaw  = "audio/x-alaw";
static char const * media_type_mulaw = "audio/x-mulaw";



void gst_mpg123_base_init(gpointer klass)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_details_simple(
		element_class,
		"mpg123 mp3 decoder",
		"Codec/Decoder/Audio",
		"Decodes mp3 streams using the mpg123 library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
}


void gst_mpg123_class_init(GstMpg123Class *klass)
{
	GObjectClass *object_class;
	GstAudioDecoderClass *base_class;
	int error;

	GST_DEBUG_CATEGORY_INIT(mpg123_debug, "mpg123", 0, "mpg123 mp3 decoder");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_AUDIO_DECODER_CLASS(klass);

	object_class->finalize = gst_mpg123_finalize;

	base_class->start = GST_DEBUG_FUNCPTR(gst_mpg123_start);
	base_class->stop = GST_DEBUG_FUNCPTR(gst_mpg123_stop);
	base_class->handle_frame = GST_DEBUG_FUNCPTR(gst_mpg123_handle_frame);

	error = mpg123_init();
	if (G_UNLIKELY(error != MPG123_OK))
	{
		GST_ERROR("Could not initialize mpg123 library: %s", mpg123_plain_strerror(error));
	}

	GST_TRACE("mpg123 library initialized");
}


void gst_mpg123_init(GstMpg123 *decoder, GstMpg123Class *klass)
{
	klass = klass;
	decoder->handle = NULL;
}


static void gst_mpg123_finalize(GObject *object)
{
	GstMpg123 *decoder = GST_MPG123(object);
	if (G_LIKELY(decoder->handle != NULL))
	{
		mpg123_delete(decoder->handle);
		decoder->handle = NULL;
	}
}


static gboolean gst_mpg123_start(GstAudioDecoder *dec)
{
	GstMpg123 *decoder;
	int error;

	decoder = GST_MPG123(dec);
	error = 0;

	decoder->format_received = FALSE;
	decoder->last_decode_retval = MPG123_NEED_MORE;
	decoder->handle = mpg123_new(NULL, &error);
	decoder->output_buffer = NULL;
	mpg123_param(decoder->handle, MPG123_ADD_FLAGS, MPG123_GAPLESS, 0);
	mpg123_param(decoder->handle, MPG123_ADD_FLAGS, MPG123_SEEKBUFFER, 0); /* this tells mpg123 to use a small read-ahead buffer for better MPEG sync; essential for MP3 radio streams */
	mpg123_param(decoder->handle, MPG123_RESYNC_LIMIT, -1, 0); /* sets the resync limit to the end of the stream (e.g. don't give up prematurely) */
	mpg123_open_feed(decoder->handle);

	if (G_UNLIKELY(error != MPG123_OK))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Error opening mpg123 feed: %s", mpg123_plain_strerror(error)));
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_mpg123_stop(GstAudioDecoder *dec)
{
	GstMpg123 *decoder = GST_MPG123(dec);

	if (G_LIKELY(decoder->handle != NULL))
	{
		mpg123_delete(decoder->handle);
		decoder->handle = NULL;
	}

	if (decoder->output_buffer != NULL)
	{
		gst_buffer_unref(decoder->output_buffer);
		decoder->output_buffer = NULL;
	}

	return TRUE;
}


static gboolean gst_mpg123_use_new_format(GstMpg123 *decoder)
{
	{
		long rate;
		int channels, enc, width;
		GstCaps *caps;
		char const *media_type;
		gboolean signed_ = FALSE;

		{
			int ret;

			ret = mpg123_getformat(decoder->handle, &rate, &channels, &enc);

			if (ret != MPG123_OK)
			{
				GstElement *element = GST_ELEMENT(decoder);
				GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Error while getting mpg123 format: %s", mpg123_plain_strerror(ret)));

				return FALSE;
			}
		}

		switch (enc)
		{
#ifdef MPG123_ENC_UNSIGNED_8_SUPPORTED
			case MPG123_ENC_UNSIGNED_8:  media_type = media_type_int; signed_ = FALSE; width = 1; break;
#endif
#ifdef MPG123_ENC_SIGNED_8_SUPPORTED
			case MPG123_ENC_SIGNED_8:    media_type = media_type_int; signed_ = TRUE;  width = 1; break;
#endif
#ifdef MPG123_ENC_UNSIGNED_16_SUPPORTED
			case MPG123_ENC_UNSIGNED_16: media_type = media_type_int; signed_ = FALSE; width = 2; break;
#endif
#ifdef MPG123_ENC_SIGNED_16_SUPPORTED
			case MPG123_ENC_SIGNED_16:   media_type = media_type_int; signed_ = TRUE;  width = 2; break;
#endif
#ifdef MPG123_ENC_UNSIGNED_24_SUPPORTED
			case MPG123_ENC_UNSIGNED_24: media_type = media_type_int; signed_ = FALSE; width = 3; break;
#endif
#ifdef MPG123_ENC_SIGNED_24_SUPPORTED
			case MPG123_ENC_SIGNED_24:   media_type = media_type_int; signed_ = TRUE;  width = 3; break;
#endif
#ifdef MPG123_ENC_UNSIGNED_32_SUPPORTED
			case MPG123_ENC_UNSIGNED_32: media_type = media_type_int; signed_ = FALSE; width = 4; break;
#endif
#ifdef MPG123_ENC_SIGNED_32_SUPPORTED
			case MPG123_ENC_SIGNED_32:   media_type = media_type_int; signed_ = TRUE;  width = 4; break;
#endif

#ifdef MPG123_ENC_FLOAT_32_SUPPORTED
			case MPG123_ENC_FLOAT_32: media_type = media_type_float; width = 4; break;
#endif
#ifdef MPG123_ENC_FLOAT_64_SUPPORTED
			case MPG123_ENC_FLOAT_64: media_type = media_type_float; width = 8; break;
#endif

#ifdef MPG123_ENC_ALAW_8_SUPPORTED
			case MPG123_ENC_ALAW_8: media_type = media_type_alaw;  width = 1; break;
#endif
#ifdef MPG123_ENC_ULAW_8_SUPPORTED
			case MPG123_ENC_ULAW_8: media_type = media_type_mulaw; width = 1; break;
#endif

			default:
				media_type = NULL;
				width = 0;
				GST_ERROR_OBJECT(decoder, "Unknown format %d", enc);
				return FALSE;
		}

		caps = gst_caps_new_simple(
			media_type,
			"width", G_TYPE_INT, width * 8,
			"depth", G_TYPE_INT, width * 8,
			"rate", G_TYPE_INT, rate,
			"channels", G_TYPE_INT, channels,
			NULL
		);
		if (caps == NULL)
		{
			GstElement *element = GST_ELEMENT(decoder);
			GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Could not get caps for new format (%d bpp, %d Hz, %d channels)", width * 8, rate, channels));
			return FALSE;
		}

		if (media_type == media_type_int) /* NOTE: pointer comparison is intentional */
		{
			gst_caps_set_simple(
				caps,
				"endianness", G_TYPE_INT, G_BYTE_ORDER,
				"signed", G_TYPE_BOOLEAN, signed_,
				NULL
			);
		}

		gst_pad_set_caps(GST_AUDIO_DECODER_SRC_PAD(decoder), caps);

		if (decoder->output_buffer != NULL)
		{
			gst_buffer_set_caps(decoder->output_buffer, caps);
		}

		gst_caps_unref(caps);
	}

	decoder->format_received = TRUE;

	return TRUE;
}


static void gst_mpg123_push_output_buffer(GstMpg123 *decoder, size_t num_decoded_bytes)
{
	if (num_decoded_bytes == 0)
		return;

	GST_BUFFER_SIZE(decoder->output_buffer) = num_decoded_bytes;
	GST_TRACE_OBJECT(decoder, "Pushing output buffer with %d byte", num_decoded_bytes);
	gst_audio_decoder_finish_frame(GST_AUDIO_DECODER(decoder), decoder->output_buffer, 1);
	decoder->output_buffer = NULL;
}


static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer)
{
	unsigned char const *inmemory;
	size_t inmemsize;
	GstMpg123 *decoder;

	if (G_UNLIKELY(!buffer))
		return GST_FLOW_OK;

	inmemory = (unsigned char const *)(GST_BUFFER_DATA(buffer));
	inmemsize = GST_BUFFER_SIZE(buffer);
	decoder = GST_MPG123(dec);

	if (decoder->format_received)
	{
		unsigned char *outmemory;
		size_t outmemsize;
		int decode_error;
		size_t num_decoded_bytes = 0;

		GST_TRACE_OBJECT(decoder, "About to decode input data and push decoded samples downstream");

		if (decoder->output_buffer == NULL)
		{
			GstFlowReturn alloc_error;
			GST_TRACE_OBJECT(decoder, "No output buffer exists - creating a new one");
			alloc_error = gst_pad_alloc_buffer_and_set_caps(
				GST_AUDIO_DECODER_SRC_PAD(decoder),
				GST_BUFFER_OFFSET_NONE,
				INITIAL_OUTPUT_BUFFER_SIZE,
				GST_PAD_CAPS(GST_AUDIO_DECODER_SRC_PAD(decoder)),
				&(decoder->output_buffer)
			);
			if (alloc_error != GST_FLOW_OK)
			{
				/* TODO: if decoder->output_buffer is not NULL after the alloc call, should it be unref'd? */
				GstElement *element = GST_ELEMENT(dec);
				GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Creating new output buffer failed"));
				return alloc_error;
			}
		}
		else
		{
			GST_TRACE_OBJECT(decoder, "Output buffer already exists - reusing it");
		}

		outmemory = (unsigned char *) GST_BUFFER_DATA(decoder->output_buffer);
		outmemsize = GST_BUFFER_SIZE(decoder->output_buffer);
		decode_error = mpg123_decode(decoder->handle, inmemory, inmemsize, outmemory, outmemsize, &num_decoded_bytes);

		switch (decode_error)
		{
			case MPG123_NEED_MORE:
				gst_mpg123_push_output_buffer(decoder, num_decoded_bytes);
				break;
			case MPG123_DONE:
				GST_DEBUG_OBJECT(decoder, "mpg123 is done decoding");
				gst_mpg123_push_output_buffer(decoder, num_decoded_bytes);
				return GST_FLOW_UNEXPECTED;
			case MPG123_NEW_FORMAT:
			{
				GST_DEBUG_OBJECT(decoder, "mpg123 reports a new format");
				if (!gst_mpg123_use_new_format(decoder))
				{
					GstElement *element = GST_ELEMENT(dec);
					GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Using new format failed"));
					return GST_FLOW_ERROR;
				}
				GST_DEBUG_OBJECT(decoder, "Successfully using new format");
				gst_mpg123_push_output_buffer(decoder, num_decoded_bytes);
				break;
			}
			default:
			{
				GstElement *element = GST_ELEMENT(dec);
				GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Decoding error: %s", mpg123_plain_strerror(decode_error)));
				return GST_FLOW_ERROR;
			}
		}
	}
	else
	{
		GST_TRACE_OBJECT(decoder, "Parsing for format data (no audio data passed downstream yet)");
		int decode_error = mpg123_decode(decoder->handle, inmemory, inmemsize, NULL, 0, NULL);

		switch (decode_error)
		{
			case MPG123_NEED_MORE:
				break;
			case MPG123_DONE:
				GST_DEBUG_OBJECT(decoder, "mpg123 is done decoding");
				return GST_FLOW_UNEXPECTED;
			case MPG123_NEW_FORMAT:
			{
				GST_DEBUG_OBJECT(decoder, "mpg123 reports a new format");
				if (!gst_mpg123_use_new_format(decoder))
				{
					GstElement *element = GST_ELEMENT(dec);
					GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Using new format failed"));
					return GST_FLOW_ERROR;
				}
				GST_DEBUG_OBJECT(decoder, "Successfully using new format");
				break;
			}
			default:
			{
				GstElement *element = GST_ELEMENT(dec);
				GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Decoding error: %s", mpg123_plain_strerror(decode_error)));
				return GST_FLOW_ERROR;
			}
		}
	}

	return GST_FLOW_OK;
}




static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "mpg123", GST_RANK_PRIMARY, gst_mpg123_get_type());
}


#define PACKAGE "package"


GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"mpg123",
	"mp3 decoding based on the mpg123 library",
	plugin_init,
	"1.0",
	"LGPL",
	PACKAGE,
	"http://no-url-yet"
)


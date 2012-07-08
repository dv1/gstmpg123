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
#include <string.h>
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


/*
Omitted sample formats that mpg123 supports (or at least can support):
8bit integer signed
8bit integer unsigned
a-law
mu-law
64bit float

The first four formats are not supported by the GstAudioDecoder base class.
(The internal gst_audio_format_from_caps_structure() call fails.)
The 64bit float issue is tricky. mpg123 actually decodes to "real", not necessarily to "float".
"real" can be fixed point, 32bit float, 64bit float. There seems to be no way how to find out which one of them is actually used.
However, in all known installations, "real" equals 32bit float, so that's what is used.
*/


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw-int, "
		"endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
		"signed = (boolean) { false, true }, "
		"width = (int) { 16, 24, 32 }, "
		"depth = (int) { 16, 24, 32 }, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ]; "

		"audio/x-raw-float, "
		"endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
		"width = (int) 32, "
		"depth = (int) 32, "
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
		"channels = (int) [ 1, 2 ], "
		"parsed = (boolean) true "
	)
);


static void gst_mpg123_finalize(GObject *object);
static gboolean gst_mpg123_start(GstAudioDecoder *dec);
static gboolean gst_mpg123_stop(GstAudioDecoder *dec);
static void gst_mpg123_push_output_buffer(GstMpg123 *decoder, GstBuffer *output_buffer, size_t num_decoded_bytes);
static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer);
static gboolean gst_mpg123_determine_encoding(char const *media_type, int const width, gboolean const width_available, gboolean const signed_, gboolean *is_integer, int *encoding);
static gboolean gst_mpg123_set_format(GstAudioDecoder *dec, GstCaps *incoming_caps);


GST_BOILERPLATE(GstMpg123, gst_mpg123, GstAudioDecoder, GST_TYPE_AUDIO_DECODER)


static char const * media_type_int   = "audio/x-raw-int";
static char const * media_type_float = "audio/x-raw-float";



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
	base_class->set_format = GST_DEBUG_FUNCPTR(gst_mpg123_set_format);

	error = mpg123_init();
	if (G_UNLIKELY(error != MPG123_OK))
		GST_ERROR("Could not initialize mpg123 library: %s", mpg123_plain_strerror(error));
	else
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

	decoder->handle = mpg123_new(NULL, &error);
	mpg123_format_none(decoder->handle);
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

	return TRUE;
}


static void gst_mpg123_push_output_buffer(GstMpg123 *decoder, GstBuffer *output_buffer, size_t num_decoded_bytes)
{
	/* This usually happens at the beginning, before mpg123 has enough information to actually decode something. */
	if (num_decoded_bytes == 0)
	{
		gst_buffer_unref(output_buffer);
		return;
	}

	GST_BUFFER_SIZE(output_buffer) = num_decoded_bytes;
	GST_TRACE_OBJECT(decoder, "Pushing output buffer with %d byte", num_decoded_bytes);
	gst_audio_decoder_finish_frame(GST_AUDIO_DECODER(decoder), output_buffer, 1);
}


static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer)
{
	unsigned char const *inmemory;
	size_t inmemsize;
	GstMpg123 *decoder;
	GstBuffer *output_buffer;

	if (G_UNLIKELY(!buffer))
		return GST_FLOW_OK;

	inmemory = (unsigned char const *)(GST_BUFFER_DATA(buffer));
	inmemsize = GST_BUFFER_SIZE(buffer);
	decoder = GST_MPG123(dec);

	unsigned char *outmemory;
	size_t outmemsize;
	int decode_error;
	size_t num_decoded_bytes = 0;

	GST_TRACE_OBJECT(decoder, "About to decode input data and push decoded samples downstream");

	{
		GstFlowReturn alloc_error;
		GST_TRACE_OBJECT(decoder, "Creating new output buffer with caps %" GST_PTR_FORMAT, GST_PAD_CAPS(GST_AUDIO_DECODER_SRC_PAD(decoder)));
		alloc_error = gst_pad_alloc_buffer(
			GST_AUDIO_DECODER_SRC_PAD(decoder),
			GST_BUFFER_OFFSET_NONE,
			INITIAL_OUTPUT_BUFFER_SIZE,
			GST_PAD_CAPS(GST_AUDIO_DECODER_SRC_PAD(decoder)),
			&output_buffer
		);
		if (alloc_error != GST_FLOW_OK)
		{
			/* TODO: if decoder->output_buffer is not NULL after the alloc call, should it be unref'd? */
			GstElement *element = GST_ELEMENT(dec);
			GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Creating new output buffer failed"));
			return alloc_error;
		}
	}

	outmemory = (unsigned char *) GST_BUFFER_DATA(output_buffer);
	outmemsize = GST_BUFFER_SIZE(output_buffer);
	decode_error = mpg123_decode(decoder->handle, inmemory, inmemsize, outmemory, outmemsize, &num_decoded_bytes);

	switch (decode_error)
	{
		/* NEW_FORMAT information is redundant; upstream told us about the rate and number of channels, downstream about the sample format */
		case MPG123_NEW_FORMAT:
		case MPG123_NEED_MORE:
		case MPG123_OK:
			gst_mpg123_push_output_buffer(decoder, output_buffer, num_decoded_bytes);
			break;
		case MPG123_DONE:
			GST_DEBUG_OBJECT(decoder, "mpg123 is done decoding");
			gst_mpg123_push_output_buffer(decoder, output_buffer, num_decoded_bytes);
			return GST_FLOW_UNEXPECTED;
		default:
		{
			GstElement *element = GST_ELEMENT(dec);
			GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Decoding error: %s", mpg123_plain_strerror(decode_error)));
			gst_buffer_unref(output_buffer);
			return GST_FLOW_ERROR;
		}
	}

	return GST_FLOW_OK;
}


static gboolean gst_mpg123_determine_encoding(char const *media_type, int const width, gboolean const width_available, gboolean const signed_, gboolean *is_integer, int *encoding)
{
	if (strcmp(media_type, media_type_int) == 0)
	{
		*is_integer = TRUE;

		if (!width_available)
			return FALSE;

		if (signed_)
		{
			switch (width)
			{
#ifdef MPG123_ENC_SIGNED_16_SUPPORTED
				case 16: *encoding = MPG123_ENC_SIGNED_16; break;
#endif
#ifdef MPG123_ENC_SIGNED_24_SUPPORTED
				case 24: *encoding = MPG123_ENC_SIGNED_24; break;
#endif
#ifdef MPG123_ENC_SIGNED_32_SUPPORTED
				case 32: *encoding = MPG123_ENC_SIGNED_32; break;
#endif
				default:
					return FALSE;
			}
		}
		else
		{
			switch (width)
			{
#ifdef MPG123_ENC_UNSIGNED_16_SUPPORTED
				case 16: *encoding = MPG123_ENC_UNSIGNED_16; break;
#endif
#ifdef MPG123_ENC_UNSIGNED_24_SUPPORTED
				case 24: *encoding = MPG123_ENC_UNSIGNED_24; break;
#endif
#ifdef MPG123_ENC_UNSIGNED_32_SUPPORTED
				case 32: *encoding = MPG123_ENC_UNSIGNED_32; break;
#endif
				default:
					return FALSE;
			}
		}
	}
	else if (strcmp(media_type, media_type_float) == 0)
	{
		if (!width_available)
			return FALSE;

		switch (width)
		{
#ifdef MPG123_ENC_FLOAT_32_SUPPORTED
			case 32: *encoding = MPG123_ENC_FLOAT_32; break;
#endif
			default:
				return FALSE;
		}
	}
	else
	{
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_mpg123_set_format(GstAudioDecoder *dec, GstCaps *incoming_caps)
{
/*
	STEPS:

	1. get rate and channels from incoming_caps
	2. get allowed caps from src pad
	3. for each structure in allowed caps:
	3.1. take signed, width, media_type
	3.2. if the combination of these three values is unsupported by mpg123, go to (3)
	3.3. create candidate srccaps out of rate,channels,signed,width,media_type
	3.4. if caps is usable (=allowed by downstream, use them, call mp123_format() with these values, and exit
	3.5. otherwise, go to (3) if there are other structures; if not, exit with error
*/


	int rate, channels;
	GstMpg123 *decoder;
	GstCaps *allowed_srccaps;
	guint structure_nr;
	gboolean match_found = FALSE;

	decoder = GST_MPG123(dec);

	/* Get rate and channels from incoming_caps */
	{
		GstStructure *structure;
		gboolean err = FALSE;

		/* Only the first structure is used (multiple incoming structures don't make sense */
		structure = gst_caps_get_structure(incoming_caps, 0);

		if (!gst_structure_get_int(structure, "rate", &rate))
		{
			err = TRUE;
			GST_ERROR_OBJECT(dec, "Incoming caps do not have a rate value");
		}
		if (!gst_structure_get_int(structure, "channels", &channels))
		{
			err = TRUE;
			GST_ERROR_OBJECT(dec, "Incoming caps do not have a channel value");
		}

		if (err)
			return FALSE;
	}

	/* Get the caps that are allowed by downstream */
	allowed_srccaps = gst_pad_get_allowed_caps(GST_AUDIO_DECODER_SRC_PAD(dec));
	allowed_srccaps = gst_caps_normalize(allowed_srccaps);

	/* Go through all allowed caps, pick the first one that matches */
	for (structure_nr = 0; structure_nr < gst_caps_get_size(allowed_srccaps); ++structure_nr)
	{
		GstStructure *structure;
		GstCaps *candidate_srccaps;
		char const *media_type;
		int width;
		gboolean signed_, width_available, is_integer, set_caps_succeeded;
		int encoding;

		width_available = FALSE;
		is_integer = FALSE;

		structure = gst_caps_get_structure(allowed_srccaps, structure_nr);
		media_type = gst_structure_get_name(structure);

		if (gst_structure_get_int(structure, "width", &width))
		{
			width_available = TRUE;
		}
		if (!gst_structure_get_boolean(structure, "signed", &signed_))
		{
			signed_ = TRUE; /* default value */
		}

		if (!gst_mpg123_determine_encoding(media_type, width, width_available, signed_, &is_integer, &encoding))
		{
			GST_TRACE_OBJECT(dec, "mpg123 cannot use caps with rate %d width %d (available = %d) signed %d", rate, width, width_available, signed_);
			continue;
		}

		{
			int err;

			err = mpg123_format(decoder->handle, rate, channels, encoding);
			if (err != MPG123_OK)
			{
				GST_TRACE_OBJECT(dec, "mpg123 cannot use caps %" GST_PTR_FORMAT " because mpg123_format() failed: %s", structure, mpg123_plain_strerror(err));
				continue;
			}
		}

		candidate_srccaps = gst_caps_new_simple(
			media_type,
 			"rate", G_TYPE_INT, rate,
 			"channels", G_TYPE_INT, channels,
 			"endianness", G_TYPE_INT, G_BYTE_ORDER,
 			NULL
 		);

		if (width_available)
		{
 			gst_caps_set_simple(
				candidate_srccaps,
				"width", G_TYPE_INT, width,
				"depth", G_TYPE_INT, width,
				NULL
			);
		};

		if (is_integer)
		{
 			gst_caps_set_simple(
				candidate_srccaps,
 				"signed", G_TYPE_BOOLEAN, signed_,
 				NULL
 			);
		}

		GST_TRACE_OBJECT(dec, "Setting srccaps %" GST_PTR_FORMAT, candidate_srccaps);

		set_caps_succeeded = gst_pad_set_caps(GST_AUDIO_DECODER_SRC_PAD(dec), candidate_srccaps);
		gst_caps_unref(candidate_srccaps);

		if (set_caps_succeeded)
		{
			match_found = TRUE;
			break;
		}
	}

	gst_caps_unref(allowed_srccaps);

	return match_found;
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


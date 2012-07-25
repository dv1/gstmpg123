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



/*
TODO: This port is not working yet - waiting for 1.0 to stabilize, then development will resume
DO NOT USE
*/



#include <stdlib.h>
#include <string.h>
#include <config.h>
#include "gstmpg123.h"


GST_DEBUG_CATEGORY_STATIC(mpg123_debug);
#define GST_CAT_DEFAULT mpg123_debug


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
		"audio/x-raw, "
		"format = { S16LE, U16LE, S24LE, U24LE, S32LE, U32LE }, "
		"rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
		"channels = (int) [ 1, 2 ], "
		"layout = (string) interleaved; "
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
static GstFlowReturn gst_mpg123_push_decoded_bytes(GstMpg123 *mpg123_decoder, unsigned char const *decoded_bytes, size_t const num_decoded_bytes);
static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer);
static gboolean gst_mpg123_set_format(GstAudioDecoder *dec, GstCaps *incoming_caps);
static void gst_mpg123_flush(GstAudioDecoder *dec, gboolean hard);


G_DEFINE_TYPE(GstMpg123, gst_mpg123, GST_TYPE_AUDIO_DECODER)



void gst_mpg123_class_init(GstMpg123Class *klass)
{
	GObjectClass *object_class;
	GstAudioDecoderClass *base_class;
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
	int error;

	GST_DEBUG_CATEGORY_INIT(mpg123_debug, "mpg123", 0, "mpg123 mp3 decoder");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_AUDIO_DECODER_CLASS(klass);

	object_class->finalize = gst_mpg123_finalize;

	gst_element_class_set_static_metadata(
		element_class,
		"mpg123 mp3 decoder",
		"Codec/Decoder/Audio",
		"Decodes mp3 streams using the mpg123 library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	base_class->start        = GST_DEBUG_FUNCPTR(gst_mpg123_start);
	base_class->stop         = GST_DEBUG_FUNCPTR(gst_mpg123_stop);
	base_class->handle_frame = GST_DEBUG_FUNCPTR(gst_mpg123_handle_frame);
	base_class->set_format   = GST_DEBUG_FUNCPTR(gst_mpg123_set_format);
	base_class->flush        = GST_DEBUG_FUNCPTR(gst_mpg123_flush);

	error = mpg123_init();
	if (G_UNLIKELY(error != MPG123_OK))
		GST_ERROR("Could not initialize mpg123 library: %s", mpg123_plain_strerror(error));
	else
		GST_TRACE("mpg123 library initialized");
}


void gst_mpg123_init(GstMpg123 *mpg123_decoder)
{
	mpg123_decoder->handle = NULL;
}


static void gst_mpg123_finalize(GObject *object)
{
	GstMpg123 *mpg123_decoder = GST_MPG123(object);
	if (G_LIKELY(mpg123_decoder->handle != NULL))
	{
		mpg123_delete(mpg123_decoder->handle);
		mpg123_decoder->handle = NULL;
	}
}


static gboolean gst_mpg123_start(GstAudioDecoder *dec)
{
	GstMpg123 *mpg123_decoder;
	int error;

	mpg123_decoder = GST_MPG123(dec);
	error = 0;

	mpg123_decoder->handle = mpg123_new(NULL, &error);
	mpg123_decoder->next_srccaps = NULL;
	mpg123_decoder->frame_offset = 0;

	/*
	Initially, the mpg123 handle comes with a set of default formats supported. This clears this set. 
	This is necessary, since only one format shall be supported (see set_format for more).
	*/
	mpg123_format_none(mpg123_decoder->handle);

	mpg123_param(mpg123_decoder->handle, MPG123_ADD_FLAGS,    MPG123_GAPLESS,    0); /* Enables support for gapless decoding (MP3 bitstream must have LAME/Xing extensions for this */
	mpg123_param(mpg123_decoder->handle, MPG123_ADD_FLAGS,    MPG123_SEEKBUFFER, 0); /* Tells mpg123 to use a small read-ahead buffer for better MPEG sync; essential for MP3 radio streams */
	mpg123_param(mpg123_decoder->handle, MPG123_RESYNC_LIMIT, -1,                0); /* Sets the resync limit to the end of the stream (e.g. don't give up prematurely) */

	/* Open in feed mode (= encoded data is fed manually into the handle). */
	error = mpg123_open_feed(mpg123_decoder->handle);

	if (G_UNLIKELY(error != MPG123_OK))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Error opening mpg123 feed: %s", mpg123_plain_strerror(error)));
		mpg123_close(mpg123_decoder->handle);
		mpg123_delete(mpg123_decoder->handle);
		mpg123_decoder->handle = NULL;
		return FALSE;
	}

	GST_DEBUG_OBJECT(dec, "mpg123 decoder started");

	return TRUE;
}


static gboolean gst_mpg123_stop(GstAudioDecoder *dec)
{
	GstMpg123 *mpg123_decoder = GST_MPG123(dec);

	if (G_LIKELY(mpg123_decoder->handle != NULL))
	{
		mpg123_close(mpg123_decoder->handle);
		mpg123_delete(mpg123_decoder->handle);
		mpg123_decoder->handle = NULL;
	}

	GST_DEBUG_OBJECT(dec, "mpg123 decoder stopped");

	return TRUE;
}


static GstFlowReturn gst_mpg123_push_decoded_bytes(GstMpg123 *mpg123_decoder, unsigned char const *decoded_bytes, size_t const num_decoded_bytes)
{
	GstBuffer *output_buffer;
	GstFlowReturn alloc_error;
	GstAudioDecoder *dec;

	output_buffer = NULL;
	dec = GST_AUDIO_DECODER(mpg123_decoder);

	if ((num_decoded_bytes == 0) || (decoded_bytes == NULL))
	{
		GST_TRACE_OBJECT(mpg123_decoder, "Nothing was decoded -> no output buffer to push");
		return gst_audio_decoder_finish_frame(dec, NULL, 1);
	}

	output_buffer = gst_buffer_new_allocate(NULL, num_decoded_bytes, NULL);
	alloc_error = (output_buffer == NULL) ? GST_FLOW_ERROR : GST_FLOW_OK;

	if (alloc_error != GST_FLOW_OK)
	{
		/* This is necessary to advance playback in time, even when nothing was decoded. */
		return gst_audio_decoder_finish_frame(dec, NULL, 1);
	}
	else
	{
		GstMemory *memory;
		GstMapInfo info;

		memory = gst_buffer_get_all_memory(output_buffer);
		if (memory != NULL)
		{
			if (gst_memory_map(memory, &info, GST_MAP_WRITE))
			{
				if (info.size != num_decoded_bytes)
					GST_ERROR_OBJECT(mpg123_decoder, "Mapped memory region has size %u instead of expected size %u", info.size, num_decoded_bytes);
				else
					memcpy(info.data, decoded_bytes, num_decoded_bytes);

				gst_memory_unmap(memory, &info);
			}
			else
				GST_ERROR_OBJECT(mpg123_decoder, "Could not map memory region of size %u", info.size);
			gst_memory_unref(memory);
		}
		else
			GST_ERROR_OBJECT(mpg123_decoder, "Unable to access output buffer memory");

		return gst_audio_decoder_finish_frame(dec, output_buffer, 1);
	}
}


static GstFlowReturn gst_mpg123_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer)
{
	GstMpg123 *mpg123_decoder;
	int decode_error;
	unsigned char *decoded_bytes;
	size_t num_decoded_bytes;

	if (G_UNLIKELY(!buffer))
		return GST_FLOW_OK;

	mpg123_decoder = GST_MPG123(dec);

	if (G_UNLIKELY(mpg123_decoder->handle == NULL))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("mpg123 handle is NULL"));
		return GST_FLOW_ERROR;
	}

	/* The actual decoding */
	{
		unsigned char const *inmemory;
		size_t inmemsize;
		GstMemory *memory;
		GstMapInfo info;

		memory = gst_buffer_get_all_memory(buffer);
		if (memory == NULL)
			return GST_FLOW_ERROR;

		if (!gst_memory_map(memory, &info, GST_MAP_WRITE))
		{
			gst_memory_unref(memory);
			return GST_FLOW_ERROR;
		}

		inmemory = info.data;
		inmemsize = info.size;

		mpg123_feed(mpg123_decoder->handle, inmemory, inmemsize);
		decoded_bytes = NULL;
		num_decoded_bytes = 0;
		decode_error = mpg123_decode_frame(
			mpg123_decoder->handle,
			&mpg123_decoder->frame_offset,
			&decoded_bytes,
			&num_decoded_bytes
		);

		gst_memory_unmap(memory, &info);
		gst_memory_unref(memory);
	}

	switch (decode_error)
	{
		case MPG123_NEW_FORMAT:
			/*
			As mentioned in gst_mpg123_set_format(), the next srccaps are not set immediately;
			instead, the code waits for mpg123 to take note of the new format, and then sets the caps
			This fixes glitches with mp3s containing several format headers (for example, first half using 44.1kHz, second half 32 kHz)
			*/

			GST_DEBUG_OBJECT(dec, "mpg123 reported a new format -> setting next srccaps");
			
			gst_mpg123_push_decoded_bytes(mpg123_decoder, decoded_bytes, num_decoded_bytes);

			/*
			If there are next srccaps, use them, unref, and set the pointer to NULL, to make sure set_caps isn't called again
			until set_format is called again by the base class
			*/
			if (mpg123_decoder->next_srccaps != NULL)
			{
				gst_pad_set_caps(GST_AUDIO_DECODER_SRC_PAD(dec), mpg123_decoder->next_srccaps);
				gst_caps_unref(mpg123_decoder->next_srccaps);
				mpg123_decoder->next_srccaps = NULL;
			}

			break;

		case MPG123_NEED_MORE:
		case MPG123_OK:
			return gst_mpg123_push_decoded_bytes(mpg123_decoder, decoded_bytes, num_decoded_bytes);

		/* If this happens, then the upstream parser somehow missed the ending of the bitstream */
		case MPG123_DONE:
			GST_DEBUG_OBJECT(dec, "mpg123 is done decoding");
			gst_mpg123_push_decoded_bytes(mpg123_decoder, decoded_bytes, num_decoded_bytes);
			return GST_FLOW_EOS;

		/* Anything else is considered an error */
		default:
		{
			GstElement *element = GST_ELEMENT(dec);
			GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Decoding error: %s", mpg123_plain_strerror(decode_error)));

			return GST_FLOW_ERROR;
		}
	}

	return GST_FLOW_OK;
}


static gboolean gst_mpg123_set_format(GstAudioDecoder *dec, GstCaps *incoming_caps)
{
/*
	Using the parsed information upstream, and the list of allowed caps downstream, this code
	tries to find a suitable format. It is important to keep in mind that the rate and number of channels
	should never deviate from the one the bitstream has, otherwise mpg123 has to mix channels and/or
	resample (and as its docs say, its internal resampler is very crude). The sample format, however,
	can be chosen freely, because the MPEG specs do not mandate any special format.
	Therefore, rate and number of channels are taken from upstream (which parsed the MPEG frames, therefore
	the incoming_caps contain exactly the rate and number of channels the bitstream actually has), while
	the sample format is chosen by trying out all caps that are allowed by downstream. This way, the output
	is adjusted to what the downstream prefers.

	Also, the new downstream caps are not set immediately. Instead, they are considered the "next srccaps".
	The code waits for mpg123 to notice the new format (= when mpg123_decode_frame() returns MPG123_NEW_FORMAT),
	and then sets the next srccaps. Otherwise, the next srccaps are set too soon, which may cause problems with
	mp3s containing several format headers. One example would be an mp3 with the first 30 seconds using 44.1 kHz,
	then the next 30 seconds using 32 kHz. Rare, but possible.

	STEPS:

	1. get rate and channels from incoming_caps
	2. get allowed caps from src pad
	3. for each structure in allowed caps:
	3.1. take signed, width, media_type
	3.2. if the combination of these three values is unsupported by mpg123, go to (3)
	3.3. create candidate srccaps out of rate,channels,signed,width,media_type
	3.4. if caps is usable (=allowed by downstream, set them as the next caps, call mp123_format() with these values, and exit
	3.5. otherwise, go to (3) if there are other structures; if not, exit with error
*/


	int rate, channels;
	GstMpg123 *mpg123_decoder;
	GstCaps *allowed_srccaps;
	guint structure_nr;
	gboolean match_found = FALSE;

	mpg123_decoder = GST_MPG123(dec);

	if (G_UNLIKELY(mpg123_decoder->handle == NULL))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("mpg123 handle is NULL"));
		return FALSE;
	}

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
	{
		GstCaps *allowed_srccaps_unnorm = gst_pad_get_allowed_caps(GST_AUDIO_DECODER_SRC_PAD(dec));
		allowed_srccaps = gst_caps_normalize(allowed_srccaps_unnorm);
		/* TODO: this causes errors with 1.0 - perhaps a bug? */
		/*gst_caps_unref(allowed_srccaps_unnorm);*/
	}

	/* Go through all allowed caps, pick the first one that matches */
	for (structure_nr = 0; structure_nr < gst_caps_get_size(allowed_srccaps); ++structure_nr)
	{
		GstStructure *structure;
		GstCaps *candidate_srccaps;
		char const *media_type;
		gchar const *format_str;
		GstAudioFormat format;
		int encoding;

		structure = gst_caps_get_structure(allowed_srccaps, structure_nr);
		media_type = gst_structure_get_name(structure);

		format_str = gst_structure_get_string(structure, "format");
		if (format_str == NULL)
		{
			GST_DEBUG_OBJECT(dec, "Could not get format from src caps");
			continue;
		}

		format = gst_audio_format_from_string(format_str);
		if (format == GST_AUDIO_FORMAT_UNKNOWN)
		{
			GST_DEBUG_OBJECT(dec, "Unknown format %s", format_str);
			continue;
		}

		switch (format)
		{
			case GST_AUDIO_FORMAT_S16: encoding = MPG123_ENC_SIGNED_16; break;
			case GST_AUDIO_FORMAT_S24: encoding = MPG123_ENC_SIGNED_24; break;
			case GST_AUDIO_FORMAT_S32: encoding = MPG123_ENC_SIGNED_32; break;
			case GST_AUDIO_FORMAT_U16: encoding = MPG123_ENC_UNSIGNED_16; break;
			case GST_AUDIO_FORMAT_U24: encoding = MPG123_ENC_UNSIGNED_24; break;
			case GST_AUDIO_FORMAT_U32: encoding = MPG123_ENC_UNSIGNED_32; break;
			case GST_AUDIO_FORMAT_F32: encoding = MPG123_ENC_FLOAT_32; break;
			default:
				GST_DEBUG_OBJECT(dec, "Format %s in srccaps is not supported by mpg123", format_str);
				continue;
		}

		{
			int err;

			/* Cleanup old formats & set new one */
			mpg123_format_none(mpg123_decoder->handle);
			err = mpg123_format(mpg123_decoder->handle, rate, channels, encoding);
			if (err != MPG123_OK)
			{
				GST_DEBUG_OBJECT(dec, "mpg123 cannot use caps %" GST_PTR_FORMAT " because mpg123_format() failed: %s", structure, mpg123_plain_strerror(err));
				continue;
			}
		}

		candidate_srccaps = gst_caps_new_simple(
			media_type,
 			"rate", G_TYPE_INT, rate,
 			"channels", G_TYPE_INT, channels,
 			"format", G_TYPE_INT, format,
 			NULL
 		);

		GST_DEBUG_OBJECT(dec, "The next srccaps are: %" GST_PTR_FORMAT, candidate_srccaps);

		match_found = TRUE;
		mpg123_decoder->next_srccaps = candidate_srccaps;
		break;
	}

	gst_caps_unref(allowed_srccaps);

	return match_found;
}


static void gst_mpg123_flush(GstAudioDecoder *dec, gboolean hard)
{
	int error;
	GstMpg123 *mpg123_decoder;

	hard = hard;

	GST_DEBUG_OBJECT(dec, "Flushing decoder");

	mpg123_decoder = GST_MPG123(dec);

	if (G_UNLIKELY(mpg123_decoder->handle == NULL))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("mpg123 handle is NULL"));
		return;
	}

	/* Flush by reopening the feed */
	mpg123_close(mpg123_decoder->handle);
	error = mpg123_open_feed(mpg123_decoder->handle);

	if (G_UNLIKELY(error != MPG123_OK))
	{
		GstElement *element = GST_ELEMENT(dec);
		GST_ELEMENT_ERROR(element, STREAM, DECODE, (NULL), ("Error reopening mpg123 feed: %s", mpg123_plain_strerror(error)));
		mpg123_close(mpg123_decoder->handle);
		mpg123_delete(mpg123_decoder->handle);
		mpg123_decoder->handle = NULL;
	}

	if (mpg123_decoder->next_srccaps != NULL)
	{
		gst_caps_unref(mpg123_decoder->next_srccaps);
		mpg123_decoder->next_srccaps = NULL;
	}

	/*
	opening/closing feeds do not affect the format defined by the mpg123_format() call that was made in gst_mpg123_set_format(),
	and since the up/downstream caps are not expected to change here, no mpg123_format() calls are done
	*/
}





static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "mpg123", GST_RANK_SECONDARY + 1, gst_mpg123_get_type());
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"mpg123",
	"mp3 decoding based on the mpg123 library",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)


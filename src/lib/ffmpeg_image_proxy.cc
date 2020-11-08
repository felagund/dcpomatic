/*
    Copyright (C) 2014-2018 Carl Hetherington <cth@carlh.net>

    This file is part of DCP-o-matic.

    DCP-o-matic is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DCP-o-matic is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DCP-o-matic.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "ffmpeg_image_proxy.h"
#include "cross.h"
#include "exceptions.h"
#include "dcpomatic_socket.h"
#include "image.h"
#include "compose.hpp"
#include "util.h"
#include "warnings.h"
#include <dcp/raw_convert.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}
DCPOMATIC_DISABLE_WARNINGS
#include <libxml++/libxml++.h>
DCPOMATIC_ENABLE_WARNINGS
#include <iostream>

#include "i18n.h"

using std::string;
using std::cout;
using std::pair;
using std::min;
using std::make_pair;
using boost::shared_ptr;
using boost::optional;
using boost::dynamic_pointer_cast;
using dcp::raw_convert;

FFmpegImageProxy::FFmpegImageProxy (boost::filesystem::path path, VideoRange video_range)
	: _data (path)
	, _video_range (video_range)
	, _pos (0)
	, _path (path)
{

}

FFmpegImageProxy::FFmpegImageProxy (dcp::ArrayData data, VideoRange video_range)
	: _data (data)
	, _video_range (video_range)
	, _pos (0)
{

}

FFmpegImageProxy::FFmpegImageProxy (shared_ptr<cxml::Node> node, shared_ptr<Socket> socket)
	: _video_range (string_to_video_range(node->string_child("VideoRange")))
	, _pos (0)
{
	uint32_t const size = socket->read_uint32 ();
	_data = dcp::ArrayData (size);
	socket->read (_data.data(), size);
}

static int
avio_read_wrapper (void* data, uint8_t* buffer, int amount)
{
	return reinterpret_cast<FFmpegImageProxy*>(data)->avio_read (buffer, amount);
}

static int64_t
avio_seek_wrapper (void* data, int64_t offset, int whence)
{
	return reinterpret_cast<FFmpegImageProxy*>(data)->avio_seek (offset, whence);
}

int
FFmpegImageProxy::avio_read (uint8_t* buffer, int const amount)
{
	int const to_do = min(static_cast<int64_t>(amount), static_cast<int64_t>(_data.size()) - _pos);
	if (to_do == 0) {
		return AVERROR_EOF;
	}
	memcpy (buffer, _data.data() + _pos, to_do);
	_pos += to_do;
	return to_do;
}

int64_t
FFmpegImageProxy::avio_seek (int64_t const pos, int whence)
{
	switch (whence) {
	case AVSEEK_SIZE:
		return _data.size();
	case SEEK_CUR:
		_pos += pos;
		break;
	case SEEK_SET:
		_pos = pos;
		break;
	case SEEK_END:
		_pos = _data.size() - pos;
		break;
	}

	return _pos;
}

DCPOMATIC_DISABLE_WARNINGS

ImageProxy::Result
FFmpegImageProxy::image (optional<dcp::Size>) const
{
	boost::mutex::scoped_lock lm (_mutex);

	if (_image) {
		return Result (_image, 0);
	}

	uint8_t* avio_buffer = static_cast<uint8_t*> (wrapped_av_malloc(4096));
	AVIOContext* avio_context = avio_alloc_context (avio_buffer, 4096, 0, const_cast<FFmpegImageProxy*>(this), avio_read_wrapper, 0, avio_seek_wrapper);
	AVFormatContext* format_context = avformat_alloc_context ();
	format_context->pb = avio_context;

	AVDictionary* options = 0;
	/* These durations are in microseconds, and represent how far into the content file
	   we will look for streams.
	*/
	av_dict_set (&options, "analyzeduration", raw_convert<string>(5 * 60 * 1000000).c_str(), 0);
	av_dict_set (&options, "probesize", raw_convert<string>(5 * 60 * 1000000).c_str(), 0);

	int e = avformat_open_input (&format_context, 0, 0, &options);
	if ((e < 0 && e == AVERROR_INVALIDDATA) || (e >= 0 && format_context->probe_score <= 25)) {
		/* Hack to fix loading of .tga files through AVIOContexts (rather then
		   directly from the file).  This code just does enough to allow the
		   probe code to take a hint from "foo.tga" and so try targa format.
		*/
		AVInputFormat* f = av_find_input_format ("image2");
		format_context = avformat_alloc_context ();
		format_context->pb = avio_context;
		format_context->iformat = f;
		e = avformat_open_input (&format_context, "foo.tga", f, &options);
	}
	if (e < 0) {
		if (_path) {
			throw OpenFileError (_path->string(), e, OpenFileError::READ);
		} else {
			boost::throw_exception(DecodeError(String::compose(_("Could not decode image (%1)"), e)));
		}
	}

	if (avformat_find_stream_info(format_context, 0) < 0) {
		throw DecodeError (_("could not find stream information"));
	}

	DCPOMATIC_ASSERT (format_context->nb_streams == 1);

	AVFrame* frame = av_frame_alloc ();
	if (!frame) {
		throw DecodeError (N_("could not allocate frame"));
	}

	AVCodecContext* codec_context = format_context->streams[0]->codec;
	AVCodec* codec = avcodec_find_decoder (codec_context->codec_id);
	DCPOMATIC_ASSERT (codec);

	if (avcodec_open2 (codec_context, codec, 0) < 0) {
		throw DecodeError (N_("could not open decoder"));
	}

	AVPacket packet;
	int r = av_read_frame (format_context, &packet);
	if (r < 0) {
		throw DecodeError (N_("could not read frame"));
	}

	int frame_finished;
	if (avcodec_decode_video2(codec_context, frame, &frame_finished, &packet) < 0 || !frame_finished) {
		throw DecodeError (N_("could not decode video"));
	}

	AVPixelFormat const pix_fmt = static_cast<AVPixelFormat>(frame->format);

	_image.reset (new Image(frame));
	if (_video_range == VIDEO_RANGE_VIDEO && av_pix_fmt_desc_get(pix_fmt)->flags & AV_PIX_FMT_FLAG_RGB) {
		/* Asking for the video range to be converted by libswscale (in Image) will not work for
		 * RGB sources since that method only processes video range in YUV and greyscale.  So we have
		 * to do it ourselves here.
		 */
		_image->video_range_to_full_range();
	}

	av_packet_unref (&packet);
	av_frame_free (&frame);
	avcodec_close (codec_context);
	avformat_close_input (&format_context);
	av_free (avio_context->buffer);
	av_free (avio_context);

	return Result (_image, 0);
}

DCPOMATIC_ENABLE_WARNINGS

void
FFmpegImageProxy::add_metadata (xmlpp::Node* node) const
{
	node->add_child("Type")->add_child_text (N_("FFmpeg"));
	node->add_child("VideoRange")->add_child_text(video_range_to_string(_video_range));
}

void
FFmpegImageProxy::write_to_socket (shared_ptr<Socket> socket) const
{
	socket->write (_data.size());
	socket->write (_data.data(), _data.size());
}

bool
FFmpegImageProxy::same (shared_ptr<const ImageProxy> other) const
{
	shared_ptr<const FFmpegImageProxy> mp = dynamic_pointer_cast<const FFmpegImageProxy> (other);
	if (!mp) {
		return false;
	}

	return _data == mp->_data;
}

size_t
FFmpegImageProxy::memory_used () const
{
	size_t m = _data.size();
	if (_image) {
		m += _image->memory_used();
	}
	return m;
}

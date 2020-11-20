/*
    Copyright (C) 2020 Carl Hetherington <cth@carlh.net>

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


/** @file  test/video_level_test.cc
 *  @brief Test that video level ranges are handled correctly.
 *  @ingroup feature
 */


#include "lib/content_factory.h"
#include "lib/content_video.h"
#include "lib/dcp_content.h"
#include "lib/film.h"
#include "lib/ffmpeg_content.h"
#include "lib/ffmpeg_decoder.h"
#include "lib/ffmpeg_image_proxy.h"
#include "lib/image.h"
#include "lib/image_content.h"
#include "lib/image_decoder.h"
#include "lib/ffmpeg_encoder.h"
#include "lib/job_manager.h"
#include "lib/transcode_job.h"
#include "lib/video_decoder.h"
#include "test.h"
#include <dcp/cpl.h>
#include <dcp/dcp.h>
#include <dcp/mono_picture_asset.h>
#include <dcp/mono_picture_frame.h>
#include <dcp/openjpeg_image.h>
#include <dcp/reel.h>
#include <dcp/reel_picture_asset.h>
#include <boost/test/unit_test.hpp>


using std::min;
using std::make_pair;
using std::max;
using std::pair;
using std::string;
using boost::dynamic_pointer_cast;
using boost::optional;
using boost::shared_ptr;


static
shared_ptr<Image>
grey_image (dcp::Size size, uint8_t pixel)
{
	shared_ptr<Image> grey(new Image(AV_PIX_FMT_RGB24, size, true));
	for (int y = 0; y < size.height; ++y) {
		uint8_t* p = grey->data()[0] + y * grey->stride()[0];
		for (int x = 0; x < size.width; ++x) {
			*p++ = pixel;
			*p++ = pixel;
			*p++ = pixel;
		}
	}

	return grey;
}


BOOST_AUTO_TEST_CASE (ffmpeg_image_full_range_not_changed)
{
	dcp::Size size(640, 480);
	uint8_t const grey_pixel = 128;
	boost::filesystem::path const file = "build/test/ffmpeg_image_full_range_not_changed.png";

	write_image (grey_image(size, grey_pixel), file);

	FFmpegImageProxy proxy (file, VIDEO_RANGE_FULL);
	ImageProxy::Result result = proxy.image ();
	BOOST_REQUIRE (!result.error);

	for (int y = 0; y < size.height; ++y) {
		uint8_t* p = result.image->data()[0] + y * result.image->stride()[0];
		for (int x = 0; x < size.width; ++x) {
			BOOST_REQUIRE (*p++ == grey_pixel);
		}
	}
}


BOOST_AUTO_TEST_CASE (ffmpeg_image_video_range_expanded)
{
	dcp::Size size(640, 480);
	uint8_t const grey_pixel = 128;
	uint8_t const expanded_grey_pixel = static_cast<uint8_t>((grey_pixel - 16) * 256.0 / 219);
	boost::filesystem::path const file = "build/test/ffmpeg_image_video_range_expanded.png";

	write_image (grey_image(size, grey_pixel), file);

	FFmpegImageProxy proxy (file, VIDEO_RANGE_VIDEO);
	ImageProxy::Result result = proxy.image ();
	BOOST_REQUIRE (!result.error);

	for (int y = 0; y < size.height; ++y) {
		uint8_t* p = result.image->data()[0] + y * result.image->stride()[0];
		for (int x = 0; x < size.width; ++x) {
			BOOST_REQUIRE_EQUAL (*p++, expanded_grey_pixel);
		}
	}
}


static optional<ContentVideo> content_video;


static
void
video_handler (ContentVideo cv)
{
	content_video = cv;
}


static
pair<int, int>
pixel_range (shared_ptr<const Image> image)
{
	pair<int, int> range(INT_MAX, 0);
	switch (image->pixel_format()) {
	case AV_PIX_FMT_RGB24:
	{
		dcp::Size const size = image->sample_size(0);
		for (int y = 0; y < size.height; ++y) {
			uint8_t* p = image->data()[0] + y * image->stride()[0];
			for (int x = 0; x < size.width * 3; ++x) {
				range.first = min(range.first, static_cast<int>(*p));
				range.second = max(range.second, static_cast<int>(*p));
				++p;
			}
		}
		break;
	}
	case AV_PIX_FMT_YUV444P:
	{
		for (int c = 0; c < 3; ++c) {
			dcp::Size const size = image->sample_size(c);
			for (int y = 0; y < size.height; ++y) {
				uint8_t* p = image->data()[c] + y * image->stride()[c];
				for (int x = 0; x < size.width; ++x) {
					range.first = min(range.first, static_cast<int>(*p));
					range.second = max(range.second, static_cast<int>(*p));
					++p;
				}
			}
		}
		break;
	}
	case AV_PIX_FMT_YUV422P10LE:
	case AV_PIX_FMT_YUV444P10LE:
	case AV_PIX_FMT_YUV444P12LE:
	{
		for (int c = 0; c < 3; ++c) {
			dcp::Size const size = image->sample_size(c);
			for (int y = 0; y < size.height; ++y) {
				uint16_t* p = reinterpret_cast<uint16_t*>(image->data()[c]) + y * image->stride()[c] / 2;
				for (int x = 0; x < size.width; ++x) {
					range.first = min(range.first, static_cast<int>(*p));
					range.second = max(range.second, static_cast<int>(*p));
					++p;
				}
			}
		}
		break;
	}
	default:
		BOOST_REQUIRE_MESSAGE (false, "No support for pixel format " << image->pixel_format());
	}

	return range;
}


static
pair<int, int>
pixel_range (shared_ptr<Film> film, shared_ptr<const FFmpegContent> content)
{
	shared_ptr<FFmpegDecoder> decoder(new FFmpegDecoder(film, content, false));
	decoder->video->Data.connect (bind(&video_handler, _1));
	content_video = boost::none;
	while (!content_video) {
		BOOST_REQUIRE (!decoder->pass());
	}

	return pixel_range (content_video->image->image().image);
}


static
pair<int, int>
pixel_range (shared_ptr<Film> film, shared_ptr<const ImageContent> content)
{
	shared_ptr<ImageDecoder> decoder(new ImageDecoder(film, content));
	decoder->video->Data.connect (bind(&video_handler, _1));
	content_video = boost::none;
	while (!content_video) {
		BOOST_REQUIRE (!decoder->pass());
	}

	return pixel_range (content_video->image->image().image);
}


static
pair<int, int>
pixel_range (boost::filesystem::path dcp_path)
{
	dcp::DCP dcp (dcp_path);
	dcp.read ();

	shared_ptr<dcp::MonoPictureAsset> picture = dynamic_pointer_cast<dcp::MonoPictureAsset>(dcp.cpls().front()->reels().front()->main_picture()->asset());
	BOOST_REQUIRE (picture);
	shared_ptr<dcp::OpenJPEGImage> frame = picture->start_read()->get_frame(0)->xyz_image();

	int const width = frame->size().width;
	int const height = frame->size().height;

	pair<int, int> range(INT_MAX, 0);
	for (int c = 0; c < 3; ++c) {
		for (int y = 0; y < height; ++y) {
			int* p = frame->data(c) + y * width;
			for (int x = 0; x < width; ++x) {
				range.first = min(range.first, *p);
				range.second = max(range.second, *p);
				++p;
			}
		}
	}

	return range;
}


/* Functions to make a Film with different sorts of content.
 *
 * In these names V = video range (limited)
 *                F = full range  (not limited)
 *                o = overridden
 */


static
shared_ptr<Film>
movie_V (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<FFmpegContent> content = dynamic_pointer_cast<FFmpegContent>(content_factory("test/data/rgb_grey_testcard.mp4").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 15);
	BOOST_CHECK_EQUAL (range.second, 243);

	return film;
}


static
shared_ptr<Film>
movie_VoF (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<FFmpegContent> content = dynamic_pointer_cast<FFmpegContent>(content_factory("test/data/rgb_grey_testcard.mp4").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());
	content->video->set_range (VIDEO_RANGE_FULL);

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 15);
	BOOST_CHECK_EQUAL (range.second, 243);

	return film;
}


static
shared_ptr<Film>
movie_F (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<FFmpegContent> content = dynamic_pointer_cast<FFmpegContent>(content_factory("test/data/rgb_grey_testcard.mov").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 1023);

	return film;
}


static
shared_ptr<Film>
movie_FoV (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<FFmpegContent> content = dynamic_pointer_cast<FFmpegContent>(content_factory("test/data/rgb_grey_testcard.mov").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());
	content->video->set_range (VIDEO_RANGE_VIDEO);

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 1023);

	return film;
}


static
shared_ptr<Film>
image_F (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<ImageContent> content = dynamic_pointer_cast<ImageContent>(content_factory("test/data/rgb_grey_testcard.png").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 255);

	return film;
}


static
shared_ptr<Film>
image_FoV (string name)
{
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<ImageContent> content = dynamic_pointer_cast<ImageContent>(content_factory("test/data/rgb_grey_testcard.png").front());
	BOOST_REQUIRE (content);
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());
	content->video->set_range (VIDEO_RANGE_VIDEO);

	pair<int, int> range = pixel_range (film, content);
	BOOST_CHECK_EQUAL (range.first, 11);
	BOOST_CHECK_EQUAL (range.second, 250);

	return film;
}


static
shared_ptr<Film>
dcp_F (string name)
{
	boost::filesystem::path const dcp = "test/data/RgbGreyTestcar_TST-1_F_MOS_2K_20201115_SMPTE_OV";
	shared_ptr<Film> film = new_test_film2 (name);
	shared_ptr<DCPContent> content(new DCPContent(dcp));
	film->examine_and_add_content (shared_ptr<DCPContent>(new DCPContent(dcp)));
	BOOST_REQUIRE (!wait_for_jobs());

	pair<int, int> range = pixel_range (dcp);
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 4081);

	return film;
}



/* Functions to get the pixel range in different sorts of output */


/** Get the pixel range in a DCP made from film */
static
pair<int, int>
dcp_range (shared_ptr<Film> film)
{
	film->make_dcp ();
	BOOST_REQUIRE (!wait_for_jobs());
	return pixel_range (film->dir(film->dcp_name()));
}


/** Get the pixel range in a video-range movie exported from film */
static
pair<int, int>
V_movie_range (shared_ptr<Film> film)
{
	shared_ptr<TranscodeJob> job (new TranscodeJob(film));
	job->set_encoder (
		shared_ptr<FFmpegEncoder>(
			new FFmpegEncoder (film, job, film->file("export.mov"), EXPORT_FORMAT_PRORES, true, false, false, 23)
			)
		);
	JobManager::instance()->add (job);
	BOOST_REQUIRE (!wait_for_jobs());

	/* This is a bit of a hack; add the exported file into the project so we can decode it */
	shared_ptr<FFmpegContent> content(new FFmpegContent(film->file("export.mov")));
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	return pixel_range (film, content);
}


/* The tests */


BOOST_AUTO_TEST_CASE (movie_V_to_dcp)
{
	pair<int, int> range = dcp_range (movie_V("movie_V_to_dcp"));
	/* Video range has been correctly expanded to full for the DCP */
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 4082);
}


BOOST_AUTO_TEST_CASE (movie_VoF_to_dcp)
{
	pair<int, int> range = dcp_range (movie_VoF("movie_VoF_to_dcp"));
	/* We said that video range data was really full range, so here we are in the DCP
	 * with video-range data.
	 */
	BOOST_CHECK_EQUAL (range.first, 350);
	BOOST_CHECK_EQUAL (range.second, 3832);
}


BOOST_AUTO_TEST_CASE (movie_F_to_dcp)
{
	pair<int, int> range = dcp_range (movie_F("movie_F_to_dcp"));
	/* The nearly-full-range of the input has been preserved */
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 4082);
}


BOOST_AUTO_TEST_CASE (video_FoV_to_dcp)
{
	pair<int, int> range = dcp_range (movie_FoV("video_FoV_to_dcp"));
	/* The nearly-full-range of the input has become even more full, and clipped */
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 4095);
}


BOOST_AUTO_TEST_CASE (image_F_to_dcp)
{
	pair<int, int> range = dcp_range (image_F("image_F_to_dcp"));
	BOOST_CHECK_EQUAL (range.first, 0);
	BOOST_CHECK_EQUAL (range.second, 4081);
}


BOOST_AUTO_TEST_CASE (image_FoV_to_dcp)
{
	pair<int, int> range = dcp_range (image_FoV("image_FoV_to_dcp"));
	BOOST_CHECK_EQUAL (range.first, 431);
	BOOST_CHECK_EQUAL (range.second, 4012);
}


BOOST_AUTO_TEST_CASE (movie_V_to_V_movie)
{
	pair<int, int> range = V_movie_range (movie_V("movie_V_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 60);
	BOOST_CHECK_EQUAL (range.second, 998);
}


BOOST_AUTO_TEST_CASE (movie_VoF_to_V_movie)
{
	pair<int, int> range = V_movie_range (movie_VoF("movie_VoF_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 116);
	BOOST_CHECK_EQUAL (range.second, 939);
}


BOOST_AUTO_TEST_CASE (movie_F_to_V_movie)
{
	pair<int, int> range = V_movie_range (movie_F("movie_F_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 4);
	BOOST_CHECK_EQUAL (range.second, 1019);
}


BOOST_AUTO_TEST_CASE (movie_FoV_to_V_movie)
{
	pair<int, int> range = V_movie_range (movie_FoV("movie_FoV_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 4);
	BOOST_CHECK_EQUAL (range.second, 1019);
}


BOOST_AUTO_TEST_CASE (image_F_to_V_movie)
{
	pair<int, int> range = V_movie_range (image_F("image_F_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 64);
	BOOST_CHECK_EQUAL (range.second, 960);
}


BOOST_AUTO_TEST_CASE (image_FoV_to_V_movie)
{
	pair<int, int> range = V_movie_range (image_FoV("image_FoV_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 102);
	BOOST_CHECK_EQUAL (range.second, 923);
}


BOOST_AUTO_TEST_CASE (dcp_F_to_V_movie)
{
	pair<int, int> range = V_movie_range (dcp_F("dcp_F_to_V_movie"));
	BOOST_CHECK_EQUAL (range.first, 64);
	BOOST_CHECK_EQUAL (range.second, 944);
}


/*
    Copyright (C) 2022 Carl Hetherington <cth@carlh.net>

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


#include "lib/audio_content.h"
#include "lib/content_factory.h"
#include "lib/maths_util.h"
#include "test.h"
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE (audio_content_fade_empty_region)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_empty_region", { content });

	BOOST_CHECK (content->audio->fade(0, 0, 48000).empty());
}


BOOST_AUTO_TEST_CASE (audio_content_fade_no_fade)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_no_fade", { content });

	BOOST_CHECK (content->audio->fade(0, 2000, 48000).empty());
	BOOST_CHECK (content->audio->fade(9999, 451, 48000).empty());
	BOOST_CHECK (content->audio->fade(content->audio->stream()->length() + 100, 8000, 48000).empty());
}


BOOST_AUTO_TEST_CASE (audio_content_fade_unfaded_part)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_unfaded_part", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(2000, 48000));

	BOOST_CHECK (content->audio->fade(2000, 50, 48000).empty());
	BOOST_CHECK (content->audio->fade(12000, 99, 48000).empty());
	BOOST_CHECK (content->audio->fade(content->audio->stream()->length() - 2051, 50, 48000).empty());
}


BOOST_AUTO_TEST_CASE (audio_content_within_the_fade_in)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_within_the_fade_in", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));

	auto const f1 = content->audio->fade(0, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000U);
	for (auto i = 0; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], logarithmic_fade_in_curve(static_cast<float>(i) / 2000), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_within_the_fade_out)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_within_the_fade_out", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(2000, 48000));

	auto const f1 = content->audio->fade(content->audio->stream()->length() - 2000, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000U);
	for (auto i = 0; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], logarithmic_fade_out_curve(static_cast<float>(i) / 2000), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_overlapping_the_fade_in)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_overlapping_the_fade_in", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(2000, 48000));

	auto const f1 = content->audio->fade(1500, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000U);
	for (auto i = 0; i < 500; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], logarithmic_fade_in_curve(static_cast<float>(i + 1500) / 2000), 0.01);
	}
	for (auto i = 500; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], 1.0f, 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_overlapping_the_fade_out)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_overlapping_the_fade_out", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(4000, 48000));

	auto const f1 = content->audio->fade(content->audio->stream()->length() - 4100, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000U);
	for (auto i = 0; i < 100; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], 1.0f, 0.01);
	}
	for (auto i = 100; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], logarithmic_fade_out_curve(static_cast<float>(i - 100) / 4000), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_fade_in_and_out)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_in_and_out", { content });

	auto const length = content->audio->stream()->length();

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(length, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(length, 48000));

	auto const f1 = content->audio->fade(0, 10000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 10000U);
	for (auto i = 0; i < 10000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], logarithmic_fade_in_curve(static_cast<float>(i) / length) * logarithmic_fade_out_curve(static_cast<float>(i) / length), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_fade_in_with_trim)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_in_with_trim", { content });

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(1000, 48000));
	content->set_trim_start(dcpomatic::ContentTime::from_frames(5200, 48000));

	/* In the trim */
	auto const f1 = content->audio->fade(0, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000);
	for (auto i = 0; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], 0.0f, 0.01);
	}

	/* In the fade */
	auto const f2 = content->audio->fade(5200, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f2.size(), 2000);
	for (auto i = 0; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f2[i], logarithmic_fade_in_curve(static_cast<float>(i) / 2000), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_fade_out_with_trim)
{
	auto content = content_factory("test/data/impulse_train.wav").front();
	auto film = new_test_film2("audio_content_fade_out_with_trim", { content });

	auto const length = content->audio->stream()->length();

	content->audio->set_fade_in(dcpomatic::ContentTime::from_frames(2000, 48000));
	content->audio->set_fade_out(dcpomatic::ContentTime::from_frames(1000, 48000));
	content->set_trim_start(dcpomatic::ContentTime::from_frames(5200, 48000));
	content->set_trim_end(dcpomatic::ContentTime::from_frames(9000, 48000));

	/* In the trim */
	auto const f1 = content->audio->fade(length - 6000, 2000, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 2000);
	for (auto i = 0; i < 2000; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], 0.0f, 0.01);
	}

	/* In the fade */
	auto const f2 = content->audio->fade(length - 9000 - 1000, 1000, 48000);
	BOOST_REQUIRE_EQUAL (f2.size(), 1000);
	for (auto i = 0; i < 1000; ++i) {
		BOOST_REQUIRE_CLOSE (f2[i], logarithmic_fade_out_curve(static_cast<float>(i) / 1000), 0.01);
	}
}


BOOST_AUTO_TEST_CASE (audio_content_fade_out_with_trim_at_44k1)
{
	/* 5s at 44.1kHz */
	auto content = content_factory("test/data/white.wav").front();
	auto film = new_test_film2("audio_content_fade_out_with_trim_at_44k1", { content });

	/* /----- 3.5s ------|-Fade-|-Trim-\
	 * |                 |  1s  | 0.5s |
	 * \-----------------|------|------/
	 */

	content->audio->set_fade_out(dcpomatic::ContentTime::from_seconds(1));
	content->set_trim_end(dcpomatic::ContentTime::from_seconds(0.5));

	/* In the trim */
	auto const f1 = content->audio->fade(std::round(48000 * 4.75), 200, 48000);
	BOOST_REQUIRE_EQUAL (f1.size(), 200);
	for (auto i = 0; i < 200; ++i) {
		BOOST_REQUIRE_CLOSE (f1[i], 0.0f, 0.01);
	}

	/* In the fade */
	auto const f2 = content->audio->fade(std::round(48000 * 3.5 + 200), 7000, 48000);
	BOOST_REQUIRE_EQUAL (f2.size(), 7000);
	for (auto i = 0; i < 7000; ++i) {
		BOOST_REQUIRE_CLOSE (f2[i], logarithmic_fade_out_curve(static_cast<float>(i + 200) / 48000), 0.01);
	}

}


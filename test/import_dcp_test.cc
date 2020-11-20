/*
    Copyright (C) 2014-2020 Carl Hetherington <cth@carlh.net>

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

/** @file  test/import_dcp_test.cc
 *  @brief Test import of encrypted DCPs.
 *  @ingroup feature
 */

#include "test.h"
#include "lib/film.h"
#include "lib/screen.h"
#include "lib/dcp_subtitle_content.h"
#include "lib/ratio.h"
#include "lib/dcp_content_type.h"
#include "lib/dcp_content.h"
#include "lib/ffmpeg_content.h"
#include "lib/examine_content_job.h"
#include "lib/job_manager.h"
#include "lib/config.h"
#include "lib/cross.h"
#include "lib/video_content.h"
#include "lib/content_factory.h"
#include <dcp/cpl.h>
#include <boost/test/unit_test.hpp>

using std::vector;
using std::string;
using std::map;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;

/** Make an encrypted DCP, import it and make a new unencrypted DCP */
BOOST_AUTO_TEST_CASE (import_dcp_test)
{
	shared_ptr<Film> A = new_test_film ("import_dcp_test");
	A->set_container (Ratio::from_id ("185"));
	A->set_dcp_content_type (DCPContentType::from_isdcf_name ("TLR"));
	A->set_name ("frobozz");
	A->set_interop (false);

	shared_ptr<FFmpegContent> c (new FFmpegContent("test/data/test.mp4"));
	A->examine_and_add_content (c);
	A->set_encrypted (true);
	BOOST_CHECK (!wait_for_jobs ());

	A->make_dcp ();
	BOOST_CHECK (!wait_for_jobs ());

	dcp::DCP A_dcp ("build/test/import_dcp_test/" + A->dcp_name());
	A_dcp.read ();

	Config::instance()->set_decryption_chain (shared_ptr<dcp::CertificateChain> (new dcp::CertificateChain (openssl_path ())));

	/* Dear future-carl: I suck!  I thought you wouldn't still be running these tests in 2030!  Sorry! */
	dcp::EncryptedKDM kdm = A->make_kdm (
		Config::instance()->decryption_chain()->leaf (),
		vector<string>(),
		A_dcp.cpls().front()->file().get(),
		dcp::LocalTime ("2030-07-21T00:00:00+00:00"),
		dcp::LocalTime ("2031-07-21T00:00:00+00:00"),
		dcp::MODIFIED_TRANSITIONAL_1,
		true, 0
		);

	shared_ptr<Film> B = new_test_film ("import_dcp_test2");
	B->set_container (Ratio::from_id ("185"));
	B->set_dcp_content_type (DCPContentType::from_isdcf_name ("TLR"));
	B->set_name ("frobozz");
	B->set_interop (false);

	shared_ptr<DCPContent> d (new DCPContent("build/test/import_dcp_test/" + A->dcp_name()));
	B->examine_and_add_content (d);
	BOOST_CHECK (!wait_for_jobs ());
	d->add_kdm (kdm);
	JobManager::instance()->add (shared_ptr<Job> (new ExamineContentJob (B, d)));
	BOOST_CHECK (!wait_for_jobs ());

	B->make_dcp ();
	BOOST_CHECK (!wait_for_jobs ());

	/* Should be 1s red, 1s green, 1s blue */
	check_dcp ("test/data/import_dcp_test2", "build/test/import_dcp_test2/" + B->dcp_name());
}


/** Check that DCP markers are imported correctly */
BOOST_AUTO_TEST_CASE (import_dcp_markers_test)
{
	/* Make a DCP with some markers */
	shared_ptr<Film> film = new_test_film2 ("import_dcp_markers_test");
	shared_ptr<Content> content = content_factory("test/data/flat_red.png").front();
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	content->video->set_length (24 * 60 * 10);

	film->set_marker(dcp::FFOC, dcpomatic::DCPTime::from_seconds(1.91));
	film->set_marker(dcp::FFMC, dcpomatic::DCPTime::from_seconds(9.4));
	film->set_marker(dcp::LFMC, dcpomatic::DCPTime::from_seconds(9.99));

	film->make_dcp ();
	BOOST_REQUIRE (!wait_for_jobs());

	/* Import the DCP to a new film and check the markers */
	shared_ptr<Film> film2 = new_test_film2 ("import_dcp_markers_test2");
	shared_ptr<DCPContent> imported (new DCPContent(film->dir(film->dcp_name())));
	film2->examine_and_add_content (imported);
	BOOST_REQUIRE (!wait_for_jobs());
	film2->write_metadata ();

	BOOST_CHECK_EQUAL (imported->markers().size(), 3U);

	map<dcp::Marker, dcpomatic::ContentTime> markers = imported->markers();
	BOOST_REQUIRE(markers.find(dcp::FFOC) != markers.end());
	BOOST_CHECK(markers[dcp::FFOC] == dcpomatic::ContentTime(184000));
	BOOST_REQUIRE(markers.find(dcp::FFMC) != markers.end());
	BOOST_CHECK(markers[dcp::FFMC] == dcpomatic::ContentTime(904000));
	BOOST_REQUIRE(markers.find(dcp::LFMC) != markers.end());
	BOOST_CHECK(markers[dcp::LFMC] == dcpomatic::ContentTime(960000));

	/* Load that film and check that the markers have been loaded */
	shared_ptr<Film> film3(new Film(boost::filesystem::path("build/test/import_dcp_markers_test2")));
	film3->read_metadata ();
	BOOST_REQUIRE_EQUAL (film3->content().size(), 1U);
	shared_ptr<DCPContent> reloaded = dynamic_pointer_cast<DCPContent>(film3->content().front());
	BOOST_REQUIRE (reloaded);

	BOOST_CHECK_EQUAL (reloaded->markers().size(), 3U);

	markers = reloaded->markers();
	BOOST_REQUIRE(markers.find(dcp::FFOC) != markers.end());
	BOOST_CHECK(markers[dcp::FFOC] == dcpomatic::ContentTime(184000));
	BOOST_REQUIRE(markers.find(dcp::FFMC) != markers.end());
	BOOST_CHECK(markers[dcp::FFMC] == dcpomatic::ContentTime(904000));
	BOOST_REQUIRE(markers.find(dcp::LFMC) != markers.end());
	BOOST_CHECK(markers[dcp::LFMC] == dcpomatic::ContentTime(960000));
}


/** Check that DCP metadata (ratings and content version) are imported correctly */
BOOST_AUTO_TEST_CASE (import_dcp_metadata_test)
{
	/* Make a DCP with some ratings and a content version */
	shared_ptr<Film> film = new_test_film2 ("import_dcp_metadata_test");
	shared_ptr<Content> content = content_factory("test/data/flat_red.png").front();
	film->examine_and_add_content (content);
	BOOST_REQUIRE (!wait_for_jobs());

	content->video->set_length (10);

	std::vector<dcp::Rating> ratings;
	ratings.push_back (dcp::Rating("BBFC", "15"));
	ratings.push_back (dcp::Rating("MPAA", "NC-17"));
	film->set_ratings (ratings);

	vector<string> cv;
	cv.push_back ("Fred");
	film->set_content_versions (cv);

	film->make_dcp ();
	BOOST_REQUIRE (!wait_for_jobs());

	/* Import the DCP to a new film and check the metadata */
	shared_ptr<Film> film2 = new_test_film2 ("import_dcp_metadata_test2");
	shared_ptr<DCPContent> imported (new DCPContent(film->dir(film->dcp_name())));
	film2->examine_and_add_content (imported);
	BOOST_REQUIRE (!wait_for_jobs());
	film2->write_metadata ();

	BOOST_CHECK (imported->ratings() == ratings);
	BOOST_CHECK (imported->content_versions() == cv);

	/* Load that film and check that the metadata has been loaded */
	shared_ptr<Film> film3(new Film(boost::filesystem::path("build/test/import_dcp_metadata_test2")));
	film3->read_metadata ();
	BOOST_REQUIRE_EQUAL (film3->content().size(), 1U);
	shared_ptr<DCPContent> reloaded = dynamic_pointer_cast<DCPContent>(film3->content().front());
	BOOST_REQUIRE (reloaded);

	BOOST_CHECK (reloaded->ratings() == ratings);
	BOOST_CHECK (reloaded->content_versions() == cv);
}


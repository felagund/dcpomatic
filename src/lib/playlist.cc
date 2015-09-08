/*
    Copyright (C) 2013-2015 Carl Hetherington <cth@carlh.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include "playlist.h"
#include "sndfile_content.h"
#include "sndfile_decoder.h"
#include "video_content.h"
#include "ffmpeg_decoder.h"
#include "ffmpeg_content.h"
#include "image_decoder.h"
#include "content_factory.h"
#include "job.h"
#include "config.h"
#include "util.h"
#include "md5_digester.h"
#include <libcxml/cxml.h>
#include <libxml++/libxml++.h>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>

#include "i18n.h"

using std::list;
using std::cout;
using std::vector;
using std::min;
using std::max;
using std::string;
using std::pair;
using boost::optional;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::dynamic_pointer_cast;

Playlist::Playlist ()
	: _sequence_video (true)
	, _sequencing_video (false)
{

}

Playlist::~Playlist ()
{
	_content.clear ();
	reconnect ();
}

void
Playlist::content_changed (weak_ptr<Content> content, int property, bool frequent)
{
	/* Don't respond to position changes here, as:
	   - sequencing after earlier/later changes is handled by move_earlier/move_later
	   - any other position changes will be timeline drags which should not result in content
	   being sequenced.
	*/

	if (property == ContentProperty::LENGTH || property == VideoContentProperty::VIDEO_FRAME_TYPE) {
		maybe_sequence_video ();
	}

	ContentChanged (content, property, frequent);
}

void
Playlist::maybe_sequence_video ()
{
	if (!_sequence_video || _sequencing_video) {
		return;
	}

	_sequencing_video = true;

	DCPTime next_left;
	DCPTime next_right;
	BOOST_FOREACH (shared_ptr<Content> i, _content) {
		shared_ptr<VideoContent> vc = dynamic_pointer_cast<VideoContent> (i);
		if (!vc) {
			continue;
		}

		if (vc->video_frame_type() == VIDEO_FRAME_TYPE_3D_RIGHT) {
			vc->set_position (next_right);
			next_right = vc->end() + DCPTime::delta ();
		} else {
			vc->set_position (next_left);
			next_left = vc->end() + DCPTime::delta ();
		}
	}

	/* This won't change order, so it does not need a sort */

	_sequencing_video = false;
}

string
Playlist::video_identifier () const
{
	string t;

	BOOST_FOREACH (shared_ptr<const Content> i, _content) {
		shared_ptr<const VideoContent> vc = dynamic_pointer_cast<const VideoContent> (i);
		shared_ptr<const SubtitleContent> sc = dynamic_pointer_cast<const SubtitleContent> (i);
		if (vc) {
			t += vc->identifier ();
		} else if (sc && sc->burn_subtitles ()) {
			t += sc->identifier ();
		}
	}

	MD5Digester digester;
	digester.add (t.c_str(), t.length());
	return digester.get ();
}

/** @param node <Playlist> node */
void
Playlist::set_from_xml (shared_ptr<const Film> film, cxml::ConstNodePtr node, int version, list<string>& notes)
{
	BOOST_FOREACH (cxml::NodePtr i, node->node_children ("Content")) {
		_content.push_back (content_factory (film, i, version, notes));
	}

	sort (_content.begin(), _content.end(), ContentSorter ());

	reconnect ();
}

/** @param node <Playlist> node */
void
Playlist::as_xml (xmlpp::Node* node)
{
	BOOST_FOREACH (shared_ptr<Content> i, _content) {
		i->as_xml (node->add_child ("Content"));
	}
}

void
Playlist::add (shared_ptr<Content> c)
{
	_content.push_back (c);
	sort (_content.begin(), _content.end(), ContentSorter ());
	reconnect ();
	Changed ();
}

void
Playlist::remove (shared_ptr<Content> c)
{
	ContentList::iterator i = _content.begin ();
	while (i != _content.end() && *i != c) {
		++i;
	}

	if (i != _content.end ()) {
		_content.erase (i);
		Changed ();
	}

	/* This won't change order, so it does not need a sort */
}

void
Playlist::remove (ContentList c)
{
	BOOST_FOREACH (shared_ptr<Content> i, c) {
		ContentList::iterator j = _content.begin ();
		while (j != _content.end() && *j != i) {
			++j;
		}

		if (j != _content.end ()) {
			_content.erase (j);
		}
	}

	/* This won't change order, so it does not need a sort */

	Changed ();
}

class FrameRateCandidate
{
public:
	FrameRateCandidate (float source_, int dcp_)
		: source (source_)
		, dcp (dcp_)
	{}

	float source;
	int dcp;
};

int
Playlist::best_dcp_frame_rate () const
{
	list<int> const allowed_dcp_frame_rates = Config::instance()->allowed_dcp_frame_rates ();

	/* Work out what rates we could manage, including those achieved by using skip / repeat. */
	list<FrameRateCandidate> candidates;

	/* Start with the ones without skip / repeat so they will get matched in preference to skipped/repeated ones */
	for (list<int>::const_iterator i = allowed_dcp_frame_rates.begin(); i != allowed_dcp_frame_rates.end(); ++i) {
		candidates.push_back (FrameRateCandidate (*i, *i));
	}

	/* Then the skip/repeat ones */
	for (list<int>::const_iterator i = allowed_dcp_frame_rates.begin(); i != allowed_dcp_frame_rates.end(); ++i) {
		candidates.push_back (FrameRateCandidate (float (*i) / 2, *i));
		candidates.push_back (FrameRateCandidate (float (*i) * 2, *i));
	}

	/* Pick the best one */
	float error = std::numeric_limits<float>::max ();
	optional<FrameRateCandidate> best;
	list<FrameRateCandidate>::iterator i = candidates.begin();
	while (i != candidates.end()) {

		float this_error = 0;
		BOOST_FOREACH (shared_ptr<Content> j, _content) {
			shared_ptr<VideoContent> vc = dynamic_pointer_cast<VideoContent> (j);
			if (!vc) {
				continue;
			}

			/* Best error for this content; we could use the content as-is or double its rate */
			float best_error = min (
				float (fabs (i->source - vc->video_frame_rate ())),
				float (fabs (i->source - vc->video_frame_rate () * 2))
				);

			/* Use the largest difference between DCP and source as the "error" */
			this_error = max (this_error, best_error);
		}

		if (this_error < error) {
			error = this_error;
			best = *i;
		}

		++i;
	}

	if (!best) {
		return 24;
	}

	return best->dcp;
}

/** @return length of the playlist from time 0 to the last thing on the playlist */
DCPTime
Playlist::length () const
{
	DCPTime len;
	BOOST_FOREACH (shared_ptr<const Content> i, _content) {
		len = max (len, i->end());
	}

	return len;
}

/** @return position of the first thing on the playlist, if it's not empty */
optional<DCPTime>
Playlist::start () const
{
	if (_content.empty ()) {
		return optional<DCPTime> ();
	}

	DCPTime start = DCPTime::max ();
	BOOST_FOREACH (shared_ptr<Content> i, _content) {
		start = min (start, i->position ());
	}

	return start;
}

void
Playlist::reconnect ()
{
	for (list<boost::signals2::connection>::iterator i = _content_connections.begin(); i != _content_connections.end(); ++i) {
		i->disconnect ();
	}

	_content_connections.clear ();

	BOOST_FOREACH (shared_ptr<Content> i, _content) {
		_content_connections.push_back (i->Changed.connect (bind (&Playlist::content_changed, this, _1, _2, _3)));
	}
}

DCPTime
Playlist::video_end () const
{
	DCPTime end;
	BOOST_FOREACH (shared_ptr<Content> i, _content) {
		if (dynamic_pointer_cast<const VideoContent> (i)) {
			end = max (end, i->end ());
		}
	}

	return end;
}

FrameRateChange
Playlist::active_frame_rate_change (DCPTime t, int dcp_video_frame_rate) const
{
	for (ContentList::const_reverse_iterator i = _content.rbegin(); i != _content.rend(); ++i) {
		shared_ptr<const VideoContent> vc = dynamic_pointer_cast<const VideoContent> (*i);
		if (!vc) {
			continue;
		}

		if (vc->position() <= t) {
			/* This is the first piece of content (going backwards...) that starts before t,
			   so it's the active one.
			*/
			return FrameRateChange (vc->video_frame_rate(), dcp_video_frame_rate);
		}
	}

	return FrameRateChange (dcp_video_frame_rate, dcp_video_frame_rate);
}

void
Playlist::set_sequence_video (bool s)
{
	_sequence_video = s;
}

bool
ContentSorter::operator() (shared_ptr<Content> a, shared_ptr<Content> b)
{
	return a->position() < b->position();
}

/** @return content in an undefined order */
ContentList
Playlist::content () const
{
	return _content;
}

void
Playlist::repeat (ContentList c, int n)
{
	pair<DCPTime, DCPTime> range (DCPTime::max (), DCPTime ());
	BOOST_FOREACH (shared_ptr<Content> i, c) {
		range.first = min (range.first, i->position ());
		range.second = max (range.second, i->position ());
		range.first = min (range.first, i->end ());
		range.second = max (range.second, i->end ());
	}

	DCPTime pos = range.second;
	for (int i = 0; i < n; ++i) {
		BOOST_FOREACH (shared_ptr<Content> j, c) {
			shared_ptr<Content> copy = j->clone ();
			copy->set_position (pos + copy->position() - range.first);
			_content.push_back (copy);
		}
		pos += range.second - range.first;
	}

	sort (_content.begin(), _content.end(), ContentSorter ());

	reconnect ();
	Changed ();
}

void
Playlist::move_earlier (shared_ptr<Content> c)
{
	sort (_content.begin(), _content.end(), ContentSorter ());

	ContentList::iterator previous = _content.end ();
	ContentList::iterator i = _content.begin();
	while (i != _content.end() && *i != c) {
		previous = i;
		++i;
	}

	DCPOMATIC_ASSERT (i != _content.end ());
	if (previous == _content.end ()) {
		return;
	}


	DCPTime const p = (*previous)->position ();
	(*previous)->set_position (p + c->length_after_trim ());
	c->set_position (p);
	sort (_content.begin(), _content.end(), ContentSorter ());
}

void
Playlist::move_later (shared_ptr<Content> c)
{
	sort (_content.begin(), _content.end(), ContentSorter ());

	ContentList::iterator i = _content.begin();
	while (i != _content.end() && *i != c) {
		++i;
	}

	DCPOMATIC_ASSERT (i != _content.end ());

	ContentList::iterator next = i;
	++next;

	if (next == _content.end ()) {
		return;
	}

	(*next)->set_position (c->position ());
	c->set_position (c->position() + (*next)->length_after_trim ());
	sort (_content.begin(), _content.end(), ContentSorter ());
}

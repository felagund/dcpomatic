/*
    Copyright (C) 2019 Carl Hetherington <cth@carlh.net>

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

#ifndef DCPOMATIC_VIDEO_VIEW_H
#define DCPOMATIC_VIDEO_VIEW_H

#include "lib/dcpomatic_time.h"
#include <boost/shared_ptr.hpp>
#include <boost/signals2.hpp>
#include <boost/thread.hpp>

class Image;
class wxWindow;
class FilmViewer;
class PlayerVideo;

class VideoView
{
public:
	VideoView (FilmViewer* viewer)
		: _viewer (viewer)
#ifdef DCPOMATIC_VARIANT_SWAROOP
		, _in_watermark (false)
#endif
		, _video_frame_rate (0)
	{}

	virtual ~VideoView () {}

	virtual void set_image (boost::shared_ptr<const Image> image) = 0;
	virtual wxWindow* get () const = 0;
	virtual void update () = 0;

	/* XXX_b: make pure */
	virtual void start () {}
	/* XXX_b: make pure */
	virtual void stop () {}

	void clear ();

	boost::signals2::signal<void()> Sized;

	virtual bool display_next_frame (bool) = 0;

	/* XXX_b: to remove */
	virtual void display_player_video () {}

	dcpomatic::DCPTime position () const {
		boost::mutex::scoped_lock lm (_mutex);
		return _player_video.second;
	}

	void set_video_frame_rate (int r) {
		boost::mutex::scoped_lock lm (_mutex);
		_video_frame_rate = r;
	}

	void set_length (dcpomatic::DCPTime len) {
		boost::mutex::scoped_lock lm (_mutex);
		_length = len;
	}

protected:
	/* XXX_b: to remove */
	friend class FilmViewer;

	bool get_next_frame (bool non_blocking);
	int time_until_next_frame () const;
	dcpomatic::DCPTime one_video_frame () const;

	int video_frame_rate () const {
		boost::mutex::scoped_lock lm (_mutex);
		return _video_frame_rate;
	}

	dcpomatic::DCPTime length () const {
		boost::mutex::scoped_lock lm (_mutex);
		return _length;
	}

	std::pair<boost::shared_ptr<PlayerVideo>, dcpomatic::DCPTime> player_video () const {
		boost::mutex::scoped_lock lm (_mutex);
		return _player_video;
	}

	FilmViewer* _viewer;

#ifdef DCPOMATIC_VARIANT_SWAROOP
	bool _in_watermark;
	int _watermark_x;
	int _watermark_y;
#endif

private:
	/** Mutex protecting all the state in VideoView */
	mutable boost::mutex _mutex;

	std::pair<boost::shared_ptr<PlayerVideo>, dcpomatic::DCPTime> _player_video;
	int _video_frame_rate;
	dcpomatic::DCPTime _length;
};

#endif

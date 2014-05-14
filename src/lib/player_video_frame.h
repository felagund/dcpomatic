/*
    Copyright (C) 2013-2014 Carl Hetherington <cth@carlh.net>

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

#include <boost/shared_ptr.hpp>
#include "types.h"
#include "position.h"
#include "colour_conversion.h"

class Image;
class Scaler;

/** Everything needed to describe a video frame coming out of the player, but with the
 *  bits still their raw form.  We may want to combine the bits on a remote machine,
 *  or maybe not even bother to combine them at all.
 */
class PlayerVideoFrame
{
public:
	PlayerVideoFrame (boost::shared_ptr<const Image>, Crop, libdcp::Size, libdcp::Size, Scaler const *, Eyes, ColourConversion);

	void set_subtitle (boost::shared_ptr<const Image>, Position<int>);
	
	boost::shared_ptr<Image> image ();

	Eyes eyes () const {
		return _eyes;
	}

	ColourConversion colour_conversion () const {
		return _colour_conversion;
	}

private:
	boost::shared_ptr<const Image> _in;
	Crop _crop;
	libdcp::Size _inter_size;
	libdcp::Size _out_size;
	Scaler const * _scaler;
	Eyes _eyes;
	ColourConversion _colour_conversion;
	boost::shared_ptr<const Image> _subtitle_image;
	Position<int> _subtitle_position;
};

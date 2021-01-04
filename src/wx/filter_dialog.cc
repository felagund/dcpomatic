/*
    Copyright (C) 2012 Carl Hetherington <cth@carlh.net>

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

/** @file  src/filter_dialog.cc
 *  @brief A dialog to select FFmpeg filters.
 */

#include "check_box.h"
#include "filter_dialog.h"
#include "static_text.h"
#include "wx_util.h"
#include "lib/film.h"
#include "lib/filter.h"


using namespace std;
using boost::bind;


FilterDialog::FilterDialog (wxWindow* parent, vector<Filter const *> const & active)
	: wxDialog (parent, wxID_ANY, wxString(_("Filters")))
{
	wxPanel* panel = new wxPanel (this);
	wxBoxSizer* sizer = new wxBoxSizer (wxVERTICAL);

	vector<Filter const *> filters = Filter::all ();

	typedef map<string, list<Filter const *> > CategoryMap;
	CategoryMap categories;

	for (auto i: filters) {
		CategoryMap::iterator j = categories.find (i->category());
		if (j == categories.end ()) {
			list<Filter const *> c;
			c.push_back (i);
			categories[i->category()] = c;
		} else {
			j->second.push_back (i);
		}
	}

	for (CategoryMap::iterator i = categories.begin(); i != categories.end(); ++i) {

		wxStaticText* c = new StaticText (panel, std_to_wx(i->first));
		wxFont font = c->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		c->SetFont(font);
		sizer->Add (c, 1, wxTOP | wxBOTTOM, DCPOMATIC_SIZER_GAP);

		for (auto j: i->second) {
			wxCheckBox* b = new CheckBox(panel, std_to_wx(j->name()));
			bool const a = find (active.begin(), active.end(), j) != active.end();
			b->SetValue (a);
			_filters[j] = b;
			b->Bind (wxEVT_CHECKBOX, boost::bind(&FilterDialog::filter_toggled, this));
			sizer->Add (b);
		}

		sizer->AddSpacer (6);
	}

	wxSizer* buttons = CreateSeparatedButtonSizer (wxOK);
	if (buttons) {
		sizer->Add (buttons, wxSizerFlags().Expand().DoubleBorder());
	}

	panel->SetSizer (sizer);

	wxBoxSizer* overall_sizer = new wxBoxSizer (wxVERTICAL);
	overall_sizer->Add (panel, 1, wxTOP | wxLEFT | wxRIGHT, DCPOMATIC_SIZER_GAP);
	SetSizerAndFit (overall_sizer);
}


void
FilterDialog::filter_toggled ()
{
	ActiveChanged (active());
}


vector<Filter const*>
FilterDialog::active () const
{
	vector<Filter const *> active;
	for (map<Filter const *, wxCheckBox*>::const_iterator i = _filters.begin(); i != _filters.end(); ++i) {
		if (i->second->IsChecked()) {
			active.push_back(i->first);
		}
	}

	return active;
}


//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2016 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include <cursespp/Colors.h>
#include <cursespp/Screen.h>
#include <cursespp/Text.h>
#include <core/library/LocalLibraryConstants.h>
#include <glue/query/NowPlayingTrackListQuery.h>
#include <glue/util/Duration.h>
#include "NowPlayingLayout.h"

#include <set>
#include <boost/format.hpp>

using namespace musik::core::library::constants;

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::library;
using namespace musik::box;
using namespace musik::glue;
using namespace cursespp;

static std::set<std::string> EDIT_KEYS = { "M-up", "M-down", "KEY_BACKSPACE", "KEY_DC" };

static std::string formatWithAlbum(TrackPtr track, size_t width);

NowPlayingLayout::NowPlayingLayout(
    musik::core::audio::PlaybackService& playback,
    musik::core::LibraryPtr library)
: LayoutBase()
, playback(playback)
, library(library)
, reselectIndex(-1) {
    this->InitializeWindows();
    this->playback.Shuffled.connect(this, &NowPlayingLayout::OnPlaybackShuffled);
}

NowPlayingLayout::~NowPlayingLayout() {

}

void NowPlayingLayout::OnLayout() {
    this->trackList->MoveAndResize(
        0,
        0,
        this->GetWidth(),
        this->GetHeight());

    this->trackList->SetFocusOrder(1);
}

void NowPlayingLayout::InitializeWindows() {
    this->trackList.reset(new TrackListView(
        this->playback,
        this->library,
        std::bind(formatWithAlbum, std::placeholders::_1, std::placeholders::_2)));

    this->trackList->Requeried.connect(this, &NowPlayingLayout::OnTrackListRequeried);
    this->AddWindow(this->trackList);
}

void NowPlayingLayout::OnVisibilityChanged(bool visible) {
    LayoutBase::OnVisibilityChanged(visible);

    if (visible) {
        this->RequeryTrackList();
    }
    else {
        this->trackList->Clear();
    }
}

void NowPlayingLayout::OnTrackListRequeried() {
    if (playback.Count()) {
        if (this->reselectIndex == -1) {
            size_t index = playback.GetIndex();
            this->trackList->SetSelectedIndex(index == (size_t) -1 ? 0 : index);
            this->trackList->ScrollTo(index == 0 ? index : index - 1);
        }
        else {
            this->trackList->SetSelectedIndex((int) this->reselectIndex);
            auto pos = this->trackList->GetScrollPosition();
            int first = (int) pos.firstVisibleEntryIndex;
            int last = (int) first + pos.visibleEntryCount;
            int index = (int) this->reselectIndex;
            if (index < first || index >= last) {
                this->trackList->ScrollTo(this->reselectIndex);
            }
        }

        if (this->trackList->GetSelectedIndex() == (size_t)-1) {
            this->trackList->SetSelectedIndex(0);
        }
    }

    this->reselectIndex = -1;
}

void NowPlayingLayout::OnPlaybackShuffled(bool shuffled) {
    this->RequeryTrackList();
}

void NowPlayingLayout::RequeryTrackList() {
    this->trackList->Requery(std::shared_ptr<TrackListQueryBase>(
        new NowPlayingTrackListQuery(this->library, this->playback)));
}

bool NowPlayingLayout::KeyPress(const std::string& key) {
    if (key == "KEY_ENTER") {
        this->playback.Play(this->trackList->GetSelectedIndex());
        return true;
    }
    else if (ProcessEditOperation(key)) {
        return true;
    }

    return LayoutBase::KeyPress(key);
}

bool NowPlayingLayout::ProcessEditOperation(const std::string& key) {
    if (EDIT_KEYS.find(key) != EDIT_KEYS.end()) {
        if (!playback.IsShuffled()) {
            PlaybackService::Editor editor = this->playback.Edit();
            size_t selected = this->trackList->GetSelectedIndex();

            if (key == "M-up") {
                if (selected > 0) {
                    size_t to = selected - 1;
                    editor.Move(selected, to);
                    this->reselectIndex = (int)to;
                }
            }
            else if (key == "M-down") {
                if (selected < this->playback.Count() - 1) {
                    size_t to = selected + 1;
                    editor.Move(selected, to);
                    this->reselectIndex = (int)to;
                }
            }
            else if (key == "KEY_BACKSPACE" || key == "KEY_DC") {
                editor.Delete(selected);
                this->reselectIndex = (int)selected;
            }

            this->RequeryTrackList();
            return true;
        }
    }

    return false;
}

#define TRACK_COL_WIDTH 3
#define ARTIST_COL_WIDTH 14
#define ALBUM_COL_WIDTH 14
#define DURATION_COL_WIDTH 5 /* 00:00 */

static std::string formatWithAlbum(TrackPtr track, size_t width) {
    std::string trackNum = text::Align(
        track->GetValue(constants::Track::TRACK_NUM),
        text::AlignRight,
        TRACK_COL_WIDTH);

    std::string duration = text::Align(
        duration::Duration(track->GetValue(constants::Track::DURATION)),
        text::AlignRight,
        DURATION_COL_WIDTH);

    std::string album = text::Align(
        track->GetValue(constants::Track::ALBUM),
        text::AlignLeft,
        ALBUM_COL_WIDTH);

    std::string artist = text::Align(
        track->GetValue(constants::Track::ARTIST),
        text::AlignLeft,
        ARTIST_COL_WIDTH);

    int titleWidth =
        width -
        TRACK_COL_WIDTH -
        DURATION_COL_WIDTH -
        ALBUM_COL_WIDTH -
        ARTIST_COL_WIDTH -
        (4 * 3); /* 3 = spacing */

    titleWidth = std::max(0, titleWidth);

    std::string title = text::Align(
        track->GetValue(constants::Track::TITLE),
        text::AlignLeft,
        (int) titleWidth);

    return boost::str(
        boost::format("%s   %s   %s   %s   %s")
        % trackNum % title % duration % album % artist);
}
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
#include "TransportWindow.h"

#include <cursespp/Screen.h>
#include <cursespp/Colors.h>
#include <cursespp/Message.h>
#include <cursespp/Text.h>

#include <app/util/Duration.h>

#include <core/debug.h>
#include <core/library/LocalLibraryConstants.h>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/chrono.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <memory>

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::library;
using namespace musik::core::db;
using namespace musik::box;
using namespace boost::chrono;
using namespace cursespp;

#define REFRESH_TRANSPORT_READOUT 1001
#define REFRESH_INTERVAL_MS 500

#define DEBOUNCE_REFRESH(x) \
    this->RemoveMessage(REFRESH_TRANSPORT_READOUT); \
    this->PostMessage(REFRESH_TRANSPORT_READOUT, 0, 0, x);

static std::string playingFormat = "playing $title from $album";

struct Token {
    enum Type { Normal, Placeholder };

    static std::unique_ptr<Token> New(const std::string& value, Type type) {
        return std::unique_ptr<Token>(new Token(value, type));
    }

    Token(const std::string& value, Type type) {
        this->value = value;
        this->type = type;
    }

    std::string value;
    Type type;
};

typedef std::unique_ptr<Token> TokenPtr;
typedef std::vector<TokenPtr> TokenList;

/* tokenizes an input string that has $placeholder values */
void tokenize(const std::string& format, TokenList& tokens) {
    tokens.clear();
    Token::Type type = Token::Normal;
    size_t i = 0;
    size_t start = 0;
    while (i < format.size()) {
        char c = format[i];
        if ((type == Token::Placeholder && c == ' ') ||
            (type == Token::Normal && c == '$')) {
            /* escape $ with $$ */
            if (c == '$' && i < format.size() - 1 && format[i + 1] == '$') {
                i++;
            }
            else {
                if (i > start) {
                    tokens.push_back(Token::New(format.substr(start, i - start), type));
                }
                start = i;
                type = (c == ' ')  ? Token::Normal : Token::Placeholder;
            }
        }
        ++i;
    }

    if (i > 0) {
        tokens.push_back(Token::New(format.substr(start, i - start), type));
    }
}

/* writes the colorized formatted string to the specified window. accounts for
utf8 characters and ellipsizing */
size_t writePlayingFormat(
    WINDOW *w,
    std::string title,
    std::string album,
    size_t width)
{
    TokenList tokens;
    tokenize(playingFormat, tokens);

    int64 gb = COLOR_PAIR(BOX_COLOR_GREEN_ON_BLACK);
    size_t remaining = width;

    auto it = tokens.begin();
    while (it != tokens.end() && remaining > 0) {
        Token *token = it->get();

        int64 attr = -1;
        std::string value;

        if (token->type == Token::Placeholder) {
            attr = gb;
            if (token->value == "$title") {
                value = title;
            }
            else if (token->value == "$album") {
                value = album;
            }
        }

        if (!value.size()) {
            value = token->value;
        }

        size_t len = u8len(value);
        if (len > remaining) {
            text::Ellipsize(value, remaining);
            len = remaining;
        }

        if (attr != -1) {
            wattron(w, attr);
        }

        wprintw(w, value.c_str());

        if (attr != -1) {
            wattroff(w, attr);
        }

        remaining -= len;
        ++it;
    }

    return (width - remaining);
}

TransportWindow::TransportWindow(musik::box::PlaybackService& playback)
: Window(NULL)
, playback(playback)
, transport(playback.GetTransport())
{
    this->SetContentColor(BOX_COLOR_WHITE_ON_BLACK);
    this->SetFrameVisible(false);
    this->playback.TrackChanged.connect(this, &TransportWindow::OnPlaybackServiceTrackChanged);
    this->transport.VolumeChanged.connect(this, &TransportWindow::OnTransportVolumeChanged);
    this->transport.TimeChanged.connect(this, &TransportWindow::OnTransportTimeChanged);
    this->paused = this->focused = false;
}

TransportWindow::~TransportWindow() {
}

void TransportWindow::Show() {
    Window::Show();
    this->Update();
}

void TransportWindow::ProcessMessage(IMessage &message) {
    int type = message.Type();

    if (type == REFRESH_TRANSPORT_READOUT) {
        this->Update();
        DEBOUNCE_REFRESH(REFRESH_INTERVAL_MS)
    }
}

void TransportWindow::OnPlaybackServiceTrackChanged(size_t index, TrackPtr track) {
    this->currentTrack = track;
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::OnTransportVolumeChanged() {
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::OnTransportTimeChanged(double time) {
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::Focus() {
    this->focused = true;
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::Blur() {
    this->focused = false;
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::Update() {
    this->Clear();
    WINDOW *c = this->GetContent();

    bool paused = (transport.GetPlaybackState() == ITransport::PlaybackPaused);
    bool stopped = (transport.GetPlaybackState() == ITransport::PlaybackStopped);

    int64 gb = COLOR_PAIR(BOX_COLOR_GREEN_ON_BLACK);

    if (focused) {
        gb = COLOR_PAIR(BOX_COLOR_RED_ON_BLACK);
    }

    /* playing SONG TITLE from ALBUM NAME */
    std::string duration = "0";

    if (stopped) {
        wattron(c, gb);
        wprintw(c, "playback is stopped\n");
        wattroff(c, gb);
    }
    else {
        std::string title, album;

        if (this->currentTrack) {
            title = this->currentTrack->GetValue(constants::Track::TITLE);
            album = this->currentTrack->GetValue(constants::Track::ALBUM);
            duration = this->currentTrack->GetValue(constants::Track::DURATION);
        }

        title = title.size() ? title : "[song]";
        album = album.size() ? album : "[album]";
        duration = duration.size() ? duration : "0";

        size_t written = writePlayingFormat(c, title, album, this->GetContentWidth());

        if (written < this->GetContentWidth()) {
            wprintw(c, "\n");
        }
    }

    /* volume slider */

    int volumePercent = (size_t) round(this->transport.Volume() * 100.0f) - 1;
    int thumbOffset = std::min(9, (volumePercent * 10) / 100);

    std::string volume = "vol ";

    for (int i = 0; i < 10; i++) {
        volume += (i == thumbOffset) ? "■" : "─";
    }

    volume += "  ";

    wprintw(c, volume.c_str());

    /* time slider */

    int64 timerAttrs = 0;

    if (paused) { /* blink the track if paused */
        int64 now = duration_cast<seconds>(
            system_clock::now().time_since_epoch()).count();

        if (now % 2 == 0) {
            timerAttrs = COLOR_PAIR(BOX_COLOR_BLACK_ON_BLACK);
        }
    }

    transport.Position();

    int secondsCurrent = (int) round(transport.Position());
    int secondsTotal = boost::lexical_cast<int>(duration);

    std::string currentTime = duration::Duration(std::min(secondsCurrent, secondsTotal));
    std::string totalTime = duration::Duration(secondsTotal);

    size_t timerWidth =
        this->GetContentWidth() -
        u8len(volume) -
        currentTime.size() -
        totalTime.size() -
        2; /* padding */

    thumbOffset = 0;

    if (secondsTotal) {
        size_t progress = (secondsCurrent * 100) / secondsTotal;
        thumbOffset = std::min(timerWidth - 1, (progress * timerWidth) / 100);
    }

    std::string timerTrack = "";
    for (size_t i = 0; i < timerWidth; i++) {
        timerTrack += (i == thumbOffset) ? "■" : "─";
    }

    wattron(c, timerAttrs); /* blink if paused */
    wprintw(c, currentTime.c_str());
    wattroff(c, timerAttrs);

    /* using wprintw() here on large displays (1440p+) will exceed the internal
    buffer length of 512 characters, so use boost format. */
    std::string fmt = boost::str(boost::format(
        " %s %s") % timerTrack % totalTime);

    waddstr(c, fmt.c_str());

    this->Repaint();
}

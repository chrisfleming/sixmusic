# 6 Music

A small desktop player for BBC Radio 6 Music. It plays the live stream and shows
the track on air, the recent tracks, and the current show, with cover art.

Built for Omarchy with C++ and Qt Quick. It follows the desktop theme accent,
Hyprland's corner rounding, and the Omarchy language.

## Keys

| Key     | Action                  |
| ------- | ----------------------- |
| `Space` | Play or stop the radio  |
| `↑` `↓` | Volume up and down      |
| `L`     | Love the current track  |
| `M`     | Mute                    |
| `?`     | Show the shortcuts      |
| `Q`     | Quit                    |

Loved tracks are logged to `~/.local/share/sixmusic/loved.json`, a plain JSON
list with the artist, track, show, and time. The Loved tab in the window shows
them, newest first, and the heart removes one again.

The output dropdown under the controls picks the speakers for this app only.
Media keys and desktop now-playing widgets work through MPRIS
(`org.mpris.MediaPlayer2.sixmusic`).

## Build and install

    bin/build      # build/sixmusic
    bin/test       # unit tests, offscreen
    bin/install    # makepkg -fsi, adds the app to the launcher

## Last.fm

Click "Connect to Last.fm" under the controls. You need your own free API
account ([create one](https://www.last.fm/api/account/create)); paste its API
key and shared secret, then allow access in the browser window that opens. Your
Last.fm password is never typed into the app.

Once connected, 6 Music updates your now-playing, scrobbles tracks, and loves or
unloves tracks on Last.fm when you do here. A track is scrobbled once you have
listened to half of it or four minutes, whichever comes first, and never when
it is 30 seconds or shorter. Time while the radio is stopped does not count.
Scrobbles that cannot be sent (no connection) wait in
`~/.local/share/sixmusic/scrobble-queue.json` and are retried.

Your API secret and login are kept in `~/.config/sixmusic/lastfm.json`, readable
only by you. Loves made while offline reach your local list but not Last.fm, and
loves from before you connected are not copied across.

## Unofficial

This is an unofficial app for personal use. It is not made, endorsed, or
supported by the BBC, and "BBC" and "6 Music" belong to the BBC.

It relies on two things the BBC does not document for third-party use: the live
stream address, and the programme feed at `rms.api.bbc.co.uk` that provides the
track and show information. The BBC can change or block either at any time, and
the app will then show an error in its window. The streams are intended for
listeners in the UK.

Check the BBC's [Terms of Use](https://www.bbc.co.uk/usingthebbc/terms/) before
you rely on it, and do not redistribute it without the BBC's permission.

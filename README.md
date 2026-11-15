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

The BBC streams are intended for listeners in the UK. Track and show data come
from the BBC's public programme feed.

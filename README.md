# Diary

A simple one-page-per-day journal for Linux with GTK 3, written in plain C.
It builds on any architecture GTK runs on, including big-endian PowerPC
(tested on a Power Mac G5 with Gentoo Linux). It's the sibling of the
[classic Mac OS](https://github.com/retrolemmy/Diary) (68k Macs, and PowerPC
Macs up to a G4 with Mac OS 9.2.2),
[AmigaOS 3.2](https://github.com/retrolemmy/Diary-Amiga) (tested on an A4000),
[MorphOS/MUI](https://github.com/retrolemmy/Diary-Amiga-MUI) (tested on a Mac mini G4 and
the A4000), [Mac OS X](https://github.com/retrolemmy/Diary-MacOSX) and
[Windows 98](https://github.com/retrolemmy/Diary-Win98) versions, and like
them it can keep its entries on a
[Diary Server](https://github.com/retrolemmy/Diary-Server), so all your
computers share one diary.

- One window: a month calendar and a list of all entries (newest first) on
  the left, the selected day on the right with `<`, `Today` and `>` buttons
- In the calendar, days with an entry are **bold** and the open day is
  selected; the arrows switch months and years without opening a day. Click
  a day (or a list entry) to open it
- **Find** (Ctrl+F) searches every entry (ignoring case) and narrows the list
  to the matches; **Find Again** (Ctrl+G) jumps to the next match, across
  entries; **Show All Entries** clears the search
- **Go** menu: Previous Day (Alt+Left), Next Day (Alt+Right), Today (Ctrl+T)
- **Format** menu: Font..., Bigger (Ctrl++), Smaller (Ctrl+-); the font
  applies to the whole entry and is remembered
- **Undo** (Ctrl+Z) and **Redo** (Ctrl+Shift+Z or Ctrl+Y), a word at a time
- The current entry is saved automatically when you switch days or quit;
  Ctrl+S saves at any time
- Entries are plain text files named `YYYY-MM-DD` (UTF-8) in
  `~/Documents/Diary Entries` (or the Documents folder your desktop sets).
  Entries from the classic Mac (MacRoman) and Amiga (ISO Latin-1) versions
  can be copied in and read as they are. Clearing an entry completely deletes
  its file
- **File → Diary Server...** keeps the entries on a Diary Server instead
  (by name or IP address, optionally with `:port`, and the server's key if
  it has one). Search then runs on the server, and if an entry was changed
  on another computer since you opened it, Diary asks before saving over it
- The settings are in `~/.config/diary/diary.conf`

## Diary Server

To share the entries between your computers, run
[Diary Server](https://github.com/retrolemmy/Diary-Server) on a machine in
your network, such as a RetroNAS or any small Linux box with Python 3.9 or
later: download `diary_server.py` from its
[latest release](https://github.com/retrolemmy/Diary-Server/releases/latest)
and start it as its README describes. Diary works with Diary Server 1.0 or
later. The same server also gives modern browsers a Diary web page.

## Building

You need a C compiler, `make`, `pkg-config` and GTK 3 with its headers
(GTK 3.22 and GLib 2.58 or later). On Gentoo, desktops like Xfce or MATE already bring
everything (`x11-libs/gtk+:3`); on Debian or Ubuntu, install
`build-essential libgtk-3-dev`. Then, in this folder:

```sh
make
make install
```

`make install` puts `diary`, a menu entry and the icon into `~/.local`, so
Diary shows up in the desktop's menu (under Office or Accessories). For all
users, run `make install PREFIX=/usr/local` as root instead. `make uninstall`
(with the same `PREFIX`) removes it again. You can also run `./diary`
straight from this folder.

## Icon

The icons in `icons` are drawn by `tools/make_png.py`, from the same pixel
art as the classic Mac version's icon:

```sh
python3 tools/make_png.py icons
```

## Diary on all systems

Diary exists for these systems; the current versions share their entries
through a [Diary Server](https://github.com/retrolemmy/Diary-Server), which
also gives modern browsers a Diary web page:

- [Classic Mac OS](https://github.com/retrolemmy/Diary) for 68k and PowerPC
  Macs (tested on a Mac with Mac OS 8.1 and a Power Mac G4 with Mac OS 9.2.2)
- [AmigaOS 3.2](https://github.com/retrolemmy/Diary-Amiga) (tested on an A4000)
- [MorphOS and AmigaOS with MUI](https://github.com/retrolemmy/Diary-Amiga-MUI)
  (tested on a Mac mini G4 with MorphOS and on the A4000)
- [Mac OS X 10.4 and 10.5](https://github.com/retrolemmy/Diary-MacOSX) for
  PowerPC and Intel Macs (tested on Leopard)
- [Windows 98](https://github.com/retrolemmy/Diary-Win98) (tested on Windows 98
  and Windows 11)
- [Linux with GTK 3](https://github.com/retrolemmy/Diary-Linux) (tested on a
  Power Mac G5 with Gentoo Linux)
- Modern browsers on computers, tablets and phones: the Diary Server's web page
  (tested in Chrome and Firefox)
- [Diary Server](https://github.com/retrolemmy/Diary-Server) (tested on a
  RetroNAS)

## License

No rights reserved: Diary is dedicated to the public domain under
[CC0 1.0](LICENSE). Use it for anything, no attribution needed.

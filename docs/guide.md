# RookStudio user guide

This guide describes every page, setting and file of RookStudio: engines playing tournaments, and the study of games
(databases, notation, the opening tree, repertoires, Lichess and a local assistant). The [README](../README.md) says
what the program is, where to download it and how to build it. In the program, F1 opens this guide beside the page, at
the section of what is under the mouse, in the language of the interface (see [Help in the
program](#help-in-the-program)).

## Quick start

### Your first tournament

1. **Engines.** In the rail on the left, *Engines*, then *Add…* and choose an engine's program (a UCI or WinBoard
   executable): it is started once to read its name and options. *Import…* > *An engine list…* takes the engines of
   another program (cutechess, Banksia, Arena, ChessGUI), *Import…* > *Every engine of a folder…* every program of a
   folder. *Check* tells whether an engine works.
2. **New tournament.** *Tournaments* > *New tournament*. Choose a *Format* in its list (*Round robin*, *Gauntlet*…)
   and a *Time control* (*10+0.1* for a first try), and leave the other rows as they are.
3. **Participants.** Double-click engines in the *Library* beside the participants, or select several and press
   Enter.
4. **Start.** The tournament opens in its workspace: *Overview* for the standings and the time left, *Live* for the
   games being played.

The summary on the right of the setup says how many games will be played and how long they will take, and what would
go wrong (a program not found, too many threads, a book missing), each with a button that fixes it.

### Testing a new version of an engine

1. Add both versions to the library.
2. *New tournament*, *Format*: *SPRT gainer [0, 3]* (does the new version gain?) or *SPRT non-regression [-5, 0]*
   (is it no weaker?).
3. Choose the new version as **A** and the old one as **B**, a short time control (*10+0.1*) and, in *Openings*, a
   book of balanced positions (EPD), then **Start**.

The match stops by itself once the SPRT decides: H1 accepted (A is stronger, by the bounds) or H0 accepted (it is
not). The *Overview* shows the Elo, the LLR and its bounds as the games come. The setup is kept: next time, choose the
next version as A and **Start**, or click *Relaunch with…* on the finished match.

### Studying your games

1. **A database.** In the rail, *Databases*, then *New database*, named in place, and *Import a file…*: a PGN file,
   or a `.zst`, `.gz` or `.zip` file holding one (a file dropped on the page does the same). The games come in the
   background, then the database's opening tree is built.
2. **A game.** A click on a game of the list shows it beside; Enter or a double click opens it on the *Board*. Ctrl+O
   opens a PGN file there, Ctrl+V pastes a game or a FEN.
3. **An engine.** The analysis needs an engine: with an empty library, the *Analysis* tab offers to download Gaia in a
   click (see [Popular engines](#popular-engines)). A starts and stops the analysis of the position, G analyses the
   whole game (Alt+A and Alt+G on the study Board).
4. **Your notes.** Enter writes a comment after the move, `!` and `?` put symbols, a move other than the game's starts
   a variation, the right button draws arrows and squares; Ctrl+S saves the game into its database.

### Where to go next

- [New tournament](#new-tournament): every setting, and [tips](#tips-for-the-settings) for choosing them;
- [Running a tournament](#running-a-tournament): what the tabs show, pausing, changing a tournament while it plays;
- [Ratings](#ratings): one rating list over every game played;
- [Databases](#databases), [the study Board](#the-study-board), [the Openings page](#the-openings-page), [the Lichess
  page](#the-lichess-page) and [the assistant](#the-assistant) for your own games;
- [Views](#views): the pages of the rail, as you want them;
- [Broadcast](#broadcast): the games live on a web page, in TLCV or on lichess;
- [Troubleshooting](#troubleshooting) when something goes wrong, and the [Glossary](#glossary) for the testers'
  words.

## The window

### The pages

A navigation rail on the left leads to the pages of the [view](#views) shown, in its order:

| Page | Contents |
|---|---|
| Home | The game to resume, what to start, the recent games, your databases, Lichess and what runs: cards of the view's pages (see [The Home page](#the-home-page)) |
| Tournaments | What runs and what needs action, the queue, the last tournaments finished, every tournament of the profile, *New tournament* |
| Board | A game or a position from anywhere, analysed by an engine of the library; in a view for study, the notation written in place (see [The study Board](#the-study-board)) |
| Databases | Your game databases: import, the game list, the searches |
| Openings | Your repertoires, their coverage and their gaps |
| Training | The day's review, Lichess's puzzles, your mistakes as exercises, guessing the moves, endgames against the tables, mates, board vision (see [The Training page](#the-training-page)) |
| Play | A game against an engine of the library at the strength you choose, with its openings and a handicap, and your games on Lichess, both played on the Board page (see [The Play page](#the-play-page)) |
| Lichess | Your Lichess account, a player's games, the official broadcasts, what the analysis may ask Lichess |
| Engines | The engine library: options, checks, groups, reference Elo, import of other programs' lists, the popular engines |
| Books | The opening book library, and the openings of each book with what is wrong with them |
| Suites | Test suites (STS, WAC, ERET…): engines against the positions of an EPD file |
| Ratings | One rating list over every game of every tournament |
| Log | fastchess' own output and the messages of the program, and *Report a problem…* |
| Settings | Language, theme, interface size, notifications, the appearance of the boards, the views, the assistant, the profile and tournaments folders, the remote machines, the lichess token, the version, the way the window is drawn: the sections of the view's pages first, the others under *Other settings* |

### Views

A view is a set of pages of your own: up to three, each with its name, icon and colour, switched at the top of the
rail. Each view has a badge there, filled for the view shown, and the name of the view shown is written under them;
a click on a badge, or Ctrl+1 to Ctrl+3, shows that view, at the page it was on. With a single view, the switch
disappears.

The first start offers three: *Tests* (Tournaments, Engines, Books, Board, Suites, Ratings, Log), *Study* (Home,
Board, Databases, Openings, Training, Play, Lichess, Engines) and *Everything* (every page). Settings is in every view.
A page that comes with a new version is not added to a view you have by itself: the Home page offers it once (see [The
Training page](#the-training-page) and [The Play page](#the-play-page)).

*Edit the views* opens the views' sheet in place of the page: *Settings* > *Views*, ⋯ under the switch, a right click
on a badge (the sheet opens at its view), or *Edit the views…* in Ctrl+K. It holds:

- the views, in the order of Ctrl+1 to Ctrl+3 (*Up*, *Down*), *New view* (three at most) and *Delete this view*;
- the view chosen: its name (short: it is written under the switch), its icon among twelve, its colour among six;
- its Board: *For study* (the notation written in place, the arrows and squares, the Tree, References and Questions
  tabs: see [The study Board](#the-study-board)) or *For the engines' analysis* (the Board of the engine testers, as
  it was);
- its pages, ticked, in the rail's order (↑ and ↓ beside each move it; Settings stays last); a page can be in several
  views, and *also: …* names the others;
- the rail, as it will look.

Everything shows in the rail at once. Ctrl+Z undoes each change while the sheet is open, Enter keeps the changes,
Escape leaves the views as they were. *Restore the views offered* brings back the three of the first start.

Ctrl+K lists every page with the view it opens in: choosing a page of another view switches to it. A page that no
view holds (a notice can lead there) opens in the view shown, with *Add it to …*.

### The first start

At the first start, RookStudio asks:

- the language of the interface;
- what you want to do: the views offered, each ticked or not (*Tests*, *Study*, *Everything*), and which one shows
  first;
- when a fastchess-gui profile is on the machine, whether to copy it: its engines, books, tournaments and settings.
  fastchess-gui stays as it is and can keep running beside; *Start without* leaves it alone.

Everything can be changed later in *Settings* > *Views*.

### Help in the program

- **Tooltips.** Rest the mouse on a control to see what it does. The tooltip of a control this guide explains
  further ends with *F1: more in the guide*.
- **F1** opens this guide in a panel at the right of the page, at the section of what is under the mouse (a row of the
  setup, a card of the Tournaments page, a tab, a column of the standings); with nothing under the mouse, at the section
  of the panel last clicked, else of the page. The round **?** beside a page's title opens the page's section. F1 again,
  or Escape in the panel, closes it.
- **The guide panel** shows one section with its subsections, and the page beside it keeps working: the explanation
  can be read while the setting is changed. Its header has *Back* and *Forward* (also Alt+Left and Alt+Right, or the
  mouse's side buttons), *Contents* (every section), the search field (Ctrl+F in the panel) and *Open in the browser*
  (this guide on GitHub, for the version that runs). The search finds every word typed, in any order, letter case and
  accents aside; titles come first, and the matches are highlighted. Links to other sections open in the panel, links
  to web pages in the browser, and a code block has *Copy*. Drag the panel's left edge to widen it; its width is kept.
- **Languages.** The guide is built into the program, in every language of the interface, and follows the version
  that runs; nothing is fetched from the network. A section whose translation is missing or older than the English
  text is shown in English, with a line saying so. The release archives hold it too (`guide.md`, and
  `guide.<language>.md` for each translation).
- **Ctrl+K**, the command palette, finds the guide's sections by their titles (*Guide: …*), and *Search the guide
  for "…"* searches it for what was typed.
- **Messages.** A problem the program reports names the engine, the file or the resource, with its fix in place; a
  **?** beside it opens its entry in [Troubleshooting](#troubleshooting).
- **Getting started.** Until a first tournament is started, the Tournaments page shows the steps of the
  [Quick start](#quick-start), each with its button, ticked once done; *Hide* puts them away.
- [Report a problem](#reporting-a-problem) when the program does not do what it should.

### The command palette

**Ctrl+K** opens the command palette: the pages and tabs, the engines of the library (it opens the card), the books, the
tournaments of the Tournaments page, the guide's sections, and the actions possible now (*Pause*, *Resume*, *Pause all*,
*Stop*, *Start*, *Add to the queue*, *Relaunch … with…*, the entries of *Import…*, *Check those not checked*, *Export
for a rating list…*, *Copy* the standings, *Copy the version*, *Report a problem…*, the board colours and piece sets,
the move sounds on or off…). Letter case does not matter and the letters typed need not be adjacent ("rl" finds *Rating
list*); names that start with them, or with a word that does, come first. Up and Down choose, Enter runs, Escape closes;
Ctrl+K is left alone while a text field is being typed in.

### Language, theme and size

The interface is in English, French, German, Spanish, Italian, Brazilian Portuguese, Russian or simplified Chinese.
At startup it follows the first preferred language of the system (English when there is no translation for it); it
can be forced in **Settings**. Chinese uses a font of the system: Microsoft YaHei on Windows, Noto Sans CJK or
WenQuanYi on Linux. The theme is light unless dark, or the system's choice, is picked in **Settings**.

The Spanish, Italian, Portuguese, Russian and Chinese translations are new: corrections from players of those
languages are welcome. The interface's are plain `.po` files in `gui/i18n`, which Poedit and the like open; this
guide's are `docs/guide.<language>.md`.

The pages use the whole window, whatever its size: the Live tab's boards grow with it, the new tournament form
splits into two columns on a wide window, and the list of the Engines, Books and Ratings pages can be widened by
dragging its right edge. Everything (text, buttons, boards) can also be made larger or smaller, from 80 to 250 %:
**Settings** > Interface size, or Ctrl + mouse wheel, Ctrl + and Ctrl -, and Ctrl 0 to go back to 100 %. F11 makes
the window full screen, and back.

A badge on the *Live* tab and on the *Tournaments* button gives the number of games in progress. A number field
takes arithmetic: `40*60`, `15+10`, with `+ - * /` and parentheses, worked out on Enter or when the field is left;
Escape gives the field its value back.

### Boards and their buttons

Under every board that shows a game (the replay of the *Games* tab, the game in focus in *Live*, a detached board,
the Board page), a row of small buttons goes to the start position, the previous move, the next move and the last
one, and turns the board over; their tooltips give the keys, which do the same: ← → Home End, and F to flip. Those
who prefer the keys hide the buttons with **Settings** > *Appearance* > *Navigation buttons under the boards*, a right
click on them, or the command palette (*Hide the navigation buttons under the boards*, and *Show…* to bring them
back). The mouse wheel over a move list scrolls the list, never the moves. **A** on any board opens its position on
the [Board](#board) page and analyses it.

### Version and new versions

The version is in the window title and in **Settings**, whose *Copy* puts it on the clipboard with the system, for a bug
report. *Tell me when a new version is out* (off by default) asks GitHub for the latest release at launch, at most once
a day and in the background; when it is newer, the Tournaments page says so, with *Download page* (the release's page on
GitHub, in the browser), *What's new* (its notes) and *Hide* (until the next version). Nothing is downloaded or
installed by the program, and a failure (no network, GitHub unreachable) is only written in the log.

### Reporting a problem

*Report a problem…* (in **Settings**, on the **Log** page, in the command palette, and in the message of a failure
at startup) opens a new issue on GitHub in the browser, from the bug report form, already filled with the version,
the system (operating system, processor, memory, graphics card and driver) and the last lines of the log. Read it
before sending it, remove what you do not want to show (paths, engine names) and say what you did, what you
expected and what happened. The program sends nothing itself. Ideas go to the same place: *New issue*, *Idea*.

### Window size and place

The window opens as it was left: its size, its place on the screens, maximized or not. When the screen it was on is
gone (a monitor unplugged), it opens in the middle of the main one; when its size no longer fits the screen (a lower
resolution), at the default size. The panels dragged out of it, such as a detached board, come back where they were
too (`layout.ini`), and into the window when their screen is gone. Wayland does not let a program choose its
window's place: there, only the size and the maximized state come back.

### Drawing the window

The window is drawn with OpenGL 3.2. Where there is none (a virtual machine without 3D acceleration, a remote
desktop, an old server), the program draws it through SDL's own renderer instead: Direct3D 11 on Windows, OpenGL
2.1, or the processor alone. The log says which, and why OpenGL was not used; **Settings** > Rendering shows it too,
with the graphics card's name when OpenGL draws. *Software rendering* there (or `--software` on the command line, for
one launch) has the processor draw the window from the next launch, for a graphics driver that fails or crashes. It
uses more processor time, redraws only what changes (and what moves, such as clocks, 15 times a second rather than
30 while the mouse and keys rest), draws square corners, and the panels of the workspace float inside the main window
rather than onto another screen. Under Wayland (and on macOS) the processor's picture is still shown through the
graphics driver; if there is none, start the program with `SDL_VIDEODRIVER=x11`. When no way of drawing works, a
message says so before the program ends.

### Appearance

**Settings** > *Appearance* chooses how every board of the program looks (the live mosaic and the game followed,
the detached boards, the big screen, a game replayed in *Games*, the *Openings* tab, the openings of a book), with
a small board beside the choices showing them:

![Settings > Appearance: the brown board and the Celtic pieces](screenshots/appearance.png)

- **Board**: the colours of the squares, for the light theme and for the dark theme each (the board of the theme in
  use is drawn). A preset (*Blue grey*, the colours of the earlier versions; *Brown*; *Blue*; *Green*; *Grey*), or
  *Custom*: a click on *Light squares*, *Dark squares* or *Last move* opens a colour picker, and changing a colour
  makes the board a custom one from the colours it had. *Last move* is laid over the two squares of the last move,
  its transparency letting the square's colour through.
- **Pieces**: a set built into the program (*Cburnett*, the default; *Chessnut*; *Celtic*; *Fantasy*; their authors
  and licenses in the tooltip and in `ATTRIBUTION.md`), or *Your own folder…*: a folder of 12 SVG pictures named
  `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` and `bK.svg` … `bP.svg` (white and black king, queen,
  rook, bishop, knight, pawn). The pictures are drawn with nanosvg, as the built-in ones: plain shapes, fills,
  strokes and gradients; no text, filters or embedded images. When a file is missing, larger than 1 MB or not a
  picture that can be drawn, the page says which and why, the default pieces are drawn meanwhile, and *Read again*
  reads the folder once the files are fixed. The web page of the broadcast keeps the default pieces.
- **Move sounds** (off by default): *A sound for each move* plays a short knock for each move of the game followed,
  in *Live* and on the big screen, and a heavier one for a capture; not for every game of the mosaic. *Volume*, and
  *Try a move* / *Try a capture* to hear them. The sounds are made by the program (no sound file); the sound device
  is opened only while they are on, and when there is none the log says why and the program stays silent.

- **Figurines in the notation** (on by default): the moves of the Board page, in its moves and its analysis's lines,
  are written with the pieces of the set chosen above (Nf3 becomes a knight and f3); off, with letters.
- **Logos** (off by default): *The engines' logos in Live and on the big screen* draws each engine's logo, chosen on its
  card (see below), beside its name in *Live*, on its cards and on the big screen; off, the names alone. The broadcast
  web page shows them either way.

The command palette has these choices too (*Board colours: Brown*, *Pieces: Celtic*, *Turn the move sounds on*, *Show
the engines' logos*…). They are kept in `settings.json` in the profile; the folder of pieces is kept relative to the
program's folder in the portable mode.

**Engine logos**: the card of an engine in the **Engines** page has *Logo* > *Choose…*, a PNG, JPEG or SVG picture of 2
MB at most (4096 pixels each way at most), shown beside the engine's name in *Live* and on the big screen when *Logos*
is on (above), and on the broadcast web page. The picture is copied into the folder `logos` of the profile, so the
original can be moved or deleted and the profile moved whole; *Remove* takes it away (and its copy, unless another
engine has it). The logo can be changed while the engine plays. While the logos are off, the card says so, with *Show
the logos*.

### The 3D board

**Settings** > *Appearance* > *3D board* > *The Board page's board in 3D* draws the board of the [Board](#board) page
in three dimensions, as a photograph of a real board (off by default; every other board stays flat). The graphics
card computes it by path tracing: the picture refines for a few seconds while nothing moves, a few milliseconds of the
card each frame, then the card rests; while the board turns, a coarser picture stands in. The small board of
*Appearance* is then drawn in 3D too, so that the choices beside it show as they change:

- the lighting, as photographers light a still life: *Studio*, *Low key*, *High key*, *Showroom*, *Window light*,
  *Golden hour* or *Tournament hall*;
- *Exposure*: brighter or darker than the lighting makes it, in stops, each one doubling or halving the light;
- *Quality*: the samples the picture is made of before it rests, 512 by default; fewer spare a processor's graphics
  during tournaments;
- *Varnish*, from waxed and matt wood to a coat that shows the lights, and *Gloss*, from satin to a mirror;
- *Default* brings the exposure, the quality, the varnish and the gloss back as they were; the lighting stays.

What the flat board shows is on it: the eval bar, the last move, the piece picked up and where it may go, a king in
check, the analysis's arrows, which show faintly through the pieces, the reply the engine that played the move
expected in dashes, the move's judgement as a badge above its square, and the frame of a line shown instead of the
game. A left click picks a square as on the flat
board, on a piece or on the board behind it, and a piece can be dragged, its ghost following the pointer (see
[Board](#board)). The right button held while the mouse moves turns round the board (a
right click without moving still lets go of the piece picked up), the wheel comes closer, and a click of the wheel
brings the view back; flipping the board (F) turns the camera half way round.

The 3D board needs OpenGL 3.3. Where the window is drawn without it (the software renderer of
[Drawing the window](#drawing-the-window), some virtual machines and remote desktops), the boards stay flat and
*Appearance* says why. The pieces are clarkerubber's Staunton-Pieces, under the MIT license; *Appearance* links to
them.

## Keyboard shortcuts

| Keys | Where | What they do |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Everywhere | The command palette |
| <kbd>Ctrl</kbd>+<kbd>1</kbd>, <kbd>Ctrl</kbd>+<kbd>2</kbd>, <kbd>Ctrl</kbd>+<kbd>3</kbd> | Everywhere | Show the first, the second, the third view |
| <kbd>F1</kbd> | Everywhere | This guide, at the section of what is under the mouse; again: closes it |
| <kbd>F11</kbd> | Everywhere | Full screen, and back (a detached board's own window when it has the keyboard) |
| <kbd>Ctrl</kbd>+wheel, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Everywhere | Interface size; <kbd>Ctrl</kbd>+<kbd>0</kbd>: back to 100 % |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | A number field | Work out the value; give the field its value back |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | The guide panel | Back, forward |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | The guide panel | Search the guide |
| <kbd>Escape</kbd> | The guide panel | Close it |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Page Up</kbd> <kbd>Page Down</kbd> | The command palette | Choose |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | The command palette | Run, close |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Home</kbd> <kbd>End</kbd> | A board showing a game | Previous move, next move, start, last move |
| <kbd>F</kbd> | A board | Turn it over |
| <kbd>A</kbd> | A board | Open the position on the Board page and analyse it; on the Board page: start or stop the analysis |
| <kbd>End</kbd> | The game in focus in *Live* | Back to the current move |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | The Board page | Paste a FEN, an EPD line, a PGN or a list of moves |
| <kbd>G</kbd> | The Board page | Analyse the game, pass after pass; again: stop |
| <kbd>X</kbd> | The Board page | Show or hide the threat |
| <kbd>N</kbd>, <kbd>Shift</kbd>+<kbd>N</kbd> | The Board page | The next, the previous move the analysis marked |
| <kbd>Escape</kbd> | The Board page | The game back after a line was shown; put down the piece picked up; a game followed from a mosaic: back to the mosaic |
| <kbd>Ctrl</kbd>+<kbd>O</kbd>, <kbd>Ctrl</kbd>+<kbd>N</kbd> | A view for study | Open a PGN file on the Board page; a new game there |
| <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> | A view for study | The recent games; letting Ctrl go opens the one chosen |
| <kbd>Enter</kbd> | The Home page | Resume the game on the board |
| A letter of a move | The study Board | Type the move: the legal moves that begin so are offered, in the letters of the interface's language or in English ones; <kbd>↑</kbd> <kbd>↓</kbd> choose, Enter plays it |
| <kbd>Enter</kbd>, <kbd>Shift</kbd>+<kbd>Enter</kbd> | The study Board | A comment after the move, before it |
| <kbd>!</kbd> <kbd>?</kbd> | The study Board | The move's symbol; two signs in a row: `!!`, `??`, `!?`, `?!` |
| <kbd>Delete</kbd> | The study Board | Delete the moves from the one shown |
| <kbd>Alt</kbd>+<kbd>↑</kbd> | The study Board | Move the variation up, to the main line at last |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | The study Board | Undo, redo |
| <kbd>Ctrl</kbd>+<kbd>S</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> | The study Board | Save the game; save a copy elsewhere |
| <kbd>Ctrl</kbd>+<kbd>←</kbd>, <kbd>Ctrl</kbd>+<kbd>→</kbd> | The study Board | The previous, the next game of the list it came from |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> | The study Board | The position's diagram into the clipboard, as an image |
| <kbd>Alt</kbd>+<kbd>H</kbd> | The study Board | Unfold the game's headers (a hint during a game against an engine) |
| <kbd>Alt</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>6</kbd> | The study Board | The Notation, Analysis, Tree, References, Questions and Endgames tabs |
| <kbd>Alt</kbd>+<kbd>C</kbd> | The study Board | The assistant comments the move |
| <kbd>Alt</kbd>+<kbd>Q</kbd> | The study Board | Why not? The move tried against the game's, or the game's move against the review's best |
| <kbd>Space</kbd>, <kbd>Enter</kbd>, <kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Escape</kbd> | The guided visit | "I don't know", the next moment, why not another move, stop |
| <kbd>Alt</kbd>+<kbd>F</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>X</kbd>, <kbd>Alt</kbd>+<kbd>G</kbd>, <kbd>Alt</kbd>+<kbd>N</kbd>, <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>N</kbd> | The study Board | What F, A, X, G, N and Shift+N do on the engines' Board (on the study Board, the letters type moves) |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd>, <kbd>*</kbd> | The Tree tab | Choose a move, play it, mark it in the active repertoire |
| <kbd>Enter</kbd> | The Openings page | Start the training of the repertoire shown |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Space</kbd>, <kbd>Escape</kbd> | A training session | A hint, "I don't know", stop |
| <kbd>Enter</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | A training session | The next position, or again after a wrong answer; take back a move added to the repertoire |
| <kbd>Enter</kbd> | The Training page | Review all; or start the puzzles once one of their choices was clicked |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Space</kbd>, <kbd>Escape</kbd> | A series of puzzles | A hint, "I don't know", stop |
| <kbd>Enter</kbd>, <kbd>→</kbd> | A series of puzzles | The next puzzle, or again after a wrong answer; the solution a move at a time |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Space</kbd>, <kbd>Escape</kbd> | An exercise of your mistakes | A hint, "I don't know", stop |
| <kbd>Enter</kbd> | An exercise of your mistakes | The next exercise, or again after a wrong answer |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | Guessing the moves | The game goes on at once after a score; stop |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | The Training page's endgames | Choose an endgame once one was clicked; play it |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Escape</kbd> | An endgame against the tables | Take back a move, the right move, stop |
| <kbd>Enter</kbd> | An endgame against the tables | After a fault: take it back; at the end: again, or the next |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | Board vision | Start, go on; stop |
| <kbd>Enter</kbd> | The Play page | Play a game against the engine chosen |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> | A game against an engine | Take back your last move; a hint |
| <kbd>Enter</kbd> | A game against an engine | Confirm *Resign…*; resume a game kept when RookStudio closed; once it is over, review it |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | The Play page, on Lichess | Accept the challenge received, else seek a game; call the seek or the challenge back |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | A game on Lichess | Send the correspondence move waiting, confirm *Resign…*; take back the move waiting or made in advance |
| <kbd>Enter</kbd> | The Play page, Lichess's tournaments | Join the arena or the Swiss tournament shown (the first time: allow it on Lichess) |
| <kbd>Alt</kbd>+<kbd>P</kbd> | Everywhere, in an arena on Lichess | Pause in the arena, the aids back; again: back in the arena |
| A letter, then a figure | The coordinates, *Where is…?* | Name a square |
| <kbd>Shift</kbd> held, <kbd>F2</kbd> | Blindfold | The pieces shown 2 seconds, three times a game; *Where is…?* |
| <kbd>N</kbd>, <kbd>n</kbd>, <kbd>-</kbd> | A line read in your head | The piece as FEN writes it (a white knight, a black one), or none |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | A game list | Go through the list, open the game |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | The Databases page | The search panel |
| <kbd>Escape</kbd> | The Databases page | Close the search panel, then the list |
| <kbd>Ctrl</kbd>+<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> | A player's dossier | The next tab, the previous one |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | A player's dossier | Choose among namesakes, keep the choice; undo it |
| <kbd>Escape</kbd> | A player's dossier | Back to the page it was opened from |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | A database's tournaments | Choose a tournament, open its crosstable |
| <kbd>Escape</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | A tournament's crosstable | Back to the tournaments; undo its last change to the database |
| <kbd>Ctrl</kbd>+<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> | An opening report | The next section, the previous one |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | An opening report's moves | Choose a move, play it on the Board page |
| <kbd>Escape</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | An opening report | Back to the position; take out the moves added to the repertoire |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | The Export sheet | Save the file (a PDF: open it to print); back to the page |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> | The Export sheet, a PDF | Open it in the system's reader, to print |
| <kbd>Ctrl</kbd>+<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> | The Export sheet | The next format, the previous one |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Home</kbd> <kbd>End</kbd>, <kbd>F</kbd> | A page of HTML made by RookStudio | Through the moves; turn the board over |
| <kbd>←</kbd> <kbd>→</kbd> | The big screen | The previous or next game |
| <kbd>+</kbd> <kbd>-</kbd> | The big screen | Larger or smaller text |
| <kbd>Escape</kbd> | The big screen | Leave it |
| <kbd>Tab</kbd>, arrows, <kbd>Enter</kbd>, <kbd>Space</kbd> | New tournament | Move between the rows, lists and buttons; open a row or a list |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | The participants | Send the participants selected to the top, to the bottom |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | The participants | Move them by one place |
| <kbd>Delete</kbd> | The participants | Remove them |
| <kbd>Menu</kbd>, <kbd>Shift</kbd>+<kbd>F10</kbd> | The participants | Open the participant's menu (its own settings) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | The library of the setup | Go to its filter |
| <kbd>Enter</kbd> | The library of the setup | Add the engines selected, or the first one shown |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | The library of the setup | Select every engine shown |
| <kbd>Ctrl</kbd>+click, <kbd>Shift</kbd>+click | Lists | Select several |
| <kbd>Delete</kbd> | The Engines page | Remove the engine selected (after a confirmation) |
| <kbd>Enter</kbd> | An engine's card, its Lichess bot ready | Put the bot online |
| <kbd>Enter</kbd> | An engine's card, *On lichess.org, for the analysis* open | Offer it to Lichess (the first time: allow it on Lichess); *Resume* the service stopped |
| <kbd>Enter</kbd>, <kbd>Escape</kbd> | Making an account a Lichess bot, its name typed | Make it a bot; cancel |
| <kbd>Enter</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | An engine's console | Send the line; bring the earlier lines back |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | The headless mode | Stop after the games being played |

## The Home page

A view's **Home** page gathers the view's pages in cards, what runs first:

- **The arena** you are in from RookStudio, at the top: its name, your rank and points, the time left, *Back to it*
  and *Pause* (Alt+P); a Swiss tournament you joined, its round (see [Arenas and Swiss
  tournaments](#arenas-and-swiss-tournaments)).
- **Resume where you were**, when the view holds the Board: the game on the board at its move, on a small board, where
  it comes from (its database and number, its place in a filter, or its file), its comments, and whether it has
  changes not saved yet. *Resume* (Enter) goes back to it; Ctrl+Tab lists the recent games.
- What to start, each with its key: *Open a PGN…* (Ctrl+O), *Paste a game or a FEN* (Ctrl+V), *New game* (Ctrl+N),
  *Import into a database…*, *Play against an engine* (the Play page), *New tournament*: those of the view's pages.
- The tournaments running, as on the Tournaments page, when the view holds Tournaments.
- **Recently**: the games opened on the board, a click reopening one with its changes.
- **My databases**: each with its games and the state of its opening tree, the import running, *+ Import…*, and the
  tournaments read without a copy (see [Databases](#databases)).
- **Lichess**: the account, a player's games with *Sync*, the new games counted if you chose it, the broadcasts
  live (see [The Lichess page](#the-lichess-page)).
- **In progress**: a game on Lichess played from RookStudio (*Back to the game*) and the challenges received (see [On
  Lichess](#on-lichess)), your Lichess bot online and its game (see [A Lichess bot](#a-lichess-bot)), your engines
  serving lichess.org's analysis board (see [Your engine on lichess.org](#your-engine-on-lichessorg)), the game's
  analysis, the position's, an import, the assistant (its download, its model
  loaded, *Unload*), a series of puzzles, a session of exercises, a game guessed, an endgame or a vision exercise under
  way, and what the day's review has
  due, in one count, a click reviewing it all (see [The day's review](#the-days-review)).

The search button at the top, or Ctrl+K, finds a command, a game, a database.

## The Tournaments page

The **Tournaments** page is a control room: what runs and what needs you come first, then the rest.

- **Getting started**, until a first tournament is started: the steps of the [Quick start](#quick-start) (add
  engines, set up a tournament, start it), each with its button and ticked once done, and *Read the quick start*,
  which opens it in the guide panel. *Hide* puts the card away.
- **Pause all**, beside *Open a folder…* and *New tournament*, pauses the running tournaments (no new game pair
  starts, the games in progress play to the end) and holds the queue: no queued tournament starts until **Resume
  all**, which resumes them and lets the queue go on, at once if a tournament ended meanwhile.
- **Cores**: the machine's hardware threads, and those the games being played take, the rest free. A game takes
  the `Threads` of its engine that has the most, since the two engines think in turn; the bar shows each tournament's
  name and its count (the Board page's engines, the Lichess bot's game and the requests of your engines on lichess.org
  too), the cursor the details.
- **Needs action**, when something does, each with its fix in place: an engine that cannot start (see [An engine
  that cannot start](#an-engine-that-cannot-start): *Try again*, *Withdraw…*, *Replace…*, *Engine log*); the
  tournament an error or an engine stopped, with what stopped it, *Open the engine*, *Open the log*, *Resume*, *Open*
  and *Hide*; a drive too full for a night of games (under 1 GB free, or under 2 % of a drive under 500 GB), the
  tournaments folder's or a running tournament's, looked at every minute, with *Open the folder* and *Change the
  tournaments folder…* (where new tournaments go, as in **Settings**).
- **Running**: a card for each running tournament: its name, format, time control and state, a progress bar with
  the games played of those planned and the games being played, when it ends, and who leads: the first of the
  standings with its rating against the field and its points (a knockout: the players left; a team match: the
  teams' score and its Elo), the engine ahead in a match with its Elo and the score, or the LLR of an SPRT with its
  bounds, then the Elo and the score. *Open* goes to its workspace, *Pause* pauses it. A tournament that can be
  resumed shows instead under *Interrupted*, with *Resume*. See [Several tournaments at
  once](#several-tournaments-at-once) for the weights and the cores of each.
- **Your Lichess bot**, while it is online: its engine, its game in progress with *On the Board*, *Its card* and *Put
  offline*; it declines challenges *later* while the tournaments need the cores (see [A Lichess bot](#a-lichess-bot)).
- **Your engines on lichess.org**, while they serve: each with its state, *Its card* and *Stop the service*; they take
  only the threads the tournaments leave free (see [Your engine on lichess.org](#your-engine-on-lichessorg)).
- **Queue**: the tournaments waiting, each with its length and when it starts, and when everything ends (see
  [Queue and relaunch](#queue-and-relaunch)).
- **Last finished**: the last five tournaments finished, with their result: a match's score, the SPRT's decision,
  a knockout's winner (of its final), a team match's score, or the first of the standings with its points of its
  games. *Relaunch with…* for a match, *Clone* (see [Queue and relaunch](#queue-and-relaunch)) and *Open*.
- **Health**: the crashes or hangs, losses on time and illegal moves of the last 24 hours, the memory free and
  installed, and the engine processes running; then the engines that lost those games, the three with the most,
  with *Open the engine* and *Open the folder* (the tournament's `logs` folder keeps the engine log of every game
  that ended badly). A loss on time before move 20 is told apart: it is more often an engine set up wrong (too many
  threads, a hash too large to allocate in time, a network loaded at every game, a slow start) than its time
  management.
- **All tournaments**: every tournament of the profile, newest first, with *Open*, *Clone* and *Folder*; its title folds
  it. The result of a finished one shows at the cursor, over its state.

The command palette has these actions too: *Pause all*, *Resume all*, *Relaunch … with…* for the last matches
finished, *Clone …* for the tournament shown and the last finished, *Open the tournaments folder* and *Change the
tournaments folder…*.

## Engines

The **Engines** page is the engine library: the list on the left, the card of the engine chosen on the right.

### Adding and checking engines

**Add…** registers a UCI executable. The engine is started once to read its name, author and options. Options
are edited in a table: modified values are highlighted, and only values that differ from the engine default are
sent with `setoption` at the start of every game. `Threads` and `Hash` come first.

- **Duplicate** makes a second entry for the same binary, with other options (a different `Hash`, a skill level,
  a different network file…). Entries are named after the UCI `id name` when first added and can be renamed.
- **Check** starts the engine, measures how fast it answers `uci` and `isready`, and has it play one move; then it
  runs fastchess' UCI compliance steps (`id name` and `id author`, positions from a FEN and with moves, `go` with
  each clock, info lines with an integer score…), up to the first that fails. The result (✓ or the error) stays
  next to the engine with its date, and *UCI compliance report* lists the steps, with what the engine wrote after
  a failed one. An engine spoken to in xboard only gets the first part.
- **Console…** talks to the engine, outside any tournament: it is started with the library's program, arguments
  and working directory, and a UCI engine gets `uci` and then the library's option values. Lines typed are sent as
  they are (Enter; Up and Down bring the earlier ones back), and the log shows every line with its time, those sent
  marked `>` and the error output `!`. Buttons send the usual commands (`isready`, `position startpos`,
  `go depth 10`, `go infinite`, `stop`…; WinBoard's for a WinBoard engine, spoken to in its own protocol unless
  *Through the adapter* is ticked) and the position of the Board page. *Copy* and *Save…* keep the log. The
  console shows in place of the engine's settings (*Settings* goes back to them, the console staying open); *Close*
  ends the engine. While a tournament runs, what the engine searches takes cores from its games: the console says
  so. Its engine is not one of the tournament's processes: the end of a tournament does not end it.
- **Quick test** plays a short match against the engine itself or another one: 10 games at 10+0.1 from 5 positions
  (one with an en passant capture), through the same code as a tournament, shown in its workspace while it plays.
  The result stays next to the engine: its score, its losses on time, crashes and illegal moves, and the share of
  its clock it used, which tells an engine that plays far too fast or too close to the flag. Its mate PVs are
  checked (see *Check the mate PVs* in [PGN and misc](#pgn-and-misc)). Only the last quick test is kept, in `tests/`
  in the profile (with the engine logs of its games that ended badly).

### A large library

- **Groups**: free labels separated by commas (`pool A`, `list 40/15`…). The list can be filtered by group or searched,
  and a group can be added to a tournament in one click. *Notes*: anything to remember about the engine. *Logo*: a
  picture shown beside its name in *Live* and on the big screen when the logos are on, and on the broadcast web page
  (see [Appearance](#appearance)).
- **Search and filters**: every engine search (the library, and the engine lists of the other pages) finds a name,
  the name the engine reports, its author, its program path or its notes; the state filter shows the engines that
  work, failed or changed, were never checked, have lost their program, speak WinBoard, or are inactive. Ctrl or
  Shift click selects several engines: *Check* (one after the other, never while a tournament plays), *Add to a
  group…*, *Remove…*. *More…* checks those never checked (*Check those not checked*) and removes the missing
  programs. A string option holding a file or a folder (`EvalFile`, `SyzygyPath`…) has a *…* button.
- **Inactive** (on the card, or from the right click for all the engines selected): the engine stays in the library,
  greyed, but is left out of the engine pickers (setup, suites, quick test, relaunch, adding an engine to a running
  tournament) and of *Check those not checked*. A tournament or a setup that has it already keeps it.
- **Uses a GPU** (on the card): a network engine that plays on a graphics card, such as Lc0. No more such engines
  play at once than the *GPUs* of **Settings**, in a tournament and across tournaments (see [Several tournaments at
  once](#several-tournaments-at-once)); from the next tournament it joins.
- **Reference Elo**: the engine's rating on a list you trust. A tournament or the rating list can be anchored on it,
  and the games of a tournament carry it in their `WhiteElo` and `BlackElo` tags (copied when the engine joins).
  *Import…* > *A rating list, for the reference Elo…* sets it for many engines at once (see [Importing engines and
  ratings](#importing-engines-and-ratings)).

### Popular engines

*Popular engines…*, beside *Add…*, lists the strongest engines whose authors publish builds for Linux, Windows and
macOS on GitHub (Gaia, Stockfish, Triumviratus, Reckless, PlentyChess, pawnocchio, Coda, Cinder, Stormphrax,
Viridithas), each at its latest version, with the file this processor runs (the fastest of its builds that the
processor supports: AVX2, BMI2, AVX-512, a build made for its Zen generation…), its size and its licence. *Add*
downloads it from its author's release, checks it against the SHA-256 hash GitHub publishes, takes it out of its
archive, starts it once (`uci`) and adds it to the library, each version in a folder of its own in the profile's
`engines` folder; a file that does not start is replaced by the next build. *Cancel* stops a download, and the next
try goes on from where it stopped. The list comes from a catalogue kept in RookStudio's repository, read again once
a day, the program's own copy otherwise. What leaves the machine: the catalogue, a request to api.github.com per
engine (kept a day), and the file chosen; only while the page is open or an engine is added.

An engine added this way follows its new versions: they are looked for once a day when the Engines page opens (or
only when *Popular engines…* opens, in *Settings*), and shown above the list with *Update*. The new version goes in a
folder of its own and the engine takes it once it has answered `uci`; *Keep … too* keeps the version before as an
engine of its own, for a match between them. A tournament that is not over keeps the files it started with; the
version before is deleted once the tournaments that play it are over (see [An engine's
versions](#an-engines-versions)).

Where an engine is first needed (the Board page's analysis, its questions and its review) and the library has none,
a card offers Gaia, downloaded with a click (or Enter) and made the engine of the analysis and of the assistant; the
other popular engines and an engine from the disk are a click away there too.

### An engine's versions

Each version of an engine taken from *Popular engines…* stays in a folder of its own in the profile's `engines` folder.
Once an update has replaced a version in the library, its folder is deleted when no tournament that is not over
(playing, paused or stopped) plays it: at once when none does, else at the end of the last one, as RookStudio looks
again at the end of each tournament. A notice at the window's corner says so: *Gaia 4.4.3 deleted: “Night Gaia” is over
· 118 MB freed*, with *Its card*.

When the engine has more than its library's version on the disk, its card lists them under *Its versions on the disk*,
each with its state:

- the library's: the one the analysis, new tournaments, the bot and the service for Lichess take;
- one a tournament not over plays, with that tournament and where it is (*playing, game 212 of 400*), deleted at its end
  when an update replaced it; *Keep … too* keeps it for good, as an engine of its own;
- one kept on purpose (*Keep … too*, here or in the update), never deleted; *Stop keeping it* (a second click) takes its
  engine out of the library, and its folder goes once no tournament that is not over plays it;
- one left from before, never replaced by an update since: kept until *Delete it* (a second click);
- one deleted that a tournament not over asks for again (resumed from a folder RookStudio did not look in): *Download
  it again* takes it from its release on GitHub, checked against the hash it had, at the same place.

### Starting engines

- **Reading the options** never blocks a start. *Resume* starts at once: a tournament plays the engines copied in
  its folder. *Start*, the queue and the quick test wait only for the read of an engine they play, read first; any
  other read or check is interrupted and done again later. A start that waits says so on its button (*Waiting for
  …*, with *Cancel*), and another start is refused meanwhile. A start also waits for a running test suite, and a test
  suite does not start while the quick test runs or a start waits. Reads, checks and test suites run beside the
  tournaments, which play in processes of their own. A queued engine can be read next from its menu.
- *Restart the engine for every game* starts a new process for each game instead of reusing it. Otherwise a
  tournament keeps one waiting process per engine that still has games to play, up to 32 (plus two per game being
  played), and closes an engine after its last game: in a knockout as soon as its match is lost, in a Swiss once it
  has no game left in the last round.
- *Start timeout (s)* (60 by default) is the time an engine is given to answer `uciok` when it starts and `isready`
  at the start of each game; a slow starter (a large network to load) needs more. An engine that fails to start is
  tried again three times, 10 s apart, with the same timeout; a pause holds the tries, without using them up. Then
  its games are set aside and the others go on (see [An engine that cannot start](#an-engine-that-cannot-start)). A
  tournament created afterwards copies the timeout into its participants.

### Rebuilt programs

- **Moved or rebuilt programs**: the library remembers each program (size, date, CRC32). A program that is no
  longer there is marked **!** with *Locate…*, and a tournament does not start with it. A program that changed
  (a new build, or another file under the same path) has its options read again automatically.
- **What a new build changed**: when the new read differs from the one before, the engine is marked **Δ** and its
  card lists the differences until *Dismiss*: its new `id name` (with *Rename it* when the engine was named after
  the old one) and author, each option whose default, bounds or choices changed (before and now), the options new
  and gone, and your value of each (the value of a gone option is dropped; one the new build no longer accepts is in
  orange). For an option whose default changed, or whose value no longer fits, choose *Keep my value* (or *Keep the
  old default* where you had none: it plays as before) or *Use the new default*; *Keep my values* and *Use the new
  defaults* choose for all. The notice goes once every choice is made, unless it has more to tell. Several builds
  before it is dismissed make one notice, against the read before the first. *Failed or changed* in the state
  filter shows these engines too, and the command palette has *What changed*, *Keep my values*, *Use the new
  defaults* and *Dismiss what changed* for each.
- **The programs of the tournaments**: each tournament records the programs it was started with. The setup's
  summary warns of an engine whose program changed since its last tournament (the newest one of the tournaments
  folder that played this program), with *What changed* when its card has a notice. Resuming a tournament with a
  program rebuilt or missing since says so in its header, with *Continue* (the notice closes) and *Pause*; the
  tournament does not stop by itself.

### Importing engines and ratings

**Import…** > *An engine list…* reads the engine list of another program: ChessGUI (`ChsGUI_Eng.TXT`, with the
options of `ChsGUI_Cfg.ini` next to it), cutechess and Banksia (`engines.json`), Arena (`ArenaENG.cfg`, in Arena's
folder: each engine's program, arguments, protocol and, for UCI engines, the option values set in Arena) and other
INI style lists. A list that gives no engine says so, with the formats read and *Every engine of a folder…* to
try instead.

*Every engine of a folder…* takes every program found in a folder, and remembers the folder: *Refresh: the new
engines of the folder* then offers only the engines added to it since. The engines found are listed first, to untick
those to leave out; engines already in the library (same program and arguments) are skipped; the options of the
others are read; WinBoard engines are imported too.

*Import…* > *A rating list, for the reference Elo…* reads a rating list, pasted or opened: a rating list's text
version or its web page (saved or copied), Ordo's output (text or CSV), or a CSV of `name,elo` lines. A name of the
list is a library engine with the same name, or the only one whose name is the same letter case and spaces aside
(as for the players of imported PGN files, see [Ratings](#ratings)). The preview shows each engine's reference Elo,
old and new, ticked to be set, and the names of the list that are no library engine. The names of a list often carry
their build (`64-bit 4CPU`): rename the engines to match them if needed.

### WinBoard engines

**WinBoard engines** (xboard protocol, CECP) are played like the others. The *Protocol* of an engine is found
out when it is added (*Automatic*), or chosen: UCI or WinBoard. fastchess only speaks UCI: a WinBoard engine is
started through a translation built into RookStudio (`rookstudio --wb2uci ENGINE [ARGS]`, which fastchess
starts in place of the engine), and its options, `memory`, `cores` and Syzygy path become UCI options. It drives
the engine the way WinBoard does: protocol 2 features, or protocol 1 for engines that send none; positions with
`setboard`, or edit mode; moves in coordinates or SAN; `level`, `time`/`otim`, `st`, `sd`, `nps`; thinking
output, resignation. Engines marked ↔ in the list are WinBoard engines.

- An engine of protocol 1 without `setboard` nor edit mode (TSCP, for instance) can only start from the initial
  position: give it openings as moves (a PGN book), not as positions (EPD).
- Chess960 for an engine with the `fischerandom` variant: `variant fischerandom`, positions in X-FEN, castling as
  `O-O`/`O-O-O`, as WinBoard sends them.
- *Scores from White's side*, among the engine's options: for the old engines that give their scores from White's
  point of view rather than the side to move's, as the protocol asks; their scores as Black are reversed.
- An engine that asks to be started again for every game (`feature reuse=0`) gets *Restart the engine for every
  game* ticked when it is added.
- `FCG_WB2UCI_LOG=FILE` in the environment writes every line exchanged with WinBoard engines to `FILE`.

### A Lichess bot

An engine's card has a line **On Lichess, as a bot**: *Make it a Lichess bot…* sets the engine up to play on Lichess
under a bot account, through Lichess's Bot API (where engines are allowed), accepting the challenges you choose while
RookStudio is open. The line becomes a part of the card, which leads through the steps below.

- **The bot's account** is a second Lichess account, apart from yours: new, with a name of its own, and with no game
  played. RookStudio does not create accounts: *Create the account on lichess.org* opens Lichess's page. *Link the
  bot's account…* opens Lichess's page asking for one permission, *play as a bot* (`bot:play`): log in there with the
  bot's account, not yours (a private window of the browser avoids logging you out). Or paste a token of the bot's
  account made on lichess.org (*Make a token on lichess.org* opens the page with that permission ticked) and *Check it*.
  Your own account stays linked apart, for the rest of RookStudio; the bot has its own token, kept in the profile's
  `lichess-bot.json`, readable by you alone.
- **The checks**, before anything is asked that changes the account: its name and when it was created, the games it
  played (none), whether it is a bot already (then it serves as it is), and that it is not your own linked account.
  Your own account is never kept (the token RookStudio got for it is revoked at once); a closed account, or a token
  without the permission, neither. An account that has played is refused, as Lichess would: create a new one, play
  nothing on it, then link it (*Link another account…*, *Unlink this one*).
- **Make it a bot…** cannot be undone: the account never plays as a person again, Lichess marks it BOT, and it plays
  challenges only (no lobby, no tournaments but the arenas open to bots). The step says so and asks for the account's
  name: *Make it a bot* lights up once the name typed is its own (Escape cancels). What leaves the machine: that
  request, with the bot's token. A refusal is said with Lichess's words; without an answer, nothing changed: try again.
- **The challenges it accepts**, on the card: the speeds (bullet, blitz, rapid, classical, correspondence; never
  UltraBullet, which Lichess does not let bots play), rated games or casual only, people and bots, a rating range for
  the challenger (0: no bound), Chess960 when the engine knows it (`UCI_Chess960`), one game at a time. At first:
  casual only, blitz and rapid, as Lichess asks bots to play casual while they are tested; widen them afterwards. A
  rated challenge from your own account is declined (Lichess forbids rated games between one person's accounts); a
  casual one is played. The other challenges are declined politely, with the reason Lichess shows the challenger:
  *too fast*, *too slow*, *not this clock*, *casual only*, *standard chess only*, *not this variant*, *no bots*, *bots
  only*, or *later* while it plays, goes offline, or a tournament needs the cores.
- **Its play**: the engine with its options of the library (its `Threads` and `Hash` counted in the machine's core
  budget while it plays), a Polyglot book of your library played while it has a move, the tournaments' tables
  (`SyzygyPath`, for an engine that has the option and no path of its own), and no thinking on the opponent's time. Its
  clock comes from Lichess at each move, less a margin for the network (a tenth of its time left, from 0.2 to 1 second);
  before both sides have moved, it plays within the time Lichess gives for the first move; by correspondence, it thinks
  15 seconds a move. It takes a draw offered when the engine stands clearly worse, or no better late in the game,
  declines a takeback, and claims the victory when its opponent left long enough. Its words, said in the players' chat
  at the start and the end, are yours to choose (empty: nothing said). An engine that cannot start or stops resigns the
  game (aborts it before both sides moved), and the card says why.
- **Put online** (Enter) opens the bot's own event stream (Lichess keeps one per token): the challenges come, answered
  at once by its rules. Online, the card shows the bot's day (games won, drawn and lost, challenges declined), its game
  on a small board with the engine's evaluation, *On the Board*, its last games and the challenges declined, *Its page
  on lichess.org* and **Put offline**: no new challenge, the game in progress plays on to its end, then the stream
  closes. A correspondence game waits on Lichess while the bot is offline; it comes back when the bot is online again.
  The bot plays only while RookStudio is open: closing RookStudio during one of its live games asks first (*Stay* ·
  Escape, *Put it offline*, *Close anyway*), as the game would be lost on time.
- **Its game on the Board**: followed move by move, the bot's side below, the players' ratings and clocks in their
  strips, and over the column its card: what the game is, whose move it is, the engine's search (its evaluation, depth
  and expected line: its own, as it plays the game), *Open on lichess.org*, *Resign for it…* (a second click) and *Put
  offline*. The moves on the board are the bot's alone. At its end, the game comes into the database *Lichess · its
  name*, made when missing.
- **Where it shows**: online, the bot shows where what runs shows: on the Home page under *In progress*, and on the
  Tournaments page, with its game, *On the Board*, *Its card* and *Put offline*.
- **A tournament first**: a challenge is declined *later* while a tournament playing on this computer needs the cores
  the bot's engine would take. A game of the bot's already playing when a tournament starts goes on to its end, its
  engine's threads counted in the core budget as the Board page's engines are; no new challenge is taken meanwhile.
- **Fair play**: the bot's games are not yours, and they do not lock your aids. During your own live game on Lichess,
  the bot's search is not shown on its card nor on the Board (the card says why); its games go on.
- **Unlink the bot…** (a second click) revokes its token at Lichess and deletes it here; the account stays a bot.
- What leaves the machine: the engine's moves, its words in the chat, its answers to the challenges, with the bot's
  token; nothing of your account. Its profile (name, description) is set on lichess.org. Its choices and its last
  games are kept in the profile's `bot/bot.json`.

The command palette has *Put the Lichess bot online*, *Put the Lichess bot offline*, *The Lichess bot's game on the
Board* and *The Lichess bot's card*, and on the Engines page *Make … a Lichess bot…*.

### Your engine on lichess.org

An engine's card has a line **On lichess.org, for the analysis**: *Offer to Lichess…* offers the engine to lichess.org's
analysis board, through Lichess's external engine API. It computes here, on your machine, and its lines show there, for
your account only, in the analysis board's engine menu, while RookStudio is open. Lichess's API for it is still in
alpha.

- **What it is offered as**: its name at Lichess (3 to 200 characters; *Gaia 4.4.4 (RookStudio)* by default), the
  machine (this computer, or a remote machine of Settings > Machines where the engine has its path, by SSH, with its own
  threads), the threads at most (Lichess asks for 1 to that many for each request) and the memory at most. Lichess runs
  such an engine in the Chess960 mode (`UCI_Chess960`) for every game: an engine without it cannot be offered, and its
  line says so instead of *Offer to Lichess…*.
- **The first time**, Lichess asks for two more permissions, `engine:read` and `engine:write` (to register an engine for
  your account, read it, withdraw it): *Allow on Lichess…* (Enter) opens Lichess's page; your account stays linked, and
  the token it replaces is revoked. Without a linked account, the card says so, with *Link the account…*.
- **Then, without asking anything more**, RookStudio registers the engine with your account (one request, with your
  token) and a secret drawn here, which Lichess never shows again; RookStudio keeps it in the profile's
  `lichess-engines.json`, readable by you alone, and never shows nor logs it. It then waits for work at
  engine.lichess.ovh with that secret alone, without your token: a long wait, renewed while the service runs. When your
  analysis board asks, a request comes with the position (a starting position and moves), the threads, the memory, 1 to
  5 lines and a time, a depth or nodes: the engine is set (the Chess960 mode, `UCI_AnalyseMode` when it has it, its
  options of the library), and its lines (depth, multipv, score, nodes, time, pv) go to Lichess as it finds them, then
  its best move. When you move on, Lichess closes the request, and the engine stops.
- **In service**, the card shows the request being answered (its lines, threads, memory, depth and speed), today's
  requests and minutes of analysis, the position asked with the engine's best move, *Open Lichess's analysis board* and
  **Stop the service**. The Lichess page lists the engines offered (*Your engines on lichess.org*); the Home page (under
  *In progress*) and the Tournaments page show those in service, and the cores' bar their threads.
- **The tournament first**: the service takes only the threads the tournaments playing on its machine leave free (the
  cores of Settings), the Lichess bot's game first too (see [A Lichess bot](#a-lichess-bot)); with none left, it pauses
  and says so, Lichess's requests waiting. A request under way when a tournament needs its threads is stopped there.
- **Fair play**: during your own live game on Lichess, the service pauses until the game ends (the requests stay
  unanswered), then resumes by itself. The positions of your correspondence games are never analysed: a request on one
  of them, on a line through it or one move before it (compared by the men and the side to move, as the lock of [Fair
  play](#fair-play) does) gets an empty answer. Lichess itself offers an external engine on its analysis pages only,
  never during a game.
- **Stop the service**: RookStudio no longer waits for work; the engine stays registered at Lichess, without answering;
  *Resume* (Enter) serves it again. *Change…* registers it again with another name, machine or limits. **Withdraw from
  Lichess** deletes the registration (one request), and its secret is no longer used; *Offer to Lichess…* makes it
  again. RookStudio closed, the engine stays in Lichess's list without answering; at the next start, the service resumes
  if it was in service when you closed.
- **When something goes wrong**: Lichess refusing the secret (401: the registration was withdrawn on lichess.org, or the
  secret changed) stops the service, with *Register again (a new secret)*; an answer of Lichess that RookStudio does not
  read (the API is in alpha) stops it rather than answer wrongly, and says so, with *See whether a version of RookStudio
  follows it*; offline, the service waits for the network and resumes by itself (after too many requests, Lichess's
  minute).
- What leaves the machine: at the registration, the name, the limits and the secret, with your token; then the engine's
  lines for the positions Lichess sends. Those positions come from your own browser, through Lichess, which sees the
  lines. Nothing else of your machine.

The command palette has *Offer … to lichess.org…* on the Engines page, *Stop the service of … on lichess.org* and
*Resume the service of … on lichess.org*.

## Books

**Add…** registers an opening book (EPD, PGN or Polyglot `.bin`) under a name, with a description; for a PGN
book the number of plies kept, for a Polyglot book the depth its lines are followed to. The setup's *Openings*
section then offers the books of the library by name, with their number of openings (any other file can still
be chosen). *Remove* takes a book out of the library and leaves the file alone.

### Checking a book

The book chosen is read in the background, with fastchess' own readers (so its openings are exactly those a
tournament plays), and checked:

- **illegal positions**: not one king of each colour, pawns on the first or last rank, the side not to move in
  check;
- **duplicates**: the same position as an earlier opening, by transposition included;
- **colours reversed**: an earlier opening with the colours swapped (a book meant to be played twice, colours
  reversed, does not need both);
- **games already over**: no legal move, or not enough material to mate.

fastchess leaves the openings whose position cannot be played out of a tournament (a FEN or EPD line without a
king or otherwise malformed, a king that could be taken, a PGN game whose FEN tag is invalid), with one warning per
book; they are counted here as illegal positions. A PGN game whose moves stop parsing (a malformed or ambiguous
move) keeps the moves before it. The lines of a PGN book that start with `%` (escape lines, which the PGN standard
makes comments; the AVT books have some) are ignored, by the tournaments too, and the Books page counts them (*lines
starting with %, ignored*).

Its openings are listed below, every one or only those with a problem, searched by part of a FEN or by moves (a
PGN book shows its moves in SAN); a click shows the position on a board. *Use it for the next tournament* puts it
in the setup (with Chess960 for a Chess960 book). *Save a cleaned copy…* writes an EPD book without the illegal
positions, the duplicates and the finished games (and, if ticked, the positions with colours reversed), and adds
it to the library. *Run as a test suite…* opens an EPD book in **Suites**.

### Creating books

**Create…** writes books for the library (also in the command palette):

- *Generate Chess324 book*: Larry Kaufman's Chess324. The kings and the rooks stay on their usual squares, so
  castling is the usual one; the queen, the bishops and the knights of each side are shuffled on the other squares
  of its back rank (the b, c, d, f and g files), the bishops on squares of opposite colours. That makes 18
  arrangements a side, White's and Black's chosen independently: *All 324 positions*, or *Only the 18 symmetrical
  ones*, where Black's pieces stand on the same files as White's. They are ordinary positions, written to an EPD
  file (offered next to the books of the library, or in `books/` in the profile) and played by any engine, without
  Chess960.
- *Build a Polyglot book…*: a Polyglot book (`.bin`) from the games of a PGN file, as Polyglot's `make-book` does,
  with the conversion that turns the participants' PGN books into Polyglot books. Its form, in place of the book's
  details, takes the PGN file (the PGN book chosen in the list, if any), the **depth** (the plies of each game that
  go into the book), the **minimum games** a move needs in its position to be kept (Polyglot's `-min-game`; the
  others are left out as played too rarely), the **weights**, *by score* (the points of the side that played the
  move: a win 2, a draw 1, a loss 0, as Polyglot counts; games without a result are left out, and so is a move whose
  games were all lost) or *by count* (the number of games that play it), and, if ticked, a **rating**: only the
  moves of a player rated at least that much count, by the game's `WhiteElo` or `BlackElo` tag (a player without
  one is left out). *Build…* asks where to write the book (next to the PGN file by default), then reads the games in
  the background, with its progress and *Cancel*, beside the form and at the end of the list; the book, sorted by
  key as the format wants, is then added to the library, its options in its description. The console says how many
  games went into it, how many the options left out and how many could not be read.

### Books from the named openings

*From Lichess's named openings…*, in **Create…**, makes a book of starting positions from Lichess's table of named
openings, in the program already (chess-openings, CC0): nothing is downloaded. Its form, in place of the book's
details, takes the **families** (A flank openings, B semi-open games, C open games, D closed games, E Indian
defences), the **lengths** of the lines kept, in plies (8 to 16 at first), *One position per named variation* (several
lines share a name), and the **number** of positions, spread evenly over the ECO codes when more qualify. It says how
many positions and openings it would hold; *Make the book…* asks where to write the EPD file, each position with its
move number (`hmvc`, `fmvn`) and its opening as `c0`, and adds it to the library.

### Books from the evaluated positions

*From Lichess's evaluated positions…*, in **Create…**, makes a book of the positions Lichess's players had analysed:
those of the named openings' lines and one move off them, each with its evaluation and its depth. They come from
Lichess's evaluations database (CC0, 22.5 GB and 418 million positions): its size is asked of database.lichess.org
and said before anything is downloaded; *Download and read* reads it while it comes, and keeps only the positions
found (a few megabytes, in the profile), not the file. *Read a file already downloaded…* reads a
`lichess_db_eval.jsonl.zst` from the disk instead. Nothing else leaves the machine than the request for the file.
The reading shows how much is read and kept; *Cancel*, a network cut, or the program closed keeps what was read, and
*Go on* goes on from there (a file changed on the server since is read again from its start).

Once read, the form takes the **evaluation** kept, for White (+0.30 to +0.80 at first), the **depth** at least (30), the
**plies** from the start (8 to 16), *One position per named variation* (the one on the variation's line first, then
the deepest evaluation, then the nearest the window's middle), and the **number** of positions, spread evenly over
the ECO codes when more qualify. It says how many positions and openings it would hold and their average evaluation;
*Make the book…* writes the EPD file, each position with its move number, its evaluation for the side to move and its
depth (`ce`, `acd`) and its opening (`c0`), and adds it to the library. *Read again* reads the database again for its
newer evaluations.

### CTG books

**Add…** also takes a book in the CTG format, the opening books of ChessBase and Fritz: a `.ctg` with its `.ctb` and
`.cto` beside it, any of the three chosen. It is only read, never written. Its page says how many positions and moves
it holds, and how many moves its author marked green (to play) and red (to avoid). For each position the book has
its moves, each with its symbol (!, ?, !?, only move…), its colour, the evaluation of the position it leads to, and
the games behind it: how many, how they ended, the players' ratings.

*Open it in the tree* shows it in the Board page's *Tree* tab, among the sources: the moves of the position shown with
their games, score, average rating of the players who played them and performance, and a *Book* column with their
colour and symbol. Green moves come first; red ones stay in the list, greyed. The years and the reference games come
from the databases, not from the book.

fastchess reads no CTG book. *For engines: make a Polyglot book…* writes a Polyglot copy (`.bin`) and adds it to the
library, for the setup's *Openings* and the participants' own books: its red moves are left out, and each move weighs
the points its games scored (two a win, one a draw), twice that for a green move; a move that scored nothing is left
out. *Check the book* follows its lines from the starting position and says how many positions it reached of those
the book holds, and whether any move could not be read. Nothing leaves the machine.

## Board

The Board page shows one game or one position, from anywhere: **A** on any board of the program (a game of the
*Live* tab's mosaic or the one followed, the board of the *Games* tab, of the *Openings* tab, of an opening of a
book) opens its position here and analyses it; on the Board page, A starts and stops the analysis. *Back to the
tournament* (or *Back*) returns to the page it came from. Its board can be drawn in 3D: see
[The 3D board](#the-3d-board).

The board is on the left, with the players above and below it; on the right, from the top, the analysis, the moves
(with the table of the moves, the review of the game and the games of the file in tabs), and the game graph. The
page's subtitle tells what the game is: its event and round, its result, its time control, its ECO code and opening,
and how it ended.

A move is played on the board with the mouse: a click on the piece then one on its square, or the piece dragged there.
While it is dragged, it follows the pointer and the square it would land on is tinted; let go anywhere else, it goes
back, still picked up, to be moved by a click. On the 3D board, a translucent ghost of the piece follows the pointer,
floating above the board and passing behind the pieces in front of it, the image staying as it is; the piece itself
moves when it is let go.

### Opening, pasting and setting up a position

- **Open a PGN…** reads any PGN file (a file can also be dropped on the window), in the background with a progress
  bar: a file of millions of games takes a few seconds, and its games are listed in a tab beside the moves, filtered
  by what is typed (every word found in the players, the event, the site, the date, the round, the opening, the
  ECO code or the result). A click shows a game. The evaluations of its moves draw the graph: the cutechess and
  fastchess comments (`{+0.35/18 1.2s}`) and the `[%eval 0.35]` of lichess and ChessBase (from White's side, `#-3`
  for a mate) are read, the clocks of `[%clk 0:01:23]` too. Its variations are kept (see
  [Variations](#variations)). A game whose FEN tag cannot be played is refused, and the moves of a game stop at the
  first that cannot be read.
- **Paste** (Ctrl+V on the page) reads the clipboard: a FEN, or the four fields of an EPD line; a PGN of one game or
  more (listed like a file's); a list of moves from the initial position, in SAN with or without numbers
  (`1. e4 e5 2. Nf3`) or in UCI (`e2e4 e7e5`). A position that cannot be played is refused with the reason (a king
  missing, a pawn on the first rank, the side not to move in check). Castling rights whose king or rook is not on
  its usual square, or written as files (`HAha`), make it a Chess960 position. A `.fen` or `.epd` file can be dropped
  too.
- **Edit the position** sets up a position on the board: a click puts the piece chosen (a second click takes it
  away), a right click takes it away; the side to move, the move number, the castling rights (offered only when the
  king and the rook are where they need to be), the en passant square (only behind a pawn that just moved two
  squares) and Chess960. The FEN below follows the board, and a FEN typed there sets it. *Done* is offered once the
  position can be played, and puts it on the board.
- *Copy PGN* (at the right of the moves' tabs) copies the game shown, with its tags, its moves (the engines' scores
  as comments when it has them) and its variations. Ctrl+S, or *Save the game as PGN…* in the command palette,
  writes the same to a file. The comments, symbols ($1 or !?), arrows and coloured squares, clocks and evaluations
  of an opened PGN file are kept, and written back as they were read.
- **The board**: the buttons under it and ← → Home End go through the moves, and F (or the last button) turns it
  over; a click on a piece and on a square plays a move (a pawn that promotes asks for its piece). *Copy FEN* copies
  the position.

### The players

Above and below the board, when the game has players: each one's name, its engine's evaluation of its last move
(White's score and the depth, as a chip), the move it played and the time it took, or *to move*, and at the right its
clock after its last move, framed while it has the move. The clocks are those of the moves' `[%clk]` when the game
has them; otherwise they are rebuilt from its `TimeControl` tag (`300+2`, `10+0.1`: seconds and increment) and the
time of each move, book moves taking none.

### The analysis

An engine of the library searches the position shown and follows the board. The card shows its engine, then one
button with its *Threads*, *Hash* (0: the engine's own) and *Lines* (MultiPV), which a click opens: the analysis's
own settings, kept for the next time, which change neither the engine of the library nor any tournament (the
library's other option values are sent as a game sends them). At the right, the threads the Board page uses out of the
machine's, in the warning colour while a tournament runs (its tooltip says how many threads the tournament's games
take: the analysis competes with them).

The search shows as tiles (depth and seldepth, nodes, speed, time, tablebase hits, how full the hash is), the chances
of a win, draw and loss when the engine gives them (`UCI_ShowWDL`), and each line: its score (White's, the best one
set apart), its depth and its moves on two lines, with figurines (see [Appearance](#appearance)). Hovering a move of a
line shows the position after it on the board, framed; a click plays the line up to that move. The first move of each
line is drawn as an arrow, the best one darker (**Settings** > *Appearance* > *The analysis's arrows on the Board page*
turns them off; the lines still show the moves).

**Threat** (X) has another process of the engine search the position with the turn passed: the opponent's best move,
were it to move again, drawn as a red arrow and written under the lines (none while the side to move is in check).

The analysis waits while another page is shown, and goes on when the Board page comes back; *Stop the analysis* (or
A) stops it, and a dot on the rail's *Board* button tells when it runs. Its engine is not one of the tournament's
processes: the end of a tournament does not end it. Stopped, the engine stays started for the next search, with its
hash table (and a network engine's GPU memory): the power button beside the engine, *Unload the engine* (also in the
command palette), ends it and the threat's.

### Analysing a whole game

**Analyse the game** (G, in the page's tools or in the graph's header) has the analysis engine (its threads and hash,
one line) search every position of the game in the background, first for 20 ms each, then again four times longer
at each pass (80 ms, 0.3 s, 1.3 s, 5 s…), up to the time chosen with the arrow beside the button (0.3 s, 1.3 s, 5 s,
20 s per position, or *until stopped*): the graph comes in a second or two and sharpens at each pass. The positions
go from the last to the first, which lets the engine's hash hold what comes after a position (measured: about 16 %
closer to long searches than from the first to the last, for the same time), the position on the board first. The
graph's header tells the pass, the time per position and the progress, and a thin band under its time strip what each
position got; *Stop* (or G) stops it.

Each move is judged by the winning chances its mover lost, from the analysis's scores before and after it (lichess'
measure: 50 % at 0.00, about 59 % at +1.00): 10 points lost is an inaccuracy (`?!`), 20 a mistake (`?`), 30 a blunder
(`??`), marked after the move, on the graph and on the board; the move the engine would have played is never marked.
A move is judged on the last complete pass; a mark the last pass changed is hollow until the next one confirms it.
N and Shift+N (or the buttons beside the navigation) go to the next and the previous marked move.

Each complete pass is kept with the game (in `analyses/` in the profile): the same game, opened again from anywhere,
shows it at once, and G goes on from the passes done. The analysis runs on its own thread with its own engine
process, its threads counted in the core budget; the board's analysis (A) pauses it, and when A searches a position
of the game longer than the pass did, its value takes that position's place.

**Several analyses**: *+ Engine* in the graph's legend has another engine of the library analyse the game too, for as
long at each pass. Each analysis has its colour on the graph and on the board (its best move as an arrow while the
board's analysis is off), its line in *This move* and its accuracies in the review. They go pass by pass together, all
at once when the cores allow, one after the other otherwise. One analysis judges the moves (the first; a right click on
another in the legend makes it the judge, or removes it); a mark is hollow when the analyses do not all judge the move
alike. Win chances compare engines better than centipawns, which differ from one engine to another: they take each
engine's WDL when it gives one.

### The game graph

Under the moves, the graph of the game, the same as in *Live* and *Games*:

- **Evaluation**: the judging analysis as a white and black area (White's advantage above the line, Black's below),
  each engine that played as a curve of its colour at its own moves, the other analyses as dashed curves; the marked
  moves on the curve; values beyond ±6 drawn along the edge in the warning colour. Without an analysis, the area is
  the two engines' evaluations averaged.
- The book moves hatched, the phases under the axis (the opening; the middlegame from the first position with at most
  10 pieces besides the kings and pawns, or fewer than 4 on a back rank; the endgame from at most 6, as lichess
  divides a game), the zone the tablebases know tinted, the move numbers along the bottom.
- Under the axis, the time of each move: White's up, Black's down.

Its tabs: *Win chances* (from the WDL when the engines give it, lichess' formula otherwise), *WDL* (the shares the
engines reported, White's wins at the bottom), *Time* (with the decision time), *Depth* (with the seldepth), *Nodes*,
*Speed*, *TB hits*, *Agreement* (how far apart the two players' evaluations are, each move against the one before,
and the analyses' at each position) and *PV agreement* (how many plies of an engine's PV repeat what the other's
predicted); those the game has no values for are left out, and those that do not fit go in the `⋯` menu. Hovering
tells the move and what each engine said; a click or a drag goes to the move. A click on a curve in the legend hides
or shows it; the arrow at the right folds the graph.

### This move

Under the moves, about the move shown: what the engine that played it said (its score, depth and seldepth, time,
nodes, speed, tablebase hits, as the PGN has them), the line it expected after it, each analysis's verdict (the share
of the winning chances lost, the judgement, the best move and its score), and with the tablebases the result after the
move and whether the move kept the one before it. *Show* lays a line on the board without touching the game: the
position after it, the board framed, what it is written over it; Esc, a click on the board or a move through the game
brings the game back. On the board, the move's judgement is a badge on its square, and the reply its engine expected
a dashed arrow.

### Variations

A move played on the board where the game went another way starts a variation: the game stays as it came, with its
analyses and its graph. The moves show the variations in brackets after the move they replace, nested and numbered; a
click on a move goes there, and *Back to the game* returns to the position the variation leaves. A right click on a
variation's move promotes it (it becomes the line it branches from, whose moves after it become the variation),
deletes it with those branching from it, or copies it. *Copy PGN* writes the variations, and those of an opened PGN
file are read, nested to any depth, with their comments and symbols (a move that cannot be played ends its variation
only). The graph, the analyses and the judgements are the game's; on a variation, the graph marks where it leaves the
game.

### The moves, the table and the review

**Moves** writes the game as flowing text: figurines (see [Appearance](#appearance)), the book moves greyed and the
end of the book marked, the judgements in colour, the current move framed; hovering a move tells what its engine said
and its judgement. **Table** lists each move with its engine's score (White's), depth and time, a bar for the time,
the marked moves coloured.

**Review**, once the game is analysed: for each side its accuracy (lichess': each move's from the winning chances it
lost, the moves of the sharp moments weighing more), its inaccuracies, mistakes and blunders, its average loss in
centipawns, the share of its moves the analysis would also play, its engine's average depth and time; its accuracy in
the opening, the middlegame and the endgame; with several analyses, the accuracies each one gives. Then the key
moments, each a click away: the turning point (the move that lost the most, with the best move), the other mistakes
and blunders, where the two engines disagreed by 1.5 pawns or more for two moves in a row, the start of the endgame
and of the tablebases. *Who saw it first* tells, for the turning point, at which move each engine that played and
each analysis first gave the side that gains a pawn or more. *Copy PGN with the analysis* writes the game with each
position's evaluation (`[%eval]`), the judgements as NAGs and, after a marked move, the line the analysis preferred;
*Copy as BBCode* gives the review for a forum.

### Tablebases on the Board page

With the Syzygy folders of the setup (*Adjudication* > tablebases), the Board page probes each position of the game:
the graph tints the zone the tables know, *This move* tells the result after the move and whether the move kept the
one before it or spoiled it, and the review marks where the tables take over. Each position is probed as if a capture
or a pawn move had just been played, which is exact except where the 50-move rule is near. The tables are those of the
program's process: they are not probed while a tournament plays in it.

### Playing a game from a position

**Play a game…** plays a game from the position on the board between two engines of the library, or between you and one
(see *Human* below) (*White* and *Black*, `<->` swaps them with their clocks), with a clock for each (minutes and an
increment in seconds; *The same for both*, or one each for time odds), and, if ticked, a second game with the colours
reversed. The engines are started with the library's option values, a WinBoard engine through the adapter. The board
follows the game as it is played (going back in it stops following until its end is reached again, or *Back to the
current move* is clicked), with the clocks, the side to move's search and *Stop*, which ends the game without a result.
A game ends by mate, stalemate, insufficient material, the 50-move rule, a threefold repetition, an illegal move, an
engine that stops, or a loss on time (the clock given 0.1 s for the move to come through). Each game is appended, as it
ends, to a PGN file in `games/` in the profile (named after the date and the engines, with a `Termination` tag), which
*Open the games* lists on the board.

The games are played by RookStudio's own engine client, apart from any tournament: they run while one plays,
without touching it. Their threads (the larger of the two engines', as they take turns) count in the core budget;
when the running tournaments' games and the analysis already fill the machine, the card says so and offers *Play
anyway* instead of *Play*. The analysis stops when a game starts, as it would compete with its engines.

**Human**, first in the lists of *White* and *Black*, is you: you play that side yourself, your moves made on the board
with the mouse, on the flat board as on the 3D one (a pawn reaching the last rank asks for its piece). The board turns
so that your side is at the bottom at each game (*Flip* still turns it), takes your pieces only at your turn (the card
says *Your move*), and a click while you look at an earlier move brings it back to the game. Your clock runs as an
engine's would, a loss on time included: give yourself more time than the engine by unticking *The same for both*. While
you play, what the engine thinks is kept back (its score, depth and line, so the evaluation bar and the graph's curve
too); the finished game has it, ready for *Analyse the game*. *Resign the game* ends it at any moment, won by the
engine; with a second game, it follows, your colour reversed. Your games are saved under your login name, and only the
engine counts in the core budget. There is no takeback, and the engine plays at its full strength unless its card sets a
lower one (`UCI_LimitStrength` and `UCI_Elo`, or `Skill Level`, where it has them).

In a view for study, the card leads to the Play page too: a game of yours against an engine at the strength you choose,
with its openings, a handicap, takebacks and hints, its aids hidden until the end (see [The Play page](#the-play-page)).

### The study Board

In a view whose Board is *For study* (see [Views](#views)), the Board page is where games are read and written. The
board is on the left; on the right, the game's strip, then six tabs, Alt+1 to Alt+6:

- **Notation**: the game's title line and its headers (see [Headers](#headers)), then the moves, their comments,
  symbols and variations, written in place (see [Notation](#notation)); the table of the moves and the review are
  in tabs of their own (see [The review of a game](#the-review-of-a-game));
- **Analysis**: up to four engines side by side (see [Several engines side by side](#several-engines-side-by-side)),
  and Lichess's cloud and endgame tables when you chose them (see [Online for the analysis](#online-for-the-analysis)),
  with the game graph;
- **Tree**: the moves played from the position in your databases, your books and on Lichess (see [The Tree
  tab](#the-tree-tab));
- **References**: the strongest games of the position (see [Reference games](#reference-games));
- **Questions**: the assistant (see [Questions](#questions));
- **Endgames**: what the endgame tables say of the position, once seven pieces or fewer are left (see [The Endgames
  tab](#the-endgames-tab)).

The strip above the tabs tells where the game comes from and what became of it: *Save* (Ctrl+S) with its state
(*Changed: 2 changes*, then *Saved in “Mega”*), the game's place in the list it came from with the previous
and the next game (Ctrl+← and Ctrl+→), and *Recent* (see [Recent games and the list](#recent-games-and-the-list)).
With an empty library, the Analysis, Questions and Review tabs offer to download Gaia in a click. *Export…*, under the
board, makes the game a page to replay, a diagram, an animated GIF or a PDF; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd>
copies the diagram of the position (see [Exporting and printing](#exporting-and-printing)).

A move is typed as well as played: its first letter opens a field above the tabs, which offers the legal moves that
begin so, in the piece letters of the interface's language: K Q R B N in English and in Chinese, R D T F C in French,
K D T L S in German, R D T A C in Spanish and in Italian, R D T B C in Brazilian Portuguese, Кр Ф Л С К in Russian.
English letters are always read too: `Ng` offers the same moves as the German `Sg`. A letter with two meanings offers
both: in French, Spanish, Italian and Portuguese, R gives the king's moves first, then the rook's, marked *rook
(English)*; in Russian, К gives the knight's moves and the king's, Кр the king's only. The squares the moves offered go
to are dotted on the board, the first one's arrow drawn: ↑ and ↓ choose another, Enter plays the one chosen, a click
plays any of them, and `0-0` is castling too. The same field takes the moves of the Training page's exercises (the
puzzles, your mistakes, guessing the moves, the endgames) and of the games of the Play page. The interface writes the
moves in the same letters (the notation when *Figurines in the notation* is off, the engines' lines, the review); the
PGN keeps English SAN, as every program reads it, and the assistant's comments keep the letters of their own language.
The letters of the engines' Board are then under Alt: Alt+F turns the board over, Alt+A starts or stops the analysis,
Alt+X shows the threat, Alt+G analyses the game, Alt+N and Alt+Shift+N go to the next and the previous move the
analysis marked.

### Several engines side by side

In the study Board's Analysis tab, up to four engines of the library analyse the position together, each in a card: its
colour and name, its evaluation (White's), its depth and seldepth, its speed and its threads, its best line, and its
other lines folded into one (a click unfolds them). Hovering a move of a line shows the position after it on the board;
a click plays the line up to that move. **+ An engine** lists the library's engines, those of the analysis ticked (↑ ↓
pick one, Enter adds it), and *Download an engine…*, the popular engines of the Engines page; an engine added searches
at once. Alt+A, or *Analyse*, starts and stops them all, and the engines are kept for the next time.

On the board, each engine's best move is an arrow of its colour: dark blue, light orange, teal and grey, from the first
engine to the fourth, their lightness apart too. Engines that play the same move share one arrow, in the colour of the
first of them, with their number beside its start. The line at the top of the tab says the same in words: which engines
agree, on which move, and how far apart their evaluations are (*7…Be7 for Stockfish and Gaia · 7…Qb6 for Leela · their
evaluations within 0.11*), or the mate an engine sees; no colour has to be told apart. With one engine, the arrows are
its lines', as on the engines' Board.

**Threads**: the analysis has one number of threads, set from the chip at the top (*24 / 32 threads*), shared equally
between its engines. An engine's menu (`…`) sets its lines, its hash and, by hand, its threads, which the others' share
then leaves aside. An engine that searches on a graphics card (*Uses a GPU* ticked in the library, or an engine like Lc0
whose *Backend* option names a GPU backend) keeps its own threads, and the share does not count them. A tournament
running on this computer keeps its cores: the analysis takes at most what its games leave, and the chip says how many
they take. These settings are the analysis's own: neither the library's engine nor a tournament sees them.

**The first engine** is the analysis's: the game's review (Alt+G), the threat (Alt+X, or the first engine's menu), the
questions and the assistant's comments use it. An engine dragged by its name onto another takes its place, to the top it
becomes the first; *Make it first* in its menu does the same. When the game has an analysis by the new first engine,
that analysis judges the moves and draws the curve of the game graph. Each engine's evaluations of the positions of the
game it searched show on the graph as a thin line of its colour (*live analysis*), the first engine's as the curve while
the game has no analysis.

While one of your games is played on Lichess, every engine stops at once, and the study Board shows none of their arrows
(see [Fair play](#fair-play)). *Unload the analysis's engines*, in an engine's menu or the command palette, ends their
processes. Every gesture of the tab is in the command palette (Ctrl+K): an engine added, made first or removed, its
settings, the analysis's threads.

### Practical moves

Under the engines, *Against a human of 1500, rapid* (the level and the speed kept from one time to the next) looks for
the practical move: among the moves the first engine puts within 0.3 pawn of its best (or, once the tables know the
position, those that keep their verdict), the one that scores best against a player of that level, in the games of that
level. With a linked Lichess account, the games are those of Lichess's explorer at the level's rating group and speed
(*Lichess 1400–1600, rapid*); without one, those of your databases whose players' average rating is in the level's band,
whatever their speed. A move's score counts from 200 games at the level; outside known games, the practical move is the
one that leaves the opponent the fewest good replies, by the engine: an engine of its own, on one thread, searches each
candidate's replies, and the table says how many hold (*2 of 31*). Never a losing move.

The table gives each candidate, its evaluation by the engine (White's), its score against that level and its games
there, the engine's best marked *best* and the practical move *practical*; on the board, the practical move is the blue
arrow, the engine's best the grey one. A sentence sums it up, held to the figures: *The practical move: 4.Ng5 — 0.13
from the engine's best, and 58 % expected at this level.* When the games show it, it goes on with the level's most
played reply along the line and what punishes it, the engine's answer to it: *After 4…d5 5.exd5, the most played reply
at 1500 is 5…Nxd5? (46 % for Black), which 6.Nxf7 punishes.* The practical moves need the analysis running (Alt+A).
What leaves the machine: with a linked account, the position and the level, to Lichess's explorer; without one,
nothing. Locked with the Analysis tab while one of your games is played on Lichess.

### Notation

The notation is written in place, without a dialog:

- **Comments**: Enter writes a comment after the move shown, Shift+Enter one before it; Enter keeps it, Escape leaves
  it. A click on a comment goes to its move, a double click corrects it.
- **Symbols**: `!` and `?` typed put the move's symbol, and a second sign right after makes the pair (`!!`, `??`,
  `!?`, `?!`). The buttons under the notation put the others: only move, novelty, and the evaluation of the position
  (=, ∞, and White's or Black's advantage, slight, clear or winning). A symbol put again is taken away.
- **Variations**: a move other than the game's, played on the board or typed, starts a variation. Held while the move
  is made, Shift replaces the moves after it, and Ctrl asks: a variation, a new main line, or the moves after replaced
  (Escape plays nothing). *Ask what to do with a move that leaves the game* (*Settings* > *Appearance*) asks each
  time.
- **Delete** deletes the moves from the one shown; **Alt+↑** moves the variation up, to the main line at last.
- **Ctrl+Z** undoes each change (a move, a comment, a symbol, a mark, a header) and **Ctrl+Y** makes it again; a line
  under the notation says what was done and how to undo it.

*Figurines in the notation* (*Settings* > *Appearance*) shows the pieces as figures. The assistant's comments show
apart from yours, in blue grey and in italics, after a bubble, or in a box under its name after a comment that was
there already (see [Comments by the assistant](#comments-by-the-assistant)). When your theory studies comment the
position shown, *Theory · 3* above the moves leads to their notes in the Tree tab (see [Theory
studies](#theory-studies)).

**A tournament's game**, in either style of its PGN (fastchess's `{+0.35/18 1.234s, n=…}` or the ChessBase style's
`[%eval]`, `[%clk]` and `[%emt]`), whether opened from its file, from a database that imported it or from a tournament
read as a database, shows its engines' comments condensed: beside each move, in small grey, the evaluation its
engine gave (for White, as the bar and the curve) and its depth, *+1.44/22*, the book's end marked. Hovering a move
tells the rest: its engine and side, the evaluation from its side too, the depth and the selective depth, the time
and when it settled on the move, the nodes and the speed, the tablebase hits, its chances of a win, a draw and a loss,
and the line it expected after it, written in moves. The right button on the move shows that line on the board, makes
it a variation from where the game left it (Ctrl+Z takes it away), or copies the engine's comment as it is. The line
under the moves chooses what shows beside them: the evaluation and the depth, the time too, or nothing (Ctrl+K as
well). The PGN does not change: Ctrl+S and *Copy PGN* write each engine's comment back as it was read, every field
kept, as long as the move's data is the same.

### Arrows and squares

The right button draws on the study Board: a click colours a square, a drag from one square to another draws an arrow,
and the same again takes it away. Green by default, red with Shift, blue with Alt, yellow with both. A left click on an
empty square clears the marks of the move. The marks belong to the move shown and are saved with the game (as `[%cal]`
and `[%csl]` in the PGN). On the 3D board, the right button draws on the board, and turns the camera when dragged beside
it; the middle button turns it too.

### Headers

The title line at the top of the notation gives the players, their Elo, the event, the round, the date and the result; a
click on it, or Alt+H, unfolds the headers in place. Tab goes to the next field and completes a name from those of the
database; a date that is not one is pointed out (`YYYY.MM.DD`, with `??` for what is not known); the result is chosen
with a click; the ECO code is offered from the moves (Lichess's opening names) and written by itself at saving when it
is missing. The other headers show as the game has them, and one more is added by its name. Each field changed is a
change (Ctrl+Z); Enter folds the headers, Escape gives the field its value back.

### Saving a game

Ctrl+S saves the game where it belongs: a game opened from a database goes back into it, under its number; a game of
a PGN file goes back into the file, at its place (the file is written again at once, its other games as they are).
Any other game opens a menu under *Save*, where Enter takes the first line: one of your databases (the one of last
time first), a new PGN file, or the end of a PGN file. Ctrl+Shift+S saves a copy of any game elsewhere. A game without
players opens its headers first.

A database that another program writes to is read-only meanwhile: the game stays changed here until it can be saved.

### Positions and games to Lichess

**lichess.org…**, under the board, sends the position or the game to Lichess; each action says what it sends.
*Open the position on lichess.org* opens Lichess's analysis board on it in the browser: the position goes in the
address, nothing else. *Share the game* imports it on Lichess without an account and copies its address: whoever has
the address sees the game, its players' names too. *Have the position played out*, with a time control (3+2 to 30+20,
or days a move), makes an open challenge from the position: a link for each side, to send to its player; such a game
is not rated. *Bookmark the game on Lichess*, for a game that comes from there, puts it in your bookmarks: it sends
the game's id with your token, and asks Lichess once for the permission to change your preferences. *To a Lichess
study…* sends the game into one of your studies or a new one (see [Your Lichess studies](#your-lichess-studies)).

While you play a game on Lichess, none of them touches its line: a position on the way to its position, or the game
that passes through it, does not leave the machine (see [Fair play](#fair-play)). The rest stays possible.

### Recent games and the list

Ctrl+Tab shows the recent games, the one on the board first (twelve at most): Ctrl+Tab again goes down the list,
Ctrl+Shift+Tab up, and letting Ctrl go opens the game chosen, with its list, its file, its changes and its undo
history. A game with changes not saved is kept as it is until you come back to it. *Recent*, on the strip, lists them
too: a click opens one, and the right button on a changed one discards its changes (Ctrl+Y makes them again).

A game opened from a database's list keeps its place in it: Ctrl+← and Ctrl+→ open the previous and the next game of
the list, or of its filter, at the same move when the game passes through the position shown.

### The Tree tab

The Tree tab gives the moves played from the position shown: their games, the shares of White's wins, draws and
Black's wins, the score, the average Elo, the performance of the side to move (from FIDE's table) and the last year
the move was played. Its sources are chips above the moves:

- **My databases**: your databases added together, move by move (the right button chooses which; each one's share
  shows at the cursor over the games);
- each database whose opening tree is built, and each Polyglot book of the library;
- **Masters (Lichess)**, **Lichess** (the right button chooses its speeds and ratings) and the player whose games
  you imported: Lichess's opening explorer, which needs a linked account (see [Linking your
  account](#linking-your-account)).

The move under the mouse, or chosen with ↑ ↓, shows as a blue arrow on the board; a click or Enter plays it, and `*`
or its star marks it in the active repertoire (see [The Openings page](#the-openings-page)). When the game's line
leaves the database, the tab says at which move, and whether that move is a **novelty**, as a position search
confirms (*See the last known position*). A database's tree is built after each import, and again in the background
after a game is saved into it, its progress shown with *Cancel*. Your theory studies' notes come with the moves,
whatever the source: a *Theory* column, and *What your studies say* under the moves (see [Theory
studies](#theory-studies)). *Report on this position*, at the right of the games, opens its opening report (see [The
opening report](#the-opening-report)).

### Reference games

The References tab looks for the strongest games of the position in your databases, in the background: the
strongest first (by the players' mean rating), then the newest; a click opens one. Early in the opening, too many
games reach the position to look for the strongest: the Tree tab tells more. *All the games of the position in “…”*
lists them in the database.

### The Endgames tab

The sixth tab of the study Board, Alt+6, says what the endgame tables say of the position shown, once seven pieces or
fewer are left, or eight with a pawn of each side on one file (Lichess's op1 tables); dimmed beyond, it says so when
chosen. At the top, the verdict (*White wins*, *Draw*, *Won for White, but drawn by the fifty-move rule*), where it
comes from and how far the end is: the moves to mate up to six pieces (DTM, as other programs call it), at seven the
moves to a capture or a pawn move (DTZ), at eight the moves to the next capture, promotion or mate (DTC). Then the moves, grouped by
the outcome they leave the side that plays them, the best first: *Keep the win*, *Keep the draw*, *Throw the win away: a
draw*, *Lose*… A move that spoils the result is said with why: the answer that refutes it and what the tables then say
(*→ 52…Kxh7 · Black wins: mate in 19 moves*). A move hovered shows its main line on the board; a click plays it, a
variation when the game went another way, as on the board. When the game goes on from the position, the assistant says
what its move did, taught by the tables (*52.Rxh7 loses: after 52…Kxh7, the tables give Black mate in 19 moves. 52.g5
kept the theoretical draw (7-piece tables).*), written from their verdicts alone: it says so only when they do.

Your tables on the disk answer first (Syzygy, those the tournaments' adjudication and the Training page's endgames use:
their verdict and the moves to a capture or a pawn move, no distance to mate); Lichess's answer the positions they lack,
once allowed. The first time, the tab asks: for this position, always, or not now (*Always* can be changed on the Lichess
page, under *Online for the analysis*). Only the position's FEN goes to tablebase.lichess.ovh, without an account, one
request at a time: the position you look at, and the main lines of its moves, those that spoil the result first and the
others when hovered. Each answer is kept in your profile and never asked again. The Analysis tab keeps the verdict and
its distance, with a link to this tab. The tab is locked while one of your games is played on Lichess, as the Analysis
tab is, and hidden during a game against an engine until its end.

### The explorer, more

With Lichess's sources in the Tree tab, **the games the explorer names** come under the moves: the masters' games
reaching the position (*Masters' games*), or the strong ones played on Lichess; a click asks Lichess for the game and
opens it on the board, at the position the tree showed.

With the *Lichess* source (all its players), *Each move month by month* asks Lichess, for each move listed, how many games played it month
by month: a line of its last two years in the *By month* column, green when it is played more lately, red when less;
under the mouse, its games in the last six months against the six before. The summary says the same of the position.
It costs a request a move, so it stays off until asked for; the masters' explorer has no months.

### The opening report

The **opening report** of a position puts together what your databases know of it. It opens from the Tree tab's
*Report on this position*, from a line of the Openings page (the right button on a gap: *Opening report*) and from
Ctrl+K, *Opening report*; it shows in place of the Board page, Esc going back to the position. It is made again each
time it opens, in the background at a low priority, section by section with its progress and *Cancel*: a fraction of a
second for most positions, about a second for one of millions of games.

At its top: the position, its opening (Lichess's named openings) and the moves that lead there; then its sources, those
of the Tree tab: your databases whose opening tree is built (those *My databases* adds together), with their games;
**Masters (Lichess)** and **Lichess** with the Tree tab's speeds and ratings, ticked when the Tree tab shows them, or by
a click (they ask Lichess's explorer for the position, as the Tree tab does, and need a linked account); and where the
evaluations come from. *To the repertoire* adds the first main line to the active repertoire (Ctrl+Z takes it out);
*Print…* makes the report a PDF, its summary marked as the assistant's, and *HTML page* a page of its own whose main
lines replay on a board (see [Exporting and printing](#exporting-and-printing)). Five sections follow (Ctrl+Page Down and
Ctrl+Page Up go from one to the next):

- **Lines**: the moves played from the position with the Tree tab's figures (games, White's wins, draws and Black's
  wins, score, average Elo, performance, last year) and an evaluation: Lichess's, kept offline (see [Lichess's
  evaluations offline](#lichesss-evaluations-offline)), else the analysis' engine's (a million nodes on one thread, for
  the first five moves without one); its source and depth at the cursor. A click, or ↑ ↓ and Enter, plays the move on
  the Board page. Then the **main lines**: the most played continuations, as long as they keep a fifth of their first
  move's games, with the games that reach their end and White's score; a click shows one on the Board page;
- **Plans and structures**: the pawn structures at move 20 (ten moves on for a later position) in the position's games,
  as the assistant's facts name them, *kept* when the position has one already; and what the players do in the next
  fifteen moves: the pawn pushes (one, or two in a row: *b4 then b5*) and the pieces' routes (as the Manoeuvre search
  follows a piece) played in a tenth of the games or more, three for each side, with their share and White's score.
  A plan the assistant's facts know is named (*The minority attack: b4 then b5*). These are frequencies, not advice;
  they need 50 games, and a position of more than 20,000 games is counted in an even sample of them. A click shows those
  games in the list;
- **Key games**: the strongest (the players' highest mean rating), the most recent of 2500 and more on average, and the
  first game of a novelty repeated since, each with why and the plan it plays; *Open* shows it on the Board page at the
  position (*Back to the report* returns);
- **Novelties**: along the main lines, the moves first played in the last five years of your databases and repeated
  since, with their first year, how often they were repeated and their score; and your own game that left your
  databases there (your names are those of *Settings › Your names*). A move's first year comes from the trees, which
  hold every game that reaches its position in their first 60 plies; your game is confirmed by a search of the position, as
  the Tree tab confirms a novelty;
- **Summary**: the assistant's summary of the report, written by its model from the report's figures and facts alone
  (see [The report's summary](#the-reports-summary)).

A position fewer than 50 games reach gives its moves and games, without plans nor structures (*Add Lichess's players as
a source*). Without a database whose tree covers its games, the report is made of Lichess's masters, with *Rebuild the
tree of …* and *Import a database*. Offline, your databases and the evaluations kept offline answer, and Lichess's
sources show the last figures the report kept for the position, dated. With neither an engine nor Lichess's
evaluations kept offline, the evaluations stay empty and the summary says nothing of them (*Download Gaia…*). While one
of your games is played live on Lichess, the report is locked, and comes back by itself at its end; on the position of a
correspondence game, the lines and figures of your databases stay (Lichess allows a book and a database), the
evaluations, Lichess's explorer and the summary wait. Nothing leaves the machine but what the Tree tab sends: the
Lichess sources ticked. Printing the report and its HTML page are for later.

### The report's summary

The summary is written in passages: the pawn structure, each side's plans, the moves played, the main line, the
evaluations. The assistant's model writes each one from that passage's figures alone, held by a grammar that lets it
name only the position's moves, the report's lines, their squares and figures; then the checker reads each sentence: a
figure must be one the report gives of what the sentence names, a move one of the position or of its lines, a plan or
a structure one the report has and of the side it says, and it gives no advice. A passage refused is written again
once; refused twice, the templates' passage stands, and the cursor over it says why. *What it rests on* lists the
figures, facts and evaluations used; a card says what the checker refused. *The templates' version* shows the
templates' summary, *Rewrite* has the model write it otherwise, *Copy* copies it. Without the model (not downloaded,
another GGUF file, or while a tournament on this machine has the processor), the templates write it from the same
figures, in English or French. On the processor, a summary takes a few seconds a passage.

### The review of a game

On the study Board, a tournament's game shows at the top of the *Review* tab what its two engines thought, as soon
as it is opened and without any analysis: White's chances along the game according to each engine at its own moves
(from the share of wins, draws and losses it gave, turned around for Black, else from its evaluation converted as the
review does), the book shaded, the stretches where the two engines saw different games tinted; under it, the time of
each move, White's above the line and Black's below; each engine's time, average depth and speed. Then its moments,
each a click away: how each engine saw the game out of the book, where they disagreed, where the tablebases entered
a search, where a mate was seen, and how the game ended. Once the game is analysed, the review's curve is drawn over
theirs, thicker.

On the study Board, the *Review* tab beside the notation reads the game's analysis (Alt+G) as a coach would:

- at the top, White's chances along the game, with the key moments as dots (dark for a good move, orange for a
  mistake); a click goes to the move;
- the game's theory in one of your databases (the Tree tab's, else the first): its book moves are not judged, and
  its novelty is named; the book stops before the first error the engine sees. The moves a tournament's opening book
  played (`{book}` in the PGN) are not judged either;
- **Key moments**: the moves of the dots, each with its verdict (a mistake with the chances it lost and the better
  move, an only move with the engine's next best: *Only move. Gaia: +0.62, the next +0.31.*), and where the endgame
  starts;
- **The key moments, in words**, **The story of the game**, and **What to work on**, for each side, with the moves
  that show it; a click goes to the move.

The same game always gets the same review: the same scores, symbols, key moments and comments, whatever the machine is
doing. The study Board's analysis of the game counts nodes rather than time: each position is searched for 20,000 nodes
at the first pass, four times more at each pass, up to the choice beside the button (320,000, 1,280,000, 5,120,000 or
20,480,000 nodes per position, or *until stopped*), on one thread and from an empty hash, so that a position's result
depends on the engine and the position alone. Several positions are searched at once, an engine process each: as many as
the analysis has threads, or half the processor's cores up to four when that is more; fewer while a tournament plays on
this machine, which keeps its cores. Where the move played was the engine's choice and the position is not one-sided, a
second search of the position with two lines gives the next best move: an only move is told with it.

The endgame's positions of seven pieces or fewer can be judged exactly by Lichess's tables, asked once for this game
or always. *Comment the key moments* writes a comment by the assistant on each, in the background, beside the comment a
move may have already (Lichess's analysis, yours, an annotator's), which is never replaced; *Annotate the game* writes
the evaluations, the symbols and the better lines into the notation. Ctrl+Z takes either back.

**The story of the game** is written by the assistant's model when it is there, in the background, from the review's
facts: its verdicts and evaluations, the key moments, the opening, the endgame and how the game ended, with what
Lichess's tables said. Each passage is held to those facts by the checker of the comments: every move, evaluation and
name it quotes must come from the review. A passage refused is written again; refused twice, it gives way to the
template's. A card in the assistant's colour says that the model wrote the story and that it was checked, and what
the checker refused. *The template version* shows the templates' story in its place, *Write it again* has the model tell
it otherwise (Ctrl+K too), *Copy it* copies the story shown. Without the model (not downloaded, or another GGUF file
that has not learnt RookStudio's comments), or while a tournament on this machine has the processor, the story is the
templates', as it says. Nothing leaves the machine.

### Every move explained

*Explain every move*, in the *Review* tab (or Ctrl+K), gives each move of the game a line, in the *Explained* tab beside
the moves; without a review, the game is analysed first (Alt+G), and its moves explained once the review is done. The
lines come in the background, each as soon as it is written, with how many are done and *Cancel*; those written stay,
and *Explain the rest* finishes them later.

- **Theory** is grouped: a line for each opening name its moves reach, three at most, with the games of your databases
  that reached its last position (*theory, 61,204 games of your databases*).
- **A move too simple for more** is said in a word: *Forced recapture.*, *The only legal move.*
- **The others** get a sentence of the assistant's model, written from the facts of the position, its grammar letting
  it name only legal moves, each sentence read by the checker; a sentence the checker refuses is replaced by the
  template's, and hovering *template: the checker refused the model's sentence* says why. Without the model, the
  templates write them; when they have nothing to say, the review's own words do (*The engine preferred 14…Bf5.*).

Each line says where it comes from: theory, the model, a template, a word, the review. *Every move*, *The moves that
matter* (the marked moves, the only moves, the turning points, the novelty) and *The key moments* choose which show; a
click goes to the move. The summary counts them and gives the time a move takes, the facts gathered and the model's
sentence checked: about a quarter of a second for the sentence on a GPU, some 5 seconds a move on a processor shared
with other work (32 moves explained in a minute and a half). They are kept with the review, beside the moves, never in
the game: *Write them into the game* makes them the assistant's comments, marked as its own in the PGN, beside the
comment a move has already, which is never replaced; Ctrl+Z takes them all back. Nothing leaves the machine. Locked
while one of your games is played on Lichess, hidden during a game against an engine until its end.

### The guided visit and “Why not?”

*Guided visit*, in the *Review* tab (or Ctrl+K), takes you through the review's key moments one after the other (its
marked moves when it has none): the board shows the position before the game's move, and asks *Before seeing why: what
would you have played?* Play the move on the board or type it; nothing is shown before your answer, neither the
evaluation nor an arrow nor the game's notation, and Space says *I don't know*. Then your answer is judged as *Guessing
the moves* judges a guess (see [Guessing the moves](#guessing-the-moves)): the engine's move 3 points, a move as good
2, a playable one 1, else 0, by the analysis's engine, a second for each; the board shows your move in blue, the
engine's in green and the game's in orange, with an explanation held to the review's facts and what it rests on (the
review's engine and its line, the facts of the positions). Enter goes to the next moment, *See the line* plays the
engine's line on the board, Escape stops. A wrong answer, no point or *I don't know*, becomes a card of today's review,
as your mistakes do (see [Your mistakes as exercises](#your-mistakes-as-exercises)): the position, the engine's move to
find.

**Why not?** (Alt+Q) asks the assistant's *Compare* (see [Questions](#questions)) with one key, wherever a move shows:
after a move tried on the board (a variation, as always), it sets the move against the game's; on the game's own move,
the review's best against it; the right button on an engine's line in the Analysis tab, or on a move of the Tree tab,
offers *Why not …?*; in the guided visit, *Why not another move?* sets your answer, or the game's move, against the
engine's. The answer comes in the Questions tab (in the visit, under it): the engine after each of the two moves (one
thread, 300,000 nodes a position), the facts of both positions, your databases' games and score, the continuation;
*Add to the notation* writes it in, as for any question. No free conversation: the assistant answers what the engine,
your databases and the facts establish. Both are locked while one of your games is played on Lichess, and hidden during
a game against an engine until its end.

### Exporting and printing

*Export…* under the study Board, beside *Copy FEN* and *lichess.org…*, opens the Export sheet in place of the page;
Escape goes back to it. Its formats are chips at its top, the one chosen last time already chosen
(<kbd>Ctrl</kbd>+<kbd>Page Down</kbd> and <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> go through them); its left shows the file
as it will be, its right what goes into it, and each format keeps its settings for the next time. Enter saves: the
system's dialog opens in the folder chosen last time. Everything is made by RookStudio, on your disk: nothing is sent
anywhere (sharing a game on
lichess.org is *lichess.org…*'s, see [Positions and games to Lichess](#positions-and-games-to-lichess)).

- **HTML page**: one file with everything in it, which opens without the Internet, on a phone too, and can be sent as
  it is. The board in the app's colours and pieces, the moves, their comments and variations; a click on a move shows
  it, in a variation too, with the arrows and squares drawn at that move; <kbd>←</kbd> <kbd>→</kbd> <kbd>Home</kbd>
  <kbd>End</kbd> go through the moves and <kbd>F</kbd> turns the board over. What it holds is ticked: the comments,
  the variations, the arrows and squares, the assistant's comments (marked as its own), the evaluations and their
  curve, the clocks. Its weight is estimated before it is saved. From the Databases page, or from the Board page when
  its game was opened from a database's list, the page holds this game or the filter's games, chosen in a list at its
  top (<kbd>Page Up</kbd>, <kbd>Page Down</kbd>); beyond 200 games it would be too heavy to open, and the sheet asks: a
  page per game in a folder, with their list (`index.html`), or the first 200.
- **Diagram, PNG or SVG**: the position shown, with the arrows and squares drawn at that move (the right button), the
  coordinates, a dot beside the edge of the side to move, Black below when it is Black's move (if ticked), the last move
  (if ticked) and a caption under the board, which starts as *White to play* and is written as you like (*White to play
  and win*). In the app's colours, or in black and white for printing. The PNG is 480 pixels wide, or twice that for a
  dense screen; the SVG has no size and is drawn sharp at any. *Copy the image*, or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+
  <kbd>C</kbd> from anywhere on the Board page (and *Copy the diagram* in the command palette), puts the PNG in the
  clipboard, for a message or a document.
- **Animated GIF**: the game in the app's colours, from one move to another (*From … to …* for a passage), a frame a
  move at the speed chosen, the last held longer; the players' names and the result in a banner, the move under the
  board, the last move marked and the game's arrows, as ticked. It is made in the background, with its progress and
  *Cancel*, and its weight estimated before: above 2 MB, the sheet says it is heavy for a message or a forum, and offers
  a passage or a smaller size.
- **Print · PDF**: a PDF made by RookStudio, the same on every system, its preview the exact page. *Open to print*
  (Enter, <kbd>Ctrl</kbd>+<kbd>P</kbd>) gives it to the system's reader, which prints; *Save the PDF…* keeps it where
  you choose. The paper is A4 or Letter, as the system's language and country have it, or as chosen. A game is printed
  in one or two columns, its headers at the top of the page, with diagrams at its key moments (the review's mistakes,
  else the moves marked !!, ??, ? and !), at each comment with arrows or squares (or `[#]`), every 10 moves, or none;
  the assistant's comments and the evaluations beside the moves when ticked. Its text can be selected and copied: its
  fonts are embedded, Inter, and the system's Chinese font when the page has Chinese characters. When the system has no
  reader of PDF files, the PDF is saved and its folder opens.
- **PGN**, from the Databases page: the chosen game or the filter's games. *Write the names in the games* (off at
  first, the games as they were read) writes White and Black as the database's table of names has them (see
  [Players' names](#players-names)), with the players' FIDE ids and titles when it knows them; the database itself
  does not change.

What an export shows names the players as the table of names does, as the game list shows them. The HTML pages and the
PDFs end with the notice of the pieces drawn (*Cburnett*, BSD 3-clause license; *Chessnut*, Apache 2.0; *Celtic* and
*Fantasy*, MIT); a diagram's PNG carries it in its text, its SVG in a comment.

*Print…* is also where there is something to read on paper: a repertoire on the Openings page (each line under the one
it leaves, a diagram at each branching, your notes, and its coverage and gaps as an annex, when measured; an empty
repertoire has nothing to print, its moves starred in the Tree tab first), a player's dossier, an opening report and a
tournament's crosstable (on landscape pages, every player listed). The dossier, the report and the crosstable also have
*HTML page*: the same content as a page of its own, their lines replayable on a board beside the text. The Engines
space's Board keeps its own exports (*Copy PGN*, *Save the game as PGN…*).

## Databases

The **Databases** page holds your game databases: their list on the left, the games of the one chosen on the right. A
database is a folder of its own (`Name.fcgdb`), read fast even with millions of games.

### Importing games

*New database* makes one, named in place; *Add a database…* lists a database folder made elsewhere, its files left
where they are. *Import into this database…* takes a PGN file, or a `.zst`, `.gz` or `.zip` file holding one; a file
dropped on the page goes into the database chosen, or into a new one named after it, and *Paste games* imports the
PGN text of the clipboard. The import runs in the background at a low priority (a tournament beside it loses no
time), with its progress, its speed and *Cancel*. The games committed stay if it stops (every 262,144 games, and at
the end), and importing the file again adds only the missing ones; games already in the database are left out.

At the end, a banner on the database tells what came: the games added, those already there, and the damaged ones
among the games added (a move that cannot be read or is illegal, a text cut short, a start position that is not one),
each kept up to that move with its text as it was; *See them in the list*, or a click on one, shows the file at its
line. The games already there from another source, the same game with its headers written otherwise, are told with
*Merge them* and *See them first* (see [Duplicate games](#duplicate-games)). A full disk, a stop asked for or another error says what is kept and what is needed, with *Resume the import* and
*Open the database's folder*; a file that cannot be read on says why, the games read before it kept. Then the
database's opening tree is built, in the background.

A file whose bytes are not text (a program, a picture, an archive the import does not know) is refused before it is
read as a PGN: nothing is imported, and a new database made for it is not kept.

A database that another program writes to (another RookStudio importing into it, for instance) opens read-only: the
list, the tree and the searches work; importing and saving games wait until that program has finished. *Remove from
the list* leaves a database's files where they are.

### The game list

The list gives each game's number, players, Elo, result, event, date, ECO code and length; a click on a heading sorts
it. A player's spellings brought together in the database's table of names (see [Players' names](#players-names)) show
under the player's name, and under the mouse the name says how the game writes it. The quick filter above takes free words: players, event, year, ECO code, result (`carlsen 2024 B90`). ↑ ↓ go
through the list and the game chosen shows beside, on a small board with its moves; Enter or a double click opens it
on the Board page. Millions of games scroll and sort at once. **Games** and **Tournaments**, beside the database's name
above the list, switch between its games and its tournaments (see [Tournaments and crosstables](#tournaments-and-crosstables)).
*Export…*, after the count of the games, makes the chosen game or the filter's games a page to replay, an animated GIF, a
PDF or a PGN file, the players' names from the table of names written in it if asked (see
[Exporting and printing](#exporting-and-printing)).

### FIDE cards

A player's **FIDE card** (title, federation, birth year, FIDE id, standard, rapid and blitz ratings) is asked of
Lichess, which keeps FIDE's list, without an account and only when you ask: *FIDE card of …*, in the menu of the right
click on a name of the game list or in the game's preview. A game that gives the player's FIDE id (*WhiteFideId*, as
Lichess's broadcasts do) sends that id; otherwise the name is sent, and when several players of FIDE's list are called
that way, you choose which one it is. The card is kept in your profile for that name: it then shows under the mouse
over the name, and in the preview. *On lichess.org* opens the player's page there, *Dossier of …* their dossier (see
[A player's dossier](#a-players-dossier)). A player matched with FIDE's rating list in the database's table of names
shows the list's card instead, without asking Lichess.

### A player's dossier

A player's **dossier** gathers what your databases know of them. It opens from their name: *Dossier of …* in the menu
of the right click on a name of the game list, in a game's preview or on a FIDE card; Ctrl+K, *Dossier: Duval*; the
Lichess page's *Prepare a game against* (see [Preparing a game](#preparing-a-game)). It shows in place of the page, Esc
going back. It is made again each time it opens, from your databases as they are, in the background at a low priority
with its progress and *Cancel* (a few seconds at most, even for a player with thousands of games in millions); nothing
of it is kept.

Its games are found through each database's table of names (see [Players' names](#players-names)): all of a player's
spellings together, in every database of yours but those of Lichess players' games (*Lichess · name*: their account's is
the Lichess source). A player matched with FIDE's rating list is told from their namesakes
by the FIDE id the games give, and the games played before they could play are left out. When FIDE's list has several
players of that name, the dossier asks which one, the one whose rating fits their games proposed first: ↑ ↓ choose,
Enter keeps the choice in the tables of names, and the dossier then takes only that player's games (Ctrl+Z undoes;
*Not now* shows every game of that name).

At its top: the player's title, federation, birth year and FIDE ratings when they are matched with the list, and what
it is made of: your databases, with their games and years, and Lichess when its source is ticked. *Their games in the
list* shows them on the Databases page; *Print…* makes the dossier a PDF of a few pages, *HTML page* a page of its own
whose lines replay on a board (see [Exporting and printing](#exporting-and-printing)). Then four tabs (Ctrl+Page Down
and Ctrl+Page Up go from one to the next):

- **Overview**: their first moves with White and their score, their answers with Black to the first moves they meet
  most, their rating over the years, their results against stronger and weaker players, their pawn structures, and
  your games against them (your names are those of *Settings › Your names*); then their last games. A click opens one
  on the Board page with the dossier as its list: Ctrl+← and Ctrl+→ go through their games, and *Back to the dossier*
  returns;
- **Openings**: their lines with White and with Black, by ECO code, each with its games, its wins, draws and losses,
  their score and the last year they played it. A click opens the line on the Board page, its Tree tab on their games
  alone, from their side (*Duval, Anne with White*). Below, the gaps of your repertoires against them: the lines they
  play three times or more that your repertoires leave without an answer, the most frequent first;
- **Rating and structures**: their rating year by year, the mean of the Elo their classical games give in each year
  (no curve under three years), their highest and what FIDE's list says; their pawn structures at move 20, as the
  assistant's facts name them (none under five games), a click showing those games in the list with the Structure
  search starting from their pawns; and when you reviewed games of theirs (Alt+G), their accuracy in each phase and
  where their clear mistakes fall;
- **Lichess**, with its source: what they play over the board and online side by side, their level online, your games
  against them on both, and your repertoires' gaps over both sources.

*+ Lichess…* links their Lichess account to them, the link kept in the tables of names, and ticks the source: their
games of the last year come as for a game prepared against them; a click on the source's chip takes it out, or puts
it back. Under twenty games, the dossier says that it gives hints rather than trends. A player with no game in your
databases is told so, with *From a source…* and *+ Lichess…* to bring some. Nothing leaves the machine, except, with the
Lichess source ticked, what preparing a game sends: their name with the filters of their games, and their public
profile. Offline, the source shows the games of its last sync, dated. While you play them on Lichess, the dossier shows
nothing of them.

### Tournaments and crosstables

*Tournaments*, beside *Games* above a database's list, shows its tournaments: the games of one event, its site and its
year (a tournament from December to January stays one; the same event in two places, with players of their own, makes
two). Online games, which share no event whose rounds are told, are in none: a database of them says it has no
tournament and offers *Games*. Each tournament gives its system, its players, its rounds, the mean Elo of its players
and its games, *286 of 288* in orange when games are missing. The list is sorted by mean Elo; a click on a heading
sorts it otherwise, and its filter (Ctrl+L) takes words of the event, the site or the year. A large database's list is
made in the background with its progress (a few seconds for ten million games), and kept until the database changes.

The system is guessed from the games: a round robin when each played each (a double round robin when each pair met
twice), Swiss otherwise; *?* when it is not sure (a pair met more often than its system lets them, games missing in a
field as small as a round robin's). Team events and knockouts are said, their crosstables for later. A click on the
system changes it, kept for the tournament.

A click on a tournament, or Enter, opens its **crosstable** in place of the list, Esc going back; Ctrl+K opens one too
(*Crosstable: Autumn Open*). Its players are those of the database's table of names (see
[Players' names](#players-names)): all of a player's spellings together. The games that came twice from two sources
(the same players with the same colours, rounds that can be one) are counted once, and each game's round is read from
its Round tag (*9.2*: round 9, board 2).

- A round robin's crosstable has a cell for each opponent: 1, ½ or 0 (a result for each cycle in a double round robin),
  + and − for a forfeit; then the points, the tie-breaks, the wins, the performance and the rating change.
- A Swiss's has the start numbers, by rating, then a cell for each round: *4b½* is a draw against number 4, with Black;
  *bye*, the only player unpaired in the round (a win's points, FIDE's rule for a pairing-allocated bye); *–*, a round
  without a game. The first ten show, their games between them in the accent colour; *All players* shows the others.

A click on a game opens it on the Board page, the tournament's games as its list (Ctrl+← and Ctrl+→, *Back to the
crosstable*); a click on a name opens the player's dossier (see [A player's dossier](#a-players-dossier)). The
performance and the rating change say how they were counted under the mouse. *Print…* makes the crosstable a PDF on
landscape pages, every player listed, and *HTML page* a page of its own (see [Exporting and
printing](#exporting-and-printing)).

Above the crosstable, a line for each thing wrong with the data, with what fixes it:

- **games missing** (a round robin's pair that did not meet, a Swiss's two players without a game in a round) are
  counted "?", and the points, tie-breaks and rating changes that read them say "?". *Look for them in your other
  databases* finds them under other spellings, *Copy it into this tournament* adds one, its event, site and players
  written as the tournament's (Ctrl+Z takes it out again); *Set the results by hand* gives each its result, as a click
  on a "?" does, kept with the tournament in your profile, the database unchanged;
- **a player under two names**: two names that can be one player's, never paired together, their rounds apart; *Bring
  them together* joins them in the table of names (Ctrl+Z undoes);
- **games that came twice**: *Merge them* keeps each as one game, its fullest fields from each source, as the
  maintenance's duplicates do (Ctrl+Z undoes); those whose moves or results differ are shown in the list instead;
- **a forfeit**: a decisive result without moves (a PGN has no word for it) counts in the points, not in the performance
  nor the rating change;
- **a system that is not sure**: a click chooses it.

A followed broadcast has its crosstable too: *Crosstable*, beside the standings in the mosaic's column, shows it in
place of the boards (*The mosaic*, or Esc, brings them back). It is made of the rounds RookStudio follows and of those
it kept in the *Broadcasts* database: ● a game being played (a click follows it on the Board page), · a game to come,
the points, the games played and the rating change so far. Lichess's own standings stay beside it, and nothing more is
asked of Lichess for it; a team broadcast keeps Lichess's team standings.

### Tie-breaks, performance and rating change

The tie-breaks order the players on the same points, one after the other: FIDE's by default, the line above the table
saying which. The current FIDE tie-break regulations (C.07, effective from 1 March 2026) recommend none for a system,
so RookStudio takes those of FIDE's own events: for a round robin, as the Candidates Tournament 2026, Sonneborn-Berger,
the wins, the direct encounter; for a Swiss, as the Grand Swiss 2025, the average rating of the opponents cut 1,
Buchholz cut 1, Buchholz, the direct encounter (the average rating left out when a player has no rating, as C.07 asks).
A click on a tie-break, in that line or on its heading, puts it first, moves it, replaces it or removes it; *+* adds
one; *FIDE's* puts theirs back. The order is kept for the tournament.

They are FIDE's: direct encounter, wins (forfeits and byes worth a win count), games won, games with Black, wins with
Black, progressive score, Buchholz, Buchholz cut 1, median Buchholz, Sonneborn-Berger, Koya, the average rating of the
opponents (also cut 1) and the performance. In a Swiss, a round without a game counts in Buchholz and Sonneborn-Berger
as FIDE says (C.07, article 16): as against an opponent of the player's own score, at most a draw a round (at most the
scheduled opponent's score for a forfeit), and a player's rounds after withdrawing count as draws for their opponents.

The **performance** is FIDE's: the average rating of the rated opponents met over the board, plus the difference
FIDE's table gives the score in those games (rounded to the hundredth). The **rating change** is FIDE's formula: for
each game played over the board against a rated opponent, the score less the expected score from FIDE's table (a
difference counted as 400 at most, below 2650), times K. K is the player's own from FIDE's rating list when they are
matched with it in the table of names and the list is of the tournament's year or the next (see
[FIDE's rating list](#fides-rating-list)); otherwise FIDE's rules give it: 40 until the end of the year of their 18th
birthday while under 2300, 10 from 2400, 20 otherwise, smaller when 700 would be passed over all their games. A rapid
or blitz tournament follows FIDE's rapid and blitz rules. A forfeit counts in the points only.

### Searching

*Search* (Ctrl+F) opens the search panel beside the list, in six tabs (Ctrl+Page Down and Ctrl+Page Up go from one to
the next); the list follows each criterion as it is typed:

- **Headers**: a player and their colour, the opponent, the event, the years, the ECO codes, the Elo of both players
  at least, the result; *Damaged games only*;
- **Position**: the Board page's position, or one set up (*Set up the pieces…*): its games are found in the
  background, those that cannot reach it (their pawns, their material) left out without being replayed;
- **Material**: a preset (rook endings, no queens, the bishop pair against bishop and knight, a queen against two
  rooks) or the men of each side counted (`2`, or `0-2`), for so many plies at least;
- **Pattern**: men put on the board with the mouse (a click puts the man chosen, a click again takes it away; Shift
  and a click, an empty square; the other squares, anything), *White has only these pawns*, three examples, and
  conditions beyond them: what attacks, defends or pins what, a line of moves (see [Patterns](#patterns));
- **Manoeuvre**: a man's route, such as a knight going b1, d2, f1, g3 (see [Manoeuvres](#manoeuvres));
- **Structure**: the pawns of the Board page, exactly or one or two pawns away (see [Pawn structures](#pawn-structures)).

Each search gives its games and its time; *Add to the filter* and *Take out of the filter* combine them, *See in the
list* (Enter) shows the result, *Clear everything* starts again. The searches set add up: a pattern, and a material,
and headers. A search that replays the games (a pattern, a route, a structure) goes on in the background with its
progress and *Cancel*; the first games it finds are in the list already, and a search cancelled keeps them, *Resume
(the rest only)* searching the games it had not reached. While the database is being imported into, a search covers
the games already committed, and is made again by itself at the end.

The list then shows each game at the position found, in a column *Found at* (the move that led there): Enter or a
double click opens the game at that move, and so does the next or previous game of the list from the Board page.

*Keep this search…* names it after its criteria (a pattern by its preset's name). Its criteria are kept, not its
games, for any database (its new games are found next time); the kept searches show at the top of the panel, where a
click sets their criteria and the right button renames or deletes one, and in Ctrl+K (*Search: …*, or *Pattern: …*
for a pattern alone).

### Patterns

The **Pattern** tab says what holds in one position: the men on the small board first, then conditions, one per row,
all true at once. A click on a word of a row changes it: the men (a white knight, the black queen or a black rook,
any black man, on squares or anywhere), the verb (*is on*, *attacks*, *defends*, *pins … on …*, *is en prise*, *is
passed*, and their contrary), the side to move, check, mate. *+ A condition* adds one, × takes it away. *The same
man as in row 1*, in the men's menu, makes two rows speak of one man: a knight that attacks the king, and the same
knight that attacks the queen or a rook, is a fork.

The presets set a whole pattern: *Knight fork*, *Pin on the king*, *Back-rank mate*, *Queen's double attack*,
*Sacrifice on h7*, *Protected passed pawn*; they are in Ctrl+K too (*Pattern: Knight fork*), on the database chosen.

*Then… (a line)* adds a line of positions after the first: each step a move ("the white bishop takes on h7 with
check") or a position, at the next ply or within so many plies (*each step at the next ply*, *within 3 plies*). A
move can also be played on the small board (*+ A step*, *a move played on the small board*: a click on the man, then
on its square). In each game, the position found is the first of the pattern.

What can never be found is said before searching, and nothing is started: a knight on c7 and c7 empty, White and
Black to move, a pawn on its first rank; a click takes one of the two conditions away. A search that finds more than
half of the games says so (*Very broad*): *Add a condition*, or *Keep it as it is*.

### The pattern language

The same pattern is a text under the builder (*Text*, or Alt+T): changing one changes the other, and a kept search
keeps its text, which can be copied and shared. Its words are CQL's, the language of chess queries:

```
; Knight fork                       a comment (// also)
piece $n in N                       $n: one white knight, the same in every line
$n attacks k                        one condition a line, all true in the same position
$n attacks [qr]
btm

Bd3 Nf3 Qd1 Pe5 kg8 ph7 _f6         men on squares, side by side
line --> move from B to h7 capture check
     --> move from k to h7 capture
     --> .{0,2} --> move from N to g5 check
```

- Men in FEN letters: K Q R B N P for White, k q r b n p for Black, A and a for any white or black man, `_` for an
  empty square, `[qr]` for one of several; squares after the letter (`Nf3`, `R[a1,h1]`, `p[a-h7]`), or alone where
  a word wants squares (`move to [g5,h5]`).
- `attacks`, `defends`, `pin from … through … to …`, `en prise`, `passed`, `check`, `mate`, `stalemate`, `wtm` and
  `btm` (the side to move), `not`, `or`, `only P` (White has no other pawns than those named on their squares).
- `line -->` then a step after each arrow, at the next ply: `move from … to … capture … promote …` and what holds
  after the move, or a position; `.{0,2}` before a step lets two plies of anything come first.
- Two names (`$n`, `$q`) are always two men; a name follows its man through the moves of a line.

A line that does not read is named, with the word it looks like ("Line 2: “atacks” is not a known word —
“attacks”?"); the rest of CQL is told as not read. The builder keeps the last version that read, and nothing is
searched until the text reads again.

### Manoeuvres

The **Manoeuvre** tab looks for a man's route: a click on a man of the small board (the Board page's position), then
a click on each square it goes to (b1, d2, f1, g3); Backspace takes the last square off. *Other moves between* lets
the side play other moves meanwhile; *The same man all along* asks for one man, else any man of its kind on each
square in turn; *In 15 moves at most* counts the side's moves from the first of the route to its last. *From
anywhere*, or *From the Board page's position*: the route once a game reaches that position.

The results say where the same man went next (on f5 in 31 % of the games), and the neighbouring routes, which part
from it after its first move (b1–d2–b3); a click sets one. The grey arrow on the board is the most played
continuation, not a condition. From the Board page, Ctrl+K *This manoeuvre* takes the man under the mouse and its
moves in the game.

### Pawn structures

The **Structure** tab looks for the pawns of the Board page alone, the pieces left aside: *Exactly*, *One pawn away*
or *Two pawns away* (a pawn more, fewer or on another square, on either side, is one pawn), *Both sides*, or
*White's pawns* or *Black's pawns* alone, *The colours reversed too*, held so many plies at least. *Take the Board
page's pawns again* takes them anew; *Set up the pawns…* changes them square by square. Its name shows when the
assistant's facts know it (Carlsbad, Maroczy, hedgehog, Stonewall, the pawn chains, Benoni, isolated queen's pawn).

The results give the games held exactly and those near, the openings that reach it most, and the variants one pawn
away with their games and White's score ("the white b-pawn on b4", "without the black a-pawn"); a click sets one.
From the Board page, Ctrl+K *Games at this structure* searches its pawns at once.

### Tournaments and PGN files

The tournaments of the profile, and the PGN file open on the Board page, show under *Read without a copy* after your
databases: their games listed and searched like a database's, brought up to date in the background when shown (the
games a tournament finishes, a file that grew or changed). They are neither imported nor removed.

### ChessBase databases

A ChessBase database dropped on the Databases page, or chosen with *Import a file…*, is copied into a RookStudio
database: its .cbh or .2cbh (or any of its files), or an archive of one, .cbv or .cbz for a .cbh, .2cbv or .2cbz for
a .2cbh. A card says first what it holds, read in its headers, what comes in and what stays out, where it goes and
about how long it takes; *Import* (Enter) starts the copy, *Not now* (Esc) leaves it. The ChessBase files are only
read, nothing is written beside them; an archive is unpacked while it is read. The password of a .cbz or a .2cbz is
typed on the card (Enter in the field starts the copy) and not kept; a wrong one is said there at once, to be typed
again, before anything is copied.

The copy keeps the moves and their variations, the comments before and after the moves (a text in several languages:
English first, the others after it), the symbols, the coloured squares and arrows, the clocks and the time spent, the
engines' evaluations, the medals (a *Medals* tag), the critical positions and the time controls. Training questions,
pictures, sounds and videos, web links, games quoted, pawn structures and piece paths, variation colours and the
texts (articles) stay out: the report at the end counts them by kind. Imported again (new games added in ChessBase
since), only the new games come in. Players' names are written as the database writes them, "Last, First".

### TWIC

*From a source…* brings games from a site, downloaded only when you ask. **TWIC**, The Week in Chess, publishes an
issue of the week's games every Monday: its sheet asks theweekinchess.com for its list of issues and shows the latest
ones, with their Monday, their games and whether each is already in the database chosen. Tick some, choose the
database (*A new database, “TWIC”* at first), then *Download and add*: each issue's PGN is downloaded, then imported,
one after the other, the database read all along; *Cancel* stops after what was added. A game the database already
has, the same tags and moves, is not added twice. Then a report gives the strongest tournaments of the games added (the
average rating of their players) and the best-rated players; a name opens its games in the list. The games already there
from another source, a broadcast followed before for instance, are told there with *Merge them*.

*Every Monday, look for a new issue* is off until you tick it. Ticked, RookStudio asks for the list of issues while it
is open, four times a day at most; a new issue shows under the list of databases, in *Followed sources*, and on the
Home page's *My databases* card, with *Add to “…”*: it is never added without you. What leaves the machine: the request for
the list of issues, then for the PGN files of the issues chosen, nothing else. TWIC is free for personal use; nothing of
it ships with RookStudio.

### Lichess months

*From a source… › Lichess broadcasts* adds a month of Lichess's official broadcasts (database.lichess.org, CC BY-SA
4.0): its sheet asks for the site's page of files and lists the latest months with their size and games. Choose one and
the database (*A new database, “Lichess broadcasts”* at first), then *Download and add*: the month's file is downloaded,
resumable, then imported, and a report gives its strongest tournaments and best-rated players. A game the database
already has is not added twice. The games keep their source, the broadcast's page in their Site tag: what you share of
them is shared under the same licence. What leaves the machine: the request for the page, then for the month's file.

*From a source… › Lichess games* reads a month of Lichess's rated games (CC0, about 95 million games and 30 GB
compressed) as it downloads, and keeps only the games that pass its filters: by default both players rated 2400 and
more, without bullet (a base time and 40 increments under three minutes). Only the games kept reach the disk, added to
the database chosen by batches, the database read all along. *Read a file already downloaded…* reads a
`lichess_db_standard_rated_*.pgn.zst` from the disk instead. *Cancel*, a network cut or the program closed keeps what was
added, and *Go on* goes on from where it stopped, with the same filters; the games read again are not added twice.

### A chess.com player

*From a source… › chess.com* adds a player's games from chess.com's public API, without an account: type their name
and *Look up* asks for the months they played; *Download and add* asks for each month's games, one month at a time,
and imports them into the database chosen (*A new database, “chess.com · name”* at first), with the same report. With
*Follow* ticked, the player shows under the list of databases, in *Followed sources*: *Look for new games* asks again
for the months not added yet and for the newest one (its games may have grown); a game already there is not added
twice. What leaves the machine: the player's name and the requests for their months, to api.chess.com.

### Lumbra's Gigabase

*From a source… › Lumbra's Gigabase* explains how to bring it: its site asks you to accept its licence (CC BY-NC
4.0) and to show you are not a robot, which RookStudio does not do for you. *Open its site* opens it in your browser;
once the file is downloaded, *Import the downloaded file…* imports it as it is. A .7z archive (LZMA or LZMA2) is
read as it is unpacked: its PGN files one after the other, its other files passed over, nothing written onto the disk
but the games. Any .7z of PGN files can be imported, or dropped on the page, the same way.

### Maintenance

A database's ⋯, beside its buttons on the Databases page, or a right click on it in the list, opens its maintenance in
place of the page, in three parts: *Duplicate games*, *Players' names*, *Check and compact*; Esc comes back. Ctrl+K has
*Duplicate games*, *Players' names*, *FIDE's rating list*, *Check the database* and *Compact the database*. Each long
task runs in the background at a low priority, with its progress and *Cancel*, the database read all along: a
tournament beside it loses no time. Ctrl+Z undoes the last merge, the last players brought together, or the damaged
games set aside. A database another program writes to is open read-only: its maintenance waits until that program
lets it go. The Home page speaks of the maintenance only when it finds something: duplicates after an import, damaged
games found by a check or when a database opens.

### Duplicate games

The same game can come twice from two sources, its headers written otherwise: a round followed on Lichess, then
TWIC's issue of the Monday. The import leaves out a game the database has exactly (the same tags and moves), not these.
*Duplicate games* finds them: the same players (read in the table of names; else their surnames and first initials),
the same year, rounds and dates that do not differ, and the same moves, or the moves of one the start of the other's,
unfinished (a broadcast cut short). A game needs ten moves at least to be told (twenty when its day is not known); two
games with an address of their own (Lichess's) are two games.

It counts them by kind: the same moves with other headers; one shorter, a broadcast cut short; results that differ,
left to you. *Merge all* (Enter) merges the first two kinds: each group becomes one game with the fullest of each field
(a name in full over an initial, the longer event, the round that says more, "9.2" over "9", a date that knows more,
the ratings, the ECO code, the FIDE ids, the longer moves with their comments, the clocks and evaluations, a result over
none), the other games marked deleted. A click on a group compares it field by field, each source in a column and the
merged game beside: a click on a cell takes that field from that source; then *Merge* (Enter), *Keep both* (two games,
never offered again) or *Next group* (↓). The merged game keeps the number of the oldest; its *MergedFrom* tag says
where each field came from, and a broadcast's address left for a place stays as its *GameURL*. Ctrl+Z brings the other
games back. *See in the list* shows the duplicates in the game list, where *Show all* shows every game again.

At the end of an import, the games already there from another source are told in its report, with *Merge them*: they
are never merged without your click. The reports of the sources (*From a source…*) tell them too.

### Players' names

One player is often written several ways: "Lindqvist, Erik", "Lindqvist,E", "Erik Lindqvist". *Players' names* brings
a player's spellings together in the database's table of names. The games are not changed, but the game list shows the
player's name, and its quick filter, its search and its sorts, the opening tree's reference games, the reports of the
sources and the players' dossiers read it. Three counts say what can be done:

- brought together at once: the games give the player's FIDE id (Lichess's broadcasts do), or FIDE's rating list has a
  single player of that name, or of that name as another alphabet writes it ("Kowalenko" for "Kovalenko") with ratings
  near the games'; *Bring together the …* (Enter) does it;
- to confirm: several players of the list have that name. The one whose rating is the nearest to the games' is proposed
  first; ↑ ↓ choose among them or *None of them*, and Enter keeps the choice and goes to the next one;
- kept as they are: a player not on the list, a name of one word (an account's), or no list taken; two spellings of
  one surname and given name are brought together by hand, with a right click and *Bring together*.

A player matched with the list keeps its FIDE id, federation, title, birth year and ratings in the table, also once the
list is removed. Ctrl+Z undoes. The table is part of the database: it goes with its folder. The games of a later import
join their players by themselves when they give a player's FIDE id or write its name in full; another spelling waits
for this page.

### FIDE's rating list

FIDE publishes its rating list each month, about two million players: their FIDE ids, names, federations, titles,
ratings and birth years. RookStudio does not ship it, since FIDE gives it no licence. *Download*, on the *Players'
names* part, asks ratings.fide.com for its combined list (a zip of about 43 MB) and reads it into your profile (about 90
MB), where it stays, never sent anywhere; *A file already downloaded…* reads one you have. A download cut short resumes
where it stopped (*Resume*). Each month after, the Home page says that a new list is out, with *Take it*: it is never
taken without your click. *Look for a newer list* asks again; *Remove it* takes the list out of your profile, the
players brought together staying as they are. A list whose columns changed is refused, the list before staying in
service, with *Report it*. What leaves the machine: the request for the file, when you download it.

### Checking and compacting

*Check* (Enter, or Ctrl+K *Check the database*) reads every segment of the database again from the disk and replays
every game: its signature and its key (a hash of its headers and moves) are made again and compared with those stored,
and a game that reads otherwise than it was written is damaged. It says *Sound*, or the damaged games and the files they
were imported from: *Keep what can be read* sets them aside (deleted; Ctrl+Z brings them back), and *Import their file
again* brings them back from their file when it is still there. The opening tree is compared with the games too.

A segment whose files no longer read when the database opens (cut short, missing or changed while RookStudio was
closed: a disk error, a copy left half done, a sync tool) does not keep the database from opening. Its other segments
work: the list, the opening tree and the searches read their games, and the damaged segment's games keep their numbers.
The Home page and the Databases page tell it, with *See them*, and *Check and compact* says it at once: the segment, how
many games cannot be read, and the files they were imported from. *Keep what can be read* writes that segment again,
its games set aside (deleted, their numbers kept). The players, events and sites first imported with it are kept when
its list of names still reads; otherwise the other games that use them show "?" in their place. Its damaged files stay
until RookStudio closes: until then, *Undo* (or Ctrl+Z) puts them back, as long as nothing else is written to the
database. *Import their file again* brings its games back from their file when it is still there. Compacting waits
until such a segment is dealt with, and a database open read-only is never changed.

Games deleted or replaced, merges included, keep their room in the files. *Compact* writes the segments that hold them
again without them: each game keeps its number (a deleted game its headers only), and the opening tree stays valid. The
old files are kept until RookStudio closes: *Undo the compaction* puts them back until then, and the room is given back
at closing. Imports and saves into the database wait while it compacts; the list, the tree and the searches work. When
the disk has not room enough for the copy, *Compact segment by segment* needs the room of one segment only, but cannot
be undone.

## The Openings page

Your repertoires: each a PGN file of the profile (one game and its variations, for White or for Black), which the
Board page opens and edits like a game (*Open its file on the board*), and *Print…* prints (see [Exporting and
printing](#exporting-and-printing)). One is active: the star of the Tree tab adds
the move shown to it, or takes it out, whatever the side to move; the first star makes a repertoire when there is
none. *New repertoire* makes one by its name and side.

The active repertoire's **coverage** is measured on your databases whose opening tree is built (*Measured on*: a chip
for each, a click takes it out or back), in the background: the share of the games along the repertoire that find an
answer to the end of its lines, or up to move 10 or 15. Its **gaps** follow, the most often met first: an opponent's
move played often enough without your answer, or a line ending on an opponent's move. A click shows the position,
with the tree beside it; the right button also offers its opening report (see [The opening
report](#the-opening-report)).

### Training a repertoire

Each position of a repertoire where it is your move, and where the repertoire has your move, is a position to train.
RookStudio asks them again and again, further apart each time you know them, with FSRS, the spaced repetition Anki
uses: a position answered right comes back in a few days, then in weeks; one you miss comes back later in the session,
then the next day.

The **Train** card of the Openings page counts the positions due today, today's new ones, and those you know, due later.
*Start · N positions* (Enter) asks the positions due, then today's new ones; *Only the new ones* asks the new ones
alone. New positions come in the order your databases meet them most, the most played first, 20 a day at most (the
button beside, *20 a day*, changes it). Under them: what comes back tomorrow, within three days and within a week.

A session takes the Board page, its column becoming the training's; the game that was on the board waits among the
recent games and comes back after it. RookStudio plays your opponent's moves along the repertoire up to each position,
the moves your databases play most first where the repertoire has several. Play your move on the board, or type it
(its letters, then Enter). During the question, the tree, the engines and your notes are hidden; they come back with
the answer.

- **Alt+H**: a hint, the square of the piece to play in blue (H alone begins a move of the h-pawn).
- **Space**: "I don't know", and the move is shown.
- **Escape**: stop. What was answered is kept; the next session goes on with what is still due.

Each answer is graded by itself, with no button to press: right at the first try, *good*; right after a hint or after
more than 20 seconds, *hard*; wrong, or "I don't know", *again*. A right answer shows the repertoire's comment on the
move and when the position comes back; the next one comes on Enter, or on its own after a second (the *…* beside
changes it, and the mouse on the answer holds it). A wrong one shows your repertoire's move on its squares, in green:
*Retry* (Enter) asks it again; *See the tree here* shows the position with the Tree tab; *Add … to the repertoire*
makes the move you played a second answer accepted there (Ctrl+Z takes it back).

At the end: the counts, the moves you missed (a click shows the position, with your repertoire's move) and *Review
the … missed positions now*.

The reviews are kept in the profile beside the repertoire (`Najdorf.training.json`, and `Najdorf.reviews.jsonl`, a line
per answer): its PGN does not change. A move changed in the repertoire makes its position new again. Nothing leaves
the machine. The positions due count in the day's review with the other kinds (see [The day's
review](#the-days-review)); Ctrl+K *Train the repertoire* too. While one of your games is played live on Lichess, the training waits; the positions on the line of
one of your correspondence games are left out until it ends.

### Theory studies

The **Theory** part of the Openings page (beside *Repertoires*, in its title's line) holds your theory studies:
annotated PGN files, your analyses or a book in PGN, and Lichess studies. Their notes show in the Board page's Tree tab,
at each position they comment, whatever the move order that leads there.

*Add a PGN…*, or a PGN dropped on the page, adds a file: RookStudio reads it where it is, in the background, and reads it
again by itself when it changes on the disk. The theory never writes into it, and nothing leaves the machine. *From a
Lichess study…* adds one of your Lichess studies read on this machine (see [Your Lichess studies](#your-lichess-studies)):
its copy in your profile is read, and read again with the study.

What becomes a note: every annotated position of the file, variations included, with the comments before and after the
move, its symbols (!, ?!, ±…), a written evaluation (`[%eval]`), and its arrows and squares; a game's comment before its
first move is a note on its start. The assistant's comments are not (see [Comments by the
assistant](#comments-by-the-assistant)): they are about one game's move, and the assistant comments any position on
request. Your repertoires' comments show the same way, under their names (*Your repertoires' comments too*, under the
list).

Each study shows its numbers: the positions annotated, the comments, symbols and evaluations. *Show its notes in the
tree* (on at first) hides them, or shows them again. *Open it on the Board* (Enter, or a double click in the list) opens
its games there as a copy: Ctrl+S saves a game elsewhere, and the file stays as its author writes it. *Remove it from
the theory* (Delete) leaves the file where it is, and *Undo* puts it back. A file moved away says so, with *Find it…*.

In the **Tree tab**, the *Theory* column marks the moves your studies comment: their symbols, and how many notes say more
than a symbol. Under the moves, **What your studies say** lists the notes of the position shown: each one's source,
where it is (*after 7.f4*, *on 7…Qb6*, or *found by transposition* with its own moves when they reach the position in
another order), its text, and its line, which a click opens on the Board at its move. The mouse over a note on a move
shows the move as the tree's arrow; over a note about the position, its arrows and squares on the board. In the
**Notation** tab, *Theory · 3* above the moves says how many notes the position has, and leads to the Tree tab (Alt+3).
While one of your games is played live on Lichess, the notes are hidden with the tree.

## The Training page

The **Training** page gathers what comes back today, then the exercises by kind. It is in the rail between the
Openings and Lichess, in the views *Study* and *Everything*. A view you made before it does not change by itself: the
Home page offers once to add the page to the view shown (*Add it to …*, or *Not now*), and Ctrl+K *Training* leads
there meanwhile.

Its title has six parts: **Today** (the day's review, the kinds of exercises, the puzzles to start), **Puzzles** (the
same choice of puzzles, Lichess's database kept on your disk, your results by theme), **My mistakes** (see [Your
mistakes as exercises](#your-mistakes-as-exercises)), **Guess the moves** (see [Guessing the
moves](#guessing-the-moves)), **Endgames** (see [Endgames against the tables](#endgames-against-the-tables) and
[Mates](#mates)) and **Vision** (see [Board vision](#board-vision)). A card for each kind of exercise leads to its part.
Everything works offline; what goes to Lichess is said where you choose it.

### The day's review

*To review today* counts what each kind of training has due today, each with its spaced repetition (FSRS), in one
queue: the mistakes of your games (see [Your mistakes as exercises](#your-mistakes-as-exercises)), the positions of your
repertoires (their training stays on the Openings page, see [Training a repertoire](#training-a-repertoire)), the
puzzles and the endgames you missed. *Review all* (Enter) takes them one after the other on the Board page: your
mistakes due, the positions due of each repertoire, the puzzles to review, then the endgames. At the end of each, *Next: …* (Enter) goes on, *Back to
the Training page* (Escape) stops there; what was answered is kept. Beside the button, the time it should take, from the
pace of your answers; under it, what comes back tomorrow and within three days. When mistakes or puzzles are due with
other kinds, *Only my mistakes* and *Only the puzzles* review them alone.

The Home page's *In progress* has the same count (*35 to review today · Review all*), a click reviewing it all; so
does Ctrl+K *Review all*.

### Puzzles

Lichess publishes its puzzles every month as a database (database.lichess.org, CC0): 6,193,530 puzzles in November
2026, each with its rating, its themes and, before move 20, its opening. RookStudio keeps it on your disk and gives
you puzzles from it without a network.

**The database.** *Download* fetches the file (about 295 MB) and reads it while it arrives into a store of about 293 MB
in the profile; the file itself is not kept. A download cut short (*Cancel*, the network, the program closed) goes on
from where it stopped: *Go on*, or *Start again*. *A file already downloaded…* reads `lichess_db_puzzle.csv.zst` from
your disk instead. Once there: *Update* asks database.lichess.org for its file's date and reads a newer one while the
puzzles kept answer; *Remove the puzzles* frees the disk (your rating, your results and your puzzles to review stay,
kept apart). Nothing leaves the machine but the request for the file.

**The choice.** *Offline* (the database) or *Online* (your Lichess account, see [Lichess puzzles
online](#lichess-puzzles-online)); a theme among the first ones offered, each with how often you solve it at the first
try, or among *All themes…*, which lists every theme and every opening with its puzzles (type a few letters to find
one); and a difficulty: *Easier* (300 points under your rating), *Your rating*, *Harder* (300 over). *Start · 20
puzzles* (Enter) begins a series near your rating, never a puzzle you saw before; Ctrl+K has *Puzzles: Fork* and the
other themes, which start a series at once.

**A series** takes the Board page, its column becoming the puzzle's, as the repertoires' training does; the game on the
board waits among the recent games and comes back after it. The board shows the position from your side, the
opponent's move just played in pale green. Play your move on the board, or type it (its letters, then Enter); RookStudio
plays the opponent's replies, the rest of the solution. During the question the engine, the tree and your notes are
hidden, and so are the puzzle's themes.

- **Alt+H**: a hint, the square of the piece to play in blue; the puzzle then counts as missed.
- **Space**: "I don't know"; the solution is shown and the puzzle counts as missed.
- **Escape**: stop; what was answered is kept.

Right: the solution in moves, your rating's change and the puzzle's themes; *Next* comes on Enter, or on its own after a
second (the repertoires' training's setting); *Analyse here* (Alt+A) starts the engine on the position; *The game on
lichess.org* opens the game the puzzle comes from. Any move that mates is right, as on Lichess. Wrong: your move in
orange and the solution's first move as a green arrow; *Retry* (Enter) plays it again, without counting again; *The
solution move by move* (→) plays it a move at a time; *Next* goes on.

At the end: the puzzles solved at the first try, those missed, your rating's change, and the missed ones (a click shows
the position with the solution); *Review the 3 now* (as many as missed) asks them again without counting, *Back to the
Training page* leaves.

**Your rating** is RookStudio's own, computed with Glicko-2 as Lichess computes its puzzle ratings: a puzzle solved at
the first try, without a hint, is won against the puzzle's rating. It starts from your Lichess puzzle rating when your
account is linked, else at 1500, and moves fast while it is new; results of offline puzzles cannot go back to Lichess.
*Your results by theme* (the Puzzles part) counts each theme's puzzles and how often you solved them at the first try.

A puzzle you miss becomes a card of the day's review: it comes back tomorrow, then further apart each time you solve
it; missed again, later in the same series, then the next day. All of it is kept in the profile's `puzzles` folder
(`rating.json`, `results.jsonl` a line per puzzle, `review.json`), the database in `lichess-puzzles`.

### Lichess puzzles online

*Online* gives puzzles from your Lichess account: puzzles you never saw, at your Lichess rating, whose results go back
to Lichess. It needs your account linked (see [Linking your account](#linking-your-account)); the first time, Lichess
asks for two more permissions, *puzzle:read* and *puzzle:write*: *Allow on Lichess…* opens its page, your account stays
linked meanwhile, and the token it replaces is revoked.

*Get a batch · 50 puzzles* asks Lichess for fifty puzzles of the theme and the difficulty chosen. The batch is kept on
your disk: it is solved offline too, over as many series as you like (*Resume · N left*). *Rated* or *Unrated, to
try* is sent with each answer. At the batch's end, its answers go to Lichess in one request, which brings the next
batch; your Lichess rating moves then, and the page says when the last answers were taken. Lichess asks programs not
to download its collection through these requests: the database is there for that.

When Lichess cannot take the answers they stay on the disk, said with what to do: too many requests (429), sent again
by themselves a minute later, even if RookStudio was closed meanwhile; the network down, sent again a minute later;
the token refused, sent once the account is linked again. *Try again now* does not wait. What leaves the machine: the
batch asked for (its theme or opening, its difficulty), then each puzzle's id with right or wrong, rated or not.

### Your mistakes as exercises

The mistakes of your own games come back as exercises: the position before your move, to find better. Your games are
those where you play under one of your names (see [Your names](#your-names)), in your databases. Each of them that has
a review (see [The review of a game](#the-review-of-a-game)) gives the moves the review marked **?** and **??** on your
side, where a better move is clear: the review searched the position deep enough to name its best move (its third pass
at least), and the game was not decided anyway (the review's best move keeps you in the game; your move did not keep a
win). Inaccuracies (?!) are left out: they seldom have one good answer.

The **My mistakes** part counts the exercises and the games they come from, those due today, the new ones and those you
know, due later. Their kinds come from *What to work on* of each review: forks, pieces left en prise, rook endgames…;
a chip for each keeps the list and the session to that kind, *Every kind* takes them all. The list gives each
exercise's game, where and when it was played, your move and the review's, and when it comes back. *Start · N
exercises* (Enter) asks those due, then today's new ones (as many a day as the repertoires' new positions).

Your latest games without a review are said under the list: *Review them · in the background* reviews them one after
the other, as the Board page reviews a game (its engine, its settings, the processes left beside a tournament), each
review kept with its game; their exercises come game by game. The progress, the time left and *Cancel* show
meanwhile; the reviews wait while the Board page analyses.

**An exercise** takes the Board page, its column becoming the exercise's, as the repertoires' training does: the game
up to the position, from your side, the opponent's last move in pale green; under the board, when it was played and
what you played then. Play your move on the board, or type it. The review's best move is right at once; any other move
is searched by the review's engine at the review's settings (one thread, its number of nodes, from an empty table), and
it is right when it loses 0.3 pawn at most against the review's best, or keeps a win the review would not mark. The
engine, the tree and your notes are hidden during the question.

- **Alt+H**: a hint, the square of the piece to play in blue.
- **Space**: "I don't know"; the review's move is shown.
- **Escape**: stop; what was answered is kept.

Each answer is graded by itself, as a repertoire's position is: right at the first try, *good*; right after a hint or
after more than 20 seconds, *hard*; wrong, *again*. Right: the review's line and your game's, what the review said of
your move, and when the exercise comes back; *Open the game at this move* opens it on the Board page. Wrong: your move
in orange, the review's move as a green arrow, and what the engine found after yours; *Retry* (Enter) plays it again
without counting again; a missed exercise comes back a few exercises later, then the next day.

The exercises due count in the day's review (see [The day's review](#the-days-review)), *Only my mistakes* reviews them
alone; Ctrl+K has *Review my mistakes* and *Exercises from my mistakes*. A position your repertoire trains is not taken
here: it is trained on the Openings page. A game removed from its database takes its exercises with it. The exercises
and their cards are kept in the profile's `mistakes` folder (`exercises.json`, and `answers.jsonl`, a line per answer);
your games do not change, and nothing leaves the machine. What your games say also orders the puzzles' themes: the
kinds of your mistakes of the last month come first among them, said under the chips.

### Your names

*Settings › Your names* (or the *My mistakes* part, before any name) lists the names you play under, as your databases
write them: a game where one of them plays is yours. A name matches by its whole words, letter case and accents left
out: *Tester* is *Tester, Sam* and *TESTER, S.*, *Sam Tester* is *Tester, Sam*, but *Test* is no one. The name of your
linked Lichess account counts by itself. Click a name to take it away: the exercises of its games go, but their cards
are kept, and come back with them if you add the name again.

### Guessing the moves

**Guess the moves** replays a game of your databases from one side: at each of its moves, you guess the one played.
The engine scores each guess against the game's move: *as the game* 3 points; *as good* by the engine (0.2 pawn worse
at most, or better) 2; *playable* (0.5 pawn worse at most) 1; else none. It is the analysis engine, on one thread, a
second a move; the game's move is searched while you think.

*A game at random* draws one from the database chosen, through a filter: the game list's quick filter (players, event,
an ECO code, a year) and the players' Elo. Choose your side, then *Start · a random game* (Enter). With *The moves of
theory pass by themselves* (kept for the next time), the guessing starts where the game leaves the moves many games of
your databases played (their opening tree). *The Board page's game* guesses the game shown from its position, for the
side to move: Ctrl+K *Guess the moves* on the Board page.

The game takes the Board page, its column becoming the guessing's; the game is not changed, and its later moves are not
shown. After each guess: your move as a blue arrow, the game's as a green one, the points and both evaluations; the game
goes on by itself after a moment (Enter at once), the opponent's answer played, and the next move is yours. Under it:
each guess and its points, and the score so far. At the end: your points, how many guesses were the game's move, the
pawns lost a move on average, and the moves that cost you most (a click shows their position); the whole game is on the
board, ← and → go through it. Each game guessed is kept in the profile's `guess` folder; the Training page shows the
last one's points.

### Endgames against the tables

The **Endgames** part is a catalogue of endgames to win or to hold, played against the tablebases: eighteen of them in
four groups. *Elementary mates*: the queen, the rook, the two bishops, bishop and knight. *Pawns*: the opposition, the
rule of the square, the outside passed pawn, Réti's manoeuvre. *Rooks*: Lucena, Philidor, Vancura, rook against pawn,
rook and two pawns against rook. *Pieces and queen*: queen against rook, queen against pawn, the wrong bishop, rook
against bishop, bishop against knight. Each says what it asks (*mate*, *win* or *hold the draw*) and where you stand:
*to do*, *done*, *done in 9 (the tables: 7)* for a mate, *to do again* after a fault. One the tables at hand cannot
answer is greyed, its pieces said (see [Tables for the endgames](#tables-for-the-endgames)); hover one for the idea that
wins or holds it. A click plays it; then ↑ and ↓ choose another and Enter plays it. Ctrl+K has *Endgame: Lucena* and the
others.

**An endgame** takes the Board page, its column becoming the endgame's, as the puzzles do; the game on the board waits
among the recent games. You play from your side; the tables answer each move with the one that resists the longest
(with Syzygy's tables, the longest to the next capture or pawn move, and among moves as long the one that leaves you the
fewest good answers). Above the question: the tables' verdict now (*The tables: White wins*), and how far, the moves to
mate when Lichess knows them, else the moves at most before a capture or a pawn move.

- **Ctrl+Z**: take back your last move and the tables' answer.
- **Alt+H**: the right move, as a green arrow; the endgame then counts as done with help.
- **Start again**: back to the start. **Escape**: stop.

Each move is judged at once. One that turns a win into a draw, or a draw into a loss, is a fault: the tables answer,
then the column says it (*1.Kd5? Ke7: a draw, now*) with the moves that kept the result (*Kept the win: 1.Kd6!,
1.Kf6!*, their squares in green, your move in orange) and the endgame's idea. *Take back* (Enter, or Ctrl+Z) plays it
again; *See the win* takes it back and writes the tables' line from there; *Continue anyway* plays on in the new
verdict. Under it, the verdict at the start and each move that changed it.

A win counts at the mate, or once a pawn of yours becomes a queen or the other king stands alone, the win kept; in the
elementary mates the mate itself is the goal, your moves set against the tables' own mate from the start. A draw counts
when a rule draws the game (stalemate, too little material, the threefold repetition, the fifty moves), or after 15
moves held. At the end: *Again* (Enter), *Next:* the next endgame not done yet, *Back to the Training page*.

The position before a fault becomes a card of the day's review (see [The day's review](#the-days-review)): it comes back
tomorrow, the endgame played again from it, then further apart each time you win or hold it without a fault (with
help, it counts as hard; a fault, as missed). Ctrl+K *Review the missed endgames*.

**From your games** takes the positions of 7 pieces or fewer among your mistakes (see [Your mistakes as
exercises](#your-mistakes-as-exercises)), where the review of your game saw the win, or the draw, slip away. *Start*
plays them one after the other against the tables, each from the position before your move; one the tables say was lost
anyway is left out. The results and the cards are kept in the profile's `endgames` folder (`record.json`).

### Tables for the endgames

The tables are those of your disk, the Syzygy folders of the tournaments (*Adjudication* > tablebases, see [Tablebases
on the Board page](#tablebases-on-the-board-page)), with the profile's own once downloaded; for more pieces than they
hold, Lichess's online, up to 7 pieces, with your consent to the online tables (the Board page's, see [Online for the
analysis](#online-for-the-analysis)). While a tournament plays in the program's process, the tables of the disk are its
own: Lichess's answer meanwhile, or the endgame waits.

When an endgame needs tables you do not have, the part says so (*No tables for "Lucena"*) and offers three ways:

- **Download the tables of 3 to 5 pieces** (290 files, 938 MB) from Lichess's mirror, one file at a time, the smallest
  first: the endgames of 3 and 4 pieces are ready within seconds, the others as their files arrive. A download cut
  short goes on from where it stopped (*Go on*); each file is checked (its size and Syzygy's mark). The tables go to the
  profile's `syzygy` folder and, once all there, to the tournaments' folders.
- **A folder of tables already on the disk…**: chosen for the tournaments too.
- **Lichess's, online**: up to 7 pieces, a request a position played; choosing it gives the consent.

Without tables, the endgames that need them are greyed, each with what it needs; the other exercises work. Online, a
cut makes the endgame wait: your move is kept, and judged when the network comes back (*Try again now* does not wait); a
429 waits the minute Lichess asks for. What leaves the machine: with the tables of the disk, nothing; with Lichess's,
each position played, as its FEN; for the download, the request of each file.

### Mates

The **Mates** card (on the Endgames part, which the Today part's card leads to) starts 20 of Lichess's mate puzzles
(see [Puzzles](#puzzles)): in 1 to 5 moves, or by pattern (back rank, smothered, Anastasia's, Arabian, Boden's, double
bishop, hook), each chip with how often you solve it at the first try. They come from the database on your disk, else
from a batch of your Lichess account; without either, the click leads to the Puzzles part. The elementary mates against
the tables are in the catalogue above, your moves counted against the tables' mate.

### Board vision

The **Vision** part trains seeing the board in your head. Nothing leaves the machine; the records are kept in the
profile's `vision.json`.

**Coordinates**: thirty seconds of squares. *Find the square*: a square is named, click it on a board without its
letters and figures; *Name the square*: a square is marked in blue, type its name (a letter, then a figure). Seen from
White, from Black, or *At random*, the board turning square by square. A fault shows the square asked in green and
yours in orange. At the end, your count and your record for that way and side. Enter starts, Escape stops.

**Blindfold**: a game against the engine of the Play page, at the strength and with the openings chosen there (*Change
on the Play page*, see [Against an engine](#against-an-engine)). You play White, Black or either at random; the board
hides every piece, every piece after the 10th move, or all but the pawns, the last move marked. Type your moves, or play
them on the board; the moves so far are written in the column. Hold Shift a moment to see the pieces for 2 seconds,
three times a game; F2 asks *Where is…?* about a piece its words name alone (*the black dark-squared bishop*): click its
square, or type it. Your clock is long, the engine's short; each game is appended to the profile's
`games/blindfold.pgn`.

**A line read in your head**: a position of a well-known opening, the six moves that follow written beside it without
the pieces moving, then a question: which piece is on a square they changed? Click the answer, or type the piece as FEN
writes it (*N* a white knight, *n* a black one, *-* an empty square); the board then shows the position after the moves.
Ten lines a series.

## The Play page

The **Play** page has two parts: *Against an engine*, a game against an engine of your library played on the Board
page, and *On Lichess*, your Lichess games played from RookStudio (see [On Lichess](#on-lichess)) and Lichess's arenas
and Swiss tournaments (see [Arenas and Swiss tournaments](#arenas-and-swiss-tournaments)); the part shown last comes
back. It is in the rail after the Training page, in the views *Study* and *Everything*. A view you made before it
does not change by itself: the Home page offers once to add it to the view shown (*Add it to …*, or *Not now*), and
Ctrl+K *Play against an engine* (or *Play on Lichess*) leads there meanwhile, as do *Play against an engine* on the
Home page and *The Play page* in the Board page's card *Play a game…*. Against an engine, nothing leaves the machine:
the engine runs on it. While one of your games is played live on Lichess, the page says so and starts no game against
an engine (see [Fair play](#fair-play)).

### Against an engine

The choice is kept for the next game: once made, *Play* (Enter) is all it takes.

- **The opponent**: an engine of your library, with the strengths it can take.
- **Its strength**: the Elo it aims at (`UCI_Elo`, with `UCI_LimitStrength`), on its own scale: an indication, not a
  FIDE rating. An engine without that setting plays a number of nodes a move instead, from 1 to a million, fewer for a
  weaker game. *Full strength* leaves it at its best, whatever its card limits.
- **Its style: its openings**, what it plays before its own moves: *Its own* (no book); *Your databases* (the moves
  played most there, in all your databases with an opening tree or in one of them); *Your repertoire* (the moves you
  prepared against, then the engine; you play the repertoire's colour, which choosing it sets); *A player's games* (the
  moves a player played with the engine's colour, from a database named *Lichess · name*, such as preparing a game
  against them makes, see [Preparing a game](#preparing-a-game)); *A book of the library* (a Polyglot or CTG book of the
  Books page, its moves to avoid left out). *Vary* picks at random among the moves played at least 5 times, as often as
  they were played (a book's moves as often as its weights say, a repertoire's moves equally often); unticked, the move
  played most, a book's main move first. Once its openings have no move for the position, the engine plays to the end.
  Its style is its openings: the engine's own settings stay on its card. With a handicap, the openings meet positions
  they do not have, and the engine plays from the first move.
- **You**: White, Black or at random; the clock, minutes and an increment, *The same for both* or, unticked, the
  engine's own (unequal time); from the start position, or from the Board page's.
- **The handicap**: what the engine gives at the start: nothing, its f-pawn, its b-knight, its a-rook (castling on that
  side with it) or its queen; a man it does not have where it starts is greyed, and Chess960 has none. *Taking a move
  back* (allowed unless unticked) and *Hints* (3 a game) are handicaps too, counted.

The game counts in the core budget (the engine's threads, as for a game played from the board); the page says when the
machine is full.

### The game against an engine

*Play* takes the game to the Board page, in a view for study: the board turned to your side, the clocks running in the
players' strips (the engine's with its strength), and over the column the game's card: whose move it is, *Offer a
draw*, *Resign…*, *Take back · Ctrl+Z*, *Hint · Alt+H*, *Stop*.

- Play your moves on the board, or type them: a move's first letter opens the field, Enter plays it.
- **The aids are hidden until the end**: the Analysis, Tree, References and Questions tabs, the analysis (Alt+A), the
  threat, the game's analysis (Alt+G), the tablebases and the assistant. The engine shows nothing of its search either:
  its evaluations come with the finished game. *Show them*, in the card or in place of a tab, gives them back during
  the game, which is then marked *with aids*.
- *Take back* (Ctrl+Z) takes back your last move and the engine's answer, or your move while the engine thinks, the
  clocks as they were before it: counted, and written in the game.
- *Hint* (Alt+H): at your move, the analysis engine's move after a second's search, as a green arrow; three a game,
  counted and written in the game.
- *Offer a draw*: the engine answers with its next move. It takes the draw when it is being mated or stands clearly
  worse, or when it stands no better once 20 moves are played; a move from its openings declines it.
- *Resign…* asks for Enter, or a second click, within a few seconds; *Stop* ends the game without a result, saved as it
  is.
- Where the engine's openings ended, the notation says so (*end of your repertoire · then the engine*).

Closing RookStudio during the game keeps it: at the next launch it is back on the Board page, its clocks waiting until
you resume it (*Resume*, Enter) or stop it (*Stop it*). Another game opened on the board meanwhile leaves yours going,
your clock running when it is your move: *Back to the game* brings it back.

### After the game

At the end, the aids come back, and the card says how the game ended (*Draw: threefold repetition at move 34*),
against what and how: the strength, your colour, the clock, the takebacks, the hints, with or without aids. The game
is saved into the database **My games against engines**, made at the first game, with its settings in its tags:
`Strength` (`UCI_Elo 1800`, `nodes 1000`, `full`), `Openings` (`repertoire Najdorf`, `database Mine`, `databases`,
`player Somebody`, `book Human`, `engine`), `Handicap` (`knight b8`, with the start position), `Takebacks`, `Hints`,
`Aids`. Ctrl+S puts it back there after a change.

- *Review it* (Enter): the game's analysis, as Alt+G does (see [The review of a game](#the-review-of-a-game)); once it
  is reviewed, your mistakes in it become exercises (see [Your mistakes as exercises](#your-mistakes-as-exercises))
  when your names say the game is yours (see [Your names](#your-names)).
- *Rematch, colours reversed*: the same choice, the other colour.
- *Change the settings*: the Play page.
- *Close*: the game stays on the board as any game.

Under it, **your games against that engine at that level**: won, drawn and lost, those with aids apart and not
counted, and where you lost the last one (*a rook endgame*), with whether it is among your exercises. The next level
is chosen there or on the Play page: nothing goes up by itself. Each game is also a line of the profile's
`play/games.jsonl`.

### On Lichess

The Play page's **On Lichess** part plays your Lichess games from RookStudio, through Lichess's Board API, which is made
for people. It has two halves, *Games*, described here, and *Tournaments* (see [Arenas and Swiss
tournaments](#arenas-and-swiss-tournaments)); the one shown last comes back. It needs your account linked (see [Linking
your account](#linking-your-account)); the first time, Lichess asks for one more permission, *Playing your games*
(`board:play`): *Allow on Lichess…* (Enter) opens Lichess's page, your account stays linked meanwhile, and the token it
replaces is revoked. RookStudio never plays a move you did not play. What leaves the machine: your seeks and challenges,
your moves, your chat messages, draws and resignations, with your account's token; the choices are kept in the profile's
`play/lichess.json`.

- **Seek a game**: rapid (10+0, 10+5, 15+10), classical (30+0, 30+20) or correspondence (1 to 14 days a move), rated
  or not, against opponents of any rating or of a rating around yours in that speed (±). Lichess keeps blitz and
  bullet for its site: a program does not seek them, and blitz stays possible as a direct challenge. *Seek an
  opponent* (Enter) puts the seek in Lichess's lobby once Lichess's event stream is open, as Lichess asks, so that the
  game's start is not missed. A live seek lives while RookStudio holds it open: Escape calls it back, and so does
  closing RookStudio; the game found opens on the Board page. A correspondence seek stays in the lobby until someone
  takes it (the API cannot call it back; lichess.org's lobby can), and its game comes among your games in progress.
- **Challenge a player**: their name, the clock (3+2, 5+3, 10+5, 15+10, 3 days, or *other…*: minutes and seconds
  added a move, or days a move; three minutes at least, the increment counted forty times, since the Board API plays no
  bullet), rated or not, your colour. A live challenge waits for the answer while you stay (Escape, or *Call it
  back*). Declined, Lichess's reason is said (*too fast for them*) with other clocks a click away (slower ones after
  "too fast"); accepted, the game opens on the Board page.
- **A challenge received** comes at the top of the part (*Accept* · Enter, *Decline ▾* with Lichess's reasons: *Not
  now*, *Later*, *Too fast for me*, *Too slow for me*, *Casual only*, *Rated only*), on the Home page under *In
  progress*, and in a notice at the window's corner wherever you are, with a sound when the sounds are on; *Later* puts
  the notice aside. During a live game, it waits for the game's end. A game the Board API cannot play (bullet, a
  variant other than Chess960) is played on lichess.org.
- **Your games in progress**: your turn first, with the time left to move; a click opens one on the Board page, where
  your move is played.

The event stream is the one of [Fair play](#fair-play): Lichess opens one per token, and it tells the games' starts and
ends, the challenges received and the fate of those you sent.

### A game on Lichess

The game is played on the Board page, in a view for study: the board turned to your side, the players' ratings and
clocks in their strips, and over the column the game's card: what it is (*Rated · Rapid 10+5 against …*), whose move it
is, *Offer a draw*, *Resign…* (Enter, or a second click, within a few seconds), *Ask for a takeback*, *Abort* (before
both sides have moved), *Chat* (the players' chat), *Open on lichess.org*.

- Live, your moves go as soon as they are played, on the board or typed (a move's first letter opens the field, Enter
  plays it). A move made during your opponent's turn waits for yours (a dashed arrow), and is played then if it is
  legal; Escape takes it back. Before your first move, the card counts down the time Lichess gives before it aborts
  the game.
- By correspondence, a move waits for your confirmation (*Your move: 10.g4 · not sent yet*, an arrow on the board):
  *Send* (Enter), or *Take it back* (Escape). The card lists your other correspondence games, a click away.
- Your opponent's draw offer or takeback request comes in the card with *Accept* and *Decline*; your own waits for
  their answer. If your opponent leaves the game, the card counts down until Lichess lets you claim the victory
  (*Claim the victory*, *Claim a draw*).
- **The aids**: the lock of [Fair play](#fair-play) holds, as for any game of yours. Live, every aid is locked on every
  position: the Analysis, Tree, References and Questions tabs, the analysis, the threat, the tables, the cloud and the
  assistant. By correspondence, the engine, the cloud, the tables and the assistant are locked on the game; the tree of
  your databases and your books stay open, as Lichess allows a book there.
- **A cut**: the card says so (*your clock runs on Lichess*) and tries again by itself, after 1, 2, 3, then every 5
  seconds; the whole game comes back with the connection, the chat too. A move played meanwhile does not go: play it
  again once the connection is back. *Open the game on lichess.org* stays at hand.
- **Closing RookStudio** during a live game asks first (*Stay* · Escape, *Close anyway*): the game goes on on Lichess,
  your clock running. At the next launch, a game still being played comes back on the Board page, and the Home page
  shows it first. Another game opened on the board meanwhile leaves yours going: *Back to the game* brings it back.
- At the end, the card says how the game ended and what it did to your rating; the game comes into your Lichess
  database, with Lichess's analysis when there is one, its review a click away, as your other finished games do (see
  [Your finished games](#your-finished-games)). *Another game* seeks again with the same clock.

### Arenas and Swiss tournaments

The *Tournaments* half of the Play page's **On Lichess** part joins a Lichess arena, or a Swiss tournament of one of
your teams, and plays its games from RookStudio. The Lichess page's card *Lichess tournaments* leads there too, *Join*
on an arena's line (see [Lichess tournaments](#lichess-tournaments)).

- **What Lichess allows.** Its Board API plays rapid, classical and correspondence games only, never blitz or bullet in
  a tournament: those arenas are followed in the mosaic and played on lichess.org, and their line says so, with the
  link. The part lists the rapid and classical arenas you can play.
- **The first time**, Lichess asks for one more permission, `tournament:write` (to join, pause and leave a
  tournament); `board:play`, to play, is given already. *Allow on Lichess…* (Enter) opens Lichess's page; your account
  stays linked meanwhile, and the token it replaces is revoked.
- **Joining an arena**: its conditions as Lichess judges them, each with your figures (*At least 20 rated games in
  Rapid (you: 312)*), a condition missing said before you join; its rules in one line each, and *The full rules on
  lichess.org*; *Join* (Enter). A refusal is said in Lichess's words: for an arena reserved to a team's members, *Open
  the team on lichess.org*, as RookStudio does not manage teams.
- **Waiting for a game**: Lichess pairs a player away from the arena's page for a minute only, so RookStudio joins you
  again each minute while you wait; closed, it no longer does, and you are paired no more, your points staying. The
  next game opens on the Board page by itself.
- **The arena's line**, over the part and over the game's card: your rank of so many, your points, the streak (*the
  next win counts double*), the time left, and *Pause* (Alt+P), which takes you out after the game being played, your
  points staying; *Back in the arena* (Alt+P) joins again. The arena's page is asked every 15 seconds; after a 429 (too
  many requests), its standing waits a minute and says so, your games going on.
- **Playing**: as any game on Lichess (see [A game on Lichess](#a-game-on-lichess)), with *Berserk* where the arena
  allows it, before your first move only (half your clock, a point more if you win); Lichess allows no takeback in a
  tournament's game, nor aborting it. A tournament's game whose first move you do not play in time is lost, not
  aborted, and the card says so. Under the card, the game before at its end:
  *Won in 34 moves · +2 points · the streak starts*.
- **The aids** stay locked while you are in the arena once it has started, between two games too, since the next one
  comes within seconds: the engine, the tree, the cloud, the tables, the assistant and the reports (a player's dossier,
  an opening report, a crosstable). *Pause* gives them back (see [Fair play](#fair-play)).
- **Its end**: your rank and your points; each game is in *Lichess · your name*, as your finished games are (see
  [Your finished games](#your-finished-games)), their review offered then: *Review the last game*, *Its games in the
  list*. Closing RookStudio during an arena asks first: *Pause and close*, or *Stay* (Escape).
- **A Swiss tournament**, for its team's members: its round of so many, the standing after its last round (points,
  tie-break, performance), your games round by round, and the next round's time. Two minutes before it, a notice at
  the window's corner says so wherever you are; your game opens on the Board page by itself, and a game whose first
  move you do not play in time is lost. *Its games in the mosaic* shows the round's games of its first eight. Once it is
  over, *Keep it in a database…* takes its games, each with its round, and its standing (its TRF file) into *Lichess
  tournaments* or another database of yours: the database's crosstable then reads it, byes and late joins included
  (see [Tournaments and crosstables](#tournaments-and-crosstables)), a click away with *Open its crosstable*.

What leaves the machine: following, the tournament asked and its games, without an account; joining, with your token,
the join, the pause and your moves. One request at a time, a minute after a 429. Ctrl+K has *Play an arena or a Swiss
tournament on Lichess*, *Join the arena: …*, *Pause in the arena*, *Back to the arena*, *Berserk* and *Keep the Swiss
in a database…*.

## The Lichess page

Everything here is optional: RookStudio works without an account and without the Internet, and what goes to Lichess
is said where you decide it. Its right-hand column holds the services: the broadcasts, Lichess's tournaments, Lichess
TV, the followed players, the evaluations kept offline, *Your engines on lichess.org* once one of your engines is
offered to lichess.org's analysis board (see [Your engine on lichess.org](#your-engine-on-lichessorg)), and what the
analysis may ask online.

### Linking your account

*Link the account…* opens lichess.org in the browser, where you allow RookStudio without giving it a password. Lichess
sends the browser back to 127.0.0.1, on a port only RookStudio listens on while it links; without an answer in 5
minutes, the link stops by itself. When no browser opens, the address shows, to paste in a browser of this machine.

Linked, the card gives the account's name and when its token ends (a year after; *Renew…* a month before). RookStudio
reads your name and public data only, and asks for more when a feature needs it: reading the broadcasts, for
instance. The token is kept in your profile, readable by you alone; *Unlink* revokes it at Lichess and deletes it
here, and your databases stay. A linked account brings Lichess's opening explorer in the Tree tab (the masters, all
the players, one player), and your games synced faster, your private ones too.

### A player's games

*A player's games* imports the public games of a Lichess player, yours or another's, into a database (*Lichess ·
name*), without an account too: *Import*, then *Sync* for the games played since. The games come by batches, the
oldest first, and what was received stays if you cancel. *What is asked*: the speeds (all; blitz, rapid and
classical; classical and correspondence), since when (the first game, a year ago, a date), standard chess and Chess960
only. The games keep their link to lichess.org, their clocks and Lichess's evaluations. *Count the new games when the
Home page opens* asks Lichess at most once an hour, with the player's name.

### Your Lichess studies

*Your studies*, on the Lichess page, lists the studies of your linked account, the most recently changed first. The
first time the card is opened, RookStudio asks Lichess for the permission to read your studies (*Allow on
Lichess…*): with it, your private and unlisted studies are listed too; *Without it*, the public ones only. Lichess's
list gives each study's name and when it last changed; its chapters show once it has been read, and its visibility for
the studies made from RookStudio. Another's study comes by its link (see [Lichess links](#lichess-links)).

A click on a study reads it the first time: its chapters show, and stay in your profile, offline too. *Open a chapter
on the Board* (or a double click on it) opens it, seen from the side Lichess shows it from. *Import into a database*
puts each chapter into a database as a game: *Lichess studies*, made when missing, or another chosen beside it. Done
again, or with *Read again* once imported, only the chapters changed since are replaced and the new ones added; a game
you deleted there stays deleted. *Make it a theory study* shows its comments in the Tree tab, read again with the study
(see [Theory studies](#theory-studies)); a study never read is read first.

*Follow it*: when the Lichess page opens (every five minutes at most), RookStudio asks Lichess whether each followed
study changed, one request each, without downloading it. A study that changed says so, in the card and on its line
when the card is folded, with *Read again*: nothing is downloaded before. After a reading, the chapters changed since
the reading before are marked.

**From the Board**, *lichess.org…* › *To a Lichess study…* opens a card beside the board that sends the game into one
of your studies (a full one, at 64 chapters, is not offered) or into a new one, whose name and visibility are chosen
there: private, unlisted or public, private first. The chapter's name, the side it is seen from and whether the
assistant's comments go too (as plain comments after the move's own) are chosen in the same card; Enter sends, Escape
leaves it. What goes is the game as PGN: its headers, moves and variations, your comments and symbols, its arrows and
squares, its clocks; the engines' data, the evaluations and anything else of yours stay here. The first send asks
Lichess for the permission to write in your studies. Once sent, *Open it on lichess.org* or *Copy its link*.

Later, the strip over the moves says *Sent to Lichess · 2 changes since*, with *Send the changes*: the chapter's moves,
variations and notes are replaced on Lichess by the game's, its tags too when they changed. Nothing goes on its own. A
chapter opened from your studies says *From your Lichess study*, and sends its changes the same way.

Lichess's limits: 64 chapters a study, 30 new studies a day.

### Official broadcasts

The official broadcasts live, the best known first (*All the broadcasts live* for the others), and the next ones.
*Follow* opens a round in a mosaic on the Lichess page: each game's card with the names, the clocks, the last move,
the evaluation bar, and the score and line of the engine chosen in *Analysed by*; on the right, the game chosen with
its last moves, *Follow on the board* and *Copy the PGN*, then the round. The games are analysed on your machine, with
your engine, as many at once as the cores the tournaments leave allow, the game chosen first, and only while the
mosaic shows. On the board, the game moves on with each move received; going back to analyse it lets it move on
behind, and *Back to live* brings it back; *Back to the mosaic* (Escape) returns to the round.

Lichess lets four rounds be followed at once without an account, eight with one. Since August 2026, it slows down
heavily a broadcast followed without an account allowed to read broadcasts: the first time, RookStudio asks for that
permission (*Allow on Lichess…*, or *Follow without it*). A round whose games have all ended goes into the
*Broadcasts* database.

### More of a broadcast

On the right of a broadcast's mosaic, under the round, **the standings**: each player's rank, title, rating and
points as the organisers give them (the federation, team, performance and rating change under the mouse), the first
twelve or all of them; for a team event, the teams first, by match points then game points. Lichess gives them without
an account; they are asked again every two minutes while the mosaic shows.

*The whole tournament*, beside a round's name, follows all of the tournament's ongoing rounds, and each next one as it
starts: the mosaic shows the round live, and **The rounds** above it shows another (a click on the live one follows the
next ones again). Each round goes into the *Broadcasts* database as it ends. *Crosstable*, beside the standings, shows
the crosstable of the rounds followed in place of the boards (see [Tournaments and crosstables](#tournaments-and-crosstables)).

**Search the broadcasts**, at the top of the official broadcasts, looks for a recent one by its name: Lichess answers
only an account allowed to read broadcasts, asked the first time. *Back to the list* shows the broadcasts on now
again.

### Lichess links

A Lichess address pasted with Ctrl+V on the Board page or on the Databases page is recognised without asking Lichess
anything: a game (`lichess.org/abcd1234`, with `/white`, `/black` or a move after `#`), a study or one of its chapters,
a broadcast round or tournament, an arena, a Swiss tournament, a player (`lichess.org/@/name`) or your bookmarks. An
address RookStudio does not know is said so, and nothing is asked.

A card then says what it is, where its games go and what is asked of Lichess; Enter does it, Escape leaves it:

- a game or a chapter opens on the board, at the side and the move the address shows, or goes into a database;
- a study, a broadcast, an arena, a Swiss tournament or your bookmarks come into a database in the background, their
  games counted as they come, with *Cancel*; then *Open the database*;
- a round under way can be followed live instead (see [Official broadcasts](#official-broadcasts));
- a player's games go into their database, through [their sync](#a-players-games).

The database offered is the one chosen on the Databases page when the link was pasted there, else *Lichess games*,
*Lichess studies*, *Broadcasts*, *Lichess tournaments* or *Lichess bookmarks*, made when missing; another can be
chosen. A game is asked by its id, without an account; a private study with your token; broadcasts with an account
allowed to read them, asked once, as for following a round; your bookmarks need a linked account.

### Fair play

Lichess forbids any help during a game. With your account linked, RookStudio keeps to it wherever you play, in the
browser or on a phone:

- while one of your live games lasts, the Board page locks its Analysis, Tree, References and Questions tabs, and
  neither the engine, the threat, the tables, the cloud nor the assistant look at any position; the boards show none
  of the analysis's arrows nor its evaluation, those of the search the lock stopped included. A review under way
  stops, the passes it has done kept. Engine tournaments go on, without analysis on their Board. Everything comes back
  by itself when the game ends.
- a correspondence game locks the engine, the cloud, the tables and the assistant on the game on the board that passes
  through its position, from the first move to what follows it, a position of that game shown from a variation included;
  Lichess allows a book and a database there, so the tree of your databases and their reference games stay open. Before
  its first move, it locks the start position alone: every game passes through it. On any other game, everything works.
- while you are in an arena from RookStudio, every aid stays locked between two games too, the reports with them,
  until *Pause* (Alt+P) or the arena's end (see [Arenas and Swiss tournaments](#arenas-and-swiss-tournaments)).

A banner on the Board page says which game locks what. The Home page's Lichess card lists your games in progress,
those where it is your turn first, each with its clock or the time left to move, and *Open on lichess.org*, or *On the
Board* once RookStudio may play your games (see [A game on Lichess](#a-game-on-lichess)). Your Lichess bot's games are
not yours and lock nothing; during your own live game, the bot's search is not shown (see [A Lichess
bot](#a-lichess-bot)). Lichess TV never shows your own games, even featured, and the mosaic's engines rest during your
live game (see [Lichess TV](#lichess-tv)). Your engine offered to lichess.org's analysis board pauses during your live
game, and never analyses the positions of your correspondence games (see [Your engine on
lichess.org](#your-engine-on-lichessorg)).

Your games in progress are asked for with your account's token when the Home page shows, every half-minute while a
live game lasts and every ten minutes while a correspondence game does. The Lichess page offers once to hear them start
and end instead, while RookStudio is open: one more permission, reading your challenges; the permission to play your
games (see [On Lichess](#on-lichess)) hears them through the same stream. RookStudio reads the moves of the games you
play from it only, and plays only the moves you play. Without either permission, the lock comes when the Home page
shows.

### Your finished games

When one of your games ends, RookStudio brings it into *Lichess · your name*, a database made when missing, with its
clocks, its opening and, when you asked Lichess to analyse it, Lichess's evaluations. A banner on the Board page, and a
line on the Home page, say how it ended; *Review it* (Enter) asks Lichess once more for its analysis, which may have
been asked for since, opens the game and starts its review. *Not now* (Escape) puts the banner aside.

Lichess's analysis then shows beside your engine's: its curve on the graph, and each analysis's accuracy under the
two sides' figures in the Review tab, the same formula for both. The Lichess page's *Review my finished games at
once* starts the review by itself, unless the Board holds a game with unsaved changes or work under way; the banner
offers it then. An arena's games come in the same way, their review offered at the arena's end.

### Followed players

The Lichess page's *Followed players* card lists the players your account follows on Lichess and those you add by
name, each with a dot: red while they play, with the game's time control, green when they are online. Their status is
asked every fifteen seconds while the card or their mosaic shows, without an account.

*Their games in the mosaic* shows the games they are playing in the broadcasts' mosaic, each analysed by your engine,
eight at most, as Lichess allows an address, the rounds you follow and Lichess TV's games counted with them; Lichess
shows their moves three moves late, to anyone. A game that ends stays with its result until another takes its place.
Reading whom your account follows needs one more permission, asked the first time; *Without it*, the players added by
name are followed. Your own account and your own games never come there.

### Lichess TV

The Lichess page's *Lichess TV* card lists the channels of standard chess: *Top rated*, *Bullet*, *Blitz*, *Rapid*,
*Classical*, *UltraBullet*, *Bots* and *Computer* (players against Lichess's AI); the variants' channels are not shown.
Each gives the game Lichess features on it, its two players with their ratings, and its clock. The list is read again
when the card shows, at most once every 30 seconds.

*The mosaic* shows the channel's games in progress in the broadcasts' mosaic, its third source: the line at its top goes
from one to another (*Broadcasts*, *Followed players*, *Lichess TV*, *Lichess tournaments*), and the channels are chips
under it. The same
tiles, an engine of yours for each game, the game chosen on the right with *Follow on the board*. Lichess lists up to 30
games a channel, and lets an address follow 8 games at once, the rounds you follow (one each) and the followed players'
games counted with them: the mosaic shows what is left, the featured game first. A finished game shows its result a
few seconds, then gives its place to another of the channel. Lichess shows the moves three moves late to anyone
watching, and the mosaic says so.

*The Board* (or a double click on a tile) opens the game on the Board, which follows it; the card over its column says
what it is: *Lichess TV · Blitz · the featured game, 3+0*. When it ends, the Board goes on to the channel's next
featured game by itself, the game left a Ctrl+Tab away; *Stay on this game* keeps the board on it. *Keep this game…*
puts it, at its end, into a database of your choice: *Lichess TV*, made when missing, or another of yours; until then
it waits in your profile, even if RookStudio closes. *Back to the mosaic* (Escape) returns to the channel's mosaic.

On the Engines page, an engine's card has *Lichess TV: Bots, Computer*: Lichess's best bots, or players against its AI,
in the mosaic, that engine analysing each game.

Your own games never show, even featured: those of your linked account and of your Lichess bot (see [A Lichess
bot](#a-lichess-bot)). While one of your live games lasts, the mosaic's engines rest (see [Fair play](#fair-play)).
What leaves the machine: the channel asked and the games followed, without an account; nothing is kept by itself.
Ctrl+K has *Lichess TV*, each channel in the mosaic (and on the Board once the card was read), and, for the Board's
game, staying on it or going on to the channel's next game, keeping it, and going back to the mosaic.

### Lichess tournaments

The Lichess page's card *Lichess tournaments* lists Lichess's arenas in progress and to come, read without an account:
*Rapid*, *Classical* or *All*, each line with its name, its time control, when (*38 min left*, *in 40 min*),
its players, *Follow*, and *Join* or why not: *Blitz: on lichess.org* (a click opens it), *condition not met* (under
the mouse, which one, with your figures). The variants' arenas are not shown; the list is read again when the card
shows, at most once a minute.

*Your teams' Swiss tournaments*, under it, lists those of the teams you are a member of: reading your teams needs one
more permission, `team:read`, asked once (*Read my teams…*). Each line gives its rounds, its time control, when, its
players, and *Join*; *Follow* while it runs; *Keep it in a database…* once it is over.

*Follow* shows the tournament in the broadcasts' mosaic, its fourth source beside *Broadcasts*, *Followed players* and
*Lichess TV*: the games of its first eight in progress, as the tournament gives them, a finished game leaving its place
to the next; Lichess shows their moves three moves late, and lets an address follow eight games at once, the rounds and
the players you follow counted with them; each analysed by your engine, as the mosaic does. On the right, its standing,
asked every 15 seconds: each player's rank, rating, sheet (each game's points, the newest last as on lichess.org, a
streak's doubled points in orange) and score; a Swiss's after its last round. A click chooses a game, a double click opens it on the
Board page; your own game never shows. A Swiss does not list its games in progress: they are found by player, its
first eight's, at each round. Without an account; nothing is kept by itself. Ctrl+K has *Lichess tournaments: arenas
and Swiss tournaments*, *Follow the arena: …* (once the card has read the list) and the tournament followed in the
mosaic.

### Preparing a game

The Lichess page's *Prepare a game against* card takes a Lichess name and opens their dossier (see
[A player's dossier](#a-players-dossier)) with its Lichess source ticked. Their games of the last year, blitz, rapid
and classical, come into a database of their own, *Lichess · name*, synced again the next time, and their public
profile gives their level. The dossier then shows what they play online beside what your databases have of them (the
player whose Lichess account it is, once linked), your games against them (your account linked), and the lines they
play often that your repertoires do not answer, over both sources, the most frequent first; a click on one opens it on
the Board, its Tree tab on their games. Only their name and those filters go to Lichess. While a game of yours against
them is played on Lichess, the dossier shows nothing of them.

### Lichess's evaluations, offline

The card *Lichess's evaluations, offline* keeps on your disk what Lichess's servers have computed for 418 million
positions (database.lichess.org, CC0): for each, the evaluation, the depth and the first move of its deepest search.
Nothing is asked of the network afterwards, and an evaluation shows at once wherever Lichess has one.

- **What is kept**: every position (about 3.6 GB), or the openings only (about 1.5 GB), the positions with 24 men
  or more on the board. Lichess's file gives no move number: in Lichess's games, 24 men or more is 89 % of the
  positions of the first 20 moves and 8 % of the later ones. Lichess's lines are left out: they would take nearly five
  times the room, for what your engine finds again in a second.
- **Download and prepare** (Enter) reads Lichess's file, about 21 GB, while it downloads, and keeps of it only what
  you chose; *A file already downloaded…* reads `lichess_db_eval.jsonl.zst` from your disk instead. While it
  prepares, it needs a little more than twice the room of what it keeps; the card says what your disk has free.
- The positions read answer a few million at a time, without waiting for the end. *Cancel*, a network cut or the
  program closed: *Go on* takes up the file where it stopped, *Start again* forgets what was read.
- Ready, the card gives the positions, the size and the file's date. Lichess publishes the file again each month:
  *Update* asks database.lichess.org for its date and reads a newer file, the evaluations kept answering until it is
  ready. *Remove them* frees the disk.

They show in three places:

- the Analysis tab: a line above the engine, *Lichess offline · +0.31 · depth 46 · 6…e6*, White's score (a click
  shows the move on the board);
- the review of a game: a position known at depth 30 or more is taken as it is, without a search, and the engine
  searches the others (its last pass, 5 seconds a position by default, seldom goes deeper);
- the Tree tab: an *Eval.* column, the evaluation after each move.

What leaves the machine: the request for the file to database.lichess.org, and nothing else. While one of your games
is played on Lichess, they hide as the engine does (see [Fair play](#fair-play)).

### Online for the analysis

Three services, each asked about at its first use (for this position, always, or not now) and changed here:

- **Lichess's cloud evaluation**, in the Analysis tab: the FEN of each position shown is sent, without an account,
  and Lichess answers with the lines its servers have already computed, when it has them. Your engine always answers.
- **Endgames online, up to 7 pieces**: for a position of seven pieces or fewer that the tables on your disk do not
  know, its FEN is sent to Lichess's tables, without an account. They give the verdict and how far the end is, in the
  Analysis tab, and each move by its outcome in the Endgames tab, with the main lines of its moves (see [The Endgames
  tab](#the-endgames-tab)). A win the fifty-move rule turns into a draw, a “cursed win” in the tables' words, is said
  “won, but drawn by the fifty-move rule”. With eight pieces and opposing pawns, they give the verdict and the moves
  to the next capture, promotion or mate. A finished tournament's check of its endgames asks them a few positions a
  game, once allowed for that check or always (see [Checking the endgames against the
  tables](#checking-the-endgames-against-the-tables)).
- **Lichess's explorer**, in the Tree tab: for each position, its FEN and the filter chosen are sent with your
  token; it needs a linked account. Nothing of your databases is sent.

The answers are kept in your profile: the tables' for good, the cloud's for a month. When Lichess asks for a pause
(too many requests in a short time), the countdown shows; offline, what still works is said.

## The assistant

The assistant comments on your games and answers your questions about them, on your machine. It relies on what your
engine, your databases and the endgame tables establish, and says it in words; when nothing establishes it, it says
so too. It writes in English or in French (in English for the other languages of the interface).

### Its model

The assistant needs its model, *RookStudio commentary v11*, downloaded once from huggingface.co (about 2.7 GB on the
disk, and about 3 GB of memory while it serves), checked when it arrives; a download cut resumes where it was. The
first card of the Questions tab, of *Comment this move* and of the review offers it; *A GGUF file on the disk…* takes
another model, which answers questions but has not learnt RookStudio's comments (those stay the templates'). Only the
model's download goes through the Internet: your games, your questions and its answers stay on the machine.

It runs in a process of its own, on the GPU when there is one to use (a comment in about a quarter of a second), else
on the processor (about 3 seconds). It is loaded at the first question or comment, and unloaded after a quarter of an
hour without serving, or with *Unload* (on the Home page, in **Settings**, or in Ctrl+K). While a tournament uses the
GPU, it answers on the processor (*Take the GPU anyway* if you wish); a graphics driver that fails sends it to the
processor, and *Try the GPU again* tries again. *Settings* > *Assistant*: its model, the GPU or the processor, the
minutes before it is unloaded (0: kept until *Unload*).

Without the model, everything else works: the analysis, the review's figures, the template comments, the tree.

### Questions

The Questions tab (Alt+5) takes a question about the position or the game, typed or chosen among the suggestions: why
the last move, what the threat is, the best move, who played this position, what this opening is. It can explain a move,
give the best move or the threat, try a move, compare two; give the reference games and the moves played in a database;
name the opening; tell what the endgame tables say. Each answer shows what it rests on (the engine's search, the tree,
the opening names, the tables) and the actions it offers: play the move, show the line in preview, open the strongest
game, add the answer to the notation. A question beyond what it can do gets an “I cannot tell” and what it can show
instead.

The engine of the Analysis tab answers for it, in a process of its own (one thread, 300,000 nodes per position): the
questions need an engine in the library.

### Comments by the assistant

*Comment this move* (Alt+C, or Ctrl+K) writes the assistant's comment on the move shown, and *Comment the key
moments*, in the review, one on each key moment. Its comments are shown in blue grey and in italics, after a bubble; at
the cursor, what each rests on: the engine, the facts of the positions, the database of the theory, the model or a
template. Without the model, the comments are templates, written from the same facts.

A move that has a comment already (yours, Lichess's analysis of a game, a game's annotator's) keeps it as it is: the
assistant's comes after it, apart, in a box of its colour under its name. In the PGN they are two comments in a row,
the assistant's marked `[%rs assistant]`; another program reads two comments, and RookStudio gives each its place
again. Ctrl+Z takes the assistant's comment back. A double click corrects it, and it becomes yours: on a move that has
a comment of yours already, the corrected text is added after it, as one comment. An answer of the Questions tab added
to the notation is the assistant's comment too.

## Test suites

The **Suites** page runs an EPD test suite: positions with their best move (`bm`), the moves to avoid (`am`), or,
for the Strategic Test Suite, the points of several moves (`c0 "f5=10, Be5+=2"`).

- **Suite**: an EPD file, or an EPD book of the library. The lines without a legal position or without one of
  these operations are left out (their number is shown).
- **Engines**: any number of engines of the library, with their options; *Hash* and *Threads* force a value on
  every one (0 = each engine's own).
- **Time**, **Nodes** or **Depth** per position. *Found for (plies)*: a search stops once a solution has stayed
  the engine's first move for that many more plies of depth (0 = every search takes its whole time).
- **Automatic concurrency**: with a time, physical cores − 1 divided by the engines' threads; with nodes or a
  depth, every hardware thread.

A position is solved when the engine's final move is a solution: one of `bm`, none of `am`, or, with points only,
the move with the most. Its time to solution is when the engine settled on that move. The grid shows the
positions against the engines, ✓ with the time to solution or ✗ with the move played, and the STS points; the
totals come first: positions solved, their average time, and the STS score. A finished run is kept in `suites/`
in the profile; *Compare with a saved run…* adds the columns of an earlier run of the same suite, to compare
versions of an engine. *Export CSV…* writes the grid.

## New tournament

**Tournaments → New tournament** opens the setup: a card of folded rows saying how the games are played, the
participants with the engine library beside them, and a live summary on the right (number of games, games per
engine, estimated duration, openings, common settings, time odds). The setup of the last tournament is kept, so
testing two versions of one engine again is: choose A and B, **Start**.

Everything on the page can be done from the keyboard: Tab and the arrows move between the rows, the lists and the
buttons, Enter or Space opens a row or a list, and the keys of the [participants and the
library](#participants-and-the-library) work there. **Start** creates the tournament and plays it; **Add to the
queue** keeps it for later (see [Queue and relaunch](#queue-and-relaunch)).

### The summary and its checks

The summary gives the memory the hash tables can take: two engine processes per game played at the same time, and
the engines waiting for their next game, each with its `Hash`. Above the free memory it warns (the system will swap),
naming the engine with the largest `Hash`; above the memory installed the tournament does not start. `--run` says the
same. When the memory budget of **Settings** lets fewer games play at once than the concurrency, the summary says how
many (each game takes the `Hash` of its two engines and what their processes take besides), and so does the
*Concurrency* row; the same for the games with an engine that uses a GPU, against the *GPUs*. It also names the
engines whose program changed since their last tournament (a new build), with *What changed* when the engine's card
lists new defaults or options (see [Rebuilt programs](#rebuilt-programs)).

What would go wrong is said with the engine, the resource and the fix, a button under the line (the command palette
lists the same fixes):

- a program or a working directory not found: *Open the engine* (its card on the Engines page), *Locate…*; an
  opening book not found: *Locate…*;
- an engine whose own `Threads` (its setting, else its default: some engines take every core) is above half the
  hardware threads, or above the threads each game has with the games played at once (the physical cores with a
  clock): *Lower Threads to N* sets the engine's option in the library, *Open the engine*. `Threads` given by the
  setup beyond the machine get *Lower Threads* where they are given (the participant's cell, the common settings).
  The participants list marks such an engine with a *!*, the match's engines are flagged under them;
- an option value an engine refuses (out of its range, not one of its choices, an option it no longer has):
  fastchess does not send it, and the engine plays with its default;
- the Syzygy tablebases the tournament uses: the adjudication's folders and what each engine's `SyzygyPath` points
  to (several folders separated by `;` on Windows, `:` elsewhere). For the pieces used (the adjudication's *Max
  pieces*, else the largest tables found), every WDL file (`.rtbw`) must be there, and no file cut short (a Syzygy
  file is 16 bytes more than a multiple of 64 long; engines leave out a file of another size, and some stop when they
  meet it): *Syzygy 6 pieces: 14 files missing in D:\TB (KRPPvKR.rtbw…)*, *Adjudication up to 6 pieces, but only
  5-piece files found*, *SyzygyPath of Alpha points to a folder with no tablebase files*, with *Open the folder* and
  *Lower to 5 pieces*; the tooltip lists the files. These are warnings: the adjudication and the engines do without
  the missing files. Only an adjudication without a folder, or whose folders hold no table, stops the tournament at
  its start (fastchess refuses it): *Locate…* chooses another folder. The folders are read in the background, and
  again every few seconds while the summary is shown.

A start refused names the same problems, with their fixes and *Open the log*. Each problem has a **?** that opens its
entry in [Troubleshooting](#troubleshooting).

### Profiles and templates

**Profiles.** The first five rows, *Format*, *Time control*, *Openings*, *Adjudication* and *Engine settings*, are
each a named profile: its list chooses one, the line beside it says what it plays ("40 moves in 15 min, repeated",
"draw 40/8/10 cp · TB 6 pieces"; its tooltip gives the same as cutechess and fastchess write it, `tc=40/900`,
`-draw movenumber=40 movecount=8 score=10`), and ✎, or a click on that line, opens the details in place under it.
A change made for this tournament only puts a dot beside the profile, and its list and its details offer *Update*
(the profile takes the change, for every tournament and template that uses it), *Revert* and *Save as profile…*
(under a new name). The program comes with presets:

- time controls: 10+0.1 and 60+0.6 (fishtest's short and long tests), 2+1, 40/4, 40/15 and 40/20 (40 moves in 4,
  15 or 20 minutes, repeated, as rating lists play them), and 200k nodes per move;
- formats: SPRT gainer [0, 3] and non-regression [-5, 0], a match of 1000 games, a round robin, a gauntlet, a Swiss
  of 9 rounds, a knockout and a team match;
- adjudications: none, a standard one (a draw from move 40 after 8 moves within 10 cp, a loss after 3 moves at
  600 cp) and the same with the tablebases up to 6 pieces;
- engine settings: each engine's own, or Hash 16, 64 or 256 MB with 1 thread and the engines' own books off;
- openings: no book, or Chess960's 960 positions.

A profile of yours saved under a preset's name takes its place; deleting it (from its list, after a confirmation)
brings the preset back, and the setups and templates that used it keep its values. Profiles are kept in
`profiles.json` in the profile folder, and the command palette (Ctrl+K) lists them: choosing one applies it to the
setup. The palette also opens each row of the setup by its title, and loads or saves templates. The Syzygy
directories are in no profile: they belong to this computer, and the adjudication and the engines share them. The
*Concurrency* and *PGN and misc* rows hold what is this tournament's only.

**Template** and **Name**, beside *Start*: a template is the whole setup, a profile for each section plus what no
profile holds (the name, the participants, the concurrency, the PGN files, the broadcast). The menu loads one or
saves the setup as one (*Save the setup as a template…*, which asks before replacing a template of the same
name), and the command palette loads them too. A section that played as its profile when the template was saved
follows that profile, so updating a profile updates every template that uses it; a section changed for that
tournament keeps its own values. Templates saved by earlier versions load as they were, and their sections get the
profiles they play like. An empty name is made from the engines.

### Format

The tooltip of each format says what it is.

- *Match*: two engines, A (the engine under test; every statistic is from its point of view) and B (the baseline).
  A fixed number of games, until you stop it, or an SPRT: elo0, elo1, alpha, beta, model (`normalized` uses nElo
  bounds like fishtest and OpenBench, `logistic` classic Elo, `bayesian` BayesElo) and an optional maximum number of
  games. The SPRT gainer and non-regression profiles give the usual bounds, and the line under the bounds tells how
  many games the test is likely to last. *Calculator…* draws, as fishtest's SPRT calculator does, the probability
  that H1 is accepted and the expected number of games against the true Elo difference, from an expected draw ratio
  and the RMS bias of the openings; *Use these bounds* copies the bounds it was given back to the setup.
  *Elo bias* (0 by default) is added to both bounds, to test against a version known to be stronger or weaker by
  that much: [0, 3] with a bias of 5 gives the SPRT [5, 8], which the summary, the overview and the headless mode
  write `SPRT [0, 3] + 5`; the calculator and the line under the bounds take it into account. *Min games* (0, none,
  by default) holds the decision until that many games are played, the LLR still computed and shown, to guard a
  short test against an early lucky stop: an LLR beyond a bound before then is said under the LLR bar and the match
  goes on; once the games are there, the next pair that ends with the LLR beyond a bound ends the match. A maximum
  of games below the minimum ends the match without a decision. This is the quickest way to compare two versions of
  an engine, and nothing else is needed. *Colours* (*Alternate* by default) says who has White: A and B in turn, or *A
  always White* or *B always White*, as fastchess' `-noswap` (and `-reverse`), for odds games where the stronger engine
  must always have the handicapped side, or an opening tried from one side. The games still go in pairs, both with the
  same colours, but each game has an opening of its own, the next of the book (as fastchess' `-games 1`): the pairs and
  their statistics stay, the score includes White's advantage, and the RMS bias, which reversing the colours cancels
  out, is not shown. The summary and the match's header say it.
- *Round robin*: every engine plays every other one, *Cycles* times, each time two games with colours reversed
  (what cutechess calls rounds, of 2 games per encounter). The rounds follow Berger tables, so every engine plays
  once per round.
- *Gauntlet*: the first *Tested engines* (the seeds) play every other one, the others do not play each other.
- *Swiss*: a number of *Rounds*. Each round is two games, colours reversed, against an engine with a similar score;
  nobody meets the same opponent twice when it can be avoided. With an odd number of engines, the lowest ranked
  engine without a bye yet gets one, worth a point.
- *Knockout*: engines are seeded in their list order, the top seeds get the byes when the field is not a power of
  two, and the first two seeds can only meet in the final. A match is *Game pairs per match* pairs of games; a tied
  match gets one more pair at a time, up to ten, after which the higher seed goes through.
- *Team match*: two teams of engines, to compare one family of engines, or a set of versions, with another as a
  whole. *Pairings*: *Everyone against everyone*, every engine of a team against every engine of the other team, one
  opponent per round (a Scheveningen system; the teams may differ in size), or *Board by board*, the first engine of
  each team against the first of the other, the second against the second and so on (teams of the same size). Each
  pairing is played *Cycles* times, each time two games on the same opening, colours reversed; the colours of the
  first game alternate from board to board and from one cycle to the next. *Team A* and *Team B* name the teams
  ("Team A" and "Team B" without a name); beside them, how many engines each has. The teams are made in the
  participants list: the upper half of the list is team A, the lower half team B (team A gets the odd one), until
  engines are put in a team.

### Participants and the library

**Participants**, with the **Library** beside them (under them in a narrow window). Each participant is a line: its
place (a star for the seeds of a gauntlet), its name, its reference Elo, what it plays differently from the common
settings ("Hash 512 MB · 80 % of the time") and ⋯, highlighted when it has settings of its own. ⋯, a right click, or
the Menu key opens them: its own `Hash`, `Threads`, fixed nodes or depth, time odds (a percentage of the time
control, for a clock), its own *Book*, *This engine once more, with another book*, the moves and *Remove*. The list
order is the seeding: drag participants to reorder them (seeds first), or press Ctrl+Up or Ctrl+Down to send them to
the top or the bottom, Alt+Up or Alt+Down to move them by one. Ctrl or Shift click selects several; Delete (or
*Remove*) removes them.

In a team match the place is the team and the board, A1, A2… for team A, B1, B2… for team B, in the list's order
(board by board, A1 plays B1). The ⋯ menu puts the participant in a team; *Team A* and *Team B* under the list put
the participants selected in one, and *Halves* makes the teams again from the halves of the list (the command
palette offers the same). An engine added once teams are chosen joins the smaller one. The summary says how many
engines each team has, and a start is refused, with the reason, when a team is empty or, board by board, when the
teams differ in size.

The library lists the active engines with their reference Elo, sorted by name or by Elo (click the column), a `!` on
those with a problem (program missing or rebuilt, last check failed: the tooltip says which) and a ✓ on those that
play already. The filter searches the names, the names the engines report, their authors, programs, notes and groups
(Ctrl+F gives it the keyboard; Enter in it adds the engines selected, or the first one shown); a chip per group shows
only that group, and *Add all shown* adds the whole group. Double-click an engine, or select several with Ctrl or
Shift (Ctrl+A: all those shown) and press Enter, to add them; or drag them to a place among the participants.
*Suggest…* selects the N active engines that work and whose reference Elo is closest to the first participant's (the
engine under test of a gauntlet), 150 at most away by default; Enter adds them. For a match, the library fills A,
then B (B is replaced when both are chosen), and an engine dragged onto A or B takes its place.

### Participants' own books

*Book*: an opening book of the participant's own, like Cute Chess' `book=`/`bookdepth=`: a Polyglot (`.bin`) or PGN
book of the Books page and how deep it is followed, in plies from the start of the game, the tournament's opening
included (*all*: as long as the book has a move; for a PGN book, its games' first 40 plies). A PGN book is first
turned into a Polyglot book: each game's moves up to that depth, each move weighted by the number of games that play
it; games with an illegal move are left out and counted. This runs in the background as soon as the book is chosen
(progress on the *Book* button, in its popup and in the summary) and is kept in the profile's `book-cache` folder
until the file or the depth changes; a start that comes before the end waits for it. A book that cannot be prepared
shows why in its popup, with *Try again*; each start also tries again. A tournament plays from the book prepared when
it was started or resumed, even if the PGN file changes meanwhile.

While the position is in that book, the participant plays the book's move at once, chosen by the book's weights as
the *Randomness* of the openings makes them (also in the *Book* popup, the same for every book of the tournament: see
[Openings](#openings)); then its engine takes over. The tournament's openings, if any, are played first, by every
engine; EPD books only give those and are greyed in the list. To compare books with one engine, choose *This engine
once more, with another book* in its ⋯ menu: each copy plays as `<engine> (<book>)`, so its results and rating stay
apart. In a match the *Book* buttons sit next to A and B, and an engine can play itself with two books. Book moves
are marked `{book}` in the PGN; hovering a name in the standings shows its book. WinBoard engines can have one too.

### Engine settings

*Hash* and *Threads* forced on every engine, for a match too (0 = each engine's own; a participant's own, from its ⋯
menu, comes first); *Syzygy tablebases for the engines* gives the tablebase directories (the same as the
adjudication's) to every engine with a `SyzygyPath` option, and *Engines' own books off* sets `OwnBook` to false
where it exists, so that every game is played from the tournament's openings. Both are off by default.

### Time control

Base time and increment in seconds, optional moves per period, and a margin before a game is lost on time; a fixed
time per move (`go movetime`, with the same margin); a fixed number of nodes per move; or a fixed depth in plies
(`go depth`), which no engine can lose on time.

### Concurrency

The number of games played at the same time. In automatic mode it is measured at startup:

- fixed nodes or depth: one game per hardware thread, since Hyper-Threading does not change the moves played;
- with a clock: physical cores − 1, so that engines do not slow each other down.

The row and the line under the field say which rule chose the number. *CPU affinity* (off by default; not on macOS,
which has none) pins every game to one processor, shared by its two engines since they never think at the same time.
fastchess only supports it for single-threaded engines, so it is turned off automatically when an engine has
`Threads` > 1. A warning appears when concurrency × threads exceeds the hardware threads (or, with a clock, the
physical cores). *Run on* chooses the machine the tournament plays on (see [A tournament on a
machine](#a-tournament-on-a-machine)).

### Openings

An EPD, PGN or Polyglot (`.bin`) book (an opening suite), random or sequential order, the number of plies kept from a
PGN book, the starting index and the random seed (0 = a new order for every tournament). Every opening is played
twice, with colours reversed (in a match whose *Colours* are locked, each game from an opening of its own).

A Polyglot book is followed from the initial position to a depth (8 plies unless set), leaving out the moves that
weigh less than a tenth of the best one of their position; the positions reached are written once to
`<book>-<N>plies.epd` in the tournament folder and played like an EPD book. In random order they are written in the
order they are drawn, each once: a line comes the sooner the likelier it is, by the weights of its moves, so that a
tournament that plays only part of the book plays its main lines first; the seed draws the same order again. In
sequential order they keep the order of the book. *Randomness* (−100 to 100, 0 by default) says how the weights
count, as Polyglot and Cute Chess choose a book move: 0 the weights as they are, −100 the best move of each position
only (one line, unless several share the best weight), 100 every move alike (the rare moves come back); in between,
−50 squares the weights and 50 takes their square roots. It chooses the moves of the participants' own books too, and
is shown for them even with an EPD or PGN book; the summary says it, and the line under the standings copies gives
the book with its depth and, for a Polyglot book, this randomness.

In a round robin, a gauntlet, a Swiss or a team match, *The same opening for every pair of a round* gives every game
pair of a round the same opening (board by board, a cycle is one round: every board plays the same opening), the next
of the book for each round, so that no engine gets easier openings than the others of its round (an engine added
later catches up in rounds of its own, each with its opening).

*Flag unbalanced openings* (off by default) shows, as the games come, the openings that decide the game by
themselves: a game is flagged when it ends if both engines saw the same side ahead by at least the score (200 cp by
default) for each of their first moves after the book (4 by default; a participant's own book moves count as book
moves). It reads the engines' scores, so an engine that gives none flags nothing; the results are not changed. The
*Games* tab filters such games (*Unbalanced opening*), the *Openings* tab marks their openings with a `!` and counts
them, and the overview counts the games. *Play the pair again from another opening* voids such a pair once its two
games are played (they stay in the PGN files and the journal, as any voided game) and plays it again from an opening
at the end of the book (in the book's playing order), which the tournament would play last, so that the openings of
the pairs to come do not move; at most three times for one pair, after which the pair counts, flagged. The console and
the overview say how many openings were replaced.

### Chess960

Fischer random, in the openings: without a book, the 960 starting positions in random order (written once to
`chess960.epd` in the tournaments folder); with a book, its positions. Every engine must play Chess960: UCI engines
with the `UCI_Chess960` option, WinBoard engines with the `fischerandom` variant. The summary names those that do not,
and such a tournament does not start.

### Adjudication

Draw (from a move number, for N consecutive moves, under a score), resign (N consecutive moves above a score,
optionally both engines agreeing), a maximum number of moves, and Syzygy tablebases.

### PGN and misc

- Event name: its tooltip names the two PGN files every game is written to.
- *Also append every game to*: a PGN file of yours that every tournament adds its games to, like cutechess'
  `-pgnout` (never overwritten, in the ChessBase / Lichess or cutechess comment style), in schedule order: the two
  games of an opening side by side, rounds in order, whatever order they end in. A game waits there until those
  before it are written; a resume carries on; when the file cannot be written (locked, moved), the games wait and the
  tournament goes on, with a warning in the overview. A crash or a power cut while games are appended never writes
  them twice: the resume checks the file and completes a write cut short, and never removes anything from it.
- Extra fields in the move comments (nodes, seldepth, nps, pv, tbhits, and the decision time `dt=`: when the engine
  settled on the move it played; `wdl=W/D/L` is always written for the engines that report it), on by default since
  the charts of the *Games* tab read them (pv makes the PGN noticeably larger).
- *Restart an engine that crashes*, and *Stop the tournament when an engine cannot start* (off by default: see [An
  engine that cannot start](#an-engine-that-cannot-start)).
- Keeping the engine log of every game (it is always kept for the games that end badly).
- *Check the mate PVs*: a mate score's PV must have the right length and end in mate. Every PV is checked for illegal
  moves, moves after the end of the game and a best move that is not its first move; the moves with a wrong PV are
  marked in the Games tab and written in the log.

### Tips for the settings

- **Comparing two versions of an engine**: an SPRT, gainer [0, 3] for a change meant to gain, non-regression
  [-5, 0] for a simplification or a cleanup. A short time control (10+0.1) first, then a long one (60+0.6) for what
  passes. Use a book of balanced positions, not the starting position: every game would otherwise be one of a few
  openings.
- **Concurrency**: leave it automatic. With a clock, more games than physical cores less one make the engines slow
  each other down, and the results drift; with nodes or a depth per move, every hardware thread may play.
- **Threads**: 1 per engine for testing; many games of one thread each give more results per hour than few games
  of many threads. Give every engine the same `Threads` and `Hash` in *Engine settings*.
- **Hash**: enough for the time control (16 to 64 MB at 10+0.1, 256 MB at 40/15 and longer); the summary says when
  the games' hash no longer fits in the memory.
- **Adjudication**: the standard profile saves time in long tournaments without changing the results much; leave it
  off to test an engine's endgames, or use the tablebases.
- **Openings**: random order with a fixed seed lets a test be played again exactly; *The same opening for every pair
  of a round* is fairer in a round robin with few cycles.
- **Several tournaments**: give the one that matters most a larger weight rather than pausing the others (see
  [Several tournaments at once](#several-tournaments-at-once)).

## Running a tournament

A running or opened tournament has a workspace with a header (name, progress, *Pause*, *Stop*, *Clone*, *Folder*,
*Layout*) and panels, shown by default as tabs:

| Tab | What it shows |
|---|---|
| Overview | The results so far and the statistics, the charts, the submission checklist |
| Standings | The standings as a table or a crosstable, an engine's opponents, the copies for a forum |
| Teams | A team match: the teams' score and the crosstable of the two teams |
| Bracket | A knockout: the bracket, round by round |
| Schedule | Every game pair, and the changes to the tournament |
| Broadcast | Live files, the TLCS server, the web page and lichess |
| Live | The games in progress |
| Games | The finished games, their filters, their replay and their export |
| Openings | The results by starting position |

### Overview

**A match**: Elo and nElo with their 95 % confidence interval, LOS, score, pentanomial counts, SPRT bounds and LLR.
*Statistics* (folded) gives fishtest's numbers from the games and from the pairs: score, Elo, nElo and BayesElo with
DrawElo, standard deviation per game, LLR of both, the variance ratio of pairs to single games, the RMS bias of the
openings, the kinds of balanced pairs and the pairs won over lost, which *Copy* includes. Then the charts: Elo and LLR
over time, game lengths, how games ended, and *Search per engine*.

**A tournament**: the standings (with *TF*, each engine's losses on time, in orange as soon as there is one; a *!*
beside an engine that crashed or played an illegal move), the Elo of each engine with its 95 % interval as a chart under
them, then *Search per engine*, progress by cycle or round, games per hour and time left, the latest results.

*Search per engine* gives each engine's average over the moves it searched, book moves left out: depth, NPS, time per
move, decision time and processor use; a click on a column sorts the engines by it. Beside its NPS, ×4 says the
engine searches with 4 threads: it counts more nodes than on one without being any faster, so compare its NPS with
engines that have as many. The NPS needs engines that give it, and *nps* in *PGN and misc* to be read back on a resume.

Both count the games from unbalanced openings, and the openings replaced, when the setup looks for them, with *Show*
for those games. A finished tournament adds a *Submission checklist*: all games played, every pair with both colours,
no duplicate games, no crashes, time losses or illegal moves (with buttons that open those games), the hash of each
engine (shown, not judged), ponder off, the opening book, the tablebases.

### Standings

Points, games, losses on time (*TF*), wins/draws/losses, ratings with their confidence interval, Sonneborn-Berger
(Buchholz for a Swiss), byes. *Table* lists them one engine per row; *Crosstable* (round robins, up to 200 engines)
shows every pairing. *Order:* by points, score % or seed order (which keeps the columns of a gauntlet's crosstable in
place). A click on an engine lists its opponents below: games, with White, with Black, + = −, points and %, with the
pairs of which only one game was played marked; a gauntlet opens with its seed selected.

*Copy* as text, or as BBCode for a forum post (with a line giving the format, time control, games and book), and in a
gauntlet *Gauntlet table as text* or *as BBCode, for a forum* (the seed, then its opponents by their own points, each
with points, %, Elo ±, W-L-D); *Export HTML…*. All have *TF* like the headless mode's standings, follow the order
chosen, and end with a line giving the games played of those planned, the draw rate, time control, book (with the
plies kept of a PGN book; for a Polyglot book its depth and how its moves were chosen, e.g. `book:Perfect2023.bin (8
plies, moves by weight)`) and event. The copies then sign with *RookStudio* and its version, and the gauntlet table
is ruled with = rather than -, so that a table posted on a forum is told apart from those of other programs.

### Teams

A team match only, in the *Standings* tab (where it opens) and atop the *Overview*: the teams' score, "Lynx 17½ – 18½
Otter", with the games played, team A's score %, the Elo difference that score stands for with its 95 % interval and
the LOS, counted on the pairs of games as the statistics of a match are (pentanomial); then each team's engines with
their games, points, %, rating and the points each scored against each engine of the other team (B1, B2… as columns,
the name at the cursor): the crosstable of the two teams. *Table* and *Crosstable* show the engines as in any
tournament. *Copy* and *Export HTML…* give the teams' scores and both teams' engines against each other, with the line
under them.

### Bracket and Schedule

*Bracket*, a knockout only: the bracket, round by round, with the score of every match. *Schedule*: every game pair
by cycle or round, with its state and the opening it plays (once started, or once its round started when the round
shares one), and the changes to the tournament (see [Changing a running tournament](#changing-a-running-tournament)).

### Live

The games in progress, as a mosaic of boards or one game in focus with the clocks, the running search of each engine
(depth, score, nodes, nps, PV in SAN), the move list and the game graph (see [The game graph](#the-game-graph)): the
two engines' evaluations, and in its tabs their win chances, time per move with the decision time, depth with
seldepth, nodes, speed, tablebase hits, how far apart they are, or how many plies a PV repeats of what the other
engine's PV predicted. The win/draw/loss shares (`UCI_ShowWDL`, switched on for the engines that have it when they are
added) follow each search line; the *WDL* tab draws them as bands, White's wins at the bottom.

Given the room (one game in focus, a large panel), each engine has a card: its logo as high as its name and clock (when
the logos are on, see [Appearance](#appearance)), what it does (thinking, or the move it played), its search figure by
figure (depth and seldepth, score, nodes, speed, the tablebase hits and the hash fill when the engine sends them, the
time spent on the move so far) and its whole PV; the engine that waits shows what it said when it played, with the
moment it settled on its move. Beside the mosaic, the panel keeps one line of figures and the PV's first 12 plies.
*Score of the side to move* | *Score of White*, at the right of the tab, sets the side of every score in it: the side to
move's, as fastchess writes them in the PGN, or White's; the evaluation bar and the big screen are always from White's
side.

Above the moves: the opening, the ECO code and name of the deepest position of the game that the table of named
openings has (book and engine moves alike, so that a transposition after the book is named too), with the book's last
move; and once the tournament's Syzygy tables (its adjudication's, else those given to the engines) know the position
shown, their verdict: the side that wins with the DTZ (the half-moves to the next capture or pawn move: Syzygy tables
have no distance to mate), or a draw, the 50-move rule counted from the game's own counter unless the adjudication
ignores it. Each position is probed once, outside the threads that play the games. In the list, each engine move has
its score, depth and time; hovering over it adds the moment the engine settled on it and the reply it expected (the
second move of its PV: fastchess plays without pondering). Book moves say *book*, a line marks the end of the book, and
the move being thought of says *thinking*.

The mosaic has a board for each game the tournament has played at once, not for each game the concurrency allows: with
fewer games than that, the boards grow. A board whose game ended keeps its final position until the next game of its
slot starts there: near the end of a tournament the boards stay in their places, each with the end of its last game.
When the boards would need a scroll bar, the mosaic takes width from the game in focus, down to 40 % of the page for
it.

Under the board: the 50-move counter (plies since the last capture or pawn move, out of 100), the material difference
with the pieces one side has more of, and the pieces left, marked *TB* when the tablebase adjudication can end the
game; *Details under the boards* adds them to the mosaic. Under them, the buttons through the moves (see [Boards and
their buttons](#boards-and-their-buttons)): the game in focus, or a detached board, can be stepped back while it goes
on (also with ← → Home End, a click on a move of the list or on the curve), the earlier position shown with its move
marked in the list and on the curve; *Back to the current move* (or End, or a click on the last move) makes it follow
the game again, as it does at each new game.

*Arrows* (on by default) draws the move the side to move is thinking of in its engine's colour and the reply it
expects, lighter, in the opponent's colour, on the board in focus and the detached ones (not the mosaic), when it
shows the current move. In a gauntlet, *Seed at the bottom* (on by default) shows each new game from the seed's side;
flipping the board (F, or its button) still turns it over until the next game. *Detach* opens a board in its own
window. A right click on a board ends its game with a result (see [Changing a running
tournament](#changing-a-running-tournament)). *Big screen* shows the game followed filling the window (see [Big
screen](#big-screen)).

### Big screen

For a television, a club or a stream, *Big screen* (in the *Live* tab, on a detached board, or in the command
palette) shows one game filling the window, light on black: the players' names and clocks in large type (the side to
move and its running clock in yellow, a clock under ten seconds in red), the evaluation bar, the evaluation, depth and
speed of the search, the last move framed on the board and written large, the opening, the engines' lines (*Lines*, on
by default: the side to move's as it searches, in yellow, the other's from its last move, two lines each at most), the
tablebases' verdict once they know the position, and the moves in large type, the last one marked; the result over the
board once the game is over. Its text follows the size of the window and has a size of
its own (*A-* / *A+*, or the + and - keys). It shows the game followed, or switches between the games in progress
every N seconds (*Every game in turn*), or follows *The most interesting* one: the closest to level, the longest of
those as close, kept while it stays so. The controls show for three seconds when the mouse moves (the pointer hides
with them); ← and → change the game, F11 makes the window full screen, Esc leaves. A detached board moved to another
screen (the television) shows it in its own window, F11 making that window full screen while the main window goes on;
docked in the main window, the main window shows it.

### Games

The finished games with their opening (ECO code and name), and a replay board: the buttons under it, ← → Home End
and F, or a click on a move or on the graph; the same details under it and the same game graph as in *Live* (see
[The game graph](#the-game-graph)), from the fields the PGN was written with: a chart whose field the tournament did
not write is in the graph's `⋯` menu, greyed, and says so with a button to the PGN options of the setup.

**Filters**: by result (wins, draws, losses, losses on time, crashes and illegal moves, adjudications, wrong PVs, and
the games of an opening flagged unbalanced when the setup looks for them); by what the evaluations tell (the engines
disagree by N pawns for 3 moves each in a row, an engine's evaluation swings by N pawns from one of its moves to the
next, the loser thought it was N pawns better; the game opens at that moment); and by opening (an ECO code or its
beginning, a range such as `B20-B99`, or words of the name).

**Export** of one, all or the filtered games: *Export all* proposes `<name>.pgn` next to the tournament's folder; the
tournament's own PGN files are refused as a target. *Export for a rating list…* writes the games as rating list
testers (e.g. CCRL) send them: `[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, a zip too if asked;
the players named as the lists write them, `<engine> 64-bit 4CPU` (editable for each participant), the rounds
numbered again, the `Event` and `Site` tags set, the voided games and the duplicates left out, and those lost by a
crash or a stall too unless unticked; the `PlyCount` of a game lost by an illegal move counts only the moves written.
The tester and the site are remembered.

*Engine log* opens the log of a game that has one (see [Files](#files)). A right click on a game plays it again, voids
it or sets its result (see [Changing a running tournament](#changing-a-running-tournament)).

### The Openings tab

Results by starting position: its game pairs, White's score, the draws, and how many pairs White won twice, Black won
twice (the opening favours a colour) or one engine won twice (it tells the engines apart), with the average length.
Sorted by any column, filtered to the unbalanced, the drawn or the decisive ones, or to those *Flagged unbalanced
during the games* (see [Openings](#openings) in the setup: marked `!`, their number beside the count, with the
openings replaced). *Export as EPD…* writes the positions shown, to keep them or to clean a book, *Without the
unbalanced* (on by default, shown once an opening is flagged) leaving out the flagged ones. A click shows the position
and its games. In a match whose *Colours* are locked, each game has an opening of its own: the count is of games, and
the columns and filters about pairs are not shown.

### Panels and layouts

Every tab is a panel. Drag one by its tab to dock it on a side of another panel or as a tab next to it, or drop it
elsewhere to float it (also onto a second screen on Windows and Linux/X11, not on Wayland nor with software
rendering). The game followed in *Live* comes apart in *Board*, *Players*, *Searches*, *Moves* and *Chart* panels; the
parts that have no panel open stay beside the mosaic in *Live*, and detached boards dock like the other panels. Close
a panel with the x on its tab; bring it back from *Layout* > *Panels*, or with *Show …* in the command palette, and it
comes back docked.

*Layout* also offers ready-made layouts (*Tabs*, as by default; *Follow a game*: the board on the left, the players,
searches and chart on the right, the standings under them; *Large tournament*: the mosaic beside the standings;
*Broadcast*: a large board with the players, searches and standings), *Save the layout as…* (your own, by name, kept
in `layouts.json` in the profile), *Delete* and *Default layout*. A panel taken out of the workspace stays on screen
on the other pages; the arrangement is kept between sessions.

### Processor use

While games are played, the processor time of every engine's processes (the engine behind a WinBoard adapter
included) is measured at each `go` and each move. The Overview shows, for each engine, the cores it keeps busy while
it thinks and the share of a core it uses while its opponent thinks, and warns when an engine uses clearly more cores
than its `Threads`, or thinks on its opponent's time (pondering it was not asked for, a background thread): what makes
a game unfair to the opponent and to the other games running. It needs 20 s of each to tell, and counts every game
played, in the window or headless (`--run`): what each game's engines used is written to `processor.tsv` in the
tournament folder, so a resume keeps it. It only warns; it never decides a game.

### Pause, stop and resume

**Pause** starts no new game pair. Games in progress play to the end, and so does the second game of a pair whose
first game has started, so a pause never throws a result away. **Resume** continues. **Stop** either finishes the
running games first, or stops now (the interrupted games are not counted and will be played again).

A tournament stopped by an engine says which one in its header and on the Tournaments page, under *Needs action*:
*Interrupted: Alpha could not start: …* (after its last try), or *Alpha crashed or stopped responding (game 12)*
(without *Restart an engine that crashes*), with *Open the engine* and *Open the log*.

**Every tournament resumes exactly where it stopped**, after a stop, a closed window, a crash or a power cut: no game is
lost and none is played twice, and a half-played game pair is completed with the same opening and colours. *Resume* on
the Tournaments page continues it, with the statistics of the games already played: results, and the engines' depth,
time per move and NPS read back from the move comments (NPS only when the PGN records it, *nps* in *PGN and misc*). When
the program ended while a tournament was running (a crash, a power cut, a reboot, or quitting while it ran), the next
launch resumes it by itself, and the queue goes on after it: *Resume at launch the tournament that was running*, in
**Settings**, on by default. A tournament paused or stopped waits for you, and `--autostart` starts the saved setup
instead. A participant whose program was rebuilt or went missing since it joined is named in the header of the resumed
tournament, with *Continue* and *Pause*: its next games are played by the program as it is now, with the options the
tournament gave it.

### Several tournaments at once

Several tournaments can play side by side: a gauntlet beside an SPRT, two gauntlets at a time. *Start* on the *New
tournament* page starts another one while some play; *Resume* resumes one beside them. Each tournament plays in a
process of its own (`rookstudio --run <folder>` driven by the window), so a crash takes down that tournament only,
and the window, its engine checks, quick tests and test suites no longer wait for a tournament to end.

The workspace has **a tab per open tournament**, above its header: a dot while it plays (a pause sign while paused), its
status at the cursor, a cross to close one that does not play. The panels show the tab chosen; the command palette lists
the others (*Show …*, *Pause*, *Resume*). A finished tournament opened from the list takes the tab of one only looked
at. The Tournaments page has a card for each tournament playing, with its progress, its leader, *Open* and *Pause*, and
under them its **weight**, the cores its games use of those it may use, and what its next game waits for; the *Cores*
bar at the top shows each tournament's part (and the Board page's), and how many cores the tournaments share. The *Log*
shows the lines of one tournament, of the window, or all (each line of a tournament carries its name); a tournament's
lines are also kept in `rookstudio.log` in its folder.

**Sharing the machine**: the tournaments playing share a budget of cores, set in **Settings** (*Cores for the
tournaments*: the physical cores less one by default). Each gets one game's worth first while the budget allows, the
heaviest weight first; the rest goes by weight (a tournament of weight 2 gets twice the cores of one of weight 1),
none above its own concurrency, what one cannot take going to the others. A game counts as the `Threads` of the
larger of its two engines: one thinks while the other waits. A paused tournament keeps only what its running games and
the second games of their pairs need. A smaller share never interrupts a game: the tournament plays fewer games from
its next one. A tournament alone plays as many games as its concurrency says.

**Memory**: a game starts only when the `Hash` of its two engines, and what each engine process takes besides (64 MB,
and 8 MB per thread), fit in what the games playing leave of the memory budget (*Memory for the engines* in
**Settings**, 75 % of the installed memory by default), shared between the tournaments. The setup summary says how
many games the budget lets play at once, and the Tournaments page's *Health* card when games wait for memory.

**GPUs**: an engine marked *Uses a GPU* on its card (a network engine such as Lc0) counts against the *GPUs* of
**Settings**: no more such engines play at once than there are GPUs, in a tournament and across tournaments, while the
other games play beside them. The setup summary says how many games with a GPU engine that allows, and warns when no
GPU is counted, or when a game between two GPU engines needs more GPUs than the machine has (it then plays alone).

**Keeping them running**: closing the window while tournaments play asks what to do. *Keep them running and quit* (the
default) leaves each one playing in its process; the window shows them again, as they stand, at its next launch. *Stop
and quit* stops them, each to be resumed exactly where it stopped. The quick test stops either way. A tournament whose
process is gone by the next launch (a reboot, a crash) is resumed as any interrupted tournament. The broadcast of the
*Broadcast* tab runs in the window, for the tournament shown: for a broadcast that goes on without the window, start
the tournament from a terminal with the broadcast options (see [Headless mode](#headless-mode)).

### An engine that cannot start

A night left alone does not stop on one engine. An engine that does not start (a program moved or rebuilt, a network
file missing, a drive gone) is tried again three times, 10 s apart; then its games are set aside and the other games
go on. It is tried again every 5 minutes, one game at a time, and its games come back as soon as it starts. The
tournament header and the Tournaments page (under *Needs action*) say which engine, the error, and offer:

- **Try again**: at once, rather than at the next try (after fixing the program, say).
- **Withdraw…** and **Replace…** (round robins, gauntlets and team matches): the Schedule tab's change panel, set for
  that engine, with the impact shown before anything changes.
- **Engine log**: `logs/start-<engine>.log` in the tournament folder, what was sent to the engines of its last try and
  what they answered, their error output included (often the reason: a library or a network file not found).

The log and the desktop notification say when an engine is set aside. The tournament stops only when nothing is left
to play without the engines set aside, after one last try of each; its status and the notification at the end say so
(*Stopped: no game is left to play without …*), and the queue goes on with the next tournament. *Resume* tries every
engine anew. A pause holds the tries. *Stop the tournament when an engine cannot start*, in the setup's *PGN and
misc*, stops it at once instead, once the tries are used up.

### Changing a running tournament

The *Schedule* tab's **Change the tournament** panel works without stopping anything: running games go on and no
played game is lost. Each change shows its impact before it is applied.

- **Add an engine**: it plays the cycles already started (*Catch up*) or joins from the next cycle.
- **Withdraw an engine**: its remaining games are dropped; its games played so far count or not.
- **Replace an engine**: its games stop counting and are played again by the new engine.
- **Number of cycles**: more or fewer.
- **Play games again**: every game of an engine, or the losses on time and crashes.
- Right click on a game (here, or in the *Games* tab): play it again, void it (no replay), or set its result.

An engine added or brought in plays with the tournament's settings (its time control, Hash and Threads, shown under
the engine), or with *Settings of its own*: Hash, Threads, share of the time (or nodes, depth), as a participant's
own settings on the setup page. A tournament made before 0.6.6 did not record its settings: those of its first engine
stand in for them, which may have been its own.

In a team match an engine added joins the team chosen and plays every engine of the other team; board by board an
engine cannot be added (its board would have no opponent): replace one instead. A replacement takes the team and the
board of the engine it replaces; more cycles pair the engines still playing (board by board, on the boards of the
first cycle). The games a withdrawn or replaced engine played still count for its team, unless they are voided.

**Set the result** decides a game the program could not decide well: a hang it did not catch, a won ending the
engines keep shuffling, a game that ended for a wrong reason. Choose 1-0, ½-½ or 0-1 and give a reason; the points it
moves are shown before *Set the result* applies it. The standings, the crosstable, the ratings and the submission
checklist count the new result (an adjudication: a loss on time set to a draw no longer counts as one); the *Games*
tab marks it with a `*`, its tooltip giving the reason and how the game ended as played, and *Back to the result as
played* takes it back. A game being played is ended the same way: right click on its board in the *Live* tab, *End
with result* (also in the command palette for the game followed). The engine on move is stopped, fastchess ends the
game at once and it counts with the result chosen; the engine starts again for its next game. It needs *Restart an
engine that crashes* (setup, *PGN and misc*, on by default).

Voided games and results set by hand stay in the PGN files and the journal as they were played: the PGN files are
only ever appended to. What reads the games applies them: voided games are left out, and a result set by hand is in
every export (*Export game…*, *Export all…*, *Export for a rating list…*, the head-to-head export of the *Ratings*
page, the file of *Also append every game to*) with `[Termination "adjudicated by the operator"]` and the reason in a
comment before the result. **Undo the last change** is offered until the next game starts: nothing has been played
under the change yet, so it can be taken back exactly (games that were already running when it was made are kept). A
result set by hand can be undone as long as it is the last change, games played since or not (in a knockout, whose
next rounds follow from the results, until the next game starts); the *Games* tab offers *Undo* beside the game too.

### Queue and relaunch

The **Queue** on the Tournaments page holds tournaments to play one after the other: when one ends by itself, the next
starts (not after a stop or an error, nor while *Pause all* holds the queue). With *Start when cores are free*, the next
one starts beside those playing as soon as the budget of cores is more than they can take, each at its concurrency. They
can be reordered or removed, and the first started now when nothing runs. The Tournaments page tells when the running
tournament ends (*Ends about HH:MM (in …)*) and when the whole queue does, each queued tournament with its length and,
while the lengths before it are known, when it starts; the tournaments whose length cannot be known (an SPRT without a
maximum, nodes or depth per move) are counted apart.

**Relaunch with…** on a finished match (in its header, or under *Last finished* on the Tournaments page) plays it again
with another engine A, typically the next version, against the same engine B, with the match's other settings (those of
the *New tournament* page for a match started by an older version, which did not keep them).

**Clone** (in a tournament's header, under *Last finished* and in *All tournaments* on the Tournaments page) opens the
*New tournament* page filled with that tournament's setup, whatever its format: the participants with their own
settings, the time control, the openings, the adjudication and the rest, every value as it played. Its profiles are
named, and one changed since is marked as changed for this tournament. Change what should differ (another time control,
the next version of an engine, another book), then **Start** or *Add to the queue*. A tournament still playing can be
cloned too; one started by an older version, which did not keep its setup, cannot.

### Checking the endgames against the tables

A finished tournament's menu, a right click on its header or on its row on the Tournaments page (or **Ctrl+K**), has
*Check the endgames against the tables*: the games that reached seven pieces or fewer, each result set against what the
tablebases say. The answer comes in a card under the tournament's header, “12 games reached seven pieces or fewer: 3
wins let slip (Alpha: 2, Bravo: 1), 1 lost position held, 2 draws adjudicated in a won position”, with a row for each
finding, *Open* showing its game in the *Games* tab at the move it is about:

- **a win let slip**: an engine's move after which the tables no longer give it the win (a draw, or the other side's
  win); **a draw let slip**: an engine's move after which they give the other side the win;
- **a lost position held**: a game drawn by the rules (a threefold repetition, the fifty-move rule) while the tables
  still give one side the win at its end;
- **a draw adjudicated in a won position**, by the tournament's draw rule or its move limit, and **a win adjudicated
  against the tables**, by its resign rule. The tables' own adjudication agrees with them; a game lost on time, by an
  illegal move or by a crash is judged on its moves only.

A few positions a game are asked: the first one the tables can tell and the last, and when their verdicts differ, a
bisection between them to the move that changed it (about ten positions for a long endgame). Your tables on the disk
first (see [Tables for the endgames](#tables-for-the-endgames)), then the answers Lichess gave before, kept in your
profile, then Lichess's 7-piece tables for the rest, once allowed: for this check, always, or not now (each position's
FEN goes to tablebase.lichess.ovh, without an account), one request at a time, each answer kept for good. Without them,
the games your disk's tables cannot tell are counted as left unjudged. Nothing of the tournament's page changes before
the click.

## Ratings

The **Ratings** page computes maximum likelihood ratings (the logistic model, draws as half a point, as Ordo does by
default) over every game of every tournament in the profile, and of PGN files imported. Engines are identified by
their name. The games counted can be narrowed at the top of the page, for every tab:

- a time control, a group of the engine library (the games between its engines), the last 30 or 365 days;
- *Imported games*: the games of the PGN files listed under *PGN files…* (a rating list's download, the games of
  another program), read from their headers; their engines are known by the names the PGN gives them, and a name
  that is a library engine's, letter case and spaces aside ("stockfish  17" for "Stockfish 17"), is that engine:
  its games count as the engine's and its reference Elo can anchor them. Nothing looser is applied by itself:
  "Stockfish 17 64-bit" stays a separate engine until you say which engine it is in *PGN players…* (beside the
  count of library engines, or in *PGN files…*). That window lists the players that are no library engine by their
  name, with a search: for each, *A separate engine* (the default) or a library engine. A name two library engines
  share, letter case aside, is shown in the warning colour for you to choose; a likely engine (its name begins the
  player's) is suggested as *Perhaps …?*, never applied. The window opens by itself after a PGN file is added when
  there is something to decide. The choices, a separate engine included, are kept with the engine (they follow a
  rename) and apply to every PGN file and to the rating lists imported for the reference Elo; the games are read
  again with them when the window is closed. The rating list says how many library engines the PGN files hold, its
  tooltip which;
- *Anchor*: the average at 0, or one engine at a fixed rating (by default the first engine with a reference Elo).

### Rating list

The list, with a minimum number of games. Besides Elo, ± and the games, each engine shows its score, its wins (+),
draws (=) and losses (−), its draw rate, *Opp.*: the average rating of the opponents it met, game by game, *LOS*: the
likelihood that it is stronger than the next engine of the list, from the two ratings and the covariance of their
errors (approximate beyond 1000 engines, as its tooltip says), and *Gap*: its Elo above the next engine. A click on a
column's header sorts by it (# keeps the place by rating); a right-click shows or hides columns (*Gap* is hidden at
first). A click on an engine opens its card.

*Copy* as text or BBCode, with *With a header* to put the header below above the list. *Export…*: CSV with every
column; an HTML page (light or dark) and Markdown (a table GitHub and forums show), both opening with a header that
tells how the list was made: the tournaments and PGN files counted, the games with White's and Black's wins, the draws
and White's score, the anchor, the filters, the minimum number of games, the date and the version; and *Games for
Ordo…*: the games counted as a PGN of headers, which Ordo and BayesElo read (`ordo -p ratings.pgn -o ratings.txt`).

### Engine card

An engine's card: its rating, its results overall and by time control, its losses on time, crashes and illegal moves
(each game opens in its tournament), its tournaments (each opens), its average depth, time per move and NPS read from
the move comments, and its rating after each tournament. The *Engines* page leads to it with *Its results…*.

### Head to head

Two engines against each other: the score, Elo and LOS, the pentanomial counts of the pairs they played both games
of, by time control and by tournament, their games (each opens in its tournament) and *Export the games…*.

### Progress

The rating of the engines ticked after each tournament they played, on a time axis: the versions of an engine side by
side.

## Broadcast

The *Broadcast* tab of a running tournament publishes it while it plays; the snapshot is refreshed twice a second.
*Game shown* picks the game sent to TLCS and the web page: automatic, or the one chosen in the *Live* tab.

### Live files

Written atomically in a folder of your choice, to publish anywhere (FTP, rsync, a website): `live.pgn` (the games in
progress), `standings.html` (standings and crosstable), `tournament.json` (everything, for scripts).

### TLCS server

UDP port, 16001 by default: the protocol of Tom's Live Chess Server, so TLCV and other TLCS viewers can follow the
tournament (board, clocks, PV, crosstable and the games played). It is checked against node-tlcv. *One port per game*
puts every concurrent game on a port of its own: game #1 of the *Live* tab on the UDP port, #2 on the next one, and so
on. Each port opens when its game starts, with its own limits on clients and logons, and gets the crosstable after
every game; *Game shown* then does not apply.

### Web page

TCP port: a page served by RookStudio itself, for any browser on the network, read-only and refreshed every
second. The followed game (the first in progress, or the one clicked) is in the middle: the board with the last move,
a king in check, the coordinates and the arrows; each player's clock in large type; each engine's evaluation in large
type with who is thinking, its search as tiles (depth/selective depth, nodes, speed, time, tablebase hits, hash) and
its line of play; and the moves, with the book moves in italics. Step through the moves with the arrows under the
board or the ← → keys (▶ past the last move goes back to the game as it is played), turn the board with ⇅ or F, and
copy the game's PGN or the position's FEN.

Beside the game, tabs show the standings and crosstable, the charts of the followed game, each engine's search on
average per move with its anomalies, the next games of the schedule (a Swiss or a knockout pairs its later rounds as
it goes), the statistics and anomalies of the games played, and the event. Every game in progress is shown below,
with a small evaluation bar beside each player. *Moves* switches the moves between letters (Nf3) and figurines (♘f3),
and the theme follows the system or is set to light or dark; the browser remembers these choices. The page loads
nothing from elsewhere and works on phones. *Download the games (PGN)* gives the tournament's PGN file, up to the last
game recorded. `state.json`, which the page reads, holds all of it for scripts. The engines' logos (see
[Appearance](#appearance)) show beside their names, as small PNG pictures the program makes from them and serves
itself; the page learns only those of the tournament's engines.

### lichess

A lichess broadcast, with lichess' board, evaluation bar and chat, for anyone on the internet. It needs a personal
API token with the `study:write` scope (*Create a token on lichess…*), kept in `settings.json` in the profile only,
saved when its field is left. With no round given, a broadcast named after the tournament is made, unlisted unless
*Listed among lichess' broadcasts* is ticked; its link is shown once it is on air. Otherwise the games go to the round
whose URL or id is given (a broadcast fed by pushes, not by a source URL). The games in progress are pushed every 8
seconds with their clocks, and each finished game once; a round holds 64 games, after which the next round of the
same broadcast is made. Only the games finished after it was switched on are pushed; `lichess.json` in the tournament
folder remembers where they went, so a resumed tournament continues the same broadcast. lichess keeps only the clocks
of what it is sent (it drops the `[%eval]` and the text of the move comments): its evaluation bar is its own, and the
engines' evaluations do not show there.

The connection uses Mbed TLS, built into the program, and trusts only the root certificates that sign lichess.org
(Google Trust Services) and those of Let's Encrypt. It goes through the HTTP proxy that `HTTPS_PROXY` (or `ALL_PROXY`)
names, as `http://proxy.example:3128` or `http://user:password@proxy.example:3128`, except for the hosts of
`NO_PROXY`; on Windows too the proxy comes from these variables, not from the system settings. SOCKS proxies are not
supported.

### Your own Lichess broadcast

*Broadcast on Lichess…*, in the header of a tournament that plays on this machine, opens a sheet: the broadcast's name
(the tournament's at first), unlisted (for those who have its link) unless *Public: listed on Lichess* is chosen, and what goes to
Lichess, the name, the tournament's format and time control, and each game's PGN. *Create the broadcast* asks your
linked account for one more permission the first time, creating studies and broadcasts (Lichess's page opens for it),
or links the account with it; the token pasted in the lichess card above serves when no account is linked. Then the
games go to the broadcast as described above, and a banner under the header says how many were sent and when the last
one was, with *Open on lichess.org* and *Stop*.

### A team match on the air

A team match shows the teams' score everywhere: in the web page's header and above its standings (with team A's
score %, Elo ± and LOS), in `standings.html` (the teams, then each team's engines against the other team's), in
`tournament.json` and `state.json` (`teams`, `team_elo`, `team_elo_error`, `team_los`), and in the TLCS crosstable, as
a line under the table ("Lynx 17½ - 18½ Otter · 48.6 % · Elo -9.7 ± 31.2"). Its games carry `WhiteTeam` and
`BlackTeam` tags in the PGN files, which the exports keep.

### Putting the web page on the internet

The web page can be followed from anywhere without opening a port in the router: tick *Publish on the internet* under
*Web page* in the *Broadcast* tab. While a tournament runs, RookStudio opens an SSH tunnel (libssh2, built into the
program) to a tunnel service, which gives the page a public https address; nothing needs to be installed.

- *localhost.run* (the default): free, without an account. Its addresses change now and then, and its speed is
  limited; the page and its state are sent compressed, so this does not matter much. Its host key is known to
  RookStudio: a connection to anything else is refused.
- *Another SSH server*: its host, port and user, and the port it opens for the page (as
  `ssh -R REMOTE_PORT:localhost:PORT user@host` would ask; 0 lets it choose). Only servers that let the user in
  without a key or a password can be used (authentication "none"), and they must offer curve25519-sha256, an RSA host
  key of 2048 to 4096 bits, and AES (GCM or CTR). The server's key is trusted the first time and kept in the profile
  (`known_hosts.json`); if it changes, the tunnel stops and says so, and *Trust the new key* is for when the server's
  owner confirms it changed.

The tab shows the state (connecting, on air with the number of connections, reconnecting, or an error) and, once
known, the public address with *Copy*, *Open* (to see the page as viewers do) and a QR code for a phone (click it to
enlarge). The tunnel reconnects by itself after a failure, waiting a little longer each time, and shows the new
address if it changed. It stops with the web page or with the tournament.

Other tunnels work too, started separately: with Cloudflare's, for example, switch on the web page (port 8080 here)
and run `cloudflared tunnel --url http://localhost:8080`; it prints an address such as
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel or a reverse proxy such as nginx, under a path of its
own as well (`location /live/ { proxy_pass http://localhost:8080/; }`), work the same way.

The page is read-only: it answers only GET requests for the page, its state and the PGN file, never a file a request
names, and nothing in it changes the tournament. Each request is answered on its own, 32 at a time at most, and must
arrive within a second and be answered within ten, so a slow or silent visitor holds no one else; behind the tunnel
each viewer's connection counts as one of those: 40 are relayed at once, as many more wait their turn, and the next
are refused; a viewer who sends more than the page takes is let go alone. Two downloads of the PGN file run at once,
and the next is asked to come back a moment later. The state is never cached, by the tunnel either, so viewers see the
games as they go; the page is kept by the browser and checked again at each load. The viewers shown in the
*Broadcast* tab are counted from the requests the pages make, so the count is right behind a tunnel too.

Viewers see the tournament's name, the engines' names, their ratings, searches and anomalies, the openings and the
PGN file of the games played. The folders and the engines' files stay private. Headless:

```
rookstudio --run FOLDER --tunnel                      # localhost.run, the address printed on the console
rookstudio --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Where the viewers talk

The page has no chat of its own; it points to where the talk happens. A *Chat link* under *Web page* (a Discord
invite, a Twitch channel, a forum thread; https only, anything else is refused with a message under the field) puts a
*Chat* button in the page's header that opens it in a new tab. While the games are pushed to lichess, the page also
links to the lichess round, which has its own chat.

A *Discord server ID* adds a *Discord* tab beside *Event*, with Discord's own server widget in the page's light or
dark theme: the server's name, the members online, its voice channels and a *Join* button, not the messages. The
server's owner switches it on in Server Settings, Widget ("Enable Server Widget"), which also shows the server ID to
copy (17 to 20 digits); with Developer Mode on, right-clicking the server's icon gives *Copy Server ID* as well. The
widget is loaded from discord.com only once a viewer opens the tab, and Discord then sees that viewer's visit (their
address, their browser, their Discord login if they have one); without an ID, the page contacts nobody but
RookStudio. The chat and lichess links send nothing until a viewer clicks them, and then no referrer. A setup file
run headless (`--run SETUP.json`) takes both from `web_chat_link` and `web_discord_id`.

## Headless mode

```
rookstudio --run TOURNAMENT_FOLDER     # resume a tournament
rookstudio --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

No window is opened; the version, then the progress, are printed on the console, with the tries of an engine that
cannot start, the engine set aside (with its engine log) or back, as in the window, and the standings every 20 games
and at the end (a team match: the teams' scores, then each team's engines against the other's), a tournament's
ending with the line of the standings copies (games, draws, time control, book and event). **Ctrl+C** stops after the
running games and the tournament can be resumed later, from the command line or the GUI. Exit codes: 0 finished, 1
error while playing, 2 nothing to run, 3 stopped before the end (by Ctrl+C, or with nothing left to play without an
engine that cannot start). The memory budget and the GPUs of the profile's **Settings** apply to it too.

`--detach` plays it in the background instead: the command returns at once, and the tournament goes on without a
terminal; the window shows it (it connects to it) when it opens with the same profile, and `--attach` talks with it:

```
rookstudio --run FOLDER --detach         # in the background, on a local socket
rookstudio --attach FOLDER               # its control channel on the standard input and output
```

The window drives each tournament the same way: `--run FOLDER --control socket` (or `--control stdio`) plays it
without printing, talking JSON lines with the window: the tournament's events (its state, each journal line, the
games in progress move by move, each game recorded, its log, its end) and the window's commands (pause, resume, stop,
changes, the end of a game, its share of the machine with `--allowance THREADS:MEMORY_MB:GPUS:ONE`). The first line
each way is a hello, `{"hello": {"protocol": 1, ...}}`; a script through `--attach` gets the tournament's hello and
events, and may send commands such as `{"c": "pause"}`. One window (or script) at a time talks with a tournament;
`--attach` lets go of it when its standard input ends or its output is no longer read, so that a script keeps its
input open while it reads the answers.

The tournament can be broadcast at the same time, as from the *Broadcast* tab:

```
rookstudio --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
rookstudio --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
rookstudio --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
rookstudio --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
rookstudio --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
rookstudio --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` takes the token from the `FCG_LICHESS_TOKEN` environment variable, or the profile's.

The options can be combined. A setup file saved with broadcasting switched on broadcasts the same way without
them. For viewers outside the local network, open the ports in the router: TLCV and node-tlcv connect to the TLCS
port. For the web page, a tunnel avoids opening a port (see [Putting the web page on the
internet](#putting-the-web-page-on-the-internet)).

## Machines

Other computers to run tournaments on: a test box, a server, a second PC. **Settings** > *Machines* lists them. A
machine is reached with SSH: nothing has to be opened on it but its SSH server, and the program keeps nothing secret.

### Adding a machine

*Add a machine*, then on its card:

- *Name*, *Host* (its name on the network, or its address), *Port* (22) and *User*, the account its tournaments run
  as;
- *Log in with*: the *SSH agent* (ssh-agent on Linux and macOS, Pageant or the *OpenSSH Authentication Agent*
  service on Windows; add your key to it with `ssh-add`), or a *Key file*, RSA or Ed25519 (`~/.ssh/id_ed25519`).
  The passphrase of a key file is asked once a session: it stays in memory and is never written anywhere. The
  machine must know the key: its public half (`id_ed25519.pub`) goes in `~/.ssh/authorized_keys` on the machine
  (`ssh-copy-id user@host` puts it there), or, for an administrator of a Windows machine, in
  `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Folder there*: RookStudio's folder on the machine, for the program, its tournaments, books and tablebases,
  relative to the user's home folder (`rookstudio` by default) or absolute (`/srv/fcg`, `D:\fcg`);
- *Budget*: the cores and the memory the tournaments on the machine may use at once; 0 takes what the test found,
  every thread but one and 80 % of the memory.

*Test the connection* logs in, then tells the machine's system, threads, memory, the free space where its folder is,
and whether RookStudio is installed there, in which version. The first connection shows the machine's host key
(its SHA256 fingerprint) and trusts it from then on (`known_hosts.json` in the profile, the broadcast tunnel's too).
When the key changes later, nothing is sent and the card says so, with *Trust the new key* for a machine that was
reinstalled. Every failure names the machine and what to do: the host that cannot be reached, a key the machine
refuses (and where its public half goes), a passphrase that is not the right one, no agent running.

*Install RookStudio* (*Update RookStudio* once it is there) copies the program to the machine's folder over
SFTP, then checks it starts there (`rookstudio --version`). It copies this program when the machine runs the
same system (Linux x86-64 to Linux x86-64); for another one, a Windows machine from Linux say, download the release
for that system and choose its program on the card (*RookStudio for Windows x86_64*). A copy cut by the network
resumes where it stopped.

### Engines on a machine

*Engines on this machine*: the library's engines the machine has, each with its path there (`/opt/engines/stockfish`,
`C:\Engines\sf.exe`). *Check the engines* checks each path: the program is there, can be run, and answers `uci`
(`xboard` for a WinBoard engine) within 10 seconds. The mark beside it, and its tooltip, say what was found: the
engine's name, or why it does not run (missing, not executable, made for another system, silent).

An engine missing on the machine is copied there with *Copy…* on its row (or *Copy \<engine\> to \<machine\>* in the
command palette, or the button of a start refused because an engine has no path there). The panel that opens says
first what goes: the program, its size and the system it is built for, read from its first bytes (a Windows
program, a Linux or macOS one, universal or not, or a script). A program the machine cannot run is refused before
anything is sent, saying which build to choose: "Alpha is a Windows program; tc-box runs Linux: choose the Linux
build of Alpha". The architecture counts as well (x86-64 or arm64); an x86-64 build on an arm64 Mac runs only under
Rosetta, and a format the program does not know is copied all the same: both are said, not refused. A script goes
only to a Linux or macOS machine, and only when the interpreter of its first line (`python3`, `/bin/sh`) is there.
*Choose the \<system\> build of \<engine\>…* takes another file of this computer, the Windows build of the engine say,
checks it the same way, and keeps it for that machine: later comparisons and replacements use it, not the library's
program.

*With its folder* copies the engine's whole folder beside the program (its working directory, or the program's
folder), for an engine that needs files beside it: its networks, a book. The panel counts its files and their size
first, and names the engine's options that point inside it (`EvalFile` of a network): in a tournament on the machine
they point to the copy there, and the folder is the engine's working directory. A folder of more than 2000 files is
not offered (a program in `/usr/bin` is not to be copied with its folder), a `.git` folder is left out, and for a
Windows machine a file name Windows cannot hold (`:`, `*`, `?`, a trailing dot, `CON`, `NUL`...) is named and stops
the copy before it starts. Files are copied as they are, line ends included.

*Copy* sends the files over SFTP to `engines/<engine>/` in the machine's folder, in the background with the time
left and *Cancel*, resumed where it stopped after a cut; each file's SHA-256 is compared with this computer's after
the copy, and files already there and the same are not sent again. On Windows the program keeps or gets its `.exe`;
elsewhere it is made executable (`chmod 755`). Then the engine's path on the machine is set to the copy and checked
as *Check the engines* does. On a Mac, a program copied from another computer that does not start may need a
signature or to leave the quarantine: the message gives the two commands to run there (`codesign -s -` and
`xattr -d com.apple.quarantine`); the program does not run them itself.

The *Build* column compares the engine there with this computer's, by their SHA-256 (*Check the engines* and
*Compare* do it, this computer's program hashed once until it changes): *same build as this computer*, or *a
different build there*, with *Replace with this computer's*. Replacing copies this computer's program to the
engine's folder in the machine's folder and points the engine there: a program elsewhere on the machine
(`/opt/engines/stockfish`) is never overwritten, it stays as it was.

### Books and tablebases on a machine

*Check what the setup uses* compares the opening book and the participants' own books of the setup with the
machine's books folder, by size and SHA-256 (a book of the same name but another content is not the same book), and
the setup's Syzygy files of 6 pieces at most with the machine's tablebases folder, by name and size. *Push the missing
files* copies what is missing or different, in the background, with the time left; each book is checked by its hash
once copied, each table by its size, and a copy cut short resumes. Tables of 7 pieces are not offered: they take
terabytes. A tournament does not start on a machine without its book; tablebases missing are a warning (the
adjudication and the engines do without them).

### A tournament on a machine

In the setup, *Run on* (in *Concurrency*) chooses where the tournament plays: *This computer*, or a machine. *Start*
then prepares it there: RookStudio must be installed there in this version (else *Update RookStudio*), every
engine must have its path on the machine (else the start offers *Copy \<engine\> to \<machine\>…*), and the setup's
books must be in its books folder (else the start says which is missing, and *Push the missing files* puts it
there); tablebases missing are only a warning. The tournament gets its folder in `tournaments/` of the machine's
folder, with the engines, books and tablebases at their paths there, and its process starts there out of the SSH
session (`rookstudio --run <folder> --detach`): it plays on whatever happens to the window or the network. The
window follows it as a tournament of this computer: Live, Games, the standings, pause, stop and the changes, through
the control channel of its process (`rookstudio --attach <folder>` over SSH).

Its card on the Tournaments page says *On \<machine\>*, and *Reaching \<machine\>…* while the connection is cut: the
window connects again on its own, a little longer after each failure, and takes up the events where its copy stopped.
The journal and the PGN files are copied back to the tournament's folder here as they grow, every few seconds, so that
Ratings, exports and the submission checklist read them; the machine's folder stays the reference (`machine.json` here
says which machine and folder). Closing the window leaves it playing there; the next launch shows it again. *Resume* of
an interrupted tournament of a machine resumes it there. The machine's budget (its cores and memory) is shared by its
own tournaments, apart from this computer's. One tournament plays on one machine: its games are not spread over several.

A machine needs an OpenSSH server (Linux, macOS, BSD; Windows 10 1809 and later), with an RSA host key and SFTP,
both there by default. Commands go through `sh` whatever the user's shell is (fish or csh are fine), and through
cmd on Windows, the default shell of its OpenSSH server.

### Engines played from this computer

An engine of the library can run on a machine while the tournament plays here: a GPU engine on the GPU box, an engine
built for another system, a machine kept for one engine. On its card, *Runs on* chooses *This computer* or a machine;
*Program there* and *Working directory there* are then paths on the machine (empty: the program's folder there), and
the *…* buttons, those of the options that name a file included, look through the machine's files. Once there are
machines, *Add…* offers *A program on \<machine\>…*, *Import…* offers *Every engine of a folder on \<machine\>…*,
and the machine's *Add an engine…* offers *A program there…*. The engine is read, checked, and played in the console,
the quick test and tournaments as an engine of this computer is. Its path is also the one of the machine's list of
engines, and its program's SHA-256 there tells when it was rebuilt (Δ, read again).

The machine must have been tested once (*Test the connection*), so that its host key is known: an engine never
connects to a machine whose key was not shown and trusted. The key's passphrase, if it has one, is asked once a
session, before the first read or the start, and kept in memory: the tournament's process gets it from the window and
keeps it, so that the tournament plays on after the window closes. `--run` in a terminal does not ask for it: use the
SSH agent there.

Each engine is started through a relay, `rookstudio --remote-engine <key>`, which the process that plays it
serves: one SSH connection to the machine for ten engines at most (the `MaxSessions` of an OpenSSH server), each engine
started from its folder there. An engine whose relay goes is ended there too, by its process id, when it does not end
by itself (on a Linux or macOS machine; on a Windows one, its channel is closed).

*Check* measures the round trip to the machine, the median of a few `isready`. The clock counts it: the setup's summary
warns when it takes more than 5 % of the increment, or of the time a move has without one, with *Set a margin of … ms*
(twice the round trip). A machine never tested stops the start, and so does a tournament that runs on another machine
than the engine's (one that runs on the engine's machine plays it there as its own engine).

While the tournament plays, the Tournaments page has a *Machines* line under the cores: each machine, connected and what
plays there, or since when it does not answer. A machine that does not answer is one entry under *Needs action*: its
engines' games are set aside and tried again, the others go on, with *Try again* and *Test \<machine\>*. A game that
an engine lost because its connection broke (its end as a disconnection) is voided and played again, written in the
journal with its reason (`connection to its machine lost: <machine>`); *Health* counts it apart, not as the engine's
crash. A machine cut off more than three times in an hour has its engines set aside. This computer's cores, memory and
GPUs do not count the engines on machines, only their relays; the machine's budget, which its own tournaments share,
does not count them yet.

### A Windows machine

Windows 10 (1809 and later) and Windows 11 include an OpenSSH server, not installed by default. In PowerShell, as
administrator:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

The installation opens port 22 in the firewall. Then put your public key in `C:\Users\<user>\.ssh\authorized_keys`,
or, for an account of the Administrators group, in `C:\ProgramData\ssh\administrators_authorized_keys`, which only
Administrators and SYSTEM may read (the groups are given by their SIDs, the same in every language of Windows):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Keep cmd as the server's shell: RookStudio speaks cmd, not PowerShell (if a `DefaultShell` value was set under
`HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, remove it). On the machine's card, choose the Windows release's
`rookstudio.exe`, then *Install RookStudio*.

## Files

Everything lives in the profile folder: `~/.local/share/rookstudio/` on Linux,
`%APPDATA%\rookstudio\` on Windows, `~/Library/Application Support/rookstudio/` on macOS.
Files the program creates are not writable by other users, and `settings.json` is readable by its owner only once
it holds a lichess token. **Settings** shows the profile folder, with *Open the folder*.

### Portable mode

With a file named `portable` (empty, without extension) beside the program (beside `rookstudio.exe` or
`rookstudio`, beside `RookStudio.app` on macOS), the profile is the folder `data` beside the program instead,
and the default tournaments folder is `data/runs`: nothing is written elsewhere, but in the folders chosen for
tournaments, exports and the like. The paths of what lies in the program's folder (engines, books, tablebases, test
suites, the PGN files of the ratings and the setup, the folders chosen) are kept relative to it in the profile's
files, so the whole folder can be moved, copied to another machine or run from a USB stick, whatever its drive
letter, and its engines are found again. **Settings** says *Portable mode* beside the profile folder. To carry an
existing profile over, copy its files into `data`. `--profile` still chooses another folder, outside the portable
mode. A tournament keeps the engines' paths of when it was created: resume an unfinished one before moving the
folder. On macOS, remove the quarantine first (see the README): otherwise macOS runs the application from a temporary
place, where the `portable` file is not.

### The profile folder

- `engines.json`: the engine library, and `engines.json.bak` its previous version, read if the first is lost;
- `setup.json`: the last setup; `profiles.json`: your profiles of the setup's sections; `templates/*.json`: the
  saved templates; `queue.json`: the queue;
- `book-cache/`: the PGN books turned into Polyglot books for the participants' own books (removed after 30 days
  unused);
- `layout.ini`: the window layout and the panels of the workspace; `layouts.json`: the layouts saved by name;
  `rookstudio.log`: the log of the last session;
- `suite.json`: the last setup of the Suites page; `suites/<suite>-<date>.json`: the finished test suite runs;
- `board.json`: the settings of the Board page's analysis and games; `analyses/*.json`: the games analysed move
  by move, one file each; `games/*.pgn`: the games played from the Board page;
- `settings.json`: the tournaments folder, when another one was chosen in **Settings**, *Software rendering*, the
  folder of *Every engine of a folder…*, the tester and site of *Export for a rating list…*, the lichess token, the
  window's size and place, the guide panel's width, the getting started card hidden, the check for a new version
  (wanted or not, when it was last made, the release it found, the notice hidden), the appearance (board colours,
  pieces, move sounds), and the machine's budget (cores, memory and GPUs for the tournaments, *Start when cores are
  free*);
- `logos/`: the engines' logos, copies of the pictures chosen on their cards;
- `machines.json`: the remote machines of **Settings** (their address, folder, budgets, engines' paths and what the
  last test found; no passphrase), and `known_hosts.json` the host keys trusted, of the machines and the tunnel;
- `running.json`: the tournaments playing now, with their weights, each removed when it stops; found at launch, it
  tells what to show again (a tournament that played on without the window) or to resume;
- `runs/`: the tournaments, unless another folder was chosen (see [A tournament's folder](#a-tournaments-folder));
- `databases.json`: your databases, and `databases/` the folders of those made here (`Name.fcgdb`); `searches.json`:
  the kept searches; `repertoires/`: your repertoires, one PGN file each;
- `recent.json`: the Board page's game and the recent games, with their changes not saved;
- `engines/`: the popular engines downloaded, each version in a folder of its own;
- `assistant.json`: the assistant's settings, and `models/` its model;
- `lichess-account.json`: the Lichess account linked, its permissions and its token (a file readable by you alone);
  `lichess-cache/`: the answers of Lichess's tables and cloud.

### A tournament's folder

`runs/<date>_<A>_vs_<B>/` for a match, `runs/<date>_<name>/` for a tournament (in `runs/` or the chosen folder), one
folder each, which can be moved, copied, zipped and sent:

- `tournament.json`: the format, the participants (engines copied with their options) and the conditions;
- `setup.json`: the *New tournament* page's setup it started from, which *Clone* and *Relaunch with…* read (see
  [Queue and relaunch](#queue-and-relaunch)); a tournament started by an older version has none;
- `journal.jsonl`: everything that happened since, one line per event (game pairs started, games finished, changes,
  Swiss and knockout pairings); with the PGN files, it is what a resume reads. A game is on disk before its line, so a
  power cut loses at most the games being played. Game text no line points to (a game dropped by a change while it
  was saved, or left by a crash) is replaced with blank lines, so other PGN viewers only show the tournament's games;
- `tournament.lock`: held by the program playing the tournament; another program opening the folder only shows it;
- `<name>.pgn`: move comments in the cutechess / fastchess style, `{+0.35/18 1.234s}`, with the score from the point
  of view of the side that moved; games in the order they ended (the journal finds each one by its place in the file;
  *Export all* in the *Games* tab, and the file of *Also append every game to*, follow the schedule);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, with the evaluation from
  White's point of view, as read by ChessBase / Fritz and Lichess;
- `<name>` is the tournament's name when it was created (renaming it later keeps the files), with the characters
  Windows does not allow replaced by `_` and cut to 64 bytes; `tournament.json` records both names. A tournament
  without a name, an older one, or a match imported from 0.2.x has `games.pgn` and `games-chessbase.pgn`;
- `lichess.json`: the lichess broadcast and round its games went to (no token);
- `processor.tsv`: the processor time each game's engines used, for the *Processor use* of a resumed tournament;
- `rookstudio.log`: the log of the processes that played it (each tournament plays in a process of its own);
  `control.json`, while one plays it: where its local socket is (`control.sock` in the folder, or in the user's
  runtime folder when that path is too long; a named pipe on Windows), and which process listens;
- `logs/game-N.log`: every line exchanged with both engines during game N (the number shown in the *Games* tab), with
  the time it was sent or read (`W>` sent to White's engine, `W<` read from its output, `W!` read from its error
  output: assertions, crash messages), for the games lost on time, by a crash, a stall or an illegal move (or every
  game, if asked). The first 300 lines of each engine and the last 5000 lines of the game are kept, in the order they
  came. It is what an engine author needs to reproduce a bug. For a WinBoard engine it shows the UCI side;
  `FCG_WB2UCI_LOG` gives the WinBoard side;
- `logs/start-<engine>.log`: the last failed try to start an engine that cannot start: its program, the error, and
  the lines exchanged with the engines of that game until then (see [An engine that cannot
  start](#an-engine-that-cannot-start)).

A folder received from someone else is safe to open: the file of *Also append every game to* must be a `.pgn` file
(another name is ignored, with a warning), and a folder whose journal, PGN files or lock file are symbolic links is
not opened for writing, so it cannot make the program write outside it. Resuming it runs the engines its
`tournament.json` names, as any tournament does.

Matches played with version 0.2 appear on the Tournaments page: *Import them* turns them into finished tournaments.

## Command line

```
rookstudio [--profile DIR] [--autostart] [--size WxH] [--software]
           [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
           [--quit-after SECONDS]
rookstudio [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
           [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
           [--control stdio|socket] [--detach] [--allowance THREADS:MEMORY_MB:GPUS:ONE]
rookstudio --attach TOURNAMENT_FOLDER
rookstudio --version
```

- `--profile DIR` uses another profile folder;
- `--autostart` starts the saved setup at launch (rather than resuming the tournament that was running, see [Pause,
  stop and resume](#pause-stop-and-resume));
- `--size WxH` opens the window at that size, centred, rather than as it was left, and leaves the size and place
  kept for the next launch alone;
- `--software` has the processor draw the window (SDL's software renderer) rather than OpenGL, as *Software
  rendering* in **Settings** does, for this launch (see [Drawing the window](#drawing-the-window));
- `--shot` saves a screenshot of the window after the given number of seconds (default 5), with WINDOW brought to
  the front, and quits once every screenshot is taken. It can be repeated. WINDOW is a page (`home`, `setup`,
  `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`,
  `settings`) or a tab (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). The mouse is
  ignored in this mode and, on Linux, the window is rendered off screen (no window appears; set `SDL_VIDEODRIVER` to
  override); the window opens at the default size, or `--size`, and the size and place kept are left alone;
- `--focus WINDOW` opens that page or tab at launch, or: `engines-card` (the first engine's card),
  `engines-refresh` (*Refresh: the new engines of the folder*), `engines-ratings=FILE` (the preview of that rating
  list), `setup-picker` and `setup-suggest` (the library's filter focused, or the suggested opponents selected),
  `setup-open=ROW` and `setup-profiles=ROW` (a row of the setup opened, or its profile list: `format`, `time`,
  `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (the first participant's own book),
  `submission-export` (*Export for a rating list…*), `ratings-players` (the *PGN players* window), `clone=DIR`
  (the *New tournament* page filled by *Clone* of that tournament's folder),
  `palette[=TEXT]` (the command palette, with TEXT typed in it), `game-menu=N` (the menu of game N in the *Games*
  tab, a result chosen), `end-menu=N` (*End with result* of the game of slot N, from 1), `big-screen[=N]` (the big
  screen, of the game of slot N), `live-back=N` (the game followed in *Live*, stepped back N plies),
  `engines-console` (the first engine's console), `board-analyse` (the Board page, analysed), `board=FILE` (a PGN,
  FEN or EPD file on the Board page; `board-analyse=FILE` analysed, `board-review=FILE` its first game analysed move
  by move), `board-edit` (the position editor), `board-play` and `board-play-start` (a game between the first two
  engines, started), `machines` (the first remote machine's card in **Settings**; `machines-test` tested,
  `machines-files` the setup's books and tablebases compared with it), `guide[=ID]` (the guide panel, at the section
  ID), `guide-search=TEXT` (the guide panel, searching TEXT);
- `--select-game N` opens game N (in finishing order) in the *Games* tab as soon as it exists;
- `--quit-after SECONDS` stops the tournament and quits;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: see [Headless mode](#headless-mode);
- `--version` prints the version and the protocol of the channel the window follows a remote tournament through
  (see [Machines](#machines)), and quits.

## Troubleshooting

### The program does not start

- **Windows says the program is unsafe, or an antivirus removes it.** The program is not signed, so Windows
  SmartScreen and some antivirus programs distrust it. *More info*, *Run anyway* lets it start; an antivirus can be
  told to allow its folder. The releases are built by GitHub Actions from the public sources.
- **macOS refuses to open it.** It is not signed by an Apple developer account: Control-click (right-click) it, Open,
  then Open again; on macOS 15 and later, try to open it once, then System Settings, Privacy & Security, Open Anyway.
  Or run `xattr -dr com.apple.quarantine` on the unzipped folder.
- **A message says no way of drawing the window works, or the window stays black.** The graphics driver lacks
  OpenGL 3.2 or fails: start with `--software` (the processor draws the window), and if that works, tick *Software
  rendering* in **Settings**. Under Wayland without a graphics driver, start with `SDL_VIDEODRIVER=x11`. See [Drawing
  the window](#drawing-the-window).
- **Chinese text shows as squares.** Chinese needs a system font: Microsoft YaHei on Windows, Noto Sans CJK or
  WenQuanYi on Linux (install `fonts-noto-cjk` or the like).

### An engine does not start

- **Its check fails, or its tournament says it cannot start.** Read the error under the engine, then its engine log
  (*Engine log*, or `logs/start-<engine>.log` in the tournament folder): the engine's error output often names the
  missing file. The usual causes: the program moved (*Locate…*), a network or a library not found (set the working
  directory, or `EvalFile`, on its card), a program built for another processor (a build using AVX-512 on a machine
  without), a slow start (raise *Start timeout* on its card). See [An engine that cannot
  start](#an-engine-that-cannot-start).
- **A WinBoard engine does not start from the book's positions.** An engine of protocol 1 without `setboard` can
  only start from the initial position: give it a PGN book (moves), not an EPD book. See [WinBoard
  engines](#winboard-engines).
- **The tournament refuses to start: an engine does not play Chess960.** Every engine must have the `UCI_Chess960`
  option (UCI) or the `fischerandom` variant (WinBoard); the summary names those that do not.

### Games are lost or look wrong

- **An engine loses on time early in its games.** It is more often set up wrong than playing badly: too many threads
  for the cores the games have, a hash too large to allocate in time, a network loaded at every game, a slow start.
  Lower its `Threads` and `Hash`, untick *Restart the engine for every game*, raise *Start timeout*; the *Health*
  card and the overview's *TF* show the losses on time.
- **The engines seem slower than they should be.** Too many games at once: leave *Concurrency* automatic, and keep
  concurrency × threads within the physical cores. *Processor use* in the overview names an engine that uses more
  cores than its `Threads`, or thinks on its opponent's time.
- **The summary warns that the memory will not do.** The games' hash tables would not fit in the free memory: lower
  the `Hash` (in *Engine settings*, or of the engine it names) or the concurrency.
- **Tablebase files are said to be missing or cut short.** A download stopped half way, or the folder holds only
  smaller tables: download the files named in the tooltip again, or lower the adjudication to the pieces found (*Lower
  to 5 pieces*).
- **A book is said to have illegal positions or duplicates.** fastchess leaves those out; *Save a cleaned copy…* on
  the Books page writes a book without them. See [Checking a book](#checking-a-book).
- **A game was decided wrongly.** Right click on it in *Games* or *Schedule*: play it again, void it, or set its
  result, with the reason. See [Changing a running tournament](#changing-a-running-tournament).

### Broadcasting

- **TLCV or another viewer does not connect.** Start the tournament (and its TLCS server) before the viewer: it does
  not log on again by itself. Open the UDP port in the router for viewers outside the local network.
- **The web page cannot be reached from outside.** Tick *Publish on the internet* for a tunnel (see [Putting the web
  page on the internet](#putting-the-web-page-on-the-internet)), or open the TCP port in the router.
- **lichess refuses the token.** The token needs the `study:write` scope: create a new one with *Create a token on
  lichess…*. Behind a proxy, set `HTTPS_PROXY`.

### Anything else

*Report a problem…* in **Settings** or on the **Log** page opens a GitHub issue already filled with the version, the
system and the last lines of the log (see [Reporting a problem](#reporting-a-problem)).

## Glossary

- **Adjudication**: ending a game before mate when its result is clear: a draw when both scores stay near 0, a loss
  when they stay far below, or what the tablebases say. See [Adjudication](#adjudication).
- **BayesElo, DrawElo**: a rating model with a separate parameter for draws (DrawElo), from Rémi Coulom's program.
- **Berger tables**: the order of the rounds of a round robin in which every engine plays once per round.
- **Buchholz, Sonneborn-Berger**: tie-breaks: the sum of the opponents' scores (a Swiss), and the sum of the scores of
  the opponents beaten plus half those drawn with (a round robin).
- **Chess960, Chess324**: starting positions with the back rank shuffled: 960 with the king between the rooks
  (castling to its usual squares), 324 with the king and rooks in place.
- **Concurrency**: the number of games played at the same time.
- **Cursed win**: a position the tablebases count as won, but whose mate takes more moves than the 50-move rule allows:
  a draw in a game played to that rule.
- **Cycle**: in a round robin, every pairing played once with each colour (cutechess calls it a round, of 2 games per
  encounter).
- **ECO**: the Encyclopaedia of Chess Openings' codes, A00 to E99.
- **Elo, nElo**: the rating difference a score stands for. nElo (normalized Elo) divides by the spread of the results,
  so that its value does not depend on the draw rate; fishtest and OpenBench state their SPRT bounds in it.
- **EPD, FEN**: one line describing a position; an EPD line may carry operations, such as the best move `bm` of a test
  suite.
- **Gauntlet**: the engines under test (the seeds) play every other engine, which do not play each other.
- **H0, H1**: the two hypotheses of an SPRT: the Elo difference is elo0 or less (H0), or elo1 or more (H1).
- **Hash**: the memory an engine takes for its transposition table, in MB.
- **LLR**: the log-likelihood ratio of an SPRT; the test ends when it leaves its bounds, from alpha and beta.
- **LOS**: the likelihood of superiority, the probability that an engine is stronger than another, from their games.
- **MultiPV**: an engine searching several lines at once.
- **Novelty**: the first move of a game that no game of the database had played in that position.
- **Opening suite, book**: the positions or moves the games start from, each played twice with colours reversed.
- **Pentanomial**: the results of the game pairs counted by their total: 0, ½, 1, 1½ or 2 points. It gives tighter
  error bars than counting single games, since the two games of a pair share an opening.
- **PGN**: the text format of chess games: tags such as `[White "…"]`, then the moves.
- **Polyglot**: the binary format of opening books (`.bin`) read by most programs, with a weight for each move.
- **Ponder**: thinking on the opponent's time; off in RookStudio's tournaments.
- **Repertoire**: the openings one plays, a move chosen for one's side in each position, and the opponent's moves
  answered.
- **Seed**: a place in the participants' order: the engines under test of a gauntlet, the favourites of a knockout.
- **SPRT**: the sequential probability ratio test, which plays games until it can tell, with the error rates chosen
  (alpha, beta), whether an engine gains elo1 or more (H1) or not more than elo0 (H0).
- **Syzygy**: the endgame tablebases that give the result of every position with few pieces (WDL files, `.rtbw`).
- **TF**: time forfeits, the games lost on time.
- **Threads**: the processor threads an engine searches with.
- **TLCS, TLCV**: Tom's Live Chess Server and Viewer, a protocol to follow engine games live.
- **UCI, WinBoard (CECP)**: the two protocols programs talk to chess engines with.
- **WDL**: the chances of a win, a draw and a loss that an engine gives with its score.

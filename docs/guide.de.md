# Anleitung zu RookStudio

Diese Anleitung beschreibt jede Seite, jede Einstellung und jede Datei von RookStudio: Engines, die Turniere spielen,
und das Studium von Partien (Datenbanken, Notation, der Eröffnungsbaum, Repertoires, Lichess und ein lokaler
Assistent). Das [README](../README.md) sagt, was das Programm ist, wo man es herunterlädt und wie man es baut. Im
Programm öffnet F1 diese Anleitung neben der Seite, beim Abschnitt zu dem, was unter der Maus liegt, in der Sprache der
Oberfläche (siehe [Hilfe im Programm](#hilfe-im-programm)).

## Schnellstart
<!-- en: quick-start 92353f2c -->

### Dein erstes Turnier
<!-- en: your-first-tournament 75e9a891 -->

1. **Engines.** In der Leiste links *Engines*, dann *Hinzufügen…*, und wähle das Programm einer Engine (eine
   ausführbare UCI- oder WinBoard-Datei): Es wird einmal gestartet, um seinen Namen und seine Optionen zu lesen.
   *Importieren…* > *Eine Engine-Liste…* übernimmt die Engines eines anderen Programms (cutechess, Banksia, Arena,
   ChessGUI), *Importieren…* > *Alle Engines eines Ordners…* alle Programme eines Ordners. *Prüfen* sagt dir, ob eine
   Engine funktioniert.
2. **Neues Turnier.** *Turniere* > *Neues Turnier*. Wähle ein *Format* aus seiner Liste (*Rundenturnier*,
   *Gauntlet*…) und eine *Bedenkzeit* (*10+0.1* für einen ersten Versuch), und lass die anderen Zeilen, wie sie sind.
3. **Teilnehmer.** Doppelklicke Engines in der *Bibliothek* neben den Teilnehmern, oder wähle mehrere aus und drücke
   Enter.
4. **Start.** Das Turnier öffnet sich in seinem Arbeitsbereich: *Übersicht* für die Tabelle und die verbleibende
   Zeit, *Live* für die laufenden Partien.

Die Zusammenfassung rechts in der Einrichtung sagt, wie viele Partien gespielt werden und wie lange sie dauern, und was
schiefgehen würde (ein Programm, das nicht gefunden wird, zu viele Threads, ein fehlendes Buch), jeweils mit einem
Knopf, der es behebt.

### Eine neue Version einer Engine testen
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Füge beide Versionen zur Bibliothek hinzu.
2. *Neues Turnier*, *Format*: *SPRT Verbesserung [0, 3]* (bringt die neue Version etwas?) oder
   *SPRT keine Verschlechterung [-5, 0]* (ist sie nicht schwächer?).
3. Wähle die neue Version als **A** und die alte als **B**, eine kurze Bedenkzeit (*10+0.1*) und unter *Eröffnungen*
   ein Buch mit ausgeglichenen Stellungen (EPD), dann **Start**.

Das Match endet von selbst, sobald der SPRT entscheidet: H1 angenommen (A ist stärker, gemessen an den Grenzen) oder
H0 angenommen (es ist nicht stärker). Die *Übersicht* zeigt die Elo, die LLR und ihre Grenzen, während die Partien
eintreffen. Die Einrichtung bleibt erhalten: Wähle beim nächsten Mal die nächste Version als A und **Start**, oder
klicke beim beendeten Match auf *Neu starten mit…*.

### Deine Partien studieren
<!-- en: studying-your-games 9a78e41c -->

1. **Eine Datenbank.** In der Leiste *Datenbanken*, dann *Neue Datenbank*, direkt benannt, und *Datei importieren…*:
   eine PGN-Datei oder eine Datei `.zst`, `.gz` oder `.zip`, die eine enthält (eine auf die Seite gezogene Datei tut
   dasselbe). Die Partien kommen im Hintergrund, danach wird der Eröffnungsbaum der Datenbank erstellt.
2. **Eine Partie.** Ein Klick auf eine Partie der Liste zeigt sie daneben; Enter oder ein Doppelklick öffnet sie auf dem
   *Brett*. Strg+O öffnet dort eine PGN-Datei, Strg+V fügt eine Partie oder eine FEN ein.
3. **Eine Engine.** Die Analyse braucht eine Engine: Bei leerer Bibliothek bietet der Tab *Analyse* an, Gaia mit einem
   Klick herunterzuladen (siehe [Beliebte Engines](#beliebte-engines)). A startet und beendet die Analyse der Stellung,
   G analysiert die ganze Partie (Alt+A und Alt+G auf dem Brett fürs Studium).
4. **Deine Notizen.** Enter schreibt einen Kommentar nach dem Zug, `!` und `?` setzen Symbole, ein anderer Zug als der
   der Partie beginnt eine Variante, die rechte Maustaste zeichnet Pfeile und Felder; Strg+S speichert die Partie in
   ihre Datenbank.

### Wie es weitergeht
<!-- en: where-to-go-next d21be582 -->

- [Neues Turnier](#neues-turnier): jede Einstellung, und [Tipps](#tipps-für-die-einstellungen), wie du sie wählst;
- [Ein Turnier durchführen](#ein-turnier-durchführen): was die Tabs zeigen, Pausieren, ein Turnier ändern, während es
  läuft;
- [Elo-Listen](#elo-listen): eine Rangliste über alle gespielten Partien;
- [Datenbanken](#datenbanken), [das Brett fürs Studium](#das-brett-fürs-studium), [die Seite
  Eröffnungen](#die-seite-eröffnungen), [die Seite Lichess](#die-seite-lichess) und [der Assistent](#der-assistent)
  für deine eigenen Partien;
- [Ansichten](#ansichten): die Seiten der Leiste, wie du sie haben willst;
- [Übertragung](#übertragung): die Partien live auf einer Webseite, in TLCV oder auf lichess;
- [Fehlerbehebung](#fehlerbehebung), wenn etwas schiefgeht, und das [Glossar](#glossar) für die Begriffe der Tester.

## Das Fenster
<!-- en: the-window 3a5230d4 -->

### Die Seiten
<!-- en: the-pages 1cab137d -->

Eine Navigationsleiste links führt zu den Seiten der gezeigten [Ansicht](#ansichten), in ihrer Reihenfolge:

| Seite | Inhalt |
|---|---|
| Startseite | Die Partie zum Weitermachen, was sich starten lässt, die letzten Partien, deine Datenbanken, Lichess und was läuft: Karten der Seiten der Ansicht (siehe [Die Startseite](#die-startseite)) |
| Turniere | Was läuft und was zu erledigen ist, die Warteschlange, die zuletzt beendeten Turniere, alle Turniere des Profils, *Neues Turnier* |
| Brett | Eine Partie oder eine Stellung von irgendwoher, analysiert von einer Engine der Bibliothek; in einer Ansicht fürs Studium die direkt bearbeitete Notation (siehe [Das Brett fürs Studium](#das-brett-fürs-studium)) |
| Datenbanken | Deine Partiedatenbanken: Import, die Partieliste, die Suchen |
| Eröffnungen | Deine Repertoires, ihre Abdeckung und ihre Lücken |
| Training | Die Wiederholung des Tages, die Aufgaben von Lichess, deine Fehler als Übungen, Züge erraten, Endspiele gegen die Tablebases, Matts, Brettvisualisierung (siehe [Die Seite Training](#die-seite-training)) |
| Spielen | Eine Partie gegen eine Engine der Bibliothek in der Stärke, die du wählst, mit ihren Eröffnungen und einer Vorgabe, und deine Partien auf Lichess, beide auf der Seite Brett gespielt (siehe [Die Seite Spielen](#die-seite-spielen)) |
| Lichess | Dein Lichess-Konto, die Partien eines Spielers, die offiziellen Übertragungen, was die Analyse Lichess fragen darf |
| Engines | Die Engine-Bibliothek: Optionen, Prüfungen, Gruppen, Referenz-Elo, Import der Listen anderer Programme, die beliebten Engines |
| Bücher | Die Bibliothek der Eröffnungsbücher, und die Eröffnungen jedes Buchs mit dem, was an ihnen nicht stimmt |
| Suiten | Testsuiten (STS, WAC, ERET…): Engines gegen die Stellungen einer EPD-Datei |
| Elo-Listen | Eine Rangliste über alle Partien aller Turniere |
| Protokoll | Die eigene Ausgabe von fastchess und die Meldungen des Programms, und *Problem melden…* |
| Einstellungen | Sprache, Design, Größe der Oberfläche, Benachrichtigungen, das Aussehen der Bretter, die Ansichten, der Assistent, der Profil- und der Turnierordner, die entfernten Maschinen, das lichess-Token, die Version, die Art, wie das Fenster gezeichnet wird: zuerst die Abschnitte der Seiten der Ansicht, die anderen unter *Weitere Einstellungen* |

### Ansichten
<!-- en: views 9c72e404 -->

Eine Ansicht ist eine eigene Zusammenstellung von Seiten: bis zu drei, jede mit Name, Symbol und Farbe, umgeschaltet
oben in der Leiste. Jede Ansicht hat dort eine Marke, gefüllt für die gezeigte Ansicht, und der Name der gezeigten
Ansicht steht darunter; ein Klick auf eine Marke oder Strg+1 bis Strg+3 zeigt diese Ansicht, bei der Seite, auf der sie
war. Mit einer einzigen Ansicht verschwindet der Umschalter.

Der erste Start bietet drei an: *Tests* (Turniere, Engines, Bücher, Brett, Suiten, Elo-Listen, Protokoll), *Studium*
(Startseite, Brett, Datenbanken, Eröffnungen, Training, Spielen, Lichess, Engines) und *Alles* (alle Seiten). Die Seite
Einstellungen ist in jeder Ansicht. Eine Seite, die mit einer neuen Version kommt, wird nicht von selbst zu einer deiner
Ansichten hinzugefügt: Die Startseite bietet sie einmal an (siehe [Die Seite Training](#die-seite-training) und [Die
Seite Spielen](#die-seite-spielen)).

*Ansichten bearbeiten* öffnet anstelle der Seite die Bearbeitung der Ansichten: über *Einstellungen* > *Ansichten*, ⋯
unter dem Umschalter, einen Rechtsklick auf eine Marke (die Bearbeitung öffnet sich bei ihrer Ansicht) oder *Ansichten
bearbeiten…* in Strg+K. Sie enthält:

- die Ansichten, in der Reihenfolge von Strg+1 bis Strg+3 (*Nach oben*, *Nach unten*), *Neue Ansicht* (höchstens drei)
  und *Diese Ansicht löschen*;
- die gewählte Ansicht: ihren Namen (kurz: Er steht unter dem Umschalter), ihr Symbol (eines von zwölf), ihre Farbe
  (eine von sechs);
- ihr Brett: *Fürs Studium* (die direkt bearbeitete Notation, die Pfeile und Felder, die Tabs Baum, Referenzen und
  Fragen: siehe [Das Brett fürs Studium](#das-brett-fürs-studium)) oder *Für die Engine-Analyse* (das Brett der
  Engine-Tester, wie es war);
- ihre Seiten, angehakt, in der Reihenfolge der Leiste (↑ und ↓ neben jeder verschieben sie; die Seite Einstellungen
  bleibt am Ende); eine Seite kann in mehreren Ansichten sein, und *auch: …* nennt die anderen;
- die Leiste, wie sie aussehen wird.

Alles erscheint sofort in der Leiste. Strg+Z macht jede Änderung rückgängig, solange die Bearbeitung offen ist, Enter
übernimmt die Änderungen, Esc lässt die Ansichten, wie sie waren. *Vorgeschlagene Ansichten wiederherstellen* holt die
drei des ersten Starts zurück.

Strg+K listet jede Seite mit der Ansicht auf, in der sie sich öffnet: Wählst du eine Seite einer anderen Ansicht, wird
zu dieser umgeschaltet. Eine Seite, die in keiner Ansicht ist (ein Hinweis kann dorthin führen), öffnet sich in der
gezeigten Ansicht, mit *Zu … hinzufügen*.

### Der erste Start
<!-- en: the-first-start 18171dec -->

Beim ersten Start fragt RookStudio:

- nach der Sprache der Oberfläche;
- was du tun möchtest: die angebotenen Ansichten, jede angehakt oder nicht (*Tests*, *Studium*, *Alles*), und welche
  zuerst erscheint;
- wenn ein fastchess-gui-Profil auf dem Computer ist, ob es kopiert werden soll: seine Engines, Bücher, Turniere und
  Einstellungen. fastchess-gui bleibt, wie es ist, und kann daneben weiterlaufen; *Ohne Kopie starten* lässt es
  unberührt.

Alles lässt sich später unter *Einstellungen* > *Ansichten* ändern.

### Hilfe im Programm
<!-- en: help-in-the-program c08dbff5 -->

- **Tooltips.** Lass die Maus auf einem Bedienelement ruhen, um zu sehen, was es tut. Der Tooltip eines Elements, das
  diese Anleitung weiter erklärt, endet mit *F1: mehr in der Anleitung*.
- **F1** öffnet diese Anleitung in einem Bereich rechts der Seite, beim Abschnitt zu dem, was unter der Maus liegt
  (eine Zeile der Einrichtung, eine Karte der Seite Turniere, ein Tab, eine Spalte der Tabelle); liegt nichts unter der
  Maus, beim Abschnitt des zuletzt angeklickten Bereichs, sonst der Seite. Das runde **?** neben dem Titel einer Seite
  öffnet den Abschnitt der Seite. F1 noch einmal oder Esc im Bereich schließt ihn.
- **Der Anleitungsbereich** zeigt einen Abschnitt mit seinen Unterabschnitten, und die Seite daneben bleibt bedienbar:
  Du kannst die Erklärung lesen, während du die Einstellung änderst. Sein Kopf hat *Zurück* und *Vorwärts* (auch
  Alt+Links und Alt+Rechts oder die Seitentasten der Maus), *Inhalt* (alle Abschnitte), das Suchfeld (Strg+F im
  Bereich) und *Im Browser öffnen* (diese Anleitung auf GitHub, für die laufende Version). Die Suche findet jedes
  eingetippte Wort, in beliebiger Reihenfolge, unabhängig von Groß-/Kleinschreibung und Akzenten; Titel kommen zuerst,
  und die Treffer werden hervorgehoben. Links zu anderen Abschnitten öffnen sich im Bereich, Links zu Webseiten im
  Browser, und ein Codeblock hat *Kopieren*. Zieh am linken Rand des Bereichs, um ihn zu verbreitern; seine Breite
  bleibt erhalten.
- **Sprachen.** Die Anleitung ist in das Programm eingebaut, in jeder Sprache der Oberfläche, und passt zur laufenden
  Version; nichts wird aus dem Netz geholt. Ein Abschnitt, dessen Übersetzung fehlt oder älter ist als der englische
  Text, wird auf Englisch gezeigt, mit einer Zeile, die darauf hinweist. Die Release-Archive enthalten sie auch
  (`guide.md`, und `guide.<language>.md` für jede Übersetzung).
- **Strg+K**, die Befehlspalette, findet die Abschnitte der Anleitung nach ihren Titeln (*Anleitung: …*), und
  *Anleitung nach „…“ durchsuchen* durchsucht sie nach dem Eingetippten.
- **Meldungen.** Ein Problem, das das Programm meldet, nennt die Engine, die Datei oder die Ressource, mit der
  Behebung gleich daneben; ein **?** daneben öffnet seinen Eintrag in der [Fehlerbehebung](#fehlerbehebung).
- **Erste Schritte.** Bis ein erstes Turnier gestartet wurde, zeigt die Seite Turniere die Schritte des
  [Schnellstarts](#schnellstart), jeden mit seinem Knopf und abgehakt, sobald er erledigt ist; *Ausblenden* räumt sie
  weg.
- [Melde ein Problem](#ein-problem-melden), wenn das Programm nicht tut, was es soll.

### Die Befehlspalette
<!-- en: the-command-palette 14590459 -->

**Strg+K** öffnet die Befehlspalette: die Seiten und Tabs, die Engines der Bibliothek (sie öffnet die Karte), die
Bücher, die Turniere der Seite Turniere, die Abschnitte der Anleitung und die gerade möglichen Aktionen (*Pause*,
*Fortsetzen*, *Alles pausieren*, *Stopp*, *Start*, *Zur Warteschlange hinzufügen*, *… neu starten mit…*, die Einträge
von *Importieren…*, *Die noch nicht geprüften prüfen*, *Für eine Rangliste exportieren…*, *Kopieren* der Tabelle,
*Version kopieren*, *Problem melden…*, die Brettfarben und Figurensätze, die Zugtöne ein oder aus…).
Groß-/Kleinschreibung spielt keine Rolle, und die eingetippten Buchstaben müssen nicht nebeneinander stehen („rl“
findet *Rangliste*); Namen, die mit ihnen beginnen oder mit einem Wort, das mit ihnen beginnt, kommen zuerst. Hoch und
Runter wählen, Enter führt aus, Esc schließt; Strg+K wird ignoriert, solange in ein Textfeld getippt wird.

### Sprache, Design und Größe
<!-- en: language-theme-and-size 954fe416 -->

Die Oberfläche gibt es auf Englisch, Französisch, Deutsch, Spanisch, Italienisch, brasilianischem Portugiesisch,
Russisch und vereinfachtem Chinesisch. Beim Start folgt sie der ersten bevorzugten Sprache des Systems (Englisch, wenn
es für diese keine Übersetzung gibt); in den **Einstellungen** lässt sie sich fest einstellen. Chinesisch verwendet
eine Schrift des Systems: Microsoft YaHei unter Windows, Noto Sans CJK oder WenQuanYi unter Linux. Das Design ist hell,
außer du wählst in den **Einstellungen** dunkel oder die Vorgabe des Systems.

Die spanische, italienische, portugiesische, russische und chinesische Übersetzung sind neu: Korrekturen von Spielern
dieser Sprachen sind willkommen. Die der Oberfläche sind einfache `.po`-Dateien in `gui/i18n`, die Poedit und
ähnliche Programme öffnen; die dieser Anleitung sind `docs/guide.<language>.md`.

Die Seiten nutzen das ganze Fenster, wie groß es auch ist: Die Bretter des Tabs Live wachsen mit, das Formular für ein
neues Turnier teilt sich in einem breiten Fenster in zwei Spalten, und die Liste der Seiten Engines, Bücher und
Elo-Listen lässt sich verbreitern, indem du ihren rechten Rand ziehst. Alles (Text, Knöpfe, Bretter) lässt sich
außerdem vergrößern oder verkleinern, von 80 bis 250 %: **Einstellungen** > Größe der Oberfläche, oder Strg + Mausrad,
Strg + und Strg -, und Strg 0 für die Rückkehr zu 100 %. F11 schaltet das Fenster auf Vollbild und zurück.

Ein Zähler auf dem Tab *Live* und auf dem Knopf *Turniere* gibt die Zahl der laufenden Partien an. Ein Zahlenfeld nimmt
Rechenausdrücke an: `40*60`, `15+10`, mit `+ - * /` und Klammern, ausgerechnet bei Enter oder wenn du das Feld
verlässt; Esc gibt dem Feld seinen Wert zurück.

### Bretter und ihre Knöpfe
<!-- en: boards-and-their-buttons 1ed997f4 -->

Unter jedem Brett, das eine Partie zeigt (das Nachspielen im Tab *Partien*, die Partie im Fokus in *Live*, ein
abgetrenntes Brett, die Seite Brett), führt eine Reihe kleiner Knöpfe zur Ausgangsstellung, zum vorherigen Zug, zum
nächsten und zum letzten Zug und dreht das Brett um; ihre Tooltips nennen die Tasten, die dasselbe tun: ← → Pos1 Ende,
und F zum Drehen. Wer die Tasten vorzieht, blendet die Knöpfe aus: mit **Einstellungen** > *Darstellung* >
*Navigationsknöpfe unter den Brettern*, mit einem Rechtsklick auf sie oder mit der Befehlspalette (*Navigationsknöpfe
unter den Brettern ausblenden*, und *Navigationsknöpfe unter den Brettern anzeigen*, um sie zurückzuholen). Das
Mausrad über einer Zugliste scrollt die Liste, nie die Züge. **A** auf einem beliebigen Brett öffnet seine Stellung auf
der Seite [Brett](#brett) und analysiert sie.

### Version und neue Versionen
<!-- en: version-and-new-versions 6305c913 -->

Die Version steht im Fenstertitel und in den **Einstellungen**, deren *Kopieren* sie zusammen mit dem System in die
Zwischenablage legt, für einen Fehlerbericht. *Mir sagen, wenn eine neue Version erscheint* (standardmäßig aus) fragt
GitHub beim Start nach der neuesten Version, höchstens einmal am Tag und im Hintergrund; ist sie neuer, sagt die
Seite Turniere es dir, mit *Download-Seite* (die Seite der Version auf GitHub, im Browser), *Neuerungen* (ihre Hinweise)
und *Ausblenden* (bis zur nächsten Version). Das Programm lädt nichts herunter und installiert nichts, und ein
Fehlschlag (kein Netz, GitHub nicht erreichbar) wird nur ins Protokoll geschrieben.

### Ein Problem melden
<!-- en: reporting-a-problem 3e4e5989 -->

*Problem melden…* (in den **Einstellungen**, auf der Seite **Protokoll**, in der Befehlspalette und in der Meldung
eines Fehlschlags beim Start) öffnet im Browser ein neues Issue auf GitHub, aus dem Formular für Fehlerberichte, schon
ausgefüllt mit der Version, dem System (Betriebssystem, Prozessor, Speicher, Grafikkarte und Treiber) und den letzten
Zeilen des Protokolls. Lies es vor dem Absenden, entferne, was du nicht zeigen willst (Pfade, Engine-Namen), und
schreib, was du getan hast, was du erwartet hast und was passiert ist. Das Programm selbst sendet nichts. Ideen
gehören an denselben Ort: *New issue*, *Idea*.

### Größe und Lage des Fensters
<!-- en: window-size-and-place 892d41c4 -->

Das Fenster öffnet sich so, wie du es verlassen hast: seine Größe, seine Lage auf den Bildschirmen, maximiert oder
nicht. Ist der Bildschirm, auf dem es war, nicht mehr da (ein abgesteckter Monitor), öffnet es sich in der Mitte des
Hauptbildschirms; passt seine Größe nicht mehr auf den Bildschirm (eine geringere Auflösung), in der Standardgröße. Die
herausgezogenen Bereiche, etwa ein abgetrenntes Brett, kommen ebenfalls dorthin zurück, wo sie waren (`layout.ini`),
und ins Fenster, wenn ihr Bildschirm fehlt. Wayland lässt ein Programm die Lage seines Fensters nicht wählen: Dort
kehren nur die Größe und der maximierte Zustand zurück.

### Wie das Fenster gezeichnet wird
<!-- en: drawing-the-window caff7bef -->

Das Fenster wird mit OpenGL 3.2 gezeichnet. Wo es das nicht gibt (eine virtuelle Maschine ohne 3D-Beschleunigung, ein
Remotedesktop, ein alter Server), zeichnet das Programm es stattdessen mit dem eigenen Renderer von SDL: Direct3D 11
unter Windows, OpenGL 2.1 oder allein mit dem Prozessor. Das Protokoll nennt den gewählten Weg und warum OpenGL nicht
verwendet wurde; **Einstellungen** > Grafikausgabe zeigt es auch, mit dem Namen der Grafikkarte, wenn OpenGL zeichnet.
*Software-Rendering* dort (oder `--software` in der Befehlszeile, für einen Start) lässt ab dem nächsten Start den
Prozessor das Fenster zeichnen, für einen Grafiktreiber, der versagt oder abstürzt. Das kostet mehr Prozessorzeit,
zeichnet nur neu, was sich ändert (und was sich bewegt, etwa Uhren, 15-mal pro Sekunde statt 30, solange Maus und
Tasten ruhen), zeichnet eckige Ecken, und die Bereiche des Arbeitsbereichs schweben im Hauptfenster statt auf einem
anderen Bildschirm. Unter Wayland (und unter macOS) wird das Bild des Prozessors trotzdem über den Grafiktreiber
gezeigt; gibt es keinen, starte das Programm mit `SDL_VIDEODRIVER=x11`. Funktioniert keine Art des Zeichnens, sagt
eine Meldung das, bevor das Programm endet.

### Darstellung
<!-- en: appearance 312266af -->

**Einstellungen** > *Darstellung* legt fest, wie jedes Brett des Programms aussieht (das Live-Mosaik und die
verfolgte Partie, die abgetrennten Bretter, das Großbild, eine in *Partien* nachgespielte Partie, der Tab
*Eröffnungen*, die Eröffnungen eines Buchs), mit einem kleinen Brett neben der Auswahl, das sie zeigt:

![Einstellungen > Darstellung: das braune Brett und die Figuren Celtic](screenshots/appearance.png)

- **Brett**: die Farben der Felder, jeweils für das helle und für das dunkle Design (gezeichnet wird das Brett des
  verwendeten Designs). Eine Vorgabe (*Blaugrau*, die Farben der früheren Versionen; *Braun*; *Blau*; *Grün*; *Grau*)
  oder *Eigene*: Ein Klick auf *Helle Felder*, *Dunkle Felder* oder *Letzter Zug* öffnet eine Farbauswahl, und wer
  eine Farbe ändert, macht das Brett zu einem eigenen, ausgehend von den Farben, die es hatte. *Letzter Zug* liegt über
  den beiden Feldern des letzten Zugs, und seine Transparenz lässt die Farbe des Felds durchscheinen.
- **Figuren**: ein in das Programm eingebauter Satz (*Cburnett*, der Standard; *Chessnut*; *Celtic*; *Fantasy*; ihre
  Autoren und Lizenzen im Tooltip und in `ATTRIBUTION.md`) oder *Dein eigener Ordner…*: ein Ordner mit 12 SVG-Bildern
  namens `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` und `bK.svg` … `bP.svg` (weißer und schwarzer
  König, Dame, Turm, Läufer, Springer, Bauer). Die Bilder werden wie die eingebauten mit nanosvg gezeichnet: einfache
  Formen, Füllungen, Linien und Verläufe; kein Text, keine Filter, keine eingebetteten Bilder. Fehlt eine Datei, ist
  sie größer als 1 MB oder kein Bild, das sich zeichnen lässt, sagt die Seite, welche und warum; bis dahin werden die
  Standardfiguren gezeichnet, und *Neu einlesen* liest den Ordner, sobald die Dateien in Ordnung sind. Die Webseite
  der Übertragung behält die Standardfiguren.
- **Zugtöne** (standardmäßig aus): *Ein Ton für jeden Zug* spielt ein kurzes Klopfen für jeden Zug der verfolgten
  Partie, in *Live* und auf dem Großbild, und ein schwereres für ein Schlagen; nicht für jede Partie des Mosaiks.
  *Lautstärke*, und *Zug anhören* / *Schlagen anhören*, um sie zu hören. Die Töne erzeugt das Programm selbst (keine
  Tondatei); das Audiogerät wird nur geöffnet, solange sie eingeschaltet sind, und gibt es keines, sagt das Protokoll,
  warum, und das Programm bleibt stumm.

- **Figurinen in der Notation** (standardmäßig an): Die Züge der Seite Brett, in ihren Zügen und in den Linien ihrer
  Analyse, werden mit den Figuren des oben gewählten Satzes geschrieben (aus Nf3 werden ein Springer und f3); aus, mit Buchstaben.
- **Logos** (standardmäßig aus): *Die Logos der Engines in Live und auf dem Großbild* zeichnet das Logo jeder Engine,
  auf ihrer Karte gewählt (siehe unten), neben ihren Namen in *Live*, auf ihren Karten und auf dem Großbild; aus, nur
  die Namen. Die Webseite der Übertragung zeigt sie in jedem Fall.

Die Befehlspalette bietet diese Wahl auch (*Brettfarben: Braun*, *Figuren: Celtic*, *Zugtöne einschalten*, *Logos der
Engines anzeigen*…). Sie wird in `settings.json` im Profil gespeichert; der Figurenordner wird im portablen Modus
relativ zum Programmordner gespeichert.

**Engine-Logos**: Die Karte einer Engine auf der Seite **Engines** hat *Logo* > *Wählen…*, ein PNG-, JPEG- oder SVG-Bild
von höchstens 2 MB (höchstens 4096 Pixel in jeder Richtung), gezeigt neben dem Namen der Engine in *Live* und auf dem
Großbild, wenn *Logos* eingeschaltet ist (oben), und auf der Webseite der Übertragung. Das Bild wird in den Ordner
`logos` des Profils kopiert, so dass das Original verschoben oder gelöscht und das Profil als Ganzes verschoben werden
kann; *Entfernen* nimmt es weg (und seine Kopie, sofern keine andere Engine sie hat). Das Logo lässt sich ändern,
während die Engine spielt. Solange die Logos aus sind, sagt die Karte es, mit *Logos anzeigen*.

### Das 3D-Brett
<!-- en: the-3d-board cc0dd596 -->

**Einstellungen** > *Darstellung* > *3D-Brett* > *Das Brett der Seite Brett in 3D* zeichnet das Brett der Seite
[Brett](#brett) dreidimensional, wie das Foto eines echten Bretts (standardmäßig aus; alle anderen Bretter bleiben
flach). Die Grafikkarte berechnet es durch Pfadverfolgung (Path Tracing): Das Bild wird einige Sekunden lang feiner,
solange sich nichts bewegt, mit einigen Millisekunden der Karte pro Bild, dann ruht die Karte; während sich das Brett
dreht, steht ein gröberes Bild an seiner Stelle. Das kleine Brett der *Darstellung* wird dann ebenfalls in 3D
gezeichnet, sodass sich die Einstellungen daneben zeigen, während sie sich ändern:

- die Beleuchtung, wie Fotografen ein Stillleben ausleuchten: *Studio*, *Low Key*, *High Key*, *Ausstellungsraum*,
  *Fensterlicht*, *Goldene Stunde* oder *Turniersaal*;
- *Belichtung*: heller oder dunkler, als die Beleuchtung es macht, in Blendenstufen, jede verdoppelt oder halbiert
  das Licht;
- *Qualität*: die Samples, aus denen das Bild besteht, bevor es ruht, standardmäßig 512; weniger schonen die Grafik
  eines Prozessors während Turnieren;
- *Lack*, von gewachstem, mattem Holz bis zu einer Schicht, in der sich die Lichter zeigen, und *Glanz*, von
  seidenmatt bis zum Spiegel;
- *Standard* stellt Belichtung, Qualität, Lack und Glanz wieder her, wie sie anfangs waren; die Beleuchtung bleibt.

Was das flache Brett zeigt, ist darauf: der Bewertungsbalken, der letzte Zug, die aufgenommene Figur und wohin sie
ziehen kann, ein König im Schach, die Pfeile der Analyse, die schwach durch die Figuren hindurch zu sehen sind, die
Antwort, die die Engine des Zuges erwartete, gestrichelt, das Urteil über den Zug als Plakette über seinem Feld und der
Rahmen einer Variante, die statt der Partie gezeigt wird. Ein Linksklick wählt ein Feld wie auf dem flachen Brett, auf
einer Figur oder auf dem Brett dahinter, und eine Figur lässt sich ziehen, ihr Abbild folgt dem Zeiger (siehe
[Brett](#brett)). Hältst du die rechte Maustaste gedrückt und bewegst die Maus, kreist die Kamera um das Brett (ein
Rechtsklick ohne Bewegung legt die aufgenommene Figur weiterhin ab), das Mausrad geht näher heran, und ein Klick mit dem
Mausrad holt die Ansicht zurück; das Brett zu drehen (F) lässt die Kamera eine halbe Runde machen.

Das 3D-Brett braucht OpenGL 3.3. Wo das Fenster ohne es gezeichnet wird (der Software-Renderer aus
[Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird), manche virtuellen Maschinen und Remotedesktops),
bleiben die Bretter flach, und die *Darstellung* sagt, warum. Die Figuren sind clarkerubbers Staunton-Pieces, unter
der MIT-Lizenz; die *Darstellung* verlinkt sie.

## Tastenkürzel
<!-- en: keyboard-shortcuts 149fc0ee -->

| Tasten | Wo | Was sie tun |
|---|---|---|
| <kbd>Strg</kbd>+<kbd>K</kbd> | Überall | Die Befehlspalette |
| <kbd>Strg</kbd>+<kbd>1</kbd>, <kbd>Strg</kbd>+<kbd>2</kbd>, <kbd>Strg</kbd>+<kbd>3</kbd> | Überall | Die erste, die zweite, die dritte Ansicht zeigen |
| <kbd>F1</kbd> | Überall | Diese Anleitung, beim Abschnitt zu dem, was unter der Maus liegt; noch einmal: schließt sie |
| <kbd>F11</kbd> | Überall | Vollbild und zurück (das eigene Fenster eines abgetrennten Bretts, wenn es die Tastatur hat) |
| <kbd>Strg</kbd>+Mausrad, <kbd>Strg</kbd>+<kbd>+</kbd>, <kbd>Strg</kbd>+<kbd>-</kbd> | Überall | Größe der Oberfläche; <kbd>Strg</kbd>+<kbd>0</kbd>: zurück auf 100 % |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Ein Zahlenfeld | Den Wert ausrechnen; dem Feld seinen Wert zurückgeben |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Der Anleitungsbereich | Zurück, vorwärts |
| <kbd>Strg</kbd>+<kbd>F</kbd> | Der Anleitungsbereich | Die Anleitung durchsuchen |
| <kbd>Esc</kbd> | Der Anleitungsbereich | Ihn schließen |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Bild↑</kbd> <kbd>Bild↓</kbd> | Die Befehlspalette | Wählen |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Die Befehlspalette | Ausführen, schließen |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Pos1</kbd> <kbd>Ende</kbd> | Ein Brett, das eine Partie zeigt | Vorheriger Zug, nächster Zug, Anfang, letzter Zug |
| <kbd>F</kbd> | Ein Brett | Es umdrehen |
| <kbd>A</kbd> | Ein Brett | Die Stellung auf der Seite Brett öffnen und analysieren; auf der Seite Brett: die Analyse starten oder beenden |
| <kbd>Ende</kbd> | Die Partie im Fokus in *Live* | Zurück zum aktuellen Zug |
| <kbd>Strg</kbd>+<kbd>V</kbd> | Die Seite Brett | Eine FEN, eine EPD-Zeile, ein PGN oder eine Zugliste einfügen |
| <kbd>G</kbd> | Die Seite Brett | Die Partie analysieren, Durchgang um Durchgang; noch einmal: anhalten |
| <kbd>X</kbd> | Die Seite Brett | Die Drohung zeigen oder ausblenden |
| <kbd>N</kbd>, <kbd>Umschalt</kbd>+<kbd>N</kbd> | Die Seite Brett | Der nächste, der vorherige von der Analyse markierte Zug |
| <kbd>Esc</kbd> | Die Seite Brett | Die Partie zurückholen, nachdem eine Linie gezeigt wurde; die aufgenommene Figur wieder ablegen; eine aus einem Mosaik verfolgte Partie: zurück zum Mosaik |
| <kbd>Strg</kbd>+<kbd>O</kbd>, <kbd>Strg</kbd>+<kbd>N</kbd> | Eine Ansicht fürs Studium | Eine PGN-Datei auf der Seite Brett öffnen; dort eine neue Partie |
| <kbd>Strg</kbd>+<kbd>Tab</kbd>, <kbd>Strg</kbd>+<kbd>Umschalt</kbd>+<kbd>Tab</kbd> | Eine Ansicht fürs Studium | Die letzten Partien; Loslassen von Strg öffnet die gewählte |
| <kbd>Enter</kbd> | Die Startseite | Die Partie auf dem Brett fortsetzen |
| Ein Buchstabe eines Zugs | Das Brett fürs Studium | Den Zug tippen: Die legalen Züge, die so beginnen, werden angeboten, in den Buchstaben der Sprache der Oberfläche oder in englischen; <kbd>↑</kbd> <kbd>↓</kbd> wählen, Enter spielt ihn |
| <kbd>Enter</kbd>, <kbd>Umschalt</kbd>+<kbd>Enter</kbd> | Das Brett fürs Studium | Ein Kommentar nach dem Zug, davor |
| <kbd>!</kbd> <kbd>?</kbd> | Das Brett fürs Studium | Das Symbol des Zugs; zwei Zeichen hintereinander: `!!`, `??`, `!?`, `?!` |
| <kbd>Entf</kbd> | Das Brett fürs Studium | Die Züge ab dem gezeigten löschen |
| <kbd>Alt</kbd>+<kbd>↑</kbd> | Das Brett fürs Studium | Die Variante nach oben verschieben, zuletzt zur Hauptvariante |
| <kbd>Strg</kbd>+<kbd>Z</kbd>, <kbd>Strg</kbd>+<kbd>Y</kbd> | Das Brett fürs Studium | Rückgängig, wiederherstellen |
| <kbd>Strg</kbd>+<kbd>S</kbd>, <kbd>Strg</kbd>+<kbd>Umschalt</kbd>+<kbd>S</kbd> | Das Brett fürs Studium | Die Partie speichern; eine Kopie anderswo speichern |
| <kbd>Strg</kbd>+<kbd>←</kbd>, <kbd>Strg</kbd>+<kbd>→</kbd> | Das Brett fürs Studium | Die vorige, die nächste Partie der Liste, aus der sie kam |
| <kbd>Strg</kbd>+<kbd>Umschalt</kbd>+<kbd>C</kbd> | Das Brett fürs Studium | Das Diagramm der Stellung als Bild in die Zwischenablage |
| <kbd>Alt</kbd>+<kbd>H</kbd> | Das Brett fürs Studium | Die Kopfdaten der Partie aufklappen (ein Hinweis während einer Partie gegen eine Engine) |
| <kbd>Alt</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>6</kbd> | Das Brett fürs Studium | Die Tabs Notation, Analyse, Baum, Referenzen, Fragen und Endspiele |
| <kbd>Alt</kbd>+<kbd>C</kbd> | Das Brett fürs Studium | Der Assistent kommentiert den Zug |
| <kbd>Alt</kbd>+<kbd>Q</kbd> | Das Brett fürs Studium | Warum nicht? Der probierte Zug gegen den der Partie oder der Zug der Partie gegen den besten der Auswertung |
| <kbd>Leertaste</kbd>, <kbd>Enter</kbd>, <kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Esc</kbd> | Der geführte Rundgang | „Ich weiß es nicht“, der nächste Moment, warum nicht ein anderer Zug, anhalten |
| <kbd>Alt</kbd>+<kbd>F</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>X</kbd>, <kbd>Alt</kbd>+<kbd>G</kbd>, <kbd>Alt</kbd>+<kbd>N</kbd>, <kbd>Alt</kbd>+<kbd>Umschalt</kbd>+<kbd>N</kbd> | Das Brett fürs Studium | Was F, A, X, G, N und Umschalt+N auf dem Brett für die Engine-Analyse tun (auf diesem Brett tippen die Buchstaben Züge) |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd>, <kbd>*</kbd> | Der Tab Baum | Einen Zug wählen, ihn spielen, ihn im aktiven Repertoire markieren |
| <kbd>Enter</kbd> | Die Seite Eröffnungen | Das Training des gezeigten Repertoires starten |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Leertaste</kbd>, <kbd>Esc</kbd> | Eine Trainingssitzung | Ein Hinweis, „Ich weiß es nicht“, anhalten |
| <kbd>Enter</kbd>, <kbd>Strg</kbd>+<kbd>Z</kbd> | Eine Trainingssitzung | Die nächste Stellung oder noch einmal nach einer falschen Antwort; einen zum Repertoire hinzugefügten Zug zurücknehmen |
| <kbd>Enter</kbd> | Die Seite Training | Alles wiederholen; oder die Aufgaben starten, sobald eine ihrer Auswahlen angeklickt wurde |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Leertaste</kbd>, <kbd>Esc</kbd> | Eine Serie von Aufgaben | Ein Hinweis, „Ich weiß es nicht“, anhalten |
| <kbd>Enter</kbd>, <kbd>→</kbd> | Eine Serie von Aufgaben | Die nächste Aufgabe oder noch einmal nach einer falschen Antwort; die Lösung Zug für Zug |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Leertaste</kbd>, <kbd>Esc</kbd> | Eine Übung aus deinen Fehlern | Ein Hinweis, „Ich weiß es nicht“, anhalten |
| <kbd>Enter</kbd> | Eine Übung aus deinen Fehlern | Die nächste Übung oder noch einmal nach einer falschen Antwort |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Züge erraten | Die Partie geht nach einer Bewertung sofort weiter; anhalten |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | Die Endspiele der Seite Training | Ein Endspiel wählen, sobald eines angeklickt wurde; es spielen |
| <kbd>Strg</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Esc</kbd> | Ein Endspiel gegen die Tablebases | Einen Zug zurücknehmen, der richtige Zug, anhalten |
| <kbd>Enter</kbd> | Ein Endspiel gegen die Tablebases | Nach einem Fehler: ihn zurücknehmen; am Ende: noch einmal oder das nächste |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Brettvisualisierung | Starten, weitermachen; anhalten |
| <kbd>Enter</kbd> | Die Seite Spielen | Eine Partie gegen die gewählte Engine spielen |
| <kbd>Strg</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> | Eine Partie gegen eine Engine | Deinen letzten Zug zurücknehmen; ein Hinweis |
| <kbd>Enter</kbd> | Eine Partie gegen eine Engine | *Aufgeben…* bestätigen; eine beim Schließen von RookStudio aufbewahrte Partie fortsetzen; nach ihrem Ende sie auswerten |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Die Seite Spielen, auf Lichess | Die empfangene Herausforderung annehmen, sonst eine Partie suchen; das Partiegesuch oder die Herausforderung zurückziehen |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Eine Partie auf Lichess | Den wartenden Fernschachzug senden, *Aufgeben…* bestätigen; den wartenden oder vorausgezogenen Zug zurücknehmen |
| <kbd>Enter</kbd> | Die Seite Spielen, die Turniere von Lichess | Der gezeigten Arena oder dem gezeigten Turnier nach Schweizer System beitreten (beim ersten Mal: auf Lichess erlauben) |
| <kbd>Alt</kbd>+<kbd>P</kbd> | Überall, in einer Arena auf Lichess | In der Arena pausieren, die Hilfsmittel zurück; noch einmal: zurück in die Arena |
| Ein Buchstabe, dann eine Ziffer | Die Koordinaten, *Wo steht…?* | Ein Feld benennen |
| <kbd>Umschalt</kbd> gehalten, <kbd>F2</kbd> | Blind | Die Figuren 2 Sekunden lang gezeigt, dreimal pro Partie; *Wo steht…?* |
| <kbd>N</kbd>, <kbd>n</kbd>, <kbd>-</kbd> | Eine Variante im Kopf nachspielen | Die Figur, wie FEN sie schreibt (ein weißer Springer, ein schwarzer), oder keine |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | Eine Partieliste | Durch die Liste gehen, die Partie öffnen |
| <kbd>Strg</kbd>+<kbd>F</kbd> | Die Seite Datenbanken | Der Suchbereich |
| <kbd>Esc</kbd> | Die Seite Datenbanken | Den Suchbereich schließen, dann die Liste |
| <kbd>Strg</kbd>+<kbd>Bild↓</kbd>, <kbd>Strg</kbd>+<kbd>Bild↑</kbd> | Das Dossier eines Spielers | Der nächste Tab, der vorige |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd>, <kbd>Strg</kbd>+<kbd>Z</kbd> | Das Dossier eines Spielers | Unter Namensvettern wählen, die Wahl behalten; sie rückgängig machen |
| <kbd>Esc</kbd> | Das Dossier eines Spielers | Zurück zur Seite, von der aus es geöffnet wurde |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | Die Turniere einer Datenbank | Ein Turnier wählen, seine Kreuztabelle öffnen |
| <kbd>Esc</kbd>, <kbd>Strg</kbd>+<kbd>Z</kbd> | Die Kreuztabelle eines Turniers | Zurück zu den Turnieren; seine letzte Änderung an der Datenbank rückgängig machen |
| <kbd>Strg</kbd>+<kbd>Bild↓</kbd>, <kbd>Strg</kbd>+<kbd>Bild↑</kbd> | Ein Eröffnungsbericht | Der nächste Abschnitt, der vorige |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Enter</kbd> | Die Züge eines Eröffnungsberichts | Einen Zug wählen, ihn auf der Seite Brett spielen |
| <kbd>Esc</kbd>, <kbd>Strg</kbd>+<kbd>Z</kbd> | Ein Eröffnungsbericht | Zurück zur Stellung; die zum Repertoire hinzugefügten Züge wieder herausnehmen |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Der Bereich Exportieren | Die Datei speichern (ein PDF: es zum Drucken öffnen); zurück zur Seite |
| <kbd>Strg</kbd>+<kbd>P</kbd> | Der Bereich Exportieren, ein PDF | Es im Anzeigeprogramm des Systems öffnen, zum Drucken |
| <kbd>Strg</kbd>+<kbd>Bild↓</kbd>, <kbd>Strg</kbd>+<kbd>Bild↑</kbd> | Der Bereich Exportieren | Das nächste Format, das vorige |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Pos1</kbd> <kbd>Ende</kbd>, <kbd>F</kbd> | Eine von RookStudio erstellte HTML-Seite | Durch die Züge; das Brett drehen |
| <kbd>←</kbd> <kbd>→</kbd> | Das Großbild | Die vorherige oder nächste Partie |
| <kbd>+</kbd> <kbd>-</kbd> | Das Großbild | Größerer oder kleinerer Text |
| <kbd>Esc</kbd> | Das Großbild | Es verlassen |
| <kbd>Tab</kbd>, Pfeiltasten, <kbd>Enter</kbd>, <kbd>Leertaste</kbd> | Neues Turnier | Zwischen den Zeilen, Listen und Knöpfen wechseln; eine Zeile oder eine Liste öffnen |
| <kbd>Strg</kbd>+<kbd>↑</kbd>, <kbd>Strg</kbd>+<kbd>↓</kbd> | Die Teilnehmer | Die ausgewählten Teilnehmer ganz nach oben, ganz nach unten schicken |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Die Teilnehmer | Sie um einen Platz verschieben |
| <kbd>Entf</kbd> | Die Teilnehmer | Sie entfernen |
| <kbd>Menü</kbd>, <kbd>Umschalt</kbd>+<kbd>F10</kbd> | Die Teilnehmer | Das Menü des Teilnehmers öffnen (seine eigenen Einstellungen) |
| <kbd>Strg</kbd>+<kbd>F</kbd> | Die Bibliothek der Einrichtung | Zu ihrem Filter springen |
| <kbd>Enter</kbd> | Die Bibliothek der Einrichtung | Die ausgewählten Engines hinzufügen, oder die erste angezeigte |
| <kbd>Strg</kbd>+<kbd>A</kbd> | Die Bibliothek der Einrichtung | Alle angezeigten Engines auswählen |
| <kbd>Strg</kbd>+Klick, <kbd>Umschalt</kbd>+Klick | Listen | Mehrere auswählen |
| <kbd>Entf</kbd> | Die Seite Engines | Die ausgewählte Engine entfernen (nach einer Bestätigung) |
| <kbd>Enter</kbd> | Die Karte einer Engine, ihr Lichess-Bot bereit | Den Bot online schalten |
| <kbd>Enter</kbd> | Die Karte einer Engine, *Auf lichess.org, für die Analyse* offen | Sie Lichess anbieten (beim ersten Mal: auf Lichess erlauben); den gestoppten Dienst *Fortsetzen* |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Ein Konto zum Lichess-Bot machen, sein Name eingetippt | Zum Bot machen; abbrechen |
| <kbd>Enter</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | Die Konsole einer Engine | Die Zeile senden; die früheren Zeilen zurückholen |
| <kbd>Strg</kbd>+<kbd>C</kbd> | Der Headless-Modus | Nach den laufenden Partien anhalten |

## Die Startseite
<!-- en: the-home-page 827b3d01 -->

Die **Startseite** einer Ansicht macht aus den Seiten der Ansicht Karten, das Laufende zuerst:

- **Die Arena**, in der du von RookStudio aus bist, ganz oben: ihr Name, dein Platz und deine Punkte, die
  verbleibende Zeit, *Zurück zur Arena* und *Pause* (Alt+P); ein Turnier nach Schweizer System, dem du beigetreten bist,
  mit seiner Runde (siehe [Arenen und Turniere nach Schweizer
  System](#arenen-und-turniere-nach-schweizer-system)).
- **Dort weitermachen, wo du warst**, wenn die Ansicht das Brett enthält: die Partie auf dem Brett bei ihrem Zug, auf
  einem kleinen Brett, woher sie stammt (ihre Datenbank und Nummer, ihr Platz in einem Filter oder ihre Datei), ihre
  Kommentare und ob sie noch nicht gespeicherte Änderungen hat. *Fortsetzen* (Enter) kehrt zu ihr zurück; Strg+Tab
  listet die letzten Partien auf.
- Was sich starten lässt, jeweils mit seiner Taste: *PGN öffnen…* (Strg+O), *Partie oder FEN einfügen* (Strg+V), *Neue
  Partie* (Strg+N), *In eine Datenbank importieren…*, *Gegen eine Engine spielen* (die Seite Spielen), *Neues Turnier*:
  die der Seiten der Ansicht.
- Die laufenden Turniere, wie auf der Seite Turniere, wenn die Ansicht die Seite Turniere enthält.
- **Zuletzt geöffnet**: die auf dem Brett geöffneten Partien; ein Klick öffnet eine wieder, mit ihren Änderungen.
- **Meine Datenbanken**: jede mit ihren Partien und dem Stand ihres Eröffnungsbaums, der laufende Import,
  *+ Importieren…* und die ohne Kopie gelesenen Turniere (siehe [Datenbanken](#datenbanken)).
- **Lichess**: das Konto, die Partien eines Spielers mit *Abgleichen*, die neuen Partien gezählt, wenn du es gewählt
  hast, die laufenden Übertragungen (siehe [Die Seite Lichess](#die-seite-lichess)).
- **Laufend**: eine von RookStudio aus gespielte Partie auf Lichess (*Zurück zur Partie*) und die empfangenen
  Herausforderungen (siehe [Auf Lichess](#auf-lichess)), dein Lichess-Bot online und seine Partie (siehe [Ein
  Lichess-Bot](#ein-lichess-bot)), deine Engines, die das Analysebrett von lichess.org bedienen (siehe [Deine Engine auf
  lichess.org](#deine-engine-auf-lichessorg)), die Analyse der Partie, die der Stellung, ein Import, der Assistent (sein
  Download, sein geladenes Modell, *Entladen*), eine laufende Serie von Aufgaben, eine Sitzung mit Übungen, eine
  geratene Partie, ein Endspiel oder eine Visualisierungsübung, und was die Wiederholung des Tages fällig hat, in einer
  Zahl; ein Klick wiederholt alles (siehe [Die Wiederholung des Tages](#die-wiederholung-des-tages)).

Der Suchknopf oben oder Strg+K findet einen Befehl, eine Partie, eine Datenbank.

## Die Seite Turniere
<!-- en: the-tournaments-page 91a20163 -->

Die Seite **Turniere** ist ein Kontrollraum: Was läuft und was dich braucht, kommt zuerst, danach der Rest.

- **Erste Schritte**, bis ein erstes Turnier gestartet wurde: die Schritte des [Schnellstarts](#schnellstart)
  (Engines hinzufügen, ein Turnier einrichten, es starten), jeder mit seinem Knopf und abgehakt, sobald er erledigt
  ist, und *Schnellstart lesen*, das ihn im Anleitungsbereich öffnet. *Ausblenden* räumt die Karte weg.
- **Alles pausieren**, neben *Ordner öffnen…* und *Neues Turnier*, pausiert die laufenden Turniere (kein neues
  Partiepaar beginnt, die laufenden Partien werden zu Ende gespielt) und hält die Warteschlange an: Kein Turnier der
  Warteschlange startet bis **Alles fortsetzen**, das sie fortsetzt und die Warteschlange weiterlaufen lässt, sofort,
  falls inzwischen ein Turnier zu Ende gegangen ist.
- **Kerne**: die Hardware-Threads der Maschine, und die davon, die die laufenden Partien belegen, der Rest frei. Eine
  Partie belegt die `Threads` derjenigen ihrer beiden Engines, die die meisten hat, da die beiden Engines abwechselnd
  denken; der Balken zeigt den Namen jedes Turniers und seine Zahl (auch die Engines der Seite Brett, die Partie des
  Lichess-Bots und die Anfragen deiner Engines auf lichess.org), der Mauszeiger die Einzelheiten.
- **Zu erledigen**, wenn etwas zu erledigen ist, jeweils mit der Behebung gleich daneben: eine Engine, die nicht
  startet (siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann): *Erneut versuchen*,
  *Zurückziehen…*, *Ersetzen…*, *Engine-Log*); das Turnier, das ein Fehler oder eine Engine angehalten hat, mit dem,
  was es angehalten hat, *Engine öffnen*, *Protokoll öffnen*, *Fortsetzen*, *Öffnen* und *Ausblenden*; ein Laufwerk,
  das für eine Nacht voller Partien zu voll ist (unter 1 GB frei, oder unter 2 % bei einem Laufwerk unter 500 GB), das
  des Turnierordners oder eines laufenden Turniers, jede Minute geprüft, mit *Ordner öffnen* und *Turnierordner
  ändern…* (wohin neue Turniere kommen, wie in den **Einstellungen**).
- **Läuft**: eine Karte für jedes laufende Turnier: sein Name, Format, Bedenkzeit und Zustand, ein Fortschrittsbalken
  mit den gespielten von den geplanten Partien und den laufenden Partien, wann es endet, und wer führt: der Erste der
  Tabelle mit seiner Wertung gegen das Feld und seinen Punkten (bei einem K.-o.-Turnier: die verbliebenen Spieler; bei
  einem Mannschaftskampf: der Stand der Mannschaften und seine Elo), die vorne liegende Engine eines Matches mit ihrer
  Elo und dem Stand, oder die LLR eines SPRT mit ihren Grenzen, dann die Elo und der Stand. *Öffnen* führt zu seinem
  Arbeitsbereich, *Pause* pausiert es. Ein Turnier, das fortgesetzt werden kann, erscheint stattdessen unter
  *Unterbrochen*, mit *Fortsetzen*. Siehe [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig) für die
  Gewichte und die Kerne jedes Turniers.
- **Dein Lichess-Bot**, solange er online ist: seine Engine, seine laufende Partie mit *Aufs Brett*, *Seine Karte* und
  *Offline schalten*; er lehnt Herausforderungen mit *später* ab, solange die Turniere die Kerne brauchen (siehe [Ein
  Lichess-Bot](#ein-lichess-bot)).
- **Deine Engines auf lichess.org**, solange sie die Anfragen bedienen: jede mit ihrem Zustand, *Die Karte der Engine*
  und *Dienst beenden*; sie nehmen nur die Threads, die die Turniere frei lassen (siehe [Deine Engine auf
  lichess.org](#deine-engine-auf-lichessorg)).
- **Warteschlange**: die wartenden Turniere, jedes mit seiner Dauer und wann es startet, und wann alles endet (siehe
  [Warteschlange und Neustart](#warteschlange-und-neustart)).
- **Zuletzt beendet**: die letzten fünf beendeten Turniere mit ihrem Ergebnis: der Stand eines Matches, die
  Entscheidung des SPRT, der Sieger eines K.-o.-Turniers (seines Finales), der Stand eines Mannschaftskampfs, oder der
  Erste der Tabelle mit seinen Punkten aus seinen Partien. *Neu starten mit…* für ein Match, *Klonen* (siehe
  [Warteschlange und Neustart](#warteschlange-und-neustart)) und *Öffnen*.
- **Zustand**: die Abstürze oder Hänger, Zeitverluste und illegalen Züge der letzten 24 Stunden, der freie und der
  eingebaute Speicher und die laufenden Engine-Prozesse; dann die Engines, die diese Partien verloren haben, die drei
  mit den meisten, mit *Engine öffnen* und *Ordner öffnen* (der Ordner `logs` des Turniers bewahrt das Engine-Log jeder
  Partie auf, die schlecht endete). Ein Zeitverlust vor dem 20. Zug wird gesondert ausgewiesen: Er liegt häufiger an
  einer falsch eingerichteten Engine (zu viele Threads, ein Hash, der zu groß ist, um rechtzeitig reserviert zu werden,
  ein Netz, das bei jeder Partie geladen wird, ein langsamer Start) als an ihrem Zeitmanagement.
- **Alle Turniere**: jedes Turnier des Profils, das neueste zuerst, mit *Öffnen*, *Klonen* und *Ordner*; ein Klick
  auf den Titel klappt die Liste zu. Das Ergebnis eines beendeten Turniers erscheint unter dem Mauszeiger, über seinem
  Zustand.

Die Befehlspalette hat diese Aktionen auch: *Alles pausieren*, *Alles fortsetzen*, *… neu starten mit…* für die
zuletzt beendeten Matches, *… klonen* für das angezeigte Turnier und die zuletzt beendeten, *Turnierordner öffnen*
und *Turnierordner ändern…*.

## Engines
<!-- en: engines 4f15155a -->

Die Seite **Engines** ist die Engine-Bibliothek: die Liste links, die Karte der gewählten Engine rechts.

### Engines hinzufügen und prüfen
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Hinzufügen…** nimmt eine ausführbare UCI-Datei auf. Die Engine wird einmal gestartet, um ihren Namen, ihren Autor und
ihre Optionen zu lesen. Die Optionen werden in einer Tabelle bearbeitet: Geänderte Werte sind hervorgehoben, und nur
Werte, die vom Standard der Engine abweichen, werden zu Beginn jeder Partie mit `setoption` gesendet. `Threads` und
`Hash` stehen oben.

- **Duplizieren** legt einen zweiten Eintrag für dasselbe Programm an, mit anderen Optionen (ein anderer `Hash`, eine
  Spielstärke, eine andere Netzdatei…). Einträge werden beim ersten Hinzufügen nach dem UCI-`id name` benannt und
  lassen sich umbenennen.
- **Prüfen** startet die Engine, misst, wie schnell sie auf `uci` und `isready` antwortet, und lässt sie einen Zug
  spielen; dann führt es die UCI-Konformitätsschritte von fastchess aus (`id name` und `id author`, Stellungen aus
  einer FEN und mit Zügen, `go` mit jeder Uhr, Info-Zeilen mit einer ganzzahligen Bewertung…), bis zum ersten, der
  scheitert. Das Ergebnis (✓ oder der Fehler) bleibt mit seinem Datum neben der Engine stehen, und
  *UCI-Konformitätsbericht* listet die Schritte auf, mit dem, was die Engine nach einem gescheiterten geschrieben hat.
  Eine Engine, mit der in xboard gesprochen wird, erhält nur den ersten Teil.
- **Konsole…** spricht mit der Engine, außerhalb jedes Turniers: Sie wird mit dem Programm, den Argumenten und dem
  Arbeitsverzeichnis der Bibliothek gestartet, und eine UCI-Engine erhält `uci` und dann die Optionswerte der
  Bibliothek. Eingetippte Zeilen werden so gesendet, wie sie sind (Enter; Hoch und Runter holen die früheren zurück),
  und das Log zeigt jede Zeile mit ihrer Uhrzeit, die gesendeten mit `>` markiert und die Fehlerausgabe mit `!`. Knöpfe
  senden die üblichen Befehle (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; die von WinBoard
  bei einer WinBoard-Engine, mit der in ihrem eigenen Protokoll gesprochen wird, sofern nicht *Über den Adapter*
  angehakt ist) und die Stellung der Seite Brett. *Kopieren* und *Speichern…* bewahren das Log auf. Die Konsole
  erscheint anstelle der Einstellungen der Engine (*Einstellungen* führt zu ihnen zurück, die Konsole bleibt offen);
  *Schließen* beendet die Engine. Während ein Turnier läuft, nimmt das, was die Engine sucht, seinen Partien Kerne weg:
  Die Konsole weist darauf hin. Ihre Engine ist keiner der Prozesse des Turniers: Das Ende eines Turniers beendet sie
  nicht.
- **Schnelltest** spielt ein kurzes Match gegen die Engine selbst oder eine andere: 10 Partien bei 10+0.1 aus 5
  Stellungen (eine mit einem En-passant-Schlag), über denselben Code wie ein Turnier, in seinem Arbeitsbereich gezeigt,
  während er läuft. Das Ergebnis bleibt neben der Engine stehen: ihre Punkte, ihre Zeitverluste, Abstürze und illegalen
  Züge, und der Anteil ihrer Uhr, den sie verbraucht hat, woran sich eine Engine erkennen lässt, die viel zu schnell
  oder zu nah am Zeitlimit spielt. Ihre Matt-PVs werden geprüft (siehe *Matt-PVs prüfen* in
  [PGN und Sonstiges](#pgn-und-sonstiges)). Nur der letzte Schnelltest wird aufbewahrt, in `tests/` im Profil (mit den
  Engine-Logs seiner Partien, die schlecht endeten).

### Eine große Bibliothek
<!-- en: a-large-library d118bc02 -->

- **Gruppen**: freie, durch Kommas getrennte Bezeichnungen (`pool A`, `list 40/15`…). Die Liste lässt sich nach Gruppe
  filtern oder durchsuchen, und eine Gruppe lässt sich mit einem Klick zu einem Turnier hinzufügen. *Notizen*: alles,
  was man sich über die Engine merken will. *Logo*: ein Bild, gezeigt neben ihrem Namen in *Live* und auf dem Großbild,
  wenn die Logos eingeschaltet sind, und auf der Webseite der Übertragung (siehe [Darstellung](#darstellung)).
- **Suche und Filter**: Jede Engine-Suche (die Bibliothek und die Engine-Listen der anderen Seiten) findet einen Namen,
  den Namen, den die Engine meldet, ihren Autor, ihren Programmpfad oder ihre Notizen; der Zustandsfilter zeigt die
  Engines, die funktionieren, fehlgeschlagen sind oder sich geändert haben, nie geprüft wurden, ihr Programm verloren
  haben, WinBoard sprechen oder inaktiv sind. Strg- oder Umschalt-Klick wählt mehrere Engines aus: *Prüfen* (eine nach
  der anderen, nie während ein Turnier läuft), *Zu einer Gruppe hinzufügen…*, *Entfernen…*. *Mehr…* prüft die nie
  geprüften (*Die noch nicht geprüften prüfen*) und entfernt die fehlenden Programme. Eine Zeichenketten-Option, die
  eine Datei oder einen Ordner enthält (`EvalFile`, `SyzygyPath`…), hat einen Knopf *…*.
- **Inaktiv** (auf der Karte, oder per Rechtsklick für alle ausgewählten Engines): Die Engine bleibt ausgegraut in der
  Bibliothek, fehlt aber in den Engine-Auswahllisten (Einrichtung, Suiten, Schnelltest, Neustart, Hinzufügen einer
  Engine zu einem laufenden Turnier) und bei *Die noch nicht geprüften prüfen*. Ein Turnier oder eine Einrichtung, die
  sie schon enthält, behält sie.
- **Nutzt eine GPU** (auf der Karte): eine Netzwerk-Engine, die auf einer Grafikkarte spielt, etwa Lc0. Es spielen nie
  mehr solche Engines gleichzeitig, als die **Einstellungen** unter *GPUs* angeben, in einem Turnier und über Turniere
  hinweg (siehe [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig)); ab dem nächsten Turnier, an dem sie
  teilnimmt.
- **Referenz-Elo**: die Wertung der Engine auf einer Liste, der du vertraust. Ein Turnier oder die Rangliste lässt
  sich daran verankern, und die Partien eines Turniers tragen sie in ihren Tags `WhiteElo` und `BlackElo` (kopiert,
  wenn die Engine dem Turnier beitritt). *Importieren…* > *Eine Rangliste, für die Referenz-Elo…* setzt sie für viele
  Engines auf einmal (siehe [Engines und Wertungen importieren](#engines-und-wertungen-importieren)).

### Beliebte Engines
<!-- en: popular-engines bda147a5 -->

*Beliebte Engines…*, neben *Hinzufügen…*, listet die stärksten Engines auf, deren Autoren auf GitHub Builds für Linux,
Windows und macOS veröffentlichen (Gaia, Stockfish, Triumviratus, Reckless, PlentyChess, pawnocchio, Coda, Cinder,
Stormphrax, Viridithas), jede in ihrer neuesten Version, mit der Datei, die dieser Prozessor ausführt (dem schnellsten
ihrer Builds, den der Prozessor unterstützt: AVX2, BMI2, AVX-512, ein Build für seine Zen-Generation…), ihrer Größe
und ihrer Lizenz. *Hinzufügen* lädt sie aus dem Release ihres Autors herunter, prüft sie gegen die SHA-256-Prüfsumme,
die GitHub veröffentlicht, packt sie aus ihrem Archiv aus, startet sie einmal (`uci`) und fügt sie zur Bibliothek
hinzu, jede Version in einem eigenen Ordner im Ordner `engines` des Profils; eine Datei, die nicht startet, wird durch
den nächsten Build ersetzt. *Abbrechen* stoppt einen Download, und der nächste Versuch macht dort weiter, wo er
aufgehört hat. Die Liste stammt aus einem Katalog im Repository von RookStudio, einmal am Tag neu gelesen, sonst aus
der eigenen Kopie des Programms. Was den Computer verlässt: der Katalog, eine Anfrage an api.github.com pro Engine
(einen Tag lang gespeichert) und die gewählte Datei; nur solange die Seite offen ist oder eine Engine hinzugefügt
wird.

Eine so hinzugefügte Engine folgt ihren neuen Versionen: Nach ihnen wird einmal am Tag gesucht, wenn sich die Seite
Engines öffnet (oder nur, wenn sich *Beliebte Engines…* öffnet, in den *Einstellungen*), und sie werden über der
Liste mit *Aktualisieren* angezeigt. Die neue Version kommt in einen eigenen Ordner, und die Engine übernimmt sie,
sobald sie auf `uci` geantwortet hat; *… auch behalten* behält die vorherige Version als eigene Engine, für ein Match
zwischen ihnen. Ein nicht beendetes Turnier behält die Dateien, mit denen es begonnen hat; die vorherige Version wird
gelöscht, sobald die Turniere, die sie spielen, beendet sind (siehe [Die Versionen einer
Engine](#die-versionen-einer-engine)).

Wo zuerst eine Engine gebraucht wird (die Analyse der Seite Brett, ihre Fragen und ihre Auswertung) und die Bibliothek
keine hat, bietet eine Karte Gaia an, mit einem Klick (oder Enter) heruntergeladen und zur Engine der Analyse und des
Assistenten gemacht; die anderen beliebten Engines und eine Engine von der Festplatte sind dort ebenfalls einen Klick
entfernt.

### Die Versionen einer Engine
<!-- en: an-engines-versions 8c733556 -->

Jede Version einer aus *Beliebte Engines…* übernommenen Engine bleibt in einem eigenen Ordner im Ordner `engines` des
Profils. Sobald ein Update eine Version in der Bibliothek ersetzt hat, wird ihr Ordner gelöscht, wenn kein nicht
beendetes Turnier (laufend, pausiert oder gestoppt) sie spielt: sofort, wenn keines es tut, sonst am Ende des letzten,
da RookStudio am Ende jedes Turniers erneut nachsieht. Ein Hinweis in der Fensterecke sagt es: *Gaia 4.4.3 gelöscht:
„Night Gaia“ ist beendet · 118 MB freigegeben*, mit *Die Karte der Engine*.

Hat die Engine mehr als die Version ihrer Bibliothek auf der Festplatte, listet ihre Karte sie unter *Ihre Versionen
auf der Festplatte* auf, jede mit ihrem Zustand:

- die der Bibliothek: die, die die Analyse, neue Turniere, der Bot und der Dienst für Lichess nehmen;
- eine, die ein nicht beendetes Turnier spielt, mit diesem Turnier und seinem Stand (*spielt, Partie 212 von 400*), an
  seinem Ende gelöscht, wenn ein Update sie ersetzt hat; *… auch behalten* behält sie für immer, als eigene Engine;
- eine absichtlich behaltene (*… auch behalten*, hier oder beim Update), nie gelöscht; *Nicht mehr behalten* (ein
  zweiter Klick) nimmt ihre Engine aus der Bibliothek, und ihr Ordner verschwindet, sobald kein nicht beendetes Turnier
  sie mehr spielt;
- eine von früher, seither nie durch ein Update ersetzt: behalten bis *Löschen* (ein zweiter Klick);
- eine gelöschte, die ein nicht beendetes Turnier wieder verlangt (fortgesetzt aus einem Ordner, in dem RookStudio
  nicht nachgesehen hat): *Erneut herunterladen* holt sie aus ihrem Release auf GitHub, mit der Prüfsumme verglichen,
  die sie hatte, an dieselbe Stelle.

### Engines starten
<!-- en: starting-engines 09d107ba -->

- **Das Lesen der Optionen** blockiert nie einen Start. *Fortsetzen* startet sofort: Ein Turnier spielt die in seinen
  Ordner kopierten Engines. *Start*, die Warteschlange und der Schnelltest warten nur auf das Lesen einer Engine, die
  sie spielen, und dieses wird vorgezogen; jedes andere Lesen oder Prüfen wird unterbrochen und später wiederholt. Ein Start, der wartet, zeigt es auf seinem Knopf (*Warte auf …*, mit *Abbrechen*), und ein weiterer Start
  wird so lange abgelehnt. Ein Start wartet auch auf eine laufende Testsuite, und eine Testsuite startet nicht, während
  der Schnelltest läuft oder ein Start wartet. Lesen, Prüfen und Testsuiten laufen neben den Turnieren, die in eigenen
  Prozessen spielen. Eine Engine in der Warteschlange lässt sich über ihr Menü als nächste lesen.
- *Die Engine für jede Partie neu starten* startet für jede Partie einen neuen Prozess, statt ihn wiederzuverwenden.
  Andernfalls hält ein Turnier einen wartenden Prozess pro Engine bereit, die noch Partien zu spielen hat, bis zu 32
  (plus zwei pro laufender Partie), und schließt eine Engine nach ihrer letzten Partie: in einem K.-o.-Turnier, sobald
  ihr Match verloren ist, im Schweizer System, sobald sie in der letzten Runde keine Partie mehr hat.
- *Start-Timeout (s)* (standardmäßig 60) ist die Zeit, die eine Engine bekommt, um beim Start mit `uciok` und zu
  Beginn jeder Partie auf `isready` zu antworten; wer langsam startet (ein großes Netz zu laden), braucht mehr. Eine
  Engine, die nicht startet, wird noch dreimal im Abstand von 10 s mit demselben Timeout versucht; eine Pause hält die
  Versuche an, ohne sie zu verbrauchen. Danach werden ihre Partien zurückgestellt, und die anderen gehen weiter (siehe
  [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)). Ein danach angelegtes Turnier kopiert den
  Timeout in seine Teilnehmer.

### Neu gebaute Programme
<!-- en: rebuilt-programs 549daa6e -->

- **Verschobene oder neu gebaute Programme**: Die Bibliothek merkt sich jedes Programm (Größe, Datum, CRC32). Ein
  Programm, das nicht mehr da ist, wird mit **!** und *Suchen…* markiert, und ein Turnier startet nicht mit ihm. Bei
  einem Programm, das sich geändert hat (ein neuer Build oder eine andere Datei unter demselben Pfad), werden die
  Optionen automatisch neu gelesen.
- **Was ein neuer Build geändert hat**: Weicht das neue Lesen vom vorigen ab, wird die Engine mit **Δ** markiert, und
  ihre Karte listet die Unterschiede auf, bis *Schließen*: ihr neuer `id name` (mit *In „…“ umbenennen*, wenn die
  Engine nach dem alten benannt war) und Autor, jede Option, deren Standard, Grenzen oder Auswahlwerte sich geändert
  haben (vorher und jetzt), die neuen und die weggefallenen Optionen, und dein Wert für jede (der Wert einer
  weggefallenen Option entfällt; einer, den der neue Build nicht mehr annimmt, steht in Orange). Für eine Option, deren
  Standard sich geändert hat oder deren Wert nicht mehr passt, wähle *Meinen Wert behalten* (oder *Alten Standard
  behalten*, wo du keinen hattest: Sie spielt wie bisher) oder *Neuen Standard nehmen*; *Meine Werte behalten* und
  *Neue Standards nehmen* wählen für alle. Der Hinweis verschwindet, sobald jede Wahl getroffen ist, es sei denn, er
  hat noch mehr mitzuteilen. Mehrere Builds, bevor er geschlossen wird, ergeben einen einzigen Hinweis, verglichen mit
  dem Lesen vor dem ersten. *Fehlgeschlagen oder geändert* im Zustandsfilter zeigt diese Engines ebenfalls, und die
  Befehlspalette hat für jede *Was sich geändert hat*, *Meine Werte behalten*, *Neue Standards nehmen* und *Hinweis zu
  den Änderungen schließen*.
- **Die Programme der Turniere**: Jedes Turnier hält die Programme fest, mit denen es gestartet wurde. Die
  Zusammenfassung der Einrichtung warnt vor einer Engine, deren Programm sich seit ihrem letzten Turnier geändert hat
  (dem neuesten Turnier des Turnierordners, das dieses Programm gespielt hat), mit *Was sich geändert hat*, wenn ihre
  Karte einen Hinweis hat. Wird ein Turnier fortgesetzt, dessen Programm seither neu gebaut wurde oder fehlt, sagt sein
  Kopf das, mit *Weiter* (der Hinweis schließt sich) und *Pause*; das Turnier hält nicht von selbst an.

### Engines und Wertungen importieren
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importieren…** > *Eine Engine-Liste…* liest die Engine-Liste eines anderen Programms: ChessGUI (`ChsGUI_Eng.TXT`,
mit den Optionen aus der daneben liegenden `ChsGUI_Cfg.ini`), cutechess und Banksia (`engines.json`), Arena
(`ArenaENG.cfg`, im Ordner von Arena: Programm, Argumente und Protokoll jeder Engine und, bei UCI-Engines, die in Arena
gesetzten Optionswerte) und andere Listen im INI-Stil. Eine Liste, die keine Engine liefert, sagt das, mit den
gelesenen Formaten und *Alle Engines eines Ordners…* als Alternative.

*Alle Engines eines Ordners…* übernimmt jedes in einem Ordner gefundene Programm und merkt sich den Ordner:
*Aktualisieren: die neuen Engines des Ordners* bietet dann nur die seither hinzugekommenen Engines an. Die gefundenen
Engines werden zuerst aufgelistet, damit du bei denen, die du weglassen willst, das Häkchen entfernst; Engines, die
schon in der Bibliothek sind (gleiches Programm und gleiche Argumente), werden übersprungen; die Optionen der anderen
werden gelesen; WinBoard-Engines werden ebenfalls importiert.

*Importieren…* > *Eine Rangliste, für die Referenz-Elo…* liest eine Rangliste, eingefügt oder geöffnet: die
Textversion einer Rangliste oder ihre Webseite (gespeichert oder kopiert), die Ausgabe von Ordo (Text oder CSV) oder
eine CSV-Datei mit Zeilen `name,elo`. Ein Name der Liste ist eine Engine der Bibliothek mit demselben Namen, oder die
einzige, deren Name von Groß-/Kleinschreibung und Leerzeichen abgesehen derselbe ist (wie bei den Spielern
importierter PGN-Dateien, siehe [Elo-Listen](#elo-listen)). Die Vorschau zeigt die alte und die neue Referenz-Elo jeder
Engine, zum Setzen angehakt, und die Namen der Liste, die keine Engine der Bibliothek sind. Die Namen einer Liste
enthalten oft ihren Build (`64-bit 4CPU`): Benenne die Engines bei Bedarf passend um.

### WinBoard-Engines
<!-- en: winboard-engines ff0920db -->

**WinBoard-Engines** (xboard-Protokoll, CECP) werden wie die anderen gespielt. Das *Protokoll* einer Engine wird beim
Hinzufügen ermittelt (*Automatisch*) oder gewählt: UCI oder WinBoard. fastchess spricht nur UCI: Eine WinBoard-Engine
wird über eine in RookStudio eingebaute Übersetzung gestartet (`rookstudio --wb2uci ENGINE [ARGS]`, das
fastchess anstelle der Engine startet), und ihre Optionen, `memory`, `cores` und der Syzygy-Pfad werden zu
UCI-Optionen. Sie steuert die Engine so, wie WinBoard es tut: Features von Protokoll 2, oder Protokoll 1 für Engines,
die keine senden; Stellungen mit `setboard` oder im Edit-Modus; Züge in Koordinaten oder SAN; `level`,
`time`/`otim`, `st`, `sd`, `nps`; Denkausgabe, Aufgabe. Engines, die in der Liste mit ↔ markiert sind, sind
WinBoard-Engines.

- Eine Engine mit Protokoll 1 ohne `setboard` und ohne Edit-Modus (TSCP zum Beispiel) kann nur aus der
  Ausgangsstellung beginnen: Gib ihr die Eröffnungen als Züge (ein PGN-Buch), nicht als Stellungen (EPD).
- Chess960 für eine Engine mit der Variante `fischerandom`: `variant fischerandom`, Stellungen in X-FEN, Rochade als
  `O-O`/`O-O-O`, so wie WinBoard sie sendet.
- *Scores from White's side* unter den Optionen der Engine: für die alten Engines, die ihre Bewertungen aus der Sicht
  von Weiß angeben statt aus der Sicht der Seite am Zug, wie es das Protokoll verlangt; ihre Bewertungen als Schwarz
  werden umgekehrt.
- Eine Engine, die verlangt, für jede Partie neu gestartet zu werden (`feature reuse=0`), bekommt beim Hinzufügen
  *Die Engine für jede Partie neu starten* angehakt.
- `FCG_WB2UCI_LOG=FILE` in der Umgebung schreibt jede mit WinBoard-Engines ausgetauschte Zeile in `FILE`.

### Ein Lichess-Bot
<!-- en: a-lichess-bot 5ae494c4 -->

Die Karte einer Engine hat eine Zeile **Auf Lichess, als Bot**: *Zum Lichess-Bot machen…* richtet die Engine so ein,
dass sie auf Lichess unter einem Bot-Konto spielt, über die Bot-API von Lichess (wo Engines erlaubt sind), und die
Herausforderungen annimmt, die du wählst, solange RookStudio offen ist. Die Zeile wird zu einem Teil der Karte, der
durch die folgenden Schritte führt.

- **Das Konto des Bots** ist ein zweites Lichess-Konto, getrennt von deinem: neu, mit einem eigenen Namen und ohne
  gespielte Partie. RookStudio erstellt keine Konten: *Das Konto auf lichess.org erstellen* öffnet die Seite von
  Lichess. *Das Konto des Bots verknüpfen…* öffnet die Seite von Lichess, die um eine Berechtigung bittet, *als Bot
  spielen* (`bot:play`): Melde dich dort mit dem Konto des Bots an, nicht mit deinem (ein privates Fenster des Browsers
  vermeidet, dass du abgemeldet wirst). Oder füge ein auf lichess.org erstelltes Token des Bot-Kontos ein (*Ein Token
  auf lichess.org erstellen* öffnet die Seite mit dieser Berechtigung angehakt) und *Prüfen*. Dein eigenes Konto bleibt
  getrennt verknüpft, für den Rest von RookStudio; der Bot hat sein eigenes Token, in `lichess-bot.json` im Profil
  aufbewahrt, nur für dich lesbar.
- **Die Prüfungen**, bevor irgendetwas erbeten wird, das das Konto verändert: sein Name und wann es erstellt wurde, die
  Partien, die es gespielt hat (keine), ob es schon ein Bot ist (dann dient es so, wie es ist) und dass es nicht dein
  eigenes verknüpftes Konto ist. Dein eigenes Konto wird nie behalten (das Token, das RookStudio dafür bekommen hat,
  wird sofort widerrufen); ein geschlossenes Konto oder ein Token ohne die Berechtigung ebenso wenig. Ein Konto, das
  gespielt hat, wird abgelehnt, wie Lichess es täte: Erstelle ein neues, spiele nichts darauf, dann verknüpfe es (*Ein
  anderes Konto verknüpfen…*, *Dieses trennen*).
- **Zum Bot machen…** lässt sich nicht rückgängig machen: Das Konto spielt nie wieder als Mensch, Lichess markiert es
  als BOT, und es spielt nur Herausforderungen (keine Lobby, keine Turniere außer den für Bots offenen Arenen). Der
  Schritt sagt das und fragt nach dem Namen des Kontos: *Zum Bot machen* wird aktiv, sobald der eingetippte Name seiner
  ist (Esc bricht ab). Was den Computer verlässt: diese Anfrage, mit dem Token des Bots. Eine Ablehnung wird mit den
  Worten von Lichess genannt; ohne Antwort hat sich nichts geändert: Versuche es erneut.
- **Die Herausforderungen, die er annimmt**, auf der Karte: die Bedenkzeiten (Bullet, Blitz, Schnellschach, klassisch,
  Fernschach; nie UltraBullet, das Lichess Bots nicht spielen lässt), gewertete Partien oder nur ungewertete, Menschen
  und Bots, ein Wertungsbereich für den Herausforderer (0: keine Grenze), Chess960, wenn die Engine es kennt
  (`UCI_Chess960`), eine Partie nach der anderen. Anfangs: nur ungewertet, Blitz und Schnellschach, da Lichess Bots
  bittet, ungewertet zu spielen, solange sie getestet werden; erweitere sie danach. Eine gewertete Herausforderung von
  deinem eigenen Konto wird abgelehnt (Lichess verbietet gewertete Partien zwischen den Konten einer Person); eine
  ungewertete wird gespielt. Die anderen Herausforderungen werden höflich abgelehnt, mit dem Grund, den Lichess dem
  Herausforderer zeigt: *zu schnell*, *zu langsam*, *nicht diese Bedenkzeit*, *nur ungewertet*, *nur Standardschach*,
  *nicht diese Variante*, *keine Bots*, *nur Bots* oder *später*, solange er spielt, offline geht oder ein Turnier die
  Kerne braucht.
- **Sein Spiel**: die Engine mit ihren Optionen der Bibliothek (ihre `Threads` und ihr `Hash` zählen zum Kernbudget der
  Maschine, solange sie spielt), ein Polyglot-Buch deiner Bibliothek, gespielt, solange es einen Zug hat, die Tablebases
  der Turniere (`SyzygyPath`, für eine Engine, die die Option und keinen eigenen Pfad hat) und kein Nachdenken in der
  Zeit des Gegners. Seine Uhr kommt bei jedem Zug von Lichess, abzüglich einer Reserve für das Netz (ein Zehntel seiner
  verbleibenden Zeit, von 0,2 bis 1 Sekunde); bevor beide Seiten gezogen haben, spielt er innerhalb der Zeit, die
  Lichess für den ersten Zug gibt; im Fernschach denkt er 15 Sekunden pro Zug. Er nimmt ein angebotenes Remis an, wenn
  die Engine klar schlechter steht oder spät in der Partie nicht besser, lehnt eine Zugrücknahme ab und beansprucht den
  Sieg, wenn sein Gegner lange genug weg ist. Seine Worte, im Chat der Spieler zu Beginn und am Ende gesagt, wählst du
  (leer: nichts gesagt). Eine Engine, die nicht startet oder stehen bleibt, gibt die Partie auf (bricht sie ab, bevor
  beide Seiten gezogen haben), und die Karte sagt, warum.
- **Online schalten** (Enter) öffnet den eigenen Ereignisstrom des Bots (Lichess hält einen pro Token): Die
  Herausforderungen kommen und werden sofort nach seinen Regeln beantwortet. Online zeigt die Karte den Tag des Bots
  (gewonnene, remis gespielte und verlorene Partien, abgelehnte Herausforderungen), seine Partie auf einem kleinen Brett
  mit der Bewertung der Engine, *Aufs Brett*, seine letzten Partien und die abgelehnten Herausforderungen, *Seine Seite
  auf lichess.org* und **Offline schalten**: keine neue Herausforderung, die laufende Partie geht bis zu ihrem Ende
  weiter, dann schließt sich der Strom. Eine Fernpartie wartet auf Lichess, solange der Bot offline ist; sie kommt
  zurück, wenn der Bot wieder online ist. Der Bot spielt nur, solange RookStudio offen ist: Das Schließen von RookStudio
  während einer seiner Live-Partien fragt zuerst nach (*Bleiben* · Esc, *Offline schalten*, *Trotzdem schließen*), da
  die Partie auf Zeit verloren ginge.
- **Seine Partie auf dem Brett**: Zug für Zug verfolgt, die Seite des Bots unten, die Wertungen und Uhren der Spieler in
  ihren Streifen, und über der Spalte seine Karte: was die Partie ist, wer am Zug ist, die Suche der Engine (ihre
  Bewertung, Tiefe und erwartete Linie: ihre eigene, da sie die Partie spielt), *Auf lichess.org öffnen*, *Für ihn
  aufgeben…* (ein zweiter Klick) und *Offline schalten*. Die Züge auf dem Brett gehören allein dem Bot. Am Ende kommt
  die Partie in die Datenbank *Lichess · sein Name*, die angelegt wird, wenn sie fehlt.
- **Wo er erscheint**: Online erscheint der Bot dort, wo das Laufende erscheint: auf der Startseite unter *Laufend* und
  auf der Seite Turniere, mit seiner Partie, *Aufs Brett*, *Seine Karte* und *Offline schalten*.
- **Das Turnier zuerst**: Eine Herausforderung wird mit *später* abgelehnt, solange ein auf diesem Computer spielendes
  Turnier die Kerne braucht, die die Engine des Bots nähme. Eine Partie des Bots, die schon läuft, wenn ein Turnier
  beginnt, geht bis zu ihrem Ende weiter, die Threads ihrer Engine zum Kernbudget gezählt wie die der Engines der Seite
  Brett; währenddessen wird keine neue Herausforderung angenommen.
- **Fairplay**: Die Partien des Bots sind nicht deine, und sie sperren deine Hilfsmittel nicht. Während deiner eigenen
  Live-Partie auf Lichess wird die Suche des Bots weder auf seiner Karte noch auf dem Brett gezeigt (die Karte sagt,
  warum); seine Partien gehen weiter.
- **Den Bot trennen…** (ein zweiter Klick) widerruft sein Token bei Lichess und löscht es hier; das Konto bleibt ein
  Bot.
- Was den Computer verlässt: die Züge der Engine, seine Worte im Chat, seine Antworten auf die Herausforderungen, mit
  dem Token des Bots; nichts von deinem Konto. Sein Profil (Name, Beschreibung) wird auf lichess.org eingestellt. Seine
  Wahl und seine letzten Partien werden in `bot/bot.json` im Profil aufbewahrt.

Die Befehlspalette hat *Den Lichess-Bot online schalten*, *Den Lichess-Bot offline schalten*, *Die Partie des
Lichess-Bots auf dem Brett* und *Die Karte des Lichess-Bots*, und auf der Seite Engines *… zum Lichess-Bot machen…*.

### Deine Engine auf lichess.org
<!-- en: your-engine-on-lichessorg 6bee0124 -->

Die Karte einer Engine hat eine Zeile **Auf lichess.org, für die Analyse**: *Lichess anbieten…* bietet die Engine dem
Analysebrett von lichess.org an, über die API für externe Engines von Lichess. Sie rechnet hier, auf deinem Computer,
und ihre Varianten erscheinen dort, nur für dein Konto, im Engine-Menü des Analysebretts, solange RookStudio offen ist.
Die API von Lichess dafür ist noch im Alpha-Stadium.

- **Als was sie angeboten wird**: ihr Name bei Lichess (3 bis 200 Zeichen; *Gaia 4.4.4 (RookStudio)* als Vorgabe), die
  Maschine (dieser Computer oder eine entfernte Maschine aus Einstellungen > Maschinen, auf der die Engine ihren Pfad
  hat, per SSH, mit eigenen Threads), die Threads höchstens (Lichess verlangt für jede Anfrage 1 bis so viele) und der
  Speicher höchstens. Lichess lässt eine solche Engine für jede Partie im Chess960-Modus (`UCI_Chess960`) laufen: Eine
  Engine ohne ihn kann nicht angeboten werden, und ihre Zeile sagt es statt *Lichess anbieten…*.
- **Beim ersten Mal** fragt Lichess nach zwei weiteren Berechtigungen, `engine:read` und `engine:write` (um eine Engine
  für dein Konto zu registrieren, sie zu lesen, sie zurückzuziehen): *Auf Lichess erlauben…* (Enter) öffnet die Seite
  von Lichess; dein Konto bleibt verknüpft, und das Token, das sie ersetzt, wird widerrufen. Ohne verknüpftes Konto sagt
  es die Karte, mit *Konto verknüpfen…*.
- **Dann, ohne weitere Fragen**, registriert RookStudio die Engine bei deinem Konto (eine Anfrage, mit deinem Token)
  und ein hier gezogenes Geheimnis, das Lichess nie wieder zeigt; RookStudio bewahrt es in der Datei
  `lichess-engines.json` des Profils auf, nur für dich lesbar, und zeigt oder protokolliert es nie. Danach wartet es bei
  engine.lichess.ovh nur mit diesem Geheimnis auf Arbeit, ohne dein Token: ein langes Warten, erneuert, solange der
  Dienst läuft. Fragt dein Analysebrett, kommt eine Anfrage mit der Stellung (eine Ausgangsstellung und Züge), den
  Threads, dem Speicher, 1 bis 5 Varianten und einer Zeit, einer Tiefe oder Knoten: Die Engine wird eingestellt (der
  Chess960-Modus, `UCI_AnalyseMode`, wenn sie ihn hat, ihre Optionen aus der Bibliothek), und ihre Varianten (Tiefe,
  MultiPV, Bewertung, Knoten, Zeit, PV) gehen an Lichess, sobald sie sie findet, dann ihr bester Zug. Wenn du
  weiterziehst, schließt Lichess die Anfrage, und die Engine hält an.
- **In Betrieb** zeigt die Karte die Anfrage, die gerade beantwortet wird (ihre Varianten, Threads, Speicher, Tiefe
  und Geschwindigkeit), die Anfragen und Analyseminuten von heute, die angefragte Stellung mit dem besten Zug der Engine,
  *Analysebrett von Lichess öffnen* und **Dienst beenden**. Die Seite Lichess listet die angebotenen Engines auf
  (*Deine Engines auf lichess.org*); die Startseite (unter *Laufend*) und die Seite Turniere zeigen die, die in Betrieb
  sind, und der Balken der Kerne ihre Threads.
- **Das Turnier zuerst**: Der Dienst nimmt nur die Threads, die die auf seiner Maschine laufenden Turniere frei lassen
  (die Kerne aus den Einstellungen), auch die Partie des Lichess-Bots geht vor (siehe [Ein
  Lichess-Bot](#ein-lichess-bot)); bleibt keiner übrig, pausiert er und sagt es, die Anfragen von Lichess warten. Eine
  laufende Anfrage wird abgebrochen, wenn ein Turnier ihre Threads braucht.
- **Fairplay**: Während deiner eigenen Live-Partie auf Lichess pausiert der Dienst bis zum Ende der Partie (die
  Anfragen bleiben unbeantwortet) und macht dann von selbst weiter. Die Stellungen deiner Fernpartien werden nie
  analysiert: Eine Anfrage zu einer von ihnen, zu einer Variante durch sie hindurch oder zu einem Zug davor (verglichen
  nach den Steinen und der Seite am Zug, wie es die Sperre aus [Fairplay](#fairplay) tut) erhält eine leere Antwort.
  Lichess selbst bietet eine externe Engine nur auf seinen Analyseseiten an, nie während einer Partie.
- **Dienst beenden**: RookStudio wartet nicht mehr auf Arbeit; die Engine bleibt bei Lichess registriert, ohne zu
  antworten; *Fortsetzen* (Enter) bedient sie wieder. *Ändern…* registriert sie neu, mit einem anderen Namen, einer
  anderen Maschine oder anderen Grenzen. **Von Lichess zurückziehen** löscht die Registrierung (eine Anfrage), und ihr
  Geheimnis wird nicht mehr verwendet; *Lichess anbieten…* erstellt sie neu. Ist RookStudio geschlossen, bleibt die
  Engine in der Liste von Lichess, ohne zu antworten; beim nächsten Start macht der Dienst weiter, wenn er beim
  Schließen in Betrieb war.
- **Wenn etwas schiefgeht**: Lehnt Lichess das Geheimnis ab (401: Die Registrierung wurde auf lichess.org
  zurückgezogen, oder das Geheimnis hat sich geändert), stoppt der Dienst, mit *Neu registrieren (ein neues
  Geheimnis)*; eine Antwort von Lichess, die RookStudio nicht liest (die API ist im Alpha-Stadium), stoppt ihn, statt
  falsch zu antworten, und sagt es, mit *Nachsehen, ob eine Version von RookStudio ihr folgt*; offline wartet der
  Dienst auf das Netz und macht von selbst weiter (nach zu vielen Anfragen nach der Minute von Lichess).
- Was den Computer verlässt: bei der Registrierung der Name, die Grenzen und das Geheimnis, mit deinem Token; danach
  die Varianten der Engine für die Stellungen, die Lichess schickt. Diese Stellungen kommen aus deinem eigenen Browser,
  über Lichess, das die Varianten sieht. Sonst nichts von deinem Computer.

Die Befehlspalette hat *… lichess.org anbieten…* auf der Seite Engines, *Den Dienst von … auf lichess.org beenden* und
*Den Dienst von … auf lichess.org fortsetzen*.

## Bücher
<!-- en: books 4cf13005 -->

**Hinzufügen…** nimmt ein Eröffnungsbuch (EPD, PGN oder Polyglot `.bin`) unter einem Namen auf, mit einer
Beschreibung; bei einem PGN-Buch die Zahl der übernommenen Halbzüge, bei einem Polyglot-Buch die Tiefe, bis zu der
seinen Linien gefolgt wird. Der Abschnitt *Eröffnungen* der Einrichtung bietet dann die Bücher der Bibliothek nach Namen
an, mit ihrer Zahl an Eröffnungen (jede andere Datei lässt sich weiterhin wählen). *Entfernen* nimmt ein Buch aus der
Bibliothek und lässt die Datei unberührt.

### Ein Buch prüfen
<!-- en: checking-a-book 96eee229 -->

Das gewählte Buch wird im Hintergrund gelesen, mit den eigenen Lesern von fastchess (seine Eröffnungen sind also
genau die, die ein Turnier spielt), und geprüft:

- **illegale Stellungen**: nicht genau ein König jeder Farbe, Bauern auf der ersten oder letzten Reihe, die Seite,
  die nicht am Zug ist, im Schach;
- **Duplikate**: dieselbe Stellung wie eine frühere Eröffnung, auch per Zugumstellung;
- **vertauschte Farben**: eine frühere Eröffnung mit getauschten Farben (ein Buch, das zweimal mit vertauschten Farben
  gespielt werden soll, braucht nicht beide);
- **bereits beendete Partien**: kein legaler Zug, oder nicht genug Material zum Mattsetzen.

fastchess lässt die Eröffnungen, deren Stellung sich nicht spielen lässt, aus einem Turnier weg (eine FEN- oder
EPD-Zeile ohne König oder sonst fehlerhaft, ein König, der geschlagen werden könnte, eine PGN-Partie mit ungültigem
FEN-Tag), mit einer Warnung pro Buch; sie werden hier als illegale Stellungen gezählt. Eine PGN-Partie, deren Züge sich
ab einer Stelle nicht mehr lesen lassen (ein fehlerhafter oder mehrdeutiger Zug), behält die Züge davor. Die Zeilen
eines PGN-Buchs, die mit `%` beginnen (Escape-Zeilen, die der PGN-Standard zu Kommentaren macht; die AVT-Bücher haben
welche), werden ignoriert, auch von den Turnieren, und die Seite Bücher zählt sie (*Zeilen, die mit % beginnen,
ignoriert*).

Seine Eröffnungen werden darunter aufgelistet, alle oder nur die mit einem Problem, durchsuchbar nach einem Teil einer
FEN oder nach Zügen (ein PGN-Buch zeigt seine Züge in SAN); ein Klick zeigt die Stellung auf einem Brett. *Für das
nächste Turnier verwenden* übernimmt es in die Einrichtung (mit Chess960 bei einem Chess960-Buch). *Bereinigte Kopie
speichern…* schreibt ein EPD-Buch ohne die illegalen Stellungen, die Duplikate und die beendeten Partien (und, falls
angehakt, ohne die Stellungen mit vertauschten Farben) und fügt es zur Bibliothek hinzu. *Als Testsuite ausführen…*
öffnet ein EPD-Buch in **Suiten**.

### Bücher erstellen
<!-- en: creating-books b9c072ab -->

**Erstellen…** schreibt Bücher für die Bibliothek (auch in der Befehlspalette):

- *Chess324-Buch erzeugen*: Chess324 von Larry Kaufman. Die Könige und die Türme bleiben auf ihren üblichen Feldern,
  die Rochade ist also die übliche; Dame, Läufer und Springer jeder Seite werden auf die anderen Felder ihrer
  Grundreihe gemischt (die Linien b, c, d, f und g), die Läufer auf Feldern verschiedener Farbe. Das ergibt 18
  Aufstellungen pro Seite, die von Weiß und die von Schwarz unabhängig gewählt: *Alle 324 Stellungen…* oder *Nur die
  18 symmetrischen…*, bei denen die Figuren von Schwarz auf denselben Linien stehen wie die von Weiß. Es sind
  gewöhnliche Stellungen, in eine EPD-Datei geschrieben (angeboten neben den Büchern der Bibliothek oder in `books/` im
  Profil) und von jeder Engine spielbar, ohne Chess960.
- *Polyglot-Buch erstellen…*: ein Polyglot-Buch (`.bin`) aus den Partien einer PGN-Datei, so wie `make-book` von
  Polyglot es macht, mit der Umwandlung, die die PGN-Bücher der Teilnehmer zu Polyglot-Büchern macht. Sein Formular,
  das anstelle der Angaben zum Buch erscheint, erfasst die PGN-Datei (das in der Liste gewählte PGN-Buch, falls
  vorhanden), die
  **Tiefe** (die Halbzüge jeder Partie, die ins Buch eingehen), die **Mindestzahl an Partien**, die ein Zug in seiner
  Stellung braucht, um zu bleiben (`-min-game` von Polyglot; die anderen werden als zu selten gespielt weggelassen),
  die **Gewichte**, *Nach Punkten* (die Punkte der Seite, die den Zug gespielt hat: ein Sieg 2, ein Remis 1, eine
  Niederlage 0, wie Polyglot zählt; Partien ohne Ergebnis werden weggelassen, ebenso ein Zug, dessen Partien alle
  verloren wurden) oder *Nach Anzahl* (die Zahl der Partien, die ihn spielen), und, falls angehakt, eine **Wertung**:
  Nur die Züge eines Spielers mit mindestens dieser Wertung zählen, nach dem Tag `WhiteElo` oder `BlackElo` der Partie
  (ein Spieler ohne diesen Tag wird weggelassen). *Erzeugen…* fragt, wohin das Buch geschrieben werden soll
  (standardmäßig neben die PGN-Datei), und liest dann die Partien im Hintergrund, mit seinem Fortschritt und
  *Abbrechen*, neben dem Formular und am Ende der Liste; das Buch, nach Schlüssel sortiert, wie das Format es
  verlangt, wird dann zur Bibliothek hinzugefügt, seine Optionen in seiner Beschreibung. Die Konsole sagt, wie viele
  Partien eingegangen sind, wie viele die Optionen weggelassen haben und wie viele nicht gelesen werden konnten.

### Bücher aus den benannten Eröffnungen
<!-- en: books-from-the-named-openings df5656f9 -->

*Aus den benannten Eröffnungen von Lichess…* in **Erstellen…** erstellt ein Buch mit Startstellungen aus der Tabelle der
benannten Eröffnungen von Lichess, die schon im Programm enthalten ist (chess-openings, CC0): Nichts wird
heruntergeladen. Sein Formular, das anstelle der Angaben zum Buch erscheint, erfasst die **Familien** (A
Flankeneröffnungen, B halboffene Spiele, C offene Spiele, D geschlossene Spiele, E indische Verteidigungen), die
**Längen** der übernommenen Varianten in Halbzügen (anfangs 8 bis 16), *Eine Stellung pro benannter Variante* (mehrere
Varianten teilen sich einen Namen) und die **Anzahl** der Stellungen, gleichmäßig über die ECO-Codes verteilt, wenn mehr
in Frage kommen. Es sagt, wie viele Stellungen und Eröffnungen es enthalten würde; *Buch erstellen…* fragt, wohin die
EPD-Datei geschrieben werden soll, jede Stellung mit ihrer Zugnummer (`hmvc`, `fmvn`) und ihrer Eröffnung als `c0`, und
fügt sie zur Bibliothek hinzu.

### Bücher aus den bewerteten Stellungen
<!-- en: books-from-the-evaluated-positions 82dd002c -->

*Aus den bewerteten Stellungen von Lichess…* in **Erstellen…** erstellt ein Buch aus den Stellungen, die die Spieler von
Lichess analysiert hatten: denen der Varianten der benannten Eröffnungen und denen, die einen Zug davon abweichen, jede
mit ihrer Bewertung und deren Tiefe. Sie stammen aus der Bewertungsdatenbank von Lichess (CC0, 22,5 GB und 418 Millionen
Stellungen): Ihre Größe wird bei database.lichess.org abgefragt und genannt, bevor irgendetwas heruntergeladen wird;
*Herunterladen und lesen* liest sie, während sie ankommt, und behält nur die gefundenen Stellungen (ein paar Megabyte,
im Profil), nicht die Datei. *Eine schon heruntergeladene Datei lesen…* liest stattdessen eine
`lichess_db_eval.jsonl.zst` von der Festplatte. Außer der Anfrage nach der Datei verlässt nichts den Computer. Das Lesen
zeigt, wie viel gelesen und behalten ist; *Abbrechen*, eine Netzunterbrechung oder das geschlossene Programm behält, was
gelesen wurde, und *Fortsetzen* macht dort weiter (eine seither auf dem Server geänderte Datei wird von vorn gelesen).

Einmal gelesen, erfasst das Formular die behaltene **Bewertung**, für Weiß (anfangs +0.30 bis +0.80), die **Tiefe**
mindestens (30), die **Halbzüge** ab dem Anfang (8 bis 16), *Eine Stellung pro benannter Variante* (zuerst die auf der
Linie der Variante, dann die mit der tiefsten Bewertung, dann die der Mitte des Fensters nächste) und die **Anzahl** der
Stellungen, gleichmäßig über die ECO-Codes verteilt, wenn mehr in Frage kommen. Es sagt, wie viele Stellungen und
Eröffnungen es enthalten würde, und ihre mittlere Bewertung; *Buch erstellen…* schreibt die EPD-Datei, jede Stellung mit
ihrer Zugnummer, ihrer Bewertung für die Seite am Zug und deren Tiefe (`ce`, `acd`) und ihrer Eröffnung (`c0`), und fügt
sie zur Bibliothek hinzu. *Neu einlesen* liest die Datenbank erneut, für ihre neueren Bewertungen.

### CTG-Bücher
<!-- en: ctg-books a908f007 -->

**Hinzufügen…** nimmt auch ein Buch im Format CTG auf, dem der Eröffnungsbücher von ChessBase und Fritz: eine `.ctg` mit
ihrer `.ctb` und ihrer `.cto` daneben, eine beliebige der drei gewählt. Es wird nur gelesen, nie geschrieben. Seine
Seite sagt, wie viele Stellungen und Züge es enthält und wie viele Züge sein Autor grün (zu spielen) und rot (zu
vermeiden) markiert hat. Für jede Stellung hat das Buch seine Züge, jeden mit seinem Symbol (!, ?, !?, einziger Zug…),
seiner Farbe, der Bewertung der Stellung, zu der er führt, und den Partien dahinter: wie viele, wie sie ausgingen, die
Wertungen der Spieler.

*Im Baum öffnen* zeigt es im Tab *Baum* der Seite Brett, unter den Quellen: die Züge der gezeigten Stellung mit ihren
Partien, der Punktzahl, der mittleren Wertung der Spieler, die sie gespielt haben, und der Performance, und eine Spalte
*Buch* mit ihrer Farbe und ihrem Symbol. Grüne Züge kommen zuerst; rote bleiben in der Liste, ausgegraut. Die Jahre und
die Referenzpartien stammen aus den Datenbanken, nicht aus dem Buch.

fastchess liest keine CTG-Bücher. *Für Engines: ein Polyglot-Buch erstellen…* schreibt eine Polyglot-Kopie (`.bin`) und
fügt sie zur Bibliothek hinzu, für die *Eröffnungen* der Einrichtung und die eigenen Bücher der Teilnehmer: Seine roten
Züge werden ausgelassen, und jeder Zug wird mit den Punkten gewichtet, die seine Partien geholt haben (zwei für einen
Sieg, einer für ein Remis), doppelt so viel für einen grünen Zug; ein Zug, der nichts gepunktet hat, wird ausgelassen.
*Buch prüfen* folgt seinen Varianten ab der Ausgangsstellung und sagt, wie viele der Stellungen des Buchs es erreicht
hat und ob sich ein Zug nicht lesen ließ. Nichts verlässt den Computer.

## Brett
<!-- en: board 0bbd00c9 -->

Die Seite Brett zeigt eine Partie oder eine Stellung, von irgendwoher: **A** auf einem beliebigen Brett des Programms
(eine Partie des Mosaiks im Tab *Live* oder die verfolgte, das Brett des Tabs *Partien*, des Tabs *Eröffnungen*, einer
Eröffnung eines Buchs) öffnet seine Stellung hier und analysiert sie; auf der Seite Brett startet und beendet A die
Analyse. *Zurück zum Turnier* (oder *Zurück*) kehrt zu der Seite zurück, von der aus sie geöffnet wurde. Ihr Brett kann
in 3D gezeichnet werden: siehe [Das 3D-Brett](#das-3d-brett).

Das Brett steht links, mit den Spielern darüber und darunter; rechts, von oben nach unten, die Analyse, die Züge (mit
der Tabelle der Züge, der Auswertung der Partie und den Partien der Datei in Tabs) und die Partiegrafik. Der
Untertitel der Seite sagt, was für eine Partie es ist: ihre Veranstaltung und Runde, ihr Ergebnis, ihre Bedenkzeit,
ihr ECO-Code und ihre Eröffnung, und wie sie endete.

Ein Zug wird mit der Maus auf dem Brett gespielt: ein Klick auf die Figur, dann einer auf ihr Feld, oder die Figur
dorthin gezogen. Während sie gezogen wird, folgt sie dem Zeiger, und das Feld, auf dem sie landen würde, ist getönt;
woanders losgelassen, kehrt sie zurück, weiterhin aufgenommen, um mit einem Klick gezogen zu werden. Auf dem 3D-Brett
folgt ein durchscheinendes Abbild der Figur dem Zeiger, über dem Brett schwebend und hinter den Figuren vor ihm vorbei,
während das Bild bleibt, wie es ist; die Figur selbst bewegt sich, wenn sie losgelassen wird.

### Eine Stellung öffnen, einfügen und aufbauen
<!-- en: opening-pasting-and-setting-up-a-position b8dc5047 -->

- **PGN öffnen…** liest eine beliebige PGN-Datei (eine Datei lässt sich auch auf das Fenster ziehen), im Hintergrund
  mit einem Fortschrittsbalken: Eine Datei mit Millionen Partien braucht ein paar Sekunden, und ihre Partien werden in
  einem Tab neben den Zügen aufgelistet, gefiltert nach dem Eingetippten (jedes Wort, gefunden in den Spielern, der
  Veranstaltung, dem Ort, dem Datum, der Runde, der Eröffnung, dem ECO-Code oder dem Ergebnis). Ein Klick zeigt eine
  Partie. Die Bewertungen ihrer Züge zeichnen die Grafik: Gelesen werden die Kommentare von cutechess und fastchess
  (`{+0.35/18 1.2s}`) und das `[%eval 0.35]` von lichess und ChessBase (aus der Sicht von Weiß, `#-3` für ein Matt),
  ebenso die Uhren von `[%clk 0:01:23]`. Ihre Varianten bleiben erhalten (siehe [Varianten](#varianten)). Eine
  Partie, deren FEN-Tag sich nicht spielen lässt, wird abgelehnt, und die Züge einer Partie enden beim ersten, der
  sich nicht lesen lässt.
- **Einfügen** (Strg+V auf der Seite) liest die Zwischenablage: eine FEN oder die vier Felder einer EPD-Zeile; ein PGN
  mit einer oder mehreren Partien (aufgelistet wie die einer Datei); eine Zugliste ab der Ausgangsstellung, in SAN mit
  oder ohne Zugnummern (`1. e4 e5 2. Nf3`) oder in UCI (`e2e4 e7e5`). Eine Stellung, die sich nicht spielen lässt,
  wird mit dem Grund abgelehnt (ein fehlender König, ein Bauer auf der ersten Reihe, die Seite, die nicht am Zug ist,
  im Schach). Rochaderechte, deren König oder Turm nicht auf seinem üblichen Feld steht, oder die als Linien
  geschrieben sind (`HAha`), machen sie zu einer Chess960-Stellung. Auch eine Datei `.fen` oder `.epd` lässt sich auf
  das Fenster ziehen.
- **Stellung bearbeiten** baut eine Stellung auf dem Brett auf: Ein Klick setzt die gewählte Figur (ein zweiter Klick
  entfernt sie), ein Rechtsklick entfernt sie; dazu die Seite am Zug, die Zugnummer, die Rochaderechte (nur angeboten,
  wenn König und Turm stehen, wo sie stehen müssen), das En-passant-Feld (nur hinter einem Bauern, der gerade zwei
  Felder gezogen ist) und Chess960. Die FEN darunter folgt dem Brett, und eine dort eingetippte FEN stellt es ein.
  *Fertig* wird angeboten, sobald sich die Stellung spielen lässt, und bringt sie auf das Brett.
- *PGN kopieren* (rechts von den Tabs der Züge) kopiert die gezeigte Partie mit ihren Tags, ihren Zügen (die
  Bewertungen der Engines als Kommentare, wenn sie welche hat) und ihren Varianten. Strg+S oder *Partie als PGN
  speichern…* in der Befehlspalette schreibt dasselbe in eine Datei. Die Kommentare, Symbole ($1 oder !?), Pfeile und
  farbigen Felder, Uhren und Bewertungen einer geöffneten PGN-Datei bleiben erhalten und werden so zurückgeschrieben,
  wie sie gelesen wurden.
- **Das Brett**: Die Knöpfe darunter und ← → Pos1 Ende gehen durch die Züge, und F (oder der letzte Knopf) dreht es
  um; ein Klick auf eine Figur und auf ein Feld spielt einen Zug (ein Bauer, der umwandelt, fragt nach seiner Figur).
  *FEN kopieren* kopiert die Stellung.

### Die Spieler
<!-- en: the-players 1f4f551f -->

Über und unter dem Brett, wenn die Partie Spieler hat: der Name jedes Spielers, die Bewertung seines letzten Zugs
durch seine Engine (die Bewertung aus der Sicht von Weiß und die Tiefe, als Chip), der Zug, den er gespielt hat, und
die Zeit, die er dafür gebraucht hat, oder *am Zug*, und rechts seine Uhr nach seinem letzten Zug, umrahmt, solange er
am Zug ist. Die Uhren sind die der `[%clk]` der Züge, wenn die Partie sie hat; sonst werden sie aus ihrem Tag
`TimeControl` (`300+2`, `10+0.1`: Sekunden und Inkrement) und der Zeit jedes Zugs rekonstruiert, wobei Buchzüge keine
Zeit verbrauchen.

### Die Analyse
<!-- en: the-analysis 1e6120de -->

Eine Engine der Bibliothek durchsucht die gezeigte Stellung und folgt dem Brett. Die Karte zeigt ihre Engine, dann
einen Knopf mit ihren *Threads*, ihrem *Hash* (0: der eigene der Engine) und ihren *Linien* (MultiPV), den ein Klick
öffnet: die Einstellungen der Analyse selbst, die für das nächste Mal erhalten bleiben und weder die Engine der
Bibliothek noch irgendein Turnier ändern (die übrigen Optionswerte der Bibliothek werden so gesendet, wie eine Partie
sie sendet). Rechts die Threads, die die Seite Brett von denen der Maschine belegt, in der Warnfarbe, während ein
Turnier läuft (ihr Tooltip sagt, wie viele Threads die Partien des Turniers belegen: Die Analyse konkurriert mit
ihnen).

Die Suche erscheint als Kacheln (Tiefe und seldepth, Knoten, Geschwindigkeit, Zeit, Tablebase-Treffer, wie voll der
Hash ist), dazu die Gewinn-, Remis- und Verlustchancen, wenn die Engine sie angibt (`UCI_ShowWDL`), und jede Linie:
ihre Bewertung (aus der Sicht von Weiß, die beste hervorgehoben), ihre Tiefe und ihre Züge auf zwei Zeilen, mit
Figurinen (siehe [Darstellung](#darstellung)). Steht die Maus über einem Zug einer Linie, zeigt das Brett die
Stellung danach, umrahmt; ein Klick spielt die Linie bis zu diesem Zug. Der erste Zug jeder Linie wird als Pfeil
gezeichnet, der beste dunkler (**Einstellungen** > *Darstellung* > *Die Pfeile der Analyse auf der Seite Brett*
blendet sie aus; die Linien zeigen die Züge weiterhin).

**Drohung** (X) lässt einen weiteren Prozess der Engine die Stellung mit übergebenem Zugrecht durchsuchen: den besten
Zug des Gegners, dürfte er noch einmal ziehen, als roter Pfeil gezeichnet und unter die Linien geschrieben (keiner,
solange die Seite am Zug im Schach steht).

Die Analyse wartet, während eine andere Seite gezeigt wird, und geht weiter, wenn die Seite Brett wiederkommt;
*Analyse beenden* (oder A) beendet sie, und ein Punkt auf dem Knopf *Brett* der Leiste zeigt an, dass sie läuft. Ihre
Engine ist keiner der Prozesse des Turniers: Das Ende eines Turniers beendet sie nicht. Nach dem Beenden bleibt die
Engine für die nächste Suche gestartet, mit ihrer Hashtabelle (und dem GPU-Speicher einer Netz-Engine): Der
Ein/Aus-Knopf neben der Engine, *Engine entladen* (auch in der Befehlspalette), beendet sie und die der Drohung.

### Eine ganze Partie analysieren
<!-- en: analysing-a-whole-game a7e6166c -->

**Partie analysieren** (G, in den Werkzeugen der Seite oder in der Kopfzeile der Grafik) lässt die Analyse-Engine
(ihre Threads und ihr Hash, eine Linie) jede Stellung der Partie im Hintergrund durchsuchen, zuerst je 20 ms, dann bei
jedem Durchgang erneut viermal so lange (80 ms, 0,3 s, 1,3 s, 5 s…), bis zu der Zeit, die mit dem Pfeil neben dem
Knopf gewählt wird (0,3 s, 1,3 s, 5 s, 20 s pro Stellung oder *bis zum Stoppen*): Die Grafik ist nach ein, zwei
Sekunden da und wird mit jedem Durchgang schärfer. Die Stellungen werden von der letzten zur ersten durchlaufen,
sodass der Hash der Engine festhält, was nach einer Stellung kommt (gemessen: bei gleicher Zeit etwa 16 % näher an
langen Suchen als von der ersten zur letzten), die Stellung auf dem Brett zuerst. Die Kopfzeile der Grafik nennt den
Durchgang, die Zeit pro Stellung und den Fortschritt, und ein schmales Band unter ihrem Zeitstreifen, was jede
Stellung bekommen hat; *Stopp* (oder G) hält sie an.

Jeder Zug wird nach den Gewinnchancen beurteilt, die die ziehende Seite mit ihm verloren hat, aus den Bewertungen der
Analyse davor und danach (das Maß von lichess: 50 % bei 0.00, etwa 59 % bei +1.00): 10 verlorene Punkte sind eine
Ungenauigkeit (`?!`), 20 ein Fehler (`?`), 30 ein grober Fehler (`??`), markiert hinter dem Zug, auf der Grafik und
auf dem Brett; der Zug, den die Engine gespielt hätte, wird nie markiert. Ein Zug wird nach dem letzten vollständigen
Durchgang beurteilt; eine Markierung, die der letzte Durchgang geändert hat, ist hohl, bis der nächste sie bestätigt.
N und Umschalt+N (oder die Knöpfe neben der Navigation) gehen zum nächsten und zum vorherigen markierten Zug.

Jeder vollständige Durchgang wird mit der Partie aufbewahrt (in `analyses/` im Profil): Dieselbe Partie, von irgendwo
erneut geöffnet, zeigt die Analyse sofort, und G macht bei den erledigten Durchgängen weiter. Die Analyse läuft in
einem eigenen Thread mit einem eigenen Engine-Prozess, ihre Threads zählen zum Kern-Budget; die Analyse des Bretts (A)
pausiert sie, und wenn A eine Stellung der Partie länger durchsucht, als der Durchgang es tat, tritt ihr Wert an die
Stelle des Werts dieser Stellung.

**Mehrere Analysen**: *+ Engine* in der Legende der Grafik lässt eine weitere Engine der Bibliothek die Partie
ebenfalls analysieren, bei jedem Durchgang gleich lang. Jede Analyse hat ihre Farbe auf der Grafik und auf dem Brett
(ihr bester Zug als Pfeil, solange die Analyse des Bretts aus ist), ihre Zeile in *Dieser Zug* und ihre Genauigkeiten
in der Auswertung. Sie gehen gemeinsam Durchgang für Durchgang vor, alle gleichzeitig, wenn die Kerne es erlauben,
sonst eine nach der anderen. Eine Analyse beurteilt die Züge (die erste; ein Rechtsklick auf eine andere in der
Legende macht sie zum Richter oder entfernt sie); eine Markierung ist hohl, wenn die Analysen den Zug nicht alle
gleich beurteilen. Gewinnchancen vergleichen Engines besser als Centipawns, die sich von einer Engine zur anderen
unterscheiden: Sie nehmen das WDL jeder Engine, wenn sie eines angibt.

### Die Partiegrafik
<!-- en: the-game-graph 14e02bad -->

Unter den Zügen die Grafik der Partie, dieselbe wie in *Live* und *Partien*:

- **Bewertung**: die beurteilende Analyse als weiße und schwarze Fläche (der Vorteil von Weiß über der Linie, der von
  Schwarz darunter), jede Engine, die gespielt hat, als Kurve in ihrer Farbe bei ihren eigenen Zügen, die anderen
  Analysen als gestrichelte Kurven; die markierten Züge auf der Kurve; Werte jenseits von ±6 am Rand entlang in der
  Warnfarbe gezeichnet. Ohne Analyse ist die Fläche der Mittelwert der Bewertungen der beiden Engines.
- Die Buchzüge schraffiert, die Phasen unter der Achse (die Eröffnung; das Mittelspiel ab der ersten Stellung mit
  höchstens 10 Figuren außer Königen und Bauern oder weniger als 4 auf einer Grundreihe; das Endspiel ab höchstens 6,
  wie lichess eine Partie einteilt), die Zone, die die Tablebases kennen, eingefärbt, die Zugnummern am unteren Rand.
- Unter der Achse die Zeit jedes Zugs: die von Weiß nach oben, die von Schwarz nach unten.

Ihre Tabs: *Gewinnchancen* (aus dem WDL, wenn die Engines es angeben, sonst nach der Formel von lichess), *S/R/N* (die
Anteile, die die Engines gemeldet haben, die Siege von Weiß unten), *Zeit* (mit der Entscheidungszeit), *Tiefe* (mit
der seldepth), *Knoten*, *Geschwindigkeit*, *TB-Treffer*, *Übereinstimmung* (wie weit die Bewertungen der beiden
Spieler auseinanderliegen, jeder Zug gegen den vorherigen, und die der Analysen in jeder Stellung) und
*PV-Übereinstimmung* (wie viele Halbzüge der PV einer Engine wiederholen, was die der anderen vorhergesagt hat); die,
für die die Partie keine Werte hat, werden weggelassen, und die, die keinen Platz haben, kommen in das Menü `⋯`. Steht
die Maus darüber, nennt die Grafik den Zug und was jede Engine sagte; ein Klick oder ein Ziehen geht zu dem Zug. Ein
Klick auf eine Kurve in der Legende blendet sie aus oder ein; der Pfeil rechts klappt die Grafik ein.

### Dieser Zug
<!-- en: this-move a4a908f8 -->

Unter den Zügen, zum gezeigten Zug: was die Engine, die ihn gespielt hat, sagte (ihre Bewertung, Tiefe und seldepth,
Zeit, Knoten, Geschwindigkeit, Tablebase-Treffer, so wie das PGN sie hat), die Linie, die sie danach erwartete, der
Befund jeder Analyse (der Anteil der verlorenen Gewinnchancen, das Urteil, der beste Zug und seine Bewertung), und mit
den Tablebases das Ergebnis nach dem Zug und ob der Zug das vorherige gehalten hat. *Zeigen* legt eine Linie auf das
Brett, ohne die Partie zu berühren: die Stellung danach, das Brett umrahmt, darüber geschrieben, was es ist; Esc, ein
Klick auf das Brett oder ein Schritt durch die Partie holt die Partie zurück. Auf dem Brett ist das Urteil über den
Zug ein Abzeichen auf seinem Feld, und die Antwort, die seine Engine erwartete, ein gestrichelter Pfeil.

### Varianten
<!-- en: variations f4e65c3b -->

Ein Zug, der auf dem Brett gespielt wird, wo die Partie anders weiterging, beginnt eine Variante: Die Partie bleibt,
wie sie war, mit ihren Analysen und ihrer Grafik. Die Züge zeigen die Varianten in Klammern nach dem Zug, den sie
ersetzen, verschachtelt und nummeriert; ein Klick auf einen Zug geht dorthin, und *Zurück zur Partie* kehrt zu der
Stellung zurück, an der die Variante abzweigt. Ein Rechtsklick auf einen Zug einer Variante stuft sie hoch (sie wird
zur Linie, von der sie abzweigt, deren Züge danach zur Variante werden), löscht sie samt den von ihr abzweigenden
oder kopiert sie. *PGN kopieren* schreibt die Varianten, und die einer geöffneten PGN-Datei werden gelesen, beliebig
tief verschachtelt, mit ihren Kommentaren und Symbolen (ein Zug, der sich nicht spielen lässt, beendet nur seine
Variante). Die Grafik, die Analysen und die Urteile gehören zur Partie; auf einer Variante markiert die Grafik,
wo sie die Partie verlässt.

### Die Züge, die Tabelle und die Auswertung
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Züge** schreibt die Partie als fließenden Text: Figurinen (siehe [Darstellung](#darstellung)), die Buchzüge grau
und das Ende des Buchs markiert, die Urteile farbig, der aktuelle Zug umrahmt; steht die Maus über einem Zug, nennt
er, was seine Engine sagte, und sein Urteil. **Tabelle** listet jeden Zug mit der Bewertung seiner Engine (aus der
Sicht von Weiß), Tiefe und Zeit, einem Balken für die Zeit, die markierten Züge farbig.

**Auswertung**, sobald die Partie analysiert ist: für jede Seite ihre Genauigkeit (die von lichess: die jedes Zugs aus
den Gewinnchancen, die er verloren hat, wobei die Züge der scharfen Momente mehr zählen), ihre Ungenauigkeiten, Fehler
und groben Fehler, ihr durchschnittlicher Verlust in Centipawns, der Anteil ihrer Züge, die auch die Analyse spielen
würde, die durchschnittliche Tiefe und Zeit ihrer Engine; ihre Genauigkeit in der Eröffnung, im Mittelspiel und im
Endspiel; mit mehreren Analysen die Genauigkeiten, die jede angibt. Dann die Schlüsselmomente, jeder einen Klick
entfernt: der Wendepunkt (der Zug, der am meisten verloren hat, mit dem besten Zug), die anderen Fehler und groben
Fehler, die Stellen, an denen die beiden Engines zwei Züge in Folge um 1,5 Bauern oder mehr uneinig waren, der Beginn
des Endspiels und der der Tablebases. *Wer es zuerst sah* sagt für den Wendepunkt, bei welchem Zug jede Engine, die
gespielt hat, und jede Analyse der Seite, die gewinnt, zum ersten Mal einen Bauern oder mehr gab.
*PGN mit der Analyse kopieren* schreibt die Partie mit der Bewertung jeder Stellung (`[%eval]`), den Urteilen als NAGs
und, nach einem markierten Zug, der Linie, die die Analyse vorzog; *Als BBCode kopieren* liefert die Auswertung für ein
Forum.

### Tablebases auf der Seite Brett
<!-- en: tablebases-on-the-board-page 721b71dd -->

Mit den Syzygy-Ordnern der Einrichtung (*Abschätzung* > Tablebases) fragt die Seite Brett jede Stellung der Partie
ab: Die Grafik färbt die Zone ein, die die Tabellen kennen, *Dieser Zug* nennt das Ergebnis nach dem Zug und ob der
Zug das vorherige gehalten oder verdorben hat, und die Auswertung markiert, wo die Tabellen übernehmen. Jede Stellung
wird abgefragt, als wäre gerade ein Schlagzug oder ein Bauernzug gespielt worden, was genau ist, außer wo die
50-Züge-Regel nahe ist. Die Tabellen sind die des Programmprozesses: Sie werden nicht abgefragt, während ein Turnier
darin spielt.

### Eine Partie aus einer Stellung spielen
<!-- en: playing-a-game-from-a-position 6b5eb32f -->

**Eine Partie spielen…** spielt eine Partie aus der Stellung auf dem Brett zwischen zwei Engines der Bibliothek oder
zwischen dir und einer davon (siehe *Mensch* weiter unten) (*Weiß* und *Schwarz*, `<->` tauscht sie samt ihren Uhren),
mit einer Uhr für jede (Minuten und ein Inkrement in Sekunden; *Für beide gleich* oder je eine eigene für Zeitvorgabe)
und, falls angehakt, eine zweite Partie mit vertauschten Farben. Die Engines werden mit den Optionswerten der Bibliothek
gestartet, eine WinBoard-Engine über den Adapter. Das Brett folgt der Partie, während sie gespielt wird (geht man in ihr
zurück, folgt es ihr nicht mehr, bis ihr Ende wieder erreicht ist oder *Zurück zum aktuellen Zug* angeklickt wird), mit
den Uhren, der Suche der Seite am Zug und *Stopp*, das die Partie ohne Ergebnis beendet. Eine Partie endet durch Matt,
Patt, ungenügendes Material, die 50-Züge-Regel, eine dreifache Wiederholung, einen illegalen Zug, eine Engine, die
stehen bleibt, oder durch Zeitverlust (mit 0,1 s Spielraum auf der Uhr, damit der Zug ankommt). Jede Partie wird an
ihrem Ende an eine PGN-Datei in `games/` im Profil angehängt (benannt nach dem Datum und den Engines, mit einem Tag
`Termination`), die *Partien öffnen* auf dem Brett auflistet.

Die Partien werden vom eigenen Engine-Client von RookStudio gespielt, unabhängig von jedem Turnier: Sie laufen,
während eines spielt, ohne es zu berühren. Ihre Threads (die der größeren der beiden Engines, da sie abwechselnd
ziehen) zählen zum Kern-Budget; füllen die Partien der laufenden Turniere und die Analyse die Maschine schon aus, sagt
die Karte das und bietet *Trotzdem spielen* statt *Spielen* an. Die Analyse endet, wenn eine Partie beginnt, da sie
mit deren Engines konkurrieren würde.

**Mensch**, als Erstes in den Listen *Weiß* und *Schwarz*, bist du: Du spielst diese Seite selbst; Deine Züge machst du
mit der Maus auf dem Brett, auf dem flachen wie auf dem 3D-Brett (ein Bauer, der die letzte Reihe erreicht, fragt nach
seiner Figur). Das Brett dreht sich bei jeder Partie so, dass deine Seite unten ist (*Drehen* dreht es weiterhin), nimmt
deine Figuren nur an, wenn du am Zug bist (die Karte zeigt *Du bist am Zug*), und ein Klick, während du dir einen
früheren Zug ansiehst, bringt es zur Partie zurück. Deine Uhr läuft wie die einer Engine, Zeitverlust inbegriffen: Gib
dir mehr Zeit als der Engine, indem du *Für beide gleich* abwählst. Während du spielst, wird zurückgehalten, was die
Engine denkt (ihre Bewertung, Tiefe und Variante, also auch der Bewertungsbalken und die Kurve des Graphen); die
beendete Partie hat es, bereit für *Partie analysieren*. *Partie aufgeben* beendet sie jederzeit, gewonnen von der
Engine; mit einer zweiten Partie folgt diese, mit vertauschter Farbe. Deine Partien werden unter deinem Anmeldenamen
gespeichert, und nur die Engine zählt im Kernbudget. Zurücknehmen geht nicht, und die Engine spielt mit voller Stärke,
außer ihre Karte stellt eine geringere ein (`UCI_LimitStrength` und `UCI_Elo` oder `Skill Level`, wo sie diese hat).

In einer Ansicht fürs Studium führt die Karte auch zur Seite Spielen: eine Partie von dir gegen eine Engine in der
Stärke, die du wählst, mit ihren Eröffnungen, einer Vorgabe, Zugrücknahmen und Hinweisen, ihre Hilfsmittel bis zum Ende
ausgeblendet (siehe [Die Seite Spielen](#die-seite-spielen)).

### Das Brett fürs Studium
<!-- en: the-study-board 165eb3bd -->

In einer Ansicht, deren Brett *Fürs Studium* ist (siehe [Ansichten](#ansichten)), ist die Seite Brett der Ort, an dem
Partien gelesen und geschrieben werden. Das Brett steht links; rechts der Streifen der Partie, dann sechs Tabs, Alt+1
bis Alt+6:

- **Notation**: die Titelzeile der Partie und ihre Kopfdaten (siehe [Kopfdaten](#kopfdaten)), dann die Züge, ihre
  Kommentare, Symbole und Varianten, direkt bearbeitet (siehe [Notation](#notation)); die Tabelle der Züge und die
  Auswertung sind in eigenen Tabs (siehe [Die Auswertung einer Partie](#die-auswertung-einer-partie));
- **Analyse**: bis zu vier Engines nebeneinander (siehe [Mehrere Engines nebeneinander](#mehrere-engines-nebeneinander))
  und, wenn du sie gewählt hast, die Cloud und die Endspieldatenbanken von Lichess (siehe
  [Online für die Analyse](#online-für-die-analyse)), mit der Partiegrafik;
- **Baum**: die von der Stellung aus gespielten Züge in deinen Datenbanken, deinen Büchern und auf Lichess (siehe
  [Der Tab Baum](#der-tab-baum));
- **Referenzen**: die stärksten Partien der Stellung (siehe [Referenzpartien](#referenzpartien));
- **Fragen**: der Assistent (siehe [Fragen](#fragen));
- **Endspiele**: was die Endspieldatenbanken zur Stellung sagen, sobald sieben Steine oder weniger übrig sind (siehe
  [Der Tab Endspiele](#der-tab-endspiele)).

Der Streifen über den Tabs sagt, woher die Partie stammt und was aus ihr geworden ist: *Speichern* (Strg+S) mit ihrem
Zustand (*Geändert: 2 Änderungen*, dann *Gespeichert in „Mega“*), der Platz der Partie in der Liste, aus
der sie kam, mit der vorigen und der nächsten Partie (Strg+← und Strg+→), und *Zuletzt* (siehe
[Letzte Partien und die Liste](#letzte-partien-und-die-liste)). Bei leerer Bibliothek bieten die Tabs Analyse, Fragen
und Auswertung an, Gaia mit einem Klick herunterzuladen. *Exportieren…* unter dem Brett macht aus der Partie eine Seite
zum Nachspielen, ein Diagramm, ein animiertes GIF oder ein PDF; <kbd>Strg</kbd>+<kbd>Umschalt</kbd>+<kbd>C</kbd> kopiert
das Diagramm der Stellung (siehe [Exportieren und drucken](#exportieren-und-drucken)).

Ein Zug lässt sich ebenso tippen wie spielen: Sein erster Buchstabe öffnet ein Feld über den Tabs, das die legalen Züge
anbietet, die so beginnen, in den Figurenbuchstaben der Sprache der Oberfläche: K Q R B N auf Englisch und Chinesisch, R
D T F C auf Französisch, K D T L S auf Deutsch, R D T A C auf Spanisch und Italienisch, R D T B C auf brasilianischem
Portugiesisch, Кр Ф Л С К auf Russisch. Englische Buchstaben werden immer auch gelesen: `Ng` bietet dieselben Züge an
wie das deutsche `Sg`. Ein Buchstabe mit zwei Bedeutungen bietet beide an: Auf Französisch, Spanisch, Italienisch und
Portugiesisch gibt R zuerst die Züge des Königs, dann die des Turms, markiert mit *Turm (englisch)*; auf Russisch gibt К
die Züge des Springers und die des Königs, Кр nur die des Königs. Die Felder, auf die die angebotenen Züge führen, sind
auf dem Brett mit Punkten markiert, der Pfeil des ersten ist gezeichnet: ↑ und ↓ wählen einen anderen, Enter spielt den
gewählten, ein Klick spielt einen beliebigen davon, und `0-0` ist ebenfalls die Rochade. Dasselbe Feld nimmt die Züge
der Übungen der Seite Training (die Aufgaben, deine Fehler, Züge erraten, die Endspiele) und der Partien der Seite
Spielen an. Die Oberfläche schreibt die Züge mit denselben Buchstaben (die Notation, wenn *Figurinen in der Notation*
aus ist, die Varianten der Engines, die Auswertung); das PGN behält das englische SAN, wie es jedes Programm liest, und
die Kommentare des Assistenten behalten die Buchstaben ihrer eigenen Sprache. Die Buchstaben des Bretts für die
Engine-Analyse liegen dann unter Alt: Alt+F dreht das Brett um, Alt+A startet oder beendet die Analyse, Alt+X zeigt die
Drohung, Alt+G analysiert die Partie, Alt+N und Alt+Umschalt+N gehen zum nächsten und zum vorigen Zug, den die Analyse
markiert hat.

### Mehrere Engines nebeneinander
<!-- en: several-engines-side-by-side 43f5e1fe -->

Im Tab Analyse des Bretts fürs Studium analysieren bis zu vier Engines der Bibliothek die Stellung gemeinsam, jede in
einer Karte: ihre Farbe und ihr Name, ihre Bewertung (aus der Sicht von Weiß), ihre Tiefe und seldepth, ihre
Geschwindigkeit und ihre Threads, ihre beste Linie und ihre anderen Linien, zu einer zusammengeklappt (ein Klick klappt
sie auf). Steht die Maus über einem Zug einer Linie, zeigt das Brett die Stellung danach; ein Klick spielt die Linie bis
zu diesem Zug. **+ Engine** listet die Engines der Bibliothek auf, die der Analyse angehakt (↑ ↓ wählen eine, Enter fügt
sie hinzu), und *Eine Engine herunterladen…*, die beliebten Engines der Seite Engines; eine hinzugefügte Engine sucht
sofort. Alt+A oder *Analysieren* startet und beendet sie alle, und die Engines bleiben für das nächste Mal erhalten.

Auf dem Brett ist der beste Zug jeder Engine ein Pfeil in ihrer Farbe: dunkelblau, hellorange, blaugrün und grau, von
der ersten Engine bis zur vierten, auch in der Helligkeit verschieden. Engines, die denselben Zug spielen, teilen sich
einen Pfeil, in der Farbe der ersten von ihnen, mit ihrer Anzahl neben seinem Anfang. Die Zeile oben im Tab sagt
dasselbe in Worten: welche Engines sich einig sind, bei welchem Zug, und wie weit ihre Bewertungen auseinanderliegen
(*7…Be7 bei Stockfish und Gaia · 7…Qb6 bei Leela · ihre Bewertungen höchstens 0.11 auseinander*), oder das Matt, das
eine Engine sieht; keine Farbe muss unterschieden werden. Mit einer einzigen Engine sind die Pfeile die ihrer Linien,
wie auf dem Brett für die Engine-Analyse.

**Threads**: Die Analyse hat eine einzige Zahl von Threads, über den Chip oben eingestellt (*24 / 32 Threads*),
gleichmäßig auf ihre Engines verteilt. Das Menü einer Engine (`…`) stellt ihre Linien, ihren Hash und, von Hand, ihre
Threads ein, die der Anteil der anderen dann außen vor lässt. Eine Engine, die auf einer Grafikkarte sucht (*Nutzt eine
GPU* in der Bibliothek angehakt, oder eine Engine wie Lc0, deren Option *Backend* ein GPU-Backend nennt), behält ihre
eigenen Threads, und der Anteil zählt sie nicht. Ein auf diesem Computer laufendes Turnier behält seine Kerne: Die
Analyse nimmt höchstens, was seine Partien übrig lassen, und der Chip sagt, wie viele sie belegen. Diese Einstellungen
gehören der Analyse allein: Weder die Engine der Bibliothek noch ein Turnier sieht sie.

**Die erste Engine** ist die der Analyse: Die Auswertung der Partie (Alt+G), die Drohung (Alt+X oder das Menü der ersten
Engine), die Fragen und die Kommentare des Assistenten nutzen sie. Eine Engine, die an ihrem Namen auf eine andere
gezogen wird, nimmt deren Platz ein, ganz nach oben gezogen wird sie die erste; *Zur ersten machen* in ihrem Menü tut
dasselbe. Hat die Partie eine Analyse der neuen ersten Engine, beurteilt diese Analyse die Züge und zeichnet die Kurve
der Partiegrafik. Die Bewertungen jeder Engine für die Stellungen der Partie, die sie durchsucht hat, erscheinen auf der
Grafik als dünne Linie in ihrer Farbe (*Live-Analyse*), die der ersten Engine als Kurve, solange die Partie keine
Analyse hat.

Während eine deiner Partien auf Lichess gespielt wird, hält jede Engine sofort an, und das Brett fürs Studium zeigt
keinen ihrer Pfeile (siehe [Fairplay](#fairplay)). *Engines der Analyse entladen*, im Menü einer Engine oder in der
Befehlspalette, beendet ihre Prozesse. Jede Geste des Tabs steht in der Befehlspalette (Strg+K): eine Engine
hinzugefügt, zur ersten gemacht oder entfernt, ihre Einstellungen, die Threads der Analyse.

### Praktische Züge
<!-- en: practical-moves 26da3fb5 -->

Unter den Engines sucht *Gegen einen Menschen mit 1500, Schnellschach* (Niveau und Bedenkzeit bleiben von einem Mal zum
nächsten erhalten) den praktischen Zug: Unter den Zügen, die die erste Engine innerhalb von 0,3 Bauern von ihrem besten
sieht (oder, sobald die Tablebases die Stellung kennen, denen, die ihr Urteil halten), derjenige, der gegen einen
Spieler dieses Niveaus am besten abschneidet, in den Partien dieses Niveaus. Mit einem verknüpften Lichess-Konto sind
es die Partien des Explorers von Lichess in der Wertungsgruppe und der Bedenkzeit des Niveaus (*Lichess 1400–1600,
Schnellschach*); ohne Konto die Partien deiner Datenbanken, deren Spieler im Schnitt eine Wertung im Band des Niveaus
haben, gleich welcher Bedenkzeit. Das Ergebnis eines Zugs zählt ab 200 Partien auf dem Niveau; außerhalb bekannter
Partien ist der praktische Zug derjenige, der dem Gegner laut Engine die wenigsten guten Antworten lässt: Eine eigene
Engine sucht auf einem Thread die Antworten jedes Kandidaten, und die Tabelle sagt, wie viele halten (*2 von 31*). Nie
ein verlierender Zug.

Die Tabelle gibt jeden Kandidaten, seine Bewertung durch die Engine (die von Weiß), sein Ergebnis gegen dieses Niveau
und seine Partien dort; der beste Zug der Engine ist mit *bester* markiert, der praktische Zug mit *praktisch*; auf dem
Brett ist der praktische Zug der blaue Pfeil, der beste der Engine der graue. Ein Satz fasst es zusammen, an die Zahlen
gebunden: *Der praktische Zug: 4.Sg5 — 0.13 vom besten der Engine entfernt, und 58 % erwartet auf diesem Niveau.* Wenn
die Partien es zeigen, geht er mit der meistgespielten Antwort des Niveaus entlang der Variante weiter und mit dem, was
sie bestraft, der Antwort der Engine darauf: *Nach 4…d5 5.exd5: Die meistgespielte Antwort bei 1500 ist 5…Sxd5? (46 %
für Schwarz), die von 6.Sxf7 bestraft wird.* Die praktischen Züge brauchen die laufende Analyse (Alt+A). Was den
Computer verlässt: mit einem verknüpften Konto die Stellung und das Niveau, an den Explorer von Lichess; ohne Konto
nichts. Gesperrt mit dem Tab Analyse, solange eine deiner Partien auf Lichess gespielt wird.

### Notation
<!-- en: notation 5147406e -->

Die Notation wird direkt bearbeitet, ohne Dialog:

- **Kommentare**: Enter schreibt einen Kommentar nach dem gezeigten Zug, Umschalt+Enter einen davor; Enter übernimmt
  ihn, Esc verwirft ihn. Ein Klick auf einen Kommentar geht zu seinem Zug, ein Doppelklick korrigiert ihn.
- **Symbole**: Ein getipptes `!` oder `?` setzt das Symbol des Zugs, und ein zweites Zeichen direkt danach bildet das
  Paar (`!!`, `??`, `!?`, `?!`). Die Knöpfe unter der Notation setzen die anderen: einziger Zug, Neuerung und die
  Bewertung der Stellung (=, ∞ und der Vorteil von Weiß oder Schwarz, leicht, klar oder entscheidend). Ein erneut
  gesetztes Symbol wird entfernt.
- **Varianten**: Ein anderer Zug als der der Partie, auf dem Brett gespielt oder getippt, beginnt eine Variante. Wird
  beim Ziehen Umschalt gehalten, ersetzt er die Folgezüge, und Strg fragt nach: eine Variante, eine neue
  Hauptvariante oder die Folgezüge ersetzt (Esc spielt nichts). *Fragen, was mit einem abweichenden Zug geschehen
  soll* (*Einstellungen* > *Darstellung*) fragt jedes Mal.
- **Entf** löscht die Züge ab dem gezeigten; **Alt+↑** verschiebt die Variante nach oben, zuletzt zur Hauptvariante.
- **Strg+Z** macht jede Änderung rückgängig (einen Zug, einen Kommentar, ein Symbol, eine Markierung, ein Feld der
  Kopfdaten) und **Strg+Y** stellt sie wieder her; eine Zeile unter der Notation sagt, was getan wurde und wie es sich
  rückgängig machen lässt.

*Figurinen in der Notation* (*Einstellungen* > *Darstellung*) zeigt die Figuren als Figurinen. Die Kommentare des
Assistenten erscheinen getrennt von deinen, in Blaugrau und kursiv, nach einer Sprechblase, oder in einem Kasten unter
seinem Namen nach einem Kommentar, der schon da war (siehe [Kommentare des Assistenten](#kommentare-des-assistenten)).
Wenn deine Theoriestudien die gezeigte Stellung kommentieren, führt *Theorie · 3* über den Zügen zu ihren Notizen im
Tab Baum (siehe [Theoriestudien](#theoriestudien)).

**Die Partie eines Turniers**, in einem der beiden Stile ihres PGN (`{+0.35/18 1.234s, n=…}` von fastchess oder
`[%eval]`, `[%clk]` und `[%emt]` im ChessBase-Stil), ob aus ihrer Datei geöffnet, aus einer Datenbank, die sie
importiert hat, oder aus einem als Datenbank gelesenen Turnier, zeigt die Kommentare ihrer Engines verdichtet: neben
jedem Zug, klein und grau, die Bewertung, die seine Engine angab (für Weiß, wie der Balken und die Kurve), und ihre
Tiefe, *+1.44/22*, das Ende des Buchs markiert. Steht die Maus über einem Zug, nennt er den Rest: seine Engine und
Seite, die Bewertung auch aus ihrer Sicht, die Tiefe und die selektive Tiefe, die Zeit und wann sie sich für den Zug
entschied, die Knoten und die Geschwindigkeit, die Tablebase-Treffer, ihre Chancen auf Sieg, Remis und Niederlage und
die Linie, die sie danach erwartete, in Zügen geschrieben. Die rechte Maustaste auf dem Zug zeigt diese Linie auf dem
Brett, macht sie zu einer Variante ab der Stelle, an der die Partie sie verließ (Strg+Z nimmt sie zurück), oder kopiert
den Kommentar der Engine unverändert. Die Zeile unter den Zügen wählt, was neben ihnen erscheint: die Bewertung und die
Tiefe, auch die Zeit, oder nichts (ebenso in Strg+K). Das PGN ändert sich nicht: Strg+S und *PGN kopieren* schreiben den
Kommentar jeder Engine so zurück, wie er gelesen wurde, jedes Feld erhalten, solange die Daten des Zugs dieselben sind.

### Pfeile und Felder
<!-- en: arrows-and-squares a46e6347 -->

Die rechte Maustaste zeichnet auf dem Brett fürs Studium: Ein Klick färbt ein Feld, ein Ziehen von einem Feld zu einem
anderen zeichnet einen Pfeil, und dasselbe noch einmal entfernt ihn. Standardmäßig grün, rot mit Umschalt, blau mit
Alt, gelb mit beiden. Ein Linksklick auf ein leeres Feld löscht die Markierungen des Zugs. Die Markierungen gehören zum
gezeigten Zug und werden mit der Partie gespeichert (als `[%cal]` und `[%csl]` im PGN). Auf dem 3D-Brett zeichnet die
rechte Maustaste auf dem Brett und dreht daneben die Kamera; die mittlere Maustaste dreht sie ebenfalls.

### Kopfdaten
<!-- en: headers 4e8fbaae -->

Die Titelzeile oben in der Notation nennt die Spieler, ihre Elo, die Veranstaltung, die Runde, das Datum und das
Ergebnis; ein Klick darauf oder Alt+H klappt die Kopfdaten an Ort und Stelle auf. Tab geht zum nächsten Feld und
ergänzt einen Namen aus denen der Datenbank; ein Datum, das keines ist, wird gemeldet (`YYYY.MM.DD`, mit `??` für
Unbekanntes); das Ergebnis wird mit einem Klick gewählt; der ECO-Code wird aus den Zügen vorgeschlagen (die
Eröffnungsnamen von Lichess) und beim Speichern von selbst eingetragen, wenn er fehlt. Die übrigen Kopfdaten erscheinen,
wie die Partie sie hat, und ein weiteres Feld wird über seinen Namen hinzugefügt. Jedes geänderte Feld ist eine
Änderung (Strg+Z); Enter klappt die Kopfdaten zu, Esc gibt dem Feld seinen Wert zurück.

### Eine Partie speichern
<!-- en: saving-a-game ca08879e -->

Strg+S speichert die Partie dort, wo sie hingehört: Eine aus einer Datenbank geöffnete Partie geht unter ihrer Nummer
dorthin zurück; eine Partie einer PGN-Datei geht an ihrer Stelle in die Datei zurück (die Datei wird sofort neu
geschrieben, ihre anderen Partien unverändert). Jede andere Partie öffnet unter *Speichern* ein Menü, in dem Enter die
erste Zeile nimmt: eine deiner Datenbanken (die vom letzten Mal zuerst), eine neue PGN-Datei oder das Ende einer
PGN-Datei. Strg+Umschalt+S speichert eine Kopie jeder Partie anderswo. Eine Partie ohne Spieler öffnet zuerst ihre
Kopfdaten.

Eine Datenbank, in die ein anderes Programm schreibt, ist währenddessen schreibgeschützt: Die Partie bleibt hier
geändert, bis sie gespeichert werden kann.

### Stellungen und Partien zu Lichess
<!-- en: positions-and-games-to-lichess 1fc6bb1d -->

**lichess.org…** unter dem Brett schickt die Stellung oder die Partie an Lichess; jede Aktion sagt, was sie sendet. *Die
Stellung auf lichess.org öffnen* öffnet das Analysebrett von Lichess mit ihr im Browser: Die Stellung steht in der
Adresse, sonst nichts. *Die Partie teilen* importiert sie ohne Konto auf Lichess und kopiert ihre Adresse: Wer die
Adresse hat, sieht die Partie, auch die Namen ihrer Spieler. *Die Stellung ausspielen lassen*, mit einer Bedenkzeit (3+2
bis 30+20 oder Tage pro Zug), erstellt eine offene Herausforderung aus der Stellung: ein Link für jede Seite, an ihren
Spieler zu senden; eine solche Partie ist nicht gewertet. *Lesezeichen für die Partie auf Lichess setzen*, für eine
Partie, die von dort stammt, legt sie in deine Lesezeichen: Es sendet die ID der Partie mit deinem Token und bittet
Lichess einmal um die Berechtigung, deine Einstellungen zu ändern. *An eine Lichess-Studie…* sendet die Partie in eine
deiner Studien oder in eine neue (siehe [Deine Lichess-Studien](#deine-lichess-studien)).

Während du eine Partie auf Lichess spielst, berührt keine dieser Aktionen ihre Variante: Eine Stellung auf dem Weg zu
ihrer Stellung oder die Partie, die durch sie geht, verlässt den Computer nicht (siehe [Fairplay](#fairplay)). Der Rest
bleibt möglich.

### Letzte Partien und die Liste
<!-- en: recent-games-and-the-list ba602f73 -->

Strg+Tab zeigt die letzten Partien, die auf dem Brett zuerst (höchstens zwölf): Strg+Tab noch einmal geht in der Liste
nach unten, Strg+Umschalt+Tab nach oben, und Loslassen von Strg öffnet die gewählte Partie, mit ihrer Liste, ihrer
Datei, ihren Änderungen und ihrem Rückgängig-Verlauf. Eine Partie mit nicht gespeicherten Änderungen bleibt, wie sie
ist, bis du zu ihr zurückkommst. *Zuletzt* im Streifen listet sie ebenfalls auf: Ein Klick öffnet eine, und die rechte
Maustaste auf einer geänderten verwirft ihre Änderungen (Strg+Y stellt sie wieder her).

Eine aus der Liste einer Datenbank geöffnete Partie behält ihren Platz darin: Strg+← und Strg+→ öffnen die vorige und
die nächste Partie der Liste oder ihres Filters, beim selben Zug, wenn die Partie durch die gezeigte Stellung geht.

### Der Tab Baum
<!-- en: the-tree-tab 184526b8 -->

Der Tab Baum zeigt die von der gezeigten Stellung aus gespielten Züge: ihre Partien, die Anteile der Siege von Weiß,
der Remis und der Siege von Schwarz, die Punktzahl, die durchschnittliche Elo, die Performance der Seite am Zug (nach
der Tabelle der FIDE) und das letzte Jahr, in dem der Zug gespielt wurde. Seine Quellen sind Chips über den Zügen:

- **Meine Datenbanken**: deine Datenbanken zusammengerechnet, Zug für Zug (die rechte Maustaste wählt, welche; der
  Anteil jeder erscheint unter dem Mauszeiger über den Partien);
- jede Datenbank, deren Eröffnungsbaum erstellt ist, und jedes Polyglot-Buch der Bibliothek;
- **Meister (Lichess)**, **Lichess** (die rechte Maustaste wählt seine Bedenkzeiten und Wertungen) und der Spieler,
  dessen Partien du importiert hast: der Eröffnungsexplorer von Lichess, der ein verknüpftes Konto braucht (siehe
  [Dein Konto verknüpfen](#dein-konto-verknüpfen)).

Der Zug unter der Maus oder der mit ↑ ↓ gewählte erscheint als blauer Pfeil auf dem Brett; ein Klick oder Enter spielt
ihn, und `*` oder sein Stern markiert ihn im aktiven Repertoire (siehe [Die Seite Eröffnungen](#die-seite-eröffnungen)).
Verlässt die Variante der Partie die Datenbank, sagt der Tab, bei welchem Zug, und ob dieser Zug eine **Neuerung** ist,
wie eine Stellungssuche bestätigt (*Letzte bekannte Stellung zeigen*). Der Baum einer Datenbank wird nach jedem Import
erstellt, und erneut im Hintergrund, nachdem eine Partie in sie gespeichert wurde, mit seinem Fortschritt und
*Abbrechen*. Die Notizen deiner Theoriestudien kommen mit den Zügen, egal aus welcher Quelle: eine Spalte *Theorie*
und *Was deine Studien sagen* unter den Zügen (siehe [Theoriestudien](#theoriestudien)). *Bericht zu dieser
Stellung*, rechts neben den Partien, öffnet ihren Eröffnungsbericht (siehe [Der
Eröffnungsbericht](#der-eröffnungsbericht)).

### Referenzpartien
<!-- en: reference-games c45f1d18 -->

Der Tab Referenzen sucht im Hintergrund die stärksten Partien der Stellung in deinen Datenbanken: die stärksten zuerst
(nach der mittleren Wertung der Spieler), dann die neuesten; ein Klick öffnet eine. Früh in der Eröffnung erreichen zu
viele Partien die Stellung, um nach den stärksten zu suchen: Der Tab Baum sagt mehr. *Alle Partien der Stellung in
„…“* listet sie in der Datenbank auf.

### Der Tab Endspiele
<!-- en: the-endgames-tab 330bcfca -->

Der sechste Tab des Bretts fürs Studium, Alt+6, sagt, was die Endspieldatenbanken zur gezeigten Stellung sagen, sobald
sieben Steine oder weniger übrig sind, oder acht mit je einem Bauern jeder Seite auf einer Linie (die op1-Tablebases von
Lichess); darüber hinaus ist er abgeblendet und sagt es, wenn er gewählt wird. Oben das Urteil (*Weiß gewinnt*,
*Remis*, *Gewonnen für Weiß, aber remis durch die 50-Züge-Regel*), woher es stammt und wie weit das Ende entfernt ist:
die Züge bis zum Matt bis sechs Steine (DTM, wie andere Programme es nennen), bei sieben die Züge bis zu einem Schlag-
oder Bauernzug (DTZ), bei acht die Züge bis zum nächsten Schlagen, zur nächsten Umwandlung oder zum Matt (DTC). Dann
die Züge, gruppiert nach dem Ergebnis, das sie der Seite lassen, die sie spielt, die besten zuerst: *Den Gewinn
halten*, *Das Remis halten*, *Den Gewinn verschenken: Remis*, *Verlieren*… Ein Zug, der das Ergebnis verdirbt, wird
mit Begründung genannt: die Antwort, die ihn widerlegt, und was die Tablebases dann sagen (*→ 52…Kxh7 · Schwarz
gewinnt: Matt in 19 Zügen*). Ein Zug unter der Maus zeigt seine Hauptvariante auf dem Brett; ein Klick spielt ihn, als
Variante, wenn die Partie anders weiterging, wie auf dem Brett. Geht die Partie von der Stellung aus weiter, sagt der
Assistent, was ihr Zug bewirkt hat, von den Tablebases unterrichtet (auf Englisch oder Französisch: *52.Rxh7 loses:
after 52…Kxh7, the tables give Black mate in 19 moves. 52.g5 kept the theoretical draw (7-piece tables).*), nur aus
ihren Urteilen geschrieben: Er sagt es nur, wenn sie es sagen.

Deine Tablebases auf der Festplatte antworten zuerst (Syzygy, die, die die Abschätzung der Turniere und die Endspiele
der Seite Training verwenden: ihr Urteil und die Züge bis zu einem Schlag- oder Bauernzug, keine Entfernung zum Matt);
die von Lichess beantworten die Stellungen, die ihnen fehlen, sobald erlaubt. Beim ersten Mal fragt der Tab: für diese
Stellung, immer oder nicht jetzt (*Immer* lässt sich auf der Seite Lichess unter *Online für die Analyse* ändern). Nur
die FEN der Stellung geht an tablebase.lichess.ovh, ohne Konto, eine Anfrage nach der anderen: die Stellung, die du
ansiehst, und die Hauptvarianten ihrer Züge, zuerst die, die das Ergebnis verderben, die anderen, wenn sie unter der
Maus sind. Jede Antwort wird in deinem Profil gespeichert und nie erneut abgefragt. Der Tab Analyse behält das Urteil
und seine Entfernung, mit einem Link zu diesem Tab. Der Tab ist gesperrt, solange eine deiner Partien auf Lichess
gespielt wird, wie der Tab Analyse, und während einer Partie gegen eine Engine bis zu ihrem Ende ausgeblendet.

### Mehr vom Explorer
<!-- en: the-explorer-more 1471aca0 -->

Mit den Quellen von Lichess im Tab Baum erscheinen **die Partien, die der Explorer nennt**, unter den Zügen: die
Meisterpartien, die die Stellung erreichen (*Meisterpartien*), oder die starken auf Lichess gespielten; ein Klick fragt
Lichess nach der Partie und öffnet sie auf dem Brett, bei der Stellung, die der Baum zeigte.

Mit der Quelle *Lichess* (alle ihre Spieler) fragt *Jeder Zug Monat für Monat* Lichess für jeden aufgelisteten Zug, wie
viele Partien ihn Monat für Monat gespielt haben: eine Linie seiner letzten zwei Jahre in der Spalte *Pro Monat*, grün,
wenn er zuletzt häufiger gespielt wird, rot, wenn seltener; unter der Maus seine Partien der letzten sechs Monate
gegenüber den sechs davor. Die Zusammenfassung sagt dasselbe über die Stellung. Das kostet eine Anfrage pro Zug, deshalb
bleibt es aus, bis es verlangt wird; der Explorer der Meister hat keine Monate.

### Der Eröffnungsbericht
<!-- en: the-opening-report 4d6f6a24 -->

Der **Eröffnungsbericht** einer Stellung stellt zusammen, was deine Datenbanken über sie wissen. Er öffnet sich über
*Bericht zu dieser Stellung* im Tab Baum, über eine Variante der Seite Eröffnungen (die rechte Maustaste auf einer
Lücke: *Eröffnungsbericht*) und über Strg+K, *Eröffnungsbericht*; er erscheint anstelle der Seite Brett, und Esc geht
zurück zur Stellung. Er wird bei jedem Öffnen neu erstellt, im Hintergrund mit niedriger Priorität, Abschnitt für
Abschnitt mit seinem Fortschritt und *Abbrechen*: ein Bruchteil einer Sekunde für die meisten Stellungen, etwa eine
Sekunde für eine mit Millionen von Partien.

Oben: die Stellung, ihre Eröffnung (die benannten Eröffnungen von Lichess) und die Züge, die dorthin führen; dann ihre
Quellen, die des Tabs Baum: deine Datenbanken, deren Eröffnungsbaum erstellt ist (die, die *Meine Datenbanken*
zusammenzählt), mit ihren Partien; **Meister (Lichess)** und **Lichess** mit den Bedenkzeiten und Wertungen des Tabs
Baum, angehakt, wenn der Tab Baum sie zeigt, oder per Klick (sie fragen den Explorer von Lichess nach der Stellung, wie
der Tab Baum, und brauchen ein verknüpftes Konto); und woher die Bewertungen stammen. *Ins Repertoire* fügt die erste
Hauptvariante dem aktiven Repertoire hinzu (Strg+Z nimmt sie wieder heraus); *Drucken…* macht aus dem Bericht ein PDF,
seine Zusammenfassung als die des Assistenten markiert, und *HTML-Seite* eine eigene Seite, deren Hauptvarianten sich
auf einem Brett nachspielen lassen (siehe [Exportieren und drucken](#exportieren-und-drucken)). Fünf Abschnitte folgen
(Strg+Bild↓ und Strg+Bild↑ gehen von einem zum nächsten):

- **Linien**: die von der Stellung aus gespielten Züge mit den Zahlen des Tabs Baum (Partien, Gewinne von Weiß, Remis
  und Gewinne von Schwarz, Punktzahl, Elo-Schnitt, Performance, letztes Jahr) und einer Bewertung: der von Lichess,
  offline aufbewahrt (siehe [Die Bewertungen von Lichess, offline](#die-bewertungen-von-lichess-offline)), sonst der
  der Engine der Analyse (eine Million Knoten auf einem Thread, für die ersten fünf Züge ohne eine); Quelle und Tiefe
  unter dem Mauszeiger. Ein Klick, oder ↑ ↓ und Enter, spielt den Zug auf der Seite Brett. Dann die
  **Hauptvarianten**: die meistgespielten Fortsetzungen, solange sie ein Fünftel der Partien ihres ersten Zugs
  behalten, mit den Partien, die ihr Ende erreichen, und dem Ergebnis von Weiß; ein Klick zeigt eine auf der Seite
  Brett;
- **Pläne und Strukturen**: die Bauernstrukturen beim 20. Zug (zehn Züge weiter bei einer späteren Stellung) in den
  Partien der Stellung, wie die Fakten des Assistenten sie benennen, *schon auf dem Brett*, wenn die Stellung bereits
  eine hat; und was die Spieler in den nächsten fünfzehn Zügen tun: die Bauernvorstöße (einer, oder zwei
  hintereinander: *b4 dann b5*) und die Routen der Figuren (so wie die Suche Manöver einer Figur folgt), die in einem
  Zehntel der Partien oder mehr gespielt werden, drei für jede Seite, mit ihrem Anteil und dem Ergebnis von Weiß. Ein
  Plan, den die Fakten des Assistenten kennen, wird benannt (*Der Minoritätsangriff: b4 dann b5*). Das sind
  Häufigkeiten, keine Ratschläge; sie brauchen 50 Partien, und eine Stellung mit mehr als 20.000 Partien wird in einer
  gleichmäßig verteilten Stichprobe davon gezählt. Ein Klick zeigt diese Partien in der Liste;
- **Schlüsselpartien**: die stärkste (die höchste durchschnittliche Wertung der Spieler), die jüngste mit 2500 und mehr
  im Schnitt und die erste Partie einer seither wiederholten Neuerung, jede mit Begründung und dem Plan, den sie spielt;
  *Öffnen* zeigt sie auf der Seite Brett bei der Stellung (*Zurück zum Bericht* kehrt zurück);
- **Neuerungen**: entlang der Hauptvarianten die Züge, die in den letzten fünf Jahren deiner Datenbanken zum ersten Mal
  gespielt und seither wiederholt wurden, mit ihrem ersten Jahr, wie oft sie wiederholt wurden, und ihrem Ergebnis;
  und deine eigene Partie, die dort deine Datenbanken verlassen hat (deine Namen sind die aus *Einstellungen › Deine
  Namen*). Das erste Jahr eines Zugs stammt aus den Bäumen, die jede Partie enthalten, die seine Stellung in ihren
  ersten 60 Halbzügen erreicht; deine Partie wird durch eine Suche der Stellung bestätigt, so wie der Tab Baum eine
  Neuerung bestätigt;
- **Zusammenfassung**: die Zusammenfassung des Berichts durch den Assistenten, von seinem Modell nur aus den Zahlen
  und Fakten des Berichts geschrieben (siehe [Die Zusammenfassung des Berichts](#die-zusammenfassung-des-berichts)).

Eine Stellung, die weniger als 50 Partien erreichen, gibt ihre Züge und Partien an, ohne Pläne und Strukturen (*Die
Lichess-Spieler als Quelle hinzufügen*). Ohne eine Datenbank, deren Baum ihre Partien abdeckt, entsteht der Bericht aus
den Meistern von Lichess, mit *Den Baum von „…“ neu erstellen* und *Eine Datenbank importieren*. Offline antworten
deine Datenbanken und die offline aufbewahrten Bewertungen, und die Lichess-Quellen zeigen die letzten Zahlen, die der
Bericht für die Stellung aufbewahrt hat, mit Datum. Ohne Engine und ohne offline aufbewahrte Bewertungen von Lichess
bleiben die Bewertungen leer, und die Zusammenfassung sagt nichts über sie (*Gaia herunterladen…*). Solange eine deiner
Partien live auf Lichess gespielt wird, ist der Bericht gesperrt und kommt an ihrem Ende von selbst zurück; bei der
Stellung einer Fernpartie bleiben die Varianten und Zahlen deiner Datenbanken (Lichess erlaubt ein Buch und eine
Datenbank), die Bewertungen, der Explorer von Lichess und die Zusammenfassung warten. Nichts verlässt den Computer außer
dem, was der Tab Baum sendet: die angehakten Lichess-Quellen.

### Die Zusammenfassung des Berichts
<!-- en: the-reports-summary a859f9a6 -->

Die Zusammenfassung wird in Abschnitten geschrieben: die Bauernstruktur, die Pläne jeder Seite, die gespielten Züge,
die Hauptvariante, die Bewertungen. Das Modell des Assistenten schreibt jeden nur aus den Zahlen dieses Abschnitts,
gehalten von einer Grammatik, die es nur die Züge der Stellung, die Varianten des Berichts, ihre Felder und Zahlen
nennen lässt; dann liest der Prüfer jeden Satz: Eine Zahl muss eine sein, die der Bericht für das Genannte angibt, ein
Zug einer der Stellung oder ihrer Varianten, ein Plan oder eine Struktur eine, die der Bericht hat, und von der
genannten Seite, und er gibt keinen Rat. Ein abgelehnter Abschnitt wird einmal neu geschrieben; zweimal abgelehnt,
bleibt der Abschnitt der Vorlagen stehen, und der Mauszeiger darauf sagt, warum. *Worauf sie beruht* listet die
verwendeten Zahlen, Fakten und Bewertungen auf; eine Karte sagt, was der Prüfer abgelehnt hat. *Die Fassung der
Vorlagen* zeigt die Zusammenfassung der Vorlagen, *Neu schreiben* lässt das Modell sie anders schreiben, *Kopieren*
kopiert sie. Ohne das Modell (nicht heruntergeladen, eine andere GGUF-Datei oder solange ein Turnier auf diesem
Computer den Prozessor belegt) schreiben die Vorlagen sie aus denselben Zahlen, auf Englisch oder Französisch. Auf dem
Prozessor braucht eine Zusammenfassung ein paar Sekunden pro Abschnitt.

### Die Auswertung einer Partie
<!-- en: the-review-of-a-game 7c5c4437 -->

Auf dem Brett fürs Studium zeigt die Partie eines Turniers oben im Tab *Auswertung*, was ihre beiden Engines dachten,
sobald sie geöffnet ist und ohne jede Analyse: die Chancen von Weiß im Verlauf der Partie laut jeder Engine bei ihren
eigenen Zügen (aus dem Anteil an Siegen, Remis und Niederlagen, den sie angab, für Schwarz umgedreht, sonst aus ihrer
Bewertung, umgerechnet wie bei der Auswertung), das Buch schraffiert, die Abschnitte, in denen die beiden Engines
verschiedene Partien sahen, eingefärbt; darunter die Zeit jedes Zugs, die von Weiß über der Linie, die von Schwarz
darunter; die Zeit, die mittlere Tiefe und die Geschwindigkeit jeder Engine. Dann ihre Momente, jeder einen Klick
entfernt: wie jede Engine die Partie nach dem Buch sah, wo sie uneins waren, wo die Tablebases in eine Suche einflossen,
wo ein Matt gesehen wurde und wie die Partie endete. Sobald die Partie analysiert ist, wird die Kurve der Auswertung
dicker über ihre gezeichnet.

Auf dem Brett fürs Studium liest der Tab *Auswertung* neben der Notation die Analyse der Partie (Alt+G), wie es ein
Trainer täte:

- oben die Chancen von Weiß im Verlauf der Partie, mit den Schlüsselmomenten als Punkten (dunkel für einen guten Zug,
  orange für einen Fehler); ein Klick springt zum Zug;
- die Theorie der Partie in einer deiner Datenbanken (der des Tabs Baum, sonst der ersten): Ihre Buchzüge werden nicht
  beurteilt, und ihre Neuerung wird genannt; das Buch endet vor dem ersten Fehler, den die Engine sieht. Die Züge, die
  das Eröffnungsbuch eines Turniers gespielt hat (`{book}` im PGN), werden ebenfalls nicht beurteilt;
- **Schlüsselmomente**: die Züge der Punkte, jeder mit seinem Urteil (ein Fehler mit den Chancen, die er verloren hat,
  und dem besseren Zug, ein einziger Zug mit dem nächstbesten der Engine: *Einziger Zug. Gaia: +0.62, der nächstbeste
  +0.31.*), und wo das Endspiel beginnt;
- **Die Schlüsselmomente in Worten**, **Die Geschichte der Partie** und **Woran zu arbeiten ist**, für jede Seite, mit
  den Zügen, die es zeigen; ein Klick springt zum Zug.

Dieselbe Partie bekommt immer dieselbe Auswertung: dieselben Bewertungen, Symbole, Schlüsselmomente und Kommentare, egal
was der Computer gerade tut. Die Analyse der Partie auf dem Brett fürs Studium zählt Knoten statt Zeit: Jede Stellung
wird beim ersten Durchgang mit 20.000 Knoten durchsucht, bei jedem weiteren Durchgang mit viermal so vielen, bis zu der
Wahl neben dem Knopf (320.000, 1.280.000, 5.120.000 oder 20.480.000 Knoten pro Stellung oder *bis zum Stoppen*), mit
einem einzigen Thread und leerem Hash, sodass das Ergebnis einer Stellung nur von der Engine und der Stellung abhängt.
Mehrere Stellungen werden gleichzeitig durchsucht, jede von einem eigenen Engine-Prozess: so viele, wie die Analyse
Threads hat, oder die Hälfte der Prozessorkerne, höchstens vier, wenn das mehr ist; weniger, während auf diesem Computer
ein Turnier spielt, das seine Kerne behält. Wo der gespielte Zug die Wahl der Engine war und die Stellung nicht
einseitig ist, liefert eine zweite Suche der Stellung mit zwei Linien den nächstbesten Zug: Ein einziger Zug wird mit
ihm genannt.

Die Stellungen des Endspiels mit sieben Steinen oder weniger können die Lichess-Tablebases exakt beurteilen, einmal für
diese Partie oder immer abgefragt. *Schlüsselmomente kommentieren* schreibt im Hintergrund zu jedem einen Kommentar
des Assistenten, neben den Kommentar, den ein Zug vielleicht schon hat (die Analyse von Lichess, deinen, den eines
Kommentators), der nie ersetzt wird; *Partie annotieren* trägt die Bewertungen, die Symbole und die besseren Varianten
in die Notation ein. Strg+Z nimmt beides zurück.

**Die Geschichte der Partie** schreibt das Modell des Assistenten, wenn es da ist, im Hintergrund, aus den Fakten der
Auswertung: ihren Urteilen und Bewertungen, den Schlüsselmomenten, der Eröffnung, dem Endspiel und wie die Partie
endete, mit dem, was die Lichess-Tablebases sagten. Jeder Abschnitt wird vom Prüfer der Kommentare an diese Fakten
gebunden: Jeder Zug, jede Bewertung und jeder Name, die er zitiert, muss aus der Auswertung stammen. Ein abgelehnter
Abschnitt wird neu geschrieben; zweimal abgelehnt, weicht er dem der Vorlage. Eine Karte in der Farbe des Assistenten
sagt, dass das Modell die Geschichte geschrieben hat und dass sie geprüft wurde, und was der Prüfer abgelehnt hat. *Die
Fassung der Vorlagen* zeigt an ihrer Stelle die Geschichte der Vorlagen, *Neu schreiben* lässt das Modell sie anders
erzählen (auch in Strg+K), *Kopieren* kopiert die gezeigte Geschichte. Ohne das Modell (nicht heruntergeladen, oder eine
andere GGUF-Datei, die die Kommentare von RookStudio nicht gelernt hat) oder während ein Turnier auf diesem Computer den
Prozessor belegt, ist die Geschichte die der Vorlagen, wie sie selbst sagt. Nichts verlässt den Computer.

### Jeder Zug erklärt
<!-- en: every-move-explained 5f600fec -->

*Jeden Zug erklären* im Tab *Auswertung* (oder Strg+K) gibt jedem Zug der Partie eine Zeile, im Tab *Erklärungen*
neben den Zügen; ohne Auswertung wird die Partie zuerst analysiert (Alt+G), und ihre Züge werden erklärt, sobald die
Auswertung fertig ist. Die Zeilen kommen im Hintergrund, jede, sobald sie geschrieben ist, mit der Zahl der fertigen
und *Abbrechen*; die geschriebenen bleiben, und *Den Rest erklären* vervollständigt sie später.

- **Theorie** wird zusammengefasst: eine Zeile für jeden Eröffnungsnamen, den ihre Züge erreichen, höchstens drei, mit
  den Partien deiner Datenbanken, die ihre letzte Stellung erreicht haben (*Theorie, 61.204 Partien deiner
  Datenbanken*).
- **Ein Zug, der für mehr zu einfach ist**, wird mit einem Wort gesagt (auf Englisch oder Französisch, in der Sprache
  des Assistenten): *Forced recapture.*, *The only legal move.*
- **Die anderen** bekommen einen Satz des Modells des Assistenten, aus den Fakten der Stellung geschrieben, wobei seine
  Grammatik es nur legale Züge nennen lässt, und jeder Satz wird vom Prüfer gelesen; ein Satz, den der Prüfer ablehnt,
  wird durch den der Vorlage ersetzt, und *Vorlage: Der Prüfer hat den Satz des Modells abgelehnt* unter der Maus sagt,
  warum. Ohne das Modell schreiben die Vorlagen sie; wenn sie nichts zu sagen haben, sprechen die eigenen Worte der
  Auswertung (*The engine preferred 14…Bf5.*).

Jede Zeile sagt, woher sie stammt: Theorie, das Modell, eine Vorlage, ein Wort, die Auswertung. *Alle Züge*, *Die
wichtigen Züge* (die markierten Züge, die einzigen Züge, die Wendepunkte, die Neuerung) und *Die Schlüsselmomente*
wählen, welche erscheinen; ein Klick geht zum Zug. Die Zusammenfassung zählt sie und gibt die Zeit an, die ein Zug
braucht, die gesammelten Fakten und den geprüften Satz des Modells: etwa eine Viertelsekunde für den Satz auf einer
GPU, rund 3 Sekunden pro Zug auf einem Prozessor, der mit anderer Arbeit geteilt wird (32 erklärte Züge in anderthalb
Minuten). Sie werden mit der Auswertung gespeichert, neben den Zügen, nie in der Partie: *In die Partie schreiben* macht
sie zu Kommentaren des Assistenten, im PGN als seine eigenen markiert, neben dem Kommentar, den ein Zug schon hat und
der nie ersetzt wird; Strg+Z nimmt sie alle zurück. Nichts verlässt den Computer. Gesperrt, solange eine deiner Partien
auf Lichess gespielt wird, während einer Partie gegen eine Engine bis zu ihrem Ende ausgeblendet.

### Der geführte Rundgang und „Warum nicht?“
<!-- en: the-guided-visit-and-why-not f67b11ea -->

*Geführter Rundgang* im Tab *Auswertung* (oder Strg+K) führt dich nacheinander durch die Schlüsselmomente der
Auswertung (ihre markierten Züge, wenn sie keine hat): Das Brett zeigt die Stellung vor dem Zug der Partie und fragt
*Bevor du siehst, warum: Was hättest du gespielt?* Spiele den Zug auf dem Brett oder tippe ihn ein; vor deiner Antwort
wird nichts gezeigt, weder die Bewertung noch ein Pfeil noch die Notation der Partie, und die Leertaste sagt *Ich weiß
es nicht*. Dann wird deine Antwort so beurteilt, wie *Züge erraten* einen Tipp beurteilt (siehe [Züge
erraten](#züge-erraten)): der Zug der Engine 3 Punkte, ein gleich guter Zug 2, ein spielbarer 1, sonst 0, von der
Engine der Analyse, eine Sekunde für jeden; das Brett zeigt deinen Zug in Blau, den der Engine in Grün und den der
Partie in Orange, mit einer an die Fakten der Auswertung gebundenen Erklärung und dem, worauf sie beruht (die Engine
der Auswertung und ihre Variante, die Fakten der Stellungen). Enter geht zum nächsten Moment, *Variante ansehen*
spielt die Variante der Engine auf dem Brett, Esc hält an. Eine falsche Antwort, kein Punkt oder *Ich weiß es nicht*,
wird zu einer Karte der heutigen Wiederholung, wie deine Fehler (siehe [Deine Fehler als
Übungen](#deine-fehler-als-übungen)): die Stellung, mit dem Zug der Engine zu finden.

**Warum nicht?** (Alt+Q) stellt die Frage *Vergleichen* des Assistenten (siehe [Fragen](#fragen)) mit einer Taste,
wo immer ein Zug zu sehen ist: nach einem auf dem Brett probierten Zug (wie immer eine Variante) stellt sie den Zug dem
der Partie gegenüber; beim Zug der Partie selbst den besten der Auswertung ihm gegenüber; die rechte Maustaste auf
einer Variante einer Engine im Tab Analyse oder auf einem Zug des Tabs Baum bietet *Warum nicht …?* an; im geführten
Rundgang stellt *Warum nicht ein anderer Zug?* deine Antwort oder den Zug der Partie dem der Engine gegenüber. Die
Antwort kommt im Tab Fragen (im Rundgang darunter): die Engine nach jedem der beiden Züge (ein Thread, 300.000 Knoten
pro Stellung), die Fakten beider Stellungen, die Partien und das Ergebnis deiner Datenbanken, die Fortsetzung; *Zur
Notation hinzufügen* schreibt sie hinein, wie bei jeder Frage. Kein freies Gespräch: Der Assistent beantwortet, was
die Engine, deine Datenbanken und die Fakten belegen. Beides ist gesperrt, solange eine deiner Partien auf Lichess
gespielt wird, und während einer Partie gegen eine Engine bis zu ihrem Ende ausgeblendet.

### Exportieren und drucken
<!-- en: exporting-and-printing 33db051f -->

*Exportieren…* unter dem Brett fürs Studium, neben *FEN kopieren* und *lichess.org…*, öffnet anstelle der Seite den
Bereich Exportieren; Esc geht zurück zur Seite. Seine Formate sind Chips oben, das zuletzt gewählte schon gewählt
(<kbd>Strg</kbd>+<kbd>Bild↓</kbd> und <kbd>Strg</kbd>+<kbd>Bild↑</kbd> gehen sie durch); links zeigt er die Datei, wie
sie sein wird, rechts, was hineinkommt, und jedes Format behält seine Einstellungen für das nächste Mal. Enter
speichert: Der Dialog des Systems öffnet sich im zuletzt gewählten Ordner. Alles wird von RookStudio erstellt, auf
deiner Festplatte: Nichts wird irgendwohin gesendet (eine Partie auf lichess.org zu teilen, ist Sache von
*lichess.org…*, siehe [Stellungen und Partien zu Lichess](#stellungen-und-partien-zu-lichess)).

- **HTML-Seite**: eine Datei mit allem darin, die sich ohne Internet öffnet, auch auf einem Handy, und so verschickt
  werden kann, wie sie ist. Das Brett in den Farben und Figuren der App, die Züge, ihre Kommentare und Varianten; ein
  Klick auf einen Zug zeigt ihn, auch in einer Variante, mit den bei diesem Zug gezeichneten Pfeilen und Feldern;
  <kbd>←</kbd> <kbd>→</kbd> <kbd>Pos1</kbd> <kbd>Ende</kbd> gehen durch die Züge, und <kbd>F</kbd> dreht das Brett. Was
  sie enthält, wird angehakt: die Kommentare, die Varianten, die Pfeile und Felder, die Kommentare des Assistenten (als
  seine eigenen markiert), die Bewertungen und ihre Kurve, die Uhren. Ihr Gewicht wird vor dem Speichern geschätzt. Von
  der Seite Datenbanken aus, oder von der Seite Brett, wenn ihre Partie aus der Liste einer Datenbank geöffnet wurde,
  enthält die Seite diese Partie oder die Partien des Filters, oben in einer Liste gewählt (<kbd>Bild↑</kbd>,
  <kbd>Bild↓</kbd>); bei mehr als 200 Partien wäre sie zu schwer zum Öffnen, und der Bereich fragt: eine Seite pro
  Partie in einem Ordner, mit ihrer Liste (`index.html`), oder die ersten 200.
- **Diagramm, PNG oder SVG**: die gezeigte Stellung, mit den bei diesem Zug gezeichneten Pfeilen und Feldern (die
  rechte Maustaste), den Koordinaten, einem Punkt am Rand der Seite am Zug, Schwarz unten, wenn Schwarz am Zug ist
  (falls angehakt), dem letzten Zug (falls angehakt) und einer Bildunterschrift unter dem Brett, die mit *Weiß am Zug*
  beginnt und sich nach Belieben schreiben lässt (*Weiß am Zug gewinnt*). In den Farben der App oder schwarz-weiß zum
  Drucken. Das PNG ist 480 Pixel breit, oder doppelt so breit für einen hochauflösenden Bildschirm; das SVG hat keine
  Größe und wird in jeder scharf gezeichnet. *Bild kopieren* oder <kbd>Strg</kbd>+<kbd>Umschalt</kbd>+<kbd>C</kbd>
  überall auf der Seite Brett (und *Diagramm kopieren* in der Befehlspalette) legt das PNG in die Zwischenablage, für
  eine Nachricht oder ein Dokument.
- **Animiertes GIF**: die Partie in den Farben der App, von einem Zug zu einem anderen (*Nur von … bis …* für einen
  Ausschnitt), ein Bild pro Zug in der gewählten Geschwindigkeit, das letzte länger gehalten; die Namen der Spieler und
  das Ergebnis in einem Banner, der Zug unter dem Brett, der letzte Zug markiert und die Pfeile der Partie, wie
  angehakt. Es wird im Hintergrund erstellt, mit seinem Fortschritt und *Abbrechen*, und sein Gewicht vorher
  geschätzt: Über 2 MB sagt der Bereich, dass es für eine Nachricht oder ein Forum groß ist, und bietet einen
  Ausschnitt oder eine kleinere Größe an.
- **Drucken · PDF**: ein von RookStudio erstelltes PDF, gleich auf jedem System, seine Vorschau die genaue Seite. *Zum
  Drucken öffnen* (Enter, <kbd>Strg</kbd>+<kbd>P</kbd>) übergibt es dem Anzeigeprogramm des Systems, das druckt; *PDF
  speichern…* bewahrt es dort auf, wo du willst. Das Papier ist A4 oder Letter, wie es Sprache und Land des Systems
  vorsehen, oder wie gewählt. Eine Partie wird in einer oder zwei Spalten gedruckt, ihre Kopfdaten oben auf der Seite,
  mit Diagrammen bei ihren Schlüsselmomenten (den Fehlern der Auswertung, sonst den mit !!, ??, ? und ! markierten
  Zügen), bei jedem Kommentar mit Pfeilen oder Feldern (oder `[#]`), alle 10 Züge oder keinen; die Kommentare des
  Assistenten und die Bewertungen neben den Zügen, wenn angehakt. Ihr Text lässt sich markieren und kopieren: Ihre
  Schriften sind eingebettet, Inter und die chinesische Schrift des Systems, wenn die Seite chinesische Zeichen hat.
  Hat das System kein Anzeigeprogramm für PDF-Dateien, wird das PDF gespeichert, und sein Ordner öffnet sich.
- **PGN**, von der Seite Datenbanken aus: die gewählte Partie oder die Partien des Filters. *Die Namen in die Partien
  schreiben* (zu Beginn aus, die Partien wie gelesen) schreibt Weiß und Schwarz so, wie die Namenstabelle der
  Datenbank sie hat (siehe [Spielernamen](#spielernamen)), mit den FIDE-IDs und Titeln der Spieler, wenn sie sie kennt;
  die Datenbank selbst ändert sich nicht.

Was ein Export zeigt, nennt die Spieler so, wie die Namenstabelle es tut, so wie die Partieliste sie zeigt. Die
HTML-Seiten und die PDFs enden mit dem Hinweis zu den gezeichneten Figuren (*Cburnett*, BSD-3-Clause-Lizenz;
*Chessnut*, Apache 2.0; *Celtic* und *Fantasy*, MIT); das PNG eines Diagramms trägt ihn in seinem Text, sein SVG in
einem Kommentar.

*Drucken…* gibt es auch dort, wo es etwas auf Papier zu lesen gibt: ein Repertoire auf der Seite Eröffnungen (jede
Variante unter der, die sie verlässt, ein Diagramm an jeder Verzweigung, deine Notizen und, wenn gemessen, seine
Abdeckung und seine Lücken als Anhang; ein leeres Repertoire hat nichts zu drucken, seine Züge zuerst im Tab Baum mit
Sternen markieren), das Dossier eines Spielers, ein Eröffnungsbericht und die Kreuztabelle eines Turniers (im
Querformat, alle Spieler aufgeführt). Das Dossier, der Bericht und die Kreuztabelle haben auch *HTML-Seite*: derselbe
Inhalt als eigene Seite, ihre Varianten auf einem Brett neben dem Text nachspielbar. Das Brett des Bereichs Engines
behält seine eigenen Exporte (*PGN kopieren*, *Partie als PGN speichern…*).

## Datenbanken
<!-- en: databases e5980ed0 -->

Die Seite **Datenbanken** enthält deine Partiedatenbanken: ihre Liste links, die Partien der gewählten rechts. Eine
Datenbank ist ein eigener Ordner (`Name.fcgdb`), schnell gelesen, selbst mit Millionen Partien.

### Partien importieren
<!-- en: importing-games 8ebdd041 -->

*Neue Datenbank* erstellt eine, direkt benannt; *Datenbank hinzufügen…* listet einen anderswo erstellten
Datenbankordner auf, seine Dateien bleiben, wo sie sind. *In diese Datenbank importieren…* nimmt eine PGN-Datei oder
eine Datei `.zst`, `.gz` oder `.zip`, die eine enthält; eine auf die Seite gezogene Datei kommt in die gewählte
Datenbank oder in eine neue, nach ihr benannte, und *Partien einfügen* importiert den PGN-Text der Zwischenablage. Der
Import läuft im Hintergrund mit niedriger Priorität (ein Turnier daneben verliert keine Zeit), mit seinem Fortschritt,
seiner Geschwindigkeit und *Abbrechen*. Die festgeschriebenen Partien bleiben, wenn er abbricht (alle 262.144 Partien
und am Ende), und ein erneuter Import der Datei fügt nur die fehlenden hinzu; Partien, die schon in der Datenbank sind,
werden ausgelassen.

Am Ende sagt ein Banner auf der Datenbank, was gekommen ist: die hinzugefügten Partien, die schon vorhandenen und die
beschädigten unter den hinzugefügten (ein Zug, der sich nicht lesen lässt oder illegal ist, ein abgeschnittener Text,
eine Ausgangsstellung, die keine ist), jede bis zu diesem Zug behalten, ihr Text unverändert; *In der Liste zeigen* oder
ein Klick auf eine zeigt die Datei an ihrer Zeile. Die schon aus einer anderen Quelle vorhandenen Partien, dieselbe
Partie mit anders geschriebenen Kopfdaten, werden mit *Zusammenführen* und *Erst ansehen* genannt (siehe [Doppelte
Partien](#doppelte-partien)). Eine volle Festplatte, ein gewünschter Abbruch oder ein anderer
Fehler sagt, was behalten ist und was gebraucht wird, mit *Import fortsetzen* und *Ordner der Datenbank öffnen*; eine
Datei, die sich nicht weiterlesen lässt, sagt, warum, und die davor gelesenen Partien bleiben. Danach wird der
Eröffnungsbaum der Datenbank im Hintergrund erstellt.

Eine Datei, deren Bytes kein Text sind (ein Programm, ein Bild, ein Archiv, das der Import nicht kennt), wird abgelehnt,
bevor sie als PGN gelesen wird: Nichts wird importiert, und eine dafür angelegte neue Datenbank wird nicht behalten.

Eine Datenbank, in die ein anderes Programm schreibt (zum Beispiel ein anderes RookStudio, das in sie importiert),
öffnet sich schreibgeschützt: Die Liste, der Baum und die Suchen funktionieren; Importieren und Speichern von Partien
warten, bis dieses Programm fertig ist. *Aus der Liste entfernen* lässt die Dateien einer Datenbank, wo sie sind.

### Die Partieliste
<!-- en: the-game-list 7dc68173 -->

Die Liste zeigt für jede Partie Nummer, Spieler, Elo, Ergebnis, Veranstaltung, Datum, ECO-Code und Länge; ein Klick auf
eine Überschrift sortiert sie. Die in der Namenstabelle der Datenbank zusammengeführten Schreibweisen eines Spielers
(siehe [Spielernamen](#spielernamen)) erscheinen unter dem Namen des Spielers, und unter der Maus sagt der Name, wie die
Partie ihn schreibt. Der Schnellfilter darüber nimmt freie Wörter: Spieler, Veranstaltung, Jahr, ECO-Code,
Ergebnis (`carlsen 2024 B90`). ↑ ↓ gehen durch die Liste, und die gewählte Partie erscheint daneben, auf einem kleinen
Brett mit ihren Zügen; Enter oder ein Doppelklick öffnet sie auf der Seite Brett. Millionen Partien scrollen und
sortieren sich sofort. **Partien** und **Turniere**, neben dem Namen der Datenbank über der Liste, wechseln zwischen
ihren Partien und ihren Turnieren (siehe [Turniere und Kreuztabellen](#turniere-und-kreuztabellen)). *Exportieren…*,
nach der Zahl der Partien, macht aus der gewählten Partie oder den Partien des Filters eine Seite zum Nachspielen, ein
animiertes GIF, ein PDF oder eine PGN-Datei, auf Wunsch mit den Spielernamen aus der Namenstabelle (siehe [Exportieren
und drucken](#exportieren-und-drucken)).

### FIDE-Karten
<!-- en: fide-cards 20b6e988 -->

Die **FIDE-Karte** eines Spielers (Titel, Föderation, Geburtsjahr, FIDE-ID, Standard-, Schnellschach- und Blitzwertung)
wird bei Lichess abgefragt, das die Liste der FIDE führt, ohne Konto und nur, wenn du es verlangst: *FIDE-Karte von …*, im
Menü des Rechtsklicks auf einen Namen der Partieliste oder in der Vorschau der Partie. Eine Partie, die die FIDE-ID des
Spielers angibt (*WhiteFideId*, wie die Übertragungen von Lichess), sendet diese ID; sonst wird der Name gesendet, und
wenn mehrere Spieler der FIDE-Liste so heißen, wählst du, welcher es ist. Die Karte wird für diesen Namen in deinem
Profil gespeichert: Sie erscheint dann, wenn die Maus über dem Namen steht, und in der Vorschau. *Auf lichess.org*
öffnet dort die Seite des Spielers, *Dossier von …* sein Dossier (siehe [Das Dossier eines
Spielers](#das-dossier-eines-spielers)). Ein Spieler, der in der Namenstabelle der Datenbank der FIDE-Elo-Liste
zugeordnet ist, zeigt stattdessen die Karte der Liste, ohne Lichess zu fragen.

### Das Dossier eines Spielers
<!-- en: a-players-dossier 3e1a2bc7 -->

Das **Dossier** eines Spielers stellt zusammen, was deine Datenbanken über ihn wissen. Es öffnet sich über seinen
Namen: *Dossier von …* im Menü des Rechtsklicks auf einen Namen der Partieliste, in der Vorschau einer Partie oder auf
einer FIDE-Karte; Strg+K, *Dossier: Duval*; *Eine Partie vorbereiten gegen* auf der Seite Lichess (siehe [Eine Partie
vorbereiten](#eine-partie-vorbereiten)). Es erscheint anstelle der Seite, und Esc geht zurück. Es wird bei jedem Öffnen
neu erstellt, aus deinen Datenbanken, wie sie sind, im Hintergrund mit niedriger Priorität, mit seinem Fortschritt und
*Abbrechen* (höchstens ein paar Sekunden, selbst für einen Spieler mit Tausenden von Partien unter Millionen); nichts
davon wird aufbewahrt.

Seine Partien werden über die Namenstabelle jeder Datenbank gefunden (siehe [Spielernamen](#spielernamen)): alle
Schreibweisen eines Spielers zusammen, in jeder deiner Datenbanken außer denen mit Partien von Lichess-Spielern
(*Lichess · Name*: Die seines Kontos sind die Quelle Lichess). Ein der FIDE-Elo-Liste zugeordneter Spieler wird von
seinen Namensvettern über die FIDE-ID unterschieden, die die Partien angeben, und die Partien, die gespielt wurden,
bevor er spielen konnte, werden ausgelassen. Hat die FIDE-Liste mehrere Spieler dieses Namens, fragt das Dossier,
welcher es ist, und schlägt zuerst den vor, dessen Wertung zu seinen Partien passt: ↑ ↓ wählen, Enter speichert die
Wahl in den Namenstabellen, und das Dossier nimmt dann nur die Partien dieses Spielers (Strg+Z macht es rückgängig;
*Nicht jetzt* zeigt alle Partien dieses Namens).

Oben: Titel, Föderation, Geburtsjahr und FIDE-Wertungen des Spielers, wenn er der Liste zugeordnet ist, und woraus es
besteht: deine Datenbanken, mit ihren Partien und Jahren, und Lichess, wenn seine Quelle angehakt ist. *Seine Partien in
der Liste* zeigt sie auf der Seite Datenbanken; *Drucken…* macht aus dem Dossier ein PDF von wenigen Seiten,
*HTML-Seite* eine eigene Seite, deren Varianten sich auf einem Brett nachspielen lassen (siehe [Exportieren und
drucken](#exportieren-und-drucken)). Dann vier Tabs (Strg+Bild↓ und Strg+Bild↑ gehen von einem zum nächsten):

- **Übersicht**: seine ersten Züge mit Weiß und seine Ausbeute, seine Antworten mit Schwarz auf die ersten Züge, die
  ihm am häufigsten begegnen, seine Wertung über die Jahre, seine Ergebnisse gegen stärkere und schwächere Spieler,
  seine Bauernstrukturen und deine Partien gegen ihn (deine Namen sind die aus *Einstellungen › Deine Namen*); dann
  seine letzten Partien. Ein Klick öffnet eine auf der Seite Brett, mit dem Dossier als Liste: Strg+← und Strg+→ gehen
  durch seine Partien, und *Zurück zum Dossier* kehrt zurück;
- **Eröffnungen**: seine Varianten mit Weiß und mit Schwarz, nach ECO-Code, jede mit ihren Partien, ihren Gewinnen,
  Remis und Verlusten, seiner Ausbeute und dem letzten Jahr, in dem er sie gespielt hat. Ein Klick öffnet die Variante
  auf der Seite Brett, ihren Tab Baum nur auf seinen Partien, von seiner Seite aus (*Duval, Anne mit Weiß*). Darunter
  die Lücken deiner Repertoires gegen ihn: die Varianten, die er dreimal oder öfter spielt und auf die deine
  Repertoires keine Antwort haben, die häufigsten zuerst;
- **Wertung und Strukturen**: seine Wertung Jahr für Jahr, der Mittelwert der Elo, die seine klassischen Partien in
  jedem Jahr angeben (keine Kurve unter drei Jahren), seine höchste und was die FIDE-Liste sagt; seine Bauernstrukturen
  beim 20. Zug, wie die Fakten des Assistenten sie benennen (keine unter fünf Partien), ein Klick zeigt diese Partien in
  der Liste, die Suche Struktur geht von seinen Bauern aus; und wenn du Partien von ihm ausgewertet hast (Alt+G), seine
  Genauigkeit in jeder Phase und wohin seine klaren Fehler fallen;
- **Lichess**, mit seiner Quelle: was er am Brett und online spielt, nebeneinander, sein Niveau online, deine Partien
  gegen ihn auf beiden und die Lücken deiner Repertoires über beide Quellen.

*+ Lichess…* verknüpft sein Lichess-Konto mit ihm, die Verknüpfung in den Namenstabellen gespeichert, und hakt die
Quelle an: Seine Partien des letzten Jahres kommen wie bei einer gegen ihn vorbereiteten Partie; ein Klick auf den Chip
der Quelle nimmt sie heraus oder fügt sie wieder hinzu. Unter zwanzig Partien sagt das Dossier, dass es eher Hinweise
als Trends gibt. Ein Spieler ohne Partie in deinen Datenbanken wird so genannt, mit *Aus einer Quelle…* und *+
Lichess…*, um welche zu holen. Nichts verlässt den Computer, außer, mit angehakter Quelle Lichess, dem, was das
Vorbereiten einer Partie sendet: sein Name mit den Filtern seiner Partien und sein öffentliches Profil. Offline zeigt
die Quelle die Partien ihres letzten Abgleichs, mit Datum. Solange du auf Lichess gegen ihn spielst, zeigt das Dossier
nichts von ihm.

### Turniere und Kreuztabellen
<!-- en: tournaments-and-crosstables 208330d7 -->

*Turniere*, neben *Partien* über der Liste einer Datenbank, zeigt ihre Turniere: die Partien einer Veranstaltung, ihr
Ort und ihr Jahr (ein Turnier von Dezember bis Januar bleibt eines; dieselbe Veranstaltung an zwei Orten, mit eigenen
Spielern, ergibt zwei). Online-Partien, die keine Veranstaltung mit angegebenen Runden teilen, gehören zu keinem: Eine
Datenbank mit solchen sagt, dass sie kein Turnier hat, und bietet *Partien* an. Jedes Turnier gibt sein System, seine
Spieler, seine Runden, den Elo-Schnitt seiner Spieler und seine Partien an, *286 von 288* in Orange, wenn Partien
fehlen. Die Liste ist nach Elo-Schnitt sortiert; ein Klick auf eine Überschrift sortiert sie anders, und ihr Filter
(Strg+L) nimmt Wörter der Veranstaltung, des Orts oder des Jahres. Die Liste einer großen Datenbank wird im
Hintergrund mit ihrem Fortschritt erstellt (ein paar Sekunden für zehn Millionen Partien) und aufbewahrt, bis sich die
Datenbank ändert.

Das System wird aus den Partien geschätzt: ein Rundenturnier, wenn jeder gegen jeden gespielt hat (ein doppelrundiges,
wenn sich jedes Paar zweimal getroffen hat), sonst Schweizer System; *?*, wenn es nicht sicher ist (ein Paar hat sich
öfter getroffen, als sein System es zulässt, Partien fehlen in einem Feld, das so klein ist wie das eines
Rundenturniers). Mannschaftsturniere und K.-o.-Turniere werden genannt, ihre Kreuztabellen kommen später. Ein Klick
auf das System ändert es, für das Turnier gespeichert.

Ein Klick auf ein Turnier oder Enter öffnet anstelle der Liste seine **Kreuztabelle**, und Esc geht zurück; auch
Strg+K öffnet eine (*Kreuztabelle: Autumn Open*). Ihre Spieler sind die der Namenstabelle der Datenbank (siehe
[Spielernamen](#spielernamen)): alle Schreibweisen eines Spielers zusammen. Die Partien, die doppelt aus zwei Quellen
gekommen sind (dieselben Spieler mit denselben Farben, Runden, die dieselbe sein können), werden einmal gezählt, und
die Runde jeder Partie wird aus ihrem Tag Round gelesen (*9.2*: Runde 9, Brett 2).

- Die Kreuztabelle eines Rundenturniers hat eine Zelle für jeden Gegner: 1, ½ oder 0 (ein Ergebnis pro Zyklus bei
  einem doppelrundigen Turnier), + und − für kampflos; dann die Punkte, die Feinwertungen, die Siege, die Performance
  und die Wertungsänderung.
- Die eines Turniers nach Schweizer System hat die Startnummern, nach Wertung, dann eine Zelle für jede Runde: *4s½*
  ist ein Remis gegen Nummer 4, mit Schwarz; *spielfrei*, der einzige Spieler ohne Paarung in der Runde (die Punkte
  eines Siegs, die Regel der FIDE für ein durch die Paarung zugeteiltes Freilos); *–*, eine Runde ohne Partie. Die
  ersten zehn werden gezeigt, ihre Partien untereinander in der Akzentfarbe; *Alle Spieler* zeigt die anderen.

Ein Klick auf eine Partie öffnet sie auf der Seite Brett, mit den Partien des Turniers als Liste (Strg+← und Strg+→,
*Zurück zur Kreuztabelle*); ein Klick auf einen Namen öffnet das Dossier des Spielers (siehe [Das Dossier eines
Spielers](#das-dossier-eines-spielers)). Die Performance und die Wertungsänderung sagen unter der Maus, wie sie
gezählt wurden. *Drucken…* macht aus der Kreuztabelle ein PDF im Querformat, alle Spieler aufgeführt, und *HTML-Seite*
eine eigene Seite (siehe [Exportieren und drucken](#exportieren-und-drucken)).

Über der Kreuztabelle steht für jeden Fehler in den Daten eine Zeile, mit dem, was ihn behebt:

- **fehlende Partien** (ein Paar eines Rundenturniers, das sich nicht getroffen hat, zwei Spieler eines Turniers nach
  Schweizer System ohne Partie in einer Runde) werden als „?“ gezählt, und die Punkte, Feinwertungen und
  Wertungsänderungen, die sie lesen, zeigen „?“. *In deinen anderen Datenbanken suchen* findet sie unter anderen
  Schreibweisen, *In dieses Turnier kopieren* fügt eine hinzu, Veranstaltung, Ort und Spieler wie die des Turniers
  geschrieben (Strg+Z nimmt sie wieder heraus); *Ergebnisse von Hand setzen* gibt jeder ihr Ergebnis, wie ein Klick auf
  ein „?“, mit dem Turnier in deinem Profil gespeichert, die Datenbank unverändert;
- **ein Spieler unter zwei Namen**: zwei Namen, die einem Spieler gehören können, nie gegeneinander gepaart, ihre
  Runden getrennt; *Zusammenführen* vereint sie in der Namenstabelle (Strg+Z macht es rückgängig);
- **doppelt gekommene Partien**: *Zusammenführen* behält jede als eine Partie, mit den vollständigsten Feldern aus jeder
  Quelle, wie es die Duplikate der Wartung tun (Strg+Z macht es rückgängig); die, deren Züge oder Ergebnisse
  abweichen, werden stattdessen in der Liste gezeigt;
- **ein kampfloses Ergebnis**: ein entschiedenes Ergebnis ohne Züge (ein PGN hat kein Wort dafür) zählt in den Punkten,
  nicht in der Performance und nicht in der Wertungsänderung;
- **ein unsicheres System**: Ein Klick wählt es.

Eine verfolgte Übertragung hat ebenfalls ihre Kreuztabelle: *Kreuztabelle*, neben der Tabelle in der Spalte der
Mosaikansicht, zeigt sie anstelle der Bretter (*Mosaik* oder Esc holt sie zurück). Sie besteht aus den Runden, die
RookStudio verfolgt, und denen, die es in der Datenbank *Übertragungen* aufbewahrt hat: ● eine laufende Partie (ein
Klick verfolgt sie auf der Seite Brett), · eine kommende Partie, die Punkte, die gespielten Partien und die bisherige
Wertungsänderung. Die eigene Tabelle von Lichess bleibt daneben, und dafür wird Lichess nichts weiter gefragt; eine
Mannschaftsübertragung behält die Mannschaftstabelle von Lichess.

### Feinwertungen, Performance und Wertungsänderung
<!-- en: tie-breaks-performance-and-rating-change 18737372 -->

Die Feinwertungen ordnen die punktgleichen Spieler, eine nach der anderen: als Vorgabe die der FIDE, und die Zeile über
der Tabelle sagt, welche. Die aktuellen Feinwertungsregeln der FIDE (C.07, gültig ab 1. März 2026) empfehlen für kein
System welche, also nimmt RookStudio die der eigenen Veranstaltungen der FIDE: für ein Rundenturnier, wie beim
Kandidatenturnier 2026, Sonneborn-Berger, die Siege, den direkten Vergleich; für ein Turnier nach Schweizer System,
wie beim Grand Swiss 2025, die durchschnittliche Wertung der Gegner mit Streichresultat, Buchholz mit Streichresultat,
Buchholz, den direkten Vergleich (ohne die Durchschnittswertung, wenn ein Spieler keine Wertung hat, wie C.07 es
verlangt). Ein Klick auf eine Feinwertung, in dieser Zeile oder auf ihrer Überschrift, setzt sie an die erste Stelle,
verschiebt, ersetzt oder entfernt sie; *+* fügt eine hinzu; *Die der FIDE für …* stellt deren wieder her. Die
Reihenfolge wird für das Turnier gespeichert.

Es sind die der FIDE: direkter Vergleich, Siege (kampflose Siege und spielfreie Runden, die einen Sieg wert sind,
zählen mit), gewonnene Partien, Partien mit Schwarz, Siege mit Schwarz, Fortschrittswertung, Buchholz, Buchholz mit
Streichresultat, mittlere Buchholz, Sonneborn-Berger, Koya, die durchschnittliche Wertung der Gegner (auch mit
Streichresultat) und die Performance. Im Schweizer System zählt eine Runde ohne Partie bei Buchholz und
Sonneborn-Berger so, wie die FIDE es sagt (C.07, Artikel 16): wie gegen einen Gegner mit der eigenen Punktzahl des
Spielers, höchstens ein Remis pro Runde (bei einem kampflosen Ergebnis höchstens die Punktzahl des angesetzten
Gegners), und die Runden eines Spielers nach seinem Rückzug zählen für seine Gegner als Remis.

Die **Performance** ist die der FIDE: die durchschnittliche Wertung der gewerteten Gegner, gegen die am Brett gespielt
wurde, plus die Differenz, die die Tabelle der FIDE der Punktzahl in diesen Partien zuordnet (auf die Hundertstel
gerundet). Die **Wertungsänderung** ist die Formel der FIDE: Für jede am Brett gegen einen gewerteten Gegner gespielte
Partie die Punktzahl weniger der erwarteten Punktzahl aus der Tabelle der FIDE (eine Differenz von höchstens 400
gezählt, unter 2650), mal K. K ist der eigene des Spielers aus der FIDE-Elo-Liste, wenn er ihr in der Namenstabelle
zugeordnet ist und die Liste aus dem Jahr des Turniers oder dem folgenden stammt (siehe [Die
FIDE-Elo-Liste](#die-fide-elo-liste)); sonst geben ihn die Regeln der FIDE: 40 bis zum Ende des Jahres seines 18.
Geburtstags, solange er unter 2300 ist, 10 ab 2400, sonst 20, kleiner, wenn über alle seine Partien 700 überschritten
würden. Ein Schnellschach- oder Blitzturnier folgt den Schnellschach- und Blitzregeln der FIDE. Ein kampfloses Ergebnis
zählt nur bei den Punkten.

### Suchen
<!-- en: searching fda4d044 -->

*Suche* (Strg+F) öffnet den Suchbereich neben der Liste, in sechs Tabs (Strg+Bild↓ und Strg+Bild↑ gehen von einem zum
nächsten); die Liste folgt jedem Kriterium, während es eingetippt wird:

- **Kopfdaten**: ein Spieler und seine Farbe, der Gegner, die Veranstaltung, die Jahre, die ECO-Codes, die Elo beider
  Spieler mindestens, das Ergebnis; *Nur beschädigte Partien*;
- **Stellung**: die Stellung der Seite Brett oder eine aufgebaute (*Figuren aufstellen…*): Ihre Partien werden im
  Hintergrund gefunden, die, die sie nicht erreichen können (ihre Bauern, ihr Material), ausgelassen, ohne nachgespielt
  zu werden;
- **Material**: eine Voreinstellung (Turmendspiele, ohne Damen, das Läuferpaar gegen Läufer und Springer, eine Dame
  gegen zwei Türme) oder die Steine jeder Seite gezählt (`2` oder `0-2`), für mindestens so viele Halbzüge;
- **Muster**: mit der Maus aufs Brett gestellte Steine (ein Klick setzt den gewählten Stein, ein weiterer Klick
  entfernt ihn; Umschalt und ein Klick: ein leeres Feld; die übrigen Felder: beliebig), *Weiß hat nur diese Bauern*, drei
  Beispiele und Bedingungen darüber hinaus: was was attackiert, deckt oder fesselt, eine Zeile mit Zügen (siehe
  [Muster](#muster));
- **Manöver**: die Route eines Steins, etwa ein Springer, der b1, d2, f1, g3 durchläuft (siehe [Manöver](#manöver));
- **Struktur**: die Bauern der Seite Brett, genau oder mit einem oder zwei Bauern Abstand (siehe
  [Bauernstrukturen](#bauernstrukturen)).

Jede Suche liefert ihre Partien und ihre Zeit; *Zum Filter hinzufügen* und *Aus dem Filter nehmen* kombinieren sie,
*In der Liste zeigen* (Enter) zeigt das Ergebnis, *Alles leeren* fängt neu an. Die gesetzten Suchen addieren sich: ein
Muster, ein Material und Kopfdaten. Eine Suche, die die Partien nachspielt (ein Muster, eine Route, eine Struktur),
läuft im Hintergrund mit ihrem Fortschritt und *Abbrechen*; die ersten gefundenen Partien sind schon in der Liste, eine
abgebrochene Suche behält sie, und *Fortsetzen (nur der Rest)* durchsucht die Partien, die sie noch nicht erreicht
hatte. Während in die Datenbank importiert wird, umfasst eine Suche die schon übernommenen Partien und wird am Ende
von selbst wiederholt.

Die Liste zeigt dann jede Partie bei der gefundenen Stellung, in einer Spalte *Gefunden bei* (der Zug, der dorthin
führte): Enter oder ein Doppelklick öffnet die Partie bei diesem Zug, ebenso die nächste oder vorige Partie der Liste
von der Seite Brett aus.

*Diese Suche speichern…* benennt sie nach ihren Kriterien (ein Muster nach dem Namen seiner Voreinstellung). Gespeichert
werden ihre Kriterien, nicht ihre Partien, für jede Datenbank (ihre neuen Partien werden beim nächsten Mal gefunden);
die gespeicherten Suchen erscheinen oben im Bereich, wo ein Klick ihre Kriterien setzt und die rechte Maustaste eine
umbenennt oder löscht, und in Strg+K (*Suche: …* oder *Muster: …* für ein Muster allein).

### Muster
<!-- en: patterns 5821a112 -->

Der Tab **Muster** sagt, was in einer Stellung zutrifft: zuerst die Steine auf dem kleinen Brett, dann Bedingungen,
eine pro Zeile, alle zugleich wahr. Ein Klick auf ein Wort einer Zeile ändert es: die Steine (ein weißer Springer, die
schwarze Dame oder ein schwarzer Turm, ein beliebiger schwarzer Stein, auf Feldern oder beliebig), das Verb (*steht
auf*, *attackiert*, *deckt*, *fesselt … an …*, *steht en prise*, *ist ein Freibauer* und ihr Gegenteil), die Seite am
Zug, Schach, Matt. *+ Eine Bedingung* fügt eine hinzu, × entfernt sie. *Derselbe Stein wie in Zeile 1* im Menü der
Steine lässt zwei Zeilen von einem Stein sprechen: Ein Springer, der den König attackiert, und derselbe Springer, der
die Dame oder einen Turm attackiert, ist eine Gabel.

Die Voreinstellungen setzen ein ganzes Muster: *Springergabel*, *Fesselung an den König*, *Grundreihenmatt*,
*Doppelangriff der Dame*, *Opfer auf h7*, *Gedeckter Freibauer*; sie sind auch in Strg+K (*Muster: Springergabel*), für
die gewählte Datenbank.

*dann… (eine Zeile)* fügt nach der ersten eine Folge von Stellungen hinzu: jeder Schritt ein Zug („der weiße Läufer
schlägt auf h7 mit Schach“) oder eine Stellung, im nächsten Halbzug oder innerhalb so vieler Halbzüge (*jeder Schritt im
nächsten Halbzug*, *innerhalb von 3 Halbzügen*). Ein Zug lässt sich auch auf dem kleinen Brett spielen (*+ Ein
Schritt*, *ein auf dem kleinen Brett gespielter Zug*: ein Klick auf den Stein, dann auf sein Feld). In jeder Partie ist
die gefundene Stellung die erste des Musters.

Was nie gefunden werden kann, wird vor der Suche gesagt, und nichts wird gestartet: ein Springer auf c7 und c7 leer,
Weiß und Schwarz am Zug, ein Bauer auf seiner ersten Reihe; ein Klick entfernt eine der beiden Bedingungen. Eine Suche,
die mehr als die Hälfte der Partien findet, sagt es (*Sehr breit*): *Bedingung hinzufügen* oder *So lassen*.

### Die Sprache der Muster
<!-- en: the-pattern-language af783ff9 -->

Dasselbe Muster ist ein Text unter dem Baukasten (*Text* oder Alt+T): Ändert sich das eine, ändert sich das andere,
und eine gespeicherte Suche behält ihren Text, der sich kopieren und weitergeben lässt. Seine Wörter sind die von CQL,
der Sprache der Schachabfragen:

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

- Steine in FEN-Buchstaben: K Q R B N P für Weiß, k q r b n p für Schwarz, A und a für einen beliebigen weißen oder
  schwarzen Stein, `_` für ein leeres Feld, `[qr]` für einen von mehreren; Felder nach dem Buchstaben (`Nf3`,
  `R[a1,h1]`, `p[a-h7]`) oder allein, wo ein Wort Felder verlangt (`move to [g5,h5]`).
- `attacks`, `defends`, `pin from … through … to …`, `en prise`, `passed`, `check`, `mate`, `stalemate`, `wtm` und
  `btm` (die Seite am Zug), `not`, `or`, `only P` (Weiß hat keine anderen Bauern als die auf ihren Feldern genannten).
- `line -->`, dann nach jedem Pfeil ein Schritt, im nächsten Halbzug: `move from … to … capture … promote …` und was
  nach dem Zug zutrifft, oder eine Stellung; `.{0,2}` vor einem Schritt lässt zuerst zwei beliebige Halbzüge kommen.
- Zwei Namen (`$n`, `$q`) sind immer zwei Steine; ein Name folgt seinem Stein durch die Züge einer Zeile.

Eine Zeile, die sich nicht lesen lässt, wird genannt, mit dem Wort, dem sie ähnelt (*Zeile 2: „atacks“ ist kein
bekanntes Wort — „attacks“?*); vom Rest von CQL wird gesagt, dass er nicht gelesen wird. Der Baukasten behält die
letzte lesbare Fassung, und nichts wird gesucht, bis sich der Text wieder lesen lässt.

### Manöver
<!-- en: manoeuvres 9ac8f6fb -->

Der Tab **Manöver** sucht die Route eines Steins: ein Klick auf einen Stein des kleinen Bretts (die Stellung der Seite
Brett), dann ein Klick auf jedes Feld, auf das er zieht (b1, d2, f1, g3); die Rücktaste nimmt das letzte Feld weg.
*Andere Züge dazwischen* lässt die Seite währenddessen andere Züge spielen; *Durchgehend derselbe Stein* verlangt einen
einzigen Stein, sonst nacheinander einen beliebigen Stein seiner Art auf jedem Feld; *In höchstens 15 Zügen* zählt die
Züge der Seite vom ersten Feld der Route bis zum letzten. *Von: eine beliebige Stellung* oder *Von: die Stellung der
Seite Brett*: die Route, sobald eine Partie diese Stellung erreicht.

Die Ergebnisse sagen, wohin derselbe Stein danach zog (auf f5 in 31 % der Partien), und die benachbarten Routen, die
sich nach seinem ersten Zug von ihr trennen (b1–d2–b3); ein Klick setzt eine. Der graue Pfeil auf dem Brett ist die
meistgespielte Fortsetzung, keine Bedingung. Von der Seite Brett aus nimmt Strg+K *Dieses Manöver* den Stein unter der
Maus und seine Züge in der Partie.

### Bauernstrukturen
<!-- en: pawn-structures 5c6eb547 -->

Der Tab **Struktur** sucht nur die Bauern der Seite Brett, die Figuren beiseitegelassen: *Genau*, *Ein Bauer Abstand*
oder *Zwei Bauern Abstand* (ein Bauer mehr, weniger oder auf einem anderen Feld, auf einer der beiden Seiten, ist ein
Bauer), *Beide Seiten* oder nur *Die Bauern von Weiß* oder *Die Bauern von Schwarz*, *Auch mit vertauschten Farben*,
mindestens so viele Halbzüge gehalten. *Die Bauern der Seite Brett erneut übernehmen* übernimmt sie neu; *Bauern
aufstellen…* ändert sie Feld für Feld. Ihr Name erscheint, wenn die Fakten des Assistenten sie kennen (Karlsbader
Struktur, Maróczy-Aufbau, Igel, Stonewall, die Bauernketten, Benoni, isolierter Damenbauer).

Die Ergebnisse geben die genau gehaltenen Partien und die nahen an, die Eröffnungen, die sie am häufigsten erreichen,
und die Varianten mit einem Bauern Abstand, mit ihren Partien und dem Ergebnis von Weiß („der weiße b-Bauer auf b4“,
„ohne den schwarzen a-Bauern“); ein Klick setzt eine. Von der Seite Brett aus sucht Strg+K *Partien mit dieser Struktur*
sofort nach ihren Bauern.

### Turniere und PGN-Dateien
<!-- en: tournaments-and-pgn-files 688ddcc2 -->

Die Turniere des Profils und die auf der Seite Brett geöffnete PGN-Datei erscheinen unter *Ohne Kopie gelesen* nach
deinen Datenbanken: Ihre Partien werden wie die einer Datenbank aufgelistet und durchsucht und beim Anzeigen im
Hintergrund aktualisiert (die Partien, die ein Turnier beendet, eine Datei, die gewachsen ist oder sich geändert hat).
Sie werden weder importiert noch entfernt.

### ChessBase-Datenbanken
<!-- en: chessbase-databases 3da99106 -->

Eine ChessBase-Datenbank, auf die Seite Datenbanken gezogen oder mit *Datei importieren…* gewählt, wird in eine
RookStudio-Datenbank kopiert: ihre .cbh oder .2cbh (oder eine beliebige ihrer Dateien) oder ein Archiv davon, .cbv oder
.cbz für eine .cbh, .2cbv oder .2cbz für eine .2cbh. Eine Karte sagt zuerst, was sie enthält, aus ihren Kopfdaten
gelesen, was übernommen und was ausgelassen wird, wohin sie kommt und wie lange es ungefähr dauert; *Importieren*
(Enter) startet die Kopie, *Nicht jetzt* (Esc) lässt sie. Die ChessBase-Dateien werden nur gelesen, nichts wird daneben
geschrieben; ein Archiv wird beim Lesen entpackt. Das Passwort einer .cbz oder einer .2cbz wird auf der Karte eingetippt
(Enter im Feld startet die Kopie) und nicht gespeichert; ein falsches wird dort sofort gemeldet, um es erneut
einzutippen, bevor irgendetwas kopiert wird.

Die Kopie behält die Züge und ihre Varianten, die Kommentare vor und nach den Zügen (ein Text in mehreren Sprachen:
Englisch zuerst, die anderen danach), die Symbole, die farbigen Felder und Pfeile, die Uhren und die verbrauchte Zeit,
die Bewertungen der Engines, die Medaillen (ein Tag *Medals*), die kritischen Stellungen und die Bedenkzeiten.
Trainingsfragen, Bilder, Töne und Videos, Weblinks, zitierte Partien, Bauernstrukturen und Figurenwege, Variantenfarben
und die Texte (Artikel) werden ausgelassen: Der Bericht am Ende zählt sie nach Art. Bei einem erneuten Import (seither
in ChessBase hinzugefügte Partien) kommen nur die neuen Partien hinzu. Die Namen der Spieler werden so geschrieben, wie
die Datenbank sie schreibt, „Nachname, Vorname“.

### TWIC
<!-- en: twic 56b70a1d -->

*Aus einer Quelle…* holt Partien von einer Website, nur heruntergeladen, wenn du es verlangst. **TWIC**, The Week in
Chess, veröffentlicht jeden Montag eine Ausgabe mit den Partien der Woche: Sein Bereich fragt theweekinchess.com nach
seiner Liste der Ausgaben und zeigt die neuesten, mit ihrem Montag, ihren Partien und ob jede schon in der gewählten
Datenbank ist. Hake einige an, wähle die Datenbank (anfangs *Eine neue Datenbank, „TWIC“*), dann *Herunterladen und
hinzufügen*: Das PGN jeder Ausgabe wird heruntergeladen und dann importiert, eine nach der anderen, während die
Datenbank lesbar bleibt; *Abbrechen* hält nach dem schon Hinzugefügten an. Eine Partie, die die Datenbank schon hat, mit
gleichen Tags und Zügen, wird nicht doppelt hinzugefügt. Danach nennt ein Bericht die stärksten Turniere der
hinzugefügten Partien (die mittlere Wertung ihrer Spieler) und die Spieler mit der besten Wertung; ein Name öffnet seine
Partien in der Liste. Die schon aus einer anderen Quelle vorhandenen Partien, etwa aus einer früher verfolgten
Übertragung, werden dort mit *Zusammenführen* genannt.

*Jeden Montag nach einer neuen Ausgabe suchen* ist aus, bis du es anhakst. Angehakt fragt RookStudio, solange es offen
ist, nach der Liste der Ausgaben, höchstens viermal am Tag; eine neue Ausgabe erscheint unter der Liste der Datenbanken
in *Verfolgte Quellen* und auf der Karte *Meine Datenbanken* der Startseite, mit *Zu „…“ hinzufügen*: Sie wird nie ohne
dich hinzugefügt. Was den Computer verlässt: die Anfrage nach der Liste der Ausgaben, dann nach den PGN-Dateien der
gewählten Ausgaben, sonst nichts. TWIC ist für den persönlichen Gebrauch frei; nichts davon wird mit RookStudio
ausgeliefert.

### Lichess-Monate
<!-- en: lichess-months 26060e4f -->

*Aus einer Quelle… › Lichess-Übertragungen* fügt einen Monat der offiziellen Übertragungen von Lichess hinzu
(database.lichess.org, CC BY-SA 4.0): Sein Bereich fragt nach der Dateiseite der Website und listet die neuesten Monate
mit ihrer Größe und ihren Partien auf. Wähle einen und die Datenbank (anfangs *Eine neue Datenbank,
„Lichess-Übertragungen“*), dann *Herunterladen und hinzufügen*: Die Datei des Monats wird heruntergeladen, fortsetzbar,
dann importiert, und ein Bericht nennt seine stärksten Turniere und die Spieler mit der besten Wertung. Eine Partie, die
die Datenbank schon hat, wird nicht doppelt hinzugefügt. Die Partien behalten ihre Quelle, die Seite der Übertragung in
ihrem Tag Site: Was du davon teilst, wird unter derselben Lizenz geteilt. Was den Computer verlässt: die Anfrage nach
der Seite, dann nach der Datei des Monats.

*Aus einer Quelle… › Lichess-Partien* liest einen Monat gewerteter Partien von Lichess (CC0, etwa 95 Millionen Partien
und 30 GB komprimiert) beim Herunterladen und behält nur die Partien, die seine Filter passieren: standardmäßig beide
Spieler mit einer Wertung von 2400 und mehr, ohne Bullet (eine Grundzeit plus 40 Inkremente unter drei Minuten). Nur die
behaltenen Partien erreichen die Festplatte, stapelweise zur gewählten Datenbank hinzugefügt, während die Datenbank
lesbar bleibt. *Eine schon heruntergeladene Datei lesen…* liest stattdessen eine `lichess_db_standard_rated_*.pgn.zst`
von der Festplatte. *Abbrechen*, eine Netzunterbrechung oder das geschlossene Programm behält, was hinzugefügt wurde,
und *Fortsetzen* macht dort weiter, wo es aufhörte, mit denselben Filtern; die erneut gelesenen Partien werden nicht
doppelt hinzugefügt.

### Ein Spieler von chess.com
<!-- en: a-chesscom-player 39157248 -->

*Aus einer Quelle… › chess.com* fügt die Partien eines Spielers über die öffentliche API von chess.com hinzu, ohne
Konto: Tippe seinen Namen ein, und *Nachschlagen* fragt nach den Monaten, in denen er gespielt hat; *Herunterladen und
hinzufügen* fragt nach den Partien jedes Monats, ein Monat nach dem anderen, und importiert sie in die gewählte
Datenbank (anfangs *Eine neue Datenbank, „chess.com · Name“*), mit demselben Bericht. Ist *Folgen* angehakt, erscheint
der Spieler unter der Liste der Datenbanken in *Verfolgte Quellen*: *Nach neuen Partien suchen* fragt erneut nach den
noch nicht hinzugefügten Monaten und nach dem neuesten (seine Partien sind vielleicht mehr geworden); eine schon
vorhandene Partie wird nicht doppelt hinzugefügt. Was den Computer verlässt: der Name des Spielers und die Anfragen nach
seinen Monaten, an api.chess.com.

### Lumbra's Gigabase
<!-- en: lumbras-gigabase 19d9d71f -->

*Aus einer Quelle… › Lumbra's Gigabase* erklärt, wie man sie holt: Die Website verlangt, dass du ihre Lizenz akzeptierst
(CC BY-NC 4.0) und zeigst, dass du kein Roboter bist; das tut RookStudio nicht für dich. *Website öffnen* öffnet sie in
deinem Browser; sobald die Datei heruntergeladen ist, importiert *Die heruntergeladene Datei importieren…* sie, wie sie
ist. Ein .7z-Archiv (LZMA oder LZMA2) wird beim Entpacken gelesen: seine PGN-Dateien eine nach der anderen, seine
anderen Dateien übergangen, nichts außer den Partien wird auf die Festplatte geschrieben. Jedes .7z mit PGN-Dateien
lässt sich genauso importieren oder auf die Seite ziehen.

### Wartung
<!-- en: maintenance 9c828c81 -->

Das ⋯ einer Datenbank, neben ihren Knöpfen auf der Seite Datenbanken, oder ein Rechtsklick auf sie in der Liste öffnet
anstelle der Seite ihre Wartung, in drei Teilen: *Doppelte Partien*, *Spielernamen*, *Prüfen und komprimieren*; Esc
kehrt zurück. Strg+K hat *Doppelte Partien*, *Spielernamen*, *FIDE-Elo-Liste*, *Datenbank prüfen* und *Datenbank
komprimieren*. Jede lange Aufgabe läuft im Hintergrund mit niedriger Priorität, mit ihrem Fortschritt und
*Abbrechen*, während die Datenbank die ganze Zeit lesbar bleibt: Ein Turnier daneben verliert keine Zeit. Strg+Z macht
die letzte Zusammenführung, die zuletzt zusammengeführten Spieler oder die beiseitegelegten beschädigten Partien
rückgängig. Eine Datenbank, in die ein anderes Programm schreibt, ist schreibgeschützt geöffnet: Ihre Wartung wartet,
bis das Programm sie freigibt. Die Startseite spricht von der Wartung nur, wenn sie etwas findet: Duplikate nach einem
Import, beschädigte Partien, die eine Prüfung oder das Öffnen einer Datenbank gefunden hat.

### Doppelte Partien
<!-- en: duplicate-games f3c8990b -->

Dieselbe Partie kann doppelt aus zwei Quellen kommen, ihre Kopfdaten anders geschrieben: eine auf Lichess verfolgte
Runde, dann die Montagsausgabe von TWIC. Der Import lässt eine Partie aus, die die Datenbank genau so hat (dieselben
Tags und Züge), diese aber nicht. *Doppelte Partien* findet sie: dieselben Spieler (in der Namenstabelle gelesen, sonst
ihre Nachnamen und ersten Initialen), dasselbe Jahr, Runden und Daten, die sich nicht widersprechen, und dieselben Züge
oder die Züge der einen als Anfang der anderen, unvollendet (eine abgebrochene Übertragung). Eine Partie braucht
mindestens zehn Züge, um erkannt zu werden (zwanzig, wenn ihr Tag unbekannt ist); zwei Partien mit einer eigenen
Adresse (der von Lichess) sind zwei Partien.

Sie zählt sie nach Art: dieselben Züge mit anderen Kopfdaten; eine kürzer, eine abgebrochene Übertragung; abweichende
Ergebnisse, dir überlassen. *Alle zusammenführen* (Enter) führt die ersten beiden Arten zusammen: Jede Gruppe wird eine
Partie mit dem vollständigsten Inhalt jedes Felds (ein ausgeschriebener Name vor einer Initiale, die längere
Veranstaltung, die aussagekräftigere Runde, „9.2“ vor „9“, ein genaueres Datum, die Wertungen, der ECO-Code, die
FIDE-IDs, die längeren Züge mit ihren Kommentaren, die Uhren und Bewertungen, ein Ergebnis vor keinem), die anderen
Partien als gelöscht markiert. Ein Klick auf eine Gruppe vergleicht sie Feld für Feld, jede Quelle in einer Spalte und
die zusammengeführte Partie daneben: Ein Klick auf eine Zelle nimmt dieses Feld aus dieser Quelle; dann
*Zusammenführen* (Enter), *Beide behalten* (zwei Partien, nie wieder angeboten) oder *Nächste Gruppe* (↓). Die
zusammengeführte Partie behält die Nummer der ältesten; ihr Tag *MergedFrom* sagt, woher jedes Feld kam, und eine
zurückgelassene Adresse einer Übertragung bleibt als ihr *GameURL*. Strg+Z holt die anderen Partien zurück. *In der
Liste zeigen* zeigt die Duplikate in der Partieliste, wo *Alle zeigen* wieder alle Partien zeigt.

Am Ende eines Imports werden die schon aus einer anderen Quelle vorhandenen Partien in seinem Bericht genannt, mit
*Zusammenführen*: Sie werden nie ohne deinen Klick zusammengeführt. Die Berichte der Quellen (*Aus einer Quelle…*)
nennen sie ebenfalls.

### Spielernamen
<!-- en: players-names 6b9a8fcb -->

Ein Spieler wird oft auf mehrere Arten geschrieben: „Lindqvist, Erik“, „Lindqvist,E“, „Erik Lindqvist“. *Spielernamen*
führt die Schreibweisen eines Spielers in der Namenstabelle der Datenbank zusammen. Die Partien werden nicht geändert,
aber die Partieliste zeigt den Namen des Spielers, und ihr Schnellfilter, ihre Suche und ihre Sortierungen, die
Referenzpartien des Eröffnungsbaums, die Berichte der Quellen und die Dossiers der Spieler lesen ihn. Drei Zahlen
sagen, was sich tun lässt:

- sofort zusammengeführt: Die Partien geben die FIDE-ID des Spielers an (die Übertragungen von Lichess tun es), oder
  die FIDE-Elo-Liste hat einen einzigen Spieler dieses Namens, oder dieses Namens, wie ein anderes Alphabet ihn schreibt
  („Kowalenko“ für „Kovalenko“), mit Wertungen nahe an denen der Partien; *Die … zusammenführen* (Enter) tut es;
- zu bestätigen: Mehrere Spieler der Liste haben diesen Namen. Der, dessen Wertung der der Partien am nächsten ist, wird
  zuerst vorgeschlagen; ↑ ↓ wählen unter ihnen oder *Keiner von ihnen*, und Enter behält die Wahl und geht zum nächsten;
- unverändert belassen: ein Spieler, der nicht auf der Liste steht, ein Name aus einem Wort (der eines Kontos) oder
  keine Liste übernommen; zwei Schreibweisen desselben Nach- und Vornamens werden von Hand zusammengeführt, mit einem
  Rechtsklick und *Zusammenführen*.

Ein der Liste zugeordneter Spieler behält seine FIDE-ID, Föderation, seinen Titel, sein Geburtsjahr und seine
Wertungen in der Tabelle, auch wenn die Liste entfernt wird. Strg+Z macht es rückgängig. Die Tabelle gehört zur
Datenbank: Sie geht mit ihrem Ordner. Die Partien eines späteren Imports schließen sich von selbst ihren Spielern an,
wenn sie die FIDE-ID eines Spielers angeben oder seinen Namen ausgeschrieben haben; eine andere Schreibweise wartet auf
diese Seite.

### Die FIDE-Elo-Liste
<!-- en: fides-rating-list 85d6baa0 -->

Die FIDE veröffentlicht jeden Monat ihre Elo-Liste, etwa zwei Millionen Spieler: ihre FIDE-IDs, Namen, Föderationen,
Titel, Wertungen und Geburtsjahre. RookStudio liefert sie nicht mit, da die FIDE ihr keine Lizenz gibt.
*Herunterladen* im Teil *Spielernamen* fragt ratings.fide.com nach ihrer kombinierten Liste (ein Zip von etwa 43 MB)
und liest sie in dein Profil ein (etwa 90 MB), wo sie bleibt, nie irgendwohin gesendet; *Eine schon heruntergeladene
Datei…* liest eine, die du hast. Ein unterbrochener Download geht dort weiter, wo er aufhörte (*Fortsetzen*). Jeden
Monat danach sagt die Startseite, dass eine neue Liste erschienen ist, mit *Übernehmen*: Sie wird nie ohne deinen Klick
übernommen. *Nach einer neueren Liste suchen* fragt erneut; *Entfernen* nimmt die Liste aus deinem Profil, die
zusammengeführten Spieler bleiben, wie sie sind. Eine Liste, deren Spalten sich geändert haben, wird abgelehnt, und die
vorige bleibt in Gebrauch, mit *Melden*. Was den Computer verlässt: die Anfrage nach der Datei, wenn du sie
herunterlädst.

### Prüfen und komprimieren
<!-- en: checking-and-compacting d17eb6df -->

*Prüfen* (Enter oder Strg+K *Datenbank prüfen*) liest jedes Segment der Datenbank neu von der Festplatte und spielt
jede Partie nach: Ihre Signatur und ihr Schlüssel (eine Prüfsumme ihrer Kopfdaten und Züge) werden neu erstellt und mit
den gespeicherten verglichen, und eine Partie, die sich anders liest, als sie geschrieben wurde, ist beschädigt. Es
sagt *Intakt* oder nennt die beschädigten Partien und die Dateien, aus denen sie importiert wurden: *Behalten, was
lesbar ist* legt sie beiseite (gelöscht; Strg+Z holt sie zurück), und *Die Datei erneut importieren* holt sie aus ihrer
Datei zurück, wenn sie noch da ist. Auch der Eröffnungsbaum wird mit den Partien verglichen.

Ein Segment, dessen Dateien sich beim Öffnen der Datenbank nicht mehr lesen lassen (abgeschnitten, fehlend oder
geändert, während RookStudio geschlossen war: ein Festplattenfehler, eine halb fertige Kopie, ein
Synchronisationswerkzeug), hindert die Datenbank nicht am Öffnen. Ihre anderen Segmente funktionieren: Die Liste, der
Eröffnungsbaum und die Suchen lesen ihre Partien, und die Partien des beschädigten Segments behalten ihre Nummern. Die
Startseite und die Seite Datenbanken sagen es, mit *Ansehen*, und *Prüfen und komprimieren* sagt es sofort: das
Segment, wie viele Partien sich nicht lesen lassen, und die Dateien, aus denen sie importiert wurden. *Behalten, was
lesbar ist* schreibt dieses Segment neu, seine Partien beiseitegelegt (gelöscht, ihre Nummern behalten). Die mit ihm
zuerst importierten Spieler, Veranstaltungen und Orte bleiben erhalten, wenn seine Namensliste noch lesbar ist; sonst
zeigen die anderen Partien, die sie verwenden, „?“ an ihrer Stelle. Seine beschädigten Dateien bleiben, bis RookStudio
geschlossen wird: Bis dahin legt *Rückgängig* (oder Strg+Z) sie zurück, solange nichts anderes in die Datenbank
geschrieben wird. *Die Datei erneut importieren* holt seine Partien aus ihrer Datei zurück, wenn sie noch da ist. Das
Komprimieren wartet, bis ein solches Segment behandelt ist, und eine schreibgeschützt geöffnete Datenbank wird nie
geändert.

Gelöschte oder ersetzte Partien, Zusammenführungen eingeschlossen, behalten ihren Platz in den Dateien. *Komprimieren*
schreibt die Segmente, die sie enthalten, ohne sie neu: Jede Partie behält ihre Nummer (eine gelöschte nur ihre
Kopfdaten), und der Eröffnungsbaum bleibt gültig. Die alten Dateien bleiben, bis RookStudio geschlossen wird:
*Komprimieren rückgängig machen* legt sie bis dahin zurück, und der Platz wird beim Schließen freigegeben. Importe und
Speichervorgänge in die Datenbank warten, solange sie komprimiert wird; die Liste, der Baum und die Suchen
funktionieren. Hat die Festplatte nicht genug Platz für die Kopie, braucht *Segment für Segment komprimieren* nur den
Platz eines Segments, lässt sich aber nicht rückgängig machen.

## Die Seite Eröffnungen
<!-- en: the-openings-page 296260e5 -->

Deine Repertoires: jedes eine PGN-Datei des Profils (eine Partie und ihre Varianten, für Weiß oder für Schwarz), die
die Seite Brett wie eine Partie öffnet und bearbeitet (*Seine Datei auf dem Brett öffnen*) und *Drucken…* druckt (siehe
[Exportieren und drucken](#exportieren-und-drucken)). Eines ist aktiv: Der Stern
des Tabs Baum fügt ihm den gezeigten Zug hinzu oder nimmt ihn heraus, egal welche Seite am Zug ist; der erste Stern
legt ein Repertoire an, wenn es keines gibt. *Neues Repertoire* erstellt eines mit seinem Namen und seiner Seite.

Die **Abdeckung** des aktiven Repertoires wird im Hintergrund an deinen Datenbanken gemessen, deren Eröffnungsbaum
erstellt ist (*Gemessen an*: ein Chip für jede, ein Klick nimmt sie heraus oder wieder hinein): der Anteil der Partien
entlang des Repertoires, die bis zum Ende seiner Varianten eine Antwort finden, oder bis Zug 10 oder 15. Seine
**Lücken** folgen, die häufigsten zuerst: ein Zug des Gegners, der oft genug ohne deine Antwort gespielt wurde, oder
eine Variante, die mit einem Zug des Gegners endet. Ein Klick zeigt die Stellung, mit dem Baum daneben; die rechte
Maustaste bietet auch ihren Eröffnungsbericht an (siehe [Der Eröffnungsbericht](#der-eröffnungsbericht)).

### Ein Repertoire trainieren
<!-- en: training-a-repertoire 6b0acafe -->

Jede Stellung eines Repertoires, in der du am Zug bist und für die das Repertoire deinen Zug hat, ist eine
Trainingsstellung. RookStudio fragt sie immer wieder ab, in immer größeren Abständen, je öfter du sie weißt, mit FSRS,
der verteilten Wiederholung, die Anki verwendet: Eine richtig beantwortete Stellung kommt nach ein paar Tagen wieder,
dann nach Wochen; eine, die du verfehlst, kommt später in der Sitzung wieder, dann am nächsten Tag.

Die Karte **Trainieren** der Seite Eröffnungen zählt die heute fälligen Stellungen, die neuen von heute und die, die du
weißt und die später fällig sind. *Start · N Stellungen* (Enter) fragt die fälligen Stellungen ab, dann die neuen von
heute; *Nur die neuen* fragt nur die neuen ab. Neue Stellungen kommen in der Reihenfolge, in der deine Datenbanken sie
am häufigsten antreffen, die meistgespielten zuerst, höchstens 20 pro Tag (der Knopf daneben, *20 pro Tag*, ändert das).
Darunter: was morgen, innerhalb von drei Tagen und innerhalb einer Woche wiederkommt.

Eine Sitzung nimmt die Seite Brett ein, deren Spalte zu der des Trainings wird; die Partie, die auf dem Brett war,
wartet unter den letzten Partien und kommt danach zurück. RookStudio spielt die Züge deines Gegners entlang des
Repertoires bis zu jeder Stellung, wo das Repertoire mehrere hat, zuerst die, die deine Datenbanken am häufigsten
spielen. Spiele deinen Zug auf dem Brett oder tippe ihn ein (seine Buchstaben, dann Enter). Während der Frage sind der
Baum, die Engines und deine Notizen ausgeblendet; sie kommen mit der Antwort zurück.

- **Alt+H**: ein Hinweis, das Feld der zu ziehenden Figur in Blau (H allein beginnt einen Zug des h-Bauern).
- **Leertaste**: „Ich weiß es nicht“, und der Zug wird gezeigt.
- **Esc**: anhalten. Was beantwortet wurde, bleibt erhalten; die nächste Sitzung macht mit dem weiter, was noch fällig
  ist.

Jede Antwort wird von selbst bewertet, ohne Knopfdruck: im ersten Versuch richtig, *gut*; richtig nach einem Hinweis
oder nach mehr als 20 Sekunden, *schwer*; falsch oder „Ich weiß es nicht“, *noch einmal*. Eine richtige Antwort zeigt
den Kommentar des Repertoires zum Zug und wann die Stellung wiederkommt; die nächste kommt mit Enter oder von selbst
nach einer Sekunde (das *…* daneben ändert das, und die Maus auf der Antwort hält sie an). Eine falsche zeigt den Zug
deines Repertoires auf seinen Feldern, in Grün: *Noch einmal* (Enter) fragt sie erneut; *Den Baum hier ansehen* zeigt
die Stellung mit dem Tab Baum; *… zum Repertoire hinzufügen* macht den gespielten Zug zu einer zweiten dort akzeptierten
Antwort (Strg+Z nimmt das zurück).

Am Ende: die Zahlen, die verfehlten Züge (ein Klick zeigt die Stellung, mit dem Zug deines Repertoires) und *Die …
verfehlten Stellungen jetzt wiederholen*.

Die Wiederholungen werden im Profil neben dem Repertoire aufbewahrt (`Najdorf.training.json` und
`Najdorf.reviews.jsonl`, eine Zeile pro Antwort): Sein PGN ändert sich nicht. Ein im Repertoire geänderter Zug macht
seine Stellung wieder neu. Nichts verlässt den Computer. Die fälligen Stellungen zählen mit den anderen Arten zur
Wiederholung des Tages (siehe [Die Wiederholung des Tages](#die-wiederholung-des-tages)); dazu *Das Repertoire …
trainieren* in Strg+K. Während eine deiner Partien live auf Lichess gespielt wird, wartet das Training; die Stellungen
auf der Variante einer deiner Fernpartien werden ausgelassen, bis sie endet.

### Theoriestudien
<!-- en: theory-studies 8ba97ca4 -->

Der Teil **Theorie** der Seite Eröffnungen (neben *Repertoires*, in der Zeile ihres Titels) enthält deine
Theoriestudien: kommentierte PGN-Dateien, deine Analysen oder ein Buch als PGN, und Lichess-Studien. Ihre Notizen
erscheinen im Tab Baum der Seite Brett, bei jeder Stellung, die sie kommentieren, egal über welche Zugfolge sie erreicht
wird.

*Ein PGN hinzufügen…* oder ein auf die Seite gezogenes PGN fügt eine Datei hinzu: RookStudio liest sie dort, wo sie
liegt, im Hintergrund, und liest sie von selbst neu, wenn sie sich auf der Festplatte ändert. Die Theorie schreibt nie
hinein, und nichts verlässt den Computer. *Aus einer Lichess-Studie…* fügt eine deiner auf diesem Computer gelesenen
Lichess-Studien hinzu (siehe [Deine Lichess-Studien](#deine-lichess-studien)): Ihre Kopie in deinem Profil wird gelesen,
und mit der Studie neu gelesen.

Was zu einer Notiz wird: jede kommentierte Stellung der Datei, Varianten eingeschlossen, mit den Kommentaren vor und
nach dem Zug, seinen Symbolen (!, ?!, ±…), einer geschriebenen Bewertung (`[%eval]`) und seinen Pfeilen und Feldern; der
Kommentar einer Partie vor ihrem ersten Zug ist eine Notiz zu ihrem Anfang. Die Kommentare des Assistenten werden es
nicht (siehe [Kommentare des Assistenten](#kommentare-des-assistenten)): Sie betreffen den Zug einer Partie, und der
Assistent kommentiert jede Stellung auf Anfrage. Die Kommentare deiner Repertoires erscheinen genauso, unter ihren Namen
(*Auch die Kommentare deiner Repertoires*, unter der Liste).

Jede Studie zeigt ihre Zahlen: die kommentierten Stellungen, die Kommentare, Symbole und Bewertungen. *Notizen im Baum
zeigen* (anfangs an) blendet sie aus oder wieder ein. *Auf dem Brett öffnen* (Enter oder ein Doppelklick in der Liste)
öffnet ihre Partien dort als Kopie: Strg+S speichert eine Partie anderswo, und die Datei bleibt, wie ihr Autor sie
schreibt. *Aus der Theorie entfernen* (Entf) lässt die Datei, wo sie ist, und *Rückgängig* holt sie zurück. Eine
verschobene Datei sagt das, mit *Suchen…*.

Im **Tab Baum** markiert die Spalte *Theorie* die Züge, die deine Studien kommentieren: ihre Symbole und wie viele
Notizen mehr als ein Symbol sagen. Unter den Zügen listet **Was deine Studien sagen** die Notizen der gezeigten Stellung
auf: die Quelle jeder Notiz, wo sie steht (*nach 7.f4*, *auf 7…Qb6* oder *durch Zugumstellung gefunden* mit ihren
eigenen Zügen, wenn sie die Stellung in anderer Reihenfolge erreichen), ihren Text und ihre Variante, die ein Klick auf
dem Brett bei ihrem Zug öffnet. Steht die Maus über einer Notiz zu einem Zug, zeigt sie den Zug als Pfeil des Baums;
über einer Notiz zur Stellung ihre Pfeile und Felder auf dem Brett. Im Tab **Notation** sagt *Theorie · 3* über den
Zügen, wie viele Notizen die Stellung hat, und führt zum Tab Baum (Alt+3). Während eine deiner Partien live auf Lichess
gespielt wird, sind die Notizen mit dem Baum ausgeblendet.

## Die Seite Training
<!-- en: the-training-page 5ebf17d1 -->

Die Seite **Training** versammelt, was heute wiederkommt, dann die Übungen nach Art. Sie steht in der Leiste zwischen
Eröffnungen und Lichess, in den Ansichten *Studium* und *Alles*. Eine Ansicht, die du vorher erstellt hast, ändert sich
nicht von selbst: Die Startseite bietet einmal an, die Seite zur gezeigten Ansicht hinzuzufügen (*Zu … hinzufügen* oder
*Nicht jetzt*), und Strg+K *Training* führt bis dahin dorthin.

Ihr Titel hat sechs Teile: **Heute** (die Wiederholung des Tages, die Arten von Übungen, die zu startenden Aufgaben),
**Aufgaben** (dieselbe Auswahl an Aufgaben, die auf deiner Festplatte aufbewahrte Datenbank von Lichess, deine
Ergebnisse nach Thema), **Meine Fehler** (siehe [Deine Fehler als Übungen](#deine-fehler-als-übungen)), **Züge erraten**
(siehe [Züge erraten](#züge-erraten)), **Endspiele** (siehe [Endspiele gegen die
Tablebases](#endspiele-gegen-die-tablebases) und [Matts](#matts)) und **Visualisierung** (siehe
[Brettvisualisierung](#brettvisualisierung)). Eine Karte für jede Art von Übung führt zu ihrem Teil. Alles funktioniert
offline; was an Lichess geht, steht dort, wo du es wählst.

### Die Wiederholung des Tages
<!-- en: the-days-review b7049c16 -->

*Heute zu wiederholen* zählt, was jede Art von Training heute fällig hat, jede mit ihrer verteilten Wiederholung (FSRS),
in einer Warteschlange: die Fehler deiner Partien (siehe [Deine Fehler als Übungen](#deine-fehler-als-übungen)), die
Stellungen deiner Repertoires (ihr Training bleibt auf der Seite Eröffnungen, siehe [Ein Repertoire
trainieren](#ein-repertoire-trainieren)), die Aufgaben und die Endspiele, die du verfehlt hast. *Alles wiederholen*
(Enter) nimmt sie nacheinander auf der Seite Brett: deine fälligen Fehler, die fälligen Stellungen jedes Repertoires,
die zu wiederholenden Aufgaben, dann die Endspiele. Am Ende jeder Art geht *Als Nächstes: …* (Enter) weiter, *Zurück zur
Seite Training* (Esc) hört dort auf; was beantwortet wurde, bleibt erhalten. Neben dem Knopf die Zeit, die es dauern
sollte, nach dem Tempo deiner Antworten; darunter, was morgen und innerhalb von drei Tagen wiederkommt. Sind Fehler oder
Aufgaben zusammen mit anderen Arten fällig, wiederholen *Nur meine Fehler* und *Nur die Aufgaben* sie allein.

*Laufend* auf der Startseite hat dieselbe Zahl (*35 heute zu wiederholen · Alles wiederholen*); ein Klick wiederholt
alles, ebenso Strg+K *Alles wiederholen*.

### Aufgaben
<!-- en: puzzles 4fd9ffec -->

Lichess veröffentlicht seine Aufgaben jeden Monat als Datenbank (database.lichess.org, CC0): 6.193.530 Aufgaben im
November 2026, jede mit ihrer Wertung, ihren Themen und, vor Zug 20, ihrer Eröffnung. RookStudio bewahrt sie auf deiner
Festplatte auf und gibt dir daraus Aufgaben ohne Netz.

**Die Datenbank.** *Herunterladen* holt die Datei (etwa 295 MB) und liest sie, während sie ankommt, in einen Speicher
von etwa 293 MB im Profil; die Datei selbst wird nicht aufbewahrt. Ein abgebrochener Download (*Abbrechen*, das Netz,
das Programm geschlossen) geht dort weiter, wo er aufhörte: *Fortsetzen* oder *Neu beginnen*. *Eine schon
heruntergeladene Datei…* liest stattdessen `lichess_db_puzzle.csv.zst` von deiner Festplatte. Einmal da: *Aktualisieren*
fragt database.lichess.org nach dem Datum seiner Datei und liest eine neuere, während die aufbewahrten Aufgaben
antworten; *Aufgaben entfernen* gibt die Festplatte frei (deine Wertung, deine Ergebnisse und deine zu wiederholenden
Aufgaben bleiben, getrennt aufbewahrt). Außer der Anfrage nach der Datei verlässt nichts den Computer.

**Die Auswahl.** *Offline* (die Datenbank) oder *Online* (dein Lichess-Konto, siehe [Lichess-Aufgaben
online](#lichess-aufgaben-online)); ein Thema unter den ersten angebotenen, jedes damit, wie oft du es im ersten Versuch
löst, oder unter *Alle Themen…*, das jedes Thema und jede Eröffnung mit ihren Aufgaben auflistet (tippe ein paar
Buchstaben, um eines zu finden); und eine Schwierigkeit: *Leichter* (300 Punkte unter deiner Wertung), *Deine Wertung*,
*Schwerer* (300 darüber). *Start · 20 Aufgaben* (Enter) beginnt eine Serie nahe deiner Wertung, nie mit einer Aufgabe,
die du schon gesehen hast; Strg+K hat *Aufgaben: Gabel* und die anderen Themen, die sofort eine Serie starten.

**Eine Serie** nimmt die Seite Brett ein, deren Spalte zu der der Aufgabe wird, wie beim Training der Repertoires; die
Partie auf dem Brett wartet unter den letzten Partien und kommt danach zurück. Das Brett zeigt die Stellung von deiner
Seite aus, den gerade gespielten Zug des Gegners in Hellgrün. Spiele deinen Zug auf dem Brett oder tippe ihn ein (seine
Buchstaben, dann Enter); RookStudio spielt die Antworten des Gegners, den Rest der Lösung. Während der Frage sind die
Engine, der Baum und deine Notizen ausgeblendet, ebenso die Themen der Aufgabe.

- **Alt+H**: ein Hinweis, das Feld der zu ziehenden Figur in Blau; die Aufgabe zählt dann als verfehlt.
- **Leertaste**: „Ich weiß es nicht“; die Lösung wird gezeigt, und die Aufgabe zählt als verfehlt.
- **Esc**: anhalten; was beantwortet wurde, bleibt erhalten.

Richtig: die Lösung in Zügen, die Änderung deiner Wertung und die Themen der Aufgabe; *Weiter* kommt mit Enter oder von
selbst nach einer Sekunde (die Einstellung des Trainings der Repertoires); *Hier analysieren* (Alt+A) startet die Engine
auf der Stellung; *Die Partie auf lichess.org* öffnet die Partie, aus der die Aufgabe stammt. Jeder Zug, der mattsetzt,
ist richtig, wie auf Lichess. Falsch: dein Zug in Orange und der erste Zug der Lösung als grüner Pfeil; *Noch einmal*
(Enter) spielt sie erneut, ohne erneut zu zählen; *Die Lösung Zug für Zug* (→) spielt sie Zug für Zug; *Weiter* geht
weiter.

Am Ende: die im ersten Versuch gelösten Aufgaben, die verfehlten, die Änderung deiner Wertung und die verfehlten
Aufgaben (ein Klick zeigt die Stellung mit der Lösung); *Die 3 jetzt wiederholen* (so viele wie verfehlt) fragt sie
erneut ab, ohne zu zählen, *Zurück zur Seite Training* verlässt die Serie.

**Deine Wertung** ist die eigene von RookStudio, mit Glicko-2 berechnet, wie Lichess seine Aufgabenwertungen berechnet:
Eine im ersten Versuch ohne Hinweis gelöste Aufgabe ist gegen die Wertung der Aufgabe gewonnen. Sie beginnt bei deiner
Lichess-Aufgabenwertung, wenn dein Konto verknüpft ist, sonst bei 1500, und bewegt sich schnell, solange sie neu ist;
Ergebnisse von Offline-Aufgaben können nicht an Lichess zurückgehen. *Deine Ergebnisse nach Thema* (der Teil Aufgaben)
zählt die Aufgaben jedes Themas und wie oft du sie im ersten Versuch gelöst hast.

Eine verfehlte Aufgabe wird zu einer Karte der Wiederholung des Tages: Sie kommt morgen wieder, dann in größeren
Abständen, je öfter du sie löst; erneut verfehlt, später in derselben Serie, dann am nächsten Tag. All das wird im
Ordner `puzzles` des Profils aufbewahrt (`rating.json`, `results.jsonl` mit einer Zeile pro Aufgabe, `review.json`), die
Datenbank in `lichess-puzzles`.

### Lichess-Aufgaben online
<!-- en: lichess-puzzles-online 51fd9d60 -->

*Online* gibt Aufgaben von deinem Lichess-Konto: Aufgaben, die du nie gesehen hast, auf deiner Lichess-Wertung, deren
Ergebnisse an Lichess zurückgehen. Es braucht dein verknüpftes Konto (siehe [Dein Konto
verknüpfen](#dein-konto-verknüpfen)); beim ersten Mal verlangt Lichess zwei weitere Berechtigungen, *puzzle:read* und
*puzzle:write*: *Auf Lichess erlauben…* öffnet seine Seite, dein Konto bleibt währenddessen verknüpft, und das Token,
das sie ersetzt, wird widerrufen.

*Ein Paket holen · 50 Aufgaben* fragt Lichess nach fünfzig Aufgaben zum gewählten Thema und in der gewählten
Schwierigkeit. Das Paket wird auf deiner Festplatte aufbewahrt: Es wird auch offline gelöst, in beliebig vielen Serien
(*Fortsetzen · noch N*). *Gewertet* oder *Ungewertet, zum Ausprobieren* wird mit jeder Antwort gesendet. Am Ende des
Pakets gehen seine Antworten in einer Anfrage an Lichess, die das nächste Paket bringt; dann bewegt sich deine
Lichess-Wertung, und die Seite sagt, wann die letzten Antworten angenommen wurden. Lichess bittet Programme, seine
Sammlung nicht über diese Anfragen herunterzuladen: Dafür gibt es die Datenbank.

Kann Lichess die Antworten nicht annehmen, bleiben sie auf der Festplatte, und es wird gesagt, was zu tun ist: zu viele
Anfragen (429), eine Minute später von selbst erneut gesendet, auch wenn RookStudio inzwischen geschlossen wurde; das
Netz weg, eine Minute später erneut gesendet; das Token abgelehnt, gesendet, sobald das Konto erneut verknüpft ist.
*Jetzt erneut versuchen* wartet nicht. Was den Computer verlässt: die Anfrage nach dem Paket (sein Thema oder seine
Eröffnung, seine Schwierigkeit), dann die ID jeder Aufgabe mit richtig oder falsch, gewertet oder nicht.

### Deine Fehler als Übungen
<!-- en: your-mistakes-as-exercises b0e75f68 -->

Die Fehler deiner eigenen Partien kommen als Übungen zurück: die Stellung vor deinem Zug, um Besseres zu finden. Deine
Partien sind die, in denen du unter einem deiner Namen spielst (siehe [Deine Namen](#deine-namen)), in deinen
Datenbanken. Jede von ihnen, die eine Auswertung hat (siehe [Die Auswertung einer
Partie](#die-auswertung-einer-partie)), liefert die Züge deiner Seite, die die Auswertung mit **?** und **??** markiert
hat, wo ein besserer Zug klar ist: Die Auswertung hat die Stellung tief genug durchsucht, um ihren besten Zug zu nennen
(mindestens ihr dritter Durchgang), und die Partie war nicht ohnehin entschieden (der beste Zug der Auswertung hält dich
in der Partie; dein Zug hat keinen Gewinn gehalten). Ungenauigkeiten (?!) werden ausgelassen: Sie haben selten eine
einzige gute Antwort.

Der Teil **Meine Fehler** zählt die Übungen und die Partien, aus denen sie stammen, die heute fälligen, die neuen und
die, die du kannst, später fällig. Ihre Arten stammen aus *Woran zu arbeiten ist* jeder Auswertung: Gabeln, hängen
gelassene Figuren, Turmendspiele…; ein Chip für jede beschränkt die Liste und die Sitzung auf diese Art, *Alle Arten*
nimmt sie alle. Die Liste nennt für jede Übung die Partie, wo und wann sie gespielt wurde, deinen Zug und den der
Auswertung und wann sie wiederkommt. *Start · N Übungen* (Enter) fragt die fälligen ab, dann die neuen von heute (so
viele pro Tag wie die neuen Stellungen der Repertoires).

Deine letzten Partien ohne Auswertung werden unter der Liste genannt: *Auswerten · im Hintergrund* wertet sie
nacheinander aus, so wie die Seite Brett eine Partie auswertet (ihre Engine, ihre Einstellungen, die neben einem Turnier
übrig gelassenen Prozesse), jede Auswertung mit ihrer Partie aufbewahrt; ihre Übungen kommen Partie für Partie. Der
Fortschritt, die verbleibende Zeit und *Abbrechen* erscheinen währenddessen; die Auswertungen warten, solange die Seite
Brett analysiert.

**Eine Übung** nimmt die Seite Brett ein, deren Spalte zu der der Übung wird, wie beim Training der Repertoires: die
Partie bis zur Stellung, von deiner Seite aus, der letzte Zug des Gegners in Hellgrün; unter dem Brett, wann sie
gespielt wurde und was du damals gespielt hast. Spiele deinen Zug auf dem Brett oder tippe ihn ein. Der beste Zug der
Auswertung ist sofort richtig; jeder andere Zug wird von der Engine der Auswertung mit den Einstellungen der Auswertung
durchsucht (ein Thread, ihre Knotenzahl, ab einer leeren Tabelle), und er ist richtig, wenn er höchstens 0,3 Bauern
gegenüber dem besten der Auswertung verliert oder einen Gewinn hält, den die Auswertung nicht markieren würde. Die
Engine, der Baum und deine Notizen sind während der Frage ausgeblendet.

- **Alt+H**: ein Hinweis, das Feld der zu ziehenden Figur in Blau.
- **Leertaste**: „Ich weiß es nicht“; der Zug der Auswertung wird gezeigt.
- **Esc**: anhalten; was beantwortet wurde, bleibt erhalten.

Jede Antwort wird von selbst bewertet, wie eine Stellung eines Repertoires: im ersten Versuch richtig, *gut*; richtig
nach einem Hinweis oder nach mehr als 20 Sekunden, *schwer*; falsch, *noch einmal*. Richtig: die Variante der Auswertung
und die deiner Partie, was die Auswertung über deinen Zug sagte und wann die Übung wiederkommt; *Die Partie bei diesem
Zug öffnen* öffnet sie auf der Seite Brett. Falsch: dein Zug in Orange, der Zug der Auswertung als grüner Pfeil und was
die Engine nach deinem fand; *Noch einmal* (Enter) spielt sie erneut, ohne erneut zu zählen; eine verfehlte Übung kommt
ein paar Übungen später wieder, dann am nächsten Tag.

Die fälligen Übungen zählen zur Wiederholung des Tages (siehe [Die Wiederholung des
Tages](#die-wiederholung-des-tages)), *Nur meine Fehler* wiederholt sie allein; Strg+K hat *Meine Fehler wiederholen*
und *Übungen aus meinen Fehlern*. Eine Stellung, die dein Repertoire trainiert, wird hier nicht übernommen: Sie wird auf
der Seite Eröffnungen trainiert. Eine aus ihrer Datenbank entfernte Partie nimmt ihre Übungen mit. Die Übungen und ihre
Karten werden im Ordner `mistakes` des Profils aufbewahrt (`exercises.json` und `answers.jsonl`, eine Zeile pro
Antwort); deine Partien ändern sich nicht, und nichts verlässt den Computer. Was deine Partien sagen, ordnet auch die
Themen der Aufgaben: Die Arten deiner Fehler des letzten Monats kommen unter ihnen zuerst, unter den Chips genannt.

### Deine Namen
<!-- en: your-names 4a0ddfdd -->

*Einstellungen › Deine Namen* (oder der Teil *Meine Fehler*, solange es keinen Namen gibt) listet die Namen auf, unter
denen du spielst, so wie deine Datenbanken sie schreiben: Eine Partie, in der einer von ihnen spielt, ist deine. Ein
Name passt über seine ganzen Wörter, Groß- und Kleinschreibung und Akzente außer Acht gelassen: *Tester* ist *Tester,
Sam* und *TESTER, S.*, *Sam Tester* ist *Tester, Sam*, aber *Test* ist niemand. Der Name deines verknüpften
Lichess-Kontos zählt von selbst. Klicke auf einen Namen, um ihn zu entfernen: Die Übungen seiner Partien gehen, aber
ihre Karten bleiben erhalten und kommen mit ihnen zurück, wenn du den Namen wieder hinzufügst.

### Züge erraten
<!-- en: guessing-the-moves e004de63 -->

**Züge erraten** spielt eine Partie deiner Datenbanken von einer Seite aus nach: Bei jedem ihrer Züge errätst du den
gespielten. Die Engine bewertet jeden Tipp gegen den Zug der Partie: *wie in der Partie* 3 Punkte; laut Engine *gleich
gut* (höchstens 0,2 Bauern schlechter, oder besser) 2; *spielbar* (höchstens 0,5 Bauern schlechter) 1; sonst keinen. Es
ist die Analyse-Engine, mit einem Thread, eine Sekunde pro Zug; der Zug der Partie wird durchsucht, während du
nachdenkst.

*Eine zufällige Partie* zieht eine aus der gewählten Datenbank, über einen Filter: den Schnellfilter der Partieliste
(Spieler, Veranstaltung, ein ECO-Code, ein Jahr) und die Elo der Spieler. Wähle deine Seite, dann *Start · eine
zufällige Partie* (Enter). Mit *Die Theoriezüge laufen von selbst durch* (für das nächste Mal gespeichert) beginnt das
Raten dort, wo die Partie die Züge verlässt, die viele Partien deiner Datenbanken gespielt haben (ihr Eröffnungsbaum).
*Die Partie der Seite Brett* lässt die gezeigte Partie ab ihrer Stellung erraten, für die Seite am Zug: Strg+K *Züge
erraten* auf der Seite Brett.

Die Partie nimmt die Seite Brett ein, deren Spalte zu der des Ratens wird; die Partie wird nicht verändert, und ihre
späteren Züge werden nicht gezeigt. Nach jedem Tipp: dein Zug als blauer Pfeil, der der Partie als grüner, die Punkte
und beide Bewertungen; die Partie geht nach einem Moment von selbst weiter (sofort mit Enter), die Antwort des Gegners
wird gespielt, und der nächste Zug gehört dir. Darunter: jeder Tipp mit seinen Punkten und der bisherige Stand. Am Ende:
deine Punkte, wie viele Tipps der Zug der Partie waren, die im Schnitt pro Zug verlorenen Bauern und die Züge, die dich
am meisten gekostet haben (ein Klick zeigt ihre Stellung); die ganze Partie steht auf dem Brett, ← und → gehen durch
sie. Jede geratene Partie wird im Ordner `guess` des Profils aufbewahrt; die Seite Training zeigt die Punkte der
letzten.

### Endspiele gegen die Tablebases
<!-- en: endgames-against-the-tables 6ded57cf -->

Der Teil **Endspiele** ist ein Katalog von Endspielen zum Gewinnen oder Halten, gespielt gegen die Tablebases: achtzehn
in vier Gruppen. *Elementare Mattführungen*: die Dame, der Turm, die zwei Läufer, Läufer und Springer. *Bauern*: die
Opposition, die Quadratregel, der entfernte Freibauer, das Réti-Manöver. *Türme*: Lucena, Philidor, Vancura, Turm gegen
Bauer, Turm und zwei Bauern gegen Turm. *Figuren und Dame*: Dame gegen Turm, Dame gegen Bauer, der falsche Läufer, Turm
gegen Läufer, Läufer gegen Springer. Jedes sagt, was es verlangt (*Matt*, *Gewinn* oder *Remis halten*) und wo du
stehst: *zu tun*, *geschafft*, bei einem Matt *geschafft in 9 (die Tablebases: 7)*, *erneut zu tun* nach einem Fehler.
Eines, das die verfügbaren Tablebases nicht beantworten können, ist ausgegraut, seine Steine genannt (siehe [Tablebases
für die Endspiele](#tablebases-für-die-endspiele)); steht die Maus darüber, erscheint die Idee, die es gewinnt oder
hält. Ein Klick spielt es; danach wählen ↑ und ↓ ein anderes, und Enter spielt es. Strg+K hat *Endspiel: Lucena* und die
anderen.

**Ein Endspiel** nimmt die Seite Brett ein, deren Spalte zu der des Endspiels wird, wie bei den Aufgaben; die Partie auf
dem Brett wartet unter den letzten Partien. Du spielst von deiner Seite aus; die Tablebases beantworten jeden Zug mit
dem, der am längsten Widerstand leistet (mit den Syzygy-Tablebases der längste bis zum nächsten Schlagen oder Bauernzug,
und unter gleich langen der, der dir die wenigsten guten Antworten lässt). Über der Frage: das Urteil der Tablebases
jetzt (*Die Tablebases: Weiß gewinnt*) und wie weit, die Züge bis zum Matt, wenn Lichess sie kennt, sonst höchstens die
Züge vor einem Schlagen oder Bauernzug.

- **Strg+Z**: deinen letzten Zug und die Antwort der Tablebases zurücknehmen.
- **Alt+H**: der richtige Zug, als grüner Pfeil; das Endspiel zählt dann als mit Hilfe geschafft.
- **Neu beginnen**: zurück zum Anfang. **Esc**: anhalten.

Jeder Zug wird sofort beurteilt. Einer, der einen Gewinn in ein Remis oder ein Remis in eine Niederlage verwandelt, ist
ein Fehler: Die Tablebases antworten, dann sagt es die Spalte (*1.Kd5? Ke7: jetzt Remis*) mit den Zügen, die das
Ergebnis gehalten hätten (*Den Gewinn hätten gehalten: 1.Kd6!, 1.Kf6!*, ihre Felder in Grün, dein Zug in Orange), und
der Idee des Endspiels. *Zurücknehmen* (Enter oder Strg+Z) spielt ihn erneut; *Den Gewinn zeigen* nimmt ihn zurück und
schreibt die Variante der Tablebases ab dort; *Trotzdem weiter* spielt im neuen Urteil weiter. Darunter das Urteil zu
Beginn und jeder Zug, der es geändert hat.

Ein Gewinn zählt beim Matt oder sobald ein Bauer von dir zur Dame wird oder der andere König allein steht, der Gewinn
gehalten; in den elementaren Mattführungen ist das Matt selbst das Ziel, deine Züge gemessen am Matt der Tablebases ab
dem Anfang. Ein Remis zählt, wenn eine Regel die Partie remis macht (Patt, zu wenig Material, die dreifache
Wiederholung, die fünfzig Züge), oder nach 15 gehaltenen Zügen. Am Ende: *Noch einmal* (Enter), *Als Nächstes:* das
nächste noch nicht geschaffte Endspiel, *Zurück zur Seite Training*.

Die Stellung vor einem Fehler wird zu einer Karte der Wiederholung des Tages (siehe [Die Wiederholung des
Tages](#die-wiederholung-des-tages)): Sie kommt morgen wieder, das Endspiel ab ihr erneut gespielt, dann in größeren
Abständen, je öfter du es ohne Fehler gewinnst oder hältst (mit Hilfe zählt es als schwer, ein Fehler als verfehlt).
Strg+K *Die verfehlten Endspiele wiederholen*.

**Aus deinen Partien** nimmt die Stellungen mit 7 Steinen oder weniger unter deinen Fehlern (siehe [Deine Fehler als
Übungen](#deine-fehler-als-übungen)), in denen die Auswertung deiner Partie den Gewinn oder das Remis entgleiten sah.
*Start* spielt sie nacheinander gegen die Tablebases, jede ab der Stellung vor deinem Zug; eine, die laut den Tablebases
ohnehin verloren war, wird ausgelassen. Die Ergebnisse und die Karten werden im Ordner `endgames` des Profils aufbewahrt
(`record.json`).

### Tablebases für die Endspiele
<!-- en: tables-for-the-endgames 0e49bbca -->

Die Tablebases sind die deiner Festplatte, die Syzygy-Ordner der Turniere (*Abschätzung* > Tablebases, siehe [Tablebases
auf der Seite Brett](#tablebases-auf-der-seite-brett)), mit den eigenen des Profils, sobald sie heruntergeladen sind;
für mehr Steine, als sie enthalten, die von Lichess online, bis 7 Steine, mit deiner Zustimmung zu den Online-Tablebases
(der der Seite Brett, siehe [Online für die Analyse](#online-für-die-analyse)). Während ein Turnier im Prozess des
Programms spielt, gehören die Tablebases der Festplatte ihm: Dann antworten die von Lichess, oder das Endspiel wartet.

Braucht ein Endspiel Tablebases, die du nicht hast, sagt der Teil das (*Keine Tablebases für „Lucena“*) und bietet drei
Wege an:

- **Die Tablebases für 3 bis 5 Steine herunterladen** (290 Dateien, 938 MB) vom Spiegelserver von Lichess, eine Datei
  nach der anderen, die kleinsten zuerst: Die Endspiele mit 3 und 4 Steinen sind in Sekunden bereit, die anderen, sobald
  ihre Dateien ankommen. Ein abgebrochener Download geht dort weiter, wo er aufhörte (*Fortsetzen*); jede Datei wird
  geprüft (ihre Größe und die Kennung von Syzygy). Die Tablebases kommen in den Ordner `syzygy` des Profils und, sobald
  alle da sind, in die Ordner der Turniere.
- **Ein Ordner mit Tablebases, schon auf der Festplatte…**: auch für die Turniere gewählt.
- **Die von Lichess, online**: bis 7 Steine, eine Anfrage pro gespielter Stellung; die Wahl gibt die Zustimmung.

Ohne Tablebases sind die Endspiele, die sie brauchen, ausgegraut, jedes mit dem, was es braucht; die anderen Übungen
funktionieren. Online lässt eine Unterbrechung das Endspiel warten: Dein Zug bleibt erhalten und wird beurteilt, wenn
das Netz zurückkommt (*Jetzt erneut versuchen* wartet nicht); ein 429 wartet die Minute, um die Lichess bittet. Was den
Computer verlässt: mit den Tablebases der Festplatte nichts; mit denen von Lichess jede gespielte Stellung als FEN; für
den Download die Anfrage nach jeder Datei.

### Matts
<!-- en: mates 803d99bd -->

Die Karte **Matts** (im Teil Endspiele, zu dem die Karte des Teils Heute führt) startet 20 Mattaufgaben von Lichess
(siehe [Aufgaben](#aufgaben)): in 1 bis 5 Zügen oder nach Muster (Grundreihe, erstickt, Anastasia, arabisch, Boden,
Doppelläufer, Haken), jeder Chip damit, wie oft du es im ersten Versuch löst. Sie kommen aus der Datenbank auf deiner
Festplatte, sonst aus einem Paket deines Lichess-Kontos; ohne beides führt der Klick zum Teil Aufgaben. Die elementaren
Mattführungen gegen die Tablebases stehen im Katalog oben, deine Züge gemessen am Matt der Tablebases.

### Brettvisualisierung
<!-- en: board-vision dd34b350 -->

Der Teil **Visualisierung** trainiert, das Brett im Kopf zu sehen. Nichts verlässt den Computer; die Rekorde werden in
`vision.json` im Profil aufbewahrt.

**Koordinaten**: dreißig Sekunden Felder. *Das Feld finden*: Ein Feld wird genannt, klicke es auf einem Brett ohne seine
Buchstaben und Ziffern an; *Das Feld benennen*: Ein Feld ist blau markiert, tippe seinen Namen ein (einen Buchstaben,
dann eine Ziffer). Aus Sicht von Weiß, von Schwarz oder *Zufällig*, wobei sich das Brett von Feld zu Feld dreht. Ein
Fehler zeigt das gefragte Feld in Grün und deines in Orange. Am Ende deine Zahl und dein Rekord für diese Art und Seite.
Enter startet, Esc beendet.

**Blind**: eine Partie gegen die Engine der Seite Spielen, in der Stärke und mit den Eröffnungen, die dort gewählt sind
(*Auf der Seite Spielen ändern*, siehe [Gegen eine Engine](#gegen-eine-engine)). Du spielst Weiß, Schwarz oder zufällig
eines von beiden; das Brett verbirgt alle Figuren, alle Figuren ab dem 10. Zug oder alle außer den Bauern, der letzte
Zug markiert. Tippe deine Züge oder spiele sie auf dem Brett; die bisherigen Züge stehen in der Spalte. Halte Umschalt
einen Moment, um die Figuren 2 Sekunden lang zu sehen, dreimal pro Partie; F2 fragt *Wo steht…?* nach einer Figur, die
ihre Worte allein bezeichnen (*der schwarzfeldrige Läufer von Schwarz*): Klicke auf ihr Feld oder tippe es ein. Deine
Uhr ist lang, die der Engine kurz; jede Partie wird an `games/blindfold.pgn` im Profil angehängt.

**Eine Variante im Kopf nachspielen**: eine Stellung einer bekannten Eröffnung, die sechs folgenden Züge daneben
geschrieben, ohne dass sich die Figuren bewegen, dann eine Frage: Welche Figur steht auf einem Feld, das sie verändert
haben? Klicke die Antwort an oder tippe die Figur so, wie FEN sie schreibt (*N* ein weißer Springer, *n* ein schwarzer,
*-* ein leeres Feld); das Brett zeigt dann die Stellung nach den Zügen. Zehn Varianten pro Serie.

## Die Seite Spielen
<!-- en: the-play-page 7168b30b -->

Die Seite **Spielen** hat zwei Teile: *Gegen eine Engine*, eine auf der Seite Brett gespielte Partie gegen eine Engine
deiner Bibliothek, und *Auf Lichess*, deine von RookStudio aus gespielten Lichess-Partien (siehe [Auf
Lichess](#auf-lichess)) und die Arenen und Turniere nach Schweizer System von Lichess (siehe [Arenen und Turniere nach
Schweizer System](#arenen-und-turniere-nach-schweizer-system)); der zuletzt gezeigte Teil kommt wieder. Sie steht in der
Leiste nach der Seite Training, in den Ansichten *Studium* und *Alles*. Eine Ansicht, die du vorher erstellt hast,
ändert sich nicht von selbst: Die Startseite bietet einmal an, sie zur gezeigten Ansicht hinzuzufügen (*Zu … hinzufügen*
oder *Nicht jetzt*), und Strg+K *Gegen eine Engine spielen* (oder *Auf Lichess spielen*) führt bis dahin dorthin, ebenso
*Gegen eine Engine spielen* auf der Startseite und *Die Seite Spielen* in der Karte *Eine Partie spielen…* der Seite
Brett. Gegen eine Engine verlässt nichts den Computer: Die Engine läuft auf ihm. Während eine deiner Partien live auf
Lichess gespielt wird, sagt die Seite das und startet keine Partie gegen eine Engine (siehe [Fairplay](#fairplay)).

### Gegen eine Engine
<!-- en: against-an-engine 1cf4f880 -->

Die Wahl wird für die nächste Partie gespeichert: Einmal getroffen, genügt *Spielen* (Enter).

- **Der Gegner**: eine Engine deiner Bibliothek, mit den Stärken, die sie annehmen kann.
- **Ihre Stärke**: die Elo, die sie anstrebt (`UCI_Elo`, mit `UCI_LimitStrength`), auf ihrer eigenen Skala: ein
  Anhaltspunkt, keine FIDE-Wertung. Eine Engine ohne diese Einstellung spielt stattdessen eine Anzahl Knoten pro Zug,
  von 1 bis zu einer Million, weniger für ein schwächeres Spiel. *Volle Stärke* lässt sie in Bestform, was auch immer
  ihre Karte begrenzt.
- **Ihr Stil: ihre Eröffnungen**, was sie vor ihren eigenen Zügen spielt: *Ihre eigenen* (kein Buch); *Deine
  Datenbanken* (die dort am häufigsten gespielten Züge, in all deinen Datenbanken mit Eröffnungsbaum oder in einer
  davon); *Dein Repertoire* (die Züge, gegen die du dich vorbereitet hast, dann die Engine; du spielst die Farbe des
  Repertoires, die seine Wahl einstellt); *Partien eines Spielers* (die Züge, die ein Spieler mit der Farbe der Engine
  gespielt hat, aus einer Datenbank namens *Lichess · Name*, wie sie das Vorbereiten einer Partie gegen ihn anlegt,
  siehe [Eine Partie vorbereiten](#eine-partie-vorbereiten)); *Ein Buch der Bibliothek* (ein Polyglot- oder CTG-Buch der
  Seite Bücher, ohne seine zu vermeidenden Züge). *Variieren* wählt zufällig unter den mindestens 5-mal gespielten
  Zügen, so oft, wie sie gespielt wurden (die Züge eines Buchs so oft, wie seine Gewichte es sagen, die eines
  Repertoires gleich oft); nicht angehakt, der am häufigsten gespielte Zug, bei einem Buch zuerst sein Hauptzug. Sobald
  ihre Eröffnungen für die Stellung keinen Zug haben, spielt die Engine bis zum Ende. Ihr Stil sind ihre Eröffnungen:
  Die eigenen Einstellungen der Engine bleiben auf ihrer Karte. Mit einer Vorgabe treffen die Eröffnungen auf
  Stellungen, die sie nicht haben, und die Engine spielt ab dem ersten Zug.
- **Du**: Weiß, Schwarz oder zufällig; die Uhr, Minuten und ein Inkrement, *Für beide gleich* oder, nicht angehakt, eine
  eigene für die Engine (ungleiche Zeit); ab der Ausgangsstellung oder ab der der Seite Brett.
- **Die Vorgabe**: was die Engine zu Beginn vorgibt: nichts, ihren f-Bauern, ihren b-Springer, ihren a-Turm (die Rochade
  auf dieser Seite fällt mit ihm weg) oder ihre Dame; ein Stein, den sie dort, wo sie beginnt, nicht hat, ist
  ausgegraut, und Chess960 hat keine. *Zugrücknahme* (erlaubt, außer nicht angehakt) und *Hinweise* (3 pro Partie) sind
  ebenfalls Vorgaben, gezählt.

Die Partie zählt zum Kernbudget (die Threads der Engine, wie bei einer vom Brett aus gespielten Partie); die Seite sagt,
wenn die Maschine voll ist.

### Die Partie gegen die Engine
<!-- en: the-game-against-an-engine bfdb25dc -->

*Spielen* bringt die Partie auf die Seite Brett, in einer Ansicht fürs Studium: das Brett zu deiner Seite gedreht, die
Uhren laufen in den Streifen der Spieler (der der Engine mit ihrer Stärke), und über der Spalte die Karte der Partie:
wer am Zug ist, *Remis anbieten*, *Aufgeben…*, *Zurücknehmen · Strg+Z*, *Hinweis · Alt+H*, *Stopp*.

- Spiele deine Züge auf dem Brett oder tippe sie ein: Der erste Buchstabe eines Zugs öffnet das Feld, Enter spielt ihn.
- **Die Hilfsmittel sind bis zum Ende ausgeblendet**: die Tabs Analyse, Baum, Referenzen und Fragen, die Analyse
  (Alt+A), die Drohung, die Analyse der Partie (Alt+G), die Tablebases und der Assistent. Auch die Engine zeigt nichts
  von ihrer Suche: Ihre Bewertungen kommen mit der beendeten Partie. *Zeigen*, in der Karte oder anstelle eines Tabs,
  gibt sie während der Partie zurück, die dann als *mit Hilfsmitteln* markiert wird.
- *Zurücknehmen* (Strg+Z) nimmt deinen letzten Zug und die Antwort der Engine zurück, oder deinen Zug, während die
  Engine nachdenkt, die Uhren wie zuvor: gezählt und in der Partie vermerkt.
- *Hinweis* (Alt+H): wenn du am Zug bist, der Zug der Analyse-Engine nach einer Sekunde Suche, als grüner Pfeil; drei
  pro Partie, gezählt und in der Partie vermerkt.
- *Remis anbieten*: Die Engine antwortet mit ihrem nächsten Zug. Sie nimmt das Remis an, wenn sie mattgesetzt wird oder
  klar schlechter steht oder wenn sie nicht besser steht, sobald 20 Züge gespielt sind; ein Zug aus ihren Eröffnungen
  lehnt es ab.
- *Aufgeben…* verlangt innerhalb weniger Sekunden Enter oder einen zweiten Klick; *Stopp* beendet die Partie ohne
  Ergebnis, so gespeichert, wie sie ist.
- Wo die Eröffnungen der Engine endeten, sagt es die Notation (*Ende deines Repertoires · dann die Engine*).

Das Schließen von RookStudio während der Partie bewahrt sie auf: Beim nächsten Start ist sie wieder auf der Seite Brett,
ihre Uhren warten, bis du sie fortsetzt (*Fortsetzen*, Enter) oder beendest (*Beenden*). Eine inzwischen auf dem Brett
geöffnete andere Partie lässt deine weiterlaufen, deine Uhr läuft, wenn du am Zug bist: *Zurück zur Partie* holt sie
zurück.

### Nach der Partie
<!-- en: after-the-game 44d80f6b -->

Am Ende kommen die Hilfsmittel zurück, und die Karte sagt, wie die Partie endete (*Remis: dreifache
Stellungswiederholung, bei Zug 34*), gegen wen und wie: die Stärke, deine Farbe, die Uhr, die Zugrücknahmen, die
Hinweise, mit oder ohne Hilfsmittel. Die Partie wird in der Datenbank **Meine Partien gegen Engines** gespeichert, die
bei der ersten Partie angelegt wird, mit ihren Einstellungen in ihren Tags: `Strength` (`UCI_Elo 1800`, `nodes 1000`,
`full`), `Openings` (`repertoire Najdorf`, `database Mine`, `databases`, `player Somebody`, `book Human`, `engine`),
`Handicap` (`knight b8`, mit der Ausgangsstellung), `Takebacks`, `Hints`, `Aids`. Strg+S speichert sie nach einer
Änderung wieder dorthin.

- *Auswerten* (Enter): die Analyse der Partie, wie Alt+G sie macht (siehe [Die Auswertung einer
  Partie](#die-auswertung-einer-partie)); sobald sie ausgewertet ist, werden deine Fehler darin zu Übungen (siehe [Deine
  Fehler als Übungen](#deine-fehler-als-übungen)), wenn deine Namen sagen, dass die Partie deine ist (siehe [Deine
  Namen](#deine-namen)).
- *Revanche, mit vertauschten Farben*: dieselbe Wahl, die andere Farbe.
- *Einstellungen ändern*: die Seite Spielen.
- *Schließen*: Die Partie bleibt wie jede Partie auf dem Brett.

Darunter **deine Partien gegen diese Engine auf dieser Stufe**: gewonnen, remis und verloren, die mit Hilfsmitteln
getrennt und nicht gezählt, und wo du die letzte verloren hast (*ein Turmendspiel*), mit der Angabe, ob sie unter deinen
Übungen ist. Die nächste Stufe wird dort oder auf der Seite Spielen gewählt: Nichts steigt von selbst. Jede Partie ist
außerdem eine Zeile von `play/games.jsonl` im Profil.

### Auf Lichess
<!-- en: on-lichess 37d530b3 -->

Der Teil **Auf Lichess** der Seite Spielen spielt deine Lichess-Partien aus RookStudio, über die Board-API von Lichess,
die für Menschen gemacht ist. Er hat zwei Hälften, *Partien*, hier beschrieben, und *Turniere* (siehe [Arenen und
Turniere nach Schweizer System](#arenen-und-turniere-nach-schweizer-system)); die zuletzt gezeigte kommt wieder. Er
braucht dein verknüpftes Konto (siehe [Dein Konto verknüpfen](#dein-konto-verknüpfen));
beim ersten Mal verlangt Lichess eine weitere Berechtigung, *Deine Partien spielen* (`board:play`): *Auf Lichess
erlauben…* (Enter) öffnet die Seite von Lichess, dein Konto bleibt währenddessen verknüpft, und das Token, das sie
ersetzt, wird widerrufen. RookStudio spielt nie einen Zug, den du nicht gespielt hast. Was den Computer verlässt: deine
Partiegesuche und Herausforderungen, deine Züge, deine Chatnachrichten, Remisangebote und Aufgaben, mit dem Token deines
Kontos; die Wahl wird in `play/lichess.json` im Profil gespeichert.

- **Eine Partie suchen**: Schnellschach (10+0, 10+5, 15+10), klassisch (30+0, 30+20) oder Fernschach (1 bis 14 Tage pro
  Zug), gewertet oder nicht, gegen Gegner jeder Wertung oder mit einer Wertung um deine in dieser Bedenkzeit (±).
  Lichess behält Blitz und Bullet seiner Website vor: Ein Programm sucht sie nicht, und Blitz bleibt als direkte
  Herausforderung möglich. *Einen Gegner suchen* (Enter) stellt das Partiegesuch in die Lobby von Lichess, sobald der
  Ereignisstrom von Lichess offen ist, wie Lichess es verlangt, damit der Beginn der Partie nicht verpasst wird. Ein
  Live-Gesuch lebt, solange RookStudio es offen hält: Esc zieht es zurück, ebenso das Schließen von RookStudio; die
  gefundene Partie öffnet sich auf der Seite Brett. Ein Fernschachgesuch bleibt in der Lobby, bis jemand es annimmt (die
  API kann es nicht zurückziehen, die Lobby von lichess.org schon), und seine Partie erscheint unter deinen laufenden
  Partien.
- **Einen Spieler herausfordern**: sein Name, die Bedenkzeit (3+2, 5+3, 10+5, 15+10, 3 Tage oder *andere…*: Minuten und
  Sekunden Zuschlag pro Zug oder Tage pro Zug; mindestens drei Minuten, das Inkrement vierzigmal gezählt, da die
  Board-API kein Bullet spielt), gewertet oder nicht, deine Farbe. Eine Live-Herausforderung wartet auf die Antwort,
  solange du bleibst (Esc oder *Zurückziehen*). Abgelehnt wird der Grund von Lichess genannt (*zu schnell für ihn*), mit
  anderen Bedenkzeiten einen Klick entfernt (langsameren nach „zu schnell“); angenommen öffnet sich die Partie auf der
  Seite Brett.
- **Eine empfangene Herausforderung** erscheint oben im Teil (*Annehmen* · Enter, *Ablehnen ▾* mit den Gründen von
  Lichess: *Nicht jetzt*, *Später*, *Zu schnell für mich*, *Zu langsam für mich*, *Nur ungewertet*, *Nur gewertet*), auf
  der Startseite unter *Laufend* und in einem Hinweis in der Ecke des Fensters, wo immer du bist, mit einem Ton, wenn
  die Töne eingeschaltet sind; *Später* legt den Hinweis beiseite. Während einer Live-Partie wartet sie auf deren Ende.
  Eine Partie, die die Board-API nicht spielen kann (Bullet, eine andere Variante als Chess960), wird auf lichess.org
  gespielt.
- **Deine laufenden Partien**: die, in denen du am Zug bist, zuerst, mit der verbleibenden Zeit für den Zug; ein Klick
  öffnet eine auf der Seite Brett, wo dein Zug gespielt wird.

Der Ereignisstrom ist der von [Fairplay](#fairplay): Lichess öffnet einen pro Token, und er meldet Beginn und Ende der
Partien, die empfangenen Herausforderungen und was aus denen wird, die du gesendet hast.

### Eine Partie auf Lichess
<!-- en: a-game-on-lichess 15ac0e49 -->

Die Partie wird auf der Seite Brett gespielt, in einer Ansicht fürs Studium: das Brett zu deiner Seite gedreht, die
Wertungen und Uhren der Spieler in ihren Streifen, und über der Spalte die Karte der Partie: was sie ist (*Gewertet ·
Schnellschach 10+5 gegen …*), wer am Zug ist, *Remis anbieten*, *Aufgeben…* (innerhalb weniger Sekunden Enter oder ein
zweiter Klick), *Um Zugrücknahme bitten*, *Partie abbrechen* (bevor beide Seiten gezogen haben), *Chat* (der Chat der
Spieler), *Auf lichess.org öffnen*.

- Live gehen deine Züge los, sobald sie gespielt sind, auf dem Brett oder getippt (der erste Buchstabe eines Zugs öffnet
  das Feld, Enter spielt ihn). Ein während des Zugs deines Gegners gemachter Zug wartet auf deinen (ein gestrichelter
  Pfeil) und wird dann gespielt, wenn er legal ist; Esc nimmt ihn zurück. Vor deinem ersten Zug zählt die Karte die Zeit
  herunter, die Lichess gibt, bevor es die Partie abbricht.
- Im Fernschach wartet ein Zug auf deine Bestätigung (*Dein Zug: 10.g4 · noch nicht gesendet*, ein Pfeil auf dem Brett):
  *Senden* (Enter) oder *Zurücknehmen* (Esc). Die Karte listet deine anderen Fernpartien auf, einen Klick entfernt.
- Das Remisangebot oder die Bitte um Zugrücknahme deines Gegners erscheint in der Karte mit *Annehmen* und *Ablehnen*;
  deines wartet auf seine Antwort. Verlässt dein Gegner die Partie, zählt die Karte herunter, bis Lichess dich den Sieg
  beanspruchen lässt (*Sieg beanspruchen*, *Remis beanspruchen*).
- **Die Hilfsmittel**: Die Sperre aus [Fairplay](#fairplay) gilt, wie für jede deiner Partien. Live ist jedes
  Hilfsmittel für jede Stellung gesperrt: die Tabs Analyse, Baum, Referenzen und Fragen, die Analyse, die Drohung, die
  Tablebases, die Cloud und der Assistent. Im Fernschach sind die Engine, die Cloud, die Tablebases und der Assistent
  für die Partie gesperrt; der Baum deiner Datenbanken und deine Bücher bleiben offen, da Lichess dort ein Buch erlaubt.
- **Eine Unterbrechung**: Die Karte sagt das (*deine Uhr läuft auf Lichess*) und versucht es von selbst erneut, nach 1,
  2, 3, dann alle 5 Sekunden; die ganze Partie kommt mit der Verbindung zurück, der Chat auch. Ein inzwischen gespielter
  Zug geht nicht durch: Spiele ihn erneut, sobald die Verbindung zurück ist. *Die Partie auf lichess.org öffnen* bleibt
  zur Hand.
- **Das Schließen von RookStudio** während einer Live-Partie fragt zuerst nach (*Bleiben* · Esc, *Trotzdem schließen*):
  Die Partie geht auf Lichess weiter, deine Uhr läuft. Beim nächsten Start kommt eine noch laufende Partie auf die Seite
  Brett zurück, und die Startseite zeigt sie zuerst. Eine inzwischen auf dem Brett geöffnete andere Partie lässt deine
  weiterlaufen: *Zurück zur Partie* holt sie zurück.
- Am Ende sagt die Karte, wie die Partie endete und was sie an deiner Wertung geändert hat; die Partie kommt in deine
  Lichess-Datenbank, mit der Analyse von Lichess, wenn es eine gibt, ihre Auswertung einen Klick entfernt, wie bei
  deinen anderen beendeten Partien (siehe [Deine beendeten Partien](#deine-beendeten-partien)). *Noch eine Partie* sucht
  erneut mit derselben Bedenkzeit.

### Arenen und Turniere nach Schweizer System
<!-- en: arenas-and-swiss-tournaments 042061fe -->

Die Hälfte *Turniere* im Teil **Auf Lichess** der Seite Spielen tritt einer Arena von Lichess oder einem Turnier nach
Schweizer System eines deiner Teams bei und spielt seine Partien von RookStudio aus. Die Karte *Lichess-Turniere* der
Seite Lichess führt ebenfalls dorthin, *Beitreten* in der Zeile einer Arena (siehe
[Lichess-Turniere](#lichess-turniere)).

- **Was Lichess erlaubt.** Seine Board-API spielt nur Schnellschach-, klassische und Fernpartien, nie Blitz oder Bullet
  in einem Turnier: Diese Arenen werden in der Mosaikansicht verfolgt und auf lichess.org gespielt, und ihre Zeile sagt
  es, mit dem Link. Der Teil listet die Schnellschach- und klassischen Arenen auf, die du spielen kannst.
- **Beim ersten Mal** fragt Lichess nach einer weiteren Berechtigung, `tournament:write` (um einem Turnier
  beizutreten, darin zu pausieren und es zu verlassen); `board:play`, zum Spielen, ist schon erteilt. *Auf Lichess
  erlauben…* (Enter) öffnet die Seite von Lichess; dein Konto bleibt währenddessen verknüpft, und das Token, das sie
  ersetzt, wird widerrufen.
- **Einer Arena beitreten**: ihre Bedingungen, wie Lichess sie beurteilt, jede mit deinen Werten (*Mindestens 20
  gewertete Partien (Schnellschach) (du: 312)*), eine fehlende Bedingung vor dem Beitritt genannt; ihre Regeln je in
  einer Zeile und *Die vollständigen Regeln auf lichess.org*; *Beitreten* (Enter). Eine Ablehnung wird in den Worten
  von Lichess gesagt: Für eine den Mitgliedern eines Teams vorbehaltene Arena *Das Team auf lichess.org öffnen*, da
  RookStudio keine Teams verwaltet.
- **Auf eine Partie warten**: Lichess paart einen Spieler, der nicht auf der Seite der Arena ist, nur eine Minute lang,
  also meldet RookStudio dich jede Minute erneut an, während du wartest; geschlossen tut es das nicht mehr, und du wirst
  nicht mehr gepaart, deine Punkte bleiben. Die nächste Partie öffnet sich von selbst auf der Seite Brett.
- **Die Zeile der Arena**, über dem Teil und über der Karte der Partie: dein Platz von so vielen, deine Punkte, die
  Serie (*Der nächste Sieg zählt doppelt*), die verbleibende Zeit und *Pause* (Alt+P), das dich nach der laufenden
  Partie herausnimmt, während deine Punkte bleiben; *Zurück in die Arena* (Alt+P) tritt wieder bei. Die Seite der Arena
  wird alle 15 Sekunden abgefragt; nach einem 429 (zu viele Anfragen) wartet ihre Tabelle eine Minute und sagt es,
  deine Partien laufen weiter.
- **Spielen**: wie jede Partie auf Lichess (siehe [Eine Partie auf Lichess](#eine-partie-auf-lichess)), mit *Berserk*,
  wo die Arena es erlaubt, nur vor deinem ersten Zug (die Hälfte deiner Bedenkzeit, ein Punkt mehr, wenn du gewinnst);
  Lichess erlaubt in einer Turnierpartie keine Rücknahme und keinen Abbruch. Eine Turnierpartie, deren ersten Zug du
  nicht rechtzeitig spielst, ist verloren, nicht abgebrochen, und die Karte sagt es. Unter der Karte die Partie davor,
  an ihrem Ende: *Gewonnen in 34 Zügen · +2 Punkte · die Serie beginnt*.
- **Die Hilfsmittel** bleiben gesperrt, solange du in der Arena bist, sobald sie begonnen hat, auch zwischen zwei
  Partien, da die nächste innerhalb von Sekunden kommt: die Engine, der Baum, die Cloud, die Tablebases, der Assistent
  und die Berichte (das Dossier eines Spielers, ein Eröffnungsbericht, eine Kreuztabelle). *Pause* gibt sie zurück
  (siehe [Fairplay](#fairplay)).
- **Ihr Ende**: dein Platz und deine Punkte; jede Partie ist in *Lichess · dein Name*, wie deine beendeten Partien
  (siehe [Deine beendeten Partien](#deine-beendeten-partien)), ihre Auswertung dann angeboten: *Die letzte Partie
  auswerten*, *Ihre Partien in der Liste*. RookStudio während einer Arena zu schließen, fragt zuerst: *Pausieren und
  schließen* oder *Bleiben* (Esc).
- **Ein Turnier nach Schweizer System**, für die Mitglieder seines Teams: seine Runde von so vielen, die Tabelle nach
  seiner letzten Runde (Punkte, Feinwertung, Performance), deine Partien Runde für Runde und die Zeit der nächsten
  Runde. Zwei Minuten vorher sagt es ein Hinweis in der Fensterecke, wo immer du bist; deine Partie öffnet sich von
  selbst auf der Seite Brett, und eine Partie, deren ersten Zug du nicht rechtzeitig spielst, ist verloren. *Seine
  Partien in der Mosaikansicht* zeigt die Partien der Runde seiner ersten acht. Ist es vorbei, nimmt *In einer
  Datenbank aufbewahren…* seine Partien, jede mit ihrer Runde, und seine Tabelle (seine TRF-Datei) in *Lichess-Turniere*
  oder eine andere deiner Datenbanken auf: Die Kreuztabelle der Datenbank liest sie dann, spielfreie Runden
  und späte Einstiege eingeschlossen (siehe [Turniere und Kreuztabellen](#turniere-und-kreuztabellen)), einen Klick
  entfernt mit *Seine Kreuztabelle öffnen*.

Was den Computer verlässt: beim Verfolgen das abgefragte Turnier und seine Partien, ohne Konto; beim Beitreten, mit
deinem Token, der Beitritt, die Pause und deine Züge. Eine Anfrage nach der anderen, eine Minute nach einem 429. Strg+K
hat *Eine Arena oder ein Turnier nach Schweizer System auf Lichess spielen*, *Der Arena beitreten: …*, *In der Arena
pausieren*, *Zurück zur Arena*, *Berserk* und *Das Turnier nach Schweizer System in einer Datenbank aufbewahren…*.

## Die Seite Lichess
<!-- en: the-lichess-page 0c6f9833 -->

Alles hier ist optional: RookStudio funktioniert ohne Konto und ohne Internet, und was an Lichess geht, steht dort, wo
du es entscheidest. Ihre rechte Spalte enthält die Dienste: die Übertragungen, die Turniere von Lichess, Lichess TV,
die verfolgten Spieler, die offline aufbewahrten Bewertungen, *Deine Engines auf lichess.org*, sobald eine deiner
Engines dem Analysebrett von lichess.org angeboten ist (siehe [Deine Engine auf
lichess.org](#deine-engine-auf-lichessorg)), und was die Analyse online abfragen darf.

### Dein Konto verknüpfen
<!-- en: linking-your-account 8b1bc649 -->

*Konto verknüpfen…* öffnet lichess.org im Browser, wo du RookStudio den Zugriff erlaubst, ohne ihm ein Passwort zu
geben. Lichess schickt den Browser zurück an 127.0.0.1, auf einen Port, auf dem nur RookStudio während der Verknüpfung
lauscht; ohne Antwort innerhalb von 5 Minuten bricht die Verknüpfung von selbst ab. Öffnet sich kein Browser, erscheint
die Adresse, um sie in einen Browser auf diesem Computer einzufügen.

Verknüpft nennt die Karte den Namen des Kontos und wann sein Token abläuft (nach einem Jahr; *Erneuern…* einen Monat
vorher). RookStudio liest nur deinen Namen und öffentliche Daten und fragt nach mehr, wenn eine Funktion es braucht:
zum Beispiel, um die Übertragungen zu lesen. Das Token wird in deinem Profil gespeichert, nur für dich lesbar;
*Trennen* widerruft es bei Lichess und löscht es hier, und deine Datenbanken bleiben. Ein verknüpftes Konto bringt den
Eröffnungsexplorer von Lichess in den Tab Baum (die Meister, alle Spieler, ein Spieler) und deine Partien schneller
abgeglichen, auch die privaten.

### Partien eines Spielers
<!-- en: a-players-games 3d841cf1 -->

*Partien eines Spielers* importiert die öffentlichen Partien eines Lichess-Spielers, deine oder die eines anderen, in
eine Datenbank (*Lichess · Name*), auch ohne Konto: *Importieren*, dann *Abgleichen* für die seither gespielten
Partien. Die Partien kommen in Stapeln, die ältesten zuerst, und was empfangen wurde, bleibt, wenn du abbrichst. *Was
abgefragt wird*: die Bedenkzeiten (alle; Blitz, Schnellschach und klassisch; klassisch und Fernschach), ab wann (die
erste Partie, vor einem Jahr, ein Datum), nur Standardschach und Chess960. Die Partien behalten ihren Link zu lichess.org,
ihre Uhren und die Bewertungen von Lichess. *Neue Partien zählen, wenn sich die Startseite öffnet* fragt Lichess
höchstens einmal pro Stunde, mit dem Namen des Spielers.

### Deine Lichess-Studien
<!-- en: your-lichess-studies 11bcbad8 -->

*Deine Studien* auf der Seite Lichess listet die Studien deines verknüpften Kontos auf, die zuletzt geänderten zuerst.
Beim ersten Öffnen der Karte bittet RookStudio Lichess um die Berechtigung, deine Studien zu lesen (*Auf Lichess
erlauben…*): Mit ihr werden auch deine privaten und nicht gelisteten Studien aufgelistet; *Ohne sie* nur die
öffentlichen. Die Liste von Lichess nennt den Namen jeder Studie und wann sie sich zuletzt geändert hat; ihre Kapitel
erscheinen, sobald sie gelesen wurde, und ihre Sichtbarkeit bei den von RookStudio erstellten Studien. Die Studie eines
anderen kommt über ihren Link (siehe [Lichess-Links](#lichess-links)).

Ein Klick auf eine Studie liest sie beim ersten Mal: Ihre Kapitel erscheinen und bleiben in deinem Profil, auch offline.
*Ein Kapitel auf dem Brett öffnen* (oder ein Doppelklick darauf) öffnet es, aus der Sicht der Seite, von der Lichess es
zeigt. *In eine Datenbank importieren* legt jedes Kapitel als Partie in eine Datenbank: *Lichess-Studien*, angelegt,
wenn sie fehlt, oder eine andere, daneben gewählt. Beim nächsten Mal, oder mit *Neu einlesen* nach einem Import, werden
nur die seither geänderten Kapitel ersetzt und die neuen hinzugefügt; eine Partie, die du dort gelöscht hast, bleibt
gelöscht. *Zur Theoriestudie machen* zeigt ihre Kommentare im Tab Baum, mit der Studie neu gelesen (siehe
[Theoriestudien](#theoriestudien)); eine nie gelesene Studie wird zuerst gelesen.

*Verfolgen*: Wenn sich die Seite Lichess öffnet (höchstens alle fünf Minuten), fragt RookStudio Lichess, ob sich jede
verfolgte Studie geändert hat, eine Anfrage pro Studie, ohne sie herunterzuladen. Eine geänderte Studie sagt das, in der
Karte und auf ihrer Zeile, wenn die Karte zugeklappt ist, mit *Neu einlesen*: Vorher wird nichts heruntergeladen. Nach
einem Lesen sind die seit dem vorherigen Lesen geänderten Kapitel markiert.

**Vom Brett aus** öffnet *lichess.org…* › *An eine Lichess-Studie…* neben dem Brett eine Karte, die die Partie in eine
deiner Studien sendet (eine volle, mit 64 Kapiteln, wird nicht angeboten) oder in eine neue, deren Name und Sichtbarkeit
dort gewählt werden: privat, nicht gelistet oder öffentlich, privat zuerst. Der Name des Kapitels, die Seite, aus deren
Sicht es gezeigt wird, und ob die Kommentare des Assistenten mitgehen (als einfache Kommentare nach denen des Zugs)
werden in derselben Karte gewählt; Enter sendet, Esc schließt sie. Gesendet wird die Partie als PGN: ihre Kopfdaten,
Züge und Varianten, deine Kommentare und Symbole, ihre Pfeile und Felder, ihre Uhren; die Daten der Engines, die
Bewertungen und alles andere von dir bleiben hier. Das erste Senden bittet Lichess um die Berechtigung, in deine Studien
zu schreiben. Einmal gesendet: *Auf lichess.org öffnen* oder *Link kopieren*.

Später sagt der Streifen über den Zügen *An Lichess gesendet · 2 Änderungen seitdem*, mit *Änderungen senden*: Die Züge,
Varianten und Notizen des Kapitels werden auf Lichess durch die der Partie ersetzt, auch ihre Tags, wenn sie sich
geändert haben. Nichts geht von selbst. Ein aus deinen Studien geöffnetes Kapitel sagt *Aus deiner Lichess-Studie* und
sendet seine Änderungen genauso.

Die Grenzen von Lichess: 64 Kapitel pro Studie, 30 neue Studien pro Tag.

### Offizielle Übertragungen
<!-- en: official-broadcasts 9c582d92 -->

Die laufenden offiziellen Übertragungen, die bekanntesten zuerst (*Alle laufenden Übertragungen* für die anderen), und
die nächsten. *Folgen* öffnet eine Runde in einem Mosaik auf der Seite Lichess: die Karte jeder Partie mit den Namen,
den Uhren, dem letzten Zug, der Bewertungsleiste sowie der Bewertung und der Variante der unter *Analysiert von*
gewählten Engine; rechts die gewählte Partie mit ihren letzten Zügen, *Auf dem Brett verfolgen* und *PGN kopieren*, dann
die Runde. Die Partien werden auf deinem Computer analysiert, mit deiner Engine, so viele gleichzeitig, wie es die von
den Turnieren übrig gelassenen Kerne erlauben, die gewählte Partie zuerst, und nur solange das Mosaik zu sehen ist. Auf
dem Brett geht die Partie mit jedem empfangenen Zug weiter; gehst du zum Analysieren zurück, läuft sie im Hintergrund
weiter, und *Zurück zu Live* holt sie zurück; *Zurück zum Mosaik* (Esc) führt zur Runde zurück.

Lichess lässt ohne Konto vier Runden gleichzeitig verfolgen, mit Konto acht. Seit August 2026 bremst es eine
Übertragung stark aus, die ohne ein Konto verfolgt wird, das Übertragungen lesen darf: Beim ersten Mal fragt
RookStudio nach dieser Berechtigung (*Auf Lichess erlauben…* oder *Ohne sie verfolgen*). Eine Runde, deren Partien alle
beendet sind, kommt in die Datenbank *Übertragungen*.

### Mehr von einer Übertragung
<!-- en: more-of-a-broadcast 3d035f0f -->

Rechts vom Mosaik einer Übertragung, unter der Runde, **die Tabelle**: Rang, Titel, Wertung und Punkte jedes Spielers,
wie die Veranstalter sie angeben (die Föderation, die Mannschaft, die Performance und die Wertungsänderung unter der
Maus), die ersten zwölf oder alle; bei einem Mannschaftswettbewerb zuerst die Mannschaften, nach Mannschaftspunkten,
dann nach Brettpunkten. Lichess liefert sie ohne Konto; sie werden alle zwei Minuten neu abgefragt, solange das Mosaik
zu sehen ist.

*Das ganze Turnier*, neben dem Namen einer Runde, verfolgt alle laufenden Runden des Turniers und jede nächste, sobald
sie beginnt: Das Mosaik zeigt die Runde live, und **Die Runden** darüber zeigt eine andere (ein Klick auf die laufende
verfolgt wieder die nächsten). Jede Runde kommt an ihrem Ende in die Datenbank *Übertragungen*. *Kreuztabelle*, neben
der Tabelle, zeigt anstelle der Bretter die Kreuztabelle der verfolgten Runden (siehe [Turniere und
Kreuztabellen](#turniere-und-kreuztabellen)).

**Übertragungen durchsuchen**, oben bei den offiziellen Übertragungen, sucht eine neuere nach ihrem Namen: Lichess
antwortet nur einem Konto, das Übertragungen lesen darf, beim ersten Mal angefragt. *Zurück zur Liste* zeigt wieder die
gerade laufenden Übertragungen.

### Lichess-Links
<!-- en: lichess-links c975cfa4 -->

Eine mit Strg+V auf der Seite Brett oder auf der Seite Datenbanken eingefügte Lichess-Adresse wird erkannt, ohne Lichess
etwas zu fragen: eine Partie (`lichess.org/abcd1234`, mit `/white`, `/black` oder einem Zug nach `#`), eine Studie oder
eines ihrer Kapitel, eine Runde oder ein Turnier einer Übertragung, eine Arena, ein Turnier nach Schweizer System, ein
Spieler (`lichess.org/@/name`) oder deine Lesezeichen. Bei einer Adresse, die RookStudio nicht kennt, wird das gesagt,
und nichts wird abgefragt.

Eine Karte sagt dann, was es ist, wohin seine Partien gehen und was bei Lichess abgefragt wird; Enter führt es aus, Esc
schließt sie:

- eine Partie oder ein Kapitel öffnet sich auf dem Brett, mit der Seite und dem Zug, die die Adresse zeigt, oder kommt
  in eine Datenbank;
- eine Studie, eine Übertragung, eine Arena, ein Turnier nach Schweizer System oder deine Lesezeichen kommen im
  Hintergrund in eine Datenbank, ihre Partien gezählt, wie sie kommen, mit *Abbrechen*; dann *Datenbank öffnen*;
- eine laufende Runde lässt sich stattdessen live verfolgen (siehe [Offizielle
  Übertragungen](#offizielle-übertragungen));
- die Partien eines Spielers kommen in seine Datenbank, über [seinen Abgleich](#partien-eines-spielers).

Angeboten wird die auf der Seite Datenbanken gewählte Datenbank, wenn der Link dort eingefügt wurde, sonst
*Lichess-Partien*, *Lichess-Studien*, *Übertragungen*, *Lichess-Turniere* oder *Lichess-Lesezeichen*, angelegt, wenn sie
fehlt; eine andere lässt sich wählen. Eine Partie wird über ihre ID abgefragt, ohne Konto; eine private Studie mit
deinem Token; Übertragungen mit einem Konto, das sie lesen darf, einmal angefragt, wie zum Verfolgen einer Runde; deine
Lesezeichen brauchen ein verknüpftes Konto.

### Fairplay
<!-- en: fair-play ca194626 -->

Lichess verbietet jede Hilfe während einer Partie. Mit verknüpftem Konto hält sich RookStudio daran, wo immer du
spielst, im Browser oder auf einem Telefon:

- solange eine deiner Live-Partien dauert, sperrt die Seite Brett ihre Tabs Analyse, Baum, Referenzen und Fragen, und
  weder die Engine noch die Drohung, die Tablebases, die Cloud oder der Assistent sehen sich irgendeine Stellung an; die
  Bretter zeigen keinen der Pfeile der Analyse und nicht ihre Bewertung, auch nicht die der Suche, die die Sperre
  angehalten hat. Eine laufende Auswertung hält an, ihre erledigten Durchgänge bleiben erhalten. Engine-Turniere laufen
  weiter, ohne Analyse auf ihrem Brett. Alles kommt von selbst zurück, wenn die Partie endet.
- eine Fernpartie sperrt die Engine, die Cloud, die Tablebases und den Assistenten für die Partie auf dem Brett, die
  durch ihre Stellung geht, vom ersten Zug bis zu dem, was folgt, eine aus einer Variante gezeigte Stellung dieser
  Partie eingeschlossen; Lichess erlaubt dort ein Buch und eine Datenbank, also bleiben der Baum deiner Datenbanken und
  ihre Referenzpartien offen. Vor ihrem ersten Zug sperrt sie nur die Ausgangsstellung: Jede Partie geht durch sie. Bei
  jeder anderen Partie funktioniert alles.
- solange du von RookStudio aus in einer Arena bist, bleiben alle Hilfsmittel auch zwischen zwei Partien gesperrt, mit
  ihnen die Berichte, bis *Pause* (Alt+P) oder zum Ende der Arena (siehe [Arenen und Turniere nach Schweizer
  System](#arenen-und-turniere-nach-schweizer-system)).

Ein Banner auf der Seite Brett sagt, welche Partie was sperrt. Die Karte Lichess der Startseite listet deine laufenden
Partien auf, die, in denen du am Zug bist, zuerst, jede mit ihrer Uhr oder der verbleibenden Zeit für den Zug, und *Auf
lichess.org öffnen* oder *Aufs Brett*, sobald RookStudio deine Partien spielen darf (siehe [Eine Partie auf
Lichess](#eine-partie-auf-lichess)). Die Partien deines Lichess-Bots sind nicht deine und sperren nichts; während deiner
eigenen Live-Partie wird die Suche des Bots nicht gezeigt (siehe [Ein Lichess-Bot](#ein-lichess-bot)). Lichess TV zeigt
deine eigenen Partien nie, auch nicht hervorgehoben, und die Engines des Mosaiks pausieren während deiner Live-Partie
(siehe [Lichess TV](#lichess-tv)). Deine dem Analysebrett von lichess.org angebotene Engine pausiert während deiner
Live-Partie und analysiert nie die Stellungen deiner Fernpartien (siehe [Deine Engine auf
lichess.org](#deine-engine-auf-lichessorg)).

Deine laufenden Partien werden mit dem Token deines Kontos abgefragt, wenn die Startseite erscheint, alle halbe Minute,
solange eine Live-Partie dauert, und alle zehn Minuten, solange eine Fernpartie läuft. Die Seite Lichess bietet einmal
an, stattdessen auf ihren Beginn und ihr Ende zu hören, solange RookStudio offen ist: eine weitere Berechtigung, deine
Herausforderungen lesen; die Berechtigung, deine Partien zu spielen (siehe [Auf Lichess](#auf-lichess)), hört sie über
denselben Strom. RookStudio liest nur die Züge der Partien, die du von ihm aus spielst, und spielt nur die Züge, die du
spielst. Ohne eine der beiden Berechtigungen greift die Sperre, wenn die Startseite erscheint.

### Deine beendeten Partien
<!-- en: your-finished-games 3109239c -->

Wenn eine deiner Partien endet, holt RookStudio sie in *Lichess · dein Name*, eine Datenbank, die angelegt wird, wenn
sie fehlt, mit ihren Uhren, ihrer Eröffnung und, wenn du Lichess um eine Analyse gebeten hast, den Bewertungen von
Lichess. Ein Banner auf der Seite Brett und eine Zeile auf der Startseite sagen, wie sie endete; *Auswerten* (Enter)
fragt Lichess noch einmal nach seiner Analyse, die inzwischen angefordert worden sein kann, öffnet die Partie und
startet ihre Auswertung. *Nicht jetzt* (Esc) legt das Banner beiseite.

Die Analyse von Lichess erscheint dann neben der deiner Engine: ihre Kurve auf der Grafik und die Genauigkeit jeder
Analyse unter den Zahlen der beiden Seiten im Tab Auswertung, für beide mit derselben Formel. *Meine beendeten Partien
sofort auswerten* auf der Seite Lichess startet die Auswertung von selbst, außer das Brett hat eine Partie mit
ungespeicherten Änderungen oder laufende Arbeit; dann bietet das Banner sie an. Die Partien einer Arena kommen auf
dieselbe Weise, ihre Auswertung am Ende der Arena angeboten.

### Verfolgte Spieler
<!-- en: followed-players 9a60f4cb -->

Die Karte *Verfolgte Spieler* der Seite Lichess listet die Spieler auf, denen dein Konto auf Lichess folgt, und die, die
du über ihren Namen hinzufügst, jeden mit einem Punkt: rot, solange er spielt, mit der Bedenkzeit der Partie, grün, wenn
er online ist. Ihr Status wird alle fünfzehn Sekunden abgefragt, solange die Karte oder ihr Mosaik zu sehen ist, ohne
Konto.

*Ihre Partien im Mosaik* zeigt die Partien, die sie gerade spielen, im Mosaik der Übertragungen, jede von deiner Engine
analysiert, höchstens acht, so viele, wie Lichess einer Adresse erlaubt, die Runden, die du verfolgst, und die Partien
von Lichess TV mitgezählt; Lichess zeigt ihre Züge drei Züge verzögert, für alle. Eine beendete Partie bleibt mit ihrem
Ergebnis, bis eine andere ihren Platz einnimmt. Um zu lesen, wem dein Konto folgt, braucht es eine weitere Berechtigung,
beim ersten Mal angefragt; *Ohne sie* werden die über ihren Namen hinzugefügten Spieler verfolgt. Dein eigenes Konto und
deine eigenen Partien erscheinen dort nie.

### Lichess TV
<!-- en: lichess-tv 820042bb -->

Die Karte *Lichess TV* der Seite Lichess listet die Kanäle des Standardschachs auf: *Höchste Wertung*, *Bullet*,
*Blitz*, *Schnellschach*, *Klassisch*, *UltraBullet*, *Bots* und *Computer* (Spieler gegen die KI von Lichess); die
Kanäle der Varianten werden nicht gezeigt. Jeder gibt die Partie an, die Lichess auf ihm hervorhebt, ihre beiden Spieler
mit ihren Wertungen und ihre Bedenkzeit. Die Liste wird neu abgerufen, wenn die Karte erscheint, höchstens einmal alle
30 Sekunden.

*Mosaik* zeigt die laufenden Partien des Kanals im Mosaik der Übertragungen, seiner dritten Quelle: Die Zeile oben darin
wechselt von einer zur anderen (*Übertragungen*, *Verfolgte Spieler*, *Lichess TV*, *Lichess-Turniere*), und die Kanäle sind
Chips darunter.
Dieselben Kacheln, eine Engine von dir für jede Partie, die gewählte Partie rechts mit *Auf dem Brett verfolgen*.
Lichess listet bis zu 30 Partien pro Kanal auf und lässt eine Adresse 8 Partien gleichzeitig verfolgen, die Runden, die
du verfolgst (je eine), und die Partien der verfolgten Spieler mitgezählt: Das Mosaik zeigt, was übrig bleibt, die
hervorgehobene Partie zuerst. Eine beendete Partie zeigt ein paar Sekunden lang ihr Ergebnis und macht dann einer
anderen des Kanals Platz. Lichess zeigt die Züge jedem Zuschauer drei Züge verzögert, und das Mosaik sagt es.

*Brett* (oder ein Doppelklick auf eine Kachel) öffnet die Partie auf dem Brett, das ihr folgt; die Karte über seiner
Spalte sagt, was sie ist: *Lichess TV · Blitz · die hervorgehobene Partie, 3+0*. Wenn sie endet, geht das Brett von
selbst zur nächsten hervorgehobenen Partie des Kanals über, die verlassene Partie mit Strg+Tab erreichbar; *Bei dieser
Partie bleiben* hält das Brett bei ihr. *Diese Partie behalten…* speichert sie an ihrem Ende in einer Datenbank deiner
Wahl: *Lichess TV*, die angelegt wird, wenn sie fehlt, oder einer anderen von dir; bis dahin wartet sie in deinem
Profil, auch wenn RookStudio geschlossen wird. *Zurück zum Mosaik* (Esc) führt zum Mosaik des Kanals zurück.

Auf der Seite Engines hat die Karte einer Engine *Lichess TV: Bots, Computer*: die besten Bots von Lichess oder Spieler
gegen seine KI im Mosaik, jede Partie von dieser Engine analysiert.

Deine eigenen Partien erscheinen nie, auch nicht hervorgehoben: die deines verknüpften Kontos und die deines
Lichess-Bots (siehe [Ein Lichess-Bot](#ein-lichess-bot)). Solange eine deiner Live-Partien dauert, pausieren die Engines
des Mosaiks (siehe [Fairplay](#fairplay)). Was den Computer verlässt: der abgefragte Kanal und die verfolgten Partien,
ohne Konto; nichts wird von selbst gespeichert. Strg+K hat *Lichess TV*, jeden Kanal im Mosaik (und auf dem Brett,
sobald die Karte abgerufen wurde) und für die Partie des Bretts Befehle, um bei ihr zu bleiben oder zur nächsten Partie
des Kanals überzugehen, sie zu behalten und zum Mosaik zurückzukehren.

### Lichess-Turniere
<!-- en: lichess-tournaments fe42cfa6 -->

Die Karte *Lichess-Turniere* der Seite Lichess listet die laufenden und kommenden Arenen von Lichess auf, ohne Konto
gelesen: *Schnellschach*, *Klassisch* oder *Alle*, jede Zeile mit ihrem Namen, ihrer Bedenkzeit, wann (*noch 38 min*,
*in 40 min*), ihren Spielern, *Folgen* und *Beitreten* oder warum nicht: *Blitz: auf lichess.org* (ein Klick öffnet
sie), *Bedingung nicht erfüllt* (unter der Maus welche, mit deinen Werten). Die Arenen der Varianten werden nicht
gezeigt; die Liste wird neu gelesen, wenn die Karte erscheint, höchstens einmal pro Minute.

*Die Turniere nach Schweizer System deiner Teams*, darunter, listet die der Teams auf, in denen du Mitglied bist:
Deine Teams zu lesen, braucht eine weitere Berechtigung, `team:read`, einmal erfragt (*Meine Teams lesen…*). Jede
Zeile gibt seine Runden, seine Bedenkzeit, wann, seine Spieler und *Beitreten* an; *Folgen*, solange es läuft; *In
einer Datenbank aufbewahren…*, sobald es vorbei ist.

*Folgen* zeigt das Turnier im Mosaik der Übertragungen, seiner vierten Quelle neben *Übertragungen*, *Verfolgte
Spieler* und *Lichess TV*: die laufenden Partien seiner ersten acht, wie das Turnier sie liefert, wobei eine beendete
Partie der nächsten Platz macht; Lichess zeigt ihre Züge drei Züge verzögert und lässt eine Adresse acht Partien
gleichzeitig verfolgen, die Runden und die Spieler, denen du folgst, mitgezählt; jede wird von deiner Engine
analysiert, wie es das Mosaik tut. Rechts seine Tabelle, alle 15 Sekunden abgefragt: Platz, Wertung, Bogen (die Punkte
jeder Partie, die neueste zuletzt wie auf lichess.org, die verdoppelten Punkte einer Serie in Orange) und Punktzahl
jedes Spielers; bei einem Turnier nach Schweizer System die nach seiner letzten Runde. Ein Klick wählt eine Partie,
ein Doppelklick öffnet sie auf der Seite Brett; deine eigene Partie erscheint nie. Ein Turnier nach Schweizer System
listet seine laufenden Partien nicht auf: Sie werden in jeder Runde über die Spieler gefunden, die seiner ersten acht.
Ohne Konto; nichts wird von selbst aufbewahrt. Strg+K hat *Lichess-Turniere: Arenen und Turniere nach Schweizer
System*, *Der Arena folgen: …* (sobald die Karte die Liste gelesen hat) und das in der Mosaikansicht verfolgte Turnier.

### Eine Partie vorbereiten
<!-- en: preparing-a-game 2c8891af -->

Die Karte *Eine Partie vorbereiten gegen* der Seite Lichess nimmt einen Lichess-Namen und öffnet das Dossier dieses
Spielers (siehe [Das Dossier eines Spielers](#das-dossier-eines-spielers)) mit angehakter Quelle Lichess. Seine Partien
aus dem letzten Jahr, Blitz, Schnellschach und klassisch, kommen in eine eigene Datenbank, *Lichess · Name*, beim
nächsten Mal erneut abgeglichen, und sein öffentliches Profil gibt sein Niveau an. Das Dossier zeigt dann, was er
online spielt, neben dem, was deine Datenbanken von ihm haben (dem Spieler, dem das Lichess-Konto gehört, sobald
verknüpft), deine Partien gegen ihn (mit verknüpftem Konto) und die Varianten, die er oft spielt und auf die deine
Repertoires nicht antworten, über beide Quellen, die häufigsten zuerst; ein Klick auf eine öffnet sie auf dem Brett,
den Tab Baum auf seinen Partien. Nur sein Name und diese Filter gehen an Lichess. Während eine deiner Partien gegen ihn
auf Lichess gespielt wird, zeigt das Dossier nichts von ihm.

### Die Bewertungen von Lichess, offline
<!-- en: lichesss-evaluations-offline e972bc19 -->

Die Karte *Die Bewertungen von Lichess, offline* bewahrt auf deiner Festplatte auf, was die Server von Lichess für 418
Millionen Stellungen berechnet haben (database.lichess.org, CC0): für jede die Bewertung, die Tiefe und den ersten Zug
ihrer tiefsten Suche. Danach wird nichts mehr im Netz abgefragt, und eine Bewertung erscheint sofort, wo immer Lichess
eine hat.

- **Was aufbewahrt wird**: jede Stellung (etwa 3,6 GB) oder nur die Eröffnungen (etwa 1,5 GB), die Stellungen mit 24
  Steinen oder mehr auf dem Brett. Die Datei von Lichess nennt keine Zugnummer: In den Partien von Lichess sind 24
  Steine oder mehr 89 % der Stellungen der ersten 20 Züge und 8 % der späteren. Die Varianten von Lichess werden
  ausgelassen: Sie bräuchten fast fünfmal so viel Platz, für das, was deine Engine in einer Sekunde wiederfindet.
- **Herunterladen und vorbereiten** (Enter) liest die Datei von Lichess, etwa 21 GB, beim Herunterladen und behält davon
  nur, was du gewählt hast; *Eine schon heruntergeladene Datei…* liest stattdessen `lichess_db_eval.jsonl.zst` von
  deiner Festplatte. Während der Vorbereitung braucht es etwas mehr als den doppelten Platz dessen, was es behält; die
  Karte sagt, wie viel deine Festplatte frei hat.
- Die gelesenen Stellungen antworten schon, ein paar Millionen auf einmal, ohne auf das Ende zu warten. *Abbrechen*,
  eine Netzunterbrechung oder das geschlossene Programm: *Fortsetzen* nimmt die Datei dort wieder auf, wo sie aufhörte,
  *Neu beginnen* vergisst, was gelesen wurde.
- Fertig nennt die Karte die Stellungen, die Größe und das Datum der Datei. Lichess veröffentlicht die Datei jeden Monat
  neu: *Aktualisieren* fragt database.lichess.org nach ihrem Datum und liest eine neuere Datei, während die aufbewahrten
  Bewertungen antworten, bis sie fertig ist. *Entfernen* gibt die Festplatte frei.

Sie erscheinen an drei Stellen:

- im Tab Analyse: eine Zeile über der Engine, *Lichess offline · +0.31 · Tiefe 46 · 6…e6*, die Bewertung aus der Sicht
  von Weiß (ein Klick zeigt den Zug auf dem Brett);
- in der Auswertung einer Partie: Eine mit Tiefe 30 oder mehr bekannte Stellung wird übernommen, wie sie ist, ohne
  Suche, und die Engine durchsucht die anderen (ihr letzter Durchgang, standardmäßig 5 Sekunden pro Stellung, geht
  selten tiefer);
- im Tab Baum: eine Spalte *Bew.*, die Bewertung nach jedem Zug.

Was den Computer verlässt: die Anfrage nach der Datei an database.lichess.org, sonst nichts. Während eine deiner Partien
auf Lichess gespielt wird, verschwinden sie wie die Engine (siehe [Fairplay](#fairplay)).

### Online für die Analyse
<!-- en: online-for-the-analysis 1f447190 -->

Drei Dienste, jeder bei seiner ersten Nutzung angefragt (für diese Stellung, immer oder nicht jetzt) und hier geändert:

- **Cloud-Bewertung von Lichess**, im Tab Analyse: Die FEN jeder gezeigten Stellung wird gesendet, ohne Konto, und
  Lichess antwortet mit den Varianten, die seine Server schon berechnet haben, falls vorhanden. Deine Engine antwortet
  immer.
- **Endspiele online, bis 7 Steine**: Für eine Stellung mit sieben Steinen oder weniger, die die Tablebases auf deiner
  Festplatte nicht kennen, wird ihre FEN an die Lichess-Tablebases gesendet, ohne Konto. Sie liefern das Urteil und
  wie weit das Ende entfernt ist, im Tab Analyse, und jeden Zug nach seinem Ergebnis im Tab Endspiele, mit den
  Hauptvarianten seiner Züge (siehe [Der Tab Endspiele](#der-tab-endspiele)). Ein Gewinn, den die 50-Züge-Regel in ein
  Remis verwandelt, ein „cursed win“ in der Sprache der Tablebases, heißt „Gewonnen, aber remis durch die
  50-Züge-Regel“. Mit acht Steinen und gegenüberstehenden Bauern liefern sie das Urteil und die Züge bis zum nächsten
  Schlagen, zur nächsten Umwandlung oder zum Matt. Die Prüfung der Endspiele eines beendeten Turniers fragt sie nach ein
  paar Stellungen pro Partie, sobald für diese Prüfung oder immer erlaubt (siehe [Die Endspiele mit den Tablebases
  prüfen](#die-endspiele-mit-den-tablebases-prüfen)).
- **Explorer von Lichess**, im Tab Baum: Für jede Stellung werden ihre FEN und der gewählte Filter mit deinem Token
  gesendet; er braucht ein verknüpftes Konto. Nichts aus deinen Datenbanken wird gesendet.

Die Antworten werden in deinem Profil gespeichert: die der Tablebases für immer, die der Cloud einen Monat lang. Bittet
Lichess um eine Pause (zu viele Anfragen in kurzer Zeit), erscheint der Countdown; offline wird gesagt, was weiterhin
funktioniert.

## Der Assistent
<!-- en: the-assistant d3ffeeba -->

Der Assistent kommentiert deine Partien und beantwortet deine Fragen zu ihnen, auf deinem Computer. Er stützt sich auf
das, was deine Engine, deine Datenbanken und die Endspieldatenbanken feststellen, und sagt es in Worten; wenn nichts es
belegt, sagt er auch das. Er schreibt auf Englisch oder Französisch (auf Englisch für die anderen Sprachen der
Oberfläche).

### Sein Modell
<!-- en: its-model 1e5f4544 -->

Der Assistent braucht sein Modell, *RookStudio commentary v11*, einmal von huggingface.co heruntergeladen (etwa 2,7 GB
auf der Festplatte und etwa 3 GB Arbeitsspeicher im Betrieb), bei Ankunft geprüft; ein abgebrochener Download wird dort
fortgesetzt, wo er war. Die erste Karte des Tabs Fragen, von *Diesen Zug kommentieren* und der Auswertung bietet es
an; *Eine GGUF-Datei von der Festplatte…* nimmt ein anderes Modell, das Fragen beantwortet, aber die Kommentare von
RookStudio nicht gelernt hat (die bleiben bei den Vorlagen). Nur der Download des Modells geht über das Internet: Deine
Partien, deine Fragen und seine Antworten bleiben auf dem Computer.

Das Modell läuft in einem eigenen Prozess, auf der GPU, wenn eine nutzbar ist (ein Kommentar in etwa einer
Viertelsekunde), sonst auf dem Prozessor (etwa 3 Sekunden). Es wird bei der ersten Frage oder dem ersten Kommentar
geladen und nach einer Viertelstunde ohne Einsatz entladen, oder mit *Entladen* (auf der Startseite, in den
**Einstellungen** oder in Strg+K). Während ein Turnier die GPU nutzt, antwortet es auf dem Prozessor (*GPU trotzdem
nehmen*, wenn du willst); ein Grafiktreiber, der versagt, schickt es auf den Prozessor, und *GPU erneut versuchen*
versucht es noch einmal. *Einstellungen* > *Assistent*: sein Modell, die GPU oder der Prozessor, die Minuten, bevor es
entladen wird (0: geladen bis *Entladen*).

Ohne das Modell funktioniert alles andere: die Analyse, die Zahlen der Auswertung, die Vorlagenkommentare, der Baum.

### Fragen
<!-- en: questions e1e09fc4 -->

Der Tab Fragen (Alt+5) nimmt eine Frage zur Stellung oder zur Partie entgegen, getippt oder unter den Vorschlägen
gewählt: warum der letzte Zug, was droht, der beste Zug, wer diese Stellung gespielt hat, welche Eröffnung das ist. Der
Assistent kann einen Zug erklären, den besten Zug oder die Drohung nennen, einen Zug ausprobieren, zwei vergleichen; die
Referenzpartien und die in einer Datenbank gespielten Züge nennen; die Eröffnung benennen; sagen, was die
Endspieldatenbanken sagen. Jede Antwort zeigt, worauf sie beruht (die Suche der Engine, der Baum, die Eröffnungsnamen,
die Tablebases), und ihre Aktionen: den Zug spielen, die Variante in der Vorschau zeigen, die stärkste Partie öffnen, die
Antwort zur Notation hinzufügen. Eine Frage über das hinaus, was er kann, bekommt ein „Das kann ich nicht sagen“ und
das, was er stattdessen zeigen kann.

Die Engine des Tabs Analyse antwortet für ihn, in einem eigenen Prozess (ein Thread, 300.000 Knoten pro Stellung): Die
Fragen brauchen eine Engine in der Bibliothek.

### Kommentare des Assistenten
<!-- en: comments-by-the-assistant 009c27cb -->

*Diesen Zug kommentieren* (Alt+C oder Strg+K) schreibt den Kommentar des Assistenten zum gezeigten Zug und
*Schlüsselmomente kommentieren* in der Auswertung einen zu jedem Schlüsselmoment. Seine Kommentare werden in Blaugrau
und kursiv gezeigt, nach einer Sprechblase; unter dem Mauszeiger steht, worauf jeder beruht: die Engine, die Fakten der
Stellungen, die Datenbank der Theorie, das Modell oder eine Vorlage. Ohne das Modell sind die Kommentare Vorlagen, aus
denselben Fakten geschrieben.

Ein Zug, der schon einen Kommentar hat (deinen, die Analyse von Lichess zu einer Partie, den des Kommentators einer
Partie), behält ihn, wie er ist: Der des Assistenten kommt danach, getrennt, in einem Kasten in seiner Farbe unter
seinem Namen. Im PGN sind es zwei Kommentare hintereinander, der des Assistenten mit `[%rs assistant]` markiert; ein
anderes Programm liest zwei Kommentare, und RookStudio gibt jedem wieder seinen Platz. Strg+Z nimmt den Kommentar des
Assistenten zurück. Ein Doppelklick korrigiert ihn, und er wird deiner: Bei einem Zug, der schon einen Kommentar von
dir hat, wird der korrigierte Text danach angefügt, als ein einziger Kommentar. Eine Antwort des Tabs Fragen, die zur
Notation hinzugefügt wird, ist ebenfalls ein Kommentar des Assistenten.

## Testsuiten
<!-- en: test-suites 42c2b2ba -->

Die Seite **Suiten** führt eine EPD-Testsuite aus: Stellungen mit ihrem besten Zug (`bm`), den zu vermeidenden Zügen
(`am`) oder, bei der Strategic Test Suite, den Punkten mehrerer Züge (`c0 "f5=10, Be5+=2"`).

- **Suite**: eine EPD-Datei oder ein EPD-Buch der Bibliothek. Die Zeilen ohne legale Stellung oder ohne eine dieser
  Operationen werden weggelassen (ihre Zahl wird angezeigt).
- **Engines**: beliebig viele Engines der Bibliothek mit ihren Optionen; *Hash* und *Threads* erzwingen einen Wert für
  alle (0 = der eigene jeder Engine).
- **Zeit**, **Knoten** oder **Tiefe** pro Stellung. *Gefunden seit (Halbzüge)*: Eine Suche endet, sobald eine Lösung
  über so viele weitere Halbzüge Tiefe der erste Zug der Engine geblieben ist (0 = jede Suche nutzt ihre ganze Zeit).
- **Automatische Parallelität**: mit einer Zeit die physischen Kerne − 1, geteilt durch die Threads der Engines; mit
  Knoten oder einer Tiefe jeder Hardware-Thread.

Eine Stellung ist gelöst, wenn der endgültige Zug der Engine eine Lösung ist: einer aus `bm`, keiner aus `am`, oder,
nur mit Punkten, der Zug mit den meisten. Ihre Lösungszeit ist der Zeitpunkt, an dem sich die Engine auf diesen Zug
festgelegt hat. Das Raster zeigt die Stellungen gegen die Engines, ✓ mit der Lösungszeit oder ✗ mit dem gespielten
Zug, und die STS-Punkte; die Summen stehen vorn: gelöste Stellungen, ihre durchschnittliche Zeit und die STS-Punktzahl.
Ein beendeter Lauf wird in `suites/` im Profil aufbewahrt; *Mit einem gespeicherten Lauf vergleichen…* fügt die
Spalten eines früheren Laufs derselben Suite hinzu, um Versionen einer Engine zu vergleichen. *CSV exportieren…*
schreibt das Raster.

## Neues Turnier
<!-- en: new-tournament 6d169de4 -->

**Turniere → Neues Turnier** öffnet die Einrichtung: eine Karte mit zugeklappten Zeilen, die sagen, wie die Partien
gespielt werden, die Teilnehmer mit der Engine-Bibliothek daneben, und rechts eine laufend aktualisierte
Zusammenfassung (Zahl der Partien, Partien pro Engine, geschätzte Dauer, Eröffnungen, gemeinsame Einstellungen,
Zeitvorgabe). Die Einrichtung des letzten Turniers bleibt erhalten, so dass ein erneuter Test zweier Versionen einer
Engine so geht: A und B wählen, **Start**.

Alles auf der Seite geht auch mit der Tastatur: Tab und die Pfeiltasten wechseln zwischen den Zeilen, den Listen und
den Knöpfen, Enter oder die Leertaste öffnet eine Zeile oder eine Liste, und die Tasten der
[Teilnehmer und der Bibliothek](#teilnehmer-und-bibliothek) funktionieren dort. **Start** legt das Turnier an und
spielt es; **Zur Warteschlange hinzufügen** hebt es für später auf (siehe
[Warteschlange und Neustart](#warteschlange-und-neustart)).

### Die Zusammenfassung und ihre Prüfungen
<!-- en: the-summary-and-its-checks 5609344a -->

Die Zusammenfassung gibt den Speicher an, den die Hashtabellen belegen können: zwei Engine-Prozesse pro gleichzeitig
gespielter Partie und die Engines, die auf ihre nächste Partie warten, jeweils mit ihrem `Hash`. Übersteigt dieser
Bedarf den freien Speicher, warnt sie (das System wird auslagern) und nennt die Engine mit dem größten `Hash`;
übersteigt er den eingebauten Speicher, startet das Turnier nicht. `--run` sagt dasselbe. Lässt das Speicherbudget der
**Einstellungen** weniger Partien gleichzeitig zu als die Parallelität, sagt die Zusammenfassung, wie viele (jede
Partie belegt den `Hash` ihrer beiden Engines und was deren Prozesse sonst noch belegen), und die Zeile
*Parallelität* ebenso; das Gleiche gilt für die Partien mit einer Engine, die eine GPU nutzt, gemessen an den *GPUs*.
Sie nennt außerdem die Engines, deren Programm sich seit ihrem letzten Turnier geändert hat (ein neuer Build), mit
*Was sich geändert hat*, wenn die Karte der Engine neue Standards oder Optionen auflistet (siehe
[Neu gebaute Programme](#neu-gebaute-programme)).

Was schiefgehen würde, wird mit der Engine, der Ressource und der Behebung genannt, einem Knopf unter der Zeile (die
Befehlspalette listet dieselben Behebungen auf):

- ein Programm oder ein Arbeitsverzeichnis, das nicht gefunden wird: *Engine öffnen* (ihre Karte auf der Seite
  Engines), *Suchen…*; ein Eröffnungsbuch, das nicht gefunden wird: *Suchen…*;
- eine Engine, deren eigene `Threads` (ihre Einstellung, sonst ihr Standard: Manche Engines nehmen jeden Kern) über
  der Hälfte der Hardware-Threads liegen oder über den Threads, die jeder Partie bei der Zahl gleichzeitiger Partien
  zustehen (mit einer Uhr gemessen an den physischen Kernen): *Threads auf N senken* setzt die Option der Engine in der Bibliothek, *Engine
  öffnen*. `Threads`, die die Einrichtung über die Maschine hinaus vorgibt, bekommen *Threads auf N senken* dort, wo
  sie vorgegeben werden (die Zelle des Teilnehmers, die gemeinsamen Einstellungen). Die Teilnehmerliste markiert eine
  solche Engine mit einem *!*, die Engines des Matches werden darunter gekennzeichnet;
- ein Optionswert, den eine Engine ablehnt (außerhalb ihres Bereichs, keiner ihrer Auswahlwerte, eine Option, die sie
  nicht mehr hat): fastchess sendet ihn nicht, und die Engine spielt mit ihrem Standard;
- die Syzygy-Tablebases, die das Turnier verwendet: die Ordner der Abschätzung und das, worauf der `SyzygyPath` jeder
  Engine zeigt (mehrere Ordner, unter Windows durch `;` getrennt, sonst durch `:`). Für die verwendete Steinzahl (die
  *Max. Steine* der Abschätzung, sonst die größten gefundenen Tabellen) muss jede WDL-Datei (`.rtbw`) da sein, und
  keine Datei darf abgeschnitten sein (eine Syzygy-Datei ist 16 Bytes länger als ein Vielfaches von 64; Engines lassen eine
  Datei anderer Größe weg, und manche bleiben stehen, wenn sie auf sie stoßen): *Syzygy 6 Steine: 14 Dateien fehlen in
  D:\TB (KRPPvKR.rtbw…)*, *Abschätzung bis 6 Steine, aber nur 5-Steiner-Dateien gefunden*, *SyzygyPath von Alpha zeigt
  auf einen Ordner ohne Tablebase-Dateien*, mit *Ordner öffnen* und *Auf 5 Steine senken*; der Tooltip listet die
  Dateien auf. Das sind Warnungen: Die Abschätzung und die Engines kommen ohne die fehlenden Dateien aus. Nur eine
  Abschätzung ohne Ordner, oder deren Ordner keine Tabelle enthalten, hält das Turnier bei seinem Start an (fastchess
  lehnt sie ab): *Suchen…* wählt einen anderen Ordner. Die Ordner werden im Hintergrund gelesen, und erneut alle paar
  Sekunden, solange die Zusammenfassung angezeigt wird.

Ein abgelehnter Start nennt dieselben Probleme, mit ihren Behebungen und *Protokoll öffnen*. Jedes Problem hat ein
**?**, das seinen Eintrag in der [Fehlerbehebung](#fehlerbehebung) öffnet.

### Profile und Vorlagen
<!-- en: profiles-and-templates df08e65d -->

**Profile.** Die ersten fünf Zeilen, *Format*, *Bedenkzeit*, *Eröffnungen*, *Abschätzung* und
*Engine-Einstellungen*, sind jeweils ein benanntes Profil: Seine Liste wählt eines, die Zeile daneben sagt, was es
spielt („40 Züge in 15 min, wiederholt“, „Remis 40/8/10 cp · TB 6 Steine“; ihr Tooltip gibt dasselbe so an, wie
cutechess und fastchess es schreiben, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), und ✎ oder ein Klick
auf diese Zeile öffnet die Einzelheiten direkt darunter. Eine Änderung nur für dieses Turnier setzt einen Punkt neben
das Profil, und seine Liste und seine Einzelheiten bieten *… aktualisieren* (das Profil übernimmt die Änderung, für
jedes Turnier und jede Vorlage, die es verwendet), *Zurücksetzen* und *Als Profil speichern…* (unter einem neuen
Namen). Das Programm bringt Vorgaben mit:

- Bedenkzeiten: 10+0.1 und 60+0.6 (die kurzen und langen Tests von fishtest), 2+1, 40/4, 40/15 und 40/20 (40 Züge in
  4, 15 oder 20 Minuten, wiederholt, wie Ranglisten sie spielen) und 200k Knoten pro Zug;
- Formate: SPRT Verbesserung [0, 3] und keine Verschlechterung [-5, 0], ein Match über 1000 Partien, ein Rundenturnier,
  ein Gauntlet, ein Schweizer System mit 9 Runden, ein K.-o.-System und ein Mannschaftskampf;
- Abschätzungen: keine, eine Standard-Abschätzung (Remis ab Zug 40 nach 8 Zügen innerhalb von 10 cp, Verlust nach 3
  Zügen bei 600 cp) und dieselbe mit den Tablebases bis 6 Steine;
- Engine-Einstellungen: die eigenen jeder Engine, oder Hash 16, 64 oder 256 MB mit 1 Thread und ausgeschalteten
  eigenen Büchern der Engines;
- Eröffnungen: kein Buch, oder die 960 Stellungen von Chess960.

Ein eigenes Profil, das unter dem Namen einer Vorgabe gespeichert wird, tritt an ihre Stelle; löschst du es (aus seiner
Liste, nach einer Bestätigung), kommt die Vorgabe zurück, und die Einrichtungen und Vorlagen, die es verwendet haben,
behalten seine Werte. Profile werden in `profiles.json` im Profilordner gespeichert, und die Befehlspalette (Strg+K)
listet sie auf: Wählst du eines, wird es auf die Einrichtung angewendet. Die Palette öffnet außerdem jede Zeile der
Einrichtung über ihren Titel und lädt oder speichert Vorlagen. Die Syzygy-Verzeichnisse stehen in keinem Profil: Sie
gehören zu diesem Computer, und die Abschätzung und die Engines teilen sie sich. Die Zeilen *Parallelität* und *PGN und
Sonstiges* enthalten, was nur zu diesem Turnier gehört.

**Vorlage** und **Name**, neben *Start*: Eine Vorlage ist die ganze Einrichtung, ein Profil für jeden Abschnitt plus
das, was kein Profil enthält (der Name, die Teilnehmer, die Parallelität, die PGN-Dateien, die Übertragung). Das Menü
lädt eine oder speichert die Einrichtung als Vorlage (*Einrichtung als Vorlage speichern…*, das nachfragt, bevor es
eine Vorlage gleichen Namens ersetzt), und auch die Befehlspalette lädt sie. Ein Abschnitt, der beim Speichern der
Vorlage seinem Profil folgte, folgt diesem Profil weiter, so dass ein aktualisiertes Profil jede Vorlage aktualisiert,
die es verwendet; ein für jenes Turnier geänderter Abschnitt behält seine eigenen Werte. Vorlagen früherer Versionen
werden so geladen, wie sie waren, und ihre Abschnitte erhalten die Profile, denen sie entsprechen. Ein leerer Name wird
aus den Engines gebildet.

### Format
<!-- en: format 29a36e7c -->

Der Tooltip jedes Formats sagt, was es ist.

- *Match*: zwei Engines, A (die getestete Engine; jede Statistik ist aus ihrer Sicht) und B (die Vergleichsbasis). Eine
  feste Zahl von Partien, bis du es anhältst, oder ein SPRT: elo0, elo1, alpha, beta, Modell (`normalized` verwendet
  nElo-Grenzen wie fishtest und OpenBench, `logistic` klassische Elo, `bayesian` BayesElo) und eine optionale
  Höchstzahl von Partien. Die Profile SPRT Verbesserung und keine Verschlechterung geben die üblichen Grenzen vor, und
  die Zeile unter den Grenzen sagt, wie viele Partien der Test voraussichtlich dauert. *Rechner…* zeichnet, wie der
  SPRT-Rechner von fishtest, die Wahrscheinlichkeit, dass H1 angenommen wird, und die erwartete Zahl der Partien in
  Abhängigkeit vom wahren Elo-Unterschied, aus einer erwarteten Remisquote und dem RMS-Bias der Eröffnungen; *Diese Grenzen
  verwenden* kopiert die ihm gegebenen Grenzen zurück in die Einrichtung. *Elo-Verschiebung* (standardmäßig 0) wird zu
  beiden Grenzen addiert, um gegen eine Version zu testen, die bekanntlich um so viel stärker oder schwächer ist:
  [0, 3] mit einer Verschiebung von 5 ergibt den SPRT [5, 8], den die Zusammenfassung, die Übersicht und der
  Headless-Modus als `SPRT [0, 3] + 5` schreiben; der Rechner und die Zeile unter den Grenzen berücksichtigen sie.
  *Min. Partien* (standardmäßig 0, keine) hält die Entscheidung zurück, bis so viele Partien gespielt sind, während die
  LLR weiter berechnet und angezeigt wird, um einen kurzen Test vor einem frühen Glückstreffer zu schützen: Eine LLR
  jenseits einer Grenze vor diesem Punkt wird unter dem LLR-Balken gemeldet, und das Match geht weiter; sind die
  Partien erreicht, beendet das nächste Paar, das mit der LLR jenseits einer Grenze endet, das Match. Eine Höchstzahl
  an Partien unter der Mindestzahl beendet das Match ohne Entscheidung. Das ist der schnellste Weg, zwei Versionen
  einer Engine zu vergleichen, und mehr braucht es nicht.
  *Farben* (standardmäßig *Abwechselnd*) legt fest, wer Weiß hat: A und B im Wechsel, oder *A immer mit Weiß* oder *B
  immer mit Weiß*, wie fastchess' `-noswap` (und `-reverse`), für Vorgabepartien, in denen die stärkere Engine immer die
  benachteiligte Seite haben muss, oder für eine Eröffnung, die nur von einer Seite erprobt wird. Die Partien gehen
  weiterhin in Paaren, beide mit denselben Farben, aber jede Partie hat ihre eigene Eröffnung, die nächste des Buchs
  (wie fastchess' `-games 1`): Die Paare und ihre Statistiken bleiben, das Ergebnis enthält den Vorteil von Weiß, und
  der RMS-Bias, den das Vertauschen der Farben aufhebt, wird nicht angezeigt. Die Zusammenfassung und der Kopf des
  Matches zeigen es an.
- *Rundenturnier*: Jede Engine spielt gegen jede andere, *Zyklen*-mal, jedes Mal zwei Partien mit vertauschten Farben
  (was cutechess Runden nennt, mit 2 Partien pro Begegnung). Die Runden folgen Berger-Tabellen, so dass jede Engine
  einmal pro Runde spielt.
- *Gauntlet*: Die ersten Engines, so viele wie *Getestete Engines* angibt (die gesetzten), spielen gegen jede andere,
  die anderen spielen nicht gegeneinander.
- *Schweizer System*: eine Zahl von *Runden*. Jede Runde besteht aus zwei Partien mit vertauschten Farben gegen eine
  Engine mit ähnlicher Punktzahl; niemand trifft zweimal auf denselben Gegner, wenn es sich vermeiden lässt. Bei einer
  ungeraden Zahl von Engines bekommt die am niedrigsten platzierte Engine, die noch kein Freilos hatte, eines, das
  einen Punkt wert ist.
- *K.-o.-System*: Die Engines werden in der Reihenfolge ihrer Liste gesetzt, die oben Gesetzten bekommen die Freilose,
  wenn das Feld keine Zweierpotenz ist, und die ersten beiden Gesetzten können sich erst im Finale treffen. Ein Match
  besteht aus so vielen Partiepaaren, wie *Partiepaare pro Match* angibt; ein unentschiedenes Match bekommt jeweils ein
  weiteres Paar, bis zu zehn, danach kommt die höher gesetzte Engine weiter.
- *Mannschaftskampf*: zwei Mannschaften von Engines, um eine Familie von Engines oder eine Reihe von Versionen als
  Ganzes mit einer anderen zu vergleichen. *Paarungen*: *Jeder gegen jeden*, jede Engine einer Mannschaft gegen jede
  Engine der anderen, ein Gegner pro Runde (ein Scheveninger System; die Mannschaften dürfen verschieden groß sein),
  oder *Brett gegen Brett*, die erste Engine jeder Mannschaft gegen die erste der anderen, die zweite gegen die zweite
  und so weiter (gleich große Mannschaften). Jede Paarung wird *Zyklen*-mal gespielt, jedes Mal zwei Partien mit
  derselben Eröffnung und vertauschten Farben; die Farben der ersten Partie wechseln von Brett zu Brett und von einem
  Zyklus zum nächsten. *Mannschaft A* und *Mannschaft B* benennen die Mannschaften („Mannschaft A“ und „Mannschaft B“
  ohne Namen); daneben steht, wie viele Engines jede hat. Die Mannschaften werden in der Teilnehmerliste gebildet: Die
  obere Hälfte der Liste ist Mannschaft A, die untere Mannschaft B (Mannschaft A bekommt die überzählige), bis Engines
  einer Mannschaft zugeteilt werden.

### Teilnehmer und Bibliothek
<!-- en: participants-and-the-library a7ef2e55 -->

**Teilnehmer**, mit der **Bibliothek** daneben (in einem schmalen Fenster darunter). Jeder Teilnehmer ist eine Zeile:
sein Platz (ein Stern für die gesetzten Engines eines Gauntlets), sein Name, seine Referenz-Elo, was er anders spielt
als die gemeinsamen Einstellungen („Hash 512 MB · 80 % der Zeit“) und ⋯, hervorgehoben, wenn er eigene Einstellungen
hat. ⋯, ein Rechtsklick oder die Menütaste öffnet sie: sein eigener `Hash`, eigene `Threads`, feste Knoten oder Tiefe,
Zeitvorgabe (ein Prozentsatz der Bedenkzeit, für eine Uhr), sein eigenes *Buch*, *Diese Engine noch einmal, mit einem
anderen Buch*, die Verschiebe-Befehle und *Entfernen*. Die Reihenfolge der Liste ist die Setzliste: Ziehe Teilnehmer,
um sie umzuordnen (die Gesetzten zuerst), oder drücke Strg+Hoch oder Strg+Runter, um sie ganz nach oben oder unten zu
schicken, Alt+Hoch oder Alt+Runter, um sie um eins zu verschieben. Strg- oder Umschalt-Klick wählt mehrere aus; Entf
(oder *Entfernen*) entfernt sie.

In einem Mannschaftskampf ist der Platz die Mannschaft und das Brett, A1, A2… für Mannschaft A, B1, B2… für
Mannschaft B, in der Reihenfolge der Liste (Brett gegen Brett spielt A1 gegen B1). Das Menü ⋯ teilt den Teilnehmer
einer Mannschaft zu; *Mannschaft A* und *Mannschaft B* unter der Liste teilen die ausgewählten Teilnehmer einer
Mannschaft zu, und *Hälften* bildet die Mannschaften wieder aus den Hälften der Liste (die Befehlspalette bietet
dasselbe). Eine Engine, die nach der Wahl der Mannschaften hinzukommt, tritt der kleineren bei. Die Zusammenfassung
sagt, wie viele Engines jede Mannschaft hat, und ein Start wird mit Grund abgelehnt, wenn eine Mannschaft leer ist
oder, bei Brett gegen Brett, wenn die Mannschaften verschieden groß sind.

Die Bibliothek listet die aktiven Engines mit ihrer Referenz-Elo auf, sortiert nach Name oder nach Elo (Klick auf die
Spalte), ein `!` bei denen mit einem Problem (Programm fehlt oder neu gebaut, letzte Prüfung fehlgeschlagen: der
Tooltip sagt, was) und ein ✓ bei denen, die schon mitspielen. Der Filter durchsucht die Namen, die Namen, die die
Engines melden, ihre Autoren, Programme, Notizen und Gruppen (Strg+F gibt ihm die Tastatur; Enter darin fügt die
ausgewählten Engines hinzu, oder die erste angezeigte); ein Chip pro Gruppe zeigt nur diese Gruppe, und *Alle
angezeigten hinzufügen* fügt die ganze Gruppe hinzu. Doppelklicke eine Engine, oder wähle mehrere mit Strg oder
Umschalt aus (Strg+A: alle angezeigten) und drücke Enter, um sie hinzuzufügen; oder ziehe sie an eine Stelle unter den
Teilnehmern. *Vorschlagen…* wählt die N aktiven Engines aus, die funktionieren und deren Referenz-Elo der des ersten
Teilnehmers (der getesteten Engine eines Gauntlets) am nächsten liegt, standardmäßig höchstens 150 entfernt; Enter
fügt sie hinzu. Bei einem Match füllt die Bibliothek A, dann B (B wird ersetzt, wenn beide gewählt sind), und eine
auf A oder B gezogene Engine nimmt deren Platz ein.

### Eigene Bücher der Teilnehmer
<!-- en: participants-own-books 580e52cc -->

*Buch*: ein eigenes Eröffnungsbuch des Teilnehmers, wie `book=`/`bookdepth=` bei Cute Chess: ein Polyglot- (`.bin`)
oder PGN-Buch der Seite Bücher und wie tief ihm gefolgt wird, in Halbzügen ab Partiebeginn, die Eröffnung des Turniers
eingeschlossen (*alles*: solange das Buch einen Zug hat; bei einem PGN-Buch die ersten 40 Halbzüge seiner Partien).
Ein PGN-Buch wird zuerst in ein Polyglot-Buch umgewandelt: die Züge jeder Partie bis zu dieser Tiefe, jeder Zug
gewichtet nach der Zahl der Partien, die ihn spielen; Partien mit einem illegalen Zug werden weggelassen und gezählt.
Das läuft im Hintergrund, sobald das Buch gewählt ist (Fortschritt auf dem Knopf *Buch*, in seinem Popup und in der
Zusammenfassung), und wird im Ordner `book-cache` des Profils aufbewahrt, bis sich die Datei oder die Tiefe ändert;
ein Start, der vorher kommt, wartet darauf. Ein Buch, das sich nicht vorbereiten lässt, zeigt in seinem Popup, warum,
mit *Erneut versuchen*; jeder Start versucht es ebenfalls erneut. Ein Turnier spielt aus dem Buch, das beim Start oder
Fortsetzen vorbereitet war, auch wenn sich die PGN-Datei inzwischen ändert.

Solange die Stellung in diesem Buch steht, spielt der Teilnehmer sofort den Zug des Buchs, gewählt nach den Gewichten
des Buchs, so wie der *Zufall* der Eröffnungen sie gestaltet (auch im Popup *Buch*, gleich für jedes Buch des
Turniers: siehe [Eröffnungen](#eröffnungen)); dann übernimmt seine Engine. Die Eröffnungen des Turniers, falls
vorhanden, werden zuerst gespielt, von jeder Engine; EPD-Bücher liefern nur diese und sind in der Liste ausgegraut. Um
Bücher mit einer Engine zu vergleichen, wähle *Diese Engine noch einmal, mit einem anderen Buch* in ihrem Menü ⋯:
Jede Kopie spielt als `<engine> (<book>)`, so dass ihre Ergebnisse und ihre Wertung getrennt bleiben. In einem Match
sitzen die Knöpfe *Buch* neben A und B, und eine Engine kann mit zwei Büchern gegen sich selbst spielen. Buchzüge
werden im PGN mit `{book}` markiert; steht die Maus über einem Namen in der Tabelle, erscheint sein Buch.
WinBoard-Engines können auch eines haben.

### Engine-Einstellungen
<!-- en: engine-settings 7a93d075 -->

*Hash* und *Threads*, für jede Engine erzwungen, auch in einem Match (0 = die eigenen jeder Engine; die eines
Teilnehmers aus seinem Menü ⋯ gehen vor); *Syzygy-Tablebases für die Engines* gibt die Tablebase-Verzeichnisse
(dieselben wie die der Abschätzung) an jede Engine mit einer Option `SyzygyPath`, und *Eigene Bücher der Engines aus*
setzt `OwnBook` auf false, wo es existiert, so dass jede Partie aus den Eröffnungen des Turniers gespielt wird. Beide
sind standardmäßig aus.

### Bedenkzeit
<!-- en: time-control 672cb041 -->

Grundzeit und Inkrement in Sekunden, optional Züge pro Periode, und eine Toleranz, bevor eine Partie durch Zeit
verloren ist; eine feste Zeit pro Zug (`go movetime`, mit derselben Toleranz); eine feste Zahl von Knoten pro Zug; oder
eine feste Tiefe in Halbzügen (`go depth`), bei der keine Engine durch Zeit verlieren kann.

### Parallelität
<!-- en: concurrency c02ee2cf -->

Die Zahl der gleichzeitig gespielten Partien. Im automatischen Modus wird sie beim Start ermittelt:

- feste Knoten oder Tiefe: eine Partie pro Hardware-Thread, da Hyper-Threading die gespielten Züge nicht ändert;
- mit einer Uhr: physische Kerne − 1, damit sich die Engines nicht gegenseitig bremsen.

Die Zeile der Einrichtung und der Hinweis unter dem Feld sagen, welche Regel die Zahl bestimmt hat. *CPU-Affinität* (standardmäßig aus;
nicht unter macOS, das keine hat) bindet jede Partie an einen Prozessor, den sich ihre beiden Engines teilen, da sie
nie gleichzeitig denken. fastchess unterstützt das nur für Engines mit einem Thread, daher wird es automatisch
ausgeschaltet, wenn eine Engine `Threads` > 1 hat. Eine Warnung erscheint, wenn Parallelität × Threads die
Hardware-Threads übersteigt (oder, mit einer Uhr, die physischen Kerne). *Spielen auf* wählt die Maschine, auf der das
Turnier spielt (siehe [Ein Turnier auf einer Maschine](#ein-turnier-auf-einer-maschine)).

### Eröffnungen
<!-- en: openings 44656411 -->

Ein EPD-, PGN- oder Polyglot-Buch (`.bin`), also eine Eröffnungssammlung, zufällige oder sequenzielle Reihenfolge, die
Zahl der aus einem PGN-Buch übernommenen Halbzüge, der Startindex und der Zufallsstartwert (0 = eine neue Reihenfolge
für jedes Turnier). Jede Eröffnung wird zweimal gespielt, mit vertauschten Farben (in einem Match, dessen *Farben*
festgelegt sind, jede Partie aus ihrer eigenen Eröffnung).

Einem Polyglot-Buch wird von der Ausgangsstellung bis zu einer Tiefe gefolgt (8 Halbzüge, sofern nicht anders
eingestellt), wobei die Züge wegfallen, die weniger als ein Zehntel des besten Zugs ihrer Stellung wiegen; die
erreichten Stellungen werden einmal in `<book>-<N>plies.epd` im Turnierordner geschrieben und wie ein EPD-Buch
gespielt. In zufälliger Reihenfolge werden sie in der Reihenfolge geschrieben, in der sie gezogen werden, jede einmal:
Eine Linie kommt umso früher, je wahrscheinlicher sie nach den Gewichten ihrer Züge ist, so dass ein Turnier, das nur
einen Teil des Buchs spielt, zuerst seine Hauptlinien spielt; derselbe Startwert zieht dieselbe Reihenfolge erneut. In
sequenzieller Reihenfolge behalten sie die Reihenfolge des Buchs. *Zufall* (−100 bis 100, standardmäßig 0) legt fest,
wie die Gewichte zählen, so wie Polyglot und Cute Chess einen Buchzug wählen: 0 die Gewichte, wie sie sind, −100 nur
den besten Zug jeder Stellung (eine Linie, es sei denn, mehrere teilen das beste Gewicht), 100 jeden Zug gleich (die
seltenen Züge kommen zurück); dazwischen quadriert −50 die Gewichte, und 50 nimmt die Quadratwurzel aus ihnen. Er wählt auch
die Züge der eigenen Bücher der Teilnehmer und wird für sie auch bei einem EPD- oder PGN-Buch angezeigt; die
Zusammenfassung nennt ihn, und die Schlusszeile der Tabellenkopien gibt das Buch mit seiner Tiefe an und, bei einem
Polyglot-Buch, diesen Zufall.

In einem Rundenturnier, einem Gauntlet, einem Schweizer System oder einem Mannschaftskampf gibt *Dieselbe Eröffnung
für alle Paare einer Runde* jedem Partiepaar einer Runde dieselbe Eröffnung (bei Brett gegen Brett ist ein Zyklus eine
Runde: Jedes Brett spielt dieselbe Eröffnung), für jede Runde die nächste des Buchs, so dass keine Engine leichtere
Eröffnungen bekommt als die anderen ihrer Runde (eine später hinzugefügte Engine holt in eigenen Runden auf, jede mit
ihrer Eröffnung).

*Unausgeglichene Eröffnungen markieren* (standardmäßig aus) zeigt, während die Partien eintreffen, die Eröffnungen, die
die Partie von sich aus entscheiden: Eine Partie wird bei ihrem Ende markiert, wenn beide Engines für jeden ihrer
ersten Züge nach dem Buch (standardmäßig 4; die Buchzüge aus dem eigenen Buch eines Teilnehmers zählen als Buchzüge)
dieselbe Seite mit mindestens der Bewertung (standardmäßig 200 cp) vorne sahen. Die Option liest die Bewertungen der
Engines, eine Engine, die keine angibt, markiert also nichts; die Ergebnisse werden nicht geändert. Der Tab *Partien* filtert
solche Partien (*Unausgeglichene Eröffnung*), der Tab *Eröffnungen* markiert ihre Eröffnungen mit einem `!` und zählt
sie, und die Übersicht zählt die Partien. *Das Paar mit einer anderen Eröffnung neu spielen* annulliert ein solches
Paar, sobald seine beiden Partien gespielt sind (sie bleiben in den PGN-Dateien und im Journal, wie jede annullierte
Partie), und spielt es neu aus einer Eröffnung am Ende des Buchs (in der Spielreihenfolge des Buchs), die das Turnier
zuletzt spielen würde, so dass sich die Eröffnungen der kommenden Paare nicht verschieben; höchstens dreimal für ein
Paar, danach zählt das Paar, markiert. Die Konsole und die Übersicht sagen, wie viele Eröffnungen ersetzt wurden.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer Random, in den Eröffnungen: ohne Buch die 960 Startstellungen in zufälliger Reihenfolge (einmal in
`chess960.epd` im Turnierordner geschrieben); mit einem Buch seine Stellungen. Jede Engine muss Chess960 spielen:
UCI-Engines mit der Option `UCI_Chess960`, WinBoard-Engines mit der Variante `fischerandom`. Die Zusammenfassung nennt
die, die es nicht tun, und ein solches Turnier startet nicht.

### Abschätzung
<!-- en: adjudication a18dca1e -->

Remis (ab einer Zugnummer, für N aufeinanderfolgende Züge, unter einer Bewertung), Aufgabe (N aufeinanderfolgende Züge
über einer Bewertung, optional mit Zustimmung beider Engines), eine Höchstzahl von Zügen und Syzygy-Tablebases.

### PGN und Sonstiges
<!-- en: pgn-and-misc 6762bab8 -->

- Name der Veranstaltung: Sein Tooltip nennt die beiden PGN-Dateien, in die jede Partie geschrieben wird.
- *Jede Partie zusätzlich anhängen an*: eine eigene PGN-Datei, an die jedes Turnier seine Partien anhängt, wie
  `-pgnout` bei cutechess (nie überschrieben, im Kommentarstil ChessBase / Lichess oder cutechess), in der Reihenfolge
  des Spielplans: die beiden Partien einer Eröffnung nebeneinander, die Runden der Reihe nach, egal in welcher
  Reihenfolge sie enden. Eine Partie wartet dort, bis die vor ihr geschrieben sind; ein Fortsetzen macht weiter; lässt
  sich die Datei nicht schreiben (gesperrt, verschoben), warten die Partien, und das Turnier geht weiter, mit einer
  Warnung in der Übersicht. Ein Absturz oder ein Stromausfall während des Anhängens schreibt Partien nie doppelt: Das
  Fortsetzen prüft die Datei und vervollständigt einen abgebrochenen Schreibvorgang, und es entfernt nie etwas aus ihr.
- Zusätzliche Felder in den Zugkommentaren (nodes, seldepth, nps, pv, tbhits und die Entscheidungszeit `dt=`: wann
  sich die Engine auf den gespielten Zug festgelegt hat; `wdl=W/D/L` wird für die Engines, die es melden, immer
  geschrieben), standardmäßig an, da die Diagramme des Tabs *Partien* sie lesen (pv macht das PGN merklich größer).
- *Eine abgestürzte Engine neu starten* und *Das Turnier anhalten, wenn eine Engine nicht startet* (standardmäßig aus:
  siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)).
- Das Engine-Log jeder Partie behalten (für die Partien, die schlecht enden, wird es immer behalten).
- *Matt-PVs prüfen*: Die PV einer Mattbewertung muss die richtige Länge haben und im Matt enden. Jede PV wird auf
  illegale Züge geprüft, auf Züge nach dem Ende der Partie und auf einen besten Zug, der nicht ihr erster Zug ist; die
  Züge mit einer falschen PV werden im Tab Partien markiert und ins Protokoll geschrieben.

### Tipps für die Einstellungen
<!-- en: tips-for-the-settings 62afdb4d -->

- **Zwei Versionen einer Engine vergleichen**: ein SPRT, Verbesserung [0, 3] für eine Änderung, die etwas bringen
  soll, keine Verschlechterung [-5, 0] für eine Vereinfachung oder ein Aufräumen. Zuerst eine kurze Bedenkzeit
  (10+0.1), dann eine lange (60+0.6) für das, was besteht. Verwende ein Buch mit ausgeglichenen Stellungen, nicht die
  Ausgangsstellung: Sonst wäre jede Partie eine von wenigen Eröffnungen.
- **Parallelität**: Lass sie automatisch. Mit einer Uhr bremsen sich die Engines bei mehr Partien als physischen
  Kernen weniger einem gegenseitig, und die Ergebnisse verschieben sich; mit Knoten oder einer Tiefe pro Zug darf jeder
  Hardware-Thread spielen.
- **Threads**: 1 pro Engine zum Testen; viele Partien mit je einem Thread liefern mehr Ergebnisse pro Stunde als wenige
  Partien mit vielen Threads. Gib jeder Engine dieselben `Threads` und denselben `Hash` unter
  *Engine-Einstellungen*.
- **Hash**: genug für die Bedenkzeit (16 bis 64 MB bei 10+0.1, 256 MB bei 40/15 und länger); die Zusammenfassung sagt,
  wenn der Hash der Partien nicht mehr in den Speicher passt.
- **Abschätzung**: Das Standardprofil spart in langen Turnieren Zeit, ohne die Ergebnisse stark zu verändern; lass sie
  aus, um die Endspiele einer Engine zu testen, oder verwende die Tablebases.
- **Eröffnungen**: Zufällige Reihenfolge mit festem Startwert macht einen Test exakt wiederholbar; *Dieselbe Eröffnung
  für alle Paare einer Runde* ist in einem Rundenturnier mit wenigen Zyklen fairer.
- **Mehrere Turniere**: Gib dem wichtigsten ein größeres Gewicht, statt die anderen zu pausieren (siehe
  [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig)).

## Ein Turnier durchführen
<!-- en: running-a-tournament 9bcba803 -->

Ein laufendes oder geöffnetes Turnier hat einen Arbeitsbereich mit einem Kopf (Name, Fortschritt, *Pause*, *Stopp*,
*Klonen*, *Ordner*, *Layout*) und Bereichen, standardmäßig als Tabs gezeigt:

| Tab | Was er zeigt |
|---|---|
| Übersicht | Die bisherigen Ergebnisse und die Statistik, die Diagramme, die Checkliste vor dem Einsenden |
| Tabelle | Die Tabelle als Liste oder als Kreuztabelle, die Gegner einer Engine, die Kopien für ein Forum |
| Mannschaften | Ein Mannschaftskampf: der Stand der Mannschaften und die Kreuztabelle der beiden Mannschaften |
| Turnierbaum | Ein K.-o.-Turnier: der Turnierbaum, Runde für Runde |
| Spielplan | Jedes Partiepaar, und die Änderungen am Turnier |
| Übertragung | Live-Dateien, der TLCS-Server, die Webseite und lichess |
| Live | Die laufenden Partien |
| Partien | Die beendeten Partien, ihre Filter, ihr Nachspielen und ihr Export |
| Eröffnungen | Die Ergebnisse nach Startstellung |

### Übersicht
<!-- en: overview 4199cd3c -->

**Ein Match**: Elo und nElo mit ihrem 95-%-Konfidenzintervall, LOS, Punktzahl, Pentanomial-Zählungen, SPRT-Grenzen und
LLR. *Statistik* (zugeklappt) gibt die Zahlen von fishtest aus den Partien und aus den Paaren an: Punktzahl, Elo, nElo
und BayesElo mit DrawElo, Standardabweichung pro Partie, die LLR von beiden, das Varianzverhältnis von Paaren zu
Einzelpartien, den RMS-Bias der Eröffnungen, die Arten ausgeglichener Paare und die gewonnenen gegenüber den verlorenen
Paaren, die *Kopieren* mitnimmt. Dann die Diagramme: Elo und LLR im Zeitverlauf, Partielängen, wie Partien endeten, und
*Suche pro Engine*.

**Ein Turnier**: die Tabelle (mit *TF*, den Zeitverlusten jeder Engine, orange, sobald es einen gibt; ein *!* neben
einer Engine, die abgestürzt ist oder einen illegalen Zug gespielt hat), darunter die Elo jeder Engine mit ihrem
95-%-Intervall als Diagramm, dann *Suche pro Engine*, der Fortschritt nach Zyklus oder Runde, Partien pro Stunde und
verbleibende Zeit, die neuesten Ergebnisse.

*Suche pro Engine* gibt für jede Engine den Schnitt über die Züge, die sie gesucht hat, ohne Buchzüge: Tiefe, NPS, Zeit
pro Zug, Entscheidungszeit und Prozessornutzung; ein Klick auf eine Spalte sortiert die Engines danach. Neben ihren NPS
sagt ×4, dass die Engine mit 4 Threads sucht: Sie zählt mehr Knoten als mit einem, ohne deshalb schneller zu sein, also
vergleiche ihre NPS mit Engines, die ebenso viele haben. Die NPS brauchen Engines, die sie angeben, und *nps* in *PGN
und Sonstiges*, um beim Fortsetzen zurückgelesen zu werden.

Beide zählen die Partien aus unausgeglichenen Eröffnungen und die ersetzten Eröffnungen, wenn die Einrichtung nach
ihnen sucht, mit *Zeigen* für diese Partien. Ein beendetes Turnier fügt eine *Checkliste vor dem Einsenden* hinzu: alle
Partien gespielt, jedes Paar mit beiden Farben, keine doppelten Partien, keine Abstürze, Zeitverluste oder illegalen
Züge (mit Knöpfen, die diese Partien öffnen), der Hash jeder Engine (angezeigt, nicht bewertet), Ponder aus, das
Eröffnungsbuch, die Tablebases.

### Tabelle
<!-- en: standings 5f5e0950 -->

Punkte, Partien, Zeitverluste (*TF*), Siege/Remis/Niederlagen, Wertungen mit ihrem Konfidenzintervall,
Sonneborn-Berger (Buchholz beim Schweizer System), Freilose. *Tabelle* listet sie mit einer Engine pro Zeile auf;
*Kreuztabelle* (Rundenturniere, bis zu 200 Engines) zeigt jede Paarung. *Reihenfolge:* nach Punkten, Punktzahl % oder
Setzliste (die die Spalten der Kreuztabelle eines Gauntlets an ihrem Platz hält). Ein Klick auf eine Engine listet
darunter ihre Gegner auf: Partien, mit Weiß, mit Schwarz, + = −, Punkte und %, wobei die Paare markiert sind, von denen
nur eine Partie gespielt wurde; ein Gauntlet öffnet sich mit seiner getesteten Engine ausgewählt.

*Kopieren* als Text oder als BBCode für einen Forumsbeitrag (mit einer Zeile, die Format, Bedenkzeit, Partien und
Buch angibt), und in einem Gauntlet *Gauntlet-Tabelle als Text* oder *Gauntlet-Tabelle als BBCode, für ein Forum* (die
getestete Engine, dann ihre Gegner nach ihren eigenen Punkten, jeder mit Punkten, %, Elo ±, W-L-D); *HTML
exportieren…*. Alle haben *TF* wie die Tabelle des Headless-Modus, folgen der gewählten Reihenfolge und enden mit
einer Zeile, die die gespielten von den geplanten Partien angibt, die Remisquote, die Bedenkzeit, das Buch (mit den
übernommenen Halbzügen bei einem PGN-Buch; bei einem Polyglot-Buch seine Tiefe und wie seine Züge gewählt wurden, z. B.
`book:Perfect2023.bin (8
plies, moves by weight)`) und die Veranstaltung. Die Kopien unterschreiben dann mit *RookStudio* und seiner
Version, und die Gauntlet-Tabelle ist mit = statt mit - liniert, damit sich eine in einem Forum gepostete Tabelle von
denen anderer Programme unterscheiden lässt.

### Mannschaften
<!-- en: teams 8c845645 -->

Nur bei einem Mannschaftskampf, im Tab *Tabelle* (wo er sich öffnet) und oben in der *Übersicht*: der Stand der
Mannschaften, „Lynx 17½ – 18½ Otter“, mit den gespielten Partien, der Punktzahl % von Mannschaft A, dem Elo-Unterschied,
für den diese Punktzahl steht, mit seinem 95-%-Intervall und der LOS, über die Partiepaare gezählt, so wie die
Statistik eines Matches (pentanomial); dann die Engines jeder Mannschaft mit ihren Partien, Punkten, %,
Wertung und den Punkten, die jede gegen jede Engine der anderen Mannschaft erzielt hat (B1, B2… als Spalten, der Name
unter dem Mauszeiger): die Kreuztabelle der beiden Mannschaften. *Tabelle* und *Kreuztabelle* zeigen die Engines wie
in jedem Turnier. *Kopieren* und *HTML exportieren…* geben die Stände der Mannschaften und die Engines beider
Mannschaften gegeneinander an, mit der Zeile darunter.

### Turnierbaum und Spielplan
<!-- en: bracket-and-schedule fe207ac0 -->

*Turnierbaum*, nur bei einem K.-o.-Turnier: der Turnierbaum, Runde für Runde, mit dem Stand jedes Matches.
*Spielplan*: jedes Partiepaar nach Zyklus oder Runde, mit seinem Zustand und der Eröffnung, die es spielt (sobald es
begonnen hat, oder sobald seine Runde begonnen hat, wenn sich die Runde eine teilt), und die Änderungen am Turnier
(siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)).

### Live
<!-- en: live 59d5ab65 -->

Die laufenden Partien, als Mosaik von Brettern oder als eine Partie im Fokus mit den Uhren, der laufenden Suche jeder
Engine (Tiefe, Bewertung, Knoten, nps, PV in SAN), der Zugliste und der Partiegrafik (siehe
[Die Partiegrafik](#die-partiegrafik)): die Bewertungen der beiden Engines und in ihren Tabs ihre Gewinnchancen, die
Zeit pro Zug mit der Entscheidungszeit, die Tiefe mit seldepth, die Knoten, die Geschwindigkeit, die
Tablebase-Treffer, wie weit sie auseinanderliegen, oder wie viele Halbzüge eine PV von dem wiederholt, was die PV der
anderen Engine vorhergesagt hat. Die Anteile für Sieg/Remis/Niederlage (`UCI_ShowWDL`, beim Hinzufügen für die Engines
eingeschaltet, die es haben) folgen jeder Suchzeile; der Tab *S/R/N* zeichnet sie als Bänder, die Siege von Weiß
unten.

Wenn der Platz reicht (eine Partie im Fokus, ein großes Panel), hat jede Engine eine Karte: ihr Logo so hoch wie Name
und Uhr (wenn die Logos eingeschaltet sind, siehe [Darstellung](#darstellung)), was sie tut (sie denkt nach, oder der
Zug, den sie spielte), ihre Suche Zahl für Zahl (Tiefe und seldepth, Bewertung, Knoten, Geschwindigkeit, die
Tablebase-Treffer und die Hash-Füllung, wenn die Engine sie sendet, die bisher auf den Zug verwendete Zeit) und ihre
ganze PV; die wartende Engine zeigt, was sie beim Zug sagte, mit dem Moment, in dem sie sich entschied. Neben dem Mosaik
behält das Panel eine Zeile mit Zahlen und die ersten 12 Halbzüge der PV. *Bewertung für die Seite am Zug* | *Bewertung
für Weiß* rechts im Tab legt die Seite aller Bewertungen darin fest: die der Seite am Zug, wie fastchess sie ins PGN
schreibt, oder die von Weiß; die Bewertungsleiste und das Großbild zeigen immer die Sicht von Weiß.

Über den Zügen: die Eröffnung, der ECO-Code und der Name der tiefsten Stellung der Partie, die die Tabelle der benannten
Eröffnungen kennt (Buch- und Engine-Züge gleichermaßen, sodass auch eine Zugumstellung nach dem Buch benannt wird), mit
dem letzten Buchzug; und sobald die Syzygy-Tablebases des Turniers (die seiner Abschätzung, sonst die den Engines
gegebenen) die gezeigte Stellung kennen, ihr Urteil: die Seite, die gewinnt, mit der DTZ (die Halbzüge bis zum nächsten
Schlag oder Bauernzug: Syzygy-Tablebases kennen keine Distanz zum Matt), oder Remis, die 50-Züge-Regel nach dem Zähler
der Partie gezählt, außer die Abschätzung ignoriert sie. Jede Stellung wird einmal abgefragt, außerhalb der Threads, die
die Partien spielen. In der Liste hat jeder Engine-Zug seine Bewertung, Tiefe und Zeit; beim Darüberfahren kommen der
Moment, in dem sich die Engine entschied, und die Antwort hinzu, die sie erwartete (der zweite Zug ihrer PV: fastchess
spielt ohne Pondern). Buchzüge sagen *Buch*, eine Zeile markiert das Ende des Buchs, und der Zug, über den nachgedacht
wird, sagt *denkt nach*.

Das Mosaik hat ein Brett je Partie, die das Turnier gleichzeitig gespielt hat, nicht je Partie, die die Parallelität
erlaubt: mit weniger Partien als das werden die Bretter größer. Ein Brett, dessen Partie beendet ist, behält seine
Endstellung, bis die nächste Partie seines Platzes dort beginnt: gegen Ende eines Turniers bleiben die Bretter an ihrem
Platz, jedes mit dem Ende seiner letzten Partie. Bräuchten die Bretter einen Rollbalken, nimmt das Mosaik der Partie im
Fokus Breite weg, bis ihr 40 % der Seite bleiben.

Unter dem Brett: der 50-Züge-Zähler (Halbzüge seit dem letzten Schlagen oder Bauernzug, von 100), der
Materialunterschied mit den Figuren, die eine Seite mehr hat, und die verbleibenden Steine, mit *TB* markiert, wenn die
Tablebase-Abschätzung die Partie beenden kann; *Details unter den Brettern* fügt sie dem Mosaik hinzu. Darunter die
Knöpfe durch die Züge (siehe [Bretter und ihre Knöpfe](#bretter-und-ihre-knöpfe)): Die Partie im Fokus oder ein
abgetrenntes Brett lässt sich zurückblättern, während die Partie weiterläuft (auch mit ← → Pos1 Ende, einem Klick auf
einen Zug der Liste oder auf die Kurve), wobei die frühere Stellung mit ihrem Zug in der Liste und auf der Kurve
markiert gezeigt wird; *Zurück zum aktuellen Zug* (oder Ende, oder ein Klick auf den letzten Zug) lässt es der Partie
wieder folgen, wie bei jeder neuen Partie.

*Pfeile* (standardmäßig an) zeichnet den Zug, an den die Seite am Zug denkt, in der Farbe ihrer Engine und die
erwartete Antwort, heller, in der Farbe des Gegners, auf dem Brett im Fokus und den abgetrennten (nicht im Mosaik),
wenn es den aktuellen Zug zeigt. In einem Gauntlet zeigt *Getestete Engine unten* (standardmäßig an) jede neue Partie
von der Seite der getesteten Engine; Drehen (F oder sein Knopf) dreht das Brett weiterhin bis zur nächsten Partie um.
*Abtrennen* öffnet ein Brett in einem eigenen Fenster. Ein Rechtsklick auf ein Brett beendet seine Partie mit einem
Ergebnis (siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)). *Großbild* zeigt die verfolgte Partie
fensterfüllend (siehe [Großbild](#großbild)).

### Großbild
<!-- en: big-screen 14c8f0d6 -->

Für einen Fernseher, einen Verein oder einen Stream zeigt *Großbild* (im Tab *Live*, auf einem abgetrennten Brett
oder in der Befehlspalette) eine Partie fensterfüllend, hell auf Schwarz: die Namen und Uhren der Spieler in großer
Schrift (die Seite am Zug und ihre laufende Uhr in Gelb, eine Uhr unter zehn Sekunden in Rot), die Bewertungsleiste,
die Bewertung, Tiefe und Geschwindigkeit der Suche, den letzten Zug auf dem Brett umrahmt und groß geschrieben, die
Eröffnung, die Linien der Engines (*Linien*, standardmäßig an: die der Seite am Zug, während sie sucht, in Gelb, die
der anderen von ihrem letzten Zug, höchstens zwei Zeilen je), das Urteil der Tablebases, sobald sie die Stellung
kennen, und die Züge in großer Schrift, der letzte markiert; das Ergebnis über dem Brett, sobald die Partie vorbei ist. Sein Text folgt
der Größe des Fensters und hat eine eigene Größe (*A-* / *A+* oder die Tasten + und -). Es zeigt die verfolgte Partie,
wechselt alle N Sekunden zwischen den laufenden Partien (*Jede Partie der Reihe nach*) oder folgt *Die
interessanteste*: der Partie, die dem Ausgleich am nächsten ist (unter gleich nahen der längsten), und bleibt bei ihr,
solange das so bleibt. Die
Bedienelemente erscheinen drei Sekunden lang, wenn sich die Maus bewegt (der Mauszeiger verschwindet mit ihnen); ← und
→ wechseln die Partie, F11 schaltet das Fenster auf Vollbild, Esc verlässt es. Ein auf einen anderen Bildschirm (den
Fernseher) verschobenes abgetrenntes Brett zeigt es in seinem eigenen Fenster, wobei F11 dieses Fenster auf Vollbild
schaltet, während das Hauptfenster weiterläuft; im Hauptfenster angedockt, zeigt das Hauptfenster es.

### Partien
<!-- en: games ba4a9604 -->

Die beendeten Partien mit ihrer Eröffnung (ECO-Code und Name) und ein Brett zum Nachspielen: die Knöpfe darunter,
← → Pos1 Ende und F, oder ein Klick auf einen Zug oder auf die Grafik; dieselben Details darunter und dieselbe
Partiegrafik wie in *Live* (siehe [Die Partiegrafik](#die-partiegrafik)), aus den Feldern, mit denen das PGN
geschrieben wurde: Ein Diagramm, dessen Feld das Turnier nicht geschrieben hat, steht ausgegraut im Menü `⋯` der
Grafik und sagt das, mit einem Knopf zu den PGN-Optionen der Einrichtung.

**Filter**: nach Ergebnis (Siege, Remis, Niederlagen, Zeitverluste, Abstürze und illegale Züge, Abschätzungen, falsche
PVs, und die Partien einer als unausgeglichen markierten Eröffnung, wenn die Einrichtung nach ihnen sucht); nach dem,
was die Bewertungen sagen (die Engines sind sich 3 Züge in Folge um je N Bauern uneinig, die Bewertung einer Engine
springt von einem ihrer Züge zum nächsten um N Bauern, der Verlierer glaubte, N Bauern besser zu stehen; die Partie
öffnet sich an dieser Stelle); und nach Eröffnung (ein ECO-Code oder sein Anfang, ein Bereich wie `B20-B99`, oder
Wörter des Namens).

**Export** einer, aller oder der gefilterten Partien: *Alle exportieren…* schlägt `<name>.pgn` neben dem Ordner des
Turniers vor; die eigenen PGN-Dateien des Turniers werden als Ziel abgelehnt. *Für eine Rangliste exportieren…*
schreibt die Partien so, wie Ranglisten-Tester (z. B. CCRL) sie einsenden:
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, auf Wunsch auch als Zip; die Spieler so benannt, wie
die Listen sie schreiben, `<engine> 64-bit 4CPU` (für jeden Teilnehmer änderbar), die Runden neu nummeriert, die Tags
`Event` und `Site` gesetzt, die annullierten Partien und die Duplikate weggelassen, und die durch einen Absturz oder
Hänger verlorenen ebenso, sofern nicht abgehakt; der `PlyCount` einer durch einen illegalen Zug verlorenen Partie
zählt nur die geschriebenen Züge. Der Tester und der Ort werden gespeichert.

*Engine-Log* öffnet das Log einer Partie, die eines hat (siehe [Dateien](#dateien)). Ein Rechtsklick auf eine Partie
spielt sie neu, annulliert sie oder setzt ihr Ergebnis (siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)).

### Der Tab Eröffnungen
<!-- en: the-openings-tab c51dd29a -->

Ergebnisse nach Startstellung: ihre Partiepaare, die Punktzahl von Weiß, die Remis, und wie viele Paare Weiß zweimal
gewonnen hat, Schwarz zweimal (die Eröffnung bevorzugt eine Farbe) oder eine Engine zweimal (sie unterscheidet die Engines),
mit der durchschnittlichen Länge. Sortierbar nach jeder Spalte, gefiltert auf die unausgeglichenen, die remisierten
oder die entschiedenen, oder auf den Filter *Während der Partien als unausgeglichen markiert* (siehe
[Eröffnungen](#eröffnungen) in der Einrichtung: mit `!` markiert, ihre Zahl neben der Gesamtzahl, mit den ersetzten
Eröffnungen). *Als EPD exportieren…* schreibt die gezeigten Stellungen, um sie aufzubewahren oder ein Buch zu
bereinigen, wobei *Ohne die unausgeglichenen* (standardmäßig an, gezeigt, sobald eine Eröffnung markiert ist) die
markierten weglässt. Ein Klick zeigt die Stellung und ihre Partien. In einem Match, dessen *Farben* festgelegt sind, hat
jede Partie ihre eigene Eröffnung: gezählt werden die Partien, und die Spalten und Filter zu den Paaren werden nicht
angezeigt.

### Bereiche und Layouts
<!-- en: panels-and-layouts 9ed9b915 -->

Jeder Tab ist ein Bereich. Zieh einen an seinem Tab, um ihn an einer Seite eines anderen Bereichs oder als Tab daneben
anzudocken, oder lass ihn woanders los, damit er frei schwebt (auch auf einen zweiten Bildschirm unter Windows und
Linux/X11, nicht unter Wayland und nicht mit Software-Rendering). Die verfolgte Partie in *Live* zerfällt in die
Bereiche *Brett*, *Spieler*, *Suchen*, *Züge* und *Diagramm*; die Teile, deren Bereich nicht geöffnet ist, bleiben
neben dem Mosaik in *Live*, und abgetrennte Bretter docken wie die anderen Bereiche an. Schließe einen Bereich mit dem
x auf seinem Tab; hol ihn über *Layout* > *Bereiche* oder mit *… anzeigen* in der Befehlspalette zurück, dann erscheint
er angedockt.

*Layout* bietet außerdem fertige Layouts an (*Reiter*, wie standardmäßig; *Eine Partie verfolgen*: das Brett links,
die Spieler, Suchen und das Diagramm rechts, die Tabelle darunter; *Großes Turnier*: das Mosaik neben der Tabelle;
*Übertragung*: ein großes Brett mit den Spielern, Suchen und der Tabelle), *Layout speichern unter…* (deine eigenen,
nach Namen, in `layouts.json` im Profil gespeichert), *Löschen* und *Standard-Layout*. Ein aus dem Arbeitsbereich
herausgenommener Bereich bleibt auf den anderen Seiten auf dem Bildschirm; die Anordnung bleibt zwischen den Sitzungen
erhalten.

### Prozessornutzung
<!-- en: processor-use e5234cbe -->

Während Partien gespielt werden, wird die Prozessorzeit der Prozesse jeder Engine (einschließlich der Engine hinter
einem WinBoard-Adapter) bei jedem `go` und jedem Zug gemessen. Die Übersicht zeigt für jede Engine die Kerne, die sie
beschäftigt, während sie denkt, und den Anteil eines Kerns, den sie nutzt, während ihr Gegner denkt, und warnt, wenn
eine Engine deutlich mehr Kerne nutzt als ihre `Threads` oder auf der Zeit ihres Gegners denkt (Pondern, das nicht
verlangt wurde, ein Hintergrund-Thread): was eine Partie unfair gegenüber dem Gegner und den anderen laufenden Partien
macht. Sie braucht von beidem je 20 s, um das zu beurteilen, und zählt jede gespielte Partie, im Fenster oder headless
(`--run`): Was die Engines jeder Partie verbraucht haben, wird in `processor.tsv` im Turnierordner geschrieben, so dass
ein Fortsetzen es behält. Sie warnt nur; sie entscheidet nie eine Partie.

### Pausieren, anhalten und fortsetzen
<!-- en: pause-stop-and-resume f6885fa5 -->

**Pause** startet kein neues Partiepaar. Laufende Partien werden zu Ende gespielt, ebenso die zweite Partie eines
Paars, dessen erste Partie begonnen hat, so dass eine Pause nie ein Ergebnis wegwirft. **Fortsetzen** macht weiter.
**Stopp** spielt entweder zuerst die laufenden Partien zu Ende oder hält sofort an (die unterbrochenen Partien werden
nicht gezählt und neu gespielt).

Ein Turnier, das von einer Engine angehalten wurde, sagt in seinem Kopf und auf der Seite Turniere unter *Zu erledigen*,
welche es war: *Unterbrochen: Alpha konnte nicht starten: …* (nach ihrem letzten Versuch) oder *Alpha ist abgestürzt
oder antwortet nicht mehr (Partie 12)* (ohne *Eine abgestürzte Engine neu starten*), mit *Engine öffnen* und
*Protokoll öffnen*.

**Jedes Turnier wird genau dort fortgesetzt, wo es aufgehört hat**, nach einem Stopp, einem geschlossenen Fenster, einem
Absturz oder einem Stromausfall: Keine Partie geht verloren und keine wird doppelt gespielt, und ein halb gespieltes
Partiepaar wird mit derselben Eröffnung und denselben Farben vervollständigt. *Fortsetzen* auf der Seite Turniere setzt
es fort, mit der Statistik der schon gespielten Partien: Ergebnisse, und Tiefe, Zeit pro Zug und NPS der Engines, aus den
Zugkommentaren zurückgelesen (NPS nur, wenn das PGN sie aufzeichnet, *nps* in *PGN und Sonstiges*). Endete das
Programm, während ein Turnier lief (ein Absturz, ein Stromausfall, ein Neustart, oder Beenden, während es lief), setzt
der nächste Start es von selbst fort, und die Warteschlange geht danach weiter: *Beim Start das Turnier fortsetzen, das
lief*, in den **Einstellungen**, standardmäßig an. Ein pausiertes oder angehaltenes Turnier wartet auf dich, und
`--autostart` startet stattdessen die gespeicherte Einrichtung. Ein Teilnehmer, dessen Programm seit seinem Beitritt neu
gebaut wurde oder verschwunden ist, wird im Kopf des fortgesetzten Turniers genannt, mit *Weiter* und *Pause*: Seine
nächsten Partien spielt das Programm, wie es jetzt ist, mit den Optionen, die das Turnier ihm gegeben hat.

### Mehrere Turniere gleichzeitig
<!-- en: several-tournaments-at-once 4914f1d0 -->

Mehrere Turniere können nebeneinander spielen: ein Gauntlet neben einem SPRT, zwei Gauntlets gleichzeitig. *Start* auf
der Seite *Neues Turnier* startet ein weiteres, während andere spielen; *Fortsetzen* setzt eines neben ihnen fort.
Jedes Turnier spielt in einem eigenen Prozess (`rookstudio --run <folder>`, vom Fenster gesteuert), so dass ein
Absturz nur dieses Turnier zu Fall bringt, und das Fenster, seine Engine-Prüfungen, Schnelltests und Testsuiten
warten nicht mehr darauf, dass ein Turnier endet.

Der Arbeitsbereich hat **einen Tab pro geöffnetem Turnier**, über seinem Kopf: ein Punkt, während es spielt (ein
Pausezeichen, während es pausiert), sein Status unter dem Mauszeiger, ein Kreuz, um eines zu schließen, das nicht
spielt. Die Bereiche zeigen den gewählten Tab; die Befehlspalette listet die anderen auf (*… anzeigen*, *Pause*,
*Fortsetzen*). Ein aus der Liste geöffnetes beendetes Turnier übernimmt den Tab eines Turniers, das nur angesehen
wurde. Die Seite Turniere hat eine Karte für jedes spielende Turnier, mit seinem Fortschritt, seinem Führenden, *Öffnen*
und *Pause*, und darunter sein **Gewicht**, die Kerne, die seine Partien von den ihm zustehenden belegen, und worauf
seine nächste Partie wartet; der Balken *Kerne* oben zeigt den Anteil jedes Turniers (und den der Seite Brett) und wie
viele Kerne sich die Turniere teilen. Das *Protokoll* zeigt die Zeilen eines Turniers, des Fensters oder alle (jede
Zeile eines Turniers trägt seinen Namen); die Zeilen eines Turniers werden außerdem in `rookstudio.log` in seinem
Ordner aufbewahrt.

**Die Maschine teilen**: Die spielenden Turniere teilen sich ein Budget an Kernen, eingestellt in den
**Einstellungen** (*Kerne für die Turniere*: standardmäßig die physischen Kerne weniger einen). Jedes bekommt zuerst
so viel wie eine Partie, solange das Budget es zulässt, das mit dem höchsten Gewicht zuerst; der Rest wird nach
Gewicht verteilt (ein Turnier mit Gewicht 2 bekommt doppelt so viele Kerne wie eines mit Gewicht 1), keines über seine
eigene Parallelität hinaus, und was eines nicht nehmen kann, geht an die anderen. Eine Partie zählt so viel wie die
`Threads` der größeren ihrer beiden Engines: Eine denkt, während die andere wartet. Ein pausiertes Turnier behält nur,
was seine laufenden Partien und die zweiten Partien ihrer Paare brauchen. Ein kleinerer Anteil unterbricht nie eine
Partie: Das Turnier spielt ab seiner nächsten Partie weniger Partien gleichzeitig. Ein Turnier allein spielt so viele Partien, wie seine
Parallelität sagt.

**Speicher**: Eine Partie beginnt nur, wenn der `Hash` ihrer beiden Engines und das, was jeder Engine-Prozess sonst
noch belegt (64 MB und 8 MB pro Thread), in das passen, was die spielenden Partien vom Speicherbudget übrig lassen
(*Speicher für die Engines* in den **Einstellungen**, standardmäßig 75 % des eingebauten Speichers), das sich die
Turniere teilen. Die Zusammenfassung der Einrichtung sagt, wie viele Partien das Budget gleichzeitig zulässt, und die
Karte *Zustand* der Seite Turniere, wenn Partien auf Speicher warten.

**GPUs**: Eine Engine, die auf ihrer Karte mit *Nutzt eine GPU* markiert ist (eine Netzwerk-Engine wie Lc0), zählt
gegen die *GPUs* der **Einstellungen**: Es spielen nie mehr solche Engines gleichzeitig, als es GPUs gibt, in einem
Turnier und über Turniere hinweg, während die anderen Partien daneben spielen. Die Zusammenfassung der Einrichtung
sagt, wie viele Partien mit einer GPU-Engine das zulässt, und warnt, wenn keine GPU gezählt ist oder wenn eine Partie
zwischen zwei GPU-Engines mehr GPUs braucht, als die Maschine hat (sie spielt dann allein).

**Weiterlaufen lassen**: Wird das Fenster geschlossen, während Turniere spielen, fragt es, was zu tun ist.
*Weiterlaufen lassen und beenden* (der Standard) lässt jedes in seinem Prozess weiterspielen; das Fenster zeigt sie bei
seinem nächsten Start wieder, so wie sie gerade stehen. *Stoppen und beenden* hält sie an; jedes wird später genau
dort fortgesetzt, wo es aufgehört hat. Der Schnelltest endet in beiden Fällen. Ein Turnier, dessen Prozess beim
nächsten Start nicht mehr da ist (ein Neustart, ein Absturz), wird wie jedes unterbrochene Turnier fortgesetzt. Die
Übertragung des Tabs *Übertragung* läuft im Fenster, für das gezeigte Turnier: Für eine Übertragung, die ohne das
Fenster weiterläuft, starte das Turnier in einem Terminal mit den Übertragungsoptionen (siehe
[Headless-Modus](#headless-modus)).

### Wenn eine Engine nicht starten kann
<!-- en: an-engine-that-cannot-start 26c20dee -->

Eine unbeaufsichtigte Nacht bleibt nicht an einer Engine hängen. Eine Engine, die nicht startet (ein verschobenes oder
neu gebautes Programm, eine fehlende Netzdatei, ein verschwundenes Laufwerk), wird noch dreimal im Abstand von 10 s
versucht; dann werden ihre Partien zurückgestellt, und die anderen Partien gehen weiter. Sie wird alle 5 Minuten erneut
versucht, eine Partie nach der anderen, und ihre Partien kommen zurück, sobald sie startet. Der Kopf des Turniers und
die Seite Turniere (unter *Zu erledigen*) nennen die Engine und den Fehler und bieten an:

- **Erneut versuchen**: sofort statt beim nächsten Versuch (etwa nachdem du das Programm repariert hast).
- **Zurückziehen…** und **Ersetzen…** (Rundenturniere, Gauntlets und Mannschaftskämpfe): der Änderungsbereich des
  Tabs Spielplan, auf diese Engine eingestellt, mit der Auswirkung, die gezeigt wird, bevor sich etwas ändert.
- **Engine-Log**: `logs/start-<engine>.log` im Turnierordner, was beim letzten Versuch an die Engines gesendet wurde
  und was sie geantwortet haben, ihre Fehlerausgabe eingeschlossen (oft der Grund: eine Bibliothek oder eine Netzdatei,
  die nicht gefunden wird).

Das Protokoll und die Desktop-Benachrichtigung sagen, wenn eine Engine zurückgestellt wird. Das Turnier hält nur an,
wenn ohne die zurückgestellten Engines nichts mehr zu spielen ist, nach einem letzten Versuch mit jeder; sein Status
und die Benachrichtigung am Ende sagen das (*Angehalten: ohne …, die nicht startet, ist keine Partie mehr zu spielen*),
und die Warteschlange geht mit dem nächsten Turnier weiter. *Fortsetzen* versucht jede Engine von Neuem. Eine Pause
hält die Versuche an. *Das Turnier anhalten, wenn eine Engine nicht startet*, unter *PGN und Sonstiges* der
Einrichtung, hält es stattdessen sofort an, sobald die Versuche aufgebraucht sind.

### Ein laufendes Turnier ändern
<!-- en: changing-a-running-tournament aebe63e2 -->

Der Bereich **Turnier ändern** des Tabs *Spielplan* arbeitet, ohne etwas anzuhalten: Laufende Partien gehen weiter, und
keine gespielte Partie geht verloren. Jede Änderung zeigt ihre Auswirkung, bevor sie angewendet wird.

- **Engine hinzufügen**: Sie spielt die bereits begonnenen Zyklen nach (*Die bereits begonnenen Zyklen nachholen*)
  oder steigt ab dem nächsten Zyklus ein.
- **Engine zurückziehen**: Ihre verbleibenden Partien fallen weg; ihre bisher gespielten Partien zählen oder nicht.
- **Engine ersetzen**: Ihre Partien zählen nicht mehr und werden von der neuen Engine neu gespielt.
- **Anzahl der Zyklen**: mehr oder weniger.
- **Partien neu spielen**: jede Partie einer Engine, oder die Zeitverluste und Abstürze.
- Rechtsklick auf eine Partie (hier oder im Tab *Partien*): sie neu spielen, sie annullieren (ohne Neuspielen) oder
  ihr Ergebnis setzen.

Eine hinzugefügte oder eingewechselte Engine spielt mit den Einstellungen des Turniers (seiner Bedenkzeit, Hash und
Threads, unter der Engine angezeigt) oder mit *Eigene Einstellungen*: Hash, Threads, Anteil an der Zeit (oder Knoten,
Tiefe), wie die eigenen Einstellungen eines Teilnehmers auf der Seite *Neues Turnier*. Ein Turnier, das vor 0.6.6
erstellt wurde, hat seine Einstellungen nicht gespeichert: Die seiner ersten Engine treten an ihre Stelle, und das
können deren eigene gewesen sein.

In einem Mannschaftskampf tritt eine hinzugefügte Engine der gewählten Mannschaft bei und spielt gegen jede Engine der
anderen Mannschaft; bei Brett gegen Brett lässt sich keine Engine hinzufügen (ihr Brett hätte keinen Gegner): Ersetze
stattdessen eine. Ein Ersatz übernimmt die Mannschaft und das Brett der Engine, die er ersetzt; weitere Zyklen paaren
die Engines, die noch spielen (bei Brett gegen Brett auf den Brettern des ersten Zyklus). Die Partien, die eine
zurückgezogene oder ersetzte Engine gespielt hat, zählen weiter für ihre Mannschaft, es sei denn, sie werden
annulliert.

**Ergebnis setzen** entscheidet eine Partie, die das Programm nicht richtig entscheiden konnte: ein Hänger, den es
nicht erkannt hat, ein gewonnenes Endspiel, in dem die Engines hin und her ziehen, eine Partie, die aus einem falschen
Grund endete. Wähle 1-0, ½-½ oder 0-1 und gib einen Grund an; die Punkte, die sich dadurch verschieben, werden gezeigt,
bevor *Ergebnis setzen* es anwendet. Die Tabelle, die Kreuztabelle, die Wertungen und die Checkliste vor dem Einsenden
zählen das neue Ergebnis (als Abschätzung: Ein auf Remis gesetzter Zeitverlust zählt nicht mehr als Zeitverlust); der
Tab *Partien* markiert es mit einem `*`, dessen Tooltip den Grund angibt und wie die Partie gespielt endete, und
*Zurück zum gespielten Ergebnis* nimmt es zurück. Eine laufende Partie wird auf dieselbe Weise beendet: Rechtsklick auf
ihr Brett im Tab *Live*, *Mit einem Ergebnis beenden* (für die verfolgte Partie auch in der Befehlspalette). Die Engine
am Zug wird gestoppt, fastchess beendet die Partie sofort, und sie zählt mit dem gewählten Ergebnis; die Engine startet
für ihre nächste Partie neu. Das setzt *Eine abgestürzte Engine neu starten* voraus (Einrichtung, *PGN und Sonstiges*,
standardmäßig an).

Annullierte Partien und von Hand gesetzte Ergebnisse bleiben in den PGN-Dateien und im Journal so, wie sie gespielt
wurden: An die PGN-Dateien wird immer nur angehängt. Alles, was die Partien liest, berücksichtigt das: Annullierte Partien werden
weggelassen, und ein von Hand gesetztes Ergebnis steht in jedem Export (*Partie exportieren…*, *Alle exportieren…*,
*Für eine Rangliste exportieren…*, der Export des direkten Vergleichs auf der Seite *Elo-Listen*, die Datei von *Jede
Partie zusätzlich anhängen an*) mit `[Termination "adjudicated by the operator"]` und dem Grund in einem Kommentar vor
dem Ergebnis. **Letzte Änderung rückgängig machen** wird angeboten, bis die nächste Partie beginnt: Unter der Änderung
wurde noch nichts gespielt, sie lässt sich also exakt zurücknehmen (Partien, die schon liefen, als sie vorgenommen
wurde, bleiben erhalten). Ein von Hand gesetztes Ergebnis lässt sich rückgängig machen, solange es die letzte Änderung
ist, ob seither Partien gespielt wurden oder nicht (in einem K.-o.-Turnier, dessen nächste Runden aus den Ergebnissen
folgen, bis die nächste Partie beginnt); der Tab *Partien* bietet *Rückgängig* auch neben der Partie an.

### Warteschlange und Neustart
<!-- en: queue-and-relaunch 06904b6c -->

Die **Warteschlange** auf der Seite Turniere enthält Turniere, die nacheinander gespielt werden: Wenn eines von selbst
endet, startet das nächste (nicht nach einem Stopp oder einem Fehler, und nicht, solange *Alles pausieren* die
Warteschlange anhält). Mit *Starten, wenn Kerne frei sind* startet das nächste neben den spielenden, sobald das Budget
an Kernen größer ist, als sie nehmen können, jedes mit seiner Parallelität. Die Turniere lassen sich umordnen oder
entfernen, und das erste lässt sich sofort starten, wenn nichts läuft. Die Seite Turniere sagt, wann das laufende Turnier
endet (*Endet gegen HH:MM (in …)*) und wann die ganze Warteschlange endet, jedes Turnier der Warteschlange mit seiner
Dauer und, solange die Dauern davor bekannt sind, wann es startet; die Turniere, deren Dauer sich nicht vorhersagen lässt
(ein SPRT ohne Höchstzahl, Knoten oder Tiefe pro Zug), werden gesondert gezählt.

**Neu starten mit…** bei einem beendeten Match (in seinem Kopf oder unter *Zuletzt beendet* auf der Seite Turniere) spielt
es erneut mit einer anderen Engine A, typischerweise der nächsten Version, gegen dieselbe Engine B, mit den übrigen
Einstellungen des Matches (denen der Seite *Neues Turnier* bei einem Match, das eine ältere Version gestartet hat, die
sie nicht aufbewahrte).

**Klonen** (im Kopf eines Turniers, unter *Zuletzt beendet* und in *Alle Turniere* auf der Seite Turniere) öffnet die Seite
*Neues Turnier*, gefüllt mit der Einrichtung dieses Turniers, gleich welches Format: die Teilnehmer mit ihren eigenen
Einstellungen, die Bedenkzeit, die Eröffnungen, die Abschätzung und der Rest, jeder Wert so, wie er gespielt wurde.
Seine Profile sind genannt, und ein seitdem geändertes Profil ist als für dieses Turnier geändert markiert. Ändere, was
anders sein soll (eine andere Bedenkzeit, die nächste Version einer Engine, ein anderes Buch), dann **Start** oder *Zur
Warteschlange hinzufügen*. Ein Turnier, das noch spielt, lässt sich ebenfalls klonen; eines, das eine ältere Version
gestartet hat, die seine Einrichtung nicht aufbewahrte, nicht.

### Die Endspiele mit den Tablebases prüfen
<!-- en: checking-the-endgames-against-the-tables fb5b1e89 -->

Das Menü eines beendeten Turniers, ein Rechtsklick auf seine Kopfzeile oder auf seine Zeile auf der Seite Turniere
(oder **Strg+K**), hat *Endspiele mit den Tablebases prüfen*: die Partien, die sieben Steine oder weniger erreicht
haben, jedes Ergebnis dem gegenübergestellt, was die Tablebases sagen. Die Antwort kommt in einer Karte unter der
Kopfzeile des Turniers, „12 Partien haben sieben Steine oder weniger erreicht: 3 entglittene Gewinne (Alpha: 2, Bravo:
1), 1 gehaltene verlorene Stellung, 2 Remis durch Abschätzung in gewonnener Stellung“, mit einer Zeile für jeden
Befund, wobei *Öffnen* seine Partie im Tab *Partien* bei dem Zug zeigt, um den es geht:

- **ein entglittener Gewinn**: ein Zug einer Engine, nach dem die Tablebases ihr den Gewinn nicht mehr geben (ein Remis
  oder den Gewinn der anderen Seite); **ein entglittenes Remis**: ein Zug einer Engine, nach dem sie der anderen Seite
  den Gewinn geben;
- **eine gehaltene verlorene Stellung**: eine nach den Regeln remis gewordene Partie (dreifache Wiederholung,
  50-Züge-Regel), während die Tablebases an ihrem Ende noch einer Seite den Gewinn geben;
- **ein Remis durch Abschätzung in gewonnener Stellung**, durch die Remisregel des Turniers oder seine Höchstzahl von
  Zügen, und **ein Gewinn durch Abschätzung gegen die Tablebases**, durch seine Aufgaberegel. Die eigene Abschätzung
  der Tablebases stimmt mit ihnen überein; eine durch Zeitüberschreitung, einen illegalen Zug oder einen Absturz
  verlorene Partie wird nur nach ihren Zügen beurteilt.

Ein paar Stellungen pro Partie werden abgefragt: die erste, die die Tablebases beurteilen können, und die letzte, und
wenn ihre Urteile abweichen, eine Bisektion zwischen ihnen bis zu dem Zug, der es geändert hat (etwa zehn Stellungen
für ein langes Endspiel). Zuerst deine Tablebases auf der Festplatte (siehe [Tablebases für die
Endspiele](#tablebases-für-die-endspiele)), dann die Antworten, die Lichess früher gegeben hat, in deinem Profil
gespeichert, dann die 7-Steine-Tablebases von Lichess für den Rest, sobald erlaubt: für diese Prüfung, immer oder nicht
jetzt (die FEN jeder Stellung geht an tablebase.lichess.ovh, ohne Konto), eine Anfrage nach der anderen, jede Antwort
für immer gespeichert. Ohne sie werden die Partien, die die Tablebases deiner Festplatte nicht beurteilen können, als
nicht beurteilt gezählt. Nichts an der Seite des Turniers ändert sich vor dem Klick.

## Elo-Listen
<!-- en: ratings 3e4be48f -->

Die Seite **Elo-Listen** berechnet Maximum-Likelihood-Wertungen (das logistische Modell, Remis als halber Punkt, wie
Ordo es standardmäßig macht) über alle Partien aller Turniere im Profil und der importierten PGN-Dateien. Engines
werden an ihrem Namen erkannt. Die gezählten Partien lassen sich oben auf der Seite eingrenzen, für jeden Tab:

- eine Bedenkzeit, eine Gruppe der Engine-Bibliothek (die Partien zwischen ihren Engines), die letzten 30 oder 365
  Tage;
- *Importierte Partien*: die Partien der unter *PGN-Dateien…* aufgeführten PGN-Dateien (der Download einer Rangliste,
  die Partien eines anderen Programms), aus ihren Kopfzeilen gelesen; ihre Engines werden an den Namen erkannt, die das
  PGN ihnen gibt, und ein Name, der von Groß-/Kleinschreibung und Leerzeichen abgesehen der einer Engine der Bibliothek
  ist („stockfish  17“ für „Stockfish 17“), ist diese Engine: Seine Partien zählen als die der Engine, und ihre
  Referenz-Elo kann sie verankern. Nichts Ungenaueres wird von selbst angewendet: „Stockfish 17 64-bit“ bleibt eine
  eigene Engine, bis du unter *PGN-Spieler…* (neben der Zahl der Engines der Bibliothek, oder unter *PGN-Dateien…*)
  angibst, welche Engine es ist. Dieses Fenster listet die Spieler auf, die dem Namen nach keine Engine der Bibliothek
  sind, mit einer Suche: für jeden *Eine eigene Engine* (der Standard) oder eine Engine der Bibliothek. Ein Name, den
  zwei Engines der Bibliothek von Groß-/Kleinschreibung abgesehen teilen, wird in der Warnfarbe gezeigt, damit du
  wählst; eine wahrscheinliche Engine (ihr Name ist der Anfang des Spielernamens) wird als *Vielleicht …?*
  vorgeschlagen, nie angewendet. Das Fenster öffnet sich von selbst, nachdem eine PGN-Datei hinzugefügt wurde, wenn es
  etwas zu entscheiden gibt. Die Wahl, eine eigene Engine eingeschlossen, wird bei der Engine gespeichert (sie folgt
  einer Umbenennung) und gilt für jede PGN-Datei und für die für die Referenz-Elo importierten Ranglisten; die Partien
  werden damit neu gelesen, wenn das Fenster geschlossen wird. Die Rangliste sagt, wie viele Engines der Bibliothek die
  PGN-Dateien enthalten, ihr Tooltip, welche;
- *Anker*: der Durchschnitt bei 0, oder eine Engine mit einer festen Wertung (standardmäßig die erste Engine mit einer
  Referenz-Elo).

### Rangliste
<!-- en: rating-list c726b55e -->

Die Liste, mit einer Mindestzahl an Partien. Neben Elo, ± und den Partien zeigt jede Engine ihre Punktzahl, ihre Siege
(+), Remis (=) und Niederlagen (−), ihre Remisquote, *Geg.*: die durchschnittliche Wertung der Gegner, auf die sie
traf, Partie für Partie, *LOS*: die Wahrscheinlichkeit, dass sie stärker ist als die nächste Engine der Liste, aus den
beiden Wertungen und der Kovarianz ihrer Fehler (näherungsweise bei mehr als 1000 Engines, wie ihr Tooltip sagt), und
*Abstand*: ihre Elo über der nächsten Engine. Ein Klick auf den Kopf einer Spalte sortiert nach ihr (# behält den Platz
nach Wertung); ein Rechtsklick blendet Spalten ein oder aus (*Abstand* ist anfangs ausgeblendet). Ein Klick auf eine
Engine öffnet ihre Karte.

*Kopieren* als Text oder BBCode, mit *Mit Kopf*, um den unten beschriebenen Kopf über die Liste zu setzen.
*Exportieren…*: CSV mit jeder Spalte; eine HTML-Seite (hell oder dunkel) und Markdown (eine Tabelle, die GitHub und
Foren anzeigen), beide beginnend mit einem Kopf, der sagt, wie die Liste entstand: die gezählten Turniere und
PGN-Dateien, die Partien mit den Siegen von Weiß und Schwarz, den Remis und der Punktzahl von Weiß, der Anker, die
Filter, die Mindestzahl an Partien, das Datum und die Version; und *Partien für Ordo…*: die gezählten Partien als PGN
aus Kopfzeilen, das Ordo und BayesElo lesen (`ordo -p ratings.pgn -o ratings.txt`).

### Engine-Karte
<!-- en: engine-card 80442e81 -->

Die Karte einer Engine: ihre Wertung, ihre Ergebnisse insgesamt und nach Bedenkzeit, ihre Zeitverluste, Abstürze und
illegalen Züge (jede Partie öffnet sich in ihrem Turnier), ihre Turniere (jedes lässt sich öffnen), ihre
durchschnittliche Tiefe, Zeit pro Zug und NPS aus den Zugkommentaren, und ihre Wertung nach jedem Turnier. Die Seite
*Engines* führt mit *Ihre Ergebnisse…* zu ihr.

### Direkter Vergleich
<!-- en: head-to-head 1021ba2f -->

Zwei Engines gegeneinander: die Punktzahl, Elo und LOS, die Pentanomial-Zählungen der Paare, von denen sie beide
Partien gespielt haben, nach Bedenkzeit und nach Turnier, ihre Partien (jede öffnet sich in ihrem Turnier) und
*Partien exportieren…*.

### Fortschritt
<!-- en: progress e2ea28fc -->

Die Wertung der angehakten Engines nach jedem Turnier, das sie gespielt haben, auf einer Zeitachse: die Versionen einer
Engine nebeneinander.

## Übertragung
<!-- en: broadcast c2f489e7 -->

Der Tab *Übertragung* eines laufenden Turniers veröffentlicht es, während es spielt; der Schnappschuss wird zweimal pro
Sekunde aktualisiert. *Gezeigte Partie* wählt die Partie, die an TLCS und die Webseite gesendet wird: automatisch, oder
die im Tab *Live* gewählte.

### Live-Dateien
<!-- en: live-files 09341896 -->

Atomar in einen Ordner deiner Wahl geschrieben, um sie irgendwo zu veröffentlichen (FTP, rsync, eine Website):
`live.pgn` (die laufenden Partien), `standings.html` (Tabelle und Kreuztabelle), `tournament.json` (alles, für
Skripte).

### TLCS-Server
<!-- en: tlcs-server 712f28cb -->

UDP-Port, standardmäßig 16001: das Protokoll von Tom's Live Chess Server, so dass TLCV und andere TLCS-Viewer das
Turnier verfolgen können (Brett, Uhren, PV, Kreuztabelle und die gespielten Partien). Es wird gegen node-tlcv geprüft.
*Ein Port pro Partie* legt jede gleichzeitige Partie auf einen eigenen Port: Partie #1 des Tabs *Live* auf den
UDP-Port, #2 auf den nächsten und so weiter. Jeder Port öffnet sich, wenn seine Partie beginnt, mit eigenen Grenzen für
Clients und Anmeldungen, und erhält die Kreuztabelle nach jeder Partie; *Gezeigte Partie* gilt dann nicht.

### Webseite
<!-- en: web-page bc743ffc -->

TCP-Port: eine Seite, die RookStudio selbst ausliefert, für jeden Browser im Netzwerk, nur lesbar und jede Sekunde
aktualisiert. Die verfolgte Partie (die erste laufende oder die angeklickte) steht in der Mitte: das Brett mit dem
letzten Zug, einem König im Schach, den Koordinaten und den Pfeilen; die Uhr jedes Spielers in großer Schrift; die
Bewertung jeder Engine in großer Schrift, mit der Angabe, wer gerade denkt, ihre Suche als Kacheln (Tiefe/selektive
Tiefe, Knoten, Geschwindigkeit, Zeit, Tablebase-Treffer, Hash) und ihre Hauptvariante; und die Züge, die Buchzüge
kursiv. Geh mit den Pfeilen unter dem Brett oder den Tasten ← → durch die Züge (▶ hinter dem letzten Zug kehrt zur
Partie zurück, wie sie gespielt wird), dreh das Brett mit ⇅ oder F, und kopiere das PGN der Partie oder die FEN der
Stellung.

Neben der Partie zeigen Tabs die Tabelle und die Kreuztabelle, die Diagramme der verfolgten Partie, die Suche jeder
Engine im Schnitt pro Zug mit ihren Auffälligkeiten, die nächsten Partien des Spielplans (ein Schweizer System oder ein
K.-o.-Turnier paart seine späteren Runden im Lauf des Turniers), die Statistik und die Auffälligkeiten der gespielten
Partien, und die Veranstaltung. Jede laufende Partie wird darunter gezeigt, mit einer kleinen Bewertungsleiste neben
jedem Spieler. *Moves* schaltet die Züge zwischen Buchstaben (Nf3) und Figurinen (♘f3) um, und das Design folgt dem
System oder ist auf hell oder dunkel eingestellt; der Browser merkt sich diese Wahl. Die Seite lädt nichts von
anderswo und funktioniert auf Telefonen. *Download the games (PGN)* liefert die PGN-Datei des Turniers, bis zur letzten
aufgezeichneten Partie. `state.json`, das die Seite liest, enthält alles für Skripte. Die Logos der Engines (siehe
[Darstellung](#darstellung)) erscheinen neben ihren Namen, als kleine PNG-Bilder, die das Programm daraus erzeugt und
selbst ausliefert; die Seite erfährt nur die der Engines des Turniers.

### lichess
<!-- en: lichess 7597e86f -->

Eine lichess-Übertragung, mit dem Brett, der Bewertungsleiste und dem Chat von lichess, für alle im Internet. Sie
braucht ein persönliches API-Token mit dem Scope `study:write` (*Token auf lichess erstellen…*), das nur in
`settings.json` im Profil gespeichert wird, sobald du sein Feld verlässt. Ist keine Runde angegeben, wird eine nach
dem Turnier benannte Übertragung angelegt, nicht gelistet, es sei denn, *Unter den lichess-Übertragungen aufgeführt*
ist angehakt; ihr Link wird gezeigt, sobald sie auf Sendung ist. Andernfalls gehen die Partien an die Runde, deren URL
oder ID angegeben ist (eine Übertragung, die per Push gespeist wird, nicht über eine Quell-URL). Die laufenden Partien
werden alle 8 Sekunden mit ihren Uhren gesendet, und jede beendete Partie einmal; eine Runde fasst 64 Partien, danach
wird die nächste Runde derselben Übertragung angelegt. Nur die Partien, die nach dem Einschalten beendet wurden, werden
gesendet; `lichess.json` im Turnierordner merkt sich, wohin sie gingen, so dass ein fortgesetztes Turnier dieselbe
Übertragung fortführt. lichess behält von dem, was es bekommt, nur die Uhren (es verwirft das `[%eval]` und den Text
der Zugkommentare): Seine Bewertungsleiste ist seine eigene, und die Bewertungen der Engines erscheinen dort nicht.

Die Verbindung verwendet Mbed TLS, in das Programm eingebaut, und vertraut nur den Stammzertifikaten, die lichess.org
signieren (Google Trust Services), und denen von Let's Encrypt. Sie geht über den HTTP-Proxy, den `HTTPS_PROXY` (oder
`ALL_PROXY`) nennt, etwa `http://proxy.example:3128` oder `http://user:password@proxy.example:3128`, außer für die
Hosts von `NO_PROXY`; auch unter Windows kommt der Proxy aus diesen Variablen, nicht aus den Systemeinstellungen.
SOCKS-Proxys werden nicht unterstützt.

### Deine eigene Lichess-Übertragung
<!-- en: your-own-lichess-broadcast 114edd02 -->

*Auf Lichess übertragen…* in der Kopfzeile eines Turniers, das auf diesem Computer spielt, öffnet einen Bereich: den
Namen der Übertragung (anfangs den des Turniers), nicht gelistet (für alle, die ihren Link haben), es sei denn,
*Öffentlich: auf Lichess gelistet* ist gewählt, und was an Lichess geht: der Name, Format und Bedenkzeit des Turniers
und das PGN jeder Partie. *Übertragung erstellen* bittet dein verknüpftes Konto beim ersten Mal um eine weitere
Berechtigung, Studien und Übertragungen zu erstellen (dafür öffnet sich die Seite von Lichess), oder verknüpft das Konto
mit ihr; das in der lichess-Karte oben eingefügte Token dient, wenn kein Konto verknüpft ist. Danach gehen die Partien
wie oben beschrieben an die Übertragung, und ein Banner unter der Kopfzeile sagt, wie viele gesendet wurden und wann die
letzte, mit *Auf lichess.org öffnen* und *Stopp*.

### Ein Mannschaftskampf auf Sendung
<!-- en: a-team-match-on-the-air fd32027f -->

Ein Mannschaftskampf zeigt den Stand der Mannschaften überall: im Kopf der Webseite und über ihrer Tabelle (mit der
Punktzahl % von Mannschaft A, Elo ± und LOS), in `standings.html` (die Mannschaften, dann die Engines jeder Mannschaft
gegen die der anderen), in `tournament.json` und `state.json` (`teams`, `team_elo`, `team_elo_error`, `team_los`) und
in der TLCS-Kreuztabelle, als Zeile unter der Tabelle („Lynx 17½ - 18½ Otter · 48.6 % · Elo -9.7 ± 31.2“). Seine
Partien tragen in den PGN-Dateien die Tags `WhiteTeam` und `BlackTeam`, die die Exporte beibehalten.

### Die Webseite ins Internet stellen
<!-- en: putting-the-web-page-on-the-internet ec2bc274 -->

Die Webseite lässt sich von überall verfolgen, ohne einen Port im Router zu öffnen: Hake *Im Internet veröffentlichen*
unter *Webseite* im Tab *Übertragung* an. Während ein Turnier läuft, öffnet RookStudio einen SSH-Tunnel (libssh2,
in das Programm eingebaut) zu einem Tunneldienst, der der Seite eine öffentliche https-Adresse gibt; nichts muss
installiert werden.

- *localhost.run* (der Standard): kostenlos, ohne Konto. Seine Adressen ändern sich ab und zu, und seine
  Geschwindigkeit ist begrenzt; die Seite und ihr Zustand werden komprimiert gesendet, das fällt also kaum ins Gewicht.
  Sein Host-Schlüssel ist RookStudio bekannt: Eine Verbindung zu irgendetwas anderem wird abgelehnt.
- *Ein anderer SSH-Server*: sein Host, Port und Benutzer, und der Port, den er für die Seite öffnet (so wie
  `ssh -R REMOTE_PORT:localhost:PORT user@host` es verlangen würde; 0 lässt ihn wählen). Nur Server, die den Benutzer
  ohne Schlüssel und ohne Passwort hereinlassen, sind verwendbar (Authentifizierung „none“), und sie müssen
  curve25519-sha256, einen RSA-Host-Schlüssel mit 2048 bis 4096 Bit und AES (GCM oder CTR) anbieten. Dem Schlüssel des
  Servers wird beim ersten Mal vertraut, und er wird im Profil gespeichert (`known_hosts.json`); ändert er sich, hält
  der Tunnel an und sagt das, und *Dem neuen Schlüssel vertrauen* ist für den Fall gedacht, dass der Besitzer des
  Servers bestätigt, dass er sich geändert hat.

Der Tab zeigt den Zustand (verbindet, auf Sendung mit der Zahl der Verbindungen, verbindet neu, oder ein Fehler) und,
sobald bekannt, die öffentliche Adresse mit *Kopieren*, *Öffnen* (um die Seite so zu sehen, wie die Zuschauer sie
sehen) und einem QR-Code für ein Telefon (klicke darauf, um ihn zu vergrößern). Der Tunnel verbindet sich nach einem
Fehlschlag von selbst neu, wartet dabei jedes Mal etwas länger und zeigt die neue Adresse, falls sie sich geändert hat.
Er endet mit der Webseite oder mit dem Turnier.

Andere Tunnel funktionieren auch, getrennt gestartet: Mit dem von Cloudflare zum Beispiel schaltest du die Webseite
ein (hier Port 8080) und führst `cloudflared tunnel --url http://localhost:8080` aus; es gibt eine Adresse wie
`https://some-words.trycloudflare.com` aus. ngrok, tailscale funnel oder ein Reverse-Proxy wie nginx, auch unter einem
eigenen Pfad (`location /live/ { proxy_pass http://localhost:8080/; }`), funktionieren genauso.

Die Seite ist nur lesbar: Sie beantwortet nur GET-Anfragen nach der Seite, ihrem Zustand und der PGN-Datei, nie nach
einer Datei, die eine Anfrage nennt, und nichts darin ändert das Turnier. Jede Anfrage wird für sich beantwortet,
höchstens 32 gleichzeitig, und muss innerhalb einer Sekunde ankommen und innerhalb von zehn beantwortet sein, so dass
ein langsamer oder stummer Besucher niemanden sonst aufhält; hinter dem Tunnel zählt die Verbindung jedes Zuschauers
als eine davon: 40 werden gleichzeitig weitergeleitet, ebenso viele weitere warten, bis sie dran sind, und die
nächsten werden abgelehnt; ein Zuschauer, der mehr sendet, als die Seite annimmt, wird als Einziger getrennt. Zwei Downloads
der PGN-Datei laufen gleichzeitig, und der nächste wird gebeten, einen Moment später wiederzukommen. Der Zustand wird
nie zwischengespeichert, auch nicht vom Tunnel, so dass die Zuschauer die Partien sehen, während sie laufen; die Seite
wird vom Browser aufbewahrt und bei jedem Laden erneut geprüft. Die im Tab *Übertragung* gezeigten Zuschauer werden
aus den Anfragen gezählt, die die Seiten stellen, so dass die Zahl auch hinter einem Tunnel stimmt.

Die Zuschauer sehen den Namen des Turniers, die Namen der Engines, ihre Wertungen, Suchen und Auffälligkeiten, die
Eröffnungen und die PGN-Datei der gespielten Partien. Die Ordner und die Dateien der Engines bleiben privat. Headless:

```
rookstudio --run FOLDER --tunnel                      # localhost.run, the address printed on the console
rookstudio --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Wo die Zuschauer sich unterhalten
<!-- en: where-the-viewers-talk e66bf5c5 -->

Die Seite hat keinen eigenen Chat; sie verweist dorthin, wo die Unterhaltung stattfindet. Ein *Chat-Link* unter
*Webseite* (eine Discord-Einladung, ein Twitch-Kanal, ein Forumsthema; nur https, alles andere wird mit einer Meldung
unter dem Feld abgelehnt) setzt einen Knopf *Chat* in den Kopf der Seite, der ihn in einem neuen Tab öffnet. Während
die Partien an lichess gesendet werden, verlinkt die Seite auch die lichess-Runde, die ihren eigenen Chat hat.

Eine *Discord-Server-ID* fügt neben *Event* einen Tab *Discord* hinzu, mit dem Server-Widget von Discord im hellen oder
dunklen Design der Seite: der Name des Servers, die Mitglieder online, seine Sprachkanäle und ein Knopf *Join*, nicht
die Nachrichten. Der Besitzer des Servers schaltet es unter Servereinstellungen, Widget („Server-Widget aktivieren“)
ein, wo auch die zu kopierende Server-ID steht (17 bis 20 Ziffern); mit eingeschaltetem Entwicklermodus liefert ein
Rechtsklick auf das Symbol des Servers ebenfalls *Server-ID kopieren*. Das Widget wird erst von discord.com geladen,
wenn ein Zuschauer den Tab öffnet, und Discord sieht dann den Besuch dieses Zuschauers (seine Adresse, seinen Browser,
seine Discord-Anmeldung, falls er eine hat); ohne ID kontaktiert die Seite niemanden außer RookStudio. Die Chat- und
lichess-Links senden nichts, bis ein Zuschauer sie anklickt, und dann keinen Referrer. Eine headless ausgeführte
Einrichtungsdatei (`--run SETUP.json`) übernimmt beide aus `web_chat_link` und `web_discord_id`.

## Headless-Modus
<!-- en: headless-mode a3cec26b -->

```
rookstudio --run TOURNAMENT_FOLDER     # resume a tournament
rookstudio --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Es wird kein Fenster geöffnet; auf der Konsole erscheinen die Version, dann der Fortschritt, mit den Versuchen einer
Engine, die nicht startet, und der zurückgestellten (mit ihrem Engine-Log) oder zurückgekehrten Engine, wie im Fenster,
dazu die Tabelle alle 20 Partien und am Ende (bei einem Mannschaftskampf: die Stände der Mannschaften, dann die Engines
jeder Mannschaft gegen die der anderen) und das Ende eines Turniers mit der Schlusszeile der Tabellenkopien (Partien,
Remis, Bedenkzeit, Buch und Veranstaltung). **Strg+C** hält nach den laufenden Partien an, und das Turnier
lässt sich später fortsetzen, von der Befehlszeile oder der GUI aus. Exit-Codes: 0 beendet, 1 Fehler beim Spielen, 2
nichts zu spielen, 3 vor dem Ende angehalten (durch Strg+C, oder weil ohne eine Engine, die nicht startet, nichts mehr
zu spielen ist). Das Speicherbudget und die GPUs der **Einstellungen** des Profils gelten auch hier.

`--detach` spielt es stattdessen im Hintergrund: Der Befehl kehrt sofort zurück, und das Turnier läuft ohne Terminal
weiter; das Fenster zeigt es (es verbindet sich damit), wenn es mit demselben Profil geöffnet wird, und `--attach`
spricht mit ihm:

```
rookstudio --run FOLDER --detach         # in the background, on a local socket
rookstudio --attach FOLDER               # its control channel on the standard input and output
```

Das Fenster steuert jedes Turnier auf dieselbe Weise: `--run FOLDER --control socket` (oder `--control stdio`) spielt
es ohne Ausgabe und tauscht JSON-Zeilen mit dem Fenster aus: die Ereignisse des Turniers (sein Zustand, jede
Journalzeile, die laufenden Partien Zug für Zug, jede aufgezeichnete Partie, sein Log, sein Ende) und die Befehle des
Fensters (Pause, Fortsetzen, Stopp, Änderungen, das Ende einer Partie, sein Anteil an der Maschine mit
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). Die erste Zeile in jeder Richtung ist ein Hallo,
`{"hello": {"protocol": 1, ...}}`; ein Skript über `--attach` erhält das Hallo und die Ereignisse des Turniers und
darf Befehle wie `{"c": "pause"}` senden. Nur ein Fenster (oder Skript) zur Zeit spricht mit einem Turnier;
`--attach` lässt es los, wenn seine Standardeingabe endet oder seine Ausgabe nicht mehr gelesen wird, ein Skript muss
also seine Eingabe offen halten, während es die Antworten liest.

Das Turnier lässt sich gleichzeitig übertragen, wie aus dem Tab *Übertragung*:

```
rookstudio --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
rookstudio --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
rookstudio --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
rookstudio --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
rookstudio --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
rookstudio --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` nimmt das Token aus der Umgebungsvariable `FCG_LICHESS_TOKEN` oder das des Profils.

Die Optionen lassen sich kombinieren. Eine mit eingeschalteter Übertragung gespeicherte Einrichtungsdatei überträgt
auch ohne diese Optionen auf dieselbe Weise. Für Zuschauer außerhalb des lokalen Netzwerks öffne die Ports im Router: TLCV und node-tlcv
verbinden sich mit dem TLCS-Port. Für die Webseite erspart ein Tunnel das Öffnen eines Ports (siehe
[Die Webseite ins Internet stellen](#die-webseite-ins-internet-stellen)).

## Maschinen
<!-- en: machines 4d8fbfef -->

Andere Computer, auf denen Turniere laufen: ein Testrechner, ein Server, ein zweiter PC. **Einstellungen** >
*Maschinen* listet sie auf. Eine Maschine wird über SSH erreicht: Auf ihr muss nichts geöffnet werden außer ihrem
SSH-Server, und das Programm speichert nichts Geheimes.

### Eine Maschine hinzufügen
<!-- en: adding-a-machine 6415de10 -->

*Maschine hinzufügen*, dann auf ihrer Karte:

- *Name*, *Host* (ihr Name im Netzwerk oder ihre Adresse), *Port* (22) und *Benutzer*, das Konto, unter dem ihre
  Turniere laufen;
- *Anmelden mit*: dem *SSH-Agent* (ssh-agent unter Linux und macOS, Pageant oder der Dienst *OpenSSH Authentication
  Agent* unter Windows; füge deinen Schlüssel mit `ssh-add` hinzu) oder einer *Schlüsseldatei*, RSA oder Ed25519
  (`~/.ssh/id_ed25519`). Die Passphrase einer Schlüsseldatei wird einmal pro Sitzung abgefragt: Sie bleibt im Speicher
  und wird nirgends geschrieben. Die Maschine muss den Schlüssel kennen: Seine öffentliche Hälfte (`id_ed25519.pub`)
  kommt in `~/.ssh/authorized_keys` auf der Maschine (`ssh-copy-id user@host` legt sie dort ab), oder, für einen
  Administrator einer Windows-Maschine, in `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Ordner dort*: der Ordner von RookStudio auf der Maschine, für das Programm, seine Turniere, Bücher und
  Tablebases, relativ zum Home-Ordner des Benutzers (standardmäßig `rookstudio`) oder absolut (`/srv/fcg`,
  `D:\fcg`);
- *Budget*: die Kerne und der Speicher, die die Turniere auf der Maschine gleichzeitig nutzen dürfen; 0 nimmt, was der
  Test ermittelt hat, alle Threads bis auf einen und 80 % des Speichers.

*Verbindung testen* meldet sich an und nennt dann das System der Maschine, ihre Threads, ihren Speicher, den freien
Platz dort, wo ihr Ordner liegt, und ob RookStudio dort installiert ist, in welcher Version. Die erste Verbindung
zeigt den Host-Schlüssel der Maschine (seinen SHA256-Fingerabdruck) und vertraut ihm von da an (`known_hosts.json` im
Profil, auch die des Übertragungstunnels). Ändert sich der Schlüssel später, wird nichts gesendet, und die Karte sagt
das, mit *Dem neuen Schlüssel vertrauen* für eine Maschine, die neu installiert wurde. Jeder Fehlschlag nennt die
Maschine und was zu tun ist: der Host, der nicht erreichbar ist, ein Schlüssel, den die Maschine ablehnt (und wohin
seine öffentliche Hälfte gehört), eine Passphrase, die nicht stimmt, kein laufender Agent.

*RookStudio installieren* (*RookStudio aktualisieren*, sobald es dort ist) kopiert das Programm über SFTP in den
Ordner der Maschine und prüft dann, ob es dort startet (`rookstudio --version`). Es kopiert dieses Programm, wenn
die Maschine dasselbe System hat (Linux x86-64 zu Linux x86-64); für ein anderes, etwa eine Windows-Maschine von Linux
aus, lade das Release für dieses System herunter und wähle sein Programm auf der Karte (*RookStudio für Windows
x86_64*). Eine vom Netzwerk unterbrochene Kopie wird dort fortgesetzt, wo sie aufgehört hat.

### Engines auf einer Maschine
<!-- en: engines-on-a-machine 4b00e462 -->

*Engines auf dieser Maschine*: die Engines der Bibliothek, die die Maschine hat, jede mit ihrem Pfad dort
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Engines prüfen* prüft jeden Pfad: Das Programm ist da, lässt sich
ausführen und antwortet innerhalb von 10 Sekunden auf `uci` (`xboard` bei einer WinBoard-Engine). Die Markierung
daneben und ihr Tooltip sagen, was gefunden wurde: den Namen der Engine, oder warum sie nicht läuft (fehlt, nicht
ausführbar, für ein anderes System gebaut, stumm).

Eine Engine, die auf der Maschine fehlt, wird mit *Kopieren…* in ihrer Zeile dorthin kopiert (oder mit *\<engine\> nach
\<machine\> kopieren* in der Befehlspalette, oder mit dem Knopf eines Starts, der abgelehnt wurde, weil eine Engine dort
keinen Pfad hat). Der Bereich, der sich öffnet, sagt zuerst, was übertragen wird: das Programm, seine Größe und das
System, für das es gebaut ist, aus seinen ersten Bytes gelesen (ein Windows-Programm, ein Linux- oder macOS-Programm,
universell oder nicht, oder ein Skript). Ein Programm, das die Maschine nicht ausführen kann, wird abgelehnt, bevor
etwas gesendet wird, mit dem Hinweis, welcher Build zu wählen ist: „Alpha ist ein Windows-Programm; tc-box läuft unter
Linux: wähle den Linux-Build von Alpha“. Die Architektur zählt ebenfalls (x86-64 oder arm64); ein x86-64-Build auf
einem arm64-Mac läuft nur unter Rosetta, und ein Format, das das Programm nicht kennt, wird trotzdem kopiert: Beides
wird gesagt, nicht abgelehnt. Ein Skript geht nur auf eine Linux- oder macOS-Maschine, und nur, wenn der Interpreter
seiner ersten Zeile (`python3`, `/bin/sh`) dort vorhanden ist. *Den \<system\>-Build von \<engine\> wählen…* nimmt eine
andere Datei dieses Computers, etwa den Windows-Build der Engine, prüft sie auf dieselbe Weise und behält sie für diese
Maschine: Spätere Vergleiche und Ersetzungen verwenden sie, nicht das Programm der Bibliothek.

*Mit seinem Ordner* kopiert den ganzen Ordner der Engine neben das Programm (ihr Arbeitsverzeichnis oder den Ordner des
Programms), für eine Engine, die Dateien neben sich braucht: ihre Netze, ein Buch. Der Bereich zählt zuerst seine
Dateien und ihre Größe und nennt die Optionen der Engine, die in ihn zeigen (`EvalFile` eines Netzes): In einem Turnier
auf der Maschine zeigen sie auf die Kopie dort, und der Ordner ist das Arbeitsverzeichnis der Engine. Ein Ordner mit
mehr als 2000 Dateien wird nicht angeboten (ein Programm in `/usr/bin` soll nicht mit seinem Ordner kopiert werden),
ein Ordner `.git` wird weggelassen, und bei einer Windows-Maschine wird ein Dateiname, den Windows nicht aufnehmen kann
(`:`, `*`, `?`, ein Punkt am Ende, `CON`, `NUL`...), genannt und hält die Kopie an, bevor sie beginnt. Dateien werden
so kopiert, wie sie sind, Zeilenenden eingeschlossen.

*Kopieren* sendet die Dateien über SFTP nach `engines/<engine>/` im Ordner der Maschine, im Hintergrund mit der
verbleibenden Zeit und *Abbrechen*, nach einer Unterbrechung dort fortgesetzt, wo es aufgehört hat; der SHA-256 jeder
Datei wird nach der Kopie mit dem dieses Computers verglichen, und Dateien, die schon da und gleich sind, werden nicht
erneut gesendet. Unter Windows behält oder bekommt das Programm sein `.exe`; anderswo wird es ausführbar gemacht
(`chmod 755`). Dann wird der Pfad der Engine auf der Maschine auf die Kopie gesetzt und so geprüft, wie *Engines
prüfen* es tut. Auf einem Mac braucht ein von einem anderen Computer kopiertes Programm, das nicht startet, unter
Umständen eine Signatur oder muss die Quarantäne verlassen: Die Meldung gibt die beiden Befehle an, die dort
auszuführen sind (`codesign -s -` und `xattr -d com.apple.quarantine`); das Programm führt sie nicht selbst aus.

Die Spalte *Build* vergleicht die Engine dort mit der dieses Computers, über ihren SHA-256 (*Engines prüfen* und
*Vergleichen* tun das, wobei der Hash des Programms dieses Computers einmal berechnet wird, bis es sich ändert):
*derselbe Build wie dieser Computer* oder *ein anderer Build dort*, mit *Durch den dieses Computers ersetzen*. Das
Ersetzen kopiert das Programm dieses Computers in den Ordner der Engine im Ordner der Maschine und lässt die Engine
dorthin zeigen: Ein Programm anderswo auf der Maschine (`/opt/engines/stockfish`) wird nie überschrieben, es bleibt,
wie es war.

### Bücher und Tablebases auf einer Maschine
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Prüfen, was die Einstellung verwendet* vergleicht das Eröffnungsbuch und die eigenen Bücher der Teilnehmer in der
Einrichtung mit dem Bücherordner der Maschine, nach Größe und SHA-256 (ein Buch gleichen Namens, aber mit anderem
Inhalt ist nicht dasselbe Buch), und die Syzygy-Dateien der Einrichtung mit höchstens 6 Steinen mit dem
Tablebase-Ordner der Maschine, nach Name und Größe. *Fehlende Dateien übertragen* kopiert, was fehlt oder abweicht, im
Hintergrund, mit der verbleibenden Zeit; jedes Buch wird nach dem Kopieren über seinen Hash geprüft, jede Tabelle über
ihre Größe, und eine abgebrochene Kopie wird fortgesetzt. Tabellen mit 7 Steinen werden nicht angeboten: Sie belegen
Terabytes. Ein Turnier startet auf einer Maschine nicht ohne sein Buch; fehlende Tablebases sind eine Warnung (die
Abschätzung und die Engines kommen ohne sie aus).

### Ein Turnier auf einer Maschine
<!-- en: a-tournament-on-a-machine f5d4c423 -->

In der Einrichtung wählt *Spielen auf* (unter *Parallelität*), wo das Turnier spielt: *Dieser Computer* oder eine
Maschine. *Start* bereitet es dann dort vor: RookStudio muss dort in dieser Version installiert sein (sonst
*RookStudio aktualisieren*), jede Engine muss ihren Pfad auf der Maschine haben (sonst bietet der Start *\<engine\>
nach \<machine\> kopieren…* an), und die Bücher der Einrichtung müssen in ihrem Bücherordner liegen (sonst sagt der
Start, welches fehlt, und *Fehlende Dateien übertragen* legt es dort ab); fehlende Tablebases sind nur eine Warnung.
Das Turnier bekommt seinen Ordner in `tournaments/` im Ordner der Maschine, mit den Engines, Büchern und Tablebases an
ihren Pfaden dort, und sein Prozess startet dort außerhalb der SSH-Sitzung (`rookstudio --run <folder> --detach`):
Es spielt weiter, was auch immer mit dem Fenster oder dem Netzwerk geschieht. Das Fenster verfolgt es wie ein Turnier
dieses Computers: Live, Partien, die Tabelle, Pause, Stopp und die Änderungen, über den Steuerkanal seines Prozesses
(`rookstudio --attach <folder>` über SSH).

Seine Karte auf der Seite Turniere sagt *Auf \<machine\>* und *Verbinde mit \<machine\>…*, solange die Verbindung unterbrochen
ist: Das Fenster verbindet sich von selbst neu, nach jedem Fehlschlag etwas später, und nimmt die Ereignisse dort auf,
wo seine Kopie aufgehört hat. Das Journal und die PGN-Dateien werden, während sie wachsen, alle paar Sekunden in den
Ordner des Turniers hier zurückkopiert, damit Elo-Listen, Exporte und die Checkliste vor dem Einsenden sie lesen; der
Ordner der Maschine bleibt die Referenz (`machine.json` hier sagt, welche Maschine und welcher Ordner). Das Schließen
des Fensters lässt es dort weiterspielen; der nächste Start zeigt es wieder. *Fortsetzen* eines unterbrochenen
Turniers einer Maschine setzt es dort fort. Das Budget der Maschine (ihre Kerne und ihr Speicher) teilen sich ihre
eigenen Turniere, getrennt von denen dieses Computers. Ein Turnier spielt auf genau einer Maschine: Seine Partien werden
nicht auf mehrere verteilt.

Eine Maschine braucht einen OpenSSH-Server (Linux, macOS, BSD; Windows 10 1809 und neuer), mit einem RSA-Host-Schlüssel
und SFTP, beides standardmäßig vorhanden. Befehle laufen über `sh`, welche Shell der Benutzer auch hat (fish oder csh
sind kein Problem), und unter Windows über cmd, die Standard-Shell seines OpenSSH-Servers.

### Von diesem Computer aus gespielte Engines
<!-- en: engines-played-from-this-computer 785e6bae -->

Eine Engine der Bibliothek kann auf einer Maschine laufen, während das Turnier hier spielt: eine GPU-Engine auf dem
GPU-Rechner, eine für ein anderes System gebaute Engine, eine Maschine, die einer einzigen Engine vorbehalten ist. Auf
ihrer Karte wählt *Läuft auf* zwischen *Dieser Computer* und einer Maschine; *Programm dort* und *Arbeitsverzeichnis
dort* sind dann Pfade auf der Maschine (leer: der Ordner des Programms dort), und die Knöpfe *…*, auch die der Optionen,
die eine Datei angeben, durchsuchen die Dateien der Maschine. Sobald es Maschinen gibt, bietet *Hinzufügen…* auch *Ein
Programm auf \<machine\>…* an, *Importieren…* auch *Alle Engines eines Ordners auf \<machine\>…*, und *Engine
hinzufügen…* der Maschine bietet *Ein Programm dort…* an. Die Engine wird wie eine Engine dieses Computers gelesen,
geprüft und in der Konsole, im Schnelltest und in Turnieren gespielt. Ihr Pfad ist zugleich der in der Engine-Liste der
Maschine, und der SHA-256 ihres Programms dort zeigt, wann sie neu gebaut wurde (Δ, neu gelesen).

Die Maschine muss einmal getestet worden sein (*Verbindung testen*), damit ihr Host-Schlüssel bekannt ist: Eine Engine
verbindet sich nie mit einer Maschine, deren Schlüssel nicht gezeigt und für vertrauenswürdig erklärt wurde. Die
Passphrase des Schlüssels, falls er eine hat, wird einmal pro Sitzung abgefragt, vor dem ersten Lesen oder dem Start,
und im Speicher behalten: Der Prozess des Turniers bekommt sie vom Fenster und behält sie, damit das Turnier
weiterspielt, nachdem das Fenster geschlossen wurde. `--run` in einem Terminal fragt nicht danach: Verwende dort den
SSH-Agent.

Jede Engine wird über ein Relais gestartet, `rookstudio --remote-engine <key>`, das der Prozess bedient, der sie
spielt: eine SSH-Verbindung zur Maschine für höchstens zehn Engines (die `MaxSessions` eines OpenSSH-Servers), jede
Engine aus ihrem Ordner dort gestartet. Eine Engine, deren Relais wegfällt, wird auch dort beendet, über ihre
Prozess-ID, wenn sie nicht von selbst endet (auf einer Linux- oder macOS-Maschine; auf einer Windows-Maschine wird ihr
Kanal geschlossen).

*Prüfen* misst die Umlaufzeit zur Maschine, den Median einiger `isready`. Sie wird auf die Uhr angerechnet: Die
Zusammenfassung der Einrichtung warnt, wenn sie mehr als 5 % des Inkrements beträgt, oder ohne Inkrement der Zeit für
einen Zug, mit *Toleranz auf … ms setzen* (das Doppelte der Umlaufzeit). Eine nie getestete Maschine verhindert den
Start, ebenso ein Turnier, das auf einer anderen Maschine läuft als der der Engine (eines, das auf der Maschine der
Engine läuft, spielt sie dort als seine eigene Engine).

Während das Turnier spielt, hat die Seite Turniere unter den Kernen eine Zeile *Maschinen*: jede Maschine, verbunden und was
dort spielt, oder seit wann sie nicht antwortet. Eine Maschine, die nicht antwortet, ist ein Eintrag unter *Zu
erledigen*: Die Partien ihrer Engines werden zurückgestellt und erneut versucht, die anderen gehen weiter, mit *Erneut
versuchen* und *\<machine\> testen*. Eine Partie, die eine Engine verloren hat, weil ihre Verbindung abbrach (ihr Ende
als Verbindungsverlust), wird annulliert und neu gespielt und mit ihrem Grund ins Journal geschrieben
(`connection to its machine lost: <machine>`); *Zustand* zählt sie gesondert, nicht als Absturz der Engine. Bei einer
Maschine, deren Verbindung innerhalb einer Stunde mehr als dreimal abbricht, werden ihre Engines zurückgestellt. Die
Kerne, der Speicher und die GPUs dieses Computers zählen die Engines auf Maschinen nicht mit, nur ihre Relais; das
Budget der Maschine, das sich ihre eigenen Turniere teilen, zählt sie noch nicht.

### Eine Windows-Maschine
<!-- en: a-windows-machine cd5493d1 -->

Windows 10 (1809 und neuer) und Windows 11 enthalten einen OpenSSH-Server, der standardmäßig nicht installiert ist.
In PowerShell, als Administrator:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

Die Installation öffnet Port 22 in der Firewall. Lege dann deinen öffentlichen Schlüssel in
`C:\Users\<user>\.ssh\authorized_keys` ab, oder, für ein Konto der Gruppe Administratoren, in
`C:\ProgramData\ssh\administrators_authorized_keys`, die nur Administratoren und SYSTEM lesen dürfen (die Gruppen
werden über ihre SIDs angegeben, die in jeder Sprache von Windows gleich sind):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Behalte cmd als Shell des Servers: RookStudio spricht cmd, nicht PowerShell (wurde ein Wert `DefaultShell` unter
`HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH` gesetzt, entferne ihn). Wähle auf der Karte der Maschine die
`rookstudio.exe` des Windows-Release, dann *RookStudio installieren*.

## Dateien
<!-- en: files d79e45d8 -->

Alles liegt im Profilordner: `~/.local/share/rookstudio/` unter Linux,
`%APPDATA%\rookstudio\` unter Windows, `~/Library/Application Support/rookstudio/` unter
macOS. Dateien, die das Programm anlegt, sind für andere Benutzer nicht beschreibbar, und `settings.json` ist nur für
seinen Besitzer lesbar, sobald es ein lichess-Token enthält. Die **Einstellungen** zeigen den Profilordner, mit
*Ordner öffnen*.

### Portabler Modus
<!-- en: portable-mode 228dba26 -->

Liegt eine Datei namens `portable` (leer, ohne Endung) neben dem Programm (neben `rookstudio.exe` oder
`rookstudio`, unter macOS neben `RookStudio.app`), ist das Profil stattdessen der Ordner `data` neben dem
Programm, und der Standard-Turnierordner ist `data/runs`: Nichts wird anderswo geschrieben, außer in den Ordnern, die
für Turniere, Exporte und Ähnliches gewählt werden. Die Pfade dessen, was im Ordner des Programms liegt (Engines,
Bücher, Tablebases, Testsuiten, die PGN-Dateien der Elo-Listen und der Einrichtung, die gewählten Ordner), werden in
den Dateien des Profils relativ zu ihm gespeichert, so dass sich der ganze Ordner verschieben, auf eine andere
Maschine kopieren oder von einem USB-Stick starten lässt, egal mit welchem Laufwerksbuchstaben, und seine Engines
wiedergefunden werden. Die **Einstellungen** zeigen *Portabler Modus* neben dem Profilordner. Um ein bestehendes Profil
mitzunehmen, kopiere seine Dateien nach `data`. `--profile` wählt weiterhin einen anderen Ordner, außerhalb des
portablen Modus. Ein Turnier behält die Pfade der Engines aus der Zeit, als es angelegt wurde: Setze ein
unvollendetes fort, bevor du den Ordner verschiebst. Entferne unter macOS zuerst die Quarantäne (siehe das README):
Sonst führt macOS die Anwendung von einem temporären Ort aus, an dem die Datei `portable` nicht liegt.

### Der Profilordner
<!-- en: the-profile-folder e39d5884 -->

- `engines.json`: die Engine-Bibliothek, und `engines.json.bak` ihre vorige Version, gelesen, wenn die erste verloren
  ist;
- `setup.json`: die letzte Einrichtung; `profiles.json`: deine Profile der Abschnitte der Einrichtung;
  `templates/*.json`: die gespeicherten Vorlagen; `queue.json`: die Warteschlange;
- `book-cache/`: die in Polyglot-Bücher umgewandelten PGN-Bücher für die eigenen Bücher der Teilnehmer (nach 30 Tagen
  ohne Verwendung entfernt);
- `layout.ini`: das Fensterlayout und die Bereiche des Arbeitsbereichs; `layouts.json`: die nach Namen gespeicherten
  Layouts; `rookstudio.log`: das Protokoll der letzten Sitzung;
- `suite.json`: die letzte Einrichtung der Seite Suiten; `suites/<suite>-<date>.json`: die beendeten Läufe der
  Testsuiten;
- `board.json`: die Einstellungen der Analyse und der Partien der Seite Brett; `analyses/*.json`: die Zug für Zug
  analysierten Partien, je eine Datei; `games/*.pgn`: die auf der Seite Brett gespielten Partien;
- `settings.json`: der Turnierordner, wenn in den **Einstellungen** ein anderer gewählt wurde, *Software-Rendering*,
  der Ordner von *Alle Engines eines Ordners…*, der Tester und der Ort von *Für eine Rangliste exportieren…*, das
  lichess-Token, Größe und Lage des Fensters, die Breite des Anleitungsbereichs, ob die Karte Erste Schritte
  ausgeblendet ist, die Suche nach einer neuen Version (gewünscht oder nicht, wann sie zuletzt lief, das gefundene Release, der
  ausgeblendete Hinweis), die Darstellung (Brettfarben, Figuren, Zugtöne) und das Budget der Maschine (Kerne, Speicher
  und GPUs für die Turniere, *Starten, wenn Kerne frei sind*);
- `logos/`: die Logos der Engines, Kopien der auf ihren Karten gewählten Bilder;
- `machines.json`: die entfernten Maschinen der **Einstellungen** (ihre Adresse, ihr Ordner, ihre Budgets, die Pfade
  der Engines und was der letzte Test ergeben hat; keine Passphrase), und `known_hosts.json` die vertrauten
  Host-Schlüssel, der Maschinen und des Tunnels;
- `running.json`: die gerade spielenden Turniere, mit ihren Gewichten, jedes entfernt, wenn es anhält; beim Start
  gefunden, sagt es, was wieder anzuzeigen (ein Turnier, das ohne das Fenster weitergespielt hat) oder fortzusetzen
  ist;
- `runs/`: die Turniere, sofern kein anderer Ordner gewählt wurde (siehe
  [Der Ordner eines Turniers](#der-ordner-eines-turniers));
- `databases.json`: deine Datenbanken, und `databases/` die Ordner der hier erstellten (`Name.fcgdb`);
  `searches.json`: die gespeicherten Suchen; `repertoires/`: deine Repertoires, je eine PGN-Datei;
- `recent.json`: die Partie der Seite Brett und die letzten Partien, mit ihren nicht gespeicherten Änderungen;
- `engines/`: die heruntergeladenen beliebten Engines, jede Version in einem eigenen Ordner;
- `assistant.json`: die Einstellungen des Assistenten, und `models/` sein Modell;
- `lichess-account.json`: das verknüpfte Lichess-Konto, seine Berechtigungen und sein Token (eine Datei, die nur du
  lesen kannst); `lichess-cache/`: die Antworten der Tablebases und der Cloud von Lichess.

### Der Ordner eines Turniers
<!-- en: a-tournaments-folder 2189632d -->

`runs/<date>_<A>_vs_<B>/` für ein Match, `runs/<date>_<name>/` für ein Turnier (in `runs/` oder im gewählten Ordner),
je ein Ordner, der sich verschieben, kopieren, zippen und verschicken lässt:

- `tournament.json`: das Format, die Teilnehmer (Engines mit ihren Optionen kopiert) und die Bedingungen;
- `setup.json`: die Einrichtung der Seite *Neues Turnier*, mit der es gestartet wurde, die *Klonen* und *Neu starten
  mit…* lesen (siehe [Warteschlange und Neustart](#warteschlange-und-neustart)); ein Turnier, das eine ältere Version
  gestartet hat, hat keine;
- `journal.jsonl`: alles, was seitdem geschehen ist, eine Zeile pro Ereignis (begonnene Partiepaare, beendete
  Partien, Änderungen, Paarungen des Schweizer Systems und des K.-o.-Systems); zusammen mit den PGN-Dateien ist es das,
  was ein Fortsetzen liest. Eine Partie steht auf der Festplatte, bevor ihre Zeile geschrieben wird, so dass ein
  Stromausfall höchstens die laufenden Partien kostet. Partietext, auf den keine Zeile verweist (eine Partie, die eine
  Änderung während des Speicherns verworfen hat, oder die ein Absturz hinterlassen hat), wird durch Leerzeilen ersetzt,
  so dass andere PGN-Viewer nur die Partien des Turniers zeigen;
- `tournament.lock`: gehalten vom Programm, das das Turnier spielt; ein anderes Programm, das den Ordner öffnet, zeigt
  es nur an;
- `<name>.pgn`: Zugkommentare im Stil von cutechess / fastchess, `{+0.35/18 1.234s}`, mit der Bewertung aus der Sicht
  der Seite, die gezogen hat; die Partien in der Reihenfolge, in der sie endeten (das Journal findet jede an ihrer
  Stelle in der Datei; *Alle exportieren…* im Tab *Partien* und die Datei von *Jede Partie zusätzlich anhängen an*
  folgen dem Spielplan);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, mit der Bewertung aus der Sicht
  von Weiß, wie ChessBase / Fritz und Lichess sie lesen;
- `<name>` ist der Name des Turniers bei seiner Anlage (eine spätere Umbenennung behält die Dateien), wobei die
  Zeichen, die Windows nicht erlaubt, durch `_` ersetzt und auf 64 Bytes gekürzt werden; `tournament.json` hält beide
  Namen fest. Ein Turnier ohne Namen, ein älteres oder ein aus 0.2.x importiertes Match hat `games.pgn` und
  `games-chessbase.pgn`;
- `lichess.json`: die lichess-Übertragung und die Runde, an die seine Partien gingen (kein Token);
- `processor.tsv`: die Prozessorzeit, die die Engines jeder Partie verbraucht haben, für die *Prozessornutzung* eines
  fortgesetzten Turniers;
- `rookstudio.log`: das Protokoll der Prozesse, die es gespielt haben (jedes Turnier spielt in einem eigenen
  Prozess); `control.json`, während eines es spielt: wo sein lokaler Socket ist (`control.sock` im Ordner, oder im
  Laufzeitordner des Benutzers, wenn dieser Pfad zu lang ist; eine Named Pipe unter Windows), und welcher Prozess
  lauscht;
- `logs/game-N.log`: jede mit beiden Engines während Partie N (die im Tab *Partien* gezeigte Nummer) ausgetauschte
  Zeile, mit der Zeit, zu der sie gesendet oder gelesen wurde (`W>` an die Engine von Weiß gesendet, `W<` aus ihrer
  Ausgabe gelesen, `W!` aus ihrer Fehlerausgabe gelesen: Assertions, Absturzmeldungen), für die Partien, die durch
  Zeit, durch einen Absturz, einen Hänger oder einen illegalen Zug verloren wurden (oder für jede Partie, falls
  gewünscht). Die ersten 300 Zeilen jeder Engine und die letzten 5000 Zeilen der Partie werden behalten, in der
  Reihenfolge, in der sie kamen. Das ist es, was ein Engine-Autor braucht, um einen Fehler nachzustellen. Bei einer
  WinBoard-Engine zeigt es die UCI-Seite; `FCG_WB2UCI_LOG` liefert die WinBoard-Seite;
- `logs/start-<engine>.log`: der letzte gescheiterte Versuch, eine Engine zu starten, die nicht startet: ihr
  Programm, der Fehler und die bis dahin mit den Engines dieser Partie ausgetauschten Zeilen (siehe
  [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)).

Ein von jemand anderem erhaltener Ordner lässt sich gefahrlos öffnen: Die Datei von *Jede Partie zusätzlich anhängen
an* muss eine `.pgn`-Datei sein (ein anderer Name wird mit einer Warnung ignoriert), und ein Ordner, dessen Journal,
PGN-Dateien oder Sperrdatei symbolische Links sind, wird nicht zum Schreiben geöffnet, so dass er das Programm nicht
dazu bringen kann, außerhalb von ihm zu schreiben. Wird er fortgesetzt, laufen die Engines, die seine
`tournament.json` nennt, wie bei jedem Turnier.

Mit Version 0.2 gespielte Matches erscheinen auf der Seite Turniere: *Importieren* macht sie zu beendeten Turnieren.

## Befehlszeile
<!-- en: command-line ab7d1780 -->

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

- `--profile DIR` verwendet einen anderen Profilordner;
- `--autostart` startet beim Start die gespeicherte Einrichtung (statt das Turnier fortzusetzen, das lief, siehe
  [Pausieren, anhalten und fortsetzen](#pausieren-anhalten-und-fortsetzen));
- `--size WxH` öffnet das Fenster in dieser Größe, zentriert, statt so, wie es verlassen wurde, und lässt die für den
  nächsten Start gespeicherte Größe und Lage unberührt;
- `--software` lässt den Prozessor das Fenster zeichnen (den Software-Renderer von SDL) statt OpenGL, wie es
  *Software-Rendering* in den **Einstellungen** tut, für diesen Start (siehe
  [Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird));
- `--shot` speichert nach der angegebenen Zahl von Sekunden (standardmäßig 5) einen Screenshot des Fensters, mit
  WINDOW im Vordergrund, und beendet sich, sobald jeder Screenshot gemacht ist. Es lässt sich wiederholen. WINDOW ist
  eine Seite (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`,
  `ratings-progress`, `console`, `settings`) oder ein Tab (`live`, `games`, `openings`, `standings`, `schedule`,
  `bracket`, `broadcast`). Die Maus wird in diesem Modus ignoriert, und unter Linux wird das Fenster außerhalb des
  Bildschirms gerendert (es erscheint kein Fenster; setze `SDL_VIDEODRIVER`, um das zu ändern); das Fenster öffnet
  sich in der Standardgröße oder mit `--size`, und die gespeicherte Größe und Lage bleiben unberührt;
- `--focus WINDOW` öffnet beim Start diese Seite oder diesen Tab, oder: `engines-card` (die Karte der ersten Engine),
  `engines-refresh` (*Aktualisieren: die neuen Engines des Ordners*), `engines-ratings=FILE` (die Vorschau dieser
  Rangliste), `setup-picker` und `setup-suggest` (der Filter der Bibliothek fokussiert, oder die vorgeschlagenen
  Gegner ausgewählt), `setup-open=ROW` und `setup-profiles=ROW` (eine Zeile der Einrichtung geöffnet, oder ihre
  Profilliste: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (das
  eigene Buch des ersten Teilnehmers), `submission-export` (*Für eine Rangliste exportieren…*), `ratings-players` (das
  Fenster *PGN-Spieler*), `clone=DIR` (die Seite *Neues Turnier*, gefüllt durch *Klonen* des Ordners dieses
  Turniers), `palette[=TEXT]` (die Befehlspalette, mit TEXT darin eingetippt), `game-menu=N` (das Menü
  von Partie N im Tab *Partien*, ein Ergebnis gewählt), `end-menu=N` (*Mit einem Ergebnis beenden* für die Partie von
  Platz N, ab 1), `big-screen[=N]` (das Großbild, mit der Partie von Platz N), `live-back=N` (die verfolgte Partie in
  *Live*, um N Halbzüge zurückgeblättert), `engines-console` (die Konsole der ersten Engine), `board-analyse` (die
  Seite Brett, analysiert), `board=FILE` (eine PGN-, FEN- oder EPD-Datei auf der Seite Brett; `board-analyse=FILE`
  analysiert, `board-review=FILE` ihre erste Partie Zug für Zug analysiert), `board-edit` (der Stellungseditor),
  `board-play` und `board-play-start` (eine Partie zwischen den ersten beiden Engines, gestartet), `machines` (die
  Karte der ersten entfernten Maschine in den **Einstellungen**; `machines-test` getestet, `machines-files` die
  Bücher und Tablebases der Einrichtung mit ihr verglichen), `guide[=ID]` (der Anleitungsbereich, beim Abschnitt ID),
  `guide-search=TEXT` (der Anleitungsbereich, auf der Suche nach TEXT);
- `--select-game N` öffnet Partie N (in der Reihenfolge ihres Endes) im Tab *Partien*, sobald es sie gibt;
- `--quit-after SECONDS` hält das Turnier an und beendet sich;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: siehe [Headless-Modus](#headless-modus);
- `--version` gibt die Version aus und das Protokoll des Kanals, über den das Fenster ein entferntes Turnier verfolgt
  (siehe [Maschinen](#maschinen)), und beendet sich.

## Fehlerbehebung
<!-- en: troubleshooting e8c4882d -->

### Das Programm startet nicht
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows sagt, das Programm sei unsicher, oder ein Virenscanner entfernt es.** Das Programm ist nicht signiert,
  daher misstrauen ihm Windows SmartScreen und manche Virenscanner. *Weitere Informationen*, *Trotzdem ausführen* lässt
  es starten; einem Virenscanner lässt sich sagen, dass er seinen Ordner zulassen soll. Die Releases werden von GitHub
  Actions aus den öffentlichen Quellen gebaut.
- **macOS weigert sich, es zu öffnen.** Es ist nicht von einem Apple-Entwicklerkonto signiert: Klicke es mit gedrückter
  Ctrl-Taste an (Rechtsklick), Öffnen, dann noch einmal Öffnen; ab macOS 15 versuche einmal, es zu öffnen, dann
  Systemeinstellungen, Datenschutz & Sicherheit, Dennoch öffnen. Oder führe `xattr -dr com.apple.quarantine` auf dem
  entpackten Ordner aus.
- **Eine Meldung sagt, dass keine Art funktioniert, das Fenster zu zeichnen, oder das Fenster bleibt schwarz.** Dem
  Grafiktreiber fehlt OpenGL 3.2, oder er versagt: Starte mit `--software` (der Prozessor zeichnet das Fenster), und
  wenn das funktioniert, hake *Software-Rendering* in den **Einstellungen** an. Unter Wayland ohne Grafiktreiber starte
  mit `SDL_VIDEODRIVER=x11`. Siehe [Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird).
- **Chinesischer Text erscheint als Kästchen.** Chinesisch braucht eine Systemschrift: Microsoft YaHei unter Windows,
  Noto Sans CJK oder WenQuanYi unter Linux (installiere `fonts-noto-cjk` oder Ähnliches).

### Eine Engine startet nicht
<!-- en: an-engine-does-not-start 202a67d4 -->

- **Ihre Prüfung schlägt fehl, oder ihr Turnier sagt, dass sie nicht starten kann.** Lies den Fehler unter der Engine,
  dann ihr Engine-Log (*Engine-Log*, oder `logs/start-<engine>.log` im Turnierordner): Die Fehlerausgabe der Engine
  nennt oft die fehlende Datei. Die üblichen Ursachen: das Programm wurde verschoben (*Suchen…*), ein Netz oder eine
  Bibliothek wird nicht gefunden (setze das Arbeitsverzeichnis oder `EvalFile` auf ihrer Karte), ein Programm für einen
  anderen Prozessor gebaut (ein Build mit AVX-512 auf einer Maschine ohne), ein langsamer Start (erhöhe
  *Start-Timeout* auf ihrer Karte). Siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann).
- **Eine WinBoard-Engine startet nicht aus den Stellungen des Buchs.** Eine Engine mit Protokoll 1 ohne `setboard`
  kann nur aus der Ausgangsstellung beginnen: Gib ihr ein PGN-Buch (Züge), kein EPD-Buch. Siehe
  [WinBoard-Engines](#winboard-engines).
- **Das Turnier startet nicht: Eine Engine spielt kein Chess960.** Jede Engine muss die Option `UCI_Chess960` (UCI)
  oder die Variante `fischerandom` (WinBoard) haben; die Zusammenfassung nennt die, die sie nicht haben.

### Verlorene oder fragwürdige Partien
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Eine Engine verliert früh in ihren Partien durch Zeit.** Häufiger ist sie falsch eingerichtet, als dass sie
  schlecht spielt: zu viele Threads für die Kerne, die die Partien haben, ein Hash, der zu groß ist, um rechtzeitig reserviert
  zu werden, ein Netz, das bei jeder Partie geladen wird, ein langsamer Start. Senke ihre `Threads` und ihren `Hash`,
  entferne das Häkchen bei *Die Engine für jede Partie neu starten*, erhöhe *Start-Timeout*; die Karte *Zustand* und
  *TF* in der Übersicht zeigen die Zeitverluste.
- **Die Engines wirken langsamer, als sie sein sollten.** Zu viele Partien gleichzeitig: Lass *Parallelität* auf
  automatisch, und halte Parallelität × Threads innerhalb der physischen Kerne. *Prozessornutzung* in der Übersicht
  nennt eine Engine, die mehr Kerne nutzt als ihre `Threads` oder auf der Zeit ihres Gegners denkt.
- **Die Zusammenfassung warnt, dass der Speicher nicht reicht.** Die Hashtabellen der Partien würden nicht in den
  freien Speicher passen: Senke den `Hash` (unter *Engine-Einstellungen* oder den der genannten Engine) oder die
  Parallelität.
- **Tablebase-Dateien sollen fehlen oder abgeschnitten sein.** Ein Download wurde mittendrin abgebrochen, oder der
  Ordner enthält nur kleinere Tabellen: Lade die im Tooltip genannten Dateien erneut herunter, oder senke die
  Abschätzung auf die gefundene Steinzahl (*Auf 5 Steine senken*).
- **Ein Buch soll illegale Stellungen oder Duplikate enthalten.** fastchess lässt sie weg; *Bereinigte Kopie
  speichern…* auf der Seite Bücher schreibt ein Buch ohne sie. Siehe [Ein Buch prüfen](#ein-buch-prüfen).
- **Eine Partie wurde falsch entschieden.** Rechtsklick auf sie in *Partien* oder *Spielplan*: Spiele sie neu,
  annulliere sie oder setze ihr Ergebnis, mit dem Grund. Siehe
  [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern).

### Probleme bei der Übertragung
<!-- en: broadcasting 4a2e6916 -->

- **TLCV oder ein anderer Viewer verbindet sich nicht.** Starte das Turnier (und seinen TLCS-Server) vor dem Viewer:
  Er meldet sich nicht von selbst neu an. Öffne den UDP-Port im Router für Zuschauer außerhalb des lokalen Netzwerks.
- **Die Webseite ist von außen nicht erreichbar.** Hake *Im Internet veröffentlichen* für einen Tunnel an (siehe
  [Die Webseite ins Internet stellen](#die-webseite-ins-internet-stellen)), oder öffne den TCP-Port im Router.
- **lichess lehnt das Token ab.** Das Token braucht den Scope `study:write`: Erstelle ein neues mit *Token auf lichess
  erstellen…*. Hinter einem Proxy setze `HTTPS_PROXY`.

### Alles andere
<!-- en: anything-else ce59c083 -->

*Problem melden…* in den **Einstellungen** oder auf der Seite **Protokoll** öffnet ein GitHub-Issue, schon ausgefüllt
mit der Version, dem System und den letzten Zeilen des Protokolls (siehe [Ein Problem melden](#ein-problem-melden)).

## Glossar
<!-- en: glossary 7cb8fba3 -->

- **Abschätzung**: das Beenden einer Partie vor dem Matt, wenn ihr Ergebnis klar ist: Remis, wenn beide Bewertungen
  nahe 0 bleiben, Verlust, wenn sie weit darunter bleiben, oder was die Tablebases sagen. Siehe
  [Abschätzung](#abschätzung).
- **BayesElo, DrawElo**: ein Wertungsmodell mit einem eigenen Parameter für Remis (DrawElo), aus dem Programm von Rémi
  Coulom.
- **Berger-Tabellen**: die Reihenfolge der Runden eines Rundenturniers, in der jede Engine einmal pro Runde spielt.
- **Buchholz, Sonneborn-Berger**: Feinwertungen: die Summe der Punkte der Gegner (Schweizer System), und die Summe der
  Punkte der besiegten Gegner plus die Hälfte derer der Gegner, gegen die remisiert wurde (Rundenturnier).
- **Chess960, Chess324**: Startstellungen mit gemischter Grundreihe: 960 mit dem König zwischen den Türmen (Rochade auf
  die üblichen Felder), 324 mit König und Türmen an ihrem Platz.
- **Cursed Win**: eine Stellung, die die Tablebases als gewonnen werten, deren Matt aber mehr Züge braucht, als die
  50-Züge-Regel erlaubt: ein Remis in einer Partie, die nach dieser Regel gespielt wird.
- **ECO**: die Codes der Encyclopaedia of Chess Openings, A00 bis E99.
- **Elo, nElo**: der Wertungsunterschied, für den eine Punktzahl steht. nElo (normalisierte Elo) teilt durch die
  Streuung der Ergebnisse, so dass ihr Wert nicht von der Remisquote abhängt; fishtest und OpenBench geben ihre
  SPRT-Grenzen darin an.
- **EPD, FEN**: eine Zeile, die eine Stellung beschreibt; eine EPD-Zeile kann Operationen tragen, etwa den besten Zug
  `bm` einer Testsuite.
- **Eröffnungssammlung, Buch**: die Stellungen oder Züge, aus denen die Partien beginnen, jede zweimal mit vertauschten
  Farben gespielt.
- **Gauntlet**: Die getesteten Engines (die gesetzten) spielen gegen jede andere Engine; diese spielen nicht
  gegeneinander.
- **H0, H1**: die beiden Hypothesen eines SPRT: Der Elo-Unterschied ist elo0 oder weniger (H0), oder elo1 oder mehr
  (H1).
- **Hash**: der Speicher, den eine Engine für ihre Hashtabelle belegt, in MB.
- **LLR**: das Log-Likelihood-Verhältnis eines SPRT; der Test endet, wenn es seine Grenzen verlässt, die sich aus alpha
  und beta ergeben.
- **LOS**: die Überlegenheitswahrscheinlichkeit (likelihood of superiority), die Wahrscheinlichkeit, dass eine Engine
  stärker ist als eine andere, aus ihren Partien.
- **MultiPV**: eine Engine, die mehrere Linien gleichzeitig durchsucht.
- **Neuerung**: der erste Zug einer Partie, den in dieser Stellung noch keine Partie der Datenbank gespielt hatte.
- **Parallelität**: die Zahl der gleichzeitig gespielten Partien.
- **Pentanomial**: die Ergebnisse der Partiepaare, gezählt nach ihrer Summe: 0, ½, 1, 1½ oder 2 Punkte. Es ergibt
  engere Fehlerbalken als das Zählen einzelner Partien, da sich die beiden Partien eines Paars eine Eröffnung teilen.
- **PGN**: das Textformat für Schachpartien: Tags wie `[White "…"]`, dann die Züge.
- **Polyglot**: das Binärformat für Eröffnungsbücher (`.bin`), das die meisten Programme lesen, mit einem Gewicht für
  jeden Zug.
- **Ponder**: Denken auf der Zeit des Gegners; in den Turnieren von RookStudio aus.
- **Repertoire**: die Eröffnungen, die man spielt: in jeder Stellung ein gewählter Zug für die eigene Seite, und auf die
  Züge des Gegners eine Antwort.
- **Setzplatz**: ein Platz in der Reihenfolge der Teilnehmer: die getesteten Engines eines Gauntlets, die Favoriten
  eines K.-o.-Turniers.
- **SPRT**: der sequenzielle Wahrscheinlichkeitsquotiententest (sequential probability ratio test), der so lange
  Partien spielt, bis er mit den gewählten Fehlerraten (alpha, beta) sagen kann, ob eine Engine elo1 oder mehr gewinnt
  (H1) oder nicht mehr als elo0 (H0).
- **Syzygy**: die Endspiel-Tablebases, die das Ergebnis jeder Stellung mit wenigen Steinen liefern (WDL-Dateien,
  `.rtbw`).
- **TF**: time forfeits, die durch Zeit verlorenen Partien.
- **Threads**: die Prozessor-Threads, mit denen eine Engine sucht.
- **TLCS, TLCV**: Tom's Live Chess Server und Viewer, ein Protokoll, um Engine-Partien live zu verfolgen.
- **UCI, WinBoard (CECP)**: die beiden Protokolle, über die Programme mit Schach-Engines sprechen.
- **WDL**: die Gewinn-, Remis- und Verlustchancen, die eine Engine mit ihrer Bewertung angibt.
- **Zyklus**: in einem Rundenturnier jede Paarung einmal mit jeder Farbe gespielt (cutechess nennt es eine Runde, mit
  2 Partien pro Begegnung).

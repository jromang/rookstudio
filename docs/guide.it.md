# Guida utente di RookStudio

Questa guida descrive ogni pagina, impostazione e file di RookStudio: i motori che giocano tornei, e lo studio delle
partite (database, notazione, albero delle aperture, repertori, Lichess e un assistente locale). Il
[README](../README.md) dice cos'è il programma, dove scaricarlo e come compilarlo. Nel programma, F1 apre questa guida
accanto alla pagina, alla sezione di ciò che si trova sotto il mouse, nella lingua dell'interfaccia (vedi [Aiuto nel
programma](#aiuto-nel-programma)).

## Avvio rapido
<!-- en: quick-start 92353f2c -->

### Il tuo primo torneo
<!-- en: your-first-tournament 75e9a891 -->

1. **Motori.** Nella barra a sinistra, *Motori*, poi *Aggiungi…* e scegli il programma di un motore (un eseguibile
   UCI o WinBoard): viene avviato una volta per leggerne il nome e le opzioni. *Importa…* > *Una lista di motori…*
   prende i motori di un altro programma (cutechess, Banksia, Arena, ChessGUI), *Importa…* > *Tutti i motori di una
   cartella…* tutti i programmi di una cartella. *Verifica* dice se un motore funziona.
2. **Nuovo torneo.** *Tornei* > *Nuovo torneo*. Scegli un *Formato* nel suo elenco (*Girone all'italiana*,
   *Gauntlet*…) e una *Cadenza* (*10+0.1* per una prima prova), e lascia le altre righe come sono.
3. **Partecipanti.** Fai doppio clic sui motori nella *Libreria* accanto ai partecipanti, oppure selezionane diversi e
   premi Invio.
4. **Avvia.** Il torneo si apre nel suo spazio di lavoro: *Panoramica* per la classifica e il tempo rimanente,
   *Diretta* per le partite in corso.

Il riepilogo a destra della configurazione dice quante partite verranno giocate e quanto dureranno, e cosa andrebbe
storto (un programma non trovato, troppi thread, un libro mancante), ogni cosa con un pulsante che la sistema.

### Testare una nuova versione di un motore
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Aggiungi entrambe le versioni alla libreria.
2. *Nuovo torneo*, *Formato*: *SPRT miglioramento [0, 3]* (la nuova versione guadagna?) o *SPRT non regressione
   [-5, 0]* (non è più debole?).
3. Scegli la nuova versione come **A** e quella vecchia come **B**, una cadenza breve (*10+0.1*) e, in *Aperture*, un
   libro di posizioni equilibrate (EPD), poi **Avvia**.

Il match si ferma da solo appena l'SPRT decide: H1 accettata (A è più forte, secondo i limiti) o H0 accettata (non lo
è). La *Panoramica* mostra l'Elo, l'LLR e i suoi limiti man mano che arrivano le partite. La configurazione viene
conservata: la volta successiva scegli la versione seguente come A e **Avvia**, oppure clicca *Rilancia con…* sul
match terminato.

### Studiare le tue partite
<!-- en: studying-your-games 9a78e41c -->

1. **Un database.** Nella barra laterale, *Database*, poi *Nuovo database*, con il nome scritto sul posto, e *Importa un
   file…*: un file PGN, o un file `.zst`, `.gz` o `.zip` che ne contiene uno (un file trascinato sulla pagina fa lo
   stesso). Le partite arrivano in background, poi viene costruito l'albero delle aperture del database.
2. **Una partita.** Un clic su una partita della lista la mostra accanto; Invio o un doppio clic la apre nella
   *Scacchiera*. Ctrl+O vi apre un file PGN, Ctrl+V incolla una partita o un FEN.
3. **Un motore.** L'analisi richiede un motore: con la libreria vuota, la scheda *Analisi* propone di scaricare Gaia con
   un clic (vedi [Motori popolari](#motori-popolari)). A avvia e ferma l'analisi della posizione, G analizza l'intera
   partita (Alt+A e Alt+G sulla Scacchiera per lo studio).
4. **Le tue note.** Invio scrive un commento dopo la mossa, `!` e `?` mettono dei simboli, una mossa diversa da quella
   della partita apre una variante, il tasto destro disegna frecce e case; Ctrl+S salva la partita nel suo database.

### Dove proseguire
<!-- en: where-to-go-next d21be582 -->

- [Nuovo torneo](#nuovo-torneo): ogni impostazione, e [consigli](#consigli-per-le-impostazioni) per sceglierle;
- [Svolgimento di un torneo](#svolgimento-di-un-torneo): cosa mostrano le schede, la pausa, come modificare un torneo
  mentre gioca;
- [Classifiche](#classifiche): una sola lista Elo su tutte le partite giocate;
- [Database](#database), [la Scacchiera per lo studio](#la-scacchiera-per-lo-studio), [la pagina
  Aperture](#la-pagina-aperture), [la pagina Lichess](#la-pagina-lichess) e [l'assistente](#lassistente) per le tue
  partite;
- [Viste](#viste): le pagine della barra laterale, come le vuoi;
- [Trasmissione](#trasmissione): le partite in diretta su una pagina web, in TLCV o su lichess;
- [Risoluzione dei problemi](#risoluzione-dei-problemi) quando qualcosa va storto, e il [Glossario](#glossario) per le
  parole dei tester.

## La finestra
<!-- en: the-window 3a5230d4 -->

### Le pagine
<!-- en: the-pages 1cab137d -->

Una barra di navigazione a sinistra porta alle pagine della [vista](#viste) mostrata, nel suo ordine:

| Pagina | Contenuto |
|---|---|
| Home | La partita da riprendere, cosa avviare, le partite recenti, i tuoi database, Lichess e ciò che è in corso: schede delle pagine della vista (vedi [La pagina Home](#la-pagina-home)) |
| Tornei | Cosa è in corso e cosa richiede un intervento, la coda, gli ultimi tornei terminati, tutti i tornei del profilo, *Nuovo torneo* |
| Scacchiera | Una partita o una posizione di qualsiasi provenienza, analizzata da un motore della libreria; in una vista per lo studio, la notazione modificabile direttamente (vedi [La Scacchiera per lo studio](#la-scacchiera-per-lo-studio)) |
| Database | I tuoi database di partite: importazione, lista delle partite, ricerche |
| Aperture | I tuoi repertori, la loro copertura e le loro lacune |
| Allenamento | Il ripasso del giorno, i problemi di Lichess, i tuoi errori come esercizi, indovinare le mosse, i finali contro le tablebase, i matti, la visione della scacchiera (vedi [La pagina Allenamento](#la-pagina-allenamento)) |
| Gioca | Una partita contro un motore della libreria alla forza che scegli, con le sue aperture e un handicap, e le tue partite su Lichess, entrambe giocate nella pagina Scacchiera (vedi [La pagina Gioca](#la-pagina-gioca)) |
| Lichess | Il tuo account Lichess, le partite di un giocatore, le trasmissioni ufficiali, ciò che l'analisi può chiedere a Lichess |
| Motori | La libreria dei motori: opzioni, verifiche, gruppi, Elo di riferimento, importazione delle liste di altri programmi, i motori popolari |
| Libri | La libreria dei libri di aperture, e le aperture di ogni libro con i loro difetti |
| Suite | Suite di test (STS, WAC, ERET…): motori contro le posizioni di un file EPD |
| Classifiche | Una sola lista Elo su tutte le partite di tutti i tornei |
| Log | L'output di fastchess stesso e i messaggi del programma, e *Segnala un problema…* |
| Impostazioni | Lingua, tema, dimensione dell'interfaccia, notifiche, aspetto delle scacchiere, viste, assistente, cartelle del profilo e dei tornei, macchine remote, token lichess, versione, modo in cui viene disegnata la finestra: prima le sezioni delle pagine della vista, le altre sotto *Altre impostazioni* |

### Viste
<!-- en: views 9c72e404 -->

Una vista è un insieme di pagine tutto tuo: fino a tre, ciascuna con il suo nome, la sua icona e il suo colore, da
scegliere in cima alla barra laterale. Ogni vista vi ha un pallino, pieno per la vista mostrata, e il nome della vista
mostrata è scritto sotto di essi; un clic su un pallino, o da Ctrl+1 a Ctrl+3, mostra quella vista, alla pagina in cui
era. Con una sola vista, il selettore scompare.

Il primo avvio ne propone tre: *Test* (Tornei, Motori, Libri, Scacchiera, Suite, Classifiche, Log), *Studio* (Home,
Scacchiera, Database, Aperture, Allenamento, Gioca, Lichess, Motori) e *Tutto* (tutte le pagine). Impostazioni è in ogni
vista. Una pagina che arriva con una nuova versione non viene aggiunta da sola a una vista che hai già: la pagina Home
la propone una volta (vedi [La pagina Allenamento](#la-pagina-allenamento) e [La pagina Gioca](#la-pagina-gioca)).

*Modifica le viste* apre l'editor delle viste al posto della pagina: *Impostazioni* > *Viste*, ⋯ sotto il selettore, un
clic destro su un pallino (l'editor si apre sulla sua vista), o *Modifica le viste…* in Ctrl+K. Contiene:

- le viste, nell'ordine da Ctrl+1 a Ctrl+3 (*Su*, *Giù*), *Nuova vista* (tre al massimo) ed *Elimina questa vista*;
- la vista scelta: il suo nome (breve: viene scritto sotto il selettore), la sua icona tra dodici, il suo colore tra
  sei;
- la sua Scacchiera: *Per lo studio* (la notazione modificabile direttamente, le frecce e le case, le schede Albero,
  Riferimenti e Domande: vedi [La Scacchiera per lo studio](#la-scacchiera-per-lo-studio)) o *Per l'analisi dei
  motori* (la Scacchiera dei tester di motori, com'era);
- le sue pagine, spuntate, nell'ordine della barra laterale (↑ e ↓ accanto a ciascuna la spostano; Impostazioni resta
  ultima); una pagina può stare in più viste, e *anche: …* nomina le altre;
- la barra laterale, come apparirà.

Tutto si vede subito nella barra laterale. Ctrl+Z annulla ogni modifica finché l'editor è aperto, Invio conserva le
modifiche, Esc lascia le viste com'erano. *Ripristina le viste proposte* riporta le tre del primo avvio.

Ctrl+K elenca ogni pagina con la vista in cui si apre: scegliere una pagina di un'altra vista passa a quella vista. Una
pagina che nessuna vista contiene (un avviso può portarvi) si apre nella vista mostrata, con *Aggiungila a …*.

### Il primo avvio
<!-- en: the-first-start 18171dec -->

Al primo avvio, RookStudio chiede:

- la lingua dell'interfaccia;
- cosa vuoi fare: le viste proposte, ciascuna spuntata o no (*Test*, *Studio*, *Tutto*), e quale si mostra per prima;
- quando sulla macchina c'è un profilo di fastchess-gui, se copiarlo: i suoi motori, libri, tornei e impostazioni.
  fastchess-gui resta com'è e può continuare a funzionare in parallelo; *Inizia senza* lo lascia stare.

Tutto si può cambiare più tardi in *Impostazioni* > *Viste*.

### Aiuto nel programma
<!-- en: help-in-the-program c08dbff5 -->

- **Tooltip.** Lascia il mouse su un controllo per vedere cosa fa. Il tooltip di un controllo che questa guida spiega
  più a fondo termina con *F1: maggiori dettagli nella guida*.
- **F1** apre questa guida in un pannello a destra della pagina, alla sezione di ciò che si trova sotto il mouse (una
  riga della configurazione, una scheda della pagina Tornei, la linguetta di una scheda, una colonna della
  classifica); se sotto il mouse non c'è nulla, alla sezione del pannello cliccato per ultimo, altrimenti a quella
  della pagina. Il **?** rotondo accanto al titolo di una pagina apre la sezione della pagina. Di nuovo F1, o Esc nel
  pannello, lo chiude.
- **Il pannello della guida** mostra una sezione con le sue sottosezioni, e la pagina accanto continua a funzionare:
  si può leggere la spiegazione mentre si cambia l'impostazione. La sua intestazione ha *Indietro* e *Avanti* (anche
  Alt+Sinistra e Alt+Destra, o i tasti laterali del mouse), *Indice* (tutte le sezioni), il campo di ricerca (Ctrl+F
  nel pannello) e *Apri nel browser* (questa guida su GitHub, per la versione in uso). La ricerca trova tutte le
  parole digitate, in qualsiasi ordine, maiuscole e accenti a parte; i titoli vengono prima, e le corrispondenze sono
  evidenziate. I link ad altre sezioni si aprono nel pannello, quelli a pagine web nel browser, e un blocco di codice
  ha *Copia*. Trascina il bordo sinistro del pannello per allargarlo; la sua larghezza viene conservata.
- **Lingue.** La guida è integrata nel programma, in tutte le lingue dell'interfaccia, e segue la versione in uso;
  niente viene scaricato dalla rete. Una sezione la cui traduzione manca o è più vecchia del testo inglese viene
  mostrata in inglese, con una riga che lo dice. Anche gli archivi delle versioni la contengono (`guide.md`, e
  `guide.<language>.md` per ogni traduzione).
- **Ctrl+K**, la tavolozza dei comandi, trova le sezioni della guida per titolo (*Guida: …*), e *Cerca «…» nella
  guida* vi cerca ciò che è stato digitato.
- **Messaggi.** Un problema segnalato dal programma nomina il motore, il file o la risorsa, con la soluzione a portata
  di mano; un **?** accanto apre la sua voce in [Risoluzione dei problemi](#risoluzione-dei-problemi).
- **Per iniziare.** Finché non viene avviato un primo torneo, la pagina Tornei mostra i passi dell'[Avvio
  rapido](#avvio-rapido), ciascuno con il suo pulsante, spuntato una volta fatto; *Nascondi* li toglie.
- [Segnala un problema](#segnalare-un-problema) quando il programma non fa ciò che dovrebbe.

### La tavolozza dei comandi
<!-- en: the-command-palette 14590459 -->

**Ctrl+K** apre la tavolozza dei comandi: le pagine e le schede, i motori della libreria (apre la loro scheda), i
libri, i tornei della pagina Tornei, le sezioni della guida, e le azioni possibili in quel momento (*Pausa*,
*Riprendi*, *Metti tutto in pausa*, *Ferma*, *Avvia*, *Metti in coda*, *Rilancia … con…*, le voci di *Importa…*,
*Verifica quelli non verificati*, *Esporta per una lista di rating…*, *Copia* la classifica, *Copia la versione*,
*Segnala un problema…*, i colori della scacchiera e i set di pezzi, i suoni delle mosse attivati o disattivati…).
Maiuscole e minuscole non contano e le lettere digitate non devono essere adiacenti («le» trova *Lista Elo*); i nomi
che iniziano con esse, o con una parola che inizia con esse, vengono prima. Su e Giù scelgono, Invio esegue, Esc
chiude; Ctrl+K viene ignorato mentre si scrive in un campo di testo.

### Lingua, tema e dimensione
<!-- en: language-theme-and-size 954fe416 -->

L'interfaccia è in inglese, francese, tedesco, spagnolo, italiano, portoghese brasiliano, russo o cinese semplificato.
All'avvio segue la prima lingua preferita del sistema (l'inglese quando non esiste una traduzione per essa); la si può
imporre in **Impostazioni**. Il cinese usa un font del sistema: Microsoft YaHei su Windows, Noto Sans CJK o
WenQuanYi su Linux. Il tema è chiaro, a meno che in **Impostazioni** non si scelga quello scuro, o quello del
sistema.

Le traduzioni spagnola, italiana, portoghese, russa e cinese sono nuove: le correzioni dei giocatori di queste lingue
sono benvenute. Quelle dell'interfaccia sono semplici file `.po` in `gui/i18n`, che Poedit e programmi simili aprono;
quelle di questa guida sono `docs/guide.<language>.md`.

Le pagine usano tutta la finestra, qualunque sia la sua dimensione: le scacchiere della scheda Diretta crescono con
essa, il modulo del nuovo torneo si divide in due colonne su una finestra larga, e l'elenco delle pagine Motori, Libri
e Classifiche si può allargare trascinandone il bordo destro. Tutto (testo, pulsanti, scacchiere) si può anche
ingrandire o rimpicciolire, dall'80 al 250 %: **Impostazioni** > Dimensione dell'interfaccia, oppure Ctrl + rotellina
del mouse, Ctrl + e Ctrl -, e Ctrl 0 per tornare al 100 %. F11 porta la finestra a schermo intero, e la riporta
indietro.

Un badge sulla scheda *Diretta* e sul pulsante *Tornei* indica il numero di partite in corso. Un campo numerico
accetta operazioni aritmetiche: `40*60`, `15+10`, con `+ - * /` e parentesi, calcolate con Invio o quando si lascia il
campo; Esc riporta il campo al suo valore precedente.

### Le scacchiere e i loro pulsanti
<!-- en: boards-and-their-buttons 1ed997f4 -->

Sotto ogni scacchiera che mostra una partita (la riproduzione della scheda *Partite*, la partita in primo piano in
*Diretta*, una scacchiera staccata, la pagina Scacchiera), una fila di piccoli pulsanti porta alla posizione
iniziale, alla mossa precedente, alla mossa successiva e all'ultima, e ruota la scacchiera; i loro tooltip indicano i
tasti, che fanno lo stesso: ← → Inizio Fine, e F per ruotare. Chi preferisce i tasti nasconde i pulsanti con
**Impostazioni** > *Aspetto* > *Pulsanti di navigazione sotto le scacchiere*, con un clic destro su di essi, o con la
tavolozza dei comandi (*Nascondi i pulsanti di navigazione sotto le scacchiere*, e *Mostra…* per farli tornare). La
rotellina del mouse su un elenco di mosse scorre l'elenco, mai le mosse. **A** su qualsiasi scacchiera apre la sua
posizione nella pagina [Scacchiera](#scacchiera) e la analizza.

### Versione e nuove versioni
<!-- en: version-and-new-versions 6305c913 -->

La versione è nel titolo della finestra e in **Impostazioni**, il cui *Copia* la mette negli appunti insieme al
sistema, per una segnalazione di bug. *Avvisami quando esce una nuova versione* (disattivato per impostazione
predefinita) chiede a GitHub l'ultima versione pubblicata all'avvio, al massimo una volta al giorno e in background;
quando è più recente, la pagina Tornei lo dice, con *Pagina di download* (la pagina della versione su GitHub, nel
browser), *Novità* (le sue note) e *Nascondi* (fino alla versione successiva). Il programma non scarica né installa
nulla, e un errore (nessuna rete, GitHub irraggiungibile) viene solo scritto nel log.

### Segnalare un problema
<!-- en: reporting-a-problem 3e4e5989 -->

*Segnala un problema…* (in **Impostazioni**, nella pagina **Log**, nella tavolozza dei comandi e nel messaggio di un
errore all'avvio) apre una nuova issue su GitHub nel browser, dal modulo di segnalazione dei bug, già compilata con la
versione, il sistema (sistema operativo, processore, memoria, scheda grafica e driver) e le ultime righe del log.
Rileggila prima di inviarla, togli ciò che non vuoi mostrare (percorsi, nomi di motori) e di' cosa hai fatto, cosa ti
aspettavi e cosa è successo. Il programma non invia nulla da sé. Le idee vanno nello stesso posto: *New issue*,
*Idea*.

### Dimensione e posizione della finestra
<!-- en: window-size-and-place 892d41c4 -->

La finestra si apre come è stata lasciata: dimensione, posizione sugli schermi, massimizzata o no. Quando lo schermo
su cui si trovava non c'è più (un monitor scollegato), si apre al centro di quello principale; quando la sua
dimensione non entra più nello schermo (una risoluzione più bassa), alla dimensione predefinita. Anche i pannelli
trascinati fuori di essa, come una scacchiera staccata, tornano dov'erano (`layout.ini`), e dentro la finestra quando
il loro schermo non c'è più. Wayland non permette a un programma di scegliere la posizione della sua finestra: lì
tornano solo la dimensione e lo stato massimizzato.

### Disegno della finestra
<!-- en: drawing-the-window caff7bef -->

La finestra è disegnata con OpenGL 3.2. Dove non è disponibile (una macchina virtuale senza accelerazione 3D, un
desktop remoto, un vecchio server), il programma la disegna invece con il renderer proprio di SDL: Direct3D 11 su
Windows, OpenGL 2.1, o il solo processore. Il log dice quale, e perché OpenGL non è stato usato; anche
**Impostazioni** > Rendering lo mostra, con il nome della scheda grafica quando disegna OpenGL. *Rendering software*
lì (o `--software` sulla riga di comando, per un solo avvio) fa disegnare la finestra al processore dall'avvio
successivo, per un driver grafico che non funziona o va in crash. Usa più tempo del processore, ridisegna solo ciò che
cambia (e ciò che si muove, come gli orologi, 15 volte al secondo invece di 30 mentre mouse e tastiera sono fermi),
disegna angoli squadrati, e i pannelli dello spazio di lavoro fluttuano dentro la finestra principale invece che su
un altro schermo. Sotto Wayland (e su macOS) l'immagine del processore viene comunque mostrata tramite il driver
grafico; se non ce n'è uno, avvia il programma con `SDL_VIDEODRIVER=x11`. Quando nessun modo di disegnare funziona,
un messaggio lo dice prima che il programma termini.

### Aspetto
<!-- en: appearance 312266af -->

**Impostazioni** > *Aspetto* sceglie l'aspetto di tutte le scacchiere del programma (il mosaico in diretta e la
partita seguita, le scacchiere staccate, lo schermo grande, una partita rivista in *Partite*, la scheda *Aperture*,
le aperture di un libro), con una piccola scacchiera accanto alle scelte che le mostra:

![Impostazioni > Aspetto: la scacchiera marrone e i pezzi Celtic](screenshots/appearance.png)

- **Scacchiera**: i colori delle case, per il tema chiaro e per quello scuro (viene disegnata la scacchiera del tema
  in uso). Uno schema predefinito (*Grigio azzurro*, i colori delle versioni precedenti; *Marrone*; *Blu*; *Verde*;
  *Grigio*), oppure *Personalizzato*: un clic su *Case chiare*, *Case scure* o *Ultima mossa* apre un selettore di
  colore, e cambiare un colore rende la scacchiera personalizzata a partire dai colori che aveva. *Ultima mossa* viene
  sovrapposta alle due case dell'ultima mossa, con una trasparenza che lascia vedere il colore della casa.
- **Pezzi**: un set integrato nel programma (*Cburnett*, quello predefinito; *Chessnut*; *Celtic*; *Fantasy*; autori e
  licenze nel tooltip e in `ATTRIBUTION.md`), oppure *La tua cartella…*: una cartella di 12 immagini SVG chiamate
  `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` e `bK.svg` … `bP.svg` (re, donna, torre, alfiere,
  cavallo e pedone bianchi e neri). Le immagini sono disegnate con nanosvg, come quelle integrate: forme semplici,
  riempimenti, tratti e sfumature; niente testo, filtri o immagini incorporate. Quando un file manca, supera 1 MB o non
  è un'immagine che si possa disegnare, la pagina dice quale e perché, nel frattempo vengono disegnati i pezzi
  predefiniti, e *Rileggi* rilegge la cartella una volta corretti i file. La pagina web della trasmissione mantiene i
  pezzi predefiniti.
- **Suoni delle mosse** (disattivati per impostazione predefinita): *Un suono per ogni mossa* emette un breve colpo
  per ogni mossa della partita seguita, in *Diretta* e sullo schermo grande, e uno più pesante per una cattura; non
  per ogni partita del mosaico. *Volume*, e *Prova una mossa* / *Prova una cattura* per sentirli. I suoni sono generati
  dal programma (nessun file audio); il dispositivo audio viene aperto solo mentre sono attivi, e quando non ce n'è uno
  il log dice perché e il programma resta in silenzio.

- **Figurine nella notazione** (attive per impostazione predefinita): le mosse della pagina Scacchiera, nelle sue mosse
  e nelle linee della sua analisi, sono scritte con i pezzi del set scelto sopra (Nf3 diventa un cavallo e f3);
  disattivate, con le lettere.
- **Loghi** (disattivati per impostazione predefinita): *I loghi dei motori in Diretta e sullo schermo grande* disegna
  il logo di ogni motore, scelto sulla sua scheda (vedi sotto), accanto al suo nome in *Diretta*, sui suoi riquadri e
  sullo schermo grande; disattivati, solo i nomi. La pagina web della trasmissione li mostra comunque.

Anche la tavolozza dei comandi ha queste scelte (*Colori della scacchiera: Marrone*, *Pezzi: Celtic*, *Attiva i suoni
delle mosse*, *Mostra i loghi dei motori*…). Sono conservate in `settings.json` nel profilo; nella modalità portatile la
cartella dei pezzi viene conservata relativa alla cartella del programma.

**Loghi dei motori**: la scheda di un motore nella pagina **Motori** ha *Logo* > *Scegli…*, un'immagine PNG, JPEG o SVG
di 2 MB al massimo (4096 pixel al massimo per lato), mostrata accanto al nome del motore in *Diretta* e sullo schermo
grande quando i *Loghi* sono attivi (sopra), e sulla pagina web della trasmissione. L'immagine viene copiata nella
cartella `logos` del profilo, quindi l'originale può essere spostato o cancellato e il profilo spostato per intero;
*Rimuovi* la toglie (con la sua copia, a meno che un altro motore non la usi). Il logo si può cambiare mentre il motore
gioca. Finché i loghi sono disattivati, la scheda lo dice, con *Mostra i loghi*.

### La scacchiera 3D
<!-- en: the-3d-board cc0dd596 -->

**Impostazioni** > *Aspetto* > *Scacchiera 3D* > *La scacchiera della pagina Scacchiera in 3D* disegna la scacchiera
della pagina [Scacchiera](#scacchiera) in tre dimensioni, come la fotografia di una scacchiera vera (disattivata per
impostazione predefinita; tutte le altre scacchiere restano piatte). La scheda grafica la calcola con il path tracing:
l'immagine si affina per qualche secondo finché nulla si muove, pochi millisecondi della scheda per fotogramma, poi la
scheda si ferma; mentre la scacchiera ruota, un'immagine più grossolana ne prende il posto. La piccola scacchiera di
*Aspetto* è allora disegnata anch'essa in 3D, perché le scelte accanto si vedano mentre cambiano:

- l'illuminazione, come i fotografi illuminano una natura morta: *Studio*, *Low key*, *High key*, *Showroom*, *Luce
  da finestra*, *Ora d'oro* o *Sala del torneo*;
- *Esposizione*: più chiaro o più scuro di come lo rende l'illuminazione, in stop, ognuno raddoppia o dimezza la luce;
- *Qualità*: i campioni di cui è fatta l'immagine prima di fermarsi, 512 per impostazione predefinita; di meno
  risparmiano la grafica di un processore durante i tornei;
- *Vernice*, da un legno cerato e opaco a uno strato in cui si vedono le luci, e *Lucentezza*, dal satinato allo
  specchio;
- *Predefinito* riporta esposizione, qualità, vernice e lucentezza come erano; l'illuminazione resta.

Ciò che mostra la scacchiera piatta c'è: la barra di valutazione, l'ultima mossa, il pezzo preso e dove può andare, un
re sotto scacco, le frecce dell'analisi, che si vedono in trasparenza attraverso i pezzi, la risposta che il motore che
ha giocato la mossa si aspettava, tratteggiata, il giudizio sulla mossa come bollino sopra la sua casa, e la cornice di
una linea mostrata al posto della partita. Un clic sinistro sceglie una casa come sulla scacchiera piatta, su un pezzo o
sulla scacchiera dietro di esso, e un pezzo si può trascinare, con il suo fantasma che segue il puntatore (vedi
[Scacchiera](#scacchiera)). Se tieni premuto il tasto destro mentre muovi il mouse, la telecamera gira intorno alla
scacchiera (un clic destro senza muoversi lascia sempre il pezzo preso), la rotellina avvicina, e un clic della
rotellina riporta la vista; ruotare la scacchiera (F) fa fare alla telecamera mezzo giro.

La scacchiera 3D richiede OpenGL 3.3. Dove la finestra è disegnata senza (il renderer software di
[Disegno della finestra](#disegno-della-finestra), alcune macchine virtuali e desktop remoti), le scacchiere restano
piatte e *Aspetto* dice perché. I pezzi sono gli Staunton-Pieces di clarkerubber, con licenza MIT; *Aspetto* ne dà il
collegamento.

## Scorciatoie da tastiera
<!-- en: keyboard-shortcuts 149fc0ee -->

| Tasti | Dove | Cosa fanno |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Ovunque | La tavolozza dei comandi |
| <kbd>Ctrl</kbd>+<kbd>1</kbd>, <kbd>Ctrl</kbd>+<kbd>2</kbd>, <kbd>Ctrl</kbd>+<kbd>3</kbd> | Ovunque | Mostra la prima, la seconda, la terza vista |
| <kbd>F1</kbd> | Ovunque | Questa guida, alla sezione di ciò che è sotto il mouse; di nuovo: la chiude |
| <kbd>F11</kbd> | Ovunque | Schermo intero, e ritorno (la finestra propria di una scacchiera staccata quando ha la tastiera) |
| <kbd>Ctrl</kbd>+rotellina, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Ovunque | Dimensione dell'interfaccia; <kbd>Ctrl</kbd>+<kbd>0</kbd>: ritorno al 100 % |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Un campo numerico | Calcola il valore; riporta il campo al suo valore precedente |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Il pannello della guida | Indietro, avanti |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | Il pannello della guida | Cerca nella guida |
| <kbd>Esc</kbd> | Il pannello della guida | Lo chiude |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Pag↑</kbd> <kbd>Pag↓</kbd> | La tavolozza dei comandi | Scelgono |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | La tavolozza dei comandi | Esegue, chiude |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Inizio</kbd> <kbd>Fine</kbd> | Una scacchiera che mostra una partita | Mossa precedente, mossa successiva, inizio, ultima mossa |
| <kbd>F</kbd> | Una scacchiera | La ruota |
| <kbd>A</kbd> | Una scacchiera | Apre la posizione nella pagina Scacchiera e la analizza; nella pagina Scacchiera: avvia o ferma l'analisi |
| <kbd>Fine</kbd> | La partita in primo piano in *Diretta* | Torna alla mossa attuale |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | La pagina Scacchiera | Incolla un FEN, una riga EPD, un PGN o un elenco di mosse |
| <kbd>G</kbd> | La pagina Scacchiera | Analizza la partita, passaggio dopo passaggio; di nuovo: la ferma |
| <kbd>X</kbd> | La pagina Scacchiera | Mostra o nasconde la minaccia |
| <kbd>N</kbd>, <kbd>Maiusc</kbd>+<kbd>N</kbd> | La pagina Scacchiera | La mossa successiva, la precedente segnata dall'analisi |
| <kbd>Esc</kbd> | La pagina Scacchiera | Riporta la partita dopo che è stata mostrata una linea; posa il pezzo preso; una partita seguita da un mosaico: torna al mosaico |
| <kbd>Ctrl</kbd>+<kbd>O</kbd>, <kbd>Ctrl</kbd>+<kbd>N</kbd> | Una vista per lo studio | Apre un file PGN nella pagina Scacchiera; lì una nuova partita |
| <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>Maiusc</kbd>+<kbd>Tab</kbd> | Una vista per lo studio | Le partite recenti; rilasciando Ctrl si apre quella scelta |
| <kbd>Invio</kbd> | La pagina Home | Riprende la partita sulla scacchiera |
| Una lettera di una mossa | La Scacchiera per lo studio | Digita la mossa: vengono proposte le mosse legali che iniziano così, nelle lettere della lingua dell'interfaccia o in quelle inglesi; <kbd>↑</kbd> <kbd>↓</kbd> scelgono, Invio la gioca |
| <kbd>Invio</kbd>, <kbd>Maiusc</kbd>+<kbd>Invio</kbd> | La Scacchiera per lo studio | Un commento dopo la mossa, prima di essa |
| <kbd>!</kbd> <kbd>?</kbd> | La Scacchiera per lo studio | Il simbolo della mossa; due segni di fila: `!!`, `??`, `!?`, `?!` |
| <kbd>Canc</kbd> | La Scacchiera per lo studio | Elimina le mosse da quella mostrata |
| <kbd>Alt</kbd>+<kbd>↑</kbd> | La Scacchiera per lo studio | Fa salire la variante, fino alla linea principale |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | La Scacchiera per lo studio | Annulla, ripristina |
| <kbd>Ctrl</kbd>+<kbd>S</kbd>, <kbd>Ctrl</kbd>+<kbd>Maiusc</kbd>+<kbd>S</kbd> | La Scacchiera per lo studio | Salva la partita; ne salva una copia altrove |
| <kbd>Ctrl</kbd>+<kbd>←</kbd>, <kbd>Ctrl</kbd>+<kbd>→</kbd> | La Scacchiera per lo studio | La partita precedente, la successiva della lista da cui viene |
| <kbd>Ctrl</kbd>+<kbd>Maiusc</kbd>+<kbd>C</kbd> | La Scacchiera per lo studio | Il diagramma della posizione negli appunti, come immagine |
| <kbd>Alt</kbd>+<kbd>H</kbd> | La Scacchiera per lo studio | Espande le intestazioni della partita (un suggerimento durante una partita contro un motore) |
| <kbd>Alt</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>6</kbd> | La Scacchiera per lo studio | Le schede Notazione, Analisi, Albero, Riferimenti, Domande e Finali |
| <kbd>Alt</kbd>+<kbd>C</kbd> | La Scacchiera per lo studio | L'assistente commenta la mossa |
| <kbd>Alt</kbd>+<kbd>Q</kbd> | La Scacchiera per lo studio | Perché no? La mossa provata contro quella della partita, o la mossa della partita contro la migliore del resoconto |
| <kbd>Spazio</kbd>, <kbd>Invio</kbd>, <kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Esc</kbd> | La visita guidata | «Non lo so», il momento successivo, perché non un'altra mossa, ferma |
| <kbd>Alt</kbd>+<kbd>F</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>X</kbd>, <kbd>Alt</kbd>+<kbd>G</kbd>, <kbd>Alt</kbd>+<kbd>N</kbd>, <kbd>Alt</kbd>+<kbd>Maiusc</kbd>+<kbd>N</kbd> | La Scacchiera per lo studio | Ciò che fanno F, A, X, G, N e Maiusc+N sulla Scacchiera dei motori (qui le lettere servono a digitare mosse) |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd>, <kbd>*</kbd> | La scheda Albero | Sceglie una mossa, la gioca, la segna nel repertorio attivo |
| <kbd>Invio</kbd> | La pagina Aperture | Avvia l'allenamento del repertorio mostrato |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Spazio</kbd>, <kbd>Esc</kbd> | Una sessione di allenamento | Un suggerimento, «Non lo so», ferma |
| <kbd>Invio</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Una sessione di allenamento | La posizione successiva, o di nuovo dopo una risposta sbagliata; toglie una mossa aggiunta al repertorio |
| <kbd>Invio</kbd> | La pagina Allenamento | Ripassa tutto; oppure avvia i problemi dopo un clic su una delle loro scelte |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Spazio</kbd>, <kbd>Esc</kbd> | Una serie di problemi | Un suggerimento, «Non lo so», ferma |
| <kbd>Invio</kbd>, <kbd>→</kbd> | Una serie di problemi | Il problema successivo, o di nuovo dopo una risposta sbagliata; la soluzione una mossa alla volta |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Spazio</kbd>, <kbd>Esc</kbd> | Un esercizio dei tuoi errori | Un suggerimento, «Non lo so», ferma |
| <kbd>Invio</kbd> | Un esercizio dei tuoi errori | L'esercizio successivo, o di nuovo dopo una risposta sbagliata |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Indovina le mosse | La partita prosegue subito dopo un punteggio; ferma |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd> | I finali della pagina Allenamento | Scegli un finale dopo averne cliccato uno; giocalo |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Esc</kbd> | Un finale contro le tablebase | Ritira una mossa, la mossa giusta, ferma |
| <kbd>Invio</kbd> | Un finale contro le tablebase | Dopo un errore: ritirala; alla fine: di nuovo, o il successivo |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Visione della scacchiera | Avvia, prosegui; ferma |
| <kbd>Invio</kbd> | La pagina Gioca | Gioca una partita contro il motore scelto |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> | Una partita contro un motore | Ritira la tua ultima mossa; un suggerimento |
| <kbd>Invio</kbd> | Una partita contro un motore | Conferma *Abbandona…*; riprende una partita conservata alla chiusura di RookStudio; una volta finita, la esamina |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | La pagina Gioca, su Lichess | Accetta la sfida ricevuta, altrimenti cerca una partita; ritira la ricerca o la sfida |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Una partita su Lichess | Invia la mossa per corrispondenza in attesa, conferma *Abbandona…*; ritira la mossa in attesa o fatta in anticipo |
| <kbd>Invio</kbd> | La pagina Gioca, i tornei di Lichess | Partecipa all'arena o al torneo svizzero mostrato (la prima volta: va autorizzato su Lichess) |
| <kbd>Alt</kbd>+<kbd>P</kbd> | Ovunque, in un'arena su Lichess | Pausa nell'arena, con gli aiuti di nuovo disponibili; di nuovo: torna nell'arena |
| Una lettera, poi un numero | Le coordinate, *Dov'è…?* | Nomina una casa |
| <kbd>Maiusc</kbd> tenuto premuto, <kbd>F2</kbd> | Alla cieca | I pezzi mostrati per 2 secondi, tre volte a partita; *Dov'è…?* |
| <kbd>N</kbd>, <kbd>n</kbd>, <kbd>-</kbd> | Una linea letta a mente | Il pezzo come lo scrive il FEN (un cavallo bianco, uno nero), o nessuno |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd> | Una lista di partite | Scorre la lista, apre la partita |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La pagina Database | Il pannello di ricerca |
| <kbd>Esc</kbd> | La pagina Database | Chiude il pannello di ricerca, poi la lista |
| <kbd>Ctrl</kbd>+<kbd>Pag↓</kbd>, <kbd>Ctrl</kbd>+<kbd>Pag↑</kbd> | Il dossier di un giocatore | La scheda successiva, la precedente |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Il dossier di un giocatore | Sceglie tra gli omonimi, conserva la scelta; la annulla |
| <kbd>Esc</kbd> | Il dossier di un giocatore | Torna alla pagina da cui è stato aperto |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd> | I tornei di un database | Sceglie un torneo, apre la sua tabella incrociata |
| <kbd>Esc</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | La tabella incrociata di un torneo | Torna ai tornei; annulla la sua ultima modifica al database |
| <kbd>Ctrl</kbd>+<kbd>Pag↓</kbd>, <kbd>Ctrl</kbd>+<kbd>Pag↑</kbd> | Un rapporto sull'apertura | La sezione successiva, la precedente |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Invio</kbd> | Le mosse di un rapporto sull'apertura | Sceglie una mossa, la gioca nella pagina Scacchiera |
| <kbd>Esc</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Un rapporto sull'apertura | Torna alla posizione; toglie le mosse aggiunte al repertorio |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Il pannello Esporta | Salva il file (un PDF: lo apre per stamparlo); torna alla pagina |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> | Il pannello Esporta, un PDF | Lo apre nel lettore del sistema, per stamparlo |
| <kbd>Ctrl</kbd>+<kbd>Pag↓</kbd>, <kbd>Ctrl</kbd>+<kbd>Pag↑</kbd> | Il pannello Esporta | Il formato successivo, il precedente |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Inizio</kbd> <kbd>Fine</kbd>, <kbd>F</kbd> | Una pagina HTML fatta da RookStudio | Scorrono le mosse; ruota la scacchiera |
| <kbd>←</kbd> <kbd>→</kbd> | Lo schermo grande | La partita precedente o successiva |
| <kbd>+</kbd> <kbd>-</kbd> | Lo schermo grande | Testo più grande o più piccolo |
| <kbd>Esc</kbd> | Lo schermo grande | Esce |
| <kbd>Tab</kbd>, frecce, <kbd>Invio</kbd>, <kbd>Spazio</kbd> | Nuovo torneo | Spostano tra righe, elenchi e pulsanti; aprono una riga o un elenco |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | I partecipanti | Mandano i partecipanti selezionati in cima, in fondo |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | I partecipanti | Li spostano di un posto |
| <kbd>Canc</kbd> | I partecipanti | Li toglie |
| <kbd>Menu</kbd>, <kbd>Maiusc</kbd>+<kbd>F10</kbd> | I partecipanti | Apre il menu del partecipante (le sue impostazioni proprie) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La libreria della configurazione | Va al suo filtro |
| <kbd>Invio</kbd> | La libreria della configurazione | Aggiunge i motori selezionati, o il primo mostrato |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | La libreria della configurazione | Seleziona tutti i motori mostrati |
| <kbd>Ctrl</kbd>+clic, <kbd>Maiusc</kbd>+clic | Elenchi | Selezionano più elementi |
| <kbd>Canc</kbd> | La pagina Motori | Rimuove il motore selezionato (dopo una conferma) |
| <kbd>Invio</kbd> | La scheda di un motore, con il suo bot Lichess pronto | Mette online il bot |
| <kbd>Invio</kbd> | La scheda di un motore, con *Su lichess.org, per l'analisi* aperto | Lo offre a Lichess (la prima volta: va autorizzato su Lichess); *Riprendi* il servizio fermato |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Rendere un account un bot Lichess, con il suo nome digitato | Lo rende un bot; annulla |
| <kbd>Invio</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | La console di un motore | Invia la riga; richiamano le righe precedenti |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | La modalità headless | Ferma dopo le partite in corso |

## La pagina Home
<!-- en: the-home-page ecabd76e -->

La pagina **Home** di una vista raccoglie in schede le pagine della vista, a cominciare da ciò che è in corso:

- **L'arena** in cui sei da RookStudio, in cima: il suo nome, la tua posizione e i tuoi punti, il tempo rimasto, *Torna
  alla partita* e *Pausa* (Alt+P); un torneo svizzero a cui ti sei iscritto, il suo turno (vedi [Arene e tornei
  svizzeri](#arene-e-tornei-svizzeri)).
- **Riprendi da dove eri**, quando la vista contiene la Scacchiera: la partita sulla scacchiera alla sua mossa, su una
  piccola scacchiera, da dove viene (il suo database e il suo numero, il suo posto in un filtro, o il suo file), i suoi
  commenti, e se ha modifiche non ancora salvate. *Riprendi* (Invio) vi ritorna; Ctrl+Tab elenca le partite recenti.
- Cosa avviare, ciascuno con il suo tasto: *Apri un PGN…* (Ctrl+O), *Incolla una partita o un FEN* (Ctrl+V), *Nuova
  partita* (Ctrl+N), *Importa in un database…*, *Gioca contro un motore* (la pagina Gioca), *Nuovo torneo*: quelli
  delle pagine della vista.
- I tornei in corso, come nella pagina Tornei, quando la vista contiene Tornei.
- **Di recente**: le partite aperte sulla scacchiera; un clic ne riapre una con le sue modifiche.
- **I miei database**: ciascuno con le sue partite e lo stato del suo albero delle aperture, l'importazione in corso,
  *+ Importa…*, e i tornei letti senza copia (vedi [Database](#database)).
- **Lichess**: l'account, le partite di un giocatore con *Sincronizza*, le nuove partite contate se l'hai scelto, le
  trasmissioni in diretta (vedi [La pagina Lichess](#la-pagina-lichess)).
- **In corso**: una partita su Lichess giocata da RookStudio (*Torna alla partita*) e le sfide ricevute (vedi [Su
  Lichess](#su-lichess)), il tuo bot Lichess online e la sua partita (vedi [Un bot Lichess](#un-bot-lichess)), i tuoi
  motori al servizio della scacchiera di analisi di lichess.org (vedi [Il tuo motore su
  lichess.org](#il-tuo-motore-su-lichessorg)), l'analisi della partita, quella della posizione, un'importazione, l'assistente (il suo
  download, il suo modello caricato, *Scarica dalla memoria*), una serie di problemi, una sessione di esercizi, una
  partita da indovinare, un finale o un esercizio di visione in corso, e ciò che il ripasso del giorno ha da ripassare,
  in un unico conteggio, che un clic ripassa tutto (vedi [Il ripasso del giorno](#il-ripasso-del-giorno)).

Il pulsante di ricerca in alto, o Ctrl+K, trova un comando, una partita, un database.

## La pagina Tornei
<!-- en: the-tournaments-page f561d9e6 -->

La pagina **Tornei** è una sala di controllo: prima ciò che è in corso e ciò che ha bisogno di te, poi il resto.

- **Per iniziare**, finché non viene avviato un primo torneo: i passi dell'[Avvio rapido](#avvio-rapido) (aggiungere
  motori, configurare un torneo, avviarlo), ciascuno con il suo pulsante e spuntato una volta fatto, e *Leggi l'avvio
  rapido*, che lo apre nel pannello della guida. *Nascondi* toglie la scheda.
- **Metti tutto in pausa**, accanto ad *Apri una cartella…* e *Nuovo torneo*, mette in pausa i tornei in corso (non
  inizia nessuna nuova coppia di partite, le partite in corso arrivano alla fine) e trattiene la coda: nessun torneo in
  coda parte prima di **Riprendi tutto**, che li riprende e lascia proseguire la coda, subito se nel frattempo un
  torneo è finito.
- **Core**: i thread hardware della macchina, e quelli occupati dalle partite in corso, il resto libero. Una partita
  occupa i `Threads` del suo motore che ne ha di più, perché i due motori pensano a turno; la barra mostra il nome di
  ogni torneo e il suo conteggio (anche i motori della pagina Scacchiera, la partita del bot Lichess e le richieste dei
  tuoi motori su lichess.org), e al passaggio del mouse i dettagli.
- **Da sistemare**, quando c'è qualcosa, ognuno con la sua soluzione a portata di mano: un motore che non parte (vedi
  [Un motore che non parte](#un-motore-che-non-parte): *Riprova*, *Ritira…*, *Sostituisci…*, *Log del motore*); il
  torneo che un errore o un motore ha fermato, con ciò che l'ha fermato, *Apri il motore*, *Apri il log*, *Riprendi*,
  *Apri* e *Nascondi*; un disco troppo pieno per una notte di partite (meno di 1 GB libero, o meno del 2 % di un disco
  sotto i 500 GB), quello della cartella dei tornei o di un torneo in corso, controllato ogni minuto, con *Apri la
  cartella* e *Cambia la cartella dei tornei…* (dove vanno i nuovi tornei, come in **Impostazioni**).
- **In corso**: una scheda per ogni torneo in corso: nome, formato, cadenza e stato, una barra di avanzamento con le
  partite giocate su quelle previste e le partite in corso, quando finisce, e chi è in testa: il primo della classifica
  con il suo rating contro il resto del campo e i suoi punti (un'eliminazione diretta: i giocatori rimasti; un incontro
  a squadre: il punteggio delle squadre e il suo Elo), il motore in vantaggio in un match con il suo Elo e il
  punteggio, o l'LLR di un SPRT con i suoi limiti, poi l'Elo e il punteggio. *Apri* va al suo spazio di lavoro,
  *Pausa* lo mette in pausa. Un torneo che si può riprendere compare invece sotto *Interrotto*, con *Riprendi*. Vedi
  [Più tornei insieme](#più-tornei-insieme) per i pesi e i core di ciascuno.
- **Il tuo bot Lichess**, mentre è online: il suo motore, la sua partita in corso con *Sulla Scacchiera*, *La sua
  scheda* e *Metti offline*; rifiuta le sfide con *più tardi* finché i tornei hanno bisogno dei core (vedi [Un bot
  Lichess](#un-bot-lichess)).
- **I tuoi motori su lichess.org**, mentre servono: ciascuno con il suo stato, *La sua scheda* e *Ferma il servizio*;
  prendono solo i thread che i tornei lasciano liberi (vedi [Il tuo motore su lichess.org](#il-tuo-motore-su-lichessorg)).
- **Coda**: i tornei in attesa, ciascuno con la sua durata e quando parte, e quando finisce tutto (vedi [Coda e
  rilancio](#coda-e-rilancio)).
- **Ultimi terminati**: gli ultimi cinque tornei terminati, con il loro risultato: il punteggio di un match, la
  decisione dell'SPRT, il vincitore di un'eliminazione diretta (della sua finale), il punteggio di un incontro a
  squadre, o il primo della classifica con i punti delle sue partite. *Rilancia con…* per un match, *Clona* (vedi
  [Coda e rilancio](#coda-e-rilancio)) e *Apri*.
- **Affidabilità**: i crash o blocchi, le sconfitte per tempo e le mosse illegali delle ultime 24 ore, la memoria
  libera e installata, e i processi dei motori in esecuzione; poi i motori che hanno perso quelle partite, i tre con il
  numero più alto, con *Apri il motore* e *Apri la cartella* (la cartella `logs` del torneo conserva il log del motore
  di ogni partita finita male). Una sconfitta per tempo prima della mossa 20 viene distinta: più spesso è un motore
  configurato male (troppi thread, un hash troppo grande per essere allocato in tempo, una rete caricata a ogni
  partita, un avvio lento) che un problema di gestione del tempo.
- **Tutti i tornei**: tutti i tornei del profilo, dal più recente, con *Apri*, *Clona* e *Cartella*; il titolo li
  ripiega. Il risultato di un torneo terminato compare al passaggio del mouse, sopra il suo stato.

Anche la tavolozza dei comandi ha queste azioni: *Metti tutto in pausa*, *Riprendi tutto*, *Rilancia … con…* per gli
ultimi match terminati, *Clona …* per il torneo mostrato e gli ultimi terminati, *Apri la cartella dei tornei* e
*Cambia la cartella dei tornei…*.

## Motori
<!-- en: engines 4f15155a -->

La pagina **Motori** è la libreria dei motori: l'elenco a sinistra, la scheda del motore scelto a destra.

### Aggiungere e verificare i motori
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Aggiungi…** registra un eseguibile UCI. Il motore viene avviato una volta per leggerne il nome, l'autore e le
opzioni. Le opzioni si modificano in una tabella: i valori modificati sono evidenziati, e solo i valori diversi dal
predefinito del motore vengono inviati con `setoption` all'inizio di ogni partita. `Threads` e `Hash` vengono per
primi.

- **Duplica** crea una seconda voce per lo stesso binario, con altre opzioni (un `Hash` diverso, un livello di forza,
  un altro file di rete…). Le voci prendono il nome dall'`id name` UCI quando vengono aggiunte e possono essere
  rinominate.
- **Verifica** avvia il motore, misura quanto rapidamente risponde a `uci` e `isready`, e gli fa giocare una mossa;
  poi esegue i passi di conformità UCI di fastchess (`id name` e `id author`, posizioni da un FEN e con mosse, `go`
  con ogni orologio, righe info con un punteggio intero…), fino al primo che fallisce. Il risultato (✓ o l'errore)
  resta accanto al motore con la sua data, e *Rapporto di conformità UCI* elenca i passi, con ciò che il motore ha
  scritto dopo quello fallito. Un motore a cui si parla in xboard riceve solo la prima parte.
- **Console…** parla con il motore, fuori da qualsiasi torneo: viene avviato con il programma, gli argomenti e la
  cartella di lavoro della libreria, e un motore UCI riceve `uci` e poi i valori delle opzioni della libreria. Le righe
  digitate vengono inviate così come sono (Invio; Su e Giù richiamano quelle precedenti), e il log mostra ogni riga
  con la sua ora, quelle inviate marcate `>` e l'output degli errori `!`. Dei pulsanti inviano i comandi abituali
  (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; quelli di WinBoard per un motore WinBoard,
  a cui si parla nel suo protocollo a meno che non sia spuntato *Tramite l'adattatore*) e la posizione della pagina
  Scacchiera. *Copia* e *Salva…* conservano il log. La console compare al posto delle impostazioni del motore
  (*Impostazioni* vi torna, lasciando la console aperta); *Chiudi* termina il motore. Mentre un torneo è in corso, ciò
  che il motore cerca toglie core alle sue partite: la console lo dice. Il suo motore non è uno dei processi del
  torneo: la fine di un torneo non lo termina.
- **Test rapido** gioca un breve match contro il motore stesso o un altro: 10 partite a 10+0.1 da 5 posizioni (una con
  una presa en passant), con lo stesso codice di un torneo, mostrato nel suo spazio di lavoro mentre gioca. Il
  risultato resta accanto al motore: il suo punteggio, le sue sconfitte per tempo, i crash e le mosse illegali, e la
  quota del suo orologio che ha usato, che rivela un motore che gioca troppo in fretta o troppo vicino alla caduta
  della bandierina. Le sue PV di matto vengono verificate (vedi *Verifica le PV di matto* in [PGN e
  varie](#pgn-e-varie)). Viene conservato solo l'ultimo test rapido, in `tests/` nel profilo (con i log del motore
  delle sue partite finite male).

### Una libreria grande
<!-- en: a-large-library d118bc02 -->

- **Gruppi**: etichette libere separate da virgole (`pool A`, `list 40/15`…). L'elenco si può filtrare per gruppo o
  cercare, e un gruppo si può aggiungere a un torneo con un clic. *Note*: qualsiasi cosa da ricordare sul motore.
  *Logo*: un'immagine mostrata accanto al suo nome in *Diretta* e sullo schermo grande quando i loghi sono attivi, e
  sulla pagina web della trasmissione (vedi [Aspetto](#aspetto)).
- **Ricerca e filtri**: ogni ricerca di motori (la libreria, e gli elenchi di motori delle altre pagine) trova un
  nome, il nome che il motore dichiara, il suo autore, il percorso del suo programma o le sue note; il filtro di stato
  mostra i motori che funzionano, che hanno fallito o sono cambiati, mai verificati, che hanno perso il loro
  programma, che parlano WinBoard, o inattivi. Ctrl o Maiusc clic seleziona più motori: *Verifica* (uno dopo l'altro,
  mai mentre un torneo gioca), *Aggiungi a un gruppo…*, *Rimuovi…*. *Altro…* verifica quelli mai verificati
  (*Verifica quelli non verificati*) e rimuove i programmi mancanti. Un'opzione stringa che contiene un file o una
  cartella (`EvalFile`, `SyzygyPath`…) ha un pulsante *…*.
- **Inattivo** (sulla scheda, o dal clic destro per tutti i motori selezionati): il motore resta nella libreria, in
  grigio, ma è escluso dai selettori di motori (configurazione, suite, test rapido, rilancio, aggiunta di un motore a
  un torneo in corso) e da *Verifica quelli non verificati*. Un torneo o una configurazione che lo ha già lo tiene.
- **Usa una GPU** (sulla scheda): un motore a rete che gioca su una scheda grafica, come Lc0. Non giocano insieme più
  motori di questo tipo delle *GPU* di **Impostazioni**, in un torneo e tra tornei (vedi [Più tornei
  insieme](#più-tornei-insieme)); dal prossimo torneo a cui partecipa.
- **Elo di riferimento**: il rating del motore in una lista di cui ti fidi. Un torneo o la lista Elo possono essere
  ancorati su di esso, e le partite di un torneo lo riportano nei loro tag `WhiteElo` e `BlackElo` (copiato quando il
  motore entra nel torneo). *Importa…* > *Una lista Elo, per l'Elo di riferimento…* lo imposta per molti motori in una
  volta (vedi [Importare motori e rating](#importare-motori-e-rating)).

### Motori popolari
<!-- en: popular-engines bda147a5 -->

*Motori popolari…*, accanto ad *Aggiungi…*, elenca i motori più forti i cui autori pubblicano build per Linux, Windows e
macOS su GitHub (Gaia, Stockfish, Triumviratus, Reckless, PlentyChess, pawnocchio, Coda, Cinder, Stormphrax,
Viridithas), ciascuno alla sua ultima versione, con il file adatto a questo processore (la più veloce delle sue build
che il processore supporta: AVX2, BMI2, AVX-512, una build fatta per la sua generazione Zen…), la sua dimensione e la
sua licenza. *Aggiungi* lo scarica dalla versione pubblicata dal suo autore, lo verifica con l'hash SHA-256 che GitHub
pubblica, lo estrae dal suo archivio, lo avvia una volta (`uci`) e lo aggiunge alla libreria, ogni versione in una
cartella tutta sua nella cartella `engines` del profilo; un file che non parte viene sostituito dalla build successiva.
*Annulla* ferma un download, e il tentativo successivo riprende da dove si era fermato. L'elenco viene da un catalogo
tenuto nel repository di RookStudio, riletto una volta al giorno, altrimenti dalla copia propria del programma. Cosa
esce dalla macchina: il catalogo, una richiesta ad api.github.com per motore (conservata un giorno), e il file scelto;
solo mentre la pagina è aperta o si aggiunge un motore.

Un motore aggiunto così segue le sue nuove versioni: vengono cercate una volta al giorno quando si apre la pagina Motori
(o solo quando si apre *Motori popolari…*, in *Impostazioni*), e mostrate sopra l'elenco con *Aggiorna*. La nuova
versione va in una cartella tutta sua e il motore la adotta quando ha risposto a `uci`; *Tieni anche la …* conserva la
versione precedente come motore a sé, per un match tra le due. Un torneo non ancora finito mantiene i file con cui è
iniziato; la versione precedente viene eliminata quando i tornei che la giocano sono finiti (vedi [Le versioni di un
motore](#le-versioni-di-un-motore)).

Dove serve per la prima volta un motore (l'analisi della pagina Scacchiera, le sue domande e il suo resoconto) e la
libreria non ne ha, un riquadro propone Gaia, scaricato con un clic (o Invio) e reso il motore dell'analisi e
dell'assistente; anche gli altri motori popolari e un motore dal disco sono lì a un clic.

### Le versioni di un motore
<!-- en: an-engines-versions e62023d7 -->

Ogni versione di un motore preso da *Motori popolari…* resta in una cartella tutta sua nella cartella `engines` del
profilo. Quando un aggiornamento ha sostituito una versione nella libreria, la sua cartella viene eliminata quando
nessun torneo non ancora finito (in gioco, in pausa o fermato) la gioca: subito se nessuno la gioca, altrimenti alla
fine dell'ultimo, perché RookStudio ricontrolla alla fine di ogni torneo. Lo dice un avviso nell'angolo della finestra:
*Gaia 4.4.3 eliminato: «Gaia di notte» è finito · 118 MB liberati*, con *La sua scheda*.

Quando il motore ha sul disco più versioni di quella della sua libreria, la sua scheda le elenca sotto *Le sue versioni
sul disco*, ciascuna con il suo stato:

- quella della libreria: quella che usano l'analisi, i nuovi tornei, il bot e il servizio per Lichess;
- una che gioca un torneo non ancora finito, con quel torneo e il punto in cui si trova (*in gioco, partita 212 di
  400*), eliminata alla sua fine quando un aggiornamento l'ha sostituita; *Tieni anche la …* la conserva per sempre,
  come motore a sé;
- una tenuta di proposito (*Tieni anche la …*, qui o nell'aggiornamento), mai eliminata; *Non tenerla più* (un secondo
  clic) toglie il suo motore dalla libreria, e la sua cartella se ne va quando nessun torneo non ancora finito la gioca;
- una rimasta da prima, mai sostituita da un aggiornamento da allora: conservata fino a *Eliminala* (un secondo clic);
- una eliminata che un torneo non ancora finito richiede di nuovo (ripreso da una cartella in cui RookStudio non ha
  guardato): *Scaricala di nuovo* la prende dalla sua versione pubblicata su GitHub, verificata con l'hash che aveva,
  nello stesso posto.

### Avvio dei motori
<!-- en: starting-engines 09d107ba -->

- **La lettura delle opzioni** non blocca mai un avvio. *Riprendi* parte subito: un torneo fa giocare i motori copiati
  nella sua cartella. *Avvia*, la coda e il test rapido attendono solo la lettura di un motore che fanno giocare, letto
  per primo; qualsiasi altra lettura o verifica viene interrotta e rifatta più tardi. Un avvio che attende lo dice sul
  suo pulsante (*In attesa di …*, con *Annulla*), e nel frattempo un altro avvio viene rifiutato. Un avvio attende
  anche una suite di test in corso, e una suite di test non parte mentre è in corso il test rapido o un avvio attende.
  Letture, verifiche e suite di test girano accanto ai tornei, che giocano in processi propri. Un motore in coda può
  essere letto subito dopo dal suo menu.
- *Riavvia il motore a ogni partita* avvia un nuovo processo per ogni partita invece di riutilizzarlo. Altrimenti un
  torneo tiene un processo in attesa per ogni motore che ha ancora partite da giocare, fino a 32 (più due per partita
  in corso), e chiude un motore dopo la sua ultima partita: in un'eliminazione diretta appena perde il suo match, in un
  sistema svizzero quando non ha più partite nell'ultimo turno.
- *Timeout di avvio (s)* (60 per impostazione predefinita) è il tempo concesso a un motore per rispondere `uciok`
  quando parte e `isready` all'inizio di ogni partita; un motore lento ad avviarsi (una grande rete da caricare) ne ha
  bisogno di più. Un motore che non riesce ad avviarsi viene riprovato tre volte, a 10 s di distanza, con lo stesso
  timeout; una pausa trattiene i tentativi, senza consumarli. Poi le sue partite vengono messe da parte e le altre
  proseguono (vedi [Un motore che non parte](#un-motore-che-non-parte)). Un torneo creato dopo copia il timeout nei
  suoi partecipanti.

### Programmi ricompilati
<!-- en: rebuilt-programs 549daa6e -->

- **Programmi spostati o ricompilati**: la libreria ricorda ogni programma (dimensione, data, CRC32). Un programma che
  non c'è più viene marcato **!** con *Individua…*, e un torneo non parte con esso. Un programma che è cambiato (una
  nuova build, o un altro file con lo stesso percorso) ha le sue opzioni rilette automaticamente.
- **Cosa ha cambiato una nuova build**: quando la nuova lettura differisce dalla precedente, il motore viene marcato
  **Δ** e la sua scheda elenca le differenze fino a *Chiudi*: il suo nuovo `id name` (con *Rinominalo* quando il
  motore aveva preso il nome da quello vecchio) e autore, ogni opzione il cui predefinito, i cui limiti o le cui
  scelte sono cambiati (prima e ora), le opzioni nuove e sparite, e il tuo valore di ciascuna (il valore di
  un'opzione sparita viene scartato; uno che la nuova build non accetta più è in arancione). Per un'opzione il cui
  predefinito è cambiato, o il cui valore non va più bene, scegli *Tieni il mio valore* (o *Tieni il vecchio
  predefinito* dove non ne avevi uno: gioca come prima) o *Usa il nuovo predefinito*; *Tieni i miei valori* e *Usa i
  nuovi predefiniti* scelgono per tutte. L'avviso se ne va una volta fatta ogni scelta, a meno che non abbia altro da
  dire. Più build prima che sia chiuso danno un solo avviso, rispetto alla lettura precedente alla prima. Anche
  *Fallito o modificato* nel filtro di stato mostra questi motori, e la tavolozza dei comandi ha *Cosa è cambiato*,
  *Tieni i miei valori*, *Usa i nuovi predefiniti* e *Chiudi l'avviso delle modifiche* per ciascuno.
- **I programmi dei tornei**: ogni torneo registra i programmi con cui è stato avviato. Il riepilogo della
  configurazione avvisa di un motore il cui programma è cambiato dal suo ultimo torneo (il più recente della cartella
  dei tornei che ha fatto giocare questo programma), con *Cosa è cambiato* quando la sua scheda ha un avviso.
  Riprendere un torneo con un programma ricompilato o sparito nel frattempo lo segnala nella sua intestazione, con
  *Continua* (l'avviso si chiude) e *Pausa*; il torneo non si ferma da solo.

### Importare motori e rating
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importa…** > *Una lista di motori…* legge la lista di motori di un altro programma: ChessGUI (`ChsGUI_Eng.TXT`, con
le opzioni di `ChsGUI_Cfg.ini` accanto), cutechess e Banksia (`engines.json`), Arena (`ArenaENG.cfg`, nella cartella
di Arena: il programma di ogni motore, gli argomenti, il protocollo e, per i motori UCI, i valori delle opzioni
impostati in Arena) e altre liste in stile INI. Una lista che non dà nessun motore lo dice, con i formati letti e
*Tutti i motori di una cartella…* da provare al suo posto.

*Tutti i motori di una cartella…* prende tutti i programmi trovati in una cartella, e ricorda la cartella: *Aggiorna:
i nuovi motori della cartella* propone poi solo i motori aggiunti da allora. I motori trovati vengono prima elencati,
per togliere la spunta a quelli da escludere; i motori già nella libreria (stesso programma e stessi argomenti)
vengono saltati; le opzioni degli altri vengono lette; vengono importati anche i motori WinBoard.

*Importa…* > *Una lista Elo, per l'Elo di riferimento…* legge una lista Elo, incollata o aperta: la versione di
testo di una lista Elo o la sua pagina web (salvata o copiata), l'output di Ordo (testo o CSV), o un CSV di righe
`name,elo`. Un nome della lista è il motore della libreria con lo stesso nome, o l'unico il cui nome è lo stesso a
parte maiuscole e spazi (come per i giocatori dei file PGN importati, vedi [Classifiche](#classifiche)). L'anteprima
mostra l'Elo di riferimento di ogni motore, vecchio e nuovo, spuntato per essere impostato, e i nomi della lista che
non sono un motore della libreria. I nomi di una lista spesso riportano la loro build (`64-bit 4CPU`): se serve,
rinomina i motori perché corrispondano.

### Motori WinBoard
<!-- en: winboard-engines ff0920db -->

I **motori WinBoard** (protocollo xboard, CECP) giocano come gli altri. Il *Protocollo* di un motore viene
determinato quando viene aggiunto (*Automatico*), o scelto: UCI o WinBoard. fastchess parla solo UCI: un motore
WinBoard viene avviato tramite una traduzione integrata in RookStudio (`rookstudio --wb2uci ENGINE [ARGS]`, che
fastchess avvia al posto del motore), e le sue opzioni, `memory`, `cores` e il percorso Syzygy diventano opzioni UCI.
Guida il motore come fa WinBoard: feature del protocollo 2, o protocollo 1 per i motori che non ne inviano; posizioni
con `setboard`, o modalità edit; mosse in coordinate o SAN; `level`, `time`/`otim`, `st`, `sd`, `nps`; output del
pensiero, abbandono. I motori marcati ↔ nell'elenco sono motori WinBoard.

- Un motore del protocollo 1 senza `setboard` né modalità edit (TSCP, per esempio) può partire solo dalla posizione
  iniziale: dagli le aperture come mosse (un libro PGN), non come posizioni (EPD).
- Chess960 per un motore con la variante `fischerandom`: `variant fischerandom`, posizioni in X-FEN, arrocco come
  `O-O`/`O-O-O`, come le invia WinBoard.
- *Scores from White's side*, tra le opzioni del motore: per i vecchi motori che danno i loro punteggi dal punto di
  vista del Bianco anziché da quello del lato che muove, come chiede il protocollo; i loro punteggi con il Nero
  vengono invertiti.
- Un motore che chiede di essere riavviato per ogni partita (`feature reuse=0`) riceve *Riavvia il motore a ogni
  partita* spuntato quando viene aggiunto.
- `FCG_WB2UCI_LOG=FILE` nell'ambiente scrive in `FILE` ogni riga scambiata con i motori WinBoard.

### Un bot Lichess
<!-- en: a-lichess-bot 5ae494c4 -->

La scheda di un motore ha una riga **Su Lichess, come bot**: *Rendilo un bot Lichess…* prepara il motore a giocare su
Lichess con un account bot, tramite la Bot API di Lichess (dove i motori sono ammessi), accettando le sfide che scegli
mentre RookStudio è aperto. La riga diventa una parte della scheda, che guida attraverso i passi qui sotto.

- **L'account del bot** è un secondo account Lichess, separato dal tuo: nuovo, con un nome proprio, e senza nessuna
  partita giocata. RookStudio non crea account: *Crea l'account su lichess.org* apre la pagina di Lichess. *Collega
  l'account del bot…* apre la pagina di Lichess che chiede un solo permesso, *giocare come bot* (`bot:play`): accedi lì
  con l'account del bot, non con il tuo (una finestra privata del browser evita di disconnetterti). Oppure incolla un
  token dell'account del bot creato su lichess.org (*Crea un token su lichess.org* apre la pagina con quel permesso
  spuntato) e *Verificalo*. Il tuo account resta collegato a parte, per il resto di RookStudio; il bot ha il suo token,
  conservato nel file `lichess-bot.json` del profilo, leggibile solo da te.
- **Le verifiche**, prima di chiedere qualsiasi cosa che modifichi l'account: il suo nome e quando è stato creato, le
  partite che ha giocato (nessuna), se è già un bot (allora serve così com'è), e che non sia il tuo account collegato.
  Il tuo account non viene mai conservato (il token che RookStudio ha ottenuto per esso viene revocato subito); né un
  account chiuso, né un token senza il permesso. Un account che ha giocato viene rifiutato, come farebbe Lichess:
  creane uno nuovo, non giocarci nulla, poi collegalo (*Collega un altro account…*, *Scollega questo*).
- **Rendilo un bot…** è irreversibile: l'account non gioca mai più come persona, Lichess lo segna BOT, e gioca solo
  sfide (niente lobby, niente tornei tranne le arene aperte ai bot). Il passo lo dice e chiede il nome dell'account:
  *Rendilo un bot* si accende quando il nome digitato è il suo (Esc annulla). Cosa esce dalla macchina: quella
  richiesta, con il token del bot. Un rifiuto viene indicato con le parole di Lichess; senza risposta, non è cambiato
  niente: riprova.
- **Le sfide che accetta**, sulla scheda: le cadenze (bullet, blitz, rapida, classica, corrispondenza; mai UltraBullet,
  che Lichess non lascia giocare ai bot), le partite classificate o solo amichevoli, persone e bot, una fascia di
  rating per lo sfidante (0: nessun limite), Chess960 quando il motore lo conosce (`UCI_Chess960`), una partita alla
  volta. All'inizio: solo amichevoli, blitz e rapida, perché Lichess chiede ai bot di giocare amichevoli mentre vengono
  provati; allargale dopo. Una sfida classificata dal tuo account viene rifiutata (Lichess vieta le partite classificate
  tra account della stessa persona); una amichevole viene giocata. Le altre sfide vengono rifiutate con cortesia, con il
  motivo che Lichess mostra allo sfidante: *troppo veloce*, *troppo lenta*, *non con questa cadenza*, *solo
  amichevoli*, *solo scacchi standard*, *non questa variante*, *niente bot*, *solo bot*, o *più tardi* mentre gioca,
  va offline, o un torneo ha bisogno dei core.
- **Il suo gioco**: il motore con le sue opzioni della libreria (i suoi `Threads` e `Hash` contati nel budget dei core
  della macchina mentre gioca), un libro Polyglot della tua libreria giocato finché ha una mossa, le tablebase dei
  tornei (`SyzygyPath`, per un motore che ha l'opzione e nessun percorso proprio), e niente riflessione sul tempo
  dell'avversario. Il suo orologio arriva da Lichess a ogni mossa, meno un margine per la rete (un decimo del tempo che
  gli resta, da 0,2 a 1 secondo); prima che entrambi i lati abbiano mosso, gioca entro il tempo che Lichess concede per
  la prima mossa; per corrispondenza, pensa 15 secondi per mossa. Accetta una patta proposta quando il motore sta
  chiaramente peggio, o non meglio a partita inoltrata, rifiuta il ritiro di una mossa, e rivendica la vittoria quando
  il suo avversario se n'è andato da abbastanza tempo. Le sue parole, dette nella chat dei giocatori all'inizio e alla
  fine, le scegli tu (vuote: non dice niente). Un motore che non parte o che si ferma abbandona la partita (la annulla
  prima che entrambi i lati abbiano mosso), e la scheda dice perché.
- **Metti online** (Invio) apre il flusso di eventi proprio del bot (Lichess ne tiene uno per token): le sfide arrivano,
  e ricevono subito risposta secondo le sue regole. Online, la scheda mostra la giornata del bot (partite vinte, patte e
  perse, sfide rifiutate), la sua partita su una piccola scacchiera con la valutazione del motore, *Sulla Scacchiera*,
  le sue ultime partite e le sfide rifiutate, *La sua pagina su lichess.org* e **Metti offline**: nessuna nuova sfida,
  la partita in corso prosegue fino alla fine, poi il flusso si chiude. Una partita per corrispondenza aspetta su
  Lichess mentre il bot è offline; torna quando il bot è di nuovo online. Il bot gioca solo mentre RookStudio è aperto:
  chiudere RookStudio durante una delle sue partite in diretta chiede prima (*Resta* · Esc, *Mettilo offline*, *Chiudi
  comunque*), perché la partita sarebbe persa per tempo.
- **La sua partita sulla Scacchiera**: seguita mossa per mossa, con il lato del bot in basso, i rating e gli orologi dei
  giocatori nelle loro strisce, e sopra la colonna la sua scheda: cos'è la partita, a chi tocca, la ricerca del motore
  (la sua valutazione, la profondità e la linea che si aspetta: la sua, poiché gioca lui la partita), *Apri su
  lichess.org*, *Abbandona al suo posto…* (un secondo clic) e *Metti offline*. Le mosse sulla scacchiera sono solo del
  bot. Alla fine, la partita entra nel database *Lichess · il suo nome*, creato se manca.
- **Dove compare**: online, il bot compare dove compare ciò che è in corso: nella pagina Home sotto *In corso*, e nella
  pagina Tornei, con la sua partita, *Sulla Scacchiera*, *La sua scheda* e *Metti offline*.
- **Prima il torneo**: una sfida viene rifiutata con *più tardi* finché un torneo che si gioca su questo computer ha
  bisogno dei core che il motore del bot occuperebbe. Una partita del bot già in corso quando inizia un torneo prosegue
  fino alla fine, con i thread del suo motore contati nel budget dei core come quelli dei motori della pagina
  Scacchiera; nel frattempo non viene accettata nessuna nuova sfida.
- **Fair play**: le partite del bot non sono tue, e non bloccano i tuoi aiuti. Durante una tua partita in diretta su
  Lichess, la ricerca del bot non viene mostrata né sulla sua scheda né sulla Scacchiera (la scheda dice perché); le
  sue partite continuano.
- **Scollega il bot…** (un secondo clic) revoca il suo token su Lichess e lo elimina qui; l'account resta un bot.
- Cosa esce dalla macchina: le mosse del motore, le sue parole nella chat, le sue risposte alle sfide, con il token del
  bot; niente del tuo account. Il suo profilo (nome, descrizione) si imposta su lichess.org. Le sue scelte e le sue
  ultime partite sono conservate nel file `bot/bot.json` del profilo.

La tavolozza dei comandi ha *Metti online il bot Lichess*, *Metti offline il bot Lichess*, *La partita del bot Lichess
sulla Scacchiera* e *La scheda del bot Lichess*, e nella pagina Motori *Rendi … un bot Lichess…*.

### Il tuo motore su lichess.org
<!-- en: your-engine-on-lichessorg 6bee0124 -->

La scheda di un motore ha una riga **Su lichess.org, per l'analisi**: *Offri a Lichess…* offre il motore alla scacchiera
di analisi di lichess.org, tramite l'API dei motori esterni di Lichess. Calcola qui, sulla tua macchina, e le sue linee
compaiono lì, solo per il tuo account, nel menu dei motori della scacchiera di analisi, mentre RookStudio è aperto.
L'API di Lichess per questo è ancora in alfa.

- **Come viene offerto**: il suo nome su Lichess (da 3 a 200 caratteri; *Gaia 4.4.4 (RookStudio)* per impostazione
  predefinita), la macchina (questo computer, o una macchina remota di Impostazioni > Macchine in cui il motore ha il
  suo percorso, via SSH, con i propri thread), i thread al massimo (Lichess ne chiede da 1 a quel numero per ogni
  richiesta) e la memoria al massimo. Lichess usa un motore del genere in modalità Chess960 (`UCI_Chess960`) per ogni
  partita: un motore che non la ha non può essere offerto, e la sua riga lo dice al posto di *Offri a Lichess…*.
- **La prima volta**, Lichess chiede due permessi in più, `engine:read` ed `engine:write` (per registrare un motore per
  il tuo account, leggerlo, ritirarlo): *Autorizza su Lichess…* (Invio) apre la pagina di Lichess; il tuo account resta
  collegato, e il token che sostituisce viene revocato. Senza un account collegato, la scheda lo dice, con *Collega
  l'account…*.
- **Poi, senza chiedere nient'altro**, RookStudio registra il motore con il tuo account (una richiesta, con il tuo
  token) e un segreto generato qui, che Lichess non mostra mai più; RookStudio lo conserva nel file
  `lichess-engines.json` del profilo, leggibile solo da te, e non lo mostra né lo scrive mai nei log. Poi aspetta
  lavoro su engine.lichess.ovh con quel solo segreto, senza il tuo token: un'attesa lunga, rinnovata finché il servizio
  è attivo. Quando la tua scacchiera di analisi chiede, arriva una richiesta con la posizione (una posizione di
  partenza e delle mosse), i thread, la memoria, da 1 a 5 linee e un tempo, una profondità o dei nodi: il motore viene
  impostato (la modalità Chess960, `UCI_AnalyseMode` quando ce l'ha, le sue opzioni della libreria), e le sue linee
  (profondità, multipv, punteggio, nodi, tempo, pv) vanno a Lichess man mano che le trova, poi la sua mossa migliore.
  Quando passi oltre, Lichess chiude la richiesta, e il motore si ferma.
- **In servizio**, la scheda mostra la richiesta a cui sta rispondendo (le sue linee, i thread, la memoria, la
  profondità e la velocità), le richieste e i minuti di analisi di oggi, la posizione richiesta con la mossa migliore
  del motore, *Apri la scacchiera di analisi di Lichess* e **Ferma il servizio**. La pagina Lichess elenca i motori
  offerti (*I tuoi motori su lichess.org*); la pagina Home (sotto *In corso*) e la pagina Tornei mostrano quelli in
  servizio, e la barra dei core i loro thread.
- **Prima il torneo**: il servizio prende solo i thread che i tornei in corso sulla sua macchina lasciano liberi (i
  core delle Impostazioni), dando la precedenza anche alla partita del bot Lichess (vedi [Un bot
  Lichess](#un-bot-lichess)); se non ne resta nessuno, si mette in pausa e lo dice, con le richieste di Lichess in
  attesa. Una richiesta in corso quando un torneo ha bisogno dei suoi thread viene fermata lì.
- **Fair play**: durante una tua partita in diretta su Lichess, il servizio si mette in pausa fino alla fine della
  partita (le richieste restano senza risposta), poi riprende da solo. Le posizioni delle tue partite per
  corrispondenza non vengono mai analizzate: una richiesta su una di esse, su una linea che vi passa o una mossa prima
  (confrontata per i pezzi e il lato al tratto, come fa il blocco del [Fair play](#fair-play)) riceve una risposta
  vuota. Lichess stesso offre un motore esterno solo sulle sue pagine di analisi, mai durante una partita.
- **Ferma il servizio**: RookStudio non aspetta più lavoro; il motore resta registrato su Lichess, senza rispondere;
  *Riprendi* (Invio) lo rimette in servizio. *Cambia…* lo registra di nuovo con un altro nome, un'altra macchina o
  altri limiti. **Ritira da Lichess** elimina la registrazione (una richiesta), e il suo segreto non viene più usato;
  *Offri a Lichess…* la rifà. Chiuso RookStudio, il motore resta nell'elenco di Lichess senza rispondere; al prossimo
  avvio, il servizio riprende se era in servizio quando hai chiuso.
- **Quando qualcosa va storto**: se Lichess rifiuta il segreto (401: la registrazione è stata ritirata su lichess.org,
  o il segreto è cambiato) il servizio si ferma, con *Registra di nuovo (un nuovo segreto)*; una risposta di Lichess
  che RookStudio non sa leggere (l'API è in alfa) lo ferma piuttosto che rispondere in modo sbagliato, e lo dice, con
  *Vedi se una versione di RookStudio si adegua*; offline, il servizio aspetta la rete e riprende da solo (dopo troppe
  richieste, il minuto di Lichess).
- Cosa esce dalla macchina: alla registrazione, il nome, i limiti e il segreto, con il tuo token; poi le linee del
  motore per le posizioni che Lichess invia. Quelle posizioni vengono dal tuo browser, tramite Lichess, che vede le
  linee. Nient'altro della tua macchina.

La tavolozza dei comandi ha *Offri … a lichess.org…* nella pagina Motori, *Ferma il servizio di … su lichess.org* e
*Riprendi il servizio di … su lichess.org*.

## Libri
<!-- en: books 4cf13005 -->

**Aggiungi…** registra un libro di aperture (EPD, PGN o Polyglot `.bin`) con un nome e una descrizione; per un libro
PGN il numero di semimosse tenute, per un libro Polyglot la profondità fino a cui vengono seguite le sue linee. La
sezione *Aperture* della configurazione propone poi i libri della libreria per nome, con il loro numero di aperture
(si può comunque scegliere qualsiasi altro file). *Rimuovi* toglie un libro dalla libreria e lascia stare il file.

### Verificare un libro
<!-- en: checking-a-book 96eee229 -->

Il libro scelto viene letto in background, con i lettori propri di fastchess (così le sue aperture sono esattamente
quelle che gioca un torneo), e verificato:

- **posizioni illegali**: non un re per colore, pedoni sulla prima o sull'ultima traversa, il lato che non muove sotto
  scacco;
- **duplicati**: la stessa posizione di un'apertura precedente, anche per trasposizione;
- **colori invertiti**: un'apertura precedente con i colori scambiati (un libro pensato per essere giocato due volte,
  a colori invertiti, non ha bisogno di entrambe);
- **partite già finite**: nessuna mossa legale, o materiale insufficiente per dare matto.

fastchess esclude da un torneo le aperture la cui posizione non si può giocare (una riga FEN o EPD senza re o comunque
malformata, un re che potrebbe essere catturato, una partita PGN il cui tag FEN non è valido), con un avviso per
libro; qui vengono contate come posizioni illegali. Una partita PGN le cui mosse smettono di essere lette (una mossa
malformata o ambigua) tiene le mosse precedenti. Le righe di un libro PGN che iniziano con `%` (righe di escape, che
lo standard PGN rende commenti; i libri AVT ne hanno alcune) vengono ignorate, anche dai tornei, e la pagina Libri le
conta (*righe che iniziano con %, ignorate*).

Le sue aperture sono elencate sotto, tutte o solo quelle con un problema, ricercabili per parte di un FEN o per mosse
(un libro PGN mostra le sue mosse in SAN); un clic mostra la posizione su una scacchiera. *Usalo per il prossimo
torneo* lo mette nella configurazione (con Chess960 per un libro Chess960). *Salva una copia ripulita…* scrive un
libro EPD senza le posizioni illegali, i duplicati e le partite finite (e, se spuntato, le posizioni a colori
invertiti), e lo aggiunge alla libreria. *Esegui come suite di test…* apre un libro EPD in **Suite**.

### Creare libri
<!-- en: creating-books b9c072ab -->

**Crea…** scrive libri per la libreria (anche nella tavolozza dei comandi):

- *Genera un libro Chess324*: il Chess324 di Larry Kaufman. I re e le torri restano sulle loro case abituali, quindi
  l'arrocco è quello abituale; la donna, gli alfieri e i cavalli di ogni parte sono mescolati sulle altre case della
  sua prima traversa (le colonne b, c, d, f e g), gli alfieri su case di colore opposto. Questo fa 18 disposizioni per
  parte, quella del Bianco e quella del Nero scelte indipendentemente: *Tutte le 324 posizioni*, o *Solo le 18
  simmetriche*, in cui i pezzi del Nero stanno sulle stesse colonne di quelli del Bianco. Sono posizioni normali,
  scritte in un file EPD (proposto accanto ai libri della libreria, o in `books/` nel profilo) e giocate da qualsiasi
  motore, senza Chess960.
- *Costruisci un libro Polyglot…*: un libro Polyglot (`.bin`) dalle partite di un file PGN, come fa `make-book` di
  Polyglot, con la conversione che trasforma i libri PGN dei partecipanti in libri Polyglot. Il suo modulo, al posto
  dei dettagli del libro, prende il file PGN (il libro PGN scelto nell'elenco, se c'è), la **profondità** (le
  semimosse di ogni partita che entrano nel libro), le **partite minime** di cui una mossa ha bisogno nella sua
  posizione per essere tenuta (`-min-game` di Polyglot; le altre vengono escluse perché giocate troppo raramente), i
  **pesi**, *per punti* (i punti della parte che ha giocato la mossa: una vittoria 2, una patta 1, una sconfitta 0,
  come conta Polyglot; le partite senza risultato vengono escluse, e così una mossa le cui partite sono state tutte
  perse) o *per numero* (il numero di partite che la giocano), e, se spuntato, un **Elo**: contano solo le mosse di un
  giocatore con almeno quel rating, secondo il tag `WhiteElo` o `BlackElo` della partita (un giocatore senza rating
  viene escluso). *Costruisci…* chiede dove scrivere il libro (accanto al file PGN per impostazione predefinita), poi
  legge le partite in background, con il suo avanzamento e *Annulla*, accanto al modulo e in fondo all'elenco; il
  libro, ordinato per chiave come vuole il formato, viene poi aggiunto alla libreria, con le sue opzioni nella
  descrizione. La console dice quante partite vi sono entrate, quante ne hanno escluse le opzioni e quante non si sono
  potute leggere.

### Libri dalle aperture denominate
<!-- en: books-from-the-named-openings df5656f9 -->

*Dalle aperture denominate di Lichess…*, in **Crea…**, genera un libro di posizioni di partenza dalla tabella delle
aperture denominate di Lichess, già inclusa nel programma (chess-openings, CC0): non viene scaricato niente. Il suo
modulo, al posto dei dettagli del libro, prende le **famiglie** (A aperture di fianco, B partite semiaperte, C partite
aperte, D partite chiuse, E difese indiane), le **lunghezze** delle linee tenute, in semimosse (da 8 a 16 all'inizio),
*Una posizione per variante denominata* (più linee condividono uno stesso nome) e il **numero** di posizioni,
distribuite in modo uniforme sui codici ECO quando ce ne sono di più. Dice quante posizioni e aperture conterrebbe;
*Crea il libro…* chiede dove scrivere il file EPD, ogni posizione con il suo numero di mossa (`hmvc`, `fmvn`) e la sua
apertura come `c0`, e lo aggiunge alla libreria.

### Libri dalle posizioni valutate
<!-- en: books-from-the-evaluated-positions 82dd002c -->

*Dalle posizioni valutate di Lichess…*, in **Crea…**, genera un libro delle posizioni che i giocatori di Lichess avevano
analizzato: quelle delle linee delle aperture denominate e a una mossa da esse, ciascuna con la sua valutazione e la
sua profondità. Vengono dal database delle valutazioni di Lichess (CC0, 22,5 GB e 418 milioni di posizioni): la
sua dimensione viene chiesta a database.lichess.org e indicata prima di scaricare qualsiasi cosa; *Scarica e leggi* lo
legge mentre arriva, e conserva solo le posizioni trovate (qualche megabyte, nel profilo), non il file. *Leggi un file
già scaricato…* legge invece un `lichess_db_eval.jsonl.zst` dal disco. Dalla macchina non esce nient'altro che la
richiesta del file. La lettura mostra quanto è stato letto e conservato; *Annulla*, un'interruzione di rete o la
chiusura del programma conservano ciò che è stato letto, e *Continua* riprende da lì (un file cambiato sul server nel
frattempo viene riletto dall'inizio).

Una volta letto, il modulo prende la **valutazione** tenuta, per il Bianco (da +0.30 a +0.80 all'inizio), la
**profondità** minima (30), le **semimosse** dall'inizio (da 8 a 16), *Una posizione per variante denominata* (prima
quella sulla linea della variante, poi la valutazione più profonda, poi la più vicina al centro dell'intervallo di
valutazione) e il **numero** di posizioni, distribuite in modo uniforme sui codici ECO quando ce ne sono di più. Dice
quante posizioni e aperture conterrebbe e la loro valutazione media; *Crea il libro…* scrive il file EPD, ogni
posizione con il suo numero di mossa, la sua valutazione per il lato al tratto e la sua profondità (`ce`, `acd`) e la
sua apertura (`c0`), e lo aggiunge alla libreria. *Rileggi* rilegge il database per le sue valutazioni più recenti.

### Libri CTG
<!-- en: ctg-books a908f007 -->

**Aggiungi…** accetta anche un libro in formato CTG, i libri di aperture di ChessBase e Fritz: un `.ctg` con accanto
il suo `.ctb` e il suo `.cto`, scegliendo uno qualsiasi dei tre. Viene solo letto, mai scritto. La sua pagina dice
quante posizioni e mosse contiene, e quante mosse il suo autore ha segnato in verde (da giocare) e in rosso (da
evitare). Per ogni posizione il libro ha le sue mosse, ciascuna con il suo simbolo (!, ?, !?, unica mossa…), il suo
colore, la valutazione della posizione a cui porta e le partite che la sostengono: quante, come sono finite, l'Elo dei
giocatori.

*Aprilo nell'albero* lo mostra nella scheda *Albero* della pagina Scacchiera, tra le fonti: le mosse della posizione
mostrata con le loro partite, il punteggio, l'Elo medio dei giocatori che le hanno giocate e la performance, e una
colonna *Libro* con il loro colore e simbolo. Le mosse verdi vengono per prime; quelle rosse restano nell'elenco, in
grigio. Gli anni e le partite di riferimento vengono dai database, non dal libro.

fastchess non legge i libri CTG. *Per i motori: crea un libro Polyglot…* scrive una copia Polyglot (`.bin`) e la
aggiunge alla libreria, per le *Aperture* della configurazione e i libri propri dei partecipanti: le sue mosse rosse
vengono escluse, e ogni mossa pesa i punti fatti dalle sue partite (due una vittoria, uno una patta), il doppio per una
mossa verde; una mossa che non ha fatto punti viene esclusa. *Verifica il libro* segue le sue linee dalla posizione
iniziale e dice quante posizioni ha raggiunto tra quelle che il libro contiene, e se qualche mossa non si è potuta
leggere. Niente esce dalla macchina.

## Scacchiera
<!-- en: board 0bbd00c9 -->

La pagina Scacchiera mostra una partita o una posizione, di qualsiasi provenienza: **A** su qualsiasi scacchiera del
programma (una partita del mosaico della scheda *Diretta* o quella seguita, la scacchiera della scheda *Partite*, della
scheda *Aperture*, di un'apertura di un libro) apre qui la sua posizione e la analizza; nella pagina Scacchiera, A avvia
e ferma l'analisi. *Torna al torneo* (o *Indietro*) riporta alla pagina da cui si è arrivati. La sua scacchiera può
essere disegnata in 3D: vedi [La scacchiera 3D](#la-scacchiera-3d).

La scacchiera è a sinistra, con i giocatori sopra e sotto di essa; a destra, dall'alto, l'analisi, le mosse (con la
tabella delle mosse, il resoconto della partita e le partite del file in schede) e il grafico della partita. Il
sottotitolo della pagina dice di che partita si tratta: il suo evento e il suo turno, il suo risultato, la sua cadenza,
il suo codice ECO e la sua apertura, e come è finita.

Una mossa si gioca sulla scacchiera con il mouse: un clic sul pezzo e poi uno sulla sua casa, oppure il pezzo trascinato
fin lì. Mentre viene trascinato, segue il puntatore e la casa in cui atterrerebbe è colorata; lasciato altrove, torna
indietro, ancora preso, per essere mosso con un clic. Sulla scacchiera 3D un fantasma traslucido del pezzo segue il
puntatore, sospeso sopra la scacchiera e passando dietro i pezzi davanti a lui, mentre l'immagine resta com'è; il pezzo
stesso si muove quando viene lasciato.

### Aprire, incollare e impostare una posizione
<!-- en: opening-pasting-and-setting-up-a-position b8dc5047 -->

- **Apri un PGN…** legge qualsiasi file PGN (un file si può anche trascinare sulla finestra), in background con una
  barra di avanzamento: un file di milioni di partite richiede pochi secondi, e le sue partite sono elencate in una
  scheda accanto alle mosse, filtrate da ciò che viene digitato (ogni parola trovata nei giocatori, nell'evento, nel
  luogo, nella data, nel turno, nell'apertura, nel codice ECO o nel risultato). Un clic mostra una partita. Le
  valutazioni delle sue mosse disegnano il grafico: vengono letti i commenti di cutechess e fastchess
  (`{+0.35/18 1.2s}`) e il `[%eval 0.35]` di lichess e ChessBase (dal punto di vista del Bianco, `#-3` per un matto),
  e anche gli orologi di `[%clk 0:01:23]`. Le sue varianti vengono conservate (vedi [Varianti](#varianti)). Una
  partita il cui tag FEN non si può giocare viene rifiutata, e le mosse di una partita si fermano alla prima che non
  si può leggere.
- **Incolla** (Ctrl+V sulla pagina) legge gli appunti: un FEN, o i quattro campi di una riga EPD; un PGN di una o più
  partite (elencate come quelle di un file); un elenco di mosse dalla posizione iniziale, in SAN con o senza numeri
  (`1. e4 e5 2. Nf3`) o in UCI (`e2e4 e7e5`). Una posizione che non si può giocare viene rifiutata con il motivo (un re
  mancante, un pedone sulla prima traversa, il lato che non muove sotto scacco). Diritti di arrocco il cui re o la cui
  torre non sono sulla loro casa abituale, o scritti come colonne (`HAha`), ne fanno una posizione Chess960. Si può
  trascinare anche un file `.fen` o `.epd`.
- **Modifica la posizione** imposta una posizione sulla scacchiera: un clic mette il pezzo scelto (un secondo clic lo
  toglie), un clic destro lo toglie; il lato che muove, il numero della mossa, i diritti di arrocco (proposti solo
  quando il re e la torre sono dove devono essere), la casa en passant (solo dietro un pedone che ha appena avanzato di
  due case) e Chess960. Il FEN sotto segue la scacchiera, e un FEN digitato lì la imposta. *Fatto* viene proposto
  appena la posizione si può giocare, e la mette sulla scacchiera.
- *Copia PGN* (a destra delle schede delle mosse) copia la partita mostrata, con i suoi tag, le sue mosse (i punteggi
  dei motori come commenti quando li ha) e le sue varianti. Ctrl+S, o *Salva la partita in PGN…* nella tavolozza dei
  comandi, scrive lo stesso in un file. I commenti, i simboli ($1 o !?), le frecce e le case colorate, gli orologi e le
  valutazioni di un file PGN aperto vengono conservati, e riscritti come sono stati letti.
- **La scacchiera**: i pulsanti sotto di essa e ← → Inizio Fine scorrono le mosse, e F (o l'ultimo pulsante) la ruota;
  un clic su un pezzo e su una casa gioca una mossa (un pedone che promuove chiede il suo pezzo). *Copia FEN* copia la
  posizione.

### I giocatori
<!-- en: the-players 1f4f551f -->

Sopra e sotto la scacchiera, quando la partita ha dei giocatori: il nome di ciascuno, la valutazione della sua ultima
mossa data dal suo motore (il punteggio del Bianco e la profondità, in un'etichetta), la mossa che ha giocato e il
tempo che ha impiegato, oppure *al tratto*, e a destra il suo orologio dopo la sua ultima mossa, incorniciato mentre ha
il tratto. Gli orologi sono quelli dei `[%clk]` delle mosse quando la partita li ha; altrimenti vengono ricostruiti dal
suo tag `TimeControl` (`300+2`, `10+0.1`: secondi e incremento) e dal tempo di ogni mossa, senza che le mosse di libro
ne consumino.

### Analisi
<!-- en: the-analysis 1e6120de -->

Un motore della libreria analizza la posizione mostrata e segue la scacchiera. La scheda mostra il suo motore, poi un
pulsante con i suoi *Threads*, *Hash* (0: quello del motore) e *Linee* (MultiPV), che un clic apre: le impostazioni
proprie dell'analisi, conservate per la volta successiva, che non cambiano né il motore della libreria né alcun torneo
(gli altri valori delle opzioni della libreria vengono inviati come li invia una partita). A destra, i thread che la
pagina Scacchiera usa su quelli della macchina, nel colore di avviso mentre un torneo è in corso (il suo tooltip dice
quanti thread prendono le partite del torneo: l'analisi compete con esse).

La ricerca si presenta in riquadri (profondità e seldepth, nodi, velocità, tempo, accessi alle tablebase, quanto è
pieno l'hash), le probabilità di vittoria, patta e sconfitta quando il motore le dà (`UCI_ShowWDL`), e ogni linea: il
suo punteggio (del Bianco, il migliore messo in risalto), la sua profondità e le sue mosse su due righe, con le figurine
(vedi [Aspetto](#aspetto)). Passare il mouse su una mossa di una linea mostra sulla scacchiera, incorniciata, la
posizione dopo di essa; un clic gioca la linea fino a quella mossa. La prima mossa di ogni linea è disegnata come una
freccia, la migliore più scura (**Impostazioni** > *Aspetto* > *Le frecce dell'analisi nella pagina Scacchiera* le
toglie; le linee mostrano comunque le mosse).

**Minaccia** (X) fa analizzare a un altro processo del motore la posizione con il tratto passato: la mossa migliore
dell'avversario, se muovesse di nuovo, disegnata come una freccia rossa e scritta sotto le linee (nessuna mentre il
lato che muove è sotto scacco).

L'analisi attende mentre è mostrata un'altra pagina, e prosegue quando si torna alla pagina Scacchiera; *Ferma
l'analisi* (o A) la ferma, e un punto sul pulsante *Scacchiera* della barra indica quando è in corso. Il suo motore non
è uno dei processi del torneo: la fine di un torneo non lo termina. Fermata l'analisi, il suo motore resta avviato per
la ricerca successiva, con la sua tabella hash (e la memoria GPU di un motore a rete neurale): il pulsante di accensione
accanto al motore, *Scarica dalla memoria il motore* (anche nella tavolozza dei comandi), lo termina, insieme a quello
della minaccia.

### Analizzare una partita intera
<!-- en: analysing-a-whole-game a7e6166c -->

**Analizza la partita** (G, negli strumenti della pagina o nell'intestazione del grafico) fa analizzare al motore
dell'analisi (i suoi thread e il suo hash, una linea) ogni posizione della partita in background, prima per 20 ms
ciascuna, poi di nuovo quattro volte più a lungo a ogni passaggio (80 ms, 0.3 s, 1.3 s, 5 s…), fino al tempo scelto con
la freccia accanto al pulsante (0.3 s, 1.3 s, 5 s, 20 s per posizione, o *fino all'arresto*): il grafico compare in un
secondo o due e si affina a ogni passaggio. Le posizioni vanno dall'ultima alla prima, così l'hash del motore conserva
ciò che viene dopo una posizione (misurato: circa il 16 % più vicino alle ricerche lunghe che dalla prima all'ultima, a
parità di tempo), con la posizione sulla scacchiera per prima. L'intestazione del grafico indica il passaggio, il tempo
per posizione e l'avanzamento, e una sottile fascia sotto la sua striscia dei tempi ciò che ha ricevuto ogni posizione;
*Ferma* (o G) la ferma.

Ogni mossa è giudicata dalle probabilità di vittoria che chi l'ha giocata ha perso, secondo i punteggi dell'analisi
prima e dopo di essa (la misura di lichess: 50 % a 0.00, circa 59 % a +1.00): 10 punti persi sono un'imprecisione
(`?!`), 20 un errore (`?`), 30 uno svarione (`??`), segnati dopo la mossa, sul grafico e sulla scacchiera; la mossa che
il motore avrebbe giocato non viene mai segnata. Una mossa è giudicata sull'ultimo passaggio completo; un segno che
l'ultimo passaggio ha cambiato resta vuoto finché il successivo non lo conferma. N e Maiusc+N (o i pulsanti accanto
alla navigazione) vanno alla mossa segnata successiva e a quella precedente.

Ogni passaggio completo viene conservato con la partita (in `analyses/` nel profilo): la stessa partita, riaperta da
qualsiasi punto, lo mostra subito, e G riprende dai passaggi fatti. L'analisi gira su un thread proprio con un proprio
processo del motore, i suoi thread contati nel budget dei core; l'analisi della scacchiera (A) la mette in pausa, e
quando A analizza una posizione della partita più a lungo del passaggio, il suo valore prende il posto di quello della
posizione.

**Più analisi**: *+ Motore* nella legenda del grafico fa analizzare la partita anche a un altro motore della libreria,
altrettanto a lungo a ogni passaggio. Ogni analisi ha il suo colore sul grafico e sulla scacchiera (la sua mossa
migliore come freccia mentre l'analisi della scacchiera è ferma), la sua riga in *Questa mossa* e le sue precisioni nel
resoconto. Procedono insieme passaggio per passaggio, tutte allo stesso tempo quando i core lo permettono, una dopo
l'altra altrimenti. Un'analisi giudica le mosse (la prima; un clic destro su un'altra nella legenda ne fa il giudice, o
la rimuove); un segno è vuoto quando le analisi non giudicano tutte la mossa allo stesso modo. Le probabilità di
vittoria confrontano i motori meglio dei centipedoni, che differiscono da un motore all'altro: prendono il WDL di ogni
motore quando ne dà uno.

### Il grafico della partita
<!-- en: the-game-graph 14e02bad -->

Sotto le mosse, il grafico della partita, lo stesso che in *Diretta* e in *Partite*:

- **Valutazione**: l'analisi che giudica come un'area bianca e nera (il vantaggio del Bianco sopra la linea, quello del
  Nero sotto), ogni motore che ha giocato come una curva del suo colore alle sue mosse, le altre analisi come curve
  tratteggiate; le mosse segnate sulla curva; i valori oltre ±6 disegnati lungo il bordo nel colore di avviso. Senza
  un'analisi, l'area è la media delle valutazioni dei due motori.
- Le mosse di libro a strisce, le fasi sotto l'asse (l'apertura; il mediogioco dalla prima posizione con al massimo 10
  pezzi oltre ai re e ai pedoni, o meno di 4 su una traversa di fondo; il finale da al massimo 6, come lichess divide
  una partita), la zona che le tablebase conoscono colorata, i numeri delle mosse lungo il fondo.
- Sotto l'asse, il tempo di ogni mossa: quello del Bianco verso l'alto, quello del Nero verso il basso.

Le sue schede: *Probabilità di vittoria* (dal WDL quando i motori lo danno, con la formula di lichess altrimenti),
*V/P/S* (le quote riportate dai motori, le vittorie del Bianco in basso), *Tempo* (con il tempo di decisione),
*Profondità* (con la seldepth), *Nodi*, *Velocità*, *Accessi alle tablebase*, *Accordo* (quanto distano le valutazioni
dei due giocatori, ogni mossa rispetto a quella prima, e quelle delle analisi in ogni posizione) e *Accordo delle PV*
(quante semimosse della PV di un motore ripetono ciò che quella dell'altro aveva previsto); quelle per cui la partita
non ha valori sono escluse, e quelle che non ci stanno vanno nel menu `⋯`. Passando il mouse si vedono la mossa e ciò
che ha detto ogni motore; un clic o un trascinamento va alla mossa. Un clic su una curva nella legenda la nasconde o la
mostra; la freccia a destra chiude il grafico.

### Questa mossa
<!-- en: this-move a4a908f8 -->

Sotto le mosse, sulla mossa mostrata: ciò che ha detto il motore che l'ha giocata (il suo punteggio, profondità e
seldepth, tempo, nodi, velocità, accessi alle tablebase, come li ha il PGN), la linea che si aspettava dopo di essa, il
verdetto di ogni analisi (la quota delle probabilità di vittoria persa, il giudizio, la mossa migliore e il suo
punteggio), e con le tablebase il risultato dopo la mossa e se la mossa ha mantenuto quello di prima. *Mostra* posa una
linea sulla scacchiera senza toccare la partita: la posizione dopo di essa, la scacchiera incorniciata, con scritto
sopra di che cosa si tratta; Esc, un clic sulla scacchiera o una mossa nella partita riporta la partita. Sulla
scacchiera, il giudizio della mossa è un distintivo sulla sua casa, e la risposta che il suo motore si aspettava una
freccia tratteggiata.

### Varianti
<!-- en: variations f4e65c3b -->

Una mossa giocata sulla scacchiera dove la partita è andata diversamente apre una variante: la partita resta com'era,
con le sue analisi e il suo grafico. Le mosse mostrano le varianti tra parentesi dopo la mossa che sostituiscono,
annidate e numerate; un clic su una mossa porta lì, e *Torna alla partita* riporta alla posizione da cui parte la
variante. Un clic destro su una mossa di una variante la promuove (diventa la linea da cui parte, le cui mosse dopo di
essa diventano la variante), la elimina con quelle che partono da essa, o la copia. *Copia PGN* scrive le varianti, e
quelle di un file PGN aperto vengono lette, annidate a qualsiasi profondità, con i loro commenti e simboli (una mossa
che non si può giocare termina solo la sua variante). Il grafico, le analisi e i giudizi sono quelli della partita; su
una variante, il grafico segna dove essa lascia la partita.

### Le mosse, la tabella e il resoconto
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Mosse** scrive la partita come testo continuo: figurine (vedi [Aspetto](#aspetto)), le mosse di libro in grigio e la
fine del libro segnata, i giudizi a colori, la mossa attuale incorniciata; passando il mouse su una mossa si vedono ciò
che ha detto il suo motore e il suo giudizio. **Tabella** elenca ogni mossa con il punteggio del suo motore (del
Bianco), la profondità e il tempo, una barra per il tempo, le mosse segnate colorate.

**Resoconto**, una volta analizzata la partita: per ciascun lato la sua precisione (quella di lichess: quella di ogni
mossa dalle probabilità di vittoria che ha perso, le mosse dei momenti critici pesano di più), le sue imprecisioni,
errori e svarioni, la sua perdita media in centipedoni, la quota delle sue mosse che anche l'analisi giocherebbe, la
profondità e il tempo medi del suo motore; la sua precisione nell'apertura, nel mediogioco e nel finale; con più
analisi, le precisioni che dà ciascuna. Poi i momenti chiave, ognuno a un clic di distanza: la svolta (la mossa che ha
perso di più, con la mossa migliore), gli altri errori e svarioni, dove i due motori sono stati in disaccordo di 1.5
pedoni o più per due mosse di fila, l'inizio del finale e quello delle tablebase. *Chi l'ha visto per primo* indica,
per la svolta, a quale mossa ogni motore che ha giocato e ogni analisi ha dato per la prima volta un pedone o più al
lato che guadagna. *Copia il PGN con l'analisi* scrive la partita con la valutazione di ogni posizione (`[%eval]`), i
giudizi come NAG e, dopo una mossa segnata, la linea che l'analisi preferiva; *Copia come BBCode* dà il resoconto per
un forum.

### Le tablebase nella pagina Scacchiera
<!-- en: tablebases-on-the-board-page 721b71dd -->

Con le cartelle Syzygy della configurazione (*Aggiudicazione* > tablebase), la pagina Scacchiera interroga ogni
posizione della partita: il grafico colora la zona che le tabelle conoscono, *Questa mossa* indica il risultato dopo la
mossa e se la mossa ha mantenuto quello di prima o lo ha rovinato, e il resoconto segna dove subentrano le tabelle. Ogni
posizione è interrogata come se fosse appena stata giocata una cattura o una mossa di pedone, il che è esatto tranne
dove la regola delle 50 mosse è vicina. Le tabelle sono quelle del processo del programma: non vengono interrogate
mentre un torneo vi gioca.

### Giocare una partita da una posizione
<!-- en: playing-a-game-from-a-position 6b5eb32f -->

**Gioca una partita…** gioca una partita dalla posizione sulla scacchiera tra due motori della libreria, o tra te e uno
di essi (vedi *Umano* più sotto) (*Bianco* e *Nero*, `<->` li scambia con i loro orologi), con un orologio per ciascuno
(minuti e un incremento in secondi; *Lo stesso per entrambi*, o uno ciascuno per un handicap di tempo), e, se spuntata,
una seconda partita a colori invertiti. I motori vengono avviati con i valori delle opzioni della libreria, un motore
WinBoard tramite l'adattatore. La scacchiera segue la partita mentre viene giocata (tornare indietro smette di seguirla
finché non se ne raggiunge di nuovo la fine, o si clicca *Torna alla mossa attuale*), con gli orologi, la ricerca del
lato che muove e *Ferma*, che termina la partita senza risultato. Una partita finisce per matto, stallo, materiale
insufficiente, regola delle 50 mosse, triplice ripetizione, mossa illegale, un motore che si ferma, o sconfitta per
tempo (all'orologio sono concessi 0.1 s perché la mossa arrivi). Ogni partita viene aggiunta, quando finisce, a un file
PGN in `games/` nel profilo (con il nome della data e dei motori, con un tag `Termination`), che *Apri le partite*
elenca sulla scacchiera.

Le partite sono giocate dal client per motori proprio di RookStudio, al di fuori di qualsiasi torneo: si svolgono
mentre ne gioca uno, senza toccarlo. I loro thread (il maggiore tra quelli dei due motori, perché giocano a turno)
contano nel budget dei core; quando le partite dei tornei in corso e l'analisi riempiono già la macchina, la scheda lo
dice e propone *Gioca comunque* invece di *Gioca*. L'analisi si ferma quando inizia una partita, perché competerebbe
con i suoi motori.

**Umano**, primo negli elenchi *Bianco* e *Nero*, sei tu: giochi quel lato in prima persona; le tue mosse si fanno sulla
scacchiera con il mouse, su quella piatta come su quella 3D (un pedone che raggiunge l'ultima traversa chiede il suo
pezzo). La scacchiera si gira per mettere il tuo lato in basso a ogni partita (*Ruota* continua a girarla), accetta i
tuoi pezzi solo quando tocca a te (la scheda mostra *Tocca a te*), e un clic mentre guardi una mossa precedente la
riporta alla partita. Il tuo orologio scorre come quello di un motore, sconfitta per tempo compresa: concediti più tempo
del motore togliendo la spunta a *Lo stesso per entrambi*. Mentre giochi, ciò che pensa il motore viene tenuto da parte
(la sua valutazione, la profondità e la variante, quindi anche la barra di valutazione e la curva del grafico); la
partita finita li ha, pronta per *Analizza la partita*. *Abbandona la partita* la chiude in qualsiasi momento, vinta dal
motore; con una seconda partita, questa segue, con il tuo colore invertito. Le tue partite vengono salvate con il tuo
nome utente, e solo il motore conta nel budget dei core. Non si può ritirare una mossa, e il motore gioca a piena forza
a meno che la sua scheda non ne imposti una minore (`UCI_LimitStrength` e `UCI_Elo`, o `Skill Level`, se li ha).

In una vista per lo studio, la scheda porta anche alla pagina Gioca: una tua partita contro un motore alla forza che
scegli, con le sue aperture, un handicap, mosse ritirate e suggerimenti, e gli aiuti nascosti fino alla fine (vedi [La
pagina Gioca](#la-pagina-gioca)).

### La Scacchiera per lo studio
<!-- en: the-study-board 165eb3bd -->

In una vista la cui Scacchiera è *Per lo studio* (vedi [Viste](#viste)), la pagina Scacchiera è dove si leggono e si
scrivono le partite. La scacchiera è a sinistra; a destra, la striscia della partita, poi sei schede, da Alt+1 ad
Alt+6:

- **Notazione**: la riga del titolo della partita e le sue intestazioni (vedi [Intestazioni](#intestazioni)), poi le
  mosse, i loro commenti, simboli e varianti, scritti direttamente (vedi [Notazione](#notazione)); la tabella delle
  mosse e il resoconto sono in schede proprie (vedi [Il resoconto di una partita](#il-resoconto-di-una-partita));
- **Analisi**: fino a quattro motori affiancati (vedi [Più motori affiancati](#più-motori-affiancati)), e il cloud e
  le tablebase di Lichess quando li hai scelti (vedi [Online per l'analisi](#online-per-lanalisi)), con il grafico
  della partita;
- **Albero**: le mosse giocate dalla posizione nei tuoi database, nei tuoi libri e su Lichess (vedi [La scheda
  Albero](#la-scheda-albero));
- **Riferimenti**: le partite più forti della posizione (vedi [Partite di riferimento](#partite-di-riferimento));
- **Domande**: l'assistente (vedi [Domande](#domande));
- **Finali**: cosa dicono le tablebase della posizione, quando restano sette pezzi o meno (vedi [La scheda
  Finali](#la-scheda-finali)).

La striscia sopra le schede dice da dove viene la partita e cosa ne è stato: *Salva* (Ctrl+S) con il suo stato
(*Modificata: 2 modifiche*, poi *Salvata in «Mega»*), il posto della partita nella lista da cui viene, con
la partita precedente e la successiva (Ctrl+← e Ctrl+→), e *Recenti* (vedi [Le partite recenti e la
lista](#le-partite-recenti-e-la-lista)). Con la libreria vuota, le schede Analisi, Domande e Resoconto propongono di
scaricare Gaia con un clic. *Esporta…*, sotto la scacchiera, trasforma la partita in una pagina da rigiocare, un
diagramma, una GIF animata o un PDF; <kbd>Ctrl</kbd>+<kbd>Maiusc</kbd>+<kbd>C</kbd> copia il diagramma della posizione
(vedi [Esportare e stampare](#esportare-e-stampare)).

Una mossa si può digitare oltre che giocare: la sua prima lettera apre un campo sopra le schede, che propone le mosse
legali che iniziano così, nelle lettere dei pezzi della lingua dell'interfaccia: K Q R B N in inglese e in cinese,
R D T F C in francese, K D T L S in tedesco, R D T A C in spagnolo e in italiano, R D T B C in portoghese brasiliano,
Кр Ф Л С К in russo. Anche le lettere inglesi vengono sempre lette: `Ng` propone le stesse mosse del tedesco `Sg`. Una
lettera con due significati propone entrambi: in francese, spagnolo, italiano e portoghese, R dà prima le mosse del re,
poi quelle della torre, segnate *torre (inglese)*; in russo, К dà le mosse del cavallo e quelle del re, Кр solo quelle
del re. Le case in cui vanno le mosse proposte sono segnate da un punto sulla scacchiera, con la freccia della prima
disegnata: ↑ e ↓ ne scelgono un'altra, Invio gioca quella scelta, un clic gioca una qualsiasi di esse, e `0-0` è anche
l'arrocco. Lo stesso campo accetta le mosse degli esercizi della pagina Allenamento (i problemi, i tuoi errori,
indovinare le mosse, i finali) e delle partite della pagina Gioca. L'interfaccia scrive le mosse con le stesse lettere
(la notazione quando *Figurine nella notazione* è disattivato, le linee dei motori, il resoconto); il PGN mantiene la
SAN inglese, come la leggono tutti i programmi, e i commenti dell'assistente mantengono le lettere della propria lingua.
Le lettere della Scacchiera dei motori passano allora sotto Alt: Alt+F ruota la scacchiera, Alt+A avvia o ferma
l'analisi, Alt+X mostra la minaccia, Alt+G analizza la partita, Alt+N e Alt+Maiusc+N vanno alla mossa successiva e alla
precedente segnate dall'analisi.

### Più motori affiancati
<!-- en: several-engines-side-by-side 43f5e1fe -->

Nella scheda Analisi della Scacchiera per lo studio, fino a quattro motori della libreria analizzano insieme la
posizione, ciascuno in una scheda: il suo colore e il suo nome, la sua valutazione (dal punto di vista del Bianco), la
profondità e la seldepth, la velocità e i thread, la linea migliore, e le altre linee raccolte in una sola (un clic le
espande). Passando il mouse su una mossa di una linea, la scacchiera mostra la posizione dopo di essa; un clic gioca la
linea fino a quella mossa. **+ Un motore** elenca i motori della libreria, con quelli dell'analisi spuntati (↑ ↓ ne
scelgono uno, Invio lo aggiunge), e *Scarica un motore…*, i motori popolari della pagina Motori; un motore aggiunto
inizia subito a cercare. Alt+A, o *Analizza*, li avvia e li ferma tutti, e i motori vengono conservati per la volta
successiva.

Sulla scacchiera, la mossa migliore di ogni motore è una freccia del suo colore: blu scuro, arancione chiaro, verde
acqua e grigio, dal primo motore al quarto, distinti anche per luminosità. I motori che giocano la stessa mossa
condividono una freccia, del colore del primo di loro, con il loro numero accanto alla sua origine. La riga in cima
alla scheda dice la stessa cosa a parole: quali motori concordano, su quale mossa, e quanto distano le loro valutazioni
(*7…Be7 per Stockfish e Gaia · 7…Qb6 per Leela · le loro valutazioni distanti al più 0.11*), o il matto che vede un
motore; nessun colore va distinto. Con un solo motore, le frecce sono quelle delle sue linee, come sulla Scacchiera dei
motori.

**Thread**: l'analisi ha un solo numero di thread, impostato dal chip in alto (*24 / 32 thread*), diviso in parti
uguali tra i suoi motori. Il menu di un motore (`…`) ne imposta le linee, l'hash e, a mano, i thread, che la
ripartizione degli altri lascia allora da parte. Un motore che cerca su una scheda grafica (*Usa una GPU* spuntato
nella libreria, o un motore come Lc0 la cui opzione *Backend* indica un backend GPU) tiene i propri thread, e la
ripartizione non li conta. Un torneo in corso su questo computer tiene i suoi core: l'analisi prende al massimo ciò che
lasciano le sue partite, e il chip dice quanti ne occupano. Queste impostazioni sono proprie dell'analisi: né il motore
della libreria né un torneo le vedono.

**Il primo motore** è quello dell'analisi: lo usano il resoconto della partita (Alt+G), la minaccia (Alt+X, o il menu
del primo motore), le domande e i commenti dell'assistente. Un motore trascinato per il nome su un altro ne prende il
posto, e in cima diventa il primo; *Rendilo il primo* nel suo menu fa lo stesso. Quando la partita ha un'analisi del
nuovo primo motore, è quell'analisi a giudicare le mosse e a disegnare la curva del grafico della partita. Le
valutazioni di ogni motore sulle posizioni della partita che ha cercato compaiono sul grafico come una linea sottile
del suo colore (*analisi in diretta*), quelle del primo motore come la curva finché la partita non ha un'analisi.

Mentre una delle tue partite si gioca su Lichess, ogni motore si ferma subito, e la Scacchiera per lo studio non
mostra nessuna delle loro frecce (vedi [Fair play](#fair-play)). *Scarica dalla memoria i motori dell'analisi*, nel
menu di un motore o nella tavolozza dei comandi, ne termina i processi. Ogni gesto della scheda è nella tavolozza dei
comandi (Ctrl+K): un motore aggiunto, reso il primo o rimosso, le sue impostazioni, i thread dell'analisi.

### Mosse pratiche
<!-- en: practical-moves 26da3fb5 -->

Sotto i motori, *Contro un umano da 1500, rapida* (il livello e la cadenza conservati da una volta all'altra) cerca la
mossa pratica: tra le mosse che il primo motore mette entro 0,3 pedoni dalla sua migliore (o, quando le tablebase
conoscono la posizione, quelle che mantengono il loro verdetto), quella che rende di più contro un giocatore di quel
livello, nelle partite di quel livello. Con un account Lichess collegato, le partite sono quelle dell'esploratore di
Lichess nella fascia di rating e alla cadenza del livello (*Lichess 1400–1600, rapida*); senza, quelle dei tuoi database
i cui giocatori hanno un rating medio nella fascia del livello, qualunque sia la loro cadenza. Il punteggio di una mossa
conta da 200 partite al livello in su; fuori dalle partite note, la mossa pratica è quella che lascia all'avversario
meno buone risposte, secondo il motore: un motore tutto suo, su un thread, cerca le risposte di ogni candidata, e la
tabella dice quante tengono (*2 di 31*). Mai una mossa perdente.

La tabella dà ogni candidata, la sua valutazione secondo il motore (dal punto di vista del Bianco), il suo punteggio
contro quel livello e le sue partite lì, con la migliore del motore segnata *migliore* e la mossa pratica *pratica*;
sulla scacchiera, la mossa pratica è la freccia blu, la migliore del motore quella grigia. Una frase riassume,
attenendosi ai numeri: *La mossa pratica: 4.Cg5 — a 0.13 dalla migliore del motore, e 58 % atteso a questo livello.*
Quando le partite lo mostrano, prosegue con la risposta più giocata a quel livello lungo la linea e ciò che la punisce,
la risposta del motore: *Dopo 4…d5 5.exd5, la risposta più giocata al livello 1500 è 5…Cxd5? (46 % per il Nero), che
6.Cxf7 punisce.* Le mosse pratiche richiedono l'analisi in corso (Alt+A). Cosa esce dalla macchina: con un account
collegato, la posizione e il livello, all'esploratore di Lichess; senza, niente. Bloccate con la scheda Analisi mentre
una delle tue partite si gioca su Lichess.

### Notazione
<!-- en: notation 5147406e -->

La notazione si scrive direttamente, senza finestre di dialogo:

- **Commenti**: Invio scrive un commento dopo la mossa mostrata, Maiusc+Invio uno prima di essa; Invio lo conferma, Esc
  lo abbandona. Un clic su un commento va alla sua mossa, un doppio clic lo corregge.
- **Simboli**: `!` e `?` digitati mettono il simbolo della mossa, e un secondo segno subito dopo forma la coppia (`!!`,
  `??`, `!?`, `?!`). I pulsanti sotto la notazione mettono gli altri: unica mossa, novità, e la valutazione della
  posizione (=, ∞, e il vantaggio del Bianco o del Nero, leggero, netto o decisivo). Un simbolo messo di nuovo viene
  tolto.
- **Varianti**: una mossa diversa da quella della partita, giocata sulla scacchiera o digitata, apre una variante.
  Tenuto premuto mentre si fa la mossa, Maiusc sostituisce le mosse successive, e Ctrl chiede: una variante, una nuova
  linea principale, o le mosse successive sostituite (Esc non gioca niente). *Chiedi cosa fare di una mossa che si
  discosta dalla partita* (*Impostazioni* > *Aspetto*) chiede ogni volta.
- **Canc** elimina le mosse da quella mostrata; **Alt+↑** fa salire la variante, fino alla linea principale.
- **Ctrl+Z** annulla ogni modifica (una mossa, un commento, un simbolo, una freccia o una casa, un'intestazione) e
  **Ctrl+Y** la ripristina; una riga sotto la notazione dice cosa è stato fatto e come annullarlo.

*Figurine nella notazione* (*Impostazioni* > *Aspetto*) mostra i pezzi come figure. I commenti dell'assistente si
distinguono dai tuoi, in grigio azzurro e in corsivo, dopo un fumetto, o in un riquadro sotto il suo nome dopo un
commento che c'era già (vedi [I commenti dell'assistente](#i-commenti-dellassistente)). Quando i tuoi studi di teoria
commentano la posizione mostrata, *Teoria · 3* sopra le mosse porta alle loro note nella scheda Albero (vedi [Studi di
teoria](#studi-di-teoria)).

**La partita di un torneo**, in entrambi gli stili del suo PGN (quello di fastchess `{+0.35/18 1.234s, n=…}` o lo stile
ChessBase `[%eval]`, `[%clk]` e `[%emt]`), che sia aperta dal suo file, da un database che l'ha importata o da un torneo
letto come database, mostra i commenti dei suoi motori in forma condensata: accanto a ogni mossa, in piccolo e in
grigio, la valutazione data dal suo motore (per il Bianco, come la barra e la curva) e la sua profondità, *+1.44/22*,
con la fine del libro segnata. Passando il mouse su una mossa si vede il resto: il suo motore e il suo lato, la
valutazione anche dal suo lato, la profondità e la profondità selettiva, il tempo e quando ha scelto la mossa, i nodi e
la velocità, gli accessi alle tablebase, le sue probabilità di vittoria, patta e sconfitta, e la linea che si aspettava
dopo di essa, scritta in mosse. Il tasto destro sulla mossa mostra quella linea sulla scacchiera, la rende una variante
dal punto in cui la partita l'ha lasciata (Ctrl+Z la toglie), o copia il commento del motore così com'è. La riga sotto
le mosse sceglie cosa compare accanto a esse: la valutazione e la profondità, anche il tempo, o niente (anche con
Ctrl+K). Il PGN non cambia: Ctrl+S e *Copia PGN* riscrivono il commento di ogni motore come è stato letto, con ogni
campo conservato, finché i dati della mossa restano gli stessi.

### Frecce e case
<!-- en: arrows-and-squares a46e6347 -->

Il tasto destro disegna sulla Scacchiera per lo studio: un clic colora una casa, un trascinamento da una casa a un'altra
disegna una freccia, e lo stesso gesto ripetuto la toglie. Verde per impostazione predefinita, rosso con Maiusc, blu
con Alt, giallo con entrambi. Un clic sinistro su una casa vuota cancella i segni della mossa. I segni appartengono
alla mossa mostrata e vengono salvati con la partita (come `[%cal]` e `[%csl]` nel PGN). Sulla scacchiera 3D, il tasto
destro disegna sulla scacchiera e ruota la telecamera accanto a essa; anche il tasto centrale la ruota.

### Intestazioni
<!-- en: headers 4e8fbaae -->

La riga del titolo in cima alla notazione dà i giocatori, il loro Elo, l'evento, il turno, la data e il risultato; un
clic su di essa, o Alt+H, espande le intestazioni sul posto. Tab va al campo successivo e completa un nome tra quelli
del database; una data non valida viene segnalata (`YYYY.MM.DD`, con `??` per ciò che non si sa); il risultato si
sceglie con un clic; il codice ECO viene proposto dalle mosse (i nomi delle aperture di Lichess) e scritto da solo al
salvataggio quando manca. Le altre intestazioni compaiono come le ha la partita, e se ne aggiunge una con il suo nome.
Ogni campo cambiato è una modifica (Ctrl+Z); Invio comprime le intestazioni, Esc riporta il campo al suo valore.

### Salvare una partita
<!-- en: saving-a-game ca08879e -->

Ctrl+S salva la partita dove deve stare: una partita aperta da un database vi ritorna, con il suo numero; una partita
di un file PGN ritorna nel file, al suo posto (il file viene riscritto subito, con le altre partite com'erano).
Qualsiasi altra partita apre un menu sotto *Salva*, dove Invio prende la prima riga: uno dei tuoi database (prima
quello dell'ultima volta), un nuovo file PGN, o la fine di un file PGN. Ctrl+Maiusc+S salva una copia di qualsiasi
partita altrove. Una partita senza giocatori apre prima le sue intestazioni.

Un database in cui un altro programma sta scrivendo è nel frattempo in sola lettura: la partita resta modificata qui
finché non si può salvare.

### Posizioni e partite verso Lichess
<!-- en: positions-and-games-to-lichess 1fc6bb1d -->

**lichess.org…**, sotto la scacchiera, invia la posizione o la partita a Lichess; ogni azione dice cosa invia. *Apri la
posizione su lichess.org* apre su di essa la scacchiera di analisi di Lichess nel browser: la posizione va
nell'indirizzo, nient'altro. *Condividi la partita* la importa su Lichess senza account e ne copia l'indirizzo: chi ha
l'indirizzo vede la partita, anche i nomi dei suoi giocatori. *Fai giocare la posizione*, con una cadenza (da 3+2 a
30+20, o giorni per mossa), crea una sfida aperta dalla posizione: un link per ogni lato, da mandare al suo giocatore;
una partita così non è classificata. *Aggiungi la partita ai preferiti su Lichess*, per una partita che viene da
lì, la mette nei tuoi preferiti: invia l'id della partita con il tuo token, e chiede una volta a Lichess il permesso
di modificare le tue preferenze. *In uno studio Lichess…* invia la partita in uno dei tuoi studi o in uno nuovo (vedi
[I tuoi studi Lichess](#i-tuoi-studi-lichess)).

Mentre giochi una partita su Lichess, nessuna di queste azioni tocca la sua linea: una posizione sulla strada verso la
sua posizione, o la partita che vi passa, non esce dalla macchina (vedi [Fair play](#fair-play)). Il resto resta
possibile.

### Le partite recenti e la lista
<!-- en: recent-games-and-the-list ba602f73 -->

Ctrl+Tab mostra le partite recenti, per prima quella sulla scacchiera (dodici al massimo): di nuovo Ctrl+Tab scende
nella lista, Ctrl+Maiusc+Tab risale, e rilasciando Ctrl si apre la partita scelta, con la sua lista, il suo file, le
sue modifiche e la sua cronologia di annullamento. Una partita con modifiche non salvate resta com'è finché non ci
torni. Anche *Recenti*, sulla striscia, le elenca: un clic ne apre una, e il tasto destro su una partita modificata ne
scarta le modifiche (Ctrl+Y le ripristina).

Una partita aperta dalla lista di un database ne conserva il posto: Ctrl+← e Ctrl+→ aprono la partita precedente e la
successiva della lista, o del suo filtro, alla stessa mossa quando la partita passa per la posizione mostrata.

### La scheda Albero
<!-- en: the-tree-tab 184526b8 -->

La scheda Albero dà le mosse giocate dalla posizione mostrata: le loro partite, le quote di vittorie del Bianco, patte e
vittorie del Nero, il punteggio, l'Elo medio, la performance del lato al tratto (dalla tabella FIDE) e l'ultimo anno in
cui la mossa è stata giocata. Le sue fonti sono dei chip sopra le mosse:

- **I miei database**: i tuoi database sommati, mossa per mossa (il tasto destro sceglie quali; la quota di ciascuno
  compare passando il mouse sulle partite);
- ogni database il cui albero delle aperture è costruito, e ogni libro Polyglot della libreria;
- **Maestri (Lichess)**, **Lichess** (il tasto destro ne sceglie cadenze e fasce Elo) e il giocatore di cui hai
  importato le partite: l'esploratore delle aperture di Lichess, che richiede un account collegato (vedi [Collegare il
  tuo account](#collegare-il-tuo-account)).

La mossa sotto il mouse, o scelta con ↑ ↓, compare come freccia blu sulla scacchiera; un clic o Invio la gioca, e `*` o
la sua stella la segna nel repertorio attivo (vedi [La pagina Aperture](#la-pagina-aperture)). Quando la linea della
partita esce dal database, la scheda dice a quale mossa, e se quella mossa è una **novità**, come conferma una ricerca
per posizione (*Vedi l'ultima posizione nota*). L'albero di un database viene costruito dopo ogni importazione, e di
nuovo in background dopo che vi è stata salvata una partita, con il suo avanzamento e *Annulla*. Le note dei tuoi studi
di teoria accompagnano le mosse, qualunque sia la fonte: una colonna *Teoria*, e *Cosa dicono i tuoi studi* sotto le
mosse (vedi [Studi di teoria](#studi-di-teoria)). *Rapporto su questa posizione*, a destra delle partite, apre il suo
rapporto sull'apertura (vedi [Il rapporto sull'apertura](#il-rapporto-sullapertura)).

### Partite di riferimento
<!-- en: reference-games c45f1d18 -->

La scheda Riferimenti cerca le partite più forti della posizione nei tuoi database, in background: prima le più forti
(secondo l'Elo medio dei giocatori), poi le più recenti; un clic ne apre una. All'inizio dell'apertura, troppe partite
raggiungono la posizione per cercare le più forti: la scheda Albero dice di più. *Tutte le partite della posizione in
«…»* le elenca nel database.

### La scheda Finali
<!-- en: the-endgames-tab 330bcfca -->

La sesta scheda della Scacchiera per lo studio, Alt+6, dice cosa dicono le tablebase della posizione mostrata, quando
restano sette pezzi o meno, oppure otto con un pedone per parte sulla stessa colonna (le tablebase op1 di Lichess);
attenuata oltre, lo dice quando viene scelta. In cima, il verdetto (*Vince il Bianco*, *Patta*, *Vinta per il Bianco,
ma patta per la regola delle cinquanta mosse*), da dove viene e quanto è lontana la fine: le mosse fino al matto fino a
sei pezzi (DTM, come lo chiamano altri programmi), a sette le mosse fino a una cattura o a una mossa di pedone (DTZ), a
otto le mosse fino alla prossima cattura, promozione o matto (DTC). Poi le mosse, raggruppate secondo l'esito che
lasciano al lato che le gioca, le migliori per prime: *Mantieni la vittoria*, *Mantieni la patta*, *Butti via la
vittoria: patta*, *Perdi*… Una mossa che rovina il risultato viene indicata con il perché: la risposta che la confuta e
cosa dicono allora le tablebase (*→ 52…Rxh7 · Vince il Nero: matto in 19 mosse*). Una mossa sotto il mouse mostra la
sua linea principale sulla scacchiera; un clic la gioca, come variante quando la partita è andata diversamente, come
sulla scacchiera. Quando la partita prosegue dalla posizione, l'assistente dice cosa ha fatto la sua mossa, istruito
dalle tablebase (*52.Txh7 perde: dopo 52…Rxh7, le tablebase danno al Nero il matto in 19 mosse. 52.g5 ha mantenuto la
patta teorica (tablebase a 7 pezzi).*), scritto solo a partire dai loro verdetti: lo dice solo quando lo dicono loro.

Le tue tablebase sul disco rispondono per prime (Syzygy, quelle usate dall'aggiudicazione dei tornei e dai finali della
pagina Allenamento: il loro verdetto e le mosse fino a una cattura o a una mossa di pedone, nessuna distanza dal matto);
quelle di Lichess rispondono per le posizioni che mancano loro, una volta autorizzate. La prima volta, la scheda chiede:
per questa posizione, sempre, o non ora (*Sempre* si può cambiare nella pagina Lichess, sotto *Online per l'analisi*).
Solo il FEN della posizione va a tablebase.lichess.ovh, senza account, una richiesta alla volta: la posizione che
guardi, e le linee principali delle sue mosse, prima quelle che rovinano il risultato e le altre al passaggio del
mouse. Ogni risposta viene conservata nel tuo profilo e non viene mai chiesta di nuovo. La scheda Analisi mantiene il
verdetto e la sua distanza, con un link a questa scheda. La scheda è bloccata mentre una delle tue partite si gioca su
Lichess, come la scheda Analisi, e nascosta durante una partita contro un motore fino alla sua fine.

### Ancora sull'esploratore
<!-- en: the-explorer-more 1471aca0 -->

Con le fonti di Lichess nella scheda Albero, **le partite che l'esploratore indica** compaiono sotto le mosse: le
partite dei maestri che raggiungono la posizione (*Partite dei maestri*), o quelle forti giocate su Lichess; un clic
chiede la partita a Lichess e la apre sulla scacchiera, alla posizione che mostrava l'albero.

Con la fonte *Lichess* (tutti i suoi giocatori), *Ogni mossa mese per mese* chiede a Lichess, per ogni mossa elencata,
quante partite l'hanno giocata mese per mese: una linea dei suoi ultimi due anni nella colonna *Per mese*, verde quando
è giocata di più di recente, rossa quando di meno; sotto il mouse, le sue partite negli ultimi sei mesi contro i sei
precedenti. Il riepilogo dice lo stesso della posizione. Costa una richiesta per mossa, quindi resta disattivato finché
non lo chiedi; l'esploratore dei maestri non ha i mesi.

### Il rapporto sull'apertura
<!-- en: the-opening-report 4b7befe4 -->

Il **rapporto sull'apertura** di una posizione riunisce ciò che ne sanno i tuoi database. Si apre da *Rapporto su questa
posizione* della scheda Albero, da una linea della pagina Aperture (il tasto destro su una lacuna: *Rapporto
sull'apertura*) e da Ctrl+K, *Rapporto sull'apertura*; compare al posto della pagina Scacchiera, ed Esc torna alla
posizione. Viene rifatto ogni volta che si apre, in background a bassa priorità, sezione per sezione con il suo
avanzamento e *Annulla*: una frazione di secondo per la maggior parte delle posizioni, circa un secondo per una con
milioni di partite.

In cima: la posizione, la sua apertura (le aperture con nome di Lichess) e le mosse che vi portano; poi le sue fonti,
quelle della scheda Albero: i tuoi database il cui albero delle aperture è costruito (quelli che *I miei database*
somma), con le loro partite; **Maestri (Lichess)** e **Lichess** con le cadenze e i rating della scheda Albero,
spuntati quando la scheda Albero li mostra, o con un clic (chiedono la posizione all'esploratore di Lichess, come fa la
scheda Albero, e richiedono un account collegato); e da dove vengono le valutazioni. *Al repertorio* aggiunge la prima
linea principale al repertorio attivo (Ctrl+Z la toglie); *Stampa…* trasforma il rapporto in un PDF, con il riepilogo
segnato come dell'assistente, e *Pagina HTML* in una pagina a sé le cui linee principali si rigiocano su una scacchiera
(vedi [Esportare e stampare](#esportare-e-stampare)). Seguono cinque sezioni (Ctrl+Pag↓ e Ctrl+Pag↑ passano dall'una
all'altra):

- **Linee**: le mosse giocate dalla posizione con i numeri della scheda Albero (partite, vittorie del Bianco, patte e
  vittorie del Nero, punteggio, Elo medio, performance, ultimo anno) e una valutazione: quella di Lichess, conservata
  offline (vedi [Le valutazioni di Lichess, offline](#le-valutazioni-di-lichess-offline)), altrimenti quella del motore
  dell'analisi (un milione di nodi su un thread, per le prime cinque mosse che non ne hanno una); la sua fonte e la sua
  profondità sotto il cursore. Un clic, o ↑ ↓ e Invio, gioca la mossa nella pagina Scacchiera. Poi le **linee
  principali**: le continuazioni più giocate, finché conservano un quinto delle partite della loro prima mossa, con le
  partite che arrivano alla loro fine e il punteggio del Bianco; un clic ne mostra una nella pagina Scacchiera;
- **Piani e strutture**: le strutture pedonali alla mossa 20 (dieci mosse più avanti per una posizione successiva)
  nelle partite della posizione, come le nominano i fatti dell'assistente, *già sulla scacchiera* quando la posizione ne
  ha già una; e cosa fanno i giocatori nelle quindici mosse successive: le spinte di pedone (una, o due di fila: *b4 poi
  b5*) e i percorsi dei pezzi (come la ricerca Manovra segue un pezzo) giocati in un decimo delle partite o più, tre
  per lato, con la loro quota e il punteggio del Bianco. Un piano che i fatti dell'assistente conoscono ha un nome
  (*L'attacco di minoranza: b4 poi b5*). Sono frequenze, non consigli; richiedono 50 partite, e una posizione con più
  di 20.000 partite viene contata su un campione uniforme di esse. Un clic mostra quelle partite nella lista;
- **Partite chiave**: la più forte (il rating medio dei giocatori più alto), la più recente con 2500 e più di media, e
  la prima partita di una novità ripetuta in seguito, ciascuna con il perché e il piano che gioca; *Apri* la mostra
  nella pagina Scacchiera alla posizione (*Torna al rapporto* riporta indietro);
- **Novità**: lungo le linee principali, le mosse giocate per la prima volta negli ultimi cinque anni dei tuoi database
  e ripetute in seguito, con il loro primo anno, quante volte sono state ripetute e il loro punteggio; e la tua partita
  che lì ha lasciato i tuoi database (i tuoi nomi sono quelli di *Impostazioni › I tuoi nomi*). Il primo anno di una
  mossa viene dagli alberi, che contengono ogni partita che raggiunge la sua posizione nelle loro prime 60 semimosse; la
  tua partita viene confermata da una ricerca della posizione, come la scheda Albero conferma una novità;
- **Riepilogo**: il riepilogo del rapporto fatto dall'assistente, scritto dal suo modello solo a partire dai numeri e
  dai fatti del rapporto (vedi [Il riepilogo del rapporto](#il-riepilogo-del-rapporto)).

Una posizione raggiunta da meno di 50 partite dà le sue mosse e le sue partite, senza piani né strutture (*Aggiungi i
giocatori di Lichess come fonte*). Senza un database il cui albero copra le sue partite, il rapporto è fatto con i
maestri di Lichess, con *Ricostruisci l'albero di …* e *Importa un database*. Offline, rispondono i tuoi database e le
valutazioni conservate offline, e le fonti di Lichess mostrano gli ultimi numeri che il rapporto ha conservato per la
posizione, datati. Senza né un motore né valutazioni di Lichess conservate offline, le valutazioni restano vuote e il
riepilogo non ne dice niente (*Scarica Gaia…*). Mentre una delle tue partite si gioca in diretta su Lichess, il rapporto
è bloccato, e torna da solo alla sua fine; sulla posizione di una partita per corrispondenza, le linee e i numeri dei
tuoi database restano (Lichess permette un libro e un database), mentre le valutazioni, l'esploratore di Lichess e il
riepilogo aspettano. Non esce niente dalla macchina tranne ciò che invia la scheda Albero: le fonti Lichess spuntate.
La stampa del rapporto e la sua pagina HTML arriveranno in seguito.

### Il riepilogo del rapporto
<!-- en: the-reports-summary a859f9a6 -->

Il riepilogo è scritto per brani: la struttura pedonale, i piani di ciascun lato, le mosse giocate, la linea principale,
le valutazioni. Il modello dell'assistente scrive ciascuno solo a partire dai numeri di quel brano, vincolato da una
grammatica che gli permette di nominare solo le mosse della posizione, le linee del rapporto, le loro case e i loro
numeri; poi il verificatore legge ogni frase: un numero deve essere uno di quelli che il rapporto dà per ciò che la
frase nomina, una mossa una della posizione o delle sue linee, un piano o una struttura uno di quelli che il rapporto
ha e del lato che dice, e la frase non deve dare consigli. Un brano rifiutato viene riscritto una volta; rifiutato due
volte, resta il brano predefinito, e il cursore sopra di esso dice il perché. *Su cosa si basa* elenca i numeri, i
fatti e le valutazioni usati; una scheda dice cosa ha rifiutato il verificatore. *La versione predefinita* mostra il
riepilogo predefinito, *Riscrivi* lo fa scrivere al modello in un altro modo, *Copia* lo copia. Senza il modello (non
scaricato, un altro file GGUF, o mentre un torneo su questa macchina occupa il processore), le frasi predefinite lo
scrivono a partire dagli stessi numeri, in inglese o in francese. Sul processore, un riepilogo richiede qualche secondo
per brano.

### Il resoconto di una partita
<!-- en: the-review-of-a-game 7c5c4437 -->

Sulla Scacchiera per lo studio, la partita di un torneo mostra in cima alla scheda *Resoconto* cosa pensavano i suoi due
motori, appena viene aperta e senza alcuna analisi: le probabilità del Bianco lungo la partita secondo ciascun motore
alle sue mosse (dalla quota di vittorie, patte e sconfitte che ha dato, invertita per il Nero, altrimenti dalla sua
valutazione convertita come fa il resoconto), il libro in ombra, i tratti in cui i due motori vedevano partite diverse
colorati; sotto, il tempo di ogni mossa, quello del Bianco sopra la linea e quello del Nero sotto; il tempo, la
profondità media e la velocità di ciascun motore. Poi i suoi momenti, ciascuno a un clic: come ciascun motore vedeva la
partita all'uscita dal libro, dove non erano d'accordo, dove le tablebase sono entrate in una ricerca, dove è stato
visto un matto, e come è finita la partita. Una volta analizzata la partita, la curva del resoconto viene disegnata
sopra le loro, più spessa.

Sulla Scacchiera per lo studio, la scheda *Resoconto* accanto alla notazione legge l'analisi della partita (Alt+G) come
farebbe un allenatore:

- in alto, le probabilità del Bianco lungo la partita, con i momenti chiave come punti (scuri per una buona mossa,
  arancioni per un errore); un clic porta alla mossa;
- la teoria della partita in uno dei tuoi database (quello della scheda Albero, altrimenti il primo): le sue mosse di
  libro non vengono giudicate, e la sua novità viene indicata; il libro si ferma prima del primo errore che il motore
  vede. Nemmeno le mosse giocate dal libro di aperture di un torneo (`{book}` nel PGN) vengono giudicate;
- **Momenti chiave**: le mosse dei punti, ciascuna con il suo verdetto (un errore con le probabilità che ha perso e la
  mossa migliore, una mossa unica con la seconda scelta del motore: *Unica mossa. Gaia: +0.62, la seconda scelta
  +0.31.*), e dove inizia il finale;
- **I momenti chiave, in parole**, **La storia della partita** e **Su cosa lavorare**, per ciascun lato, con le mosse
  che lo mostrano; un clic porta alla mossa.

La stessa partita riceve sempre lo stesso resoconto: le stesse valutazioni, gli stessi simboli, momenti chiave e
commenti, qualunque cosa stia facendo la macchina. L'analisi della partita della Scacchiera per lo studio conta i nodi
anziché il tempo: ogni posizione viene cercata per 20.000 nodi al primo passaggio, quattro volte di più a ogni
passaggio, fino alla scelta accanto al pulsante (320.000, 1.280.000, 5.120.000 o 20.480.000 nodi per posizione, o
*fino all'arresto*), su un thread e con una tabella hash vuota, così che il risultato di una posizione dipenda solo dal
motore e dalla posizione. Vengono cercate più posizioni insieme, ciascuna con un processo del motore: tante quanti sono
i thread dell'analisi, o la metà dei core del processore fino a quattro quando sono di più; meno mentre su questa
macchina si gioca un torneo, che tiene i suoi core. Dove la mossa giocata era la scelta del motore e la posizione non è già
decisa, una seconda ricerca della posizione con due linee dà la seconda scelta: una mossa unica viene indicata insieme
a essa.

Le posizioni del finale con sette pezzi o meno possono essere giudicate esattamente dalle tablebase di Lichess,
interrogate una volta per questa partita o sempre. *Commenta i momenti chiave* scrive un commento dell'assistente su
ciascuno, in background, accanto al commento che una mossa può già avere (l'analisi di Lichess, il tuo, quello di un
annotatore), che non viene mai sostituito; *Annota la partita* scrive le valutazioni, i simboli e le linee migliori
nella notazione. Ctrl+Z annulla l'uno o l'altro.

**La storia della partita** viene scritta dal modello dell'assistente quando c'è, in background, a partire dai fatti
del resoconto: i suoi verdetti e le sue valutazioni, i momenti chiave, l'apertura, il finale e come è finita la
partita, con ciò che hanno detto le tablebase di Lichess. Ogni brano è vincolato a quei fatti dal verificatore dei
commenti: ogni mossa, valutazione e nome che cita deve venire dal resoconto. Un brano rifiutato viene riscritto;
rifiutato due volte, lascia il posto a quello predefinito. Una scheda del colore dell'assistente dice che la storia
l'ha scritta il modello e che è stata verificata, e cosa ha rifiutato il verificatore. *La versione predefinita*
mostra al suo posto la storia dei testi predefiniti, *Riscrivila* la fa raccontare al modello in un altro modo (anche
Ctrl+K), *Copiala* copia la storia mostrata. Senza il modello (non scaricato, o un altro file GGUF che non ha imparato
i commenti di RookStudio), o mentre un torneo su questa macchina occupa il processore, la storia è quella dei testi
predefiniti, come indica. Niente esce dalla macchina.

### Ogni mossa spiegata
<!-- en: every-move-explained 52f41ee2 -->

*Spiega ogni mossa*, nella scheda *Resoconto* (o Ctrl+K), dà a ogni mossa della partita una riga, nella scheda
*Spiegazioni* accanto alle mosse; senza resoconto, la partita viene prima analizzata (Alt+G), e le sue mosse spiegate
una volta finito il resoconto. Le righe arrivano in background, ciascuna appena è scritta, con quante ne sono fatte e
*Annulla*; quelle scritte restano, e *Spiega il resto* le completa più tardi.

- **La teoria** è raggruppata: una riga per ogni nome di apertura raggiunto dalle sue mosse, tre al massimo, con le
  partite dei tuoi database che hanno raggiunto la sua ultima posizione (*teoria, 61.204 partite dei tuoi database*).
- **Una mossa troppo semplice per dire di più** è detta in una parola: *Forced recapture.*, *The only legal move.*
- **Le altre** ricevono una frase del modello dell'assistente, scritta a partire dai fatti della posizione, con la sua
  grammatica che gli permette di nominare solo mosse legali, e ogni frase letta dal verificatore; una frase che il
  verificatore rifiuta viene sostituita da quella predefinita, e passando il mouse su *frase predefinita: il
  verificatore ha rifiutato la frase del modello* si vede il perché. Senza il modello, le scrivono le frasi
  predefinite; quando non hanno niente da dire, lo fanno le parole del resoconto stesso (*The engine preferred
  14…Bf5.*).

Ogni riga dice da dove viene: teoria, il modello, una frase predefinita, una parola, il resoconto. *Ogni mossa*, *Le
mosse che contano* (le mosse segnate, le uniche mosse, i punti di svolta, la novità) e *I momenti chiave* scelgono
quali mostrare; un clic va alla mossa. Il riepilogo le conta e dà il tempo che richiede una mossa, tra fatti raccolti
e frase del modello verificata: circa un quarto di secondo per la frase su una GPU, circa 5 secondi per mossa su un
processore condiviso con altro lavoro (32 mosse spiegate in un minuto e mezzo). Sono conservate con il resoconto,
accanto alle mosse, mai nella partita: *Scrivile nella partita* le rende commenti dell'assistente, segnati come suoi
nel PGN, accanto al commento che una mossa ha già, che non viene mai sostituito; Ctrl+Z li toglie tutti. Niente esce
dalla macchina. Bloccate mentre una delle tue partite si gioca su Lichess, nascoste durante una partita contro un
motore fino alla sua fine.

### La visita guidata e «Perché no?»
<!-- en: the-guided-visit-and-why-not f67b11ea -->

*Visita guidata*, nella scheda *Resoconto* (o Ctrl+K), ti accompagna attraverso i momenti chiave del resoconto uno
dopo l'altro (le sue mosse segnate quando non ne ha): la scacchiera mostra la posizione prima della mossa della
partita, e chiede *Prima di vedere il perché: cosa avresti giocato?* Gioca la mossa sulla scacchiera o digitala; niente
viene mostrato prima della tua risposta, né la valutazione, né una freccia, né la notazione della partita, e Spazio
dice *Non lo so*. Poi la tua risposta viene giudicata come *Indovina le mosse* giudica un tentativo (vedi [Indovinare
le mosse](#indovinare-le-mosse)): la mossa del motore 3 punti, una mossa altrettanto buona 2, una giocabile 1,
altrimenti 0, secondo il motore dell'analisi, un secondo per ciascuna; la scacchiera mostra la tua mossa in blu, quella
del motore in verde e quella della partita in arancione, con una spiegazione che si attiene ai fatti del resoconto e
ciò su cui si basa (il motore del resoconto e la sua linea, i fatti delle posizioni). Invio va al momento successivo,
*Vedi la linea* gioca la linea del motore sulla scacchiera, Esc ferma. Una risposta sbagliata, nessun punto o *Non lo
so*, diventa una scheda del ripasso di oggi, come i tuoi errori (vedi [I tuoi errori come
esercizi](#i-tuoi-errori-come-esercizi)): la posizione, con la mossa del motore da trovare.

**Perché no?** (Alt+Q) pone la domanda *Confronta* dell'assistente (vedi [Domande](#domande)) con un tasto, ovunque
compaia una mossa: dopo una mossa provata sulla scacchiera (una variante, come sempre), mette a confronto la mossa con
quella della partita; sulla mossa stessa della partita, la migliore del resoconto con essa; il tasto destro su una
linea di un motore nella scheda Analisi, o su una mossa della scheda Albero, propone *Perché non …?*; nella visita
guidata, *Perché non un'altra mossa?* mette a confronto la tua risposta, o la mossa della partita, con quella del
motore. La risposta arriva nella scheda Domande (nella visita, sotto di essa): il motore dopo ciascuna delle due mosse
(un thread, 300.000 nodi per posizione), i fatti di entrambe le posizioni, le partite e il punteggio dei tuoi database,
la continuazione; *Aggiungi alla notazione* la scrive, come per qualsiasi domanda. Nessuna conversazione libera:
l'assistente risponde ciò che stabiliscono il motore, i tuoi database e i fatti. Entrambe sono bloccate mentre una
delle tue partite si gioca su Lichess, e nascoste durante una partita contro un motore fino alla sua fine.

### Esportare e stampare
<!-- en: exporting-and-printing 47d16e05 -->

*Esporta…* sotto la Scacchiera per lo studio, accanto a *Copia FEN* e *lichess.org…*, apre il pannello Esporta al posto
della pagina; Esc vi torna. I suoi formati sono dei chip in cima, con quello scelto l'ultima volta già scelto
(<kbd>Ctrl</kbd>+<kbd>Pag↓</kbd> e <kbd>Ctrl</kbd>+<kbd>Pag↑</kbd> li scorrono); a sinistra mostra il file come sarà, a
destra cosa ci va dentro, e ogni formato conserva le sue impostazioni per la volta successiva. Invio salva: la finestra
di dialogo del sistema si apre nella cartella scelta l'ultima volta. Tutto è fatto da RookStudio, sul tuo disco: niente
viene inviato da nessuna parte (condividere una partita su lichess.org spetta a *lichess.org…*, vedi [Posizioni e
partite verso Lichess](#posizioni-e-partite-verso-lichess)).

- **Pagina HTML**: un solo file con tutto dentro, che si apre senza Internet, anche su un telefono, e si può inviare
  così com'è. La scacchiera con i colori e i pezzi dell'app, le mosse, i loro commenti e le varianti; un clic su una
  mossa la mostra, anche in una variante, con le frecce e le case disegnate a quella mossa; <kbd>←</kbd> <kbd>→</kbd>
  <kbd>Inizio</kbd> <kbd>Fine</kbd> scorrono le mosse e <kbd>F</kbd> ruota la scacchiera. Ciò che contiene si spunta:
  i commenti, le varianti, le frecce e le case, i commenti dell'assistente (segnalati come suoi), le valutazioni e la
  loro curva, gli orologi. Il suo peso viene stimato prima di salvarla. Dalla pagina Database, o dalla pagina
  Scacchiera quando la sua partita è stata aperta dalla lista di un database, la pagina contiene questa partita o le
  partite del filtro, scelte in un elenco in cima (<kbd>Pag↑</kbd>, <kbd>Pag↓</kbd>); oltre 200 partite sarebbe troppo
  pesante da aprire, e il pannello chiede: una pagina per partita in una cartella, con il loro elenco (`index.html`), o
  le prime 200.
- **Diagramma, PNG o SVG**: la posizione mostrata, con le frecce e le case disegnate a quella mossa (il tasto destro),
  le coordinate, un punto accanto al bordo del lato al tratto, il Nero in basso quando tocca al Nero (se spuntato),
  l'ultima mossa (se spuntato) e una didascalia sotto la scacchiera, che inizia come *Muove il Bianco* e si scrive come
  vuoi (*Il Bianco muove e vince*). Con i colori dell'app, o in bianco e nero per la stampa. Il PNG è largo 480 pixel,
  o il doppio per uno schermo ad alta densità; l'SVG non ha dimensione e si disegna nitido a qualsiasi dimensione.
  *Copia l'immagine*, o <kbd>Ctrl</kbd>+<kbd>Maiusc</kbd>+<kbd>C</kbd> da qualsiasi punto della pagina Scacchiera (e
  *Copia il diagramma* nella tavolozza dei comandi), mette il PNG negli appunti, per un messaggio o un documento.
- **GIF animata**: la partita con i colori dell'app, da una mossa a un'altra (*Da … fino a …* per un tratto), un
  fotogramma per mossa alla velocità scelta, l'ultimo tenuto più a lungo; i nomi dei giocatori e il risultato in una
  fascia, la mossa sotto la scacchiera, l'ultima mossa evidenziata e le frecce della partita, come spuntato. Viene
  creata in background, con il suo avanzamento e *Annulla*, e il suo peso stimato prima: sopra i 2 MB, il pannello dice
  che è pesante per un messaggio o un forum, e propone un tratto o una dimensione più piccola.
- **Stampa · PDF**: un PDF creato da RookStudio, identico su ogni sistema, con l'anteprima che è la pagina esatta.
  *Apri per stampare* (Invio, <kbd>Ctrl</kbd>+<kbd>P</kbd>) lo passa al lettore del sistema, che stampa; *Salva il
  PDF…* lo conserva dove scegli. La carta è A4 o Letter, secondo la lingua e il paese del sistema, o come scelto. Una
  partita si stampa su una o due colonne, con le intestazioni in cima alla pagina, con diagrammi ai suoi momenti chiave
  (gli errori del resoconto, altrimenti le mosse segnate !!, ??, ? e !), a ogni commento con frecce o case (o `[#]`),
  ogni 10 mosse, o nessuno; i commenti dell'assistente e le valutazioni accanto alle mosse quando spuntati. Il suo testo
  si può selezionare e copiare: i suoi caratteri sono incorporati, Inter, e il carattere cinese del sistema quando la
  pagina ha caratteri cinesi. Quando il sistema non ha un lettore di file PDF, il PDF viene salvato e si apre la sua
  cartella.
- **PGN**, dalla pagina Database: la partita scelta o le partite del filtro. *Scrivi i nomi nelle partite*
  (disattivato all'inizio, le partite come sono state lette) scrive il Bianco e il Nero come li ha la tabella dei nomi
  del database (vedi [Nomi dei giocatori](#nomi-dei-giocatori)), con gli ID FIDE e i titoli dei giocatori quando li
  conosce; il database stesso non cambia.

Ciò che un'esportazione mostra nomina i giocatori come fa la tabella dei nomi, come li mostra la lista delle partite.
Le pagine HTML e i PDF finiscono con l'avviso dei pezzi disegnati (*Cburnett*, licenza BSD 3-clause; *Chessnut*,
Apache 2.0; *Celtic* e *Fantasy*, MIT); il PNG di un diagramma lo porta nel suo testo, il suo SVG in un commento.

*Stampa…* c'è anche dove c'è qualcosa da leggere su carta: un repertorio nella pagina Aperture (ogni linea sotto quella
da cui si separa, un diagramma a ogni diramazione, le tue note, e la sua copertura e le sue lacune in appendice, quando
misurate; un repertorio vuoto non ha niente da stampare, prima metti una stella alle sue mosse nella scheda Albero), il
dossier di un giocatore, un rapporto sull'apertura e la tabella incrociata di un torneo (su pagine orizzontali, con
tutti i giocatori). Il dossier, il rapporto e la tabella incrociata hanno anche *Pagina HTML*: lo stesso contenuto come
pagina a sé, con le loro linee rigiocabili su una scacchiera accanto al testo. La Scacchiera dello spazio Motori
mantiene le sue esportazioni (*Copia PGN*, *Salva la partita in PGN…*).

## Database
<!-- en: databases e5980ed0 -->

La pagina **Database** contiene i tuoi database di partite: il loro elenco a sinistra, le partite di quello scelto a
destra. Un database è una cartella a sé (`Name.fcgdb`), letta velocemente anche con milioni di partite.

### Importare partite
<!-- en: importing-games 8ebdd041 -->

*Nuovo database* ne crea uno, con il nome scritto sul posto; *Aggiungi un database…* elenca una cartella di database
creata altrove, con i suoi file lasciati dove sono. *Importa in questo database…* prende un file PGN, o un file `.zst`,
`.gz` o `.zip` che ne contiene uno; un file trascinato sulla pagina va nel database scelto, o in uno nuovo con il suo
nome, e *Incolla partite* importa il testo PGN degli appunti. L'importazione gira in background a bassa priorità (un
torneo accanto non perde tempo), con il suo avanzamento, la sua velocità e *Annulla*. Le partite registrate restano se
si interrompe (ogni 262.144 partite, e alla fine), e importare di nuovo il file aggiunge solo quelle mancanti; le
partite già presenti nel database vengono escluse.

Alla fine, un riquadro sul database dice cosa è arrivato: le partite aggiunte, quelle già presenti e, tra le partite
aggiunte, quelle danneggiate (una mossa illeggibile o illegale, un testo troncato, una posizione iniziale non valida),
ciascuna conservata fino a quella mossa con il suo testo com'era; *Vedile nella lista*, o un clic su una di esse,
mostra il file alla sua riga. Le partite già presenti da un'altra fonte, la stessa partita con le intestazioni scritte
in modo diverso, vengono segnalate con *Fondile* e *Prima vedile* (vedi [Partite duplicate](#partite-duplicate)). Un
disco pieno, un'interruzione richiesta o un altro errore dicono cosa viene conservato
e cosa serve, con *Riprendi l'importazione* e *Apri la cartella del database*; un file di cui non si può proseguire la
lettura dice perché, e le partite lette prima restano. Poi viene costruito l'albero delle aperture del database, in
background.

Un file i cui byte non sono testo (un programma, un'immagine, un archivio che l'importazione non conosce) viene
rifiutato prima di essere letto come PGN: non viene importato niente, e un nuovo database creato per esso non viene
conservato.

Un database in cui un altro programma sta scrivendo (un altro RookStudio che vi importa, per esempio) si apre in sola
lettura: la lista, l'albero e le ricerche funzionano; l'importazione e il salvataggio di partite aspettano che quel
programma abbia finito. *Togli dalla lista* lascia i file di un database dove sono.

### La lista delle partite
<!-- en: the-game-list 7dc68173 -->

La lista dà per ogni partita il numero, i giocatori, l'Elo, il risultato, l'evento, la data, il codice ECO e la
lunghezza; un clic su un'intestazione di colonna la ordina. Le grafie di un giocatore unite nella tabella dei nomi del
database (vedi [Nomi dei giocatori](#nomi-dei-giocatori)) compaiono sotto il nome del giocatore, e sotto il mouse il
nome dice come lo scrive la partita. Il filtro rapido in alto accetta parole libere: giocatori, evento, anno, codice
ECO, risultato (`carlsen 2024 B90`). ↑ ↓ scorrono la lista e la partita scelta compare accanto, su una piccola
scacchiera con le sue mosse; Invio o un doppio clic la apre nella pagina Scacchiera. Milioni di partite si scorrono e
si ordinano all'istante. **Partite** e **Tornei**, accanto al nome del database sopra la lista, passano dalle sue
partite ai suoi tornei (vedi [Tornei e tabelle incrociate](#tornei-e-tabelle-incrociate)). *Esporta…*, dopo il
conteggio delle partite, trasforma la partita scelta o le partite del filtro in una pagina da rigiocare, una GIF
animata, un PDF o un file PGN, con i nomi dei giocatori della tabella dei nomi scritti al suo interno se richiesto (vedi
[Esportare e stampare](#esportare-e-stampare)).

### Schede FIDE
<!-- en: fide-cards 20b6e988 -->

La **scheda FIDE** di un giocatore (titolo, federazione, anno di nascita, ID FIDE, Elo standard, rapid e blitz) viene
chiesta a Lichess, che conserva la lista FIDE, senza account e solo quando lo chiedi: *Scheda FIDE di …*, nel menu del
clic destro su un nome della lista delle partite o nell'anteprima della partita. Una partita che dà l'ID FIDE del
giocatore (*WhiteFideId*, come fanno le trasmissioni di Lichess) invia quell'ID; altrimenti viene inviato il nome, e
quando nella lista FIDE ci sono più giocatori con quel nome, scegli tu quale è. La scheda viene conservata nel tuo
profilo per quel nome: compare poi passando il mouse sul nome, e nell'anteprima. *Su lichess.org* apre lì la pagina del
giocatore, *Dossier di …* il suo dossier (vedi [Il dossier di un giocatore](#il-dossier-di-un-giocatore)). Un
giocatore associato alla lista Elo FIDE nella tabella dei nomi del database mostra invece la scheda della lista, senza
chiedere a Lichess.

### Il dossier di un giocatore
<!-- en: a-players-dossier 3e1a2bc7 -->

Il **dossier** di un giocatore riunisce ciò che ne sanno i tuoi database. Si apre dal suo nome: *Dossier di …* nel menu
del clic destro su un nome della lista delle partite, nell'anteprima di una partita o su una scheda FIDE; Ctrl+K,
*Dossier: Duval*; *Prepara una partita contro* della pagina Lichess (vedi [Preparare una
partita](#preparare-una-partita)). Compare al posto della pagina, ed Esc torna indietro. Viene rifatto ogni volta che si
apre, dai tuoi database così come sono, in background a bassa priorità con il suo avanzamento e *Annulla* (al massimo
qualche secondo, anche per un giocatore con migliaia di partite tra milioni); non ne viene conservato niente.

Le sue partite vengono trovate tramite la tabella dei nomi di ogni database (vedi [Nomi dei
giocatori](#nomi-dei-giocatori)): tutte le grafie di un giocatore insieme, in ogni tuo database tranne quelli delle
partite dei giocatori di Lichess (*Lichess · nome*: quello del suo account è la fonte Lichess). Un giocatore associato
alla lista Elo FIDE viene distinto dai suoi omonimi grazie all'ID FIDE che danno le partite, e le partite giocate prima
che potesse giocare vengono escluse. Quando la lista FIDE ha più giocatori con quel nome, il dossier chiede quale,
proponendo per primo quello il cui rating corrisponde alle sue partite: ↑ ↓ scelgono, Invio conserva la scelta nelle
tabelle dei nomi, e il dossier prende allora solo le partite di quel giocatore (Ctrl+Z annulla; *Non ora* mostra ogni
partita con quel nome).

In cima: il titolo, la federazione, l'anno di nascita e gli Elo FIDE del giocatore quando è associato alla lista, e di
cosa è composto: i tuoi database, con le loro partite e i loro anni, e Lichess quando la sua fonte è spuntata. *Le sue
partite nella lista* le mostra nella pagina Database; *Stampa…* trasforma il dossier in un PDF di poche pagine,
*Pagina HTML* in una pagina a sé le cui linee si rigiocano su una scacchiera (vedi [Esportare e
stampare](#esportare-e-stampare)). Poi quattro schede (Ctrl+Pag↓ e Ctrl+Pag↑ passano dall'una all'altra):

- **Panoramica**: le sue prime mosse con il Bianco e il suo punteggio, le sue risposte con il Nero alle prime mosse
  che incontra di più, il suo rating negli anni, i suoi risultati contro giocatori più forti e più deboli, le sue
  strutture pedonali, e le tue partite contro di lui (i tuoi nomi sono quelli di *Impostazioni › I tuoi nomi*); poi le
  sue ultime partite. Un clic ne apre una nella pagina Scacchiera con il dossier come lista: Ctrl+← e Ctrl+→ scorrono
  le sue partite, e *Torna al dossier* riporta indietro;
- **Aperture**: le sue linee con il Bianco e con il Nero, per codice ECO, ciascuna con le sue partite, le sue vittorie,
  patte e sconfitte, il suo punteggio e l'ultimo anno in cui l'ha giocata. Un clic apre la linea nella pagina
  Scacchiera, con la scheda Albero solo sulle sue partite, dalla sua parte (*Duval, Anne con il Bianco*). Sotto, le
  lacune dei tuoi repertori contro di lui: le linee che gioca tre volte o più e che i tuoi repertori lasciano senza
  risposta, le più frequenti per prime;
- **Rating e strutture**: il suo rating anno per anno, la media degli Elo che danno le sue partite classiche in ogni
  anno (nessuna curva sotto i tre anni), il suo più alto e cosa dice la lista FIDE; le sue strutture pedonali alla
  mossa 20, come le nominano i fatti dell'assistente (nessuna sotto le cinque partite), con un clic che mostra quelle
  partite nella lista con la ricerca Struttura che parte dai suoi pedoni; e quando hai fatto il resoconto di alcune sue
  partite (Alt+G), la sua precisione in ogni fase e dove cadono i suoi errori netti;
- **Lichess**, con la sua fonte: cosa gioca a tavolino e online a confronto, il suo livello online, le tue partite
  contro di lui su entrambi, e le lacune dei tuoi repertori su entrambe le fonti.

*+ Lichess…* collega il suo account Lichess al giocatore, con il collegamento conservato nelle tabelle dei nomi, e
spunta la fonte: le sue partite dell'ultimo anno arrivano come per una partita preparata contro di lui; un clic sul chip
della fonte la toglie, o la rimette. Sotto le venti partite, il dossier dice che dà indizi più che tendenze. Per un
giocatore senza partite nei tuoi database lo dice, con *Da una fonte…* e *+ Lichess…* per portarne qualcuna. Niente
esce dalla macchina, tranne, con la fonte Lichess spuntata, ciò che invia la preparazione di una partita: il suo nome
con i filtri delle sue partite, e il suo profilo pubblico. Offline, la fonte mostra le partite della sua ultima
sincronizzazione, datate. Mentre giochi contro di lui su Lichess, il dossier non mostra niente di lui.

### Tornei e tabelle incrociate
<!-- en: tournaments-and-crosstables 208330d7 -->

*Tornei*, accanto a *Partite* sopra la lista di un database, mostra i suoi tornei: le partite di un evento, del suo
luogo e del suo anno (un torneo da dicembre a gennaio resta uno solo; lo stesso evento in due luoghi, con giocatori
propri, ne fa due). Le partite online, che non hanno in comune nessun evento con i turni indicati, non sono in nessuno:
un database di queste partite dice che non ha tornei e propone *Partite*. Ogni torneo indica il suo sistema, i suoi
giocatori, i suoi turni, l'Elo medio dei suoi giocatori e le sue partite, *286 di 288* in arancione quando mancano
partite. L'elenco è ordinato per Elo medio; un clic su un'intestazione lo ordina diversamente, e il suo filtro (Ctrl+L)
accetta parole dell'evento, del luogo o dell'anno. L'elenco di un grande database viene fatto in background con il suo
avanzamento (qualche secondo per dieci milioni di partite), e conservato finché il database non cambia.

Il sistema viene dedotto dalle partite: un girone all'italiana quando ognuno ha giocato con ognuno (un doppio girone
all'italiana quando ogni coppia si è incontrata due volte), altrimenti svizzero; *?* quando non è sicuro (una coppia si
è incontrata più spesso di quanto permetta il suo sistema, partite mancanti in un gruppo piccolo come quello di un
girone all'italiana). I tornei a squadre e le eliminazioni dirette vengono indicati, con le loro tabelle incrociate in
arrivo più avanti. Un clic sul sistema lo cambia, e la scelta viene conservata per il torneo.

Un clic su un torneo, o Invio, apre la sua **tabella incrociata** al posto dell'elenco, ed Esc torna indietro; anche
Ctrl+K ne apre una (*Tabella incrociata: Open d'autunno*). I suoi giocatori sono quelli della tabella dei nomi del
database (vedi [Nomi dei giocatori](#nomi-dei-giocatori)): tutte le grafie di un giocatore insieme. Le partite arrivate
due volte da due fonti (gli stessi giocatori con gli stessi colori, turni che possono essere uno solo) vengono contate
una volta, e il turno di ogni partita viene letto dal suo tag Round (*9.2*: turno 9, scacchiera 2).

- La tabella incrociata di un girone all'italiana ha una cella per ogni avversario: 1, ½ o 0 (un risultato per ogni
  ciclo in un doppio girone all'italiana), + e − per un forfait; poi i punti, gli spareggi, le vittorie, la performance
  e la variazione di rating.
- Quella di uno svizzero ha i numeri di partenza, per rating, poi una cella per ogni turno: *4b½* è una patta contro il
  numero 4, con il Nero; *riposo*, l'unico giocatore non abbinato nel turno (i punti di una vittoria, la regola FIDE per
  un riposo assegnato dall'abbinamento); *–*, un turno senza partita. Compaiono i primi dieci, con le loro partite tra loro
  nel colore d'accento; *Tutti i giocatori* mostra gli altri.

Un clic su una partita la apre nella pagina Scacchiera, con le partite del torneo come lista (Ctrl+← e Ctrl+→, *Torna
alla tabella incrociata*); un clic su un nome apre il dossier del giocatore (vedi [Il dossier di un
giocatore](#il-dossier-di-un-giocatore)). La performance e la variazione di rating dicono sotto il mouse come sono
state calcolate. *Stampa…* trasforma la tabella incrociata in un PDF su pagine orizzontali, con tutti i giocatori, e
*Pagina HTML* in una pagina a sé (vedi [Esportare e stampare](#esportare-e-stampare)).

Sopra la tabella incrociata, una riga per ogni problema dei dati, con ciò che lo risolve:

- **partite mancanti** (una coppia di un girone all'italiana che non si è incontrata, due giocatori di uno svizzero
  senza partita in un turno) vengono contate «?», e i punti, gli spareggi e le variazioni di rating che le leggono
  dicono «?». *Cercale negli altri tuoi database* le trova sotto altre grafie, *Copiala in questo torneo* ne aggiunge
  una, con evento, luogo e giocatori scritti come quelli del torneo (Ctrl+Z la toglie di nuovo); *Inserisci i risultati
  a mano* dà a ciascuna il suo risultato, come fa un clic su un «?», conservato con il torneo nel tuo profilo, con il
  database invariato;
- **un giocatore con due nomi**: due nomi che possono essere di un solo giocatore, mai abbinati tra loro, con i loro
  turni separati; *Uniscili* li unisce nella tabella dei nomi (Ctrl+Z annulla);
- **partite arrivate due volte**: *Fondile* tiene ciascuna come una sola partita, con i campi più completi di ogni
  fonte, come fanno i duplicati della manutenzione (Ctrl+Z annulla); quelle le cui mosse o i cui risultati differiscono
  vengono invece mostrate nella lista;
- **un forfait**: un risultato decisivo senza mosse (un PGN non ha una parola per indicarlo) conta nei punti, non nella
  performance né nella variazione di rating;
- **un sistema incerto**: un clic lo sceglie.

Anche una trasmissione seguita ha la sua tabella incrociata: *Tabella incrociata*, accanto alla classifica nella
colonna del mosaico, la mostra al posto delle scacchiere (*Il mosaico*, o Esc, le riporta). È fatta dei turni che
RookStudio segue e di quelli che ha conservato nel database *Trasmissioni*: ● una partita in corso (un clic la segue
nella pagina Scacchiera), · una partita in arrivo, i punti, le partite giocate e la variazione di rating finora. La
classifica di Lichess resta accanto, e per essa non viene chiesto altro a Lichess; una trasmissione a squadre mantiene la
classifica a squadre di Lichess.

### Spareggi, performance e variazione di rating
<!-- en: tie-breaks-performance-and-rating-change 18737372 -->

Gli spareggi ordinano i giocatori a pari punti, uno dopo l'altro: quelli della FIDE per impostazione predefinita, con la
riga sopra la tabella che dice quali. Il regolamento FIDE attuale sugli spareggi (C.07, in vigore dal 1° marzo 2026) non
ne raccomanda nessuno per un sistema, quindi RookStudio prende quelli degli eventi della FIDE stessa: per un girone
all'italiana, come il Torneo dei Candidati 2026, Sonneborn-Berger, le vittorie, lo scontro diretto; per uno svizzero,
come il Grand Swiss 2025, il rating medio degli avversari cut 1, Buchholz cut 1, Buchholz, lo scontro diretto (senza il
rating medio quando un giocatore non ha rating, come chiede il C.07). Un clic su uno spareggio, in quella riga o sulla
sua intestazione, lo mette al primo posto, lo sposta, lo sostituisce o lo toglie; *+* ne aggiunge uno; *Quelli della
FIDE* rimette i suoi. L'ordine viene conservato per il torneo.

Sono quelli della FIDE: scontro diretto, vittorie (contano i forfait e i riposi che valgono una vittoria), partite
vinte, partite con il Nero, vittorie con il Nero, punteggio progressivo, Buchholz, Buchholz cut 1, Buchholz mediano,
Sonneborn-Berger, Koya, il rating medio degli avversari (anche cut 1) e la performance. In uno svizzero, un turno senza
partita conta nel Buchholz e nel Sonneborn-Berger come dice la FIDE (C.07, articolo 16): come contro un avversario con
lo stesso punteggio del giocatore, al massimo una patta per turno (al massimo il punteggio dell'avversario previsto per
un forfait), e i turni di un giocatore dopo il suo ritiro contano come patte per i suoi avversari.

La **performance** è quella della FIDE: il rating medio degli avversari con rating incontrati sulla scacchiera, più la
differenza che la tabella della FIDE dà per il punteggio in quelle partite (arrotondata al centesimo). La **variazione
di rating** è la formula della FIDE: per ogni partita giocata sulla scacchiera contro un avversario con rating, il
punteggio meno il punteggio atteso dalla tabella della FIDE (una differenza contata al massimo 400, sotto 2650), per K.
K è quello proprio del giocatore dalla lista Elo FIDE quando è associato a essa nella tabella dei nomi e la lista è
dell'anno del torneo o del successivo (vedi [La lista Elo FIDE](#la-lista-elo-fide)); altrimenti lo danno le regole
della FIDE: 40 fino alla fine dell'anno del suo 18° compleanno finché è sotto 2300, 10 da 2400, 20 altrimenti, più
piccolo quando si supererebbero i 700 su tutte le sue partite. Un torneo rapid o blitz segue le regole rapid e blitz
della FIDE. Un forfait conta solo nei punti.

### Cercare
<!-- en: searching fda4d044 -->

*Cerca* (Ctrl+F) apre il pannello di ricerca accanto alla lista, in sei schede (Ctrl+Pag↓ e Ctrl+Pag↑ passano dall'una
all'altra); la lista segue ogni criterio man mano che viene digitato:

- **Intestazioni**: un giocatore e il suo colore, l'avversario, l'evento, gli anni, i codici ECO, l'Elo minimo di
  entrambi i giocatori, il risultato; *Solo partite danneggiate*;
- **Posizione**: la posizione della pagina Scacchiera, o una impostata a mano (*Disponi i pezzi…*): le sue partite
  vengono trovate in background, e quelle che non possono raggiungerla (per i pedoni, per il materiale) vengono escluse
  senza essere rigiocate;
- **Materiale**: una scelta predefinita (finali di torre, senza donne, la coppia degli alfieri contro alfiere e
  cavallo, una donna contro due torri) o i pezzi e pedoni di ciascun lato contati (`2`, o `0-2`), per almeno un certo
  numero di semimosse;
- **Schema**: pezzi e pedoni messi sulla scacchiera con il mouse (un clic mette il pezzo scelto, un altro clic lo
  toglie; Maiusc e un clic, una casa vuota; le altre case, qualsiasi cosa), *Il Bianco ha solo questi pedoni*, tre
  esempi, e condizioni oltre a questi: cosa attacca, difende o inchioda cosa, una linea di mosse (vedi
  [Schemi](#schemi));
- **Manovra**: il percorso di un pezzo, come un cavallo che va in b1, d2, f1, g3 (vedi [Manovre](#manovre));
- **Struttura**: i pedoni della pagina Scacchiera, esattamente o a uno o due pedoni di distanza (vedi [Strutture
  pedonali](#strutture-pedonali)).

Ogni ricerca dà le sue partite e il suo tempo; *Aggiungi al filtro* e *Togli dal filtro* le combinano, *Vedi nella
lista* (Invio) mostra il risultato, *Cancella tutto* ricomincia. Le ricerche impostate si sommano: uno schema, e un
materiale, e delle intestazioni. Una ricerca che rigioca le partite (uno schema, un percorso, una struttura) prosegue in
background con il suo avanzamento e *Annulla*; le prime partite che trova sono già nella lista, e una ricerca annullata
le conserva, mentre *Riprendi (solo il resto)* cerca nelle partite che non aveva raggiunto. Mentre nel database è in
corso un'importazione, una ricerca copre le partite già registrate, e viene rifatta da sola alla fine.

La lista mostra poi ogni partita alla posizione trovata, in una colonna *Trovata a* (la mossa che vi ha portato): Invio
o un doppio clic apre la partita a quella mossa, e così fa la partita successiva o precedente della lista dalla pagina
Scacchiera.

*Salva questa ricerca…* la salva con un nome tratto dai suoi criteri (uno schema con il nome del suo predefinito).
Vengono conservati i suoi criteri, non le sue partite, per qualsiasi database (le sue nuove partite vengono trovate la
volta successiva); le ricerche salvate compaiono in cima al pannello, dove un clic ne imposta i criteri e il tasto
destro ne rinomina o ne elimina una, e in Ctrl+K (*Ricerca: …*, o *Schema: …* per uno schema da solo).

### Schemi
<!-- en: patterns 5821a112 -->

La scheda **Schema** dice cosa vale in una posizione: prima i pezzi sulla scacchiera piccola, poi le condizioni, una per
riga, tutte vere insieme. Un clic su una parola di una riga la cambia: i pezzi (un cavallo bianco, la donna nera o una
torre nera, qualsiasi pezzo nero, su delle case o in qualsiasi punto), il verbo (*è in*, *attacca*, *difende*,
*inchioda … contro …*, *è in presa*, *è passato*, e il loro contrario), il lato al tratto, lo scacco, il matto. *+ Una
condizione* ne aggiunge una, × la toglie. *Lo stesso pezzo della riga 1*, nel menu dei pezzi, fa parlare due righe di un
solo pezzo: un cavallo che attacca il re, e lo stesso cavallo che attacca la donna o una torre, è una forchetta.

I predefiniti impostano uno schema intero: *Forchetta di cavallo*, *Inchiodatura sul re*, *Matto del corridoio*,
*Attacco doppio di donna*, *Sacrificio in h7*, *Pedone passato protetto*; sono anche in Ctrl+K (*Schema: Forchetta di
cavallo*), sul database scelto.

*Poi… (una linea)* aggiunge una linea di posizioni dopo la prima: ogni passo una mossa ("l'alfiere bianco cattura in h7
con scacco") o una posizione, alla semimossa successiva o entro un certo numero di semimosse (*ogni passo alla semimossa
successiva*, *entro 3 semimosse*). Una mossa si può anche giocare sulla scacchiera piccola (*+ Un passo*, *una mossa
giocata sulla scacchiera piccola*: un clic sul pezzo, poi sulla sua casa). In ogni partita, la posizione trovata è la
prima dello schema.

Ciò che non si può mai trovare viene detto prima di cercare, e non viene avviato niente: un cavallo in c7 e c7 vuota, il
Bianco e il Nero al tratto, un pedone sulla sua prima traversa; un clic toglie una delle due condizioni. Una ricerca che
trova più della metà delle partite lo dice (*Molto ampia*): *Aggiungi una condizione*, o *Lasciala com'è*.

### Il linguaggio degli schemi
<!-- en: the-pattern-language af783ff9 -->

Lo stesso schema è un testo sotto il costruttore (*Testo*, o Alt+T): cambiare l'uno cambia l'altro, e una ricerca
salvata conserva il suo testo, che si può copiare e condividere. Le sue parole sono quelle di CQL, il linguaggio delle
query scacchistiche:

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

- I pezzi in lettere FEN: K Q R B N P per il Bianco, k q r b n p per il Nero, A e a per qualsiasi pezzo bianco o nero,
  `_` per una casa vuota, `[qr]` per uno tra più pezzi; le case dopo la lettera (`Nf3`, `R[a1,h1]`, `p[a-h7]`), o da
  sole dove una parola vuole delle case (`move to [g5,h5]`).
- `attacks`, `defends`, `pin from … through … to …`, `en prise`, `passed`, `check`, `mate`, `stalemate`, `wtm` e
  `btm` (il lato al tratto), `not`, `or`, `only P` (il Bianco non ha altri pedoni che quelli nominati sulle loro case).
- `line -->` poi un passo dopo ogni freccia, alla semimossa successiva: `move from … to … capture … promote …` e ciò
  che vale dopo la mossa, o una posizione; `.{0,2}` prima di un passo lascia venire prima due semimosse qualsiasi.
- Due nomi (`$n`, `$q`) sono sempre due pezzi; un nome segue il suo pezzo attraverso le mosse di una linea.

Una riga che non si legge viene indicata, con la parola a cui somiglia ("Riga 2: «atacks» non è una parola nota —
«attacks»?"); il resto di CQL viene segnalato come non letto. Il costruttore tiene l'ultima versione leggibile, e non
si cerca niente finché il testo non si legge di nuovo.

### Manovre
<!-- en: manoeuvres adacd213 -->

La scheda **Manovra** cerca il percorso di un pezzo: un clic su un pezzo della scacchiera piccola (la posizione della
pagina Scacchiera), poi un clic su ogni casa in cui va (b1, d2, f1, g3); Backspace toglie l'ultima casa. *Altre mosse
in mezzo* lascia che il lato giochi altre mosse nel frattempo; *Lo stesso pezzo per tutto il percorso* chiede un solo
pezzo, altrimenti qualsiasi pezzo del suo tipo su ogni casa a turno; *In 15 mosse al massimo* conta le mosse del lato
dalla prima del percorso all'ultima. *Da qualsiasi punto*, o dalla *posizione della pagina Scacchiera*: il percorso una
volta che una partita raggiunge quella posizione.

I risultati dicono dove è andato poi lo stesso pezzo (in f5 nel 31 % delle partite), e i percorsi vicini, che si
separano da esso dopo la sua prima mossa (b1–d2–b3); un clic ne imposta uno. La freccia grigia sulla scacchiera è la
continuazione più giocata, non una condizione. Dalla pagina Scacchiera, Ctrl+K *Questa manovra* prende il pezzo sotto il
mouse e le sue mosse nella partita.

### Strutture pedonali
<!-- en: pawn-structures 5c6eb547 -->

La scheda **Struttura** cerca solo i pedoni della pagina Scacchiera, lasciando da parte i pezzi: *Esattamente*, *A un
pedone di distanza* o *A due pedoni di distanza* (un pedone in più, in meno o su un'altra casa, da una parte o
dall'altra, conta come un pedone), *Entrambi i lati*, o solo *I pedoni del Bianco* o *I pedoni del Nero*, *Anche a
colori invertiti*, mantenuta per almeno un certo numero di semimosse. *Riprendi i pedoni della pagina Scacchiera* li
riprende da capo; *Imposta i pedoni…* li cambia casa per casa. Il suo nome compare quando i fatti dell'assistente lo
conoscono (Carlsbad, Maroczy, riccio, Stonewall, le catene di pedoni, Benoni, pedone di donna isolato).

I risultati danno le partite che la mantengono esattamente e quelle vicine, le aperture che la raggiungono di più, e le
varianti a un pedone di distanza con le loro partite e il punteggio del Bianco ("il pedone bianco della colonna b in
b4", "senza il pedone nero della colonna a"); un clic ne imposta una. Dalla pagina Scacchiera, Ctrl+K *Partite con
questa struttura* ne cerca subito i pedoni.

### Tornei e file PGN
<!-- en: tournaments-and-pgn-files 688ddcc2 -->

I tornei del profilo, e il file PGN aperto nella pagina Scacchiera, compaiono sotto *Letti senza copia* dopo i tuoi
database: le loro partite si elencano e si cercano come quelle di un database, aggiornate in background quando vengono
mostrate (le partite che un torneo termina, un file che è cresciuto o cambiato). Non vengono né importati né rimossi.

### Database ChessBase
<!-- en: chessbase-databases 3da99106 -->

Un database ChessBase trascinato sulla pagina Database, o scelto con *Importa un file…*, viene copiato in un database
RookStudio: il suo .cbh o .2cbh (o uno qualsiasi dei suoi file), o un suo archivio, .cbv o .cbz per un .cbh, .2cbv o
.2cbz per un .2cbh. Una scheda dice prima cosa contiene, letto nelle sue intestazioni, cosa entra e cosa resta fuori,
dove va e quanto tempo ci vuole all'incirca; *Importa* (Invio) avvia la copia, *Non ora* (Esc) la lascia stare. I file
ChessBase vengono solo letti, e accanto a loro non viene scritto niente; un archivio viene estratto mentre viene letto.
La password di un .cbz o di un .2cbz si scrive sulla scheda (Invio nel campo avvia la copia) e non viene conservata;
una password sbagliata viene segnalata lì subito, da riscrivere, prima che venga copiato qualsiasi cosa.

La copia conserva le mosse e le loro varianti, i commenti prima e dopo le mosse (un testo in più lingue: prima
l'inglese, poi le altre), i simboli, le case e le frecce colorate, gli orologi e il tempo impiegato, le valutazioni dei
motori, le medaglie (un tag *Medals*), le posizioni critiche e le cadenze. Domande di allenamento, immagini, suoni e
video, link web, partite citate, strutture pedonali e percorsi dei pezzi, colori delle varianti e i testi (articoli)
restano fuori: il rapporto alla fine li conta per tipo. Se lo importi di nuovo (con nuove partite aggiunte in
ChessBase nel frattempo), entrano solo le nuove partite. I nomi dei giocatori vengono scritti come li scrive il
database, «Cognome, Nome».

### TWIC
<!-- en: twic 56b70a1d -->

*Da una fonte…* porta partite da un sito, scaricate solo quando lo chiedi. **TWIC**, The Week in Chess, pubblica ogni
lunedì un numero con le partite della settimana: la sua scheda chiede a theweekinchess.com l'elenco dei numeri e
mostra gli ultimi, con il loro lunedì, le loro partite e se ciascuno è già nel database scelto. Spunta quelli che
vuoi, scegli il database (*Un nuovo database, «TWIC»* all'inizio), poi *Scarica e aggiungi*: il PGN di ogni numero
viene scaricato, poi importato, uno dopo l'altro, con il database leggibile per tutto il tempo; *Annulla* si ferma
dopo ciò che è stato aggiunto. Una partita che il database ha già, con gli stessi tag e le stesse mosse, non viene
aggiunta due volte. Poi un rapporto dà i tornei più forti delle partite aggiunte (secondo l'Elo medio dei loro
giocatori) e i giocatori con l'Elo più alto; un nome apre le sue partite nella lista. Le partite già presenti da
un'altra fonte, per esempio una trasmissione seguita prima, vengono segnalate lì con *Fondile*.

*Ogni lunedì, cerca un nuovo numero* è disattivato finché non lo spunti. Spuntato, RookStudio chiede l'elenco dei
numeri mentre è aperto, al massimo quattro volte al giorno; un nuovo numero compare sotto l'elenco dei database, in
*Fonti seguite*, e nella scheda *I miei database* della pagina Home, con *Aggiungi a «…»*: non viene mai aggiunto
senza di te. Cosa esce dalla macchina: la richiesta dell'elenco dei numeri, poi dei file PGN dei numeri scelti, nient'altro.
TWIC è gratuito per uso personale; niente di esso è distribuito con RookStudio.

### Mesi di Lichess
<!-- en: lichess-months 26060e4f -->

*Da una fonte… › Trasmissioni Lichess* aggiunge un mese delle trasmissioni ufficiali di Lichess (database.lichess.org,
CC BY-SA 4.0): la sua scheda chiede la pagina dei file del sito ed elenca gli ultimi mesi con la loro dimensione e le
loro partite. Scegline uno e il database (*Un nuovo database, «Trasmissioni Lichess»* all'inizio), poi *Scarica e
aggiungi*: il file del mese viene scaricato, con la possibilità di riprendere, poi importato, e un rapporto dà i suoi
tornei più forti e i giocatori con l'Elo più alto. Una partita che il database ha già non viene aggiunta due volte. Le
partite conservano la loro fonte, la pagina della trasmissione nel loro tag Site: ciò che ne condividi viene condiviso
con la stessa licenza. Cosa esce dalla macchina: la richiesta della pagina, poi del file del mese.

*Da una fonte… › Partite Lichess* legge un mese delle partite classificate di Lichess (CC0, circa 95 milioni di
partite e 30 GB compressi) mentre lo scarica, e conserva solo le partite che passano i suoi filtri: per impostazione
predefinita entrambi i giocatori con Elo di 2400 e più, senza bullet (un tempo base più 40 incrementi sotto i tre
minuti). Solo le partite conservate arrivano sul disco, aggiunte al database scelto a blocchi, con il database
leggibile per tutto il tempo. *Leggi un file già scaricato…* legge invece un `lichess_db_standard_rated_*.pgn.zst` dal
disco. *Annulla*, un'interruzione di rete o la chiusura del programma conservano ciò che è stato aggiunto, e *Continua*
riprende da dove si era fermato, con gli stessi filtri; le partite rilette non vengono aggiunte due volte.

### Un giocatore di chess.com
<!-- en: a-chesscom-player 39157248 -->

*Da una fonte… › chess.com* aggiunge le partite di un giocatore dall'API pubblica di chess.com, senza account: scrivi
il suo nome e *Cerca* chiede i mesi in cui ha giocato; *Scarica e aggiungi* chiede le partite di ogni mese, un mese
alla volta, e le importa nel database scelto (*Un nuovo database, «chess.com · nome»* all'inizio), con lo stesso
rapporto. Con *Segui* spuntato, il giocatore compare sotto l'elenco dei database, in *Fonti seguite*: *Cerca nuove
partite* chiede di nuovo i mesi non ancora aggiunti e il più recente (le sue partite possono essere aumentate); una
partita già presente non viene aggiunta due volte. Cosa esce dalla macchina: il nome del giocatore e le richieste dei
suoi mesi, a api.chess.com.

### Lumbra's Gigabase
<!-- en: lumbras-gigabase 19d9d71f -->

*Da una fonte… › Lumbra's Gigabase* spiega come ottenerla: il suo sito ti chiede di accettarne la licenza (CC BY-NC
4.0) e di dimostrare che non sei un robot, cosa che RookStudio non fa al posto tuo. *Apri il suo sito* lo apre nel tuo
browser; una volta scaricato il file, *Importa il file scaricato…* lo importa così com'è. Un archivio .7z (LZMA o
LZMA2) viene letto mentre viene estratto: i suoi file PGN uno dopo l'altro, gli altri file saltati, senza scrivere
niente sul disco tranne le partite. Qualsiasi .7z di file PGN si può importare, o trascinare sulla pagina, allo stesso
modo.

### Manutenzione
<!-- en: maintenance 9c828c81 -->

Il ⋯ di un database, accanto ai suoi pulsanti nella pagina Database, o un clic destro su di esso nell'elenco, apre la
sua manutenzione al posto della pagina, in tre parti: *Partite duplicate*, *Nomi dei giocatori*, *Verifica e
compatta*; Esc torna indietro. Ctrl+K ha *Partite duplicate*, *Nomi dei giocatori*, *Lista Elo FIDE*, *Verifica il
database* e *Compatta il database*. Ogni operazione lunga gira in background a bassa priorità, con il suo avanzamento e
*Annulla*, con il database leggibile per tutto il tempo: un torneo accanto non perde tempo. Ctrl+Z annulla l'ultima
fusione, gli ultimi giocatori uniti, o le partite danneggiate messe da parte. Un database in cui scrive un altro
programma è aperto in sola lettura: la sua manutenzione aspetta che quel programma lo lasci. La pagina Home parla della
manutenzione solo quando trova qualcosa: dei duplicati dopo un'importazione, delle partite danneggiate trovate da una
verifica o all'apertura di un database.

### Partite duplicate
<!-- en: duplicate-games f3c8990b -->

La stessa partita può arrivare due volte da due fonti, con le intestazioni scritte in modo diverso: un turno seguito su
Lichess, poi il numero di TWIC del lunedì. L'importazione esclude una partita che il database ha identica (gli stessi
tag e le stesse mosse), non queste. *Partite duplicate* le trova: gli stessi giocatori (letti nella tabella dei nomi;
altrimenti i loro cognomi e le iniziali dei nomi), lo stesso anno, turni e date che non differiscono, e le stesse
mosse, o le mosse di una che sono l'inizio di quelle dell'altra, non finita (una trasmissione interrotta). Una partita
deve avere almeno dieci mosse per essere riconosciuta (venti quando il suo giorno non è noto); due partite con un
indirizzo proprio (quello di Lichess) sono due partite.

Le conta per tipo: le stesse mosse con altre intestazioni; una più corta, una trasmissione interrotta; risultati
diversi, lasciati a te. *Fondi tutti* (Invio) fonde i primi due tipi: ogni gruppo diventa una sola partita con il più
completo di ogni campo (un nome per intero al posto di un'iniziale, l'evento più lungo, il turno che dice di più, "9.2"
al posto di "9", una data che sa di più, i rating, il codice ECO, gli ID FIDE, le mosse più lunghe con i loro commenti,
gli orologi e le valutazioni, un risultato al posto di nessuno), e le altre partite vengono segnate come eliminate. Un
clic su un gruppo lo confronta campo per campo, con ogni fonte in una colonna e la partita fusa accanto: un clic su una
cella prende quel campo da quella fonte; poi *Fondi* (Invio), *Tienile entrambe* (due partite, mai più proposte) o
*Gruppo successivo* (↓). La partita fusa tiene il numero della più vecchia; il suo tag *MergedFrom* dice da dove viene
ogni campo, e l'indirizzo di una trasmissione lasciato da parte resta come suo *GameURL*. Ctrl+Z rimette le altre
partite. *Vedi nella lista* mostra i duplicati nella lista delle partite, dove *Mostra tutte* mostra di nuovo ogni
partita.

Alla fine di un'importazione, le partite già presenti da un'altra fonte vengono segnalate nel suo rapporto, con
*Fondile*: non vengono mai fuse senza un tuo clic. Lo dicono anche i rapporti delle fonti (*Da una fonte…*).

### Nomi dei giocatori
<!-- en: players-names 6b9a8fcb -->

Un giocatore viene spesso scritto in più modi: "Lindqvist, Erik", "Lindqvist,E", "Erik Lindqvist". *Nomi dei
giocatori* unisce le grafie di un giocatore nella tabella dei nomi del database. Le partite non vengono modificate, ma
la lista delle partite mostra il nome del giocatore, e lo leggono il suo filtro rapido, la sua ricerca e i suoi
ordinamenti, le partite di riferimento dell'albero delle aperture, i rapporti delle fonti e i dossier dei giocatori. Tre
conteggi dicono cosa si può fare:

- uniti subito: le partite danno l'ID FIDE del giocatore (le trasmissioni di Lichess lo fanno), o la lista Elo FIDE ha
  un solo giocatore con quel nome, o con quel nome come lo scrive un altro alfabeto ("Kowalenko" per "Kovalenko") con
  rating vicini a quelli delle partite; *Unisci i …* (Invio) lo fa;
- da confermare: più giocatori della lista hanno quel nome. Viene proposto per primo quello il cui rating è più vicino a
  quello delle partite; ↑ ↓ scelgono tra loro o *Nessuno di questi*, e Invio conserva la scelta e passa al successivo;
- lasciati come sono: un giocatore che non è nella lista, un nome di una sola parola (quello di un account), o nessuna
  lista presa; due grafie di uno stesso cognome e nome si uniscono a mano, con un clic destro e *Unisci*.

Un giocatore associato alla lista conserva nella tabella il suo ID FIDE, la federazione, il titolo, l'anno di nascita e
i rating, anche dopo che la lista è stata rimossa. Ctrl+Z annulla. La tabella fa parte del database: va con la sua
cartella. Le partite di un'importazione successiva raggiungono da sole i loro giocatori quando danno l'ID FIDE di un
giocatore o ne scrivono il nome per intero; un'altra grafia aspetta questa pagina.

### La lista Elo FIDE
<!-- en: fides-rating-list 85d6baa0 -->

La FIDE pubblica la sua lista Elo ogni mese, circa due milioni di giocatori: i loro ID FIDE, nomi, federazioni, titoli,
rating e anni di nascita. RookStudio non la distribuisce, perché la FIDE non le dà nessuna licenza. *Scarica*, nella
parte *Nomi dei giocatori*, chiede a ratings.fide.com la sua lista combinata (uno zip di circa 43 MB) e la legge nel tuo
profilo (circa 90 MB), dove resta, mai inviata da nessuna parte; *Un file già scaricato…* ne legge uno che hai già. Un
download interrotto riprende da dove si era fermato (*Riprendi*). Ogni mese successivo, la pagina Home dice che è uscita
una nuova lista, con *Prendila*: non viene mai presa senza un tuo clic. *Cerca una lista più recente* chiede di nuovo;
*Rimuovila* toglie la lista dal tuo profilo, e i giocatori uniti restano come sono. Una lista le cui colonne sono
cambiate viene rifiutata, e la lista precedente resta in uso, con *Segnalalo*. Cosa esce dalla macchina: la richiesta
del file, quando lo scarichi.

### Verifica e compattazione
<!-- en: checking-and-compacting d17eb6df -->

*Verifica* (Invio, o Ctrl+K *Verifica il database*) rilegge dal disco ogni segmento del database e rigioca ogni
partita: la sua firma e la sua chiave (un hash delle sue intestazioni e mosse) vengono rifatte e confrontate con quelle
memorizzate, e una partita che si legge diversamente da come è stata scritta è danneggiata. Dice *Integro*, oppure le
partite danneggiate e i file da cui sono state importate: *Tieni ciò che si può leggere* le mette da parte (eliminate;
Ctrl+Z le rimette), e *Importa di nuovo il loro file* le riporta dal loro file quando c'è ancora. Anche l'albero delle
aperture viene confrontato con le partite.

Un segmento i cui file non si leggono più all'apertura del database (troncati, mancanti o cambiati mentre RookStudio
era chiuso: un errore del disco, una copia lasciata a metà, uno strumento di sincronizzazione) non impedisce al database
di aprirsi. I suoi altri segmenti funzionano: la lista, l'albero delle aperture e le ricerche leggono le loro partite, e
le partite del segmento danneggiato mantengono i loro numeri. La pagina Home e la pagina Database lo segnalano, con
*Vedile*, e *Verifica e compatta* lo dice subito: il segmento, quante partite non si possono leggere, e i file da cui
sono state importate. *Tieni ciò che si può leggere* riscrive quel segmento, con le sue partite messe da parte
(eliminate, con i loro numeri conservati). I giocatori, gli eventi e i luoghi importati per la prima volta con esso
vengono conservati quando la sua lista dei nomi si legge ancora; altrimenti le altre partite che li usano mostrano "?"
al loro posto. I suoi file danneggiati restano fino alla chiusura di RookStudio: fino ad allora, *Annulla* (o Ctrl+Z) li
rimette, purché nel database non venga scritto nient'altro. *Importa di nuovo il loro file* riporta le sue partite dal
loro file quando c'è ancora. La compattazione aspetta che un segmento del genere sia sistemato, e un database aperto in
sola lettura non viene mai modificato.

Le partite eliminate o sostituite, fusioni comprese, mantengono il loro spazio nei file. *Compatta* riscrive senza di
esse i segmenti che le contengono: ogni partita mantiene il suo numero (una partita eliminata solo le sue
intestazioni), e l'albero delle aperture resta valido. I vecchi file sono conservati fino alla chiusura di RookStudio:
fino ad allora *Annulla la compattazione* li rimette, e lo spazio viene restituito alla chiusura. Le importazioni e i
salvataggi nel database aspettano mentre si compatta; la lista, l'albero e le ricerche funzionano. Quando il disco non
ha abbastanza spazio per la copia, *Compatta segmento per segmento* richiede lo spazio di un solo segmento, ma non si
può annullare.

## La pagina Aperture
<!-- en: the-openings-page 296260e5 -->

I tuoi repertori: ciascuno un file PGN del profilo (una partita e le sue varianti, per il Bianco o per il Nero), che la
pagina Scacchiera apre e modifica come una partita (*Apri il suo file sulla scacchiera*), e che *Stampa…* stampa (vedi
[Esportare e stampare](#esportare-e-stampare)). Uno è attivo: la stella della
scheda Albero vi aggiunge la mossa mostrata, o la toglie, qualunque sia il lato al tratto; la prima stella crea un
repertorio quando non ce n'è nessuno. *Nuovo repertorio* ne crea uno dal suo nome e dal suo lato.

La **copertura** del repertorio attivo si misura sui tuoi database il cui albero delle aperture è costruito (*Misurata
su*: un chip per ciascuno, un clic lo toglie o lo rimette), in background: la quota delle partite lungo il repertorio
che trovano una risposta fino alla fine delle sue linee, o fino alla mossa 10 o 15. Seguono le sue **lacune**, le più
frequenti per prime: una mossa dell'avversario giocata abbastanza spesso senza una tua risposta, o una linea che finisce
su una mossa dell'avversario. Un clic mostra la posizione, con l'albero accanto; il tasto destro propone anche il suo
rapporto sull'apertura (vedi [Il rapporto sull'apertura](#il-rapporto-sullapertura)).

### Allenare un repertorio
<!-- en: training-a-repertoire 6b0acafe -->

Ogni posizione di un repertorio in cui tocca a te, e in cui il repertorio ha la tua mossa, è una posizione da allenare.
RookStudio te le chiede ancora e ancora, a intervalli sempre più lunghi ogni volta che le sai, con FSRS, la
ripetizione dilazionata usata da Anki: una posizione a cui rispondi giusto ritorna dopo qualche giorno, poi dopo
settimane; una che sbagli ritorna più avanti nella sessione, poi il giorno dopo.

La scheda **Allenamento** della pagina Aperture conta le posizioni da ripassare oggi, le nuove di oggi e quelle che
sai, da ripassare più avanti. *Inizia · N posizioni* (Invio) chiede le posizioni da ripassare, poi le nuove di oggi;
*Solo le nuove* chiede soltanto quelle nuove. Le nuove posizioni arrivano nell'ordine in cui i tuoi database le incontrano più spesso,
prima le più giocate, al massimo 20 al giorno (il pulsante accanto, *20 al giorno*, cambia il limite). Sotto: ciò che
torna domani, entro tre giorni ed entro una settimana.

Una sessione occupa la pagina Scacchiera, la cui colonna diventa quella dell'allenamento; la partita che era sulla
scacchiera aspetta tra le partite recenti e torna dopo. RookStudio gioca le mosse del tuo avversario lungo il
repertorio fino a ogni posizione, per prime le mosse che i tuoi database giocano di più dove il repertorio ne ha più di
una. Gioca la tua mossa sulla scacchiera, o digitala (le sue lettere, poi Invio). Durante la domanda, l'albero, i
motori e le tue note sono nascosti; tornano con la risposta.

- **Alt+H**: un suggerimento, la casa del pezzo da giocare in blu (H da sola inizia una mossa del pedone h).
- **Spazio**: «Non lo so», e la mossa viene mostrata.
- **Esc**: ferma. Ciò a cui hai risposto viene conservato; la sessione successiva prosegue con ciò che resta da
  ripassare.

Ogni risposta viene valutata da sola, senza pulsanti da premere: giusta al primo tentativo, *buona*; giusta dopo un
suggerimento o dopo più di 20 secondi, *difficile*; sbagliata, o «Non lo so», *di nuovo*. Una risposta giusta mostra il
commento del repertorio sulla mossa e quando la posizione ritornerà; la successiva arriva con Invio, o da sola dopo un
secondo (il *…* accanto lo cambia, e il mouse sulla risposta la trattiene). Una sbagliata mostra la mossa del tuo
repertorio sulle sue case, in verde: *Riprova* (Invio) la chiede di nuovo; *Vedi l'albero qui* mostra la posizione con
la scheda Albero; *Aggiungi … al repertorio* fa della mossa che hai giocato una seconda risposta accettata lì (Ctrl+Z
la toglie).

Alla fine: i conteggi, le mosse che hai sbagliato (un clic mostra la posizione, con la mossa del tuo repertorio) e
*Ripassa ora le … posizioni sbagliate*.

I ripassi vengono conservati nel profilo accanto al repertorio (`Najdorf.training.json`, e `Najdorf.reviews.jsonl`,
una riga per risposta): il suo PGN non cambia. Una mossa cambiata nel repertorio rende di nuovo nuova la sua posizione.
Niente esce dalla macchina. Le posizioni da ripassare contano nel ripasso del giorno insieme agli altri tipi (vedi [Il
ripasso del giorno](#il-ripasso-del-giorno)); anche Ctrl+K *Allena il repertorio*. Mentre una delle tue partite si gioca
in diretta su Lichess, l'allenamento aspetta; le posizioni sulla linea di una delle tue partite per corrispondenza sono
escluse finché non finisce.

### Studi di teoria
<!-- en: theory-studies 8ba97ca4 -->

La parte **Teoria** della pagina Aperture (accanto a *Repertori*, nella riga del suo titolo) contiene i tuoi studi di
teoria: file PGN annotati, le tue analisi o un libro in PGN, e studi Lichess. Le loro note compaiono nella scheda
Albero della pagina Scacchiera, in ogni posizione che commentano, qualunque sia l'ordine di mosse che vi porta.

*Aggiungi un PGN…*, o un PGN trascinato sulla pagina, aggiunge un file: RookStudio lo legge dov'è, in background, e lo
rilegge da solo quando cambia sul disco. La teoria non vi scrive mai, e niente esce dalla macchina. *Da uno studio
Lichess…* aggiunge uno dei tuoi studi Lichess letti su questa macchina (vedi [I tuoi studi
Lichess](#i-tuoi-studi-lichess)): viene letta la sua copia nel tuo profilo, e riletta con lo studio.

Cosa diventa una nota: ogni posizione annotata del file, varianti comprese, con i commenti prima e dopo la mossa, i
suoi simboli (!, ?!, ±…), una valutazione scritta (`[%eval]`), e le sue frecce e case; il commento di una partita prima
della sua prima mossa è una nota sul suo inizio. I commenti dell'assistente non lo sono (vedi [I commenti
dell'assistente](#i-commenti-dellassistente)): riguardano la mossa di una partita, e l'assistente commenta qualsiasi
posizione su richiesta. I commenti dei tuoi repertori compaiono allo stesso modo, sotto i loro nomi (*Anche i commenti
dei tuoi repertori*, sotto l'elenco).

Ogni studio mostra i suoi numeri: le posizioni annotate, i commenti, i simboli e le valutazioni. *Mostra le sue note
nell'albero* (attivo all'inizio) le nasconde, o le mostra di nuovo. *Aprilo sulla Scacchiera* (Invio, o un doppio clic
nell'elenco) vi apre le sue partite come copia: Ctrl+S salva una partita altrove, e il file resta come lo scrive il suo
autore. *Toglilo dalla teoria* (Canc) lascia il file dov'è, e *Annulla* lo rimette. Un file spostato altrove lo dice,
con *Trovalo…*.

Nella **scheda Albero**, la colonna *Teoria* segna le mosse che i tuoi studi commentano: i loro simboli, e quante note
dicono più di un simbolo. Sotto le mosse, **Cosa dicono i tuoi studi** elenca le note della posizione mostrata: la
fonte di ciascuna, dove si trova (*dopo 7.f4*, *su 7…Qb6*, o *trovata per trasposizione* con le sue mosse quando
raggiungono la posizione in un altro ordine), il suo testo, e la sua linea, che un clic apre sulla Scacchiera alla sua
mossa. Il mouse su una nota su una mossa mostra la mossa come freccia dell'albero; su una nota sulla posizione, le sue
frecce e case sulla scacchiera. Nella scheda **Notazione**, *Teoria · 3* sopra le mosse dice quante note ha la
posizione, e porta alla scheda Albero (Alt+3). Mentre una delle tue partite si gioca in diretta su Lichess, le note
sono nascoste insieme all'albero.

## La pagina Allenamento
<!-- en: the-training-page 5ebf17d1 -->

La pagina **Allenamento** raccoglie ciò che torna oggi, poi gli esercizi per tipo. Si trova nella barra laterale tra le
Aperture e Lichess, nelle viste *Studio* e *Tutto*. Una vista che hai creato prima non cambia da sola: la pagina Home
propone una volta di aggiungere la pagina alla vista mostrata (*Aggiungila a …*, o *Non ora*), e nel frattempo Ctrl+K
*Allenamento* porta lì.

Il suo titolo ha sei parti: **Oggi** (il ripasso del giorno, i tipi di esercizi, i problemi da avviare), **Problemi**
(la stessa scelta di problemi, il database di Lichess conservato sul tuo disco, i tuoi risultati per tema), **I miei
errori** (vedi [I tuoi errori come esercizi](#i-tuoi-errori-come-esercizi)), **Indovina le mosse** (vedi [Indovinare le
mosse](#indovinare-le-mosse)), **Finali** (vedi [Finali contro le tablebase](#finali-contro-le-tablebase) e
[Matti](#matti)) e **Visione** (vedi [Visione della scacchiera](#visione-della-scacchiera)). Una scheda per ogni tipo di
esercizio porta alla sua parte. Tutto funziona offline; ciò che va a Lichess viene indicato dove lo scegli.

### Il ripasso del giorno
<!-- en: the-days-review b7049c16 -->

*Da ripassare oggi* conta ciò che ogni tipo di allenamento ha da ripassare oggi, ciascuno con la sua ripetizione
dilazionata (FSRS), in un'unica coda: gli errori delle tue partite (vedi [I tuoi errori come
esercizi](#i-tuoi-errori-come-esercizi)), le posizioni dei tuoi repertori (il loro allenamento resta nella pagina
Aperture, vedi [Allenare un repertorio](#allenare-un-repertorio)), i problemi e i finali che hai sbagliato. *Ripassa
tutto* (Invio) li propone uno dopo l'altro nella pagina Scacchiera: i tuoi errori da ripassare, le posizioni da
ripassare di ogni repertorio, i problemi da ripassare, poi i finali. Alla fine di ciascuno, *Avanti: …* (Invio)
prosegue, *Torna alla pagina Allenamento* (Esc) si ferma lì; ciò a cui hai risposto viene conservato. Accanto al
pulsante, il tempo che dovrebbe servire, secondo il ritmo delle tue risposte; sotto, ciò che torna domani ed entro tre
giorni. Quando errori o problemi sono da ripassare insieme ad altri tipi, *Solo i miei errori* e *Solo i problemi* li
ripassano da soli.

*In corso* della pagina Home ha lo stesso conteggio (*35 da ripassare oggi · Ripassa tutto*), e un clic ripassa tutto;
lo stesso fa Ctrl+K *Ripassa tutto*.

### Problemi
<!-- en: puzzles 4fd9ffec -->

Lichess pubblica ogni mese i suoi problemi come database (database.lichess.org, CC0): 6.193.530 problemi a novembre
2026, ciascuno con il suo rating, i suoi temi e, prima della mossa 20, la sua apertura. RookStudio lo conserva sul tuo
disco e ti dà problemi da esso senza rete.

**Il database.** *Scarica* scarica il file (circa 295 MB) e lo legge mentre arriva in un archivio di circa 293 MB nel
profilo; il file stesso non viene conservato. Un download interrotto (*Annulla*, la rete, il programma chiuso) riprende
da dove si era fermato: *Continua*, o *Ricomincia*. *Un file già scaricato…* legge invece `lichess_db_puzzle.csv.zst`
dal tuo disco. Una volta lì: *Aggiorna* chiede a database.lichess.org la data del suo file e legge un file più recente
mentre i problemi conservati continuano a rispondere; *Rimuovi i problemi* libera il disco (il tuo rating, i tuoi
risultati e i tuoi problemi da ripassare restano, conservati a parte). Dalla macchina non esce nient'altro che la
richiesta del file.

**La scelta.** *Offline* (il database) o *Online* (il tuo account Lichess, vedi [Problemi di Lichess
online](#problemi-di-lichess-online)); un tema tra i primi proposti, ciascuno con quanto spesso lo risolvi al primo
tentativo, o tra *Tutti i temi…*, che elenca ogni tema e ogni apertura con i suoi problemi (digita qualche lettera per
trovarne uno); e una difficoltà: *Più facile* (300 punti sotto il tuo rating), *Il tuo rating*, *Più difficile* (300
sopra). *Inizia · 20 problemi* (Invio) avvia una serie vicina al tuo rating, mai un problema che hai già visto; Ctrl+K
ha *Problemi: Forchetta* e gli altri temi, che avviano subito una serie.

**Una serie** occupa la pagina Scacchiera, la cui colonna diventa quella del problema, come fa l'allenamento dei
repertori; la partita sulla scacchiera aspetta tra le partite recenti e torna dopo. La scacchiera mostra la posizione
dal tuo lato, con la mossa dell'avversario appena giocata in verde chiaro. Gioca la tua mossa sulla scacchiera, o
digitala (le sue lettere, poi Invio); RookStudio gioca le risposte dell'avversario, il resto della soluzione. Durante
la domanda il motore, l'albero e le tue note sono nascosti, e così i temi del problema.

- **Alt+H**: un suggerimento, la casa del pezzo da giocare in blu; il problema conta allora come sbagliato.
- **Spazio**: «Non lo so»; viene mostrata la soluzione e il problema conta come sbagliato.
- **Esc**: ferma; ciò a cui hai risposto viene conservato.

Giusto: la soluzione in mosse, la variazione del tuo rating e i temi del problema; *Avanti* arriva con Invio, o da solo
dopo un secondo (l'impostazione dell'allenamento dei repertori); *Analizza qui* (Alt+A) avvia il motore sulla
posizione; *La partita su lichess.org* apre la partita da cui viene il problema. Qualsiasi mossa che dà matto è giusta,
come su Lichess. Sbagliato: la tua mossa in arancione e la prima mossa della soluzione come freccia verde; *Riprova*
(Invio) la fa rigiocare, senza contare di nuovo; *La soluzione mossa per mossa* (→) la gioca una mossa alla volta;
*Avanti* prosegue.

Alla fine: i problemi risolti al primo tentativo, quanti ne hai sbagliati, la variazione del tuo rating, e l'elenco di
quelli sbagliati (un clic mostra la posizione con la soluzione); *Ripassa ora i 3* (tanti quanti gli sbagliati) li
ripropone senza contare, *Torna alla pagina Allenamento* esce.

**Il tuo rating** è proprio di RookStudio, calcolato con Glicko-2 come Lichess calcola i rating dei suoi problemi: un
problema risolto al primo tentativo, senza suggerimenti, è vinto contro il rating del problema. Parte dal tuo rating dei
problemi di Lichess quando il tuo account è collegato, altrimenti da 1500, e si muove in fretta finché è nuovo; i
risultati dei problemi offline non possono tornare a Lichess. *I tuoi risultati per tema* (la parte Problemi) conta i
problemi di ogni tema e quanto spesso li hai risolti al primo tentativo.

Un problema che sbagli diventa una scheda del ripasso del giorno: torna domani, poi a intervalli sempre più lunghi ogni
volta che lo risolvi; sbagliato di nuovo, più avanti nella stessa serie, poi il giorno dopo. Tutto viene conservato
nella cartella `puzzles` del profilo (`rating.json`, `results.jsonl` una riga per problema, `review.json`), il database
in `lichess-puzzles`.

### Problemi di Lichess online
<!-- en: lichess-puzzles-online 51fd9d60 -->

*Online* dà problemi dal tuo account Lichess: problemi che non hai mai visto, al tuo rating Lichess, i cui risultati
tornano a Lichess. Richiede il tuo account collegato (vedi [Collegare il tuo account](#collegare-il-tuo-account)); la
prima volta, Lichess chiede due permessi in più, *puzzle:read* e *puzzle:write*: *Autorizza su Lichess…* apre la sua
pagina, nel frattempo il tuo account resta collegato, e il token che sostituisce viene revocato.

*Chiedi un lotto · 50 problemi* chiede a Lichess cinquanta problemi del tema e della difficoltà scelti. Il lotto viene
conservato sul tuo disco: si risolve anche offline, in quante serie vuoi (*Riprendi · ne restano N*). *Classificata* o
*Non classificata, per provare* viene inviato con ogni risposta. Alla fine del lotto, le sue risposte vanno a Lichess
con una sola richiesta, che porta il lotto successivo; il tuo rating Lichess si muove allora, e la pagina dice quando
sono state accettate le ultime risposte. Lichess chiede ai programmi di non scaricare la sua raccolta tramite queste
richieste: per questo c'è il database.

Quando Lichess non può accettare le risposte, restano sul disco, con l'indicazione di cosa fare: troppe richieste
(429), reinviate da sole un minuto dopo, anche se nel frattempo RookStudio è stato chiuso; la rete assente, reinviate
un minuto dopo; il token rifiutato, inviate una volta collegato di nuovo l'account. *Riprova ora* non aspetta. Cosa
esce dalla macchina: il lotto richiesto (il suo tema o la sua apertura, la sua difficoltà), poi l'id di ogni problema
con giusto o sbagliato, classificato o no.

### I tuoi errori come esercizi
<!-- en: your-mistakes-as-exercises b0e75f68 -->

Gli errori delle tue partite tornano come esercizi: la posizione prima della tua mossa, per trovare di meglio. Le tue
partite sono quelle in cui giochi con uno dei tuoi nomi (vedi [I tuoi nomi](#i-tuoi-nomi)), nei tuoi database. Ognuna
di esse che ha un resoconto (vedi [Il resoconto di una partita](#il-resoconto-di-una-partita)) dà le mosse che il
resoconto ha segnato **?** e **??** dalla tua parte, dove una mossa migliore è chiara: il resoconto ha cercato la
posizione abbastanza a fondo da indicarne la mossa migliore (almeno il suo terzo passaggio), e la partita non era
comunque decisa (la mossa migliore del resoconto ti tiene in partita; la tua mossa non ha mantenuto una vittoria). Le
imprecisioni (?!) sono escluse: di rado hanno una sola buona risposta.

La parte **I miei errori** conta gli esercizi e le partite da cui vengono, quelli da ripassare oggi, quelli nuovi e
quelli che sai, da ripassare più avanti. I loro tipi vengono da *Su cosa lavorare* di ogni resoconto: forchette, pezzi
lasciati in presa, finali di torre…; un chip per ciascuno limita l'elenco e la sessione a quel tipo, *Ogni tipo* li
prende tutti. L'elenco dà per ogni esercizio la partita, dove e quando è stata giocata, la tua mossa e quella del
resoconto, e quando torna. *Inizia · N esercizi* (Invio) chiede quelli da ripassare, poi i nuovi di oggi (tanti al
giorno quante le nuove posizioni dei repertori).

Le tue ultime partite senza resoconto sono indicate sotto l'elenco: *Esaminale · in background* le esamina una dopo
l'altra, come la pagina Scacchiera fa il resoconto di una partita (il suo motore, le sue impostazioni, i processi
lasciati liberi accanto a un torneo), e ogni resoconto viene conservato con la sua partita; i loro esercizi arrivano
partita per partita. Nel frattempo compaiono l'avanzamento, il tempo rimasto e *Annulla*; i resoconti aspettano mentre
la pagina Scacchiera analizza.

**Un esercizio** occupa la pagina Scacchiera, la cui colonna diventa quella dell'esercizio, come fa l'allenamento dei
repertori: la partita fino alla posizione, dal tuo lato, l'ultima mossa dell'avversario in verde chiaro; sotto la
scacchiera, quando è stata giocata e cosa hai giocato allora. Gioca la tua mossa sulla scacchiera, o digitala. La mossa
migliore del resoconto è subito giusta; qualsiasi altra mossa viene cercata dal motore del resoconto con le impostazioni
del resoconto (un thread, il suo numero di nodi, partendo da una tabella vuota), ed è giusta quando perde al massimo
0,3 pedoni rispetto alla migliore del resoconto, o mantiene una vittoria che il resoconto non segnerebbe. Il motore,
l'albero e le tue note sono nascosti durante la domanda.

- **Alt+H**: un suggerimento, la casa del pezzo da giocare in blu.
- **Spazio**: «Non lo so»; viene mostrata la mossa del resoconto.
- **Esc**: ferma; ciò a cui hai risposto viene conservato.

Ogni risposta viene valutata da sola, come una posizione di repertorio: giusta al primo tentativo, *buona*; giusta dopo
un suggerimento o dopo più di 20 secondi, *difficile*; sbagliata, *di nuovo*. Giusta: la linea del resoconto e quella
della tua partita, cosa ha detto il resoconto della tua mossa, e quando l'esercizio torna; *Apri la partita a questa
mossa* la apre nella pagina Scacchiera. Sbagliata: la tua mossa in arancione, la mossa del resoconto come freccia
verde, e cosa ha trovato il motore dopo la tua; *Riprova* (Invio) la fa rigiocare senza contare di nuovo; un esercizio
sbagliato torna qualche esercizio dopo, poi il giorno dopo.

Gli esercizi da ripassare contano nel ripasso del giorno (vedi [Il ripasso del giorno](#il-ripasso-del-giorno)), *Solo i
miei errori* li ripassa da soli; Ctrl+K ha *Ripassa i miei errori* ed *Esercizi dai miei errori*. Una posizione che il
tuo repertorio allena non viene presa qui: si allena nella pagina Aperture. Una partita tolta dal suo database porta
via con sé i suoi esercizi. Gli esercizi e le loro schede sono conservati nella cartella `mistakes` del profilo
(`exercises.json`, e `answers.jsonl`, una riga per risposta); le tue partite non cambiano, e niente esce dalla macchina.
Ciò che dicono le tue partite ordina anche i temi dei problemi: i tipi dei tuoi errori dell'ultimo mese vengono per
primi tra essi, indicati sotto i chip.

### I tuoi nomi
<!-- en: your-names 4a0ddfdd -->

*Impostazioni › I tuoi nomi* (o la parte *I miei errori*, prima di qualsiasi nome) elenca i nomi con cui giochi, come
li scrivono i tuoi database: una partita in cui gioca uno di essi è tua. Un nome corrisponde per parole intere, senza
badare a maiuscole e accenti: *Tester* è *Tester, Sam* e *TESTER, S.*, *Sam Tester* è *Tester, Sam*, ma *Test* non è
nessuno. Il nome del tuo account Lichess collegato conta da solo. Clicca un nome per toglierlo: gli esercizi delle sue
partite se ne vanno, ma le loro schede vengono conservate, e tornano con essi se aggiungi di nuovo il nome.

### Indovinare le mosse
<!-- en: guessing-the-moves e004de63 -->

**Indovina le mosse** rigioca una partita dei tuoi database da un lato: a ognuna delle sue mosse, indovini quella
giocata. Il motore dà un punteggio a ogni tentativo rispetto alla mossa della partita: *come la partita* 3 punti;
*altrettanto buona* per il motore (al massimo 0,2 pedoni peggio, o meglio) 2; *giocabile* (al massimo 0,5 pedoni
peggio) 1; altrimenti nessuno. È il motore di analisi, su un thread, un secondo per mossa; la mossa della partita viene
cercata mentre pensi.

*Una partita a caso* ne estrae una dal database scelto, attraverso un filtro: il filtro rapido della lista delle
partite (giocatori, evento, un codice ECO, un anno) e l'Elo dei giocatori. Scegli il tuo lato, poi *Inizia · una
partita a caso* (Invio). Con *Le mosse di teoria passano da sole* (conservato per la volta successiva), si inizia a
indovinare dove la partita lascia le mosse che molte partite dei tuoi database hanno giocato (il loro albero delle
aperture). *La partita della pagina Scacchiera* fa indovinare la partita mostrata dalla sua posizione, per il lato al
tratto: Ctrl+K *Indovina le mosse* nella pagina Scacchiera.

La partita occupa la pagina Scacchiera, la cui colonna diventa quella di Indovina le mosse; la partita non viene
modificata, e le sue mosse successive non vengono mostrate. Dopo ogni tentativo: la tua mossa come freccia blu, quella
della partita come freccia verde, i punti ed entrambe le valutazioni; la partita prosegue da sola dopo un momento
(subito con Invio), con la risposta dell'avversario giocata, e la mossa successiva è tua. Sotto: ogni tentativo con i
suoi punti, e il punteggio finora. Alla fine: i tuoi punti, quanti tentativi erano la mossa della partita, i pedoni
persi in media per mossa, e le mosse che ti sono costate di più (un clic mostra la loro posizione); tutta la partita è
sulla scacchiera, ← e → la percorrono. Ogni partita indovinata viene conservata nella cartella `guess` del profilo; la
pagina Allenamento mostra i punti dell'ultima.

### Finali contro le tablebase
<!-- en: endgames-against-the-tables 6ded57cf -->

La parte **Finali** è un catalogo di finali da vincere o da tenere, giocati contro le tablebase: diciotto, in quattro
gruppi. *Matti elementari*: la donna, la torre, i due alfieri, alfiere e cavallo. *Pedoni*: l'opposizione, la regola
del quadrato, il pedone passato lontano, la manovra di Réti. *Torri*: Lucena, Philidor, Vancura, torre contro pedone,
torre e due pedoni contro torre. *Pezzi e donna*: donna contro torre, donna contro pedone, l'alfiere sbagliato, torre
contro alfiere, alfiere contro cavallo. Ognuno dice cosa chiede (*matto*, *vittoria* o *tenere la patta*) e a che punto
sei: *da fare*, *fatto*, *fatto in 9 (le tablebase: 7)* per un matto, *da rifare* dopo un errore. Uno a cui le
tablebase disponibili non sanno rispondere è in grigio, con i suoi pezzi indicati (vedi [Tablebase per i
finali](#tablebase-per-i-finali)); passa il mouse su uno per l'idea che lo vince o lo tiene. Un clic lo gioca; poi ↑ e
↓ ne scelgono un altro e Invio lo gioca. Ctrl+K ha *Finale: Lucena* e gli altri.

**Un finale** occupa la pagina Scacchiera, la cui colonna diventa quella del finale, come fanno i problemi; la partita
sulla scacchiera aspetta tra le partite recenti. Giochi dal tuo lato; le tablebase rispondono a ogni mossa con quella
che resiste più a lungo (con le tablebase Syzygy, la più lunga fino alla prossima cattura o mossa di pedone, e tra
mosse altrettanto lunghe quella che ti lascia meno buone risposte). Sopra la domanda: il verdetto attuale delle
tablebase (*Le tablebase: vince il Bianco*), e a che distanza, le mosse al matto quando Lichess le conosce, altrimenti
le mosse al massimo prima di una cattura o di una mossa di pedone.

- **Ctrl+Z**: ritira la tua ultima mossa e la risposta delle tablebase.
- **Alt+H**: la mossa giusta, come freccia verde; il finale conta allora come fatto con aiuto.
- **Ricomincia**: torna all'inizio. **Esc**: ferma.

Ogni mossa viene giudicata subito. Una che trasforma una vittoria in patta, o una patta in sconfitta, è un errore: le
tablebase rispondono, poi la colonna lo dice (*1.Kd5? Ke7: ora è patta*) con le mosse che mantenevano il risultato
(*Mantenevano la vittoria: 1.Kd6!, 1.Kf6!*, con le loro case in verde e la tua mossa in arancione) e l'idea del finale.
*Ritira la mossa* (Invio, o Ctrl+Z) la fa rigiocare; *Vedi la vittoria* la ritira e scrive da lì la linea delle
tablebase; *Continua comunque* prosegue con il nuovo verdetto. Sotto, il verdetto all'inizio e ogni mossa che lo ha
cambiato.

Una vittoria conta al matto, o quando un tuo pedone diventa donna o l'altro re resta solo, con la vittoria mantenuta;
nei matti elementari l'obiettivo è il matto stesso, con le tue mosse confrontate con il matto delle tablebase
dall'inizio. Una patta conta quando una regola rende patta la partita (stallo, materiale insufficiente, la triplice
ripetizione, le cinquanta mosse), o dopo 15 mosse tenute. Alla fine: *Di nuovo* (Invio), *Avanti:* il finale
successivo non ancora fatto, *Torna alla pagina Allenamento*.

La posizione prima di un errore diventa una scheda del ripasso del giorno (vedi [Il ripasso del
giorno](#il-ripasso-del-giorno)): torna domani, con il finale rigiocato da lì, poi a intervalli sempre più lunghi ogni
volta che lo vinci o lo tieni senza errori (con aiuto conta come difficile; con un errore, come sbagliato). Ctrl+K
*Ripassa i finali sbagliati*.

**Dalle tue partite** prende le posizioni con 7 pezzi o meno tra i tuoi errori (vedi [I tuoi errori come
esercizi](#i-tuoi-errori-come-esercizi)), in cui il resoconto della tua partita ha visto sfuggire la vittoria, o la
patta. *Inizia* le gioca una dopo l'altra contro le tablebase, ciascuna dalla posizione prima della tua mossa; una che
le tablebase dicono persa comunque viene esclusa. I risultati e le schede sono conservati nella cartella `endgames` del
profilo (`record.json`).

### Tablebase per i finali
<!-- en: tables-for-the-endgames 0e49bbca -->

Le tablebase sono quelle del tuo disco, le cartelle Syzygy dei tornei (*Aggiudicazione* > tablebase, vedi [Le tablebase
nella pagina Scacchiera](#le-tablebase-nella-pagina-scacchiera)), con quelle proprie del profilo una volta scaricate;
per più pezzi di quanti ne contengano, quelle online di Lichess, fino a 7 pezzi, con il tuo consenso alle tablebase
online (quello della pagina Scacchiera, vedi [Online per l'analisi](#online-per-lanalisi)). Mentre un torneo gioca nel
processo del programma, le tablebase del disco sono sue: nel frattempo rispondono quelle di Lichess, o il finale
aspetta.

Quando un finale richiede tablebase che non hai, la parte lo dice (*Nessuna tablebase per «Lucena»*) e propone tre
strade:

- **Scarica le tablebase da 3 a 5 pezzi** (290 file, 938 MB) dal mirror di Lichess, un file alla volta, prima i più
  piccoli: i finali con 3 e 4 pezzi sono pronti in pochi secondi, gli altri man mano che arrivano i loro file. Un
  download interrotto riprende da dove si era fermato (*Continua*); ogni file viene verificato (la sua dimensione e il
  marchio di Syzygy). Le tablebase vanno nella cartella `syzygy` del profilo e, una volta tutte lì, nelle cartelle dei
  tornei.
- **Una cartella di tablebase già sul disco…**: scelta anche per i tornei.
- **Quelle di Lichess, online**: fino a 7 pezzi, una richiesta per posizione giocata; sceglierle dà il consenso.

Senza tablebase, i finali che ne hanno bisogno sono in grigio, ciascuno con ciò che gli serve; gli altri esercizi
funzionano. Online, un'interruzione fa aspettare il finale: la tua mossa viene conservata, e giudicata quando torna la
rete (*Riprova ora* non aspetta); un 429 aspetta il minuto che chiede Lichess. Cosa esce dalla macchina: con le
tablebase del disco, niente; con quelle di Lichess, ogni posizione giocata, come FEN; per il download, la richiesta di
ogni file.

### Matti
<!-- en: mates 803d99bd -->

La scheda **Matti** (nella parte Finali, a cui porta la scheda della parte Oggi) avvia 20 problemi di matto di Lichess
(vedi [Problemi](#problemi)): da 1 a 5 mosse, o per schema (del corridoio, affogato, di Anastasia, arabo, di Boden, dei
due alfieri, a uncino), ciascun chip con quanto spesso lo risolvi al primo tentativo. Vengono dal database sul tuo
disco, altrimenti da un lotto del tuo account Lichess; senza nessuno dei due, il clic porta alla parte Problemi. I
matti elementari contro le tablebase sono nel catalogo qui sopra, con le tue mosse contate rispetto al matto delle
tablebase.

### Visione della scacchiera
<!-- en: board-vision dd34b350 -->

La parte **Visione** allena a vedere la scacchiera a mente. Niente esce dalla macchina; i record sono conservati nel
file `vision.json` del profilo.

**Coordinate**: trenta secondi di case. *Trova la casa*: viene nominata una casa, cliccala su una scacchiera senza
lettere né numeri; *Nomina la casa*: una casa è segnata in blu, digita il suo nome (una lettera, poi un numero). Vista
dal Bianco, dal Nero, o *A caso*, con la scacchiera che ruota casa per casa. Un errore mostra la casa richiesta in verde
e la tua in arancione. Alla fine, il tuo conteggio e il tuo record per quel modo e quel lato. Invio avvia, Esc ferma.

**Alla cieca**: una partita contro il motore della pagina Gioca, alla forza e con le aperture scelte lì (*Cambia nella
pagina Gioca*, vedi [Contro un motore](#contro-un-motore)). Giochi con il Bianco, il Nero o uno dei due a caso; la
scacchiera nasconde tutti i pezzi, tutti i pezzi dopo la 10ª mossa, o tutti tranne i pedoni, con l'ultima mossa
segnata. Digita le tue mosse, o giocale sulla scacchiera; le mosse fatte finora sono scritte nella colonna. Tieni
premuto Maiusc un momento per vedere i pezzi per 2 secondi, tre volte a partita; F2 chiede *Dov'è…?* di un pezzo che le
sue parole nominano da sole (*l'alfiere nero camposcuro*): clicca la sua casa, o digitala. Il tuo orologio è lungo,
quello del motore breve; ogni partita viene aggiunta al file `games/blindfold.pgn` del profilo.

**Una linea letta a mente**: una posizione di un'apertura nota, le sei mosse che seguono scritte accanto senza che i
pezzi si muovano, poi una domanda: quale pezzo c'è su una casa che hanno cambiato? Clicca la risposta, o digita il
pezzo come lo scrive il FEN (*N* un cavallo bianco, *n* uno nero, *-* una casa vuota); la scacchiera mostra poi la
posizione dopo le mosse. Dieci linee per serie.

## La pagina Gioca
<!-- en: the-play-page 7168b30b -->

La pagina **Gioca** ha due parti: *Contro un motore*, una partita contro un motore della tua libreria giocata nella
pagina Scacchiera, e *Su Lichess*, le tue partite Lichess giocate da RookStudio (vedi [Su Lichess](#su-lichess)) e le
arene e i tornei svizzeri di Lichess (vedi [Arene e tornei svizzeri](#arene-e-tornei-svizzeri)); riappare la parte
mostrata per ultima. Si trova nella barra laterale dopo la pagina Allenamento, nelle viste *Studio* e
*Tutto*. Una vista che hai creato prima non cambia da sola: la pagina Home propone una volta di aggiungerla alla vista
mostrata (*Aggiungila a …*, o *Non ora*), e nel frattempo Ctrl+K *Gioca contro un motore* (o *Gioca su Lichess*) porta
lì, come *Gioca contro un motore* nella pagina Home e *La pagina Gioca* nella scheda *Gioca una partita…* della pagina
Scacchiera. Contro un motore, niente esce dalla macchina: il motore gira su di essa. Mentre una delle tue partite si
gioca in diretta su Lichess, la pagina lo dice e non avvia nessuna partita contro un motore (vedi [Fair
play](#fair-play)).

### Contro un motore
<!-- en: against-an-engine 1cf4f880 -->

La scelta viene conservata per la partita successiva: una volta fatta, basta *Gioca* (Invio).

- **L'avversario**: un motore della tua libreria, con le forze che può assumere.
- **La sua forza**: l'Elo a cui mira (`UCI_Elo`, con `UCI_LimitStrength`), sulla sua scala: un'indicazione, non un Elo
  FIDE. Un motore senza quell'impostazione gioca invece un numero di nodi per mossa, da 1 a un milione, meno per un
  gioco più debole. *Piena forza* lo lascia al suo meglio, qualunque limite imponga la sua scheda.
- **Il suo stile: le sue aperture**, ciò che gioca prima delle sue mosse: *Le proprie* (nessun libro); *I tuoi database*
  (le mosse più giocate lì, in tutti i tuoi database con un albero delle aperture o in uno di essi); *Il tuo repertorio*
  (le mosse contro cui ti sei preparato, poi il motore; giochi il colore del repertorio, che sceglierlo imposta); *Le
  partite di un giocatore* (le mosse che un giocatore ha giocato con il colore del motore, da un database chiamato
  *Lichess · nome*, come quello che crea la preparazione di una partita contro di lui, vedi [Preparare una
  partita](#preparare-una-partita)); *Un libro della libreria* (un libro Polyglot o CTG della pagina Libri, senza le sue
  mosse da evitare). *Varia* sceglie a caso tra le mosse giocate almeno 5 volte, con la frequenza con cui sono state
  giocate (quelle di un libro con la frequenza indicata dai loro pesi, quelle di un repertorio con la stessa frequenza);
  senza spunta, la mossa più giocata, prima la mossa principale di un libro. Quando le sue aperture non hanno più mosse
  per la posizione, il motore gioca fino alla fine. Il suo stile sono le sue aperture: le impostazioni proprie del
  motore restano sulla sua scheda. Con un handicap, le aperture incontrano posizioni che non hanno, e il motore gioca
  dalla prima mossa.
- **Tu**: Bianco, Nero o a caso; l'orologio, minuti e un incremento, *Lo stesso per entrambi* o, senza spunta, quello
  proprio del motore (tempo diseguale); dalla posizione iniziale, o da quella della pagina Scacchiera.
- **L'handicap**: ciò che il motore dà all'inizio: niente, il suo pedone f, il suo cavallo b, la sua torre a (e con
  essa l'arrocco da quel lato) o la sua donna; un pezzo che non ha dove inizia è in grigio, e Chess960 non ne ha.
  *Ritirare una mossa* (permesso, a meno che non togli la spunta) e *Suggerimenti* (3 a partita) sono anch'essi
  handicap, conteggiati.

La partita conta nel budget dei core (i thread del motore, come per una partita giocata dalla scacchiera); la pagina
dice quando la macchina è piena.

### La partita contro un motore
<!-- en: the-game-against-an-engine bfdb25dc -->

*Gioca* porta la partita nella pagina Scacchiera, in una vista per lo studio: la scacchiera girata dal tuo lato, gli
orologi che corrono nelle strisce dei giocatori (quella del motore con la sua forza), e sopra la colonna la scheda della
partita: a chi tocca, *Proponi patta*, *Abbandona…*, *Ritira la mossa · Ctrl+Z*, *Suggerimento · Alt+H*, *Ferma*.

- Gioca le tue mosse sulla scacchiera, o digitale: la prima lettera di una mossa apre il campo, Invio la gioca.
- **Gli aiuti sono nascosti fino alla fine**: le schede Analisi, Albero, Riferimenti e Domande, l'analisi (Alt+A), la
  minaccia, l'analisi della partita (Alt+G), le tablebase e l'assistente. Nemmeno il motore mostra qualcosa della sua
  ricerca: le sue valutazioni arrivano con la partita finita. *Mostrali*, nella scheda della partita o al posto di una
  scheda nascosta, li restituisce durante la partita, che viene allora segnata *con aiuti*.
- *Ritira la mossa* (Ctrl+Z) ritira la tua ultima mossa e la risposta del motore, o la tua mossa mentre il motore
  pensa, con gli orologi come erano prima: conteggiato, e scritto nella partita.
- *Suggerimento* (Alt+H): quando tocca a te, la mossa del motore di analisi dopo un secondo di ricerca, come freccia
  verde; tre a partita, conteggiati e scritti nella partita.
- *Proponi patta*: il motore risponde con la sua prossima mossa. Accetta la patta quando sta per subire matto o sta
  chiaramente peggio, o quando non sta meglio dopo 20 mosse giocate; una mossa delle sue aperture la rifiuta.
- *Abbandona…* chiede Invio, o un secondo clic, entro pochi secondi; *Ferma* chiude la partita senza risultato,
  salvata così com'è.
- Dove sono finite le aperture del motore, la notazione lo dice (*fine del tuo repertorio · poi il motore*).

Chiudere RookStudio durante la partita la conserva: al prossimo avvio è di nuovo nella pagina Scacchiera, con gli
orologi in attesa finché non la riprendi (*Riprendi*, Invio) o la fermi (*Fermala*). Un'altra partita aperta sulla
scacchiera nel frattempo lascia proseguire la tua, con il tuo orologio che corre quando tocca a te: *Torna alla
partita* la riporta.

### Dopo la partita
<!-- en: after-the-game 44d80f6b -->

Alla fine, gli aiuti tornano, e la scheda dice come è finita la partita (*Patta: triplice ripetizione alla mossa 34*),
contro cosa e come: la forza, il tuo colore, l'orologio, le mosse ritirate, i suggerimenti, con o senza aiuti. La
partita viene salvata nel database **Le mie partite contro i motori**, creato alla prima partita, con le sue
impostazioni nei suoi tag: `Strength` (`UCI_Elo 1800`, `nodes 1000`, `full`), `Openings` (`repertoire Najdorf`,
`database Mine`, `databases`, `player Somebody`, `book Human`, `engine`), `Handicap` (`knight b8`, con la posizione
iniziale), `Takebacks`, `Hints`, `Aids`. Ctrl+S la rimette lì dopo una modifica.

- *Esaminala* (Invio): l'analisi della partita, come fa Alt+G (vedi [Il resoconto di una
  partita](#il-resoconto-di-una-partita)); una volta fatto il resoconto, i tuoi errori in essa diventano esercizi (vedi
  [I tuoi errori come esercizi](#i-tuoi-errori-come-esercizi)) quando i tuoi nomi dicono che la partita è tua (vedi [I
  tuoi nomi](#i-tuoi-nomi)).
- *Rivincita, a colori invertiti*: la stessa scelta, l'altro colore.
- *Cambia le impostazioni*: la pagina Gioca.
- *Chiudi*: la partita resta sulla scacchiera come qualsiasi partita.

Sotto, **le tue partite contro quel motore a quel livello**: vinte, patte e perse, quelle con aiuti a parte e non
contate, e dove hai perso l'ultima (*un finale di torre*), con l'indicazione se è tra i tuoi esercizi. Il livello
successivo si sceglie lì o nella pagina Gioca: niente sale da solo. Ogni partita è anche una riga del file
`play/games.jsonl` del profilo.

### Su Lichess
<!-- en: on-lichess 37d530b3 -->

La parte **Su Lichess** della pagina Gioca gioca le tue partite Lichess da RookStudio, tramite la Board API di Lichess,
che è fatta per le persone. Ha due metà, *Partite*, descritta qui, e *Tornei* (vedi [Arene e tornei
svizzeri](#arene-e-tornei-svizzeri)); riappare quella mostrata per ultima. Richiede il tuo account collegato (vedi
[Collegare il tuo account](#collegare-il-tuo-account)); la prima volta, Lichess chiede un permesso in più, *Giocare le tue partite*
(`board:play`): *Autorizza su Lichess…* (Invio) apre la pagina di Lichess, nel frattempo il tuo account resta
collegato, e il token che sostituisce viene revocato. RookStudio non gioca mai una mossa che non hai giocato tu. Cosa
esce dalla macchina: le tue ricerche e sfide, le tue mosse, i tuoi messaggi in chat, patte e abbandoni, con il token del
tuo account; le scelte sono conservate nel file `play/lichess.json` del profilo.

- **Cerca una partita**: rapida (10+0, 10+5, 15+10), classica (30+0, 30+20) o per corrispondenza (da 1 a 14 giorni per
  mossa), classificata o no, contro avversari di qualsiasi rating o di un rating vicino al tuo a quella cadenza (±).
  Lichess riserva blitz e bullet al suo sito: un programma non li cerca, e il blitz resta possibile come sfida diretta.
  *Cerca un avversario* (Invio) mette la ricerca nella lobby di Lichess una volta aperto il flusso di eventi di
  Lichess, come chiede Lichess, così che l'inizio della partita non venga perso. Una ricerca in diretta vive finché
  RookStudio la tiene aperta: Esc la ritira, e così chiudere RookStudio; la partita trovata si apre nella pagina
  Scacchiera. Una ricerca per corrispondenza resta nella lobby finché qualcuno non la accetta (l'API non può ritirarla;
  la lobby di lichess.org sì), e la sua partita arriva tra le tue partite in corso.
- **Sfida un giocatore**: il suo nome, l'orologio (3+2, 5+3, 10+5, 15+10, 3 giorni, o *altro…*: minuti e secondi
  aggiunti per mossa, o giorni per mossa; almeno tre minuti, con l'incremento contato quaranta volte, perché la Board
  API non gioca il bullet), classificata o no, il tuo colore. Una sfida in diretta aspetta la risposta finché resti
  (Esc, o *Ritirala*). Rifiutata, viene indicato il motivo di Lichess (*troppo veloce*) con altre cadenze a un clic (più
  lente dopo «troppo veloce»); accettata, la partita si apre nella pagina Scacchiera.
- **Una sfida ricevuta** arriva in cima alla parte (*Accetta* · Invio, *Rifiuta ▾* con i motivi di Lichess: *Non ora*,
  *Più tardi*, *Troppo veloce per me*, *Troppo lenta per me*, *Solo amichevoli*, *Solo classificate*), nella pagina Home
  sotto *In corso*, e in un avviso nell'angolo della finestra ovunque tu sia, con un suono quando i suoni sono attivi;
  *Più tardi* mette da parte l'avviso. Durante una partita in diretta, aspetta la fine della partita. Una partita che la
  Board API non può giocare (bullet, una variante diversa da Chess960) si gioca su lichess.org.
- **Le tue partite in corso**: prima quelle in cui tocca a te, con il tempo rimasto per muovere; un clic ne apre una
  nella pagina Scacchiera, dove si gioca la tua mossa.

Il flusso di eventi è quello del [Fair play](#fair-play): Lichess ne apre uno per token, e comunica l'inizio e la fine
delle partite, le sfide ricevute e la sorte di quelle che hai inviato.

### Una partita su Lichess
<!-- en: a-game-on-lichess 15ac0e49 -->

La partita si gioca nella pagina Scacchiera, in una vista per lo studio: la scacchiera girata dal tuo lato, i rating e
gli orologi dei giocatori nelle loro strisce, e sopra la colonna la scheda della partita: cos'è (*Classificata · Rapida
10+5 contro …*), a chi tocca, *Proponi patta*, *Abbandona…* (Invio, o un secondo clic, entro pochi secondi), *Chiedi di
ritirare la mossa*, *Annulla partita* (prima che entrambi i lati abbiano mosso), *Chat* (la chat dei giocatori), *Apri
su lichess.org*.

- In diretta, le tue mosse partono appena giocate, sulla scacchiera o digitate (la prima lettera di una mossa apre il
  campo, Invio la gioca). Una mossa fatta durante il turno dell'avversario aspetta il tuo (una freccia tratteggiata), e
  viene giocata allora se è legale; Esc la ritira. Prima della tua prima mossa, la scheda conta alla rovescia il tempo
  che Lichess concede prima di annullare la partita.
- Per corrispondenza, una mossa aspetta la tua conferma (*La tua mossa: 10.g4 · non ancora inviata*, una freccia sulla
  scacchiera): *Invia* (Invio), o *Ritirala* (Esc). La scheda elenca le tue altre partite per corrispondenza, a un clic.
- La proposta di patta o la richiesta di ritirare la mossa del tuo avversario arriva nella scheda con *Accetta* e
  *Rifiuta*; la tua aspetta la sua risposta. Se il tuo avversario lascia la partita, la scheda conta alla rovescia
  finché Lichess non ti permette di rivendicare la vittoria (*Rivendica la vittoria*, *Rivendica la patta*).
- **Gli aiuti**: vale il blocco del [Fair play](#fair-play), come per qualsiasi tua partita. In diretta, ogni aiuto è
  bloccato su ogni posizione: le schede Analisi, Albero, Riferimenti e Domande, l'analisi, la minaccia, le tablebase, il
  cloud e l'assistente. Per corrispondenza, il motore, il cloud, le tablebase e l'assistente sono bloccati sulla
  partita; l'albero dei tuoi database e i tuoi libri restano aperti, perché lì Lichess permette un libro.
- **Un'interruzione**: la scheda lo dice (*il tuo orologio corre su Lichess*) e riprova da sola, dopo 1, 2, 3, poi ogni
  5 secondi; l'intera partita torna con la connessione, anche la chat. Una mossa giocata nel frattempo non parte:
  rigiocala una volta tornata la connessione. *Apri la partita su lichess.org* resta a portata di mano.
- **Chiudere RookStudio** durante una partita in diretta chiede prima (*Resta* · Esc, *Chiudi comunque*): la partita
  continua su Lichess, con il tuo orologio che corre. Al prossimo avvio, una partita ancora in corso torna nella pagina
  Scacchiera, e la pagina Home la mostra per prima. Un'altra partita aperta sulla scacchiera nel frattempo lascia
  proseguire la tua: *Torna alla partita* la riporta.
- Alla fine, la scheda dice come è finita la partita e cosa ha fatto al tuo rating; la partita entra nel tuo database
  Lichess, con l'analisi di Lichess quando c'è, e il suo resoconto a un clic, come le tue altre partite finite (vedi
  [Le tue partite finite](#le-tue-partite-finite)). *Un'altra partita* cerca di nuovo con lo stesso orologio.

### Arene e tornei svizzeri
<!-- en: arenas-and-swiss-tournaments 042061fe -->

La metà *Tornei* della parte **Su Lichess** della pagina Gioca si iscrive a un'arena di Lichess, o a un torneo svizzero
di una delle tue squadre, e ne gioca le partite da RookStudio. Ci porta anche la scheda *Tornei Lichess* della pagina
Lichess, con *Partecipa* sulla riga di un'arena (vedi [Tornei Lichess](#tornei-lichess)).

- **Cosa permette Lichess.** La sua Board API gioca solo partite rapide, classiche e per corrispondenza, mai blitz o
  bullet in un torneo: quelle arene si seguono nel mosaico e si giocano su lichess.org, e la loro riga lo dice, con il
  link. La parte elenca le arene rapide e classiche che puoi giocare.
- **La prima volta**, Lichess chiede un permesso in più, `tournament:write` (per iscriverti a un torneo, metterti in
  pausa e lasciarlo); `board:play`, per giocare, è già concesso. *Autorizza su Lichess…* (Invio) apre la pagina di
  Lichess; nel frattempo il tuo account resta collegato, e il token che sostituisce viene revocato.
- **Iscriversi a un'arena**: le sue condizioni come le giudica Lichess, ciascuna con i tuoi numeri (*Almeno 20 partite
  con rating, cadenza Rapida (tu: 312)*), con una condizione mancante segnalata prima di iscriverti; le sue regole, una
  riga ciascuna, e *Le regole complete su lichess.org*; *Partecipa* (Invio). Un rifiuto viene indicato con le parole di
  Lichess: per un'arena riservata ai membri di una squadra, *Apri la squadra su lichess.org*, perché RookStudio non
  gestisce le squadre.
- **In attesa di una partita**: Lichess abbina un giocatore lontano dalla pagina dell'arena solo per un minuto, quindi
  RookStudio ti iscrive di nuovo ogni minuto mentre aspetti; chiuso, non lo fa più, e non vieni più abbinato, mentre i
  tuoi punti restano. La prossima partita si apre da sola nella pagina Scacchiera.
- **La riga dell'arena**, sopra la parte e sopra la scheda della partita: la tua posizione su tanti giocatori, i tuoi
  punti, la serie (*la prossima vittoria conta doppio*), il tempo rimasto, e *Pausa* (Alt+P), che ti fa uscire dopo la
  partita in corso, mentre i tuoi punti restano; *Torna nell'arena* (Alt+P) ti iscrive di nuovo. La pagina dell'arena
  viene chiesta ogni 15 secondi; dopo un 429 (troppe richieste), la sua classifica aspetta un minuto e lo dice, mentre
  le tue partite continuano.
- **Giocare**: come qualsiasi partita su Lichess (vedi [Una partita su Lichess](#una-partita-su-lichess)), con
  *Berserk* dove l'arena lo permette, solo prima della tua prima mossa (metà del tuo tempo, un punto in più se vinci);
  Lichess non permette di ritirare una mossa in una partita di torneo, né di annullarla. Una partita di torneo in cui
  non giochi in tempo la prima mossa è persa, non annullata, e la scheda lo dice. Sotto la scheda, la partita
  precedente alla sua fine: *Vinta in 34 mosse · +2 punti · inizia la serie*.
- **Gli aiuti** restano bloccati mentre sei nell'arena una volta iniziata, anche tra due partite, perché la successiva
  arriva in pochi secondi: il motore, l'albero, il cloud, le tablebase, l'assistente e i rapporti (il dossier di un
  giocatore, un rapporto sull'apertura, una tabella incrociata). *Pausa* li restituisce (vedi [Fair play](#fair-play)).
- **La sua fine**: la tua posizione e i tuoi punti; ogni partita è in *Lichess · il tuo nome*, come le tue partite
  finite (vedi [Le tue partite finite](#le-tue-partite-finite)), con il loro resoconto proposto allora: *Analizza
  l'ultima partita*, *Le sue partite nella lista*. Chiudere RookStudio durante un'arena chiede prima: *Metti in pausa e
  chiudi*, o *Resta* (Esc).
- **Un torneo svizzero**, per i membri della sua squadra: il suo turno su tanti, la classifica dopo il suo ultimo turno
  (punti, spareggio, performance), le tue partite turno per turno, e l'ora del turno successivo. Due minuti prima, un
  avviso nell'angolo della finestra lo dice ovunque tu sia; la tua partita si apre da sola nella pagina Scacchiera, e una
  partita in cui non giochi in tempo la prima mossa è persa. *Le sue partite nel mosaico* mostra le partite del turno
  dei suoi primi otto. Una volta finito, *Conservalo in un database…* prende le sue partite, ciascuna con il suo turno,
  e la sua classifica (il suo file TRF) in *Lichess tournaments* o in un altro tuo database: la tabella incrociata del
  database poi la legge, riposi e ingressi in ritardo compresi (vedi [Tornei e tabelle
  incrociate](#tornei-e-tabelle-incrociate)), a un clic con *Apri la sua tabella incrociata*.

Cosa esce dalla macchina: per seguire, il torneo chiesto e le sue partite, senza account; per partecipare, con il tuo
token, l'iscrizione, la pausa e le tue mosse. Una richiesta alla volta, un minuto dopo un 429. Ctrl+K ha *Gioca un'arena
o un torneo svizzero su Lichess*, *Partecipa all'arena: …*, *Pausa nell'arena*, *Torna all'arena*, *Berserk* e
*Conserva lo svizzero in un database…*.

## La pagina Lichess
<!-- en: the-lichess-page 0c6f9833 -->

Qui tutto è facoltativo: RookStudio funziona senza account e senza Internet, e ciò che va a Lichess viene indicato dove
lo decidi. La sua colonna di destra raccoglie i servizi: le trasmissioni, i tornei di Lichess, Lichess TV, i giocatori
seguiti, le valutazioni conservate offline, *I tuoi motori su lichess.org* quando uno dei tuoi motori è offerto alla
scacchiera di analisi di lichess.org (vedi [Il tuo motore su lichess.org](#il-tuo-motore-su-lichessorg)), e ciò che
l'analisi può chiedere online.

### Collegare il tuo account
<!-- en: linking-your-account 8b1bc649 -->

*Collega l'account…* apre lichess.org nel browser, dove autorizzi RookStudio senza dargli una password. Lichess rimanda
il browser a 127.0.0.1, su una porta che solo RookStudio ascolta durante il collegamento; senza risposta entro 5
minuti, il collegamento si interrompe da solo. Quando non si apre nessun browser, compare l'indirizzo, da incollare in
un browser di questa macchina.

Una volta collegato, la scheda dà il nome dell'account e quando scade il suo token (un anno dopo; *Rinnova…* un mese
prima). RookStudio legge solo il tuo nome e i dati pubblici, e chiede di più quando una funzione ne ha bisogno: leggere
le trasmissioni, per esempio. Il token è conservato nel tuo profilo, leggibile solo da te; *Scollega* lo revoca su
Lichess e lo elimina qui, e i tuoi database restano. Un account collegato porta l'esploratore delle aperture di Lichess
nella scheda Albero (i maestri, tutti i giocatori, un giocatore), e le tue partite sincronizzate più in fretta, anche
quelle private.

### Le partite di un giocatore
<!-- en: a-players-games 3d841cf1 -->

*Le partite di un giocatore* importa le partite pubbliche di un giocatore di Lichess, tue o di un altro, in un database
(*Lichess · nome*), anche senza account: *Importa*, poi *Sincronizza* per le partite giocate da allora. Le partite
arrivano a blocchi, prima le più vecchie, e ciò che è stato ricevuto resta se annulli. *Cosa viene chiesto*: le cadenze
(tutte; blitz, rapide e classiche; classiche e per corrispondenza), da quando (la prima partita, un anno fa, una data),
solo scacchi standard e Chess960. Le partite mantengono il loro link a lichess.org, gli orologi e le valutazioni di
Lichess. *Conta le nuove partite quando si apre la pagina Home* interroga Lichess al massimo una volta all'ora, con il
nome del giocatore.

### I tuoi studi Lichess
<!-- en: your-lichess-studies 11bcbad8 -->

*I tuoi studi*, nella pagina Lichess, elenca gli studi del tuo account collegato, prima quelli modificati più di
recente. La prima volta che si apre la scheda, RookStudio chiede a Lichess il permesso di leggere i tuoi studi
(*Autorizza su Lichess…*): con esso vengono elencati anche i tuoi studi privati e non in elenco; *Senza permesso*,
solo quelli pubblici. L'elenco di Lichess dà il nome di ogni studio e quando è cambiato l'ultima volta; i suoi capitoli
compaiono una volta letto, e la sua visibilità per gli studi creati da RookStudio. Lo studio di un altro arriva tramite
il suo link (vedi [Link di Lichess](#link-di-lichess)).

Un clic su uno studio lo legge la prima volta: i suoi capitoli compaiono, e restano nel tuo profilo, anche offline.
*Apri un capitolo sulla Scacchiera* (o un doppio clic su di esso) lo apre, visto dal lato da cui lo mostra Lichess.
*Importa in un database* mette ogni capitolo in un database come partita: *Studi Lichess*, creato se manca, o un altro
scelto accanto. Se lo rifai, o con *Rileggi* una volta importato, vengono sostituiti solo i capitoli cambiati nel
frattempo e aggiunti quelli nuovi; una partita che hai eliminato lì resta eliminata. *Rendilo uno studio di teoria*
mostra i suoi commenti nella scheda Albero, riletti con lo studio (vedi [Studi di teoria](#studi-di-teoria)); uno
studio mai letto viene letto prima.

*Seguilo*: quando si apre la pagina Lichess (al massimo ogni cinque minuti), RookStudio chiede a Lichess se ogni studio
seguito è cambiato, una richiesta ciascuno, senza scaricarlo. Uno studio cambiato lo dice, nella scheda e sulla sua
riga quando la scheda è chiusa, con *Rileggi*: prima non viene scaricato niente. Dopo una lettura, vengono segnati i
capitoli cambiati rispetto alla lettura precedente.

**Dalla Scacchiera**, *lichess.org…* › *In uno studio Lichess…* apre accanto alla scacchiera una scheda che invia la
partita in uno dei tuoi studi (uno pieno, a 64 capitoli, non viene proposto) o in uno nuovo, di cui lì si scelgono il
nome e la visibilità: privato, non in elenco o pubblico, privato all'inizio. Il nome del capitolo, il lato da cui è
visto e se vanno anche i commenti dell'assistente (come semplici commenti dopo quelli della mossa) si scelgono nella
stessa scheda; Invio invia, Esc la lascia. Ciò che va è la partita in PGN: le sue intestazioni, le mosse e le varianti,
i tuoi commenti e simboli, le frecce e le case, gli orologi; i dati dei motori, le valutazioni e qualsiasi altra cosa
tua restano qui. Il primo invio chiede a Lichess il permesso di scrivere nei tuoi studi. Una volta inviata, *Aprilo su
lichess.org* o *Copia il suo link*.

In seguito, la striscia sopra le mosse dice *Inviata a Lichess · 2 modifiche da allora*, con *Invia le modifiche*: le
mosse, le varianti e le note del capitolo vengono sostituite su Lichess da quelle della partita, anche i suoi tag
quando sono cambiati. Niente parte da solo. Un capitolo aperto dai tuoi studi dice *Dal tuo studio Lichess*, e invia
le sue modifiche allo stesso modo.

Limiti di Lichess: 64 capitoli per studio, 30 nuovi studi al giorno.

### Trasmissioni ufficiali
<!-- en: official-broadcasts 9c582d92 -->

Le trasmissioni ufficiali in diretta, prima le più note (*Tutte le trasmissioni in diretta* per le altre), e le
prossime. *Segui* apre un turno in un mosaico nella pagina Lichess: la scheda di ogni partita con i nomi, gli orologi,
l'ultima mossa, la barra di valutazione, e il punteggio e la linea del motore scelto in *Analizzate da*; a destra, la
partita scelta con le sue ultime mosse, *Segui sulla scacchiera* e *Copia il PGN*, poi il turno. Le partite vengono
analizzate sulla tua macchina, con il tuo motore, tante insieme quante lo permettono i core lasciati liberi dai tornei,
prima la partita scelta, e solo mentre il mosaico è visibile. Sulla scacchiera la partita avanza a ogni mossa ricevuta;
tornare indietro per analizzarla la lascia avanzare dietro le quinte, e *Torna alla diretta* la riporta al punto
attuale; *Torna al mosaico* (Esc) riporta al turno.

Lichess permette di seguire quattro turni contemporaneamente senza account, otto con un account. Da agosto 2026 rallenta
molto una trasmissione seguita senza un account autorizzato a leggere le trasmissioni: la prima volta, RookStudio chiede
quel permesso (*Autorizza su Lichess…*, o *Segui senza*). Un turno le cui partite sono tutte finite va nel database
*Trasmissioni*.

### Ancora sulle trasmissioni
<!-- en: more-of-a-broadcast 3d035f0f -->

A destra del mosaico di una trasmissione, sotto il turno, **la classifica**: il posto, il titolo, l'Elo e i punti di
ogni giocatore come li danno gli organizzatori (la federazione, la squadra, la performance e la variazione Elo passando
il mouse), i primi dodici o tutti; per un evento a squadre, prima le squadre, per punti squadra e poi per punti
individuali. Lichess la dà senza account; viene richiesta di nuovo ogni due minuti mentre il mosaico è visibile.

*Tutto il torneo*, accanto al nome di un turno, segue tutti i turni in corso del torneo, e ogni turno successivo appena
inizia: il mosaico mostra il turno in diretta, e **I turni** sopra di esso ne mostra un altro (un clic su quello in
diretta segue di nuovo i successivi). Ogni turno va nel database *Trasmissioni* quando finisce. *Tabella incrociata*,
accanto alla classifica, mostra la tabella incrociata dei turni seguiti al posto delle scacchiere (vedi [Tornei e tabelle
incrociate](#tornei-e-tabelle-incrociate)).

**Cerca tra le trasmissioni**, in cima alle trasmissioni ufficiali, cerca una trasmissione recente per nome: Lichess
risponde solo a un account autorizzato a leggere le trasmissioni, permesso chiesto la prima volta. *Torna alla lista*
mostra di nuovo le trasmissioni in corso.

### Link di Lichess
<!-- en: lichess-links c975cfa4 -->

Un indirizzo di Lichess incollato con Ctrl+V nella pagina Scacchiera o nella pagina Database viene riconosciuto senza
chiedere niente a Lichess: una partita (`lichess.org/abcd1234`, con `/white`, `/black` o una mossa dopo `#`), uno
studio o uno dei suoi capitoli, un turno o un torneo di una trasmissione, un'arena, un torneo svizzero, un giocatore
(`lichess.org/@/name`) o i tuoi preferiti. Un indirizzo che RookStudio non conosce viene segnalato, e non viene chiesto
niente.

Una scheda dice poi cos'è, dove vanno le sue partite e cosa viene chiesto a Lichess; Invio esegue, Esc la lascia:

- una partita o un capitolo si apre sulla scacchiera, dal lato e alla mossa che mostra l'indirizzo, o va in un
  database;
- uno studio, una trasmissione, un'arena, un torneo svizzero o i tuoi preferiti entrano in un database in background,
  con le partite contate man mano che arrivano, e *Annulla*; poi *Apri il database*;
- un turno in corso si può invece seguire in diretta (vedi [Trasmissioni ufficiali](#trasmissioni-ufficiali));
- le partite di un giocatore vanno nel suo database, tramite [la sua sincronizzazione](#le-partite-di-un-giocatore).

Il database proposto è quello scelto nella pagina Database quando il link è stato incollato lì, altrimenti *Partite
Lichess*, *Studi Lichess*, *Trasmissioni*, *Tornei Lichess* o *Preferiti Lichess*, creato se manca; se ne può scegliere
un altro. Una partita viene chiesta tramite il suo id, senza account; uno studio privato con il tuo token; le
trasmissioni con un account autorizzato a leggerle, permesso chiesto una volta, come per seguire un turno; i tuoi
preferiti richiedono un account collegato.

### Fair play
<!-- en: fair-play ca194626 -->

Lichess vieta qualsiasi aiuto durante una partita. Con il tuo account collegato, RookStudio rispetta questa regola
ovunque tu giochi, nel browser o su un telefono:

- finché dura una delle tue partite in diretta, la pagina Scacchiera blocca le schede Analisi, Albero, Riferimenti e
  Domande, e né il motore, né la minaccia, né le tablebase, né il cloud, né l'assistente guardano alcuna posizione; le
  scacchiere non mostrano né le frecce dell'analisi né la sua valutazione, comprese quelle della ricerca interrotta dal
  blocco. Un resoconto in corso si ferma, e i passaggi già fatti vengono conservati. I tornei tra motori continuano,
  senza analisi sulla loro Scacchiera. Tutto torna da solo quando la partita finisce.
- una partita per corrispondenza blocca il motore, il cloud, le tablebase e l'assistente sulla partita sulla
  scacchiera che passa per la sua posizione, dalla prima mossa a ciò che la segue, compresa una posizione di quella
  partita mostrata da una variante; lì Lichess permette un libro e un database, quindi l'albero dei tuoi database e le
  loro partite di riferimento restano aperti. Prima della sua prima mossa, blocca solo la posizione iniziale: ogni
  partita vi passa. Su qualsiasi altra partita, tutto funziona.
- mentre sei in un'arena da RookStudio, ogni aiuto resta bloccato anche tra due partite, e con essi i rapporti, fino a
  *Pausa* (Alt+P) o alla fine dell'arena (vedi [Arene e tornei svizzeri](#arene-e-tornei-svizzeri)).

Un banner nella pagina Scacchiera dice quale partita blocca cosa. La scheda Lichess della pagina Home elenca le tue
partite in corso, prima quelle in cui tocca a te, ciascuna con il suo orologio o il tempo rimasto per muovere, e *Apri
su lichess.org*, o *Sulla Scacchiera* una volta che RookStudio può giocare le tue partite (vedi [Una partita su
Lichess](#una-partita-su-lichess)). Le partite del tuo bot Lichess non sono tue e non bloccano niente; durante una tua
partita in diretta, la ricerca del bot non viene mostrata (vedi [Un bot Lichess](#un-bot-lichess)). Lichess TV non
mostra mai le tue partite, nemmeno in evidenza, e i motori del mosaico restano a riposo durante la tua partita in
diretta (vedi [Lichess TV](#lichess-tv)). Il tuo motore offerto alla scacchiera di analisi di lichess.org si mette in
pausa durante la tua partita in diretta, e non analizza mai le posizioni delle tue partite per corrispondenza (vedi [Il
tuo motore su lichess.org](#il-tuo-motore-su-lichessorg)).

Le tue partite in corso vengono chieste con il token del tuo account quando compare la pagina Home, ogni mezzo minuto
finché dura una partita in diretta e ogni dieci minuti finché dura una partita per corrispondenza. La pagina Lichess
propone una volta di sapere invece quando iniziano e finiscono, mentre RookStudio è aperto: un permesso in più, leggere
le tue sfide; il permesso di giocare le tue partite (vedi [Su Lichess](#su-lichess)) le ascolta tramite lo stesso
flusso. RookStudio legge le mosse solo delle partite che giochi da esso, e gioca solo le mosse che giochi tu. Senza
nessuno dei due permessi, il blocco arriva quando compare la pagina Home.

### Le tue partite finite
<!-- en: your-finished-games 3109239c -->

Quando una delle tue partite finisce, RookStudio la porta in *Lichess · tuo nome*, un database creato se manca, con i
suoi orologi, la sua apertura e, quando hai chiesto a Lichess di analizzarla, le valutazioni di Lichess. Un banner
nella pagina Scacchiera, e una riga nella pagina Home, dicono come è finita; *Esaminala* (Invio) chiede di nuovo a
Lichess la sua analisi, che nel frattempo può essere stata chiesta, apre la partita e ne avvia il resoconto. *Non ora*
(Esc) mette da parte il banner.

L'analisi di Lichess compare allora accanto a quella del tuo motore: la sua curva sul grafico, e la precisione secondo
ciascuna analisi sotto i numeri dei due lati nella scheda Resoconto, con la stessa formula per entrambe. *Esamina subito
le mie partite finite* della pagina Lichess avvia il resoconto da solo, a meno che la Scacchiera non contenga una
partita con modifiche non salvate o un lavoro in corso; in quel caso lo propone il banner. Le partite di un'arena
arrivano allo stesso modo, con il loro resoconto proposto alla fine dell'arena.

### Giocatori seguiti
<!-- en: followed-players 9a60f4cb -->

La scheda *Giocatori seguiti* della pagina Lichess elenca i giocatori che il tuo account segue su Lichess e quelli che
aggiungi per nome, ciascuno con un punto: rosso mentre gioca, con la cadenza della partita, verde quando è online. Il
loro stato viene chiesto ogni quindici secondi mentre la scheda o il loro mosaico sono visibili, senza account.

*Le loro partite nel mosaico* mostra le partite che stanno giocando nel mosaico delle trasmissioni, ciascuna analizzata
dal tuo motore, otto al massimo, quante Lichess ne permette a un indirizzo, contando con esse i turni che segui e le
partite di Lichess TV; Lichess mostra le loro mosse con tre mosse di ritardo, a chiunque. Una partita che finisce resta
con il suo risultato finché un'altra non ne prende il posto. Leggere chi segue il tuo account richiede un permesso in
più, chiesto la prima volta; *Senza permesso*, vengono seguiti i giocatori aggiunti per nome. Il tuo account e le tue
partite non compaiono mai lì.

### Lichess TV
<!-- en: lichess-tv 820042bb -->

La scheda *Lichess TV* della pagina Lichess elenca i canali degli scacchi standard: *I più forti*, *Bullet*, *Blitz*,
*Rapida*, *Classica*, *UltraBullet*, *Bot* e *Computer* (giocatori contro l'IA di Lichess); i canali delle varianti non
vengono mostrati. Ognuno dà la partita che Lichess vi mette in evidenza, i suoi due giocatori con i loro rating, e la
sua cadenza. L'elenco viene riletto quando compare la scheda, al massimo una volta ogni 30 secondi.

*Il mosaico* mostra le partite in corso del canale nel mosaico delle trasmissioni, la sua terza fonte: la riga in alto
passa dall'una all'altra (*Trasmissioni*, *Giocatori seguiti*, *Lichess TV*, *Tornei Lichess*), e i canali sono dei
chip sotto di essa.
Gli stessi riquadri, un tuo motore per ogni partita, la partita scelta a destra con *Segui sulla scacchiera*. Lichess
elenca fino a 30 partite per canale, e permette a un indirizzo di seguire 8 partite alla volta, contando con esse i
turni che segui (uno ciascuno) e le partite dei giocatori seguiti: il mosaico mostra ciò che resta, prima la partita in
evidenza. Una partita finita mostra il suo risultato per qualche secondo, poi lascia il posto a un'altra del canale.
Lichess mostra le mosse con tre mosse di ritardo a chiunque guardi, e il mosaico lo dice.

*La Scacchiera* (o un doppio clic su un riquadro) apre la partita sulla Scacchiera, che la segue; la scheda sopra la sua
colonna dice cos'è: *Lichess TV · Blitz · la partita in evidenza, 3+0*. Quando finisce, la Scacchiera passa da sola alla
prossima partita in evidenza del canale, e la partita lasciata resta a un Ctrl+Tab di distanza; *Resta su questa
partita* tiene la scacchiera su di essa. *Conserva questa partita…* la mette, alla fine, in un database a tua scelta:
*Lichess TV*, creato se manca, o un altro dei tuoi; fino ad allora aspetta nel tuo profilo, anche se RookStudio si
chiude. *Torna al mosaico* (Esc) riporta al mosaico del canale.

Nella pagina Motori, la scheda di un motore ha *Lichess TV: Bot, Computer*: i migliori bot di Lichess, o giocatori
contro la sua IA, nel mosaico, con quel motore che analizza ogni partita.

Le tue partite non compaiono mai, nemmeno in evidenza: quelle del tuo account collegato e del tuo bot Lichess (vedi [Un
bot Lichess](#un-bot-lichess)). Finché dura una delle tue partite in diretta, i motori del mosaico restano a riposo
(vedi [Fair play](#fair-play)). Cosa esce dalla macchina: il canale chiesto e le partite seguite, senza account; niente
viene conservato da solo. Ctrl+K ha *Lichess TV*, ogni canale nel mosaico (e sulla Scacchiera una volta letta la
scheda), e, per la partita della Scacchiera, restarci o passare alla prossima partita del canale, conservarla, e tornare
al mosaico.

### Tornei Lichess
<!-- en: lichess-tournaments fe42cfa6 -->

La scheda *Tornei Lichess* della pagina Lichess elenca le arene di Lichess in corso e in arrivo, lette senza account:
*Rapida*, *Classica* o *Tutte*, ogni riga con il suo nome, la sua cadenza, quando (*mancano 38 min*, *tra 40 min*), i
suoi giocatori, *Segui*, e *Partecipa* o il motivo per cui non si può: *Blitz: su lichess.org* (un clic lo apre),
*condizione non soddisfatta* (sotto il mouse, quale, con i tuoi numeri). Le arene delle varianti non sono mostrate;
l'elenco viene riletto quando compare la scheda, al massimo una volta al minuto.

*I tornei svizzeri delle tue squadre*, sotto, elenca quelli delle squadre di cui fai parte: leggere le tue squadre
richiede un permesso in più, `team:read`, chiesto una volta sola (*Leggi le mie squadre…*). Ogni riga indica i suoi
turni, la sua cadenza, quando, i suoi giocatori, e *Partecipa*; *Segui* mentre è in corso; *Conservalo in un
database…* una volta finito.

*Segui* mostra il torneo nel mosaico delle trasmissioni, la sua quarta fonte accanto a *Trasmissioni*, *Giocatori
seguiti* e *Lichess TV*: le partite in corso dei suoi primi otto, come le dà il torneo, con una partita finita che lascia
il posto alla successiva; Lichess mostra le loro mosse con tre mosse di ritardo, e permette a un indirizzo di seguire
otto partite alla volta, contando con esse i turni e i giocatori che segui; ciascuna analizzata dal tuo motore, come fa
il mosaico. A destra, la sua classifica, chiesta ogni 15 secondi: il posto di ogni giocatore, il rating, il foglio (i
punti di ogni partita, la più recente per ultima come su lichess.org, i punti raddoppiati di una serie in arancione) e
il punteggio; quella di uno svizzero dopo il suo ultimo turno. Un clic sceglie una partita, un doppio clic la apre nella
pagina Scacchiera; la tua partita non compare mai. Uno svizzero non elenca le sue partite in corso: vengono trovate per
giocatore, quelle dei suoi primi otto, a ogni turno. Senza account; niente viene conservato da solo. Ctrl+K ha *Tornei
Lichess: arene e tornei svizzeri*, *Segui l'arena: …* (una volta che la scheda ha letto l'elenco) e il torneo seguito
nel mosaico.

### Preparare una partita
<!-- en: preparing-a-game 2c8891af -->

La scheda *Prepara una partita contro* della pagina Lichess prende un nome Lichess e apre il suo dossier (vedi [Il
dossier di un giocatore](#il-dossier-di-un-giocatore)) con la sua fonte Lichess spuntata. Le partite dell'avversario
nell'ultimo anno, blitz, rapide e classiche, entrano in un database a parte, *Lichess · nome*, sincronizzato di nuovo
la volta successiva, e il suo profilo pubblico ne dà il livello. Il dossier mostra poi cosa gioca online accanto a ciò
che i tuoi database hanno di lui (il giocatore di cui è l'account Lichess, una volta collegato), le tue partite contro
di lui (con il tuo account collegato), e le linee che gioca spesso a cui i tuoi repertori non rispondono, su entrambe le
fonti, prima le più frequenti; un clic su una di esse la apre sulla Scacchiera, con la scheda Albero sulle sue partite.
A Lichess vanno solo il suo nome e quei filtri. Mentre una tua partita contro di lui si gioca su Lichess, il dossier non
mostra niente di lui.

### Le valutazioni di Lichess, offline
<!-- en: lichesss-evaluations-offline e972bc19 -->

La scheda *Valutazioni di Lichess, offline* conserva sul tuo disco ciò che i server di Lichess hanno calcolato per 418
milioni di posizioni (database.lichess.org, CC0): per ciascuna, la valutazione, la profondità e la prima mossa della sua
ricerca più profonda. Dopo non si chiede più niente alla rete, e una valutazione compare subito ovunque Lichess ne
abbia una.

- **Cosa viene conservato**: tutte le posizioni (circa 3,6 GB), o solo le aperture (circa 1,5 GB), cioè le posizioni
  con 24 pezzi o più sulla scacchiera, pedoni compresi. Il file di Lichess non dà il numero di mossa: nelle partite di
  Lichess, 24 pezzi o più sono l'89 % delle posizioni delle prime 20 mosse e l'8 % di quelle successive. Le linee di
  Lichess sono escluse: occuperebbero quasi cinque volte lo spazio, per ciò che il tuo motore ritrova in un secondo.
- **Scarica e prepara** (Invio) legge il file di Lichess, circa 21 GB, mentre si scarica, e ne conserva solo ciò che
  hai scelto; *Un file già scaricato…* legge invece `lichess_db_eval.jsonl.zst` dal tuo disco. Durante la preparazione
  serve poco più del doppio dello spazio di ciò che conserva; la scheda dice quanto spazio libero ha il tuo disco.
- Le posizioni lette rispondono qualche milione alla volta, senza aspettare la fine. *Annulla*, un'interruzione di rete
  o la chiusura del programma: *Continua* riprende il file da dove si era fermato, *Ricomincia* dimentica ciò che è
  stato letto.
- Pronta, la scheda dà le posizioni, la dimensione e la data del file. Lichess ripubblica il file ogni mese: *Aggiorna*
  chiede a database.lichess.org la sua data e legge un file più recente, mentre le valutazioni conservate continuano a
  rispondere finché non è pronto. *Rimuovile* libera il disco.

Compaiono in tre punti:

- la scheda Analisi: una riga sopra il motore, *Lichess offline · +0.31 · profondità 46 · 6…e6*, la valutazione dal
  punto di vista del Bianco (un clic mostra la mossa sulla scacchiera);
- il resoconto di una partita: una posizione nota a profondità 30 o più viene presa così com'è, senza ricerca, e il
  motore cerca le altre (il suo ultimo passaggio, 5 secondi per posizione per impostazione predefinita, raramente va
  più a fondo);
- la scheda Albero: una colonna *Val.*, la valutazione dopo ogni mossa.

Cosa esce dalla macchina: la richiesta del file a database.lichess.org, e nient'altro. Mentre una delle tue partite si
gioca su Lichess, si nascondono come fa il motore (vedi [Fair play](#fair-play)).

### Online per l'analisi
<!-- en: online-for-the-analysis 1f447190 -->

Tre servizi, per ciascuno dei quali viene chiesto al primo utilizzo (per questa posizione, sempre, o non ora) e che si
cambiano qui:

- **La valutazione cloud di Lichess**, nella scheda Analisi: viene inviato il FEN di ogni posizione mostrata, senza
  account, e Lichess risponde con le linee che i suoi server hanno già calcolato, quando le ha. Il tuo motore risponde
  sempre.
- **Finali online, fino a 7 pezzi**: per una posizione con sette pezzi o meno che le tablebase sul tuo disco non
  conoscono, il suo FEN viene inviato alle tablebase di Lichess, senza account. Danno il verdetto e quanto è lontana la
  fine, nella scheda Analisi, e ogni mossa secondo il suo esito nella scheda Finali, con le linee principali delle sue
  mosse (vedi [La scheda Finali](#la-scheda-finali)). Una vittoria che la regola delle cinquanta mosse trasforma in
  patta, una «cursed win» nelle parole delle tablebase, viene detta «vinta, ma patta per la regola delle cinquanta
  mosse». Con otto pezzi e pedoni contrapposti, danno il verdetto e le mosse fino alla prossima cattura, promozione o
  matto. La verifica dei finali di un torneo finito chiede loro qualche posizione per partita, una volta autorizzate per
  quella verifica o sempre (vedi [Verificare i finali con le
  tablebase](#verificare-i-finali-con-le-tablebase)).
- **L'esploratore di Lichess**, nella scheda Albero: per ogni posizione, il suo FEN e il filtro scelto vengono inviati
  con il tuo token; richiede un account collegato. Non viene inviato niente dei tuoi database.

Le risposte vengono conservate nel tuo profilo: quelle delle tablebase per sempre, quelle del cloud per un mese. Quando
Lichess chiede una pausa (troppe richieste in poco tempo), compare il conto alla rovescia; offline, viene indicato ciò
che funziona ancora.

## L'assistente
<!-- en: the-assistant d3ffeeba -->

L'assistente commenta le tue partite e risponde alle tue domande su di esse, sulla tua macchina. Si basa su ciò che
stabiliscono il tuo motore, i tuoi database e le tablebase, e lo dice a parole; quando niente lo stabilisce, lo dice.
Scrive in inglese o in francese (in inglese per le altre lingue dell'interfaccia).

### Il suo modello
<!-- en: its-model 1e5f4544 -->

L'assistente ha bisogno del suo modello, *RookStudio commentary v11*, scaricato una volta sola da huggingface.co (circa
2,7 GB sul disco, e circa 3 GB di memoria mentre è in uso), verificato all'arrivo; un download interrotto riprende da
dove era arrivato. Il primo riquadro della scheda Domande, di *Commenta questa mossa* e del resoconto lo propone; *Un
file GGUF sul disco…* prende un altro modello, che risponde alle domande ma non ha imparato i commenti di RookStudio
(quelli restano predefiniti). Solo il download del modello passa da Internet: le tue partite, le tue domande e le sue
risposte restano sulla macchina.

Gira in un processo proprio, sulla GPU quando ce n'è una utilizzabile (un commento in circa un quarto di secondo),
altrimenti sul processore (circa 3 secondi). Viene caricato alla prima domanda o al primo commento, e scaricato dalla
memoria dopo un quarto d'ora di inattività, o con *Scarica dalla memoria* (nella pagina Home, in **Impostazioni**, o in
Ctrl+K).
Mentre un torneo usa la GPU, risponde sul processore (*Usa comunque la GPU* se vuoi); un driver grafico che non
funziona lo manda sul processore, e *Riprova la GPU* riprova. *Impostazioni* > *Assistente*: il suo modello, la GPU o
il processore, i minuti prima che venga scaricato dalla memoria (0: resta caricato finché non premi *Scarica dalla
memoria*).

Senza il modello, tutto il resto funziona: l'analisi, i numeri del resoconto, i commenti predefiniti, l'albero.

### Domande
<!-- en: questions e1e09fc4 -->

La scheda Domande (Alt+5) accetta una domanda sulla posizione o sulla partita, digitata o scelta tra i suggerimenti:
perché l'ultima mossa, qual è la minaccia, la mossa migliore, chi ha giocato questa posizione, che apertura è questa.
Sa spiegare una mossa, dare la mossa migliore o la minaccia, provare una mossa, confrontarne due; dare le partite di
riferimento e le mosse giocate in un database; nominare l'apertura; dire cosa dicono le tablebase. Ogni risposta mostra
su cosa si basa (la ricerca del motore, l'albero, i nomi delle aperture, le tablebase) e le sue azioni: giocare la
mossa, mostrare la linea in anteprima, aprire la partita più forte, aggiungere la risposta alla notazione. Una domanda
che va oltre ciò che sa fare riceve un «Non so dirlo» e ciò che può mostrare invece.

Risponde per lui il motore della scheda Analisi, in un processo proprio (un thread, 300.000 nodi per posizione): le
domande richiedono un motore nella libreria.

### I commenti dell'assistente
<!-- en: comments-by-the-assistant 009c27cb -->

*Commenta questa mossa* (Alt+C, o Ctrl+K) scrive il commento dell'assistente sulla mossa mostrata, e *Commenta i
momenti chiave*, nel resoconto, uno su ciascun momento chiave. I suoi commenti sono mostrati in grigio azzurro e in
corsivo, dopo un fumetto; passando il mouse, su cosa si basa ciascuno: il motore, i fatti delle posizioni, il database
della teoria, il modello o un testo predefinito. Senza il modello, i commenti sono predefiniti, scritti a partire dagli
stessi fatti.

Una mossa che ha già un commento (tuo, dell'analisi di Lichess di una partita, dell'annotatore di una partita) lo
conserva così com'è: quello dell'assistente viene dopo, a parte, in un riquadro del suo colore sotto il suo nome. Nel
PGN sono due commenti di seguito, quello dell'assistente segnato `[%rs assistant]`; un altro programma legge due
commenti, e RookStudio ridà a ciascuno il suo posto. Ctrl+Z toglie il commento dell'assistente. Un doppio clic lo
corregge, e diventa tuo: su una mossa che ha già un tuo commento, il testo corretto viene aggiunto dopo di esso, come un
unico commento. Anche una risposta della scheda Domande aggiunta alla notazione è un commento dell'assistente.

## Suite di test
<!-- en: test-suites 42c2b2ba -->

La pagina **Suite** esegue una suite di test EPD: posizioni con la loro mossa migliore (`bm`), le mosse da evitare
(`am`), o, per la Strategic Test Suite, i punti di più mosse (`c0 "f5=10, Be5+=2"`).

- **Suite**: un file EPD, o un libro EPD della libreria. Le righe senza una posizione legale o senza una di queste
  operazioni vengono escluse (ne viene mostrato il numero).
- **Motori**: un numero qualsiasi di motori della libreria, con le loro opzioni; *Hash* e *Threads* impongono un
  valore a ciascuno (0 = quello di ogni motore).
- **Tempo**, **Nodi** o **Profondità** per posizione. *Trovata da (semimosse)*: una ricerca si ferma quando una
  soluzione è rimasta la prima mossa del motore per altrettante semimosse di profondità in più (0 = ogni ricerca usa
  tutto il suo tempo).
- **Parallelismo automatico**: con un tempo, core fisici − 1 divisi per i thread dei motori; con nodi o una
  profondità, ogni thread hardware.

Una posizione è risolta quando la mossa finale del motore è una soluzione: una delle `bm`, nessuna delle `am`, o, con
soli punti, la mossa con il punteggio più alto. Il suo tempo di soluzione è il momento in cui il motore si è fissato su
quella mossa. La griglia mostra le posizioni contro i motori, ✓ con il tempo di soluzione o ✗ con la mossa giocata, e
i punti STS; i totali vengono prima: posizioni risolte, il loro tempo medio e il punteggio STS. Un'esecuzione
terminata viene conservata in `suites/` nel profilo; *Confronta con un'esecuzione salvata…* aggiunge le colonne di
un'esecuzione precedente della stessa suite, per confrontare versioni di un motore. *Esporta CSV…* scrive la griglia.

## Nuovo torneo
<!-- en: new-tournament 6d169de4 -->

**Tornei → Nuovo torneo** apre la configurazione: una scheda di righe ripiegate che dicono come si giocano le partite,
i partecipanti con la libreria dei motori accanto, e un riepilogo aggiornato in tempo reale a destra (numero di
partite, partite per motore, durata stimata, aperture, impostazioni comuni, handicap di tempo). La configurazione
dell'ultimo torneo viene conservata, quindi testare di nuovo due versioni di un motore significa: scegli A e B,
**Avvia**.

Tutto nella pagina si può fare da tastiera: Tab e le frecce spostano tra le righe, gli elenchi e i pulsanti, Invio o
Spazio aprono una riga o un elenco, e lì funzionano i tasti dei [partecipanti e della
libreria](#partecipanti-e-libreria). **Avvia** crea il torneo e lo gioca; **Metti in coda** lo tiene per dopo (vedi
[Coda e rilancio](#coda-e-rilancio)).

### Il riepilogo e i suoi controlli
<!-- en: the-summary-and-its-checks 5609344a -->

Il riepilogo indica la memoria che possono occupare le tabelle hash: due processi di motore per ogni partita giocata
in contemporanea, e i motori in attesa della loro prossima partita, ciascuno con il suo `Hash`. Oltre la memoria
libera avvisa (il sistema userà lo swap), nominando il motore con l'`Hash` più grande; oltre la memoria installata il
torneo non parte. `--run` dice lo stesso. Quando il budget di memoria di **Impostazioni** permette di giocare insieme
meno partite di quante ne indica il parallelismo, il riepilogo dice quante (ogni partita occupa l'`Hash` dei suoi due
motori e ciò che i loro processi occupano in più), e così la riga *Parallelismo*; lo stesso per le partite con un
motore che usa una GPU, rispetto alle *GPU*. Nomina anche i motori il cui programma è cambiato dal loro ultimo torneo
(una nuova build), con *Cosa è cambiato* quando la scheda del motore elenca nuovi predefiniti o nuove opzioni (vedi
[Programmi ricompilati](#programmi-ricompilati)).

Ciò che andrebbe storto viene detto con il motore, la risorsa e la soluzione, un pulsante sotto la riga (la tavolozza
dei comandi elenca le stesse soluzioni):

- un programma o una cartella di lavoro non trovati: *Apri il motore* (la sua scheda nella pagina Motori),
  *Individua…*; un libro di aperture non trovato: *Individua…*;
- un motore il cui `Threads` proprio (la sua impostazione, altrimenti il suo predefinito: alcuni motori prendono tutti
  i core) supera la metà dei thread hardware, o i thread di cui dispone ogni partita con le partite giocate
  insieme (i core fisici con un orologio): *Abbassa Threads a N* imposta l'opzione del motore nella libreria, *Apri il
  motore*. I `Threads` dati dalla configurazione oltre le capacità della macchina ricevono *Abbassa Threads* dove sono
  dati (la cella del partecipante, le impostazioni comuni). L'elenco dei partecipanti marca un motore del genere con
  un *!*, i motori del match sono segnalati sotto di essi;
- un valore di opzione che un motore rifiuta (fuori dal suo intervallo, non tra le sue scelte, un'opzione che non ha
  più): fastchess non lo invia, e il motore gioca con il suo predefinito;
- le tablebase Syzygy che usa il torneo: le cartelle dell'aggiudicazione e ciò a cui punta il `SyzygyPath` di ogni
  motore (più cartelle separate da `;` su Windows, `:` altrove). Per i pezzi usati (i *Pezzi max* dell'aggiudicazione,
  altrimenti le tabelle più grandi trovate), deve esserci ogni file WDL (`.rtbw`), e nessun file troncato (un file
  Syzygy è lungo 16 byte in più di un multiplo di 64; i motori escludono un file di altra dimensione, e alcuni si
  fermano quando lo incontrano): *Syzygy 6 pezzi: 14 file mancanti in D:\TB (KRPPvKR.rtbw…)*, *Aggiudicazione fino a
  6 pezzi, ma solo file da 5 pezzi trovati*, *SyzygyPath di Alpha punta a una cartella senza file di tablebase*, con
  *Apri la cartella* e *Abbassa a 5 pezzi*; il tooltip elenca i file. Sono avvisi: l'aggiudicazione e i motori fanno a
  meno dei file mancanti. Solo un'aggiudicazione senza cartella, o le cui cartelle non contengono tabelle, ferma il
  torneo al suo avvio (fastchess lo rifiuta): *Individua…* sceglie un'altra cartella. Le cartelle vengono lette in
  background, e di nuovo ogni pochi secondi mentre il riepilogo è mostrato.

Un avvio rifiutato nomina gli stessi problemi, con le loro soluzioni e *Apri il log*. Ogni problema ha un **?** che
apre la sua voce in [Risoluzione dei problemi](#risoluzione-dei-problemi).

### Profili e modelli
<!-- en: profiles-and-templates df08e65d -->

**Profili.** Le prime cinque righe, *Formato*, *Cadenza*, *Aperture*, *Aggiudicazione* e *Impostazioni dei motori*,
sono ciascuna un profilo con un nome: il suo elenco ne sceglie uno, la riga accanto dice cosa gioca ("40 mosse in
15 min, ripetuta", "patta 40/8/10 cp · TB 6 pezzi"; il suo tooltip dà lo stesso come lo scrivono cutechess e
fastchess, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), e ✎, o un clic su quella riga, apre i dettagli
sul posto, sotto di essa. Una modifica fatta solo per questo torneo mette un punto accanto al profilo, e il suo
elenco e i suoi dettagli propongono *Aggiorna* (il profilo prende la modifica, per ogni torneo e modello che lo usa),
*Ripristina* e *Salva come profilo…* (con un nuovo nome). Il programma include dei profili predefiniti:

- cadenze: 10+0.1 e 60+0.6 (i test brevi e lunghi di fishtest), 2+1, 40/4, 40/15 e 40/20 (40 mosse in 4, 15 o 20
  minuti, ripetute, come le giocano le liste Elo), e 200k nodi per mossa;
- formati: SPRT miglioramento [0, 3] e non regressione [-5, 0], un match di 1000 partite, un girone all'italiana, un
  gauntlet, un sistema svizzero di 9 turni, un'eliminazione diretta e un incontro a squadre;
- aggiudicazioni: nessuna, una standard (patta dalla mossa 40 dopo 8 mosse entro 10 cp, sconfitta dopo 3 mosse a
  600 cp) e la stessa con le tablebase fino a 6 pezzi;
- impostazioni dei motori: quelle di ogni motore, o Hash 16, 64 o 256 MB con 1 thread e i libri propri dei motori
  disattivati;
- aperture: nessun libro, o le 960 posizioni di Chess960.

Un tuo profilo salvato con il nome di un predefinito ne prende il posto; eliminarlo (dal suo elenco, dopo una
conferma) riporta il predefinito, e le configurazioni e i modelli che lo usavano ne conservano i valori. I profili
sono conservati in `profiles.json` nella cartella del profilo, e la tavolozza dei comandi (Ctrl+K) li elenca:
sceglierne uno lo applica alla configurazione. La tavolozza apre anche ogni riga della configurazione per titolo, e
carica o salva i modelli. Le cartelle Syzygy non sono in nessun profilo: appartengono a questo computer, e
l'aggiudicazione e i motori le condividono. Le righe *Parallelismo* e *PGN e varie* contengono ciò che è proprio solo
di questo torneo.

**Modello** e **Nome**, accanto ad *Avvia*: un modello è l'intera configurazione, un profilo per ogni sezione più ciò
che nessun profilo contiene (il nome, i partecipanti, il parallelismo, i file PGN, la trasmissione). Il menu ne carica
uno o salva la configurazione come modello (*Salva la configurazione come modello…*, che chiede conferma prima di
sostituire un modello con lo stesso nome), e anche la tavolozza dei comandi li carica. Una sezione che seguiva il suo
profilo quando il modello è stato salvato segue quel profilo, quindi aggiornare un profilo aggiorna ogni modello che
lo usa; una sezione modificata per quel torneo conserva i propri valori. I modelli salvati da versioni precedenti si
caricano com'erano, e le loro sezioni ricevono i profili a cui corrispondono. Un nome vuoto viene composto dai
motori.

### Formato
<!-- en: format 29a36e7c -->

Il tooltip di ogni formato dice di cosa si tratta.

- *Match*: due motori, A (il motore in prova; ogni statistica è dal suo punto di vista) e B (il riferimento). Un
  numero fisso di partite, fino a quando lo fermi, o un SPRT: elo0, elo1, alpha, beta, modello (`normalized` usa limiti
  in nElo come fishtest e OpenBench, `logistic` l'Elo classico, `bayesian` il BayesElo) e un numero massimo di partite
  facoltativo. I profili SPRT miglioramento e non regressione danno i limiti abituali, e la riga sotto i limiti dice
  quante partite durerà probabilmente il test. *Calcolatore…* disegna, come fa il calcolatore SPRT di fishtest, la
  probabilità che H1 sia accettata e il numero atteso di partite in funzione della vera differenza Elo, a partire da
  una percentuale di patte attesa e dal bias RMS delle aperture; *Usa questi limiti* ricopia nella configurazione i
  limiti che gli sono stati dati. *Scarto Elo* (0 per impostazione predefinita) viene aggiunto a entrambi i limiti, per
  testare contro una versione nota per essere più forte o più debole di tanto: [0, 3] con uno scarto di 5 dà l'SPRT
  [5, 8], che il riepilogo, la panoramica e la modalità headless scrivono `SPRT [0, 3] + 5`; il calcolatore e la riga
  sotto i limiti ne tengono conto. *Partite min.* (0, nessun minimo, per impostazione predefinita) trattiene la
  decisione finché non sono state giocate altrettante partite, mentre l'LLR viene comunque calcolato e mostrato, per
  proteggere un test breve da un arresto precoce dovuto alla fortuna: un LLR oltre un limite prima di allora viene
  segnalato sotto la barra dell'LLR e il match continua; una volta raggiunte le partite, la prossima coppia che
  termina con l'LLR oltre un limite termina il match. Un massimo di partite inferiore al minimo termina il match senza
  decisione. È il modo più rapido per confrontare due versioni di un motore, e non serve altro.
  *Colori* (*Alternati* per impostazione predefinita) dice chi ha il Bianco: A e B a turno, oppure *A sempre col Bianco*
  o *B sempre col Bianco*, come `-noswap` (e `-reverse`) di fastchess, per le partite con handicap in cui il motore più
  forte deve avere sempre il lato svantaggiato, o un'apertura provata da un solo lato. Le partite vanno sempre a coppie,
  entrambe con gli stessi colori, ma ogni partita ha la propria apertura, la successiva del libro (come `-games 1` di
  fastchess): le coppie e le loro statistiche restano, il punteggio include il vantaggio del Bianco, e il bias RMS, che
  invertire i colori annulla, non viene mostrato. Il riepilogo e l'intestazione del match lo indicano.
- *Girone all'italiana*: ogni motore gioca contro ogni altro, *Cicli* volte, ogni volta due partite a colori invertiti
  (ciò che cutechess chiama round, da 2 partite per incontro). I turni seguono le tabelle Berger, quindi ogni motore
  gioca una volta per turno.
- *Gauntlet*: i primi *Motori testati* (le teste di serie) giocano contro tutti gli altri, gli altri non giocano tra
  loro.
- *Sistema svizzero*: un numero di *Turni*. Ogni turno è di due partite, a colori invertiti, contro un motore con un
  punteggio simile; nessuno incontra due volte lo stesso avversario quando si può evitare. Con un numero dispari di
  motori, il motore peggio classificato che non ha ancora avuto un turno di riposo ne riceve uno, che vale un punto.
- *Eliminazione diretta*: i motori sono teste di serie nell'ordine del loro elenco, le prime teste di serie ricevono i
  turni di riposo quando il numero di partecipanti non è una potenza di due, e le prime due teste di serie possono
  incontrarsi solo in finale. Un match è di *Coppie di partite per match* coppie di partite; un match in parità riceve
  una coppia in più alla volta, fino a dieci, dopo di che passa la testa di serie meglio classificata.
- *Incontro a squadre*: due squadre di motori, per confrontare una famiglia di motori, o un insieme di versioni, con
  un'altra nel suo complesso. *Abbinamenti*: *Tutti contro tutti*, ogni motore di una squadra contro ogni motore
  dell'altra, un avversario per turno (un sistema Scheveningen; le squadre possono essere di dimensioni diverse), o
  *Scacchiera per scacchiera*, il primo motore di ogni squadra contro il primo dell'altra, il secondo contro il secondo
  e così via (squadre della stessa dimensione). Ogni abbinamento viene giocato *Cicli* volte, ogni volta due partite
  sulla stessa apertura, a colori invertiti; i colori della prima partita si alternano da una scacchiera all'altra e da
  un ciclo al successivo. *Squadra A* e *Squadra B* danno il nome alle squadre ("Squadra A" e "Squadra B" senza un
  nome); accanto, quanti motori ha ciascuna. Le squadre si formano nell'elenco dei partecipanti: la metà superiore
  dell'elenco è la squadra A, la metà inferiore la squadra B (la squadra A riceve quello dispari), finché non si
  mettono dei motori in una squadra.

### Partecipanti e libreria
<!-- en: participants-and-the-library a7ef2e55 -->

**Partecipanti**, con la **Libreria** accanto (sotto, in una finestra stretta). Ogni partecipante è una riga: il suo
posto (una stella per le teste di serie di un gauntlet), il suo nome, il suo Elo di riferimento, ciò che gioca in modo
diverso dalle impostazioni comuni ("Hash 512 MB · 80 % del tempo") e ⋯, evidenziato quando ha impostazioni proprie.
⋯, un clic destro o il tasto Menu le aprono: il suo `Hash`, i suoi `Threads`, nodi o profondità fissi, handicap di
tempo (una percentuale della cadenza, per un orologio), il suo *Libro*, *Questo motore ancora una volta, con un altro
libro*, gli spostamenti e *Rimuovi*. L'ordine dell'elenco definisce le teste di serie: trascina i partecipanti per
riordinarli (prima le teste di serie), o premi Ctrl+Su o Ctrl+Giù per mandarli in cima o in fondo, Alt+Su o Alt+Giù
per spostarli di uno. Ctrl o Maiusc clic ne seleziona diversi; Canc (o *Rimuovi*) li toglie.

In un incontro a squadre il posto è la squadra e la scacchiera, A1, A2… per la squadra A, B1, B2… per la squadra B,
nell'ordine dell'elenco (scacchiera per scacchiera, A1 gioca contro B1). Il menu ⋯ mette il partecipante in una
squadra; *Squadra A* e *Squadra B* sotto l'elenco mettono in una di esse i partecipanti selezionati, e *Metà* rifà le
squadre dalle metà dell'elenco (la tavolozza dei comandi propone lo stesso). Un motore aggiunto quando le squadre sono
già scelte entra nella più piccola. Il riepilogo dice quanti motori ha ogni squadra, e un avvio viene rifiutato, con il
motivo, quando una squadra è vuota o, scacchiera per scacchiera, quando le squadre hanno dimensioni diverse.

La libreria elenca i motori attivi con il loro Elo di riferimento, ordinati per nome o per Elo (clicca sulla colonna),
un `!` su quelli con un problema (programma mancante o ricompilato, ultima verifica fallita: il tooltip dice quale) e
un ✓ su quelli che giocano già. Il filtro cerca nei nomi, nei nomi dichiarati dai motori, nei loro autori, programmi,
note e gruppi (Ctrl+F gli dà la tastiera; Invio al suo interno aggiunge i motori selezionati, o il primo mostrato); un
chip per gruppo mostra solo quel gruppo, e *Aggiungi tutti quelli mostrati* aggiunge l'intero gruppo. Fai doppio clic
su un motore, o selezionane diversi con Ctrl o Maiusc (Ctrl+A: tutti quelli mostrati) e premi Invio, per aggiungerli;
oppure trascinali in un punto tra i partecipanti. *Suggerisci…* seleziona gli N motori attivi che funzionano e il cui
Elo di riferimento è il più vicino a quello del primo partecipante (il motore in prova di un gauntlet), al massimo a
150 di distanza per impostazione predefinita; Invio li aggiunge. Per un match, la libreria riempie A, poi B (B viene
sostituito quando entrambi sono scelti), e un motore trascinato su A o B ne prende il posto.

### Libri propri dei partecipanti
<!-- en: participants-own-books 580e52cc -->

*Libro*: un libro di aperture proprio del partecipante, come `book=`/`bookdepth=` di Cute Chess: un libro Polyglot
(`.bin`) o PGN della pagina Libri e fino a che profondità viene seguito, in semimosse dall'inizio della partita,
compresa l'apertura del torneo (*tutto*: finché il libro ha una mossa; per un libro PGN, le prime 40 semimosse delle
sue partite). Un libro PGN viene prima trasformato in un libro Polyglot: le mosse di ogni partita fino a quella
profondità, ogni mossa pesata dal numero di partite che la giocano; le partite con una mossa illegale vengono escluse
e contate. Questo avviene in background appena il libro viene scelto (avanzamento sul pulsante *Libro*, nel suo popup
e nel riepilogo) e viene conservato nella cartella `book-cache` del profilo finché il file o la profondità non
cambiano; un avvio che arriva prima della fine lo attende. Un libro che non si può preparare mostra il motivo nel suo
popup, con *Riprova*; anche ogni avvio riprova. Un torneo gioca dal libro preparato quando è stato avviato o ripreso,
anche se nel frattempo il file PGN cambia.

Finché la posizione è in quel libro, il partecipante gioca subito la mossa del libro, scelta secondo i pesi del libro
così come li rende la *Casualità* delle aperture (anche nel popup *Libro*, la stessa per tutti i libri del torneo:
vedi [Aperture](#aperture)); poi subentra il suo motore. Le aperture del torneo, se ci sono, vengono giocate per prime,
da ogni motore; i libri EPD danno solo quelle e sono in grigio nell'elenco. Per confrontare libri con un solo motore,
scegli *Questo motore ancora una volta, con un altro libro* nel suo menu ⋯: ogni copia gioca come `<engine> (<book>)`,
così i suoi risultati e il suo rating restano separati. In un match i pulsanti *Libro* stanno accanto ad A e B, e un
motore può giocare contro se stesso con due libri. Le mosse di libro sono marcate `{book}` nel PGN; passare il mouse
su un nome nella classifica mostra il suo libro. Anche i motori WinBoard possono averne uno.

### Impostazioni dei motori
<!-- en: engine-settings 7a93d075 -->

*Hash* e *Threads* imposti a ogni motore, anche per un match (0 = quello di ogni motore; quello proprio di un
partecipante, dal suo menu ⋯, ha la precedenza); *Tablebase Syzygy per i motori* dà le cartelle delle tablebase (le
stesse dell'aggiudicazione) a ogni motore con un'opzione `SyzygyPath`, e *Libri propri dei motori disattivati*
imposta `OwnBook` a false dove esiste, così ogni partita viene giocata dalle aperture del torneo. Entrambe sono
disattivate per impostazione predefinita.

### Cadenza
<!-- en: time-control 672cb041 -->

Tempo base e incremento in secondi, mosse per periodo facoltative, e un margine prima che una partita sia persa per
tempo; un tempo fisso per mossa (`go movetime`, con lo stesso margine); un numero fisso di nodi per mossa; o una
profondità fissa in semimosse (`go depth`), con cui nessun motore può perdere per tempo.

### Parallelismo
<!-- en: concurrency c02ee2cf -->

Il numero di partite giocate contemporaneamente. In modalità automatica viene misurato all'avvio:

- nodi o profondità fissi: una partita per thread hardware, perché l'Hyper-Threading non cambia le mosse giocate;
- con un orologio: core fisici − 1, perché i motori non si rallentino a vicenda.

La riga e la linea sotto il campo dicono quale regola ha scelto il numero. *Affinità CPU* (disattivata per
impostazione predefinita; non su macOS, che non ne dispone) vincola ogni partita a un processore, condiviso dai suoi due
motori perché non pensano mai nello stesso momento. fastchess la supporta solo per motori a un thread, quindi viene
disattivata automaticamente quando un motore ha `Threads` > 1. Compare un avviso quando parallelismo × thread supera i
thread hardware (o, con un orologio, i core fisici). *Gioca su* sceglie la macchina su cui gioca il torneo (vedi [Un
torneo su una macchina](#un-torneo-su-una-macchina)).

### Aperture
<!-- en: openings 44656411 -->

Un libro EPD, PGN o Polyglot (`.bin`) (una suite di aperture), ordine casuale o sequenziale, il numero di semimosse
tenute da un libro PGN, l'indice di partenza e il seme casuale (0 = un nuovo ordine per ogni torneo). Ogni apertura
viene giocata due volte, a colori invertiti (in un match i cui *Colori* sono fissati, ogni partita dalla propria
apertura).

Un libro Polyglot viene seguito dalla posizione iniziale fino a una profondità (8 semimosse se non impostata),
escludendo le mosse che pesano meno di un decimo della migliore della loro posizione; le posizioni raggiunte vengono
scritte una volta in `<book>-<N>plies.epd` nella cartella del torneo e giocate come un libro EPD. In ordine casuale
vengono scritte nell'ordine in cui vengono estratte, ciascuna una volta: una linea arriva tanto prima quanto è più
probabile, secondo i pesi delle sue mosse, così un torneo che gioca solo una parte del libro gioca per prime le sue
linee principali; il seme estrae di nuovo lo stesso ordine. In ordine sequenziale mantengono l'ordine del libro.
*Casualità* (da −100 a 100, 0 per impostazione predefinita) dice come contano i pesi, come Polyglot e Cute Chess
scelgono una mossa di libro: 0 i pesi così come sono, −100 solo la mossa migliore di ogni posizione (una sola linea, a
meno che più mosse non condividano il peso migliore), 100 tutte le mosse allo stesso modo (le mosse rare tornano); a
metà strada, −50 eleva i pesi al quadrato e 50 ne prende la radice quadrata. Sceglie anche le mosse dei libri propri
dei partecipanti, e viene mostrata per loro anche con un libro EPD o PGN; il riepilogo la indica, e la riga sotto le
copie della classifica dà il libro con la sua profondità e, per un libro Polyglot, questa casualità.

In un girone all'italiana, un gauntlet, un sistema svizzero o un incontro a squadre, *La stessa apertura per tutte le
coppie di un turno* dà a ogni coppia di partite di un turno la stessa apertura (scacchiera per scacchiera, un ciclo è
un turno: ogni scacchiera gioca la stessa apertura), la successiva del libro a ogni turno, così nessun motore ha
aperture più facili degli altri del suo turno (un motore aggiunto dopo recupera in turni propri, ciascuno con la sua
apertura).

*Segnala le aperture sbilanciate* (disattivato per impostazione predefinita) mostra, man mano che arrivano le partite,
le aperture che decidono la partita da sole: una partita viene segnalata quando finisce se entrambi i motori hanno
visto lo stesso lato in vantaggio di almeno il punteggio indicato (200 cp per impostazione predefinita) a ciascuna
delle loro prime mosse dopo il libro (4 per impostazione predefinita; le mosse del libro proprio di un partecipante
contano come mosse di libro). Legge i punteggi dei motori, quindi un motore che non ne dà non segnala nulla; i
risultati non cambiano. La scheda *Partite* filtra queste partite (*Apertura sbilanciata*), la scheda *Aperture* segna
le loro aperture con un `!` e le conta, e la panoramica conta le partite. *Rigiocare la coppia da un'altra apertura*
annulla una coppia del genere una volta giocate le sue due partite (restano nei file PGN e nel diario, come ogni
partita annullata) e la rigioca da un'apertura alla fine del libro (nell'ordine di gioco del libro), che il torneo
giocherebbe per ultima, così le aperture delle coppie successive non si spostano; al massimo tre volte per una coppia,
dopo di che la coppia conta, segnalata. La console e la panoramica dicono quante aperture sono state sostituite.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, nelle aperture: senza libro, le 960 posizioni di partenza in ordine casuale (scritte una volta in
`chess960.epd` nella cartella dei tornei); con un libro, le sue posizioni. Ogni motore deve giocare a Chess960: i
motori UCI con l'opzione `UCI_Chess960`, i motori WinBoard con la variante `fischerandom`. Il riepilogo nomina quelli
che non lo fanno, e un torneo del genere non parte.

### Aggiudicazione
<!-- en: adjudication a18dca1e -->

Patta (da un numero di mossa, per N mosse consecutive, sotto una valutazione), abbandono (N mosse consecutive sopra
una valutazione, facoltativamente con entrambi i motori d'accordo), un numero massimo di mosse, e tablebase Syzygy.

### PGN e varie
<!-- en: pgn-and-misc 6762bab8 -->

- Nome dell'evento: il suo tooltip nomina i due file PGN in cui viene scritta ogni partita.
- *Aggiungi anche ogni partita a*: un tuo file PGN a cui ogni torneo aggiunge le sue partite, come `-pgnout` di
  cutechess (mai sovrascritto, nello stile di commento ChessBase / Lichess o cutechess), nell'ordine del calendario: le
  due partite di un'apertura una accanto all'altra, i turni in ordine, in qualunque ordine finiscano. Una partita
  attende lì finché quelle precedenti non sono scritte; una ripresa continua; quando il file non si può scrivere
  (bloccato, spostato), le partite attendono e il torneo prosegue, con un avviso nella panoramica. Un crash o
  un'interruzione di corrente mentre le partite vengono aggiunte non le scrive mai due volte: la ripresa controlla il
  file e completa una scrittura interrotta, e non vi toglie mai nulla.
- Campi aggiuntivi nei commenti delle mosse (nodes, seldepth, nps, pv, tbhits, e il tempo di decisione `dt=`: quando il
  motore si è fissato sulla mossa che ha giocato; `wdl=W/D/L` viene sempre scritto per i motori che lo forniscono),
  attivi per impostazione predefinita perché i grafici della scheda *Partite* li leggono (pv rende il PGN
  sensibilmente più grande).
- *Riavvia un motore che va in crash*, e *Ferma il torneo quando un motore non parte* (disattivato per impostazione
  predefinita: vedi [Un motore che non parte](#un-motore-che-non-parte)).
- Conservare il log del motore di ogni partita (viene sempre conservato per le partite finite male).
- *Verifica le PV di matto*: la PV di una valutazione di matto deve avere la lunghezza giusta e finire con un matto.
  Ogni PV viene verificata per mosse illegali, mosse dopo la fine della partita e una mossa migliore che non è la sua
  prima mossa; le mosse con una PV errata sono segnate nella scheda Partite e scritte nel log.

### Consigli per le impostazioni
<!-- en: tips-for-the-settings 62afdb4d -->

- **Confrontare due versioni di un motore**: un SPRT, miglioramento [0, 3] per una modifica pensata per guadagnare,
  non regressione [-5, 0] per una semplificazione o una pulizia del codice. Prima una cadenza breve (10+0.1), poi una
  lunga (60+0.6) per ciò che passa. Usa un libro di posizioni equilibrate, non la posizione iniziale: altrimenti ogni
  partita sarebbe una di poche aperture.
- **Parallelismo**: lascialo automatico. Con un orologio, più partite dei core fisici meno uno fanno rallentare i
  motori a vicenda, e i risultati si falsano; con nodi o una profondità per mossa, può giocare ogni thread hardware.
- **Threads**: 1 per motore per i test; molte partite da un thread ciascuna danno più risultati all'ora di poche
  partite da molti thread. Dai a ogni motore gli stessi `Threads` e `Hash` in *Impostazioni dei motori*.
- **Hash**: abbastanza per la cadenza (da 16 a 64 MB a 10+0.1, 256 MB a 40/15 e oltre); il riepilogo dice quando
  l'hash delle partite non entra più nella memoria.
- **Aggiudicazione**: il profilo standard fa risparmiare tempo nei tornei lunghi senza cambiare molto i risultati;
  lasciala disattivata per testare i finali di un motore, o usa le tablebase.
- **Aperture**: l'ordine casuale con un seme fisso permette di rigiocare un test esattamente; *La stessa apertura per
  tutte le coppie di un turno* è più equa in un girone all'italiana con pochi cicli.
- **Più tornei**: dai a quello che conta di più un peso maggiore invece di mettere in pausa gli altri (vedi [Più tornei
  insieme](#più-tornei-insieme)).

## Svolgimento di un torneo
<!-- en: running-a-tournament 9bcba803 -->

Un torneo in corso o aperto ha uno spazio di lavoro con un'intestazione (nome, avanzamento, *Pausa*, *Ferma*,
*Clona*, *Cartella*, *Layout*) e dei pannelli, mostrati per impostazione predefinita come schede:

| Scheda | Cosa mostra |
|---|---|
| Panoramica | I risultati fin qui e le statistiche, i grafici, la checklist dell'invio |
| Classifica | La classifica come tabella o tabella incrociata, gli avversari di un motore, le copie per un forum |
| Squadre | Un incontro a squadre: il punteggio delle squadre e la tabella incrociata delle due squadre |
| Tabellone | Un'eliminazione diretta: il tabellone, turno per turno |
| Calendario | Ogni coppia di partite, e le modifiche al torneo |
| Trasmissione | File in diretta, il server TLCS, la pagina web e lichess |
| Diretta | Le partite in corso |
| Partite | Le partite terminate, i loro filtri, la loro riproduzione e la loro esportazione |
| Aperture | I risultati per posizione di partenza |

### Panoramica
<!-- en: overview 4199cd3c -->

**Un match**: Elo e nElo con il loro intervallo di confidenza al 95 %, LOS, punteggio, conteggi pentanomiali, limiti
dell'SPRT e LLR. *Statistiche* (ripiegato) dà i numeri di fishtest calcolati sulle partite e sulle coppie: punteggio,
Elo, nElo e BayesElo con DrawElo, deviazione standard per partita, LLR di entrambi, il rapporto di varianza tra coppie e
partite singole, il bias RMS delle aperture, i tipi di coppie in equilibrio e le coppie vinte su quelle perse, che
*Copia* include. Poi i grafici: Elo e LLR nel tempo, lunghezza delle partite, come sono finite le partite e *Ricerca per
motore*.

**Un torneo**: la classifica (con *TF*, le sconfitte per tempo di ogni motore, in arancione appena ce n'è una; un *!*
accanto a un motore che è andato in crash o ha giocato una mossa illegale), sotto di essa l'Elo di ogni motore con il
suo intervallo al 95 % in un grafico, poi *Ricerca per motore*, l'avanzamento per ciclo o per turno, le partite all'ora
e il tempo rimanente, gli ultimi risultati.

*Ricerca per motore* dà la media di ogni motore sulle mosse che ha cercato, escluse le mosse di libro: profondità, NPS,
tempo per mossa, tempo di decisione e uso del processore; un clic su una colonna ordina i motori secondo quella. Accanto
ai suoi NPS, ×4 dice che il motore cerca con 4 thread: conta più nodi che con uno solo senza essere per questo più
veloce, quindi confronta i suoi NPS con quelli dei motori che ne hanno altrettanti. Gli NPS richiedono motori che li
forniscano, e *nps* in *PGN e varie* per essere riletti alla ripresa.

Entrambi contano le partite da aperture sbilanciate, e le aperture sostituite, quando la configurazione le cerca, con
*Mostra* per quelle partite. Un torneo terminato aggiunge una *Checklist dell'invio*: tutte le partite giocate, ogni
coppia con entrambi i colori, nessuna partita duplicata, nessun crash, nessuna sconfitta per tempo né mossa illegale
(con pulsanti che aprono quelle partite), l'hash di ogni motore (mostrato, non giudicato), ponder disattivato, il
libro di aperture, le tablebase.

### Classifica
<!-- en: standings 5f5e0950 -->

Punti, partite, sconfitte per tempo (*TF*), vittorie/patte/sconfitte, rating con il loro intervallo di confidenza,
Sonneborn-Berger (Buchholz per un sistema svizzero), turni di riposo. *Tabella* li elenca un motore per riga;
*Tabella incrociata* (gironi all'italiana, fino a 200 motori) mostra ogni abbinamento. *Ordine:* per punti, punteggio %
o ordine iniziale (che tiene al loro posto le colonne della tabella incrociata di un gauntlet). Un clic su un motore
elenca sotto i suoi avversari: partite, con il Bianco, con il Nero, + = −, punti e %, con segnate le coppie di cui è
stata giocata una sola partita; un gauntlet si apre con il suo motore testato selezionato.

*Copia* come testo, o come BBCode per un post in un forum (con una riga che indica formato, cadenza, partite e libro),
e in un gauntlet *Tabella del gauntlet come testo* o *in BBCode, per un forum* (il motore testato, poi i suoi
avversari secondo i loro punti, ciascuno con punti, %, Elo ±, W-L-D); *Esporta HTML…*. Tutti hanno *TF* come la
classifica della modalità headless, seguono l'ordine scelto, e finiscono con una riga che indica le partite giocate su
quelle previste, la percentuale di patte, la cadenza, il libro (con le semimosse tenute di un libro PGN; per un libro
Polyglot la sua profondità e come sono state scelte le sue mosse, per esempio `book:Perfect2023.bin (8
plies, moves by weight)`) e l'evento. Le copie poi firmano con *RookStudio* e la sua versione, e la tabella del
gauntlet è rigata con = invece di -, così una tabella pubblicata in un forum si distingue da quelle di altri
programmi.

### Squadre
<!-- en: teams 8c845645 -->

Solo un incontro a squadre, nella scheda *Classifica* (dove si apre) e in cima alla *Panoramica*: il punteggio delle
squadre, "Lynx 17½ – 18½ Otter", con le partite giocate, il punteggio % della squadra A, la differenza Elo che quel
punteggio rappresenta con il suo intervallo al 95 % e il LOS, contati sulle coppie di partite come le statistiche di
un match (pentanomiale); poi i motori di ogni squadra con le loro partite, punti, %, rating e i punti che ciascuno ha
fatto contro ogni motore dell'altra squadra (B1, B2… come colonne, il nome al passaggio del mouse): la tabella
incrociata delle due squadre. *Tabella* e *Tabella incrociata* mostrano i motori come in qualsiasi torneo. *Copia* ed
*Esporta HTML…* danno i punteggi delle squadre e i motori delle due squadre gli uni contro gli altri, con la riga
sotto di essi.

### Tabellone e calendario
<!-- en: bracket-and-schedule fe207ac0 -->

*Tabellone*, solo per un'eliminazione diretta: il tabellone, turno per turno, con il punteggio di ogni match.
*Calendario*: ogni coppia di partite per ciclo o per turno, con il suo stato e l'apertura che gioca (una volta
iniziata, o una volta iniziato il suo turno quando il turno ne condivide una), e le modifiche al torneo (vedi
[Modificare un torneo in corso](#modificare-un-torneo-in-corso)).

### Diretta
<!-- en: live 59d5ab65 -->

Le partite in corso, come un mosaico di scacchiere o una sola partita in primo piano con gli orologi, la ricerca in
corso di ogni motore (profondità, valutazione, nodi, nps, PV in SAN), l'elenco delle mosse e il grafico della partita
(vedi [Il grafico della partita](#il-grafico-della-partita)): le valutazioni dei due motori, e nelle sue schede le loro
probabilità di vittoria, il tempo per mossa con il tempo di decisione, la profondità con la seldepth, i nodi, la
velocità, gli accessi alle tablebase, quanto distano, o quante semimosse una PV ripete di ciò che la PV dell'altro
motore aveva previsto. Le quote di vittoria/patta/sconfitta (`UCI_ShowWDL`, attivato per i motori che lo hanno quando
vengono aggiunti) seguono ogni riga di ricerca; la scheda *V/P/S* le disegna come fasce, le vittorie del Bianco in
basso.

Quando c'è spazio (una partita in primo piano, un pannello grande), ogni motore ha il suo riquadro: il logo alto quanto
il nome e l'orologio (quando i loghi sono attivi, vedi [Aspetto](#aspetto)), cosa fa (pensa, o la mossa che ha giocato),
la sua ricerca cifra per cifra (profondità e seldepth, valutazione, nodi, velocità, gli accessi alle tablebase e il
riempimento dell'hash quando il motore li invia, il tempo speso sulla mossa finora) e tutta la sua PV; il motore che
attende mostra ciò che diceva quando ha giocato, con il momento in cui si è deciso. Accanto al mosaico, il pannello
tiene una riga di cifre e le prime 12 semimosse della PV. *Valutazione del lato al tratto* | *Valutazione del Bianco*, a
destra della scheda, sceglie il lato di tutte le sue valutazioni: quello del lato al tratto, come fastchess le scrive
nel PGN, o quello del Bianco; la barra di valutazione e lo schermo grande mostrano sempre quello del Bianco.

Sopra le mosse: l'apertura, il codice ECO e il nome della posizione più avanzata della partita che la tabella delle
aperture con nome conosce (mosse del libro e dei motori allo stesso modo, così che anche una trasposizione dopo il libro
abbia un nome), con l'ultima mossa del libro; e appena le tablebase Syzygy del torneo (quelle della sua aggiudicazione,
altrimenti quelle date ai motori) conoscono la posizione mostrata, il loro verdetto: il lato che vince con la DTZ (le
semimosse fino alla prossima cattura o mossa di pedone: le tablebase Syzygy non hanno la distanza dal matto), o la
patta, con la regola delle 50 mosse contata dal contatore della partita salvo che l'aggiudicazione la ignori. Ogni
posizione viene interrogata una volta, fuori dai thread che giocano le partite. Nell'elenco, ogni mossa di un motore ha
la sua valutazione, profondità e tempo; passandoci sopra si aggiungono il momento in cui il motore si è deciso e la
risposta che si aspettava (la seconda mossa della sua PV: fastchess gioca senza ponder). Le mosse del libro dicono
*libro*, una riga segna la fine del libro, e la mossa su cui si pensa dice *pensa*.

Il mosaico ha una scacchiera per ogni partita che il torneo ha giocato contemporaneamente, non per ogni partita che il
parallelismo consente: con meno partite di così, le scacchiere si ingrandiscono. Una scacchiera la cui partita è finita
mantiene la posizione finale finché la partita successiva del suo posto non inizia lì: verso la fine di un torneo le
scacchiere restano al loro posto, ciascuna con la fine della sua ultima partita. Quando le scacchiere richiederebbero
una barra di scorrimento, il mosaico toglie larghezza alla partita in primo piano, fino a lasciarle il 40 % della
pagina.

Sotto la scacchiera: il contatore delle 50 mosse (semimosse dall'ultima cattura o mossa di pedone, su 100), la
differenza di materiale con i pezzi che una parte ha in più, e i pezzi rimasti, marcati *TB* quando l'aggiudicazione
con le tablebase può terminare la partita; *Dettagli sotto le scacchiere* li aggiunge al mosaico. Sotto di essi, i
pulsanti per scorrere le mosse (vedi [Le scacchiere e i loro pulsanti](#le-scacchiere-e-i-loro-pulsanti)): la
partita in primo piano, o una scacchiera staccata, si può riportare indietro mentre prosegue (anche con ← → Inizio
Fine, un clic su una mossa dell'elenco o sulla curva), con la posizione precedente mostrata e la sua mossa segnata
nell'elenco e sulla curva; *Torna alla mossa attuale* (o Fine, o un clic sull'ultima mossa) la riporta a seguire la
partita, come avviene a ogni nuova partita.

*Frecce* (attivo per impostazione predefinita) disegna la mossa a cui pensa il lato che muove nel colore del suo
motore e la risposta che si aspetta, più chiara, nel colore dell'avversario, sulla scacchiera in primo piano e su
quelle staccate (non sul mosaico), quando mostra la mossa attuale. In un gauntlet, *Motore testato in basso* (attivo
per impostazione predefinita) mostra ogni nuova partita dal lato del motore testato; ruotare la scacchiera (F, o il suo
pulsante) la capovolge comunque fino alla partita successiva. *Stacca* apre una scacchiera in una finestra propria. Un
clic destro su una scacchiera termina la sua partita con un risultato (vedi [Modificare un torneo in
corso](#modificare-un-torneo-in-corso)). *Schermo grande* mostra la partita seguita a tutta finestra (vedi [Schermo
grande](#schermo-grande)).

### Schermo grande
<!-- en: big-screen 14c8f0d6 -->

Per una televisione, un circolo o uno stream, *Schermo grande* (nella scheda *Diretta*, su una scacchiera staccata, o
nella tavolozza dei comandi) mostra una partita a tutta finestra, chiaro su nero: i nomi dei giocatori e gli orologi a
caratteri grandi (il lato che muove e il suo orologio che scorre in giallo, un orologio sotto i dieci secondi in rosso),
la barra di valutazione, la valutazione, la profondità e la velocità della ricerca, l'ultima mossa incorniciata sulla
scacchiera e scritta in grande, l'apertura, le linee dei motori (*Linee*, attivo per impostazione predefinita: quella
del lato al tratto mentre cerca, in giallo, quella dell'altro dalla sua ultima mossa, al massimo due righe ciascuna),
il verdetto delle tablebase appena conoscono la posizione, e le mosse a caratteri grandi, con l'ultima segnata; il risultato sopra la scacchiera
quando la partita è finita. Il suo testo segue la dimensione della finestra e ha una dimensione propria (*A-* / *A+*,
o i tasti + e -). Mostra la partita seguita, o passa da una partita in corso all'altra ogni N secondi (*Ogni partita a
turno*), o segue *La più interessante*: la più vicina alla parità, la più lunga tra quelle altrettanto vicine, tenuta
finché resta tale. I controlli compaiono per tre secondi quando il mouse si muove (il puntatore si nasconde con essi);
← e → cambiano partita, F11 porta la finestra a schermo intero, Esc esce. Una scacchiera staccata spostata su un altro
schermo (la televisione) lo mostra nella sua finestra, con F11 che porta quella finestra a schermo intero mentre la
finestra principale continua; agganciata nella finestra principale, lo mostra la finestra principale.

### Partite
<!-- en: games ba4a9604 -->

Le partite terminate con la loro apertura (codice ECO e nome), e una scacchiera di riproduzione: i pulsanti sotto di
essa, ← → Inizio Fine e F, o un clic su una mossa o sul grafico; gli stessi dettagli sotto di essa e lo stesso grafico
della partita che in *Diretta* (vedi [Il grafico della partita](#il-grafico-della-partita)), dai campi con cui è stato
scritto il PGN: un grafico il cui campo il torneo non ha scritto si trova nel menu `⋯` del grafico della partita, in
grigio, e lo dice con un pulsante verso le opzioni PGN della configurazione.

**Filtri**: per risultato (vittorie, patte, sconfitte, sconfitte per tempo, crash e mosse illegali, aggiudicazioni, PV
errate, e le partite di un'apertura segnalata come sbilanciata quando la configurazione le cerca); per ciò che dicono
le valutazioni (i motori sono in disaccordo di N pedoni per 3 mosse ciascuno di fila, la valutazione di un motore
oscilla di N pedoni da una sua mossa alla successiva, chi ha perso pensava di essere N pedoni meglio; la partita si apre
in quel momento); e per apertura (un codice ECO o il suo inizio, un intervallo come `B20-B99`, o parole del nome).

**Esportazione** di una, di tutte o delle partite filtrate: *Esporta tutto…* propone `<name>.pgn` accanto alla
cartella del torneo; i file PGN propri del torneo vengono rifiutati come destinazione. *Esporta per una lista di
rating…* scrive le partite come le inviano i tester delle liste di rating (per esempio CCRL):
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, anche uno zip se richiesto; i giocatori nominati
come li scrivono le liste, `<engine> 64-bit 4CPU` (modificabile per ogni partecipante), i turni rinumerati, i tag
`Event` e `Site` impostati, le partite annullate e i duplicati esclusi, e anche quelle perse per crash o blocco a meno
che non si tolga la spunta; il `PlyCount` di una partita persa per mossa illegale conta solo le mosse scritte. Il
tester e il sito vengono ricordati.

*Log del motore* apre il log di una partita che ne ha uno (vedi [File](#file)). Un clic destro su una partita la
rigioca, la annulla o ne fissa il risultato (vedi [Modificare un torneo in corso](#modificare-un-torneo-in-corso)).

### La scheda Aperture
<!-- en: the-openings-tab c51dd29a -->

Risultati per posizione di partenza: le sue coppie di partite, il punteggio del Bianco, le patte, e quante coppie il
Bianco ha vinto due volte, il Nero ha vinto due volte (l'apertura favorisce un colore) o un motore ha vinto due volte
(distingue i motori), con la lunghezza media. Ordinabili per qualsiasi colonna, filtrate sulle sbilanciate, sulle
patte o sulle decisive, o su quelle *Segnalate come sbilanciate durante le partite* (vedi [Aperture](#aperture) nella
configurazione: marcate `!`, il loro numero accanto al conteggio, con le aperture sostituite). *Esporta come EPD…*
scrive le posizioni mostrate, per conservarle o per ripulire un libro, con *Senza le sbilanciate* (attivo per
impostazione predefinita, mostrato appena un'apertura viene segnalata) che esclude quelle segnalate. Un clic mostra la
posizione e le sue partite. In un match i cui *Colori* sono fissati, ogni partita ha la propria apertura: si contano le
partite, e le colonne e i filtri sulle coppie non vengono mostrati.

### Pannelli e layout
<!-- en: panels-and-layouts 9ed9b915 -->

Ogni scheda è un pannello. Trascinane uno per la sua linguetta per agganciarlo a un lato di un altro pannello o come
scheda accanto ad esso, o rilascialo altrove per farlo fluttuare (anche su un secondo schermo su Windows e Linux/X11,
non su Wayland né con il rendering software). La partita seguita in *Diretta* si scompone nei pannelli *Scacchiera*,
*Giocatori*, *Ricerche*, *Mosse* e *Grafico*; le parti che non hanno un pannello aperto restano accanto al mosaico in
*Diretta*, e le scacchiere staccate si agganciano come gli altri pannelli. Chiudi un pannello con la x sulla sua
linguetta; fallo tornare da *Layout* > *Pannelli*, o con *Mostra: …* nella tavolozza dei comandi, e torna agganciato.

*Layout* propone anche dei layout già pronti (*Schede*, come per impostazione predefinita; *Segui una partita*: la
scacchiera a sinistra, i giocatori, le ricerche e il grafico a destra, la classifica sotto di essi; *Torneo grande*: il
mosaico accanto alla classifica; *Trasmissione*: una scacchiera grande con i giocatori, le ricerche e la classifica),
*Salva il layout come…* (i tuoi, per nome, conservati in `layouts.json` nel profilo), *Elimina* e *Layout
predefinito*. Un pannello portato fuori dallo spazio di lavoro resta sullo schermo nelle altre pagine; la disposizione
viene conservata tra una sessione e l'altra.

### Uso del processore
<!-- en: processor-use e5234cbe -->

Mentre si giocano le partite, il tempo di processore dei processi di ogni motore (compreso il motore dietro un
adattatore WinBoard) viene misurato a ogni `go` e a ogni mossa. La Panoramica mostra, per ogni motore, i core che tiene
occupati mentre pensa e la quota di un core che usa mentre pensa il suo avversario, e avvisa quando un motore usa
chiaramente più core dei suoi `Threads`, o pensa sul tempo del suo avversario (un ponder non richiesto, un thread in
background): ciò che rende una partita sleale verso l'avversario e verso le altre partite in corso. Gli servono 20 s di
ciascuno per dirlo, e conta ogni partita giocata, nella finestra o in modalità headless (`--run`): ciò che hanno usato
i motori di ogni partita viene scritto in `processor.tsv` nella cartella del torneo, così una ripresa lo conserva. Si
limita ad avvisare; non decide mai una partita.

### Pausa, arresto e ripresa
<!-- en: pause-stop-and-resume f6885fa5 -->

**Pausa** non avvia nuove coppie di partite. Le partite in corso arrivano alla fine, e così la seconda partita di una
coppia la cui prima partita è iniziata, quindi una pausa non butta mai via un risultato. **Riprendi** continua.
**Ferma** o termina prima le partite in corso, o si ferma subito (le partite interrotte non vengono contate e saranno
rigiocate).

Un torneo fermato da un motore dice quale nella sua intestazione e nella pagina Tornei, sotto *Da sistemare*:
*Interrotto: Alpha non è riuscito ad avviarsi: …* (dopo il suo ultimo tentativo), o *Alpha è andato in crash o non
risponde più (partita 12)* (senza *Riavvia un motore che va in crash*), con *Apri il motore* e *Apri il log*.

**Ogni torneo riprende esattamente da dove si è fermato**, dopo un arresto, una finestra chiusa, un crash o
un'interruzione di corrente: nessuna partita va persa e nessuna viene giocata due volte, e una coppia di partite giocata
a metà viene completata con la stessa apertura e gli stessi colori. *Riprendi* nella pagina Tornei lo prosegue, con
le statistiche delle partite già giocate: risultati, e profondità, tempo per mossa e NPS dei motori riletti dai
commenti delle mosse (NPS solo quando il PGN lo registra, *nps* in *PGN e varie*). Quando il programma è terminato
mentre un torneo era in corso (un crash, un'interruzione di corrente, un riavvio, o l'uscita mentre era in corso),
l'avvio successivo lo riprende da solo, e la coda prosegue dopo di esso: *Riprendi all'avvio il torneo che era in
corso*, in **Impostazioni**, attivo per impostazione predefinita. Un torneo in pausa o fermato ti aspetta, e
`--autostart` avvia invece la configurazione salvata. Un partecipante il cui programma è stato ricompilato o è sparito
da quando è entrato viene nominato nell'intestazione del torneo ripreso, con *Continua* e *Pausa*: le sue prossime
partite vengono giocate dal programma com'è ora, con le opzioni che il torneo gli ha dato.

### Più tornei insieme
<!-- en: several-tournaments-at-once 4914f1d0 -->

Più tornei possono giocare fianco a fianco: un gauntlet accanto a un SPRT, due gauntlet alla volta. *Avvia* nella
pagina *Nuovo torneo* ne avvia un altro mentre altri giocano; *Riprendi* ne riprende uno accanto a essi. Ogni torneo
gioca in un processo proprio (`rookstudio --run <folder>` guidato dalla finestra), quindi un crash fa cadere solo
quel torneo, e la finestra, le sue verifiche dei motori, i test rapidi e le suite di test non aspettano più la fine di
un torneo.

Lo spazio di lavoro ha **una scheda per ogni torneo aperto**, sopra la sua intestazione: un punto mentre gioca (un
segno di pausa mentre è in pausa), il suo stato al passaggio del mouse, una croce per chiudere quello che non gioca. I
pannelli mostrano la scheda scelta; la tavolozza dei comandi elenca le altre (*Mostra: …*, *Pausa*, *Riprendi*). Un
torneo terminato aperto dall'elenco prende la scheda di uno solo consultato. La pagina Tornei ha una scheda per ogni
torneo che gioca, con il suo avanzamento, chi è in testa, *Apri* e *Pausa*, e sotto il suo **peso**, i core che le sue
partite usano su quelli che può usare, e ciò che attende la sua prossima partita; la barra *Core* in alto mostra la
parte di ogni torneo (e quella della pagina Scacchiera), e quanti core i tornei condividono. Il *Log* mostra le righe
di un torneo, della finestra, o tutte (ogni riga di un torneo riporta il suo nome); le righe di un torneo sono anche
conservate in `rookstudio.log` nella sua cartella.

**Condividere la macchina**: i tornei che giocano condividono un budget di core, impostato in **Impostazioni** (*Core
per i tornei*: i core fisici meno uno per impostazione predefinita). Ciascuno riceve prima l'equivalente di una
partita finché il budget lo permette, a partire dal peso maggiore; il resto va secondo il peso (un torneo di peso 2
riceve il doppio dei core di uno di peso 1), nessuno oltre il proprio parallelismo, e ciò che uno non può prendere va
agli altri. Una partita conta come i `Threads` del maggiore dei suoi due motori: uno pensa mentre l'altro aspetta. Un
torneo in pausa tiene solo ciò di cui hanno bisogno le sue partite in corso e le seconde partite delle loro coppie.
Una quota più piccola non interrompe mai una partita: il torneo gioca meno partite dalla successiva. Un torneo da solo
gioca tante partite quante ne dice il suo parallelismo.

**Memoria**: una partita parte solo quando l'`Hash` dei suoi due motori, e ciò che ogni processo di motore occupa in
più (64 MB, e 8 MB per thread), entrano in ciò che le partite in corso lasciano del budget di memoria (*Memoria per i
motori* in **Impostazioni**, il 75 % della memoria installata per impostazione predefinita), condiviso tra i tornei.
Il riepilogo della configurazione dice quante partite il budget permette di giocare insieme, e la scheda
*Affidabilità* della pagina Tornei quando delle partite attendono memoria.

**GPU**: un motore segnato *Usa una GPU* sulla sua scheda (un motore a rete come Lc0) conta rispetto alle *GPU* di
**Impostazioni**: non giocano insieme più motori di questo tipo di quante GPU ci sono, in un torneo e tra tornei,
mentre le altre partite giocano accanto a essi. Il riepilogo della configurazione dice quante partite con un motore GPU
ciò permette, e avvisa quando non è contata nessuna GPU, o quando una partita tra due motori GPU richiede più GPU di
quante ne abbia la macchina (allora gioca da sola).

**Lasciarli in corso**: chiudere la finestra mentre dei tornei giocano chiede cosa fare. *Lasciarli in corso e uscire*
(la scelta predefinita) lascia ciascuno a giocare nel suo processo; la finestra li mostra di nuovo, al punto in cui
sono, al suo avvio successivo. *Ferma ed esci* li ferma, ciascuno da riprendere esattamente da dove si è fermato. Il
test rapido si ferma in entrambi i casi. Un torneo il cui processo non c'è più all'avvio successivo (un riavvio, un
crash) viene ripreso come qualsiasi torneo interrotto. La trasmissione della scheda *Trasmissione* gira nella
finestra, per il torneo mostrato: per una trasmissione che continui senza la finestra, avvia il torneo da un terminale
con le opzioni di trasmissione (vedi [Modalità headless](#modalità-headless)).

### Un motore che non parte
<!-- en: an-engine-that-cannot-start 26c20dee -->

Una notte lasciata da sola non si ferma per un motore. Un motore che non parte (un programma spostato o ricompilato,
un file di rete mancante, un disco sparito) viene riprovato tre volte, a 10 s di distanza; poi le sue partite vengono
messe da parte e le altre partite proseguono. Viene riprovato ogni 5 minuti, una partita alla volta, e le sue partite
tornano appena parte. L'intestazione del torneo e la pagina Tornei (sotto *Da sistemare*) dicono quale motore,
l'errore, e propongono:

- **Riprova**: subito, invece che al prossimo tentativo (dopo aver sistemato il programma, per esempio).
- **Ritira…** e **Sostituisci…** (gironi all'italiana, gauntlet e incontri a squadre): il pannello delle modifiche
  della scheda Calendario, impostato per quel motore, con l'impatto mostrato prima che qualcosa cambi.
- **Log del motore**: `logs/start-<engine>.log` nella cartella del torneo, ciò che è stato inviato ai motori del suo
  ultimo tentativo e ciò che hanno risposto, compreso il loro output degli errori (spesso il motivo: una libreria o un
  file di rete non trovati).

Il log e la notifica del desktop dicono quando un motore viene messo da parte. Il torneo si ferma solo quando non resta
nulla da giocare senza i motori messi da parte, dopo un ultimo tentativo per ciascuno; il suo stato e la notifica
alla fine lo dicono (*Fermato: non resta nessuna partita da giocare senza …*), e la coda prosegue con il torneo
successivo. *Riprendi* riprova ogni motore da capo. Una pausa trattiene i tentativi. *Ferma il torneo quando un motore
non parte*, in *PGN e varie* della configurazione, lo ferma invece subito, una volta esauriti i tentativi.

### Modificare un torneo in corso
<!-- en: changing-a-running-tournament aebe63e2 -->

Il pannello **Modifica il torneo** della scheda *Calendario* funziona senza fermare nulla: le partite in corso
proseguono e nessuna partita giocata va persa. Ogni modifica mostra il suo impatto prima di essere applicata.

- **Aggiungi un motore**: gioca i cicli già iniziati (*Recupera*) o entra dal ciclo successivo.
- **Ritira un motore**: le sue partite rimanenti vengono eliminate; le sue partite giocate fin qui contano o no.
- **Sostituisci un motore**: le sue partite smettono di contare e vengono rigiocate dal nuovo motore.
- **Numero di cicli**: di più o di meno.
- **Rigioca partite**: tutte le partite di un motore, o le sconfitte per tempo e i crash.
- Clic destro su una partita (qui, o nella scheda *Partite*): rigiocarla, annullarla (senza rigiocarla), o fissarne il
  risultato.

Un motore aggiunto o entrato come sostituto gioca con le impostazioni del torneo (la sua cadenza, Hash e Threads,
mostrati sotto il motore), o con *Impostazioni proprie*: Hash, Threads, quota del tempo (o nodi, profondità), come le
impostazioni proprie di un partecipante nella pagina *Nuovo torneo*. Un torneo creato prima della 0.6.6 non registrava
le sue impostazioni: le sostituiscono quelle del suo primo motore, che potevano essere le impostazioni proprie di quel
motore.

In un incontro a squadre un motore aggiunto entra nella squadra scelta e gioca contro ogni motore dell'altra squadra;
scacchiera per scacchiera non si può aggiungere un motore (la sua scacchiera non avrebbe avversario): sostituiscine
uno invece. Un sostituto prende la squadra e la scacchiera del motore che sostituisce; più cicli abbinano i motori che
giocano ancora (scacchiera per scacchiera, sulle scacchiere del primo ciclo). Le partite giocate da un motore ritirato
o sostituito contano ancora per la sua squadra, a meno che non vengano annullate.

**Fissa il risultato** decide una partita che il programma non ha saputo decidere bene: un blocco che non ha
rilevato, un finale vinto in cui i motori girano a vuoto, una partita finita per un motivo sbagliato. Scegli 1-0, ½-½
o 0-1 e indica un motivo; i punti che sposta vengono mostrati prima che *Fissa il risultato* lo applichi. La
classifica, la tabella incrociata, i rating e la checklist dell'invio contano il nuovo risultato (un'aggiudicazione:
una sconfitta per tempo cambiata in patta non conta più come tale); la scheda *Partite* la segna con un `*`, con il
tooltip che indica il motivo e come è finita la partita giocata, e *Torna al risultato giocato* torna indietro. Una
partita in corso si termina allo stesso modo: clic destro sulla sua scacchiera nella scheda *Diretta*, *Termina con un
risultato* (anche nella tavolozza dei comandi per la partita seguita). Il motore al tratto viene fermato, fastchess
termina subito la partita e questa conta con il risultato scelto; il motore riparte per la sua partita successiva.
Richiede *Riavvia un motore che va in crash* (configurazione, *PGN e varie*, attivo per impostazione predefinita).

Le partite annullate e i risultati fissati a mano restano nei file PGN e nel diario come sono stati giocati: ai file
PGN si aggiunge soltanto. Ciò che legge le partite li applica: le partite annullate vengono escluse, e un risultato
fissato a mano compare in ogni esportazione (*Esporta la partita…*, *Esporta tutto…*, *Esporta per una lista di
rating…*, l'esportazione degli scontri diretti della pagina *Classifiche*, il file di *Aggiungi anche ogni partita a*)
con `[Termination "adjudicated by the operator"]` e il motivo in un commento prima del risultato. **Annulla l'ultima
modifica** viene proposto finché non inizia la partita successiva: sotto la modifica non è ancora stato giocato nulla,
quindi può essere annullata esattamente (le partite già in corso quando è stata fatta vengono conservate). Un risultato
fissato a mano si può annullare finché è l'ultima modifica, che siano state giocate partite da allora o no (in
un'eliminazione diretta, i cui turni successivi dipendono dai risultati, finché non inizia la partita successiva); la
scheda *Partite* propone anche *Annulla* accanto alla partita.

### Coda e rilancio
<!-- en: queue-and-relaunch 06904b6c -->

La **Coda** nella pagina Tornei contiene tornei da giocare uno dopo l'altro: quando uno finisce da solo, parte il
successivo (non dopo un arresto o un errore, né mentre *Metti tutto in pausa* trattiene la coda). Con *Avvia quando ci
sono core liberi*, il successivo parte accanto a quelli che giocano appena il budget di core supera ciò che essi
possono prendere, ciascuno al suo parallelismo. Si possono riordinare o rimuovere, e avviare subito il primo quando non
è in corso nulla. La pagina Tornei dice quando finisce il torneo in corso (*Finisce verso HH:MM (tra …)*) e quando
finisce l'intera coda, ogni torneo in coda con la sua durata e, finché le durate di quelli che lo precedono sono note,
quando parte; i tornei la cui durata non si può conoscere (un SPRT senza massimo, nodi o profondità per mossa) vengono
contati a parte.

**Rilancia con…** su un match terminato (nella sua intestazione, o sotto *Ultimi terminati* nella pagina Tornei) lo
rigioca con un altro motore A, tipicamente la versione successiva, contro lo stesso motore B, con le altre
impostazioni del match (quelle della pagina *Nuovo torneo* per un match avviato da una versione precedente, che non le
conservava).

**Clona** (nell'intestazione di un torneo, sotto *Ultimi terminati* e in *Tutti i tornei* nella pagina Tornei) apre
la pagina *Nuovo torneo* compilata con la configurazione di quel torneo, qualunque sia il formato: i partecipanti con le
loro impostazioni, la cadenza, le aperture, l'aggiudicazione e il resto, ogni valore com'è stato giocato. I suoi
profili sono indicati per nome, e un profilo modificato da allora è segnato come cambiato per questo torneo. Cambia ciò
che deve essere diverso (un'altra cadenza, la versione successiva di un motore, un altro libro), poi **Avvia** o *Metti
in coda*. Anche un torneo ancora in corso si può clonare; uno avviato da una versione precedente, che non ne conservava
la configurazione, no.

### Verificare i finali con le tablebase
<!-- en: checking-the-endgames-against-the-tables fb5b1e89 -->

Il menu di un torneo finito, un clic destro sulla sua intestazione o sulla sua riga nella pagina Tornei (o **Ctrl+K**),
ha *Verifica i finali con le tablebase*: le partite arrivate a sette pezzi o meno, ogni risultato confrontato con ciò
che dicono le tablebase. La risposta arriva in un riquadro sotto l'intestazione del torneo, "12 partite sono arrivate a
sette pezzi o meno: 3 vittorie lasciate sfuggire (Alpha: 2, Bravo: 1), 1 posizione persa tenuta, 2 patte aggiudicate in
una posizione vinta", con una riga per ogni rilievo, e *Apri* che mostra la sua partita nella scheda *Partite* alla
mossa di cui si tratta:

- **una vittoria lasciata sfuggire**: una mossa di un motore dopo la quale le tablebase non gli danno più la vittoria
  (una patta, o la vittoria dell'altro lato); **una patta lasciata sfuggire**: una mossa di un motore dopo la quale
  danno la vittoria all'altro lato;
- **una posizione persa tenuta**: una partita patta per regolamento (una triplice ripetizione, la regola delle
  cinquanta mosse) mentre alla sua fine le tablebase danno ancora la vittoria a un lato;
- **una patta aggiudicata in una posizione vinta**, dalla regola di patta del torneo o dal suo numero massimo di mosse,
  e **una vittoria aggiudicata contro le tablebase**, dalla sua regola di abbandono. L'aggiudicazione delle tablebase
  stesse concorda con esse; una partita persa per tempo, per una mossa illegale o per un crash viene giudicata solo
  sulle sue mosse.

Per ogni partita vengono chieste poche posizioni: la prima che le tablebase sanno giudicare e l'ultima, e quando i loro
verdetti differiscono, una bisezione tra le due fino alla mossa che l'ha cambiato (circa dieci posizioni per un finale
lungo). Prima le tue tablebase sul disco (vedi [Tablebase per i finali](#tablebase-per-i-finali)), poi le risposte che
Lichess ha dato in precedenza, conservate nel tuo profilo, poi le tablebase a 7 pezzi di Lichess per il resto, una volta
autorizzate: per questa verifica, sempre, o non ora (il FEN di ogni posizione va a tablebase.lichess.ovh, senza
account), una richiesta alla volta, ogni risposta conservata per sempre. Senza di esse, le partite che le tablebase del
tuo disco non sanno giudicare vengono contate come non giudicate. Niente della pagina del torneo cambia prima del clic.

## Classifiche
<!-- en: ratings 3e4be48f -->

La pagina **Classifiche** calcola rating di massima verosimiglianza (il modello logistico, le patte come mezzo punto,
come fa Ordo per impostazione predefinita) su tutte le partite di tutti i tornei del profilo, e dei file PGN importati.
I motori sono identificati dal loro nome. Le partite contate si possono restringere in cima alla pagina, per ogni
scheda:

- una cadenza, un gruppo della libreria dei motori (le partite tra i suoi motori), gli ultimi 30 o 365 giorni;
- *Partite importate*: le partite dei file PGN elencati in *File PGN…* (il download di una lista Elo, le partite di un
  altro programma), lette dalle loro intestazioni; i loro motori sono riconosciuti dai nomi che dà loro il PGN, e un
  nome che è quello di un motore della libreria, maiuscole e spazi a parte ("stockfish  17" per "Stockfish 17"), è quel
  motore: le sue partite contano come quelle del motore e il suo Elo di riferimento può ancorarle. Nulla di più
  approssimativo viene applicato da sé: "Stockfish 17 64-bit" resta un motore separato finché non dici quale motore è
  in *Giocatori PGN…* (accanto al conteggio dei motori della libreria, o in *File PGN…*). Quella finestra elenca i
  giocatori che non sono un motore della libreria per nome, con una ricerca: per ciascuno, *Un motore a sé* (il
  predefinito) o un motore della libreria. Un nome che hanno due motori della libreria, maiuscole a parte, viene
  mostrato nel colore di avviso perché tu scelga; un motore probabile (il cui nome è l'inizio di quello del giocatore)
  viene suggerito come *Forse …?*, mai applicato. La finestra si apre da sola dopo l'aggiunta di un file PGN quando c'è
  qualcosa da decidere. Le scelte, compreso un motore a sé, vengono conservate con il motore (seguono una
  ridenominazione) e valgono per ogni file PGN e per le liste Elo importate per l'Elo di riferimento; le partite vengono
  rilette con esse quando la finestra si chiude. La lista Elo dice quanti motori della libreria contengono i file PGN,
  il suo tooltip quali;
- *Ancora*: la media a 0, o un motore a un rating fisso (per impostazione predefinita il primo motore con un Elo di
  riferimento).

### Lista Elo
<!-- en: rating-list c726b55e -->

La lista, con un numero minimo di partite. Oltre a Elo, ± e partite, ogni motore mostra il suo punteggio, le sue
vittorie (+), patte (=) e sconfitte (−), la sua percentuale di patte, *Avv.*: il rating medio degli avversari che ha
incontrato, partita per partita, *LOS*: la probabilità che sia più forte del motore successivo della lista, calcolata
dai due rating e dalla covarianza dei loro errori (approssimata oltre i 1000 motori, come dice il suo tooltip), e
*Distacco*: i suoi punti Elo in più rispetto al motore successivo. Un clic sull'intestazione di una colonna ordina per
essa (# mantiene il posto per rating); un clic destro mostra o nasconde le colonne (*Distacco* è nascosta all'inizio).
Un clic su un motore apre la sua scheda.

*Copia* come testo o BBCode, con *Con intestazione* per mettere sopra la lista l'intestazione descritta sotto.
*Esporta…*: CSV con tutte le colonne; una pagina HTML (chiara o scura) e Markdown (una tabella che GitHub e i forum
mostrano come tale), entrambe aperte da un'intestazione che dice come è stata fatta la lista: i tornei e i file PGN
contati, le partite con le vittorie del Bianco e del Nero, le patte e il punteggio del Bianco, l'ancora, i filtri, il
numero minimo di partite, la data e la versione; e *Partite per Ordo…*: le partite contate come un PGN di sole
intestazioni, che Ordo e BayesElo leggono (`ordo -p ratings.pgn -o ratings.txt`).

### Scheda del motore
<!-- en: engine-card 80442e81 -->

La scheda di un motore: il suo rating, i suoi risultati complessivi e per cadenza, le sue sconfitte per tempo, i crash
e le mosse illegali (ogni partita si apre nel suo torneo), i suoi tornei (ciascuno si apre), la sua profondità media,
il tempo per mossa e gli NPS letti dai commenti delle mosse, e il suo rating dopo ogni torneo. La pagina *Motori* vi
porta con *I suoi risultati…*.

### Scontri diretti
<!-- en: head-to-head 1021ba2f -->

Due motori l'uno contro l'altro: il punteggio, Elo e LOS, i conteggi pentanomiali delle coppie di cui hanno giocato
entrambe le partite, per cadenza e per torneo, le loro partite (ciascuna si apre nel suo torneo) ed *Esporta le
partite…*.

### Avanzamento
<!-- en: progress e2ea28fc -->

Il rating dei motori spuntati dopo ogni torneo che hanno giocato, su un asse temporale: le versioni di un motore una
accanto all'altra.

## Trasmissione
<!-- en: broadcast c2f489e7 -->

La scheda *Trasmissione* di un torneo in corso lo pubblica mentre gioca; l'istantanea viene aggiornata due volte al
secondo. *Partita mostrata* sceglie la partita inviata a TLCS e alla pagina web: automatica, o quella scelta nella
scheda *Diretta*.

### File in diretta
<!-- en: live-files 09341896 -->

Scritti in modo atomico in una cartella a tua scelta, da pubblicare dove vuoi (FTP, rsync, un sito web): `live.pgn`
(le partite in corso), `standings.html` (classifica e tabella incrociata), `tournament.json` (tutto, per gli script).

### Server TLCS
<!-- en: tlcs-server 712f28cb -->

Porta UDP, 16001 per impostazione predefinita: il protocollo del Tom's Live Chess Server, così TLCV e gli altri
visualizzatori TLCS possono seguire il torneo (scacchiera, orologi, PV, tabella incrociata e partite giocate). È
verificato con node-tlcv. *Una porta per partita* mette ogni partita in contemporanea su una porta propria: la partita
n. 1 della scheda *Diretta* sulla porta UDP, la n. 2 sulla successiva, e così via. Ogni porta si apre quando inizia la
sua partita, con i propri limiti di client e di accessi, e riceve la tabella incrociata dopo ogni partita; *Partita
mostrata* allora non si applica.

### Pagina web
<!-- en: web-page bc743ffc -->

Porta TCP: una pagina servita da RookStudio stesso, per qualsiasi browser sulla rete, in sola lettura e aggiornata
ogni secondo. La partita seguita (la prima in corso, o quella cliccata) è al centro: la scacchiera con l'ultima mossa,
un re sotto scacco, le coordinate e le frecce; l'orologio di ogni giocatore a caratteri grandi; la valutazione di ogni
motore a caratteri grandi con chi sta pensando, la sua ricerca a riquadri (profondità/profondità selettiva, nodi,
velocità, tempo, accessi alle tablebase, hash) e la sua linea di gioco; e le mosse, con le mosse di libro in corsivo.
Scorri le mosse con le frecce sotto la scacchiera o i tasti ← → (▶ oltre l'ultima mossa torna alla partita mentre
viene giocata), ruota la scacchiera con ⇅ o F, e copia il PGN della partita o il FEN della posizione.

Accanto alla partita, delle schede mostrano la classifica e la tabella incrociata, i grafici della partita seguita, la
ricerca di ogni motore in media per mossa con le sue anomalie, le prossime partite del calendario (un sistema svizzero
o un'eliminazione diretta abbina i turni successivi man mano), le statistiche e le anomalie delle partite giocate, e
l'evento. Ogni partita in corso è mostrata sotto, con una piccola barra di valutazione accanto a ogni giocatore.
*Moves* alterna le mosse tra lettere (Nf3) e figurine (♘f3), e il tema segue il sistema o è impostato su chiaro o
scuro; il browser ricorda queste scelte. La pagina non carica nulla da altrove e funziona sui telefoni. *Download the
games (PGN)* dà il file PGN del torneo, fino all'ultima partita registrata. `state.json`, che la pagina legge, contiene
tutto per gli script. I loghi dei motori (vedi [Aspetto](#aspetto)) compaiono accanto ai loro nomi, come piccole
immagini PNG che il programma ne ricava e serve da sé; la pagina conosce solo quelli dei motori del torneo.

### lichess
<!-- en: lichess 7597e86f -->

Una trasmissione lichess, con la scacchiera, la barra di valutazione e la chat di lichess, per chiunque su internet.
Richiede un token API personale con il permesso `study:write` (*Crea un token su lichess…*), conservato solo in
`settings.json` nel profilo, salvato quando si lascia il suo campo. Senza un turno indicato, viene creata una
trasmissione con il nome del torneo, non elencata a meno che non sia spuntato *Elencata tra le trasmissioni di
lichess*; il suo link viene mostrato quando è in onda. Altrimenti le partite vanno al turno di cui si indica l'URL o
l'id (una trasmissione alimentata da invii, non da un URL sorgente). Le partite in corso vengono inviate ogni 8
secondi con i loro orologi, e ogni partita terminata una volta; un turno contiene 64 partite, dopo di che viene creato
il turno successivo della stessa trasmissione. Vengono inviate solo le partite terminate dopo che è stata attivata;
`lichess.json` nella cartella del torneo ricorda dove sono andate, così un torneo ripreso continua la stessa
trasmissione. lichess tiene solo gli orologi di ciò che riceve (scarta il `[%eval]` e il testo dei commenti delle
mosse): la sua barra di valutazione è la sua, e le valutazioni dei motori lì non compaiono.

La connessione usa Mbed TLS, integrato nel programma, e si fida solo dei certificati radice che firmano lichess.org
(Google Trust Services) e di quelli di Let's Encrypt. Passa per il proxy HTTP indicato da `HTTPS_PROXY` (o
`ALL_PROXY`), come `http://proxy.example:3128` o `http://user:password@proxy.example:3128`, tranne che per gli host di
`NO_PROXY`; anche su Windows il proxy viene da queste variabili, non dalle impostazioni di sistema. I proxy SOCKS non
sono supportati.

### La tua trasmissione su Lichess
<!-- en: your-own-lichess-broadcast 114edd02 -->

*Trasmetti su Lichess…*, nell'intestazione di un torneo che si gioca su questa macchina, apre un riquadro: il nome
della trasmissione (all'inizio quello del torneo), non elencata (per chi ha il suo link) a meno che non si scelga
*Pubblica: elencata su Lichess*, e cosa va a Lichess, il nome, il formato e la cadenza del torneo, e il PGN di ogni
partita. *Crea la trasmissione* chiede la prima volta al tuo account collegato un permesso in più, creare studi e
trasmissioni (per questo si apre la pagina di Lichess), o collega l'account con quel permesso; il token incollato nella
scheda lichess qui sopra serve quando nessun account è collegato. Poi le partite vanno alla trasmissione come descritto
sopra, e un banner sotto l'intestazione dice quante ne sono state inviate e quando è stata inviata l'ultima, con *Apri
su lichess.org* e *Ferma*.

### Un incontro a squadre in onda
<!-- en: a-team-match-on-the-air fd32027f -->

Un incontro a squadre mostra il punteggio delle squadre ovunque: nell'intestazione della pagina web e sopra la sua
classifica (con il punteggio % della squadra A, Elo ± e LOS), in `standings.html` (le squadre, poi i motori di ogni
squadra contro quelli dell'altra), in `tournament.json` e `state.json` (`teams`, `team_elo`, `team_elo_error`,
`team_los`), e nella tabella incrociata TLCS, come una riga sotto la tabella ("Lynx 17½ - 18½ Otter · 48.6 % · Elo
-9.7 ± 31.2"). Le sue partite portano i tag `WhiteTeam` e `BlackTeam` nei file PGN, che le esportazioni conservano.

### Pubblicare la pagina web su internet
<!-- en: putting-the-web-page-on-the-internet ec2bc274 -->

La pagina web si può seguire da ovunque senza aprire una porta nel router: spunta *Pubblica su internet* sotto
*Pagina web* nella scheda *Trasmissione*. Mentre un torneo è in corso, RookStudio apre un tunnel SSH (libssh2,
integrato nel programma) verso un servizio di tunnel, che dà alla pagina un indirizzo https pubblico; non serve
installare nulla.

- *localhost.run* (il predefinito): gratuito, senza account. I suoi indirizzi cambiano ogni tanto, e la sua velocità
  è limitata; la pagina e il suo stato vengono inviati compressi, quindi questo conta poco. La sua chiave host è nota a
  RookStudio: una connessione a qualsiasi altra cosa viene rifiutata.
- *Un altro server SSH*: il suo host, porta e utente, e la porta che apre per la pagina (come chiederebbe
  `ssh -R REMOTE_PORT:localhost:PORT user@host`; 0 la lascia scegliere a lui). Si possono usare solo server che fanno
  entrare l'utente senza chiave né password (autenticazione "none"), e devono offrire curve25519-sha256, una chiave
  host RSA da 2048 a 4096 bit, e AES (GCM o CTR). La chiave del server viene accettata come fidata la prima volta e
  conservata nel profilo (`known_hosts.json`); se cambia, il tunnel si ferma e lo dice, e *Fidati della nuova chiave*
  serve per quando il proprietario del server conferma che è cambiata.

La scheda mostra lo stato (connessione in corso, in onda con il numero di connessioni, riconnessione, o un errore) e,
una volta noto, l'indirizzo pubblico con *Copia*, *Apri* (per vedere la pagina come la vedono gli spettatori) e un
codice QR per un telefono (cliccalo per ingrandirlo). Il tunnel si riconnette da solo dopo un errore, aspettando un
po' di più ogni volta, e mostra il nuovo indirizzo se è cambiato. Si ferma con la pagina web o con il torneo.

Funzionano anche altri tunnel, avviati a parte: con quello di Cloudflare, per esempio, attiva la pagina web (qui porta
8080) ed esegui `cloudflared tunnel --url http://localhost:8080`; stampa un indirizzo come
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel o un reverse proxy come nginx, anche sotto un percorso
proprio (`location /live/ { proxy_pass http://localhost:8080/; }`), funzionano allo stesso modo.

La pagina è in sola lettura: risponde solo a richieste GET per la pagina, il suo stato e il file PGN, mai a un file
nominato da una richiesta, e nulla in essa modifica il torneo. Ogni richiesta riceve una risposta a sé, 32 alla volta
al massimo, e deve arrivare entro un secondo e ricevere risposta entro dieci, così un visitatore lento o silenzioso non
blocca nessun altro; dietro il tunnel la connessione di ogni spettatore conta come una di queste: 40 vengono inoltrate
insieme, altrettante attendono il loro turno, e le successive vengono rifiutate; uno spettatore che invia più di quanto
la pagina accetti viene lasciato andare da solo. Due download del file PGN avvengono insieme, e al successivo viene
chiesto di riprovare un attimo dopo. Lo stato non viene mai messo in cache, nemmeno dal tunnel, così gli spettatori
vedono le partite mentre procedono; la pagina viene conservata dal browser e ricontrollata a ogni caricamento. Gli
spettatori mostrati nella scheda *Trasmissione* sono contati dalle richieste fatte dalle pagine, quindi il conteggio è
giusto anche dietro un tunnel.

Gli spettatori vedono il nome del torneo, i nomi dei motori, i loro rating, ricerche e anomalie, le aperture e il file
PGN delle partite giocate. Le cartelle e i file dei motori restano privati. In modalità headless:

```
rookstudio --run FOLDER --tunnel                      # localhost.run, the address printed on the console
rookstudio --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Dove parlano gli spettatori
<!-- en: where-the-viewers-talk e66bf5c5 -->

La pagina non ha una chat propria; rimanda a dove si svolge la conversazione. Un *Link della chat* sotto *Pagina web*
(un invito Discord, un canale Twitch, una discussione di un forum; solo https, qualsiasi altra cosa viene rifiutata
con un messaggio sotto il campo) mette nell'intestazione della pagina un pulsante *Chat* che lo apre in una nuova
scheda. Mentre le partite vengono inviate a lichess, la pagina rimanda anche al turno su lichess, che ha la sua chat.

Un *ID del server Discord* aggiunge una scheda *Discord* accanto a *Event*, con il widget del server di Discord stesso
nel tema chiaro o scuro della pagina: il nome del server, i membri online, i suoi canali vocali e un pulsante *Join*,
non i messaggi. Il proprietario del server lo attiva in Server Settings, Widget ("Enable Server Widget"), che mostra
anche l'ID del server da copiare (da 17 a 20 cifre); con la Developer Mode attiva, un clic destro sull'icona del server
dà anche *Copy Server ID*. Il widget viene caricato da discord.com solo quando uno spettatore apre la scheda, e
Discord vede allora la visita di quello spettatore (il suo indirizzo, il suo browser, il suo accesso a Discord se ne ha
uno); senza un ID, la pagina non contatta nessuno tranne RookStudio. I link della chat e di lichess non inviano
nulla finché uno spettatore non li clicca, e allora nessun referrer. Un file di configurazione eseguito in modalità
headless (`--run SETUP.json`) prende entrambi da `web_chat_link` e `web_discord_id`.

## Modalità headless
<!-- en: headless-mode a3cec26b -->

```
rookstudio --run TOURNAMENT_FOLDER     # resume a tournament
rookstudio --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Non viene aperta nessuna finestra; la versione, poi l'avanzamento, vengono stampati sulla console, con i tentativi di
un motore che non parte, il motore messo da parte (con il suo log del motore) o tornato, come nella finestra, e la
classifica ogni 20 partite e alla fine (un incontro a squadre: i punteggi delle squadre, poi i motori di ogni squadra
contro quelli dell'altra), con la fine di un torneo accompagnata dalla riga delle copie della classifica (partite,
patte, cadenza, libro ed evento). **Ctrl+C** ferma dopo le partite in corso e il torneo può essere ripreso più tardi,
dalla riga di comando o dall'interfaccia grafica. Codici di uscita: 0 terminato, 1 errore durante il gioco, 2 niente da
eseguire, 3 fermato prima della fine (con Ctrl+C, o perché non resta nulla da giocare senza un motore che non parte).
Anche il budget di memoria e le GPU delle **Impostazioni** del profilo vi si applicano.

`--detach` lo gioca invece in background: il comando ritorna subito, e il torneo prosegue senza un terminale; la
finestra lo mostra (vi si collega) quando si apre con lo stesso profilo, e `--attach` parla con esso:

```
rookstudio --run FOLDER --detach         # in the background, on a local socket
rookstudio --attach FOLDER               # its control channel on the standard input and output
```

La finestra guida ogni torneo allo stesso modo: `--run FOLDER --control socket` (o `--control stdio`) lo gioca senza
stampare nulla, scambiando righe JSON con la finestra: gli eventi del torneo (il suo stato, ogni riga del diario, le
partite in corso mossa per mossa, ogni partita registrata, il suo log, la sua fine) e i comandi della finestra (pausa,
ripresa, arresto, modifiche, la fine di una partita, la sua quota della macchina con
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). La prima riga in ogni direzione è un saluto,
`{"hello": {"protocol": 1, ...}}`; uno script tramite `--attach` riceve il saluto e gli eventi del torneo, e può
inviare comandi come `{"c": "pause"}`. Una sola finestra (o script) alla volta parla con un torneo; `--attach` lo
lascia quando il suo standard input finisce o il suo output non viene più letto, quindi uno script deve tenere aperto
il suo input mentre legge le risposte.

Il torneo può essere trasmesso nello stesso momento, come dalla scheda *Trasmissione*:

```
rookstudio --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
rookstudio --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
rookstudio --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
rookstudio --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
rookstudio --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
rookstudio --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` prende il token dalla variabile d'ambiente `FCG_LICHESS_TOKEN`, o quello del profilo.

Le opzioni si possono combinare. Un file di configurazione salvato con la trasmissione attiva trasmette allo stesso
modo senza di esse. Per gli spettatori fuori dalla rete locale, apri le porte nel router: TLCV e node-tlcv si collegano
alla porta TLCS. Per la pagina web, un tunnel evita di aprire una porta (vedi [Pubblicare la pagina web su
internet](#pubblicare-la-pagina-web-su-internet)).

## Macchine
<!-- en: machines 4d8fbfef -->

Altri computer su cui far girare tornei: una macchina di test, un server, un secondo PC. **Impostazioni** > *Macchine*
li elenca. Una macchina si raggiunge con SSH: su di essa non bisogna aprire nulla tranne il suo server SSH, e il
programma non conserva nulla di segreto.

### Aggiungere una macchina
<!-- en: adding-a-machine 6415de10 -->

*Aggiungi una macchina*, poi sulla sua scheda:

- *Nome*, *Host* (il suo nome sulla rete, o il suo indirizzo), *Porta* (22) e *Utente*, l'account con cui girano i suoi
  tornei;
- *Accedi con*: l'*Agente SSH* (ssh-agent su Linux e macOS, Pageant o il servizio *OpenSSH Authentication Agent* su
  Windows; aggiungigli la tua chiave con `ssh-add`), o un *File di chiave*, RSA o Ed25519 (`~/.ssh/id_ed25519`). La
  passphrase di un file di chiave viene chiesta una volta per sessione: resta in memoria e non viene mai scritta da
  nessuna parte. La macchina deve conoscere la chiave: la sua metà pubblica (`id_ed25519.pub`) va in
  `~/.ssh/authorized_keys` sulla macchina (`ssh-copy-id user@host` ce la mette), o, per un amministratore di una
  macchina Windows, in `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Cartella lì*: la cartella di RookStudio sulla macchina, per il programma, i suoi tornei, libri e tablebase,
  relativa alla cartella home dell'utente (`rookstudio` per impostazione predefinita) o assoluta (`/srv/fcg`,
  `D:\fcg`);
- *Budget*: i core e la memoria che i tornei sulla macchina possono usare insieme; 0 prende ciò che ha trovato il
  test, tutti i thread tranne uno e l'80 % della memoria.

*Prova la connessione* accede, poi indica il sistema della macchina, i thread, la memoria, lo spazio libero dove si
trova la sua cartella, e se RookStudio vi è installato, in quale versione. La prima connessione mostra la chiave
host della macchina (la sua impronta SHA256) e da allora la considera fidata (`known_hosts.json` nel profilo, lo stesso
del tunnel della trasmissione). Quando in seguito la chiave cambia, non viene inviato nulla e la scheda lo dice, con
*Fidati della nuova chiave* per una macchina che è stata reinstallata. Ogni errore nomina la macchina e cosa fare:
l'host che non si riesce a raggiungere, una chiave che la macchina rifiuta (e dove va la sua metà pubblica), una
passphrase che non è quella giusta, nessun agente in esecuzione.

*Installa RookStudio* (*Aggiorna RookStudio* una volta che c'è) copia il programma nella cartella della macchina
via SFTP, poi verifica che lì si avvii (`rookstudio --version`). Copia questo programma quando la macchina usa lo
stesso sistema (da Linux x86-64 a Linux x86-64); per un altro, per esempio una macchina Windows da Linux, scarica la
versione per quel sistema e scegli il suo programma sulla scheda (*RookStudio per Windows x86_64*). Una copia
interrotta dalla rete riprende da dove si era fermata.

### Motori su una macchina
<!-- en: engines-on-a-machine 4b00e462 -->

*Motori su questa macchina*: i motori della libreria che la macchina ha, ciascuno con il suo percorso lì
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Verifica i motori* verifica ogni percorso: il programma c'è, si può
eseguire, e risponde a `uci` (`xboard` per un motore WinBoard) entro 10 secondi. Il segno accanto, e il suo tooltip,
dicono cosa è stato trovato: il nome del motore, o perché non gira (mancante, non eseguibile, fatto per un altro
sistema, muto).

Un motore mancante sulla macchina vi viene copiato con *Copia…* sulla sua riga (o *Copia \<motore\> su \<macchina\>*
nella tavolozza dei comandi, o il pulsante di un avvio rifiutato perché un motore non ha un percorso lì). Il pannello
che si apre dice prima cosa viene inviato: il programma, la sua dimensione e il sistema per cui è compilato, letto dai suoi
primi byte (un programma Windows, uno Linux o macOS, universale o no, o uno script). Un programma che la macchina non
può eseguire viene rifiutato prima di inviare qualsiasi cosa, indicando quale build scegliere: "Alpha è un programma
Windows; tc-box usa Linux: scegli la build Linux di Alpha". Conta anche l'architettura (x86-64 o arm64); una build
x86-64 su un Mac arm64 gira solo sotto Rosetta, e un formato che il programma non conosce viene copiato lo stesso:
entrambi i casi vengono segnalati, non rifiutati. Uno script va solo su una macchina Linux o macOS, e solo quando
l'interprete della sua prima riga (`python3`, `/bin/sh`) c'è. *Scegli la build \<sistema\> di \<motore\>…* prende
un altro file di questo computer, per esempio la build Windows del motore, lo verifica allo stesso modo, e lo tiene per
quella macchina: i confronti e le sostituzioni successivi usano quello, non il programma della libreria.

*Con la sua cartella* copia l'intera cartella del motore accanto al programma (la sua cartella di lavoro, o la cartella
del programma), per un motore che ha bisogno di file accanto a sé: le sue reti, un libro. Il pannello conta prima i
suoi file e la loro dimensione, e nomina le opzioni del motore che puntano al suo interno (`EvalFile` di una rete): in
un torneo sulla macchina puntano alla copia lì, e la cartella è la cartella di lavoro del motore. Una cartella di più di
2000 file non viene proposta (un programma in `/usr/bin` non va copiato con la sua cartella), una cartella `.git` viene
esclusa, e per una macchina Windows un nome di file che Windows non può contenere (`:`, `*`, `?`, un punto finale,
`CON`, `NUL`...) viene nominato e ferma la copia prima che inizi. I file vengono copiati così come sono, fine riga
compresi.

*Copia* invia i file via SFTP in `engines/<engine>/` nella cartella della macchina, in background con il tempo
rimanente e *Annulla*, ripresa da dove si era fermata dopo un'interruzione; lo SHA-256 di ogni file viene confrontato
con quello di questo computer dopo la copia, e i file già presenti e identici non vengono inviati di nuovo. Su Windows
il programma mantiene o riceve la sua `.exe`; altrove viene reso eseguibile (`chmod 755`). Poi il percorso del motore
sulla macchina viene impostato sulla copia e verificato come fa *Verifica i motori*. Su un Mac, un programma copiato da
un altro computer che non si avvia può aver bisogno di una firma o di uscire dalla quarantena: il messaggio indica i
due comandi da eseguire lì (`codesign -s -` e `xattr -d com.apple.quarantine`); il programma non li esegue da sé.

La colonna *Build* confronta il motore lì con quello di questo computer, tramite il loro SHA-256 (lo fanno *Verifica i
motori* e *Confronta*, con il programma di questo computer sottoposto a hash una volta finché non cambia): *stessa
build di questo computer*, o *un'altra build lì*, con *Sostituisci con quello di questo computer*. La sostituzione
copia il programma di questo computer nella cartella del motore dentro la cartella della macchina e vi fa puntare il
motore: un programma altrove sulla macchina (`/opt/engines/stockfish`) non viene mai sovrascritto, resta com'era.

### Libri e tablebase su una macchina
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Verifica ciò che usa la configurazione* confronta il libro di aperture e i libri propri dei partecipanti della
configurazione con la cartella dei libri della macchina, per dimensione e SHA-256 (un libro con lo stesso nome ma un
altro contenuto non è lo stesso libro), e i file Syzygy della configurazione di 6 pezzi al massimo con la cartella
delle tablebase della macchina, per nome e dimensione. *Invia i file mancanti* copia ciò che manca o è diverso, in
background, con il tempo rimanente; ogni libro viene verificato tramite il suo hash una volta copiato, ogni tabella
tramite la sua dimensione, e una copia interrotta riprende. Le tabelle a 7 pezzi non vengono proposte: occupano
terabyte. Un torneo non parte su una macchina senza il suo libro; le tablebase mancanti sono un avviso
(l'aggiudicazione e i motori ne fanno a meno).

### Un torneo su una macchina
<!-- en: a-tournament-on-a-machine f5d4c423 -->

Nella configurazione, *Gioca su* (in *Parallelismo*) sceglie dove gioca il torneo: *Questo computer*, o una macchina.
*Avvia* allora lo prepara lì: RookStudio deve esservi installato in questa versione (altrimenti *Aggiorna
RookStudio*), ogni motore deve avere il suo percorso sulla macchina (altrimenti l'avvio propone *Copia \<motore\> su
\<macchina\>…*), e i libri della configurazione devono essere nella sua cartella dei libri (altrimenti l'avvio dice
quale manca, e *Invia i file mancanti* ce lo mette); le tablebase mancanti sono solo un avviso. Il torneo riceve la sua
cartella in `tournaments/` della cartella della macchina, con i motori, i libri e le tablebase ai loro percorsi lì, e
il suo processo parte lì fuori dalla sessione SSH (`rookstudio --run <folder> --detach`): gioca qualunque cosa
accada alla finestra o alla rete. La finestra lo segue come un torneo di questo computer: Diretta, Partite, la
classifica, pausa, arresto e modifiche, tramite il canale di controllo del suo processo
(`rookstudio --attach <folder>` via SSH).

La sua scheda nella pagina Tornei dice *Su \<macchina\>*, e *Connessione a \<macchina\>…* mentre la connessione
è interrotta: la finestra si riconnette da sola, un po' più tardi dopo ogni errore, e riprende gli eventi dove si era
fermata la sua copia. Il diario e i file PGN vengono ricopiati nella cartella del torneo qui man mano che crescono, ogni
pochi secondi, così le Classifiche, le esportazioni e la checklist dell'invio li leggono; la cartella della macchina
resta il riferimento (`machine.json` qui dice quale macchina e quale cartella). Chiudere la finestra lo lascia giocare
lì; l'avvio successivo lo mostra di nuovo. *Riprendi* su un torneo interrotto di una macchina lo riprende lì. Il budget
della macchina (i suoi core e la sua memoria) è condiviso dai suoi tornei, separatamente da quelli di questo computer.
Un torneo gioca su una sola macchina: le sue partite non vengono distribuite su più macchine.

Una macchina ha bisogno di un server OpenSSH (Linux, macOS, BSD; Windows 10 1809 e successivi), con una chiave host
RSA e SFTP, entrambi presenti per impostazione predefinita. I comandi passano per `sh` qualunque sia la shell
dell'utente (fish o csh vanno bene), e per cmd su Windows, la shell predefinita del suo server OpenSSH.

### Motori giocati da questo computer
<!-- en: engines-played-from-this-computer 785e6bae -->

Un motore della libreria può girare su una macchina mentre il torneo gioca qui: un motore GPU sulla macchina con le GPU,
un motore compilato per un altro sistema, una macchina riservata a un solo motore. Sulla sua scheda, *Gira su* sceglie
*Questo computer* o una macchina; *Programma lì* e *Cartella di lavoro lì* sono allora percorsi sulla macchina (vuoto:
la cartella del programma lì), e i pulsanti *…*, compresi quelli delle opzioni che nominano un file, sfogliano i file
della macchina. Una volta che ci sono delle macchine, *Aggiungi…* propone *Un programma su \<macchina\>…*, *Importa…*
propone *Tutti i motori di una cartella su \<macchina\>…*, e *Aggiungi un motore…* della macchina propone *Un programma
lì…*. Il motore viene letto, verificato e giocato nella console, nel test rapido e nei tornei come lo è un motore di
questo computer. Il suo percorso è anche quello dell'elenco dei motori della macchina, e lo SHA-256 del suo programma lì
dice quando è stato ricompilato (Δ, riletto).

La macchina deve essere stata provata una volta (*Prova la connessione*), così che la sua chiave host sia nota: un
motore non si connette mai a una macchina la cui chiave non è stata mostrata e considerata fidata. La passphrase della
chiave, se ne ha una, viene chiesta una volta per sessione, prima della prima lettura o dell'avvio, e tenuta in memoria:
il processo del torneo la riceve dalla finestra e la conserva, così che il torneo continui a giocare dopo la chiusura
della finestra. `--run` in un terminale non la chiede: lì usa l'agente SSH.

Ogni motore viene avviato tramite un relay, `rookstudio --remote-engine <key>`, servito dal processo che lo fa
giocare: una connessione SSH alla macchina per dieci motori al massimo (il `MaxSessions` di un server OpenSSH), ogni
motore avviato dalla sua cartella lì. Un motore il cui relay se ne va viene terminato anche lì, tramite il suo
identificativo di processo, quando non termina da solo (su una macchina Linux o macOS; su una Windows, viene chiuso il
suo canale).

*Verifica* misura il tempo di andata e ritorno verso la macchina, la mediana di alcuni `isready`. L'orologio lo conta:
il riepilogo della configurazione avvisa quando supera il 5 % dell'incremento, o del tempo che una mossa ha senza
incremento, con *Imposta un margine di … ms* (il doppio dell'andata e ritorno). Una macchina mai provata ferma l'avvio,
e così pure un torneo che gira su una macchina diversa da quella del motore (uno che gira sulla macchina del motore lo
gioca lì come un suo motore).

Mentre il torneo gioca, la pagina Tornei ha una riga *Macchine* sotto i core: ogni macchina, connessa e ciò che vi
gioca, o da quando non risponde. Una macchina che non risponde è una sola voce sotto *Da sistemare*: le partite dei suoi
motori vengono messe da parte e riprovate, le altre proseguono, con *Riprova* e *Prova \<macchina\>*. Una partita che un
motore ha perso perché la sua connessione si è interrotta (la sua fine come disconnessione) viene annullata e rigiocata,
scritta nel diario con il suo motivo (`connection to its machine lost: <machine>`); *Affidabilità* la conta a parte, non
come crash del motore. I motori di una macchina disconnessa più di tre volte in un'ora vengono messi da parte. I core,
la memoria e le GPU di questo computer non contano i motori sulle macchine, solo i loro relay; il budget della macchina,
condiviso dai suoi tornei, non li conta ancora.

### Una macchina Windows
<!-- en: a-windows-machine cd5493d1 -->

Windows 10 (1809 e successivi) e Windows 11 includono un server OpenSSH, non installato per impostazione predefinita.
In PowerShell, come amministratore:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

L'installazione apre la porta 22 nel firewall. Poi metti la tua chiave pubblica in
`C:\Users\<user>\.ssh\authorized_keys`, o, per un account del gruppo Administrators, in
`C:\ProgramData\ssh\administrators_authorized_keys`, che solo Administrators e SYSTEM possono leggere (i gruppi sono
indicati dai loro SID, uguali in tutte le lingue di Windows):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Mantieni cmd come shell del server: RookStudio parla cmd, non PowerShell (se è stato impostato un valore
`DefaultShell` sotto `HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, rimuovilo). Sulla scheda della macchina, scegli il
`rookstudio.exe` della versione per Windows, poi *Installa RookStudio*.

## File
<!-- en: files d79e45d8 -->

Tutto risiede nella cartella del profilo: `~/.local/share/rookstudio/` su Linux,
`%APPDATA%\rookstudio\` su Windows, `~/Library/Application Support/rookstudio/` su macOS. I
file creati dal programma non sono scrivibili da altri utenti, e `settings.json` è leggibile solo dal suo proprietario
appena contiene un token lichess. **Impostazioni** mostra la cartella del profilo, con *Apri la cartella*.

### Modalità portatile
<!-- en: portable-mode 228dba26 -->

Con un file chiamato `portable` (vuoto, senza estensione) accanto al programma (accanto a `rookstudio.exe` o
`rookstudio`, accanto a `RookStudio.app` su macOS), il profilo è invece la cartella `data` accanto al programma,
e la cartella dei tornei predefinita è `data/runs`: non viene scritto nulla altrove, se non nelle cartelle scelte per
tornei, esportazioni e simili. I percorsi di ciò che si trova nella cartella del programma (motori, libri, tablebase,
suite di test, i file PGN delle classifiche e della configurazione, le cartelle scelte) vengono conservati relativi a
essa nei file del profilo, così l'intera cartella si può spostare, copiare su un'altra macchina o eseguire da una
chiavetta USB, qualunque sia la lettera della sua unità, e i suoi motori vengono ritrovati. **Impostazioni** indica
*Modalità portatile* accanto alla cartella del profilo. Per trasferire un profilo esistente, copia i suoi file in
`data`. `--profile` sceglie comunque un'altra cartella, fuori dalla modalità portatile. Un torneo conserva i percorsi
dei motori del momento in cui è stato creato: riprendi un torneo non finito prima di spostare la cartella. Su macOS,
togli prima la quarantena (vedi il README): altrimenti macOS esegue l'applicazione da una posizione temporanea, dove il
file `portable` non c'è.

### La cartella del profilo
<!-- en: the-profile-folder e39d5884 -->

- `engines.json`: la libreria dei motori, e `engines.json.bak` la sua versione precedente, letta se il primo va perso;
- `setup.json`: l'ultima configurazione; `profiles.json`: i tuoi profili delle sezioni della configurazione;
  `templates/*.json`: i modelli salvati; `queue.json`: la coda;
- `book-cache/`: i libri PGN trasformati in libri Polyglot per i libri propri dei partecipanti (rimossi dopo 30 giorni
  di inutilizzo);
- `layout.ini`: la disposizione della finestra e i pannelli dello spazio di lavoro; `layouts.json`: i layout salvati
  per nome; `rookstudio.log`: il log dell'ultima sessione;
- `suite.json`: l'ultima configurazione della pagina Suite; `suites/<suite>-<date>.json`: le esecuzioni terminate
  delle suite di test;
- `board.json`: le impostazioni dell'analisi e delle partite della pagina Scacchiera; `analyses/*.json`: le partite
  analizzate mossa per mossa, un file ciascuna; `games/*.pgn`: le partite giocate dalla pagina Scacchiera;
- `settings.json`: la cartella dei tornei, quando ne è stata scelta un'altra in **Impostazioni**, *Rendering
  software*, la cartella di *Tutti i motori di una cartella…*, il tester e il sito di *Esporta per una lista di
  rating…*, il token lichess, la dimensione e la posizione della finestra, la larghezza del pannello della guida, la
  scheda Per iniziare nascosta, il controllo di una nuova versione (desiderato o no, quando è stato fatto l'ultima
  volta, la versione trovata, l'avviso nascosto), l'aspetto (colori della scacchiera, pezzi, suoni delle mosse), e il
  budget della macchina (core, memoria e GPU per i tornei, *Avvia quando ci sono core liberi*);
- `logos/`: i loghi dei motori, copie delle immagini scelte sulle loro schede;
- `machines.json`: le macchine remote di **Impostazioni** (il loro indirizzo, cartella, budget, percorsi dei motori e
  ciò che ha trovato l'ultimo test; nessuna passphrase), e `known_hosts.json` le chiavi host considerate fidate, delle
  macchine e del tunnel;
- `running.json`: i tornei che giocano in questo momento, con i loro pesi, ciascuno rimosso quando si ferma; trovato
  all'avvio, dice cosa mostrare di nuovo (un torneo che ha continuato a giocare senza la finestra) o riprendere;
- `runs/`: i tornei, a meno che non sia stata scelta un'altra cartella (vedi [La cartella di un
  torneo](#la-cartella-di-un-torneo));
- `databases.json`: i tuoi database, e `databases/` le cartelle di quelli creati qui (`Name.fcgdb`); `searches.json`:
  le ricerche salvate; `repertoires/`: i tuoi repertori, un file PGN ciascuno;
- `recent.json`: la partita della pagina Scacchiera e le partite recenti, con le loro modifiche non salvate;
- `engines/`: i motori popolari scaricati, ogni versione in una cartella tutta sua;
- `assistant.json`: le impostazioni dell'assistente, e `models/` il suo modello;
- `lichess-account.json`: l'account Lichess collegato, i suoi permessi e il suo token (un file leggibile solo da te);
  `lichess-cache/`: le risposte delle tablebase e del cloud di Lichess.

### La cartella di un torneo
<!-- en: a-tournaments-folder 2189632d -->

`runs/<date>_<A>_vs_<B>/` per un match, `runs/<date>_<name>/` per un torneo (in `runs/` o nella cartella scelta), una
cartella ciascuno, che si può spostare, copiare, comprimere in zip e inviare:

- `tournament.json`: il formato, i partecipanti (motori copiati con le loro opzioni) e le condizioni;
- `setup.json`: la configurazione della pagina *Nuovo torneo* da cui è partito, che leggono *Clona* e *Rilancia con…*
  (vedi [Coda e rilancio](#coda-e-rilancio)); un torneo avviato da una versione precedente non ce l'ha;
- `journal.jsonl`: tutto ciò che è successo da allora, una riga per evento (coppie di partite iniziate, partite
  terminate, modifiche, abbinamenti del sistema svizzero e dell'eliminazione diretta); con i file PGN, è ciò che legge
  una ripresa. Una partita è su disco prima della sua riga, quindi un'interruzione di corrente perde al massimo le
  partite in corso. Il testo di una partita a cui nessuna riga rimanda (una partita eliminata da una modifica mentre
  veniva salvata, o lasciata da un crash) viene sostituito da righe vuote, così gli altri visualizzatori PGN mostrano
  solo le partite del torneo;
- `tournament.lock`: tenuto dal programma che gioca il torneo; un altro programma che apre la cartella si limita a
  mostrarlo;
- `<name>.pgn`: commenti delle mosse nello stile cutechess / fastchess, `{+0.35/18 1.234s}`, con il punteggio dal punto
  di vista del lato che ha mosso; partite nell'ordine in cui sono finite (il diario trova ciascuna dal suo posto nel
  file; *Esporta tutto…* nella scheda *Partite*, e il file di *Aggiungi anche ogni partita a*, seguono il calendario);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, con la valutazione dal punto di
  vista del Bianco, come la leggono ChessBase / Fritz e Lichess;
- `<name>` è il nome del torneo al momento della creazione (rinominarlo in seguito conserva i file), con i caratteri
  che Windows non permette sostituiti da `_` e troncato a 64 byte; `tournament.json` registra entrambi i nomi. Un
  torneo senza nome, uno più vecchio, o un match importato dalla 0.2.x ha `games.pgn` e `games-chessbase.pgn`;
- `lichess.json`: la trasmissione e il turno lichess a cui sono andate le sue partite (nessun token);
- `processor.tsv`: il tempo di processore usato dai motori di ogni partita, per l'*Uso del processore* di un torneo
  ripreso;
- `rookstudio.log`: il log dei processi che lo hanno giocato (ogni torneo gioca in un processo proprio);
  `control.json`, mentre uno lo gioca: dove si trova il suo socket locale (`control.sock` nella cartella, o nella
  cartella runtime dell'utente quando quel percorso è troppo lungo; una named pipe su Windows), e quale processo è in
  ascolto;
- `logs/game-N.log`: ogni riga scambiata con entrambi i motori durante la partita N (il numero mostrato nella scheda
  *Partite*), con l'ora in cui è stata inviata o letta (`W>` inviata al motore del Bianco, `W<` letta dal suo output,
  `W!` letta dal suo output degli errori: asserzioni, messaggi di crash), per le partite perse per tempo, per crash,
  per blocco o per mossa illegale (o per ogni partita, se richiesto). Vengono conservate le prime 300 righe di ogni
  motore e le ultime 5000 righe della partita, nell'ordine in cui sono arrivate. È ciò di cui ha bisogno l'autore di un
  motore per riprodurre un bug. Per un motore WinBoard mostra il lato UCI; `FCG_WB2UCI_LOG` dà il lato WinBoard;
- `logs/start-<engine>.log`: l'ultimo tentativo fallito di avviare un motore che non parte: il suo programma, l'errore,
  e le righe scambiate con i motori di quella partita fino a quel momento (vedi [Un motore che non
  parte](#un-motore-che-non-parte)).

Una cartella ricevuta da qualcun altro si può aprire senza rischi: il file di *Aggiungi anche ogni partita a* deve
essere un file `.pgn` (un altro nome viene ignorato, con un avviso), e una cartella il cui diario, i cui file PGN o il
cui file di lock sono link simbolici non viene aperta in scrittura, quindi non può far scrivere il programma al di
fuori di essa. Riprenderla esegue i motori nominati dal suo `tournament.json`, come fa qualsiasi torneo.

I match giocati con la versione 0.2 compaiono nella pagina Tornei: *Importali* li trasforma in tornei terminati.

## Riga di comando
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

- `--profile DIR` usa un'altra cartella del profilo;
- `--autostart` avvia la configurazione salvata all'avvio (invece di riprendere il torneo che era in corso, vedi
  [Pausa, arresto e ripresa](#pausa-arresto-e-ripresa));
- `--size WxH` apre la finestra a quella dimensione, centrata, invece che come è stata lasciata, e non tocca la
  dimensione e la posizione conservate per l'avvio successivo;
- `--software` fa disegnare la finestra al processore (il renderer software di SDL) invece che a OpenGL, come fa
  *Rendering software* in **Impostazioni**, per questo avvio (vedi [Disegno della finestra](#disegno-della-finestra));
- `--shot` salva uno screenshot della finestra dopo il numero di secondi indicato (5 per impostazione predefinita), con
  WINDOW portata in primo piano, ed esce una volta presi tutti gli screenshot. Si può ripetere. WINDOW è una pagina
  (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`,
  `console`, `settings`) o una scheda (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). Il
  mouse viene ignorato in questa modalità e, su Linux, la finestra viene disegnata fuori dallo schermo (non compare
  nessuna finestra; imposta `SDL_VIDEODRIVER` per cambiarlo); la finestra si apre alla dimensione predefinita, o
  `--size`, e la dimensione e la posizione conservate non vengono toccate;
- `--focus WINDOW` apre all'avvio quella pagina o scheda, oppure: `engines-card` (la scheda del primo motore),
  `engines-refresh` (*Aggiorna: i nuovi motori della cartella*), `engines-ratings=FILE` (l'anteprima di quella lista
  Elo), `setup-picker` e `setup-suggest` (il filtro della libreria con il focus, o gli avversari suggeriti
  selezionati), `setup-open=ROW` e `setup-profiles=ROW` (una riga della configurazione aperta, o il suo elenco di
  profili: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (il libro
  proprio del primo partecipante), `submission-export` (*Esporta per una lista di rating…*), `ratings-players` (la
  finestra *Giocatori PGN*), `clone=DIR` (la pagina *Nuovo torneo* compilata da *Clona* dalla cartella di quel
  torneo), `palette[=TEXT]` (la tavolozza dei comandi, con TEXT digitato), `game-menu=N` (il menu
  della partita N nella scheda *Partite*, con un risultato scelto), `end-menu=N` (*Termina con un risultato* della
  partita dello slot N, da 1), `big-screen[=N]` (lo schermo grande, della partita dello slot N), `live-back=N` (la
  partita seguita in *Diretta*, riportata indietro di N semimosse), `engines-console` (la console del primo motore),
  `board-analyse` (la pagina Scacchiera, con l'analisi), `board=FILE` (un file PGN, FEN o EPD nella pagina Scacchiera;
  `board-analyse=FILE` con l'analisi, `board-review=FILE` con la sua prima partita analizzata mossa per mossa),
  `board-edit` (l'editor di posizione), `board-play` e `board-play-start` (una partita tra i primi due motori,
  avviata), `machines` (la scheda della prima macchina remota in **Impostazioni**; `machines-test` con la prova,
  `machines-files` con i libri e le tablebase della configurazione confrontati con essa), `guide[=ID]` (il pannello
  della guida, alla sezione ID), `guide-search=TEXT` (il pannello della guida, con la ricerca di TEXT);
- `--select-game N` apre la partita N (in ordine di conclusione) nella scheda *Partite* appena esiste;
- `--quit-after SECONDS` ferma il torneo ed esce;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: vedi [Modalità headless](#modalità-headless);
- `--version` stampa la versione e il protocollo del canale tramite cui la finestra segue un torneo remoto (vedi
  [Macchine](#macchine)), ed esce.

## Risoluzione dei problemi
<!-- en: troubleshooting e8c4882d -->

### Il programma non parte
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows dice che il programma non è sicuro, o un antivirus lo rimuove.** Il programma non è firmato, quindi Windows
  SmartScreen e alcuni antivirus ne diffidano. *Ulteriori informazioni*, *Esegui comunque* lo lascia partire; a un
  antivirus si può dire di consentire la sua cartella. Le versioni sono compilate da GitHub Actions a partire dai
  sorgenti pubblici.
- **macOS si rifiuta di aprirlo.** Non è firmato da un account sviluppatore Apple: fai Ctrl-clic (clic destro) su di
  esso, Apri, poi di nuovo Apri; su macOS 15 e successivi, prova ad aprirlo una volta, poi Impostazioni di Sistema,
  Privacy e sicurezza, Apri comunque. Oppure esegui `xattr -dr com.apple.quarantine` sulla cartella estratta.
- **Un messaggio dice che nessun modo di disegnare la finestra funziona, o la finestra resta nera.** Al driver grafico
  manca OpenGL 3.2 o non funziona: avvia con `--software` (il processore disegna la finestra), e se funziona, spunta
  *Rendering software* in **Impostazioni**. Sotto Wayland senza un driver grafico, avvia con `SDL_VIDEODRIVER=x11`.
  Vedi [Disegno della finestra](#disegno-della-finestra).
- **Il testo cinese appare come quadratini.** Il cinese ha bisogno di un font del sistema: Microsoft YaHei su Windows,
  Noto Sans CJK o WenQuanYi su Linux (installa `fonts-noto-cjk` o simili).

### Un motore non parte
<!-- en: an-engine-does-not-start 202a67d4 -->

- **La sua verifica fallisce, o il suo torneo dice che non riesce ad avviarsi.** Leggi l'errore sotto il motore, poi il
  suo log del motore (*Log del motore*, o `logs/start-<engine>.log` nella cartella del torneo): l'output degli errori
  del motore spesso nomina il file mancante. Le cause abituali: il programma spostato (*Individua…*), una rete o una
  libreria non trovate (imposta la cartella di lavoro, o `EvalFile`, sulla sua scheda), un programma compilato per un
  altro processore (una build che usa AVX-512 su una macchina che non lo ha), un avvio lento (aumenta *Timeout di
  avvio* sulla sua scheda). Vedi [Un motore che non parte](#un-motore-che-non-parte).
- **Un motore WinBoard non parte dalle posizioni del libro.** Un motore del protocollo 1 senza `setboard` può partire
  solo dalla posizione iniziale: dagli un libro PGN (mosse), non un libro EPD. Vedi [Motori
  WinBoard](#motori-winboard).
- **Il torneo si rifiuta di partire: un motore non gioca a Chess960.** Ogni motore deve avere l'opzione
  `UCI_Chess960` (UCI) o la variante `fischerandom` (WinBoard); il riepilogo nomina quelli che non le hanno.

### Partite perse o che sembrano sbagliate
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Un motore perde per tempo all'inizio delle sue partite.** Più spesso è configurato male che non gioca male: troppi
  thread per i core di cui dispongono le partite, un hash troppo grande per essere allocato in tempo, una rete
  caricata a ogni partita, un avvio lento. Abbassa i suoi `Threads` e il suo `Hash`, togli la spunta a *Riavvia il
  motore a ogni partita*, aumenta *Timeout di avvio*; la scheda *Affidabilità* e la colonna *TF* della panoramica
  mostrano le sconfitte per tempo.
- **I motori sembrano più lenti di quanto dovrebbero.** Troppe partite insieme: lascia *Parallelismo* automatico, e
  mantieni parallelismo × thread entro i core fisici. *Uso del processore* nella panoramica nomina un motore che usa
  più core dei suoi `Threads`, o pensa sul tempo del suo avversario.
- **Il riepilogo avvisa che la memoria non basta.** Le tabelle hash delle partite non entrerebbero nella memoria
  libera: abbassa l'`Hash` (in *Impostazioni dei motori*, o quello del motore che nomina) o il parallelismo.
- **Dei file di tablebase risultano mancanti o troncati.** Un download si è fermato a metà, o la cartella contiene solo
  tabelle più piccole: scarica di nuovo i file nominati nel tooltip, o abbassa l'aggiudicazione ai pezzi trovati
  (*Abbassa a 5 pezzi*).
- **Un libro risulta avere posizioni illegali o duplicati.** fastchess le esclude; *Salva una copia ripulita…* nella
  pagina Libri scrive un libro senza di esse. Vedi [Verificare un libro](#verificare-un-libro).
- **Una partita è stata decisa in modo sbagliato.** Clic destro su di essa in *Partite* o *Calendario*: rigiocala,
  annullala, o fissane il risultato, con il motivo. Vedi [Modificare un torneo in
  corso](#modificare-un-torneo-in-corso).

### Trasmettere
<!-- en: broadcasting 4a2e6916 -->

- **TLCV o un altro visualizzatore non si collega.** Avvia il torneo (e il suo server TLCS) prima del visualizzatore:
  non si riconnette da solo. Apri la porta UDP nel router per gli spettatori fuori dalla rete locale.
- **La pagina web non è raggiungibile dall'esterno.** Spunta *Pubblica su internet* per un tunnel (vedi [Pubblicare la
  pagina web su internet](#pubblicare-la-pagina-web-su-internet)), o apri la porta TCP nel router.
- **lichess rifiuta il token.** Il token ha bisogno del permesso `study:write`: creane uno nuovo con *Crea un token su
  lichess…*. Dietro un proxy, imposta `HTTPS_PROXY`.

### Tutto il resto
<!-- en: anything-else ce59c083 -->

*Segnala un problema…* in **Impostazioni** o nella pagina **Log** apre una issue su GitHub già compilata con la
versione, il sistema e le ultime righe del log (vedi [Segnalare un problema](#segnalare-un-problema)).

## Glossario
<!-- en: glossary 7cb8fba3 -->

- **Aggiudicazione**: terminare una partita prima del matto quando il suo risultato è chiaro: patta quando entrambe le
  valutazioni restano vicine a 0, sconfitta quando restano molto sotto, o ciò che dicono le tablebase. Vedi
  [Aggiudicazione](#aggiudicazione).
- **BayesElo, DrawElo**: un modello di rating con un parametro separato per le patte (DrawElo), dal programma di Rémi
  Coulom.
- **Buchholz, Sonneborn-Berger**: criteri di spareggio: la somma dei punteggi degli avversari (un sistema svizzero), e
  la somma dei punteggi degli avversari battuti più la metà di quelli con cui si è pattato (un girone all'italiana).
- **Chess960, Chess324**: posizioni di partenza con la prima traversa mescolata: 960 con il re tra le torri (l'arrocco
  porta sulle case abituali), 324 con il re e le torri al loro posto.
- **Ciclo**: in un girone all'italiana, ogni abbinamento giocato una volta con ciascun colore (cutechess lo chiama
  round, da 2 partite per incontro).
- **Cursed win**: una posizione che le tablebase considerano vinta, ma il cui matto richiede più mosse di quante ne
  permetta la regola delle 50 mosse: patta in una partita giocata con quella regola.
- **ECO**: i codici dell'Encyclopaedia of Chess Openings, da A00 a E99.
- **Elo, nElo**: la differenza di rating che un punteggio rappresenta. Il nElo (Elo normalizzato) divide per la
  dispersione dei risultati, così il suo valore non dipende dalla percentuale di patte; fishtest e OpenBench vi
  esprimono i limiti dei loro SPRT.
- **EPD, FEN**: una riga che descrive una posizione; una riga EPD può contenere operazioni, come la mossa migliore `bm`
  di una suite di test.
- **Gauntlet**: i motori in prova (le teste di serie) giocano contro tutti gli altri motori, che non giocano tra loro.
- **H0, H1**: le due ipotesi di un SPRT: la differenza Elo è elo0 o meno (H0), o elo1 o più (H1).
- **Hash**: la memoria che un motore occupa per la sua tabella delle trasposizioni, in MB.
- **LLR**: il log-likelihood ratio (logaritmo del rapporto di verosimiglianza) di un SPRT; il test finisce quando esce
  dai suoi limiti, ricavati da alpha e beta.
- **LOS**: la likelihood of superiority, la probabilità che un motore sia più forte di un altro, calcolata dalle loro
  partite.
- **MultiPV**: un motore che analizza più linee insieme.
- **Novità**: la prima mossa di una partita che nessuna partita del database aveva giocato in quella posizione.
- **Parallelismo**: il numero di partite giocate contemporaneamente.
- **Pentanomiale**: i risultati delle coppie di partite contati in base al loro totale: 0, ½, 1, 1½ o 2 punti. Dà
  barre d'errore più strette che contare le partite singole, perché le due partite di una coppia condividono
  un'apertura.
- **PGN**: il formato testuale delle partite a scacchi: tag come `[White "…"]`, poi le mosse.
- **Polyglot**: il formato binario dei libri di aperture (`.bin`) letto dalla maggior parte dei programmi, con un peso
  per ogni mossa.
- **Ponder**: pensare sul tempo dell'avversario; disattivato nei tornei di RookStudio.
- **Repertorio**: le aperture che si giocano, una mossa scelta per il proprio lato in ogni posizione, e le mosse
  dell'avversario a cui si risponde.
- **SPRT**: il sequential probability ratio test (test sequenziale del rapporto di verosimiglianza), che gioca partite
  finché può dire, con i tassi di errore scelti (alpha, beta), se un motore guadagna elo1 o più (H1) o non più di elo0
  (H0).
- **Suite di aperture, libro**: le posizioni o le mosse da cui partono le partite, ciascuna giocata due volte a colori
  invertiti.
- **Syzygy**: le tablebase dei finali che danno il risultato di ogni posizione con pochi pezzi (file WDL, `.rtbw`).
- **Tabelle Berger**: l'ordine dei turni di un girone all'italiana in cui ogni motore gioca una volta per turno.
- **Testa di serie**: un posto nell'ordine dei partecipanti: i motori in prova di un gauntlet, i favoriti di
  un'eliminazione diretta.
- **TF**: time forfeit, le partite perse per tempo.
- **Threads**: i thread del processore con cui un motore analizza.
- **TLCS, TLCV**: Tom's Live Chess Server e Viewer, un protocollo per seguire in diretta le partite tra motori.
- **UCI, WinBoard (CECP)**: i due protocolli con cui i programmi parlano con i motori di scacchi.
- **WDL**: le probabilità di vittoria, patta e sconfitta che un motore dà insieme alla sua valutazione.

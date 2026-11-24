# Guide de l'utilisateur de RookStudio

Ce guide décrit chaque page, chaque réglage et chaque fichier de RookStudio : des moteurs qui jouent des tournois,
et l'étude des parties (bases de parties, notation, arbre d'ouvertures, répertoires, Lichess et un assistant local).
Le [README](../README.md) dit ce qu'est le programme, où le télécharger et comment le compiler. Dans le programme, F1
ouvre ce guide à côté de la page, à la section de ce qui est sous la souris, dans la langue de l'interface (voir
[Aide dans le programme](#aide-dans-le-programme)).

## Démarrage rapide
<!-- en: quick-start 92353f2c -->

### Ton premier tournoi
<!-- en: your-first-tournament 75e9a891 -->

1. **Moteurs.** Dans la barre de gauche, *Moteurs*, puis *Ajouter…* et choisis le programme d'un moteur (un
   exécutable UCI ou WinBoard) : il est lancé une fois pour lire son nom et ses options. *Importer…* > *Une liste de
   moteurs…* reprend les moteurs d'un autre programme (cutechess, Banksia, Arena, ChessGUI), *Importer…* > *Tous les
   moteurs d'un dossier…* tous les programmes d'un dossier. *Vérifier* dit si un moteur fonctionne.
2. **Nouveau tournoi.** *Tournois* > *Nouveau tournoi*. Choisis un *Format* dans sa liste (*Toutes rondes*,
   *Gauntlet*…) et une *Cadence* (*10+0.1* pour un premier essai), et laisse les autres lignes telles quelles.
3. **Participants.** Double-clique sur des moteurs dans la *Bibliothèque*, à côté des participants, ou
   sélectionnes-en plusieurs et appuie sur Entrée.
4. **Lancer.** Le tournoi s'ouvre dans son espace de travail : *Vue d'ensemble* pour le classement et le temps
   restant, *Direct* pour les parties en cours.

Le résumé, à droite de la configuration, indique combien de parties seront jouées et combien de temps elles
prendront, et ce qui n'irait pas (un programme introuvable, trop de threads, un livre manquant), chaque fois avec un
bouton qui le corrige.

### Tester une nouvelle version d'un moteur
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Ajoute les deux versions à la bibliothèque.
2. *Nouveau tournoi*, *Format* : *SPRT gain [0, 3]* (la nouvelle version gagne-t-elle ?) ou *SPRT non-régression
   [-5, 0]* (n'est-elle pas plus faible ?).
3. Choisis la nouvelle version comme **A** et l'ancienne comme **B**, une cadence courte (*10+0.1*) et, dans
   *Ouvertures*, un livre de positions équilibrées (EPD), puis **Lancer**.

Le match s'arrête de lui-même dès que le SPRT tranche : H1 acceptée (A est plus fort, au sens des bornes) ou H0
acceptée (il ne l'est pas). La *Vue d'ensemble* montre l'Elo, le LLR et ses bornes au fil des parties. La
configuration est gardée : la prochaine fois, choisis la version suivante comme A, puis **Lancer**, ou clique sur
*Relancer avec…* sur le match terminé.

### Étudier tes parties
<!-- en: studying-your-games 9a78e41c -->

1. **Une base.** Dans la barre, *Bases*, puis *Nouvelle base*, nommée sur place, et *Importer un fichier…* : un
   fichier PGN, ou un fichier `.zst`, `.gz` ou `.zip` qui en contient un (un fichier déposé sur la page fait de
   même). Les parties arrivent en arrière-plan, puis l'arbre d'ouvertures de la base est construit.
2. **Une partie.** Un clic sur une partie de la liste la montre à côté ; Entrée ou un double clic l'ouvre sur
   l'*Échiquier*. Ctrl+O y ouvre un fichier PGN, Ctrl+V colle une partie ou un FEN.
3. **Un moteur.** L'analyse a besoin d'un moteur : avec une bibliothèque vide, l'onglet *Analyse* propose de
   télécharger Gaia en un clic (voir [Moteurs populaires](#moteurs-populaires)). A lance et arrête l'analyse de la
   position, G analyse toute la partie (Alt+A et Alt+G sur l'Échiquier d'étude).
4. **Tes notes.** Entrée écrit un commentaire après le coup, `!` et `?` posent des symboles, un coup autre que celui de
   la partie commence une variante, le bouton droit dessine des flèches et des cases ; Ctrl+S enregistre la partie dans
   sa base.

### Pour aller plus loin
<!-- en: where-to-go-next d21be582 -->

- [Nouveau tournoi](#nouveau-tournoi) : chaque réglage, et des [conseils](#conseils-pour-les-réglages) pour les
  choisir ;
- [Un tournoi en cours](#un-tournoi-en-cours) : ce que montrent les onglets, la pause, la modification d'un tournoi
  pendant qu'il se joue ;
- [Classements](#classements) : une seule liste de classement sur toutes les parties jouées ;
- [Bases](#bases), [l'Échiquier d'étude](#léchiquier-détude), [la page Ouvertures](#la-page-ouvertures), [la page
  Lichess](#la-page-lichess) et [l'assistant](#lassistant) pour tes propres parties ;
- [Vues](#vues) : les pages de la barre, comme tu les veux ;
- [Diffusion](#diffusion) : les parties en direct sur une page web, dans TLCV ou sur lichess ;
- [Dépannage](#dépannage) quand quelque chose ne va pas, et le [Glossaire](#glossaire) pour les mots des testeurs.

## La fenêtre
<!-- en: the-window 3a5230d4 -->

### Les pages
<!-- en: the-pages 1cab137d -->

Une barre de navigation, à gauche, mène aux pages de la [vue](#vues) affichée, dans son ordre :

| Page | Contenu |
|---|---|
| Accueil | La partie à reprendre, ce qu'on peut lancer, les parties récentes, tes bases, Lichess et ce qui tourne : des cartes des pages de la vue (voir [La page Accueil](#la-page-accueil)) |
| Tournois | Ce qui tourne et ce qui demande une action, la file d'attente, les derniers tournois terminés, tous les tournois du profil, *Nouveau tournoi* |
| Échiquier | Une partie ou une position venue de n'importe où, analysée par un moteur de la bibliothèque ; dans une vue pour l'étude, la notation écrite sur place (voir [L'Échiquier d'étude](#léchiquier-détude)) |
| Bases | Tes bases de parties : l'import, la liste des parties, les recherches |
| Ouvertures | Tes répertoires, leur couverture et leurs lacunes |
| Entraînement | La révision du jour, les problèmes de Lichess, tes erreurs en exercices, deviner les coups, les finales contre les tables, les mats, la vision de l'échiquier (voir [La page Entraînement](#la-page-entraînement)) |
| Jouer | Une partie contre un moteur de la bibliothèque à la force que tu choisis, avec ses ouvertures et un handicap, et tes parties sur Lichess, jouées les unes et les autres sur la page Échiquier (voir [La page Jouer](#la-page-jouer)) |
| Lichess | Ton compte Lichess, les parties d'un joueur, les diffusions officielles, ce que l'analyse peut demander à Lichess |
| Moteurs | La bibliothèque de moteurs : options, vérifications, groupes, Elo de référence, import des listes d'autres programmes, les moteurs populaires |
| Livres | La bibliothèque de livres d'ouvertures, et les ouvertures de chaque livre avec ce qui ne va pas chez elles |
| Suites | Les suites de tests (STS, WAC, ERET…) : des moteurs face aux positions d'un fichier EPD |
| Classements | Une seule liste de classement sur toutes les parties de tous les tournois |
| Journal | La sortie de fastchess lui-même et les messages du programme, et *Signaler un problème…* |
| Réglages | Langue, thème, taille de l'interface, notifications, apparence des échiquiers, vues, assistant, dossiers du profil et des tournois, machines distantes, jeton lichess, version, manière de dessiner la fenêtre : d'abord les sections des pages de la vue, les autres sous *Autres réglages* |

### Vues
<!-- en: views 9c72e404 -->

Une vue est un ensemble de pages à toi : jusqu'à trois, chacune avec son nom, son icône et sa couleur, et on passe de
l'une à l'autre en haut de la barre. Chaque vue y a une pastille, pleine pour la vue affichée, et le nom de la vue
affichée est écrit en dessous ; un clic sur une pastille, ou Ctrl+1 à Ctrl+3, affiche cette vue, à la page où elle
était. Avec une seule vue, le sélecteur disparaît.

Le premier démarrage en propose trois : *Tests* (Tournois, Moteurs, Livres, Échiquier, Suites, Classements, Journal),
*Étude* (Accueil, Échiquier, Bases, Ouvertures, Entraînement, Jouer, Lichess, Moteurs) et *Tout* (toutes les pages).
Réglages est dans chaque vue. Une page qui arrive avec une nouvelle version n'est pas ajoutée d'elle-même à une vue que
tu as : la page Accueil la propose une fois (voir [La page Entraînement](#la-page-entraînement) et [La page
Jouer](#la-page-jouer)).

*Modifier les vues* ouvre l'éditeur des vues à la place de la page : *Réglages* > *Vues*, ⋯ sous le sélecteur, un clic
droit sur une pastille (l'éditeur s'ouvre sur sa vue), ou *Modifier les vues…* dans Ctrl+K. On y trouve :

- les vues, dans l'ordre de Ctrl+1 à Ctrl+3 (*Monter*, *Descendre*), *Nouvelle vue* (trois au plus) et *Supprimer
  cette vue* ;
- la vue choisie : son nom (court : il s'écrit sous le sélecteur), son icône parmi douze, sa couleur parmi six ;
- son Échiquier : *Pour l'étude* (la notation écrite sur place, les flèches et les cases, les onglets Arbre,
  Références et Questions : voir [L'Échiquier d'étude](#léchiquier-détude)) ou *Pour l'analyse des moteurs*
  (l'Échiquier des testeurs de moteurs, tel qu'il était) ;
- ses pages, cochées, dans l'ordre de la barre (↑ et ↓ à côté de chacune la déplacent ; Réglages reste en dernier) ;
  une page peut être dans plusieurs vues, et *aussi : …* nomme les autres ;
- la barre, telle qu'elle sera.

Tout s'affiche aussitôt dans la barre. Ctrl+Z annule chaque modification tant que l'éditeur est ouvert, Entrée garde
les modifications, Échap laisse les vues comme elles étaient. *Rétablir les vues proposées* ramène les trois du premier
démarrage.

Ctrl+K liste chaque page avec la vue dans laquelle elle s'ouvre : choisir une page d'une autre vue passe à celle-ci.
Une page qu'aucune vue ne contient (un avis peut y mener) s'ouvre dans la vue affichée, avec *L'ajouter à …*.

### Le premier démarrage
<!-- en: the-first-start 18171dec -->

Au premier démarrage, RookStudio demande :

- la langue de l'interface ;
- ce que tu veux faire : les vues proposées, chacune cochée ou non (*Tests*, *Étude*, *Tout*), et laquelle s'affiche
  en premier ;
- quand un profil fastchess-gui est sur la machine, s'il faut le copier : ses moteurs, ses livres, ses tournois et ses
  réglages. fastchess-gui reste tel quel et peut continuer à tourner à côté ; *Démarrer sans* n'y touche pas.

Tout peut se changer plus tard dans *Réglages* > *Vues*.

### Aide dans le programme
<!-- en: help-in-the-program c08dbff5 -->

- **Infobulles.** Laisse la souris sur un contrôle pour voir ce qu'il fait. L'infobulle d'un contrôle que ce guide
  explique plus en détail se termine par *F1 : en savoir plus dans le guide*.
- **F1** ouvre ce guide dans un panneau à droite de la page, à la section de ce qui est sous la souris (une ligne de
  la configuration, une carte de la page Tournois, un onglet, une colonne du classement) ; sans rien sous la souris,
  à la section du dernier panneau cliqué, sinon de la page. Le **?** rond à côté du titre d'une page ouvre la section
  de la page. F1 de nouveau, ou Échap dans le panneau, le ferme.
- **Le panneau du guide** montre une section avec ses sous-sections, et la page à côté continue de fonctionner : on
  lit l'explication tout en changeant le réglage. Son en-tête a *Retour* et *Avancer* (aussi Alt+Gauche et
  Alt+Droite, ou les boutons latéraux de la souris), *Sommaire* (toutes les sections), le champ de recherche (Ctrl+F
  dans le panneau) et *Ouvrir dans le navigateur* (ce guide sur GitHub, pour la version qui tourne). La recherche
  trouve chaque mot tapé, dans n'importe quel ordre, sans tenir compte de la casse ni des accents ; les titres
  viennent en premier, et les correspondances sont surlignées. Les liens vers d'autres sections s'ouvrent dans le
  panneau, les liens vers des pages web dans le navigateur, et un bloc de code a *Copier*. Fais glisser le bord
  gauche du panneau pour l'élargir ; sa largeur est gardée.
- **Langues.** Le guide est intégré au programme, dans chaque langue de l'interface, et suit la version qui tourne ;
  rien n'est récupéré sur le réseau. Une section dont la traduction manque ou est plus ancienne que le texte anglais
  est montrée en anglais, avec une ligne qui le dit. Les archives des versions le contiennent aussi (`guide.md`, et
  `guide.<language>.md` pour chaque traduction).
- **Ctrl+K**, la palette de commandes, trouve les sections du guide par leurs titres (*Guide : …*), et *Rechercher
  « … » dans le guide* y cherche ce qui a été tapé.
- **Messages.** Un problème que le programme signale nomme le moteur, le fichier ou la ressource, avec sa correction
  sur place ; un **?** à côté ouvre son entrée dans [Dépannage](#dépannage).
- **Premiers pas.** Jusqu'au lancement d'un premier tournoi, la page Tournois montre les étapes du
  [Démarrage rapide](#démarrage-rapide), chacune avec son bouton, cochée une fois faite ; *Masquer* les range.
- [Signale un problème](#signaler-un-problème) quand le programme ne fait pas ce qu'il devrait.

### La palette de commandes
<!-- en: the-command-palette 14590459 -->

**Ctrl+K** ouvre la palette de commandes : les pages et les onglets, les moteurs de la bibliothèque (elle ouvre leur
fiche), les livres, les tournois de la page Tournois, les sections du guide, et les actions possibles sur le moment
(*Pause*, *Reprendre*, *Tout mettre en pause*, *Arrêter*, *Lancer*, *Mettre en file d'attente*, *Relancer … avec…*,
les entrées d'*Importer…*, *Vérifier ceux qui ne l'ont pas été*, *Exporter pour une liste de classement…*, *Copier* le
classement, *Copier la version*, *Signaler un problème…*, les couleurs d'échiquier et les jeux de pièces, les sons des
coups activés ou non…). La casse ne compte pas et les lettres tapées n'ont pas besoin de se suivre (« lc » trouve
*Liste de classement*) ; les noms qui commencent par elles, ou par un mot qui commence par elles, viennent en premier.
Haut et Bas choisissent, Entrée exécute, Échap ferme ; Ctrl+K n'agit pas pendant la saisie dans un champ de texte.

### Langue, thème et taille
<!-- en: language-theme-and-size 954fe416 -->

L'interface est en anglais, français, allemand, espagnol, italien, portugais du Brésil, russe ou chinois simplifié.
Au démarrage, elle suit la première langue préférée du système (l'anglais quand il n'y a pas de traduction pour
elle) ; on peut l'imposer dans **Réglages**. Le chinois utilise une police du système : Microsoft YaHei sous Windows,
Noto Sans CJK ou WenQuanYi sous Linux. Le thème est clair, à moins que le thème sombre, ou celui du système, ne soit
sélectionné dans **Réglages**.

Les traductions espagnole, italienne, portugaise, russe et chinoise sont nouvelles : les corrections des joueurs qui
parlent ces langues sont les bienvenues. Celles de l'interface sont de simples fichiers `.po` dans `gui/i18n`, que
Poedit et ses semblables ouvrent ; celles de ce guide sont `docs/guide.<language>.md`.

Les pages occupent toute la fenêtre, quelle que soit sa taille : les échiquiers de l'onglet Direct grandissent avec
elle, le formulaire de nouveau tournoi se partage en deux colonnes dans une fenêtre large, et la liste des pages
Moteurs, Livres et Classements s'élargit quand on fait glisser son bord droit. Tout (texte, boutons, échiquiers) peut
aussi être agrandi ou réduit, de 80 à 250 % : **Réglages** > Taille de l'interface, ou Ctrl + molette de la souris,
Ctrl + et Ctrl -, et Ctrl 0 pour revenir à 100 %. F11 met la fenêtre en plein écran, et l'en sort.

Un badge sur l'onglet *Direct* et sur le bouton *Tournois* donne le nombre de parties en cours. Un champ numérique
accepte le calcul : `40*60`, `15+10`, avec `+ - * /` et les parenthèses, calculé sur Entrée ou quand on quitte le
champ ; Échap rend au champ sa valeur.

### Les échiquiers et leurs boutons
<!-- en: boards-and-their-buttons 1ed997f4 -->

Sous chaque échiquier qui montre une partie (le rejeu de l'onglet *Parties*, la partie mise en avant dans *Direct*,
un échiquier détaché, la page Échiquier), une rangée de petits boutons va à la position de départ, au coup précédent,
au coup suivant et au dernier, et retourne l'échiquier ; leurs infobulles donnent les touches, qui font de même : ← →
Origine Fin, et F pour retourner. Ceux qui préfèrent les touches masquent les boutons avec **Réglages** >
*Apparence* > *Boutons de navigation sous les échiquiers*, un clic droit dessus, ou la palette de commandes
(*Masquer les boutons de navigation sous les échiquiers*, et *Afficher…* pour les faire revenir). La molette de la
souris sur une liste de coups fait défiler la liste, jamais les coups. **A** sur n'importe quel échiquier ouvre sa
position sur la page [Échiquier](#échiquier) et l'analyse.

### Version et nouvelles versions
<!-- en: version-and-new-versions 6305c913 -->

La version est dans le titre de la fenêtre et dans **Réglages**, dont *Copier* la met dans le presse-papiers avec le
système, pour un rapport de bug. *Me prévenir quand une nouvelle version sort* (désactivé par défaut) demande à GitHub
la dernière version publiée au lancement, au plus une fois par jour et en arrière-plan ; quand elle est plus récente,
la page Tournois le dit, avec *Page de téléchargement* (la page de la version sur GitHub, dans le navigateur),
*Nouveautés* (ses notes) et *Masquer* (jusqu'à la version suivante). Le programme ne télécharge et n'installe rien,
et un échec (pas de réseau, GitHub injoignable) est seulement écrit dans le journal.

### Signaler un problème
<!-- en: reporting-a-problem 3e4e5989 -->

*Signaler un problème…* (dans **Réglages**, sur la page **Journal**, dans la palette de commandes, et dans le message
d'un échec au démarrage) ouvre un nouveau ticket sur GitHub dans le navigateur, à partir du formulaire de rapport de
bug, déjà rempli avec la version, le système (système d'exploitation, processeur, mémoire, carte graphique et pilote)
et les dernières lignes du journal. Relis-le avant de l'envoyer, retire ce que tu ne veux pas montrer (chemins, noms
de moteurs) et dis ce que tu as fait, ce que tu attendais et ce qui s'est passé. Le programme n'envoie rien lui-même.
Les idées vont au même endroit : *New issue*, *Idea*.

### Taille et position de la fenêtre
<!-- en: window-size-and-place 892d41c4 -->

La fenêtre s'ouvre comme elle a été laissée : sa taille, sa position sur les écrans, agrandie ou non. Quand l'écran
où elle se trouvait a disparu (un moniteur débranché), elle s'ouvre au milieu de l'écran principal ; quand sa taille
ne tient plus dans l'écran (une résolution plus basse), à la taille par défaut. Les panneaux sortis de la fenêtre,
comme un échiquier détaché, reviennent aussi là où ils étaient (`layout.ini`), et dans la fenêtre quand leur écran a
disparu. Wayland ne laisse pas un programme choisir la position de sa fenêtre : là, seuls la taille et l'état agrandi
reviennent.

### Rendu de la fenêtre
<!-- en: drawing-the-window caff7bef -->

La fenêtre est dessinée avec OpenGL 3.2. Là où il n'y en a pas (une machine virtuelle sans accélération 3D, un bureau à
distance, un vieux serveur), le programme la dessine à la place avec le rendu propre à SDL : Direct3D 11 sous Windows,
OpenGL 2.1, ou le processeur seul. Le journal dit lequel, et pourquoi OpenGL n'a pas été utilisé ; **Réglages** > Rendu
le montre aussi, avec le nom de la carte graphique quand OpenGL dessine. *Rendu logiciel* à cet endroit (ou `--software`
sur la ligne de commande, pour un lancement) fait dessiner la fenêtre par le processeur à partir du lancement suivant,
pour un pilote graphique qui échoue ou plante. Il prend plus de temps processeur, ne redessine que ce qui change (et ce
qui bouge, comme les pendules, 15 fois par seconde plutôt que 30 tant que la souris et le clavier sont au repos),
dessine des coins carrés, et les panneaux de l'espace de travail flottent à l'intérieur de la fenêtre principale plutôt
que sur un autre écran. Sous Wayland (et sur macOS), l'image dessinée par le processeur passe tout de même par le pilote
graphique ; s'il n'y en a pas, lance le programme avec `SDL_VIDEODRIVER=x11`. Quand aucune manière de dessiner ne
fonctionne, un message le dit avant que le programme se termine.

### Apparence
<!-- en: appearance 312266af -->

**Réglages** > *Apparence* choisit l'aspect de chaque échiquier du programme (la mosaïque en direct et la partie
suivie, les échiquiers détachés, le grand écran, une partie rejouée dans *Parties*, l'onglet *Ouvertures*, les
ouvertures d'un livre), avec, à côté des choix, un petit échiquier qui les montre :

![Réglages > Apparence : l'échiquier brun et les pièces Celtic](screenshots/appearance.png)

- **Échiquier** : les couleurs des cases, pour le thème clair et pour le thème sombre (l'échiquier du thème en usage
  est dessiné). Un préréglage (*Gris bleu*, les couleurs des versions précédentes ; *Brun* ; *Bleu* ; *Vert* ;
  *Gris*), ou *Personnalisé* : un clic sur *Cases claires*, *Cases sombres* ou *Dernier coup* ouvre un sélecteur de
  couleur, et changer une couleur fait de l'échiquier un échiquier personnalisé à partir des couleurs qu'il avait.
  *Dernier coup* est posé sur les deux cases du dernier coup, sa transparence laissant passer la couleur de la case.
- **Pièces** : un jeu intégré au programme (*Cburnett*, celui par défaut ; *Chessnut* ; *Celtic* ; *Fantasy* ; leurs
  auteurs et leurs licences dans l'infobulle et dans `ATTRIBUTION.md`), ou *Ton propre dossier…* : un dossier de
  12 images SVG nommées `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` et `bK.svg` … `bP.svg` (roi,
  dame, tour, fou, cavalier, pion, blancs puis noirs). Les images sont dessinées avec nanosvg, comme celles intégrées :
  formes simples, remplissages, contours et dégradés ; ni texte, ni filtres, ni images incorporées. Quand un fichier
  manque, dépasse 1 Mo ou n'est pas une image qui peut être dessinée, la page dit lequel et pourquoi, les pièces par
  défaut sont dessinées en attendant, et *Relire* relit le dossier une fois les fichiers corrigés. La page web de la
  diffusion garde les pièces par défaut.
- **Sons des coups** (désactivés par défaut) : *Un son pour chaque coup* joue un bref claquement à chaque coup de la
  partie suivie, dans *Direct* et sur le grand écran, et un plus lourd pour une prise ; pas pour chaque partie de la
  mosaïque. *Volume*, et *Essayer un coup* / *Essayer une prise* pour les entendre. Les sons sont produits par le
  programme (aucun fichier son) ; le périphérique audio n'est ouvert que lorsqu'ils sont activés, et quand il n'y en a
  pas, le journal dit pourquoi et le programme reste muet.

- **Figurines dans la notation** (activées par défaut) : les coups de la page Échiquier, dans ses coups et dans les
  lignes de son analyse, sont écrits avec les pièces du jeu choisi plus haut (Nf3 devient un cavalier et f3) ;
  désactivées, avec des lettres.
- **Logos** (désactivés par défaut) : *Les logos des moteurs dans Direct et sur le grand écran* dessine le logo de
  chaque moteur, choisi sur sa fiche (voir plus bas), à côté de son nom dans *Direct*, sur ses cartes et sur le grand
  écran ; désactivés, les noms seuls. La page web de la diffusion les montre dans tous les cas.

La palette de commandes propose aussi ces choix (*Couleurs de l'échiquier : Brun*, *Pièces : Celtic*, *Activer les sons
des coups*, *Afficher les logos des moteurs*…). Ils sont gardés dans `settings.json`, dans le profil ; le dossier des
pièces est gardé relatif au dossier du programme en mode portable.

**Logos des moteurs** : la fiche d'un moteur, dans la page **Moteurs**, a *Logo* > *Choisir…*, une image PNG, JPEG ou
SVG de 2 Mo au plus (4096 pixels au plus dans chaque sens), montrée à côté du nom du moteur dans *Direct* et sur le
grand écran quand les *Logos* sont activés (plus haut), et sur la page web de la diffusion. L'image est copiée dans le
dossier `logos` du profil : l'original peut donc être déplacé ou supprimé, et le profil déplacé en entier ; *Retirer*
l'enlève (avec sa copie, à moins qu'un autre moteur ne l'ait). Le logo peut être changé pendant que le moteur joue. Tant
que les logos sont désactivés, la fiche le dit, avec *Afficher les logos*.

### L'échiquier 3D
<!-- en: the-3d-board cc0dd596 -->

**Réglages** > *Apparence* > *Échiquier 3D* > *L'échiquier de la page Échiquier en 3D* dessine l'échiquier de la page
[Échiquier](#échiquier) en trois dimensions, comme la photographie d'un vrai échiquier (désactivé par défaut ; tous
les autres échiquiers restent plats). La carte graphique le calcule par tracé de chemins : l'image s'affine quelques
secondes tant que rien ne bouge, quelques millisecondes de la carte à chaque image, puis la carte se repose ; pendant
que l'échiquier tourne, une image plus grossière la remplace. Le petit échiquier d'*Apparence* est alors dessiné en 3D
lui aussi, pour que les choix à côté se voient à mesure qu'ils changent :

- l'éclairage, comme les photographes éclairent une nature morte : *Studio*, *Low key*, *High key*, *Salle
  d'exposition*, *Lumière de fenêtre*, *Heure dorée* ou *Salle de tournoi* ;
- *Exposition* : plus clair ou plus sombre que l'éclairage ne le rend, en diaphs, chacun doublant ou divisant par deux
  la lumière ;
- *Qualité* : les échantillons dont l'image est faite avant de se reposer, 512 par défaut ; moins ménage la puce
  graphique d'un processeur pendant les tournois ;
- *Vernis*, d'un bois ciré et mat à une couche où se reflètent les lumières, et *Brillance*, du satiné au miroir ;
- *Défaut* remet l'exposition, la qualité, le vernis et la brillance comme ils étaient ; l'éclairage reste.

Ce que montre l'échiquier plat y est : la barre d'évaluation, le dernier coup, la pièce prise en main et où elle peut
aller, un roi en échec, les flèches de l'analyse, qui se voient en transparence à travers les pièces, la réponse
qu'attendait le moteur qui a joué le coup en pointillés, le jugement du coup en pastille au-dessus de sa case, et le
cadre d'une ligne montrée à la place de la partie. Un clic gauche choisit une case comme sur l'échiquier plat, sur une
pièce ou sur l'échiquier derrière elle, et une pièce peut être glissée, son fantôme suivant le pointeur (voir
[Échiquier](#échiquier)). Le bouton droit maintenu pendant que la souris bouge fait tourner autour de l'échiquier (un
clic droit sans bouger lâche toujours la pièce prise en main), la molette rapproche, et un clic de la molette ramène la
vue ; retourner l'échiquier (F) fait faire un demi-tour à la caméra.

L'échiquier 3D demande OpenGL 3.3. Là où la fenêtre est dessinée sans lui (le rendu logiciel de
[Rendu de la fenêtre](#rendu-de-la-fenêtre), certaines machines virtuelles et certains bureaux à distance), les
échiquiers restent plats et *Apparence* dit pourquoi. Les pièces sont les Staunton-Pieces de clarkerubber, sous
licence MIT ; *Apparence* donne le lien vers elles.

## Raccourcis clavier
<!-- en: keyboard-shortcuts 149fc0ee -->

| Touches | Où | Ce qu'elles font |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Partout | La palette de commandes |
| <kbd>Ctrl</kbd>+<kbd>1</kbd>, <kbd>Ctrl</kbd>+<kbd>2</kbd>, <kbd>Ctrl</kbd>+<kbd>3</kbd> | Partout | Afficher la première, la deuxième, la troisième vue |
| <kbd>F1</kbd> | Partout | Ce guide, à la section de ce qui est sous la souris ; une seconde fois : le ferme |
| <kbd>F11</kbd> | Partout | Plein écran, et retour (la fenêtre propre d'un échiquier détaché quand elle a le clavier) |
| <kbd>Ctrl</kbd>+molette, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Partout | Taille de l'interface ; <kbd>Ctrl</kbd>+<kbd>0</kbd> : retour à 100 % |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Un champ numérique | Calculer la valeur ; rendre au champ sa valeur |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Le panneau du guide | Retour, avancer |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | Le panneau du guide | Rechercher dans le guide |
| <kbd>Échap</kbd> | Le panneau du guide | Le fermer |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Page préc.</kbd> <kbd>Page suiv.</kbd> | La palette de commandes | Choisir |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | La palette de commandes | Exécuter, fermer |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Origine</kbd> <kbd>Fin</kbd> | Un échiquier qui montre une partie | Coup précédent, coup suivant, départ, dernier coup |
| <kbd>F</kbd> | Un échiquier | Le retourner |
| <kbd>A</kbd> | Un échiquier | Ouvrir la position sur la page Échiquier et l'analyser ; sur la page Échiquier : lancer ou arrêter l'analyse |
| <kbd>Fin</kbd> | La partie mise en avant dans *Direct* | Revenir au coup en cours |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | La page Échiquier | Coller un FEN, une ligne EPD, un PGN ou une liste de coups |
| <kbd>G</kbd> | La page Échiquier | Analyser la partie, passe après passe ; une seconde fois : l'arrêter |
| <kbd>X</kbd> | La page Échiquier | Afficher ou masquer la menace |
| <kbd>N</kbd>, <kbd>Maj</kbd>+<kbd>N</kbd> | La page Échiquier | Le coup suivant, le coup précédent marqué par l'analyse |
| <kbd>Échap</kbd> | La page Échiquier | Rétablir la partie après qu'une ligne a été montrée ; reposer la pièce prise en main ; une partie suivie depuis une mosaïque : retour à la mosaïque |
| <kbd>Ctrl</kbd>+<kbd>O</kbd>, <kbd>Ctrl</kbd>+<kbd>N</kbd> | Une vue pour l'étude | Ouvrir un fichier PGN sur la page Échiquier ; y commencer une nouvelle partie |
| <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>Maj</kbd>+<kbd>Tab</kbd> | Une vue pour l'étude | Les parties récentes ; relâcher Ctrl ouvre celle choisie |
| <kbd>Entrée</kbd> | La page Accueil | Reprendre la partie sur l'échiquier |
| Une lettre d'un coup | L'Échiquier d'étude | Taper le coup : les coups légaux qui commencent ainsi sont proposés, dans les lettres de la langue de l'interface ou dans les lettres anglaises ; <kbd>↑</kbd> <kbd>↓</kbd> choisissent, Entrée le joue |
| <kbd>Entrée</kbd>, <kbd>Maj</kbd>+<kbd>Entrée</kbd> | L'Échiquier d'étude | Un commentaire après le coup, avant lui |
| <kbd>!</kbd> <kbd>?</kbd> | L'Échiquier d'étude | Le symbole du coup ; deux signes à la suite : `!!`, `??`, `!?`, `?!` |
| <kbd>Suppr</kbd> | L'Échiquier d'étude | Supprimer les coups à partir de celui affiché |
| <kbd>Alt</kbd>+<kbd>↑</kbd> | L'Échiquier d'étude | Remonter la variante, jusqu'à la ligne principale |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | L'Échiquier d'étude | Annuler, rétablir |
| <kbd>Ctrl</kbd>+<kbd>S</kbd>, <kbd>Ctrl</kbd>+<kbd>Maj</kbd>+<kbd>S</kbd> | L'Échiquier d'étude | Enregistrer la partie ; en enregistrer une copie ailleurs |
| <kbd>Ctrl</kbd>+<kbd>←</kbd>, <kbd>Ctrl</kbd>+<kbd>→</kbd> | L'Échiquier d'étude | La partie précédente, la suivante de la liste d'où elle vient |
| <kbd>Ctrl</kbd>+<kbd>Maj</kbd>+<kbd>C</kbd> | L'Échiquier d'étude | Le diagramme de la position dans le presse-papiers, en image |
| <kbd>Alt</kbd>+<kbd>H</kbd> | L'Échiquier d'étude | Déplier les en-têtes de la partie (un indice pendant une partie contre un moteur) |
| <kbd>Alt</kbd>+<kbd>1</kbd> … <kbd>Alt</kbd>+<kbd>6</kbd> | L'Échiquier d'étude | Les onglets Notation, Analyse, Arbre, Références, Questions et Finales |
| <kbd>Alt</kbd>+<kbd>C</kbd> | L'Échiquier d'étude | L'assistant commente le coup |
| <kbd>Alt</kbd>+<kbd>Q</kbd> | L'Échiquier d'étude | Pourquoi pas ? Le coup essayé contre celui de la partie, ou le coup de la partie contre le meilleur du bilan |
| <kbd>Espace</kbd>, <kbd>Entrée</kbd>, <kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Échap</kbd> | La visite guidée | « Je ne sais pas », le moment suivant, pourquoi pas un autre coup, arrêter |
| <kbd>Alt</kbd>+<kbd>F</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>X</kbd>, <kbd>Alt</kbd>+<kbd>G</kbd>, <kbd>Alt</kbd>+<kbd>N</kbd>, <kbd>Alt</kbd>+<kbd>Maj</kbd>+<kbd>N</kbd> | L'Échiquier d'étude | Ce que font F, A, X, G, N et Maj+N sur l'Échiquier des moteurs (ici, les lettres tapent des coups) |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd>, <kbd>*</kbd> | L'onglet Arbre | Choisir un coup, le jouer, le marquer dans le répertoire actif |
| <kbd>Entrée</kbd> | La page Ouvertures | Commencer l'entraînement sur le répertoire affiché |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Espace</kbd>, <kbd>Échap</kbd> | Une séance d'entraînement | Un indice, « Je ne sais pas », arrêter |
| <kbd>Entrée</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Une séance d'entraînement | La position suivante, ou de nouveau après une mauvaise réponse ; retirer un coup ajouté au répertoire |
| <kbd>Entrée</kbd> | La page Entraînement | Tout réviser ; ou lancer les problèmes une fois l'un de leurs choix cliqué |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Espace</kbd>, <kbd>Échap</kbd> | Une série de problèmes | Un indice, « Je ne sais pas », arrêter |
| <kbd>Entrée</kbd>, <kbd>→</kbd> | Une série de problèmes | Le problème suivant, ou de nouveau après une mauvaise réponse ; la solution un coup à la fois |
| <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Espace</kbd>, <kbd>Échap</kbd> | Un exercice de tes erreurs | Un indice, « Je ne sais pas », arrêter |
| <kbd>Entrée</kbd> | Un exercice de tes erreurs | L'exercice suivant, ou de nouveau après une mauvaise réponse |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Deviner les coups | La partie continue aussitôt après une note ; arrêter |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd> | Les finales de la page Entraînement | Choisir une finale une fois l'une d'elles cliquée ; la jouer |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd>, <kbd>Échap</kbd> | Une finale contre les tables | Reprendre un coup, le bon coup, arrêter |
| <kbd>Entrée</kbd> | Une finale contre les tables | Après une faute : la reprendre ; à la fin : recommencer, ou la suivante |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Vision de l'échiquier | Commencer, continuer ; arrêter |
| <kbd>Entrée</kbd> | La page Jouer | Jouer une partie contre le moteur choisi |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> | Une partie contre un moteur | Reprendre ton dernier coup ; un indice |
| <kbd>Entrée</kbd> | Une partie contre un moteur | Confirmer *Abandonner…* ; reprendre une partie gardée à la fermeture de RookStudio ; une fois terminée, faire son bilan |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | La page Jouer, sur Lichess | Accepter le défi reçu, sinon chercher une partie ; retirer la recherche d'adversaire ou le défi |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Une partie sur Lichess | Envoyer le coup par correspondance en attente, confirmer *Abandonner…* ; retirer le coup en attente ou joué à l'avance |
| <kbd>Entrée</kbd> | La page Jouer, les tournois de Lichess | Rejoindre l'arène ou le tournoi suisse affiché (la première fois : l'autoriser sur Lichess) |
| <kbd>Alt</kbd>+<kbd>P</kbd> | Partout, dans une arène sur Lichess | Pause dans l'arène, les aides de retour ; une seconde fois : retour dans l'arène |
| Une lettre, puis un chiffre | Les coordonnées, *Où est… ?* | Nommer une case |
| <kbd>Maj</kbd> enfoncée, <kbd>F2</kbd> | À l'aveugle | Les pièces montrées 2 secondes, trois fois par partie ; *Où est… ?* |
| <kbd>N</kbd>, <kbd>n</kbd>, <kbd>-</kbd> | Une ligne lue de tête | La pièce comme l'écrit le FEN (un cavalier blanc, un noir), ou aucune |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd> | Une liste de parties | Parcourir la liste, ouvrir la partie |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La page Bases | Le panneau de recherche |
| <kbd>Échap</kbd> | La page Bases | Fermer le panneau de recherche, puis la liste |
| <kbd>Ctrl</kbd>+<kbd>Page suiv.</kbd>, <kbd>Ctrl</kbd>+<kbd>Page préc.</kbd> | Le dossier d'un joueur | L'onglet suivant, le précédent |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Le dossier d'un joueur | Choisir parmi les homonymes, garder le choix ; l'annuler |
| <kbd>Échap</kbd> | Le dossier d'un joueur | Revenir à la page d'où il a été ouvert |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd> | Les tournois d'une base | Choisir un tournoi, ouvrir son tableau croisé |
| <kbd>Échap</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Le tableau croisé d'un tournoi | Revenir aux tournois ; annuler sa dernière modification de la base |
| <kbd>Ctrl</kbd>+<kbd>Page suiv.</kbd>, <kbd>Ctrl</kbd>+<kbd>Page préc.</kbd> | Un rapport d'ouverture | La section suivante, la précédente |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Entrée</kbd> | Les coups d'un rapport d'ouverture | Choisir un coup, le jouer sur la page Échiquier |
| <kbd>Échap</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Un rapport d'ouverture | Revenir à la position ; retirer les coups ajoutés au répertoire |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | La feuille Export | Enregistrer le fichier (un PDF : l'ouvrir pour l'imprimer) ; revenir à la page |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> | La feuille Export, un PDF | L'ouvrir dans le lecteur du système, pour l'imprimer |
| <kbd>Ctrl</kbd>+<kbd>Page suiv.</kbd>, <kbd>Ctrl</kbd>+<kbd>Page préc.</kbd> | La feuille Export | Le format suivant, le précédent |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Origine</kbd> <kbd>Fin</kbd>, <kbd>F</kbd> | Une page HTML faite par RookStudio | Parcourir les coups ; retourner l'échiquier |
| <kbd>←</kbd> <kbd>→</kbd> | Le grand écran | La partie précédente ou suivante |
| <kbd>+</kbd> <kbd>-</kbd> | Le grand écran | Texte plus grand ou plus petit |
| <kbd>Échap</kbd> | Le grand écran | Le quitter |
| <kbd>Tab</kbd>, flèches, <kbd>Entrée</kbd>, <kbd>Espace</kbd> | Nouveau tournoi | Passer d'une ligne, d'une liste ou d'un bouton à l'autre ; ouvrir une ligne ou une liste |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | Les participants | Envoyer les participants sélectionnés tout en haut, tout en bas |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Les participants | Les déplacer d'une place |
| <kbd>Suppr</kbd> | Les participants | Les retirer |
| <kbd>Menu</kbd>, <kbd>Maj</kbd>+<kbd>F10</kbd> | Les participants | Ouvrir le menu du participant (ses propres réglages) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La bibliothèque de la configuration | Aller à son filtre |
| <kbd>Entrée</kbd> | La bibliothèque de la configuration | Ajouter les moteurs sélectionnés, ou le premier affiché |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | La bibliothèque de la configuration | Sélectionner tous les moteurs affichés |
| <kbd>Ctrl</kbd>+clic, <kbd>Maj</kbd>+clic | Listes | En sélectionner plusieurs |
| <kbd>Suppr</kbd> | La page Moteurs | Retirer le moteur sélectionné (après confirmation) |
| <kbd>Entrée</kbd> | La fiche d'un moteur, son bot Lichess prêt | Mettre le bot en ligne |
| <kbd>Entrée</kbd> | La fiche d'un moteur, *Sur lichess.org, pour l'analyse* ouvert | Le proposer à Lichess (la première fois : l'autoriser sur Lichess) ; *Reprendre* le service arrêté |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Faire d'un compte un bot Lichess, son nom tapé | En faire un bot ; annuler |
| <kbd>Entrée</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | La console d'un moteur | Envoyer la ligne ; faire revenir les lignes précédentes |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | Le mode sans interface | S'arrêter après les parties en cours |

## La page Accueil
<!-- en: the-home-page 827b3d01 -->

La page **Accueil** d'une vue fait des cartes des pages de la vue, ce qui tourne en premier :

- **L'arène** où tu es depuis RookStudio, en haut : son nom, ton rang et tes points, le temps restant, *Revenir à
  l'arène* et *Pause* (Alt+P) ; un tournoi suisse que tu as rejoint, sa ronde (voir [Arènes et tournois
  suisses](#arènes-et-tournois-suisses)).
- **Reprendre là où tu en étais**, quand la vue contient l'Échiquier : la partie sur l'échiquier à son coup, sur un
  petit échiquier, d'où elle vient (sa base et son numéro, sa place dans un filtre, ou son fichier), ses commentaires,
  et si elle a des modifications pas encore enregistrées. *Reprendre* (Entrée) y revient ; Ctrl+Tab liste les parties
  récentes.
- Ce qu'on peut lancer, chacun avec sa touche : *Ouvrir un PGN…* (Ctrl+O), *Coller une partie ou un FEN* (Ctrl+V),
  *Nouvelle partie* (Ctrl+N), *Importer dans une base…*, *Jouer contre un moteur* (la page Jouer), *Nouveau tournoi* :
  ceux des pages de la vue.
- Les tournois en cours, comme sur la page Tournois, quand la vue contient Tournois.
- **Récemment** : les parties ouvertes sur l'échiquier, un clic en rouvrant une avec ses modifications.
- **Mes bases** : chacune avec ses parties et l'état de son arbre d'ouvertures, l'import en cours, *+ Importer…*, et
  les tournois lus sans copie (voir [Bases](#bases)).
- **Lichess** : le compte, les parties d'un joueur avec *Synchroniser*, les nouvelles parties comptées si tu l'as
  choisi, les diffusions en direct (voir [La page Lichess](#la-page-lichess)).
- **En cours** : une partie sur Lichess jouée depuis RookStudio (*Revenir à la partie*) et les défis reçus (voir [Sur
  Lichess](#sur-lichess)), ton bot Lichess en ligne et sa partie (voir [Un bot Lichess](#un-bot-lichess)), tes moteurs
  au service de l'échiquier d'analyse de lichess.org (voir [Ton moteur sur lichess.org](#ton-moteur-sur-lichessorg)),
  l'analyse de la partie, celle de la position, un import, l'assistant (son téléchargement, son modèle chargé, *Décharger*), une série de problèmes, une séance d'exercices, une partie à deviner, une finale ou un
  exercice de vision en cours, et ce que la révision du jour a à revoir, en un seul compte, un clic révisant le tout
  (voir [La révision du jour](#la-révision-du-jour)).

Le bouton de recherche en haut, ou Ctrl+K, trouve une commande, une partie, une base.

## La page Tournois
<!-- en: the-tournaments-page 91a20163 -->

La page **Tournois** est une salle de contrôle : d'abord ce qui tourne et ce qui a besoin de toi, puis le reste.

- **Premiers pas**, jusqu'au lancement d'un premier tournoi : les étapes du [Démarrage rapide](#démarrage-rapide)
  (ajouter des moteurs, préparer un tournoi, le lancer), chacune avec son bouton et cochée une fois faite, et *Lire le
  démarrage rapide*, qui l'ouvre dans le panneau du guide. *Masquer* range la carte.
- **Tout mettre en pause**, à côté d'*Ouvrir un dossier…* et de *Nouveau tournoi*, met en pause les tournois en cours
  (aucune nouvelle paire de parties ne commence, les parties en cours vont jusqu'au bout) et retient la file
  d'attente : aucun tournoi en attente ne commence avant **Tout reprendre**, qui les reprend et laisse la file
  d'attente continuer, aussitôt si un tournoi s'est terminé entre-temps.
- **Cœurs** : les threads matériels de la machine, et ceux que prennent les parties en cours, le reste libre. Une
  partie prend les `Threads` de celui de ses deux moteurs qui en a le plus, puisque les deux moteurs réfléchissent
  chacun à leur tour ; la barre montre le nom de chaque tournoi et son nombre (les moteurs de la page Échiquier, la
  partie du bot Lichess et les requêtes de tes moteurs sur lichess.org aussi), les détails s'affichant au curseur.
- **À traiter**, quand quelque chose l'exige, chaque fois avec sa correction sur place : un moteur qui ne démarre pas
  (voir [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas) : *Réessayer*, *Retirer…*, *Remplacer…*,
  *Journal moteur*) ; le tournoi qu'une erreur ou un moteur a arrêté, avec ce qui l'a arrêté, *Ouvrir le moteur*,
  *Ouvrir le journal*, *Reprendre*, *Ouvrir* et *Masquer* ; un disque trop plein pour une nuit de parties (moins de
  1 Go libre, ou moins de 2 % d'un disque de moins de 500 Go), celui du dossier des tournois ou d'un tournoi en cours,
  vérifié chaque minute, avec *Ouvrir le dossier* et *Changer le dossier des tournois…* (là où vont les nouveaux
  tournois, comme dans **Réglages**).
- **En cours** : une carte par tournoi en cours, avec son nom, son format, sa cadence et son état, une barre de
  progression avec les parties jouées sur celles prévues et les parties en cours, l'heure à laquelle il finit, et qui
  mène : le premier du classement avec son Elo face au plateau et ses points (un KO : les joueurs restants ; un
  match par équipes : le score des équipes et son Elo), le moteur en tête d'un match avec son Elo et le score, ou le
  LLR d'un SPRT avec ses bornes, puis l'Elo et le score. *Ouvrir* va à son espace de travail, *Pause* le met en pause.
  Un tournoi qui peut être repris s'affiche à la place sous *Interrompu*, avec *Reprendre*. Voir
  [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois) pour les poids et les cœurs de chacun.
- **Ton bot Lichess**, tant qu'il est en ligne : son moteur, sa partie en cours avec *Sur l'Échiquier*, *Sa fiche* et
  *Mettre hors ligne* ; il refuse les défis *plus tard* tant que les tournois ont besoin des cœurs (voir [Un bot
  Lichess](#un-bot-lichess)).
- **Tes moteurs sur lichess.org**, tant qu'ils servent : chacun avec son état, *La fiche du moteur* et *Arrêter le
  service* ; ils ne prennent que les threads que les tournois laissent libres (voir [Ton moteur sur
  lichess.org](#ton-moteur-sur-lichessorg)).
- **File d'attente** : les tournois en attente, chacun avec sa durée et l'heure à laquelle il commence, et l'heure à
  laquelle tout se termine (voir [File d'attente et relance](#file-dattente-et-relance)).
- **Derniers terminés** : les cinq derniers tournois terminés, avec leur résultat : le score d'un match, la décision
  du SPRT, le vainqueur d'un KO (celui de sa finale), le score d'un match par équipes, ou le premier du classement avec
  les points de ses parties. *Relancer avec…* pour un match, *Cloner* (voir [File d'attente et
  relance](#file-dattente-et-relance)) et *Ouvrir*.
- **Santé** : les plantages ou blocages, les pertes au temps et les coups illégaux des dernières 24 heures, la mémoire
  libre et installée, et les processus de moteurs en cours ; puis les moteurs qui ont perdu ces parties, les trois qui
  en comptent le plus, avec *Ouvrir le moteur* et *Ouvrir le dossier* (le dossier `logs` du tournoi garde le journal
  moteur de chaque partie qui a mal fini). Une perte au temps avant le coup 20 est mise à part : c'est plus souvent un
  moteur mal réglé (trop de threads, un hash trop grand pour être alloué à temps, un réseau chargé à chaque partie, un
  démarrage lent) que sa gestion du temps.
- **Tous les tournois** : tous les tournois du profil, du plus récent au plus ancien, avec *Ouvrir*, *Cloner* et
  *Dossier* ; un clic sur le titre de la carte la replie. Le résultat d'un tournoi terminé s'affiche au curseur,
  par-dessus son état.

La palette de commandes propose aussi ces actions : *Tout mettre en pause*, *Tout reprendre*, *Relancer … avec…*
pour les derniers matchs terminés, *Cloner …* pour le tournoi affiché et les derniers terminés, *Ouvrir le dossier
des tournois* et *Changer le dossier des tournois…*.

## Moteurs
<!-- en: engines 4f15155a -->

La page **Moteurs** est la bibliothèque de moteurs : la liste à gauche, la fiche du moteur choisi à droite.

### Ajouter et vérifier des moteurs
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Ajouter…** enregistre un exécutable UCI. Le moteur est lancé une fois pour lire son nom, son auteur et ses options.
Les options se modifient dans un tableau : les valeurs modifiées sont mises en évidence, et seules les valeurs qui
diffèrent du défaut du moteur sont envoyées avec `setoption` au début de chaque partie. `Threads` et `Hash` viennent
en premier.

- **Dupliquer** crée une seconde entrée pour le même binaire, avec d'autres options (un autre `Hash`, un niveau de
  jeu, un autre fichier de réseau…). Les entrées prennent le nom UCI `id name` au premier ajout et peuvent être
  renommées.
- **Vérifier** lance le moteur, mesure la vitesse à laquelle il répond à `uci` et `isready`, et lui fait jouer un
  coup ; puis il fait passer au moteur les étapes de conformité UCI de fastchess (`id name` et `id author`, positions
  depuis un FEN
  et avec des coups, `go` avec chaque pendule, lignes info avec un score entier…), jusqu'à la première qui échoue.
  Le résultat (✓ ou l'erreur) reste à côté du moteur avec sa date, et *Rapport de conformité UCI* liste les étapes,
  avec ce que le moteur a écrit après celle qui a échoué. Un moteur à qui l'on parle en xboard n'a que la première
  partie.
- **Console…** dialogue avec le moteur, hors de tout tournoi : il est lancé avec le programme, les arguments et le
  dossier de travail de la bibliothèque, et un moteur UCI reçoit `uci` puis les valeurs d'options de la bibliothèque.
  Les lignes tapées sont envoyées telles quelles (Entrée ; Haut et Bas font revenir les précédentes), et le journal
  montre chaque ligne avec son heure, celles envoyées marquées `>` et la sortie d'erreur `!`. Des boutons envoient les
  commandes habituelles (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`… ; celles de WinBoard
  pour un moteur WinBoard, à qui l'on parle dans son propre protocole sauf si *Par l'adaptateur* est coché) et la
  position de la page Échiquier. *Copier* et *Enregistrer…* gardent le journal. La console s'affiche à la place des
  réglages du moteur (*Réglages* y revient, la console restant ouverte) ; *Fermer* arrête le moteur. Pendant qu'un
  tournoi tourne, ce que le moteur calcule prend des cœurs à ses parties : la console le dit. Son moteur ne fait pas
  partie des processus du tournoi : la fin d'un tournoi ne l'arrête pas.
- **Test rapide** joue un court match contre le moteur lui-même ou un autre : 10 parties en 10+0.1 depuis 5 positions
  (dont une avec une prise en passant), par le même code qu'un tournoi, affiché dans son espace de travail pendant
  qu'il joue. Le résultat reste à côté du moteur : son score, ses pertes au temps, plantages et coups illégaux, et la
  part de sa pendule qu'il a utilisée, qui signale un moteur qui joue bien trop vite ou trop près de la chute du
  drapeau. Ses PV de mat sont vérifiées (voir *Vérifier les PV de mat* dans [PGN et divers](#pgn-et-divers)). Seul le
  dernier test rapide est gardé, dans `tests/` dans le profil (avec les journaux moteur de ses parties qui ont mal
  fini).

### Une grande bibliothèque
<!-- en: a-large-library d118bc02 -->

- **Groupes** : des étiquettes libres séparées par des virgules (`pool A`, `list 40/15`…). La liste peut être filtrée
  par groupe ou par une recherche, et un groupe peut être ajouté à un tournoi en un clic. *Notes* : tout ce qu'il faut
  retenir sur le moteur. *Logo* : une image montrée à côté de son nom dans *Direct* et sur le grand écran quand les
  logos sont activés, et sur la page web de la diffusion (voir [Apparence](#apparence)).
- **Recherche et filtres** : chaque recherche de moteur (la bibliothèque, et les listes de moteurs des autres pages)
  trouve un nom, le nom que le moteur annonce, son auteur, le chemin de son programme ou ses notes ; le filtre d'état
  montre les moteurs qui fonctionnent, en échec ou modifiés, jamais vérifiés, qui ont perdu leur programme, qui
  parlent WinBoard, ou inactifs. Un clic avec Ctrl ou Maj sélectionne plusieurs moteurs : *Vérifier* (l'un après
  l'autre, jamais pendant qu'un tournoi joue), *Ajouter à un groupe…*, *Retirer…*. *Plus…* vérifie ceux qui ne l'ont
  jamais été (*Vérifier ceux qui ne l'ont pas été*) et retire les programmes manquants. Une option texte qui contient
  un fichier ou un dossier (`EvalFile`, `SyzygyPath`…) a un bouton *…*.
- **Inactif** (sur la fiche, ou par le clic droit pour tous les moteurs sélectionnés) : le moteur reste dans la
  bibliothèque, grisé, mais est absent des sélecteurs de moteurs (configuration, suites, test rapide, relance, ajout
  d'un moteur à un tournoi en cours) et de *Vérifier ceux qui ne l'ont pas été*. Un tournoi ou une configuration qui
  l'a déjà le garde.
- **Utilise un GPU** (sur la fiche) : un moteur à réseau qui joue sur une carte graphique, comme Lc0. Pas plus de tels
  moteurs ne jouent à la fois que les *GPU* des **Réglages**, dans un tournoi et entre tournois (voir
  [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois)) ; à partir du prochain tournoi qu'il rejoint.
- **Elo de référence** : le classement du moteur sur une liste à laquelle tu te fies. Un tournoi ou la liste de
  classement peuvent être ancrés dessus, et les parties d'un tournoi le portent dans leurs balises `WhiteElo` et
  `BlackElo` (copié quand le moteur rejoint le tournoi). *Importer…* > *Une liste de classement, pour l'Elo de
  référence…* le règle pour beaucoup de moteurs à la fois (voir
  [Importer des moteurs et des classements](#importer-des-moteurs-et-des-classements)).

### Moteurs populaires
<!-- en: popular-engines bda147a5 -->

*Moteurs populaires…*, à côté d'*Ajouter…*, liste les moteurs les plus forts dont les auteurs publient des
compilations pour Linux, Windows et macOS sur GitHub (Gaia, Stockfish, Triumviratus, Reckless, PlentyChess,
pawnocchio, Coda, Cinder, Stormphrax, Viridithas), chacun dans sa dernière version, avec le fichier que ce processeur
fait tourner (la plus rapide de ses compilations que le processeur prend en charge : AVX2, BMI2, AVX-512, une
compilation faite pour sa génération Zen…), sa taille et sa licence. *Ajouter* le télécharge depuis la version publiée
par son auteur, le vérifie avec l'empreinte SHA-256 que publie GitHub, le sort de son archive, le lance une fois
(`uci`) et l'ajoute à la bibliothèque, chaque version dans un dossier à part dans le dossier `engines` du profil ; un
fichier qui ne démarre pas est remplacé par la compilation suivante. *Annuler* arrête un téléchargement, et l'essai
suivant reprend là où il s'est arrêté. La liste vient d'un catalogue tenu dans le dépôt de RookStudio, relu une fois
par jour, sinon de la copie propre au programme. Ce qui sort de la machine : le catalogue, une requête à
api.github.com par moteur (gardée un jour), et le fichier choisi ; seulement quand la page est ouverte ou qu'un moteur
est ajouté.

Un moteur ajouté ainsi suit ses nouvelles versions : elles sont cherchées une fois par jour à l'ouverture de la page
Moteurs (ou seulement à l'ouverture de *Moteurs populaires…*, dans *Réglages*), et montrées au-dessus de la liste avec
*Mettre à jour*. La nouvelle version va dans un dossier à part et le moteur la prend une fois qu'elle a répondu à
`uci` ; *Garder aussi …* garde la version précédente comme moteur à part, pour un match entre elles. Un tournoi qui
n'est pas terminé garde les fichiers avec lesquels il a commencé ; la version précédente est supprimée une fois
terminés les tournois qui la jouent (voir [Les versions d'un moteur](#les-versions-dun-moteur)).

Là où un moteur est nécessaire pour la première fois (l'analyse de la page Échiquier, ses questions et son bilan) et
que la bibliothèque n'en a aucun, une carte propose Gaia, téléchargé en un clic (ou avec Entrée) et fait moteur de
l'analyse et de l'assistant ; les autres moteurs populaires et un moteur du disque y sont aussi à un clic.

### Les versions d'un moteur
<!-- en: an-engines-versions 8c733556 -->

Chaque version d'un moteur prise dans *Moteurs populaires…* reste dans un dossier à part dans le dossier `engines` du
profil. Une fois qu'une mise à jour a remplacé une version dans la bibliothèque, son dossier est supprimé quand aucun
tournoi non terminé (en cours, en pause ou arrêté) ne la joue : aussitôt quand aucun ne la joue, sinon à la fin du
dernier, car RookStudio regarde de nouveau à la fin de chaque tournoi. Un avis dans le coin de la fenêtre le dit :
*Gaia 4.4.3 supprimé : « Night Gaia » est terminé · 118 Mo libérés*, avec *La fiche du moteur*.

Quand le moteur a sur le disque plus que la version de sa bibliothèque, sa fiche les liste sous *Ses versions sur le
disque*, chacune avec son état :

- celle de la bibliothèque : celle que prennent l'analyse, les nouveaux tournois, le bot et le service pour Lichess ;
- une qu'un tournoi non terminé joue, avec ce tournoi et où il en est (*en cours, partie 212 sur 400*), supprimée à sa
  fin quand une mise à jour l'a remplacée ; *Garder aussi …* la garde pour de bon, comme moteur à part ;
- une gardée exprès (*Garder aussi …*, ici ou dans la mise à jour), jamais supprimée ; *Ne plus la garder* (un second
  clic) retire son moteur de la bibliothèque, et son dossier part dès qu'aucun tournoi non terminé ne la joue ;
- une restée d'avant, jamais remplacée par une mise à jour depuis : gardée jusqu'à *La supprimer* (un second clic) ;
- une supprimée qu'un tournoi non terminé redemande (repris depuis un dossier où RookStudio n'a pas regardé) : *La
  télécharger à nouveau* la reprend depuis sa version publiée sur GitHub, vérifiée avec l'empreinte qu'elle avait, au
  même endroit.

### Démarrage des moteurs
<!-- en: starting-engines 09d107ba -->

- **La lecture des options** ne bloque jamais un lancement. *Reprendre* démarre aussitôt : un tournoi fait jouer les
  moteurs copiés dans son dossier. *Lancer*, la file d'attente et le test rapide n'attendent que la lecture d'un
  moteur qu'ils font jouer, lue en premier ; toute autre lecture ou vérification est interrompue et refaite plus tard.
  Un lancement qui attend le dit sur son bouton (*En attente de …*, avec *Annuler*), et un autre lancement est refusé
  entre-temps. Un lancement attend aussi une suite de tests en cours, et une suite de tests ne démarre pas pendant que
  le test rapide tourne ou qu'un lancement attend. Les lectures, les vérifications et les suites de tests tournent à
  côté des tournois, qui jouent dans leurs propres processus. Un moteur en attente de lecture peut être lu ensuite
  depuis son menu.
- *Relancer le moteur à chaque partie* démarre un nouveau processus pour chaque partie au lieu de le réutiliser.
  Sinon, un tournoi garde un processus en attente par moteur qui a encore des parties à jouer, jusqu'à 32 (plus deux
  par partie en cours), et ferme un moteur après sa dernière partie : dans un KO dès que son match est perdu, dans un
  suisse dès qu'il n'a plus de partie dans la dernière ronde.
- *Délai de démarrage (s)* (60 par défaut) est le temps laissé à un moteur pour répondre `uciok` à son lancement et
  `isready` au début de chaque partie ; un moteur lent à démarrer (un gros réseau à charger) a besoin de plus. Un
  moteur qui ne démarre pas est réessayé trois fois, à 10 s d'intervalle, avec le même délai ; une pause retient les
  essais, sans les consommer. Ensuite, ses parties sont mises de côté et les autres continuent (voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)). Un tournoi créé ensuite copie le délai dans ses
  participants.

### Programmes recompilés
<!-- en: rebuilt-programs 549daa6e -->

- **Programmes déplacés ou recompilés** : la bibliothèque se souvient de chaque programme (taille, date, CRC32). Un
  programme qui n'est plus là est marqué **!** avec *Localiser…*, et un tournoi ne démarre pas avec lui. Un programme
  qui a changé (une nouvelle compilation, ou un autre fichier sous le même chemin) voit ses options relues
  automatiquement.
-  **Ce qu'une nouvelle compilation a changé** : quand la nouvelle lecture diffère de la précédente, le moteur est
  marqué **Δ** et sa fiche liste les différences jusqu'à *Fermer* : son nouveau `id name` (avec *Le renommer* quand le
  moteur portait l'ancien nom) et son auteur, chaque option dont le défaut, les bornes ou les choix ont changé (avant et
  maintenant), les options nouvelles et disparues, et ta valeur pour chacune (la valeur d'une option disparue est
  abandonnée ; celle que la nouvelle compilation n'accepte plus est en orange). Pour une option dont le défaut a changé,
  ou dont la valeur ne convient plus, choisis *Garder ma valeur* (ou *Garder l'ancien défaut* là où tu n'en avais pas :
  il joue comme avant) ou *Prendre le nouveau défaut* ; *Garder mes valeurs* et *Prendre les nouveaux défauts*
  choisissent pour toutes. L'avis disparaît une fois chaque choix fait, à moins qu'il n'ait autre chose à signaler.
  Plusieurs compilations avant qu'il soit fermé font un seul avis, par rapport à la lecture d'avant la première. *En
  échec ou modifié*, dans le filtre d'état, montre aussi ces moteurs, et la palette de commandes a *Ce qui a changé*,
  *Garder mes valeurs*, *Prendre les nouveaux défauts* et *Fermer l'avis des changements* pour chacun.
- **Les programmes des tournois** : chaque tournoi note les programmes avec lesquels il a été lancé. Le résumé de la
  configuration avertit d'un moteur dont le programme a changé depuis son dernier tournoi (le plus récent du dossier
  des tournois qui a fait jouer ce programme), avec *Ce qui a changé* quand sa fiche a un avis. Reprendre un tournoi
  dont un programme a été recompilé ou a disparu depuis le dit dans son en-tête, avec *Continuer* (l'avis se ferme)
  et *Pause* ; le tournoi ne s'arrête pas de lui-même.

### Importer des moteurs et des classements
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importer…** > *Une liste de moteurs…* lit la liste de moteurs d'un autre programme : ChessGUI (`ChsGUI_Eng.TXT`,
avec les options de `ChsGUI_Cfg.ini` à côté), cutechess et Banksia (`engines.json`), Arena (`ArenaENG.cfg`, dans le
dossier d'Arena : le programme, les arguments, le protocole de chaque moteur et, pour les moteurs UCI, les valeurs
d'options réglées dans Arena) et d'autres listes de style INI. Une liste qui ne donne aucun moteur le dit, avec les
formats lus et *Tous les moteurs d'un dossier…* à essayer à la place.

*Tous les moteurs d'un dossier…* prend tous les programmes trouvés dans un dossier, et se souvient du dossier :
*Actualiser : les nouveaux moteurs du dossier* ne propose ensuite que les moteurs qui y ont été ajoutés depuis. Les
moteurs trouvés sont d'abord listés, pour décocher ceux à laisser de côté ; les moteurs déjà dans la bibliothèque
(même programme et mêmes arguments) sont sautés ; les options des autres sont lues ; les moteurs WinBoard sont
importés aussi.

*Importer…* > *Une liste de classement, pour l'Elo de référence…* lit une liste de classement, collée ou ouverte : la
version texte d'une liste de classement ou sa page web (enregistrée ou copiée), la sortie d'Ordo (texte ou CSV), ou un
CSV de lignes `name,elo`. Un nom de la liste correspond au moteur de la bibliothèque qui porte le même nom, ou au seul
dont le nom est le même aux majuscules et aux espaces près (comme pour les joueurs des fichiers PGN importés, voir
[Classements](#classements)). L'aperçu montre l'Elo de référence de chaque moteur, ancien et nouveau, coché pour être
appliqué, et les noms de la liste qui ne sont aucun moteur de la bibliothèque. Les noms d'une liste portent souvent
leur compilation (`64-bit 4CPU`) : renomme les moteurs pour qu'ils correspondent si besoin.

### Moteurs WinBoard
<!-- en: winboard-engines ff0920db -->

**Les moteurs WinBoard** (protocole xboard, CECP) jouent comme les autres. Le *Protocole* d'un moteur est déterminé
quand il est ajouté (*Automatique*), ou choisi : UCI ou WinBoard. fastchess ne parle que l'UCI : un moteur WinBoard
est lancé à travers une traduction intégrée à RookStudio (`rookstudio --wb2uci ENGINE [ARGS]`, que fastchess
lance à la place du moteur), et ses options, `memory`, `cores` et le chemin Syzygy deviennent des options UCI. Elle
pilote le moteur comme le fait WinBoard : fonctionnalités du protocole 2, ou protocole 1 pour les moteurs qui n'en
envoient aucune ; positions par `setboard`, ou par le mode édition ; coups en coordonnées ou en SAN ; `level`,
`time`/`otim`, `st`, `sd`, `nps` ; sortie de réflexion, abandon. Les moteurs marqués ↔ dans la liste sont des moteurs
WinBoard.

- Un moteur du protocole 1 sans `setboard` ni mode édition (TSCP, par exemple) ne peut partir que de la position
  initiale : donne-lui des ouvertures sous forme de coups (un livre PGN), pas de positions (EPD).
- Chess960 pour un moteur qui a la variante `fischerandom` : `variant fischerandom`, positions en X-FEN, roques en
  `O-O`/`O-O-O`, comme WinBoard les envoie.
- *Scores from White's side*, parmi les options du moteur : pour les vieux moteurs qui donnent leurs scores du point
  de vue des Blancs plutôt que de celui du camp au trait, comme le protocole le demande ; leurs scores avec les Noirs
  sont inversés.
- Un moteur qui demande à être relancé à chaque partie (`feature reuse=0`) a *Relancer le moteur à chaque partie*
  coché quand il est ajouté.
- `FCG_WB2UCI_LOG=FILE` dans l'environnement écrit dans `FILE` chaque ligne échangée avec les moteurs WinBoard.

### Un bot Lichess
<!-- en: a-lichess-bot 5ae494c4 -->

La fiche d'un moteur a une ligne **Sur Lichess, en bot** : *En faire un bot Lichess…* prépare le moteur à jouer sur
Lichess sous un compte bot, par la Bot API de Lichess (là où les moteurs sont autorisés), en acceptant les défis que tu
choisis pendant que RookStudio est ouvert. La ligne devient une section de la fiche, qui guide à travers les étapes
ci-dessous.

- **Le compte du bot** est un second compte Lichess, distinct du tien : nouveau, avec un nom à lui, et sans aucune
  partie jouée. RookStudio ne crée pas de comptes : *Créer le compte sur lichess.org* ouvre la page de Lichess.
  *Associer le compte du bot…* ouvre la page de Lichess qui demande une permission, *jouer en tant que bot*
  (`bot:play`) : connecte-toi là avec le compte du bot, pas le tien (une fenêtre privée du navigateur évite de te
  déconnecter). Ou colle un jeton du compte du bot créé sur lichess.org (*Créer un jeton sur lichess.org* ouvre la page
  avec cette permission cochée) et *Le vérifier*. Ton propre compte reste associé à part, pour le reste de RookStudio ;
  le bot a son propre jeton, gardé dans le `lichess-bot.json` du profil, lisible par toi seul.
- **Les vérifications**, avant que rien qui modifie le compte ne soit demandé : son nom et sa date de création, les
  parties qu'il a jouées (aucune), s'il est déjà un bot (il sert alors tel quel), et qu'il n'est pas ton propre compte
  associé. Ton propre compte n'est jamais gardé (le jeton que RookStudio a obtenu pour lui est aussitôt révoqué) ; un
  compte fermé, ou un jeton sans la permission, non plus. Un compte qui a joué est refusé, comme Lichess le ferait :
  crée un nouveau compte, n'y joue rien, puis associe-le (*Associer un autre compte…*, *Dissocier celui-ci*).
- **En faire un bot…** est irréversible : le compte ne joue plus jamais comme une personne, Lichess le marque BOT, et il
  ne joue que des défis (pas de salon, pas de tournois sauf les arènes ouvertes aux bots). L'étape le dit et demande le
  nom du compte : *En faire un bot* s'allume dès que le nom tapé est le sien (Échap annule). Ce qui quitte la machine :
  cette demande, avec le jeton du bot. Un refus est dit avec les mots de Lichess ; sans réponse, rien n'a changé :
  réessaie.
- **Les défis qu'il accepte**, sur la fiche : les cadences (bullet, blitz, rapide, classique, correspondance ; jamais
  l'UltraBullet, que Lichess ne laisse pas jouer aux bots), les parties classées ou seulement amicales, les personnes et
  les bots, une fourchette de classement pour l'auteur du défi (0 : sans limite), le Chess960 quand le moteur le connaît
  (`UCI_Chess960`), une partie à la fois. Au départ : amicales seulement, blitz et rapide, puisque Lichess demande aux
  bots de jouer en amical pendant qu'on les essaie ; élargis-les ensuite. Un défi classé venant de ton propre compte est
  refusé (Lichess interdit les parties classées entre les comptes d'une même personne) ; un défi amical est joué. Les
  autres défis sont refusés poliment, avec la raison que Lichess montre à l'auteur du défi : *trop rapide*, *trop lent*,
  *pas cette cadence*, *amicales seulement*, *échecs standard seulement*, *pas cette variante*, *pas de bots*, *bots
  seulement*, ou *plus tard* pendant qu'il joue, qu'il passe hors ligne, ou qu'un tournoi a besoin des cœurs.
- **Son jeu** : le moteur avec ses options de la bibliothèque (ses `Threads` et son `Hash` comptés dans le budget de
  cœurs de la machine pendant qu'il joue), un livre Polyglot de ta bibliothèque joué tant qu'il a un coup, les tables des
  tournois (`SyzygyPath`, pour un moteur qui a l'option et pas de chemin à lui), et pas de réflexion pendant le temps de
  l'adversaire. Sa pendule vient de Lichess à chaque coup, moins une marge pour le réseau (un dixième de son temps
  restant, de 0,2 à 1 seconde) ; avant que les deux camps aient joué, il joue dans le temps que Lichess donne pour le
  premier coup ; par correspondance, il réfléchit 15 secondes par coup. Il accepte une nulle proposée quand le moteur est nettement moins
  bien, ou pas mieux tard dans la partie, refuse une reprise de coup, et revendique la victoire quand son adversaire est
  parti assez longtemps. Ses mots, dits dans la discussion des joueurs au début et à la fin, sont à toi de choisir
  (vides : rien n'est dit). Un moteur qui ne peut pas démarrer ou qui s'arrête abandonne la partie (l'annule avant que
  les deux camps aient joué), et la fiche dit pourquoi.
- **Mettre en ligne** (Entrée) ouvre le flux d'événements propre au bot (Lichess en garde un par jeton) : les défis
  arrivent, auxquels ses règles répondent aussitôt. En ligne, la fiche montre la journée du bot (parties gagnées, nulles
  et perdues, défis refusés), sa partie sur un petit échiquier avec l'évaluation du moteur, *Sur l'Échiquier*, ses
  dernières parties et les défis refusés, *Sa page sur lichess.org* et **Mettre hors ligne** : plus de nouveau défi, la
  partie en cours se joue jusqu'à sa fin, puis le flux se ferme. Une partie par correspondance attend sur Lichess pendant
  que le bot est hors ligne ; elle revient quand le bot est de nouveau en ligne. Le bot ne joue que pendant que
  RookStudio est ouvert : fermer RookStudio pendant une de ses parties en direct demande d'abord (*Rester* · Échap, *Le
  mettre hors ligne*, *Fermer quand même*), puisque la partie serait perdue au temps.
- **Sa partie sur l'Échiquier** : suivie coup par coup, le camp du bot en bas, les classements et les pendules des
  joueurs dans leurs bandeaux, et au-dessus de la colonne sa carte : ce qu'est la partie, à qui c'est de jouer, la
  recherche du moteur (son évaluation, sa profondeur et la ligne qu'il attend : la sienne, puisqu'il joue la partie),
  *Ouvrir sur lichess.org*, *Abandonner pour lui…* (un second clic) et *Mettre hors ligne*. Les coups sur l'échiquier
  sont ceux du bot seul. À sa fin, la partie arrive dans la base *Lichess · son nom*, créée si elle manque.
- **Où il s'affiche** : en ligne, le bot s'affiche là où s'affiche ce qui tourne : sur la page Accueil sous *En cours*,
  et sur la page Tournois, avec sa partie, *Sur l'Échiquier*, *Sa fiche* et *Mettre hors ligne*.
- **Un tournoi d'abord** : un défi est refusé *plus tard* tant qu'un tournoi qui joue sur cet ordinateur a besoin des
  cœurs que prendrait le moteur du bot. Une partie du bot déjà en cours quand un tournoi commence va jusqu'à sa fin, les
  threads de son moteur comptés dans le budget de cœurs comme le sont ceux des moteurs de la page Échiquier ; aucun
  nouveau défi n'est pris pendant ce temps.
- **Fair-play** : les parties du bot ne sont pas les tiennes, et elles ne verrouillent pas tes aides. Pendant ta propre
  partie en direct sur Lichess, la recherche du bot n'est montrée ni sur sa carte ni sur l'Échiquier (la carte dit
  pourquoi) ; ses parties continuent.
- **Dissocier le bot…** (un second clic) révoque son jeton chez Lichess et le supprime ici ; le compte reste un bot.
- Ce qui quitte la machine : les coups du moteur, ses mots dans la discussion, ses réponses aux défis, avec le jeton du
  bot ; rien de ton compte. Son profil (nom, description) se règle sur lichess.org. Ses choix et ses dernières parties
  sont gardés dans le `bot/bot.json` du profil.

La palette de commandes a *Mettre le bot Lichess en ligne*, *Mettre le bot Lichess hors ligne*, *La partie du bot
Lichess sur l'Échiquier* et *La fiche du bot Lichess*, et sur la page Moteurs *Faire de … un bot Lichess…*.

### Ton moteur sur lichess.org
<!-- en: your-engine-on-lichessorg 6bee0124 -->

La fiche d'un moteur a une ligne **Sur lichess.org, pour l'analyse** : *Proposer à Lichess…* propose le moteur à
l'échiquier d'analyse de lichess.org, par l'API des moteurs externes de Lichess. Il calcule ici, sur ta machine, et ses
lignes s'affichent là-bas, pour ton compte seulement, dans le menu des moteurs de l'échiquier d'analyse, tant que
RookStudio est ouvert. L'API de Lichess pour cela est encore en alpha.

- **Ce sous quoi il est proposé** : son nom chez Lichess (3 à 200 caractères ; *Gaia 4.4.4 (RookStudio)* par défaut),
  la machine (cet ordinateur, ou une machine distante de Réglages > Machines où le moteur a son chemin, par SSH, avec
  ses propres threads), les threads au plus (Lichess en demande de 1 à ce nombre pour chaque requête) et la mémoire au
  plus. Lichess fait tourner un tel moteur en mode Chess960 (`UCI_Chess960`) pour toutes les parties : un moteur qui ne
  l'a pas ne peut pas être proposé, et sa ligne le dit à la place de *Proposer à Lichess…*.
- **La première fois**, Lichess demande deux permissions de plus, `engine:read` et `engine:write` (pour enregistrer un
  moteur pour ton compte, le lire, le retirer) : *Autoriser sur Lichess…* (Entrée) ouvre la page de Lichess ; ton
  compte reste associé, et le jeton qu'elle remplace est révoqué. Sans compte associé, la fiche le dit, avec *Associer
  le compte…*.
- **Ensuite, sans rien demander de plus**, RookStudio enregistre le moteur sur ton compte (une requête, avec ton jeton)
  et un secret tiré ici, que Lichess ne montre plus jamais ; RookStudio le garde dans le `lichess-engines.json` du
  profil, lisible par toi seul, et ne le montre ni ne le journalise jamais. Il attend ensuite du travail sur
  engine.lichess.ovh avec ce seul secret, sans ton jeton : une longue attente, renouvelée tant que le service tourne.
  Quand ton échiquier d'analyse le demande, une requête arrive avec la position (une position de départ et des coups),
  les threads, la mémoire, 1 à 5 lignes et un temps, une profondeur ou des nœuds : le moteur est réglé (le mode
  Chess960, `UCI_AnalyseMode` quand il l'a, ses options de la bibliothèque), et ses lignes (profondeur, multipv, score,
  nœuds, temps, pv) partent vers Lichess à mesure qu'il les trouve, puis son meilleur coup. Quand tu passes à autre
  chose, Lichess ferme la requête, et le moteur s'arrête.
- **En service**, la fiche montre la requête en cours (ses lignes, threads, mémoire, profondeur et vitesse), les
  requêtes et les minutes d'analyse du jour, la position demandée avec le meilleur coup du moteur, *Ouvrir l'échiquier
  d'analyse de Lichess* et **Arrêter le service**. La page Lichess liste les moteurs proposés (*Tes moteurs sur
  lichess.org*) ; la page Accueil (sous *En cours*) et la page Tournois montrent ceux en service, et la barre des cœurs
  leurs threads.
- **Le tournoi d'abord** : le service ne prend que les threads que laissent libres les tournois qui jouent sur sa
  machine (les cœurs des Réglages), la partie du bot Lichess passant d'abord aussi (voir [Un bot
  Lichess](#un-bot-lichess)) ; s'il n'en reste aucun, il se met en pause et le dit, les requêtes de Lichess attendant.
  Une requête en cours quand un tournoi a besoin de ses threads s'arrête là.
- **Fair-play** : pendant ta propre partie en direct sur Lichess, le service se met en pause jusqu'à la fin de la
  partie (les requêtes restent sans réponse), puis reprend de lui-même. Les positions de tes parties par
  correspondance ne sont jamais analysées : une requête sur l'une d'elles, sur une ligne qui y passe ou un coup avant
  elle (comparée par les pièces et le camp au trait, comme le fait le verrou du [Fair-play](#fair-play)) reçoit une
  réponse vide. Lichess lui-même ne propose un moteur externe que sur ses pages d'analyse, jamais pendant une partie.
- **Arrêter le service** : RookStudio n'attend plus de travail ; le moteur reste enregistré chez Lichess, sans
  répondre ; *Reprendre* (Entrée) le remet en service. *Changer…* l'enregistre à nouveau avec un autre nom, une autre
  machine ou d'autres limites. **Retirer de Lichess** supprime l'enregistrement (une requête), et son secret n'est plus
  utilisé ; *Proposer à Lichess…* le refait. RookStudio fermé, le moteur reste dans la liste de Lichess sans répondre ;
  au prochain démarrage, le service reprend s'il était en service à la fermeture.
- **Quand quelque chose ne va pas** : Lichess qui refuse le secret (401 : l'enregistrement a été retiré sur
  lichess.org, ou le secret a changé) arrête le service, avec *Enregistrer à nouveau (un nouveau secret)* ; une réponse
  de Lichess que RookStudio ne lit pas (l'API est en alpha) l'arrête plutôt que de répondre faux, et le dit, avec *Voir
  si une version de RookStudio la suit* ; hors ligne, le service attend le réseau et reprend de lui-même (après trop
  de requêtes, la minute de Lichess).
- Ce qui quitte la machine : à l'enregistrement, le nom, les limites et le secret, avec ton jeton ; puis les lignes du
  moteur pour les positions qu'envoie Lichess. Ces positions viennent de ton propre navigateur, via Lichess, qui voit
  les lignes. Rien d'autre de ta machine.

La palette de commandes a *Proposer … à lichess.org…* sur la page Moteurs, *Arrêter le service de … sur lichess.org*
et *Reprendre le service de … sur lichess.org*.

## Livres
<!-- en: books 4cf13005 -->

**Ajouter…** enregistre un livre d'ouvertures (EPD, PGN ou Polyglot `.bin`) sous un nom, avec une description ; pour
un livre PGN, le nombre de demi-coups gardés, pour un livre Polyglot, la profondeur jusqu'à laquelle ses lignes sont
suivies. La section *Ouvertures* de la configuration propose ensuite les livres de la bibliothèque par leur nom, avec
leur nombre d'ouvertures (tout autre fichier peut encore être choisi). *Retirer* sort un livre de la bibliothèque et
laisse le fichier tranquille.

### Vérifier un livre
<!-- en: checking-a-book 96eee229 -->

Le livre choisi est lu en arrière-plan, avec les lecteurs de fastchess lui-même (ses ouvertures sont donc exactement
celles qu'un tournoi joue), et vérifié :

- **positions illégales** : pas exactement un roi de chaque couleur, des pions sur la première ou la dernière rangée,
  le camp qui n'a pas le trait en échec ;
- **doublons** : la même position qu'une ouverture précédente, y compris par transposition ;
- **couleurs inversées** : une ouverture précédente avec les couleurs échangées (un livre fait pour être joué deux
  fois, couleurs inversées, n'a pas besoin des deux) ;
- **parties déjà finies** : aucun coup légal, ou pas assez de matériel pour mater.

fastchess écarte d'un tournoi les ouvertures dont la position ne peut pas être jouée (une ligne FEN ou EPD sans roi
ou mal formée d'une autre façon, un roi qui pourrait être pris, une partie PGN dont la balise FEN est invalide), avec
un avertissement par livre ; elles sont comptées ici comme des positions illégales. Une partie PGN dont les coups ne
se lisent plus à partir d'un point (un coup mal formé ou ambigu) garde les coups d'avant. Les lignes d'un livre PGN
qui commencent par `%` (lignes d'échappement, dont la norme PGN fait des commentaires ; les livres AVT en ont) sont
ignorées, par les tournois aussi, et la page Livres les compte (*lignes commençant par %, ignorées*).

Ses ouvertures sont listées en dessous, toutes ou seulement celles qui ont un problème, avec une recherche par
morceau de FEN ou par coups (un livre PGN montre ses coups en SAN) ; un clic montre la position sur un échiquier.
*L'utiliser pour le prochain tournoi* le met dans la configuration (avec Chess960 pour un livre Chess960).
*Enregistrer une copie nettoyée…* écrit un livre EPD sans les positions illégales, les doublons et les parties finies
(et, si c'est coché, les positions aux couleurs inversées), et l'ajoute à la bibliothèque. *Lancer comme suite de
tests…* ouvre un livre EPD dans **Suites**.

### Créer des livres
<!-- en: creating-books b9c072ab -->

**Créer…** écrit des livres pour la bibliothèque (aussi dans la palette de commandes) :

- *Générer un livre Chess324* : le Chess324 de Larry Kaufman. Les rois et les tours restent sur leurs cases
  habituelles, le roque est donc le roque habituel ; la dame, les fous et les cavaliers de chaque camp sont mélangés
  sur les autres cases de sa première rangée (les colonnes b, c, d, f et g), les fous sur des cases de couleurs
  opposées. Cela fait 18 dispositions par camp, celle des Blancs et celle des Noirs choisies indépendamment : *Les 324
  positions…*, ou *Seulement les 18 symétriques…*, où les pièces noires sont sur les mêmes colonnes que les blanches.
  Ce sont des positions ordinaires, écrites dans un fichier EPD (proposé à côté des livres de la bibliothèque, ou dans
  `books/` dans le profil) et jouées par n'importe quel moteur, sans Chess960.
- *Construire un livre Polyglot…* : un livre Polyglot (`.bin`) à partir des parties d'un fichier PGN, comme le fait
  `make-book` de Polyglot, avec la conversion qui transforme les livres PGN des participants en livres Polyglot. Son
  formulaire, à la place des détails du livre, prend le fichier PGN (le livre PGN choisi dans la liste, s'il y en a
  un), la **profondeur** (les demi-coups de chaque partie qui entrent dans le livre), le **nombre minimum de parties**
  qu'un coup doit avoir dans sa position pour être gardé (le `-min-game` de Polyglot ; les autres sont écartés comme
  trop rarement joués), les **poids**, *au score* (les points du camp qui a joué le coup : une victoire 2, une nulle 1,
  une défaite 0, comme les compte Polyglot ; les parties sans résultat sont écartées, de même qu'un coup dont toutes
  les parties ont été perdues) ou *au nombre* (le nombre de parties qui le jouent), et, si c'est coché, un
  **classement** : seuls comptent les coups d'un joueur classé au moins à ce niveau, d'après la balise `WhiteElo` ou
  `BlackElo` de la partie (un joueur qui n'en a pas est écarté). *Construire…* demande où écrire le livre (à côté du
  fichier PGN par défaut), puis lit les parties en arrière-plan, avec sa progression et *Annuler*, à côté du
  formulaire et au bas de la liste ; le livre, trié par clé comme le format le veut, est ensuite ajouté à la
  bibliothèque, ses options dans sa description. La console dit combien de parties y sont entrées, combien les options
  en ont écartées et combien n'ont pas pu être lues.

### Livres à partir des ouvertures nommées
<!-- en: books-from-the-named-openings df5656f9 -->

*À partir des ouvertures nommées de Lichess…*, dans **Créer…**, fait un livre de positions de départ à partir de la
table des ouvertures nommées de Lichess, déjà dans le programme (chess-openings, CC0) : rien n'est téléchargé. Son
formulaire, à la place des détails du livre, prend les **familles** (A débuts de flanc, B parties semi-ouvertes,
C parties ouvertes, D parties fermées, E défenses indiennes), les **longueurs** des lignes gardées, en demi-coups (de 8
à 16 par défaut), *Une position par variante nommée* (plusieurs lignes partagent un nom), et le **nombre** de
positions, réparties également sur les codes ECO quand davantage conviennent. Il dit combien de positions et
d'ouvertures le livre contiendrait ; *Créer le livre…* demande où écrire le fichier EPD, chaque position avec son
numéro de coup (`hmvc`, `fmvn`) et son ouverture en `c0`, et l'ajoute à la bibliothèque.

### Livres à partir des positions évaluées
<!-- en: books-from-the-evaluated-positions 82dd002c -->

*À partir des positions évaluées de Lichess…*, dans **Créer…**, fait un livre des positions que les joueurs de Lichess
avaient analysées : celles des lignes des ouvertures nommées et à un coup de celles-ci, chacune avec son évaluation et
sa profondeur. Elles viennent de la base d'évaluations de Lichess (CC0, 22,5 Go et 418 millions de positions) :
sa taille est demandée à database.lichess.org et annoncée avant tout téléchargement ; *Télécharger et lire* la lit à
mesure qu'elle arrive, et ne garde que les positions trouvées (quelques mégaoctets, dans le profil), pas le fichier.
*Lire un fichier déjà téléchargé…* lit à la place un `lichess_db_eval.jsonl.zst` du disque. Rien d'autre que la
demande du fichier ne quitte la machine. La lecture montre ce qui est lu et ce qui est gardé ; *Annuler*, une coupure du réseau ou
la fermeture du programme garde ce qui a été lu, et *Continuer* reprend de là (un fichier changé sur le serveur
entre-temps est relu depuis son début).

Une fois la lecture faite, le formulaire prend l'**évaluation** gardée, pour les Blancs (de +0.30 à +0.80 par défaut),
la **profondeur** minimale (30), les **demi-coups** depuis le départ (de 8 à 16), *Une position par variante nommée*
(celle qui est sur la ligne de la variante d'abord, puis l'évaluation la plus profonde, puis la plus proche du milieu
de la fenêtre), et le **nombre** de positions, réparties également sur les codes ECO quand davantage conviennent. Il
dit combien de positions et d'ouvertures le livre contiendrait et leur évaluation moyenne ; *Créer le livre…* écrit le
fichier EPD, chaque position avec son numéro de coup, son évaluation pour le camp au trait et sa profondeur (`ce`,
`acd`) et son ouverture (`c0`), et l'ajoute à la bibliothèque. *Relire* relit la base pour ses évaluations plus
récentes.

### Livres CTG
<!-- en: ctg-books a908f007 -->

**Ajouter…** prend aussi un livre au format CTG, les livres d'ouvertures de ChessBase et de Fritz : un `.ctg` avec ses
`.ctb` et `.cto` à côté, n'importe lequel des trois étant choisi. Il est seulement lu, jamais écrit. Sa page dit
combien de positions et de coups il contient, et combien de coups son auteur a marqués en vert (à jouer) et en rouge
(à éviter). Pour chaque position, le livre a ses coups, chacun avec son symbole (!, ?, !?, seul coup…), sa couleur,
l'évaluation de la position à laquelle il mène, et les parties derrière lui : combien, comment elles se sont
terminées, les classements des joueurs.

*L'ouvrir dans l'arbre* le montre dans l'onglet *Arbre* de la page Échiquier, parmi les sources : les coups de la
position affichée avec leurs parties, leur score, le classement moyen des joueurs qui les ont joués et leur
performance, et une colonne *Livre* avec leur couleur et leur symbole. Les coups verts viennent en premier ; les rouges
restent dans la liste, grisés. Les années et les parties de référence viennent des bases, pas du livre.

fastchess ne lit aucun livre CTG. *Pour les moteurs : créer un livre Polyglot…* en écrit une copie Polyglot (`.bin`)
et l'ajoute à la bibliothèque, pour les *Ouvertures* de la configuration et les livres propres des participants : ses
coups rouges sont écartés, et chaque coup pèse les points marqués par ses parties (deux pour une victoire, un pour une
nulle), le double pour un coup vert ; un coup qui n'a rien marqué est écarté. *Vérifier le livre* suit ses lignes
depuis la position initiale et dit combien de positions il a atteintes parmi celles que contient le livre, et si un
coup n'a pas pu être lu. Rien ne quitte la machine.

## Échiquier
<!-- en: board 0bbd00c9 -->

La page Échiquier montre une partie ou une position, venue de n'importe où : **A** sur n'importe quel échiquier du
programme (une partie de la mosaïque de l'onglet *Direct* ou celle suivie, l'échiquier de l'onglet *Parties*, de
l'onglet *Ouvertures*, d'une ouverture d'un livre) ouvre sa position ici et l'analyse ; sur la page Échiquier, A lance
et arrête l'analyse. *Revenir au tournoi* (ou *Retour*) ramène à la page d'où l'on vient. Son échiquier peut être
dessiné en 3D : voir [L'échiquier 3D](#léchiquier-3d).

L'échiquier est à gauche, avec les joueurs au-dessus et en dessous ; à droite, de haut en bas, l'analyse, les coups
(avec, en onglets, le tableau des coups, le bilan de la partie et les parties du fichier), et le graphe de la partie.
Le sous-titre de la page dit ce qu'est la partie : son événement et sa ronde, son résultat, sa cadence, son code ECO
et son ouverture, et comment elle s'est terminée.

Un coup se joue sur l'échiquier à la souris : un clic sur la pièce puis un sur sa case, ou la pièce glissée jusque-là.
Pendant qu'elle est glissée, elle suit le pointeur et la case où elle arriverait est teintée ; lâchée ailleurs, elle
revient, toujours prise, pour être jouée par un clic. Sur l'échiquier 3D, un fantôme translucide de la pièce suit le
pointeur, flottant au-dessus de l'échiquier et passant derrière les pièces devant lui, l'image restant telle quelle ; la
pièce elle-même bouge quand elle est lâchée.

### Ouvrir, coller et mettre en place une position
<!-- en: opening-pasting-and-setting-up-a-position b8dc5047 -->

- **Ouvrir un PGN…** lit n'importe quel fichier PGN (un fichier peut aussi être déposé sur la fenêtre), en
  arrière-plan avec une barre de progression : un fichier de millions de parties prend quelques secondes, et ses
  parties sont listées dans un onglet à côté des coups, filtrées par ce qui est tapé (chaque mot trouvé dans les
  joueurs, l'événement, le lieu, la date, la ronde, l'ouverture, le code ECO ou le résultat). Un clic montre une
  partie. Les évaluations de ses coups dessinent le graphe : les commentaires de cutechess et de fastchess
  (`{+0.35/18 1.2s}`) et le `[%eval 0.35]` de lichess et de ChessBase (du point de vue des Blancs, `#-3` pour un mat)
  sont lus, les pendules de `[%clk 0:01:23]` aussi. Ses variantes sont gardées (voir [Variantes](#variantes)). Une
  partie dont la balise FEN ne peut pas être jouée est refusée, et les coups d'une partie s'arrêtent au premier qui ne
  peut pas être lu.
- **Coller** (Ctrl+V sur la page) lit le presse-papiers : un FEN, ou les quatre champs d'une ligne EPD ; un PGN d'une
  partie ou plus (listées comme celles d'un fichier) ; une liste de coups depuis la position initiale, en SAN avec ou
  sans numéros (`1. e4 e5 2. Nf3`) ou en UCI (`e2e4 e7e5`). Une position qui ne peut pas être jouée est refusée avec
  la raison (un roi manquant, un pion sur la première rangée, le camp qui n'a pas le trait en échec). Des droits de
  roque dont le roi ou la tour n'est pas sur sa case habituelle, ou écrits sous forme de colonnes (`HAha`), en font
  une position Chess960. Un fichier `.fen` ou `.epd` peut aussi être déposé.
- **Éditer la position** met en place une position sur l'échiquier : un clic pose la pièce choisie (un second clic
  l'enlève), un clic droit l'enlève ; le trait, le numéro du coup, les droits de roque (proposés seulement quand le
  roi et la tour sont là où ils doivent être), la case de prise en passant (seulement derrière un pion qui vient
  d'avancer de deux cases) et Chess960. Le FEN en dessous suit l'échiquier, et un FEN tapé là le règle. *Terminé* est
  proposé dès que la position peut être jouée, et la met sur l'échiquier.
- *Copier le PGN* (à droite des onglets des coups) copie la partie affichée, avec ses balises, ses coups (les scores
  des moteurs en commentaires quand elle en a) et ses variantes. Ctrl+S, ou *Enregistrer la partie en PGN…* dans la
  palette de commandes, écrit la même chose dans un fichier. Les commentaires, les symboles ($1 ou !?), les flèches et
  les cases colorées, les pendules et les évaluations d'un fichier PGN ouvert sont gardés, et réécrits tels qu'ils ont
  été lus.
- **L'échiquier** : les boutons en dessous et ← → Origine Fin parcourent les coups, et F (ou le dernier bouton) le
  retourne ; un clic sur une pièce puis sur une case joue un coup (un pion qui est promu demande sa pièce). *Copier le
  FEN* copie la position.

### Les joueurs
<!-- en: the-players 1f4f551f -->

Au-dessus et en dessous de l'échiquier, quand la partie a des joueurs : le nom de chacun, l'évaluation de son dernier
coup par son moteur (le score des Blancs et la profondeur, en pastille), le coup qu'il a joué et le temps qu'il y a
mis, ou *au trait*, et à droite sa pendule après son dernier coup, encadrée tant qu'il a le trait. Les pendules sont
celles des `[%clk]` des coups quand la partie en a ; sinon elles sont reconstituées à partir de sa balise
`TimeControl` (`300+2`, `10+0.1` : secondes et incrément) et du temps de chaque coup, les coups du livre n'en prenant
aucun.

### Analyse
<!-- en: the-analysis 1e6120de -->

Un moteur de la bibliothèque analyse la position affichée et suit l'échiquier. La carte montre son moteur, puis un
bouton avec ses *Threads*, son *Hash* (0 : celui du moteur) et ses *Lignes* (MultiPV), qu'un clic ouvre : les
réglages propres à l'analyse, gardés pour la prochaine fois, qui ne changent ni le moteur de la bibliothèque ni aucun
tournoi (les autres valeurs d'options de la bibliothèque sont envoyées comme une partie les envoie). À droite, les
threads qu'utilise la page Échiquier sur ceux de la machine, dans la couleur d'avertissement pendant qu'un tournoi
tourne (son infobulle dit combien de threads prennent les parties du tournoi : l'analyse est en concurrence avec
elles).

La recherche s'affiche en tuiles (profondeur et seldepth, nœuds, vitesse, temps, accès aux tables, remplissage du
hash), les chances de gain, de nulle et de perte quand le moteur les donne (`UCI_ShowWDL`), et chaque ligne : son
score (celui des Blancs, le meilleur mis à part), sa profondeur et ses coups sur deux lignes de texte, en figurines
(voir [Apparence](#apparence)). Survoler un coup d'une ligne montre la position après lui sur l'échiquier, encadré ;
un clic joue la ligne jusqu'à ce coup. Le premier coup de chaque ligne est dessiné en flèche, la meilleure plus foncée
(**Réglages** > *Apparence* > *Les flèches de l'analyse sur la page Échiquier* les retire ; les lignes montrent
toujours les coups).

**Menace** (X) fait analyser la position, le trait passé, par un autre processus du moteur : le meilleur coup de
l'adversaire, s'il jouait de nouveau, dessiné en flèche rouge et écrit sous les lignes (aucun quand le camp au trait
est en échec).

L'analyse attend pendant qu'une autre page est affichée, et reprend quand la page Échiquier revient ; *Arrêter
l'analyse* (ou A) l'arrête, et un point sur le bouton *Échiquier* de la barre de navigation indique qu'elle tourne.
Son moteur ne fait pas partie des processus du tournoi : la fin d'un tournoi ne l'arrête pas. Une fois l'analyse
arrêtée, son moteur reste démarré pour la recherche suivante, avec sa table de hachage (et la mémoire GPU d'un moteur à
réseau) : le bouton d'alimentation à côté du moteur, *Décharger le moteur* (aussi dans la palette de commandes),
l'arrête, ainsi que celui de la menace.

### Analyser une partie entière
<!-- en: analysing-a-whole-game a7e6166c -->

**Analyser la partie** (G, dans les outils de la page ou dans l'en-tête du graphe) fait analyser chaque position de
la partie par le moteur d'analyse (ses threads et son hash, une ligne), en arrière-plan, d'abord 20 ms chacune, puis
de nouveau quatre fois plus longtemps à chaque passe (80 ms, 0,3 s, 1,3 s, 5 s…), jusqu'au temps choisi avec la
flèche à côté du bouton (0,3 s, 1,3 s, 5 s, 20 s par position, ou *jusqu'à l'arrêt*) : le graphe vient en une seconde
ou deux et s'affine à chaque passe. Les positions vont de la dernière à la première, ce qui laisse le hash du moteur
garder ce qui vient après une position (mesuré : environ 16 % plus près des longues recherches que de la première à
la dernière, pour le même temps), la position de l'échiquier en premier. L'en-tête du graphe indique la passe, le
temps par position et la progression, et un mince liseré sous sa bande des temps ce que chaque position a reçu ;
*Arrêter* (ou G) l'arrête.

Chaque coup est jugé sur les chances de gain que son auteur a perdues, d'après les scores de l'analyse avant et après
lui (la mesure de lichess : 50 % à 0.00, environ 59 % à +1.00) : 10 points perdus font une imprécision (`?!`), 20 une
erreur (`?`), 30 une gaffe (`??`), marquées après le coup, sur le graphe et sur l'échiquier ; le coup que le moteur
aurait joué n'est jamais marqué. Un coup est jugé sur la dernière passe complète ; une marque que la dernière passe a
changée reste creuse jusqu'à ce que la suivante la confirme. N et Maj+N (ou les boutons à côté de la navigation) vont
au coup marqué suivant et au précédent.

Chaque passe complète est gardée avec la partie (dans `analyses/` dans le profil) : la même partie, rouverte depuis
n'importe où, la montre aussitôt, et G reprend à partir des passes terminées. L'analyse tourne sur son propre thread
avec son propre processus de moteur, ses threads comptés dans le budget de cœurs ; l'analyse de l'échiquier (A) la met
en pause, et quand A analyse une position de la partie plus longtemps que la passe ne l'a fait, sa valeur prend la
place de celle de cette position.

**Plusieurs analyses** : *+ Moteur*, dans la légende du graphe, fait analyser la partie par un autre moteur de la
bibliothèque en plus, aussi longtemps à chaque passe. Chaque analyse a sa couleur sur le graphe et sur l'échiquier
(son meilleur coup en flèche quand l'analyse de l'échiquier est arrêtée), sa ligne dans *Ce coup* et ses précisions
dans le bilan. Elles avancent ensemble passe par passe, toutes à la fois quand les cœurs le permettent, l'une après
l'autre sinon. Une analyse juge les coups (la première ; un clic droit sur une autre dans la légende en fait le juge,
ou la retire) ; une marque est creuse quand les analyses ne jugent pas toutes le coup de la même façon. Les chances de
gain comparent mieux les moteurs que les centipions, qui diffèrent d'un moteur à l'autre : elles prennent le WDL de
chaque moteur quand il en donne un.

### Le graphe de la partie
<!-- en: the-game-graph 14e02bad -->

Sous les coups, le graphe de la partie, le même que dans *Direct* et *Parties* :

- **Évaluation** : l'analyse qui juge, en aire blanche et noire (l'avantage des Blancs au-dessus de la ligne, celui
  des Noirs en dessous), chaque moteur qui a joué en courbe de sa couleur à ses propres coups, les autres analyses en
  courbes pointillées ; les coups marqués sur la courbe ; les valeurs au-delà de ±6 dessinées le long du bord dans la
  couleur d'avertissement. Sans analyse, l'aire est la moyenne des évaluations des deux moteurs.
- Les coups du livre hachurés, les phases sous l'axe (l'ouverture ; le milieu de partie à partir de la première
  position avec au plus 10 pièces en dehors des rois et des pions, ou moins de 4 sur une rangée du fond ; la finale à
  partir de 6 au plus, comme lichess découpe une partie), la zone que connaissent les tables teintée, les numéros des
  coups en bas.
- Sous l'axe, le temps de chaque coup : celui des Blancs vers le haut, celui des Noirs vers le bas.

Ses onglets : *Chances de gain* (d'après le WDL quand les moteurs le donnent, la formule de lichess sinon), *G/N/P*
(les parts que les moteurs ont données, les victoires des Blancs en bas), *Temps* (avec le temps de décision),
*Profondeur* (avec la seldepth), *Nœuds*, *Vitesse*, *Accès aux tables*, *Accord* (l'écart entre les évaluations des
deux joueurs, chaque coup contre le précédent, et celui des analyses à chaque position) et *Accord des PV* (combien de
demi-coups de la PV d'un moteur reprennent ce que celle de l'autre avait prévu) ; ceux pour lesquels la partie n'a pas
de valeurs sont omis, et ceux qui ne tiennent pas vont dans le menu `⋯`. Le survol indique le coup et ce que chaque
moteur a dit ; un clic ou un glissement va au coup. Un clic sur une courbe dans la légende la masque ou l'affiche ; la
flèche à droite replie le graphe.

### Ce coup
<!-- en: this-move a4a908f8 -->

Sous les coups, à propos du coup affiché : ce qu'a dit le moteur qui l'a joué (son score, sa profondeur et sa
seldepth, son temps, ses nœuds, sa vitesse, ses accès aux tables, tels que le PGN les a), la ligne qu'il attendait
après lui, le verdict de chaque analyse (la part des chances de gain perdue, le jugement, le meilleur coup et son
score), et avec les tables de finales le résultat après le coup et si le coup a gardé celui d'avant. *Afficher* pose
une ligne sur l'échiquier sans toucher à la partie : la position après elle, l'échiquier encadré, et par-dessus ce
qu'est cette ligne ; Échap, un clic sur l'échiquier ou un déplacement dans la partie ramène la partie. Sur
l'échiquier, le jugement du coup est un badge sur sa case, et la réponse que son moteur attendait une flèche en
pointillés.

### Variantes
<!-- en: variations f4e65c3b -->

Un coup joué sur l'échiquier là où la partie a pris un autre chemin commence une variante : la partie reste telle
qu'elle était, avec ses analyses et son graphe. Les coups montrent les variantes entre parenthèses après le coup
qu'elles remplacent, imbriquées et numérotées ; un clic sur un coup y va, et *Revenir à la partie* ramène à la
position que la variante quitte. Un clic droit sur un coup d'une variante la promeut (elle devient la ligne dont elle
part, dont les coups après elle deviennent la variante), la supprime avec celles qui en partent, ou la copie.
*Copier le PGN* écrit les variantes, et celles d'un fichier PGN ouvert sont lues, imbriquées à n'importe quelle
profondeur, avec leurs commentaires et leurs symboles (un coup qui ne peut pas être joué ne termine que sa variante). Le graphe, les analyses et les
jugements sont ceux de la partie ; sur une variante, le graphe marque où elle quitte la partie.

### Les coups, le tableau et le bilan
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Coups** écrit la partie en texte continu : figurines (voir [Apparence](#apparence)), les coups du livre grisés et
la fin du livre marquée, les jugements en couleur, le coup en cours encadré ; survoler un coup indique ce qu'a dit son
moteur et son jugement. **Tableau** liste chaque coup avec le score de son moteur (celui des Blancs), sa profondeur et
son temps, une barre pour le temps, les coups marqués en couleur.

**Bilan**, une fois la partie analysée : pour chaque camp sa précision (celle de lichess : celle de chaque coup
d'après les chances de gain qu'il a perdues, les coups des moments tendus pesant plus), ses imprécisions, erreurs et
gaffes, sa perte moyenne en centipions, la part de ses coups que l'analyse jouerait aussi, la profondeur et le temps
moyens de son moteur ; sa précision dans l'ouverture, le milieu de partie et la finale ; avec plusieurs analyses, les
précisions que donne chacune. Puis les moments clés, chacun à un clic : le tournant (le coup qui a perdu le plus, avec
le meilleur coup), les autres erreurs et gaffes, les moments où les deux moteurs étaient en désaccord de 1,5 pion ou
plus pendant deux coups de suite, le début de la finale et l'entrée dans les tables. *Qui l'a vu en premier* indique,
pour le tournant, à quel coup chaque moteur qui a joué et chaque analyse a donné pour la première fois un pion ou plus
au camp qui gagne. *Copier le PGN avec l'analyse* écrit la partie avec l'évaluation de chaque position (`[%eval]`),
les jugements en NAG et, après un coup marqué, la ligne que l'analyse préférait ; *Copier en BBCode* donne le bilan
pour un forum.

### Les tables de finales sur la page Échiquier
<!-- en: tablebases-on-the-board-page 721b71dd -->

Avec les dossiers Syzygy de la configuration (*Adjudication* > tables de finales), la page Échiquier interroge les
tables pour chaque position de la partie : le graphe teinte la zone que connaissent les tables, *Ce coup* indique le
résultat après le coup et si le coup a gardé celui d'avant ou l'a gâché, et le bilan marque où les tables prennent le
relais. Chaque position est interrogée comme si une prise ou un coup de pion venait d'être joué, ce qui est exact sauf
là où la règle des 50 coups est proche. Les tables sont celles du processus du programme : elles ne sont pas
interrogées pendant qu'un tournoi y joue.

### Jouer une partie depuis une position
<!-- en: playing-a-game-from-a-position 6b5eb32f -->

**Jouer une partie…** joue une partie depuis la position de l'échiquier entre deux moteurs de la bibliothèque, ou entre
toi et l'un d'eux (voir *Humain* plus bas) (*Blancs* et *Noirs*, `<->` les échange avec leurs pendules), avec une
pendule pour chacun (des minutes et un incrément en secondes ; *La même pour les deux*, ou une chacun pour un handicap
de temps), et, si c'est coché, une seconde partie avec les couleurs inversées. Les moteurs sont lancés avec les valeurs
d'options de la bibliothèque, un moteur WinBoard par l'adaptateur. L'échiquier suit la partie au fil du jeu (revenir en
arrière arrête de la suivre jusqu'à ce que sa fin soit de nouveau atteinte, ou qu'on clique sur *Revenir au coup en
cours*), avec les pendules, la recherche du camp au trait et *Arrêter*, qui termine la partie sans résultat. Une partie
se termine par mat, pat, matériel insuffisant, la règle des 50 coups, une triple répétition, un coup illégal, un moteur
qui s'arrête, ou une perte au temps (la pendule accordant 0,1 s pour que le coup arrive). Chaque partie est ajoutée,
quand elle se termine, à un fichier PGN dans `games/` dans le profil (nommé d'après la date et les moteurs, avec une
balise `Termination`), que *Ouvrir les parties* liste sur l'échiquier.

Les parties sont jouées par le client de moteurs propre à RookStudio, à l'écart de tout tournoi : elles tournent
pendant qu'un tournoi joue, sans y toucher. Leurs threads (le plus grand nombre des deux moteurs, puisqu'ils jouent à
tour de rôle) comptent dans le budget de cœurs ; quand les parties des tournois en cours et l'analyse remplissent déjà
la machine, la carte le dit et propose *Jouer quand même* au lieu de *Jouer*. L'analyse s'arrête quand une partie
commence, car elle serait en concurrence avec ses moteurs.

**Humain**, en tête des listes *Blancs* et *Noirs*, c'est toi : tu joues ce camp toi-même ; tes coups se jouent sur le
plateau à la souris, sur le plateau à plat comme sur le plateau 3D (un pion qui atteint la dernière rangée demande sa
pièce). Le plateau se retourne pour mettre ton camp en bas à chaque partie (*Retourner* le tourne toujours), ne prend
tes pièces qu'à ton tour (la carte affiche *À toi de jouer*), et un clic pendant que tu regardes un coup précédent le
ramène à la partie. Ta pendule tourne comme celle d'un moteur, perte au temps comprise : donne-toi plus de temps qu'au
moteur en décochant *La même pour les deux*. Pendant que tu joues, ce que pense le moteur est gardé de côté (son score,
sa profondeur et sa ligne, donc aussi la barre d'évaluation et la courbe du graphe) ; la partie terminée les a, prête
pour *Analyser la partie*. *Abandonner la partie* la termine à tout moment, gagnée par le moteur ; avec une seconde
partie, celle-ci suit, ta couleur inversée. Tes parties sont enregistrées sous ton nom de session, et seul le moteur
compte dans le budget de cœurs. On ne peut pas reprendre un coup, et le moteur joue à pleine force sauf si sa fiche en
règle une plus faible (`UCI_LimitStrength` et `UCI_Elo`, ou `Skill Level`, s'il les a).

Dans une vue pour l'étude, la carte mène aussi à la page Jouer : une partie à toi contre un moteur à la force que tu
choisis, avec ses ouvertures, un handicap, des reprises et des indices, ses aides masquées jusqu'à la fin (voir [La page
Jouer](#la-page-jouer)).

### L'Échiquier d'étude
<!-- en: the-study-board 165eb3bd -->

Dans une vue dont l'Échiquier est *Pour l'étude* (voir [Vues](#vues)), la page Échiquier est l'endroit où les parties
se lisent et s'écrivent. L'échiquier est à gauche ; à droite, le bandeau de la partie, puis six onglets, Alt+1 à
Alt+6 :

- **Notation** : la ligne de titre de la partie et ses en-têtes (voir [En-têtes](#en-têtes)), puis les coups, leurs
  commentaires, symboles et variantes, écrits sur place (voir [Notation](#notation)) ; le tableau des coups et le bilan
  sont dans des onglets à eux (voir [Le bilan d'une partie](#le-bilan-dune-partie)) ;
- **Analyse** : jusqu'à quatre moteurs côte à côte (voir [Plusieurs moteurs côte à
  côte](#plusieurs-moteurs-côte-à-côte)), et le cloud et les tables de finales de Lichess quand tu les as choisis (voir
  [En ligne pour l'analyse](#en-ligne-pour-lanalyse)), avec le graphe de la partie ;
- **Arbre** : les coups joués depuis la position dans tes bases, tes livres et sur Lichess (voir
  [L'onglet Arbre](#longlet-arbre)) ;
- **Références** : les parties les plus fortes de la position (voir [Parties de référence](#parties-de-référence)) ;
- **Questions** : l'assistant (voir [Questions](#questions)) ;
- **Finales** : ce que les tables de finales disent de la position, une fois qu'il reste sept pièces ou moins (voir
  [L'onglet Finales](#longlet-finales)).

Le bandeau au-dessus des onglets dit d'où vient la partie et ce qu'elle est devenue : *Enregistrer* (Ctrl+S) avec son
état (*Modifiée : 2 modifications*, puis *Enregistrée dans « Mega »*), la place de la partie dans la liste
d'où elle vient avec la partie précédente et la suivante (Ctrl+← et Ctrl+→), et *Récentes* (voir [Les parties
récentes et la liste](#les-parties-récentes-et-la-liste)). Avec une bibliothèque vide, les onglets Analyse, Questions
et Bilan proposent de télécharger Gaia en un clic. *Exporter…*, sous l'échiquier, fait de la partie une page à
rejouer, un diagramme, un GIF animé ou un PDF ; <kbd>Ctrl</kbd>+<kbd>Maj</kbd>+<kbd>C</kbd> copie le diagramme de la
position (voir [Exporter et imprimer](#exporter-et-imprimer)).

Un coup se tape aussi bien qu'il se joue : sa première lettre ouvre un champ au-dessus des onglets, qui propose les
coups légaux commençant ainsi, dans les lettres de pièces de la langue de l'interface : K Q R B N en anglais et en
chinois, R D T F C en français, K D T L S en allemand, R D T A C en espagnol et en italien, R D T B C en portugais du
Brésil, Кр Ф Л С К en russe. Les lettres anglaises sont toujours lues aussi : `Ng` propose les mêmes coups que le `Sg`
allemand. Une lettre à deux sens propose les deux : en français, en espagnol, en italien et en portugais, R donne
d'abord les coups du roi, puis ceux de la tour, marqués *tour (en anglais)* ; en russe, К donne les coups du cavalier
et ceux du roi, Кр ceux du roi seulement. Les cases où vont les coups proposés sont pointées sur l'échiquier, la flèche
du premier dessinée : ↑ et ↓ en choisissent un autre, Entrée joue celui choisi, un clic joue n'importe lequel, et
`0-0` est aussi le roque. Le même champ prend les coups des exercices de la page Entraînement (les problèmes, tes
erreurs, deviner les coups, les finales) et des parties de la page Jouer. L'interface écrit les coups dans les mêmes
lettres (la notation quand *Figurines dans la notation* est désactivé, les lignes des moteurs, le bilan) ; le PGN
garde le SAN anglais, comme le lit tout programme, et les commentaires de l'assistant gardent les lettres de leur
propre langue. Les lettres de l'Échiquier des moteurs passent alors sous Alt : Alt+F retourne l'échiquier, Alt+A lance
ou arrête l'analyse, Alt+X montre la menace, Alt+G analyse la partie, Alt+N et Alt+Maj+N vont au coup suivant et au
coup précédent marqués par l'analyse.

### Plusieurs moteurs côte à côte
<!-- en: several-engines-side-by-side 43f5e1fe -->

Dans l'onglet Analyse de l'Échiquier d'étude, jusqu'à quatre moteurs de la bibliothèque analysent la position ensemble,
chacun dans une carte : sa couleur et son nom, son évaluation (celle des Blancs), sa profondeur et sa seldepth, sa
vitesse et ses threads, sa meilleure ligne, et ses autres lignes repliées en une seule (un clic les déplie). Survoler
un coup d'une ligne montre la position après lui sur l'échiquier ; un clic joue la ligne jusqu'à ce coup.
**+ Un moteur** liste les moteurs de la bibliothèque, ceux de l'analyse cochés (↑ ↓ en choisissent un, Entrée
l'ajoute), et *Télécharger un moteur…*, les moteurs populaires de la page Moteurs ; un moteur ajouté cherche aussitôt.
Alt+A, ou *Analyser*, les lance et les arrête tous, et les moteurs sont gardés pour la prochaine fois.

Sur l'échiquier, le meilleur coup de chaque moteur est une flèche de sa couleur : bleu foncé, orange clair, bleu canard
et gris, du premier moteur au quatrième, de clartés différentes aussi. Les moteurs qui jouent le même coup partagent
une flèche, de la couleur du premier d'entre eux, avec leur nombre à côté de son départ. La ligne en haut de l'onglet
dit la même chose en mots : quels moteurs sont d'accord, sur quel coup, et l'écart entre leurs évaluations
(*7…Fe7 pour Stockfish et Gaia · 7…Db6 pour Leela · leurs évaluations à 0.11 près*), ou le mat que voit un moteur ;
aucune couleur n'a à être distinguée. Avec un seul moteur, les flèches sont celles de ses lignes, comme sur
l'Échiquier des moteurs.

**Threads** : l'analyse a un seul nombre de threads, réglé depuis la pastille du haut (*24 / 32 threads*), partagé à
parts égales entre ses moteurs. Le menu d'un moteur (`…`) règle ses lignes, son hash et, à la main, ses threads, que le
partage des autres laisse alors de côté. Un moteur qui cherche sur une carte graphique (*Utilise un GPU* coché dans la
bibliothèque, ou un moteur comme Lc0 dont l'option *Backend* nomme un backend GPU) garde ses propres threads, et le
partage ne les compte pas. Un tournoi qui tourne sur cet ordinateur garde ses cœurs : l'analyse prend au plus ce que
laissent ses parties, et la pastille dit combien elles en prennent. Ces réglages sont propres à l'analyse : ni le
moteur de la bibliothèque ni un tournoi ne les voient.

**Le premier moteur** est celui de l'analyse : le bilan de la partie (Alt+G), la menace (Alt+X, ou le menu du premier
moteur), les questions et les commentaires de l'assistant l'utilisent. Un moteur glissé par son nom sur un autre prend
sa place, et en haut il devient le premier ; *En faire le premier*, dans son menu, fait de même. Quand la partie a une
analyse par le nouveau premier moteur, cette analyse juge les coups et dessine la courbe du graphe de la partie. Les
évaluations que chaque moteur a données des positions de la partie qu'il a cherchées s'affichent sur le graphe en
ligne fine de sa couleur (*analyse en direct*), celles du premier moteur en courbe tant que la partie n'a pas
d'analyse.

Pendant qu'une de tes parties se joue sur Lichess, tous les moteurs s'arrêtent aussitôt, et l'Échiquier d'étude ne
montre aucune de leurs flèches (voir [Fair-play](#fair-play)). *Décharger les moteurs de l'analyse*, dans le menu d'un
moteur ou dans la palette de commandes, termine leurs processus. Chaque geste de l'onglet est dans la palette de
commandes (Ctrl+K) : un moteur ajouté, mis en premier ou retiré, ses réglages, les threads de l'analyse.

### Coups pratiques
<!-- en: practical-moves 26da3fb5 -->

Sous les moteurs, *Contre un humain de 1500, rapide* (le niveau et la vitesse gardés d'une fois sur l'autre) cherche le
coup pratique : parmi les coups que le premier moteur place à moins de 0,3 pion de son meilleur (ou, une fois que les
tables connaissent la position, ceux qui gardent leur verdict), celui qui marque le plus contre un joueur de ce niveau,
dans les parties de ce niveau. Avec un compte Lichess associé, les parties sont celles de l'explorateur de Lichess au
groupe de classement et à la vitesse du niveau (*Lichess 1400–1600, rapide*) ; sans compte, celles de tes bases dont le
classement moyen des joueurs est dans la tranche du niveau, quelle que soit leur vitesse. Le score d'un coup compte à
partir de 200 parties au niveau ; hors des parties connues, le coup pratique est celui qui laisse à l'adversaire le
moins de bonnes réponses, selon le moteur : un moteur à part, sur un thread, cherche les réponses de chaque candidat,
et le tableau dit combien tiennent (*2 sur 31*). Jamais un coup perdant.

Le tableau donne chaque candidat, son évaluation par le moteur (celle des Blancs), son score contre ce niveau et ses
parties à ce niveau, le meilleur du moteur marqué *meilleur* et le coup pratique *pratique* ; sur l'échiquier, le coup
pratique est la flèche bleue, le meilleur du moteur la grise. Une phrase résume, fidèle aux chiffres : *Le coup
pratique : 4.Cg5 — à 0.13 du meilleur coup du moteur, et 58 % attendus à ce niveau.* Quand les parties le montrent,
elle continue avec la réponse la plus jouée au niveau le long de la ligne et ce qui la punit, la réponse du moteur :
*Après 4…d5 5.exd5, la réponse la plus jouée à 1500 est 5…Cxd5? (46 % pour les Noirs), que 6.Cxf7 punit.* Les coups
pratiques demandent l'analyse en cours (Alt+A). Ce qui quitte la machine : avec un compte associé, la position et le
niveau, vers l'explorateur de Lichess ; sans compte, rien. Verrouillé avec l'onglet Analyse pendant qu'une de tes
parties se joue sur Lichess.

### Notation
<!-- en: notation 5147406e -->

La notation s'écrit sur place, sans boîte de dialogue :

- **Commentaires** : Entrée écrit un commentaire après le coup affiché, Maj+Entrée un commentaire avant lui ; Entrée le
  garde, Échap l'abandonne. Un clic sur un commentaire va à son coup, un double clic le corrige.
- **Symboles** : `!` et `?` tapés posent le symbole du coup, et un second signe juste après forme la paire (`!!`, `??`,
  `!?`, `?!`). Les boutons sous la notation posent les autres : seul coup, nouveauté, et l'évaluation de la position
  (=, ∞, et l'avantage des Blancs ou des Noirs, léger, net ou décisif). Un symbole posé de nouveau est retiré.
- **Variantes** : un coup autre que celui de la partie, joué sur l'échiquier ou tapé, commence une variante. Avec Maj
  enfoncée pendant le coup, les coups qui le suivent sont remplacés ; avec Ctrl, une question : une variante, une
  nouvelle ligne principale, ou les coups suivants remplacés (Échap ne joue rien). *Demander quoi faire d'un coup qui
  s'écarte de la partie* (*Réglages* > *Apparence*) pose la question à chaque fois.
- **Suppr** supprime les coups à partir de celui affiché ; **Alt+↑** remonte la variante, jusqu'à la ligne principale.
- **Ctrl+Z** annule chaque modification (un coup, un commentaire, un symbole, une flèche ou une case, un en-tête) et
  **Ctrl+Y** la rétablit ; une ligne sous la notation dit ce qui a été fait et comment l'annuler.

*Figurines dans la notation* (*Réglages* > *Apparence*) montre les pièces en figurines. Les commentaires de
l'assistant s'affichent à part des tiens, en gris bleu et en italique, après une bulle, ou dans un encadré sous son nom
après un commentaire qui était déjà là (voir [Les commentaires de l'assistant](#les-commentaires-de-lassistant)).
Quand tes études de théorie commentent la position affichée, *Théorie · 3* au-dessus des coups mène à leurs notes dans
l'onglet Arbre (voir [Études de théorie](#études-de-théorie)).

**La partie d'un tournoi**, dans l'un ou l'autre style de son PGN (le `{+0.35/18 1.234s, n=…}` de fastchess ou les
`[%eval]`, `[%clk]` et `[%emt]` du style ChessBase), qu'elle soit ouverte depuis son fichier, depuis une base qui l'a
importée ou depuis un tournoi lu comme une base, montre les commentaires de ses moteurs condensés : à côté de chaque
coup, en petit et en gris, l'évaluation qu'a donnée son moteur (pour les Blancs, comme la barre et la courbe) et sa
profondeur, *+1.44/22*, la fin du livre marquée. Survoler un coup dit le reste : son moteur et son camp, l'évaluation de
son côté aussi, la profondeur et la profondeur sélective, le temps et le moment où il s'est décidé pour le coup, les
nœuds et la vitesse, les accès aux tables, ses chances de gain, de nulle et de perte, et la ligne qu'il attendait après
lui, écrite en coups. Le bouton droit sur le coup montre cette ligne sur l'échiquier, en fait une variante à partir de
l'endroit où la partie l'a quittée (Ctrl+Z la retire), ou copie le commentaire du moteur tel quel. La ligne sous les
coups choisit ce qui s'affiche à côté d'eux : l'évaluation et la profondeur, le temps aussi, ou rien (Ctrl+K aussi). Le
PGN ne change pas : Ctrl+S et *Copier le PGN* réécrivent le commentaire de chaque moteur tel qu'il a été lu, chaque
champ gardé, tant que les données du coup sont les mêmes.

### Flèches et cases
<!-- en: arrows-and-squares a46e6347 -->

Le bouton droit dessine sur l'Échiquier d'étude : un clic colore une case, un glissement d'une case à une autre
dessine une flèche, et le même geste de nouveau l'efface. Vert par défaut, rouge avec Maj, bleu avec Alt, jaune avec
les deux. Un clic gauche sur une case vide efface les flèches et les cases du coup. Elles appartiennent au coup affiché
et sont enregistrées avec la partie (en `[%cal]` et `[%csl]` dans le PGN). Sur l'échiquier 3D, le bouton droit dessine
sur l'échiquier et fait tourner la caméra à côté de lui ; le bouton du milieu la fait tourner aussi.

### En-têtes
<!-- en: headers 4e8fbaae -->

La ligne de titre en haut de la notation donne les joueurs, leur Elo, l'événement, la ronde, la date et le résultat ;
un clic dessus, ou Alt+H, déplie les en-têtes sur place. Tab passe au champ suivant et complète un nom d'après ceux de
la base ; une date qui n'en est pas une est signalée (`YYYY.MM.DD`, avec `??` pour ce qui n'est pas connu) ; le
résultat se choisit d'un clic ; le code ECO est proposé d'après les coups (les noms d'ouvertures de Lichess) et écrit
de lui-même à l'enregistrement quand il manque. Les autres en-têtes s'affichent tels que la partie les a, et un de plus
s'ajoute par son nom. Chaque champ changé est une modification (Ctrl+Z) ; Entrée replie les en-têtes, Échap rend au
champ sa valeur.

### Enregistrer une partie
<!-- en: saving-a-game ca08879e -->

Ctrl+S enregistre la partie là où elle doit aller : une partie ouverte depuis une base y retourne, sous son numéro ;
une partie d'un fichier PGN retourne dans le fichier, à sa place (le fichier est réécrit aussitôt, ses autres parties
telles quelles). Toute autre partie ouvre un menu sous *Enregistrer*, où Entrée prend la première ligne : l'une de tes
bases (celle de la dernière fois en premier), un nouveau fichier PGN, ou la fin d'un fichier PGN. Ctrl+Maj+S
enregistre une copie de n'importe quelle partie ailleurs. Une partie sans joueurs ouvre d'abord ses en-têtes.

Une base dans laquelle un autre programme écrit est en lecture seule pendant ce temps : la partie reste modifiée ici
jusqu'à ce qu'elle puisse être enregistrée.

### Positions et parties vers Lichess
<!-- en: positions-and-games-to-lichess 1fc6bb1d -->

**lichess.org…**, sous l'échiquier, envoie la position ou la partie à Lichess ; chaque action dit ce qu'elle envoie.
*Ouvrir la position sur lichess.org* ouvre sur elle l'échiquier d'analyse de Lichess, dans le navigateur : la position
va dans l'adresse, rien d'autre. *Partager la partie* l'importe sur Lichess sans compte et copie son adresse :
quiconque a l'adresse voit la partie, les noms de ses joueurs aussi. *Faire jouer la position*, avec une cadence (de
3+2 à 30+20, ou des jours par coup), crée un défi ouvert depuis la position : un lien pour chaque camp, à envoyer à son
joueur ; une telle partie n'est pas classée. *Mettre la partie en favori sur Lichess*, pour une partie qui en vient,
la met dans tes favoris : l'action envoie l'identifiant de la partie avec ton jeton, et demande une fois à Lichess la
permission de modifier tes préférences. *Vers une étude Lichess…* envoie la partie dans une de tes études ou dans une
nouvelle (voir [Tes études Lichess](#tes-études-lichess)).

Pendant que tu joues une partie sur Lichess, aucune de ces actions ne touche à sa ligne : une position sur le chemin
de sa position, ou la partie qui passe par elle, ne quitte pas la machine (voir [Fair-play](#fair-play)). Le reste
demeure possible.

### Les parties récentes et la liste
<!-- en: recent-games-and-the-list ba602f73 -->

Ctrl+Tab montre les parties récentes, celle de l'échiquier en premier (douze au plus) : Ctrl+Tab de nouveau descend
dans la liste, Ctrl+Maj+Tab remonte, et relâcher Ctrl ouvre la partie choisie, avec sa liste, son fichier, ses
modifications et son historique d'annulation. Une partie dont les modifications ne sont pas enregistrées est gardée
telle quelle jusqu'à ce que tu y reviennes. *Récentes*, sur le bandeau, les liste aussi : un clic en ouvre une, et le
bouton droit sur une partie modifiée abandonne ses modifications (Ctrl+Y les rétablit).

Une partie ouverte depuis la liste d'une base y garde sa place : Ctrl+← et Ctrl+→ ouvrent la partie précédente et la
suivante de la liste, ou de son filtre, au même coup quand la partie passe par la position affichée.

### L'onglet Arbre
<!-- en: the-tree-tab 184526b8 -->

L'onglet Arbre donne les coups joués depuis la position affichée : leurs parties, les parts des victoires des Blancs,
des nulles et des victoires des Noirs, le score, l'Elo moyen, la performance du camp au trait (d'après la table de la
FIDE) et la dernière année où le coup a été joué. Ses sources sont des boutons au-dessus des coups :

- **Mes bases** : tes bases additionnées, coup par coup (le bouton droit choisit lesquelles ; la part de chacune
  s'affiche au curseur sur les parties) ;
- chaque base dont l'arbre d'ouvertures est construit, et chaque livre Polyglot de la bibliothèque ;
- **Maîtres (Lichess)**, **Lichess** (le bouton droit choisit ses cadences et ses classements) et le joueur dont tu as
  importé les parties : l'explorateur d'ouvertures de Lichess, qui demande un compte associé (voir [Associer ton
  compte](#associer-ton-compte)).

Le coup sous la souris, ou choisi avec ↑ ↓, s'affiche en flèche bleue sur l'échiquier ; un clic ou Entrée le joue, et
`*` ou son étoile le marque dans le répertoire actif (voir [La page Ouvertures](#la-page-ouvertures)). Quand la ligne
de la partie quitte la base, l'onglet dit à quel coup, et si ce coup est une **nouveauté**, ce que confirme une
recherche de position (*Voir la dernière position connue*). L'arbre d'une base est construit après chaque import, et
de nouveau en arrière-plan après qu'une partie y a été enregistrée, sa progression affichée avec *Annuler*. Les notes
de tes études de théorie accompagnent les coups, quelle que soit la source : une colonne *Théorie*, et *Ce que disent
tes études* sous les coups (voir [Études de théorie](#études-de-théorie)). *Rapport sur cette position*, à droite des
parties, ouvre son rapport d'ouverture (voir [Le rapport d'ouverture](#le-rapport-douverture)).

### Parties de référence
<!-- en: reference-games c45f1d18 -->

L'onglet Références cherche les parties les plus fortes de la position dans tes bases, en arrière-plan : les plus
fortes d'abord (selon le classement moyen des joueurs), puis les plus récentes ; un clic en ouvre une. Tôt dans
l'ouverture, trop de parties atteignent la position pour chercher les plus fortes : l'onglet Arbre en dit plus.
*Toutes les parties de la position dans « … »* les liste dans la base.

### L'onglet Finales
<!-- en: the-endgames-tab 330bcfca -->

Le sixième onglet de l'Échiquier d'étude, Alt+6, dit ce que les tables de finales disent de la position affichée, une
fois qu'il reste sept pièces ou moins, ou huit avec un pion de chaque camp sur une même colonne (les tables op1 de
Lichess) ; grisé au-delà, il le dit quand on le choisit. En haut, le verdict (*Gain des Blancs*, *Nulle*, *Gain des
Blancs, mais nulle par la règle des 50 coups*), d'où il vient et la distance de la fin : les coups jusqu'au mat jusqu'à
six pièces (DTM, comme l'appellent d'autres programmes), à sept les coups jusqu'à une prise ou un coup de pion (DTZ), à
huit les coups jusqu'à la prochaine prise, promotion ou mat (DTC). Puis les coups, groupés selon l'issue qu'ils
laissent au camp qui les joue, les meilleurs d'abord : *Garder le gain*, *Garder la nulle*, *Gâcher le gain : nulle*,
*Perdre*… Un coup qui gâche le résultat est dit avec la raison : la réponse qui le réfute et ce que disent alors les
tables (*→ 52…Rxh7 · Gain des Noirs : mat en 19 coups*). Un coup survolé montre sa ligne principale sur l'échiquier ;
un clic le joue, en variante quand la partie a pris un autre chemin, comme sur l'échiquier. Quand la partie continue
depuis la position, l'assistant dit ce qu'a fait son coup, instruit par les tables (*52.Txh7 perd : après 52…Rxh7, les
tables donnent le mat aux Noirs en 19 coups. 52.g5 gardait la nulle théorique (tables à 7 pièces).*), écrit d'après
leurs seuls verdicts : il ne le dit que quand elles le disent.

Tes tables sur le disque répondent d'abord (Syzygy, celles qu'utilisent l'adjudication des tournois et les finales de
la page Entraînement : leur verdict et les coups jusqu'à une prise ou un coup de pion, pas de distance au mat) ; celles
de Lichess répondent pour les positions qui leur manquent, une fois autorisées. La première fois, l'onglet demande :
pour cette position, toujours, ou pas maintenant (*Toujours* peut être changé sur la page Lichess, sous *En ligne pour
l'analyse*). Seul le FEN de la position part vers tablebase.lichess.ovh, sans compte, une requête à la fois : la
position que tu regardes, et les lignes principales de ses coups, celles qui gâchent le résultat d'abord et les autres
quand elles sont survolées. Chaque réponse est gardée dans ton profil et n'est jamais redemandée. L'onglet Analyse garde
le verdict et sa distance, avec un lien vers cet onglet. L'onglet est verrouillé pendant qu'une de tes parties se joue
sur Lichess, comme l'onglet Analyse, et masqué pendant une partie contre un moteur jusqu'à sa fin.

### Plus loin avec l'explorateur
<!-- en: the-explorer-more 1471aca0 -->

Avec les sources de Lichess dans l'onglet Arbre, **les parties que nomme l'explorateur** viennent sous les coups : les
parties de maîtres qui atteignent la position (*Parties de maîtres*), ou les parties fortes jouées sur Lichess ; un
clic demande la partie à Lichess et l'ouvre sur l'échiquier, à la position que montrait l'arbre.

Avec la source *Lichess* (tous ses joueurs), *Chaque coup mois par mois* demande à Lichess, pour chaque coup listé,
combien de parties l'ont joué mois par mois : une ligne de ses deux dernières années dans la colonne *Par mois*, verte
quand il est plus joué ces derniers temps, rouge quand il l'est moins ; sous la souris, ses parties des six derniers
mois contre celles des six mois d'avant. Le résumé dit la même chose de la position. Cela coûte une requête par coup,
aussi l'option reste-t-elle désactivée tant que tu ne la demandes pas ; l'explorateur des maîtres n'a pas de mois.

### Le rapport d'ouverture
<!-- en: the-opening-report 4d6f6a24 -->

Le **rapport d'ouverture** d'une position rassemble ce que tes bases en savent. Il s'ouvre depuis *Rapport sur cette
position* de l'onglet Arbre, depuis une ligne de la page Ouvertures (le bouton droit sur une lacune : *Rapport
d'ouverture*) et depuis Ctrl+K, *Rapport d'ouverture* ; il s'affiche à la place de la page Échiquier, Échap revenant à
la position. Il est refait à chaque ouverture, en arrière-plan à basse priorité, section par section avec sa
progression et *Annuler* : une fraction de seconde pour la plupart des positions, une seconde environ pour une position
de millions de parties.

En haut : la position, son ouverture (les ouvertures nommées de Lichess) et les coups qui y mènent ; puis ses sources,
celles de l'onglet Arbre : tes bases dont l'arbre d'ouvertures est construit (celles qu'additionne *Mes bases*), avec
leurs parties ; **Maîtres (Lichess)** et **Lichess** avec les vitesses et les classements de l'onglet Arbre, cochées
quand l'onglet Arbre les montre, ou d'un clic (elles demandent la position à l'explorateur de Lichess, comme le fait
l'onglet Arbre, et demandent un compte associé) ; et d'où viennent les évaluations. *Au répertoire* ajoute la première
ligne principale au répertoire actif (Ctrl+Z la retire) ; *Imprimer…* fait du rapport un PDF, son résumé marqué comme
celui de l'assistant, et *Page HTML* une page à part dont les lignes principales se rejouent sur un échiquier (voir
[Exporter et imprimer](#exporter-et-imprimer)). Cinq sections suivent (Ctrl+Page suiv. et Ctrl+Page préc. passent de
l'une à l'autre) :

- **Lignes** : les coups joués depuis la position avec les chiffres de l'onglet Arbre (parties, gains des Blancs,
  nulles et gains des Noirs, score, Elo moyen, performance, dernière année) et une évaluation : celle de Lichess, gardée
  hors ligne (voir [Les évaluations de Lichess hors ligne](#les-évaluations-de-lichess-hors-ligne)), sinon celle du
  moteur de l'analyse (un million de nœuds sur un thread, pour les cinq premiers coups qui n'en ont pas) ; sa source et
  sa profondeur sous le curseur. Un clic, ou ↑ ↓ et Entrée, joue le coup sur la page Échiquier. Puis les **lignes
  principales** : les suites les plus jouées, tant qu'elles gardent un cinquième des parties de leur premier coup, avec
  les parties qui vont jusqu'à leur fin et le score des Blancs ; un clic en montre une sur la page Échiquier ;
- **Plans et structures** : les structures de pions au 20e coup (dix coups plus loin pour une position plus tardive)
  dans les parties de la position, telles que les nomment les faits de l'assistant, *déjà sur l'échiquier* quand la
  position en a déjà une ; et ce que font les joueurs dans les quinze coups suivants : les poussées de pions (une, ou
  deux à la suite : *b4 puis b5*) et les trajets des pièces (comme la recherche Manœuvre suit une pièce) joués dans un
  dixième des parties ou plus, trois pour chaque camp, avec leur part et le score des Blancs. Un plan que connaissent
  les faits de l'assistant est nommé (*L'attaque de minorité : b4 puis b5*). Ce sont des fréquences, pas des conseils ;
  elles demandent 50 parties, et une position de plus de 20 000 parties est comptée sur un échantillon régulier de
  celles-ci. Un clic montre ces parties dans la liste ;
- **Parties clés** : la plus forte (le classement moyen des joueurs le plus haut), la plus récente à 2500 et plus en
  moyenne, et la première partie d'une nouveauté répétée depuis, chacune avec sa raison et le plan qu'elle joue ;
  *Ouvrir* la montre sur la page Échiquier à la position (*Revenir au rapport* y ramène) ;
- **Nouveautés** : le long des lignes principales, les coups joués pour la première fois dans les cinq dernières années
  de tes bases et répétés depuis, avec leur première année, combien de fois ils ont été répétés et leur score ; et ta
  propre partie qui a quitté tes bases là (tes noms sont ceux de *Réglages › Tes noms*). La première année d'un coup
  vient des arbres, qui contiennent toutes les parties qui atteignent sa position dans leurs 60 premiers demi-coups ;
  ta partie est confirmée par une recherche de la position, comme l'onglet Arbre confirme une nouveauté ;
- **Résumé** : le résumé du rapport par l'assistant, écrit par son modèle d'après les seuls chiffres et faits du
  rapport (voir [Le résumé du rapport](#le-résumé-du-rapport)).

Une position qu'atteignent moins de 50 parties donne ses coups et ses parties, sans plans ni structures (*Ajouter les
joueurs de Lichess comme source*). Sans base dont l'arbre couvre ses parties, le rapport est fait des maîtres de
Lichess, avec *Reconstruire l'arbre de …* et *Importer une base*. Hors ligne, tes bases et les évaluations gardées hors
ligne répondent, et les sources de Lichess montrent les derniers chiffres que le rapport a gardés pour la position,
datés. Sans moteur ni évaluations de Lichess gardées hors ligne, les évaluations restent vides et le résumé n'en dit
rien (*Télécharger Gaia…*). Pendant qu'une de tes parties se joue en direct sur Lichess, le rapport est verrouillé, et
revient de lui-même à sa fin ; sur la position d'une partie par correspondance, les lignes et les chiffres de tes bases
restent (Lichess autorise un livre et une base), les évaluations, l'explorateur de Lichess et le résumé attendent. Rien
ne quitte la machine sinon ce qu'envoie l'onglet Arbre : les sources Lichess cochées.

### Le résumé du rapport
<!-- en: the-reports-summary a859f9a6 -->

Le résumé s'écrit par passages : la structure de pions, les plans de chaque camp, les coups joués, la ligne
principale, les évaluations. Le modèle de l'assistant écrit chacun d'après les seuls chiffres de ce passage, tenu par
une grammaire qui ne le laisse nommer que les coups de la position, les lignes du rapport, leurs cases et leurs
chiffres ; puis le vérificateur lit chaque phrase : un chiffre doit être un de ceux que le rapport donne pour ce que
nomme la phrase, un coup un de ceux de la position ou de ses lignes, un plan ou une structure un de ceux qu'a le
rapport et du camp qu'elle dit, et elle ne donne aucun conseil. Un passage refusé est réécrit une fois ; refusé deux
fois, le passage de la version type reste, et le curseur dessus dit pourquoi. *Sur quoi il s'appuie* liste les
chiffres, les faits et les évaluations utilisés ; une carte dit ce que le vérificateur a refusé. *La version type*
montre le résumé des phrases types, *Réécrire* fait écrire le modèle autrement, *Copier* le copie. Sans le modèle (pas
téléchargé, un autre fichier GGUF, ou pendant qu'un tournoi sur cette machine occupe le processeur), les phrases types
l'écrivent d'après les mêmes chiffres, en anglais ou en français. Sur le processeur, un résumé prend quelques secondes
par passage.

### Le bilan d'une partie
<!-- en: the-review-of-a-game 7c5c4437 -->

Sur l'Échiquier d'étude, la partie d'un tournoi montre en haut de l'onglet *Bilan* ce que pensaient ses deux moteurs, dès
son ouverture et sans aucune analyse : les chances des Blancs au fil de la partie selon chaque moteur à ses propres
coups (d'après la part de gains, de nulles et de pertes qu'il a donnée, retournée pour les Noirs, sinon d'après son
évaluation convertie comme le fait le bilan), le livre ombré, les passages où les deux moteurs voyaient des parties
différentes teintés ; en dessous, le temps de chaque coup, celui des Blancs au-dessus de la ligne et celui des Noirs en
dessous ; le temps, la profondeur moyenne et la vitesse de chaque moteur. Puis ses moments, chacun à un clic : comment
chaque moteur voyait la partie à la sortie du livre, où ils n'étaient pas d'accord, où les tables de finales sont
entrées dans une recherche, où un mat a été vu, et comment la partie s'est terminée. Une fois la partie analysée, la
courbe du bilan est dessinée par-dessus les leurs, plus épaisse.

Sur l'Échiquier d'étude, l'onglet *Bilan* à côté de la notation lit l'analyse de la partie (Alt+G) comme le ferait
un entraîneur :

- en haut, les chances des Blancs au fil de la partie, avec les moments clés en points (foncés pour un bon coup,
  orange pour une erreur) ; un clic va au coup ;
- la théorie de la partie dans l'une de tes bases (celle de l'onglet Arbre, sinon la première) : ses coups du livre ne
  sont pas jugés, et sa nouveauté est nommée ; le livre s'arrête avant la première erreur que voit le moteur. Les coups
  qu'a joués le livre d'ouvertures d'un tournoi (`{book}` dans le PGN) ne sont pas jugés non plus ;
- **Moments clés** : les coups des points, chacun avec son verdict (une erreur avec les chances qu'elle a perdues et le
  meilleur coup, un seul coup avec le deuxième meilleur coup du moteur : *Seul coup. Gaia : +0.62, le suivant +0.31.*),
  et l'endroit où commence la finale ;
- **Les moments clés, en mots**, **L'histoire de la partie**, et **Ce qu'il faut travailler**, pour chaque camp, avec
  les coups qui le montrent ; un clic va au coup.

La même partie reçoit toujours le même bilan : les mêmes scores, symboles, moments clés et commentaires, quoi que
fasse la machine. L'analyse de la partie sur l'Échiquier d'étude compte des nœuds plutôt que du temps : chaque position
est cherchée sur 20 000 nœuds à la première passe, quatre fois plus à chaque passe, jusqu'au choix à côté du bouton
(320 000, 1 280 000, 5 120 000 ou 20 480 000 nœuds par position, ou *jusqu'à l'arrêt*), sur un seul thread et
depuis un hash vide, pour que le résultat d'une position ne dépende que du moteur et de la position. Plusieurs
positions sont cherchées à la fois, un processus de moteur chacune : autant que l'analyse a de threads, ou la moitié
des cœurs du processeur, jusqu'à quatre, quand c'est davantage ; moins pendant qu'un tournoi joue sur cette machine,
qui garde ses cœurs. Là où le coup joué était le choix du moteur et où la position n'est pas déséquilibrée, une seconde recherche de
la position avec deux lignes donne le deuxième meilleur coup : un seul coup est annoncé avec lui.

Les positions de la finale à sept pièces ou moins peuvent être jugées exactement par les tables de Lichess,
interrogées une fois pour cette partie ou toujours. *Commenter les moments clés* écrit un commentaire de l'assistant
sur chacun, en arrière-plan, à côté du commentaire qu'un coup peut déjà avoir (l'analyse de Lichess, le tien, celui
d'un annotateur), qui n'est jamais remplacé ; *Annoter la partie* écrit les évaluations, les symboles et les meilleures
lignes dans la notation. Ctrl+Z retire l'un comme l'autre.

**L'histoire de la partie** est écrite par le modèle de l'assistant quand il est là, en arrière-plan, à partir des
faits du bilan : ses verdicts et ses évaluations, les moments clés, l'ouverture, la finale et la façon dont la partie
s'est terminée, avec ce qu'ont dit les tables de Lichess. Chaque passage est tenu à ces faits par le vérificateur des
commentaires : chaque coup, évaluation et nom qu'il cite doit venir du bilan. Un passage refusé est réécrit ; refusé
deux fois, il cède la place à celui de la version type. Une carte de la couleur de l'assistant dit que le modèle a
écrit l'histoire et qu'elle a été vérifiée, et ce que le vérificateur a refusé. *La version type* montre à sa place
l'histoire écrite par les commentaires types, *La réécrire* la fait raconter autrement par le modèle (Ctrl+K aussi),
*La copier* copie l'histoire affichée. Sans le modèle (pas téléchargé, ou un autre fichier GGUF qui n'a pas appris les
commentaires de RookStudio), ou pendant qu'un tournoi occupe le processeur de cette machine, l'histoire est la version
type, comme elle le dit. Rien ne quitte la machine.

### Chaque coup expliqué
<!-- en: every-move-explained 5f600fec -->

*Expliquer chaque coup*, dans l'onglet *Bilan* (ou Ctrl+K), donne à chaque coup de la partie une ligne, dans l'onglet
*Explications* à côté des coups ; sans bilan, la partie est d'abord analysée (Alt+G), et ses coups expliqués une fois
le bilan fait. Les lignes arrivent en arrière-plan, chacune dès qu'elle est écrite, avec le nombre de lignes faites et
*Annuler* ; celles déjà écrites restent, et *Expliquer le reste* les termine plus tard.

- **La théorie** est regroupée : une ligne pour chaque nom d'ouverture qu'atteignent ses coups, trois au plus, avec les
  parties de tes bases qui ont atteint sa dernière position (*théorie, 61 204 parties de tes bases*).
- **Un coup trop simple pour plus** est dit en un mot : *Reprise forcée.*, *Seul coup légal.*
- **Les autres** reçoivent une phrase du modèle de l'assistant, écrite d'après les faits de la position, sa grammaire
  ne le laissant nommer que des coups légaux, chaque phrase lue par le vérificateur ; une phrase que le vérificateur
  refuse est remplacée par la phrase type, et survoler *phrase type : le vérificateur a refusé la phrase du modèle* dit
  pourquoi. Sans le modèle, les phrases types les écrivent ; quand elles n'ont rien à dire, ce sont les mots du bilan
  lui-même (*Le moteur préférait 14…Ff5.*).

Chaque ligne dit d'où elle vient : la théorie, le modèle, une phrase type, un mot, le bilan. *Tous les coups*, *Les
coups qui comptent* (les coups marqués, les coups uniques, les tournants, la nouveauté) et *Les moments clés* choisissent
lesquels s'affichent ; un clic va au coup. Le résumé les compte et donne le temps que prend un coup, faits rassemblés
et phrase du modèle vérifiée : environ un quart de seconde pour la phrase sur un GPU, quelque 3 secondes par coup sur
un processeur partagé avec d'autres travaux (32 coups expliqués en une minute et demie). Elles sont gardées avec le
bilan, à côté des coups, jamais dans la partie : *Les écrire dans la partie* en fait des commentaires de l'assistant,
marqués comme les siens dans le PGN, à côté du commentaire qu'un coup a déjà, qui n'est jamais remplacé ; Ctrl+Z les
retire tous. Rien ne quitte la machine. Verrouillé pendant qu'une de tes parties se joue sur Lichess, masqué pendant
une partie contre un moteur jusqu'à sa fin.

### La visite guidée et « Pourquoi pas ? »
<!-- en: the-guided-visit-and-why-not f67b11ea -->

*Visite guidée*, dans l'onglet *Bilan* (ou Ctrl+K), te fait parcourir les moments clés du bilan l'un après l'autre
(ses coups marqués quand il n'en a pas) : l'échiquier montre la position avant le coup de la partie, et demande *Avant
de voir pourquoi : qu'aurais-tu joué ?* Joue le coup sur l'échiquier ou tape-le ; rien n'est montré avant ta réponse,
ni l'évaluation, ni une flèche, ni la notation de la partie, et Espace dit *Je ne sais pas*. Puis ta réponse est jugée
comme *Deviner les coups* juge une proposition (voir [Deviner les coups](#deviner-les-coups)) : le coup du moteur
3 points, un coup aussi bon 2, un coup jouable 1, sinon 0, par le moteur de l'analyse, une seconde pour chacun ;
l'échiquier montre ton coup en bleu, celui du moteur en vert et celui de la partie en orange, avec une explication
tenue aux faits du bilan et ce sur quoi elle s'appuie (le moteur du bilan et sa ligne, les faits des positions). Entrée
passe au moment suivant, *Voir la ligne* joue la ligne du moteur sur l'échiquier, Échap arrête. Une mauvaise réponse,
aucun point ou *Je ne sais pas*, devient une carte à revoir aujourd'hui, comme tes erreurs (voir [Tes erreurs en
exercices](#tes-erreurs-en-exercices)) : la position, le coup du moteur à trouver.

**Pourquoi pas ?** (Alt+Q) pose d'une seule touche la question *Comparer* de l'assistant (voir
[Questions](#questions)), partout où un coup s'affiche : après un coup essayé sur l'échiquier (une variante, comme
toujours), il oppose ce coup à celui de la partie ; sur le coup même de la partie, le meilleur du bilan contre lui ; le
bouton droit sur une ligne d'un moteur dans l'onglet Analyse, ou sur un coup de l'onglet Arbre, propose *Pourquoi
pas … ?* ; dans la visite guidée, *Pourquoi pas un autre coup ?* oppose ta réponse, ou le coup de la partie, à celui du
moteur. La réponse arrive dans l'onglet Questions (dans la visite, en dessous) : le moteur après chacun des deux coups
(un thread, 300 000 nœuds par position), les faits des deux positions, les parties et le score de tes bases, la suite ;
*Ajouter à la notation* l'écrit, comme pour toute question. Pas de conversation libre : l'assistant répond ce
qu'établissent le moteur, tes bases et les faits. Les deux sont verrouillés pendant qu'une de tes parties se joue sur
Lichess, et masqués pendant une partie contre un moteur jusqu'à sa fin.

### Exporter et imprimer
<!-- en: exporting-and-printing 33db051f -->

*Exporter…* sous l'Échiquier d'étude, à côté de *Copier le FEN* et de *lichess.org…*, ouvre la feuille Export à la
place de la page ; Échap y revient. Ses formats sont des pastilles en haut, celui choisi la dernière fois déjà choisi
(<kbd>Ctrl</kbd>+<kbd>Page suiv.</kbd> et <kbd>Ctrl</kbd>+<kbd>Page préc.</kbd> les parcourent) ; sa gauche montre le
fichier tel qu'il sera, sa droite ce qui y entre, et chaque format garde ses réglages pour la fois suivante. Entrée
enregistre : la boîte de dialogue du système s'ouvre dans le dossier choisi la dernière fois. Tout est fait par
RookStudio, sur ton disque : rien n'est envoyé nulle part (partager une partie sur lichess.org, c'est le rôle de
*lichess.org…*, voir [Positions et parties vers Lichess](#positions-et-parties-vers-lichess)).

- **Page HTML** : un seul fichier qui contient tout, qui s'ouvre sans Internet, sur un téléphone aussi, et peut être
  envoyé tel quel. L'échiquier aux couleurs et aux pièces de l'application, les coups, leurs commentaires et leurs
  variantes ; un clic sur un coup le montre, dans une variante aussi, avec les flèches et les cases dessinées à ce
  coup ; <kbd>←</kbd> <kbd>→</kbd> <kbd>Origine</kbd> <kbd>Fin</kbd> parcourent les coups et <kbd>F</kbd> retourne
  l'échiquier. Ce qu'il contient se coche : les commentaires, les variantes, les flèches et les cases, les commentaires
  de l'assistant (marqués comme les siens), les évaluations et leur courbe, les pendules. Son poids est estimé avant
  l'enregistrement. Depuis la page Bases, ou depuis la page Échiquier quand sa partie a été ouverte depuis la liste
  d'une base, la page contient cette partie ou les parties du filtre, choisies dans une liste en haut
  (<kbd>Page préc.</kbd>, <kbd>Page suiv.</kbd>) ; au-delà de 200 parties elle serait trop lourde à ouvrir, et la
  feuille demande : une page par partie dans un dossier, avec leur liste (`index.html`), ou les 200 premières.
- **Diagramme, PNG ou SVG** : la position affichée, avec les flèches et les cases dessinées à ce coup (le bouton
  droit), les coordonnées, un point près du bord du camp au trait, les Noirs en bas quand c'est aux Noirs de jouer (si
  coché), le dernier coup (si coché) et une légende sous l'échiquier, qui commence par *Trait aux Blancs* et s'écrit
  comme tu veux (*Les Blancs jouent et gagnent*). Aux couleurs de l'application, ou en noir et blanc pour
  l'impression. Le PNG fait 480 pixels de large, ou le double pour un écran dense ; le SVG n'a pas de taille et se
  dessine net à n'importe laquelle. *Copier l'image*, ou <kbd>Ctrl</kbd>+<kbd>Maj</kbd>+<kbd>C</kbd> de n'importe où
  sur la page Échiquier (et *Copier le diagramme* dans la palette de commandes), met le PNG dans le presse-papiers,
  pour un message ou un document.
- **GIF animé** : la partie aux couleurs de l'application, d'un coup à un autre (*Seulement de … à …* pour un
  passage), une image par coup à la vitesse choisie, la dernière tenue plus longtemps ; les noms des joueurs et le résultat dans un
  bandeau, le coup sous l'échiquier, le dernier coup marqué et les flèches de la partie, selon ce qui est coché. Il est
  fait en arrière-plan, avec sa progression et *Annuler*, et son poids est estimé avant : au-delà de 2 Mo, la feuille
  dit qu'il est lourd pour un message ou un forum, et propose un passage ou une taille plus petite.
- **Impression · PDF** : un PDF fait par RookStudio, le même sur tous les systèmes, son aperçu étant la page exacte.
  *Ouvrir pour imprimer* (Entrée, <kbd>Ctrl</kbd>+<kbd>P</kbd>) le donne au lecteur du système, qui imprime ;
  *Enregistrer le PDF…* le garde là où tu le choisis. Le papier est A4 ou Letter, selon la langue et le pays du système,
  ou comme choisi. Une partie s'imprime sur une ou deux colonnes, ses en-têtes en haut de la page, avec des diagrammes
  à ses moments clés (les erreurs du bilan, sinon les coups marqués !!, ??, ? et !), à chaque commentaire avec des
  flèches ou des cases (ou `[#]`), tous les 10 coups, ou aucun ; les commentaires de l'assistant et les évaluations à
  côté des coups quand c'est coché. Son texte peut être sélectionné et copié : ses polices sont incorporées, Inter, et
  la police chinoise du système quand la page a des caractères chinois. Quand le système n'a pas de lecteur de fichiers
  PDF, le PDF est enregistré et son dossier s'ouvre.
- **PGN**, depuis la page Bases : la partie choisie ou les parties du filtre. *Écrire les noms dans les parties*
  (désactivé au départ, les parties telles qu'elles ont été lues) écrit White et Black comme les a la table des noms de
  la base (voir [Noms des joueurs](#noms-des-joueurs)), avec les identifiants FIDE et les titres des joueurs quand elle
  les connaît ; la base elle-même ne change pas.

Ce qu'un export montre nomme les joueurs comme le fait la table des noms, comme les montre la liste des parties. Les
pages HTML et les PDF se terminent par la mention des pièces dessinées (*Cburnett*, licence BSD 3-clause ;
*Chessnut*, Apache 2.0 ; *Celtic* et *Fantasy*, MIT) ; le PNG d'un diagramme la porte dans son texte, son SVG dans un
commentaire.

*Imprimer…* est aussi là où il y a quelque chose à lire sur papier : un répertoire sur la page Ouvertures (chaque ligne
sous celle qu'elle quitte, un diagramme à chaque embranchement, tes notes, et sa couverture et ses lacunes en annexe,
quand elles sont mesurées ; un répertoire vide n'a rien à imprimer, ses coups à marquer d'abord d'une étoile dans
l'onglet Arbre), le dossier d'un joueur, un rapport d'ouverture et le tableau croisé d'un tournoi (sur des pages en
paysage, chaque joueur listé). Le dossier, le rapport et le tableau croisé ont aussi *Page HTML* : le même contenu en
page à part, leurs lignes rejouables sur un échiquier à côté du texte. L'Échiquier de l'espace Moteurs garde ses
propres exports (*Copier le PGN*, *Enregistrer la partie en PGN…*).

## Bases
<!-- en: databases e5980ed0 -->

La page **Bases** contient tes bases de parties : leur liste à gauche, les parties de celle choisie à droite. Une base
est un dossier à part (`Name.fcgdb`), lu vite même avec des millions de parties.

### Importer des parties
<!-- en: importing-games 8ebdd041 -->

*Nouvelle base* en crée une, nommée sur place ; *Ajouter une base…* liste un dossier de base créé ailleurs, ses
fichiers laissés où ils sont. *Importer dans cette base…* prend un fichier PGN, ou un fichier `.zst`, `.gz` ou `.zip`
qui en contient un ; un fichier déposé sur la page va dans la base choisie, ou dans une nouvelle base qui porte son
nom, et *Coller des parties* importe le texte PGN du presse-papiers. L'import tourne en arrière-plan avec une faible
priorité (un tournoi à côté ne perd pas de temps), avec sa progression, sa vitesse et *Annuler*. Les parties
enregistrées restent s'il s'arrête (toutes les 262 144 parties, et à la fin), et importer de nouveau le fichier
n'ajoute que celles qui manquent ; les parties déjà dans la base sont écartées.

À la fin, un bandeau sur la base dit ce qui est arrivé : les parties ajoutées, celles déjà présentes, et celles
endommagées parmi les parties ajoutées (un coup illisible ou illégal, un texte coupé, une position de départ qui n'en
est pas une), chacune gardée jusqu'à ce coup avec son texte tel quel ; *Les voir dans la liste*, ou un clic sur l'une
d'elles, montre le fichier à sa ligne. Les parties déjà là, venues d'une autre source, la même partie avec ses en-têtes
écrits autrement, sont signalées avec *Les fusionner* et *Les voir d'abord* (voir [Parties en
double](#parties-en-double)). Un disque plein, un arrêt demandé ou une autre erreur dit ce qui est gardé et ce
qu'il faut, avec *Reprendre l'import* et *Ouvrir le dossier de la base* ; un fichier dont la lecture ne peut pas
continuer dit pourquoi, les parties lues avant étant gardées. Puis l'arbre d'ouvertures de la base se construit, en
arrière-plan.

Un fichier dont les octets ne sont pas du texte (un programme, une image, une archive que l'import ne connaît pas) est
refusé avant d'être lu comme un PGN : rien n'est importé, et une nouvelle base créée pour lui n'est pas gardée.

Une base dans laquelle un autre programme écrit (un autre RookStudio qui y importe, par exemple) s'ouvre en lecture
seule : la liste, l'arbre et les recherches fonctionnent ; l'import et l'enregistrement de parties attendent que ce
programme ait fini. *Retirer de la liste* laisse les fichiers d'une base où ils sont.

### La liste des parties
<!-- en: the-game-list 7dc68173 -->

La liste donne pour chaque partie son numéro, ses joueurs, leur Elo, son résultat, son événement, sa date, son code
ECO et sa longueur ; un clic sur un titre de colonne la trie. Les graphies d'un joueur réunies dans la table des noms
de la base (voir [Noms des joueurs](#noms-des-joueurs)) s'affichent sous le nom du joueur, et sous la souris le nom
dit comment la partie l'écrit. Le filtre rapide au-dessus prend des mots libres : joueurs, événement, année, code
ECO, résultat (`carlsen 2024 B90`). ↑ ↓ parcourent la liste et la partie choisie s'affiche à côté, sur un petit
échiquier avec ses coups ; Entrée ou un double clic l'ouvre sur la page Échiquier. Des millions de parties défilent et
se trient aussitôt. **Parties** et **Tournois**, à côté du nom de la base au-dessus de la liste, passent de ses parties
à ses tournois (voir [Tournois et tableaux croisés](#tournois-et-tableaux-croisés)). *Exporter…*, après le nombre de
parties, fait de la partie choisie ou des parties du filtre une page à rejouer, un GIF animé, un PDF ou un fichier
PGN, les noms des joueurs de la table des noms écrits dedans si on le demande (voir [Exporter et
imprimer](#exporter-et-imprimer)).

### Fiches FIDE
<!-- en: fide-cards 20b6e988 -->

La **fiche FIDE** d'un joueur (titre, fédération, année de naissance, identifiant FIDE, classements standard, rapide et
blitz) est demandée à Lichess, qui tient la liste de la FIDE, sans compte et seulement quand tu le demandes : *Fiche
FIDE de …*, dans le menu du clic droit sur un nom de la liste des parties ou dans l'aperçu de la partie. Une partie qui
donne l'identifiant FIDE du joueur (*WhiteFideId*, comme le font les diffusions de Lichess) envoie cet identifiant ;
sinon, le nom est envoyé, et quand plusieurs joueurs de la liste de la FIDE s'appellent ainsi, tu choisis lequel c'est.
La fiche est gardée dans ton profil pour ce nom : elle s'affiche ensuite sous la souris sur le nom, et dans l'aperçu.
*Sur lichess.org* y ouvre la page du joueur, *Dossier de …* son dossier (voir [Le dossier d'un
joueur](#le-dossier-dun-joueur)). Un joueur rapproché de la liste de classement FIDE dans la table des noms de la base
montre à la place la fiche de la liste, sans demander à Lichess.

### Le dossier d'un joueur
<!-- en: a-players-dossier 3e1a2bc7 -->

Le **dossier** d'un joueur rassemble ce que tes bases savent de lui. Il s'ouvre depuis son nom : *Dossier de …* dans
le menu du clic droit sur un nom de la liste des parties, dans l'aperçu d'une partie ou sur une fiche FIDE ; Ctrl+K,
*Dossier : Duval* ; *Préparer une partie contre* de la page Lichess (voir [Préparer une
partie](#préparer-une-partie)). Il s'affiche à la place de la page, Échap revenant en arrière. Il est refait à chaque
ouverture, d'après tes bases telles qu'elles sont, en arrière-plan à basse priorité avec sa progression et *Annuler*
(quelques secondes au plus, même pour un joueur de milliers de parties parmi des millions) ; rien n'en est gardé.

Ses parties sont trouvées par la table des noms de chaque base (voir [Noms des joueurs](#noms-des-joueurs)) : toutes
les graphies d'un joueur réunies, dans chacune de tes bases sauf celles des parties de joueurs Lichess (*Lichess ·
nom* : celles de son compte sont la source Lichess). Un joueur rapproché de la liste de classement FIDE se distingue de
ses homonymes par l'identifiant FIDE que donnent les parties, et les parties jouées avant qu'il ait pu jouer sont
écartées. Quand la liste de la FIDE a plusieurs joueurs de ce nom, le dossier demande lequel, celui dont le classement
correspond à ses parties proposé en premier : ↑ ↓ choisissent, Entrée garde le choix dans les tables des noms, et le
dossier ne prend alors que les parties de ce joueur (Ctrl+Z annule ; *Pas maintenant* montre toutes les parties de ce
nom).

En haut : le titre du joueur, sa fédération, son année de naissance et ses classements FIDE quand il est rapproché de
la liste, et ce dont il est fait : tes bases, avec leurs parties et leurs années, et Lichess quand sa source est cochée.
*Ses parties dans la liste* les montre sur la page Bases ; *Imprimer…* fait du dossier un PDF de quelques pages, *Page
HTML* une page à part dont les lignes se rejouent sur un échiquier (voir [Exporter et
imprimer](#exporter-et-imprimer)). Puis quatre onglets (Ctrl+Page suiv. et Ctrl+Page préc. passent de l'un à
l'autre) :

- **Vue d'ensemble** : ses premiers coups avec les Blancs et son score, ses réponses avec les Noirs aux premiers coups
  qu'il rencontre le plus, son classement au fil des années, ses résultats contre des joueurs plus forts et plus
  faibles, ses structures de pions, et tes parties contre lui (tes noms sont ceux de *Réglages › Tes noms*) ; puis ses
  dernières parties. Un clic en ouvre une sur la page Échiquier avec le dossier comme liste : Ctrl+← et Ctrl+→
  parcourent ses parties, et *Revenir au dossier* y ramène ;
- **Ouvertures** : ses lignes avec les Blancs et avec les Noirs, par code ECO, chacune avec ses parties, ses gains,
  nulles et pertes, son score et la dernière année où il l'a jouée. Un clic ouvre la ligne sur la page Échiquier, son
  onglet Arbre sur ses seules parties, de son côté (*Duval, Anne avec les Blancs*). En dessous, les lacunes de tes
  répertoires contre lui : les lignes qu'il joue trois fois ou plus et que tes répertoires laissent sans réponse, les
  plus fréquentes d'abord ;
- **Classement et structures** : son classement année par année, la moyenne de l'Elo que donnent ses parties
  classiques chaque année (pas de courbe sous trois ans), son plus haut et ce que dit la liste de la FIDE ; ses
  structures de pions au 20e coup, telles que les nomment les faits de l'assistant (aucune sous cinq parties), un clic
  montrant ces parties dans la liste avec la recherche Structure partant de ses pions ; et quand tu as fait le bilan de
  parties à lui (Alt+G), sa précision dans chaque phase et où tombent ses erreurs nettes ;
- **Lichess**, avec sa source : ce qu'il joue sur l'échiquier et en ligne côte à côte, son niveau en ligne, tes parties
  contre lui des deux côtés, et les lacunes de tes répertoires sur les deux sources.

*+ Lichess…* lui associe son compte Lichess, le lien gardé dans les tables des noms, et coche la source : ses parties
de la dernière année arrivent comme pour une partie préparée contre lui ; un clic sur la pastille de la source la
retire, ou la remet. Sous vingt parties, le dossier dit qu'il donne des indices plutôt que des tendances. Un joueur
sans partie dans tes bases est signalé, avec *Depuis une source…* et *+ Lichess…* pour en faire venir. Rien ne quitte
la machine, sauf, avec la source Lichess cochée, ce qu'envoie la préparation d'une partie : son nom avec les filtres de
ses parties, et son profil public. Hors ligne, la source montre les parties de sa dernière synchronisation, datées.
Pendant que tu joues contre lui sur Lichess, le dossier ne montre rien de lui.

### Tournois et tableaux croisés
<!-- en: tournaments-and-crosstables 208330d7 -->

*Tournois*, à côté de *Parties* au-dessus de la liste d'une base, montre ses tournois : les parties d'un même
événement, de son lieu et de son année (un tournoi de décembre à janvier reste un seul tournoi ; le même événement en
deux lieux, avec des joueurs à eux, en fait deux). Les parties en ligne, qui n'ont en commun aucun événement dont les
rondes sont indiquées, ne sont dans aucun : une base de telles parties dit qu'elle n'a pas de tournoi et propose
*Parties*. Chaque tournoi donne son système, ses joueurs, ses rondes, l'Elo moyen de ses joueurs et ses parties,
*286 sur 288* en orange quand des parties manquent. La liste est triée par Elo moyen ; un clic sur un en-tête la trie
autrement, et son filtre (Ctrl+L) prend des mots de l'événement, du lieu ou de l'année. La liste d'une grande base se
fait en arrière-plan avec sa progression (quelques secondes pour dix millions de parties), et elle est gardée jusqu'à
ce que la base change.

Le système est deviné d'après les parties : toutes rondes quand chacun a joué contre chacun (toutes rondes
aller-retour quand chaque paire s'est rencontrée deux fois), suisse sinon ; *?* quand ce n'est pas sûr (une paire s'est
rencontrée plus souvent que son système ne le permet, des parties manquent dans un groupe aussi petit que celui d'un
toutes rondes). Les compétitions par équipes et les éliminations directes sont signalées, leurs tableaux croisés pour
plus tard. Un clic sur le système le change, gardé pour le tournoi.

Un clic sur un tournoi, ou Entrée, ouvre son **tableau croisé** à la place de la liste, Échap revenant en arrière ;
Ctrl+K en ouvre un aussi (*Tableau croisé : Open d'automne*). Ses joueurs sont ceux de la table des noms de la base
(voir [Noms des joueurs](#noms-des-joueurs)) : toutes les graphies d'un joueur réunies. Les parties venues deux fois de
deux sources (les mêmes joueurs avec les mêmes couleurs, des rondes qui peuvent n'en faire qu'une) sont comptées une
fois, et la ronde de chaque partie est lue dans son étiquette Round (*9.2* : ronde 9, échiquier 2).

- Le tableau croisé d'un toutes rondes a une case pour chaque adversaire : 1, ½ ou 0 (un résultat par cycle dans un
  toutes rondes aller-retour), + et − pour un forfait ; puis les points, les départages, les victoires, la performance
  et la variation de classement.
- Celui d'un suisse a les numéros de départ, par classement, puis une case pour chaque ronde : *4n½* est une nulle
  contre le numéro 4, avec les Noirs ; *exempt*, le seul joueur non apparié de la ronde (les points d'un
  gain, la règle de la FIDE pour une exemption attribuée par l'appariement) ; *–*, une ronde sans partie. Les dix
  premiers s'affichent, leurs parties entre eux dans la couleur d'accent ; *Tous les joueurs* montre les autres.

Un clic sur une partie l'ouvre sur la page Échiquier, les parties du tournoi comme liste (Ctrl+← et Ctrl+→, *Revenir
au tableau croisé*) ; un clic sur un nom ouvre le dossier du joueur (voir [Le dossier d'un
joueur](#le-dossier-dun-joueur)). La performance et la variation de classement disent sous la souris comment elles ont
été comptées. *Imprimer…* fait du tableau croisé un PDF sur des pages en paysage, chaque joueur listé, et *Page HTML*
une page à part (voir [Exporter et imprimer](#exporter-et-imprimer)).

Au-dessus du tableau croisé, une ligne pour chaque chose qui ne va pas dans les données, avec ce qui la corrige :

- **des parties manquantes** (une paire d'un toutes rondes qui ne s'est pas rencontrée, deux joueurs d'un suisse sans
  partie dans une ronde) sont comptées « ? », et les points, les départages et les variations de classement qui les
  lisent disent « ? ». *Les chercher dans tes autres bases* les trouve sous d'autres graphies, *La copier dans ce
  tournoi* en ajoute une, son événement, son lieu et ses joueurs écrits comme ceux du tournoi (Ctrl+Z la retire) ;
  *Fixer les résultats à la main* donne à chacune son résultat, comme le fait un clic sur un « ? », gardé avec le
  tournoi dans ton profil, la base inchangée ;
- **un joueur sous deux noms** : deux noms qui peuvent être ceux d'un même joueur, jamais appariés ensemble, leurs
  rondes séparées ; *Les réunir* les joint dans la table des noms (Ctrl+Z annule) ;
- **des parties venues deux fois** : *Les fusionner* garde chacune en une seule partie, avec les champs les plus
  complets de chaque source, comme le font les doublons de la maintenance (Ctrl+Z annule) ; celles dont les coups ou
  les résultats diffèrent sont montrées dans la liste à la place ;
- **un forfait** : un résultat décisif sans coups (un PGN n'a pas de mot pour cela) compte dans les points, ni dans la
  performance ni dans la variation de classement ;
- **un système pas sûr** : un clic le choisit.

Une diffusion suivie a aussi son tableau croisé : *Tableau croisé*, à côté du classement dans la colonne de la
mosaïque, le montre à la place des échiquiers (*La mosaïque*, ou Échap, les ramène). Il est fait des rondes que
RookStudio suit et de celles qu'il a gardées dans la base *Diffusions* : ● une partie en cours (un clic la suit sur la
page Échiquier), · une partie à venir, les points, les parties jouées et la variation de classement jusqu'ici. Le
classement propre de Lichess reste à côté, et rien de plus n'est demandé à Lichess pour lui ; une diffusion par équipes
garde le classement par équipes de Lichess.

### Départages, performance et variation de classement
<!-- en: tie-breaks-performance-and-rating-change 18737372 -->

Les départages ordonnent les joueurs à égalité de points, l'un après l'autre : ceux de la FIDE par défaut, la ligne
au-dessus du tableau disant lesquels. Le règlement actuel des départages de la FIDE (C.07, en vigueur depuis le
1er mars 2026) n'en recommande aucun pour un système, aussi RookStudio prend-il ceux des propres compétitions de la
FIDE : pour un toutes rondes, comme le Tournoi des candidats 2026, Sonneborn-Berger, les victoires, la confrontation
directe ; pour un suisse, comme le Grand Swiss 2025, le classement moyen des adversaires tronqué 1, Buchholz tronqué 1,
Buchholz, la confrontation directe (le classement moyen écarté quand un joueur n'a pas de classement, comme le demande
C.07). Un clic sur un départage, dans cette ligne ou sur son en-tête, le met en premier, le déplace, le remplace ou le
retire ; *+* en ajoute un ; *Ceux de la FIDE* remet les leurs. L'ordre est gardé pour le tournoi.

Ce sont ceux de la FIDE : confrontation directe, victoires (les forfaits et les exemptions qui valent un gain
comptent), parties gagnées, parties avec les Noirs, victoires avec les Noirs, score cumulatif, Buchholz, Buchholz
tronqué 1, Buchholz médian, Sonneborn-Berger, Koya, le classement moyen des adversaires (aussi tronqué 1) et la
performance. Dans un suisse, une ronde sans partie compte dans le Buchholz et le Sonneborn-Berger comme le dit la FIDE
(C.07, article 16) : comme contre un adversaire qui a le score du joueur lui-même, au plus une nulle par ronde (au plus
le score de l'adversaire prévu pour un forfait), et les rondes d'un joueur après son retrait comptent comme des nulles
pour ses adversaires.

La **performance** est celle de la FIDE : le classement moyen des adversaires classés rencontrés sur l'échiquier, plus
l'écart que la table de la FIDE associe au score dans ces parties (arrondi au centième). La **variation de
classement** suit la formule de la FIDE : pour chaque partie jouée sur l'échiquier contre un adversaire classé, le
score moins le score attendu d'après la table de la FIDE (un écart compté 400 au plus, sous 2650), multiplié par K. K
est celui du joueur sur la liste de classement de la FIDE quand il y est rapproché dans la table des noms et que la
liste est de l'année du tournoi ou de la suivante (voir [La liste de classement FIDE](#la-liste-de-classement-fide)) ;
sinon les règles de la FIDE le donnent : 40 jusqu'à la fin de l'année de ses 18 ans tant qu'il est sous 2300, 10 à
partir de 2400, 20 sinon, moins quand 700 serait dépassé sur l'ensemble de ses parties. Un tournoi rapide ou blitz suit
les règles rapide et blitz de la FIDE. Un forfait ne compte que dans les points.

### Rechercher
<!-- en: searching fda4d044 -->

*Rechercher* (Ctrl+F) ouvre le panneau de recherche à côté de la liste, en six onglets (Ctrl+Page suiv. et Ctrl+Page
préc. passent de l'un à l'autre) ; la liste suit chaque critère à mesure qu'il est tapé :

- **En-têtes** : un joueur et sa couleur, l'adversaire, l'événement, les années, les codes ECO, l'Elo minimal des deux
  joueurs, le résultat ; *Parties endommagées seulement* ;
- **Position** : la position de la page Échiquier, ou une position mise en place (*Placer les pièces…*) : ses parties
  sont trouvées en arrière-plan, celles qui ne peuvent pas l'atteindre (leurs pions, leur matériel) étant écartées sans
  être rejouées ;
- **Matériel** : un préréglage (finales de tours, sans dames, la paire de fous contre fou et cavalier, une dame contre
  deux tours) ou les pièces de chaque camp comptées (`2`, ou `0-2`), pendant au moins tant de demi-coups ;
- **Motif** : des pièces posées sur l'échiquier à la souris (un clic pose la pièce choisie, un nouveau clic l'enlève ;
  Maj et un clic, une case vide ; les autres cases, n'importe quoi), *Les Blancs n'ont que ces pions*, trois exemples,
  et des conditions au-delà : ce qui attaque, défend ou cloue quoi, une ligne de coups (voir [Motifs](#motifs)) ;
- **Manœuvre** : le trajet d'une pièce, comme un cavalier qui va en b1, d2, f1, g3 (voir [Manœuvres](#manœuvres)) ;
- **Structure** : les pions de la page Échiquier, exactement ou à un ou deux pions près (voir [Structures de
  pions](#structures-de-pions)).

Chaque recherche donne ses parties et son temps ; *Ajouter au filtre* et *Retirer du filtre* les combinent, *Voir dans
la liste* (Entrée) montre le résultat, *Tout effacer* recommence. Les recherches réglées s'additionnent : un motif, et
un matériel, et des en-têtes. Une recherche qui rejoue les parties (un motif, un trajet, une structure) continue en
arrière-plan avec sa progression et *Annuler* ; les premières parties qu'elle trouve sont déjà dans la liste, et une
recherche annulée les garde, *Reprendre (le reste seulement)* cherchant dans les parties qu'elle n'avait pas
atteintes. Pendant un import dans la base, une recherche couvre les parties déjà enregistrées, et se refait d'elle-même
à la fin.

La liste montre alors chaque partie à la position trouvée, dans une colonne *Trouvée à* (le coup qui y a mené) : Entrée
ou un double clic ouvre la partie à ce coup, comme le font la partie suivante ou précédente de la liste depuis la page
Échiquier.

*Garder cette recherche…* la nomme d'après ses critères (un motif d'après le nom de son préréglage). Ce sont ses
critères qui sont gardés, pas ses parties, pour n'importe quelle base (ses nouvelles parties sont trouvées la fois
suivante) ; les recherches gardées s'affichent en haut du panneau, où un clic applique leurs critères et le bouton
droit en renomme ou en supprime une, et dans Ctrl+K (*Recherche : …*, ou *Motif : …* pour un motif seul).

### Motifs
<!-- en: patterns 5821a112 -->

L'onglet **Motif** dit ce qui est vrai dans une position : d'abord les pièces du petit échiquier, puis des conditions,
une par ligne, toutes vraies à la fois. Un clic sur un mot d'une ligne le change : les pièces (un cavalier blanc, la
dame noire ou une tour noire, n'importe quelle pièce noire, sur des cases ou n'importe où), le verbe (*est sur*,
*attaque*, *défend*, *cloue … sur …*, *est en prise*, *est passé*, et leur contraire), le camp au trait, l'échec, le
mat. *+ Une condition* en ajoute une, × la retire. *La même pièce qu'à la ligne 1*, dans le menu des pièces, fait
parler deux lignes d'une même pièce : un cavalier qui attaque le roi, et le même cavalier qui attaque la dame ou une
tour, c'est une fourchette.

Les préréglages fixent un motif entier : *Fourchette de cavalier*, *Clouage sur le roi*, *Mat du couloir*, *Attaque
double de la dame*, *Sacrifice en h7*, *Pion passé protégé* ; ils sont aussi dans Ctrl+K (*Motif : Fourchette de
cavalier*), sur la base choisie.

*puis… (une ligne)* ajoute une ligne de positions après la première : chaque étape un coup (« le fou blanc prend en h7
avec échec ») ou une position, au demi-coup suivant ou en tant de demi-coups au plus (*chaque étape au demi-coup
suivant*, *en 3 demi-coups au plus*). Un coup peut aussi se jouer sur le petit échiquier (*+ Une étape*, *un coup joué
sur le petit échiquier* : un clic sur la pièce, puis sur sa case). Dans chaque partie, la position trouvée est la
première du motif.

Ce qui ne peut jamais être trouvé est dit avant de chercher, et rien n'est lancé : un cavalier en c7 et c7 vide, les
Blancs et les Noirs au trait, un pion sur sa première rangée ; un clic retire l'une des deux conditions. Une recherche
qui trouve plus de la moitié des parties le dit (*Très large*) : *Ajouter une condition*, ou *La garder telle quelle*.

### Le langage des motifs
<!-- en: the-pattern-language af783ff9 -->

Le même motif est un texte sous le constructeur (*Texte*, ou Alt+T) : changer l'un change l'autre, et une recherche
gardée garde son texte, qui peut être copié et partagé. Ses mots sont ceux de CQL, le langage des requêtes d'échecs :

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

- Les pièces en lettres FEN : K Q R B N P pour les Blancs, k q r b n p pour les Noirs, A et a pour n'importe quelle
  pièce blanche ou noire, `_` pour une case vide, `[qr]` pour l'une de plusieurs ; les cases après la lettre (`Nf3`,
  `R[a1,h1]`, `p[a-h7]`), ou seules là où un mot attend des cases (`move to [g5,h5]`).
- `attacks`, `defends`, `pin from … through … to …`, `en prise`, `passed`, `check`, `mate`, `stalemate`, `wtm` et
  `btm` (le camp au trait), `not`, `or`, `only P` (les Blancs n'ont pas d'autres pions que ceux nommés sur leurs
  cases).
- `line -->` puis une étape après chaque flèche, au demi-coup suivant : `move from … to … capture … promote …` et ce
  qui est vrai après le coup, ou une position ; `.{0,2}` avant une étape laisse passer d'abord deux demi-coups
  quelconques.
- Deux noms (`$n`, `$q`) sont toujours deux pièces ; un nom suit sa pièce à travers les coups d'une ligne.

Une ligne qui ne se lit pas est nommée, avec le mot auquel elle ressemble (*Ligne 2 : « atacks » n'est pas un mot
connu — « attacks » ?*) ; le reste de CQL est signalé comme non lu. Le constructeur garde la dernière version qui se
lisait, et rien n'est cherché tant que le texte ne se lit pas de nouveau.

### Manœuvres
<!-- en: manoeuvres 9ac8f6fb -->

L'onglet **Manœuvre** cherche le trajet d'une pièce : un clic sur une pièce du petit échiquier (la position de la page
Échiquier), puis un clic sur chaque case où elle va (b1, d2, f1, g3) ; Retour arrière retire la dernière case.
*D'autres coups entre* laisse le camp jouer d'autres coups en attendant ; *La même pièce tout du long* demande une
seule pièce, sinon n'importe quelle pièce de son espèce sur chaque case à tour de rôle ; *En 15 coups au plus* compte
les coups du camp du premier du trajet à son dernier. *Depuis n'importe quelle position*, ou *Depuis la position de
la page Échiquier* : le trajet une fois qu'une partie atteint cette position.

Les résultats disent où la même pièce est allée ensuite (en f5 dans 31 % des parties), et les trajets voisins, qui se
séparent du sien après son premier coup (b1–d2–b3) ; un clic en applique un. La flèche grise sur l'échiquier est la
suite la plus jouée, pas une condition. Depuis la page Échiquier, Ctrl+K *Cette manœuvre* prend la pièce sous la
souris et ses coups dans la partie.

### Structures de pions
<!-- en: pawn-structures 5c6eb547 -->

L'onglet **Structure** cherche les seuls pions de la page Échiquier, les pièces mises de côté : *Exactement*, *À un
pion près* ou *À deux pions près* (un pion de plus, de moins ou sur une autre case, d'un côté ou de l'autre, compte
pour un pion), *Les deux camps*, ou *Les pions blancs* ou *Les pions noirs* seuls, *Les couleurs inversées aussi*,
tenue au moins tant de demi-coups. *Reprendre les pions de la page Échiquier* les reprend à nouveau ; *Placer les
pions…* les change case par case. Son nom s'affiche quand les faits de l'assistant le connaissent (Carlsbad, Maroczy,
hérisson, Stonewall, les chaînes de pions, Benoni, pion dame isolé).

Les résultats donnent les parties qui la tiennent exactement et celles qui en sont proches, les ouvertures qui y
mènent le plus, et les variantes à un pion près avec leurs parties et le score des Blancs (« le pion b blanc en b4 »,
« sans le pion a noir ») ; un clic en applique une. Depuis la page Échiquier, Ctrl+K *Parties avec cette structure*
cherche aussitôt ses pions.

### Tournois et fichiers PGN
<!-- en: tournaments-and-pgn-files 688ddcc2 -->

Les tournois du profil, et le fichier PGN ouvert sur la page Échiquier, s'affichent sous *Lus sans copie* après tes
bases : leurs parties listées et cherchées comme celles d'une base, mises à jour en arrière-plan à l'affichage (les
parties qu'un tournoi termine, un fichier qui a grandi ou changé). Ils ne sont ni importés ni supprimés.

### Bases ChessBase
<!-- en: chessbase-databases 3da99106 -->

Une base ChessBase déposée sur la page Bases, ou choisie avec *Importer un fichier…*, est copiée dans une base
RookStudio : son .cbh ou son .2cbh (ou n'importe lequel de ses fichiers), ou une archive qui en contient une, .cbv ou
.cbz pour un .cbh, .2cbv ou .2cbz pour un .2cbh. Une carte dit d'abord ce qu'elle contient, lu dans ses en-têtes, ce qui
entre et ce qui reste dehors, où elle va et combien de temps cela prend environ ; *Importer* (Entrée) lance la copie,
*Pas maintenant* (Échap) la laisse. Les fichiers ChessBase sont seulement lus, rien n'est écrit à côté d'eux ; une
archive est décompressée pendant sa lecture. Le mot de passe d'un .cbz ou d'un .2cbz se tape sur la carte (Entrée dans
le champ lance la copie) et n'est pas gardé ; un mauvais mot de passe y est signalé aussitôt, pour être retapé, avant
que rien ne soit copié.

La copie garde les coups et leurs variantes, les commentaires avant et après les coups (un texte en plusieurs
langues : l'anglais d'abord, les autres après lui), les symboles, les cases et flèches colorées, les pendules et le
temps passé, les évaluations des moteurs, les médailles (une balise *Medals*), les positions critiques et les cadences.
Les questions d'entraînement, les images, sons et vidéos, les liens web, les parties citées, les structures de pions
et trajets de pièces, les couleurs de variantes et les textes (articles) restent dehors : le rapport de fin les compte
par type. Importée de nouveau (après l'ajout de nouvelles parties dans ChessBase), la base n'apporte que ses nouvelles
parties. Les noms des joueurs sont écrits comme la base les écrit, « Nom, Prénom ».

### TWIC
<!-- en: twic 56b70a1d -->

*Depuis une source…* apporte des parties d'un site, téléchargées seulement quand tu le demandes. **TWIC**, The Week in
Chess, publie chaque lundi un numéro des parties de la semaine : sa feuille demande à theweekinchess.com sa liste de
numéros et montre les derniers, avec leur lundi, leurs parties et si chacun est déjà dans la base choisie. Coches-en
quelques-uns, choisis la base (*Une nouvelle base, « TWIC »* par défaut), puis *Télécharger et ajouter* : le PGN de
chaque numéro est téléchargé, puis importé, l'un après l'autre, la base restant lisible tout du long ; *Annuler*
s'arrête après ce qui a été ajouté. Une partie que la base a déjà, mêmes balises et mêmes coups, n'est pas ajoutée deux
fois. Ensuite, un rapport donne les tournois les plus forts des parties ajoutées (le classement moyen de leurs joueurs)
et les joueurs les mieux classés ; un nom ouvre ses parties dans la liste. Les parties déjà là, venues d'une autre
source, une diffusion suivie auparavant par exemple, y sont signalées avec *Les fusionner*.

*Chaque lundi, chercher un nouveau numéro* est désactivé tant que tu ne le coches pas. Coché, RookStudio demande la
liste des numéros pendant qu'il est ouvert, quatre fois par jour au plus ; un nouveau numéro s'affiche sous la liste
des bases, dans *Sources suivies*, et sur la carte *Mes bases* de la page Accueil, avec *Ajouter à « … »* : il n'est
jamais ajouté sans toi. Ce qui quitte la machine : la demande de la liste des numéros, puis des fichiers PGN des
numéros choisis, rien d'autre. TWIC est gratuit pour un usage personnel ; rien de lui n'est livré avec RookStudio.

### Les mois de Lichess
<!-- en: lichess-months 26060e4f -->

*Depuis une source… › Diffusions Lichess* ajoute un mois des diffusions officielles de Lichess (database.lichess.org,
CC BY-SA 4.0) : sa feuille demande la page des fichiers du site et liste les derniers mois avec leur taille et leurs
parties. Choisis-en un et la base (*Une nouvelle base, « Diffusions Lichess »* par défaut), puis *Télécharger et
ajouter* : le fichier du mois est téléchargé, avec reprise possible, puis importé, et un rapport donne ses tournois les
plus forts et ses joueurs les mieux classés. Une partie que la base a déjà n'est pas ajoutée deux fois. Les parties
gardent leur source, la page de la diffusion dans leur balise Site : ce que tu en partages est partagé sous la même
licence. Ce qui quitte la machine : la demande de la page, puis du fichier du mois.

*Depuis une source… › Parties Lichess* lit un mois des parties classées de Lichess (CC0, environ 95 millions de
parties et 30 Go compressés) pendant son téléchargement, et ne garde que les parties qui passent ses filtres : par
défaut, les deux joueurs classés 2400 et plus, sans le bullet (un temps de base plus 40 incréments sous trois minutes).
Seules les parties gardées atteignent le disque, ajoutées par lots à la base choisie, la base restant lisible tout du
long. *Lire un fichier déjà téléchargé…* lit à la place un `lichess_db_standard_rated_*.pgn.zst` du disque. *Annuler*,
une coupure du réseau ou la fermeture du programme garde ce qui a été ajouté, et *Continuer* reprend là où la lecture
s'est arrêtée, avec les mêmes filtres ; les parties relues ne sont pas ajoutées deux fois.

### Un joueur de chess.com
<!-- en: a-chesscom-player 39157248 -->

*Depuis une source… › chess.com* ajoute les parties d'un joueur depuis l'API publique de chess.com, sans compte : tape
son nom, et *Chercher* demande les mois où il a joué ; *Télécharger et ajouter* demande les parties de chaque mois, un
mois à la fois, et les importe dans la base choisie (*Une nouvelle base, « chess.com · nom »* par défaut), avec le même
rapport. Avec *Suivre* coché, le joueur s'affiche sous la liste des bases, dans *Sources suivies* : *Chercher de
nouvelles parties* redemande les mois pas encore ajoutés et le plus récent (ses parties ont pu augmenter) ; une partie
déjà là n'est pas ajoutée deux fois. Ce qui quitte la machine : le nom du joueur et les demandes de ses mois, à
api.chess.com.

### Lumbra's Gigabase
<!-- en: lumbras-gigabase 19d9d71f -->

*Depuis une source… › Lumbra's Gigabase* explique comment l'obtenir : son site te demande d'accepter sa licence (CC
BY-NC 4.0) et de montrer que tu n'es pas un robot, ce que RookStudio ne fait pas pour toi. *Ouvrir son site* l'ouvre
dans ton navigateur ; une fois le fichier téléchargé, *Importer le fichier téléchargé…* l'importe tel quel. Une
archive .7z (LZMA ou LZMA2) est lue pendant sa décompression : ses fichiers PGN l'un après l'autre, ses autres
fichiers passés, rien n'étant écrit sur le disque que les parties. N'importe quel .7z de fichiers PGN peut être
importé, ou déposé sur la page, de la même façon.

### Maintenance
<!-- en: maintenance 9c828c81 -->

Le ⋯ d'une base, à côté de ses boutons sur la page Bases, ou un clic droit sur elle dans la liste, ouvre sa maintenance
à la place de la page, en trois parties : *Parties en double*, *Noms des joueurs*, *Vérifier et compacter* ; Échap
revient. Ctrl+K a *Parties en double*, *Noms des joueurs*, *Liste de classement FIDE*, *Vérifier la base* et *Compacter
la base*. Chaque tâche longue tourne en arrière-plan à basse priorité, avec sa progression et *Annuler*, la base
restant lisible tout du long : un tournoi à côté ne perd pas de temps. Ctrl+Z annule la dernière fusion, les derniers
joueurs réunis, ou les parties endommagées mises de côté. Une base dans laquelle un autre programme écrit est ouverte en
lecture seule : sa maintenance attend que ce programme la libère. La page Accueil ne parle de la maintenance que
lorsqu'elle trouve quelque chose : des doublons après un import, des parties endommagées trouvées par une vérification
ou à l'ouverture d'une base.

### Parties en double
<!-- en: duplicate-games f3c8990b -->

La même partie peut venir deux fois de deux sources, ses en-têtes écrits autrement : une ronde suivie sur Lichess,
puis le numéro de TWIC du lundi. L'import écarte une partie que la base a exactement (mêmes balises et mêmes coups),
pas celles-ci. *Parties en double* les trouve : les mêmes joueurs (lus dans la table des noms ; sinon leurs noms de
famille et l'initiale de leur prénom), la même année, des rondes et des dates qui ne diffèrent pas, et les mêmes coups,
ou les coups de l'une au début de ceux de l'autre, inachevée (une diffusion écourtée). Une partie demande au moins dix
coups pour être reconnue (vingt quand son jour n'est pas connu) ; deux parties avec chacune une adresse à elle (celles
de Lichess) sont deux parties.

Il les compte par genre : les mêmes coups avec d'autres en-têtes ; l'une plus courte, une diffusion écourtée ; des
résultats qui diffèrent, laissés à toi. *Tout fusionner* (Entrée) fusionne les deux premiers genres : chaque groupe
devient une seule partie avec le plus complet de chaque champ (un nom en entier plutôt qu'une initiale, l'événement le
plus long, la ronde qui en dit le plus, « 9.2 » plutôt que « 9 », une date qui en sait plus, les classements, le code
ECO, les identifiants FIDE, les coups les plus longs avec leurs commentaires, les pendules et les évaluations, un
résultat plutôt qu'aucun), les autres parties marquées supprimées. Un clic sur un groupe le compare champ par champ,
chaque source dans une colonne et la partie fusionnée à côté : un clic sur une case prend ce champ de cette source ;
puis *Fusionner* (Entrée), *Garder les deux* (deux parties, plus jamais proposées) ou *Groupe suivant* (↓). La partie
fusionnée garde le numéro de la plus ancienne ; son étiquette *MergedFrom* dit d'où vient chaque champ, et l'adresse
d'une diffusion écartée au profit d'un lieu reste comme son *GameURL*. Ctrl+Z rétablit les autres parties. *Voir dans
la liste* montre les doublons dans la liste des parties, où *Tout afficher* montre de nouveau toutes les parties.

À la fin d'un import, les parties déjà là, venues d'une autre source, sont signalées dans son rapport, avec *Les
fusionner* : elles ne sont jamais fusionnées sans ton clic. Les rapports des sources (*Depuis une source…*) les
signalent aussi.

### Noms des joueurs
<!-- en: players-names 6b9a8fcb -->

Un même joueur s'écrit souvent de plusieurs façons : « Lindqvist, Erik », « Lindqvist,E », « Erik Lindqvist ». *Noms
des joueurs* réunit les graphies d'un joueur dans la table des noms de la base. Les parties ne changent pas, mais la
liste des parties montre le nom du joueur, et son filtre rapide, sa recherche et ses tris, les parties de référence de
l'arbre d'ouvertures, les rapports des sources et les dossiers des joueurs le lisent. Trois nombres disent ce qui peut
être fait :

- réunis d'emblée : les parties donnent l'identifiant FIDE du joueur (les diffusions de Lichess le font), ou la liste
  de classement FIDE a un seul joueur de ce nom, ou de ce nom tel qu'un autre alphabet l'écrit (« Kowalenko » pour
  « Kovalenko ») avec des classements proches de ceux des parties ; *Réunir les …* (Entrée) le fait ;
- à confirmer : plusieurs joueurs de la liste ont ce nom. Celui dont le classement est le plus proche de celui des
  parties est proposé en premier ; ↑ ↓ choisissent parmi eux ou *Aucun d'eux*, et Entrée garde le choix et passe au
  suivant ;
- gardés tels quels : un joueur absent de la liste, un nom d'un seul mot (celui d'un compte), ou aucune liste prise ;
  deux graphies d'un même nom et prénom se réunissent à la main, avec un clic droit et *Réunir*.

Un joueur rapproché de la liste garde dans la table son identifiant FIDE, sa fédération, son titre, son année de
naissance et ses classements, même une fois la liste retirée. Ctrl+Z annule. La table fait partie de la base : elle va
avec son dossier. Les parties d'un import ultérieur rejoignent leurs joueurs d'elles-mêmes quand elles donnent
l'identifiant FIDE d'un joueur ou écrivent son nom en entier ; une autre graphie attend cette page.

### La liste de classement FIDE
<!-- en: fides-rating-list 85d6baa0 -->

La FIDE publie sa liste de classement chaque mois, environ deux millions de joueurs : leurs identifiants FIDE, noms,
fédérations, titres, classements et années de naissance. RookStudio ne la livre pas, puisque la FIDE ne lui donne
aucune licence. *Télécharger*, dans la partie *Noms des joueurs*, demande à ratings.fide.com sa liste combinée (un zip
d'environ 43 Mo) et la lit dans ton profil (environ 90 Mo), où elle reste, jamais envoyée nulle part ; *Un fichier déjà
téléchargé…* en lit un que tu as. Un téléchargement interrompu reprend où il s'est arrêté (*Reprendre*). Chaque mois
ensuite, la page Accueil dit qu'une nouvelle liste est parue, avec *La télécharger* : elle n'est jamais prise sans ton
clic. *Chercher une liste plus récente* redemande ; *La retirer* retire la liste de ton profil, les joueurs réunis
restant tels quels. Une liste dont les colonnes ont changé est refusée, la précédente restant en service, avec *Le
signaler*. Ce qui quitte la machine : la demande du fichier, quand tu le télécharges.

### Vérifier et compacter
<!-- en: checking-and-compacting d17eb6df -->

*Vérifier* (Entrée, ou Ctrl+K *Vérifier la base*) relit chaque segment de la base depuis le disque et rejoue chaque
partie : sa signature et sa clé (une empreinte de ses en-têtes et de ses coups) sont refaites et comparées à celles
enregistrées, et une partie qui se lit autrement qu'elle a été écrite est endommagée. Il dit *Saine*, ou les parties
endommagées et les fichiers d'où elles ont été importées : *Garder ce qui peut être lu* les met de côté (supprimées ;
Ctrl+Z les rétablit), et *Importer à nouveau leur fichier* les fait revenir depuis leur fichier quand il est encore là.
L'arbre d'ouvertures est comparé aux parties aussi.

Un segment dont les fichiers ne se lisent plus à l'ouverture de la base (tronqués, manquants ou modifiés pendant que
RookStudio était fermé : une erreur de disque, une copie à moitié faite, un outil de synchronisation) n'empêche pas la
base de s'ouvrir. Ses autres segments fonctionnent : la liste, l'arbre d'ouvertures et les recherches lisent leurs
parties, et les parties du segment endommagé gardent leurs numéros. La page Accueil et la page Bases le signalent, avec
*Les voir*, et *Vérifier et compacter* le dit aussitôt : le segment, combien de parties ne peuvent pas être lues, et les
fichiers d'où elles ont été importées. *Garder ce qui peut être lu* réécrit ce segment, ses parties mises de côté
(supprimées, leurs numéros gardés). Les joueurs, événements et lieux importés pour la première fois avec lui sont gardés
quand sa liste de noms se lit encore ; sinon les autres parties qui les utilisent montrent « ? » à leur place. Ses
fichiers endommagés restent jusqu'à la fermeture de RookStudio : d'ici là, *Annuler* (ou Ctrl+Z) les remet en place,
tant que rien d'autre n'est écrit dans la base. *Importer à nouveau leur fichier* fait revenir ses parties depuis leur
fichier quand il est encore là. Le compactage attend qu'un tel segment soit traité, et une base ouverte en lecture seule
n'est jamais modifiée.

Les parties supprimées ou remplacées, fusions comprises, gardent leur place dans les fichiers. *Compacter* réécrit sans
elles les segments qui les contiennent : chaque partie garde son numéro (une partie supprimée ses seuls en-têtes), et
l'arbre d'ouvertures reste valide. Les anciens fichiers sont gardés jusqu'à la fermeture de RookStudio : *Annuler le
compactage* les remet en place d'ici là, et la place est rendue à la fermeture. Les imports et les enregistrements dans
la base attendent pendant le compactage ; la liste, l'arbre et les recherches fonctionnent. Quand le disque n'a pas
assez de place pour la copie, *Compacter segment par segment* ne demande que la place d'un segment, mais ne peut pas
être annulé.

## La page Ouvertures
<!-- en: the-openings-page 296260e5 -->

Tes répertoires : chacun un fichier PGN du profil (une partie et ses variantes, pour les Blancs ou pour les Noirs), que
la page Échiquier ouvre et modifie comme une partie (*Ouvrir son fichier sur l'échiquier*), et qu'*Imprimer…* imprime
(voir [Exporter et imprimer](#exporter-et-imprimer)). L'un d'eux est actif :
l'étoile de l'onglet Arbre y ajoute le coup affiché, ou l'en retire, quel que soit le camp au trait ; la première
étoile crée un répertoire quand il n'y en a aucun. *Nouveau répertoire* en crée un d'après son nom et son camp.

La **couverture** du répertoire actif se mesure sur tes bases dont l'arbre d'ouvertures est construit (*Mesurée sur* :
un bouton pour chacune, un clic la retire ou la remet), en arrière-plan : la part des parties le long du répertoire
qui trouvent une réponse jusqu'au bout de ses lignes, ou jusqu'au coup 10 ou 15. Ses **lacunes** suivent, les plus
fréquentes d'abord : un coup de l'adversaire joué assez souvent sans ta réponse, ou une ligne qui se termine sur un
coup de l'adversaire. Un clic montre la position, avec l'arbre à côté ; le bouton droit propose aussi son rapport
d'ouverture (voir [Le rapport d'ouverture](#le-rapport-douverture)).

### S'entraîner sur un répertoire
<!-- en: training-a-repertoire 6b0acafe -->

Chaque position d'un répertoire où c'est à toi de jouer, et où le répertoire a ton coup, est une position à
travailler. RookStudio te les redemande encore et encore, à intervalles plus longs chaque fois que tu les sais, avec
FSRS, la répétition espacée qu'utilise Anki : une position bien répondue revient dans quelques jours, puis dans
quelques semaines ; une position que tu manques revient plus tard dans la séance, puis le lendemain.

La carte **Entraînement** de la page Ouvertures compte les positions à revoir aujourd'hui, les nouvelles du jour, et
celles que tu sais, à revoir plus tard. *Commencer · N positions* (Entrée) demande les positions à revoir, puis les
nouvelles du jour ; *Seulement les nouvelles* ne demande que les nouvelles. Les nouvelles positions viennent dans
l'ordre où tes bases les rencontrent le plus, les plus jouées d'abord, 20 par jour au plus (le bouton à côté, *20 par
jour*, le change). En dessous : ce qui revient demain, d'ici trois jours et d'ici une semaine.

Une séance prend la page Échiquier, dont la colonne devient celle de l'entraînement ; la partie qui était sur
l'échiquier attend parmi les parties récentes et revient après. RookStudio joue les coups de ton adversaire en suivant
le répertoire jusqu'à chaque position, les coups que tes bases jouent le plus d'abord là où le répertoire en a
plusieurs. Joue ton coup sur l'échiquier, ou tape-le (ses lettres, puis Entrée). Pendant la question, l'arbre, les
moteurs et tes notes sont masqués ; ils reviennent avec la réponse.

- **Alt+H** : un indice, la case de la pièce à jouer en bleu (H seul commence un coup du pion h).
- **Espace** : « Je ne sais pas », et le coup est montré.
- **Échap** : arrêter. Ce qui a été répondu est gardé ; la séance suivante continue avec ce qui reste à revoir.

Chaque réponse est notée d'elle-même, sans bouton à presser : juste du premier coup, *correct* ; juste après un indice
ou après plus de 20 secondes, *difficile* ; fausse, ou « Je ne sais pas », *à revoir*. Une bonne réponse montre le
commentaire du répertoire sur le coup et quand la position reviendra ; la suivante vient sur Entrée, ou d'elle-même
après une seconde (le *…* à côté le change, et la souris sur la réponse la retient). Une mauvaise réponse montre le
coup de ton répertoire sur ses cases, en vert : *Réessayer* (Entrée) le redemande ; *Voir l'arbre ici* montre la
position avec l'onglet Arbre ; *Ajouter … au répertoire* fait du coup que tu as joué une seconde réponse acceptée là
(Ctrl+Z la retire).

À la fin : les comptes, les coups que tu as manqués (un clic montre la position, avec le coup de ton répertoire) et
*Réviser maintenant les … positions manquées*.

Les révisions sont gardées dans le profil à côté du répertoire (`Najdorf.training.json`, et `Najdorf.reviews.jsonl`,
une ligne par réponse) : son PGN ne change pas. Un coup modifié dans le répertoire rend sa position de nouveau
nouvelle. Rien ne quitte la machine. Les positions à revoir comptent dans la révision du jour avec les autres types
(voir [La révision du jour](#la-révision-du-jour)) ; Ctrl+K *S'entraîner sur le répertoire* aussi. Pendant qu'une de
tes parties se joue en direct sur Lichess, l'entraînement attend ; les positions sur la ligne d'une de tes parties par
correspondance sont laissées de côté jusqu'à ce qu'elle se termine.

### Études de théorie
<!-- en: theory-studies 8ba97ca4 -->

La section **Théorie** de la page Ouvertures (à côté de *Répertoires*, sur la ligne de son titre) contient tes études
de théorie : des fichiers PGN annotés, tes analyses ou un livre en PGN, et des études Lichess. Leurs notes s'affichent
dans l'onglet Arbre de la page Échiquier, à chaque position qu'elles commentent, quel que soit l'ordre de coups qui y
mène.

*Ajouter un PGN…*, ou un PGN déposé sur la page, ajoute un fichier : RookStudio le lit là où il est, en arrière-plan,
et le relit de lui-même quand il change sur le disque. La théorie n'y écrit jamais, et rien ne quitte la machine.
*Depuis une étude Lichess…* ajoute une de tes études Lichess lues sur cette machine (voir [Tes études
Lichess](#tes-études-lichess)) : sa copie dans ton profil est lue, et relue avec l'étude.

Ce qui devient une note : chaque position annotée du fichier, variantes comprises, avec les commentaires avant et
après le coup, ses symboles (!, ?!, ±…), une évaluation écrite (`[%eval]`), et ses flèches et ses cases ; le
commentaire d'une partie avant son premier coup est une note sur son début. Les commentaires de l'assistant n'en sont
pas (voir [Les commentaires de l'assistant](#les-commentaires-de-lassistant)) : ils portent sur le coup d'une partie,
et l'assistant commente n'importe quelle position sur demande. Les commentaires de tes répertoires s'affichent de la
même façon, sous leurs noms (*Les commentaires de tes répertoires aussi*, sous la liste).

Chaque étude montre ses chiffres : les positions annotées, les commentaires, les symboles et les évaluations.
*Montrer ses notes dans l'arbre* (activé par défaut) les masque, ou les montre de nouveau. *L'ouvrir sur l'Échiquier*
(Entrée, ou un double clic dans la liste) y ouvre ses parties en copie : Ctrl+S enregistre une partie ailleurs, et le
fichier reste tel que son auteur l'écrit. *La retirer de la théorie* (Suppr) laisse le fichier où il est, et
*Annuler* la remet. Un fichier déplacé le dit, avec *Le retrouver…*.

Dans l'**onglet Arbre**, la colonne *Théorie* marque les coups que commentent tes études : leurs symboles, et combien
de notes en disent plus qu'un symbole. Sous les coups, **Ce que disent tes études** liste les notes de la position
affichée : la source de chacune, où elle est (*après 7.f4*, *sur 7…Db6*, ou *trouvée par transposition* avec ses
propres coups quand ils atteignent la position dans un autre ordre), son texte, et sa ligne, qu'un clic ouvre sur
l'Échiquier à son coup. La souris sur une note portant sur un coup montre ce coup comme la flèche de l'arbre ; sur une
note portant sur la position, ses flèches et ses cases sur l'échiquier. Dans l'onglet **Notation**, *Théorie · 3*
au-dessus des coups dit combien de notes a la position, et mène à l'onglet Arbre (Alt+3). Pendant qu'une de tes
parties se joue en direct sur Lichess, les notes sont masquées avec l'arbre.

## La page Entraînement
<!-- en: the-training-page 5ebf17d1 -->

La page **Entraînement** rassemble ce qui revient aujourd'hui, puis les exercices par type. Elle est dans la barre entre
Ouvertures et Lichess, dans les vues *Étude* et *Tout*. Une vue que tu as créée avant elle ne change pas d'elle-même :
la page Accueil propose une fois d'ajouter la page à la vue affichée (*L'ajouter à …*, ou *Pas maintenant*), et Ctrl+K
*Entraînement* y mène en attendant.

Son titre a six sections : **Aujourd'hui** (la révision du jour, les types d'exercices, les problèmes à lancer),
**Problèmes** (le même choix de problèmes, la base de Lichess gardée sur ton disque, tes résultats par thème), **Mes
erreurs** (voir [Tes erreurs en exercices](#tes-erreurs-en-exercices)), **Deviner les coups** (voir [Deviner les
coups](#deviner-les-coups)), **Finales** (voir [Les finales contre les tables](#les-finales-contre-les-tables) et
[Mats](#mats)) et **Vision** (voir [Vision de l'échiquier](#vision-de-léchiquier)). Une carte pour chaque type
d'exercice mène à sa section. Tout fonctionne hors ligne ; ce qui part chez Lichess est indiqué là où tu le choisis.

### La révision du jour
<!-- en: the-days-review b7049c16 -->

*À revoir aujourd'hui* compte ce que chaque type d'entraînement a à revoir aujourd'hui, chacun avec sa répétition
espacée (FSRS), en une seule file : les erreurs de tes parties (voir [Tes erreurs en
exercices](#tes-erreurs-en-exercices)), les positions de tes répertoires (leur entraînement reste sur la page
Ouvertures, voir [S'entraîner sur un répertoire](#sentraîner-sur-un-répertoire)), les problèmes et les finales que tu as
manqués. *Tout réviser* (Entrée) les prend l'un après l'autre sur la page Échiquier : tes erreurs à revoir, les
positions à revoir de chaque répertoire, les problèmes à revoir, puis les finales. À la fin de chacun, *Ensuite : …*
(Entrée) continue, *Revenir à la page Entraînement* (Échap) s'arrête là ; ce qui a été répondu est gardé. À côté du
bouton, le temps que cela devrait prendre, d'après le rythme de tes réponses ; en dessous, ce qui revient demain et d'ici
trois jours. Quand des erreurs ou des problèmes sont à revoir avec d'autres types, *Seulement mes erreurs* et
*Seulement les problèmes* les révisent seuls.

*En cours*, sur la page Accueil, a le même compte (*35 à revoir aujourd'hui · Tout réviser*), un clic révisant le tout ;
Ctrl+K *Tout réviser* aussi.

### Problèmes
<!-- en: puzzles 4fd9ffec -->

Lichess publie ses problèmes chaque mois sous forme de base (database.lichess.org, CC0) : 6 193 530 problèmes en
novembre 2026, chacun avec son classement, ses thèmes et, avant le coup 20, son ouverture. RookStudio la garde sur ton
disque et t'en donne des problèmes sans réseau.

**La base.** *Télécharger* récupère le fichier (environ 295 Mo) et le lit à mesure qu'il arrive dans un magasin
d'environ 293 Mo dans le profil ; le fichier lui-même n'est pas gardé. Un téléchargement interrompu (*Annuler*, le
réseau, le programme fermé) reprend là où il s'était arrêté : *Continuer*, ou *Recommencer*. *Un fichier déjà
téléchargé…* lit à la place `lichess_db_puzzle.csv.zst` sur ton disque. Une fois la base là : *Mettre à jour* demande à
database.lichess.org la date de son fichier et lit un fichier plus récent pendant que les problèmes gardés répondent ;
*Retirer les problèmes* libère le disque (ton classement, tes résultats et tes problèmes à revoir restent, gardés à part). Rien
d'autre que la demande du fichier ne quitte la machine.

**Le choix.** *Hors ligne* (la base) ou *En ligne* (ton compte Lichess, voir [Les problèmes Lichess en
ligne](#les-problèmes-lichess-en-ligne)) ; un thème parmi les premiers proposés, chacun avec la fréquence à laquelle tu
le résous du premier coup, ou parmi *Tous les thèmes…*, qui liste chaque thème et chaque ouverture avec ses problèmes
(tape quelques lettres pour en trouver un) ; et une difficulté : *Plus facile* (300 points sous ton classement), *Ton
classement*, *Plus difficile* (300 au-dessus). *Commencer · 20 problèmes* (Entrée) lance une série proche de ton
classement, jamais un problème déjà vu ; Ctrl+K a *Problèmes : Fourchette* et les autres thèmes, qui lancent aussitôt une
série.

**Une série** prend la page Échiquier, dont la colonne devient celle du problème, comme le fait l'entraînement des
répertoires ; la partie de l'échiquier attend parmi les parties récentes et revient après. L'échiquier montre la
position de ton côté, le coup que l'adversaire vient de jouer en vert pâle. Joue ton coup sur l'échiquier, ou tape-le
(ses lettres, puis Entrée) ; RookStudio joue les réponses de l'adversaire, le reste de la solution. Pendant la question,
le moteur, l'arbre et tes notes sont masqués, de même que les thèmes du problème.

- **Alt+H** : un indice, la case de la pièce à jouer en bleu ; le problème compte alors comme manqué.
- **Espace** : « Je ne sais pas » ; la solution est montrée et le problème compte comme manqué.
- **Échap** : arrêter ; ce qui a été répondu est gardé.

Juste : la solution en coups, la variation de ton classement et les thèmes du problème ; *Suivant* vient sur Entrée, ou
d'elle-même après une seconde (le réglage de l'entraînement des répertoires) ; *Analyser ici* (Alt+A) lance le moteur sur
la position ; *La partie sur lichess.org* ouvre la partie d'où vient le problème. Tout coup qui mate est juste, comme
sur Lichess. Faux : ton coup en orange et le premier coup de la solution en flèche verte ; *Réessayer* (Entrée) le fait
rejouer, sans compter de nouveau ; *La solution coup par coup* (→) la joue un coup à la fois ; *Suivant* continue.

À la fin : les problèmes résolus du premier coup, ceux manqués, la variation de ton classement, et les manqués (un clic
montre la position avec la solution) ; *Réviser les 3 maintenant* (autant que de manqués) les redemande sans compter, *Revenir à la page
Entraînement* quitte.

**Ton classement** est celui de RookStudio, calculé avec Glicko-2 comme Lichess calcule ses classements de problèmes :
un problème résolu du premier coup, sans indice, est gagné contre le classement du problème. Il part de ton classement
de problèmes Lichess quand ton compte est associé, sinon de 1500, et bouge vite tant qu'il est nouveau ; les résultats
des problèmes hors ligne ne peuvent pas retourner à Lichess. *Tes résultats par thème* (la section Problèmes) compte les
problèmes de chaque thème et la fréquence à laquelle tu les as résolus du premier coup.

Un problème que tu manques devient une carte de la révision du jour : il revient le lendemain, puis à intervalles plus
longs chaque fois que tu le résous ; manqué de nouveau, plus tard dans la même série, puis le lendemain. Tout cela est
gardé dans le dossier `puzzles` du profil (`rating.json`, `results.jsonl` une ligne par problème, `review.json`), la
base dans `lichess-puzzles`.

### Les problèmes Lichess en ligne
<!-- en: lichess-puzzles-online 51fd9d60 -->

*En ligne* donne des problèmes de ton compte Lichess : des problèmes que tu n'as jamais vus, à ton classement Lichess,
dont les résultats retournent à Lichess. Il faut ton compte associé (voir [Associer ton compte](#associer-ton-compte)) ;
la première fois, Lichess demande deux permissions de plus, *puzzle:read* et *puzzle:write* : *Autoriser sur Lichess…*
ouvre sa page, ton compte reste associé pendant ce temps, et le jeton qu'elle remplace est révoqué.

*Obtenir un lot · 50 problèmes* demande à Lichess cinquante problèmes du thème et de la difficulté choisis. Le lot est
gardé sur ton disque : il se résout aussi hors ligne, en autant de séries que tu veux (*Reprendre · encore N*). *Classé*
ou *Non classé, pour essayer* est envoyé avec chaque réponse. À la fin du lot, ses réponses partent chez Lichess en une
requête, qui apporte le lot suivant ; ton classement Lichess bouge alors, et la page dit quand les dernières réponses
ont été prises. Lichess demande aux programmes de ne pas télécharger sa collection par ces requêtes : la base est là
pour cela.

Quand Lichess ne peut pas prendre les réponses, elles restent sur le disque, avec ce qu'il faut faire : trop de
requêtes (429), renvoyées d'elles-mêmes une minute plus tard, même si RookStudio a été fermé entre-temps ; le réseau
coupé, renvoyées une minute plus tard ; le jeton refusé, envoyées une fois le compte associé de nouveau. *Réessayer
maintenant* n'attend pas. Ce qui quitte la machine : le lot demandé (son thème ou son ouverture, sa difficulté), puis
l'identifiant de chaque problème avec juste ou faux, classé ou non.

### Tes erreurs en exercices
<!-- en: your-mistakes-as-exercises b0e75f68 -->

Les erreurs de tes propres parties reviennent en exercices : la position avant ton coup, pour trouver mieux. Tes
parties sont celles où tu joues sous un de tes noms (voir [Tes noms](#tes-noms)), dans tes bases. Chacune qui a un
bilan (voir [Le bilan d'une partie](#le-bilan-dune-partie)) donne les coups que le bilan a marqués **?** et **??** de
ton côté, là où un meilleur coup est net : le bilan a cherché la position assez profondément pour nommer son meilleur
coup (sa troisième passe au moins), et la partie n'était pas décidée de toute façon (le meilleur coup du bilan te garde
dans la partie ; ton coup n'a pas gardé un gain). Les imprécisions (?!) sont laissées de côté : elles ont rarement une
seule bonne réponse.

La section **Mes erreurs** compte les exercices et les parties d'où ils viennent, ceux à revoir aujourd'hui, les
nouveaux et ceux que tu sais, à revoir plus tard. Leurs types viennent de *Ce qu'il faut travailler* de chaque bilan :
fourchettes, pièces laissées en prise, finales de tours… ; une pastille pour chacun limite la liste et la séance à ce
type, *Tous les types* les prend tous. La liste donne pour chaque exercice sa partie, où et quand elle a été jouée, ton
coup et celui du bilan, et quand il revient. *Commencer · N exercices* (Entrée) demande ceux à revoir, puis les nouveaux
du jour (autant par jour que les nouvelles positions des répertoires).

Tes dernières parties sans bilan sont indiquées sous la liste : *En faire le bilan · en arrière-plan* en fait le bilan
l'une après l'autre, comme la page Échiquier fait le bilan d'une partie (son moteur, ses réglages, les processus laissés
à côté d'un tournoi), chaque bilan gardé avec sa partie ; leurs exercices viennent partie par partie. La progression, le
temps restant et *Annuler* s'affichent pendant ce temps ; les bilans attendent pendant que la page Échiquier analyse.

**Un exercice** prend la page Échiquier, dont la colonne devient celle de l'exercice, comme le fait l'entraînement des
répertoires : la partie jusqu'à la position, de ton côté, le dernier coup de l'adversaire en vert pâle ; sous
l'échiquier, quand elle a été jouée et ce que tu as joué alors. Joue ton coup sur l'échiquier, ou tape-le. Le meilleur
coup du bilan est juste aussitôt ; tout autre coup est cherché par le moteur du bilan aux réglages du bilan (un thread,
son nombre de nœuds, depuis une table vide), et il est juste quand il perd 0,3 pion au plus par rapport au meilleur coup
du bilan, ou garde un gain que le bilan ne marquerait pas. Le moteur, l'arbre et tes notes sont masqués pendant la
question.

- **Alt+H** : un indice, la case de la pièce à jouer en bleu.
- **Espace** : « Je ne sais pas » ; le coup du bilan est montré.
- **Échap** : arrêter ; ce qui a été répondu est gardé.

Chaque réponse est notée d'elle-même, comme une position d'un répertoire : juste du premier coup, *correct* ; juste après
un indice ou après plus de 20 secondes, *difficile* ; fausse, *à revoir*. Juste : la ligne du bilan et celle de ta
partie, ce que le bilan disait de ton coup, et quand l'exercice revient ; *Ouvrir la partie à ce coup* l'ouvre sur la
page Échiquier. Faux : ton coup en orange, le coup du bilan en flèche verte, et ce que le moteur a trouvé après le tien ;
*Réessayer* (Entrée) le fait rejouer sans compter de nouveau ; un exercice manqué revient quelques exercices plus tard,
puis le lendemain.

Les exercices à revoir comptent dans la révision du jour (voir [La révision du jour](#la-révision-du-jour)), *Seulement
mes erreurs* les révise seuls ; Ctrl+K a *Réviser mes erreurs* et *Exercices tirés de mes erreurs*. Une position que ton
répertoire entraîne n'est pas prise ici : elle se travaille sur la page Ouvertures. Une partie retirée de sa base emporte
ses exercices avec elle. Les exercices et leurs cartes sont gardés dans le dossier `mistakes` du profil
(`exercises.json`, et `answers.jsonl`, une ligne par réponse) ; tes parties ne changent pas, et rien ne quitte la
machine. Ce que disent tes parties ordonne aussi les thèmes des problèmes : les types de tes erreurs du dernier mois
viennent en premier parmi eux, indiqués sous les pastilles.

### Tes noms
<!-- en: your-names 4a0ddfdd -->

*Réglages › Tes noms* (ou la section *Mes erreurs*, avant tout nom) liste les noms sous lesquels tu joues, comme tes
bases les écrivent : une partie où l'un d'eux joue est la tienne. Un nom correspond par ses mots entiers, sans tenir
compte de la casse ni des accents : *Tester* est *Tester, Sam* et *TESTER, S.*, *Sam Tester* est *Tester, Sam*, mais
*Test* n'est personne. Le nom de ton compte Lichess associé compte de lui-même. Clique sur un nom pour le retirer : les
exercices de ses parties s'en vont, mais leurs cartes sont gardées, et reviennent avec eux si tu ajoutes de nouveau le
nom.

### Deviner les coups
<!-- en: guessing-the-moves e004de63 -->

**Deviner les coups** rejoue une partie de tes bases d'un côté : à chacun de ses coups, tu devines celui qui a été joué.
Le moteur note chaque réponse par rapport au coup de la partie : *comme la partie* 3 points ; *aussi bon* selon le moteur
(0,2 pion de moins au plus, ou mieux) 2 ; *jouable* (0,5 pion de moins au plus) 1 ; sinon aucun. C'est le moteur
d'analyse, sur un thread, une seconde par coup ; le coup de la partie est cherché pendant que tu réfléchis.

*Une partie au hasard* en tire une de la base choisie, à travers un filtre : le filtre rapide de la liste des parties
(joueurs, événement, un code ECO, une année) et l'Elo des joueurs. Choisis ton camp, puis *Commencer · une partie au
hasard* (Entrée). Avec *Les coups de la théorie passent d'eux-mêmes* (gardé pour la prochaine fois), l'exercice commence
là où la partie quitte les coups qu'ont joués beaucoup de parties de tes bases (leur arbre d'ouvertures). *La partie de
la page Échiquier* fait deviner la partie affichée depuis sa position, pour le camp au trait : Ctrl+K *Deviner les
coups* sur la page Échiquier.

La partie prend la page Échiquier, dont la colonne devient celle de l'exercice ; la partie n'est pas modifiée, et ses
coups suivants ne sont pas montrés. Après chaque réponse : ton coup en flèche bleue, celui de la partie en flèche verte,
les points et les deux évaluations ; la partie continue d'elle-même après un instant (Entrée aussitôt), la réponse de
l'adversaire jouée, et le coup suivant est à toi. En dessous : chaque réponse et ses points, et le score jusqu'ici. À la
fin : tes points, combien de réponses étaient le coup de la partie, les pions perdus par coup en moyenne, et les coups
qui t'ont coûté le plus (un clic montre leur position) ; toute la partie est sur l'échiquier, ← et → la parcourent.
Chaque partie devinée est gardée dans le dossier `guess` du profil ; la page Entraînement montre les points de la
dernière.

### Les finales contre les tables
<!-- en: endgames-against-the-tables 6ded57cf -->

La section **Finales** est un catalogue de finales à gagner ou à tenir, jouées contre les tables de finales : dix-huit,
en quatre groupes. *Mats élémentaires* : la dame, la tour, les deux fous, fou et cavalier. *Pions* : l'opposition, la
règle du carré, le pion passé éloigné, la manœuvre de Réti. *Tours* : Lucena, Philidor, Vancura, tour contre pion, tour
et deux pions contre tour. *Pièces et dame* : dame contre tour, dame contre pion, le mauvais fou, tour contre fou, fou
contre cavalier. Chacune dit ce qu'elle demande (*mat*, *gain* ou *tenir la nulle*) et où tu en es : *à faire*, *fait*,
*fait en 9 (les tables : 7)* pour un mat, *à refaire* après une faute. Une finale à laquelle les tables disponibles ne
peuvent pas répondre est grisée, ses pièces indiquées (voir [Des tables pour les finales](#des-tables-pour-les-finales)) ;
survole-en une pour l'idée qui la gagne ou la tient. Un clic la joue ; ensuite ↑ et ↓ en choisissent une autre et
Entrée la joue. Ctrl+K a *Finale : Lucena* et les autres.

**Une finale** prend la page Échiquier, dont la colonne devient celle de la finale, comme le font les problèmes ; la
partie de l'échiquier attend parmi les parties récentes. Tu joues de ton côté ; les tables répondent à chaque coup par
celui qui résiste le plus longtemps (avec les tables Syzygy, le plus long jusqu'à la prochaine prise ou le prochain coup
de pion, et parmi les coups aussi longs celui qui te laisse le moins de bonnes réponses). Au-dessus de la question : le
verdict des tables maintenant (*Les tables : les Blancs gagnent*), et à quelle distance, les coups jusqu'au mat quand
Lichess les connaît, sinon les coups au plus avant une prise ou un coup de pion.

- **Ctrl+Z** : reprendre ton dernier coup et la réponse des tables.
- **Alt+H** : le bon coup, en flèche verte ; la finale compte alors comme faite avec aide.
- **Recommencer** : retour au départ. **Échap** : arrêter.

Chaque coup est jugé aussitôt. Un coup qui change un gain en nulle, ou une nulle en perte, est une faute : les tables
répondent, puis la colonne le dit (*1.Rd5? Re7 : c'est nulle, maintenant*) avec les coups qui gardaient le résultat
(*Coups qui gardaient le gain : 1.Rd6!, 1.Rf6!*, leurs cases en vert, ton coup en orange) et l'idée de la finale.
*Reprendre* (Entrée, ou Ctrl+Z) le fait rejouer ; *Voir le gain* le reprend et écrit la ligne des tables à partir de là ;
*Continuer quand même* continue dans le nouveau verdict. En dessous, le verdict au départ et chaque coup qui l'a changé.

Un gain compte au mat, ou dès qu'un de tes pions devient dame ou que l'autre roi est seul, le gain gardé ; dans les mats
élémentaires, le mat lui-même est le but, tes coups comparés au mat des tables depuis le départ. Une nulle compte quand
une règle annule la partie (pat, matériel insuffisant, la triple répétition, les cinquante coups), ou après 15 coups
tenus. À la fin : *Recommencer* (Entrée), *Suivant :* la finale suivante pas encore faite, *Revenir à la page
Entraînement*.

La position d'avant une faute devient une carte de la révision du jour (voir [La révision du
jour](#la-révision-du-jour)) : elle revient le lendemain, la finale rejouée à partir d'elle, puis à intervalles plus
longs chaque fois que tu la gagnes ou la tiens sans faute (avec de l'aide, elle compte comme difficile ; une faute,
comme manquée). Ctrl+K *Réviser les finales manquées*.

**Tirées de tes parties** prend les positions de 7 pièces ou moins parmi tes erreurs (voir [Tes erreurs en
exercices](#tes-erreurs-en-exercices)), là où le bilan de ta partie a vu le gain, ou la nulle, s'échapper. *Commencer* les
joue l'une après l'autre contre les tables, chacune depuis la position avant ton coup ; une position que les tables
disent perdue de toute façon est laissée de côté. Les résultats et les cartes sont gardés dans le dossier `endgames` du
profil (`record.json`).

### Des tables pour les finales
<!-- en: tables-for-the-endgames 0e49bbca -->

Les tables sont celles de ton disque, les dossiers Syzygy des tournois (*Adjudication* > tables de finales, voir [Les
tables de finales sur la page Échiquier](#les-tables-de-finales-sur-la-page-échiquier)), avec celles du profil une fois
téléchargées ; pour plus de pièces qu'elles n'en contiennent, celles de Lichess en ligne, jusqu'à 7 pièces, avec ton
accord pour les tables en ligne (celui de la page Échiquier, voir [En ligne pour l'analyse](#en-ligne-pour-lanalyse)).
Pendant qu'un tournoi joue dans le processus du programme, les tables du disque sont à lui : celles de Lichess
répondent en attendant, ou la finale attend.

Quand une finale demande des tables que tu n'as pas, la section le dit (*Pas de tables pour « Lucena »*) et propose
trois solutions :

- **Télécharger les tables de 3 à 5 pièces** (290 fichiers, 938 Mo) depuis le miroir de Lichess, un fichier à la fois,
  les plus petits d'abord : les finales de 3 et 4 pièces sont prêtes en quelques secondes, les autres à mesure que leurs
  fichiers arrivent. Un téléchargement interrompu reprend là où il s'était arrêté (*Continuer*) ; chaque fichier est
  vérifié (sa taille et la marque de Syzygy). Les tables vont dans le dossier `syzygy` du profil et, une fois toutes là,
  dans les dossiers des tournois.
- **Un dossier de tables déjà sur le disque…** : choisi aussi pour les tournois.
- **Celles de Lichess, en ligne** : jusqu'à 7 pièces, une requête par position jouée ; les choisir donne l'accord.

Sans tables, les finales qui en ont besoin sont grisées, chacune avec ce qu'il lui faut ; les autres exercices
fonctionnent. En ligne, une coupure fait attendre la finale : ton coup est gardé, et jugé quand le réseau revient
(*Réessayer maintenant* n'attend pas) ; un 429 attend la minute que demande Lichess. Ce qui quitte la machine : avec les
tables du disque, rien ; avec celles de Lichess, chaque position jouée, sous forme de FEN ; pour le téléchargement, la
demande de chaque fichier.

### Mats
<!-- en: mates 803d99bd -->

La carte **Mats** (dans la section Finales, à laquelle mène la carte de la section Aujourd'hui) lance 20 problèmes de mat
de Lichess (voir [Problèmes](#problèmes)) : en 1 à 5 coups, ou par motif (du couloir, de l'étouffé, d'Anastasia, arabe,
de Boden, des deux fous, du crochet), chaque pastille avec la fréquence à laquelle tu le résous du premier coup. Ils
viennent de la base sur ton disque, sinon d'un lot de ton compte Lichess ; sans l'un ni l'autre, le clic mène à la
section Problèmes. Les mats élémentaires contre les tables sont dans le catalogue ci-dessus, tes coups comptés par
rapport au mat des tables.

### Vision de l'échiquier
<!-- en: board-vision dd34b350 -->

La section **Vision** entraîne à voir l'échiquier dans ta tête. Rien ne quitte la machine ; les records sont gardés dans
le `vision.json` du profil.

**Coordonnées** : trente secondes de cases. *Trouver la case* : une case est nommée, clique dessus sur un échiquier sans
ses lettres ni ses chiffres ; *Nommer la case* : une case est marquée en bleu, tape son nom (une lettre, puis un
chiffre). Vu des Blancs, des Noirs, ou *Au hasard*, l'échiquier tournant case après case. Une faute montre la case
demandée en vert et la tienne en orange. À la fin, ton compte et ton record pour cette façon et ce côté. Entrée commence,
Échap arrête.

**À l'aveugle** : une partie contre le moteur de la page Jouer, à la force et avec les ouvertures choisies là-bas
(*Changer sur la page Jouer*, voir [Contre un moteur](#contre-un-moteur)). Tu joues les Blancs, les Noirs ou l'un ou
l'autre au hasard ; l'échiquier masque toutes les pièces, toutes les pièces après le 10e coup, ou toutes sauf les pions,
le dernier coup marqué. Tape tes coups, ou joue-les sur l'échiquier ; les coups joués jusqu'ici sont écrits dans la
colonne. Garde Maj enfoncée un instant pour voir les pièces pendant 2 secondes, trois fois par partie ; F2 demande *Où
est… ?* d'une pièce que ses mots seuls désignent (*le fou noir de cases noires*) : clique sur sa case, ou tape-la. Ta
pendule est longue, celle du moteur courte ; chaque partie est ajoutée au `games/blindfold.pgn` du profil.

**Une ligne lue de tête** : une position d'une ouverture connue, les six coups qui suivent écrits à côté sans que les
pièces bougent, puis une question : quelle pièce est sur une case qu'ils ont changée ? Clique sur la réponse, ou tape la
pièce comme l'écrit le FEN (*N* un cavalier blanc, *n* un noir, *-* une case vide) ; l'échiquier montre alors la
position après les coups. Dix lignes par série.

## La page Jouer
<!-- en: the-play-page 7168b30b -->

La page **Jouer** a deux sections : *Contre un moteur*, une partie contre un moteur de ta bibliothèque jouée sur la page
Échiquier, et *Sur Lichess*, tes parties Lichess jouées depuis RookStudio (voir [Sur Lichess](#sur-lichess)) et les
arènes et tournois suisses de Lichess (voir [Arènes et tournois suisses](#arènes-et-tournois-suisses)) ; la section
affichée en dernier revient. Elle est dans la barre après la page Entraînement, dans les vues *Étude* et *Tout*.
Une vue que tu as créée avant elle ne change pas d'elle-même : la page Accueil propose une fois de l'ajouter à la vue
affichée (*L'ajouter à …*, ou *Pas maintenant*), et Ctrl+K *Jouer contre un moteur* (ou *Jouer sur Lichess*) y mène en
attendant, comme *Jouer contre un moteur* sur la page Accueil et *La page Jouer* dans la carte *Jouer une partie…* de la
page Échiquier. Contre un moteur, rien ne quitte la machine : le moteur y tourne. Pendant qu'une de tes parties se joue
en direct sur Lichess, la page le dit et ne lance aucune partie contre un moteur (voir [Fair-play](#fair-play)).

### Contre un moteur
<!-- en: against-an-engine 1cf4f880 -->

Le choix est gardé pour la partie suivante : une fois fait, *Jouer* (Entrée) suffit.

- **L'adversaire** : un moteur de ta bibliothèque, avec les forces qu'il peut prendre.
- **Sa force** : l'Elo qu'il vise (`UCI_Elo`, avec `UCI_LimitStrength`), sur sa propre échelle : une indication, pas un
  classement FIDE. Un moteur sans ce réglage joue à la place un nombre de nœuds par coup, de 1 à un million, moins pour
  un jeu plus faible. *Pleine force* le laisse à son meilleur, quoi que limite sa fiche.
- **Son style : ses ouvertures**, ce qu'il joue avant ses propres coups : *Les siennes* (pas de livre) ; *Tes bases*
  (les coups les plus joués là, dans toutes tes bases ayant un arbre d'ouvertures ou dans l'une d'elles) ; *Ton
  répertoire* (les coups contre lesquels tu t'es préparé, puis le moteur ; tu joues la couleur du répertoire, que ce
  choix règle) ; *Les parties d'un joueur* (les coups qu'un joueur a joués avec la couleur du moteur, d'une base nommée
  *Lichess · nom*, comme en crée la préparation d'une partie contre lui, voir [Préparer une
  partie](#préparer-une-partie)) ; *Un livre de la bibliothèque* (un livre Polyglot ou CTG de la page Livres, ses coups
  à éviter écartés). *Varier* choisit au hasard parmi les coups joués au moins 5 fois, aussi souvent qu'ils ont été
  joués (les coups d'un livre aussi souvent que le disent ses poids, ceux d'un répertoire aussi souvent les uns que les autres) ; décoché, le coup le plus joué, le coup
  principal d'un livre d'abord. Une fois que ses ouvertures n'ont plus de coup pour la position, le moteur joue jusqu'à
  la fin. Son style, ce sont ses ouvertures : les réglages propres du moteur restent sur sa fiche. Avec un
  handicap, les ouvertures rencontrent des positions qu'elles n'ont pas, et le moteur joue dès le premier coup.
- **Toi** : les Blancs, les Noirs ou au hasard ; la pendule, des minutes et un incrément, *La même pour les deux* ou,
  décoché, celle du moteur à part (temps inégal) ; depuis la position initiale, ou depuis celle de la page Échiquier.
- **Le handicap** : ce que le moteur donne au départ : rien, son pion f, son cavalier b, sa tour a (le roque de ce côté
  disparaissant avec elle) ou sa dame ; une pièce qu'il n'a pas à son départ est grisée, et le Chess960 n'en a aucun.
  *Reprendre un coup* (autorisé sauf si décoché) et *Indices* (3 par partie) sont aussi des handicaps, comptés.

La partie compte dans le budget de cœurs (les threads du moteur, comme pour une partie jouée depuis l'échiquier) ; la
page dit quand la machine est pleine.

### La partie contre un moteur
<!-- en: the-game-against-an-engine bfdb25dc -->

*Jouer* emmène la partie sur la page Échiquier, dans une vue pour l'étude : l'échiquier tourné de ton côté, les pendules
tournant dans les bandeaux des joueurs (celui du moteur avec sa force), et au-dessus de la colonne la carte de la
partie : à qui c'est de jouer, *Proposer la nulle*, *Abandonner…*, *Reprendre · Ctrl+Z*, *Indice · Alt+H*, *Arrêter*.

- Joue tes coups sur l'échiquier, ou tape-les : la première lettre d'un coup ouvre le champ, Entrée le joue.
- **Les aides sont masquées jusqu'à la fin** : les onglets Analyse, Arbre, Références et Questions, l'analyse (Alt+A), la
  menace, l'analyse de la partie (Alt+G), les tables de finales et l'assistant. Le moteur ne montre rien de sa recherche
  non plus : ses évaluations arrivent avec la partie terminée. *Les montrer*, dans la carte ou à la place d'un onglet,
  les rend pendant la partie, qui est alors marquée *avec aides*.
- *Reprendre* (Ctrl+Z) reprend ton dernier coup et la réponse du moteur, ou ton coup pendant que le moteur réfléchit,
  les pendules comme elles étaient avant : compté, et écrit dans la partie.
- *Indice* (Alt+H) : à ton tour, le coup du moteur d'analyse après une seconde de recherche, en flèche verte ; trois par
  partie, comptés et écrits dans la partie.
- *Proposer la nulle* : le moteur répond avec son prochain coup. Il accepte la nulle quand il se fait mater ou est
  nettement moins bien, ou quand il n'est pas mieux une fois 20 coups joués ; un coup de ses ouvertures la refuse.
- *Abandonner…* demande Entrée, ou un second clic, dans les quelques secondes ; *Arrêter* termine la partie sans
  résultat, enregistrée telle quelle.
- Là où les ouvertures du moteur se sont terminées, la notation le dit (*fin de ton répertoire · puis le moteur*).

Fermer RookStudio pendant la partie la garde : au prochain lancement, elle revient sur la page Échiquier, ses pendules
attendant que tu la reprennes (*Reprendre*, Entrée) ou que tu l'arrêtes (*L'arrêter*). Une autre partie ouverte sur
l'échiquier entre-temps laisse la tienne continuer, ta pendule tournant quand c'est à toi de jouer : *Revenir à la
partie* la ramène.

### Après la partie
<!-- en: after-the-game 44d80f6b -->

À la fin, les aides reviennent, et la carte dit comment la partie s'est terminée (*Nulle : triple répétition au coup
34*), contre quoi et comment : la force, ta couleur, la pendule, les reprises, les indices, avec ou sans aides. La partie
est enregistrée dans la base **Mes parties contre les moteurs**, créée à la première partie, avec ses réglages dans ses
balises : `Strength` (`UCI_Elo 1800`, `nodes 1000`, `full`), `Openings` (`repertoire Najdorf`, `database Mine`,
`databases`, `player Somebody`, `book Human`, `engine`), `Handicap` (`knight b8`, avec la position de départ),
`Takebacks`, `Hints`, `Aids`. Ctrl+S l'y remet après une modification.

- *Faire son bilan* (Entrée) : l'analyse de la partie, comme le fait Alt+G (voir [Le bilan d'une
  partie](#le-bilan-dune-partie)) ; une fois le bilan fait, tes erreurs y deviennent des exercices (voir [Tes erreurs en
  exercices](#tes-erreurs-en-exercices)) quand tes noms disent que la partie est la tienne (voir [Tes noms](#tes-noms)).
- *Revanche, couleurs inversées* : le même choix, l'autre couleur.
- *Changer les réglages* : la page Jouer.
- *Fermer* : la partie reste sur l'échiquier comme n'importe quelle partie.

En dessous, **tes parties contre ce moteur à ce niveau** : gagnées, nulles et perdues, celles avec aides à part et non
comptées, et où tu as perdu la dernière (*une finale de tours*), avec si elle est parmi tes exercices. Le niveau suivant
se choisit là ou sur la page Jouer : rien ne monte de lui-même. Chaque partie est aussi une ligne du `play/games.jsonl`
du profil.

### Sur Lichess
<!-- en: on-lichess 37d530b3 -->

La section **Sur Lichess** de la page Jouer joue tes parties Lichess depuis RookStudio, par la Board API de Lichess, qui
est faite pour des personnes. Elle a deux moitiés, *Parties*, décrite ici, et *Tournois* (voir [Arènes et tournois
suisses](#arènes-et-tournois-suisses)) ; celle affichée en dernier revient. Il faut ton compte associé (voir [Associer
ton compte](#associer-ton-compte)) ; la première fois, Lichess demande une permission de plus, *Jouer tes parties* (`board:play`) : *Autoriser sur Lichess…* (Entrée)
ouvre la page de Lichess, ton compte reste associé pendant ce temps, et le jeton qu'elle remplace est révoqué. RookStudio
ne joue jamais un coup que tu n'as pas joué. Ce qui quitte la machine : tes recherches d'adversaire et tes défis, tes
coups, tes messages de discussion, nulles et abandons, avec le jeton de ton compte ; les choix sont gardés dans le
`play/lichess.json` du profil.

- **Chercher une partie** : rapide (10+0, 10+5, 15+10), classique (30+0, 30+20) ou par correspondance (1 à 14 jours par
  coup), classée ou non, contre des adversaires de tout classement ou d'un classement proche du tien à cette cadence
  (±). Lichess garde le blitz et le bullet pour son site : un programme ne les cherche pas, et le blitz reste possible en
  défi direct. *Chercher un adversaire* (Entrée) met la recherche dans le salon de Lichess une fois le flux d'événements
  de Lichess ouvert, comme Lichess le demande, pour que le début de la partie ne soit pas manqué. Une recherche en direct
  vit tant que RookStudio la tient ouverte : Échap la retire, de même que fermer RookStudio ; la partie trouvée s'ouvre
  sur la page Échiquier. Une recherche par correspondance reste dans le salon jusqu'à ce que quelqu'un l'accepte (l'API
  ne peut pas la retirer ; le salon de lichess.org le peut), et sa partie arrive parmi tes parties en cours.
- **Défier un joueur** : son nom, la cadence (3+2, 5+3, 10+5, 15+10, 3 jours, ou *autre…* : minutes et secondes ajoutées
  par coup, ou jours par coup ; trois minutes au moins, l'incrément compté quarante fois, puisque la Board API ne joue
  pas de bullet), classée ou non, ta couleur. Un défi en direct attend la réponse tant que tu restes (Échap, ou *Le
  retirer*). Refusé, la raison de Lichess est indiquée (*trop rapide pour lui*) avec d'autres cadences à un clic (plus
  lentes après « trop rapide ») ; accepté, la partie s'ouvre sur la page Échiquier.
- **Un défi reçu** arrive en haut de la section (*Accepter* · Entrée, *Refuser ▾* avec les raisons de Lichess : *Pas
  maintenant*, *Plus tard*, *Trop rapide pour moi*, *Trop lent pour moi*, *Amicales seulement*, *Classées seulement*),
  sur la page Accueil sous *En cours*, et dans un avis au coin de la fenêtre où que tu sois, avec un son quand les sons
  sont activés ; *Plus tard* met l'avis de côté. Pendant une partie en direct, il attend la fin de la partie. Une partie
  que la Board API ne peut pas jouer (bullet, une variante autre que le Chess960) se joue sur lichess.org.
- **Tes parties en cours** : celles où c'est à toi de jouer d'abord, avec le temps qui reste pour jouer ; un clic en
  ouvre une sur la page Échiquier, où ton coup se joue.

Le flux d'événements est celui du [Fair-play](#fair-play) : Lichess en ouvre un par jeton, et il dit les débuts et les
fins des parties, les défis reçus et le sort de ceux que tu as envoyés.

### Une partie sur Lichess
<!-- en: a-game-on-lichess 15ac0e49 -->

La partie se joue sur la page Échiquier, dans une vue pour l'étude : l'échiquier tourné de ton côté, les classements et
les pendules des joueurs dans leurs bandeaux, et au-dessus de la colonne la carte de la partie : ce qu'elle est
(*Classée · Rapide 10+5 contre …*), à qui c'est de jouer, *Proposer la nulle*, *Abandonner…* (Entrée, ou un second clic,
dans les quelques secondes), *Proposer de reprendre le coup*, *Annuler la partie* (avant que les deux camps aient joué),
*Discussion* (la discussion des joueurs), *Ouvrir sur lichess.org*.

- En direct, tes coups partent dès qu'ils sont joués, sur l'échiquier ou tapés (la première lettre d'un coup ouvre le
  champ, Entrée le joue). Un coup joué pendant le tour de ton adversaire attend le tien (une flèche en pointillés), et il
  est alors joué s'il est légal ; Échap le retire. Avant ton premier coup, la carte décompte le temps que donne Lichess
  avant d'annuler la partie.
- Par correspondance, un coup attend ta confirmation (*Ton coup : 10.g4 · pas encore envoyé*, une flèche sur
  l'échiquier) : *Envoyer* (Entrée), ou *Le reprendre* (Échap). La carte liste tes autres parties par correspondance, à
  un clic.
- La proposition de nulle ou la demande de reprise de ton adversaire arrive dans la carte avec *Accepter* et *Refuser* ;
  la tienne attend sa réponse. Si ton adversaire quitte la partie, la carte décompte jusqu'à ce que Lichess te laisse
  revendiquer la victoire (*Revendiquer la victoire*, *Réclamer la nulle*).
- **Les aides** : le verrouillage du [Fair-play](#fair-play) tient, comme pour toute partie à toi. En direct, toutes les
  aides sont verrouillées sur chaque position : les onglets Analyse, Arbre, Références et Questions, l'analyse, la
  menace, les tables, le cloud et l'assistant. Par correspondance, le moteur, le cloud, les tables et l'assistant sont
  verrouillés sur la partie ; l'arbre de tes bases et tes livres restent ouverts, puisque Lichess y autorise un livre.
- **Une coupure** : la carte le dit (*ta pendule tourne sur Lichess*) et réessaie d'elle-même, après 1, 2, 3, puis
  toutes les 5 secondes ; toute la partie revient avec la connexion, la discussion aussi. Un coup joué entre-temps ne
  part pas : rejoue-le une fois la connexion revenue. *Ouvrir la partie sur lichess.org* reste à portée de main.
- **Fermer RookStudio** pendant une partie en direct demande d'abord (*Rester* · Échap, *Fermer quand même*) : la partie
  continue sur Lichess, ta pendule tournant. Au prochain lancement, une partie encore en cours revient sur la page
  Échiquier, et la page Accueil la montre en premier. Une autre partie ouverte sur l'échiquier entre-temps laisse la
  tienne continuer : *Revenir à la partie* la ramène.
- À la fin, la carte dit comment la partie s'est terminée et ce qu'elle a fait à ton classement ; la partie arrive dans
  ta base Lichess, avec l'analyse de Lichess quand il y en a une, son bilan à un clic, comme tes autres parties
  terminées (voir [Tes parties terminées](#tes-parties-terminées)). *Une autre partie* cherche de nouveau avec la même
  cadence.

### Arènes et tournois suisses
<!-- en: arenas-and-swiss-tournaments 042061fe -->

La moitié *Tournois* de la section **Sur Lichess** de la page Jouer rejoint une arène de Lichess, ou un tournoi suisse
d'une de tes équipes, et joue ses parties depuis RookStudio. La carte *Tournois Lichess* de la page Lichess y mène
aussi, *Rejoindre* sur la ligne d'une arène (voir [Les tournois Lichess](#les-tournois-lichess)).

- **Ce que Lichess permet.** Sa Board API ne joue que des parties rapides, classiques et par correspondance, jamais de
  blitz ni de bullet en tournoi : ces arènes-là se suivent dans la mosaïque et se jouent sur lichess.org, et leur
  ligne le dit, avec le lien. La section liste les arènes rapides et classiques que tu peux jouer.
- **La première fois**, Lichess demande une permission de plus, `tournament:write` (pour rejoindre un tournoi, s'y
  mettre en pause et le quitter) ; `board:play`, pour jouer, est déjà donnée. *Autoriser sur Lichess…* (Entrée) ouvre
  la page de Lichess ; ton compte reste associé en attendant, et le jeton qu'elle remplace est révoqué.
- **Rejoindre une arène** : ses conditions telles que Lichess les juge, chacune avec tes chiffres (*Au moins 20 parties
  classées en Rapide (toi : 312)*), une condition manquante dite avant que tu rejoignes ; ses règles en une ligne
  chacune, et *Les règles complètes sur lichess.org* ; *Rejoindre* (Entrée). Un refus est dit avec les mots de
  Lichess : pour une arène réservée aux membres d'une équipe, *Ouvrir l'équipe sur lichess.org*, car RookStudio ne gère
  pas les équipes.
- **En attendant une partie** : Lichess n'apparie un joueur absent de la page de l'arène que pendant une minute, aussi
  RookStudio te réinscrit-il chaque minute pendant que tu attends ; fermé, il ne le fait plus, et tu n'es plus apparié,
  tes points restant. La partie suivante s'ouvre d'elle-même sur la page Échiquier.
- **La ligne de l'arène**, au-dessus de la section et de la carte de la partie : ton rang sur tant de joueurs, tes
  points, la série (*le prochain gain compte double*), le temps restant, et *Pause* (Alt+P), qui te retire après la
  partie en cours, tes points restant ; *Retour dans l'arène* (Alt+P) te réinscrit. La page de l'arène est demandée
  toutes les 15 secondes ; après un 429 (trop de requêtes), son classement attend une minute et le dit, tes parties
  continuant.
- **Jouer** : comme toute partie sur Lichess (voir [Une partie sur Lichess](#une-partie-sur-lichess)), avec *Berserk*
  là où l'arène le permet, avant ton premier coup seulement (la moitié de ta pendule, un point de plus si tu gagnes) ;
  Lichess ne permet ni de reprendre un coup ni d'annuler la partie dans un tournoi. Une partie de tournoi dont tu ne
  joues pas le premier coup à temps est perdue, pas annulée, et la carte le dit. Sous la carte, la partie précédente à
  sa fin : *Gagnée en 34 coups · +2 points · la série commence*.
- **Les aides** restent verrouillées tant que tu es dans l'arène une fois qu'elle a commencé, entre deux parties
  aussi, puisque la suivante arrive en quelques secondes : le moteur, l'arbre, le cloud, les tables, l'assistant et les
  rapports (le dossier d'un joueur, un rapport d'ouverture, un tableau croisé). *Pause* te les rend (voir
  [Fair-play](#fair-play)).
- **Sa fin** : ton rang et tes points ; chaque partie est dans *Lichess · ton nom*, comme tes parties terminées (voir
  [Tes parties terminées](#tes-parties-terminées)), leur bilan proposé alors : *Faire le bilan de la dernière partie*,
  *Ses parties dans la liste*. Fermer RookStudio pendant une arène demande d'abord : *Mettre en pause et fermer*, ou
  *Rester* (Échap).
- **Un tournoi suisse**, pour les membres de son équipe : sa ronde sur tant, le classement après sa dernière ronde
  (points, départage, performance), tes parties ronde par ronde, et l'heure de la ronde suivante. Deux minutes avant,
  un avis dans le coin de la fenêtre le dit où que tu sois ; ta partie s'ouvre d'elle-même sur la page Échiquier, et une
  partie dont tu ne joues pas le premier coup à temps est perdue. *Ses parties dans la mosaïque* montre les parties de
  la ronde de ses huit premiers. Une fois qu'il est terminé, *Le garder dans une base…* prend ses parties, chacune avec
  sa ronde, et son classement (son fichier TRF) dans *Tournois Lichess* ou une autre de tes bases : le tableau croisé
  de la base le lit alors, exemptions et arrivées tardives comprises (voir [Tournois et tableaux
  croisés](#tournois-et-tableaux-croisés)), à un clic avec *Ouvrir son tableau croisé*.

Ce qui quitte la machine : pour suivre, le tournoi demandé et ses parties, sans compte ; pour rejoindre, avec ton
jeton, l'inscription, la pause et tes coups. Une requête à la fois, une minute après un 429. Ctrl+K a *Jouer une arène
ou un tournoi suisse sur Lichess*, *Rejoindre l'arène : …*, *Pause dans l'arène*, *Revenir à l'arène*, *Berserk* et
*Garder le suisse dans une base…*.

## La page Lichess
<!-- en: the-lichess-page 0c6f9833 -->

Tout ici est facultatif : RookStudio fonctionne sans compte et sans Internet, et ce qui part chez Lichess est indiqué
là où tu le décides. Sa colonne de droite contient les services : les diffusions, les tournois de Lichess, Lichess TV,
les joueurs suivis, les évaluations gardées hors ligne, *Tes moteurs sur lichess.org* dès qu'un de tes moteurs est
proposé à l'échiquier d'analyse de lichess.org (voir [Ton moteur sur lichess.org](#ton-moteur-sur-lichessorg)), et ce
que l'analyse peut demander en ligne.

### Associer ton compte
<!-- en: linking-your-account 8b1bc649 -->

*Associer le compte…* ouvre lichess.org dans le navigateur, où tu autorises RookStudio sans lui donner de mot de
passe. Lichess renvoie le navigateur vers 127.0.0.1, sur un port que seul RookStudio écoute pendant l'association ;
sans réponse en 5 minutes, l'association s'arrête d'elle-même. Quand aucun navigateur ne s'ouvre, l'adresse s'affiche,
à coller dans un navigateur de cette machine.

Une fois le compte associé, la carte donne son nom et la date d'expiration de son jeton (un an après ; *Renouveler…* un
mois avant). RookStudio ne lit que ton nom et tes données publiques, et en demande plus quand une fonction en a
besoin : lire les diffusions, par exemple. Le jeton est gardé dans ton profil, lisible par toi seul ; *Dissocier* le
révoque chez Lichess et le supprime ici, et tes bases restent. Un compte associé apporte l'explorateur d'ouvertures de
Lichess dans l'onglet Arbre (les maîtres, tous les joueurs, un joueur), et tes parties synchronisées plus vite, tes
parties privées aussi.

### Les parties d'un joueur
<!-- en: a-players-games 3d841cf1 -->

*Les parties d'un joueur* importe les parties publiques d'un joueur Lichess, toi ou un autre, dans une base
(*Lichess · nom*), sans compte aussi : *Importer*, puis *Synchroniser* pour les parties jouées depuis. Les parties
arrivent par lots, les plus anciennes d'abord, et ce qui est reçu reste si tu annules. *Ce qui est demandé* : les
cadences (toutes ; blitz, rapide et classique ; classique et correspondance), depuis quand (la première partie, il y a un an,
une date), les échecs standard et le Chess960 seulement. Les parties gardent leur lien vers lichess.org, leurs pendules
et les évaluations de Lichess. *Compter les nouvelles parties à l'ouverture de la page Accueil* interroge Lichess au
plus une fois par heure, avec le nom du joueur.

### Tes études Lichess
<!-- en: your-lichess-studies 11bcbad8 -->

*Tes études*, sur la page Lichess, liste les études de ton compte associé, les plus récemment modifiées d'abord. La
première fois que la carte est ouverte, RookStudio demande à Lichess la permission de lire tes études (*Autoriser sur
Lichess…*) : avec elle, tes études privées et non listées sont listées aussi ; *Sans elle*, seulement les publiques.
La liste de Lichess donne le nom de chaque étude et la date de sa dernière modification ; ses chapitres s'affichent une
fois qu'elle a été lue, et sa visibilité pour les études créées depuis RookStudio. L'étude d'un autre vient par son
lien (voir [Liens Lichess](#liens-lichess)).

Un clic sur une étude la lit la première fois : ses chapitres s'affichent, et restent dans ton profil, hors ligne
aussi. *Ouvrir un chapitre sur l'Échiquier* (ou un double clic dessus) l'ouvre, vu du côté d'où Lichess le montre.
*Importer dans une base* met chaque chapitre dans une base, comme une partie : *Études Lichess*, créée si elle manque,
ou une autre choisie à côté. Refait, ou avec *Relire* une fois l'étude importée, seuls les chapitres modifiés depuis
sont remplacés et les nouveaux ajoutés ; une partie que tu y as supprimée reste supprimée. *En faire une étude de
théorie* montre ses commentaires dans l'onglet Arbre, relus avec l'étude (voir [Études de théorie](#études-de-théorie)) ;
une étude jamais lue est d'abord lue.

*La suivre* : à l'ouverture de la page Lichess (toutes les cinq minutes au plus), RookStudio demande à Lichess si
chaque étude suivie a changé, une requête chacune, sans la télécharger. Une étude qui a changé le dit, dans la carte et
sur sa ligne quand la carte est repliée, avec *Relire* : rien n'est téléchargé avant. Après une lecture, les chapitres
modifiés depuis la lecture précédente sont marqués.

**Depuis l'Échiquier**, *lichess.org…* › *Vers une étude Lichess…* ouvre à côté de l'échiquier une carte qui envoie la
partie dans une de tes études (une étude pleine, à 64 chapitres, n'est pas proposée) ou dans une nouvelle, dont le nom
et la visibilité se choisissent là : privée, non listée ou publique, privée d'abord. Le nom du chapitre, le côté d'où
il est vu et si les commentaires de l'assistant partent aussi (comme simples commentaires après ceux du coup) se
choisissent dans la même carte ; Entrée envoie, Échap l'abandonne. Ce qui part, c'est la partie en PGN : ses en-têtes,
ses coups et variantes, tes commentaires et symboles, ses flèches et cases, ses pendules ; les données des moteurs, les
évaluations et tout le reste de ce qui est à toi restent ici. Le premier envoi demande à Lichess la permission d'écrire
dans tes études. Une fois l'envoi fait, *L'ouvrir sur lichess.org* ou *Copier son lien*.

Plus tard, le bandeau au-dessus des coups dit *Envoyée à Lichess · 2 modifications depuis*, avec *Envoyer les
modifications* : les coups, variantes et notes du chapitre sont remplacés sur Lichess par ceux de la partie, ses
balises aussi quand elles ont changé. Rien ne part de lui-même. Un chapitre ouvert depuis tes études dit *De ton étude
Lichess*, et envoie ses modifications de la même façon.

Les limites de Lichess : 64 chapitres par étude, 30 nouvelles études par jour.

### Diffusions officielles
<!-- en: official-broadcasts 9c582d92 -->

Les diffusions officielles en direct, les plus connues d'abord (*Toutes les diffusions en direct* pour les autres), et
les prochaines. *Suivre* ouvre une ronde dans une mosaïque sur la page Lichess : la carte de chaque partie avec les
noms, les pendules, le dernier coup, la barre d'évaluation, et le score et la ligne du moteur choisi dans *Analysées
par* ; à droite, la partie choisie avec ses derniers coups, *Suivre sur l'échiquier* et *Copier le PGN*, puis la ronde.
Les parties sont analysées sur ta machine, avec ton moteur, autant à la fois que le permettent les cœurs que laissent
les tournois, la partie choisie en premier, et seulement tant que la mosaïque est affichée. Sur l'échiquier, la partie
avance à chaque coup reçu ; revenir en arrière pour l'analyser la laisse avancer derrière, et *Retour au direct* la
ramène ; *Retour à la mosaïque* (Échap) revient à la ronde.

Lichess permet de suivre quatre rondes à la fois sans compte, huit avec un compte. Depuis août 2026, il ralentit
fortement une diffusion suivie sans un compte autorisé à lire les diffusions : la première fois, RookStudio demande
cette permission (*Autoriser sur Lichess…*, ou *Suivre sans elle*). Une ronde dont toutes les parties sont terminées va
dans la base *Diffusions*.

### Plus loin avec une diffusion
<!-- en: more-of-a-broadcast 3d035f0f -->

À droite de la mosaïque d'une diffusion, sous la ronde, **le classement** : le rang, le titre, le classement Elo et les
points de chaque joueur tels que les organisateurs les donnent (la fédération, l'équipe, la performance et la
variation de classement sous la souris), les douze premiers ou tous ; pour une compétition par équipes, les équipes
d'abord, par points de match puis par points de partie. Lichess les donne sans compte ; ils sont redemandés toutes les
deux minutes tant que la mosaïque est affichée.

*Tout le tournoi*, à côté du nom d'une ronde, suit toutes les rondes en cours du tournoi, et chaque suivante dès
qu'elle commence : la mosaïque montre la ronde en direct, et **Les rondes** au-dessus en montre une autre (un clic sur
celle en direct suit de nouveau les suivantes). Chaque ronde va dans la base *Diffusions* quand elle se termine.
*Tableau croisé*, à côté du classement, montre le tableau croisé des rondes suivies à la place des échiquiers (voir
[Tournois et tableaux croisés](#tournois-et-tableaux-croisés)).

**Chercher dans les diffusions**, en haut des diffusions officielles, cherche une diffusion récente par son nom :
Lichess ne répond qu'à un compte autorisé à lire les diffusions, demandé la première fois. *Retour à la liste* montre de
nouveau les diffusions en cours.

### Liens Lichess
<!-- en: lichess-links c975cfa4 -->

Une adresse Lichess collée avec Ctrl+V sur la page Échiquier ou sur la page Bases est reconnue sans rien demander à
Lichess : une partie (`lichess.org/abcd1234`, avec `/white`, `/black` ou un coup après `#`), une étude ou un de ses
chapitres, une ronde ou un tournoi de diffusion, une arène, un tournoi suisse, un joueur (`lichess.org/@/name`) ou tes
favoris. Une adresse que RookStudio ne connaît pas est signalée comme telle, et rien n'est demandé.

Une carte dit alors ce que c'est, où vont ses parties et ce qui est demandé à Lichess ; Entrée le fait, Échap
l'abandonne :

- une partie ou un chapitre s'ouvre sur l'échiquier, du côté et au coup que montre l'adresse, ou va dans une base ;
- une étude, une diffusion, une arène, un tournoi suisse ou tes favoris arrivent dans une base en arrière-plan, leurs
  parties comptées à mesure qu'elles arrivent, avec *Annuler* ; puis *Ouvrir la base* ;
- une ronde en cours peut plutôt être suivie en direct (voir [Diffusions officielles](#diffusions-officielles)) ;
- les parties d'un joueur vont dans sa base, par [sa synchronisation](#les-parties-dun-joueur).

La base proposée est celle choisie sur la page Bases quand le lien y a été collé, sinon *Parties Lichess*, *Études
Lichess*, *Diffusions*, *Tournois Lichess* ou *Favoris Lichess*, créée si elle manque ; une autre peut être choisie.
Une partie est demandée par son identifiant, sans compte ; une étude privée avec ton jeton ; les diffusions avec un
compte autorisé à les lire, demandé une fois, comme pour suivre une ronde ; tes favoris demandent un compte associé.

### Fair-play
<!-- en: fair-play ca194626 -->

Lichess interdit toute aide pendant une partie. Avec ton compte associé, RookStudio s'y tient où que tu joues, dans le
navigateur ou sur un téléphone :

- tant que dure une de tes parties en direct, la page Échiquier verrouille ses onglets Analyse, Arbre, Références et
  Questions, et ni le moteur, ni la menace, ni les tables, ni le cloud, ni l'assistant ne regardent la moindre
  position ; les échiquiers ne montrent aucune des flèches de l'analyse ni son évaluation, y compris celles de la
  recherche que le verrouillage a arrêtée. Un bilan en cours s'arrête, les passes qu'il a faites étant gardées. Les
  tournois de moteurs continuent, sans analyse sur leur Échiquier. Tout revient de lui-même quand la partie se termine.
- une partie par correspondance verrouille le moteur, le cloud, les tables et l'assistant sur la partie de l'échiquier
  qui passe par sa position, du premier coup jusqu'à ce qui la suit, y compris une position de cette partie montrée
  depuis une variante ; Lichess y autorise un livre et une base, si bien que l'arbre de tes bases et leurs parties de
  référence restent ouverts. Avant son premier coup, elle ne verrouille que la position de départ : toute partie y
  passe. Sur toute autre partie, tout fonctionne.
- tant que tu es dans une arène depuis RookStudio, chaque aide reste verrouillée entre deux parties aussi, les rapports
  avec elles, jusqu'à *Pause* (Alt+P) ou la fin de l'arène (voir [Arènes et tournois
  suisses](#arènes-et-tournois-suisses)).

Un bandeau sur la page Échiquier dit quelle partie verrouille quoi. La carte Lichess de la page Accueil liste tes
parties en cours, celles où c'est ton tour d'abord, chacune avec sa pendule ou le temps qui reste pour jouer, et
*Ouvrir sur lichess.org*, ou *Sur l'Échiquier* une fois que RookStudio peut jouer tes parties (voir [Une partie sur
Lichess](#une-partie-sur-lichess)). Les parties de ton bot Lichess ne sont pas les tiennes et ne verrouillent rien ;
pendant ta propre partie en direct, la recherche du bot n'est pas montrée (voir [Un bot Lichess](#un-bot-lichess)).
Lichess TV ne montre jamais tes propres parties, même à la une, et les moteurs de la mosaïque se reposent pendant ta
partie en direct (voir [Lichess TV](#lichess-tv)). Ton moteur proposé à l'échiquier d'analyse de lichess.org se met en
pause pendant ta partie en direct, et n'analyse jamais les positions de tes parties par correspondance (voir [Ton
moteur sur lichess.org](#ton-moteur-sur-lichessorg)).

Tes parties en cours sont demandées avec le jeton de ton compte quand la page Accueil s'affiche, toutes les trente
secondes tant que dure une partie en direct et toutes les dix minutes tant que dure une partie par correspondance. La
page Lichess propose une fois d'être plutôt averti de leur début et de leur fin, pendant que RookStudio est ouvert :
une permission de plus, lire tes défis ; la permission de jouer tes parties (voir [Sur Lichess](#sur-lichess)) en est
avertie par le même flux. RookStudio ne lit les coups que des parties que tu joues depuis lui, et ne joue que les coups
que tu joues. Sans l'une ni l'autre permission, le verrouillage vient quand la page Accueil s'affiche.

### Tes parties terminées
<!-- en: your-finished-games 3109239c -->

Quand une de tes parties se termine, RookStudio l'amène dans *Lichess · ton nom*, une base créée si elle manque, avec
ses pendules, son ouverture et, quand tu as demandé à Lichess de l'analyser, les évaluations de Lichess. Un bandeau sur
la page Échiquier, et une ligne sur la page Accueil, disent comment elle s'est terminée ; *Faire son bilan* (Entrée)
redemande à Lichess son analyse, qui a pu être demandée entre-temps, ouvre la partie et lance son bilan.
*Pas maintenant* (Échap) met le bandeau de côté.

L'analyse de Lichess s'affiche alors à côté de celle de ton moteur : sa courbe sur le graphe, et la précision de chaque
analyse sous les chiffres des deux camps dans l'onglet Bilan, la même formule pour les deux. *Faire aussitôt le bilan
de mes parties terminées*, sur la page Lichess, lance le bilan de lui-même, sauf si l'Échiquier contient une partie
aux modifications non enregistrées ou un travail en cours ; le bandeau le propose alors. Les parties d'une arène
arrivent de la même façon, leur bilan proposé à la fin de l'arène.

### Joueurs suivis
<!-- en: followed-players 9a60f4cb -->

La carte *Joueurs suivis* de la page Lichess liste les joueurs que ton compte suit sur Lichess et ceux que tu ajoutes
par leur nom, chacun avec un point : rouge pendant qu'il joue, avec la cadence de la partie, vert quand il est en
ligne. Leur statut est demandé toutes les quinze secondes tant que la carte ou leur mosaïque est affichée, sans compte.

*Leurs parties dans la mosaïque* montre les parties qu'ils jouent dans la mosaïque des diffusions, chacune analysée
par ton moteur, huit au plus, comme Lichess le permet à une adresse, les rondes que tu suis et les parties de Lichess
TV comptées avec elles ; Lichess montre leurs coups avec trois coups de retard, à tout le monde. Une partie qui se
termine reste avec son résultat jusqu'à ce qu'une autre prenne sa place. Lire les joueurs que suit ton compte demande
une permission de plus, demandée la première fois ; *Sans elle*, les joueurs ajoutés par leur nom sont suivis. Ton
propre compte et tes propres parties n'y viennent jamais.

### Lichess TV
<!-- en: lichess-tv 820042bb -->

La carte *Lichess TV* de la page Lichess liste les chaînes d'échecs standard : *Mieux classés*, *Bullet*, *Blitz*,
*Rapide*, *Classique*, *UltraBullet*, *Bots* et *Ordinateur* (des joueurs contre l'IA de Lichess) ; les chaînes des
variantes ne sont pas montrées. Chacune donne la partie que Lichess met à la une sur elle, ses deux joueurs avec leurs
classements, et sa cadence. La liste est relue quand la carte s'affiche, au plus une fois toutes les
30 secondes.

*La mosaïque* montre les parties en cours de la chaîne dans la mosaïque des diffusions, sa troisième source : la ligne
en haut passe de l'une à l'autre (*Diffusions*, *Joueurs suivis*, *Lichess TV*, *Tournois Lichess*), et les chaînes
sont des pastilles en dessous. Les mêmes tuiles, un de tes moteurs pour chaque partie, la partie choisie à droite avec *Suivre sur
l'échiquier*. Lichess liste jusqu'à 30 parties par chaîne, et permet à une adresse de suivre 8 parties à la fois, les
rondes que tu suis (une chacune) et les parties des joueurs suivis comptées avec elles : la mosaïque montre ce qui reste,
la partie à la une d'abord. Une partie terminée montre son résultat quelques secondes, puis laisse sa place à une autre
de la chaîne. Lichess montre les coups avec trois coups de retard à quiconque regarde, et la mosaïque le dit.

*L'Échiquier* (ou un double clic sur une tuile) ouvre la partie sur l'Échiquier, qui la suit ; la carte au-dessus de sa
colonne dit ce qu'elle est : *Lichess TV · Blitz · la partie à la une, 3+0*. Quand elle se termine, l'Échiquier passe de
lui-même à la partie à la une suivante de la chaîne, la partie quittée à un Ctrl+Tab ; *Rester sur cette partie* garde
l'échiquier sur elle. *Garder cette partie…* la met, à sa fin, dans une base de ton choix : *Lichess TV*, créée si elle
manque, ou une autre des tiennes ; d'ici là, elle attend dans ton profil, même si RookStudio se ferme. *Retour à la
mosaïque* (Échap) revient à la mosaïque de la chaîne.

Sur la page Moteurs, la fiche d'un moteur a *Lichess TV : Bots, Ordinateur* : les meilleurs bots de Lichess, ou des
joueurs contre son IA, dans la mosaïque, ce moteur analysant chaque partie.

Tes propres parties ne s'affichent jamais, même à la une : celles de ton compte associé et de ton bot Lichess (voir [Un
bot Lichess](#un-bot-lichess)). Tant que dure une de tes parties en direct, les moteurs de la mosaïque se reposent (voir
[Fair-play](#fair-play)). Ce qui quitte la machine : la chaîne demandée et les parties suivies, sans compte ; rien n'est
gardé de lui-même. Ctrl+K a *Lichess TV*, chaque chaîne dans la mosaïque (et sur l'Échiquier une fois la carte lue),
et, pour la partie de l'Échiquier, rester sur elle ou passer à la partie suivante de la chaîne, la garder, et revenir à
la mosaïque.

### Les tournois Lichess
<!-- en: lichess-tournaments fe42cfa6 -->

La carte *Tournois Lichess* de la page Lichess liste les arènes de Lichess en cours et à venir, lues sans compte :
*Rapide*, *Classique* ou *Toutes*, chaque ligne avec son nom, sa cadence, quand (*encore 38 min*, *dans 40 min*), ses
joueurs, *Suivre*, et *Rejoindre* ou pourquoi pas : *Blitz : sur lichess.org* (un clic l'ouvre), *condition non
remplie* (sous la souris, laquelle, avec tes chiffres). Les arènes des variantes ne sont pas montrées ; la liste est
relue quand la carte s'affiche, au plus une fois par minute.

*Les tournois suisses de tes équipes*, en dessous, liste ceux des équipes dont tu es membre : lire tes équipes demande
une permission de plus, `team:read`, demandée une fois (*Lire mes équipes…*). Chaque ligne donne ses rondes, sa
cadence, quand, ses joueurs, et *Rejoindre* ; *Suivre* pendant qu'il se joue ; *Le garder dans une base…* une fois
terminé.

*Suivre* montre le tournoi dans la mosaïque des diffusions, sa quatrième source à côté de *Diffusions*, *Joueurs
suivis* et *Lichess TV* : les parties en cours de ses huit premiers, comme le tournoi les donne, une partie terminée
laissant sa place à la suivante ; Lichess montre leurs coups avec trois coups de retard, et permet à une adresse de
suivre huit parties à la fois, les rondes et les joueurs que tu suis comptés avec elles ; chacune analysée par ton
moteur, comme le fait la mosaïque. À droite, son classement, demandé toutes les 15 secondes : le rang, le classement,
la feuille de chaque joueur (les points de chaque partie, la plus récente en dernier comme sur lichess.org, les points
doublés d'une série en orange) et son score ; celui d'un suisse après sa dernière ronde. Un clic choisit une partie, un
double clic l'ouvre sur la page Échiquier ; ta propre partie ne s'affiche jamais. Un suisse ne liste pas ses parties en
cours : elles sont trouvées par joueur, ceux de ses huit premiers, à chaque ronde. Sans compte ; rien n'est gardé de
lui-même. Ctrl+K a *Tournois Lichess : arènes et tournois suisses*, *Suivre l'arène : …* (une fois que la carte a lu
la liste) et le tournoi suivi dans la mosaïque.

### Préparer une partie
<!-- en: preparing-a-game 2c8891af -->

La carte *Préparer une partie contre* de la page Lichess prend un nom Lichess et ouvre son dossier (voir [Le dossier
d'un joueur](#le-dossier-dun-joueur)) avec sa source Lichess cochée. Les parties de ce joueur sur la dernière année, en
blitz, rapide et classique, arrivent dans une base à part, *Lichess · nom*, synchronisée de nouveau la fois suivante,
et son profil public donne son niveau. Le dossier montre alors ce qu'il joue en ligne à côté de ce que tes bases ont
de lui (le joueur dont c'est le compte Lichess, une fois associé), tes parties contre lui (ton compte associé), et les
lignes qu'il joue souvent auxquelles tes répertoires ne répondent pas, sur les deux sources, les plus fréquentes
d'abord ; un clic sur l'une d'elles l'ouvre sur l'Échiquier, son onglet Arbre sur ses parties. Seuls son nom et ces
filtres partent chez Lichess. Pendant qu'une de tes parties contre lui se joue sur Lichess, le dossier ne montre rien
de lui.

### Les évaluations de Lichess, hors ligne
<!-- en: lichesss-evaluations-offline e972bc19 -->

La carte *Les évaluations de Lichess, hors ligne* garde sur ton disque ce que les serveurs de Lichess ont calculé pour
418 millions de positions (database.lichess.org, CC0) : pour chacune, l'évaluation, la profondeur et le premier coup
de sa recherche la plus profonde. Plus rien n'est demandé au réseau ensuite, et une évaluation s'affiche aussitôt
partout où Lichess en a une.

- **Ce qui est gardé** : toutes les positions (environ 3,6 Go), ou les ouvertures seulement (environ 1,5 Go), les
  positions à 24 pièces ou plus sur l'échiquier, pions compris. Le fichier de Lichess ne donne aucun numéro de coup :
  dans les parties de Lichess, 24 pièces ou plus, c'est 89 % des positions des 20 premiers coups et 8 % des suivantes.
  Les lignes de Lichess sont écartées : elles prendraient près de cinq fois la place, pour ce que ton moteur retrouve
  en une seconde.
- **Télécharger et préparer** (Entrée) lit le fichier de Lichess, environ 21 Go, pendant son téléchargement, et n'en
  garde que ce que tu as choisi ; *Un fichier déjà téléchargé…* lit à la place `lichess_db_eval.jsonl.zst` sur ton
  disque. Pendant la préparation, il faut un peu plus de deux fois la place de ce qui est gardé ; la carte dit ce que
  ton disque a de libre.
- Les positions lues répondent par quelques millions à la fois, sans attendre la fin. *Annuler*, une coupure du
  réseau ou la fermeture du programme : *Continuer* reprend le fichier là où il s'était arrêté, *Recommencer* oublie
  ce qui a été lu.
- Une fois prête, la carte donne les positions, la taille et la date du fichier. Lichess publie le fichier de nouveau
  chaque mois : *Mettre à jour* demande sa date à database.lichess.org et lit un fichier plus récent, les évaluations
  gardées répondant jusqu'à ce qu'il soit prêt. *Les retirer* libère le disque.

Elles s'affichent à trois endroits :

- l'onglet Analyse : une ligne au-dessus du moteur, *Lichess hors ligne · +0.31 · profondeur 46 · 6…e6*, le score des
  Blancs (un clic montre le coup sur l'échiquier) ;
- le bilan d'une partie : une position connue à la profondeur 30 ou plus est prise telle quelle, sans recherche, et le
  moteur cherche les autres (sa dernière passe, 5 secondes par position par défaut, va rarement plus loin) ;
- l'onglet Arbre : une colonne *Éval.*, l'évaluation après chaque coup.

Ce qui quitte la machine : la demande du fichier à database.lichess.org, et rien d'autre. Pendant qu'une de tes
parties se joue sur Lichess, elles se masquent comme le moteur (voir [Fair-play](#fair-play)).

### En ligne pour l'analyse
<!-- en: online-for-the-analysis 1f447190 -->

Trois services, chacun demandé à sa première utilisation (pour cette position, toujours, ou pas maintenant) et
réglable ici :

- **L'évaluation cloud de Lichess**, dans l'onglet Analyse : le FEN de chaque position affichée est envoyé, sans
  compte, et Lichess répond avec les lignes que ses serveurs ont déjà calculées, quand il les a. Ton moteur répond
  toujours.
- **Finales en ligne, jusqu'à 7 pièces** : pour une position de sept pièces ou moins que les tables de ton disque ne
  connaissent pas, son FEN est envoyé aux tables de Lichess, sans compte. Elles donnent le verdict et la distance de la
  fin, dans l'onglet Analyse, et chaque coup selon son issue dans l'onglet Finales, avec les lignes principales de ses
  coups (voir [L'onglet Finales](#longlet-finales)). Un gain que la règle des 50 coups change en nulle, un « gain
  maudit » dans les mots des tables, se dit « gain, mais nulle par la règle des 50 coups ». Avec huit pièces et des
  pions face à face, elles donnent le verdict et les coups jusqu'à la prochaine prise, promotion ou mat. La vérification
  des finales d'un tournoi terminé leur demande quelques positions par partie, une fois autorisée pour cette
  vérification ou toujours (voir [Vérifier les finales avec les tables](#vérifier-les-finales-avec-les-tables)).
- **L'explorateur de Lichess**, dans l'onglet Arbre : pour chaque position, son FEN et le filtre choisi sont envoyés
  avec ton jeton ; il demande un compte associé. Rien de tes bases n'est envoyé.

Les réponses sont gardées dans ton profil : celles des tables pour toujours, celles du cloud pendant un mois. Quand
Lichess demande une pause (trop de requêtes en peu de temps), le compte à rebours s'affiche ; hors ligne, ce qui
fonctionne encore est indiqué.

## L'assistant
<!-- en: the-assistant d3ffeeba -->

L'assistant commente tes parties et répond à tes questions à leur sujet, sur ta machine. Il s'appuie sur ce
qu'établissent ton moteur, tes bases et les tables de finales, et le dit en mots ; quand rien ne l'établit, il le dit
aussi. Il écrit en anglais ou en français (en anglais pour les autres langues de l'interface).

### Son modèle
<!-- en: its-model 1e5f4544 -->

L'assistant a besoin de son modèle, *RookStudio commentary v11*, téléchargé une fois pour toutes depuis huggingface.co
(environ 2,7 Go sur le disque, et environ 3 Go de mémoire quand il est actif), vérifié à l'arrivée ; un téléchargement
coupé reprend où il en était. La première carte de l'onglet Questions, de *Commenter ce coup* et du bilan le propose ;
*Un fichier GGUF sur le disque…* prend un autre modèle, qui répond aux questions mais n'a pas appris les commentaires
de RookStudio (ceux-ci restent des commentaires types). Seul le téléchargement du modèle passe par Internet : tes
parties, tes questions et ses réponses restent sur la machine.

Il tourne dans un processus à part, sur le GPU quand il y en a un disponible (un commentaire en un quart de seconde
environ), sinon sur le processeur (environ 3 secondes). Il est chargé à la première question ou au premier
commentaire, et déchargé après un quart d'heure d'inactivité, ou avec *Décharger* (sur la page Accueil, dans
**Réglages**, ou dans Ctrl+K). Pendant qu'un tournoi utilise le GPU, il répond sur le processeur (*Prendre le GPU quand
même* si tu le souhaites) ; un pilote graphique qui échoue l'envoie sur le processeur, et *Réessayer le GPU* réessaie.
*Réglages* > *Assistant* : son modèle, le GPU ou le processeur, les minutes avant qu'il soit déchargé (0 : gardé
jusqu'à *Décharger*).

Sans le modèle, tout le reste fonctionne : l'analyse, les chiffres du bilan, les commentaires types, l'arbre.

### Questions
<!-- en: questions e1e09fc4 -->

L'onglet Questions (Alt+5) prend une question sur la position ou la partie, tapée ou choisie parmi les suggestions :
pourquoi le dernier coup, quelle est la menace, le meilleur coup, qui a joué cette position, quelle est cette ouverture.
L'assistant sait expliquer un coup, donner le meilleur coup ou la menace, essayer un coup, en comparer deux ; donner
les parties de référence et les coups joués dans une base ; nommer l'ouverture ; dire ce que disent les tables de
finales. Chaque réponse montre ce sur quoi elle s'appuie (la recherche du moteur, l'arbre, les noms d'ouvertures, les
tables) et ses gestes : jouer le coup, montrer la ligne en aperçu, ouvrir la partie la plus forte, ajouter la réponse
à la notation. Une question au-delà de ce qu'il sait faire reçoit un « Je ne peux pas le dire » et ce qu'il peut
montrer à la place.

Le moteur de l'onglet Analyse répond pour lui, dans un processus à part (un thread, 300 000 nœuds par position) : les
questions ont besoin d'un moteur dans la bibliothèque.

### Les commentaires de l'assistant
<!-- en: comments-by-the-assistant 009c27cb -->

*Commenter ce coup* (Alt+C, ou Ctrl+K) écrit le commentaire de l'assistant sur le coup affiché, et *Commenter les
moments clés*, dans le bilan, un commentaire sur chaque moment clé. Ses commentaires sont affichés en gris bleu et en
italique, après une bulle ; au curseur, ce sur quoi chacun s'appuie : le moteur, les faits des positions, la base de la
théorie, le modèle ou un commentaire type. Sans le modèle, les commentaires sont des commentaires types, écrits à
partir des mêmes faits.

Un coup qui a déjà un commentaire (le tien, l'analyse de Lichess d'une partie, celui de l'annotateur d'une partie) le
garde tel quel : celui de l'assistant vient après lui, à part, dans un encadré de sa couleur sous son nom. Dans le PGN,
ce sont deux commentaires à la suite, celui de l'assistant marqué `[%rs assistant]` ; un autre programme lit deux
commentaires, et RookStudio redonne à chacun sa place. Ctrl+Z retire le commentaire de l'assistant. Un double clic le
corrige, et il devient le tien : sur un coup qui a déjà un commentaire de toi, le texte corrigé est ajouté après lui,
en un seul commentaire. Une réponse de l'onglet Questions ajoutée à la notation est aussi un commentaire de
l'assistant.

## Suites de tests
<!-- en: test-suites 42c2b2ba -->

La page **Suites** fait passer une suite de tests EPD : des positions avec leur meilleur coup (`bm`), les coups à
éviter (`am`), ou, pour la Strategic Test Suite, les points de plusieurs coups (`c0 "f5=10, Be5+=2"`).

- **Suite** : un fichier EPD, ou un livre EPD de la bibliothèque. Les lignes sans position légale ou sans aucune de
  ces opérations sont écartées (leur nombre est affiché).
- **Moteurs** : autant de moteurs de la bibliothèque que tu veux, avec leurs options ; *Hash* et *Threads* imposent
  une valeur à chacun (0 = celle de chaque moteur).
- **Temps**, **Nœuds** ou **Profondeur** par position. *Trouvé depuis (demi-coups)* : une recherche s'arrête dès
  qu'une solution est restée le premier coup du moteur pendant ce nombre de demi-coups de profondeur supplémentaires
  (0 = chaque recherche prend tout son temps).
- **Parallélisme automatique** : avec un temps, les cœurs physiques − 1 divisés par les threads des moteurs ; avec des
  nœuds ou une profondeur, tous les threads matériels.

Une position est résolue quand le coup final du moteur est une solution : l'un des `bm`, aucun des `am`, ou, avec des
points seulement, le coup qui en a le plus. Son temps de résolution est le moment où le moteur s'est fixé sur ce coup.
La grille montre les positions face aux moteurs, ✓ avec le temps de résolution ou ✗ avec le coup joué, et les points
STS ; les totaux viennent en premier : positions résolues, leur temps moyen, et le score STS. Un passage terminé est
gardé dans `suites/` dans le profil ; *Comparer avec un passage enregistré…* ajoute les colonnes d'un passage
précédent de la même suite, pour comparer des versions d'un moteur. *Exporter en CSV…* écrit la grille.

## Nouveau tournoi
<!-- en: new-tournament 6d169de4 -->

**Tournois → Nouveau tournoi** ouvre la configuration : une carte de lignes repliées qui disent comment les parties
se jouent, les participants avec la bibliothèque de moteurs à côté, et un résumé en direct à droite (nombre de
parties, parties par moteur, durée estimée, ouvertures, réglages communs, handicaps de temps). La configuration du
dernier tournoi est gardée : tester de nouveau deux versions d'un moteur revient donc à choisir A et B, puis
**Lancer**.

Tout, sur la page, peut se faire au clavier : Tab et les flèches passent d'une ligne, d'une liste ou d'un bouton à
l'autre, Entrée ou Espace ouvre une ligne ou une liste, et les touches des
[participants et de la bibliothèque](#participants-et-bibliothèque) y fonctionnent. **Lancer** crée le tournoi et le
joue ; **Mettre en file d'attente** le garde pour plus tard (voir
[File d'attente et relance](#file-dattente-et-relance)).

### Le résumé et ses vérifications
<!-- en: the-summary-and-its-checks 5609344a -->

Le résumé donne la mémoire que peuvent prendre les tables de hachage : deux processus de moteur par partie jouée en
même temps, et les moteurs qui attendent leur partie suivante, chacun avec son `Hash`. Au-delà de la mémoire libre, il
avertit (le système va utiliser le swap), en nommant le moteur qui a le plus grand `Hash` ; au-delà de la mémoire
installée, le tournoi ne démarre pas. `--run` dit la même chose. Quand le budget de mémoire des **Réglages** laisse
jouer moins de parties à la fois que le parallélisme, le résumé dit combien (chaque partie prend le `Hash` de ses deux
moteurs et ce que leurs processus prennent en plus), et la ligne *Parallélisme* aussi ; de même pour les parties avec
un moteur qui utilise un GPU, face aux *GPU*. Il nomme aussi les moteurs dont le programme a changé depuis leur dernier
tournoi (une nouvelle compilation), avec *Ce qui a changé* quand la fiche du moteur liste de nouveaux défauts ou de
nouvelles options (voir [Programmes recompilés](#programmes-recompilés)).

Ce qui n'irait pas est dit avec le moteur, la ressource et la correction, un bouton sous la ligne (la palette de
commandes liste les mêmes corrections) :

- un programme ou un dossier de travail introuvable : *Ouvrir le moteur* (sa fiche dans la page Moteurs),
  *Localiser…* ; un livre d'ouvertures introuvable : *Localiser…* ;
-  un moteur dont les `Threads` propres (son réglage, sinon son défaut : certains moteurs prennent tous les cœurs)
  dépassent la moitié des threads matériels, ou les threads dont dispose chaque partie avec les parties jouées à la fois
  (les cœurs physiques avec une pendule) : *Ramener Threads à N* règle l'option du moteur dans la bibliothèque, *Ouvrir
  le moteur*. Des `Threads` donnés par la configuration au-delà de ce qu'a la machine reçoivent *Ramener Threads* là où
  ils sont donnés (la case du participant, les réglages communs). La liste des participants marque un tel moteur d'un
  *!*, les moteurs du match sont signalés en dessous ;
- une valeur d'option qu'un moteur refuse (hors de sa plage, pas l'un de ses choix, une option qu'il n'a plus) :
  fastchess ne l'envoie pas, et le moteur joue avec son défaut ;
- les tables Syzygy qu'utilise le tournoi : les dossiers de l'adjudication et ce que désigne le `SyzygyPath` de chaque
  moteur (plusieurs dossiers séparés par `;` sous Windows, `:` ailleurs). Pour les pièces utilisées (les *Pièces max*
  de l'adjudication, sinon les plus grandes tables trouvées), chaque fichier WDL (`.rtbw`) doit être là, et aucun
  fichier tronqué (un fichier Syzygy fait 16 octets de plus qu'un multiple de 64 ; les moteurs laissent de côté un
  fichier d'une autre taille, et certains s'arrêtent quand ils le rencontrent) : *Syzygy 6 pièces : 14 fichiers
  manquants dans D:\TB (KRPPvKR.rtbw…)*, *Adjudication jusqu'à 6 pièces, mais seulement des fichiers de 5 pièces*,
  *SyzygyPath de Alpha désigne un dossier sans fichier de tables*, avec *Ouvrir le dossier* et *Descendre à 5
  pièces* ; l'infobulle liste les fichiers. Ce sont des avertissements : l'adjudication et les moteurs se passent des
  fichiers manquants. Seule une adjudication sans dossier, ou dont les dossiers ne contiennent aucune table, arrête le
  tournoi à son lancement (fastchess la refuse) : *Localiser…* choisit un autre dossier. Les dossiers sont lus en
  arrière-plan, et de nouveau toutes les quelques secondes tant que le résumé est affiché.

Un lancement refusé nomme les mêmes problèmes, avec leurs corrections et *Ouvrir le journal*. Chaque problème a un
**?** qui ouvre son entrée dans [Dépannage](#dépannage).

### Profils et modèles
<!-- en: profiles-and-templates df08e65d -->

**Profils.** Les cinq premières lignes, *Format*, *Cadence*, *Ouvertures*, *Adjudication* et *Réglages des moteurs*,
sont chacune un profil nommé : sa liste en choisit un, la ligne à côté dit ce qu'il joue (« 40 coups en 15 min,
répété », « nulle 40/8/10 cp · TB 6 pièces » ; son infobulle donne la même chose telle que cutechess et fastchess
l'écrivent, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), et ✎, ou un clic sur cette ligne, ouvre les
détails sur place, en dessous. Une modification faite pour ce tournoi seulement met un point à côté du profil, et sa
liste et ses détails proposent *Mettre à jour* (le profil prend la modification, pour chaque tournoi et chaque modèle
qui l'utilisent), *Revenir* et *Enregistrer comme profil…* (sous un nouveau nom). Le programme est livré avec des
préréglages :

- cadences : 10+0.1 et 60+0.6 (les tests courts et longs de fishtest), 2+1, 40/4, 40/15 et 40/20 (40 coups en 4, 15 ou
  20 minutes, répété, comme les jouent les listes de classement), et 200k nœuds par coup ;
- formats : SPRT gain [0, 3] et non-régression [-5, 0], un match de 1000 parties, un toutes rondes, un gauntlet, un
  suisse de 9 rondes, un KO et un match par équipes ;
- adjudications : aucune, une standard (nulle à partir du coup 40 après 8 coups à moins de 10 cp, défaite après
  3 coups à 600 cp) et la même avec les tables jusqu'à 6 pièces ;
- réglages des moteurs : ceux de chaque moteur, ou Hash 16, 64 ou 256 Mo avec 1 thread et les livres propres des
  moteurs désactivés ;
- ouvertures : pas de livre, ou les 960 positions du Chess960.

Un profil à toi enregistré sous le nom d'un préréglage prend sa place ; le supprimer (depuis sa liste, après
confirmation) fait revenir le préréglage, et les configurations et modèles qui l'utilisaient gardent ses valeurs. Les
profils sont gardés dans `profiles.json`, dans le dossier du profil, et la palette de commandes (Ctrl+K) les liste :
en choisir un l'applique à la configuration. La palette ouvre aussi chaque ligne de la configuration par son titre, et
charge ou enregistre des modèles. Les dossiers Syzygy ne sont dans aucun profil : ils appartiennent à cet ordinateur,
et l'adjudication et les moteurs les partagent. Les lignes *Parallélisme* et *PGN et divers* contiennent ce qui
n'appartient qu'à ce tournoi.

**Modèle** et **Nom**, à côté de *Lancer* : un modèle est la configuration entière, un profil pour chaque section plus
ce qu'aucun profil ne contient (le nom, les participants, le parallélisme, les fichiers PGN, la diffusion). Le menu en
charge un ou enregistre la configuration comme modèle (*Enregistrer le réglage comme modèle…*, qui demande avant de
remplacer un modèle du même nom), et la palette de commandes les charge aussi. Une section qui suivait son profil
quand le modèle a été enregistré suit ce profil, si bien que mettre à jour un profil met à jour chaque modèle qui
l'utilise ; une section modifiée pour ce tournoi garde ses propres valeurs. Les modèles enregistrés par des versions
précédentes se chargent tels qu'ils étaient, et leurs sections prennent les profils auxquels elles correspondent. Un
nom vide est formé à partir des moteurs.

### Format
<!-- en: format 29a36e7c -->

L'infobulle de chaque format dit ce qu'il est.

-  *Match* : deux moteurs, A (le moteur testé ; chaque statistique est donnée de son point de vue) et B (la référence).
  Un nombre fixe de parties, jusqu'à ce que tu l'arrêtes, ou un SPRT : elo0, elo1, alpha, beta, modèle (`normalized`
  utilise des bornes en nElo comme fishtest et OpenBench, `logistic` l'Elo classique, `bayesian` le BayesElo) et un
  nombre maximum de parties facultatif. Les profils SPRT gain et non-régression donnent les bornes habituelles, et la
  ligne sous les bornes dit combien de parties le test devrait durer. *Calculateur…* trace, comme le calculateur SPRT de
  fishtest, la probabilité que H1 soit acceptée et le nombre de parties attendu en fonction de la vraie différence
  d'Elo, à partir d'un taux de nulles attendu et du biais RMS des ouvertures ; *Utiliser ces bornes* recopie dans la
  configuration les bornes qui lui ont été données. *Biais Elo* (0 par défaut) est ajouté aux deux bornes, pour tester
  contre une version qu'on sait plus forte ou plus faible d'autant : [0, 3] avec un biais de 5 donne le SPRT [5, 8], que
  le résumé, la vue d'ensemble et le mode sans interface écrivent `SPRT [0, 3] + 5` ; le calculateur et la ligne sous
  les bornes en tiennent compte. *Parties min.* (0, aucune, par défaut) retient la décision tant que ce nombre de
  parties n'est pas joué, le LLR étant toujours calculé et affiché, pour protéger un test court d'un arrêt précoce dû à
  la chance : un LLR au-delà d'une borne avant ce moment est signalé sous la barre du LLR et le match continue ; une
  fois les parties jouées, la première paire qui se termine avec le LLR au-delà d'une borne termine le match. Un maximum
  de parties inférieur au minimum termine le match sans décision. C'est la façon la plus rapide de comparer deux
  versions d'un moteur, et il n'en faut pas plus.
  *Couleurs* (*Alternées* par défaut) dit qui a les Blancs : A et B à tour de rôle, ou *A toujours avec les Blancs* ou
  *B toujours avec les Blancs*, comme `-noswap` (et `-reverse`) de fastchess, pour les parties à handicap où le moteur
  le plus fort doit toujours avoir le camp handicapé, ou une ouverture essayée d'un seul côté. Les parties vont toujours
  par paires, toutes deux avec les mêmes couleurs, mais chaque partie a sa propre ouverture, la suivante du livre (comme
  `-games 1` de fastchess) : les paires et leurs statistiques restent, le score inclut l'avantage des Blancs, et le
  biais RMS, qu'inverser les couleurs annule, n'est pas affiché. Le résumé et l'en-tête du match l'indiquent.
- *Toutes rondes* : chaque moteur joue contre chacun des autres, *Cycles* fois, chaque fois deux parties avec les
  couleurs inversées (ce que cutechess appelle des rondes, de 2 parties par rencontre). Les rondes suivent les tables
  de Berger, si bien que chaque moteur joue une fois par ronde.
- *Gauntlet* : les premiers *Moteurs testés* (les têtes de série) jouent contre tous les autres, les autres ne jouent
  pas entre eux.
- *Suisse* : un nombre de *Rondes*. Chaque ronde est de deux parties, couleurs inversées, contre un moteur au score
  proche ; personne ne rencontre deux fois le même adversaire quand on peut l'éviter. Avec un nombre impair de
  moteurs, le moteur le moins bien classé qui n'a pas encore été exempt l'est, ce qui vaut un point.
- *KO* : les moteurs sont têtes de série dans l'ordre de leur liste, les meilleures têtes de série sont exemptes du
  premier tour quand le nombre de participants n'est pas une puissance de deux, et les deux premières têtes de série
  ne peuvent se rencontrer qu'en finale. Un match est de *Paires de parties par match* paires de parties ; un match à
  égalité reçoit une paire de plus à la fois, jusqu'à dix, après quoi la meilleure tête de série se qualifie.
- *Match par équipes* : deux équipes de moteurs, pour comparer une famille de moteurs, ou un ensemble de versions, à
  une autre dans son ensemble. *Appariements* : *Tous contre tous*, chaque moteur d'une équipe contre chaque moteur de
  l'autre équipe, un adversaire par ronde (un système Scheveningen ; les équipes peuvent être de tailles
  différentes), ou *Échiquier par échiquier*, le premier moteur de chaque équipe contre le premier de l'autre, le
  deuxième contre le deuxième et ainsi de suite (équipes de même taille). Chaque appariement est joué *Cycles* fois,
  chaque fois deux parties sur la même ouverture, couleurs inversées ; les couleurs de la première partie alternent
  d'un échiquier à l'autre et d'un cycle au suivant. *Équipe A* et *Équipe B* nomment les équipes (« Équipe A » et
  « Équipe B » sans nom) ; à côté, combien de moteurs chacune compte. Les équipes se font dans la liste des
  participants : la moitié haute de la liste est l'équipe A, la moitié basse l'équipe B (l'équipe A a le moteur en
  plus), jusqu'à ce que des moteurs soient mis dans une équipe.

### Participants et bibliothèque
<!-- en: participants-and-the-library a7ef2e55 -->

**Participants**, avec la **Bibliothèque** à côté (en dessous dans une fenêtre étroite). Chaque participant est une
ligne : sa place (une étoile pour les têtes de série d'un gauntlet), son nom, son Elo de référence, ce qu'il joue
autrement que les réglages communs (« Hash 512 Mo · 80 % du temps ») et ⋯, mis en évidence quand il a des réglages
propres. ⋯, un clic droit, ou la touche Menu les ouvre : son propre `Hash`, ses `Threads`, des nœuds ou une profondeur
fixes, un handicap de temps (un pourcentage de la cadence, pour une pendule), son propre *Livre*, *Ce moteur une fois
de plus, avec un autre livre*, les déplacements et *Retirer*. L'ordre de la liste est celui des têtes de série : fais
glisser les participants pour changer leur ordre (têtes de série en premier), ou appuie sur Ctrl+Haut ou Ctrl+Bas
pour les envoyer tout en haut ou tout en bas, Alt+Haut ou Alt+Bas pour les déplacer d'une place. Un clic avec Ctrl ou
Maj en sélectionne plusieurs ; Suppr (ou *Retirer*) les retire.

Dans un match par équipes, la place est l'équipe et l'échiquier, A1, A2… pour l'équipe A, B1, B2… pour l'équipe B,
dans l'ordre de la liste (échiquier par échiquier, A1 joue B1). Le menu ⋯ met le participant dans une équipe ;
*Équipe A* et *Équipe B* sous la liste mettent les participants sélectionnés dans l'une d'elles, et *Moitiés* refait
les équipes à partir des moitiés de la liste (la palette de commandes propose la même chose). Un moteur ajouté une
fois les équipes choisies rejoint la plus petite. Le résumé dit combien de moteurs compte chaque équipe, et un
lancement est refusé, avec la raison, quand une équipe est vide ou, échiquier par échiquier, quand les équipes sont de
tailles différentes.

La bibliothèque liste les moteurs actifs avec leur Elo de référence, triés par nom ou par Elo (clique sur la
colonne), un `!` sur ceux qui ont un problème (programme manquant ou recompilé, dernière vérification en échec :
l'infobulle dit lequel) et un ✓ sur ceux qui jouent déjà. Le filtre cherche dans les noms, les noms que les moteurs
annoncent, leurs auteurs, programmes, notes et groupes (Ctrl+F lui donne le clavier ; Entrée dans le filtre ajoute
les moteurs sélectionnés, ou le premier affiché) ; une puce par groupe ne montre que ce groupe, et *Ajouter tous ceux
affichés* ajoute le groupe entier. Double-clique sur un moteur, ou sélectionnes-en plusieurs avec Ctrl ou Maj
(Ctrl+A : tous ceux affichés) et appuie sur Entrée, pour les ajouter ; ou fais-les glisser à une place parmi les
participants. *Suggérer…* sélectionne les N moteurs actifs qui fonctionnent et dont l'Elo de référence est le plus
proche de celui du premier participant (le moteur testé d'un gauntlet), à 150 au plus par défaut ; Entrée les ajoute.
Pour un match, la bibliothèque remplit A, puis B (B est remplacé quand les deux sont choisis), et un moteur glissé sur
A ou B prend sa place.

### Livres propres des participants
<!-- en: participants-own-books 580e52cc -->

*Livre* : un livre d'ouvertures propre au participant, comme `book=`/`bookdepth=` de Cute Chess : un livre Polyglot
(`.bin`) ou PGN de la page Livres et la profondeur jusqu'à laquelle il est suivi, en demi-coups depuis le début de la
partie, ouverture du tournoi comprise (*tout* : tant que le livre a un coup ; pour un livre PGN, les 40 premiers
demi-coups de ses parties). Un livre PGN est d'abord transformé en livre Polyglot : les coups de chaque partie jusqu'à
cette profondeur, chaque coup pondéré par le nombre de parties qui le jouent ; les parties avec un coup illégal sont
écartées et comptées. Cela se fait en arrière-plan dès que le livre est choisi (progression sur le bouton *Livre*, dans
sa fenêtre et dans le résumé), et le résultat est gardé dans le dossier `book-cache` du profil jusqu'à ce que le fichier
ou la profondeur change ; un lancement qui arrive avant la fin l'attend. Un livre qui ne peut pas être préparé montre
pourquoi dans sa fenêtre, avec *Réessayer* ; chaque lancement réessaie aussi. Un tournoi joue à partir du livre préparé
quand il a été lancé ou repris, même si le fichier PGN change entre-temps.

Tant que la position est dans ce livre, le participant joue aussitôt le coup du livre, choisi selon les poids du
livre tels que les règle le *Hasard* des ouvertures (aussi dans la fenêtre *Livre*, le même pour tous les livres du
tournoi : voir [Ouvertures](#ouvertures)) ; puis son moteur prend le relais. Les ouvertures du tournoi, s'il y en a,
sont jouées d'abord, par chaque moteur ; les livres EPD ne donnent que celles-là et sont grisés dans la liste. Pour
comparer des livres avec un moteur, choisis *Ce moteur une fois de plus, avec un autre livre* dans son menu ⋯ : chaque
copie joue sous le nom `<engine> (<book>)`, si bien que ses résultats et son classement restent à part. Dans un match,
les boutons *Livre* sont à côté de A et de B, et un moteur peut jouer contre lui-même avec deux livres. Les coups de
livre sont marqués `{book}` dans le PGN ; survoler un nom dans le classement montre son livre. Les moteurs WinBoard
peuvent en avoir un aussi.

### Réglages des moteurs
<!-- en: engine-settings 7a93d075 -->

*Hash* et *Threads* imposés à chaque moteur, pour un match aussi (0 = ceux de chaque moteur ; ceux d'un participant,
depuis son menu ⋯, passent en premier) ; *Tables Syzygy pour les moteurs* donne les dossiers des tables (les mêmes que
ceux de l'adjudication) à chaque moteur qui a une option `SyzygyPath`, et *Livres propres des moteurs désactivés*
règle `OwnBook` à false là où elle existe, pour que chaque partie soit jouée à partir des ouvertures du tournoi. Les
deux sont désactivés par défaut.

### Cadence
<!-- en: time-control 672cb041 -->

Temps de base et incrément en secondes, nombre de coups par période facultatif, et une marge avant qu'une partie soit
perdue au temps ; un temps fixe par coup (`go movetime`, avec la même marge) ; un nombre fixe de nœuds par coup ; ou
une profondeur fixe en demi-coups (`go depth`), avec laquelle aucun moteur ne peut perdre au temps.

### Parallélisme
<!-- en: concurrency c02ee2cf -->

Le nombre de parties jouées en même temps. En mode automatique, il est mesuré au démarrage :

- nœuds ou profondeur fixes : une partie par thread matériel, puisque l'Hyper-Threading ne change pas les coups joués ;
- avec une pendule : les cœurs physiques − 1, pour que les moteurs ne se ralentissent pas les uns les autres.

La ligne et le texte sous le champ disent quelle règle a choisi le nombre. *Affinité CPU* (désactivée par défaut ;
pas sur macOS, qui n'en a pas) attache chaque partie à un processeur, partagé par ses deux moteurs puisqu'ils ne
réfléchissent jamais en même temps. fastchess ne la prend en charge que pour les moteurs à un seul thread, elle est
donc désactivée automatiquement quand un moteur a `Threads` > 1. Un avertissement apparaît quand parallélisme × threads
dépasse les threads matériels (ou, avec une pendule, les cœurs physiques). *Jouer sur* choisit la machine sur laquelle
le tournoi joue (voir [Un tournoi sur une machine](#un-tournoi-sur-une-machine)).

### Ouvertures
<!-- en: openings 44656411 -->

Un livre EPD, PGN ou Polyglot (`.bin`) (une suite d'ouvertures), l'ordre aléatoire ou séquentiel, le nombre de
demi-coups gardés d'un livre PGN, l'indice de départ et la graine aléatoire (0 = un nouvel ordre pour chaque tournoi).
Chaque ouverture est jouée deux fois, couleurs inversées (dans un match dont les *Couleurs* sont fixées, chaque partie
depuis sa propre ouverture).

Un livre Polyglot est suivi depuis la position initiale jusqu'à une profondeur (8 demi-coups sauf réglage contraire), en
laissant de côté les coups qui pèsent moins d'un dixième du meilleur de leur position ; les positions atteintes sont
écrites une fois dans `<book>-<N>plies.epd` dans le dossier du tournoi et jouées comme un livre EPD. En ordre aléatoire,
elles sont écrites dans l'ordre où elles sont tirées, chacune une fois : une ligne vient d'autant plus tôt qu'elle est
probable, selon les poids de ses coups, si bien qu'un tournoi qui ne joue qu'une partie du livre joue d'abord ses lignes
principales ; la même graine reproduit le même ordre. En ordre séquentiel, elles gardent l'ordre du livre. *Hasard*
(−100 à 100, 0 par défaut) dit comment comptent les poids, comme Polyglot et Cute Chess choisissent un coup de livre : 0
les poids tels quels, −100 le meilleur coup de chaque position seulement (une seule ligne, à moins que plusieurs
partagent le meilleur poids), 100 tous les coups à égalité (les coups rares reviennent) ; entre les deux, −50 élève les
poids au carré et 50 prend leurs racines carrées. Il choisit aussi les coups des livres propres des participants, et est
affiché pour eux même avec un livre EPD ou PGN ; le résumé l'indique, et la ligne sous les copies du classement donne le
livre avec sa profondeur et, pour un livre Polyglot, ce hasard.

Dans un toutes rondes, un gauntlet, un suisse ou un match par équipes, *La même ouverture pour toutes les paires d'une
ronde* donne la même ouverture à chaque paire de parties d'une ronde (échiquier par échiquier, un cycle est une ronde :
chaque échiquier joue la même ouverture), la suivante du livre pour chaque ronde, pour qu'aucun moteur n'ait des
ouvertures plus faciles que les autres de sa ronde (un moteur ajouté plus tard rattrape son retard dans des rondes à
lui, chacune avec son ouverture).

*Signaler les ouvertures déséquilibrées* (désactivé par défaut) montre, au fil des parties, les ouvertures qui décident
la partie à elles seules : une partie est signalée quand elle se termine si les deux moteurs ont vu le même camp en
avance d'au moins le score réglé (200 cp par défaut) à chacun de leurs premiers coups après le livre (4 par défaut ; les
coups du livre propre d'un participant comptent comme coups de livre). Il lit les scores des moteurs, si bien qu'un
moteur qui n'en donne pas ne signale rien ; les résultats ne sont pas changés. L'onglet *Parties* filtre ces parties
(*Ouverture déséquilibrée*), l'onglet *Ouvertures* marque leurs ouvertures d'un `!` et les compte, et la vue d'ensemble
compte les parties. *Rejouer la paire depuis une autre ouverture* annule une telle paire une fois ses deux parties
jouées (elles restent dans les fichiers PGN et le journal, comme toute partie annulée) et la rejoue depuis une ouverture
de la fin du livre (dans l'ordre de jeu du livre), que le tournoi aurait jouée en dernier, pour que les ouvertures des
paires à venir ne bougent pas ; au plus trois fois pour une paire, après quoi la paire compte, signalée. La console et
la vue d'ensemble disent combien d'ouvertures ont été remplacées.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, dans les ouvertures : sans livre, les 960 positions de départ en ordre aléatoire (écrites une fois dans
`chess960.epd` dans le dossier des tournois) ; avec un livre, ses positions. Chaque moteur doit jouer le Chess960 : les
moteurs UCI avec l'option `UCI_Chess960`, les moteurs WinBoard avec la variante `fischerandom`. Le résumé nomme ceux
qui ne le jouent pas, et un tel tournoi ne démarre pas.

### Adjudication
<!-- en: adjudication a18dca1e -->

Nulle (à partir d'un numéro de coup, pendant N coups consécutifs, sous un score), abandon (N coups consécutifs
au-dessus d'un score, avec l'accord des deux moteurs si l'on veut), un nombre maximum de coups, et les tables Syzygy.

### PGN et divers
<!-- en: pgn-and-misc 6762bab8 -->

- Nom de l'événement : son infobulle nomme les deux fichiers PGN dans lesquels chaque partie est écrite.
- *Ajouter aussi chaque partie à* : un fichier PGN à toi auquel chaque tournoi ajoute ses parties, comme `-pgnout` de
  cutechess (jamais écrasé, dans le style de commentaires ChessBase / Lichess ou cutechess), dans l'ordre du
  calendrier : les deux parties d'une ouverture côte à côte, les rondes dans l'ordre, quel que soit l'ordre dans lequel
  elles se terminent. Une partie y attend que celles qui la précèdent soient écrites ; une reprise continue ; quand le
  fichier ne peut pas être écrit (verrouillé, déplacé), les parties attendent et le tournoi continue, avec un
  avertissement dans la vue d'ensemble. Un plantage ou une coupure de courant pendant l'ajout de parties ne les écrit
  jamais deux fois : la reprise vérifie le fichier et complète une écriture interrompue, et n'en retire jamais rien.
- Champs en plus dans les commentaires des coups (nodes, seldepth, nps, pv, tbhits, et le temps de décision `dt=` : le
  moment où le moteur s'est fixé sur le coup qu'il a joué ; `wdl=W/D/L` est toujours écrit pour les moteurs qui le
  donnent), activés par défaut puisque les graphiques de l'onglet *Parties* les lisent (pv rend le PGN nettement plus
  gros).
- *Relancer un moteur qui plante*, et *Arrêter le tournoi quand un moteur ne démarre pas* (désactivé par défaut : voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)).
- Garder le journal moteur de chaque partie (il est toujours gardé pour les parties qui finissent mal).
- *Vérifier les PV de mat* : la PV d'un score de mat doit avoir la bonne longueur et finir par un mat. Chaque PV est
  vérifiée pour les coups illégaux, les coups après la fin de la partie et un meilleur coup qui n'est pas son premier
  coup ; les coups avec une PV fausse sont marqués dans l'onglet Parties et écrits dans le journal.

### Conseils pour les réglages
<!-- en: tips-for-the-settings 62afdb4d -->

- **Comparer deux versions d'un moteur** : un SPRT, gain [0, 3] pour une modification censée gagner, non-régression
  [-5, 0] pour une simplification ou un nettoyage. Une cadence courte (10+0.1) d'abord, puis une longue (60+0.6) pour
  ce qui passe. Utilise un livre de positions équilibrées, pas la position de départ : sinon chaque partie serait l'une
  d'une poignée d'ouvertures.
- **Parallélisme** : laisse-le automatique. Avec une pendule, plus de parties que de cœurs physiques moins un font que
  les moteurs se ralentissent les uns les autres, et les résultats dérivent ; avec des nœuds ou une profondeur par
  coup, chaque thread matériel peut jouer.
- **Threads** : 1 par moteur pour tester ; beaucoup de parties d'un thread chacune donnent plus de résultats par heure
  que peu de parties de beaucoup de threads. Donne à chaque moteur les mêmes `Threads` et `Hash` dans *Réglages des
  moteurs*.
- **Hash** : assez pour la cadence (16 à 64 Mo en 10+0.1, 256 Mo en 40/15 et plus long) ; le résumé dit quand le hash
  des parties ne tient plus dans la mémoire.
- **Adjudication** : le profil standard fait gagner du temps dans les longs tournois sans beaucoup changer les
  résultats ; laisse-la désactivée pour tester les finales d'un moteur, ou utilise les tables.
- **Ouvertures** : l'ordre aléatoire avec une graine fixe permet de rejouer un test à l'identique ; *La même ouverture
  pour toutes les paires d'une ronde* est plus équitable dans un toutes rondes avec peu de cycles.
- **Plusieurs tournois** : donne à celui qui compte le plus un poids plus grand plutôt que de mettre les autres en
  pause (voir [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois)).

## Un tournoi en cours
<!-- en: running-a-tournament 9bcba803 -->

Un tournoi en cours ou ouvert a un espace de travail avec un en-tête (nom, progression, *Pause*, *Arrêter*,
*Cloner*, *Dossier*, *Disposition*) et des panneaux, montrés par défaut sous forme d'onglets :

| Onglet | Ce qu'il montre |
|---|---|
| Vue d'ensemble | Les résultats jusqu'ici et les statistiques, les graphiques, la liste de contrôle avant envoi |
| Classement | Le classement sous forme de tableau ou de tableau croisé, les adversaires d'un moteur, les copies pour un forum |
| Équipes | Un match par équipes : le score des équipes et le tableau croisé des deux équipes |
| Tableau | Un KO : le tableau, tour par tour |
| Calendrier | Chaque paire de parties, et les modifications du tournoi |
| Diffusion | Les fichiers en direct, le serveur TLCS, la page web et lichess |
| Direct | Les parties en cours |
| Parties | Les parties terminées, leurs filtres, leur rejeu et leur export |
| Ouvertures | Les résultats par position de départ |

### Vue d'ensemble
<!-- en: overview 4199cd3c -->

**Un match** : l'Elo et le nElo avec leur intervalle de confiance à 95 %, la LOS, le score, les comptes pentanomiaux,
les bornes du SPRT et le LLR. *Statistiques* (replié) donne les chiffres de fishtest à partir des parties et à partir
des paires : score, Elo, nElo et BayesElo avec DrawElo, écart type par partie, LLR des deux, le rapport de variance des
paires aux parties seules, le biais RMS des ouvertures, les sortes de paires équilibrées et les paires gagnées sur
perdues, que *Copier* inclut. Puis les graphiques : Elo et LLR au fil du temps, longueurs des parties, façons dont les
parties se sont terminées, et *Recherche par moteur*.

**Un tournoi** : le classement (avec *TF*, les pertes au temps de chaque moteur, en orange dès qu'il y en a une ; un *!*
à côté d'un moteur qui a planté ou joué un coup illégal), l'Elo de chaque moteur avec son intervalle à 95 % en graphique
en dessous, puis *Recherche par moteur*, la progression par cycle ou par ronde, les parties par heure et le temps
restant, les derniers résultats.

*Recherche par moteur* donne la moyenne de chaque moteur sur les coups qu'il a cherchés, hors coups du livre :
profondeur, NPS, temps par coup, temps de décision et utilisation du processeur ; un clic sur une colonne trie les
moteurs selon elle. À côté de ses NPS, ×4 dit que le moteur cherche avec 4 threads : il compte plus de nœuds que sur un
seul sans être plus rapide pour autant, donc compare ses NPS à ceux des moteurs qui en ont autant. Les NPS demandent des
moteurs qui les donnent, et *nps* dans *PGN et divers* pour être relus à une reprise.

Les deux comptent les parties issues d'ouvertures déséquilibrées, et les ouvertures remplacées, quand la
configuration les cherche, avec *Afficher* pour ces parties. Un tournoi terminé ajoute une *Liste de contrôle avant
envoi* : toutes les parties jouées, chaque paire avec les deux couleurs, pas de parties en double, pas de plantages,
de pertes au temps ni de coups illégaux (avec des boutons qui ouvrent ces parties), le hash de chaque moteur (montré,
pas jugé), ponder désactivé, le livre d'ouvertures, les tables.

### Classement
<!-- en: standings 5f5e0950 -->

Points, parties, pertes au temps (*TF*), victoires/nulles/défaites, classements avec leur intervalle de confiance,
Sonneborn-Berger (Buchholz pour un suisse), exempts. *Tableau* les liste un moteur par ligne ; *Tableau croisé* (toutes
rondes, jusqu'à 200 moteurs) montre chaque appariement. *Ordre :* aux points, au score % ou dans l'ordre de départ
(qui garde en place les colonnes du tableau croisé d'un gauntlet). Un clic sur un moteur liste ses adversaires en
dessous : parties, avec les Blancs, avec les Noirs, + = −, points et %, avec les paires dont une seule partie a été
jouée marquées ; un gauntlet s'ouvre avec son moteur testé sélectionné.

*Copier* en texte, ou en BBCode pour un message de forum (avec une ligne qui donne le format, la cadence, les parties
et le livre), et dans un gauntlet *Tableau du gauntlet en texte* ou *En BBCode, pour un forum* (le moteur testé, puis
ses adversaires selon leurs propres points, chacun avec ses points, %, Elo ±, victoires-défaites-nulles) ; *Exporter
en HTML…*. Tous ont *TF* comme le classement du mode sans interface, suivent l'ordre choisi, et finissent par une
ligne qui donne les parties jouées sur celles prévues, le taux de nulles, la cadence, le livre (avec les demi-coups
gardés d'un livre PGN ; pour un livre Polyglot, sa profondeur et la façon dont ses coups ont été choisis, par exemple
`book:Perfect2023.bin (8
plies, moves by weight)`) et l'événement. Les copies signent ensuite avec *RookStudio* et sa version, et le tableau
du gauntlet est tracé avec des = plutôt que des -, pour qu'un tableau posté sur un forum se distingue de ceux des
autres programmes.

### Équipes
<!-- en: teams 8c845645 -->

Un match par équipes seulement, dans l'onglet *Classement* (où il s'ouvre) et en haut de la *Vue d'ensemble* : le
score des équipes, « Lynx 17½ – 18½ Otter », avec les parties jouées, le score % de l'équipe A, la différence d'Elo
que ce score représente avec son intervalle à 95 % et la LOS, comptés sur les paires de parties comme le sont les
statistiques d'un match (pentanomial) ; puis les moteurs de chaque équipe avec leurs parties, points, %, classement
et les points que chacun a marqués contre chaque moteur de l'autre équipe (B1, B2… en colonnes, le nom au curseur) :
le tableau croisé des deux équipes. *Tableau* et *Tableau croisé* montrent les moteurs comme dans tout tournoi.
*Copier* et *Exporter en HTML…* donnent les scores des équipes et les moteurs des deux équipes les uns contre les
autres, avec la ligne en dessous.

### Tableau et calendrier
<!-- en: bracket-and-schedule fe207ac0 -->

*Tableau*, un KO seulement : le tableau, tour par tour, avec le score de chaque match. *Calendrier* : chaque paire de
parties par cycle ou par ronde, avec son état et l'ouverture qu'elle joue (une fois commencée, ou une fois sa ronde
commencée quand la ronde en partage une), et les modifications du tournoi (voir
[Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).

### Direct
<!-- en: live 59d5ab65 -->

Les parties en cours, en mosaïque d'échiquiers ou en une partie mise en avant avec les pendules, la recherche en cours
de chaque moteur (profondeur, score, nœuds, nps, PV en SAN), la liste des coups et le graphe de la partie (voir
[Le graphe de la partie](#le-graphe-de-la-partie)) : les évaluations des deux moteurs, et dans ses onglets leurs
chances de gain, le temps par coup avec le temps de décision, la profondeur avec la seldepth, les nœuds, la vitesse,
les accès aux tables, leur écart, ou combien de demi-coups une PV reprend de ce que la PV de l'autre moteur avait
prévu. Les parts de victoire, de nulle et de défaite (`UCI_ShowWDL`, activé pour les moteurs qui l'ont quand ils sont
ajoutés) suivent chaque ligne de recherche ; l'onglet *G/N/P* les dessine en bandes, les victoires des Blancs en bas.

Quand la place le permet (une partie mise en avant, un grand panneau), chaque moteur a sa carte : son logo aussi haut
que son nom et sa pendule (quand les logos sont activés, voir [Apparence](#apparence)), ce qu'il fait (il réfléchit, ou
le coup qu'il a joué), sa recherche chiffre par chiffre (profondeur et seldepth, score, nœuds, vitesse, les accès aux
tables et le remplissage du hash quand le moteur les envoie, le temps passé sur le coup jusque-là) et toute sa PV ; le
moteur qui attend montre ce qu'il disait au moment de jouer, avec l'instant où il s'est décidé. À côté de la mosaïque,
le panneau garde une ligne de chiffres et les 12 premiers demi-coups de la PV. *Score du camp au trait* | *Score des
Blancs*, à droite de l'onglet, choisit le côté de tous ses scores : celui du camp au trait, comme fastchess les écrit
dans le PGN, ou celui des Blancs ; la barre d'évaluation et le grand écran sont toujours du côté des Blancs.

Au-dessus des coups : l'ouverture, le code ECO et le nom de la position la plus avancée de la partie que la table des
ouvertures nommées connaît (coups du livre et des moteurs confondus, si bien qu'une transposition après le livre est
nommée aussi), avec le dernier coup du livre ; et dès que les tables Syzygy du tournoi (celles de son adjudication,
sinon celles données aux moteurs) connaissent la position affichée, leur verdict : le camp qui gagne avec la DTZ (les
demi-coups jusqu'à la prochaine prise ou au prochain coup de pion : les tables Syzygy n'ont pas de distance au mat), ou
la nulle, la règle des 50 coups comptée d'après le compteur de la partie sauf si l'adjudication l'ignore. Chaque
position est sondée une fois, hors des fils qui font jouer les parties. Dans la liste, chaque coup d'un moteur a son
score, sa profondeur et son temps ; le survol y ajoute l'instant où le moteur s'est décidé et la réponse qu'il attendait
(le second coup de sa PV : fastchess fait jouer sans réflexion sur le temps de l'adversaire). Les coups du livre disent
*livre*, une ligne marque la fin du livre, et le coup en réflexion dit *réfléchit*.

La mosaïque a un échiquier par partie que le tournoi a jouée en même temps, et non par partie que le parallélisme permet
: avec moins de parties que cela, les échiquiers grandissent. Un échiquier dont la partie s'est terminée garde sa
position finale jusqu'à ce que la partie suivante de son emplacement y commence : vers la fin d'un tournoi, les
échiquiers restent à leur place, chacun avec la fin de sa dernière partie. Quand les échiquiers demanderaient une barre
de défilement, la mosaïque prend de la largeur à la partie mise en avant, jusqu'à lui laisser 40 % de la page.

Sous l'échiquier : le compteur des 50 coups (demi-coups depuis la dernière prise ou le dernier coup de pion, sur 100),
la différence de matériel avec les pièces qu'un camp a en plus, et les pièces restantes, marquées *TB* quand
l'adjudication par les tables peut terminer la partie ; *Détails sous les échiquiers* les ajoute à la mosaïque. En
dessous, les boutons qui parcourent les coups (voir [Les échiquiers et leurs boutons](#les-échiquiers-et-leurs-boutons))
: on peut revenir en arrière dans la partie mise en avant, ou dans un échiquier détaché, pendant qu'elle continue (aussi
avec ← → Origine Fin, un clic sur un coup de la liste ou sur la courbe), la position antérieure affichée avec son coup
marqué dans la liste et sur la courbe ; *Revenir au coup en cours* (ou Fin, ou un clic sur le dernier coup) fait de
nouveau suivre la partie, comme à chaque nouvelle partie.

*Flèches* (activé par défaut) dessine le coup auquel pense le camp au trait, dans la couleur de son moteur, et la
réponse qu'il attend, plus claire, dans la couleur de l'adversaire, sur l'échiquier mis en avant et les échiquiers
détachés (pas la mosaïque), quand il montre le coup en cours. Dans un gauntlet, *Moteur testé en bas* (activé par
défaut) montre chaque nouvelle partie du côté du moteur testé ; retourner l'échiquier (F, ou son bouton) le retourne
toujours jusqu'à la partie suivante. *Détacher* ouvre un échiquier dans sa propre fenêtre. Un clic droit sur un
échiquier termine sa partie avec un résultat (voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).
*Grand écran* montre la partie suivie en remplissant la fenêtre (voir [Grand écran](#grand-écran)).

### Grand écran
<!-- en: big-screen 14c8f0d6 -->

Pour une télévision, un club ou un stream, *Grand écran* (dans l'onglet *Direct*, sur un échiquier détaché, ou dans
la palette de commandes) montre une partie qui remplit la fenêtre, en clair sur fond noir : les noms des joueurs et
leurs pendules en gros caractères (le camp au trait et sa pendule qui tourne en jaune, une pendule sous dix secondes en
rouge), la barre d'évaluation, l'évaluation, la profondeur et la vitesse de la recherche, le dernier coup encadré sur
l'échiquier et écrit en grand, l'ouverture, les lignes des moteurs (*Lignes*, activé par défaut : celle du camp au
trait pendant qu'il cherche, en jaune, celle de l'autre à son dernier coup, deux lignes chacune au plus), le verdict
des tables une fois qu'elles connaissent la position, et les coups en gros caractères, le dernier marqué ; le résultat par-dessus l'échiquier
une fois la partie finie. Son texte suit la taille de la fenêtre et a une taille propre (*A-* / *A+*, ou les touches
+ et -). Il montre la partie suivie, ou passe d'une partie en cours à l'autre toutes les N secondes (*Chaque partie à
tour de rôle*), ou suit *La plus intéressante* : la plus proche de l'équilibre, la plus longue parmi celles qui le
sont autant, gardée tant qu'elle le reste. Les commandes s'affichent trois secondes quand la souris bouge (le pointeur
se cache avec elles) ; ← et → changent de partie, F11 met la fenêtre en plein écran, Échap quitte. Un échiquier
détaché déplacé sur un autre écran (la télévision) le montre dans sa propre fenêtre, F11 mettant cette fenêtre en
plein écran pendant que la fenêtre principale continue ; ancré dans la fenêtre principale, c'est la fenêtre principale
qui le montre.

### Parties
<!-- en: games ba4a9604 -->

Les parties terminées avec leur ouverture (code ECO et nom), et un échiquier de rejeu : les boutons en dessous, ← →
Origine Fin et F, ou un clic sur un coup ou sur le graphe ; les mêmes détails en dessous et le même graphe de la
partie que dans *Direct* (voir [Le graphe de la partie](#le-graphe-de-la-partie)), à partir des champs avec lesquels
le PGN a été écrit : un graphique dont le tournoi n'a pas écrit le champ est dans le menu `⋯` du graphe, grisé, et le
dit avec un bouton vers les options PGN de la configuration.

**Filtres** : par résultat (victoires, nulles, défaites, pertes au temps, plantages et coups illégaux, adjudications,
PV fausses, et les parties d'une ouverture signalée déséquilibrée quand la configuration les cherche) ; par ce que
disent les évaluations (les moteurs sont en désaccord de N pions pendant 3 coups de chacun d'affilée, l'évaluation d'un
moteur varie de N pions d'un de ses coups au suivant, le perdant se croyait mieux de N pions ; la partie s'ouvre à ce
moment) ; et par ouverture (un code ECO ou son début, une plage comme `B20-B99`, ou des mots du nom).

**Export** d'une partie, de toutes ou des parties filtrées : *Exporter tout…* propose `<name>.pgn` à côté du dossier du
tournoi ; les propres fichiers PGN du tournoi sont refusés comme cible. *Exporter pour une liste de classement…* écrit
les parties comme les envoient les testeurs des listes de classement (par exemple le CCRL) :
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, un zip aussi si on le demande ; les joueurs nommés
comme les listes les écrivent, `<engine> 64-bit 4CPU` (modifiable pour chaque participant), les rondes renumérotées, les
balises `Event` et `Site` remplies, les parties annulées et les doublons écartés, et celles perdues par un plantage ou
un blocage aussi, sauf si c'est décoché ; le `PlyCount` d'une partie perdue par un coup illégal ne compte que les coups
écrits. Le testeur et le lieu sont mémorisés.

*Journal moteur* ouvre le journal d'une partie qui en a un (voir [Fichiers](#fichiers)). Un clic droit sur une partie
la rejoue, l'annule ou fixe son résultat (voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).

### L'onglet Ouvertures
<!-- en: the-openings-tab c51dd29a -->

Les résultats par position de départ : ses paires de parties, le score des Blancs, les nulles, et combien de paires
les Blancs ont gagnées deux fois, les Noirs deux fois (l'ouverture favorise une couleur) ou un même moteur deux fois
(elle départage les moteurs), avec la longueur moyenne. Triées par n'importe quelle colonne, filtrées sur les
déséquilibrées, les nulles ou les décisives, ou sur celles *Signalées déséquilibrées pendant les parties* (voir
[Ouvertures](#ouvertures) dans la configuration : marquées `!`, leur nombre à côté du total, avec les ouvertures
remplacées). *Exporter en EPD…* écrit les positions affichées, pour les garder ou pour nettoyer un livre, *Sans les
déséquilibrées* (activé par défaut, affiché dès qu'une ouverture est signalée) écartant celles qui sont signalées. Un
clic montre la position et ses parties. Dans un match dont les *Couleurs* sont fixées, chaque partie a sa propre
ouverture : le décompte porte sur les parties, et les colonnes et filtres sur les paires ne sont pas affichés.

### Panneaux et dispositions
<!-- en: panels-and-layouts 9ed9b915 -->

Chaque onglet est un panneau. Fais-en glisser un par son onglet pour l'ancrer sur un côté d'un autre panneau ou comme
onglet à côté de lui, ou dépose-le ailleurs pour le faire flotter (aussi sur un second écran sous Windows et
Linux/X11, pas sous Wayland ni avec le rendu logiciel). La partie suivie dans *Direct* se décompose en panneaux
*Échiquier*, *Joueurs*, *Recherches*, *Coups* et *Graphique* ; les éléments qui n'ont pas de panneau ouvert restent à
côté de la mosaïque dans *Direct*, et les échiquiers détachés s'ancrent comme les autres panneaux. Ferme un panneau
avec le x de son onglet ; fais-le revenir depuis *Disposition* > *Panneaux*, ou avec *Afficher : …* dans la palette de
commandes, et il revient ancré.

*Disposition* propose aussi des dispositions toutes prêtes (*Onglets*, comme par défaut ; *Suivre une partie* :
l'échiquier à gauche, les joueurs, les recherches et le graphique à droite, le classement en dessous ; *Grand tournoi* :
la mosaïque à côté du classement ; *Diffusion* : un grand échiquier avec les joueurs, les recherches et le classement),
*Enregistrer la disposition sous…* (les tiennes, par nom, gardées dans `layouts.json` dans le profil), *Supprimer* et
*Disposition par défaut*. Un panneau sorti de l'espace de travail reste à l'écran sur les autres pages ; l'agencement
est gardé d'une session à l'autre.

### Utilisation du processeur
<!-- en: processor-use e5234cbe -->

Pendant que des parties se jouent, le temps processeur des processus de chaque moteur (le moteur derrière un
adaptateur WinBoard compris) est mesuré à chaque `go` et à chaque coup. La vue d'ensemble montre, pour chaque moteur,
les cœurs qu'il occupe pendant qu'il réfléchit et la part d'un cœur qu'il utilise pendant que son adversaire
réfléchit, et avertit quand un moteur utilise nettement plus de cœurs que ses `Threads`, ou réfléchit sur le temps de
son adversaire (un ponder qu'on ne lui a pas demandé, un thread d'arrière-plan) : ce qui rend une partie inéquitable
pour l'adversaire et pour les autres parties en cours. La mesure a besoin de 20 s de chacun pour se prononcer, et
compte chaque partie jouée, dans la fenêtre ou sans interface (`--run`) : ce qu'ont utilisé les moteurs de chaque
partie est écrit dans `processor.tsv` dans le dossier du tournoi, si bien qu'une reprise le garde. Elle ne fait
qu'avertir ; elle ne décide jamais une partie.

### Pause, arrêt et reprise
<!-- en: pause-stop-and-resume f6885fa5 -->

**Pause** ne commence aucune nouvelle paire de parties. Les parties en cours vont jusqu'au bout, de même que la
seconde partie d'une paire dont la première a commencé, si bien qu'une pause ne jette jamais un résultat. **Reprendre**
continue. **Arrêter** termine d'abord les parties en cours, ou arrête tout de suite (les parties interrompues ne sont
pas comptées et seront rejouées).

Un tournoi arrêté par un moteur dit lequel dans son en-tête et sur la page Tournois, sous *À traiter* :
*Interrompu : Alpha n'a pas pu démarrer : …* (après son dernier essai), ou *Alpha a planté ou ne répond plus
(partie 12)* (sans *Relancer un moteur qui plante*), avec *Ouvrir le moteur* et *Ouvrir le journal*.

**Chaque tournoi reprend exactement là où il s'est arrêté**, après un arrêt, une fenêtre fermée, un plantage ou une
coupure de courant : aucune partie n'est perdue et aucune n'est jouée deux fois, et une paire de parties à moitié jouée
est complétée avec la même ouverture et les mêmes couleurs. *Reprendre* sur la page Tournois le continue, avec les
statistiques des parties déjà jouées : les résultats, et la profondeur, le temps par coup et les NPS des moteurs relus
dans les commentaires des coups (les NPS seulement quand le PGN les enregistre, *nps* dans *PGN et divers*). Quand le
programme s'est terminé pendant qu'un tournoi tournait (un plantage, une coupure de courant, un redémarrage, ou le
programme quitté pendant qu'il tournait), le lancement suivant le reprend de lui-même, et la file d'attente continue
après lui : *Reprendre au lancement le tournoi qui était en cours*, dans **Réglages**, activé par défaut. Un tournoi mis
en pause ou arrêté t'attend, et `--autostart` lance à la place la configuration enregistrée. Un participant dont le
programme a été recompilé ou a disparu depuis qu'il a rejoint le tournoi est nommé dans l'en-tête du tournoi repris,
avec *Continuer* et *Pause* : ses parties suivantes sont jouées par le programme tel qu'il est maintenant, avec les
options que le tournoi lui a données.

### Plusieurs tournois à la fois
<!-- en: several-tournaments-at-once 4914f1d0 -->

Plusieurs tournois peuvent jouer côte à côte : un gauntlet à côté d'un SPRT, deux gauntlets à la fois. *Lancer* sur la
page *Nouveau tournoi* en lance un autre pendant que d'autres jouent ; *Reprendre* en reprend un à côté d'eux. Chaque
tournoi joue dans son propre processus (`rookstudio --run <folder>` piloté par la fenêtre), si bien qu'un
plantage n'emporte que ce tournoi, et la fenêtre, ses vérifications de moteurs, ses tests rapides et ses suites de
tests n'attendent plus la fin d'un tournoi.

L'espace de travail a **un onglet par tournoi ouvert**, au-dessus de son en-tête : un point pendant qu'il joue (un
symbole de pause pendant qu'il est en pause), son statut au curseur, une croix pour fermer celui qui ne joue pas. Les
panneaux montrent l'onglet choisi ; la palette de commandes liste les autres (*Afficher : …*, *Pause*, *Reprendre*).
Un tournoi terminé ouvert depuis la liste prend l'onglet de celui qui était seulement consulté. La page Tournois a
une carte pour chaque tournoi qui joue, avec sa progression, son meneur, *Ouvrir* et *Pause*, et en dessous son
**poids**, les cœurs que ses parties utilisent sur ceux qu'il peut utiliser, et ce qu'attend sa prochaine partie ; la
barre *Cœurs* en haut montre la part de chaque tournoi (et celle de la page Échiquier), et combien de cœurs les tournois
se partagent. Le *Journal* montre les lignes d'un tournoi, de la fenêtre, ou toutes (chaque ligne d'un tournoi porte
son nom) ; les lignes d'un tournoi sont aussi gardées dans `rookstudio.log` dans son dossier.

**Partager la machine** : les tournois qui jouent se partagent un budget de cœurs, réglé dans **Réglages** (*Cœurs
pour les tournois* : les cœurs physiques moins un par défaut). Chacun reçoit d'abord de quoi jouer une partie tant que
le budget le permet, le plus gros poids en premier ; le reste va selon le poids (un tournoi de poids 2 reçoit deux fois
les cœurs d'un tournoi de poids 1), aucun au-delà de son propre parallélisme, ce que l'un ne peut pas prendre allant
aux autres. Une partie compte pour les `Threads` du plus gros de ses deux moteurs : l'un réfléchit pendant que l'autre
attend. Un tournoi en pause ne garde que ce dont ont besoin ses parties en cours et les secondes parties de leurs
paires. Une part plus petite n'interrompt jamais une partie : le tournoi joue moins de parties à partir de la
suivante. Un tournoi seul joue autant de parties que son parallélisme l'indique.

**Mémoire** : une partie ne commence que quand le `Hash` de ses deux moteurs, et ce que chaque processus de moteur
prend en plus (64 Mo, et 8 Mo par thread), tiennent dans ce que les parties en cours laissent du budget de mémoire
(*Mémoire pour les moteurs* dans **Réglages**, 75 % de la mémoire installée par défaut), partagé entre les tournois. Le
résumé de la configuration dit combien de parties le budget laisse jouer à la fois, et la carte *Santé* de la page
Tournois le dit quand des parties attendent de la mémoire.

**GPU** : un moteur marqué *Utilise un GPU* sur sa fiche (un moteur à réseau comme Lc0) compte sur les *GPU* des
**Réglages** : pas plus de tels moteurs ne jouent à la fois qu'il n'y a de GPU, dans un tournoi et entre tournois,
pendant que les autres parties jouent à côté. Le résumé de la configuration dit combien de parties avec un moteur GPU
cela permet, et avertit quand aucun GPU n'est compté, ou quand une partie entre deux moteurs GPU demande plus de GPU que
la machine n'en a (elle joue alors seule).

**Les laisser jouer** : fermer la fenêtre pendant que des tournois jouent demande quoi faire. *Les laisser jouer et
quitter* (le choix par défaut) laisse chacun jouer dans son processus ; la fenêtre les montre de nouveau, où ils en
sont, à son lancement suivant. *Arrêter et quitter* les arrête, chacun pour être repris exactement là où il s'est
arrêté. Le test rapide s'arrête dans les deux cas. Un tournoi dont le processus a disparu au lancement suivant (un
redémarrage, un plantage) est repris comme tout tournoi interrompu. La diffusion de l'onglet *Diffusion* tourne dans
la fenêtre, pour le tournoi affiché : pour une diffusion qui continue sans la fenêtre, lance le tournoi depuis un
terminal avec les options de diffusion (voir [Mode sans interface](#mode-sans-interface)).

### Un moteur qui ne démarre pas
<!-- en: an-engine-that-cannot-start 26c20dee -->

Une nuit sans surveillance ne s'arrête pas sur un seul moteur. Un moteur qui ne démarre pas (un programme déplacé ou
recompilé, un fichier de réseau manquant, un disque disparu) est réessayé trois fois, à 10 s d'intervalle ; puis ses
parties sont mises de côté et les autres parties continuent. Il est réessayé toutes les 5 minutes, une partie à la
fois, et ses parties reviennent dès qu'il démarre. L'en-tête du tournoi et la page Tournois (sous *À traiter*) disent
quel moteur, l'erreur, et proposent :

- **Réessayer** : tout de suite, plutôt qu'au prochain essai (après avoir corrigé le programme, par exemple).
- **Retirer…** et **Remplacer…** (toutes rondes, gauntlets et matchs par équipes) : le panneau de modification de
  l'onglet Calendrier, réglé pour ce moteur, avec l'impact montré avant que quoi que ce soit change.
- **Journal moteur** : `logs/start-<engine>.log` dans le dossier du tournoi, ce qui a été envoyé aux moteurs de son
  dernier essai et ce qu'ils ont répondu, leur sortie d'erreur comprise (souvent la raison : une bibliothèque ou un
  fichier de réseau introuvable).

Le journal et la notification du bureau disent quand un moteur est mis de côté. Le tournoi ne s'arrête que quand il ne
reste rien à jouer sans les moteurs mis de côté, après un dernier essai de chacun ; son statut et la notification de
fin le disent (*Arrêté : il ne reste aucune partie à jouer sans …*), et la file d'attente continue avec le tournoi
suivant. *Reprendre* réessaie chaque moteur à neuf. Une pause retient les essais. *Arrêter le tournoi quand un moteur ne
démarre pas*, dans *PGN et divers* de la configuration, l'arrête plutôt tout de suite, une fois les essais épuisés.

### Modifier un tournoi en cours
<!-- en: changing-a-running-tournament aebe63e2 -->

Le panneau **Modifier le tournoi** de l'onglet *Calendrier* fonctionne sans rien arrêter : les parties en cours
continuent et aucune partie jouée n'est perdue. Chaque modification montre son impact avant d'être appliquée.

- **Ajouter un moteur** : il joue les cycles déjà commencés (*Rattraper*) ou se joint à partir du cycle suivant.
- **Retirer un moteur** : ses parties restantes sont abandonnées ; ses parties jouées jusque-là comptent ou non.
- **Remplacer un moteur** : ses parties cessent de compter et sont rejouées par le nouveau moteur.
- **Nombre de cycles** : plus ou moins.
- **Rejouer des parties** : toutes les parties d'un moteur, ou les pertes au temps et les plantages.
- Clic droit sur une partie (ici, ou dans l'onglet *Parties*) : la rejouer, l'annuler (sans la rejouer), ou fixer son
  résultat.

Un moteur ajouté ou remplaçant joue avec les réglages du tournoi (sa cadence, son Hash et ses Threads, affichés sous le
moteur), ou avec des *Réglages propres* : Hash, Threads, part du temps (ou nœuds, profondeur), comme les réglages
propres d'un participant sur la page *Nouveau tournoi*. Un tournoi créé avant la 0.6.6 n'enregistrait pas ses réglages :
ceux de son premier moteur en tiennent lieu, et ce pouvaient être ses réglages propres.

Dans un match par équipes, un moteur ajouté rejoint l'équipe choisie et joue contre chaque moteur de l'autre équipe ;
échiquier par échiquier, on ne peut pas ajouter de moteur (son échiquier n'aurait pas d'adversaire) : remplaces-en un
à la place. Un remplaçant prend l'équipe et l'échiquier du moteur qu'il remplace ; davantage de cycles apparient les
moteurs qui jouent encore (échiquier par échiquier, sur les échiquiers du premier cycle). Les parties qu'un moteur
retiré ou remplacé a jouées comptent toujours pour son équipe, à moins qu'elles ne soient annulées.

**Fixer le résultat** tranche une partie que le programme n'a pas su bien trancher : un blocage qu'il n'a pas détecté,
une finale gagnée où les moteurs tournent en rond, une partie terminée pour une mauvaise raison. Choisis 1-0, ½-½ ou
0-1 et donne une raison ; les points que cela déplace sont montrés avant que *Fixer le résultat* l'applique. Le
classement, le tableau croisé, les classements Elo et la liste de contrôle avant envoi comptent le nouveau résultat
(une adjudication : une perte au temps changée en nulle ne compte plus comme telle) ; l'onglet *Parties* le marque d'un
`*`, son infobulle donnant la raison et la façon dont la partie s'est terminée telle que jouée, et *Revenir au résultat
joué* revient en arrière. Une partie en cours se termine de la même façon : clic droit sur son échiquier dans l'onglet
*Direct*, *Terminer avec un résultat* (aussi dans la palette de commandes pour la partie suivie). Le moteur au trait est
arrêté, fastchess termine aussitôt la partie et elle compte avec le résultat choisi ; le moteur redémarre pour sa
partie suivante. Il faut pour cela *Relancer un moteur qui plante* (configuration, *PGN et divers*, activé par
défaut).

Les parties annulées et les résultats fixés à la main restent dans les fichiers PGN et le journal tels qu'ils ont été
joués : les fichiers PGN ne font jamais que s'allonger. Tout ce qui lit les parties en tient compte : les parties
annulées sont écartées, et un résultat fixé à la main est dans chaque export (*Exporter la partie…*, *Exporter tout…*,
*Exporter pour une liste de classement…*, l'export face-à-face de la page *Classements*, le fichier d'*Ajouter aussi
chaque partie à*) avec `[Termination "adjudicated by the operator"]` et la raison dans un commentaire avant le résultat.
**Annuler la dernière modification** est proposé jusqu'au début de la partie suivante : rien n'a encore été joué sous la
modification, elle peut donc être défaite exactement (les parties qui étaient déjà en cours quand elle a été faite sont
gardées). Un résultat fixé à la main peut être défait tant que c'est la dernière modification, que des parties aient été
jouées depuis ou non (dans un KO, dont les tours suivants découlent des résultats, jusqu'au début de la partie suivante)
; l'onglet *Parties* propose aussi *Annuler* à côté de la partie.

### File d'attente et relance
<!-- en: queue-and-relaunch 06904b6c -->

La **File d'attente** de la page Tournois contient des tournois à jouer l'un après l'autre : quand l'un se termine de
lui-même, le suivant commence (pas après un arrêt ou une erreur, ni pendant que *Tout mettre en pause* retient la file
d'attente). Avec *Lancer quand des cœurs sont libres*, le suivant commence à côté de ceux qui jouent dès que le budget
de cœurs dépasse ce qu'ils peuvent prendre, chacun à son parallélisme. Ils peuvent être réordonnés ou retirés, et le
premier lancé tout de suite quand rien ne tourne. La page Tournois dit quand le tournoi en cours se termine (*Fin vers
HH:MM (dans …)*) et quand toute la file d'attente se termine, chaque tournoi en attente avec sa durée et, tant que les
durées de ceux qui le précèdent sont connues, l'heure à laquelle il commence ; les tournois dont la durée ne peut pas
être connue (un SPRT sans maximum, des nœuds ou une profondeur par coup) sont comptés à part.

**Relancer avec…** sur un match terminé (dans son en-tête, ou sous *Derniers terminés* sur la page Tournois) le
rejoue avec un autre moteur A, typiquement la version suivante, contre le même moteur B, avec les autres réglages du
match (ceux de la page *Nouveau tournoi* pour un match lancé par une version plus ancienne, qui ne les gardait pas).

**Cloner** (dans l'en-tête d'un tournoi, sous *Derniers terminés* et dans *Tous les tournois* sur la page Tournois)
ouvre la page *Nouveau tournoi* remplie avec la configuration de ce tournoi, quel que soit son format : les
participants avec leurs propres réglages, la cadence, les ouvertures, l'adjudication et le reste, chaque valeur telle
qu'elle a été jouée. Ses profils sont nommés, et un profil modifié depuis est marqué comme changé pour ce tournoi.
Change ce qui doit différer (une autre cadence, la version suivante d'un moteur, un autre livre), puis **Lancer** ou
*Mettre en file d'attente*. Un tournoi qui joue encore peut aussi être cloné ; un tournoi lancé par une version plus
ancienne, qui ne gardait pas sa configuration, ne le peut pas.

### Vérifier les finales avec les tables
<!-- en: checking-the-endgames-against-the-tables fb5b1e89 -->

Le menu d'un tournoi terminé, un clic droit sur son en-tête ou sur sa ligne de la page Tournois (ou **Ctrl+K**), a
*Vérifier les finales avec les tables* : les parties arrivées à sept pièces ou moins, chaque résultat confronté à ce
que disent les tables de finales. La réponse arrive dans une carte sous l'en-tête du tournoi, « 12 parties sont
arrivées à sept pièces ou moins : 3 gains gâchés (Alpha: 2, Bravo: 1), 1 position perdue sauvée, 2 nulles adjugées
dans une position gagnée », avec une ligne pour chaque constat, *Ouvrir* montrant sa partie dans l'onglet *Parties* au
coup concerné :

- **un gain gâché** : un coup d'un moteur après lequel les tables ne lui donnent plus le gain (une nulle, ou le gain
  de l'autre camp) ; **une nulle gâchée** : un coup d'un moteur après lequel elles donnent le gain à l'autre camp ;
- **une position perdue sauvée** : une partie annulée par les règles (une triple répétition, la règle des 50 coups)
  alors que les tables donnent encore le gain à un camp à sa fin ;
- **une nulle adjugée dans une position gagnée**, par la règle de nulle du tournoi ou sa limite de coups, et **un gain
  adjugé contre les tables**, par sa règle d'abandon. L'adjudication propre aux tables est d'accord avec elles ; une
  partie perdue au temps, par un coup illégal ou par un crash n'est jugée que sur ses coups.

Quelques positions par partie sont demandées : la première que les tables savent dire et la dernière, et quand leurs
verdicts diffèrent, une dichotomie entre elles jusqu'au coup qui l'a changé (une dizaine de positions pour une longue
finale). Tes tables sur le disque d'abord (voir [Des tables pour les finales](#des-tables-pour-les-finales)), puis les
réponses que Lichess a données avant, gardées dans ton profil, puis les tables à 7 pièces de Lichess pour le reste,
une fois autorisées : pour cette vérification, toujours, ou pas maintenant (le FEN de chaque position part vers
tablebase.lichess.ovh, sans compte), une requête à la fois, chaque réponse gardée pour de bon. Sans elles, les parties
que les tables de ton disque ne savent pas dire sont comptées comme non jugées. Rien de la page du tournoi ne change
avant le clic.

## Classements
<!-- en: ratings 3e4be48f -->

La page **Classements** calcule des classements au maximum de vraisemblance (le modèle logistique, les nulles comptant
pour un demi-point, comme Ordo le fait par défaut) sur toutes les parties de tous les tournois du profil, et des
fichiers PGN importés. Les moteurs sont identifiés par leur nom. Les parties comptées peuvent être restreintes en haut
de la page, pour tous les onglets :

- une cadence, un groupe de la bibliothèque de moteurs (les parties entre ses moteurs), les 30 ou 365 derniers jours ;
- *Parties importées* : les parties des fichiers PGN listés sous *Fichiers PGN…* (le téléchargement d'une liste de
  classement, les parties d'un autre programme), lues dans leurs en-têtes ; leurs moteurs sont connus par les noms que
  le PGN leur donne, et un nom qui est celui d'un moteur de la bibliothèque, aux majuscules et aux espaces près
  (« stockfish  17 » pour « Stockfish 17 »), est ce moteur : ses parties comptent comme celles du moteur et son Elo
  de référence peut les ancrer. Rien de plus lâche n'est appliqué de soi-même : « Stockfish 17 64-bit » reste un
  moteur à part jusqu'à ce que tu dises quel moteur c'est dans *Joueurs PGN…* (à côté du nombre de moteurs de la
  bibliothèque, ou dans *Fichiers PGN…*). Cette fenêtre liste par leur nom les joueurs qui ne sont aucun moteur de la
  bibliothèque, avec une recherche : pour chacun, *Un moteur à part* (le défaut) ou un moteur de la bibliothèque. Un
  nom que deux moteurs de la bibliothèque partagent, aux majuscules près, est affiché dans la couleur d'avertissement
  pour que tu choisisses ; un moteur probable (son nom commence celui du joueur) est suggéré sous la forme *Peut-être
  … ?*, jamais appliqué. La fenêtre s'ouvre d'elle-même après l'ajout d'un fichier PGN quand il y a quelque chose à
  décider. Les choix, un moteur à part compris, sont gardés avec le moteur (ils suivent un renommage) et s'appliquent
  à chaque fichier PGN et aux listes de classement importées pour l'Elo de référence ; les parties sont relues avec eux
  quand la fenêtre est fermée. La liste de classement dit combien de moteurs de la bibliothèque contiennent les
  fichiers PGN, son infobulle lesquels ;
- *Ancre* : la moyenne à 0, ou un moteur à un classement fixe (par défaut le premier moteur qui a un Elo de
  référence).

### Liste de classement
<!-- en: rating-list c726b55e -->

La liste, avec un nombre minimum de parties. Outre l'Elo, le ± et les parties, chaque moteur montre son score, ses
victoires (+), nulles (=) et défaites (−), son taux de nulles, *Adv.* : le classement moyen des adversaires qu'il a
rencontrés, partie par partie, *LOS* : la probabilité qu'il soit plus fort que le moteur suivant de la liste, d'après
les deux classements et la covariance de leurs erreurs (approchée au-delà de 1000 moteurs, comme le dit son
infobulle), et *Écart* : son Elo au-dessus du moteur suivant. Un clic sur l'en-tête d'une colonne trie selon elle
(# garde la place au classement) ; un clic droit montre ou masque des colonnes (*Écart* est masquée au départ). Un
clic sur un moteur ouvre sa fiche.

*Copier* en texte ou en BBCode, avec *Avec un en-tête* pour mettre au-dessus de la liste l'en-tête décrit ci-dessous.
*Exporter…* : CSV avec toutes les colonnes ; une page HTML (claire ou sombre) et du Markdown (un tableau que GitHub et
les forums affichent), qui s'ouvrent tous deux par un en-tête qui dit comment la liste a été faite : les tournois et
les fichiers PGN comptés, les parties avec les victoires des Blancs et des Noirs, les nulles et le score des Blancs,
l'ancre, les filtres, le nombre minimum de parties, la date et la version ; et *Parties pour Ordo…* : les parties
comptées sous forme de PGN d'en-têtes, que lisent Ordo et BayesElo (`ordo -p ratings.pgn -o ratings.txt`).

### Fiche du moteur
<!-- en: engine-card 80442e81 -->

La fiche d'un moteur : son classement, ses résultats en tout et par cadence, ses pertes au temps, plantages et coups
illégaux (chaque partie s'ouvre dans son tournoi), ses tournois (chacun s'ouvre), sa profondeur moyenne, son temps par
coup et ses NPS lus dans les commentaires des coups, et son classement après chaque tournoi. La page *Moteurs* y mène
avec *Ses résultats…*.

### Face-à-face
<!-- en: head-to-head 1021ba2f -->

Deux moteurs l'un contre l'autre : le score, l'Elo et la LOS, les comptes pentanomiaux des paires dont ils ont joué
les deux parties, par cadence et par tournoi, leurs parties (chacune s'ouvre dans son tournoi) et *Exporter les
parties…*.

### Progression
<!-- en: progress e2ea28fc -->

Le classement des moteurs cochés après chaque tournoi qu'ils ont joué, sur un axe du temps : les versions d'un moteur
côte à côte.

## Diffusion
<!-- en: broadcast c2f489e7 -->

L'onglet *Diffusion* d'un tournoi en cours le publie pendant qu'il joue ; l'instantané est actualisé deux fois par
seconde. *Partie diffusée* choisit la partie envoyée à TLCS et à la page web : automatique, ou celle choisie dans
l'onglet *Direct*.

### Fichiers en direct
<!-- en: live-files 09341896 -->

Écrits de façon atomique dans un dossier de ton choix, à publier où tu veux (FTP, rsync, un site web) : `live.pgn`
(les parties en cours), `standings.html` (classement et tableau croisé), `tournament.json` (tout, pour les scripts).

### Serveur TLCS
<!-- en: tlcs-server 712f28cb -->

Port UDP, 16001 par défaut : le protocole du Tom's Live Chess Server, pour que TLCV et d'autres visionneuses TLCS
puissent suivre le tournoi (échiquier, pendules, PV, tableau croisé et parties jouées). Il est vérifié avec
node-tlcv. *Un port par partie* met chaque partie simultanée sur son propre port : la partie n° 1 de l'onglet *Direct*
sur le port UDP, la n° 2 sur le suivant, et ainsi de suite. Chaque port s'ouvre quand sa partie commence, avec ses
propres limites de clients et de connexions, et reçoit le tableau croisé après chaque partie ; *Partie diffusée* ne
s'applique alors pas.

### Page web
<!-- en: web-page bc743ffc -->

Port TCP : une page servie par RookStudio lui-même, pour n'importe quel navigateur du réseau, en lecture seule et
actualisée chaque seconde. La partie suivie (la première en cours, ou celle cliquée) est au milieu : l'échiquier avec le
dernier coup, un roi en échec, les coordonnées et les flèches ; la pendule de chaque joueur en gros caractères ;
l'évaluation de chaque moteur en gros caractères, avec celui qui réfléchit, sa recherche en vignettes
(profondeur/profondeur sélective, nœuds, vitesse, temps, accès aux tables, hash) et sa ligne de jeu ; et les coups, avec
les coups de livre en italique. Parcours les coups avec les flèches sous l'échiquier ou les touches ← → (▶ après le
dernier coup revient à la partie telle qu'elle se joue), retourne l'échiquier avec ⇅ ou F, et copie le PGN de la partie
ou le FEN de la position.

À côté de la partie, des onglets montrent le classement et le tableau croisé, les graphiques de la partie suivie, la
recherche de chaque moteur en moyenne par coup avec ses anomalies, les prochaines parties du calendrier (un suisse ou un
KO apparie ses rondes suivantes au fur et à mesure), les statistiques et anomalies des parties jouées, et l'événement.
Chaque partie en cours est montrée en dessous, avec une petite barre d'évaluation à côté de chaque joueur. *Moves* fait
passer les coups des lettres (Nf3) aux figurines (♘f3), et le thème suit le système ou est réglé sur clair ou sombre ;
le navigateur se souvient de ces choix. La page ne charge rien d'ailleurs et fonctionne sur les téléphones. *Download
the games (PGN)* donne le fichier PGN du tournoi, jusqu'à la dernière partie enregistrée. `state.json`, que la page lit,
contient tout cela pour les scripts. Les logos des moteurs (voir [Apparence](#apparence)) s'affichent à côté de leurs
noms, sous forme de petites images PNG que le programme en tire et sert lui-même ; la page ne connaît que ceux des
moteurs du tournoi.

### lichess
<!-- en: lichess 7597e86f -->

Une diffusion lichess, avec l'échiquier, la barre d'évaluation et la discussion de lichess, pour n'importe qui sur
internet. Il lui faut un jeton d'API personnel avec la portée `study:write` (*Créer un jeton sur lichess…*), gardé dans
`settings.json` dans le profil seulement, enregistré quand on quitte son champ. Sans ronde donnée, une diffusion
nommée d'après le tournoi est créée, non listée sauf si *Listée parmi les diffusions de lichess* est coché ; son lien
est affiché une fois qu'elle est à l'antenne. Sinon, les parties vont à la ronde dont l'URL ou l'identifiant est donné
(une diffusion alimentée par envois, pas par une URL source). Les parties en cours sont envoyées toutes les 8 secondes
avec leurs pendules, et chaque partie terminée une fois ; une ronde contient 64 parties, après quoi la ronde suivante
de la même diffusion est créée. Seules les parties terminées après son activation sont envoyées ; `lichess.json` dans
le dossier du tournoi se souvient de l'endroit où elles sont allées, si bien qu'un tournoi repris continue la même
diffusion. lichess ne garde que les pendules de ce qu'on lui envoie (il abandonne les `[%eval]` et le texte des
commentaires des coups) : sa barre d'évaluation est la sienne, et les évaluations des moteurs n'y apparaissent pas.

La connexion utilise Mbed TLS, intégré au programme, et ne fait confiance qu'aux certificats racines qui signent
lichess.org (Google Trust Services) et à ceux de Let's Encrypt. Elle passe par le proxy HTTP que nomme `HTTPS_PROXY`
(ou `ALL_PROXY`), sous la forme `http://proxy.example:3128` ou `http://user:password@proxy.example:3128`, sauf pour
les hôtes de `NO_PROXY` ; sous Windows aussi, le proxy vient de ces variables, pas des réglages du système. Les proxys
SOCKS ne sont pas pris en charge.

### Ta propre diffusion Lichess
<!-- en: your-own-lichess-broadcast 114edd02 -->

*Diffuser sur Lichess…*, dans l'en-tête d'un tournoi qui joue sur cette machine, ouvre une feuille : le nom de la
diffusion (celui du tournoi par défaut), non listée (pour ceux qui ont son lien) sauf si tu choisis *Publique : listée
sur Lichess*, et ce qui part chez Lichess, le nom, le format et la cadence du tournoi, et le PGN de chaque partie.
*Créer la diffusion* demande la première fois une permission de plus à ton compte associé, créer des études et des
diffusions (la page de Lichess s'ouvre pour cela), ou associe le compte avec elle ; le jeton collé dans la carte
lichess ci-dessus sert quand aucun compte n'est associé. Ensuite, les parties vont à la diffusion comme décrit plus
haut, et un bandeau sous l'en-tête dit combien de parties ont été envoyées et quand la dernière l'a été, avec *Ouvrir
sur lichess.org* et *Arrêter*.

### Un match par équipes à l'antenne
<!-- en: a-team-match-on-the-air fd32027f -->

Un match par équipes montre le score des équipes partout : dans l'en-tête de la page web et au-dessus de son
classement (avec le score % de l'équipe A, l'Elo ± et la LOS), dans `standings.html` (les équipes, puis les moteurs de
chaque équipe face à ceux de l'autre équipe), dans `tournament.json` et `state.json` (`teams`, `team_elo`,
`team_elo_error`, `team_los`), et dans le tableau croisé TLCS, sous forme d'une ligne sous le tableau (« Lynx 17½ -
18½ Otter · 48.6 % · Elo -9.7 ± 31.2 »). Ses parties portent les balises `WhiteTeam` et `BlackTeam` dans les fichiers
PGN, que les exports gardent.

### Mettre la page web sur internet
<!-- en: putting-the-web-page-on-the-internet ec2bc274 -->

La page web peut être suivie de n'importe où sans ouvrir de port dans le routeur : coche *Publier sur internet* sous
*Page web* dans l'onglet *Diffusion*. Pendant qu'un tournoi tourne, RookStudio ouvre un tunnel SSH (libssh2,
intégré au programme) vers un service de tunnel, qui donne à la page une adresse https publique ; rien n'a besoin
d'être installé.

- *localhost.run* (le défaut) : gratuit, sans compte. Ses adresses changent de temps en temps, et son débit est
  limité ; la page et son état sont envoyés compressés, si bien que cela compte peu. Sa clé d'hôte est connue de
  RookStudio : une connexion à quoi que ce soit d'autre est refusée.
- *Un autre serveur SSH* : son hôte, son port et son utilisateur, et le port qu'il ouvre pour la page (comme le
  demanderait `ssh -R REMOTE_PORT:localhost:PORT user@host` ; 0 le laisse choisir). Seuls les serveurs qui laissent
  entrer l'utilisateur sans clé ni mot de passe peuvent être utilisés (authentification « none »), et ils doivent
  proposer curve25519-sha256, une clé d'hôte RSA de 2048 à 4096 bits, et AES (GCM ou CTR). La clé du serveur est
  approuvée la première fois et gardée dans le profil (`known_hosts.json`) ; si elle change, le tunnel s'arrête et le
  dit, et *Faire confiance à la nouvelle clé* sert quand le propriétaire du serveur confirme qu'elle a changé.

L'onglet montre l'état (connexion, à l'antenne avec le nombre de connexions, reconnexion, ou une erreur) et, une fois
connue, l'adresse publique avec *Copier*, *Ouvrir* (pour voir la page comme les spectateurs) et un QR code pour un
téléphone (clique dessus pour l'agrandir). Le tunnel se reconnecte de lui-même après un échec, en attendant un peu plus
longtemps chaque fois, et montre la nouvelle adresse si elle a changé. Il s'arrête avec la page web ou avec le tournoi.

D'autres tunnels fonctionnent aussi, lancés à part : avec celui de Cloudflare, par exemple, active la page web (port
8080 ici) et lance `cloudflared tunnel --url http://localhost:8080` ; il affiche une adresse comme
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel ou un reverse proxy comme nginx, également sous un
chemin à lui (`location /live/ { proxy_pass http://localhost:8080/; }`), fonctionnent de la même façon.

La page est en lecture seule : elle ne répond qu'aux requêtes GET pour la page, son état et le fichier PGN, jamais à
un fichier qu'une requête nomme, et rien en elle ne change le tournoi. Chaque requête reçoit une réponse séparée, 32 à
la fois au plus, et doit arriver en moins d'une seconde et recevoir sa réponse en moins de dix, si bien qu'un
visiteur lent ou muet ne bloque personne d'autre ; derrière le tunnel, la connexion de chaque spectateur compte pour
l'une d'elles : 40 sont relayées à la fois, autant attendent leur tour, et les suivantes sont refusées ; un spectateur
qui envoie plus que ce que la page accepte est déconnecté, lui seul. Deux téléchargements du fichier PGN se font à la
fois, et il est demandé au suivant de revenir un instant plus tard. L'état n'est jamais mis en cache, par le tunnel
non plus, si bien que les spectateurs voient les parties au fur et à mesure ; la page est gardée par le navigateur et
revérifiée à chaque chargement. Les spectateurs affichés dans l'onglet *Diffusion* sont comptés d'après les requêtes
que font les pages, si bien que le compte est juste derrière un tunnel aussi.

Les spectateurs voient le nom du tournoi, les noms des moteurs, leurs classements, recherches et anomalies, les
ouvertures et le fichier PGN des parties jouées. Les dossiers et les fichiers des moteurs restent privés. Sans
interface :

```
rookstudio --run FOLDER --tunnel                      # localhost.run, the address printed on the console
rookstudio --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Où les spectateurs discutent
<!-- en: where-the-viewers-talk e66bf5c5 -->

La page n'a pas de discussion à elle ; elle renvoie là où la discussion a lieu. Un *Lien de discussion* sous *Page web*
(une invitation Discord, une chaîne Twitch, un fil de forum ; https seulement, tout le reste est refusé avec un
message sous le champ) met dans l'en-tête de la page un bouton *Chat* qui l'ouvre dans un nouvel onglet. Pendant que
les parties sont envoyées à lichess, la page renvoie aussi à la ronde lichess, qui a sa propre discussion.

Un *ID du serveur Discord* ajoute un onglet *Discord* à côté d'*Event*, avec le widget de serveur de Discord lui-même
dans le thème clair ou sombre de la page : le nom du serveur, les membres en ligne, ses salons vocaux et un bouton
*Join*, pas les messages. Le propriétaire du serveur l'active dans Paramètres du serveur, Widget (« Activer le widget
du serveur »), qui montre aussi l'ID du serveur à copier (17 à 20 chiffres) ; avec le mode développeur activé, un clic
droit sur l'icône du serveur donne aussi *Copier l'identifiant du serveur*. Le widget n'est chargé depuis discord.com
qu'une fois qu'un spectateur ouvre l'onglet, et Discord voit alors la visite de ce spectateur (son adresse, son
navigateur, sa connexion Discord s'il en a une) ; sans ID, la page ne contacte personne d'autre que RookStudio. Les
liens de discussion et de lichess n'envoient rien tant qu'un spectateur ne clique pas dessus, et alors pas de
référent. Un fichier de configuration lancé sans interface (`--run SETUP.json`) prend les deux dans `web_chat_link` et
`web_discord_id`.

## Mode sans interface
<!-- en: headless-mode a3cec26b -->

```
rookstudio --run TOURNAMENT_FOLDER     # resume a tournament
rookstudio --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Aucune fenêtre n'est ouverte ; la version, puis la progression, sont affichées dans la console, avec les essais d'un
moteur qui ne démarre pas, le moteur mis de côté (avec son journal moteur) ou revenu, comme dans la fenêtre, et le
classement toutes les 20 parties et à la fin (un match par équipes : les scores des équipes, puis les moteurs de chaque
équipe face à ceux de l'autre), et, à la fin d'un tournoi, la ligne des copies du classement (parties, nulles,
cadence, livre et événement). **Ctrl+C** arrête après les parties en cours et le tournoi peut être repris plus
tard, depuis la ligne de commande ou l'interface graphique. Codes de sortie : 0 terminé, 1 erreur pendant le jeu,
2 rien à lancer, 3 arrêté avant la fin (par Ctrl+C, ou parce qu'il ne reste rien à jouer sans un moteur qui ne démarre
pas). Le
budget de mémoire et les GPU des **Réglages** du profil s'y appliquent aussi.

`--detach` le joue plutôt en arrière-plan : la commande rend la main aussitôt, et le tournoi continue sans terminal ;
la fenêtre le montre (elle s'y connecte) quand elle s'ouvre avec le même profil, et `--attach` dialogue avec lui :

```
rookstudio --run FOLDER --detach         # in the background, on a local socket
rookstudio --attach FOLDER               # its control channel on the standard input and output
```

La fenêtre pilote chaque tournoi de la même façon : `--run FOLDER --control socket` (ou `--control stdio`) le joue sans
rien afficher, en échangeant des lignes JSON avec la fenêtre : les événements du tournoi (son état, chaque ligne du
journal, les parties en cours coup par coup, chaque partie enregistrée, son journal, sa fin) et les commandes de la
fenêtre (pause, reprise, arrêt, modifications, la fin d'une partie, sa part de la machine avec
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). La première ligne dans chaque sens est un bonjour,
`{"hello": {"protocol": 1, ...}}` ; un script passant par `--attach` reçoit le bonjour et les événements du tournoi, et
peut envoyer des commandes comme `{"c": "pause"}`. Une seule fenêtre (ou un seul script) à la fois dialogue avec un
tournoi ; `--attach` le lâche quand son entrée standard se termine ou que sa sortie n'est plus lue, si bien qu'un
script garde son entrée ouverte pendant qu'il lit les réponses.

Le tournoi peut être diffusé en même temps, comme depuis l'onglet *Diffusion* :

```
rookstudio --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
rookstudio --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
rookstudio --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
rookstudio --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
rookstudio --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
rookstudio --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` prend le jeton dans la variable d'environnement `FCG_LICHESS_TOKEN`, ou celui du profil.

Les options peuvent être combinées. Un fichier de configuration enregistré avec la diffusion activée diffuse de la même
façon sans elles. Pour des spectateurs hors du réseau local, ouvre les ports dans le routeur : TLCV et node-tlcv se
connectent au port TLCS. Pour la page web, un tunnel évite d'ouvrir un port (voir
[Mettre la page web sur internet](#mettre-la-page-web-sur-internet)).

## Machines
<!-- en: machines 4d8fbfef -->

D'autres ordinateurs sur lesquels faire tourner des tournois : une machine de test, un serveur, un second PC.
**Réglages** > *Machines* les liste. Une machine est jointe par SSH : rien n'a besoin d'y être ouvert à part son
serveur SSH, et le programme ne garde rien de secret.

### Ajouter une machine
<!-- en: adding-a-machine 6415de10 -->

*Ajouter une machine*, puis sur sa carte :

- *Nom*, *Hôte* (son nom sur le réseau, ou son adresse), *Port* (22) et *Utilisateur*, le compte sous lequel ses
  tournois tournent ;
- *Connexion avec* : l'*Agent SSH* (ssh-agent sous Linux et macOS, Pageant ou le service *OpenSSH Authentication
  Agent* sous Windows ; ajoutes-y ta clé avec `ssh-add`), ou un *Fichier de clé*, RSA ou Ed25519
  (`~/.ssh/id_ed25519`). La phrase de passe d'un fichier de clé est demandée une fois par session : elle reste en
  mémoire et n'est jamais écrite nulle part. La machine doit connaître la clé : sa moitié publique (`id_ed25519.pub`)
  va dans `~/.ssh/authorized_keys` sur la machine (`ssh-copy-id user@host` l'y met), ou, pour un administrateur d'une
  machine Windows, dans `C:\ProgramData\ssh\administrators_authorized_keys` ;
- *Dossier là-bas* : le dossier de RookStudio sur la machine, pour le programme, ses tournois, ses livres et ses
  tables, relatif au dossier personnel de l'utilisateur (`rookstudio` par défaut) ou absolu (`/srv/fcg`, `D:\fcg`) ;
- *Budget* : les cœurs et la mémoire que les tournois de la machine peuvent utiliser à la fois ; 0 prend ce que le test
  a trouvé, tous les threads sauf un et 80 % de la mémoire.

*Tester la connexion* se connecte, puis indique le système de la machine, ses threads, sa mémoire, l'espace libre là où
se trouve son dossier, et si RookStudio y est installé, dans quelle version. La première connexion montre la clé
d'hôte de la machine (son empreinte SHA256) et lui fait confiance dès lors (`known_hosts.json` dans le profil, celui du
tunnel de diffusion aussi). Quand la clé change plus tard, rien n'est envoyé et la carte le dit, avec *Faire confiance
à la nouvelle clé* pour une machine qui a été réinstallée. Chaque échec nomme la machine et ce qu'il faut faire : l'hôte
qui ne peut pas être joint, une clé que la machine refuse (et où va sa moitié publique), une phrase de passe qui n'est
pas la bonne, aucun agent en cours.

*Installer RookStudio* (*Mettre à jour RookStudio* une fois qu'il y est) copie le programme dans le dossier de la
machine par SFTP, puis vérifie qu'il y démarre (`rookstudio --version`). Il copie ce programme-ci quand la machine a
le même système (Linux x86-64 vers Linux x86-64) ; pour un autre, une machine Windows depuis Linux par exemple,
télécharge la version pour ce système et choisis son programme sur la carte (*RookStudio pour Windows x86_64*). Une
copie coupée par le réseau reprend là où elle s'est arrêtée.

### Moteurs sur une machine
<!-- en: engines-on-a-machine 4b00e462 -->

*Moteurs sur cette machine* : les moteurs de la bibliothèque que la machine a, chacun avec son chemin là-bas
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Vérifier les moteurs* vérifie chaque chemin : le programme est là,
peut être lancé, et répond à `uci` (`xboard` pour un moteur WinBoard) en moins de 10 secondes. La marque à côté, et son
infobulle, disent ce qui a été trouvé : le nom du moteur, ou pourquoi il ne tourne pas (manquant, non exécutable, fait
pour un autre système, muet).

Un moteur absent de la machine y est copié avec *Copier…* sur sa ligne (ou *Copier \<moteur\> sur \<machine\>* dans la
palette de commandes, ou le bouton d'un lancement refusé parce qu'un moteur n'a pas de chemin là-bas). Le panneau qui
s'ouvre dit d'abord ce qui part : le programme, sa taille et le système pour lequel il est compilé, lu dans ses
premiers octets (un programme Windows, un programme Linux ou macOS, universel ou non, ou un script). Un programme que
la machine ne peut pas lancer est refusé avant que quoi que ce soit soit envoyé, en disant quelle compilation choisir :
« Alpha est un programme Windows ; tc-box tourne sous Linux : choisis la compilation Linux d'Alpha ». L'architecture
compte aussi (x86-64 ou arm64) ; une compilation x86-64 sur un Mac arm64 ne tourne que sous Rosetta, et un format que
le programme ne connaît pas est copié quand même : les deux sont signalés, pas refusés. Un script ne va que sur une
machine Linux ou macOS, et seulement quand l'interpréteur de sa première ligne (`python3`, `/bin/sh`) y est.
*Choisir la compilation \<système\> de \<moteur\>…* prend un autre fichier de cet ordinateur, la compilation Windows du
moteur par exemple, le vérifie de la même façon, et le garde pour cette machine : les comparaisons et remplacements
suivants l'utilisent, pas le programme de la bibliothèque.

*Avec son dossier* copie tout le dossier du moteur à côté du programme (son dossier de travail, ou le dossier du
programme), pour un moteur qui a besoin de fichiers à côté de lui : ses réseaux, un livre. Le panneau compte d'abord
ses fichiers et leur taille, et nomme les options du moteur qui pointent à l'intérieur (`EvalFile` d'un réseau) : dans
un tournoi sur la machine, elles pointent vers la copie là-bas, et le dossier est le dossier de travail du moteur. Un
dossier de plus de 2000 fichiers n'est pas proposé (un programme dans `/usr/bin` n'est pas à copier avec son dossier),
un dossier `.git` est laissé de côté, et pour une machine Windows, un nom de fichier que Windows ne peut pas accepter
(`:`, `*`, `?`, un point final, `CON`, `NUL`...) est nommé et arrête la copie avant qu'elle commence. Les fichiers sont
copiés tels quels, fins de ligne comprises.

*Copier* envoie les fichiers par SFTP vers `engines/<engine>/` dans le dossier de la machine, en arrière-plan avec le
temps restant et *Annuler*, la copie reprenant là où elle s'est arrêtée après une coupure ; le SHA-256 de chaque fichier
est comparé à celui de cet ordinateur après la copie, et les fichiers déjà là et identiques ne sont pas renvoyés. Sous
Windows, le programme garde ou reçoit son `.exe` ; ailleurs, il est rendu exécutable (`chmod 755`). Puis le chemin du
moteur sur la machine est réglé sur la copie et vérifié comme le fait *Vérifier les moteurs*. Sur un Mac, un programme
copié depuis un autre ordinateur, s'il ne démarre pas, peut avoir besoin d'une signature ou de sortir de la quarantaine
: le message donne les deux commandes à y lancer (`codesign -s -` et `xattr -d com.apple.quarantine`) ; le programme ne
les lance pas lui-même.

La colonne *Compilation* compare le moteur de là-bas avec celui de cet ordinateur, par leur SHA-256 (*Vérifier les
moteurs* et *Comparer* le font, le programme de cet ordinateur étant haché une fois jusqu'à ce qu'il change) : *même
compilation que cet ordinateur*, ou *une autre compilation là-bas*, avec *Remplacer par celui de cet ordinateur*.
Remplacer copie le programme de cet ordinateur dans le dossier du moteur, dans le dossier de la machine, et y fait
pointer le moteur : un programme situé ailleurs sur la machine (`/opt/engines/stockfish`) n'est jamais écrasé, il
reste tel qu'il était.

### Livres et tables sur une machine
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Vérifier ce que la configuration utilise* compare le livre d'ouvertures et les livres propres des participants de la
configuration avec le dossier des livres de la machine, par taille et SHA-256 (un livre du même nom mais d'un autre
contenu n'est pas le même livre), et les fichiers Syzygy de 6 pièces au plus de la configuration avec le dossier des
tables de la machine, par nom et taille. *Envoyer les fichiers manquants* copie ce qui manque ou diffère, en
arrière-plan, avec le temps restant ; chaque livre est vérifié par son hachage une fois copié, chaque table par sa
taille, et une copie interrompue reprend. Les tables de 7 pièces ne sont pas proposées : elles occupent des
téraoctets. Un tournoi ne démarre pas sur une machine sans son livre ; des tables manquantes sont un avertissement
(l'adjudication et les moteurs s'en passent).

### Un tournoi sur une machine
<!-- en: a-tournament-on-a-machine f5d4c423 -->

Dans la configuration, *Jouer sur* (dans *Parallélisme*) choisit où le tournoi joue : *Cet ordinateur*, ou une
machine. *Lancer* l'y prépare alors : RookStudio doit y être installé dans cette version (sinon *Mettre à jour
RookStudio*), chaque moteur doit avoir son chemin sur la machine (sinon le lancement propose *Copier \<moteur\> sur
\<machine\>…*), et les livres de la configuration doivent être dans son dossier des livres (sinon le lancement dit lequel
manque, et *Envoyer les fichiers manquants* l'y met) ; des tables manquantes ne sont qu'un avertissement. Le tournoi
reçoit son dossier dans `tournaments/` du dossier de la machine, avec les moteurs, les livres et les tables à leurs
chemins là-bas, et son processus y démarre hors de la session SSH (`rookstudio --run <folder> --detach`) : il
joue quoi qu'il arrive à la fenêtre ou au réseau. La fenêtre le suit comme un tournoi de cet ordinateur : Direct,
Parties, le classement, la pause, l'arrêt et les modifications, par le canal de contrôle de son processus
(`rookstudio --attach <folder>` par SSH).

Sa carte sur la page Tournois dit *Sur \<machine\>*, et *Connexion à \<machine\>…* pendant que la connexion est coupée : la
fenêtre se reconnecte d'elle-même, en attendant un peu plus après chaque échec, et reprend les événements là où sa copie
s'était arrêtée. Le journal et les fichiers PGN sont recopiés dans le dossier du tournoi ici au fur et à mesure qu'ils
grandissent, toutes les quelques secondes, pour que Classements, les exports et la liste de contrôle avant envoi les
lisent ; le dossier de la machine reste la référence (`machine.json` ici dit quelle machine et quel dossier). Fermer la
fenêtre le laisse jouer là-bas ; le lancement suivant le montre de nouveau. *Reprendre* sur un tournoi interrompu d'une
machine le reprend là-bas. Le budget de la machine (ses cœurs et sa mémoire) est partagé par ses propres tournois,
séparément de celui de cet ordinateur. Un tournoi joue sur une seule machine : ses parties ne sont pas réparties sur
plusieurs.

Une machine a besoin d'un serveur OpenSSH (Linux, macOS, BSD ; Windows 10 1809 et ultérieur), avec une clé d'hôte RSA
et SFTP, tous deux présents par défaut. Les commandes passent par `sh` quel que soit le shell de l'utilisateur (fish ou
csh conviennent), et par cmd sous Windows, le shell par défaut de son serveur OpenSSH.

### Moteurs joués depuis cet ordinateur
<!-- en: engines-played-from-this-computer 785e6bae -->

Un moteur de la bibliothèque peut tourner sur une machine pendant que le tournoi joue ici : un moteur GPU sur la machine
à GPU, un moteur compilé pour un autre système, une machine réservée à un moteur. Sur sa carte, *Tourne sur* choisit
*Cet ordinateur* ou une machine ; *Programme là-bas* et *Dossier de travail là-bas* sont alors des chemins sur la
machine (vide : le dossier du programme là-bas), et les boutons *…*, ceux des options qui nomment un fichier compris,
parcourent les fichiers de la machine. Dès qu'il y a des machines, *Ajouter…* propose *Un programme sur \<machine\>…*,
*Importer…* propose *Tous les moteurs d'un dossier sur \<machine\>…*, et *Ajouter un moteur…* de la machine propose *Un
programme là-bas…*. Le moteur est lu, vérifié, et joué dans la console, le test rapide et les tournois comme l'est un
moteur de cet ordinateur. Son chemin est aussi celui de la liste des moteurs de la machine, et le SHA-256 de son
programme là-bas dit quand il a été recompilé (Δ, relu).

La machine doit avoir été testée une fois (*Tester la connexion*), pour que sa clé d'hôte soit connue : un moteur ne se
connecte jamais à une machine dont la clé n'a pas été montrée et acceptée. La phrase de passe de la clé, si elle en a
une, est demandée une fois par session, avant la première lecture ou le lancement, et gardée en mémoire : le processus
du tournoi la reçoit de la fenêtre et la garde, pour que le tournoi continue de jouer une fois la fenêtre fermée.
`--run` dans un terminal ne la demande pas : là, utilise l'agent SSH.

Chaque moteur est lancé par un relais, `rookstudio --remote-engine <key>`, que sert le processus qui le fait jouer :
une connexion SSH à la machine pour dix moteurs au plus (le `MaxSessions` d'un serveur OpenSSH), chaque moteur étant
lancé depuis son dossier là-bas. Un moteur dont le relais disparaît est arrêté là-bas aussi, par son identifiant de
processus, quand il ne s'arrête pas de lui-même (sur une machine Linux ou macOS ; sur une machine Windows, son canal est
fermé).

*Vérifier* mesure l'aller-retour jusqu'à la machine, la médiane de quelques `isready`. La pendule le compte : le résumé
de la configuration avertit quand il prend plus de 5 % de l'incrément, ou du temps dont dispose un coup sans incrément,
avec *Mettre une marge de … ms* (deux fois l'aller-retour). Une machine jamais testée arrête le lancement, de même qu'un
tournoi qui joue sur une autre machine que celle du moteur (un tournoi qui joue sur la machine du moteur l'y fait jouer
comme son propre moteur).

Pendant que le tournoi joue, la page Tournois a une ligne *Machines* sous les cœurs : chaque machine, connectée et ce
qui y joue, ou depuis quand elle ne répond pas. Une machine qui ne répond pas fait une seule entrée sous *À traiter* :
les parties de ses moteurs sont mises de côté et réessayées, les autres continuent, avec *Réessayer* et *Tester
\<machine\>*. Une partie qu'un moteur a perdue parce que sa connexion s'est rompue (sa fin étant une déconnexion) est
annulée et rejouée, écrite dans le journal avec sa raison (`connection to its machine lost: <machine>`) ; *Santé* la
compte à part, pas comme un plantage du moteur. Une machine coupée plus de trois fois en une heure voit ses moteurs mis
de côté. Les cœurs, la mémoire et les GPU de cet ordinateur ne comptent pas les moteurs sur des machines, seulement
leurs relais ; le budget de la machine, que partagent ses propres tournois, ne les compte pas encore.

### Une machine Windows
<!-- en: a-windows-machine cd5493d1 -->

Windows 10 (1809 et ultérieur) et Windows 11 contiennent un serveur OpenSSH, non installé par défaut. Dans PowerShell,
en administrateur :

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

L'installation ouvre le port 22 dans le pare-feu. Mets ensuite ta clé publique dans
`C:\Users\<user>\.ssh\authorized_keys`, ou, pour un compte du groupe Administrateurs, dans
`C:\ProgramData\ssh\administrators_authorized_keys`, que seuls les Administrateurs et SYSTEM peuvent lire (les groupes
sont désignés par leurs SID, les mêmes dans toutes les langues de Windows) :

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Garde cmd comme shell du serveur : RookStudio parle cmd, pas PowerShell (si une valeur `DefaultShell` a été créée
sous `HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, supprime-la). Sur la carte de la machine, choisis le `rookstudio.exe` de
la version Windows, puis *Installer RookStudio*.

## Fichiers
<!-- en: files d79e45d8 -->

Tout se trouve dans le dossier du profil : `~/.local/share/rookstudio/` sous Linux,
`%APPDATA%\rookstudio\` sous Windows, `~/Library/Application Support/rookstudio/` sous macOS.
Les fichiers que le programme crée ne sont pas modifiables par les autres utilisateurs, et `settings.json` n'est
lisible que par son propriétaire dès qu'il contient un jeton lichess. **Réglages** montre le dossier du profil, avec
*Ouvrir le dossier*.

### Mode portable
<!-- en: portable-mode 228dba26 -->

Avec un fichier nommé `portable` (vide, sans extension) à côté du programme (à côté de `rookstudio.exe` ou de
`rookstudio`, à côté de `RookStudio.app` sous macOS), le profil est à la place le dossier `data` à côté du
programme, et le dossier des tournois par défaut est `data/runs` : rien n'est écrit ailleurs, sauf dans les dossiers
choisis pour les tournois, les exports et autres. Les chemins de ce qui se trouve dans le dossier du programme
(moteurs, livres, tables, suites de tests, fichiers PGN des classements et de la configuration, dossiers choisis) sont
gardés relatifs à lui dans les fichiers du profil, si bien que le dossier entier peut être déplacé, copié sur une
autre machine ou lancé depuis une clé USB, quelle que soit sa lettre de lecteur, et ses moteurs sont retrouvés.
**Réglages** affiche *Mode portable* à côté du dossier du profil. Pour reprendre un profil existant, copie ses
fichiers dans `data`. `--profile` choisit toujours un autre dossier, hors du mode portable. Un tournoi garde les
chemins des moteurs du moment de sa création : reprends un tournoi inachevé avant de déplacer le dossier. Sous macOS,
retire d'abord la quarantaine (voir le README) : sinon macOS lance l'application depuis un emplacement temporaire, où
le fichier `portable` n'est pas.

### Le dossier du profil
<!-- en: the-profile-folder e39d5884 -->

- `engines.json` : la bibliothèque de moteurs, et `engines.json.bak` sa version précédente, lue si la première est
  perdue ;
- `setup.json` : la dernière configuration ; `profiles.json` : tes profils des sections de la configuration ;
  `templates/*.json` : les modèles enregistrés ; `queue.json` : la file d'attente ;
- `book-cache/` : les livres PGN transformés en livres Polyglot pour les livres propres des participants (supprimés
  après 30 jours sans usage) ;
- `layout.ini` : la disposition de la fenêtre et les panneaux de l'espace de travail ; `layouts.json` : les
  dispositions enregistrées par nom ; `rookstudio.log` : le journal de la dernière session ;
- `suite.json` : la dernière configuration de la page Suites ; `suites/<suite>-<date>.json` : les passages de suites
  de tests terminés ;
- `board.json` : les réglages de l'analyse et des parties de la page Échiquier ; `analyses/*.json` : les parties
  analysées coup par coup, un fichier chacune ; `games/*.pgn` : les parties jouées depuis la page Échiquier ;
- `settings.json` : le dossier des tournois, quand un autre a été choisi dans **Réglages**, *Rendu logiciel*, le
  dossier de *Tous les moteurs d'un dossier…*, le testeur et le lieu d'*Exporter pour une liste de classement…*, le
  jeton lichess, la taille et la position de la fenêtre, la largeur du panneau du guide, la carte des premiers pas
  masquée, la recherche de nouvelle version (voulue ou non, sa dernière date, la version trouvée, l'avis masqué),
  l'apparence (couleurs de l'échiquier, pièces, sons des coups), et le budget de la machine (cœurs, mémoire et GPU pour
  les tournois, *Lancer quand des cœurs sont libres*) ;
- `logos/` : les logos des moteurs, copies des images choisies sur leurs fiches ;
- `machines.json` : les machines distantes des **Réglages** (leur adresse, leur dossier, leurs budgets, les chemins
  des moteurs et ce que le dernier test a trouvé ; pas de phrase de passe), et `known_hosts.json` les clés d'hôte
  approuvées, des machines et du tunnel ;
- `running.json` : les tournois qui jouent en ce moment, avec leurs poids, chacun retiré quand il s'arrête ; trouvé au
  lancement, il dit quoi montrer de nouveau (un tournoi qui a continué de jouer sans la fenêtre) ou reprendre ;
- `runs/` : les tournois, à moins qu'un autre dossier ait été choisi (voir
  [Le dossier d'un tournoi](#le-dossier-dun-tournoi)) ;
- `databases.json` : tes bases, et `databases/` les dossiers de celles créées ici (`Name.fcgdb`) ; `searches.json` :
  les recherches gardées ; `repertoires/` : tes répertoires, un fichier PGN chacun ;
- `recent.json` : la partie de la page Échiquier et les parties récentes, avec leurs modifications non enregistrées ;
- `engines/` : les moteurs populaires téléchargés, chaque version dans un dossier à part ;
- `assistant.json` : les réglages de l'assistant, et `models/` son modèle ;
- `lichess-account.json` : le compte Lichess associé, ses permissions et son jeton (un fichier lisible par toi seul) ;
  `lichess-cache/` : les réponses des tables et du cloud de Lichess.

### Le dossier d'un tournoi
<!-- en: a-tournaments-folder 2189632d -->

`runs/<date>_<A>_vs_<B>/` pour un match, `runs/<date>_<name>/` pour un tournoi (dans `runs/` ou le dossier choisi), un
dossier chacun, qui peut être déplacé, copié, zippé et envoyé :

- `tournament.json` : le format, les participants (moteurs copiés avec leurs options) et les conditions ;
- `setup.json` : la configuration de la page *Nouveau tournoi* dont il est parti, que lisent *Cloner* et *Relancer
  avec…* (voir [File d'attente et relance](#file-dattente-et-relance)) ; un tournoi lancé par une version plus
  ancienne n'en a pas ;
- `journal.jsonl` : tout ce qui s'est passé depuis, une ligne par événement (paires de parties commencées, parties
  terminées, modifications, appariements des suisses et des KO) ; avec les fichiers PGN, c'est ce que lit une reprise.
  Une partie est sur le disque avant sa ligne, si bien qu'une coupure de courant fait perdre au plus les parties en
  cours. Le texte d'une partie vers lequel aucune ligne ne pointe (une partie abandonnée par une modification pendant
  qu'elle était enregistrée, ou laissée par un plantage) est remplacé par des lignes vides, si bien que les autres
  visionneuses PGN ne montrent que les parties du tournoi ;
- `tournament.lock` : tenu par le programme qui joue le tournoi ; un autre programme qui ouvre le dossier ne fait que
  le montrer ;
- `<name>.pgn` : commentaires des coups dans le style cutechess / fastchess, `{+0.35/18 1.234s}`, avec le score du
  point de vue du camp qui a joué ; les parties dans l'ordre où elles se sont terminées (le journal retrouve chacune par
  sa place dans le fichier ; *Exporter tout…* dans l'onglet *Parties*, et le fichier d'*Ajouter aussi chaque partie à*,
  suivent le calendrier) ;
- `<name>-chessbase.pgn` : `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, avec l'évaluation du point de
  vue des Blancs, telle que la lisent ChessBase / Fritz et Lichess ;
- `<name>` est le nom du tournoi au moment de sa création (le renommer plus tard garde les fichiers), avec les
  caractères que Windows n'autorise pas remplacés par `_` et coupé à 64 octets ; `tournament.json` enregistre les deux
  noms. Un tournoi sans nom, un tournoi plus ancien, ou un match importé de la 0.2.x a `games.pgn` et
  `games-chessbase.pgn` ;
- `lichess.json` : la diffusion et la ronde lichess où sont allées ses parties (pas de jeton) ;
- `processor.tsv` : le temps processeur qu'ont utilisé les moteurs de chaque partie, pour l'*Utilisation du
  processeur* d'un tournoi repris ;
- `rookstudio.log` : le journal des processus qui l'ont joué (chaque tournoi joue dans son propre processus) ;
  `control.json`, pendant qu'un processus le joue : où est son socket local (`control.sock` dans le dossier, ou dans
  le dossier d'exécution de l'utilisateur quand ce chemin est trop long ; un tube nommé sous Windows), et quel
  processus écoute ;
- `logs/game-N.log` : chaque ligne échangée avec les deux moteurs pendant la partie N (le numéro affiché dans l'onglet
  *Parties*), avec l'heure à laquelle elle a été envoyée ou lue (`W>` envoyée au moteur des Blancs, `W<` lue sur sa
  sortie, `W!` lue sur sa sortie d'erreur : assertions, messages de plantage), pour les parties perdues au temps, par un
  plantage, un blocage ou un coup illégal (ou toutes les parties, si on le demande). Les 300 premières lignes de chaque
  moteur et les 5000 dernières lignes de la partie sont gardées, dans l'ordre où elles sont venues. C'est ce dont
  l'auteur d'un moteur a besoin pour reproduire un bug. Pour un moteur WinBoard, il montre le côté UCI ;
  `FCG_WB2UCI_LOG` donne le côté WinBoard ;
- `logs/start-<engine>.log` : le dernier essai raté de lancer un moteur qui ne démarre pas : son programme, l'erreur,
  et les lignes échangées avec les moteurs de cette partie jusque-là (voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)).

Un dossier reçu de quelqu'un d'autre peut être ouvert sans risque : le fichier d'*Ajouter aussi chaque partie à* doit
être un fichier `.pgn` (un autre nom est ignoré, avec un avertissement), et un dossier dont le journal, les fichiers
PGN ou le fichier de verrou sont des liens symboliques n'est pas ouvert en écriture, si bien qu'il ne peut pas faire
écrire le programme en dehors de lui. Le reprendre lance les moteurs que nomme son `tournament.json`, comme le fait
tout tournoi.

Les matchs joués avec la version 0.2 apparaissent sur la page Tournois : *Les importer* en fait des tournois terminés.

## Ligne de commande
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

- `--profile DIR` utilise un autre dossier de profil ;
- `--autostart` lance la configuration enregistrée au démarrage (plutôt que de reprendre le tournoi qui était en
  cours, voir [Pause, arrêt et reprise](#pause-arrêt-et-reprise)) ;
- `--size WxH` ouvre la fenêtre à cette taille, centrée, plutôt que comme elle a été laissée, et ne touche pas à la
  taille et à la position gardées pour le lancement suivant ;
- `--software` fait dessiner la fenêtre par le processeur (le rendu logiciel de SDL) plutôt que par OpenGL, comme le
  fait *Rendu logiciel* dans **Réglages**, pour ce lancement (voir [Rendu de la fenêtre](#rendu-de-la-fenêtre)) ;
- `--shot` enregistre une capture d'écran de la fenêtre après le nombre de secondes donné (5 par défaut), avec WINDOW
  mis au premier plan, et quitte une fois toutes les captures faites. Il peut être répété. WINDOW est une page
  (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`,
  `console`, `settings`) ou un onglet (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`).
  La souris est ignorée dans ce mode et, sous Linux, la fenêtre est rendue hors écran (aucune fenêtre n'apparaît ;
  règle `SDL_VIDEODRIVER` pour passer outre) ; la fenêtre s'ouvre à la taille par défaut, ou à `--size`, et la taille
  et la position gardées ne sont pas touchées ;
- `--focus WINDOW` ouvre cette page ou cet onglet au lancement, ou : `engines-card` (la fiche du premier moteur),
  `engines-refresh` (*Actualiser : les nouveaux moteurs du dossier*), `engines-ratings=FILE` (l'aperçu de cette liste
  de classement), `setup-picker` et `setup-suggest` (le filtre de la bibliothèque avec le focus, ou les adversaires
  suggérés sélectionnés), `setup-open=ROW` et `setup-profiles=ROW` (une ligne de la configuration ouverte, ou sa liste
  de profils : `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (le livre
  propre du premier participant), `submission-export` (*Exporter pour une liste de classement…*), `ratings-players`
  (la fenêtre *Joueurs PGN…*), `clone=DIR` (la page *Nouveau tournoi* remplie par *Cloner* du dossier de ce
  tournoi), `palette[=TEXT]` (la palette de commandes, avec TEXT tapé dedans), `game-menu=N` (le
  menu de la partie N dans l'onglet *Parties*, un résultat choisi), `end-menu=N` (*Terminer avec un résultat* de la
  partie de l'emplacement N, à partir de 1), `big-screen[=N]` (le grand écran, de la partie de l'emplacement N),
  `live-back=N` (la partie suivie dans *Direct*, reculée de N demi-coups), `engines-console` (la console du premier
  moteur), `board-analyse` (la page Échiquier, analysée), `board=FILE` (un fichier PGN, FEN ou EPD sur la page
  Échiquier ; `board-analyse=FILE` analysé, `board-review=FILE` sa première partie analysée coup par coup),
  `board-edit` (l'éditeur de position), `board-play` et `board-play-start` (une partie entre les deux premiers
  moteurs, lancée), `machines` (la carte de la première machine distante dans **Réglages** ; `machines-test` testée,
  `machines-files` les livres et les tables de la configuration comparés avec elle), `guide[=ID]` (le panneau du guide,
  à la section ID), `guide-search=TEXT` (le panneau du guide, en train de chercher TEXT) ;
- `--select-game N` ouvre la partie N (dans l'ordre de fin) dans l'onglet *Parties* dès qu'elle existe ;
- `--quit-after SECONDS` arrête le tournoi et quitte ;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance` : voir [Mode sans interface](#mode-sans-interface) ;
- `--version` affiche la version et le protocole du canal par lequel la fenêtre suit un tournoi distant (voir
  [Machines](#machines)), et quitte.

## Dépannage
<!-- en: troubleshooting e8c4882d -->

### Le programme ne démarre pas
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows dit que le programme n'est pas sûr, ou un antivirus le supprime.** Le programme n'est pas signé, si bien
  que Windows SmartScreen et certains antivirus s'en méfient. *Informations complémentaires*, *Exécuter quand même*
  le laisse démarrer ; on peut dire à un antivirus d'autoriser son dossier. Les versions sont compilées par GitHub
  Actions à partir des sources publiques.
- **macOS refuse de l'ouvrir.** Il n'est pas signé par un compte de développeur Apple : Contrôle-clic (clic droit)
  dessus, Ouvrir, puis Ouvrir de nouveau ; sur macOS 15 et ultérieur, essaie de l'ouvrir une fois, puis Réglages
  Système, Confidentialité et sécurité, Ouvrir quand même. Ou lance `xattr -dr com.apple.quarantine` sur le dossier
  décompressé.
- **Un message dit qu'aucune manière de dessiner la fenêtre ne fonctionne, ou la fenêtre reste noire.** Le pilote
  graphique n'a pas OpenGL 3.2 ou échoue : lance avec `--software` (le processeur dessine la fenêtre), et si cela
  fonctionne, coche *Rendu logiciel* dans **Réglages**. Sous Wayland sans pilote graphique, lance avec
  `SDL_VIDEODRIVER=x11`. Voir [Rendu de la fenêtre](#rendu-de-la-fenêtre).
- **Le texte chinois s'affiche en carrés.** Le chinois a besoin d'une police du système : Microsoft YaHei sous
  Windows, Noto Sans CJK ou WenQuanYi sous Linux (installe `fonts-noto-cjk` ou un équivalent).

### Un moteur ne démarre pas
<!-- en: an-engine-does-not-start 202a67d4 -->

- **Sa vérification échoue, ou son tournoi dit qu'il ne démarre pas.** Lis l'erreur sous le moteur, puis son journal
  moteur (*Journal moteur*, ou `logs/start-<engine>.log` dans le dossier du tournoi) : la sortie d'erreur du moteur
  nomme souvent le fichier manquant. Les causes habituelles : le programme a été déplacé (*Localiser…*), un réseau ou
  une bibliothèque introuvable (règle le dossier de travail, ou `EvalFile`, sur sa fiche), un programme compilé pour un
  autre processeur (une compilation qui utilise l'AVX-512 sur une machine qui ne l'a pas), un démarrage lent (augmente
  le *Délai de démarrage* sur sa fiche). Voir [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas).
- **Un moteur WinBoard ne part pas des positions du livre.** Un moteur du protocole 1 sans `setboard` ne peut partir
  que de la position initiale : donne-lui un livre PGN (des coups), pas un livre EPD. Voir
  [Moteurs WinBoard](#moteurs-winboard).
- **Le tournoi refuse de démarrer : un moteur ne joue pas le Chess960.** Chaque moteur doit avoir l'option
  `UCI_Chess960` (UCI) ou la variante `fischerandom` (WinBoard) ; le résumé nomme ceux qui ne l'ont pas.

### Des parties sont perdues ou semblent fausses
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Un moteur perd au temps tôt dans ses parties.** Il est plus souvent mal réglé qu'il ne joue mal : trop de threads
  pour les cœurs dont disposent les parties, un hash trop grand pour être alloué à temps, un réseau chargé à chaque
  partie, un démarrage lent. Baisse ses `Threads` et son `Hash`, décoche *Relancer le moteur à chaque partie*, augmente
  le *Délai de démarrage* ; la carte *Santé* et la colonne *TF* de la vue d'ensemble montrent les pertes au temps.
- **Les moteurs semblent plus lents qu'ils ne devraient.** Trop de parties à la fois : laisse le *Parallélisme* en
  automatique, et garde parallélisme × threads dans la limite des cœurs physiques. *Utilisation du processeur*, dans la
  vue d'ensemble, nomme un moteur qui utilise plus de cœurs que ses `Threads`, ou réfléchit sur le temps de son
  adversaire.
- **Le résumé avertit que la mémoire ne suffira pas.** Les tables de hachage des parties ne tiendraient pas dans la
  mémoire libre : baisse le `Hash` (dans *Réglages des moteurs*, ou celui du moteur qu'il nomme) ou le parallélisme.
- **Des fichiers de tables seraient manquants ou tronqués.** Un téléchargement s'est arrêté à mi-chemin, ou le dossier
  ne contient que des tables plus petites : télécharge de nouveau les fichiers nommés dans l'infobulle, ou ramène
  l'adjudication aux pièces trouvées (*Descendre à 5 pièces*).
- **Un livre contiendrait des positions illégales ou des doublons.** fastchess les écarte ; *Enregistrer une copie
  nettoyée…* sur la page Livres écrit un livre sans eux. Voir [Vérifier un livre](#vérifier-un-livre).
- **Une partie a été mal tranchée.** Clic droit dessus dans *Parties* ou *Calendrier* : la rejouer, l'annuler, ou fixer
  son résultat, avec la raison. Voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours).

### Problèmes de diffusion
<!-- en: broadcasting 4a2e6916 -->

- **TLCV ou une autre visionneuse ne se connecte pas.** Lance le tournoi (et son serveur TLCS) avant la visionneuse :
  elle ne se reconnecte pas d'elle-même. Ouvre le port UDP dans le routeur pour les spectateurs hors du réseau local.
- **La page web n'est pas joignable de l'extérieur.** Coche *Publier sur internet* pour un tunnel (voir
  [Mettre la page web sur internet](#mettre-la-page-web-sur-internet)), ou ouvre le port TCP dans le routeur.
- **lichess refuse le jeton.** Le jeton a besoin de la portée `study:write` : crées-en un nouveau avec *Créer un jeton
  sur lichess…*. Derrière un proxy, règle `HTTPS_PROXY`.

### Autre chose
<!-- en: anything-else ce59c083 -->

*Signaler un problème…* dans **Réglages** ou sur la page **Journal** ouvre un ticket GitHub déjà rempli avec la
version, le système et les dernières lignes du journal (voir [Signaler un problème](#signaler-un-problème)).

## Glossaire
<!-- en: glossary 7cb8fba3 -->

- **Adjudication** : terminer une partie avant le mat quand son résultat est clair : une nulle quand les deux scores
  restent proches de 0, une défaite quand ils restent très en dessous, ou ce que disent les tables. Voir
  [Adjudication](#adjudication).
- **BayesElo, DrawElo** : un modèle de classement avec un paramètre à part pour les nulles (DrawElo), tiré du
  programme de Rémi Coulom.
- **Buchholz, Sonneborn-Berger** : départages : la somme des scores des adversaires (un suisse), et la somme des scores
  des adversaires battus plus la moitié de ceux avec qui la partie a été nulle (un toutes rondes).
- **Chess960, Chess324** : des positions de départ avec la première rangée mélangée : 960 avec le roi entre les tours
  (le roque allant sur ses cases habituelles), 324 avec le roi et les tours à leur place.
- **Cycle** : dans un toutes rondes, chaque appariement joué une fois avec chaque couleur (cutechess l'appelle une
  ronde, de 2 parties par rencontre).
- **ECO** : les codes de l'Encyclopaedia of Chess Openings, de A00 à E99.
- **Elo, nElo** : la différence de classement que représente un score. Le nElo (Elo normalisé) divise par la
  dispersion des résultats, si bien que sa valeur ne dépend pas du taux de nulles ; fishtest et OpenBench expriment
  en nElo les bornes de leurs SPRT.
- **EPD, FEN** : une ligne qui décrit une position ; une ligne EPD peut porter des opérations, comme le meilleur coup
  `bm` d'une suite de tests.
- **Gain maudit** : une position que les tables comptent comme gagnée, mais dont le mat demande plus de coups que la
  règle des 50 coups n'en permet : une nulle dans une partie jouée selon cette règle.
- **Gauntlet** : les moteurs testés (les têtes de série) jouent contre tous les autres moteurs, qui ne jouent pas entre
  eux.
- **H0, H1** : les deux hypothèses d'un SPRT : la différence d'Elo est de elo0 ou moins (H0), ou de elo1 ou plus (H1).
- **Hash** : la mémoire qu'un moteur prend pour sa table de transposition, en Mo.
- **LLR** : le log-likelihood ratio (logarithme du rapport de vraisemblance) d'un SPRT ; le test se termine quand il
  sort de ses bornes, calculées à partir d'alpha et beta.
- **LOS** : la likelihood of superiority, la probabilité qu'un moteur soit plus fort qu'un autre, d'après leurs
  parties.
- **MultiPV** : un moteur qui analyse plusieurs lignes à la fois.
- **Nouveauté** : le premier coup d'une partie qu'aucune partie de la base n'avait joué dans cette position.
- **Parallélisme** : le nombre de parties jouées en même temps.
- **Pentanomial** : les résultats des paires de parties comptés selon leur total : 0, ½, 1, 1½ ou 2 points. Il donne
  des barres d'erreur plus serrées que le compte des parties seules, puisque les deux parties d'une paire partagent une
  ouverture.
- **PGN** : le format texte des parties d'échecs : des balises comme `[White "…"]`, puis les coups.
- **Polyglot** : le format binaire de livres d'ouvertures (`.bin`) lu par la plupart des programmes, avec un poids pour
  chaque coup.
- **Ponder** : réfléchir sur le temps de l'adversaire ; désactivé dans les tournois de RookStudio.
- **Répertoire** : les ouvertures qu'on joue, un coup choisi pour son camp dans chaque position, et les coups de
  l'adversaire qui ont leur réponse.
- **SPRT** : le sequential probability ratio test (test séquentiel du rapport de vraisemblance), qui joue des parties
  jusqu'à pouvoir dire, avec les taux d'erreur choisis (alpha, beta), si un moteur gagne elo1 ou plus (H1) ou pas plus
  de elo0 (H0).
- **Suite d'ouvertures, livre** : les positions ou les coups dont partent les parties, chacun joué deux fois, couleurs
  inversées.
- **Syzygy** : les tables de finales qui donnent le résultat de chaque position avec peu de pièces (fichiers WDL,
  `.rtbw`).
- **Tables de Berger** : l'ordre des rondes d'un toutes rondes dans lequel chaque moteur joue une fois par ronde.
- **Tête de série** : une place dans l'ordre des participants : les moteurs testés d'un gauntlet, les favoris d'un KO.
- **TF** : time forfeits, les parties perdues au temps.
- **Threads** : les threads du processeur avec lesquels un moteur calcule.
- **TLCS, TLCV** : Tom's Live Chess Server et Viewer, un protocole pour suivre des parties de moteurs en direct.
- **UCI, WinBoard (CECP)** : les deux protocoles par lesquels les programmes parlent aux moteurs d'échecs.
- **WDL** : les chances de victoire, de nulle et de défaite qu'un moteur donne avec son score.

# Guide détaillé (français)

Mod expérimental pour **Dofus Unity 3.7.12.12**, version **1.18.1**.

Miniatures au choix, animations à la carte, surbrillance, curseurs dans le style
de Dofus, jauges à la verticale, aide à la capture d’âmes, trajets rapides
(passage secret des Roublards compris), ressources, archimonstres manquants et
infobulles enrichies sur les cartes, ouvertures Kralamoure dans le calendrier
et panneau natif de recherche manuelle DofusDB. Carnet personnel avec repères
sur la carte, planification de l’XP des métiers, affichage des performances et
statistiques par personnage.
Les détails et les limites figurent dans [LISEZ-MOI.txt](../package/LISEZ-MOI.txt).
Les nouveautés depuis la 1.13.0 sont vérifiées hors jeu ; leur rendu en jeu reste à confirmer.

## Mises à jour

Les versions sont disponibles dans les [Releases](https://github.com/Altansar69/dofus-enhanced/releases).
Extraire l’archive entière. Le nom historique `DofusMiniatures-3.7.11.11-rN`
conserve les réglages partagés ; la version du jeu ciblée figure dans `release.ini`.

Dans **Options → Dofus Enhanced → Chargement et mises à jour** :

- **Chargement automatique** charge le mod lors des prochains lancements.
- **Mises à jour GitHub** recherche, télécharge et vérifie une version compatible
  avant le chargement. Activé par défaut. Aucun compte GitHub requis.

Le lanceur conserve les anciennes versions et les réglages. En cas d’absence
de réseau, il utilise une version locale compatible. Il ne remplace pas un module
dans un jeu déjà ouvert. Il laisse intact un client sans version compatible.
Depuis la 1.9.2, si GitHub renvoie une liste de publications vide (service
dégradé), il consulte la dernière publication avant de conclure qu’aucune
version n’est compatible. Les publications portent leur numéro de version
(`v1.13.0`, `v1.14.0`…) ; les archives et les dossiers gardent la révision `rN`,
que la mise à jour vérifie toujours. Une installation antérieure à la 1.13.1 ne
lit que les anciennes étiquettes `rN` : elle se met à jour une fois à la main
(voir la FAQ du README).

L’assistant de démarrage peut reconnaître une future version du jeu et lancer
le chargeur livré avec son correctif. Cela ne rend pas les fonctions natives du mod
universelles : des changements dans Dofus peuvent nécessiter une adaptation et
une nouvelle publication.

Pour enregistrer manuellement l’assistant, depuis le dossier extrait :

```powershell
.\ModuleLoader.exe --enable-auto "C:\chemin\vers\Dofus.exe"
```

Pour vérifier les mises à jour sans charger quoi que ce soit dans le jeu :

```powershell
.\ModuleLoader.exe --check-updates "C:\chemin\vers\Dofus.exe"
```

Les diagnostics sont dans `autoload.log`, puis `module.log` après chargement.
L’onglet des options s’appelle **Dofus Enhanced**, en blocs par sujet :
Apparence, Animations, Surbrillance, Curseur, Jauges, Cartes, Carnet, Chasse au
trésor, Déplacements, Pierre d’âme (avec Metamob), Chat, Calendrier,
Performances, Statistiques, Chargement et mises à jour et À propos. La version
chargée (x.y.z)
figure dans le dernier bloc, **À propos**, avec l’auteur Altansar et un bouton
**Ouvrir GitHub** vers le dépôt.

## Chasse au trésor

Cliquer sur le bouton **Chasse au trésor**, en haut à droite, juste après
**Capture d’âme** (le premier des boutons du mod si **Capture d’âme** est
masqué).
Un second clic ou **⋯ → Masquer le module** ferme le panneau.
Le menu ⋯ des modules de la barre ne liste plus ce panneau : son bouton est le seul raccourci.
Son entrée **Réduire les titres des modules** propose en revanche les quatre
panneaux du mod (Pierre d’âme, Chasse au trésor · DofusDB, pour le distinguer
du module « Chasse au trésor » du jeu, Carnet personnel, Statistiques) juste
après les modules du jeu, sans icône comme eux, au-dessus de **Tout réduire** /
**Tout agrandir** ; le choix est conservé entre les lancements et **Tout
réduire** / **Tout agrandir** les suivent aussi.
**Options → Dofus Enhanced → Chasse au trésor → Bouton dans la barre du haut**
(coché par défaut) affiche ce bouton. Décoché, le bouton disparaît et le
panneau ne s’ouvre plus : recoche la case pour le retrouver. Un panneau déjà
ouvert reste ouvert et se ferme toujours avec sa croix ou
**⋯ → Masquer le module**.
La fenêtre n’utilise que des composants du jeu, comme le carnet : barre de titre
native avec sa croix de fermeture, poignées de redimensionnement, trois sections
natives **Point de départ** (X, Y, **Ma position**, onglets natifs Nord / Ouest /
Sud / Est), **Indice à trouver** (filtre, **Étape en cours**, **Rechercher les
indices**) et **Résultats** (lignes natives avec l’icône de chasse, **Chemin**
sur chaque ligne, compteur et pagination), puis une ligne d’état.
À l’ouverture, le départ reprend la carte où tu te trouves et, si une chasse est
en cours, sa direction et son indice sont lus dans le jeu (**Étape en cours**
relance cette lecture et la recherche ; **Ma position** ne reprend que les
coordonnées). Renseigner X, Y et une direction reste possible à la main, puis
Rechercher et filtrer par indice. Chaque résultat affiche sa position la plus
proche et sa distance. Un clic prépare le départ suivant ; **Chemin** lance le
trajet amélioré du mod vers l’indice (zaaps, bateaux, puis `/travel`), comme le
bouton TP du chat. Aucune validation de chasse n’est envoyée.
Les requêtes vont directement à DofusDB, sans identifiants ni cookies.
Un refus d’accès ou une erreur réseau reste visible dans le panneau.

## Carnet

Cliquer sur le bouton **Carnet personnel**, dans la même barre, après
**Capture d’âme** et **Chasse au trésor** quand ils sont affichés.
Un second clic, **⋯ → Masquer le module** ou le bouton de fermeture de la barre
de titre ferme le carnet (une modification non enregistrée demande un second
clic). Le menu ⋯ des modules ne le liste plus non plus, sauf dans **Réduire les
titres des modules**. Dans **Options → Dofus Enhanced → Carnet**, **Bouton dans
la barre du haut** (coché par défaut) affiche ce bouton. Décoché, le bouton
disparaît et le carnet ne s’ouvre plus, faute d’autre raccourci : recoche la
case pour le retrouver. Les entrées restent enregistrées, **Ajouter aux
positions** continue de les compléter, et un carnet déjà ouvert le reste
jusqu’à sa fermeture.
La fenêtre se redimensionne avec les poignées du jeu, comme la carte ou le chat,
et se raccourcit davantage depuis la 1.17.1 : aucune aide ne reste affichée
sous les listes. Son menu ⋯ liste les deux dispositions comme des choix à puce
du jeu, la disposition active cochée : **Version complète** (la grande fenêtre,
seule à permettre de créer et modifier les entrées) et **Version compacte**
(une fenêtre étroite avec les onglets, la liste des entrées, la pagination et,
sur la ligne de chaque entrée, ses petits boutons, sans formulaire). Le choix
est enregistré et appliqué aussitôt à la fenêtre ouverte, qui garde l’onglet et
l’entrée choisis et reste à sa place (ramenée dans l’écran si la version
complète, plus large, en dépasserait).
Le carnet n’utilise que des composants du jeu : onglets natifs **Positions**,
**Notes** et **Messages**, la liste des entrées sans en-tête ni compteur (entrée
courante surlignée ; les flèches de pagination n’apparaissent qu’à partir de
sept entrées) et une section **Détail** (titre, coordonnées ou texte, canal
d’un message, boutons natifs **Enregistrer**, **Supprimer** et **Ma position**).
Un onglet vide le dit dans sa liste, avec la façon de créer une entrée. La ligne
d’état, sous les listes, n’apparaît que quelques secondes après une action ou
un changement du fichier (« Copié dans le presse-papiers. »,
« Trajet demandé vers [x,y]. », un enregistrement, une suppression, une
erreur) et ne prend aucune place le reste du temps ; seul un carnet illisible y
reste signalé jusqu’à sa correction.
Le fichier du carnet est relu de lui-même quand il change (un autre client, une
position ajoutée depuis une carte, une correction à la main) : la liste suit
sans bouton à presser, et un brouillon en cours n’est jamais remplacé. Un
fichier illisible est signalé et conservé ; il est relu dès qu’il est corrigé.
Un brouillon modifié demande une confirmation avant d’être abandonné. Si la
question disparaît avant ta réponse, le clic suivant la repose.
Les onglets **Positions**, **Notes** et **Messages** permettent de créer, modifier
et supprimer les entrées. Une position peut reprendre la carte actuelle ou des
coordonnées saisies ; le monde se choisit dans une liste déroulante native
alimentée par les mondes du jeu. Le trajet se lance d’un clic sur le bouton zaap
de sa ligne. Un message s’envoie dans le chat avec le bouton d’envoi de sa
ligne, par la fonction d’envoi du jeu elle-même (celle de la touche Entrée), sur
le canal enregistré avec lui : actuel (celui du chat), Général, Équipe, Guilde,
Alliance, Groupe, Commerce ou Recrutement. Ce canal se choisit dans le
formulaire avec le bouton **Canal** et ne sert qu’une fois l’entrée
enregistrée ; l’infobulle du bouton d’envoi nomme le canal utilisé. Le bouton
suivant place le message dans le brouillon du chat sans l’envoyer. Ces boutons
utilisent le message tel qu’il est enregistré.

Dans l’onglet **Positions**, le petit bouton drapeau à droite de chaque nom place
un repère sur cette position : le drapeau du jeu, au nom de l’entrée
(« Carnet : … »), visible sur la grande carte et la mini-carte. Le bouton reste
enfoncé tant que le repère est affiché ; un second clic le retire. Le repère
suit la position quand elle est modifiée et disparaît avec elle quand elle est
supprimée ; il se retire aussi par le menu du drapeau sur la carte. Seul le
repère du carnet est retiré, jamais les drapeaux posés par le joueur. Le bouton
se masque dans **Options → Dofus Enhanced → Carnet**.

À droite de chaque entrée, de petits boutons du jeu agissent sur l’entrée de
leur ligne, dans les deux versions du carnet, sans changer l’entrée
sélectionnée ni les modifications en cours : pour une position, après le
drapeau, **copier** place ses coordonnées dans le presse-papiers (par exemple
`[-25,36]`) et **zaap** lance le trajet du mod vers elle (zaaps, bateaux puis
`/travel`, indisponible en combat) ; pour une note, **copier** place son texte
dans le presse-papiers ; pour un message, **envoyer**, **préparer dans le
chat** et **copier**. Ils remplacent les boutons **Voyager** et **Copier** du
formulaire des positions (avant la 1.16.0), **Copier** de celui des notes et
**Envoyer**, **Préparer dans le chat** et **Copier** de celui des messages
(avant la 1.17.1). **Bouton de copie à côté des entrées**, **Bouton de voyage
à côté des positions** et **Boutons d’envoi à côté des messages** (cochés par
défaut), dans les mêmes options : décoché, le bouton correspondant disparaît
aussitôt, même carnet ouvert ; sans le bouton de copie, le carnet ne propose
plus de copier ses entrées, et sans les boutons d’envoi, plus d’envoyer ni de
préparer un message dans le chat. La version complète du carnet est un peu plus
large (730 px) pour leur faire place.
Un clic droit sur une position de la liste ouvre le menu du jeu : **Voyager**
(le trajet du mod, comme le bouton zaap de la ligne) et **Afficher sur la
carte** ou **Retirer de la carte**.

Au clic droit sur une case de la grande carte ou de la mini-carte, l’entrée
**Ajouter aux positions**, placée après les entrées de téléportation, enregistre
cette case dans les positions du carnet : coordonnées, monde, carte du jeu et,
pour titre, le nom de sa zone. Le carnet n’a pas besoin d’être ouvert ; le chat
local le confirme (« Carnet : … »). Une case déjà enregistrée n’est pas ajoutée
deux fois. Cette entrée se retire dans les mêmes options ; **Voyager jusqu’à
cette position** et les entrées du Havre-Sac ne changent pas.

Le carnet est enregistré localement à côté des préférences partagées, hors des
dossiers de révision. Une modification concurrente de la même entrée est signalée
pour éviter d’écraser celle d’un autre client.

## XP des métiers

Le troisième onglet **XP** de la fenêtre **Métiers** reprend le métier sélectionné,
son niveau et son XP total. Les recettes viennent de
[l’outil DofusDB](https://dofusdb.fr/fr/tools/jobs-xp).
La simulation calcule le nombre de fabrications et les ingrédients nécessaires
pour atteindre le niveau cible, en recalculant le gain quand le niveau augmente.
Le panneau sépare l’objectif de niveau, la liste des recettes et les ingrédients
à préparer. La recette sélectionnée est signalée par un liseré vert. Le coefficient
d’XP et le bonus de serveur Épique se règlent manuellement ; garder 100 % sans bonus.
L’onglet s’appelle uniquement **XP**, s’aligne sur les onglets natifs et conserve
le même espace après Ressources qu’entre Recettes et Ressources. Un seul onglet
reste sélectionné quand on passe de Recettes ou Ressources à XP.
Chaque recette proposée et chaque ingrédient à préparer affiche son objet natif,
avec son image, son infobulle au survol et ses actions habituelles.
Ce calcul ne choisit pas automatiquement la recette la moins chère et ne fabrique rien.
Les requêtes sont anonymes et effectuées en arrière-plan ; une erreur reste visible.

## Apparence et animations

**Options → Dofus Enhanced → Apparence** : **En combat** et **Hors combat**
choisissent chacun Normal, Créatures ou Miniatures. Avec Miniatures, les cases
placées sous chaque choix disent qui est réduit : en combat **Moi**, **Joueurs
alliés**, **Invocations alliées**, **Mes invocations**, **Ennemis** et
**Invocations ennemies** ; hors combat **Moi**, **Membres du groupe**, **Autres
joueurs**, **Monstres** et **PNJ**. Une catégorie décochée garde sa taille
normale ; les autres éléments restent réduits. Tout est coché par défaut, comme
avant ces cases, sauf **PNJ** (depuis la 1.14.0) : la plupart des PNJ n’étaient
jamais réduits, et ceux qui l’étaient gardent désormais leur taille tant que la
case reste décochée. Les mêmes cases sont aussi dans le **clic droit sur la carte →
Affichage**, sous Normal / Créatures / Miniatures : celles du combat en combat,
celles du hors combat en dehors, comme les cases de **Surbrillance**.

**Options → Dofus Enhanced → Animations** (en combat uniquement) compte six
cases indépendantes :

- **Sans effets des sorts** (cochée par défaut) : le jeu fait comme si son
  option des effets de sorts était désactivée, sans la modifier ;
- **Lancers de sorts raccourcis** (cochée par défaut) : l’animation de lancer
  réduite du jeu, pour les personnages joueurs ;
- **Sans animations d’attaque et de sort** (expérimental) : attaques, lancers et
  coups reçus ne sont plus joués, les déplacements restent animés ;
- **Sans animations de mort** (expérimental) : un combattant vaincu disparaît
  sans animation, seulement quand les deux cases suivantes sont décochées ;
- **Déplacements instantanés** (expérimental) : marche, poussées, lancers de
  personnage porté et sauts arrivent aussitôt ;
- **Sans attentes visuelles** (expérimental) : le jeu n’attend plus certaines
  fins d’animation.

Les deux premières cases forment l’ancien mode Raccourcies ; Sans animations
des combattants y ajoutait les deux suivantes, et Instantané les six. Les
réglages d’une version antérieure sont convertis à l’identique, et le fichier
reste lisible par ces versions. Le clic droit sur la carte → **Animations**
propose les mêmes six cases. Les délais du serveur ne changent pas : toutes les
animations ne disparaissent pas.

## Surbrillance

**Options → Dofus Enhanced → Surbrillance** choisit le niveau : derrière
les modules d’affichage, derrière ces modules et les murs, ou tout le temps.
Sous le niveau, six cases indépendantes choisissent ce qui est entouré hors
combat : **Monstres** (cochée par défaut), **PNJ**, **Éléments interactifs**,
**Moi** (ton personnage), **Membres du groupe** et **Autres joueurs** (hors de
ton groupe), ces cinq-là décochées par défaut. Sans case cochée, rien n’est
entouré. Depuis la 1.16.0, **PNJ** et **Éléments interactifs** remplacent
« Inclure les PNJ » et « Inclure les éléments interactifs », dont le choix est
conservé.
Les mêmes cases sont disponibles dans **clic droit → Surbrillance**, sous les
quatre modes. Elles sont indépendantes et le menu reste ouvert après sélection.
Les éléments interactifs (ressources récoltables, portes, zaaps…) doivent être
visibles et proposer une action disponible dans le jeu.
Aucun contour n’est dessiné pendant un combat. Le contour est calculé en fin
d’image, après l’animation de zoom du jeu, et dessiné dans la même image : il
suit la caméra image par image pendant un zoom ou un déplacement de la vue, et
environ 30 fois par seconde quand la vue est immobile.
Les grandes fenêtres restent devant les contours et les clics restent natifs.
Jusqu’à 32 cibles sont entourées en même temps, toutes catégories confondues.
Sur une carte bondée, passent d’abord ton personnage, puis ton groupe, les
monstres, les PNJ, les éléments interactifs et enfin les autres joueurs.

## Curseur

**Options → Dofus Enhanced → Curseur → Curseur Dofus** (décoché par défaut)
remplace la souris de Windows, dans le jeu, par des curseurs dessinés par le
mod dans le style de Dofus. Le jeu décide quand le curseur change et le mod en
change le dessin, à deux exceptions près (le curseur de texte dans les champs
que le jeu ne signale pas, la main fermée pendant un glisser-déposer) :

- la **flèche de sélection** à la place de la flèche de Windows ;
- la **main** partout où le jeu affiche la sienne : boutons, onglets, liens du
  chat, bords de carte et cases de la carte ;
- le **curseur de texte** dans les champs de saisie, y compris ceux que le jeu
  ne signale pas (le chat, les recherches) ; un champ désactivé garde la
  flèche ;
- la **main ouverte** sur les barres de titre des fenêtres, **fermée** tant que
  le bouton de la souris reste enfoncé (fenêtre déplacée) et pendant le
  glisser-déposer d’un objet ;
- les **doubles flèches** sur les bords et les coins des fenêtres
  redimensionnables.

Les curseurs sont dessinés par le mod lui-même, sans fichier d’image, nets à
toutes les tailles de 32 à 128 pixels : une flèche sertie d’une gemme, un gant
qui pointe, s’ouvre ou se ferme, un curseur de texte en forme de I et des
doubles flèches. Chacun a un contour sombre, une ombre douce et un relief
éclairé d’en haut à gauche, pour rester lisible sur le parchemin et
l’interface sombre comme sur l’herbe. Le clic part de la pointe de la flèche,
du bout de l’index du gant qui pointe, et du centre des autres curseurs.

Les curseurs propres au jeu ne changent pas : épée et bulle sur les
personnages, outils de récolte et d’atelier, repères de combat, point
d’interrogation ; ils gardent leur croix ou leur sablier quand une action est
impossible ou pas encore disponible. Le mod a aussi une flèche et un gant avec
une pastille rouge « interdit » ou un sablier, mais ils ne s’affichent que
dans les rares cas où le jeu demande cette croix ou ce sablier sur la flèche
ou sur le gant. Si le jeu n’a pas l’image d’un de ses curseurs, la flèche du
mod la remplace plutôt que celle de Windows. En combat, l’icône du sort,
l’interdiction et les PA/PM que le jeu affiche à côté de la souris restent
les siens.

**Couleur du curseur** : « Défaut » (par défaut) reprend le thème choisi dans
les options du jeu, dont le nom s’affiche entre parenthèses (« Défaut
(Bonta) », par exemple), et le suit en une demi-seconde quand tu en changes ;
sans couleurs de thème lisibles, celles du thème Standard sont utilisées.
Viennent ensuite les onze thèmes du jeu, chacun proposé seul (Standard, Bonta,
Brakmar, Tribute, Gold and steel, Belladone, Unicorn, Emerald mine, Sufokia,
Pandala, Wabbit) : le curseur garde alors ses couleurs, quel que soit le thème
de l’interface. Avec un thème, le curseur prend sa couleur d’accent (jaune-vert
avec Standard), éclaircie si elle est trop sombre pour rester visible, sur un
contour tiré de son fond sombre, avec un ornement doré si elle est verte et une
émeraude sinon. Enfin les couleurs du mod : « Or » est doré, avec une émeraude
sur la flèche et sur le gant ; « Émeraude » est vert, avec un ornement doré ;
« Argent » est gris clair, avec une émeraude. Depuis la 1.18.1, l’ancien choix
« Couleur du thème du jeu » devient « Défaut » ; « Or », « Émeraude » et
« Argent » sont gardés.

**Taille du curseur** : « Comme la souris de Windows » (par défaut) reprend la
taille du pointeur de Windows et la mise à l’échelle de l’écran où se trouve le
jeu : 32 pixels à 100 %, 40 à 125 %, 48 à 150 %, 64 à 200 %, davantage si tu as
agrandi le pointeur dans les paramètres d’accessibilité de Windows (Pointeur de
la souris). « Grande » l’agrandit de moitié (48 pixels à 100 %) et
« Très grande » la double (64 pixels à 100 %), sans dépasser 128 pixels. À
toutes les tailles, ce sont de vrais curseurs de Windows, comme la souris : ils
suivent tes mouvements sans retard, quelle que soit la fluidité du jeu. Les
curseurs propres au jeu (épée, bulle, outils de récolte, repères de combat…)
gardent leur taille habituelle. Un changement d’écran ou d’échelle est pris en
compte en une seconde environ. Jusqu’à la 1.17.0, le jeu ramenait les curseurs
du mod à la taille du pointeur de Windows : « Grande » et « Très grande » ne
changeaient presque rien.

Cocher la case, ou changer de couleur ou de taille, prend effet en une fraction
de seconde : le mod dessine ses treize curseurs, un par image, pendant que le
curseur précédent reste affiché. Décochée, la souris de Windows et les curseurs
habituels du jeu reviennent aussitôt et les images du mod sont supprimées.

En cas de souci, `module.log` contient des lignes `CUSTOM_CURSOR_…` :
`CUSTOM_CURSOR_READY` (installation et parties disponibles ; `size=1` : le jeu
affiche les curseurs du mod à leur taille), `CUSTOM_CURSOR_ON` et
`CUSTOM_CURSOR_REDRAWN` (taille, échelle, taille du pointeur de Windows et
couleur, puis `mode=hardware` : toujours des curseurs de Windows, avec
`load=texture` s’ils sont affichés à leur taille ou `load=windows` s’ils
restent à la taille du pointeur de Windows), `CUSTOM_CURSOR_LOADED` (le jeu a
chargé un curseur du mod à sa taille, avec celle que Windows lui aurait
donnée, par exemple `size=64 windows=32`), `CUSTOM_CURSOR_THEME` (couleurs du
thème lues), `CUSTOM_CURSOR_SEEN` (les premiers curseurs demandés par le jeu et
celui affiché) et `CUSTOM_CURSOR_OFF`. `CUSTOM_CURSOR_PART_UNAVAILABLE size` :
ce jeu ne charge pas ses curseurs comme la version prise en charge ; les
curseurs du mod restent, mais à la taille du pointeur de Windows, quelle que
soit la « Taille du curseur ».
`CUSTOM_CURSOR_DISABLED` : le curseur du mod n’a pas pu s’installer ; la souris
de Windows et les curseurs habituels du jeu restent jusqu’au prochain lancement
du jeu, même si tu coches la case. `CUSTOM_CURSOR_HOOK_ERROR` ou
`CUSTOM_CURSOR_FAULTED` : après une erreur, le mod a rendu la main au curseur
du jeu (`CUSTOM_CURSOR_OFF faulted` si ses curseurs étaient affichés) ; décocher
puis recocher la case réessaie. Si le curseur paraît trop petit, choisis
« Grande » ou « Très grande ».

## Jauges

Chaque jauge du jeu a son propre menu ⋯ (les trois petits points, à sa
droite) : expérience du personnage, expérience de guilde, points d’honneur,
pods, points d’énergie et expérience de chaque métier. Le mod y ajoute la case
**Afficher à la verticale**, juste après « Masquer le module » (après
« Couleur de jauge » quand le jeu ne propose pas « Masquer le module ») et
avant « Gestion des jauges ». Un clic applique le choix aussitôt et ferme le
menu.

Cochée, la jauge passe à la verticale :

- c’est la **barre du jeu elle-même**, dans son mode vertical : elle se
  remplit **de bas en haut**, avec ses graduations et sa couleur (celle de
  « Couleur de jauge » comprise) ; son infobulle ne change pas ;
- de haut en bas : l’icône de la jauge, la barre, « NIV. » au-dessus du
  niveau (rien pour les pods, comme à l’horizontale) ou le badge Oméga d’un
  personnage Oméga, puis le bouton ⋯ ;
- elle fait **48 pixels de large** ; sa longueur est d’abord sa largeur à
  l’horizontale, entre 160 pixels et la hauteur de l’écran moins 16, et suit
  un changement de résolution ou d’échelle de l’interface ;
- elle **garde sa place** : son coin en haut à gauche ne bouge pas, sauf du
  nécessaire pour rester entièrement à l’écran (une jauge en bas de l’écran
  remonte d’autant) ; si le jeu l’avait décalée pour la garder entière à
  l’écran à l’horizontale (une jauge laissée à la verticale contre le bord
  droit, par exemple), elle revient là où tu l’avais laissée ;
- elle **se déplace comme avant** (glisser, aimantation aux autres fenêtres,
  Alt pour s’en passer, Maj pour déplacer le groupe aimanté) ;
- sa **longueur se change en tirant son bord haut ou son bord bas**, de 160
  pixels à la hauteur de l’écran moins 16 ; sa largeur ne change pas.
  Depuis la 1.18.1, sa longueur et sa place à la verticale sont gardées par le
  mod pour la prochaine fois, et jamais enregistrées dans la disposition du
  jeu, qui garde celles de la jauge horizontale.

Décochée, la jauge redevient **exactement** celle du jeu (ses réglages, ses
bords de redimensionnement), à sa place et à sa taille d’avant, gardée
entièrement à l’écran.

Le choix de chaque jauge est enregistré dans les réglages du mod, pas dans la
disposition du jeu, et vaut pour tous tes personnages. Une jauge masquée puis
réaffichée (« Masquer le module », « Gestion des jauges », le bouton du
panneau des métiers, le bouton d’expérience de la barre des modules) reste
verticale ; recréée par le jeu (connexion, changement de personnage), elle
redevient verticale dès qu’elle s’affiche, en moins d’un quart de seconde, à
la place où tu l’avais laissée. Quand le jeu replace ses fenêtres (combat,
disposition, « Réinitialiser » les positions), elle reste verticale et à
l’écran, même masquée ; masquée pendant un replacement, elle réapparaît à sa
place. Seule limite connue : après un passage à un écran moins haut
(résolution, échelle de l’interface), une jauge verticale plus longue que la
hauteur de l’écran peut dépasser en haut pendant au plus 0,2 seconde.

**Options → Dofus Enhanced → Jauges →** « Affichage vertical dans le menu … des
jauges » (cochée par défaut) : décochée, la case disparaît des menus et toutes
les jauges reviennent à l’horizontale ; le choix de chaque jauge est gardé et
revient quand tu la recoches.

## Ressources sur les cartes

Sur la **mini-carte**, une barre de six icônes rondes donne accès aux métiers.
Un clic ouvre uniquement les ressources du métier choisi ; cliquer à nouveau
sur son icône referme la liste. La bulle ouverte devient jaune, sans case à cocher.
Les bulles mesurent 48 px, avec une image de 40 px ; les noms sont disponibles au survol.
La barre conserve sa taille au zoom et au dézoom de la carte. Une main signale
les boutons cliquables ; leur survol ne sélectionne pas la case de carte située
derrière. La molette dans le sélecteur fait défiler les ressources sans zoomer
la carte.
Sur la grande carte (**M**), le carré **Ressources**, sous **Anomalies** dans la
colonne des modes, ouvre à droite la fenêtre des ressources, faite comme celles
du jeu (cadre, titre, bouton de fermeture, pied de page avec la case
d’affichage). Comme pour les modes du jeu, un seul est ouvert à la fois : choisir
**Anomalies** ou **Conquête** ferme la fenêtre des ressources, et l’ouvrir
referme le mode du jeu ou le mode **Ocre** ; la carte reste en mode monde, avec
ses repères. Chaque catégorie affiche la première ressource du métier : eau,
frêne, fer, ortie, blé ou goujon. Les mêmes images servent de boutons sur la
mini-carte et proviennent du catalogue des objets du jeu.

La sélection des ressources est commune aux deux cartes et sauvegardée entre
les lancements. **Afficher sur la mini-carte** et **Afficher sur la grande carte**
se règlent indépendamment. Les filtres restent accessibles sans ressource
sélectionnée. Décocher l’affichage d’une carte, dans sa fenêtre ou dans les
options, masque aussi son choix des ressources (le carré **Ressources** de la
grande carte, la barre de la mini-carte) jusqu’à réactivation
par l’option ou par l’autre carte. Le bouton de recentrage de la mini-carte, en
bas à gauche, reprend le composant et l’icône du bouton joueur de la grande
carte, avec son survol et son infobulle natifs ; son clic est lu directement sur
le composant du jeu et déclenche le recentrage natif, instantané, en conservant
le zoom. Le zoom à la molette et le recentrage sont ceux du jeu, sans animation
ajoutée : les glissements des versions 1.2.0 et 1.3.0 ont été retirés en 1.3.1.
L’option **Bouton de recentrage sur la mini-carte** permet de masquer le bouton.
Les filtres de transport restent à leur place. 86 ressources dont 16 minerais
sont disponibles, avec leur icône et leur niveau.

Chaque repère est un repère natif du jeu (le composant de ses propres indices
de carte : foreuse, indice de chasse), avec la taille des indices du jeu sur les
deux cartes et l’image de l’objet chargée par le jeu depuis son catalogue. Les
quantités en gras occupent une petite pastille dorée claire dans le coin
inférieur droit, proportionnelle au repère. Les ressources d’une même case
rejoignent le groupe de position du jeu de cette case, avec ses éléments
(foreuse, indice, attitude) : dès que la case empile plusieurs éléments, la
pastille de nombre native affiche le total des ressources de la case (3 cuivre
et 11 fer donnent 14, même avec une foreuse ou une attitude du jeu dans la
pile) ; au survol de la souris, le groupe
s’étale avec l’animation native de Dofus, foreuse comprise, chaque ressource
montrant alors sa propre quantité, puis se replie quand le curseur s’éloigne.
Les repères apparaissent par lots de 32 par image pour ne pas figer le jeu.
Un repère n’est plus masqué lorsque l’ajout d’une ressource provoque un chevauchement.
Un astérisque signale des points en intérieur ; le bandeau explicatif a été retiré. Les repères laissent passer
les clics de navigation. Le message demandant de zoomer a été retiré ; les
repères trop proches peuvent toujours être espacés en zoomant.
La barre se répartit sur plusieurs lignes si nécessaire et la liste dispose
de son propre défilement. Agrandir la mini-carte si elle est trop petite pour
contenir la barre et au moins une ligne de ressources.
Les cristaux pliable et liquide apparaissent dans leur monde de Pandala.
[Détails des données DofusDB](RESOURCE-DATA.md). La disponibilité après récolte
n’est pas connue. La 1.8.0 (r104) est vérifiée hors jeu ; son rendu en jeu reste à confirmer.

Dans le chat, un monstre connu comme absent de L’éternelle moisson conserve
seulement le bouton TP ; les indicateurs d’âme en sac et d’Ocre sont masqués.

## Infobulles des cartes et dimension

Au survol d’une case, la grande carte et la mini-carte affichent l’infobulle du
jeu avec ses coordonnées [x,y]. Le mod ne réécrit que ce texte : **Dimension
avec les coordonnées sur la grande carte** et **… sur la mini-carte** y ajoutent
la dimension de la carte affichée, par exemple [4,-19, Monde des Douze] ou
Incarnam. Chacune se règle à part, avec les choix de la dimension dans le chat :
**Oui, sauf dans le Monde des Douze** (par défaut : seules les autres
dimensions, Incarnam, Srambad…, l’affichent), **Oui** (partout) ou **Non**.
Avant la 1.16.0, c’étaient des cases : cochée, une case devient « Oui, sauf
dans le Monde des Douze » ; décochée, « Non ». **Nom de la zone au survol de la
mini-carte** affiche au-dessus le nom de la zone de la case (par exemple
Village d’Amakna). Depuis la 1.14.0, le fond noir de l’infobulle englobe toutes
ses lignes, même pour un long nom de zone. Le clic, le glisser, le zoom et le
double-clic des cartes ne changent pas, et leur clic droit propose aussi ces
réglages (voir « Réglages des cartes au clic droit ») ; quand il n’y a rien à
ajouter (« Non », ou une case du Monde des Douze avec « Oui, sauf dans le Monde
des Douze », sans nom de zone), l’infobulle reste celle du jeu.

**Barre des ressources sur la mini-carte** décochée retire de la mini-carte la
barre des six métiers et sa liste ; les repères des ressources choisies restent
affichés tant que **Ressources sur la mini-carte** est cochée, et le choix se
fait depuis la grande carte.

Dans le chat, **Options → Dofus Enhanced → Chat → Dimension avec les coordonnées
dans le chat** affiche les positions et les groupes de monstres partagés sous la
forme [x,y, dimension], avec les mêmes choix : **Oui, sauf dans le Monde des
Douze** (par défaut depuis la 1.16.0), **Oui** ou **Non**. Le lien reste celui
du jeu (clic, infobulle), le bouton TP ne change pas, et les coordonnées tapées
à la main restent telles quelles. À la mise à jour vers la 1.16.0, **Oui**,
l’ancien choix par défaut, devient **Oui, sauf dans le Monde des Douze** :
choisis **Oui** à nouveau pour voir la dimension partout.

## Mode « Ocre » de la carte

Avec **Options → Dofus Enhanced → Cartes → Archimonstres manquants sur la
grande carte** (coché par défaut), la colonne des modes de la grande carte (**M**)
gagne un carré **Ocre** sous **Conquête**, du même type que ceux du jeu. Un clic
active le mode Ocre et quitte Anomalies ou Conquête ; un second clic, la croix du
panneau, le choix d’un autre mode ou la fermeture de la carte le quitte,
comme pour les modes du jeu.

Toutes les zones qui abritent des archimonstres ou des boss de donjon de
L’éternelle moisson sont colorées. Un monstre manque quand il n’est pas validé
dans la quête, qu’il n’est pas dans ton sac et que Metamob (avec une clé
enregistrée) n’en indique aucun. La couleur suit le nombre de manquants dans la
zone : 1, 2, 3, 4 à 5, puis 6 et plus, du jaune au pourpre ; une zone où il ne
te manque rien est bleue (« Complet »), un peu plus légère. Tant que
l’avancement de la quête n’est pas connu, rien n’est coloré et le panneau dit
d’ouvrir le journal de quêtes.

Le panneau **Ocre** reprend celui des anomalies : **Rechercher** (une zone, un
territoire ou un monstre, sans tenir compte des accents), **Tous les
territoires**, **Niveau** de … à … (le niveau de la zone) et **Tous /
Archimonstres / Boss de donjon** (uniquement les 286 archimonstres et 51 boss de
la quête). La liste donne d’abord les zones où il te manque des monstres, puis
les zones complètes ; les colonnes Territoire, Niveau et Manquants se trient. Un
clic sur une ligne centre la carte sur la zone (sur son entrée pour un donjon,
en changeant de monde si besoin) et ouvre sous la ligne **Il te manque** et **Tu
as** : chaque monstre avec son icône d’archimonstre ou de boss et, pour ceux que
tu as, pourquoi (validé dans l’Ocre, dans le sac, sur Metamob avec sa quantité
quand elle dépasse 1).
Un second clic sur la ligne la referme. Le bas du panneau donne la légende des
couleurs, « Complet » compris, et l’état des lectures (Ocre, sac, Metamob).

**Archimonstres manquants sur la mini-carte** (décoché par défaut) colore les
mêmes zones sur la mini-carte, avec les mêmes couleurs, selon le choix Tous /
Archimonstres / Boss de donjon du panneau (sans sa recherche ni ses filtres de
territoire et de niveau). Les deux options décochées : ni carré, ni zones, ni
lecture. La 1.13.0 corrige la coloration, invisible en 1.12.0 : le jeu crée ces
zones transparentes et le mod ne les rendait pas visibles. La 1.13.0 est vérifiée
hors jeu ; son rendu en jeu reste à confirmer.

## Réglages des cartes au clic droit

Au clic droit sur la mini-carte, le menu du jeu se termine par un séparateur
et les réglages du mod pour la mini-carte, les mêmes que dans **Options →
Dofus Enhanced → Cartes** : les cases **Ressources**, **Barre des
ressources**, **Bouton de recentrage**, **Nom de la zone au survol** et
**Archimonstres manquants**, puis le sous-menu **Dimension avec les
coordonnées** (**Non**, **Oui, sauf dans le Monde des Douze**, **Oui**). Au
clic droit sur la grande carte, ceux de la grande carte : **Ressources**,
**Archimonstres manquants** et **Dimension avec les coordonnées**. Ce sont des
cases et des choix comme ceux du jeu, chaque case et chaque choix avec son aide
au survol ; ils viennent après les entrées du jeu et celles du mod (**Ajouter
aux positions**, zaaps du Havre-Sac), hors combat. Un clic s’applique et
s’enregistre aussitôt, sans fermer le menu : les repères, la barre, le bouton,
l’infobulle et les zones suivent, et les options affichent le nouveau choix.
**Réglages des cartes au clic droit** (coché par défaut, à la fin de
**Cartes**) : décoché, ces réglages ne sont plus ajoutés aux menus des cartes.

## Trajets et chat

Le bouton TP du chat, le double-clic sur la grande carte ou la mini-carte et
l’entrée « Voyager jusqu’à cette position » de leur clic droit lancent le trajet
du mod : zaaps, directs ou par le Havre-Sac, puis l’autopilote du jeu. Ctrl +
double-clic, le menu d’un point de repère et `/travel` gardent l’autopilote du jeu.
**Options → Dofus Enhanced → Déplacements → Remplacer l’autopilote du jeu**,
décoché, rend le double-clic et cette entrée à l’autopilote du jeu.
Déjà sur la carte que le jeu vise pour ces coordonnées (hors Havre-Sac) : pas
de trajet, « TP : tu es déjà sur [x,y]. ». La carte actuelle compte toujours
comme départ, même coupée en zones ; un zaap sur une carte coupée en zones
compte une étape de plus. La destination reste la carte que le jeu choisit
pour ces coordonnées. La recherche du départ est quasi instantanée : le graphe
de déplacement du jeu est copié une fois (gardé tant que le jeu garde le même), et seules les
conditions liées au personnage sont redemandées au jeu à chaque recherche.

**Déplacements → Passage secret et temple des Roublards** (coché par défaut).
Bad Aboum, dans la crypte sous la vieille chapelle de la Cité d’Astrub [5,-15],
conduit au temple Roublard [10,16] (sous le cimetière d’Amakna) et au village
d’Amakna [3,3], et en revient. Aucune classe n’est demandée, mais ton personnage
doit avoir fait ce qui lui ouvre ces trajets : le jeu en décide et le mod ne
peut pas le savoir à l’avance. Quand ce passage raccourcit le trajet, le chat
l’annonce (« TP : le trajet passe par le passage secret des Roublards, d’Astrub
au temple Roublard. »), l’autopilote du jeu conduit ton personnage jusqu’à Bad
Aboum (par les égouts d’Astrub pour la crypte), puis le mod lui parle, clique la
destination comme toi, attend la téléportation et calcule la suite du trajet
depuis l’arrivée. Il ne choisit jamais une réponse payante, n’agit pas en combat
et s’arrête comme tout trajet si tu interromps l’autopilote, quittes la carte ou
lances un autre trajet.

Le mod ne sait pas d’avance ce que Bad Aboum proposera : il le lui demande
pendant le trajet. S’il est introuvable, ne répond pas ou ne propose pas ce
trajet à ce personnage, le chat l’explique (« TP : … ; le trajet continue sans
le passage secret. ») et le trajet continue sans lui ; un refus est retenu pour
ce personnage et ce sens, et un trajet qui a échoué n’est pas retenté pendant
30 minutes. Toute action de ta part (un autre dialogue, le Havre-Sac,
l’autopilote du jeu, l’option décochée) arrête le trajet. Le mod apprend en le
voyant où Bad Aboum se tient côté temple (dès un premier passage dans ce sens,
ou quand tu passes près de lui) et où arrive chaque trajet, y compris ceux que
tu fais toi-même ; il le garde, par personnage, dans
`DofusMiniatures-roublard-passage.json`, à côté des préférences partagées. Le
trajet peut aussi traverser le temple ; sa statue reste réservée aux Roublards
par le jeu. Décoché, le mod n’emprunte ni le passage ni le temple ; l’autopilote
du jeu, qui finit le trajet, garde ses propres chemins.

**Options → Dofus Enhanced → Chat** affiche ou masque séparément, à côté des
positions et des archimonstres ou boss de L’éternelle moisson partagés dans le
chat, le bouton TP, « Âme en sac » et l’avancement Ocre. Tout est affiché par
défaut ; sans TP, les coordonnées écrites [x,y] restent du texte. Avec une clé
Metamob enregistrée, **Metamob dans le chat** ajoute après l’avancement Ocre la
quantité de ce monstre dans ta quête Metamob : « Metamob : 2 », en rouge à 0,
« Metamob : ? » tant que la quête n’est pas lue ou si sa dernière lecture a
échoué (l’infobulle dit quoi corriger si aucune quête Ocre ne correspond). La
quête est lue en une fois (2 ou 3 requêtes), relue au plus toutes les 10 minutes
tant que le chat montre un archimonstre ou boss de L’éternelle moisson (après un
échec : nouvel essai 30 s plus tard, puis à intervalle doublé jusqu’à 15 minutes),
et mise à jour aussitôt après une capture synchronisée ; aucune requête par
monstre, aucun message en cas d’échec.

## Pierre d’âme

**Options → Dofus Enhanced → Pierre d’âme** règle l’alerte pierre d’âme et
l’équipement automatique de la bonne pierre, puis la synchro Metamob décrite
ci-dessous. Le bouton **Capture d’âme**, en haut à droite, avant les autres
boutons du mod, active ou désactive l’alerte comme la case **Alerte pierre
d’âme** et reste enfoncé tant qu’elle est active. Depuis la 1.16.1, **Bouton
dans la barre du haut** (coché par défaut) l’affiche. Décoché, le bouton
disparaît sans changer l’alerte, qui ne se réactive alors qu’avec la case
**Alerte pierre d’âme** ; l’aide de **⋯ → Masquer le module** du panneau de
capture le rappelle. Les autres boutons du mod restent à leur place, juste
avant Menu, et l’aide de **Masquer le module** de leurs panneaux situe alors
leur bouton en haut à droite de l’écran. Sans aucun bouton coché (Capture
d’âme, Chasse au trésor, Carnet, Statistiques, Performances), le mod n’ajoute
plus rien à la barre.

## Metamob

Dans **Options → Dofus Enhanced → Pierre d’âme**, sous l’alerte, l’équipement
automatique et le bouton de la barre du haut, deux lignes synchronisent les
captures avec ta quête Ocre sur [metamob.fr](https://www.metamob.fr) :

1. Sur Metamob, **Paramètres → Clé API → Générer une clé**, puis copie-la.
2. Colle-la dans le champ masqué **Clé API Metamob** : le bouton **Enregistrer**
   apparaît dès le premier caractère. Le mod vérifie la clé auprès de Metamob et
   indique dans le chat le compte reconnu.
3. Coche **Synchroniser les captures avec Metamob**.

Une fois la clé enregistrée, la ligne n’a plus de champ modifiable : des étoiles
fixes (jamais la clé ni sa longueur), un bouton d’état et une petite corbeille.
Le bouton d’état dit ce que le mod sait de la clé, et un clic l’explique dans le
chat :

- **Clé valide** : Metamob l’a acceptée pendant cette session (à l’enregistrement,
  lors d’une synchronisation ou de la vérification ci-dessous) ;
- **Vérification…** : une clé enregistrée lors d’une session précédente ou par un
  autre client ; le mod la vérifie une fois, sans message dans le chat, quand la
  ligne s’affiche ;
- **Clé non vérifiée** : Metamob n’a pas pu la confirmer (injoignable) ; un clic
  relance la vérification, après une courte pause ;
- **Clé refusée** (révoquée ou régénérée sur Metamob) ou **Clé illisible**
  (chiffrée pour un autre compte Windows, ou abîmée) : supprime-la avec la
  corbeille, puis enregistre une clé valide.

La corbeille reste toujours disponible et demande « Voulez-vous supprimer la clé
Metamob ? » : **Oui** la supprime de ce PC et rend le champ vide, **Non** (ou dix
secondes sans réponse) la garde. Pour changer de clé, supprime l’ancienne puis
colle la nouvelle.

Après chaque combat où ton personnage obtient l’âme d’un archimonstre ou d’un
boss, le mod ajoute 1 à ce monstre dans ta quête Ocre Dofus Unity, sans rien
modifier d’autre, et l’écrit dans le chat (« Metamob : +1 « Tofumanchou
l’Empereur » (de 2 à 3). »). Il lit le rapport de fin de combat du jeu : seul le
butin du personnage de ce client compte, donc avec un client par personnage
chaque capture est comptée une fois. Les achats, échanges, la banque et le
courrier ne sont jamais comptés. Metamob n’accepte que des quantités absolues :
le mod relit la quantité juste avant d’écrire, et un envoi interrompu est
vérifié avant d’être renvoyé, pour ne jamais compter deux fois. Avec plusieurs
quêtes Ocre sur le compte, indique le nom du personnage dans « Personnage à
contacter » de la bonne quête.

La case vaut pour tous tes clients ouverts : cochée ou décochée dans l’un, elle
suit dans les autres en deux secondes. Décocher abandonne aussi les captures en
attente, sans rien envoyer. Le jeu doit être en français, en anglais ou en
espagnol, les langues des noms Metamob. Une âme de salle de donjon (Tynrils,
Gelées, Rats…) compte pour le boss dont c’est l’âme ; les autres boss de la salle
se mettent à jour à la main sur Metamob.

Les clients ouverts sur ce PC écrivent un même monstre l’un après l’autre. Avec
la même clé sur un autre PC (ou une autre session Windows), rien ne les
coordonne : deux captures du même monstre faites au même moment des deux côtés
peuvent n’en compter qu’une. Ne coche la synchro que sur un seul PC, et
redémarre les clients encore ouverts avec une ancienne version avant de la cocher.

La clé est personnelle (CGU de Metamob) : aucune clé n’est incluse dans le mod.
Elle est chiffrée pour ton compte Windows dans `DofusMiniatures-metamob.key`, à
côté des réglages, et n’est jamais affichée, journalisée ni publiée. Supprimée
avec la corbeille, elle reste valable sur Metamob : régénère-la sur le site pour
la révoquer. Les captures faites pendant que Metamob est injoignable sont
renvoyées automatiquement pendant la session.

## Kralamoures dans le calendrier

**Options → Dofus Enhanced → Calendrier → Ouvertures Kralamoure dans le
calendrier** (coché par défaut) n’agit qu’avec une clé Metamob enregistrée. Dans
le calendrier du jeu, « Mes calendriers » gagne une entrée « Kralamoure », en
tête de liste, avec son icône (une vague et un monstre) et sa couleur violette.
Sa case est cette option : la cocher ou la décocher dans le calendrier change
l’option, et inversement.

Cochée, une ligne Kralamoure s’ajoute sous les jours de la semaine, comme celles
des autres événements : la bannière à la couleur de Kralamoure, puis une carte
par ouverture planifiée sur Metamob pour le serveur de ta quête Ocre, sur son
jour, avec « Début : 21h00 » (heure de ce PC, changements d’heure compris) et le
nombre de personnages inscrits. Un jour à plusieurs ouvertures ajoute des
lignes, comme le jeu pour des événements simultanés ; au-delà de trois, la
dernière carte indique « +N ouvertures » et ouvre la liste des ouvertures sur
metamob.fr. Les ouvertures passées sont grisées.
Le survol d’une carte donne la date, l’organisateur, les inscrits et la
description ; un clic ouvre l’événement sur metamob.fr. Les flèches de semaine
sont suivies ; les semaines passées ne sont pas lues. Sans ouverture à montrer,
la ligne dit pourquoi (lecture en cours, aucune ouverture cette semaine, Metamob
injoignable…).

Les ouvertures ne sont lues que calendrier ouvert et Kralamoure coché : à
l’ouverture, puis au plus toutes les 10 minutes, jamais avant une capture en
attente d’envoi. Après un échec, nouvel essai 30 s plus tard, puis à intervalle
doublé jusqu’à 15 minutes. Le serveur est celui de ta quête Ocre Dofus Unity sur
Metamob (avec plusieurs quêtes, celle dont « Personnage à contacter » indique ce
personnage). Sans clé Metamob utilisable, ni entrée ni ligne. Les calendriers du
jeu, leurs cases et leurs couleurs ne sont pas modifiés.

## Performances

**Options → Dofus Enhanced → Performances** affiche un petit cadre sombre et
semi-transparent dès qu’une de ses trois cases de mesure est cochée, avec
seulement les mesures cochées ; seul ce qui est affiché est mesuré. Les trois
cases sont décochées par défaut :

- **Afficher les FPS et la mémoire** ajoute trois lignes, mises à jour deux fois
  par seconde. La première donne les images par seconde (FPS) et la durée d’une
  image : la moyenne sur la dernière seconde et, entre parenthèses, la plus
  longue de cette seconde (un à-coup y reste visible une seconde). La deuxième
  donne la mémoire vive occupée par le jeu (plage de travail) et sa mémoire
  privée. La troisième donne le tas géré, la mémoire des objets du jeu gérée par
  son ramasse-miettes (utilisée / réservée), et la part du processeur utilisée
  par le jeu sur l’ensemble des cœurs (100 % : tous les cœurs occupés).
- **Charge du GPU** : la part de la carte graphique due au jeu, celle de son
  moteur le plus chargé, comme la colonne GPU du Gestionnaire des tâches.
- **Activité du disque** : le temps d’activité et le débit du disque où le jeu
  est installé, tous programmes confondus.

Le GPU et le disque sont mesurés une fois par seconde, par exemple
« GPU 23 % · Disque C: 4 % (1,2 Mo/s) » ; chacune de ces deux cases
suffit à faire apparaître le cadre, même sans les FPS et la mémoire. Une mesure
que Windows ne fournit pas n’apparaît simplement pas. Sans les FPS, le cadre
affiche « Mesure en cours… » jusqu’à la première mesure (environ une seconde)
et « Mesure indisponible » si Windows ne fournit aucune des mesures cochées.

Tant que tu ne l’as pas déplacé, le cadre est en haut à gauche, ou dans le coin
choisi avant la 1.16.0 (en haut à droite, il se place sous les boutons du menu
du jeu). Avec **Déplacer et redimensionner l’affichage à la souris** (coché par
défaut), il se fait glisser au clic gauche et se pose où tu veux. Quand la
souris survole le cadre, une petite poignée de six points apparaît dans son
coin en bas à droite, le seul sans arrondi ; elle reste visible pendant que tu
le déplaces ou le redimensionnes et disparaît quand la souris s’en va. Le cadre
se redimensionne comme une fenêtre, par ses quatre bords et ses quatre coins :
un bord gauche ou droit règle sa largeur, un bord haut ou bas sa hauteur, un
coin les deux à la fois, et le côté opposé reste en place. Sa largeur et sa
hauteur vont chacune de la moitié au triple de leur taille normale ; le bord
ou le coin tiré suit la souris, et le cadre ne sort jamais de l’écran. Les
mesures se rangent selon la place : plus le cadre est large, plus elles se
suivent sur une même ligne, séparées par « · » ; plus il est étroit ou haut,
plus elles passent sur les lignes du dessous. Le texte garde ses proportions :
il prend la plus grande taille qui tient dans le cadre, sans jamais être
coupé ; ses marges, ses coins arrondis et la poignée suivent la même taille.
Au survol, le jeu affiche le curseur correspondant, comme sur ses propres
fenêtres : une double flèche sur les bords et les coins, dans leur sens, et le
curseur de déplacement ailleurs (les curseurs du mod quand **Curseur Dofus**
est coché) ; le curseur habituel revient dès que la
souris quitte le cadre. Le coin se prend sur au moins 18 pixels de l’écran
(davantage quand le texte est agrandi) et chaque bord sur au moins 6, sauf sur
un tout petit cadre (une seule ligne réduite, par exemple), où ils
rétrécissent ; le reste du cadre sert toujours à le déplacer. Sa place, sa
largeur et sa hauteur sont gardées, même après un redémarrage ou un changement
de résolution ; un simple clic ne change rien. Les clics sur le cadre ne vont
alors pas au jeu. Décochée, le cadre reste verrouillé à sa place et à sa
taille, sans poignée ni curseur particulier, et laisse passer les clics vers
le jeu. Le cadre suit l’échelle de l’interface et ne capte jamais le clavier ;
il passe au-dessus du HUD et des fenêtres du jeu, mais les menus, fenêtres de
confirmation, infobulles et écrans de chargement restent au-dessus de lui, et
il disparaît avec l’interface quand le jeu la masque. Les trois cases de mesure
décochées, il disparaît aussitôt et plus rien n’est mesuré.

**Bouton dans la barre du haut** (coché par défaut) ajoute en haut à droite,
après les autres boutons du mod, un bouton **Performances**, enfoncé tant que le
cadre est affiché. Un clic masque le cadre sans décocher ses mesures, un autre
le réaffiche ; si aucune mesure n’est cochée, le clic coche
**Afficher les FPS et la mémoire** pour montrer quelque chose. Masqué avec ce
bouton, le cadre ne mesure plus rien et le reste après un redémarrage, jusqu’à
un nouveau clic ou une mesure cochée dans les options. Décoché, le bouton
disparaît et seules les cases de mesure commandent le cadre, qui réapparaît
s’il était masqué. Le cadre garde son style discret : ce n’est pas une fenêtre
du jeu.

À la mise à jour vers la 1.16.0, si **Afficher les FPS et la mémoire** était
décochée, **Charge du GPU** et **Activité du disque** sont décochées aussi
(avant, elles ne faisaient rien sans elle) : coche-les à nouveau pour les
afficher seules. À la mise à jour vers la 1.17.1, la taille du cadre choisie
avant devient sa largeur et sa hauteur ; sa place est gardée.

## Statistiques

**Options → Dofus Enhanced → Statistiques** compte, pour chacun de tes
personnages, ce que le jeu ne garde pas, à partir de la 1.15.0 et seulement
pendant que le mod est chargé. **Compter mes statistiques** (coché par défaut)
les active ; décoché, plus rien n’est compté et ce qui l’a été reste affiché.

Depuis la 1.16.0, elles s’affichent dans leur propre fenêtre **Statistiques**,
faite comme celles du jeu, et non plus dans les options. Elle s’ouvre avec son
bouton en haut à droite, à côté de **Capture d’âme**, **Chasse au trésor** et
**Carnet personnel**, ou avec **Voir mes statistiques** → **Ouvrir** dans les
options. **Bouton dans la barre du haut** (coché par défaut) affiche ce bouton ;
décoché, la fenêtre ne s’ouvre plus que depuis les options. Comme le carnet,
elle se déplace, se redimensionne avec les poignées du jeu et se ferme avec sa
croix, un second clic sur son bouton ou **⋯ → Masquer le module** ;
**Réduire les titres des modules** réduit aussi son titre.

En haut, les onglets **Ce personnage** (le personnage connecté) et **Tous mes
personnages** (la somme de tous tes personnages comptés sur ce PC ; les records
gardent la plus grande valeur) : l’onglet choisi reste quand la fenêtre se
ferme, et revient à **Ce personnage** à chaque lancement du jeu. Une ligne dit
ensuite quel personnage est affiché, ou combien, et depuis quel jour. Les
chiffres se mettent à jour chaque seconde tant que la fenêtre est ouverte ;
fermée, rien n’est lu. Une ligne sans rien à montrer reste cachée (sorts n° 2 à
5 pas encore lancés, part des éléments avant les premiers dégâts, durée moyenne
d’un combat avant le premier combat terminé). Dessous, sept sections, sur trois
colonnes ou deux quand la fenêtre est étroite, avec une ligne par chiffre (son
nom à gauche, sa valeur à droite, ce qu’elle compte au survol) :

- Temps et déplacements : temps de jeu, plus longue session, cases parcourues
  hors combat et en combat, cartes parcourues, changements de dimension et la
  dimension la plus visitée.
- Progression : expérience et niveaux gagnés.
- Kamas : gagnés et dépensés, sans les dépôts et retraits à la banque ou dans un
  coffre ni les échanges avec tes autres personnages joués avec le mod sur ce
  PC ; les ventes à l’hôtel des ventes, créditées à la banque, ne sont pas
  comptées.
- Récolte et artisanat : récoltes (une par ressource récoltée, quelle que soit la
  quantité), objets fabriqués, quêtes terminées et succès obtenus.
- Combats : combats JcM, JcJ et Kolizéum avec leurs victoires, temps en combat,
  tours, dégâts infligés et subis, soins, plus gros coup, monstres vaincus,
  morts, âmes capturées, donjons terminés et le plus fait, avec son nom dans le
  jeu (la 1.15.0 n’en affichait que le numéro). Un combat regardé en spectateur
  ne compte pas ; un combat déjà commencé au chargement du mod ne compte pas
  parmi les combats, mais ses sorts lancés, ses coups et les morts du personnage
  le sont. La part des éléments et le plus gros coup ne comptent que les coups
  du personnage, pas ceux de ses invocations.
- Sorts et éléments : sorts lancés, les cinq sorts les plus lancés avec leur nom
  dans le jeu, et la part de chaque élément dans les dégâts infligés. Seuls
  comptent les sorts de ta classe : ni les coups d’arme, ni les effets que tes
  équipements lancent en ton nom, comme « Pourpre Profond » du Dofus Pourpre
  ou « Vert Émeraude » du Dofus Émeraude ; la 1.15.0 et la 1.16.0 comptaient
  ces effets comme des sorts.
- Téléportations : zaaps, zaapis, prismes, potions de rappel, entrées dans un
  Havre-Sac, trajets du mod et téléportations par une anomalie.

Tout reste sur ce PC, dans `DofusMiniatures-stats.json`, à côté des préférences
partagées ; plusieurs jeux ouverts (un par personnage) y écrivent sans
s’effacer. Il est écrit au plus toutes les 15 secondes et dès que le comptage
s’arrête (déconnexion, choix du personnage) : fermer le jeu peut perdre les
dernières secondes. **Réinitialiser**, en bas de la fenêtre, demande une
confirmation (« Confirmer », à cliquer entre une et dix secondes après) et
n’efface que l’onglet affiché : ce personnage, ou tous tes personnages. Changer
d’onglet ou fermer la fenêtre annule la question. Le bouton de réinitialisation
de la section des options ne remet que ses deux cases et n’efface aucune
statistique.

Depuis la 1.16.1, les sorts y ont leurs propres clés (`classSpells` et
`classSpellCasts`). Les sorts qu’y ont mis la 1.15.0 et la 1.16.0, ou qu’un jeu
encore dans une de ces versions y ajoute, sont convertis une seule fois, en ne
gardant que les sorts de classe, par le premier jeu à jour qui compte les
statistiques ou dont la fenêtre **Statistiques** est ouverte. Un jeu encore
dans une de ces versions n’affiche plus ensuite les sorts déjà convertis,
jusqu’à ce que tu le relances.

## Compilation et publication

Windows x64, outils C++ Visual Studio, CMake, Ninja, Python 3 et Windows PowerShell.

```powershell
git clone --recurse-submodules https://github.com/Altansar69/dofus-enhanced.git
cd dofus-enhanced
.\build.ps1
ctest --test-dir build --output-on-failure --output-junit test-results.xml
python tools/package-release.py --output "C:\chemin\vers\releases"
```

Le code est rangé par domaine : `src/<domaine>/` pour le mod (core, loader, ui,
appearance, animations, glow, souls, metamob, maps, travel, treasure, notebook,
jobs, social, stats), `tests/<domaine>/` pour les tests hors jeu (un programme par
fonction, déclaré avec `dofus_test()` dans `CMakeLists.txt`), `package/` pour les
fichiers livrés tels quels dans l’archive et `tools/` pour l’empaquetage et la
publication.

Après incrément de révision, validation, commit et push sur `main`, publier
avec `tools/publish-release.py --package <rN-package.json> --notes <notes.md>`.
Cet outil utilise la connexion Git Credential Manager du propriétaire, vérifie
l’empreinte calculée par GitHub, puis publie la release. Il ne remplace jamais
une version déjà publiée.

L’archive distribuée contient uniquement les fichiers nécessaires à l’exécution,
les manifestes et la documentation : aucun fichier Dofus, réglage personnel,
journal ou identifiant. Les téléchargements utilisent HTTPS et le SHA-256 fourni
par GitHub ; le module est sélectionné par les empreintes exactes du binaire et
des métadonnées du jeu.

## Dépendances

MinHook est un sous-module, sous sa licence d’origine (BSD à deux clauses, reproduite
à la fin de `package/LISEZ-MOI.txt`). Le SDK WebView2 inclut
sa licence dans `third_party/webview2`. Le compagnon WebView2 reste dans les archives pour la compatibilité des anciens
chargeurs, mais le panneau natif r95 ne le lance pas et ne dépend pas de son moteur.
Le lecteur JSON utilise nlohmann/json 3.12.0 (MIT), licence incluse dans
`third_party/nlohmann` et `package/LISEZ-MOI.txt`.

La compilation et les tests hors jeu ne remplacent pas la vérification de
l’initialisation et du rendu dans Dofus.

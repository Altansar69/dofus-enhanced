# Dofus Miniatures

Mod expérimental pour **Dofus Unity 3.7.12.12**, version **1.9.2** (révision r108).

Miniatures, options d’animations, surbrillance, aide à la capture d’âmes,
raccourcis de trajet, ressources sur la carte et panneau natif de recherche manuelle DofusDB.
Carnet personnel et planification de l’XP des métiers.
Les détails et les limites figurent dans [LISEZ-MOI.txt](LISEZ-MOI.txt).

## Mises à jour

Les versions sont disponibles dans les [Releases](https://github.com/Altansar69/dofus-miniatures/releases).
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
version n’est compatible.

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
L’onglet des options s’appelle **Dofus Enhanced**, en blocs par sujet : Apparence et animations,
Surbrillance, Cartes, Déplacements, Pierre d’âme (avec Metamob), Chargement et mises à jour
et À propos. La version chargée (x.y.z et
révision) figure dans le dernier bloc, **À propos**, avec l’auteur Altansar
et un bouton **Ouvrir GitHub** vers le dépôt. Les publications gardent leur
étiquette `rN` pour le vérificateur de mises à jour.

## Chasse au trésor

Cliquer sur le bouton **Chasse au trésor**, à côté de **Capture d’âme**, en haut à droite.
Un second clic ou **⋯ → Masquer le module** ferme le panneau.
Le menu ⋯ des modules de la barre ne liste plus ce panneau : son bouton est le seul raccourci.
Son entrée **Réduire les titres des modules** propose en revanche les trois
panneaux du mod (Pierre d’âme, Chasse au trésor, Carnet) à la suite des modules
du jeu ; le choix est conservé entre les lancements et **Tout réduire** /
**Tout agrandir** les suivent.
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

Cliquer sur le bouton **Carnet personnel**, dans la même barre, à côté de **Capture d’âme**.
Un second clic, **⋯ → Masquer le module** ou le bouton de fermeture de la barre
de titre ferme le carnet (une modification non enregistrée demande un second
clic). Le menu ⋯ des modules ne le liste plus non plus, sauf dans **Réduire les
titres des modules**.
La fenêtre se redimensionne avec les poignées du jeu, comme la carte ou le chat.
Son menu ⋯ liste les deux dispositions comme des choix à puce du jeu, la
disposition active cochée : **Version complète** (la grande fenêtre, seule à
permettre de créer et modifier les entrées) et **Version compacte** (une
fenêtre étroite avec les onglets, la liste des entrées, la pagination et les
actions Voyager, Envoyer, Préparer dans le chat, Copier et canal des messages
sous la liste, sans formulaire). Le choix est enregistré et appliqué aussitôt
à la fenêtre ouverte, qui garde l’onglet et l’entrée choisis et reste à sa
place (ramenée dans l’écran si la version complète, plus large, en dépasserait).
Le carnet n’utilise que des composants du jeu : onglets natifs **Positions**,
**Notes** et **Messages**, la liste des entrées sans en-tête (entrée courante
surlignée, pagination et compteur) et une section **Détail**
(titre, coordonnées ou texte, boutons natifs **Enregistrer**, **Supprimer**,
**Ma position**, puis **Voyager**, **Envoyer**, **Préparer dans le chat** et
**Copier** selon l’onglet), et une ligne d’état avec **Actualiser**.
Un brouillon modifié demande une confirmation avant d’être abandonné.
Les onglets **Positions**, **Notes** et **Messages** permettent de créer, modifier
et supprimer les entrées. Une position peut reprendre la carte actuelle ou des
coordonnées saisies ; le monde se choisit dans une liste déroulante native
alimentée par les mondes du jeu. Le trajet se lance au clic. Un message s’envoie dans le chat
par **Envoyer**, par la fonction d’envoi du jeu elle-même (celle de la touche
Entrée), sur le canal choisi avec le bouton **Canal** : actuel (celui du chat),
Général, Équipe, Guilde, Alliance, Groupe, Commerce ou Recrutement. Ce canal est
enregistré avec le message. **Préparer dans le chat** place le texte dans le
brouillon sans l’envoyer.

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

## Surbrillance

**Options → Dofus Enhanced → Surbrillance** choisit le niveau : derrière
les modules d’affichage, derrière ces modules et les murs, ou tout le temps.
Les deux cases **Inclure les PNJ** et **Inclure les éléments interactifs** ajoutent
ces catégories hors combat, indépendamment. Elles sont décochées par défaut.
Jusqu’à 32 cibles sont contourées en même temps, dans l’ordre monstres, PNJ
puis éléments interactifs.
Les mêmes cases sont disponibles dans **clic droit → Surbrillance**, sous les
quatre modes. Elles sont indépendantes et le menu reste ouvert après sélection.
Les objets doivent être visibles et proposer une action disponible dans le jeu.
Aucun contour n’est dessiné pendant un combat. Le contour est calculé en fin
d’image, après l’animation de zoom du jeu, et dessiné dans la même image : il
suit la caméra image par image pendant un zoom ou un déplacement de la vue, et
environ 30 fois par seconde quand la vue est immobile.
Les grandes fenêtres restent devant les contours et les clics restent natifs.
Ce budget de 32 contours simultanés est partagé entre les catégories.

## Ressources sur les cartes

Sur la **mini-carte**, une barre de six icônes rondes donne accès aux métiers.
Un clic ouvre uniquement les ressources du métier choisi ; cliquer à nouveau
sur son icône referme la liste. La bulle ouverte devient jaune, sans case à cocher.
Les bulles mesurent 48 px, avec une image de 40 px ; les noms sont disponibles au survol.
La barre conserve sa taille au zoom et au dézoom de la carte. Une main signale
les boutons cliquables ; leur survol ne sélectionne pas la case de carte située
derrière. La molette dans le sélecteur fait défiler les ressources sans zoomer
la carte.
Sur la grande carte (**M**), ouvrir le volet **Ressources**, à droite : chaque
catégorie affiche la première ressource du métier : eau, frêne, fer, ortie,
blé ou goujon. Les mêmes images servent de boutons sur la mini-carte et
proviennent du catalogue des objets du jeu.

La sélection des ressources est commune aux deux cartes et sauvegardée entre
les lancements. **Afficher sur la mini-carte** et **Afficher sur la grande carte**
se règlent indépendamment. Les filtres restent accessibles sans ressource
sélectionnée. Décocher l’affichage d’une carte, dans son volet ou dans les
options, masque aussi le volet de sélection de cette carte jusqu’à réactivation
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

## Metamob

Dans **Options → Dofus Enhanced → Pierre d’âme**, sous l’alerte et l’équipement
automatique, trois lignes synchronisent les captures avec ta quête Ocre sur
[metamob.fr](https://www.metamob.fr) :

1. Sur Metamob, **Paramètres → Clé API → Générer une clé**, puis copie-la.
2. Colle-la dans le champ masqué **Clé API Metamob** et clique sur **Enregistrer**.
   Le mod la vérifie auprès de Metamob et indique dans le chat le compte reconnu.
3. Coche **Synchroniser les captures avec Metamob**.

Après chaque combat où ton personnage obtient l’âme d’un archimonstre ou d’un
boss, le mod ajoute 1 à ce monstre dans ta quête Ocre Dofus Unity, sans rien
modifier d’autre, et l’écrit dans le chat (« Metamob : +1 « Tofumanchou
l’Empereur » (2 → 3). »). Il lit le rapport de fin de combat du jeu : seul le
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
côté des réglages, et n’est jamais affichée, journalisée ni publiée.
**Oublier la clé Metamob** la supprime. Les captures faites pendant que Metamob
est injoignable sont renvoyées automatiquement pendant la session.

## Compilation et publication

Windows x64, outils C++ Visual Studio, CMake, Ninja, Python 3 et Windows PowerShell.

```powershell
git clone --recurse-submodules https://github.com/Altansar69/dofus-miniatures.git
cd dofus-miniatures
.\build.ps1
ctest --test-dir build --output-on-failure --output-junit test-results.xml
python tools/package-release.py --output "C:\chemin\vers\releases"
```

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
à la fin de `LISEZ-MOI.txt`). Le SDK WebView2 inclut
sa licence dans `third_party/webview2`. Le compagnon WebView2 reste dans les archives pour la compatibilité des anciens
chargeurs, mais le panneau natif r95 ne le lance pas et ne dépend pas de son moteur.
Le lecteur JSON utilise nlohmann/json 3.12.0 (MIT), licence incluse dans
`third_party/nlohmann` et `LISEZ-MOI.txt`.

La compilation et les tests hors jeu ne remplacent pas la vérification de
l’initialisation et du rendu dans Dofus.

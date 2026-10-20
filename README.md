# Dofus Miniatures

Mod expérimental pour **Dofus Unity 3.7.12.12**, révision **r80**.

Miniatures, options d’animations, surbrillance, aide à la capture d’âmes,
raccourcis de trajet, ressources sur la carte et panneau natif de recherche manuelle DofusDB.
Carnet personnel, planification de l’XP des métiers et suivi des croisements de dragodindes.
Les détails et les limites figurent dans [LISEZ-MOI.txt](LISEZ-MOI.txt).

## Mises à jour

Les versions sont disponibles dans les [Releases](https://github.com/Altansar69/dofus-miniatures/releases).
Extraire l’archive entière. Le nom historique `DofusMiniatures-3.7.11.11-rN`
conserve les réglages partagés ; la version du jeu ciblée figure dans `release.ini`.

Dans **Options → Mod (r80) → Général** :

- **Chargement automatique** charge le mod lors des prochains lancements.
- **Mises à jour GitHub** recherche, télécharge et vérifie une version compatible
  avant le chargement. Activé par défaut. Aucun compte GitHub requis.

Le lanceur conserve les anciennes versions et les réglages. En cas d’absence
de réseau, il utilise une version locale compatible. Il ne remplace pas un module
dans un jeu déjà ouvert. Il laisse intact un client sans version compatible.

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
La révision réellement chargée apparaît dans le nom de l’onglet **Mod (rN)**.

## Chasse au trésor

**Options → Mod (r80) → Général → Chasse au trésor → Ouvrir le panneau**.
La fenêtre utilise les contrôles et le déplacement natifs de Dofus.
Renseigner X, Y et une direction, cliquer sur Rechercher, puis filtrer par indice.
Chaque résultat affiche sa position la plus proche et sa distance. Un clic prépare
le départ suivant ; aucun trajet ni validation de chasse ne se lance.
Les requêtes vont directement à DofusDB, sans identifiants ni cookies.
Un refus d’accès ou une erreur réseau reste visible dans le panneau.

## Carnet

**Options → Mod (r80) → Général → Carnet → Ouvrir le carnet**.
Les onglets **Positions**, **Notes** et **Messages** permettent de créer, modifier
et supprimer les entrées. Une position peut reprendre la carte actuelle ou des
coordonnées saisies. Le trajet se lance au clic. Un message enregistré est ajouté
au brouillon du chat ; son envoi reste manuel.

Le carnet est enregistré localement à côté des préférences partagées, hors des
dossiers de révision. Une modification concurrente de la même entrée est signalée
pour éviter d’écraser celle d’un autre client.

## XP des métiers

Le troisième onglet **XP** de la fenêtre **Métiers** reprend le métier sélectionné,
son niveau et son XP total. Les recettes viennent de
[l’outil DofusDB](https://dofusdb.fr/fr/tools/jobs-xp).
La simulation calcule le nombre de fabrications et les ingrédients nécessaires
pour atteindre le niveau cible, en recalculant le gain quand le niveau augmente.
Le coefficient d’XP et le bonus de serveur Épique se règlent manuellement.
Ce calcul ne choisit pas automatiquement la recette la moins chère et ne fabrique rien.
Les requêtes sont anonymes et effectuées en arrière-plan ; une erreur reste visible.

## Croisements de dragodindes

Le bouton **Croisements** dans l’interface d’élevage ouvre le suivi des 66
combinaisons de l’outil
[DofusDB](https://dofusdb.fr/fr/tools/breeding/crossing/dragodinde).
Cocher les variétés obtenues permet de suivre la progression par génération.
Ce suivi est manuel, sauvegardé localement entre les mises à jour ; il ne suppose
ni la possession d’une monture ni la validation d’un succès.

## Surbrillance

**Options → Mod (r80) → Général → Surbrillance** choisit le niveau : derrière
les modules d’affichage, derrière ces modules et les murs, ou tout le temps.
Les deux cases **Inclure les PNJ** et **Inclure les éléments interactifs** ajoutent
ces catégories hors combat, indépendamment. Elles sont décochées par défaut.
Les objets doivent être visibles et proposer une action disponible dans le jeu.
Les grandes fenêtres restent devant les contours et les clics restent natifs.
Le budget existant de huit contours simultanés est partagé entre les catégories.

## Ressources sur les cartes

Sur la **mini-carte** ou sur la grande carte (**M**), ouvrir le volet repliable
**Ressources**, à droite. Il démarre fermé et reste accessible même sans
ressource sélectionnée ou lorsque leur affichage est désactivé.
La sélection est commune aux deux cartes et se synchronise entre leurs volets.
Les filtres de transport restent à leur place. Ouvrir un métier pour sélectionner
les ressources : 86 choix dont 16 minerais, avec leur icône et leur niveau.
Les choix sont conservés entre les lancements.

La carte et la mini-carte affichent les icônes du jeu avec les quantités recensées
par ressource. Les repères sont plus compacts sur la mini-carte (28 px,
contre 36 px sur la grande carte). Le volet suit sa taille et dispose de son
propre défilement ; il apparaît à partir de 144 × 160 unités UI visibles.
Un astérisque signale des points en intérieur ; zoomer pour voir
les repères trop proches. Les repères laissent passer les clics de navigation.
Les cristaux pliable et liquide apparaissent dans leur monde de Pandala.
[Détails des données DofusDB](RESOURCE-DATA.md). La disponibilité après récolte
n’est pas connue. La r80 est vérifiée hors jeu ; son rendu en jeu reste à confirmer.

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

MinHook est un sous-module, sous sa licence d’origine. Le SDK WebView2 inclut
sa licence dans `third_party/webview2`. Le compagnon WebView2 reste dans les archives pour la compatibilité des anciens
chargeurs, mais le panneau natif r80 ne le lance pas et ne dépend pas de son moteur.
Le lecteur JSON utilise nlohmann/json 3.12.0 (MIT), licence incluse dans
`third_party/nlohmann` et `LISEZ-MOI.txt`.

La compilation et les tests hors jeu ne remplacent pas la vérification de
l’initialisation et du rendu dans Dofus.

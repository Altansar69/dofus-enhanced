# Dofus Miniatures

Mod expérimental pour **Dofus Unity 3.7.12.12**, révision **r76**.

Miniatures, options d’animations, surbrillance, aide à la capture d’âmes,
raccourcis de trajet, ressources sur la carte et panneau natif de recherche manuelle DofusDB.
Les détails et les limites figurent dans [LISEZ-MOI.txt](LISEZ-MOI.txt).

## Mises à jour

Les versions sont disponibles dans les [Releases](https://github.com/Altansar69/dofus-miniatures/releases).
Extraire l’archive entière. Le nom historique `DofusMiniatures-3.7.11.11-rN`
conserve les réglages partagés ; la version du jeu ciblée figure dans `release.ini`.

Dans **Options → Mod (r76) → Général** :

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

**Options → Mod (r76) → Général → Chasse au trésor → Ouvrir le panneau**.
La fenêtre utilise les contrôles et le déplacement natifs de Dofus.
Renseigner X, Y et une direction, cliquer sur Rechercher, puis filtrer par indice.
Chaque résultat affiche sa position la plus proche et sa distance. Un clic prépare
le départ suivant ; aucun trajet ni validation de chasse ne se lance.
Les requêtes vont directement à DofusDB, sans identifiants ni cookies.
Un refus d’accès ou une erreur réseau reste visible dans le panneau.

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
chargeurs, mais le panneau natif r76 ne le lance pas et ne dépend pas de son moteur.
Le lecteur JSON utilise nlohmann/json 3.12.0 (MIT), licence incluse dans
`third_party/nlohmann` et `LISEZ-MOI.txt`.

La compilation et les tests hors jeu ne remplacent pas la vérification de
l’initialisation et du rendu dans Dofus.

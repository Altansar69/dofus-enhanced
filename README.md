# 🐉 Dofus Enhanced

**[🇫🇷 Français](#francais) · [🇬🇧 English](#english)**

[![License](https://img.shields.io/badge/License-UNLICENSE-red)](LICENSE) [![Release](https://img.shields.io/github/v/release/Altansar69/dofus-miniatures)](../../releases/latest) ![Dofus Unity](https://img.shields.io/badge/Dofus%20Unity-3.7.12.12-orange) ![OS](https://img.shields.io/badge/OS-Windows-green)

> [!WARNING]
> **🇫🇷 Projet non officiel : à lire avant toute utilisation**
>
> - **Je ne suis pas affilié à Ankama.** Dofus Enhanced est un projet de fan non officiel, ni approuvé ni sponsorisé par Ankama.
> - **Dofus**, son nom, ses visuels et **tous les droits sur le jeu appartiennent à Ankama**.
> - **Je ne sais pas si l’utilisation de ce mod est autorisée** par les conditions d’utilisation de Dofus et d’Ankama : **la possibilité d’un bannissement n’est pas à exclure totalement**.
> - Tu l’utilises **à tes risques et périls**. Le logiciel est fourni **« tel quel », sans aucune garantie**.
>
> **🇬🇧 Unofficial project: please read before using it**
>
> - **I am not affiliated with Ankama.** Dofus Enhanced is an unofficial fan project, not endorsed or sponsored by Ankama.
> - **Dofus**, its name, its assets and **all rights to the game belong to Ankama**.
> - **I don’t know whether using this mod is allowed** by the terms of use of Dofus and Ankama: **a ban cannot be completely ruled out**.
> - Use it **at your own risk**. The software is provided **“as is”, without any warranty**.

<a id="francais"></a>

# 🇫🇷 Français

**Dofus Enhanced** ajoute à **Dofus Unity** une bonne dose de confort, directement dans l’interface du jeu : 🤏 miniatures, ⏩ animations de combat raccourcies, ⛏️ ressources sur les cartes, 🚀 trajets en un clic (zaaps et Havre-Sac compris), 💎 aide à la capture d’âmes avec synchro Metamob, 🏴‍☠️ chasse au trésor, 📒 carnet et 📈 XP des métiers.

Tout se règle dans les options du jeu, les panneaux du mod sont construits avec les composants du jeu, et le mod se met à jour tout seul au lancement du jeu. Gratuit et open source. 🎁

## ✨ Fonctionnalités

⚙️ **Un onglet « Dofus Enhanced » dans les options du jeu**, en blocs comme les autres options : Apparence et animations, Surbrillance, Cartes, Déplacements, Pierre d’âme, Chat, Chargement et mises à jour, À propos. Presque tout s’active, se désactive ou se règle là.

### 🎨 Apparence et animations
- 🤏 **Miniatures, Créatures ou Normal**, au choix en combat et hors combat, séparément. Miniatures réduit à 60 % de la taille d’origine, sans descendre sous 50 % de la hauteur d’un joueur pour les monstres et les invocations.
- ⏩ **Animations de combat raccourcies** : Raccourcies (par défaut), Sans animations des combattants ou Instantané (ces deux modes sont expérimentaux). Les délais du serveur ne changent pas : toutes les animations ne disparaissent pas.
- 🖱️ Le bouton créature du jeu devient un sélecteur Normal / Créatures / Miniatures, et le clic droit sur la carte propose Affichage, Animations et Surbrillance.

### 🔦 Surbrillance
- 👀 **Contour des monstres** hors combat, pour les repérer quand ils sont cachés derrière l’interface, derrière l’interface et les murs, ou tout le temps.
- 🧑‍🌾 En option, les **PNJ** et les **éléments interactifs** (ressources récoltables, portes, zaaps…). Jusqu’à 32 contours à la fois, toutes catégories confondues.

### 🗺️ Cartes
- ⛏️ **Ressources sur la grande carte et la mini-carte** : 86 ressources de récolte, dont 16 minerais, à choisir par métier, avec la quantité recensée sur chaque case (données DofusDB). Ce sont les points de récolte connus, pas leur disponibilité en temps réel.
- 🎯 **Bouton de recentrage** sur la mini-carte, pour la ramener sur ton personnage.

### 🧭 Déplacements
- 🚀 **Trajets améliorés** : bouton **TP** du chat, double-clic sur la grande carte ou la mini-carte, ou « Voyager jusqu’à cette position » au clic droit. Le mod compare le départ à pied et par tes zaaps connus (directement ou depuis le Havre-Sac), puis laisse l’autopilote du jeu finir le trajet. Les prix des zaaps s’appliquent.
- 🕹️ Tu préfères l’autopilote d’origine ? Décoche **Remplacer l’autopilote du jeu** ; Ctrl + double-clic et `/travel` le gardent toujours.
- 💨 **Transitions rapides entre cartes** (fondus raccourcis) et **préchargement de la carte suivante** (expérimental). Le chargement et le serveur restent nécessaires.
- 💞 **Téléportation au conjoint** : bouton, cœur et distance sur la vignette de ton conjoint dans le groupe, plus une entrée au clic droit. Uniquement au clic, selon les conditions du jeu.

### 💎 Pierre d’âme et Metamob
- 🔔 **Alerte pierre d’âme** : au placement d’un combat contre un archimonstre ou un boss capturable, la tranche de pierre requise, ton stock et la pierre équipée. En combat, **Capture d’âme** passe au vert quand l’état Chercheur d’âmes est actif sur toi, au rouge sinon. Le vert ne garantit pas à lui seul la capture.
- 🎒 **Équipement automatique** de la bonne pierre pendant le placement (une seule tentative par combat), sinon un bouton **Équiper**.
- 🔗 **Synchro Metamob** (désactivée par défaut) : après chaque combat où ton personnage obtient l’âme d’un archimonstre ou d’un boss, +1 à ce monstre dans ta quête Ocre sur [metamob.fr](https://www.metamob.fr). Ta clé API personnelle est chiffrée pour ton compte Windows, jamais affichée ni journalisée.

### 💬 Chat
- 💬 À côté des positions et des archimonstres ou boss de L’éternelle moisson partagés dans le chat : bouton **TP**, **Âme en sac** / Pas en sac (sac uniquement, pas la banque), avancement **Ocre** (un clic ouvre la quête) et quantité **Metamob** (avec une clé enregistrée).
- 🎛️ Chaque ajout s’active séparément. Avec le TP, les coordonnées écrites `[x,y]` deviennent cliquables.

### 🧰 Outils intégrés
- 🏴‍☠️ **Chasse au trésor** : un panneau reprend ta position et l’étape en cours, cherche les indices sur DofusDB et lance le trajet vers l’indice en un clic (**Chemin**). Aucune validation n’est envoyée à ta place.
- 📒 **Carnet personnel** : positions, notes et messages. Voyage vers une position ou envoie un message dans le canal choisi en un clic. Version complète ou compacte.
- 📈 **XP des métiers** : un onglet **XP** dans la fenêtre Métiers calcule les fabrications et les ingrédients nécessaires pour atteindre le niveau visé, avec les recettes DofusDB.

### 🔄 Mises à jour
- 🔄 **Mises à jour automatiques depuis GitHub** au lancement du jeu : chaque archive est vérifiée (SHA-256 et compatibilité exacte avec ta version de Dofus) avant d’être installée. Sans réseau, la version locale compatible est utilisée.

## 🔨 Installation

1. 📦 Sur la **[dernière version](../../releases/latest)** (page Releases), télécharge l’archive `DofusMiniatures-rN-….zip` (pas « Source code »).
2. 📂 Extrais **toute l’archive** dans un dossier que tu gardes, au chemin court et hors du dossier du jeu (par exemple `C:\DofusEnhanced`). Elle contient un dossier `DofusMiniatures-3.7.11.11-rN` : ce nom historique est normal, la version de Dofus visée est indiquée dans `release.ini`.
3. ▶️ Ouvre un terminal dans ce dossier `DofusMiniatures-3.7.11.11-rN` (dans l’Explorateur, tape `powershell` dans la barre d’adresse puis Entrée), puis choisis l’une des deux méthodes :

   **A. Jeu déjà lancé** : connecte-toi à ton personnage, lance la commande ci-dessous et entre le PID de Dofus qu’elle affiche. Après ce premier chargement, le chargement automatique s’active par défaut pour les lancements suivants.

   ```powershell
   .\ModuleLoader.exe --interactive
   ```

   **B. Chargement automatique d’emblée** : indique le chemin de ton `Dofus.exe`, puis lance le jeu normalement. Le mod se chargera à chaque lancement ; un jeu déjà ouvert n’est pas touché.

   ```powershell
   .\ModuleLoader.exe --enable-auto "C:\chemin\vers\Dofus.exe"
   ```

   💡 Pour trouver `Dofus.exe` : jeu lancé, Gestionnaire des tâches → clic droit sur Dofus → *Ouvrir l’emplacement du fichier*.
4. 🎮 En jeu : **Options → Dofus Enhanced**. C’est parti ! 🎉

🔄 **Mises à jour** : avec le chargement automatique et l’option **Mises à jour GitHub** (activée par défaut), chaque nouvelle version est téléchargée, vérifiée et installée automatiquement au lancement du jeu. Tes réglages sont conservés d’une version à l’autre. Un jeu déjà ouvert garde sa version jusqu’au prochain lancement.

## ❓ FAQ et remarques

- 🪟 **Windows uniquement** (64 bits).
- 🎯 **Une version de Dofus par version du mod** : actuellement **Dofus Unity 3.7.12.12**. Le mod vérifie les empreintes exactes du jeu avant de se charger.
- 🛠️ **Mise à jour de Dofus par Ankama ?** Le mod peut devoir être adapté : tant qu’aucune version compatible n’est publiée, il ne se charge pas et le jeu reste intact. Dès qu’elle sort, la mise à jour automatique l’installe.
- 🔁 **Version antérieure à 1.11.0 ?** Après le renommage du dépôt en `dofus-enhanced`, sa mise à jour automatique ne trouve plus les nouvelles versions. Télécharge la dernière, extrais-la dans le dossier qui contient déjà `DofusMiniatures-3.7.11.11-rN` (pas dans un sous-dossier), puis décoche et recoche **Chargement automatique** en jeu (ou lance `.\ModuleLoader.exe --enable-auto "C:\chemin\vers\Dofus.exe"` depuis le nouveau dossier). Tes réglages sont conservés.
- 📝 **Journaux** : `module.log` dans le dossier de la version chargée (`DofusMiniatures-3.7.11.11-rN`), `autoload.log` dans celui de l’assistant de chargement automatique.
- 🌐 **Connexions** : GitHub pour les mises à jour, DofusDB pour la chasse au trésor et l’XP des métiers, Metamob seulement si tu enregistres ta clé.
- 🗣️ **Langue** : l’interface du mod est en français. La synchro Metamob demande un jeu en français, en anglais ou en espagnol.
- 🧪 **Mod expérimental** : chaque version passe ses tests hors jeu avant publication, mais certaines fonctions restent marquées expérimentales et des bugs sont possibles.
- 🧹 **Désinstaller** : décoche **Chargement automatique** (ou lance `.\ModuleLoader.exe --disable-auto`), ferme le jeu, puis supprime le dossier du mod. Ta clé Metamob reste valable sur metamob.fr tant que tu ne la régénères pas.
- 🐛 **Un bug ?** Ouvre une [issue](../../issues) avec ta version (Options → Dofus Enhanced → À propos) et les dernières lignes de `module.log`.

## 📖 Documentation

- 📘 [Guide détaillé (français)](docs/GUIDE.md) : chaque fonction en détail, Metamob, compilation et publication.
- 📜 [LISEZ-MOI.txt](LISEZ-MOI.txt) : historique des versions, aussi inclus dans l’archive.
- ⛏️ [RESOURCE-DATA.md](RESOURCE-DATA.md) : origine des données des ressources.

## 🔢 Versions

Chaque version porte un numéro **x.y.z**, affiché en jeu dans **Options → Dofus Enhanced → À propos**, et une révision **rN** : l’étiquette GitHub (`r109`, `r110`…) que suit la mise à jour automatique. Exemple : 1.11.0 = r110.

## 🤝 Crédits

- [Altansar69](https://github.com/Altansar69) - a créé le projet
- [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu) - bibliothèque de hooks
- [nlohmann/json](https://github.com/nlohmann/json) (Niels Lohmann) - lecture du JSON
- [DofusDB](https://dofusdb.fr) - données des ressources, API de la chasse au trésor et recettes des métiers
- [Metamob](https://www.metamob.fr) - API de suivi des quêtes Ocre

## 📄 Licence

Le code de Dofus Enhanced est placé dans le domaine public sous **Unlicense** : voir [LICENSE](LICENSE).

Les composants tiers gardent leur propre licence : MinHook (BSD 2-Clause, `third_party/minhook`), nlohmann/json (MIT, `third_party/nlohmann`) et le SDK WebView2 de Microsoft (`third_party/webview2/LICENSE.txt`). Les textes de MinHook et nlohmann/json figurent aussi à la fin de `LISEZ-MOI.txt`.

L’Unlicense ne couvre que le code écrit pour Dofus Enhanced : ni les composants tiers ci-dessus, ni les données de ressources issues de DofusDB (`resource-map-generated.hpp`, voir [RESOURCE-DATA.md](RESOURCE-DATA.md)), ni Dofus et ses contenus, qui restent la propriété d’Ankama.

<a id="english"></a>

# 🇬🇧 English

**Dofus Enhanced** brings a good dose of quality of life to **Dofus Unity**, right inside the game’s interface: 🤏 miniatures, ⏩ shortened combat animations, ⛏️ resources on the maps, 🚀 one-click travel (zaaps and Havenbag included), 💎 soul capture helper with Metamob sync, 🏴‍☠️ treasure hunt, 📒 notebook and 📈 profession XP.

Everything is set up in the game’s options, the mod’s panels are built from the game’s own components, and the mod updates itself when the game starts. Free and open source. 🎁

## ✨ Features

⚙️ **A “Dofus Enhanced” tab in the game’s own options**, split into blocks like the other options: Apparence et animations (appearance and animations), Surbrillance (highlight), Cartes (maps), Déplacements (travel), Pierre d’âme (soul stone), Chat, Chargement et mises à jour (loading and updates), À propos (about). Almost everything is switched on, off or tuned there.

### 🎨 Appearance and animations
- 🤏 **Miniatures, Créatures (creatures) or Normal**, chosen separately in and out of combat. Miniatures shrinks to 60% of the original size, never below 50% of a player’s height for monsters and summons.
- ⏩ **Shortened combat animations**: Raccourcies (shortened, default), Sans animations des combattants (no fighter animations) or Instantané (instant); the last two modes are experimental. Server delays do not change, so not every animation disappears.
- 🖱️ The game’s creature button becomes a Normal / Créatures / Miniatures selector, and right-clicking the map offers Affichage (display), Animations and Surbrillance (highlight).

### 🔦 Highlight
- 👀 **Monster outlines** out of combat, to spot monsters hidden behind the interface, behind the interface and walls, or all the time.
- 🧑‍🌾 Optionally, **NPCs** and **interactive elements** (harvestable resources, doors, zaaps…). Up to 32 outlines at once, all categories combined.

### 🗺️ Maps
- ⛏️ **Resources on the world map and the minimap**: 86 gathering resources, including 16 ores, picked per profession, with the recorded quantity on each map cell (DofusDB data). These are known gathering spots, not their real-time availability.
- 🎯 **Recenter button** on the minimap, bringing it back to your character.

### 🧭 Travel
- 🚀 **Smarter travel**: the chat **TP** button, double-clicking the world map or the minimap, or the right-click « Voyager jusqu’à cette position » (travel to this position) entry. The mod compares walking with your known zaaps (directly or from the Havenbag), then lets the game’s autopilot finish the trip. Zaap prices apply.
- 🕹️ Prefer the original autopilot? Untick **Remplacer l’autopilote du jeu** (replace the game’s autopilot); Ctrl + double-click and `/travel` always keep it.
- 💨 **Fast map transitions** (shorter fades) and **next-map preloading** (experimental). Loading and the server are still needed.
- 💞 **Spouse teleport**: a button, a heart and the distance on your spouse’s portrait in the party, plus a right-click entry. Only on click, under the game’s own conditions.

### 💎 Soul stones and Metamob
- 🔔 **Soul stone alert**: during the placement phase of a fight against a capturable archmonster or boss, the required soul stone tier, your stock and the equipped stone. In combat, **Capture d’âme** (soul capture) turns green when the Chercheur d’âmes (soul seeker) state is on you, red otherwise. Green alone does not guarantee the capture.
- 🎒 **Auto-equip** of the right stone during placement (a single attempt per fight), otherwise an **Équiper** (equip) button.
- 🔗 **Metamob sync** (off by default): after each fight where your character gets the soul of an archmonster or a boss, +1 for that monster in your Ochre quest on [metamob.fr](https://www.metamob.fr). Your personal API key is encrypted for your Windows account, never shown or logged.

### 💬 Chat
- 💬 Next to positions and to archmonsters or bosses of the Ochre quest (L’éternelle moisson) shared in chat: a **TP** button, **Âme en sac** / Pas en sac (soul in your bag or not; the bank is not checked), **Ocre** progress (a click opens the quest) and your **Metamob** count (with a saved key).
- 🎛️ Each addition can be turned on or off. With TP, typed `[x,y]` coordinates become clickable.

### 🧰 Built-in tools
- 🏴‍☠️ **Treasure hunt**: a panel picks up your position and the current step, searches clues on DofusDB and starts the trip to the clue in one click (**Chemin**, path). Nothing is validated on your behalf.
- 📒 **Personal notebook**: positions, notes and messages. Travel to a position or send a message on the chosen channel in one click. Full or compact layout.
- 📈 **Profession XP**: an **XP** tab in the professions (Métiers) window works out the crafts and ingredients needed to reach your target level, using DofusDB recipes.

### 🔄 Updates
- 🔄 **Automatic updates from GitHub** when the game starts: each archive is verified (SHA-256 and exact compatibility with your Dofus version) before it is installed. Offline, the compatible local version is used.

## 🔨 Installation

1. 📦 On the **[latest release](../../releases/latest)** (Releases page), download the `DofusMiniatures-rN-….zip` archive (not “Source code”).
2. 📂 Extract **the whole archive** into a folder you keep, with a short path and outside the game folder (for example `C:\DofusEnhanced`). It contains a `DofusMiniatures-3.7.11.11-rN` folder: this historical name is normal, the targeted Dofus version is written in `release.ini`.
3. ▶️ Open a terminal in that `DofusMiniatures-3.7.11.11-rN` folder (in File Explorer, type `powershell` in the address bar, then Enter), then pick one of the two methods:

   **A. Game already running**: log in to your character, run the command below and type the Dofus PID it shows. After this first load, automatic loading is turned on by default for the next launches.

   ```powershell
   .\ModuleLoader.exe --interactive
   ```

   **B. Automatic loading right away**: give the path of your `Dofus.exe`, then start the game as usual. The mod will load at every launch; a game that is already running is left alone.

   ```powershell
   .\ModuleLoader.exe --enable-auto "C:\path\to\Dofus.exe"
   ```

   💡 To find `Dofus.exe`: with the game running, Task Manager → right-click Dofus → *Open file location*.
4. 🎮 In game: **Options → Dofus Enhanced**. Have fun! 🎉

🔄 **Updates**: with automatic loading and the **Mises à jour GitHub** (GitHub updates) option, on by default, each new version is downloaded, verified and installed automatically when the game starts. Your settings are kept from one version to the next. A game that is already running keeps its version until the next launch.

## ❓ FAQ and notes

- 🪟 **Windows only** (64-bit).
- 🎯 **One Dofus version per mod version**: currently **Dofus Unity 3.7.12.12**. The mod checks the game’s exact fingerprints before loading.
- 🛠️ **Ankama updated Dofus?** The mod may need an update: until a compatible version is published, it does not load and the game is left untouched. As soon as it is out, the automatic update installs it.
- 🔁 **On a version older than 1.11.0?** Once the repository is renamed to `dofus-enhanced`, its automatic update no longer finds new versions. Download the latest one, extract it into the folder that already contains `DofusMiniatures-3.7.11.11-rN` (not into a subfolder), then untick and tick **Chargement automatique** in game (or run `.\ModuleLoader.exe --enable-auto "C:\path\to\Dofus.exe"` from the new folder). Your settings are kept.
- 📝 **Logs**: `module.log` in the folder of the loaded version (`DofusMiniatures-3.7.11.11-rN`), `autoload.log` in the folder of the automatic loading helper.
- 🌐 **Connections**: GitHub for updates, DofusDB for the treasure hunt and profession XP, Metamob only if you save your key.
- 🗣️ **Language**: the mod’s interface is in French. Metamob sync needs the game in French, English or Spanish.
- 🧪 **Experimental mod**: every version passes its offline tests before release, but some features are still marked experimental and bugs are possible.
- 🧹 **Uninstall**: untick **Chargement automatique** (automatic loading), or run `.\ModuleLoader.exe --disable-auto`, close the game, then delete the mod’s folder. Your Metamob key stays valid on metamob.fr until you regenerate it.
- 🐛 **Found a bug?** Open an [issue](../../issues) with your version (Options → Dofus Enhanced → À propos) and the last lines of `module.log`.

## 📖 Documentation

- 📘 [Detailed guide (French)](docs/GUIDE.md): every feature in detail, Metamob, build and release.
- 📜 [LISEZ-MOI.txt](LISEZ-MOI.txt): version history (in French), also included in the archive.
- ⛏️ [RESOURCE-DATA.md](RESOURCE-DATA.md): where the resource data comes from.

## 🔢 Versioning

Each version has an **x.y.z** number, shown in game in **Options → Dofus Enhanced → À propos** (about), and an **rN** revision: the GitHub tag (`r109`, `r110`…) that the automatic updates follow. Example: 1.11.0 = r110.

## 🤝 Credits

- [Altansar69](https://github.com/Altansar69) - Has created the project
- [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu) - hooking library
- [nlohmann/json](https://github.com/nlohmann/json) (Niels Lohmann) - JSON parsing
- [DofusDB](https://dofusdb.fr) - resource data, treasure hunt API and profession recipes
- [Metamob](https://www.metamob.fr) - Ochre quest tracking API

## 📄 License

Dofus Enhanced’s code is released into the public domain under the **Unlicense**: see [LICENSE](LICENSE).

Third-party components keep their own licenses: MinHook (BSD 2-Clause, `third_party/minhook`), nlohmann/json (MIT, `third_party/nlohmann`) and Microsoft’s WebView2 SDK (`third_party/webview2/LICENSE.txt`). The MinHook and nlohmann/json texts are also reproduced at the end of `LISEZ-MOI.txt`.

The Unlicense only covers the code written for Dofus Enhanced: not the third-party components above, not the DofusDB resource data (`resource-map-generated.hpp`, see [RESOURCE-DATA.md](RESOURCE-DATA.md)), and not Dofus and its content, which remain Ankama’s property.

[Français](README.md) | English

# 🐉 **Dofus Enhanced**
![License](https://img.shields.io/badge/License-UNLICENSE-red) [![Version](https://img.shields.io/github/v/release/Altansar69/dofus-enhanced?label=Version&color=blue)](../../releases/latest) ![Dofus Unity](https://img.shields.io/badge/Dofus%20Unity-3.7.12.12-orange) ![OS](https://img.shields.io/badge/OS-Windows-green)

> [!WARNING]
> **Unofficial project: please read before using it**
>
> - **I am not affiliated with Ankama.** Dofus Enhanced is an unofficial fan project, not endorsed or sponsored by Ankama.
> - **Dofus**, its name, its assets and **all rights to the game belong to Ankama**.
> - **I don’t know whether using this mod is allowed** by the terms of use of Dofus and Ankama: **a ban cannot be completely ruled out**.
> - Use it **at your own risk**. The software is provided **“as is”, without any warranty**.

**Dofus Enhanced** brings a good dose of quality of life to **Dofus Unity**, right inside the game’s interface: miniatures your way, combat animations à la carte, resources on the maps, missing archmonsters zone by zone, one-click travel (zaaps, Havenbag and the Roublards’ secret passage included), soul capture helper with Metamob sync, Kralamoure openings in the calendar, treasure hunt, notebook, profession XP, on-screen FPS, per-character statistics, Dofus-style cursors and vertical gauges.

Everything is set up in the game’s options, the mod’s panels are built from the game’s own components, and the mod updates itself when the game starts. Free and open source.

## ✨ Features

**A “Dofus Enhanced” tab in the game’s own options**, split into blocks like the other options: Apparence (appearance), Animations, Surbrillance (highlight), Curseur (cursor), Jauges (gauges), Affichages enregistrés (saved layouts), Cartes (maps), Carnet (notebook), Chasse au trésor (treasure hunt), Déplacements (travel), Pierre d’âme (soul stone), Chat, Calendrier (calendar), Performances (performance), Statistiques (statistics), Chargement et mises à jour (loading and updates), À propos (about). Almost everything is switched on, off or tuned there.

### Appearance
- **Miniatures, Créatures (creatures) or Normal**, chosen separately in and out of combat. Miniatures shrinks to 60% of the original size, never below 50% of a player’s height for monsters and summons.
- **You pick who becomes a miniature**: in combat, yourself, allied players, allied summons, your own summons, enemies and enemy summons; out of combat, yourself, party members, other players, monsters and, if you want, NPCs. Everything is ticked by default except NPCs. The same boxes are in the **Affichage** menu of the map's right-click.
- The game’s creature button becomes a Normal / Créatures / Miniatures selector, and right-clicking the map offers Affichage (display), Animations and Surbrillance (highlight).

### Animations
- **Six independent checkboxes** for fights: **Sans effets des sorts** (no spell effects) and **Lancers de sorts raccourcis** (shortened spell casts), ticked by default, then, as experimental options, **Sans animations d’attaque et de sort** (no attack and spell animations), **Sans animations de mort** (no death animations), **Déplacements instantanés** (instant movements) and **Sans attentes visuelles** (no visual waits).
- Server delays do not change, so not every animation disappears. The same checkboxes are in the map’s right-click menu.

### Highlight
- **Outlines out of combat** around what you tick, to spot it when it is hidden behind the interface, behind the interface and walls, or all the time.
- **Six independent checkboxes**: **Monstres** (monsters, ticked by default), **PNJ** (NPCs), **Éléments interactifs** (interactive elements: harvestable resources, doors, zaaps…), **Moi** (yourself), **Membres du groupe** (party members) and **Autres joueurs** (other players). The same checkboxes are in the map’s right-click menu. Up to 32 outlines at once: your character and your party come first, other players last.

### Cursor
- **Curseur Dofus** (Dofus cursor, off by default): cursors drawn by the mod in Dofus’ style replace the Windows pointer: the selection arrow, the hand over what the game makes clickable (buttons, tabs, links, map cells and map edges…), the text cursor in input fields (chat and searches included), the open hand on window title bars, closed while you move a window or drag an item, and the double arrows on the edges of resizable windows. With **Curseurs du jeu aussi** (game cursors too, ticked by default), the game’s own cursors (bubble, gear, harvesting tools, sword, fight markers…) are drawn by the mod as well, in the chosen colour and size.
- **Couleur du curseur** (colour): Défaut (default: the theme chosen in the game’s options, named in brackets and followed when you change it), each of the game’s eleven themes (Standard, Bonta, Brakmar…), Or (gold), Émeraude (emerald) or Argent (silver).
- **Taille du curseur** (size): Comme la souris de Windows (like the Windows pointer, default, display scaling included), Grande (large, one and a half times) or Très grande (very large, twice), always real Windows cursors that follow the mouse without lag.

### Gauges
- **Afficher à la verticale** (show vertically): a checkbox in the ⋯ menu (the three small dots) of each of the game’s gauges (character experience, guild experience, honour points, pods, energy points, each profession’s experience) turns it vertical. The game’s own bar fills from the bottom up, the icon above and the level below; the gauge keeps its place and moves as before; its length changes by pulling its top or bottom edge. The mod keeps its vertical length and place, and it becomes exactly the game’s gauge again, at its previous place and size, when you untick it. Each gauge keeps its choice, for all your characters.
- **Affichage vertical dans le menu … des jauges** (vertical display in the gauges’ … menu, ticked by default): unticked, the checkbox leaves the menus and every gauge goes back to horizontal, their choices kept.

### Saved layouts
- **Garder les fenêtres du mod dans les affichages enregistrés** (keep the mod’s windows in saved layouts, ticked by default): a layout you save in the game also keeps the mod’s windows (statistics, notebook, treasure hunt, performance overlay, vertical gauges, Capture d’âme), open or closed, at their place and size; applying it puts them back, the fight layout included. The mod keeps them in its own file, never in the game’s layouts.

### Maps
- **Resources on the world map and the minimap**: 86 gathering resources, including 16 ores, picked per profession, with the recorded quantity on each map cell (DofusDB data). On the world map they are picked in the **Ressources** tab, a real map mode like **Anomalies**. These are known gathering spots, not their real-time availability.
- **Map tooltips**: hovering a cell, the minimap shows its zone’s name, and both maps add the dimension to the coordinates, only outside the Monde des Douze (the default) or everywhere, chosen for each map: [x,y, Incarnam]. The minimap’s resource bar can be hidden while keeping the markers.
- **« Ocre » mode**: a square under « Conquête » (conquest) on the world map colours every zone of the archmonsters and dungeon bosses of the Ochre quest (L’éternelle moisson) by how many you are still missing (not validated in the quest, not in your bag, not on Metamob), and in blue « Complet » (complete) the zones where you have them all. Its panel works like the anomalies one: search, territories, levels and an All / Archmonsters / Dungeon bosses filter; clicking a zone centres the map and shows which ones you miss and which you have. The same colours can also be shown on the minimap.
- **Recenter button** on the minimap, bringing it back to your character.
- **Right-click settings**: right-clicking the minimap offers its own settings (resources, resource bar, recenter button, zone name on hover, missing archmonsters, dimension with the coordinates), and right-clicking the world map its own (resources, missing archmonsters, dimension with the coordinates); a click applies at once without closing the menu.

### Travel
- **Smarter travel**: the chat **TP** button, double-clicking the world map or the minimap, or the right-click « Voyager jusqu’à cette position » (travel to this position) entry. Within milliseconds, the mod compares walking with your known zaaps (directly or from the Havenbag), then lets the game’s autopilot finish the trip. Zaap prices apply.
- Prefer the original autopilot? Untick **Remplacer l’autopilote du jeu** (replace the game’s autopilot); Ctrl + double-click and `/travel` always keep it.
- **Roublards’ secret passage**: when it is shorter, the trip goes through Bad Aboum, in the crypt of the Cité d’Astrub [5,-15], to the Roublard temple [10,16] or Amakna village, and back. No need to be a Roublard, only to have access; the mod talks to him for you and never pays anything. Can be turned off with **Passage secret et temple des Roublards** (Roublards’ secret passage and temple).
- **Fast map transitions** (shorter fades) and **next-map preloading** (experimental). Loading and the server are still needed.
- **Spouse teleport**: a button at the top of your spouse’s portrait in the party and a heart at the bottom left, next to the life bar (recognised by name, never mistaken for a companion), plus a right-click entry. Only on click, under the game’s own conditions.
- **Travel to a party member**: a small zaap button at the top of each other member’s portrait; its tooltip gives the last position the game knows, and a click starts the mod’s trip (or the game’s autopilot if you kept it).

### Soul stones and Metamob
- **Soul stone alert**: during the placement phase of a fight against a capturable archmonster or boss, the required soul stone tier, your stock and the equipped stone. In combat, **Capture d’âme** (soul capture) turns green when the Chercheur d’âmes (soul seeker) state is on you, red otherwise. Green alone does not guarantee the capture.
- **Auto-equip** of the right stone during placement (a single attempt per fight), otherwise an **Équiper** (equip) button.
- **Metamob sync** (off by default): after each fight where your character gets the soul of an archmonster or a boss, +1 for that monster in your Ochre quest on [metamob.fr](https://www.metamob.fr). Your personal API key is encrypted for your Windows account, never shown or logged.

### Chat
- Next to positions and to archmonsters or bosses of the Ochre quest (L’éternelle moisson) shared in chat: a **TP** button, **Âme en sac** / Pas en sac (soul in your bag or not; the bank is not checked), **Ocre** progress (a click opens the quest) and your **Metamob** count (with a saved key).
- Shared positions and monster groups can show their dimension, only outside the Monde des Douze (the default) or everywhere: [x,y, Srambad].
- Each addition can be turned on or off. With TP, typed `[x,y]` coordinates become clickable.

### Calendar
- **Metamob’s Kralamoure openings in the game’s calendar** (with your Metamob key), like the game’s own events: a banner, then one card per opening on its day with the time (your PC’s) and the registered characters; details on hover, and a click opens the event on metamob.fr. « Kralamoure » joins « Mes calendriers » (my calendars), with its colour and icon, to show or hide it. Read only while the calendar is open, at most every 10 minutes.

### Built-in tools
- **Treasure hunt**: a panel picks up your position and the current step, searches clues on DofusDB and starts the trip to the clue in one click (**Chemin**, path). Nothing is validated on your behalf.
- **Personal notebook**: positions, notes and messages, each usable from its line in one click: travel to a position, copy its coordinates or pin it on the map, copy a note, send a message on its channel or prepare it in the chat; add a cell from the map’s right-click menu. Full or compact layout.
- **Profession XP**: an **XP** tab in the professions (Métiers) window works out the crafts and ingredients needed to reach your target level, using DofusDB recipes.
- **Performance**: optionally, the FPS, frame time, memory, CPU and GPU used by the game, and its disk's activity, in a small discreet frame you place anywhere with the mouse and resize like a window, from its four edges and four corners (a small grip appears at the bottom right while the mouse is over the frame); the measurements reflow with the width and the text takes the largest size that fits the frame, and the game shows the matching double arrow or move cursor on hover. Three independent checkboxes (FPS and memory, GPU, disk): the frame shows only what is ticked. A button at the top right, next to Capture d’âme, shows or hides it.
- **Statistics** per character, counted since 1.15.0: play time, cells and maps traversed, dimensions, experience, kamas earned and spent, harvests, crafted items, quests, achievements, fights (PvM, PvP, Kolossium), damage, your class’s spells cast by you and elements, teleports. They show in their own window, opened from its button at the top right of the screen or from **Voir mes statistiques** (view my statistics) in the options, and stay on your PC.
- **Top-bar buttons**: Capture d’âme, treasure hunt, personal notebook, statistics and performance, at the top right; each can be hidden in the options (**Bouton dans la barre du haut**, top-bar button) without changing what it controls. The notebook and the treasure hunt open only from their button.

### Updates
- **Automatic updates from GitHub** when the game starts: each archive is verified (SHA-256 and exact compatibility with your Dofus version) before it is installed. Offline, the compatible local version is used.

## 🔨 Installation

1. On the **[latest release](../../releases/latest)** (Releases page), download the `DofusMiniatures-….zip` archive (not “Source code”).
2. Extract **the whole archive** into a folder you keep, with a short path and outside the game folder (for example `C:\DofusEnhanced`). It contains a `DofusMiniatures-3.7.11.11-rN` folder: this historical name is normal, the targeted Dofus version is written in `release.ini`.
3. Open a terminal in that `DofusMiniatures-3.7.11.11-rN` folder (in File Explorer, type `powershell` in the address bar, then Enter), then pick one of the two methods:

   **A. Game already running**: log in to your character, run the command below and type the Dofus PID it shows. After this first load, automatic loading is turned on by default for the next launches.

   ```powershell
   .\ModuleLoader.exe --interactive
   ```

   **B. Automatic loading right away**: give the path of your `Dofus.exe`, then start the game as usual. The mod will load at every launch; a game that is already running is left alone.

   ```powershell
   .\ModuleLoader.exe --enable-auto "C:\path\to\Dofus.exe"
   ```

To find `Dofus.exe`: with the game running, Task Manager → right-click Dofus → *Open file location*.
4. In game: **Options → Dofus Enhanced**. Have fun!

**Updates**: with automatic loading and the **Mises à jour GitHub** (GitHub updates) option, on by default, each new version is downloaded, verified and installed automatically when the game starts. Your settings are kept from one version to the next. A game that is already running keeps its version until the next launch.

## ❓ FAQ and notes

- **Windows only** (64-bit).
- **One Dofus version per mod version**: currently **Dofus Unity 3.7.12.12**. The mod checks the game’s exact fingerprints before loading.
- **Ankama updated Dofus?** The mod may need an update: until a compatible version is published, it does not load and the game is left untouched. As soon as it is out, the automatic update installs it.
- **On a version older than 1.13.1?** Since the repository was renamed to `dofus-enhanced` and releases are named by version (`v1.15.0`…), its automatic update no longer finds new versions. Download the latest one, extract it into the folder that already contains `DofusMiniatures-3.7.11.11-rN` (not into a subfolder), then untick and tick **Chargement automatique** in game (or run `.\ModuleLoader.exe --enable-auto "C:\path\to\Dofus.exe"` from the new folder). Your settings are kept.
- **Logs**: `module.log` in the folder of the loaded version (`DofusMiniatures-3.7.11.11-rN`), `autoload.log` in the folder of the automatic loading helper.
- **Connections**: GitHub for updates, DofusDB for the treasure hunt and profession XP, Metamob only if you save your key.
- **Language**: the mod’s interface is in French. Metamob sync needs the game in French, English or Spanish.
- **Experimental mod**: every version passes its offline tests before release, but some features are still marked experimental and bugs are possible.
- **Uninstall**: untick **Chargement automatique** (automatic loading), or run `.\ModuleLoader.exe --disable-auto`, close the game, then delete the mod’s folder. Your Metamob key stays valid on metamob.fr until you regenerate it.
- **Found a bug?** Open an [issue](../../issues) with your version (Options → Dofus Enhanced → À propos) and the last lines of `module.log`.

## 📖 Documentation

- [Detailed guide (French)](docs/GUIDE.md): every feature in detail, Metamob, build and release.
- [LISEZ-MOI.txt](package/LISEZ-MOI.txt): version history (in French), also included in the archive.
- [RESOURCE-DATA.md](docs/RESOURCE-DATA.md): where the resource data comes from.
- The code: `src/` (the mod, one folder per area), `tests/` (offline tests), `package/` (files shipped as they are in the archive), `tools/` (packaging and publishing).

## 🔢 Versioning

This project is following [semantic versioning schema](https://semver.org/). Each version has an **x.y.z** number, shown in game in **Options → Dofus Enhanced → À propos** (about), which is also its GitHub tag (`v1.13.0`, `v1.14.0`…). The revisions published before 1.0.0 are numbered 0.y.0 (`v0.75.0` to `v0.91.0`).

## 🤝 Credits

- [Altansar69](https://github.com/Altansar69) - Has created the project
- [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu) - hooking library
- [nlohmann/json](https://github.com/nlohmann/json) (Niels Lohmann) - JSON parsing
- [DofusDB](https://dofusdb.fr) - resource data, treasure hunt API and profession recipes
- [Metamob](https://www.metamob.fr) - Ochre quest tracking API

## 📄 License

This software is under the [Unlicense](https://unlicense.org/), terms of which are available in [LICENSE](LICENSE).

Third-party components keep their own licenses: MinHook (BSD 2-Clause, `third_party/minhook`), nlohmann/json (MIT, `third_party/nlohmann`) and Microsoft’s WebView2 SDK (`third_party/webview2/LICENSE.txt`). The MinHook and nlohmann/json texts are also reproduced at the end of `package/LISEZ-MOI.txt`.

The Unlicense only covers the code written for Dofus Enhanced: not the third-party components above, not the DofusDB resource data (`src/maps/resource-map-generated.hpp`, see [RESOURCE-DATA.md](docs/RESOURCE-DATA.md)), and not Dofus and its content, which remain Ankama’s property.

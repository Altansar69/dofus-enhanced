Français | [English](README_en.md)

# 🐉 **Dofus Enhanced**
![License](https://img.shields.io/badge/License-UNLICENSE-red) [![Version](https://img.shields.io/github/v/release/Altansar69/dofus-enhanced?label=Version&color=blue)](../../releases/latest) ![Dofus Unity](https://img.shields.io/badge/Dofus%20Unity-3.7.12.12-orange) ![OS](https://img.shields.io/badge/OS-Windows-green)

> [!WARNING]
> **Projet non officiel : à lire avant toute utilisation**
>
> - **Je ne suis pas affilié à Ankama.** Dofus Enhanced est un projet de fan non officiel, ni approuvé ni sponsorisé par Ankama.
> - **Dofus**, son nom, ses visuels et **tous les droits sur le jeu appartiennent à Ankama**.
> - **Je ne sais pas si l’utilisation de ce mod est autorisée** par les conditions d’utilisation de Dofus et d’Ankama : **la possibilité d’un bannissement n’est pas à exclure totalement**.
> - Tu l’utilises **à tes risques et périls**. Le logiciel est fourni **« tel quel », sans aucune garantie**.

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

## 🔢 Versionnage

Ce projet suit un schéma de [versionnement sémantique](https://semver.org/). Chaque version porte un numéro **x.y.z**, affiché en jeu dans **Options → Dofus Enhanced → À propos**, et une révision **rN** : l’étiquette GitHub (`r109`, `r110`…) que suit la mise à jour automatique. Exemple : 1.11.0 = r110.

## 🤝 Crédits

- [Altansar69](https://github.com/Altansar69) - a créé le projet
- [MinHook](https://github.com/TsudaKageyu/minhook) (Tsuda Kageyu) - bibliothèque de hooks
- [nlohmann/json](https://github.com/nlohmann/json) (Niels Lohmann) - lecture du JSON
- [DofusDB](https://dofusdb.fr) - données des ressources, API de la chasse au trésor et recettes des métiers
- [Metamob](https://www.metamob.fr) - API de suivi des quêtes Ocre

## 📄 Licence

Ce logiciel est sous [Unlicense](https://unlicense.org/), dont les termes sont disponibles dans [LICENSE](LICENSE).

Les composants tiers gardent leur propre licence : MinHook (BSD 2-Clause, `third_party/minhook`), nlohmann/json (MIT, `third_party/nlohmann`) et le SDK WebView2 de Microsoft (`third_party/webview2/LICENSE.txt`). Les textes de MinHook et nlohmann/json figurent aussi à la fin de `LISEZ-MOI.txt`.

L’Unlicense ne couvre que le code écrit pour Dofus Enhanced : ni les composants tiers ci-dessus, ni les données de ressources issues de DofusDB (`resource-map-generated.hpp`, voir [RESOURCE-DATA.md](RESOURCE-DATA.md)), ni Dofus et ses contenus, qui restent la propriété d’Ankama.

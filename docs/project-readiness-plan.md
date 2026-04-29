# Plan de préparation et feuille de route

Ce document rassemble les réflexions pratiques avant de transformer le starter kit WavePreview for Explorer en extension Windows réellement utilisable. L'objectif est de clarifier ce qu'il faut installer, les risques attendus, puis l'ordre de travail recommandé.

## 1. Ce qu'il faut installer ou vérifier

### Obligatoire

- Windows 10 ou 11 x64.
- Visual Studio 2022 avec le workload `Desktop development with C++`.
- Windows SDK récent, installé via Visual Studio.
- CMake.
- Git pour Windows.
- PowerShell 7 conseillé, même si Windows PowerShell peut suffire pour les scripts simples.
- vcpkg, idéalement dans `C:\dev\vcpkg` ou via `VCPKG_ROOT`.

Commandes prévues par le projet :

```powershell
./scripts/bootstrap-dev.ps1
./scripts/configure.ps1
./scripts/build.ps1
./scripts/test.ps1
```

Etat observé localement :

- `cmake` est disponible.
- `vcpkg` n'est pas encore trouvé dans `C:\dev\vcpkg`.
- `VCPKG_ROOT` n'est pas défini.
- Git local a un problème d'authentification HTTPS vers GitHub dans cette session.

### Fortement recommandé

- GitHub CLI (`gh`) pour simplifier l'authentification GitHub.
- Visual Studio Installer accessible pour ajouter des composants C++/SDK si CMake échoue.
- Un environnement de test séparé, idéalement une VM Windows, pour enregistrer la DLL shell sans risquer la machine principale.
- Sysinternals Process Explorer pour observer Explorer, les DLL chargées et les handles.
- Sysinternals DebugView ou un équivalent si on ajoute des traces debug.
- Windows Event Viewer pour diagnostiquer les crashs Explorer.

### Plus tard

- WiX Toolset pour l'installateur MSI.
- Outils de profiling Windows si les miniatures ou la preview deviennent lentes.
- Audios de test WAV générés par script, plutôt que de stocker de gros fichiers dans Git.

## 2. Dépendances prévues

Le manifest `vcpkg.json` déclare :

- `sqlite3` pour le cache.
- `libsndfile` pour WAV, AIFF, FLAC et formats audio non-MP3 selon support.
- `miniaudio` pour une couche audio légère et éventuellement le décodage ou la lecture.
- `gtest` pour les tests unitaires.

Media Foundation, Direct2D, WIC, COM, Shell APIs et Win32 viennent du Windows SDK.

La stratégie saine est de ne pas ajouter d'autres dépendances tant que le MVP WAV n'est pas stable. Chaque dépendance dans une extension shell augmente le risque de chargement, de packaging et de crash dans Explorer.

## 3. Difficultés techniques attendues

### Shell extension COM

C'est la partie la plus risquée. Explorer charge notre DLL dans un contexte sensible. Une erreur mémoire, un blocage, une exception non maîtrisée ou une attente réseau peut dégrader Explorer.

Points difficiles :

- implémenter correctement `IUnknown`, `IClassFactory`, `IPreviewHandler`, `IInitializeWithStream`, `IObjectWithSite`, `IOleWindow`;
- gérer le cycle de vie COM sans fuite et sans double release;
- créer une fenêtre enfant Win32 robuste;
- répondre correctement à `SetWindow`, `SetRect`, `DoPreview`, `Unload`, `TranslateAccelerator`;
- ne jamais bloquer un callback Explorer;
- éviter les dépendances qui initialisent trop lourdement au chargement de la DLL.

Conclusion : il faut d'abord stabiliser le moteur audio hors Explorer, puis intégrer progressivement le shell.

### Décodage WAV

Le code actuel lit seulement les métadonnées RIFF/WAVE de base et génère une waveform synthétique. La vraie étape suivante est de lire les samples PCM.

Cas à gérer :

- fichiers vides ou tronqués;
- chunks RIFF dans un ordre variable;
- chunks inconnus;
- tailles de chunk impaires;
- PCM 8/16/24/32 bits;
- float 32 bits;
- mono/stéréo, voire plus de deux canaux;
- valeurs incohérentes dans `fmt`;
- fichiers très gros;
- chemins Unicode;
- fichiers verrouillés ou supprimés pendant la lecture.

La lecture waveform ne doit pas forcément décoder tout le fichier pour le MVP. Une lecture échantillonnée ou bornée peut suffire pour éviter les latences.

### Rendu waveform

Le calcul min/max est simple, mais le rendu dans Explorer doit rester rapide et lisible.

Risques :

- mauvaise gestion DPI;
- rendu flou ou trop fin;
- flicker Win32;
- incompatibilité thème clair/sombre;
- allocation GDI/Direct2D mal libérée;
- rendu trop lent dans le Thumbnail Provider.

Pour le MVP, mieux vaut séparer :

- génération `WaveformData` testable hors UI;
- rendu Direct2D isolé;
- intégration Preview Handler ensuite.

### Lecture audio

La lecture dans Explorer est plus délicate qu'elle n'a l'air.

Risques :

- initialisation audio lente;
- conflit entre plusieurs previews;
- changement rapide de fichier;
- arrêt incomplet lors de `Unload`;
- callbacks audio encore actifs après destruction;
- latence ou crash si fichier supprimé.

Pour le MVP, le bouton lecture/pause peut venir après l'affichage metadata + waveform. Il ne faut pas démarrer par la lecture.

### Cache

Le cache est nécessaire pour les thumbnails et utile pour la preview, mais il ne doit jamais être critique.

Risques :

- verrou SQLite;
- corruption ou version incompatible;
- chemin réseau lent;
- cache trop gros;
- invalidation incorrecte;
- accès concurrent depuis plusieurs processus Explorer.

Approche recommandée :

- d'abord pas de cache pour le MVP Preview Handler;
- ensuite cache simple pour waveform;
- enfin politique de nettoyage.

### Compatibilité Windows

Risques :

- différences Windows 10 / Windows 11;
- DPI scaling;
- dark mode;
- Preview Pane désactivé ou non visible;
- associations de fichiers déjà prises;
- droits admin pour l'enregistrement COM;
- registre 64-bit vs 32-bit;
- SmartScreen ou antivirus sur DLL non signée;
- codecs système variables, surtout pour MP3/AAC si on utilise Media Foundation.

Le projet cible clairement x64. Il faut éviter de supporter x86 au début.

## 4. Stabilité : règles de développement

- Aucun scan récursif automatique.
- Aucune télémétrie.
- Aucune analyse lourde dans un callback COM synchrone.
- Timeouts courts.
- Annulation immédiate lors du changement de fichier.
- Aucune exception C++ ne doit traverser la frontière COM.
- Tous les handles Win32, COM, GDI et Direct2D doivent être RAII.
- Tout fichier est considéré corrompu jusqu'à preuve du contraire.
- Les erreurs doivent produire une preview minimale ou un message discret, pas un crash.

## 5. Plan d'action étape par étape

### Etape 0 - Préparer l'environnement

Objectif : pouvoir compiler localement.

Actions :

1. Corriger l'auth GitHub locale si nécessaire.
2. Installer ou vérifier Visual Studio 2022 C++ + Windows SDK.
3. Installer vcpkg avec `scripts/bootstrap-dev.ps1`.
4. Configurer avec `scripts/configure.ps1`.
5. Compiler avec `scripts/build.ps1`.
6. Lancer `scripts/test.ps1`.

Critère de fin : le projet compile en Debug x64 et les tests existants passent.

### Etape 1 - Nettoyer le socle CMake et tests

Objectif : avoir une base fiable avant d'ajouter du code.

Actions :

1. S'assurer que les includes fonctionnent proprement sur MSVC.
2. Ajouter des tests unitaires pour `WaveformGenerator`.
3. Ajouter une petite fabrique de WAV de test dans les tests, sans fichiers audio lourds.
4. Vérifier que GitHub Actions compile.

Critère de fin : CI verte ou au minimum build local reproductible.

### Etape 2 - Implémenter le parseur WAV robuste

Objectif : remplacer la waveform synthétique par une waveform issue des vrais samples.

Actions :

1. Renforcer `parseRiff`.
2. Valider `audioFormat`, `channels`, `sampleRate`, `bitsPerSample`, `blockAlign`.
3. Stocker l'offset et la taille du chunk `data`.
4. Décoder PCM 16 bits en premier.
5. Ajouter PCM 24/32 bits puis float 32.
6. Normaliser vers `float [-1.0, 1.0]`.
7. Générer min/max sans charger inutilement les gros fichiers.
8. Tester fichiers invalides, tronqués, vides, mono/stéréo.

Critère de fin : `waveform-test-cli` affiche metadata + points waveform réels pour un WAV PCM.

### Etape 3 - Stabiliser l'API AudioEngine

Objectif : exposer une interface interne propre avant le Shell.

Actions :

1. Définir les erreurs utiles dans `Result`.
2. Séparer lecture metadata et lecture waveform.
3. Ajouter limites de temps ou de taille.
4. Ajouter options de génération waveform : nombre de points, mono mixdown, stéréo.
5. Couvrir les chemins Unicode.

Critère de fin : AudioEngine testable sans Explorer.

### Etape 4 - Rendu waveform hors Shell

Objectif : dessiner la waveform sans encore intégrer COM.

Actions :

1. Créer un composant de rendu Direct2D ou GDI minimal.
2. Générer une bitmap ou surface à partir de `WaveformData`.
3. Tester rendu clair/sombre et différentes tailles.
4. Prévoir thumbnails simples plus tard.

Critère de fin : un rendu waveform peut être produit hors Explorer.

### Etape 5 - Preview Handler minimal

Objectif : charger une DLL COM qui affiche une UI minimale sans crash.

Actions :

1. Implémenter `IClassFactory`.
2. Implémenter `IPreviewHandler`.
3. Implémenter `IInitializeWithStream` ou `IInitializeWithFile` temporairement si plus simple.
4. Créer une fenêtre enfant Win32.
5. Afficher seulement texte metadata au début.
6. Ajouter waveform ensuite.
7. Tester dans une VM ou machine non critique.

Critère de fin : Explorer affiche une preview WAV sans lecture audio et sans freeze.

### Etape 6 - Threading et annulation

Objectif : rendre la preview sûre lors de navigation rapide.

Actions :

1. Déplacer décodage et waveform sur worker thread.
2. Ajouter cancellation token ou génération idempotente par fichier.
3. Empêcher toute mise à jour UI après `Unload`.
4. Tester navigation rapide dans un dossier de WAV.

Critère de fin : changement rapide de fichier sans crash ni blocage.

### Etape 7 - Lecture audio

Objectif : ajouter play/pause seulement après stabilité visuelle.

Actions :

1. Choisir Media Foundation ou miniaudio pour la lecture MVP.
2. Charger le fichier sans bloquer l'UI.
3. Gérer play, pause, stop, unload.
4. Interrompre immédiatement au changement de fichier.
5. Ajouter progression simple.

Critère de fin : lecture/pause stable pour WAV PCM.

### Etape 8 - Cache

Objectif : accélérer les recalculs, préparer les thumbnails.

Actions :

1. Implémenter SQLite minimal.
2. Stocker metadata et emplacement blob waveform.
3. Implémenter format binaire `WVPR`.
4. Invalidation path + size + mtime, puis hash partiel.
5. Rendre le cache optionnel et non bloquant.

Critère de fin : preview fonctionne même si cache inaccessible.

### Etape 9 - Thumbnail Provider

Objectif : afficher des miniatures utiles sans ralentir Explorer.

Actions :

1. Implémenter `IThumbnailProvider`.
2. Utiliser le cache prioritairement.
3. Retourner une image fallback en cas d'erreur.
4. Limiter fortement le temps de génération.

Critère de fin : dossiers WAV affichent des thumbnails sans freeze.

### Etape 10 - Installateur et désinstallation

Objectif : installation propre.

Actions :

1. Stabiliser GUID et registre.
2. Finaliser `DllRegisterServer` / `DllUnregisterServer` ou utiliser MSI registry entries.
3. Créer MSI WiX.
4. Tester install/uninstall.
5. Documenter les étapes manuelles de secours.

Critère de fin : installation et désinstallation sans pollution registre.

## 6. Par quoi commencer

La première vraie action doit être l'environnement de build, pas le code Shell.

Ordre immédiat recommandé :

1. Installer ou réparer vcpkg.
2. Configurer CMake.
3. Compiler le scaffold.
4. Lancer les tests.
5. Ensuite seulement, implémenter la vraie lecture waveform WAV PCM 16 bits.

Le premier chantier de code recommandé est donc :

`src/AudioEngine/Decoders/WavDecoder.cpp`

Raison : c'est testable hors Explorer, utile au CLI, nécessaire au Preview Handler et beaucoup moins risqué que de commencer par COM.


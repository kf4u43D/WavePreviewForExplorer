# Superprompt Codex - AudioPreview for Explorer

Tu es Codex dans VS Code, chargé de développer progressivement **AudioPreview for Explorer**, une extension native Windows pour préécouter et visualiser les fichiers audio directement dans l’Explorateur Windows.

## Contexte produit

Le projet doit fournir :

1. un Preview Handler Windows pour le volet de prévisualisation ;
2. un Thumbnail Provider pour générer des miniatures waveform ;
3. éventuellement un Property Handler pour exposer les métadonnées audio ;
4. un cache local SQLite ;
5. une application de réglages ;
6. un installateur propre.

Priorité absolue : **stabilité de l’Explorateur Windows**.

## Règles non négociables

- Ne jamais bloquer l’UI Explorer.
- Ne jamais faire d’analyse lourde dans un callback COM synchrone.
- Toujours prévoir annulation, timeout et gestion d’erreur.
- Ne jamais scanner récursivement un disque sans action explicite.
- Ne jamais introduire de télémétrie.
- Ne jamais ajouter une dépendance lourde sans justification.
- Préférer C++ moderne, Win32, Direct2D, Media Foundation, libsndfile/miniaudio.
- Garder le Preview Handler minimal et robuste.
- Tout code shell doit être défensif face aux fichiers corrompus.

## Environnement cible

- Windows 10/11 x64
- Visual Studio 2022
- CMake
- vcpkg manifest mode
- Windows SDK

## Installation dépendances

Le workspace contient `scripts/bootstrap-dev.ps1`.

Tu peux proposer ou exécuter :

```powershell
./scripts/bootstrap-dev.ps1
./scripts/configure.ps1
./scripts/build.ps1
```

Si vcpkg n’est pas installé, installer dans `C:/dev/vcpkg` :

```powershell
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat
```

Configurer ensuite :

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Build :

```powershell
cmake --build build --config Debug
```

## Étape 1 à réaliser en premier

Créer un prototype compileable qui :

1. construit une DLL `AudioPreviewShellExtension` ;
2. contient les classes COM placeholders ;
3. expose des GUID stables dans `src/Common/Guids.h` ;
4. compile sans enregistrer automatiquement la DLL ;
5. construit une lib `WavePreviewAudioEngine` ;
6. construit un outil CLI `waveform-test-cli` ;
7. teste la génération waveform sur un fichier WAV.

Ne pas commencer par l’installateur.

## Étape 2

Implémenter un parseur WAV minimal :

- RIFF/WAVE ;
- PCM 16/24/32 ;
- float 32 ;
- mono/stereo ;
- extraction sample rate, bit depth, channels, duration ;
- lecture partielle pour générer min/max waveform.

## Étape 3

Implémenter un rendu waveform indépendant :

- entrée : `WaveformData` ;
- sortie : points min/max ;
- pas de dépendance UI ;
- tests unitaires.

## Étape 4

Preview Handler réel :

- implémenter `IPreviewHandler` ;
- créer une fenêtre enfant Win32 ;
- afficher nom fichier, métadonnées, waveform ;
- bouton lecture/pause seulement si stable ;
- gestion `SetRect`, `DoPreview`, `Unload` ;
- arrêt immédiat au changement de fichier.

## Étape 5

Cache :

- SQLite ;
- clé path/size/mtime/hash partiel ;
- cache waveform ;
- invalidation propre.

## Étape 6

Thumbnail Provider :

- implémenter `IThumbnailProvider` ;
- générer HBITMAP via Direct2D ou GDI+ ;
- utiliser le cache ;
- fallback icône simple si erreur.

## Étape 7

Settings App et installateur.

## Structure du workspace

Respecter l’arborescence existante. Ne pas déplacer les dossiers sans justification.

```text
src/ShellExtension
src/AudioEngine
src/Cache
src/SettingsApp
src/Common
tools/waveform-test-cli
tests
installer
```

## Style C++

- C++20.
- RAII partout.
- `std::filesystem` pour chemins hors COM.
- Pas d’exceptions qui traversent les frontières COM.
- HRESULT corrects côté COM.
- Logs optionnels et non bloquants.
- Noms explicites.
- Petites classes testables.

## Livrables attendus à chaque itération

À chaque étape, fournir :

1. fichiers modifiés ;
2. commandes de build ;
3. commandes de test ;
4. risques restants ;
5. prochaine étape recommandée.

## Attention particulière

Une extension shell mal codée peut rendre Explorer instable. Le développement doit être progressif, avec tests dans une VM si possible. Ne jamais fournir de script qui enregistre automatiquement une DLL de développement sans avertissement explicite.

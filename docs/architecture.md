# Architecture technique

Le projet expose deux providers COM : un volet d'aperçu et un générateur de
miniatures WAV. L'enregistrement est assuré par l'installateur natif, avec des
scripts de développement utilisant le même moteur.

## Décodage

`AudioEngine/Decoders/WavDecoder` partage la validation RIFF et le décodage entre
les entrées fichier et mémoire. Il produit métadonnées, waveform min/max et
échantillons PCM16 pour le lecteur. Les entrées fichier sont lues par blocs.
Les bornes des chunks, les formats et les calculs de tailles sont vérifiés avant
l'allocation et l'accès aux échantillons. L'annulation utilise un stop_token.

## Aperçu COM

Initialize conserve la source sans lire ni convertir tout le fichier. DoPreview
crée une fenêtre Win32 puis lance un travail dans le pool Windows. Les IStream
sont transférés entre appartements COM par marshaling ; leurs lectures sont
sérialisées pour préserver le curseur lors d'une annulation et réutilisation.

Chaque travail conserve un état indépendant et une référence au module DLL.
Le volet récupère les résultats sur son thread via un timer. Unload demande
l'annulation et abandonne l'état, sans attendre le worker ; aucun worker ne
conserve de pointeur vers le volet ni de HWND. Deux travaux au plus décodent en
parallèle. Une lecture externe déjà bloquée ne peut pas toujours être interrompue.

La classe de fenêtre utilise le HINSTANCE de la DLL, empêche son déchargement
pendant son existence et est désinscrite après fermeture de sa dernière fenêtre.

## Audio et rendu

La conversion PCM16 est déclenchée à la demande, ou par l'auto-play. Chaque volet
possède son périphérique waveOut et attend sa fin effective ; il libère les buffers
après arrêt du périphérique. L'entrée et la sortie de lecture tamponnée restent
plafonnées à 256 Mio.

Le rendu actuel utilise une bitmap RGB et GDI/Win32. Le thème sombre, la mise à
l'échelle DPI complète et l'accessibilité du bouton dessiné restent des évolutions.

## Cache et installation

Le cache binaire optionnel est décrit dans [cache-format.md](cache-format.md).
Il sert les fichiers avec un chemin stable et ignore les données invalides.

L'installateur conserve les valeurs antérieures des associations et leurs clés
exactes. Il les restaure uniquement si elles appartiennent toujours à AudioPreview.
Les tests d'installation utilisent des racines de registre isolées.
Voir [installer.md](installer.md) et [testing-plan.md](testing-plan.md).

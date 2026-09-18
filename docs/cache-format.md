# Cache waveform

Le cache utilisé est `WaveformStore`, un ensemble de fichiers binaires dans
`FOLDERID_LocalAppDataLow/AudioPreviewForExplorer/Waveforms`. Il reste facultatif :
un échec de lecture ou d'écriture entraîne un recalcul. Le module SQLite historique
n'est pas utilisé par ce cache.

## Format version 1

Chaque fichier `wpv-<empreinte>.wpc` contient, en little-endian :

- signature de format de 8 octets : `WPVC0001` ;
- taille de la clé sur 32 bits ;
- nombre de points sur 32 bits ;
- nombre de canaux sur 32 bits, actuellement 1 ;
- clé complète : chemin canonique UTF-8, terminateur nul, résolution sur 32 bits,
  taille du fichier et date de modification sur 64 bits ;
- couples min/max float32, normalisés entre -1 et 1.

L'empreinte du nom de fichier accélère la recherche ; la clé complète est comparée
avant utilisation pour détecter les collisions. La longueur, la version, le nombre
de points et la validité des amplitudes sont vérifiés avant de rendre les données.

## Publication et invalidation

Le même objet `WaveformStore` doit être utilisé pour `Load(source, points)`, le
décodage puis `Save(source, points, waveform)`. Load mémorise la signature même en
cas de cache absent. Save refuse la publication si la source a changé depuis.

L'écriture passe par un fichier temporaire unique, fermé avant son remplacement
atomique. Aucun fichier source n'est modifié. Les flux COM sans chemin stable ne
sont pas conservés dans ce cache.

## Limites

Le nettoyage retire les entrées les moins récemment utilisées pour conserver au
plus 512 fichiers et 64 Mio. Le parcours du dossier est plafonné à 4096 éléments ;
un dossier excessif rend le cache indisponible sans empêcher le décodage.
Les fichiers étrangers au format de nom du cache sont conservés.

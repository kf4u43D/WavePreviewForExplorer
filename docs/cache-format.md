# Format du cache

## 1. Objectif

Le cache évite de relire et recalculer les waveforms pour chaque affichage dans Explorer.

## 2. Tables SQLite proposées

```sql
CREATE TABLE files (
  id INTEGER PRIMARY KEY,
  path TEXT NOT NULL,
  file_size INTEGER NOT NULL,
  last_write_time_utc INTEGER NOT NULL,
  partial_hash TEXT,
  codec TEXT,
  duration_ms INTEGER,
  sample_rate INTEGER,
  bit_depth INTEGER,
  channels INTEGER,
  analyzed_at_utc INTEGER NOT NULL,
  analyzer_version INTEGER NOT NULL
);

CREATE TABLE waveforms (
  file_id INTEGER NOT NULL,
  resolution INTEGER NOT NULL,
  channel_mode TEXT NOT NULL,
  blob_path TEXT NOT NULL,
  created_at_utc INTEGER NOT NULL,
  PRIMARY KEY(file_id, resolution, channel_mode)
);
```

## 3. Données waveform

Format binaire simple :

```text
magic: WVPR
version: uint32
channels: uint32
point_count: uint32
layout: minmax-interleaved
points: float min, float max, ...
```

Les amplitudes sont normalisées entre -1.0 et +1.0.

## 4. Invalidation

Un cache est invalide si :

- chemin différent ;
- taille différente ;
- date de modification différente ;
- hash partiel différent ;
- version d’analyse incompatible.

## 5. Politique de taille

Options :

- limite par défaut : 512 Mo ;
- suppression LRU ;
- possibilité de vider depuis Settings App.

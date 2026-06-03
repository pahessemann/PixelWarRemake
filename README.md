# PixelWarRemake

![Interface PixelWarRemake](docs/pixelwar-demo.png)

Serveur Web C++20 pour une carte de pixels persistante. Les utilisateurs peuvent s'inscrire, se connecter, poser un pixel toutes les 10 minutes, lire la carte complete ou recuperer un diff depuis une sequence connue.

## Fonctionnalites

- Serveur HTTP self-contained en C++20 avec thread pool.
- API REST JSON: `/register`, `/login`, `/map`, `/pixel`, `/cooldown`.
- Sessions par token Bearer avec expiration.
- Hashage des mots de passe PBKDF2-HMAC-SHA256 avec sel aleatoire.
- Cooldown strict cote serveur.
- Pixel map en memoire protegee par `std::shared_mutex`.
- Persistance binaire de la map avec encodage RLE.
- Cache en memoire de la derniere map compressee.
- Rate limiting simple pour login, register et placement de pixels.
- Documentation OpenAPI dans `docs/openapi.yaml`.
- Frontend web servi par le binaire C++: canvas pixel map, auth, palette, cooldown, zoom et refresh automatique.

## Build

Prerequis:

- CMake 3.20+
- Compilateur C++20

Windows PowerShell:

```powershell
.\scripts\build.cmd
.\scripts\test.cmd
.\scripts\run.cmd
```

Ouvrir ensuite `http://127.0.0.1:8080/` dans le navigateur.

Commande portable:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/pixelwar_server config/server.example.json
```

## Configuration

Copier `config/server.example.json` vers `config/server.json`, puis ajuster:

```json
{
  "host": "0.0.0.0",
  "port": 8080,
  "map_width": 1000,
  "map_height": 1000,
  "palette_size": 16,
  "cooldown_seconds": 600,
  "thread_pool_size": 8,
  "data_dir": "data"
}
```

## Exemples API

```bash
curl -X POST http://localhost:8080/register \
  -H "Content-Type: application/json" \
  -d '{"username":"paul","password":"motdepasse-solide"}'

TOKEN=$(curl -s -X POST http://localhost:8080/login \
  -H "Content-Type: application/json" \
  -d '{"username":"paul","password":"motdepasse-solide"}' | jq -r .token)

curl http://localhost:8080/map

curl -X POST http://localhost:8080/pixel \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"x":10,"y":20,"color":3}'
```

## Payload `/map`

La route renvoie soit une carte complete:

```json
{
  "type": "full",
  "width": 1000,
  "height": 1000,
  "sequence": 42,
  "encoding": "rle-base64",
  "palette_size": 16,
  "data": "..."
}
```

Soit un diff si `GET /map?since=41` peut etre satisfait depuis l'historique en memoire:

```json
{
  "type": "diff",
  "width": 1000,
  "height": 1000,
  "sequence": 42,
  "changes": [{"seq":42,"x":10,"y":20,"color":3}]
}
```

## Frontend

Le dossier `public/` contient l'interface web servie par le serveur C++:

- `index.html`: structure de l'application.
- `styles.css`: interface responsive.
- `app.js`: auth, rendu canvas, decode RLE, diffs, cooldown et pose de pixel.

Le navigateur appelle `/map` au chargement puis toutes les 60 secondes. Les clics sur le canvas envoient `POST /pixel` avec le token Bearer courant.

## Tests

Le projet cherche Catch2 v3 si disponible. Sinon, un mini runner compatible avec les macros utilisees dans `tests/test_core.cpp` permet de compiler les tests sans dependance externe.

```bash
cmake -S . -B build -DPIXELWAR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Les scenarios de charge et securite sont detailles dans `docs/testing.md`.

## Workflow Git recommande

```bash
git config user.name "Paul Hessemann"
git config user.email "ton_email"

git checkout -b dev
git checkout -b feature/pixel-api
git commit -m "[feature] Ajout endpoint /pixel"
```

Branches:

- `main`: stable
- `dev`: developpement
- `feature/*`: nouvelles fonctionnalites

# PixelWarRemake

![Interface PixelWarRemake](docs/pixelwar-demo.png)

Serveur web C++20 pour une carte de pixels persistante. Les utilisateurs creent un compte local avec pseudo, email et mot de passe, puis posent des pixels sous quota/cooldown. Le frontend est servi directement par le binaire C++.

## Fonctionnalites

- Serveur HTTP self-contained en C++20 avec thread pool.
- API REST JSON: `/register`, `/verify-email`, `/login`, `/map`, `/events`, `/history`, `/pixel`, `/cooldown`.
- Comptes locaux avec pseudo unique, email unique, verification email et mot de passe hashe.
- Login possible avec le pseudo ou l'email.
- Sessions par token Bearer avec expiration.
- Cooldown strict cote serveur: 3 pixels par fenetre de 10 minutes par defaut.
- Pixel map en memoire protegee par `std::shared_mutex`.
- Persistance binaire de la map avec encodage RLE.
- Cache en memoire de la derniere map compressee.
- Rate limiting et verrouillage temporaire apres trop d'echecs de login.
- Frontend web: canvas pixel map, palette, cooldown, zoom, deplacement, mini-map, historique et flux temps reel SSE.
- Panel administrateur cache sur `/gestion`, protege par token Bearer et `admin_username`.
- Journal d'audit administrateur pour reset, rollback, backup et reset cooldown.
- Backups serveur horaires de la map, backups manuels, rollback et reset avec screenshot BMP final.
- Dockerfile et `docker-compose.yml` pour deploiement.
- Documentation OpenAPI dans `docs/openapi.yaml`.

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
  "pixel_quota_per_cooldown": 3,
  "session_ttl_seconds": 86400,
  "thread_pool_size": 8,
  "max_body_bytes": 8192,
  "admin_username": "pahessemann",
  "public_base_url": "http://127.0.0.1:8080",
  "require_email_verification": true,
  "expose_local_verification_link": true,
  "email_verification_ttl_seconds": 86400,
  "login_failure_limit": 5,
  "login_lock_seconds": 900,
  "data_dir": "data"
}
```

Le compte administrateur est le compte local dont le pseudo correspond a `admin_username`. Il faut donc creer ce compte depuis l'interface avant d'utiliser `/gestion`.

## Authentification Locale

Cette version n'utilise plus de fournisseur de connexion externe. Tout fonctionne en local:

1. L'utilisateur cree un compte avec `username`, `email` et `password`.
2. Le serveur verifie le format du pseudo, le format basique de l'email et la force minimale du mot de passe.
3. Le serveur refuse les pseudos et emails deja utilises.
4. Le mot de passe est stocke sous forme de hash PBKDF2-HMAC-SHA256 avec sel aleatoire.
5. Si `require_email_verification` est actif, le serveur cree un token de verification et ecrit le lien dans `data/email_outbox.txt`.
6. En local, `expose_local_verification_link` affiche aussi le lien dans l'interface pour pouvoir tester sans serveur mail.
7. `/login` renvoie un token de session Bearer uniquement apres validation de l'email.

Cette methode est volontairement pratique pour le developpement local. Elle force un passage par lien de verification, mais l'envoi reel du mail reste a brancher a un fournisseur transactionnel pour la production. Le guide `docs/deployment.md` decrit ce passage.

Les anciens comptes externes sans mot de passe local ne sont plus acceptes au chargement.

## Exemples API

```bash
curl -X POST http://localhost:8080/register \
  -H "Content-Type: application/json" \
  -d '{"username":"paul","email":"paul@example.test","password":"motdepasse-solide"}'

# En local, ouvrir le lien ecrit dans data/email_outbox.txt,
# ou le champ verification_link renvoye par /register.

curl -X POST http://localhost:8080/login \
  -H "Content-Type: application/json" \
  -d '{"login":"paul@example.test","password":"motdepasse-solide"}'

curl http://localhost:8080/map

TOKEN="..."

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

Le navigateur utilise aussi `GET /events?since=<sequence>` en SSE. Le serveur renvoie un evenement `pixels` contenant le meme format JSON que `/map`; le navigateur reconnecte automatiquement si la connexion se ferme.

## Frontend

Le dossier `public/` contient l'interface web servie par le serveur C++:

- `index.html`: structure de l'application.
- `admin.html`: panel de gestion accessible via `/gestion`.
- `styles.css`: interface responsive.
- `app.js`: auth locale, verification email, rendu canvas, mini-map, historique, SSE, cooldown et pose de pixel.
- `admin.js`: statistiques admin, liste utilisateurs, backups, audit, rollback, reset carte et reset cooldown.

Le navigateur appelle `/map` au chargement, ouvre `/events` pour recevoir les diffs en quasi temps reel, puis garde un refresh de securite toutes les 60 secondes. Les clics sur le canvas selectionnent une case; le bouton "Valider le pixel" envoie `POST /pixel` avec le token Bearer courant.

Le panel `/gestion` n'est pas lie depuis l'interface publique. Il utilise le token de session stocke par l'interface et refuse tout compte dont le pseudo ne correspond pas a `admin_username`.

Le serveur cree un backup de la map toutes les heures dans `data/backups`. Depuis `/gestion`, un administrateur peut creer un backup manuel, restaurer un backup, ou reset la carte. Avant chaque reset et rollback, un backup de securite est cree; le reset genere aussi un screenshot BMP de l'etat final avant remise a zero. Les actions sensibles sont ajoutees dans `data/audit.log`.

## Docker

```bash
docker compose up --build -d
```

Voir `docs/deployment.md` pour les volumes, l'outbox email local et les recommandations production.

## Tests

Le projet cherche Catch2 v3 si disponible. Sinon, un mini runner compatible avec les macros utilisees dans `tests/test_core.cpp` permet de compiler les tests sans dependance externe.

```bash
cmake -S . -B build -DPIXELWAR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Les scenarios de charge et securite sont detailles dans `docs/testing.md`.

## Idee: authentification en deux facteurs

Pour une version publique plus fiable contre les faux emails, l'evolution la plus simple serait d'ajouter une confirmation email obligatoire avant d'activer le compte: le serveur genere un token aleatoire, envoie un lien de validation, puis refuse `/login` tant que `email_verified` reste faux.

Une seconde etape serait l'authentification en deux facteurs, d'abord pour les administrateurs puis pour tous les comptes: TOTP compatible Google Authenticator/Authy, QR code d'activation, codes de secours a usage unique, et verification du code apres le mot de passe avant de creer la session Bearer.

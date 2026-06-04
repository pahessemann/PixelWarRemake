# PixelWarRemake

![Interface PixelWarRemake](docs/pixelwar-demo.png)

Serveur Web C++20 pour une carte de pixels persistante. Les utilisateurs se connectent avec un fournisseur OpenID Connect dont l'email est verifie, posent des pixels sous quota/cooldown, lisent la carte complete ou recuperent un diff depuis une sequence connue.

## Fonctionnalites

- Serveur HTTP self-contained en C++20 avec thread pool.
- API REST JSON: `/auth/status`, `/auth/login`, `/map`, `/pixel`, `/cooldown`.
- Creation de compte uniquement via OpenID Connect avec `email_verified=true`.
- Sessions par token Bearer avec expiration.
- Aucun mot de passe local stocke; les comptes legacy sans identite externe valide sont ignores au chargement.
- Cooldown strict cote serveur.
- Pixel map en memoire protegee par `std::shared_mutex`.
- Persistance binaire de la map avec encodage RLE.
- Cache en memoire de la derniere map compressee.
- Rate limiting simple pour le placement de pixels.
- Documentation OpenAPI dans `docs/openapi.yaml`.
- Frontend web servi par le binaire C++: canvas pixel map, auth, palette, cooldown, zoom et refresh automatique.
- Panel administrateur cache sur `/gestion`, protege par token Bearer et `admin_oidc_subject`.
- Backups serveur horaires de la map, backups manuels, rollback et reset avec screenshot BMP final.

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
  "admin_oidc_subject": "",
  "public_base_url": "http://127.0.0.1:8080",
  "oidc_provider_name": "Google",
  "oidc_authorization_endpoint": "https://accounts.google.com/o/oauth2/v2/auth",
  "oidc_token_endpoint": "https://oauth2.googleapis.com/token",
  "oidc_userinfo_endpoint": "https://openidconnect.googleapis.com/v1/userinfo",
  "oidc_client_id": "",
  "oidc_client_secret": "",
  "oidc_redirect_path": "/auth/callback",
  "data_dir": "data"
}
```

### Authentification fiable sans faux emails

La methode retenue est OpenID Connect cote serveur:

1. Le navigateur part sur `/auth/login`.
2. Le serveur redirige vers le fournisseur OIDC configure, par defaut Google.
3. Le serveur echange le `code` OAuth contre un access token.
4. Le serveur appelle l'endpoint `userinfo`.
5. Le compte local est cree uniquement si la reponse contient:
   - `sub` non vide, utilise comme identifiant stable du compte;
   - `email` non vide;
   - `email_verified=true`.

Ce que cette methode garantit: un utilisateur ne peut pas creer un compte PixelWar avec une simple adresse inventee ou non verifiee. Le serveur refuse tout fournisseur qui ne renvoie pas explicitement `email_verified=true`.

Ce que cette methode ne garantit pas: une personne physique unique au sens strict. Une meme personne peut posseder plusieurs comptes Google ou plusieurs emails verifies. Pour une unicite humaine forte, il faudrait ajouter une verification d'identite/KYC externe payante, par exemple Stripe Identity, Persona, Veriff ou equivalent, puis stocker un identifiant de verification unique.

References:

- OpenID Connect Core 1.0 definit l'endpoint UserInfo et les claims standards: `https://openid.net/specs/openid-connect-core-1_0-18.html`
- Google OpenID Connect expose `sub`, `email` et `email_verified`, et recommande d'utiliser `sub` plutot que l'email comme identifiant unique: `https://developers.google.com/identity/openid-connect/openid-connect`

Pour activer l'auth Google/OIDC:

1. Creer un client OAuth/OIDC dans le fournisseur choisi.
2. Ajouter l'URL de redirection exacte: `http://127.0.0.1:8080/auth/callback` en local.
3. Generer la config locale ignoree par Git:

```powershell
.\scripts\configure-oidc.ps1 `
  -ClientId "TON_CLIENT_ID" `
  -ClientSecret "TON_CLIENT_SECRET" `
  -AdminSubject "TON_SUB_ADMIN"
```

4. Relancer avec `.\scripts\run.cmd`. Le script utilise `config/server.json` automatiquement s'il existe.

Tu peux aussi renseigner les champs `oidc_*` dans `config/server.json`, ou utiliser les variables d'environnement `PIXELWAR_OIDC_CLIENT_ID`, `PIXELWAR_OIDC_CLIENT_SECRET`, `PIXELWAR_OIDC_AUTHORIZATION_ENDPOINT`, `PIXELWAR_OIDC_TOKEN_ENDPOINT`, `PIXELWAR_OIDC_USERINFO_ENDPOINT` et `PIXELWAR_ADMIN_OIDC_SUBJECT`.

Les routes `POST /register` et `POST /login` repondent `410` volontairement: les comptes ne sont plus crees par mot de passe.
Au demarrage, les anciennes entrees password-only de `data/users.db` ne sont pas chargees. Seuls les comptes crees par une identite externe verifiee restent utilisables.

## Exemples API

```bash
curl http://localhost:8080/map

# Apres connexion OIDC dans le navigateur, recuperer le token de session PixelWarRemake.
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

## Frontend

Le dossier `public/` contient l'interface web servie par le serveur C++:

- `index.html`: structure de l'application.
- `admin.html`: panel de gestion accessible via `/gestion`.
- `styles.css`: interface responsive.
- `app.js`: auth OIDC avec email verifie, rendu canvas, decode RLE, diffs, cooldown et pose de pixel.
- `admin.js`: statistiques admin, liste utilisateurs, backups, rollback, reset carte et reset cooldown.

Le navigateur appelle `/map` au chargement puis toutes les 60 secondes. Les clics sur le canvas envoient `POST /pixel` avec le token Bearer courant.

Le panel `/gestion` n'est pas lie depuis l'interface publique. Il utilise le token de session stocke par l'interface et refuse tout compte qui ne correspond pas a `admin_oidc_subject`. Si `admin_oidc_subject` est vide, `admin_username` reste un fallback, mais seulement pour un compte OIDC charge.

Le serveur cree un backup de la map toutes les heures dans `data/backups`. Depuis `/gestion`, un administrateur peut creer un backup manuel, restaurer un backup, ou reset la carte. Avant chaque reset et rollback, un backup de securite est cree; le reset genere aussi un screenshot BMP de l'etat final avant remise a zero.

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

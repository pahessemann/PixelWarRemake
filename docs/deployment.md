# Deploiement

## Docker Compose

```bash
docker compose up --build -d
```

L'application ecoute ensuite sur `http://localhost:8080`.

Les donnees persistantes sont stockees dans le volume Docker `pixelwar-data`:

- `map.pwm`: carte courante;
- `users.db`: comptes locaux et etat de verification email;
- `email_outbox.txt`: liens de verification email en mode local;
- `pixel_history.log`: derniers pixels poses;
- `audit.log`: actions administrateur;
- `backups/`: sauvegardes et screenshots.

## Email en production

La version actuelle ecrit les liens de verification dans `data/email_outbox.txt`, ce qui est pratique en local. Pour un vrai deploiement public, il faut remplacer cet outbox par un fournisseur email transactionnel comme Mailgun, Resend, SendGrid ou Amazon SES, puis mettre `expose_local_verification_link` a `false`.

Le flux attendu reste le meme:

1. `/register` cree un compte non verifie.
2. Le serveur envoie un lien `/verify-email?token=...`.
3. `/login` reste refuse tant que l'email n'est pas valide.
4. Le lien expire apres `email_verification_ttl_seconds`.

## Securite

Le serveur inclut deja:

- hash de mot de passe PBKDF2-HMAC-SHA256;
- email unique;
- verification email obligatoire;
- rate limit sur register/login/pixel;
- verrouillage temporaire apres trop d'echecs de login;
- journal d'audit pour les actions admin;
- panel `/gestion` protege par le compte `admin_username`.

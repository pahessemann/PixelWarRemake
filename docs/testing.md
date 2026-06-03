# Tests

## Unitaires

```bash
cmake -S . -B build -DPIXELWAR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Le projet utilise Catch2 v3 si disponible. Sans Catch2, `tests/minicatch.hpp` fournit un runner minimal pour les tests du noyau.

## Charge

Avec k6:

```bash
k6 run scripts/k6-pixel-smoke.js
```

Avec wrk:

```bash
wrk -t4 -c100 -d30s http://localhost:8080/map
```

## Securite

Scenarios a verifier avant exposition publique:

- Brute force login: envoyer plus de 10 tentatives par minute depuis la meme IP et verifier `429 rate_limited`.
- Flood register: envoyer plus de 20 inscriptions par minute depuis la meme IP et verifier `429 rate_limited`.
- Flood pixel: envoyer plus de 120 poses par minute avec le meme token et verifier `429 rate_limited`.
- Injection payload: envoyer du JSON invalide, des coordonnees negatives, un `color` hors palette et des usernames avec espaces ou caracteres speciaux.
- Cooldown: poser deux pixels consecutifs avec le meme token et verifier `429 cooldown_active`.


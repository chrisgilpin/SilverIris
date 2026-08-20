# Remote testing on 007.goodhouseinc.com

This Hetzner box already terminates TLS with **nginx + Certbot** (same pattern as
`trackeditor.goodhouseinc.com`). Do not bind Vite to `0.0.0.0`. Nginx proxies `/` to `127.0.0.1:5173` and `/ws` + `/api` to `127.0.0.1:18787`.

The ROM file is selected in *your* browser. It is never uploaded through nginx.

## 1. DNS (you do this)

At Namecheap (or wherever `goodhouseinc.com` is hosted — nameservers are
`dns1/dns2.registrar-servers.com`), add:

| Type | Name | Value |
| --- | --- | --- |
| A | `007` | `49.12.213.190` |
| AAAA | `007` | `2a01:4f8:c015:496e::1` (optional) |

Same A record as `trackeditor.goodhouseinc.com`. Wait until:

```bash
dig +short 007.goodhouseinc.com
# must print 49.12.213.190
```

## 2. Enable the nginx site (on this box)

```bash
sudo ln -sf /home/grok/GoldenEye/services/nginx/007.goodhouseinc.com.conf \
  /etc/nginx/sites-enabled/007.goodhouseinc.com
sudo nginx -t && sudo systemctl reload nginx
sudo certbot --nginx -d 007.goodhouseinc.com
```

Firewall already allows HTTP/HTTPS (`Nginx Full`).

## 3. Persistent Vite + signal (systemd)

Vite and signal run as systemd units so they survive reboot and SSH disconnect.
Repo copies: `deploy/systemd/`. Installed: `/etc/systemd/system/`. Both
`User=grok`, `Restart=always`, localhost only (not 0.0.0.0).

- `silveriris-vite.service` — cwd `/home/grok/GoldenEye/web/shell`, bind `127.0.0.1:5173`
  `node ./node_modules/.bin/vite --host 127.0.0.1 --port 5173 --strictPort`
- `silveriris-signal.service` — cwd `/home/grok/GoldenEye/services/signal`, bind `127.0.0.1:18787`
  `HOST=127.0.0.1 PORT=18787 node src/server.js`

Enable: sudo systemctl enable --now silveriris-vite silveriris-signal
Restart: sudo systemctl restart silveriris-vite silveriris-signal
Nginx still proxies / to 5173 and /ws /api to 18787.

## 4. Production build instead of Vite HMR

```bash
cd /home/grok/GoldenEye/web/shell
npm run build
npm run preview
```

Change `proxy_pass` in the nginx file from `5173` to `4173`, then
`sudo nginx -t && sudo systemctl reload nginx`.

## 5. coturn STUN/TURN

System `coturn` (not Docker) on this box. Config source: `services/turn/turnserver.conf`.
Live file: `/etc/turnserver.conf` plus `static-auth-secret` from `/etc/silveriris/turn.env`.

| Port | Proto | Role |
| --- | --- | --- |
| 3478 | udp+tcp | STUN + TURN |
| 5349 | tcp | TURNS (same Let’s Encrypt cert as nginx) |
| 49152-49200 | udp+tcp | bounded TURN relay |

`silveriris-signal` mints ephemeral room-scoped REST creds (`expiry:room` + HMAC-SHA1)
on create/join. **No anonymous allocate.** Default ICE is `all`; `?ff_turnForce=1` is
optional. `wsRelay` stays the ICE-fail fallback. ufw allows only the ports above;
do not change 22/80/443.

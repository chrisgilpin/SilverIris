# Remote instance -- 007.goodhouseinc.com

Public URL: **https://007.goodhouseinc.com** (no access secret). Anyone who
opens it can drop their own NTSC-U cartridge dump and, with `?ff_netplay=1`,
create or join a room. The file is selected in *your* browser. It is never
uploaded through nginx.

This box already terminates TLS with **nginx + Certbot** (same pattern as
`trackeditor.goodhouseinc.com`). SilverIris is one vhost among others.
**Do not edit, disable, or reload other nginx vhosts** (`track-editor`,
`storykrafters-api`, anything that is not `007.goodhouseinc.com`).
Vite stays on localhost. Nginx proxies `/` to `127.0.0.1:5173` and `/ws` +
`/api` to `127.0.0.1:18787`.

## Restart (usual work)

Units are already enabled. After a shell or signal change:

```bash
sudo systemctl restart silveriris-vite silveriris-signal
sudo systemctl status silveriris-vite silveriris-signal --no-pager
```

Logs:

```bash
journalctl -u silveriris-vite -u silveriris-signal -n 80 --no-pager
```

coturn is the system `coturn` unit. Restart it only if you changed
`/etc/turnserver.conf` (do not). Do **not** edit `/etc/silveriris/turn.env`
or any TURN secret.

```bash
# read-only check -- do not restart unless the operator asked
systemctl is-active coturn
```

`nginx -t` + `reload` only if you are changing the **007** vhost. Never
`systemctl restart nginx` as a first move -- that is how you disrupt other
sites.

## Units

Vite and signal run as systemd so they survive reboot and SSH disconnect.
Repo copies: `deploy/systemd/`. Installed: `/etc/systemd/system/`. Both
`User=grok`, `Restart=always`, localhost only (not `0.0.0.0`).

| Unit | cwd | bind | command |
| --- | --- | --- | --- |
| `silveriris-vite.service` | `/home/grok/GoldenEye/web/shell` | `127.0.0.1:5173` | `node ./node_modules/.bin/vite --host 127.0.0.1 --port 5173 --strictPort` |
| `silveriris-signal.service` | `/home/grok/GoldenEye/services/signal` | `127.0.0.1:18787` | `HOST=127.0.0.1 PORT=18787 node src/server.js` |

First-time enable (already done on this box):

```bash
sudo systemctl enable --now silveriris-vite silveriris-signal
```

## coturn STUN/TURN

System `coturn` (not Docker) on this box. Config source:
`services/turn/turnserver.conf`. Live file: `/etc/turnserver.conf` plus
`static-auth-secret` from `/etc/silveriris/turn.env` -- **never commit or
print that secret**.

| Port | Proto | Role |
| --- | --- | --- |
| 3478 | udp+tcp | STUN + TURN |
| 5349 | tcp | TURNS (same Let's Encrypt cert as nginx) |
| 49152-49200 | udp+tcp | bounded TURN relay |

`silveriris-signal` mints ephemeral room-scoped REST creds (`expiry:room` +
HMAC-SHA1) on create/join. **No anonymous allocate.** Default ICE is `all`;
`?ff_turnForce=1` is optional. `wsRelay` stays the ICE-fail fallback. ufw
allows only the ports above; do not change 22/80/443.

## DNS (already pointed)

| Type | Name | Value |
| --- | --- | --- |
| A | `007` | `49.12.213.190` |
| AAAA | `007` | `2a01:4f8:c015:496e::1` (optional) |

```bash
dig +short 007.goodhouseinc.com
# must print 49.12.213.190
```

## nginx (already live -- do not redo)

TLS lives in `/etc/nginx/sites-available/007.goodhouseinc.com`
(Certbot-managed). The file in `services/nginx/` is a **template**. Do not
symlink the template over the live vhost (that drops the certificate
block). Do not re-issue the cert unless it is actually expired.

If you must change only the 007 proxy:

```bash
sudo nginx -t && sudo systemctl reload nginx
```

Firewall already allows HTTP/HTTPS (`Nginx Full`).


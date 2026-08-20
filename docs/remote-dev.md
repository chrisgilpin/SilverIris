# Remote testing on 007.goodhouseinc.com

This Hetzner box already terminates TLS with **nginx + Certbot** (same pattern as
`trackeditor.goodhouseinc.com`). Do not bind Vite to `0.0.0.0`. Nginx proxies
to `127.0.0.1:5173`.

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

## 3. Run the shell (localhost only)

```bash
cd /home/grok/GoldenEye/web/shell
npm ci
npm run dev
```

Vite listens on `127.0.0.1:5173`. Then open:

https://007.goodhouseinc.com/

Stop Vite and the public site goes 502 until you start it again. For a
longer-lived process, use `tmux` or `systemd`.

## 4. Production build instead of Vite HMR

```bash
cd /home/grok/GoldenEye/web/shell
npm run build
npm run preview
```

Change `proxy_pass` in the nginx file from `5173` to `4173`, then
`sudo nginx -t && sudo systemctl reload nginx`.

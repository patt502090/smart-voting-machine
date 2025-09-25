#!/usr/bin/env bash
# Ensure safe, idempotent script to run on the Odroid after a reboot.
# Usage: sudo bash ensure_odroid_startup.sh

set -u

TIMESTAMP=$(date +%s)
REDIRECT_CONF="/etc/nginx/sites-available/00-http-to-https-redirect"
REDIRECT_SYM="/etc/nginx/sites-enabled/00-http-to-https-redirect"
SMART_CONF="/etc/nginx/sites-available/smart-voting-machine"
BACKUP_DIR="/var/backups/nginx-$TIMESTAMP"

fail() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "-- $*"; }

if [ "$(id -u)" -ne 0 ]; then
  fail "This script must be run as root (use sudo)."
fi

mkdir -p "$BACKUP_DIR"

info "Ensuring HTTP->HTTPS redirect file exists: $REDIRECT_CONF"
cat > "$REDIRECT_CONF.tmp" <<'EOF'
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    server_name _;

    # Redirect all HTTP to HTTPS
    location / {
        return 301 https://$host$request_uri;
    }
}
EOF

if [ -f "$REDIRECT_CONF" ]; then
  if cmp -s "$REDIRECT_CONF.tmp" "$REDIRECT_CONF"; then
    info "Redirect config unchanged."
    rm -f "$REDIRECT_CONF.tmp"
  else
    info "Updating existing redirect config (backup created)."
    cp -a "$REDIRECT_CONF" "$BACKUP_DIR/00-http-to-https-redirect.bak.$TIMESTAMP"
    mv "$REDIRECT_CONF.tmp" "$REDIRECT_CONF"
  fi
else
  mv "$REDIRECT_CONF.tmp" "$REDIRECT_CONF"
  info "Wrote new redirect config."
fi

if [ ! -L "$REDIRECT_SYM" ]; then
  if [ -e "$REDIRECT_SYM" ]; then
    info "$REDIRECT_SYM exists but is not a symlink; moving to backup."
    mv "$REDIRECT_SYM" "$BACKUP_DIR/00-http-to-https-redirect.enabled.$TIMESTAMP"
  fi
  ln -s "$REDIRECT_CONF" "$REDIRECT_SYM" || fail "Failed to symlink redirect into sites-enabled"
  info "Enabled redirect site."
else
  info "Redirect symlink already enabled."
fi

# Fix smart-voting-machine site to remove any 'listen ...80' lines (idempotent)
if [ -f "$SMART_CONF" ]; then
  if grep -E "listen[[:space:]]+.*80" -q "$SMART_CONF"; then
    info "Found port-80 listener(s) in $SMART_CONF. Creating backup and removing them."
    cp -a "$SMART_CONF" "$BACKUP_DIR/smart-voting-machine.bak.$TIMESTAMP"
    # Remove lines that explicitly listen on port 80 (keeps other listen lines intact)
    sed '/^[[:space:]]*listen[[:space:]]\+.*80.*$/d' "$SMART_CONF" > "$SMART_CONF.new"

    # Replace file and test nginx; if test fails, restore backup and exit
    mv "$SMART_CONF" "$SMART_CONF.orig.$TIMESTAMP"
    mv "$SMART_CONF.new" "$SMART_CONF"
    if nginx -t >/dev/null 2>&1; then
      info "nginx config OK after editing $SMART_CONF."
    else
      info "nginx test failed after editing; restoring original."
      mv "$SMART_CONF.orig.$TIMESTAMP" "$SMART_CONF"
      nginx -t || fail "nginx config invalid even after restore. Manual intervention required."
    fi
  else
    info "No port-80 listeners found in $SMART_CONF."
  fi
else
  info "$SMART_CONF not present; skipping site fix."
fi

info "Testing nginx configuration..."
if nginx -t; then
  info "nginx test successful — reloading nginx."
  systemctl reload nginx || fail "Failed to reload nginx"
else
  fail "nginx test failed. Aborting."
fi

# Check for SSL cert presence (warn only)
if [ ! -f /etc/nginx/ssl/nginx.crt ] || [ ! -f /etc/nginx/ssl/nginx.key ]; then
  info "WARNING: TLS cert or key not found at /etc/nginx/ssl/*. If you expect HTTPS to work, ensure certs are present."
fi

info "Enabling and starting smart-voting-machine systemd service (if present)."
if systemctl list-unit-files | grep -q '^smart-voting-machine.service'; then
  systemctl enable --now smart-voting-machine.service || info "Failed to enable/start service; you may need to inspect logs."
  systemctl is-active --quiet smart-voting-machine.service && info "smart-voting-machine is active" || info "smart-voting-machine is not active"
  info "Last 50 log lines for smart-voting-machine (journalctl):"
  journalctl -u smart-voting-machine -n 50 --no-pager || true
else
  info "smart-voting-machine.service not found; skipping service start."
fi

info "Verifying local HTTP -> HTTPS redirect and HTTPS endpoint (using localhost)."
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost/config || echo "000")
if [ "$HTTP_CODE" = "301" ] || [ "$HTTP_CODE" = "302" ]; then
  REDIR_URL=$(curl -s -I http://localhost/config | sed -n 's/Location: \(.*\)/\1/p' | tr -d '\r')
  info "HTTP returned $HTTP_CODE and redirects to: $REDIR_URL"
else
  info "HTTP did not return a redirect (code $HTTP_CODE)."
fi

HTTPS_CODE=$(curl -ks -o /dev/null -w "%{http_code}" https://localhost/config || echo "000")
info "HTTPS localhost /config returned HTTP status: $HTTPS_CODE (use -k because cert may be self-signed)."

info "All done. Backup directory: $BACKUP_DIR"

exit 0

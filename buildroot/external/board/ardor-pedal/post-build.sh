#!/bin/sh
set -eu

TARGET="$1"

release_version=${ARDOR_RELEASE_VERSION:-0.0.0}
release_commit=${ARDOR_RELEASE_COMMIT:-unknown}
cat > "${TARGET}/etc/ardor-release.json" <<EOF
{
  "version": "${release_version}",
  "commit": "${release_commit}"
}
EOF
chmod 0644 "${TARGET}/etc/ardor-release.json"

# The Ed25519 public key is not secret. Release images provide it as base64;
# local development images without a configured key keep OTA disabled.
if [ -n "${ARDOR_UPDATE_PUBLIC_KEY_BASE64:-}" ]; then
    printf '%s\n' "${ARDOR_UPDATE_PUBLIC_KEY_BASE64}" > "${TARGET}/etc/ardor-update.pub"
    chmod 0644 "${TARGET}/etc/ardor-update.pub"
else
    rm -f "${TARGET}/etc/ardor-update.pub"
fi

# Generate SSH host keys into the target rootfs if not present.
# The rootfs is read-only at runtime so sshd cannot generate them on first boot.
SSH_DIR="${TARGET}/etc/ssh"
mkdir -p "${SSH_DIR}"

for type in rsa ecdsa ed25519; do
    key="${SSH_DIR}/ssh_host_${type}_key"
    if [ ! -f "${key}" ]; then
        ssh-keygen -q -t "${type}" -N "" -f "${key}"
        chmod 600 "${key}"
        chmod 644 "${key}.pub"
    fi
done

# Drop the framebuffer console getty. The DSI panel is owned by the LVGL UI;
# a login prompt on tty1 just overdraws the app (and reappears on logout).
# Serial getty (ttyAMA0) stays for debugging. Boot text on tty1 is unaffected.
INITTAB="${TARGET}/etc/inittab"
[ -f "${INITTAB}" ] && sed -i '/^tty1::/d' "${INITTAB}"

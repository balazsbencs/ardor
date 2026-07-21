#!/bin/sh
set -eu

TARGET="$1"

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

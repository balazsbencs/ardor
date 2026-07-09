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

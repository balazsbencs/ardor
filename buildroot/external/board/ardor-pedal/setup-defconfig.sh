#!/bin/sh
set -eu

usage() {
  cat <<'EOF'
Usage:
  setup-defconfig.sh --write <BR2_EXTERNAL>
  setup-defconfig.sh --check <BR2_EXTERNAL>

Run from the root of the Buildroot checkout selected by
buildroot/buildroot-version.env.
EOF
}

die() {
  echo "setup-defconfig: $*" >&2
  exit 1
}

mode=${1:-}
external_arg=${2:-}

case "$mode" in
  --write|--check) ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

[ -n "$external_arg" ] || die "BR2_EXTERNAL path is required"
[ "$#" -eq 2 ] || die "expected a mode and one BR2_EXTERNAL path"

topdir=$(pwd -P)
external_dir=$(CDPATH= cd -- "$external_arg" && pwd -P)
version_file="$external_dir/../buildroot-version.env"
upstream="$topdir/configs/raspberrypi4_64_defconfig"
fragment="$external_dir/configs/ardor-pedal.fragment"
defconfig_out="$external_dir/configs/raspberrypi4_ardor_pedal_defconfig"

[ -f "$topdir/Makefile" ] || die "run this script from a Buildroot checkout"
[ -f "$version_file" ] || die "missing version manifest: $version_file"
[ -f "$upstream" ] || die "missing upstream base: $upstream"
[ -f "$fragment" ] || die "missing Ardor fragment: $fragment"

# shellcheck disable=SC1090
. "$version_file"
: "${BUILDROOT_VERSION:?BUILDROOT_VERSION is required}"

actual_version=$(sed -n 's/^export BR2_VERSION := //p' "$topdir/Makefile")
[ "$actual_version" = "$BUILDROOT_VERSION" ] ||
  die "Buildroot $actual_version does not match required $BUILDROOT_VERSION"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ardor-defconfig.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/output"

CONFIG_=BR2_ KCONFIG_CONFIG="$tmp/merged.config" \
  "$topdir/support/kconfig/merge_config.sh" -m \
  -e "$external_dir" "$upstream" "$fragment"

make -C "$topdir" O="$tmp/output" \
  BR2_EXTERNAL="$external_dir" \
  BR2_CONFIG="$tmp/merged.config" olddefconfig

make -C "$topdir" O="$tmp/output" \
  BR2_EXTERNAL="$external_dir" \
  BR2_CONFIG="$tmp/merged.config" \
  BR2_DEFCONFIG="$tmp/generated.defconfig" savedefconfig

case "$mode" in
  --write)
    cp "$tmp/generated.defconfig" "$defconfig_out"
    echo "Wrote $defconfig_out"
    ;;
  --check)
    if ! cmp -s "$tmp/generated.defconfig" "$defconfig_out"; then
      echo "setup-defconfig: generated defconfig is stale" >&2
      diff -u "$defconfig_out" "$tmp/generated.defconfig" || true
      echo "Run: $0 --write $external_dir" >&2
      exit 1
    fi
    echo "Defconfig is current: $defconfig_out"
    ;;
esac

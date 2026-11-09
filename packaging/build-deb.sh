#!/bin/sh
# usage: packaging/build-deb.sh <debian|termux> <deb-arch> <version> <bindir> <outdir>
#   optional: PEMU_HOMEPAGE=https://... adds a Homepage: field to the package
#   debian: /usr/bin/pemu, /usr/bin/pemu_x64        (arch: amd64 | arm64)
#   termux: $TERMUX_PREFIX/bin/...                  (arch: aarch64)
set -e
flavor=$1 arch=$2 version=$3 bindir=$4 outdir=$5
[ -n "$outdir" ] || { echo "usage: $0 debian|termux ARCH VERSION BINDIR OUTDIR" >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
case "$flavor" in
  debian) prefix=/usr; deps="libc6"; rec="tar, xz-utils" ;;
  termux) prefix=/data/data/com.termux/files/usr; deps="tar, xz-utils"; rec="" ;;
  *) echo "unknown flavor $flavor" >&2; exit 2 ;;
esac
pkg=$(mktemp -d); trap 'rm -rf "$pkg"' EXIT
root=$pkg/root; dest=$root$prefix
mkdir -p "$dest/bin" "$dest/share/doc/pemu" "$root/DEBIAN"
install -m 755 "$bindir/pemu" "$bindir/pemu_x64" "$dest/bin/"
install -m 644 "$here/README.md" "$here/LICENSE" "$dest/share/doc/pemu/"
size=$(du -sk "$root$prefix" | cut -f1)
{
  echo "Package: pemu"
  echo "Version: $version"
  echo "Architecture: $arch"
  echo "Maintainer: pemu maintainers <tnaka8285@gmail.com>"
  echo "Section: utils"
  echo "Priority: optional"
  echo "Installed-Size: $size"
  echo "Depends: $deps"
  [ -z "$rec" ] || echo "Recommends: $rec"
  [ -z "$PEMU_HOMEPAGE" ] || echo "Homepage: $PEMU_HOMEPAGE"
  echo "Description: user-space CPU emulator with built-in proot (chroot, bind mounts, fake root)"
  echo " pemu runs foreign-architecture Linux rootfs images without ptrace or qemu."
  echo " Guest paths are translated directly in the syscall layer."
} > "$root/DEBIAN/control"
mkdir -p "$outdir"
out="$outdir/pemu_${version}_${flavor}_${arch}.deb"
dpkg-deb --root-owner-group -Zxz --build "$root" "$out" >/dev/null
echo "$out"

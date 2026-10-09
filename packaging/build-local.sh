#!/usr/bin/env bash
# Build the Arch package from the local checkout (HEAD, committed files only)
# and refresh the local pacman repo in dist/repo.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
pkgver="$(sed -n 's/^pkgver=//p' "$here/PKGBUILD")"
cd "$here"
rm -rf src pkg "omaear-$pkgver.tar.gz"
git -C "$root" archive --format=tar.gz --prefix="omaear-$pkgver/" -o "$here/omaear-$pkgver.tar.gz" HEAD
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME
makepkg -f --noconfirm "$@"
repo="$root/dist/repo"
mkdir -p "$repo"
pkg="$(ls -t omaear-"$pkgver"-*.pkg.tar.zst | head -1)"
cp -f "$pkg" "$repo/"
repo-add -R "$repo/omaear.db.tar.gz" "$repo/$pkg"
echo "Package: $repo/$pkg"

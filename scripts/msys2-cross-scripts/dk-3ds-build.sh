#!/bin/bash
# $1 = branch

[ -z "$DEVKITPRO" ] && { echo "DEVKITPRO environment variable must be set!"; exit 1; }
[ -z "$1" ] && { echo "argument 1 required."; exit 1; }

export DEVKITPRO=`cygpath -u "$DEVKITPRO"`

export PATH="$PATH:$DEVKITPRO/devkitARM/bin"
export PATH="$PATH:$DEVKITPRO/tools/bin"

cd /mzx-build-workingdir
mkdir -p zips
cd megazeux

git checkout $1

arch/3ds/CONFIG.3DS
make debuglink -j8
make archive

mv build/dist/3ds/* /mzx-build-workingdir/zips/

make distclean

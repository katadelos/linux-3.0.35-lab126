#!/bin/bash
#
# Script to apply the kernel patches in the order specified in the series file.
#

#
# How to do it:
#
# First untar the kernel source downloaded from kernel.org:
# tar xjf linux-[version].bz2
#
# cd to the base of the linux source tree:
# cd linux-[version]
#
# Then untar the patches package that is provided in the release:
# tar xjf ../linux-[version]-[release].bz2
#
# And run this script from the base of the linux source tree:
# ./patches/patch-kernel.sh
#

# Use 'git am' instead of patch to apply patches because some patches
# have symbolic links
git init  && \
git add . && \
git commit -m "clean kernel" && \
git am --whitespace=nowarn patches/*.patch

# linux/scripts/setlocalversion will attempt to get the git version and
# append it to the linux version.  This is a hack to make this kernel
# git repository not take git commands for a while.
mv .git .gitsaved

if [ -f patches/localversion ]; then
	cp -f patches/localversion .
fi

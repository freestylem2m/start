#!/bin/bash -x

git ctags
if [ -f "Makefile" ]; then
	make distclean > /dev/null 2>&1
fi
autoconf &&
./configure &&
make

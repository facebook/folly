#!/bin/sh

autoconf
aclocal
autoconf 
autoheader
libtoolize --force
automake --force-missing --add-missing
./configure


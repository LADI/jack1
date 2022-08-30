#!/bin/sh

#
# on OS X, which(1) returns 0 even when it can't find a program
#

if type libtoolize >/dev/null 2>&1
then
    LIBTOOLIZE=libtoolize
else
    if which glibtoolize >/dev/null
    then
	# on the Mac it's called glibtoolize for some reason
	LIBTOOLIZE=glibtoolize
    else
	echo "libtoolize not found"
	exit 1
    fi
fi

$LIBTOOLIZE --force --automake 2>&1 | sed '/^You should/d' || {
    echo "libtool failed, exiting..."
    exit 1
}

ACLOCAL_FLAGS="-I m4"
aclocal $ACLOCAL_FLAGS || {
    echo "aclocal \$ACLOCAL_FLAGS where \$ACLOCAL_FLAGS= failed, exiting..."
    exit 1
}

autoheader || {
    echo "autoheader failed, exiting..."
    exit 1
}

automake --add-missing --foreign || {
    echo "automake --add-missing --foreign failed, exiting..."
    exit 1
}

autoconf || {
    echo "autoconf failed, exiting..."
    exit 1
}

if test x$1 = x--run-conf; then
  echo "because of \"$1\" Running ./configure --enable-maintainer-mode $@..."
  ./configure --enable-maintainer-mode $@
fi

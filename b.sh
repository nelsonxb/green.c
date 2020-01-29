#!/bin/bash

set -e

USAGE="usage: $0 [-qvgrb -ttarget]"

CFLAGS="$CFLAGS"
CC=gcc
build=false
run=false
add_asm=false
vn=1
mname="$(uname -m)"
mn="$mname"
oname=test-green

declare -A GCCs=( \
    [native]=gcc                        \
    [x86_64]=x86_64-pc-linux-gnu-gcc    \
    [x64]=x86_64-pc-linux-gnu-gcc       \
    [aarch64]=aarch64-linux-gnu-gcc     \
    [arm64]=aarch64-linux-gnu-gcc       \
)

while getopts qvgrbt:h name; do
    case $name in
    q)  vn=0
        ;;

    v)  let ++vn
        ;;

    g)  CFLAGS+=" -g -D_GREEN_ASM_DEBUG"
        add_asm=true
        ;;

    r)  run=true
        ;;

    b)  build=true
        ;;

    t)  CC="${GCCs[$OPTARG]}"
        if [ -z "$CC" ]; then
            echo "unknown target $OPTARG"
            exit 2
        fi

        if [ "$OPTARG" != 'native' ]; then
            mn="$(cut -d- -f1 <<<$CC)"
        fi

        ;;

    h)  echo "$USAGE"
        exit 0
        ;;

    ?)  echo "$USAGE"
        exit 2
        ;;
    esac
done

if [ "$mn" != "$mname" ]; then
    mname="$mn"
    oname+=".$mname"
    build=true

    if $run; then
        echo "can't run that target under this system"
        exit 2
    fi
fi

if ! $build && ! $run; then
    build=true
    run=true
fi

if $build && ! command -v "$CC" >/dev/null; then
    echo "$CC not installed"
    exit 2
fi

SOURCES="test-green.c green.c"

if $add_asm; then
    SOURCES+=" green.$mname.s"
fi

if $build; then
    if [ $vn -eq 1 ]; then
        echo "cc $SOURCES"
    elif [ $vn -ge 2 ]; then
        echo "$CC -o $oname $CFLAGS $SOURCES"
    fi

    "$CC" -o $oname $CFLAGS $SOURCES
fi

if $run; then
    if [ $vn -eq 1 ]; then
        echo "test-green"
    elif [ $vn -ge 2 ]; then
        echo "./test-green"
    fi

    ./test-green
fi

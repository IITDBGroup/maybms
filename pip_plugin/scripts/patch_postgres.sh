#!/bin/bash

ORIGINALWD=$(pwd)
cd $(dirname $0)
BASEDIR=`pwd`
PIPDIR=$(dirname $BASEDIR)
MAYBMSDIR=$(dirname $PIPDIR)
PGDIR=$MAYBMSDIR/postgresql-8.3.3
CTYPEDIR=$MAYBMSDIR/postgresql-ctype

find $PIPDIR/src/patch -name .DS_Store | xargs rm


if [ ! -d $CTYPEDIR ] ; then
  echo "======================= Cleaning up MayBMS Postgres =======================";
  cd $PGDIR;
  make distclean;
  cd ..
  echo "========================= Cloning MayBMS Postgres =========================";
  cp -r $PGDIR $CTYPEDIR;
  echo "... done";
fi

echo "================ Identifying Patches For CTYPE Postgres ===================";
cd $MAYBMSDIR
for i in $(find $PIPDIR/src/patch -type f | grep -v CVS  | grep -v '.original' | sed "s:^$PIPDIR/src/patch/::"); do 
  if [ -f $CTYPEDIR/src/$i ] ; then
    if diff -q $PIPDIR/src/patch/$i $CTYPEDIR/src/$i > /dev/null ; then
      echo "Ignoring up-to-date file $i";
    else
      if [ ! -f $PIPDIR/src/patch/$i.original ] ; then
        echo "Replacing file $i";
        cp $PIPDIR/src/patch/$i $CTYPEDIR/src/$i
      else 
        echo "Patching file $i";
        merge $CTYPEDIR/src/$i $PIPDIR/src/patch/$i.original $PIPDIR/src/patch/$i
      fi
    fi
  else 
    echo "Copying new file $i";
    cp $PIPDIR/src/patch/$i $CTYPEDIR/src/$i;
  fi
done

cd $ORIGINALWD
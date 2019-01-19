
ORIGINALWD=$(pwd)
cd $(dirname $0)
BASEDIR=`pwd`
PIPDIR=$(dirname $BASEDIR)
MAYBMSDIR=$(dirname $PIPDIR)
PGDIR=$MAYBMSDIR/postgresql-8.3.3
CTYPEDIR=$MAYBMSDIR/postgresql-ctype
TARGETDIR=$PGDIR

find $PIPDIR/src/patch -name .DS_Store | xargs rm

cd $MAYBMSDIR
for i in $(find $PIPDIR/src/patch -type f | grep -v CVS | grep -v '.original' | sed "s:^$PIPDIR/src/patch/::"); do 
  if [ -f $TARGETDIR/src/$i ] ; then
    if diff -q $PIPDIR/src/patch/$i $TARGETDIR/src/$i > /dev/null ; then
      echo "======== $i Unchanged =======";
    else
      echo "======== $i Changed =======";
      if [ -n "$1" ] ; then
        $1 $TARGETDIR/src/$i $PIPDIR/src/patch/$i
      else 
        diff $TARGETDIR/src/$i $PIPDIR/src/patch/$i
      fi
    fi
  else 
    echo "======== $i Added =======";
  fi
done

cd $ORIGINALWD
#!/bin/bash
PGUSER=$1
MAYBMSHOME=/home/maybms
SRCDIR=${MAYBMSHOME}/src
INSTALLBINDIR=${MAYBMSHOME}/install/bin
DATADIR=${MAYBMSHOME}/datadir
####################
echo - start server
${INSTALLBINDIR}/pg_ctl -D ${DATADIR} -w start
sleep 10
####################
echo - create testdb
${INSTALLBINDIR}/psql -h localhost -p 5432 -U $PGUSER -d maybms -f /home/maybms/src/pip_plugin/pip-noctype.sql
${INSTALLBINDIR}/psql -h localhost -p 5432 -U $PGUSER -d template1 -f /home/maybms/src/pip_plugin/pip-noctype.sql
####################
echo - shutdown server
${INSTALLBINDIR}/pg_ctl -w stop

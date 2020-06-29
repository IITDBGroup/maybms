#!/bin/bash
PGUSER=$1
MAYBMSHOME=/home/maybms
SRCDIR=${MAYBMSHOME}/src
INSTALLBINDIR=${MAYBMSHOME}/install/bin
DATADIR=${MAYBMSHOME}/datadir
####################
echo ---- Configure Maybms installation and create TestDatabase
echo -- for user ${PGUSER} with INSTALLBINDIR=${INSTALLBINDIR} and DATADIR=${DATADIR}
echo - Create cluster, DB user and testdb database
mkdir -p ${DATADIR}
${INSTALLBINDIR}/initdb -D ${DATADIR}
####################
echo - start server
${INSTALLBINDIR}/pg_ctl -D ${DATADIR} -w start
${INSTALLBINDIR}/psql -h localhost -p 5432 -U maybms -d template1 -c 'CREATE LANGUAGE plpgsql'
${INSTALLBINDIR}/psql -h localhost -p 5432 -U maybms -d template1 -c '\i /home/maybms/src/contrib/xml2/pgxml.sql'
####################
echo - create user and testdb
${INSTALLBINDIR}/createuser -s -l -U $PGUSER maybms
${INSTALLBINDIR}/createdb -U $PGUSER maybms
####################
echo - shutdown server
${INSTALLBINDIR}/pg_ctl -w stop
####################
echo - change listen addresses
/bin/sed -i -e "s/#listen_addresses = 'localhost'/listen_addresses = '*'/g" ${DATADIR}/postgresql.conf
echo "host all all 0.0.0.0/0 trust" >> ${DATADIR}/pg_hba.conf

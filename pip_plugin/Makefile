#-------------------------------------------------------------------------
#
# Makefile for pip
#
#-------------------------------------------------------------------------

PG_FUNCTIONS :=$(wildcard src/funcs/*.c)
DISTRIBUTIONS:=$(wildcard src/dist/*.c)
TYPES        :=$(wildcard src/type/*.c)
SAMPLING     :=$(wildcard src/sample/*.c)
LIBRARYCODE  :=$(wildcard src/library/*.c)
CORE         :=src/pip.c src/list_utils.c
GENERATED    :=src/disttable.c src/funcs.c
SCRIPTS      := #src/dist/normal.dist
# INSTALL_DIR  :=/maybms/install/

# because postgres likes to have the function info in the same
# file as the functions themselves... we don't include the
# function files here.  Instead, they get included indirectly
# via funcs.c
SOURCEFILES=$(CORE) $(GENERATED) $(DISTRIBUTIONS) $(TYPES) $(SAMPLING) $(LIBRARYCODE)
OBJS =$(subst .c,.o,$(SOURCEFILES))

PG_CONFIG=pg_config

MODULE_big:= pip
DATA_built:= pip.sql pip-noctype.sql pip-uninstall.sql pip-test.sql # install.test.sql install.sql uninstall.sql $(SCRIPTS)
PIP_CFLAGS+= -I$(shell $(PG_CONFIG) --includedir) \
             -I$(shell $(PG_CONFIG) --includedir)/server \
             -Isrc/include

PGXS=$(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
override CFLAGS   += $(PIP_CFLAGS)

src/funcs.c: $(PG_FUNCTIONS) scripts/gen_functable.sh
	./scripts/gen_functable.sh

src/disttable.c: $(DISTRIBUTIONS) scripts/gen_disttable.sh
	./scripts/gen_disttable.sh

src/dist/normal.dist: scripts/normal.rb
	./scripts/normal.rb > src/dist/normal.dist

pip.sql.in: src/pip.source
	cat $< > $@

pip-noctype.sql: pip.sql
	cat $< | grep -v "HACKED_SQL_ONLY" > $@

pip-test.sql.in: src/pip.source
	rm -f $@; \
	cat $< > $@

pip-uninstall.sql.in: src/pip.source scripts/gen_uninstall.sh
	./scripts/gen_uninstall.sh > $@

# let postrgres extension build system install
# install:
#	install -d $INSTALL_DIR/libpip*

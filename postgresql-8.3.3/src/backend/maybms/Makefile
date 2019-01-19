#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for backend/maybms
#
#
#-------------------------------------------------------------------------

subdir = src/backend/maybms
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

OBJS = aconf.o argmax.o bitset.o SPROUT.o localcond.o rewrite.o rewrite_updates.o \
       supported.o tupleconf.o utils.o ws-tree.o repair_key.o signature.o \
       rewrite_utils.o pick_tuples.o d-tree.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS) 

clean: 
	rm -f SUBSYS.o $(OBJS) 

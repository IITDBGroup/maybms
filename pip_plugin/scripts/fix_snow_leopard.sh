#!/bin/bash

OLDWD=`pwd`
cd `dirname $0`/../../postgresql-ctype/

pwd

if [ ! -f src/interfaces/ecpg/preproc/pgc.c.orig ] ; then
  cp src/interfaces/ecpg/preproc/pgc.c src/interfaces/ecpg/preproc/pgc.c.orig
fi

cp src/interfaces/ecpg/preproc/pgc.c.orig src/interfaces/ecpg/preproc/pgc.c

patch -p0 << EOF
--- src/interfaces/ecpg/preproc/pgc.c
+++ src/interfaces/ecpg/preproc/pgc.c
@@ -158,7 +158,7 @@
 typedef size_t yy_size_t;
 #endif
 
-extern yy_size_t yyleng;
+extern int yyleng;
 
 extern FILE *yyin, *yyout;
 
@@ -285,7 +285,7 @@
 /* yy_hold_char holds the character lost when yytext is formed. */
 static char yy_hold_char;
 static yy_size_t yy_n_chars;		/* number of characters read into yy_ch_buf */
-yy_size_t yyleng;
+int yyleng;
 
 /* Points to current character in buffer. */
 static char *yy_c_buf_p = (char *) 0;
EOF

cd $OLDWD;
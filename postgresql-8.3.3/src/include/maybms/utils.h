/*-------------------------------------------------------------------------
 *
 * utils.h
 *	  Utility functions.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

extern void myLog( char* text );
extern void myLogi( int a );
extern void myLogp( void* p );
extern void myLogb( bool b );
extern void myLogf( float a );
extern void myLogc( char c );
extern void nl(int a);


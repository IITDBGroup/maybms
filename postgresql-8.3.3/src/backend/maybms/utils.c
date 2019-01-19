/*-------------------------------------------------------------------------
 *
 * utils.c
 *	  Utility functions.
 *
 *
 * Copyright (c) 2008, MayBMS Development Group
 *
 *-------------------------------------------------------------------------
 */
 
#include "maybms/utils.h"

/* myLogc
 *
 * Print a character
 */
void 
myLogc( char c )
{
	FILE * fp = fopen( "report.txt", "a" );
	fprintf( fp, ":%c", c );
	fclose(fp);
}

/* myLog
 *
 * Print text.
 */
void 
myLog( char* text )
{
	FILE * fp = fopen( "report.txt", "a" );
	fprintf( fp, text );
	fclose(fp);
}

/* myLogi
 *
 * Print an integer.
 */
void 
myLogi( int a )
{
	FILE * fp = fopen( "report.txt", "a" );
	fprintf( fp, ": %d ", a );
	fclose(fp);
}

/* myLogc
 *
 * Print a float.
 */
void 
myLogf( float a )
{
	FILE * fp = fopen( "report.txt", "a" );
	fprintf( fp, ": %f ", a );
	fclose(fp);
}

/* myLogc
 *
 * Print a new line.
 */
void 
nl(int a)
{
	FILE * fp = fopen( "report.txt", "a" );
	fprintf( fp, "\n" );
	fclose(fp);
}

/* myLogp
 *
 * Print "not null" is the pointer is not null.
 */
void 
myLogp( void* p )
{
	FILE * fp = fopen( "report.txt", "a" );
	
	if ( p == NULL )
		fprintf( fp, "null" );	
	else
		fprintf( fp, "not null" );	
		
	fclose(fp);	
}

/* myLogb
 *
 * Print a Boolean value.
 */
void 
myLogb( bool b ){
	FILE * fp = fopen( "report.txt", "a" );
	
	if ( b == true )
		fprintf( fp, "true" );	
	else
		fprintf( fp, "false" );	
		
	fclose(fp);	
}


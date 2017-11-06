/*
  mdhimiftst.c - file based test frame

  * based on the pbliftst.c
  Copyright (C) 2002 - 2007   Peter Graf

  pbliftst.c file is part of PBL - The Program Base Library.
  PBL is free software.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  For more information on the Program Base Library or Peter Graf,
  please see: http://www.mission-base.com/.


  ------------------------------------------------------------------------------
*/

/*
 * make sure "strings <exe> | grep Id | sort -u" shows the source file versions
 */
char * mdhimTst_c_id = "$Id: mdhimTst.c,v 1.00 2013/07/08 20:56:50 JHR Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <ctype.h>

#include "mpi.h"
#include "mdhim.h"
#include "mdhim_options.h"

// From partitioner.h:
/*
  #define MDHIM_INT_KEY 1
  //64 bit signed integer
  #define MDHIM_LONG_INT_KEY 2
  #define MDHIM_FLOAT_KEY 3
  #define MDHIM_DOUBLE_KEY 4
  #define MDHIM_LONG_DOUBLE_KEY 5
  #define MDHIM_STRING_KEY 6
  //An arbitrary sized key
  #define MDHIM_BYTE_KEY 7  */

#define TEST_BUFLEN              4096
int BYTE_BUFLEN = 4;
int VAL_BUFLEN = 4;

static FILE * logfile;
static FILE * infile;
int verbose = 1;   // By default generate lots of feedback status lines
int dbOptionAppend = MDHIM_DB_OVERWRITE;
int to_log = 0;
// MDHIM_INT_KEY=1, MDHIM_LONG_INT_KEY=2, MDHIM_FLOAT_KEY=3, MDHIM_DOUBLE_KEY=4
// MDHIM_LONG_DOUBLE_KEY=5, MDHIM_STRING_KEY=6, MDHIM_BYTE_KEY=7 
int key_type = 1;  // Default "int"

#define MAX_ERR_REPORT 50
static char *errMsgs[MAX_ERR_REPORT];
static int errMsgIdx = 0;

static int sc_len;  // Source Character string length for Random String generation
static char *sourceChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ124567890"; 

// Labels for basic get operations
static char *getOpLabel[] = { "MDHIM_GET_EQ", "MDHIM_GET_NEXT", "MDHIM_GET_PREV",
				"MDHIM_GET_FIRST", "MDHIM_GET_LAST"};

#ifdef _WIN32
#include <direct.h>
#endif

#include <stdarg.h>

// Add to error message list
static void addErrorMessage(char *msg) {
// Max number of error messages reached ignore the rest, but still increment count
  if (errMsgIdx > MAX_ERR_REPORT)
    errMsgIdx++;
  else
    errMsgs[errMsgIdx++] = msg;
}

static void tst_say(int err, char * format, ...) {
  va_list ap;

  if (err) {
    char *errMsg = (char *)malloc(sizeof(char)* TEST_BUFLEN);
    va_start( ap, format );
    vsnprintf(errMsg, TEST_BUFLEN, format, ap);
    addErrorMessage(errMsg);

    // Make sure error messages print to stderr when loging output
    if (to_log) fprintf(stderr, "%s", errMsg); 
    va_end(ap);
  }

  /*
   * use fprintf to give out the text
   */
  if (to_log)
    {
      va_start( ap, format );
      vfprintf( logfile, format, ap);
      va_end(ap);
    }
  else
    {
      va_start( ap, format );
      vfprintf( stdout, format, ap);
      va_end(ap);
    }
    
}

int check_rank_range(int rank, int init, int final) {

  if (rank >= init && rank <= final) { //printf("\nRank %d is in range between %d and %d\n", rank, init, final); 
    return 0;}
  else {//printf("\nRank %d is NOT in range between %d and %d\n", rank, init, final); 
    return 1;
  }
}

int check_rank_mod(int rank, int rmod){

  if ((rank % rmod) ==0){ //printf("\nRank %d is divisible by %d\n", rank, rmod); 
    return 0;}
  else{ //printf("\nRank %d is NOT divisible by %d\n", rank, rmod); 
    return 1; }
}

static void putChar( int c )
{
  static int last = 0;

  if( last == '\n' && c == '\n' )
    {
      return;
    }

  last = c;
  putc( last, logfile );
}

static int getChar( void )
{
  int c;
  c = getc( infile );

  /*
   * a '#' starts a comment for the rest of the line
   */
  if( c == '#')
    {
      /*
       * comments starting with ## are duplicated to the output
       */
      c = getc( infile );
      if( c == '#' )
	{
	  putChar( '#' );
	  putChar( '#' );

	  while( c != '\n' && c != EOF )
	    {
	      c = getc( infile );
	      if( c != EOF )
		{
		  putChar( c );
		}
	    }
	}
      else
	{
	  while( c != '\n' && c != EOF )
	    {
	      c = getc( infile );
	    }
	}
    }

  /*
    if( c != EOF )
    {
    putChar( c );
    }
  */

  return( c );
}

static int getWordFromString (char *aLine,  char *buffer, int charIdx )
{
  int c;
  int i;

  // Check to see if past the end
  if (charIdx >= strlen(aLine))
    {
      *buffer = '\0';
      return charIdx;
    }
    
  /*
   * skip preceeding blanks
   */
  c = aLine[charIdx++];
  while( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
    {
      c = aLine[charIdx++];
    }

  /*
   * read one word
   */
  for( i = 0; i < TEST_BUFLEN - 1; i++, c = aLine[charIdx++] )
    {

      if( c == '\r' )
	{
	  continue;
	}

      if( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
	{
	  *buffer = '\0';
	  return charIdx;
	}

      *buffer++ = c;
    }

  *buffer = '\0';
  return charIdx;
}

/* Read one line at a time. Skip any leadings blanks, and send an end of file
   as if a "q" command had been encountered. Return a string with the line read. */
static void getLine( char * buffer )
{
  int c;
  int i;

  // skip preceeding blanks
  c = ' ';
  while( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
    {
      c = getChar();
    }

  // End of input file (even if we did not find a q!
  if( c == EOF )
    {
      *buffer++ = 'q';
      *buffer++ = '\0';
      return;
    }
    
  // Read one line
  for( i = 0; i < TEST_BUFLEN - 1; i++, c = getChar() )
    {

      if( c == EOF || c == '\n' || c == '\r' )
	{
	  *buffer = '\0';
	  return;
	}

      *buffer++ = c;
    }

  *buffer = '\0';
}

/* Expands escapes from a sequence of characters to null terminated string
 *
 * src must be a sequence of characters.
 * len is the size of the sequence of characters to convert.
 * 
 * returns a string of size 2 * len + 1 will always be sufficient
 *
 */

char *expand_escapes(const char* src, int len) 
{
  char c;
  int i;
  char* dest;
  char* res;
  
  if ((res = malloc(2 * len + 1)) == NULL)
    {
      printf("Error allocating memory in expand_escapes.\n");
      return NULL;
    }
  dest = res;

  for (i=0 ; i<len-1; i++ ) {
    c = *(src++);
    switch(c) {
    case '\0': 
      *(dest++) = '\\';
      *(dest++) = '0';
      break;
    case '\a': 
      *(dest++) = '\\';
      *(dest++) = 'a';
      break;
    case '\b': 
      *(dest++) = '\\';
      *(dest++) = 'b';
      break;
    case '\t': 
      *(dest++) = '\\';
      *(dest++) = 't';
      break;
    case '\n': 
      *(dest++) = '\\';
      *(dest++) = 'n';
      break;
    case '\v': 
      *(dest++) = '\\';
      *(dest++) = 'v';
      break;
    case '\f': 
      *(dest++) = '\\';
      *(dest++) = 'f';
      break;
    case '\r': 
      *(dest++) = '\\';
      *(dest++) = 'r';
      break;
    case '\\': 
      *(dest++) = '\\';
      *(dest++) = '\\';
      break;
    case '\"': 
      *(dest++) = '\\';
      *(dest++) = '\"';
      break;
    default:
      *(dest++) = c;
    }
  }

  *dest = '\0'; /* Ensure null terminator */
  return res;
}

// Handle erroneous requests
char *getValLabel(int idx)
{
  if (idx < 0 || idx > (sizeof(getOpLabel) / sizeof(*getOpLabel)))
    return "Invalid_get_OPERATOR";
  else
    return getOpLabel[idx];
}

void usage(void)
{
  printf("Usage:\n");
  printf(" -f<BatchInputFileName> (file with batch commands)\n");
  printf(" -l<Key Lenth> (Key length for file)\n");
  printf(" -d<DataBaseType> (Type of DB to use: levelDB=1 mysql=3)\n");
  printf(" -t<IndexKeyType> (Type of keys: int=1, longInt=2, float=3, "
	 "double=4, longDouble=5, string=6, byte=7)\n");
  printf(" -p<pathForDataBase> (path where DB will be created)\n");
  printf(" -n<DataBaseName> (Name of DataBase file or directory)\n");
  printf(" -b<DebugLevel> (MLOG_CRIT=1, MLOG_DBG=2)\n");
  printf(" -a (DB store append mode. By default records with same key are "
	 "overwritten. This flag turns on the option to append to existing values.\n");
  printf(" -w<Rank modlus> This flag turns on the option to either allow or deny threads to do command based on if it is dividiable by the modlus of the modulus number\n");
  printf(" -r<lowest rank number> ~ <highest rank number>This flag turns on the option to either allow or deny threads to do command based on if the rank falls inclusively inbetween the rank ranges.  NOTE: You must use the '~' inbetween the numbers.  Example: -r0~2\n");
  printf(" -q<0|1> (Quiet mode, default is verbose) 1=write out to log file\n");
  exit (8);
}

// Release the memory used in a Bulk request. If values/value_lens not present just use NULL
void freeKeyValueMem(int nkeys, void **keys, int *key_lens, char **values, int *value_lens)
{
  int i;
  for (i = 0; i < nkeys; i++)
    {
      if (keys[i]) free(keys[i]);
      if (values && value_lens && value_lens[i] && values[i]) free(values[i]);
    }
  if (key_lens) free(key_lens);	
  if (keys) free(keys);
  if (value_lens) free(value_lens);
  if (values) free(values);
}

//======================================FLUSH============================
static void execFlush(char *command, struct mdhim_t *md, int charIdx)
{
  //Get the stats
  int ret = mdhimStatFlush(md, md->primary_index);

  if (ret != MDHIM_SUCCESS) {
    tst_say(1, "ERROR: rank %d executing flush.\n", md->mdhim_rank);
  } else {
    tst_say(0, "Flush executed successfully.\n");
  }
        
}

//======================================PUT============================
static void execPut(char *command, struct mdhim_t *md, int charIdx)
{
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  struct mdhim_brm_t *brm;
  unsigned char str_key [ TEST_BUFLEN ];
  //unsigned char buffer2 [ TEST_BUFLEN ];
  unsigned char value [ TEST_BUFLEN ] ;
  char fhandle [TEST_BUFLEN]; //Filename handle
  //int data_index =0; //Index for reading data.
  char data_read[TEST_BUFLEN];
  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read));
  //int ret;
 
  if (verbose) tst_say(0, "# put key data\n" );
  charIdx = getWordFromString( command, fhandle, charIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  int g=read(x, str_key, BYTE_BUFLEN);
  if( g<0 )
    {
      fprintf( stderr, "Failed to read data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  g=read(x, value, VAL_BUFLEN);
  if( g<0 )
    {
      fprintf( stderr, "Failed to read data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
	 
  //fgets(data_read, TEST_BUFLEN, datafile);
  //data_index = getWordFromString( data_read, str_key, data_index);
  // Get value to store
	
  //charIdx = getWordFromString( data_read, buffer2, data_index);
  //sprintf(value, "%s", buffer2);
  // Based on key type generate a key using rank 
  //	switch (key_type)
  //	{
  //        case MDHIM_INT_KEY:
  //		i_key = atoi(str_key) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%d", i_key);
  //		if (verbose) tst_say(0, "# mdhimPut( %s, %s) [int]\n", key_string, value );
  //		brm = mdhimPut(md, &i_key, sizeof(i_key), value, strlen(value)+1, NULL, NULL);
  //		break;
  //             
  //        case MDHIM_LONG_INT_KEY:
  //		l_key = atol(str_key) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%ld", l_key);
  //		if (verbose) tst_say(0, "# mdhimPut( %s, %s) [long]\n", key_string, value );
  //		brm = mdhimPut(md, &l_key, sizeof(l_key), value, strlen(value)+1, NULL, NULL);
  //		break;e
  // 
  //        case MDHIM_FLOAT_KEY:
  //		f_key = atof( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%f", f_key);
  //		if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [float]\n", key_string, value );
  //		brm = mdhimPut(md, &f_key, sizeof(f_key), value, strlen(value)+1, NULL, NULL);
  //		break;
  //            
  //	case MDHIM_DOUBLE_KEY:
  //		d_key = atof( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%e", d_key);
  //		if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [double]\n", key_string, value );
  //		brm = mdhimPut(md, &d_key, sizeof(d_key), value, strlen(value)+1, NULL, NULL);
  //		break;
  //                                     
  //        case MDHIM_STRING_KEY:
  //        case MDHIM_BYTE_KEY:
  //sprintf(key_string, "%s", str_key, (md->mdhim_rank + 1));
  if (verbose) tst_say(0, "# mdhimPut( %s, %s) [string|byte]\n", str_key, value );
  brm = mdhimPut(md, (void*)str_key, BYTE_BUFLEN, value, 
		 BYTE_BUFLEN, NULL, NULL);
  //		break;
  //             
  //        default:
  //		tst_say(1, "ERROR: unrecognized Key_type in execPut\n");
  //	}

  // Report any error(s)
  if (!brm || brm->error)
    {
      tst_say(1, "ERROR: rank %d putting key: %s with value: %s into MDHIM\n", 
	      md->mdhim_rank, str_key, value);
    }
  else
    {
      tst_say(0, "Successfully put key/value into MDHIM\n");
    }

  //	fclose(datafile);
  close(x);

}

//======================================GET============================
// Operations for getting a key/value from messages.h
// MDHIM_GET_EQ=0, MDHIM_GET_NEXT=1, MDHIM_GET_PREV=2
// MDHIM_GET_FIRST=3, MDHIM_GET_LAST=4
static void execGet(char *command, struct mdhim_t *md, int charIdx)
{
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  struct mdhim_bgetrm_t *bgrm;
  unsigned char str_key [ TEST_BUFLEN ];
  char buffer2 [ TEST_BUFLEN ];
  char key_string [ TEST_BUFLEN ];
  char returned_key [ TEST_BUFLEN ];
  char ophandle [TEST_BUFLEN];
  int getOp, newIdx, nkeys=100;
  char fhandle [TEST_BUFLEN]; //Filename handle
  int data_index =0; //Index for reading data.
  char data_read[TEST_BUFLEN];
  int k;

  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read)); 
  memset(key_string, 0, TEST_BUFLEN);
  memset(returned_key, 0, TEST_BUFLEN);
  memset(ophandle, 0, TEST_BUFLEN);

  if (verbose) tst_say(0, "# get key <getOperator verfication_value>\n" );
  charIdx = getWordFromString( command, buffer2, charIdx);
  nkeys = atoi(buffer2);
  newIdx=	getWordFromString( command, ophandle, charIdx);
  if (newIdx != charIdx)
    {
      charIdx= newIdx;
      if(strcmp(ophandle,"EQUAL")==0) getOp=MDHIM_GET_EQ;
      else if(strcmp(ophandle,"PREV")==0) getOp=MDHIM_GET_PREV;
      else if(strcmp(ophandle,"NEXT")==0) getOp=MDHIM_GET_NEXT;
      else if(strcmp(ophandle,"FIRST")==0) getOp=MDHIM_GET_FIRST;
      else if(strcmp(ophandle,"LAST")==0) getOp=MDHIM_GET_LAST;
      else getOp=MDHIM_GET_EQ;
    }
  else
    {
      getOp = MDHIM_GET_EQ;  //Default a get with an equal operator
    }
	
  charIdx = getWordFromString( command, fhandle, newIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }



  //	datafile = fopen( fhandle, "r" );
  //	if( !datafile )
  //	{
  //		fprintf( stderr, "Failed to open data file %s, %s\n", 
  //			 fhandle, strerror( errno ));
  //		exit( -1 );
  //	}
  //	fgets(data_read, TEST_BUFLEN, datafile);
  // 
  //	data_index= getWordFromString( data_read, str_key, data_index);
  //	newIdx = getWordFromString( data_read, buffer2, data_index);
  //    
  for (k=0; k<nkeys; k++){
    bgrm = NULL;
    data_index=read(x, str_key, BYTE_BUFLEN);
    if( data_index<0 )
      {
	fprintf( stderr, "Failed to read data file %s, %s\n", 
		 fhandle, strerror( errno ));
	exit( -1 );
      }
    data_index=read(x,buffer2, 1);
    // Based on key type generate a key using rank 
    //	switch (key_type)
    //	{
    //	case MDHIM_INT_KEY:
    //		i_key = atoi( str_key ) * (md->mdhim_rank + 1);
    //		sprintf(key_string, "%d", i_key);
    //		if (verbose) tst_say(0, "# mdhimGet( %s, %s ) [int]\n", key_string, getValLabel(getOp));
    //		bgrm = mdhimGet(md, md->primary_index, &i_key, sizeof(i_key), getOp);
    //		break;
    //            
    //	case MDHIM_LONG_INT_KEY:
    //		l_key = atol( str_key ) * (md->mdhim_rank + 1);
    //		sprintf(key_string, "%ld", l_key);
    //		if (verbose) tst_say(0, "# mdhimGet( %s, %s ) [long]\n", key_string, getValLabel(getOp));
    //		bgrm = mdhimGet(md, md->primary_index, &l_key, sizeof(l_key), getOp);
    //		break;
    //            
    //	case MDHIM_FLOAT_KEY:
    //		f_key = atof( str_key ) * (md->mdhim_rank + 1);
    //		sprintf(key_string, "%f", f_key);
    //		if (verbose) tst_say(0, "# mdhimGet( %s, %s ) [float]\n", key_string, getValLabel(getOp));
    //		bgrm = mdhimGet(md, md->primary_index, &f_key, sizeof(f_key), getOp);
    //		break;
    //            
    //	case MDHIM_DOUBLE_KEY:
    //		d_key = atof( str_key ) * (md->mdhim_rank + 1);
    //		sprintf(key_string, "%e", d_key);
    //		if (verbose) tst_say(0, "# mdhimGet( %s, %s ) [double]\n", key_string, getValLabel(getOp));
    //		bgrm = mdhimGet(md, md->primary_index, &d_key, sizeof(d_key), getOp);
    //		break;
    //                        
    //	case MDHIM_STRING_KEY:
    //	case MDHIM_BYTE_KEY:
    //sprintf(key_string, "%s", str_key);
	
    if (verbose) tst_say(0, "# mdhimGet( %s, %s ) [string|byte]\n", str_key, getValLabel(getOp));
    if(strcmp(ophandle,"EQUAL")==0) bgrm = mdhimGet(md, md->primary_index, (void *)str_key, BYTE_BUFLEN+1, getOp);
    else if(strcmp(ophandle,"PREV")==0) bgrm = mdhimBGetOp(md, md->primary_index, (void *)str_key, BYTE_BUFLEN+1, 1, getOp);
    else if(strcmp(ophandle,"NEXT")==0) bgrm = mdhimBGetOp(md, md->primary_index, (void *)str_key, BYTE_BUFLEN+1, 1, getOp);
    else if(strcmp(ophandle,"FIRST")==0) bgrm =mdhimBGetOp(md, md->primary_index, (void *)str_key, BYTE_BUFLEN+1, 1, getOp);
    else {
      if (strcmp(ophandle,"LAST")==0) bgrm = mdhimBGetOp(md, md->primary_index, (void *)str_key, BYTE_BUFLEN+1, 1, getOp);
    }
    //bgrm = mdhimGet(md, md->primary_index, (void *)str_key, BYTE_BUFLEN, getOp);
    //		break;
    // 
    //	default:mdhimBGet(md, md->primary_index, keys, key_lens, nkeys, MDHIM_GET_EQ);
    //		else {tst_say(1, "Error, unrecognized Key_type in execGet\n");
    //		return;
    //	}
    
    if (!bgrm || bgrm->error)
      {
	tst_say(1, "ERROR: rank %d getting value for key (%s): %s from MDHIM\n", 
		md->mdhim_rank, getValLabel(getOp), key_string);
      }
    //printf("This is  the strlen for bgrm->keys[0]: %s", bgrm->keys[0]);
    else if (bgrm->keys[0] && bgrm->values[0])
      {
	// Generate correct string from returned key
	switch (key_type)
	  {
	  case MDHIM_INT_KEY:
	    sprintf(returned_key, "[int]: %d", *((int *) bgrm->keys[0]));
	    break;
               
	  case MDHIM_LONG_INT_KEY:
	    sprintf(returned_key, "[long]: %ld", *((long *) bgrm->keys[0]));
	    break;
            
	  case MDHIM_FLOAT_KEY:
	    sprintf(returned_key, "[float]: %f", *((float *) bgrm->keys[0]));
	    break;
            
	  case MDHIM_DOUBLE_KEY:
	    sprintf(returned_key, "[double]: %e", *((double *) bgrm->keys[0]));
	    break;
                        
	  case MDHIM_STRING_KEY:
	  case MDHIM_BYTE_KEY:
	    sprintf(returned_key, "[string|byte]: %s", (char *)bgrm->keys[0]);
   
	  }
        
	//if ( v_value == NULL )  // No verification value, anything is OK.
	if (bgrm->values[0] != NULL || bgrm->value_lens[0] != 0)
	  {
	    tst_say(0, "Successfully get(%s) correct value: %s for key %s from MDHIM\n", 
		    getValLabel(getOp), expand_escapes(bgrm->values[0], bgrm->value_lens[0]+1), returned_key);
	  }

      }
    else
      {
	tst_say(1, "ERROR: rank %d got(%s) null value or return key for key: %s from MDHIM\n", 
		md->mdhim_rank, getValLabel(getOp), key_string);
      }

  }
  close(x);
}

//======================================BPUT============================
static void execBput(char *command, struct mdhim_t *md, int charIdx)
{
  int nkeys = 100;
  int ret;
  char buffer1 [ TEST_BUFLEN ];
  unsigned char str_key [ TEST_BUFLEN ];
  unsigned char value [ TEST_BUFLEN ];
  struct mdhim_brm_t *brm, *brmp;
  int i;// size_of;
  void **keys;
  int *key_lens;
  char **values;
  int *value_lens;    
  char fhandle [TEST_BUFLEN]; //Filename handle
  //int data_index
  //; //Index for reading data.
  char data_read[TEST_BUFLEN];


  if (verbose) tst_say(0, "# bput n key data\n" );


  //Initialize variables
  //size_of = 0;
  keys = NULL;

  // Number of keys to generate
  charIdx = getWordFromString( command, buffer1, charIdx);
  nkeys = atoi( buffer1 );

  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read));   

  charIdx = getWordFromString( command, fhandle, charIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  int g;
	
  key_lens = malloc(sizeof(int) * nkeys);
  value_lens = malloc(sizeof(int) * nkeys);

  if (verbose) tst_say(0, "# mdhimBPut(%d, %s, %s )\n", nkeys, str_key, value );

  // Allocate memory and size of key (size of string|byte key will be modified
  // when the key is constructed.)
  values = malloc(sizeof(char *) * nkeys);
  //	switch (key_type)
  //	{
  //	case MDHIM_INT_KEY:
  //		keys = malloc(sizeof(int *) * nkeys);
  //		size_of = sizeof(int);
  //		break;
  //           
  //	case MDHIM_LONG_INT_KEY:
  //		keys = malloc(sizeof(long *) * nkeys);
  //		size_of = sizeof(long);
  //		break;
  //           
  //	case MDHIM_FLOAT_KEY:
  //		keys = malloc(sizeof(float *) * nkeys);
  //		size_of = sizeof(float);
  //		break;
  //           
  //	case MDHIM_DOUBLE_KEY:
  //		keys = malloc(sizeof(double *) * nkeys);
  //		size_of = sizeof(double);
  //		break;
  //           
  //	case MDHIM_STRING_KEY:
  //	case MDHIM_BYTE_KEY:
  keys = malloc(sizeof(char *) * nkeys);
  //size_of = sizeof(char);
  //		break;
  //	}    

  //int q =0;
  //while (q==0) sleep(5);
  // Create the keys and values to store
  for (i = 0; i < nkeys; i++)
    {
      //data_index = 0;
      memset(str_key, 0, TEST_BUFLEN);
      memset(value, 0, TEST_BUFLEN);
      g=read(x, str_key, BYTE_BUFLEN);
      if( g<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}
      g=read(x, value, BYTE_BUFLEN);
      if( g<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}
    
      g=read(x, fhandle,1); //Need it to get rid of the new line.

      keys[i] = malloc(BYTE_BUFLEN+1);
      memset(keys[i], 0, BYTE_BUFLEN+1);
      key_lens[i] = BYTE_BUFLEN; //size_of;
      values[i] = malloc(sizeof(unsigned char) * BYTE_BUFLEN+1);
      memset(values[i], 0, BYTE_BUFLEN+1);
      memcpy(values[i], value, VAL_BUFLEN);
      value_lens[i] = BYTE_BUFLEN;//strlen(values[i]) + 1;
		
		

      // Based on key type, rank and index number generate a key
      //		switch (key_type)
      //		{
      //		case MDHIM_INT_KEY:
      //                {
      //			int **i_keys = (int **)keys;
      //			*i_keys[i] = (atoi( str_key ) * (md->mdhim_rank + 1));
      //			if (verbose) tst_say(0, "Rank: %d - Creating int key (to insert): "
      //					     "%d with value: %s\n", 
      //					     md->mdhim_rank, *i_keys[i], values[i]);
      //                }
      //                break;
      // 
      //		case MDHIM_LONG_INT_KEY:
      //                {
      //			long **l_keys = (long **)keys;
      //			*l_keys[i] = (atol( str_key ) * (md->mdhim_rank + 1));
      //			if (verbose) tst_say(0, "Rank: %d - Creating long key (to insert): "
      //					     "%ld with value: %s\n", 
      //					     md->mdhim_rank, *l_keys[i], values[i]);
      //                }
      //                break;
      //                
      //		case MDHIM_FLOAT_KEY:
      //                {
      //			float **f_keys = (float **)keys;
      //			*f_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
      //			if (verbose) tst_say(0, "Rank: %d - Creating float key (to insert): "
      //					     "%f with value: %s\n", 
      //					     md->mdhim_rank, *f_keys[i], values[i]);
      //		}
      //                break;
      // 
      //		case MDHIM_DOUBLE_KEY:
      //                {
      //			double **d_keys = (double **)keys;
      //			*d_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1));
      //			if (verbose) tst_say(0, "Rank: %d - Creating double key (to insert): "
      //					     "%e with value: %s\n", 
      //					     md->mdhim_rank, *d_keys[i], values[i]);
      //                }
      //                break;
      //                
      //		case MDHIM_STRING_KEY:
      //		case MDHIM_BYTE_KEY:
      //                {
      //unsigned char **s_keys = (unsigned char **)keys;
      //s_keys[i] = malloc(TEST_BUFLEN);
      //sprintf(s_keys[i], "%s", str_key);
      memcpy(keys[i], str_key, BYTE_BUFLEN);
      key_lens[i] = BYTE_BUFLEN;
      if (verbose) tst_say(0, "Rank: %d - Creating string|byte key "
			   "(to insert): %s with value: %s\n", 
			   md->mdhim_rank, (unsigned char *)keys[i], values[i]);
      //                }
      //                break;
      //}		
    }

  //Insert the keys into MDHIM
  brm = mdhimBPut(md, keys, key_lens, (void **) values, value_lens, nkeys, NULL, NULL);
  brmp = brm;
  ret = 0;
  if (!brm || brm->error)
    {
      tst_say(1, "ERROR: rank - %d bulk inserting keys/values into MDHIM\n", 
	      md->mdhim_rank);
      ret = 1;
    }
    
  while (brmp)
    {
      if (brmp->error < 0)
	{
	  tst_say(1, "ERROR: rank %d - Error bulk inserting key/values info MDHIM\n", 
		  md->mdhim_rank);
	  ret = 1;
	}

      brmp = brmp->next;
      //Free the message
      mdhim_full_release_msg(brm);
      brm = brmp;
    }
    
  // if NO errors report success
  if (!ret)
    {
      tst_say(0, "Rank: %d - Successfully bulk inserted key/values into MDHIM\n", 
	      md->mdhim_rank);
    }

  // Release memory
  freeKeyValueMem(nkeys, keys, key_lens, values, value_lens);
  close(x);
  //fclose(datafile);
}
        
//======================================BGET============================
static void execBget(char *command, struct mdhim_t *md, int charIdx)
{
  int nkeys = 100;
  char buffer [ TEST_BUFLEN ];
  unsigned char str_key [ TEST_BUFLEN ];
  char *v_value = NULL;
  if (verbose) tst_say(0, "# bget n key <verfication_value>\n" );
  struct mdhim_bgetrm_t *bgrm, *bgrmp;
  int i, /*size_of,*/ ret;
  void **keys;
  int *key_lens;
  int totRecds;
  char fhandle[TEST_BUFLEN];
  char data_read[TEST_BUFLEN];
  char opname[TEST_BUFLEN];
  //int data_index;
  //size_of = 0;
  keys = NULL;

  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read));   


  // Get the number of records to create for bget
  charIdx = getWordFromString( command, buffer, charIdx);
  nkeys = atoi( buffer );
  //printf("Here is the nkeys: %d\n", nkeys);
  charIdx = getWordFromString( command, opname, charIdx);
    
  charIdx = getWordFromString( command, fhandle, charIdx);
  //printf("Here is the file name: %s\n", fhandle);
  //	datafile = fopen( fhandle, "r" );
  //	if( !datafile )
  //	{
  //		fprintf( stderr, "Failed to open data file %s, %s\n", 
  //			 fhandle, strerror( errno ));
  //		exit( -1 );
  //	}

  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  int g;
  key_lens = malloc(sizeof(int) * nkeys);
 

  if (verbose) tst_say(0, "# mdhimBGet(%d, %s)\n", nkeys, str_key );

  // Allocate memory and size of key (size of string|byte key will be modified
  // when the key is constructed.)
  //	switch (key_type)
  //	{
  //	case MDHIM_INT_KEY:
  //		keys = malloc(sizeof(int *) * nkeys);
  //		size_of = sizeof(int);
  //		break;
  //           
  //	case MDHIM_LONG_INT_KEY:
  //		keys = malloc(sizeof(long *) * nkeys);
  //		size_of = sizeof(long);fcreat
  //		break;
  //           
  //	case MDHIM_FLOAT_KEY:
  //		keys = malloc(sizeof(float *) * nkeys);
  //		size_of = sizeof(float);
  //		break;
  //           
  //	case MDHIM_DOUBLE_KEY:
  //		keys = malloc(sizeof(double *) * nkeys);
  //		size_of = sizeof(double);
  //		break;
  //                 
  //	case MDHIM_STRING_KEY:
  //	case MDHIM_BYTE_KEY:
  keys = malloc(sizeof(unsigned char *) * nkeys);
  //size_of = sizeof(char);
  //		break;
  //	}
    

  // Generate the keys as set above
  for (i = 0; i < nkeys; i++)
    {   
      //data_index = 0;
      //		fgets(data_read, TEST_BUFLEN, datafile);
      memset(str_key, 0, BYTE_BUFLEN);
      //		data_index = getWordFromString( data_read, str_key, data_index);
      keys[i] = malloc(BYTE_BUFLEN+1);
      memset(keys[i],0,BYTE_BUFLEN+1);
      g=0;
      // Based on key type, rank and index number generate a key
      g=read(x, str_key, BYTE_BUFLEN);
      if( g<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}	
      g=read(x, fhandle,1); //Need it to get rid of the new line.
		
      memcpy(keys[i], str_key, BYTE_BUFLEN);
      key_lens[i] = BYTE_BUFLEN+1;
      if (verbose) tst_say(0, "Rank: %d - Creating string|byte key (to get):"
			   " %s\n", md->mdhim_rank, keys[i]);
      //                }
      //                break;
      //  
      //		default:
      //			tst_say(1, "Error, unrecognized Key_type in execBGet\n");
      //			return;
      //		}
      //            		
    }
    
  //Get the values back for each key retrieved;
  bgrm = mdhimBGet(md, md->primary_index, keys, key_lens, nkeys, MDHIM_GET_EQ);

	
  ret = 0; // Used to determine if any errors are encountered
    
  totRecds = 0;
  bgrmp = bgrm;

  while (bgrmp) {
    if (bgrmp->error < 0)
      {
	tst_say(1, "ERROR: rank %d retrieving values\n", md->mdhim_rank);
	ret = 1;
      }

    totRecds += bgrmp->num_keys;
    for (i = 0; i < bgrmp->num_keys && bgrmp->error >= 0; i++)
      {
	//if ( v_value != NULL ) 
	//				sprintf(buffer, "%s_%d", v_value, i + 1); //Value to verify

	if (verbose) tst_say(0, "Rank: %d successfully get[%d] correct value: %s from MDHIM key %s\n", 
			     md->mdhim_rank, i, expand_escapes(bgrmp->values[i], bgrmp->value_lens[i]+1), bgrmp->keys[i]);

      }

    bgrmp = bgrmp->next;
    //Free the message received
    mdhim_full_release_msg(bgrm);
    bgrm = bgrmp;
  }
    
  // if NO errors report success
  if (!ret)
    {
      if (totRecds)
	tst_say(0, "Rank: %d - Successfully bulk retrieved %d key/values from MDHIM\n", 
		md->mdhim_rank, totRecds);
      else
	tst_say(1, "ERROR: rank %d got no records for bulk retrieved from MDHIM\n", 
		md->mdhim_rank);
    }
    
  // Release memory
  freeKeyValueMem(nkeys, keys, key_lens, NULL, NULL);
  free(v_value);
  close(x);
}

//======================================BGETOP============================
static void execBgetOp(char *command, struct mdhim_t *md, int charIdx)
{
  int nrecs = 100;
  char buffer1 [ TEST_BUFLEN ];
  char key_string [ TEST_BUFLEN ];
  struct mdhim_bgetrm_t *bgrm;
  int i, getOp, newIdx, data_index;
  char ophandle[TEST_BUFLEN];
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  char fhandle[TEST_BUFLEN];
  char str_key[TEST_BUFLEN];
    
  if (verbose) tst_say(0, "# bgetop n key op\n" );
    
  bgrm = NULL;
  // Get the number of records to retrieve in bgetop
  charIdx = getWordFromString( command, buffer1, charIdx);
  nrecs = atoi( buffer1 );
    		
  // Get the key to use as starting point
	
  newIdx = getWordFromString( command, ophandle, charIdx);
  if (newIdx != charIdx)
    {
      charIdx = newIdx;
      if(strcmp(ophandle,"EQUAL")==0) getOp=MDHIM_GET_EQ;
      else if(strcmp(ophandle,"PREV")==0) getOp=MDHIM_GET_PREV;
      else if(strcmp(ophandle,"NEXT")==0) getOp=MDHIM_GET_NEXT;
      else if(strcmp(ophandle,"FIRST")==0) getOp=MDHIM_GET_FIRST;
      else if(strcmp(ophandle,"LAST")==0) getOp=MDHIM_GET_LAST;
      else getOp=MDHIM_GET_EQ;
    }
  else
    {
      getOp = MDHIM_GET_EQ;  //Default a get with an equal operator
    }
  charIdx = getWordFromString( command, fhandle, charIdx);	
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }

  // Get the operation type to use

  if (verbose) tst_say(0, "# mdhimBGetOp(%d, %s)\n", 
		       nrecs, getValLabel(getOp) );

	
  data_index = read(x, str_key, BYTE_BUFLEN);
  if( data_index<0 )
    {
      fprintf( stderr, "Failed to read data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  data_index=read(x,buffer1, 1);
  // Based on key type generate a key using rank 
  //	switch (key_type)
  //	{
  //	case MDHIM_INT_KEY:
  //		i_key = atoi( key_string ) * (md->mdhim_rank + 1) + 1;
  //		sprintf(key_string, "%d", i_key);
  //		if (verbose) tst_say(0, "# mdhimBGetOp( %d, %s, %s ) [int]\n", 
  //				     nrecs, key_string, getValLabel(getOp));
  //		bgrm = mdhimBGetOp(md, md->primary_index, &i_key, sizeof(i_key), nrecs, getOp);
  //		break;
  //            
  //	case MDHIM_LONG_INT_KEY:
  //		l_key = atol( key_string ) * (md->mdhim_rank + 1) + 1;
  //		sprintf(key_string, "%ld", l_key);
  //		if (verbose) tst_say(0, "# mdhimBGetOp( %d, %s, %s ) [long]\n", 
  //				     nrecs, key_string, getValLabel(getOp));
  //		bgrm = mdhimBGetOp(md, md->primary_index, &l_key, sizeof(l_key), nrecs, getOp);
  //		break;
  //            
  //	case MDHIM_FLOAT_KEY:
  //		f_key = atof( key_string ) * (md->mdhim_rank + 1) + 1;
  //		sprintf(key_string, "%f", f_key);
  //		if (verbose) tst_say(0, "# mdhimBGetOp( %d, %s, %s ) [float]\n", 
  //				     nrecs, key_string, getValLabel(getOp));
  //		bgrm = mdhimBGetOp(md, md->primary_index, &f_key, sizeof(f_key), nrecs, getOp);
  //		break;
  //            
  //	case MDHIM_DOUBLE_KEY:
  //		d_key = atof( key_string ) * (md->mdhim_rank + 1) + 1;
  //		sprintf(key_string, "%e", d_key);
  //		if (verbose) tst_say(0, "# mdhimBGetOp( %d, %s, %s ) [double]\n", 
  //				     nrecs, key_string, getValLabel(getOp));
  //		bgrm = mdhimBGetOp(md, md->primary_index, &d_key, sizeof(d_key), nrecs, getOp);
  //		break;
  //                        
  //	case MDHIM_STRING_KEY://	case MDHIM_STRING_KEY:

  //	case MDHIM_BYTE_KEY:
  memset(key_string, 0, TEST_BUFLEN);
  memcpy(key_string, str_key, BYTE_BUFLEN);
  //sprintf(key_string, "%s", key_string);
  //if (verbose) tst_say(0, "# mdhimBGetOp( %d, %s, %s ) [string|byte]\n", 
  // nrecs, key_string, getValLabel(getOp));
  bgrm = mdhimBGetOp(md, md->primary_index, (void *)key_string, BYTE_BUFLEN+1, 
		     nrecs, getOp);
  //		break;
  // 
  //	default:
  //		tst_say(1, "Error, unrecognized Key_type in execGet\n");
  //	}
    
  if (!bgrm || bgrm->error)
    {
      tst_say(1, "ERROR: rank %d getting %d values for start key (%s): %s from MDHIM\n", 
	      md->mdhim_rank, nrecs, getValLabel(getOp), key_string);
    }
  else if (verbose)
    {
      for (i = 0; i < bgrm->num_keys && !bgrm->error; i++)
	{			
	  tst_say(0, "Rank: %d - Got value[%d]: %s for start key: %s from MDHIM\n", 
		  md->mdhim_rank, i, expand_escapes(bgrm->values[i], bgrm->value_lens[i]+1), 
		  key_string);
	}
    }
  else
    {
      tst_say(0, "Rank: %d - Successfully got %d values for start key: %s from MDHIM\n", 
	      md->mdhim_rank, bgrm->num_keys, key_string);
    }
    
  //Free the message received
  mdhim_full_release_msg(bgrm);
}
        
//======================================DEL============================
static void execDel(char *command, struct mdhim_t *md, int charIdx)
{
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  char str_key [ TEST_BUFLEN ];
  char key_string [ TEST_BUFLEN ];
  struct mdhim_brm_t *brm;
  char fhandle [TEST_BUFLEN]; //Filename handle
  int data_index =0; //Index for reading data.
  char data_read[TEST_BUFLEN];
  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read));   

  charIdx = getWordFromString( command, fhandle, charIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  data_index=read(x, str_key, BYTE_BUFLEN);
  if( data_index<0 )
    {
      fprintf( stderr, "Failed to read data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }

  if (verbose) tst_say(0, "# del key\n" );

  brm = NULL;
  //charIdx = getWordFromString( data_read, str_key, data_index);
    
  //	switch (key_type)
  //	{
  //	case MDHIM_INT_KEY:
  //		i_key = atoi( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%d", i_key);
  //		if (verbose) tst_say(0, "# mdhimDelete( %s ) [int]\n", key_string);
  //		brm = mdhimDelete(md, md->primary_index, &i_key, sizeof(i_key));
  //		break;
  //            
  //	case MDHIM_LONG_INT_KEY:
  //		l_key = atol( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%ld", l_key);
  //		if (verbose) tst_say(0, "# mdhimDelete( %s ) [long]\n", key_string);
  //		brm = mdhimDelete(md, md->primary_index, &l_key, sizeof(l_key));
  //		break;
  //            
  //	case MDHIM_FLOAT_KEY:
  //		f_key = atof( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%f", f_key);
  //		if (verbose) tst_say(0, "# mdhimDelete( %s ) [float]\n", key_string);
  //		brm = mdhimDelete(md, md->primary_index, &f_key, sizeof(f_key));
  //		break;
  //            
  //	case MDHIM_DOUBLE_KEY:
  //		d_key = atof( str_key ) * (md->mdhim_rank + 1);
  //		sprintf(key_string, "%e", d_key);
  //		if (verbose) tst_say(0, "# mdhimDelete( %s ) [double]\n", key_string);
  //		brm = mdhimDelete(md, md->primary_index, &d_key, sizeof(d_key));
  //		break;
  //                        
  //	case MDHIM_STRING_KEY:
  //	case MDHIM_BYTE_KEY:
  //sprintf(key_string, "%s", str_key);
  if (verbose) tst_say(0, "# mdhimDelete( %s ) [string|byte]\n", key_string);
  brm = mdhimDelete(md, md->primary_index, (void *)str_key, BYTE_BUFLEN);
  //		break;
  // 
  //	default:
  //		tst_say(1, "Error, unrecognized Key_type in execDelete\n");
  //	}

  if (!brm || brm->error)
    {
      tst_say(1, "ERROR: rank %d deleting key/value from MDHIM. key: %s\n", 
	      md->mdhim_rank, str_key);
    }
  else
    {
      tst_say(0, "Successfully deleted key/value from MDHIM. key: %s\n", str_key);
    }

}

//======================================BDEL============================
static void execBdel(char *command, struct mdhim_t *md, int charIdx)
{
  int nkeys = 100;
  char buffer1 [ TEST_BUFLEN ];
  unsigned char str_key [ TEST_BUFLEN ];
  void **keys;
  int *key_lens;
  struct mdhim_brm_t *brm, *brmp;
  int i, /*size_of, */ ret;
  char fhandle [TEST_BUFLEN]; //Filename handle
  int data_index; //Index for reading data.
  char data_read[TEST_BUFLEN];
  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read)); 
    
  if (verbose) tst_say(0, "# bdel n key\n" );
    
  keys = NULL;
  //size_of = 0;

  // Number of records to delete
  charIdx = getWordFromString( command, buffer1, charIdx);
  nkeys = atoi( buffer1 );
  key_lens = malloc(sizeof(int) * nkeys);

  // Starting key value
  charIdx = getWordFromString( command, fhandle, charIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  //	datafile = fopen( fhandle, "r" );
  //	if( !datafile )
  //	{
  //		fprintf( stderr, "Failed to open data file %s, %s\n", 
  //			 fhandle, strerror( errno ));
  //		exit( -1 );
  //	}


  if (verbose) tst_say(0, "# mdhimBDelete(%d, %s )\n", nkeys, str_key );
    
  // Allocate memory and size of key (size of string|byte key will be modified
  // when the key is constructed.)
  //	switch (key_type)
  //	{
  //	case MDHIM_INT_KEY:
  //		keys = malloc(sizeof(int *) * nkeys);
  //		size_of = sizeof(int);
  //		break;
  //           
  //	case MDHIM_LONG_INT_KEY:
  //		keys = malloc(sizeof(long *) * nkeys);
  //		size_of = sizeof(long);
  //		break;
  //           
  //	case MDHIM_FLOAT_KEY:
  //		keys = malloc(sizeof(float *) * nkeys);
  //		size_of = sizeof(float);
  //		break;
  //           
  //	case MDHIM_DOUBLE_KEY:
  //		keys = malloc(sizeof(double *) * nkeys);
  //		size_of = sizeof(double);
  //		break;
  //                 
  //	case MDHIM_STRING_KEY:
  //	case MDHIM_BYTE_KEY:
  keys = malloc(sizeof(char *) * nkeys);
  //		size_of = sizeof(char);
  //		break;
  //	}

  for (i = 0; i < nkeys; i++)
    {
      data_index =0;
      data_index=read(x, str_key, BYTE_BUFLEN);
      if( data_index<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}

      keys[i] = malloc(BYTE_BUFLEN+1);
      key_lens[i] = BYTE_BUFLEN;
      data_index=read(x, fhandle,1); //Need it to get rid of the new line.

      // Based on key type, rank and index number generate a key
      //		switch (key_type)
      //		{
      //		case MDHIM_INT_KEY:
      //                {
      //			int **i_keys = (int **)keys;
      //			*i_keys[i] = (atoi( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
      //			if (verbose) tst_say(0, "Rank: %d - Creating int key (to delete): %d\n", 
      //					     md->mdhim_rank, *i_keys[i]);
      //                }
      //                break;
      // 
      //		case MDHIM_LONG_INT_KEY:
      //                {
      //			long **l_keys = (long **)keys;
      //			*l_keys[i] = (atol( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
      //			if (verbose) tst_say(0, "Rank: %d - Creating long key (to delete): %ld\n", 
      //					     md->mdhim_rank, *l_keys[i]);
      //                }
      //                break;
      // 
      //		case MDHIM_FLOAT_KEY:
      //                {
      //			float **f_keys = (float **)keys;
      //			*f_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
      //			if (verbose) tst_say(0, "Rank: %d - Creating float key (to delete): %f\n", 
      //					     md->mdhim_rank, *f_keys[i]);
      //                }
      //                break;
      // 
      //		case MDHIM_DOUBLE_KEY:
      //                {
      //			double **d_keys = (double **)keys;
      //			*d_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
      //			if (verbose) tst_say(0, "Rank: %d - Creating double key (to delete): "
      //					     " %e\n", md->mdhim_rank, *d_keys[i]);
      //                }
      //                break; 
      // 
      //		case MDHIM_STRING_KEY:
      //		case MDHIM_BYTE_KEY:
      //                {
      // 			char **s_keys = (char **)keys;
      // 			s_keys[i] = malloc(BYTE_BUFLEN);
      memset(keys[i],0,BYTE_BUFLEN+1);
      memcpy(keys[i], str_key, BYTE_BUFLEN);
      key_lens[i] = BYTE_BUFLEN; 
      if (verbose) tst_say(0, "Rank: %d - Creating string|byte key (to delete):"
			   " %s\n", md->mdhim_rank, keys[i]);
      //                }
      //                break;
      //  
      //		default:
      //			tst_say(1, "Error, unrecognized Key_type in execBDel\n");
      //			return;
      //		}
            
    }

  //Delete the records
  brm = mdhimBDelete(md, md->primary_index, (void **) keys, key_lens, nkeys);
  brmp = brm;
  if (!brm || brm->error) {
    tst_say(1, "ERROR: rank %d deleting keys/values from MDHIM\n", 
	    md->mdhim_rank);
  } 
    
  ret = 0;
  while (brmp)
    {
      if (brmp->error < 0)
	{
	  tst_say(1, "ERROR: rank %d deleting keys\n", md->mdhim_rank);
	  ret = 1;
	}

      brmp = brmp->next;
      //Free the message
      mdhim_full_release_msg(brm);
      brm = brmp;
    }
    
  // if NO errors report success
  if (!ret)
    {
      tst_say(0, "Rank: %d - Successfully bulk deleted key/values from MDHIM\n", 
	      md->mdhim_rank);
    }

  // Release memory
  freeKeyValueMem(nkeys, keys, key_lens, NULL, NULL);
  close(x);
}

// Generate a random string of up to max_len
char *random_string( int max_len, int exact_size)
{
  int len;
  char *retVal;
  int i;

  if (exact_size)
    len = max_len;
  else
    len = rand() % max_len + 1;
  retVal = (char *) malloc( len + 1 );

  for (i=0; i<len; ++i)
    retVal[i] = sourceChars[ rand() % sc_len ];

  retVal[len] = '\0';
  return retVal;
} 

//======================================NPUT============================
static void execNput(char *command, struct mdhim_t *md, int charIdx)
{
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  struct mdhim_brm_t *brm;
  char buffer [ TEST_BUFLEN ];
  unsigned buffer2[TEST_BUFLEN];
  unsigned char key_string[TEST_BUFLEN]; 
  unsigned char value[TEST_BUFLEN];
  int n_iter;
  int  i;
  int ret;
  char fhandle [TEST_BUFLEN]; //Filename handle
  //int data_index; //Index for reading data.
  char data_read[TEST_BUFLEN];
  unsigned char str_key[TEST_BUFLEN];
  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read)); 
    
  brm = NULL;
  ret = 0;
    
  if (verbose) tst_say(0, "# nput n key_length data_length exact_size\n" );
    
  charIdx = getWordFromString( command, buffer, charIdx);
  n_iter = atoi( buffer ); // Get number of iterations

  charIdx = getWordFromString( command, fhandle, charIdx);
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
  int g;
    
  //	key_len = atoi( buffer ); // For string/byte key types otherwise ignored (but required)
  //    
  //	charIdx = getWordFromString( command, buffer, charIdx);
  //	value_len = atoi( buffer ); // Get maximum length of value string
  //    
  //	charIdx = getWordFromString( command, buffer, charIdx);
  //	// If zero strings are of the exact length stated above, otherwise they are
  // of variable length up to data_len or key_len.
  //rand_str_size = atoi( buffer );  
    
  for (i=0; i<n_iter; i++)
    {
      //data_index =0;
      //if (i > 0) free(value);
      memset(str_key, 0, sizeof(str_key));   
      memset(buffer2, 0, sizeof(buffer2));    
      g=read(x, str_key, BYTE_BUFLEN);
      if( g<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}
      //		data_index= getWordFromString( data_read, str_key, data_index);
      //		data_index = getWordFromString( data_read, value, data_index);

      g=read(x, buffer2, BYTE_BUFLEN);
      if( g<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}
      g=read(x, fhandle,1); //Need it to get rid of the new line.
      // Based on key type generate appropriate random key
      //		switch (key_type)
      //		{
      //		case MDHIM_INT_KEY:
      //			i_key = atoi(str_key) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%d", i_key);
      //			if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [int]\n", 
      //					     key_string, value );
      //			brm = mdhimPut(md, &i_key, sizeof(i_key), value, strlen(value)+1, NULL, NULL);
      //			break;
      // 
      //		case MDHIM_LONG_INT_KEY:
      //			l_key = atol(str_key) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%ld", l_key);
      //			if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [long]\n", 
      //					     key_string, value );
      //			brm = mdhimPut(md, &l_key, sizeof(l_key), value, strlen(value)+1, NULL, NULL);
      //			break;
      // 
      //		case MDHIM_FLOAT_KEY:
      //			f_key = atof( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%f", f_key);
      //			if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [float]\n", 
      //					     key_string, value );
      //			brm = mdhimPut(md, &f_key, sizeof(f_key), value, strlen(value)+1, NULL, NULL);
      //			break;
      // 
      //		case MDHIM_DOUBLE_KEY:
      //			d_key = atof( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%e", d_key);
      //			if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [double]\n", 
      //					     key_string, value );
      //			brm = mdhimPut(md, &d_key, sizeof(d_key), value, strlen(value)+1, NULL, NULL);
      //			break;
      // 
      //		case MDHIM_STRING_KEY:
      //		case MDHIM_BYTE_KEY:
			
      memset(value,0 ,VAL_BUFLEN+1);
      memset(key_string, 0,BYTE_BUFLEN+1);
      memcpy(key_string, str_key,BYTE_BUFLEN);
      memcpy(value, buffer2,VAL_BUFLEN);
      if (verbose) tst_say(0, "# mdhimPut( %s, %s ) [string|byte]\n", 
			   key_string, value );
      brm = mdhimPut(md, (void *)key_string, BYTE_BUFLEN, 
		     value, BYTE_BUFLEN, NULL, NULL);
      //			break;
      // 
      //		default:
      //			tst_say(1, "Error, unrecognized Key_type in execNput\n");
      //		}
        
      // Record any error(s)
      if (!brm || brm->error)
	{
	  if (verbose) tst_say(1, "ERROR: rank %d N putting key: %s with value: %s "
			       "into MDHIM\n", md->mdhim_rank, key_string, value);
	  ret ++;
	}
  
    }

  // Report any error(s)
  if (ret)
    {
      tst_say(1, "ERROR: rank %d - %d error(s) N putting key/value into MDHIM\n", 
	      md->mdhim_rank, ret);
    }
  else
    {
      tst_say(0, "Successfully N put %d key/values into MDHIM\n", n_iter);
    }

  close(x);
}

//======================================NGETN============================
static void execNgetn(char *command, struct mdhim_t *md, int charIdx)
{
  //	int i_key;
  //	long l_key;
  //	float f_key;
  //	double d_key;
  struct mdhim_bgetrm_t *bgrm;
  char buffer [ TEST_BUFLEN ];
  unsigned char key_string[TEST_BUFLEN];
  int n_iter;
  int ret, i;
  int getOp;
  //int newIdx;
  char fhandle [TEST_BUFLEN]; //Filename handle
  int data_index =0; //Index for reading data.
  char data_read[TEST_BUFLEN];
  unsigned char str_key[TEST_BUFLEN];
  char buffer2[TEST_BUFLEN];
  memset(fhandle, 0, sizeof(fhandle));
  memset(data_read, 0, sizeof(data_read)); 
	
  charIdx = getWordFromString( command, buffer, charIdx);
  n_iter = atoi( buffer ); // Get number of iterations
	
    	
  charIdx = getWordFromString( command, fhandle, charIdx);  
  int x = open( fhandle, O_RDONLY );
  if( x <0 )
    {
      fprintf( stderr, "Failed to open data file %s, %s\n", 
	       fhandle, strerror( errno ));
      exit( -1 );
    }
	

  //	datafile = fopen( fhandle, "r" );
  //	if( !datafile )
  //	{
  //		fprintf( stderr, "Failed to open data file %s, %s\n", 
  //			 fhandle, strerror( errno ));
  //		exit( -1 );
  //	}
  for (i=0; i<n_iter; i++)
    {
      data_index = 0;
      memset(buffer2, 0, sizeof(buffer2)); 
      memset(str_key, 0, sizeof(str_key)); 
      //fgets(data_read, TEST_BUFLEN, datafile);
	
      //		data_index= getWordFromString( data_read, str_key, data_index);
      //		newIdx = getWordFromString( data_read, buffer2, data_index);
      //    
      data_index=read(x, str_key, BYTE_BUFLEN);
      if( data_index<0 )
	{
	  fprintf( stderr, "Failed to read data file %s, %s\n", 
		   fhandle, strerror( errno ));
	  exit( -1 );
	}
      data_index=read(x, fhandle,1);
      // 	newIdx=read(x, buffer2, sizeof(int));
      // 	if( newIdx<0 )
      // 	{
      // 		fprintf( stderr, "Failed to read data file %s, %s\n", 
      // 			 fhandle, strerror( errno ));
      // 		exit( -1 );
      // 	}
      // 		if (newIdx != data_index)
      // 		{
      // 			getOp = atoi(buffer2); // Get operation type
      // 			data_index = newIdx;
      // 		}
      // 		else
      // 		{
      getOp = MDHIM_GET_EQ;  //Default a get with an equal operator
      // 		}
      //     
      bgrm = NULL;
      memset(key_string, 0, TEST_BUFLEN);
      ret = 0;
    
      if (verbose) tst_say(0, "# ngetn n key_length exact_size\n" );
    
      //	key_len = atoi( buffer ); // For string/byte key types otherwise ignored (but required)

    
    
      //charIdx = getWordFromString( command, buffer, charIdx);
      // If zero strings are of the exact length stated above, otherwise they are
      // of variable length up to key_len.
      //rand_str_size = atoi( buffer );
    
      // Based on key type generate a random first key or use zero if key_len = 0
      //		switch (key_type)
      //		{
      //		case MDHIM_INT_KEY:
      //			i_key = atoi( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%d", i_key);
      //			if (verbose) tst_say(0, "# mdhimGet( %s ) [int]\n", key_string );
      //			bgrm = mdhimGet(md, md->primary_index, &i_key, sizeof(i_key), getOp);
      //			break;
      // 
      //		case MDHIM_LONG_INT_KEY:
      //			l_key = atol( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%ld", l_key);
      //			if (verbose) tst_say(0, "# mdhimGet( %s ) [long]\n", key_string );
      //			bgrm = mdhimGet(md, md->primary_index, &l_key, sizeof(l_key), getOp);
      //			break;
      // 
      //		case MDHIM_FLOAT_KEY:
      //			f_key = atof( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%f", f_key);
      //			if (verbose) tst_say(0, "# mdhimGet( %s ) [float]\n", key_string );
      //			bgrm = mdhimGet(md, md->primary_index, &f_key, sizeof(f_key), getOp);
      //			break;
      // 
      //		case MDHIM_DOUBLE_KEY:
      //			d_key = atof( str_key ) * (md->mdhim_rank + 1);
      //			sprintf(key_string, "%e", d_key);
      //			if (verbose) tst_say(0, "# mdhimGet( %s ) [double]\n", key_string );
      //			bgrm = mdhimGet(md, md->primary_index, &d_key, sizeof(d_key), getOp);
      //			break;
      // 
      //		case MDHIM_STRING_KEY:
      //		case MDHIM_BYTE_KEY:
      memcpy(key_string, str_key, BYTE_BUFLEN);
      if (verbose) tst_say(0, "# mdhimGet( %s ) [string|byte]\n", key_string );
      bgrm = mdhimGet(md, md->primary_index, (void *)key_string, 
		      BYTE_BUFLEN, getOp);
      //			break;
      // 
      //		default:
      //			tst_say(1, "Error, unrecognized Key_type in execNgetn\n");
      //		}

      // Record any error(s)
      //	if (!grm || grm->error)
      //	{
      //		// For some reason could not get first record abort the request
      //		tst_say(1, "ERROR: rank %d N getting FIRST key: %s from MDHIM\n", 
      //			md->mdhim_rank, key_string);
      //		ret++;
      //	}
      //	else if (grm->key && grm->value)
      //	{
      //		if (verbose) tst_say(0, "Successfully got FIRST value: %s for key "
      //				     "[string|byte](%s) from MDHIM\n", 
      //				     expand_escapes(grm->value, grm->value_len), 
      //				     getValLabel(MDHIM_GET_NEXT));
      //        

      //grm = mdhimGet(md, grm->key, grm->key_len, MDHIM_GET_NEXT);
      // Record any error(s)
      if (!bgrm || bgrm->error)
	{
	  tst_say(1, "ERROR: rank %d N getting key[%d] from MDHIM. Abort request.\n", 
		  md->mdhim_rank, i);
	  ret ++;
	  break;
	}
      else if (bgrm->keys[0] && bgrm->values[0])
	{
	  if (verbose) tst_say(0, "Successfully got  value %d: %s for key "
			       "[string|byte](%s) from MDHIM\n", i,
			       expand_escapes(bgrm->values[0], bgrm->value_lens[0]),
			       getValLabel(getOp));
	}
      else
	{
	  tst_say(1, "ERROR: rank %d got null value or key at N get key[%d] "
		  "from MDHIM. Abort request.\n", md->mdhim_rank, i);
	  ret ++;
	  break;
	}
    }
  //	else
  //	{
  //		tst_say(1, "ERROR: rank %d got null value or return  key for FIRST key (%s): %s from MDHIM\n", 
  //			md->mdhim_rank, getValLabel(getOp), key_string);
  //		ret++;
  //	}

  // Report any error(s)
  if (ret)
    {
      tst_say(1, "ERROR: rank %d got %d error(s) N getting key/value from MDHIM\n", 
	      md->mdhim_rank, ret);
    }
  else
    {
      tst_say(0, "Successfully N got %d out of %d key/values desired from MDHIM\n", 
	      i, n_iter);
    }
  close(x);
}

/**
 * test frame for the MDHIM
 *
 * This test frame calls the MDHIM subroutines, it is an interactive or batch file
 * test frame which could be used for regression tests.
 * 
 * When the program is called in verbose mode (not quiet) it also writes a log file
 * for each rank with the name mdhimTst-#.log (where # is the rank)
 *
 * <B>Interactive mode or commands read from input file.</B>
 * -- Interactive mode simply execute mdhimtst, (by default it is verbose)
 * -- Batch mode mdhimtst -f <file_name> -d <databse_type> -t <key_type> <-quiet>
 * <UL>
 * Call the program mdhimtst from a UNIX or DOS shell. (with a -f<filename> for batch mode)
 * <BR>
 * Use the following commands to test the MDHIM subroutines supplied:
 * <UL>
 * <PRE>
 q       FOR QUIT
 /////////open filename 
 /////////transaction < START | COMMIT | ROLLBACK >
 /////////close
 /////////flush
 put key data
 bput n key data
 /////////find index key < LT | LE | FI | EQ | LA | GE | GT >
 /////////nfind n index key < LT | LE | FI | EQ | LA | GE | GT >
 get key
 bget n key
 del key
 bdel n key
 /////////datalen
 /////////readdata
 /////////readkey index
 /////////updatedata data
 /////////updatekey index key
 </PRE>
 * </UL>
 * Do the following if you want to run the test cases per hand
 * <PRE>
 1. Build the mdhimtst executable.          make all
 2. Run the test frame on a file.        mdhimtst tst0001.TST
 </PRE>
 *
 * </UL>
 */

int main( int argc, char * argv[] )
{
  char     commands[ 1000 ] [ TEST_BUFLEN ]; // Command to be read
  int      cmdIdx = 0; // Command current index
  int      cmdTot = 0; // Total number of commands read
  int      charIdx; // Index to last processed character of a command line
  char     command  [ TEST_BUFLEN ];
  char     filename [ TEST_BUFLEN ];
  char     *db_path = "mdhimTst";
  char     *db_name = "mdhimTst-";
  int      dowork = 1;
  int      dbug = 1; //MLOG_CRIT=1, MLOG_DBG=2
  int      factor = 1; //Range server factor
  int      slice = 100000; //Range server slice size

  struct timeval begin, end;
  long double   time_spent;
    
  mdhim_options_t *db_opts; // Local variable for db create options to be passed
    
  int ret;
  int provided = 0;
  struct mdhim_t *md;
    
  int db_type = LEVELDB; //(data_store.h) 
  MPI_Comm comm;
  //Variables to get range for 
  char *rs, *rso;  //Holders for strsep 
  int ri = 0, rf = 0; //Rank range initial and rank range final
  int rd; //Rank divider
  //int rdc = 1; //Rank divider control
  int rrc = 0; //Rank range control

  // Process arguments
  infile = stdin;
  while ((argc > 1) && (argv[1][0] == '-'))
    {
      switch (argv[1][1])
	{
	case 'f':
	  printf("Input file: %s || ", &argv[1][2]);
	  infile = fopen( &argv[1][2], "r" );
	  if( !infile )
	    {
	      fprintf( stderr, "Failed to open %s, %s\n", 
		       &argv[1][2], strerror( errno ));
	      exit( -1 );
	    }
	  break;

	case 'l':
	  printf("Key length: %s || ", &argv[1][2]);
	  BYTE_BUFLEN = atoi( &argv[1][2]);
	  break;

			
	case 'v':
	  printf("Value length: %s || ", &argv[1][2]);
	  VAL_BUFLEN = atoi( &argv[1][2]);
	  break;
			
	case 'd': // DataBase type (1=levelDB)
	  printf("Data Base type: %s || ", &argv[1][2]);
	  db_type = atoi( &argv[1][2] );
	  break;

	case 't':
	  printf("Key type: %s || ", &argv[1][2]);
	  key_type = atoi( &argv[1][2] );
	  break;

	case 'b':
	  printf("Debug mode: %s || ", &argv[1][2]);
	  dbug = atoi( &argv[1][2] );
	  break;
                
	case 'p':
	  printf("DB Path: %s || ", &argv[1][2]);
	  db_path = &argv[1][2];
	  break;
                
	case 'n':
	  printf("DB name: %s || ", &argv[1][2]);
	  db_name = &argv[1][2];
	  break;

	case 'c':
	  printf("Range server factor: %s || ", &argv[1][2]);
	  factor = atoi( &argv[1][2] );
	  break;  

	case 's':
	  printf("Range server slice size: %s || ", &argv[1][2]);
	  slice = atoi( &argv[1][2] );
	  break; 
 
	case 'a':
	  printf("DB option append value is on || ");
	  dbOptionAppend = MDHIM_DB_APPEND;
	  break;

	case 'q':
	  to_log = atoi( &argv[1][2] );
	  if (!to_log) 
	    {
	      printf("Quiet mode || ");
	      verbose = 0;
	    }
	  else
	    {
	      printf("Quiet to_log file mode || ");
	    }
	  break;
	case 'r':
	  rs=strdup(&argv[1][2]); 
	  rso = strsep(&rs, "~");
	  ri = atoi(rso);
	  rf = atoi(rs);
	  printf("Range: %d to %d || ", ri, rf);
	  break;
	case 'w':
	  rd=atoi(&argv[1][2]); 
	  printf("Range divider : %d ||", rd);
	  break;
	case 'h':
	  usage();
	  break;
	default:
	  printf("Wrong Argument (it will be ignored): %s\n", argv[1]);
	  usage();
	}

      ++argv;
      --argc;
    }
  printf("\n");
    
  // Set the debug flag to the appropriate Mlog mask
  switch (dbug)
    {
    case 2:
      dbug = MLOG_DBG;
      break;
            
    default:
      dbug = MLOG_CRIT;
    }
    
  // calls to init MPI for mdhim
  argc = 1;  // Ignore other parameters passed to program
  ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (ret != MPI_SUCCESS)
    {
      printf("Error initializing MPI with threads\n");
      exit(1);
    }

  if (provided != MPI_THREAD_MULTIPLE)
    {
      printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
      exit(1);
    }

  // Create options for DB initialization
  db_opts = mdhim_options_init();
  mdhim_options_set_db_path(db_opts, db_path);
  mdhim_options_set_db_name(db_opts, db_name);
  mdhim_options_set_db_type(db_opts, db_type);
  mdhim_options_set_key_type(db_opts, key_type);
  mdhim_options_set_debug_level(db_opts, dbug);
  mdhim_options_set_login_c(db_opts, "localhost", "root", "pass", "localhost", "stater", "pass");
  mdhim_options_set_server_factor(db_opts, factor);
  mdhim_options_set_max_recs_per_slice(db_opts, slice);
  mdhim_options_set_value_append(db_opts, dbOptionAppend);  // Default is overwrite
    
  //Setup Login credientials to database
  //mdhim_options_set_login_c(db_opts, host server, user name, user' password, statstics user name, statistic user name's password);
  comm = MPI_COMM_WORLD;
  md = mdhimInit(&comm, db_opts);
  if (!md)
    {
      printf("Error initializing MDHIM\n");
      exit(1);
    }
   
  /* initialization for random string generation */
  srand( time( NULL ) + md->mdhim_rank);
  sc_len = strlen( sourceChars );

  /*
   * open the log file (one per rank if in verbose mode, otherwise write to stderr)
   */
  if (verbose)
    {
      sprintf(filename, "./%s%d.log", db_name, md->mdhim_rank);
      logfile = fopen( filename, "wb" );
      if( !logfile )
	{
	  fprintf( stderr, "can't open logfile, %s, %s\n", filename,
		   strerror( errno ));
	  exit( 1 );
	}
    }
  else
    {
      logfile = stderr;
    }
    
  // Read all command(s) to execute
  while( dowork && cmdIdx < 1000)
    {
      // read the next command
      memset( commands[cmdIdx], 0, sizeof( command ));
      errno = 0;
      getLine( commands[cmdIdx]);
        
      if (verbose) tst_say(0, "\n##command %d: %s\n", cmdIdx, commands[cmdIdx]);
        
      // Is this the last/quit command?
      if( commands[cmdIdx][0] == 'q' || commands[cmdIdx][0] == 'Q' )
	{
	  dowork = 0;
	}
      cmdIdx++;
    }
  cmdTot = cmdIdx -1;

  // Main command execute loop
  for(cmdIdx=0; cmdIdx < cmdTot; cmdIdx++)
    {
	
      memset( command, 0, sizeof( command ));
      errno = 0;
        
      charIdx = getWordFromString( commands[cmdIdx], command, 0);

      if (verbose) tst_say(0, "\n##exec command: %s\n", command );
      gettimeofday(&begin, NULL);
      // execute the command given
      if( !strcmp( command, "put" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execPut(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "get" ))
	{
			
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc)execGet(commands[cmdIdx], md, charIdx);
	}
      else if ( !strcmp( command, "bput" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execBput(commands[cmdIdx], md, charIdx);
	}
      else if ( !strcmp( command, "bget" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execBget(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "del" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execDel(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "bdel" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execBdel(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "flush" ))
	{
	  execFlush(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "nput" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execNput(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "ngetn" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execNgetn(commands[cmdIdx], md, charIdx);
	}
      else if( !strcmp( command, "bgetop" ))
	{
	  if (check_rank_range(md->mdhim_rank, ri, rf)==rrc) execBgetOp(commands[cmdIdx], md, charIdx);
	}
      else
	{
	  printf( "# q       FOR QUIT\n" );
	  //printf( "# open filename keyfile1,dkeyfile2,... update\n" );
	  //printf( "# close\n" );
	  printf( "# flush\n" );
	  printf( "# put key value\n" );
	  printf( "# bput n key value\n" );
	  printf( "# nput n key_size value_size exact_size #(0=variable | 1=exact)\n");
	  printf( "# get key getOp #(EQ=0 | NEXT=1 | PREV=2 | FIRST=3 | LAST=4)\n" );
	  printf( "# bget n key\n" );
	  printf( "# bgetop n key getOp #(NEXT=1 | FIRST=3)\n" );
	  printf( "# ngetn n key_length exact_size #(0=variable | 1=exact)\n" );
	  printf( "# del key\n" );
	  printf( "# bdel n key\n" );

	}
        
      gettimeofday(&end, NULL);
      time_spent = (long double) (end.tv_sec - begin.tv_sec) + 
	((long double) (end.tv_usec - begin.tv_usec)/1000000.0);
      tst_say(0, "Seconds to %s : %Lf\n\n", commands[cmdIdx], time_spent);
    }
    
	
  if (errMsgIdx)
    {
		
	
      int i, errsInCmds = errMsgIdx; // Only list the errors up to now
      for (i=0; i<MAX_ERR_REPORT && i<errsInCmds; i++)
	tst_say(1, "==%s", errMsgs[i]);
      tst_say(1, "==TOTAL ERRORS for rank: %d => %d (first %d shown)\n", 
	      md->mdhim_rank, errsInCmds, i);
    }
  else
    {
      tst_say(0, "\n==No errors for rank: %d\n", md->mdhim_rank);
    }
    
  // Calls to finalize mdhim session and close MPI-communication
  ret = mdhimClose(md);
  if (ret != MDHIM_SUCCESS)
    {
      tst_say(1, "Error closing MDHIM\n");
    }    
  fclose(logfile);
    
  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();

  return( 0 );
}


/*
*    C Interface: SPString
*      
*    Description: simple String structure and manipulation
*    
*    This class provides a safe and convenient, albeit slower, alternative
*    to C character chains.
*
*    There are two types of strings:
*    1. String is a fully dynamic string, that is be resized automatically
*    at runtime when calling stringcpy and similar functions.
*    One doesn't have to worry about buffer length.
*    2. LString (Local String) has a fixed buffer size.
*    lstringcpy and the like will check that the buffer size is respected
*    and raise an error if it's not the case.
*
*    One can easily create a String from a C string with
*    newString(const char *).
*    String.str always returns a null-terminated C chain, and String.len
*    returns its length.
*    One should always avoid to manipulate the String structure members
*    directly in order to prevent corruption (i.e mainly incorrect sz and
*    len values).
*
*    Most functions mirror the standard C functions.
*/
 
#ifndef SPSTRING_H
#define SPSTRING_H
 
#include <stddef.h>
 
/* =========================================================================
   Allocation on the stack (non dynamic)
   
   The size sz is fixed once and for all at creation and must be the maximum
   size of the buffer holding the string, not the length of the C character
   string passed at initialization. Anything past will be truncated.
 
    Example Usage:
    {
      #define SIZE 40   <--- the LString can never grow longer than 39 chars
      static char buff[SIZE] = "Hello";
      LString *hello = localString(buff, SIZE);
      ...
      lstringchcat(hello, " World !");
    }  <--- automatic deallocation at the end of the scope
 
   Warning: The following does not work (because localString does  
            not allocate memory):
    LString *hello = localString("Hello World", SIZE);  
*/
 
typedef struct {
    size_t sz;  /* max buffer size, fixed, always > len */
    size_t len; /*length (final '\0' excluded)            */
    char *str;  /* null terminated character chain        */
} LString;
 
/* Create a local String */
LString localString ( char *chars, size_t szMax );
 
size_t lstringcpy ( LString *dst, const LString *src );
size_t lstringchcpy ( LString *dst, const char *src );
size_t lstringcat ( LString *dst, const LString *src );
size_t lstringchcat ( LString *dst, const char *src );
 
 
/* =================== Dynamic allocation on the Heap =======================
   The size is automatically adjusted at runtime.
   
   Usage example:
      LString *hello = newString("Hello ");
      ...
      stringchcat(hello, " world !");
      ...
      delString(myString);
*/
 
typedef struct{
    size_t sz;  /* max buffer size, dynamic(always > len) */
    size_t len; /* length (final '\0' excluded)          */
    char *str;  /* null terminated character chain       */
} String;
 
/* Allocate a new String object.
 * The source buffer passed as argument is copied, so it must be
 * freed manually. */
String * newString(const char *);
void delString(String *);
 
size_t stringcpy(String *dst, const String *src);
size_t stringchcpy(String *dst, const char *src);
size_t stringcat(String *dst, const String *src);
size_t stringchcat(String *dst, const char *src);
String * stringdup(const String *);
 
/* ========== The following functions apply both
   ========== on String and LString ===================*/
 
size_t stringlen(const String *);
int stringcmp(const String *st1, const String *st2);
int stringchcmp(const String *st1, const char *st2);
/* Truncation of length to Nth character */
String * stringtrunc(String *string, size_t N);
void stringprintf ( String *dst, const char * format, ... );

/* Verification of internal consistency of a String/LString
   Can be useful in a debugging session */
int stringcheck(const String *string, char *file, int line);
 
/* Use this function to get the error code     */
/* if (get_stringerr() != ST_NO_ERR) --> error */
int get_stringerr();  
 
/* Possible errors */
#define ST_NO_ERR                   -1000
#define ST_OVERWRITE_ERR            -1001
#define ST_ALLOC_ERR                -1002
#define ST_NULLPARAM_ERR            -1003
#define ST_NULLSTR_ERR              -1004
#define ST_INCONSISTENTSZ_ERR       -1005
#define ST_INCONSISTENTLEN_ERR      -1006
 
#endif

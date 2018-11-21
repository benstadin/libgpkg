/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code to implement the "sqlite" command line
** utility for accessing SQLite databases.
*/
#if (defined(_WIN32) || defined(WIN32)) && !defined(_CRT_SECURE_NO_WARNINGS)
/* This needs to come before any includes for MSVC compiler */
#define _CRT_SECURE_NO_WARNINGS
#endif

/*
** Warning pragmas copied from msvc.h in the core.
*/
#if defined(_MSC_VER)
#pragma warning(disable : 4054)
#pragma warning(disable : 4055)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4130)
#pragma warning(disable : 4152)
#pragma warning(disable : 4189)
#pragma warning(disable : 4206)
#pragma warning(disable : 4210)
#pragma warning(disable : 4232)
#pragma warning(disable : 4244)
#pragma warning(disable : 4305)
#pragma warning(disable : 4306)
#pragma warning(disable : 4702)
#pragma warning(disable : 4706)
#endif /* defined(_MSC_VER) */

/*
** No support for loadable extensions in VxWorks.
*/
#if (defined(__RTP__) || defined(_WRS_KERNEL)) && !SQLITE_OMIT_LOAD_EXTENSION
# define SQLITE_OMIT_LOAD_EXTENSION 1
#endif

/*
** Enable large-file support for fopen() and friends on unix.
*/
#ifndef SQLITE_DISABLE_LFS
# define _LARGE_FILE       1
# ifndef _FILE_OFFSET_BITS
#   define _FILE_OFFSET_BITS 64
# endif
# define _LARGEFILE_SOURCE 1
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3.h"
typedef sqlite3_int64 i64;
typedef sqlite3_uint64 u64;
typedef unsigned char u8;
#if SQLITE_USER_AUTHENTICATION
# include "sqlite3userauth.h"
#endif

#ifndef SQLITE_CORE
#define SQLITE_CORE_DEFINED
#define SQLITE_CORE
#endif

#include "gpkg.h"

#ifdef SQLITE_CORE_DEFINED
#undef SQLITE_CORE_DEFINED
#undef SQLITE_CORE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdarg.h>

#if !defined(_WIN32) && !defined(WIN32)
# include <signal.h>
# if !defined(__RTP__) && !defined(_WRS_KERNEL)
#  include <pwd.h>
# endif
#endif
#if (!defined(_WIN32) && !defined(WIN32)) || defined(__MINGW32__)
# include <unistd.h>
# include <dirent.h>
# define GETPID getpid
# if defined(__MINGW32__)
#  define DIRENT dirent
#  ifndef S_ISLNK
#   define S_ISLNK(mode) (0)
#  endif
# endif
#else
# define GETPID (int)GetCurrentProcessId
#endif
#include <sys/types.h>
#include <sys/stat.h>

#if HAVE_READLINE
# include <readline/readline.h>
# include <readline/history.h>
#endif

#if HAVE_EDITLINE
# include <editline/readline.h>
#endif

#if HAVE_EDITLINE || HAVE_READLINE

# define shell_add_history(X) add_history(X)
# define shell_read_history(X) read_history(X)
# define shell_write_history(X) write_history(X)
# define shell_stifle_history(X) stifle_history(X)
# define shell_readline(X) readline(X)

#elif HAVE_LINENOISE

# include "linenoise.h"
# define shell_add_history(X) linenoiseHistoryAdd(X)
# define shell_read_history(X) linenoiseHistoryLoad(X)
# define shell_write_history(X) linenoiseHistorySave(X)
# define shell_stifle_history(X) linenoiseHistorySetMaxLen(X)
# define shell_readline(X) linenoise(X)

#else

# define shell_read_history(X)
# define shell_write_history(X)
# define shell_stifle_history(X)

# define SHELL_USE_LOCAL_GETLINE 1
#endif


#if defined(_WIN32) || defined(WIN32)
# include <io.h>
# include <fcntl.h>
# define isatty(h) _isatty(h)
# ifndef access
#  define access(f,m) _access((f),(m))
# endif
# ifndef unlink
#  define unlink _unlink
# endif
# undef popen
# define popen _popen
# undef pclose
# define pclose _pclose
#else
 /* Make sure isatty() has a prototype. */
 extern int isatty(int);

# if !defined(__RTP__) && !defined(_WRS_KERNEL)
  /* popen and pclose are not C89 functions and so are
  ** sometimes omitted from the <stdio.h> header */
   extern FILE *popen(const char*,const char*);
   extern int pclose(FILE*);
# else
#  define SQLITE_OMIT_POPEN 1
# endif
#endif

#if defined(_WIN32_WCE)
/* Windows CE (arm-wince-mingw32ce-gcc) does not provide isatty()
 * thus we always assume that we have a console. That can be
 * overridden with the -batch command line option.
 */
#define isatty(x) 1
#endif

/* ctype macros that work with signed characters */
#define IsSpace(X)  isspace((unsigned char)X)
#define IsDigit(X)  isdigit((unsigned char)X)
#define ToLower(X)  (char)tolower((unsigned char)X)

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>

/* string conversion routines only needed on Win32 */
extern char *sqlite3_win32_unicode_to_utf8(LPCWSTR);
extern char *sqlite3_win32_mbcs_to_utf8_v2(const char *, int);
extern char *sqlite3_win32_utf8_to_mbcs_v2(const char *, int);
extern LPWSTR sqlite3_win32_utf8_to_unicode(const char *zText);
#endif

/* On Windows, we normally run with output mode of TEXT so that \n characters
** are automatically translated into \r\n.  However, this behavior needs
** to be disabled in some cases (ex: when generating CSV output and when
** rendering quoted strings that contain \n characters).  The following
** routines take care of that.
*/
#if defined(_WIN32) || defined(WIN32)
static void setBinaryMode(FILE *file, int isOutput){
  if( isOutput ) fflush(file);
  _setmode(_fileno(file), _O_BINARY);
}
static void setTextMode(FILE *file, int isOutput){
  if( isOutput ) fflush(file);
  _setmode(_fileno(file), _O_TEXT);
}
#else
# define setBinaryMode(X,Y)
# define setTextMode(X,Y)
#endif


/* True if the timer is enabled */
static int enableTimer = 0;

/* Return the current wall-clock time */
static sqlite3_int64 timeOfDay(void){
  static sqlite3_vfs *clockVfs = 0;
  sqlite3_int64 t;
  if( clockVfs==0 ) clockVfs = sqlite3_vfs_find(0);
  if( clockVfs->iVersion>=2 && clockVfs->xCurrentTimeInt64!=0 ){
    clockVfs->xCurrentTimeInt64(clockVfs, &t);
  }else{
    double r;
    clockVfs->xCurrentTime(clockVfs, &r);
    t = (sqlite3_int64)(r*86400000.0);
  }
  return t;
}

#if !defined(_WIN32) && !defined(WIN32) && !defined(__minux)
#include <sys/time.h>
#include <sys/resource.h>

/* VxWorks does not support getrusage() as far as we can determine */
#if defined(_WRS_KERNEL) || defined(__RTP__)
struct rusage {
  struct timeval ru_utime; /* user CPU time used */
  struct timeval ru_stime; /* system CPU time used */
};
#define getrusage(A,B) memset(B,0,sizeof(*B))
#endif

/* Saved resource information for the beginning of an operation */
static struct rusage sBegin;  /* CPU time at start */
static sqlite3_int64 iBegin;  /* Wall-clock time at start */

/*
** Begin timing an operation
*/
static void beginTimer(void){
  if( enableTimer ){
    getrusage(RUSAGE_SELF, &sBegin);
    iBegin = timeOfDay();
  }
}

/* Return the difference of two time_structs in seconds */
static double timeDiff(struct timeval *pStart, struct timeval *pEnd){
  return (pEnd->tv_usec - pStart->tv_usec)*0.000001 +
         (double)(pEnd->tv_sec - pStart->tv_sec);
}

/*
** Print the timing results.
*/
static void endTimer(void){
  if( enableTimer ){
    sqlite3_int64 iEnd = timeOfDay();
    struct rusage sEnd;
    getrusage(RUSAGE_SELF, &sEnd);
    printf("Run Time: real %.3f user %f sys %f\n",
       (iEnd - iBegin)*0.001,
       timeDiff(&sBegin.ru_utime, &sEnd.ru_utime),
       timeDiff(&sBegin.ru_stime, &sEnd.ru_stime));
  }
}

#define BEGIN_TIMER beginTimer()
#define END_TIMER endTimer()
#define HAS_TIMER 1

#elif (defined(_WIN32) || defined(WIN32))

/* Saved resource information for the beginning of an operation */
static HANDLE hProcess;
static FILETIME ftKernelBegin;
static FILETIME ftUserBegin;
static sqlite3_int64 ftWallBegin;
typedef BOOL (WINAPI *GETPROCTIMES)(HANDLE, LPFILETIME, LPFILETIME,
                                    LPFILETIME, LPFILETIME);
static GETPROCTIMES getProcessTimesAddr = NULL;

/*
** Check to see if we have timer support.  Return 1 if necessary
** support found (or found previously).
*/
static int hasTimer(void){
  if( getProcessTimesAddr ){
    return 1;
  } else {
    /* GetProcessTimes() isn't supported in WIN95 and some other Windows
    ** versions. See if the version we are running on has it, and if it
    ** does, save off a pointer to it and the current process handle.
    */
    hProcess = GetCurrentProcess();
    if( hProcess ){
      HINSTANCE hinstLib = LoadLibrary(TEXT("Kernel32.dll"));
      if( NULL != hinstLib ){
        getProcessTimesAddr =
            (GETPROCTIMES) GetProcAddress(hinstLib, "GetProcessTimes");
        if( NULL != getProcessTimesAddr ){
          return 1;
        }
        FreeLibrary(hinstLib);
      }
    }
  }
  return 0;
}

/*
** Begin timing an operation
*/
static void beginTimer(void){
  if( enableTimer && getProcessTimesAddr ){
    FILETIME ftCreation, ftExit;
    getProcessTimesAddr(hProcess,&ftCreation,&ftExit,
                        &ftKernelBegin,&ftUserBegin);
    ftWallBegin = timeOfDay();
  }
}

/* Return the difference of two FILETIME structs in seconds */
static double timeDiff(FILETIME *pStart, FILETIME *pEnd){
  sqlite_int64 i64Start = *((sqlite_int64 *) pStart);
  sqlite_int64 i64End = *((sqlite_int64 *) pEnd);
  return (double) ((i64End - i64Start) / 10000000.0);
}

/*
** Print the timing results.
*/
static void endTimer(void){
  if( enableTimer && getProcessTimesAddr){
    FILETIME ftCreation, ftExit, ftKernelEnd, ftUserEnd;
    sqlite3_int64 ftWallEnd = timeOfDay();
    getProcessTimesAddr(hProcess,&ftCreation,&ftExit,&ftKernelEnd,&ftUserEnd);
    printf("Run Time: real %.3f user %f sys %f\n",
       (ftWallEnd - ftWallBegin)*0.001,
       timeDiff(&ftUserBegin, &ftUserEnd),
       timeDiff(&ftKernelBegin, &ftKernelEnd));
  }
}

#define BEGIN_TIMER beginTimer()
#define END_TIMER endTimer()
#define HAS_TIMER hasTimer()

#else
#define BEGIN_TIMER
#define END_TIMER
#define HAS_TIMER 0
#endif

/*
** Used to prevent warnings about unused parameters
*/
#define UNUSED_PARAMETER(x) (void)(x)

/*
** Number of elements in an array
*/
#define ArraySize(X)  (int)(sizeof(X)/sizeof(X[0]))

/*
** If the following flag is set, then command execution stops
** at an error if we are not interactive.
*/
static int bail_on_error = 0;

/*
** Threat stdin as an interactive input if the following variable
** is true.  Otherwise, assume stdin is connected to a file or pipe.
*/
static int stdin_is_interactive = 1;

/*
** On Windows systems we have to know if standard output is a console
** in order to translate UTF-8 into MBCS.  The following variable is
** true if translation is required.
*/
static int stdout_is_console = 1;

/*
** The following is the open SQLite database.  We make a pointer
** to this database a static variable so that it can be accessed
** by the SIGINT handler to interrupt database processing.
*/
static sqlite3 *globalDb = 0;

/*
** True if an interrupt (Control-C) has been received.
*/
static volatile int seenInterrupt = 0;

/*
** This is the name of our program. It is set in main(), used
** in a number of other places, mostly for error messages.
*/
static char *Argv0;

/*
** Prompt strings. Initialized in main. Settable with
**   .prompt main continue
*/
static char mainPrompt[20];     /* First line prompt. default: "sqlite> "*/
static char continuePrompt[20]; /* Continuation prompt. default: "   ...> " */

/*
** Render output like fprintf().  Except, if the output is going to the
** console and if this is running on a Windows machine, translate the
** output from UTF-8 into MBCS.
*/
#if defined(_WIN32) || defined(WIN32)
void utf8_printf(FILE *out, const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  if( stdout_is_console && (out==stdout || out==stderr) ){
    char *z1 = sqlite3_vmprintf(zFormat, ap);
    char *z2 = sqlite3_win32_utf8_to_mbcs_v2(z1, 0);
    sqlite3_free(z1);
    fputs(z2, out);
    sqlite3_free(z2);
  }else{
    vfprintf(out, zFormat, ap);
  }
  va_end(ap);
}
#elif !defined(utf8_printf)
# define utf8_printf fprintf
#endif

/*
** Render output like fprintf().  This should not be used on anything that
** includes string formatting (e.g. "%s").
*/
#if !defined(raw_printf)
# define raw_printf fprintf
#endif

/* Indicate out-of-memory and exit. */
static void shell_out_of_memory(void){
  raw_printf(stderr,"Error: out of memory\n");
  exit(1);
}

/*
** Write I/O traces to the following stream.
*/
#ifdef SQLITE_ENABLE_IOTRACE
static FILE *iotrace = 0;
#endif

/*
** This routine works like printf in that its first argument is a
** format string and subsequent arguments are values to be substituted
** in place of % fields.  The result of formatting this string
** is written to iotrace.
*/
#ifdef SQLITE_ENABLE_IOTRACE
static void SQLITE_CDECL iotracePrintf(const char *zFormat, ...){
  va_list ap;
  char *z;
  if( iotrace==0 ) return;
  va_start(ap, zFormat);
  z = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  utf8_printf(iotrace, "%s", z);
  sqlite3_free(z);
}
#endif

/*
** Output string zUtf to stream pOut as w characters.  If w is negative,
** then right-justify the text.  W is the width in UTF-8 characters, not
** in bytes.  This is different from the %*.*s specification in printf
** since with %*.*s the width is measured in bytes, not characters.
*/
static void utf8_width_print(FILE *pOut, int w, const char *zUtf){
  int i;
  int n;
  int aw = w<0 ? -w : w;
  char zBuf[1000];
  if( aw>(int)sizeof(zBuf)/3 ) aw = (int)sizeof(zBuf)/3;
  for(i=n=0; zUtf[i]; i++){
    if( (zUtf[i]&0xc0)!=0x80 ){
      n++;
      if( n==aw ){
        do{ i++; }while( (zUtf[i]&0xc0)==0x80 );
        break;
      }
    }
  }
  if( n>=aw ){
    utf8_printf(pOut, "%.*s", i, zUtf);
  }else if( w<0 ){
    utf8_printf(pOut, "%*s%s", aw-n, "", zUtf);
  }else{
    utf8_printf(pOut, "%s%*s", zUtf, aw-n, "");
  }
}


/*
** Determines if a string is a number of not.
*/
static int isNumber(const char *z, int *realnum){
  if( *z=='-' || *z=='+' ) z++;
  if( !IsDigit(*z) ){
    return 0;
  }
  z++;
  if( realnum ) *realnum = 0;
  while( IsDigit(*z) ){ z++; }
  if( *z=='.' ){
    z++;
    if( !IsDigit(*z) ) return 0;
    while( IsDigit(*z) ){ z++; }
    if( realnum ) *realnum = 1;
  }
  if( *z=='e' || *z=='E' ){
    z++;
    if( *z=='+' || *z=='-' ) z++;
    if( !IsDigit(*z) ) return 0;
    while( IsDigit(*z) ){ z++; }
    if( realnum ) *realnum = 1;
  }
  return *z==0;
}

/*
** Compute a string length that is limited to what can be stored in
** lower 30 bits of a 32-bit signed integer.
*/
static int strlen30(const char *z){
  const char *z2 = z;
  while( *z2 ){ z2++; }
  return 0x3fffffff & (int)(z2 - z);
}

/*
** Return the length of a string in characters.  Multibyte UTF8 characters
** count as a single character.
*/
static int strlenChar(const char *z){
  int n = 0;
  while( *z ){
    if( (0xc0&*(z++))!=0x80 ) n++;
  }
  return n;
}

/*
** This routine reads a line of text from FILE in, stores
** the text in memory obtained from malloc() and returns a pointer
** to the text.  NULL is returned at end of file, or if malloc()
** fails.
**
** If zLine is not NULL then it is a malloced buffer returned from
** a previous call to this routine that may be reused.
*/
static char *local_getline(char *zLine, FILE *in){
  int nLine = zLine==0 ? 0 : 100;
  int n = 0;

  while( 1 ){
    if( n+100>nLine ){
      nLine = nLine*2 + 100;
      zLine = realloc(zLine, nLine);
      if( zLine==0 ) shell_out_of_memory();
    }
    if( fgets(&zLine[n], nLine - n, in)==0 ){
      if( n==0 ){
        free(zLine);
        return 0;
      }
      zLine[n] = 0;
      break;
    }
    while( zLine[n] ) n++;
    if( n>0 && zLine[n-1]=='\n' ){
      n--;
      if( n>0 && zLine[n-1]=='\r' ) n--;
      zLine[n] = 0;
      break;
    }
  }
#if defined(_WIN32) || defined(WIN32)
  /* For interactive input on Windows systems, translate the
  ** multi-byte characterset characters into UTF-8. */
  if( stdin_is_interactive && in==stdin ){
    char *zTrans = sqlite3_win32_mbcs_to_utf8_v2(zLine, 0);
    if( zTrans ){
      int nTrans = strlen30(zTrans)+1;
      if( nTrans>nLine ){
        zLine = realloc(zLine, nTrans);
        if( zLine==0 ) shell_out_of_memory();
      }
      memcpy(zLine, zTrans, nTrans);
      sqlite3_free(zTrans);
    }
  }
#endif /* defined(_WIN32) || defined(WIN32) */
  return zLine;
}

/*
** Retrieve a single line of input text.
**
** If in==0 then read from standard input and prompt before each line.
** If isContinuation is true, then a continuation prompt is appropriate.
** If isContinuation is zero, then the main prompt should be used.
**
** If zPrior is not NULL then it is a buffer from a prior call to this
** routine that can be reused.
**
** The result is stored in space obtained from malloc() and must either
** be freed by the caller or else passed back into this routine via the
** zPrior argument for reuse.
*/
static char *one_input_line(FILE *in, char *zPrior, int isContinuation){
  char *zPrompt;
  char *zResult;
  if( in!=0 ){
    zResult = local_getline(zPrior, in);
  }else{
    zPrompt = isContinuation ? continuePrompt : mainPrompt;
#if SHELL_USE_LOCAL_GETLINE
    printf("%s", zPrompt);
    fflush(stdout);
    zResult = local_getline(zPrior, stdin);
#else
    free(zPrior);
    zResult = shell_readline(zPrompt);
    if( zResult && *zResult ) shell_add_history(zResult);
#endif
  }
  return zResult;
}


/*
** Return the value of a hexadecimal digit.  Return -1 if the input
** is not a hex digit.
*/
static int hexDigitValue(char c){
  if( c>='0' && c<='9' ) return c - '0';
  if( c>='a' && c<='f' ) return c - 'a' + 10;
  if( c>='A' && c<='F' ) return c - 'A' + 10;
  return -1;
}

/*
** Interpret zArg as an integer value, possibly with suffixes.
*/
static sqlite3_int64 integerValue(const char *zArg){
  sqlite3_int64 v = 0;
  static const struct { char *zSuffix; int iMult; } aMult[] = {
    { "KiB", 1024 },
    { "MiB", 1024*1024 },
    { "GiB", 1024*1024*1024 },
    { "KB",  1000 },
    { "MB",  1000000 },
    { "GB",  1000000000 },
    { "K",   1000 },
    { "M",   1000000 },
    { "G",   1000000000 },
  };
  int i;
  int isNeg = 0;
  if( zArg[0]=='-' ){
    isNeg = 1;
    zArg++;
  }else if( zArg[0]=='+' ){
    zArg++;
  }
  if( zArg[0]=='0' && zArg[1]=='x' ){
    int x;
    zArg += 2;
    while( (x = hexDigitValue(zArg[0]))>=0 ){
      v = (v<<4) + x;
      zArg++;
    }
  }else{
    while( IsDigit(zArg[0]) ){
      v = v*10 + zArg[0] - '0';
      zArg++;
    }
  }
  for(i=0; i<ArraySize(aMult); i++){
    if( sqlite3_stricmp(aMult[i].zSuffix, zArg)==0 ){
      v *= aMult[i].iMult;
      break;
    }
  }
  return isNeg? -v : v;
}

/*
** A variable length string to which one can append text.
*/
typedef struct ShellText ShellText;
struct ShellText {
  char *z;
  int n;
  int nAlloc;
};

/*
** Initialize and destroy a ShellText object
*/
static void initText(ShellText *p){
  memset(p, 0, sizeof(*p));
}
static void freeText(ShellText *p){
  free(p->z);
  initText(p);
}

/* zIn is either a pointer to a NULL-terminated string in memory obtained
** from malloc(), or a NULL pointer. The string pointed to by zAppend is
** added to zIn, and the result returned in memory obtained from malloc().
** zIn, if it was not NULL, is freed.
**
** If the third argument, quote, is not '\0', then it is used as a
** quote character for zAppend.
*/
static void appendText(ShellText *p, char const *zAppend, char quote){
  int len;
  int i;
  int nAppend = strlen30(zAppend);

  len = nAppend+p->n+1;
  if( quote ){
    len += 2;
    for(i=0; i<nAppend; i++){
      if( zAppend[i]==quote ) len++;
    }
  }

  if( p->n+len>=p->nAlloc ){
    p->nAlloc = p->nAlloc*2 + len + 20;
    p->z = realloc(p->z, p->nAlloc);
    if( p->z==0 ) shell_out_of_memory();
  }

  if( quote ){
    char *zCsr = p->z+p->n;
    *zCsr++ = quote;
    for(i=0; i<nAppend; i++){
      *zCsr++ = zAppend[i];
      if( zAppend[i]==quote ) *zCsr++ = quote;
    }
    *zCsr++ = quote;
    p->n = (int)(zCsr - p->z);
    *zCsr = '\0';
  }else{
    memcpy(p->z+p->n, zAppend, nAppend);
    p->n += nAppend;
    p->z[p->n] = '\0';
  }
}

/*
** Attempt to determine if identifier zName needs to be quoted, either
** because it contains non-alphanumeric characters, or because it is an
** SQLite keyword.  Be conservative in this estimate:  When in doubt assume
** that quoting is required.
**
** Return '"' if quoting is required.  Return 0 if no quoting is required.
*/
static char quoteChar(const char *zName){
  int i;
  if( !isalpha((unsigned char)zName[0]) && zName[0]!='_' ) return '"';
  for(i=0; zName[i]; i++){
    if( !isalnum((unsigned char)zName[i]) && zName[i]!='_' ) return '"';
  }
  return sqlite3_keyword_check(zName, i) ? '"' : 0;
}

/*
** Construct a fake object name and column list to describe the structure
** of the view, virtual table, or table valued function zSchema.zName.
*/
static char *shellFakeSchema(
  sqlite3 *db,            /* The database connection containing the vtab */
  const char *zSchema,    /* Schema of the database holding the vtab */
  const char *zName       /* The name of the virtual table */
){
  sqlite3_stmt *pStmt = 0;
  char *zSql;
  ShellText s;
  char cQuote;
  char *zDiv = "(";
  int nRow = 0;

  zSql = sqlite3_mprintf("PRAGMA \"%w\".table_info=%Q;",
                         zSchema ? zSchema : "main", zName);
  sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  sqlite3_free(zSql);
  initText(&s);
  if( zSchema ){
    cQuote = quoteChar(zSchema);
    if( cQuote && sqlite3_stricmp(zSchema,"temp")==0 ) cQuote = 0;
    appendText(&s, zSchema, cQuote);
    appendText(&s, ".", 0);
  }
  cQuote = quoteChar(zName);
  appendText(&s, zName, cQuote);
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    const char *zCol = (const char*)sqlite3_column_text(pStmt, 1);
    nRow++;
    appendText(&s, zDiv, 0);
    zDiv = ",";
    cQuote = quoteChar(zCol);
    appendText(&s, zCol, cQuote);
  }
  appendText(&s, ")", 0);
  sqlite3_finalize(pStmt);
  if( nRow==0 ){
    freeText(&s);
    s.z = 0;
  }
  return s.z;
}

/*
** SQL function:  shell_module_schema(X)
**
** Return a fake schema for the table-valued function or eponymous virtual
** table X.
*/
static void shellModuleSchema(
  sqlite3_context *pCtx,
  int nVal,
  sqlite3_value **apVal
){
  const char *zName = (const char*)sqlite3_value_text(apVal[0]);
  char *zFake = shellFakeSchema(sqlite3_context_db_handle(pCtx), 0, zName);
  UNUSED_PARAMETER(nVal);
  if( zFake ){
    sqlite3_result_text(pCtx, sqlite3_mprintf("/* %s */", zFake),
                        -1, sqlite3_free);
    free(zFake);
  }
}

/*
** SQL function:  shell_add_schema(S,X)
**
** Add the schema name X to the CREATE statement in S and return the result.
** Examples:
**
**    CREATE TABLE t1(x)   ->   CREATE TABLE xyz.t1(x);
**
** Also works on
**
**    CREATE INDEX
**    CREATE UNIQUE INDEX
**    CREATE VIEW
**    CREATE TRIGGER
**    CREATE VIRTUAL TABLE
**
** This UDF is used by the .schema command to insert the schema name of
** attached databases into the middle of the sqlite_master.sql field.
*/
static void shellAddSchemaName(
  sqlite3_context *pCtx,
  int nVal,
  sqlite3_value **apVal
){
  static const char *aPrefix[] = {
     "TABLE",
     "INDEX",
     "UNIQUE INDEX",
     "VIEW",
     "TRIGGER",
     "VIRTUAL TABLE"
  };
  int i = 0;
  const char *zIn = (const char*)sqlite3_value_text(apVal[0]);
  const char *zSchema = (const char*)sqlite3_value_text(apVal[1]);
  const char *zName = (const char*)sqlite3_value_text(apVal[2]);
  sqlite3 *db = sqlite3_context_db_handle(pCtx);
  UNUSED_PARAMETER(nVal);
  if( zIn!=0 && strncmp(zIn, "CREATE ", 7)==0 ){
    for(i=0; i<(int)(sizeof(aPrefix)/sizeof(aPrefix[0])); i++){
      int n = strlen30(aPrefix[i]);
      if( strncmp(zIn+7, aPrefix[i], n)==0 && zIn[n+7]==' ' ){
        char *z = 0;
        char *zFake = 0;
        if( zSchema ){
          char cQuote = quoteChar(zSchema);
          if( cQuote && sqlite3_stricmp(zSchema,"temp")!=0 ){
            z = sqlite3_mprintf("%.*s \"%w\".%s", n+7, zIn, zSchema, zIn+n+8);
          }else{
            z = sqlite3_mprintf("%.*s %s.%s", n+7, zIn, zSchema, zIn+n+8);
          }
        }
        if( zName
         && aPrefix[i][0]=='V'
         && (zFake = shellFakeSchema(db, zSchema, zName))!=0
        ){
          if( z==0 ){
            z = sqlite3_mprintf("%s\n/* %s */", zIn, zFake);
          }else{
            z = sqlite3_mprintf("%z\n/* %s */", z, zFake);
          }
          free(zFake);
        }
        if( z ){
          sqlite3_result_text(pCtx, z, -1, sqlite3_free);
          return;
        }
      }
    }
  }
  sqlite3_result_value(pCtx, apVal[0]);
}

/*
** The source code for several run-time loadable extensions is inserted
** below by the ../tool/mkshellc.tcl script.  Before processing that included
** code, we need to override some macros to make the included program code
** work here in the middle of this regular program.
*/
#define SQLITE_EXTENSION_INIT1
#define SQLITE_EXTENSION_INIT2(X) (void)(X)

#if defined(_WIN32) && defined(_MSC_VER)
/************************* Begin test_windirent.h ******************/
/*
** 2015 November 30
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains declarations for most of the opendir() family of
** POSIX functions on Win32 using the MSVCRT.
*/

#if defined(_WIN32) && defined(_MSC_VER) && !defined(SQLITE_WINDIRENT_H)
#define SQLITE_WINDIRENT_H

/*
** We need several data types from the Windows SDK header.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "windows.h"

/*
** We need several support functions from the SQLite core.
*/


/*
** We need several things from the ANSI and MSVCRT headers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
** We may need several defines that should have been in "sys/stat.h".
*/

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISLNK
#define S_ISLNK(mode) (0)
#endif

/*
** We may need to provide the "mode_t" type.
*/

#ifndef MODE_T_DEFINED
  #define MODE_T_DEFINED
  typedef unsigned short mode_t;
#endif

/*
** We may need to provide the "ino_t" type.
*/

#ifndef INO_T_DEFINED
  #define INO_T_DEFINED
  typedef unsigned short ino_t;
#endif

/*
** We need to define "NAME_MAX" if it was not present in "limits.h".
*/

#ifndef NAME_MAX
#  ifdef FILENAME_MAX
#    define NAME_MAX (FILENAME_MAX)
#  else
#    define NAME_MAX (260)
#  endif
#endif

/*
** We need to define "NULL_INTPTR_T" and "BAD_INTPTR_T".
*/

#ifndef NULL_INTPTR_T
#  define NULL_INTPTR_T ((intptr_t)(0))
#endif

#ifndef BAD_INTPTR_T
#  define BAD_INTPTR_T ((intptr_t)(-1))
#endif

/*
** We need to provide the necessary structures and related types.
*/

#ifndef DIRENT_DEFINED
#define DIRENT_DEFINED
typedef struct DIRENT DIRENT;
typedef DIRENT *LPDIRENT;
struct DIRENT {
  ino_t d_ino;               /* Sequence number, do not use. */
  unsigned d_attributes;     /* Win32 file attributes. */
  char d_name[NAME_MAX + 1]; /* Name within the directory. */
};
#endif

#ifndef DIR_DEFINED
#define DIR_DEFINED
typedef struct DIR DIR;
typedef DIR *LPDIR;
struct DIR {
  intptr_t d_handle; /* Value returned by "_findfirst". */
  DIRENT d_first;    /* DIRENT constructed based on "_findfirst". */
  DIRENT d_next;     /* DIRENT constructed based on "_findnext". */
};
#endif

/*
** Provide a macro, for use by the implementation, to determine if a
** particular directory entry should be skipped over when searching for
** the next directory entry that should be returned by the readdir() or
** readdir_r() functions.
*/

#ifndef is_filtered
#  define is_filtered(a) ((((a).attrib)&_A_HIDDEN) || (((a).attrib)&_A_SYSTEM))
#endif

/*
** Provide the function prototype for the POSIX compatiable getenv()
** function.  This function is not thread-safe.
*/

extern const char *windirent_getenv(const char *name);

/*
** Finally, we can provide the function prototypes for the opendir(),
** readdir(), readdir_r(), and closedir() POSIX functions.
*/

extern LPDIR opendir(const char *dirname);
extern LPDIRENT readdir(LPDIR dirp);
extern INT readdir_r(LPDIR dirp, LPDIRENT entry, LPDIRENT *result);
extern INT closedir(LPDIR dirp);

#endif /* defined(WIN32) && defined(_MSC_VER) */

/************************* End test_windirent.h ********************/
/************************* Begin test_windirent.c ******************/
/*
** 2015 November 30
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code to implement most of the opendir() family of
** POSIX functions on Win32 using the MSVCRT.
*/

#if defined(_WIN32) && defined(_MSC_VER)
/* #include "test_windirent.h" */

/*
** Implementation of the POSIX getenv() function using the Win32 API.
** This function is not thread-safe.
*/
const char *windirent_getenv(
  const char *name
){
  static char value[32768]; /* Maximum length, per MSDN */
  DWORD dwSize = sizeof(value) / sizeof(char); /* Size in chars */
  DWORD dwRet; /* Value returned by GetEnvironmentVariableA() */

  memset(value, 0, sizeof(value));
  dwRet = GetEnvironmentVariableA(name, value, dwSize);
  if( dwRet==0 || dwRet>dwSize ){
    /*
    ** The function call to GetEnvironmentVariableA() failed -OR-
    ** the buffer is not large enough.  Either way, return NULL.
    */
    return 0;
  }else{
    /*
    ** The function call to GetEnvironmentVariableA() succeeded
    ** -AND- the buffer contains the entire value.
    */
    return value;
  }
}

/*
** Implementation of the POSIX opendir() function using the MSVCRT.
*/
LPDIR opendir(
  const char *dirname
){
  struct _finddata_t data;
  LPDIR dirp = (LPDIR)sqlite3_malloc(sizeof(DIR));
  SIZE_T namesize = sizeof(data.name) / sizeof(data.name[0]);

  if( dirp==NULL ) return NULL;
  memset(dirp, 0, sizeof(DIR));

  /* TODO: Remove this if Unix-style root paths are not used. */
  if( sqlite3_stricmp(dirname, "/")==0 ){
    dirname = windirent_getenv("SystemDrive");
  }

  memset(&data, 0, sizeof(struct _finddata_t));
  _snprintf(data.name, namesize, "%s\\*", dirname);
  dirp->d_handle = _findfirst(data.name, &data);

  if( dirp->d_handle==BAD_INTPTR_T ){
    closedir(dirp);
    return NULL;
  }

  /* TODO: Remove this block to allow hidden and/or system files. */
  if( is_filtered(data) ){
next:

    memset(&data, 0, sizeof(struct _finddata_t));
    if( _findnext(dirp->d_handle, &data)==-1 ){
      closedir(dirp);
      return NULL;
    }

    /* TODO: Remove this block to allow hidden and/or system files. */
    if( is_filtered(data) ) goto next;
  }

  dirp->d_first.d_attributes = data.attrib;
  strncpy(dirp->d_first.d_name, data.name, NAME_MAX);
  dirp->d_first.d_name[NAME_MAX] = '\0';

  return dirp;
}

/*
** Implementation of the POSIX readdir() function using the MSVCRT.
*/
LPDIRENT readdir(
  LPDIR dirp
){
  struct _finddata_t data;

  if( dirp==NULL ) return NULL;

  if( dirp->d_first.d_ino==0 ){
    dirp->d_first.d_ino++;
    dirp->d_next.d_ino++;

    return &dirp->d_first;
  }

next:

  memset(&data, 0, sizeof(struct _finddata_t));
  if( _findnext(dirp->d_handle, &data)==-1 ) return NULL;

  /* TODO: Remove this block to allow hidden and/or system files. */
  if( is_filtered(data) ) goto next;

  dirp->d_next.d_ino++;
  dirp->d_next.d_attributes = data.attrib;
  strncpy(dirp->d_next.d_name, data.name, NAME_MAX);
  dirp->d_next.d_name[NAME_MAX] = '\0';

  return &dirp->d_next;
}

/*
** Implementation of the POSIX readdir_r() function using the MSVCRT.
*/
INT readdir_r(
  LPDIR dirp,
  LPDIRENT entry,
  LPDIRENT *result
){
  struct _finddata_t data;

  if( dirp==NULL ) return EBADF;

  if( dirp->d_first.d_ino==0 ){
    dirp->d_first.d_ino++;
    dirp->d_next.d_ino++;

    entry->d_ino = dirp->d_first.d_ino;
    entry->d_attributes = dirp->d_first.d_attributes;
    strncpy(entry->d_name, dirp->d_first.d_name, NAME_MAX);
    entry->d_name[NAME_MAX] = '\0';

    *result = entry;
    return 0;
  }

next:

  memset(&data, 0, sizeof(struct _finddata_t));
  if( _findnext(dirp->d_handle, &data)==-1 ){
    *result = NULL;
    return ENOENT;
  }

  /* TODO: Remove this block to allow hidden and/or system files. */
  if( is_filtered(data) ) goto next;

  entry->d_ino = (ino_t)-1; /* not available */
  entry->d_attributes = data.attrib;
  strncpy(entry->d_name, data.name, NAME_MAX);
  entry->d_name[NAME_MAX] = '\0';

  *result = entry;
  return 0;
}

/*
** Implementation of the POSIX closedir() function using the MSVCRT.
*/
INT closedir(
  LPDIR dirp
){
  INT result = 0;

  if( dirp==NULL ) return EINVAL;

  if( dirp->d_handle!=NULL_INTPTR_T && dirp->d_handle!=BAD_INTPTR_T ){
    result = _findclose(dirp->d_handle);
  }

  sqlite3_free(dirp);
  return result;
}

#endif /* defined(WIN32) && defined(_MSC_VER) */

/************************* End test_windirent.c ********************/
#define dirent DIRENT
#endif
/************************* Begin ../ext/misc/shathree.c ******************/
/*
** 2017-03-08
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This SQLite extension implements a functions that compute SHA1 hashes.
** Two SQL functions are implemented:
**
**     sha3(X,SIZE)
**     sha3_query(Y,SIZE)
**
** The sha3(X) function computes the SHA3 hash of the input X, or NULL if
** X is NULL.
**
** The sha3_query(Y) function evalutes all queries in the SQL statements of Y
** and returns a hash of their results.
**
** The SIZE argument is optional.  If omitted, the SHA3-256 hash algorithm
** is used.  If SIZE is included it must be one of the integers 224, 256,
** 384, or 512, to determine SHA3 hash variant that is computed.
*/
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>
#include <stdarg.h>
/* typedef sqlite3_uint64 u64; */

/******************************************************************************
** The Hash Engine
*/
/*
** Macros to determine whether the machine is big or little endian,
** and whether or not that determination is run-time or compile-time.
**
** For best performance, an attempt is made to guess at the byte-order
** using C-preprocessor macros.  If that is unsuccessful, or if
** -DSHA3_BYTEORDER=0 is set, then byte-order is determined
** at run-time.
*/
#ifndef SHA3_BYTEORDER
# if defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)  ||    \
     defined(_M_AMD64) || defined(_M_ARM)     || defined(__x86)   ||    \
     defined(__arm__)
#   define SHA3_BYTEORDER    1234
# elif defined(sparc)    || defined(__ppc__)
#   define SHA3_BYTEORDER    4321
# else
#   define SHA3_BYTEORDER 0
# endif
#endif


/*
** State structure for a SHA3 hash in progress
*/
typedef struct SHA3Context SHA3Context;
struct SHA3Context {
  union {
    u64 s[25];                /* Keccak state. 5x5 lines of 64 bits each */
    unsigned char x[1600];    /* ... or 1600 bytes */
  } u;
  unsigned nRate;        /* Bytes of input accepted per Keccak iteration */
  unsigned nLoaded;      /* Input bytes loaded into u.x[] so far this cycle */
  unsigned ixMask;       /* Insert next input into u.x[nLoaded^ixMask]. */
};

/*
** A single step of the Keccak mixing function for a 1600-bit state
*/
static void KeccakF1600Step(SHA3Context *p){
  int i;
  u64 b0, b1, b2, b3, b4;
  u64 c0, c1, c2, c3, c4;
  u64 d0, d1, d2, d3, d4;
  static const u64 RC[] = {
    0x0000000000000001ULL,  0x0000000000008082ULL,
    0x800000000000808aULL,  0x8000000080008000ULL,
    0x000000000000808bULL,  0x0000000080000001ULL,
    0x8000000080008081ULL,  0x8000000000008009ULL,
    0x000000000000008aULL,  0x0000000000000088ULL,
    0x0000000080008009ULL,  0x000000008000000aULL,
    0x000000008000808bULL,  0x800000000000008bULL,
    0x8000000000008089ULL,  0x8000000000008003ULL,
    0x8000000000008002ULL,  0x8000000000000080ULL,
    0x000000000000800aULL,  0x800000008000000aULL,
    0x8000000080008081ULL,  0x8000000000008080ULL,
    0x0000000080000001ULL,  0x8000000080008008ULL
  };
# define a00 (p->u.s[0])
# define a01 (p->u.s[1])
# define a02 (p->u.s[2])
# define a03 (p->u.s[3])
# define a04 (p->u.s[4])
# define a10 (p->u.s[5])
# define a11 (p->u.s[6])
# define a12 (p->u.s[7])
# define a13 (p->u.s[8])
# define a14 (p->u.s[9])
# define a20 (p->u.s[10])
# define a21 (p->u.s[11])
# define a22 (p->u.s[12])
# define a23 (p->u.s[13])
# define a24 (p->u.s[14])
# define a30 (p->u.s[15])
# define a31 (p->u.s[16])
# define a32 (p->u.s[17])
# define a33 (p->u.s[18])
# define a34 (p->u.s[19])
# define a40 (p->u.s[20])
# define a41 (p->u.s[21])
# define a42 (p->u.s[22])
# define a43 (p->u.s[23])
# define a44 (p->u.s[24])
# define ROL64(a,x) ((a<<x)|(a>>(64-x)))

  for(i=0; i<24; i+=4){
    c0 = a00^a10^a20^a30^a40;
    c1 = a01^a11^a21^a31^a41;
    c2 = a02^a12^a22^a32^a42;
    c3 = a03^a13^a23^a33^a43;
    c4 = a04^a14^a24^a34^a44;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a11^d1), 44);
    b2 = ROL64((a22^d2), 43);
    b3 = ROL64((a33^d3), 21);
    b4 = ROL64((a44^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i];
    a11 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a20^d0), 3);
    b3 = ROL64((a31^d1), 45);
    b4 = ROL64((a42^d2), 61);
    b0 = ROL64((a03^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a20 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a40^d0), 18);
    b0 = ROL64((a01^d1), 1);
    b1 = ROL64((a12^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a34^d4), 8);
    a40 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a10^d0), 36);
    b2 = ROL64((a21^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a43^d3), 56);
    b0 = ROL64((a04^d4), 27);
    a10 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a30^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a02^d2), 62);
    b1 = ROL64((a13^d3), 55);
    b2 = ROL64((a24^d4), 39);
    a30 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    c0 = a00^a20^a40^a10^a30;
    c1 = a11^a31^a01^a21^a41;
    c2 = a22^a42^a12^a32^a02;
    c3 = a33^a03^a23^a43^a13;
    c4 = a44^a14^a34^a04^a24;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a31^d1), 44);
    b2 = ROL64((a12^d2), 43);
    b3 = ROL64((a43^d3), 21);
    b4 = ROL64((a24^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+1];
    a31 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a40^d0), 3);
    b3 = ROL64((a21^d1), 45);
    b4 = ROL64((a02^d2), 61);
    b0 = ROL64((a33^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a40 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a30^d0), 18);
    b0 = ROL64((a11^d1), 1);
    b1 = ROL64((a42^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a04^d4), 8);
    a30 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a20^d0), 36);
    b2 = ROL64((a01^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a13^d3), 56);
    b0 = ROL64((a44^d4), 27);
    a20 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a10^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a22^d2), 62);
    b1 = ROL64((a03^d3), 55);
    b2 = ROL64((a34^d4), 39);
    a10 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    c0 = a00^a40^a30^a20^a10;
    c1 = a31^a21^a11^a01^a41;
    c2 = a12^a02^a42^a32^a22;
    c3 = a43^a33^a23^a13^a03;
    c4 = a24^a14^a04^a44^a34;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a21^d1), 44);
    b2 = ROL64((a42^d2), 43);
    b3 = ROL64((a13^d3), 21);
    b4 = ROL64((a34^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+2];
    a21 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a30^d0), 3);
    b3 = ROL64((a01^d1), 45);
    b4 = ROL64((a22^d2), 61);
    b0 = ROL64((a43^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a30 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a10^d0), 18);
    b0 = ROL64((a31^d1), 1);
    b1 = ROL64((a02^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a44^d4), 8);
    a10 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a40^d0), 36);
    b2 = ROL64((a11^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a03^d3), 56);
    b0 = ROL64((a24^d4), 27);
    a40 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a20^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a12^d2), 62);
    b1 = ROL64((a33^d3), 55);
    b2 = ROL64((a04^d4), 39);
    a20 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    c0 = a00^a30^a10^a40^a20;
    c1 = a21^a01^a31^a11^a41;
    c2 = a42^a22^a02^a32^a12;
    c3 = a13^a43^a23^a03^a33;
    c4 = a34^a14^a44^a24^a04;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a01^d1), 44);
    b2 = ROL64((a02^d2), 43);
    b3 = ROL64((a03^d3), 21);
    b4 = ROL64((a04^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+3];
    a01 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a10^d0), 3);
    b3 = ROL64((a11^d1), 45);
    b4 = ROL64((a12^d2), 61);
    b0 = ROL64((a13^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a10 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a20^d0), 18);
    b0 = ROL64((a21^d1), 1);
    b1 = ROL64((a22^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a24^d4), 8);
    a20 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a30^d0), 36);
    b2 = ROL64((a31^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a33^d3), 56);
    b0 = ROL64((a34^d4), 27);
    a30 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a40^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a42^d2), 62);
    b1 = ROL64((a43^d3), 55);
    b2 = ROL64((a44^d4), 39);
    a40 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );
  }
}

/*
** Initialize a new hash.  iSize determines the size of the hash
** in bits and should be one of 224, 256, 384, or 512.  Or iSize
** can be zero to use the default hash size of 256 bits.
*/
static void SHA3Init(SHA3Context *p, int iSize){
  memset(p, 0, sizeof(*p));
  if( iSize>=128 && iSize<=512 ){
    p->nRate = (1600 - ((iSize + 31)&~31)*2)/8;
  }else{
    p->nRate = (1600 - 2*256)/8;
  }
#if SHA3_BYTEORDER==1234
  /* Known to be little-endian at compile-time. No-op */
#elif SHA3_BYTEORDER==4321
  p->ixMask = 7;  /* Big-endian */
#else
  {
    static unsigned int one = 1;
    if( 1==*(unsigned char*)&one ){
      /* Little endian.  No byte swapping. */
      p->ixMask = 0;
    }else{
      /* Big endian.  Byte swap. */
      p->ixMask = 7;
    }
  }
#endif
}

/*
** Make consecutive calls to the SHA3Update function to add new content
** to the hash
*/
static void SHA3Update(
  SHA3Context *p,
  const unsigned char *aData,
  unsigned int nData
){
  unsigned int i = 0;
#if SHA3_BYTEORDER==1234
  if( (p->nLoaded % 8)==0 && ((aData - (const unsigned char*)0)&7)==0 ){
    for(; i+7<nData; i+=8){
      p->u.s[p->nLoaded/8] ^= *(u64*)&aData[i];
      p->nLoaded += 8;
      if( p->nLoaded>=p->nRate ){
        KeccakF1600Step(p);
        p->nLoaded = 0;
      }
    }
  }
#endif
  for(; i<nData; i++){
#if SHA3_BYTEORDER==1234
    p->u.x[p->nLoaded] ^= aData[i];
#elif SHA3_BYTEORDER==4321
    p->u.x[p->nLoaded^0x07] ^= aData[i];
#else
    p->u.x[p->nLoaded^p->ixMask] ^= aData[i];
#endif
    p->nLoaded++;
    if( p->nLoaded==p->nRate ){
      KeccakF1600Step(p);
      p->nLoaded = 0;
    }
  }
}

/*
** After all content has been added, invoke SHA3Final() to compute
** the final hash.  The function returns a pointer to the binary
** hash value.
*/
static unsigned char *SHA3Final(SHA3Context *p){
  unsigned int i;
  if( p->nLoaded==p->nRate-1 ){
    const unsigned char c1 = 0x86;
    SHA3Update(p, &c1, 1);
  }else{
    const unsigned char c2 = 0x06;
    const unsigned char c3 = 0x80;
    SHA3Update(p, &c2, 1);
    p->nLoaded = p->nRate - 1;
    SHA3Update(p, &c3, 1);
  }
  for(i=0; i<p->nRate; i++){
    p->u.x[i+p->nRate] = p->u.x[i^p->ixMask];
  }
  return &p->u.x[p->nRate];
}
/* End of the hashing logic
*****************************************************************************/

/*
** Implementation of the sha3(X,SIZE) function.
**
** Return a BLOB which is the SIZE-bit SHA3 hash of X.  The default
** size is 256.  If X is a BLOB, it is hashed as is.  
** For all other non-NULL types of input, X is converted into a UTF-8 string
** and the string is hashed without the trailing 0x00 terminator.  The hash
** of a NULL value is NULL.
*/
static void sha3Func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  SHA3Context cx;
  int eType = sqlite3_value_type(argv[0]);
  int nByte = sqlite3_value_bytes(argv[0]);
  int iSize;
  if( argc==1 ){
    iSize = 256;
  }else{
    iSize = sqlite3_value_int(argv[1]);
    if( iSize!=224 && iSize!=256 && iSize!=384 && iSize!=512 ){
      sqlite3_result_error(context, "SHA3 size should be one of: 224 256 "
                                    "384 512", -1);
      return;
    }
  }
  if( eType==SQLITE_NULL ) return;
  SHA3Init(&cx, iSize);
  if( eType==SQLITE_BLOB ){
    SHA3Update(&cx, sqlite3_value_blob(argv[0]), nByte);
  }else{
    SHA3Update(&cx, sqlite3_value_text(argv[0]), nByte);
  }
  sqlite3_result_blob(context, SHA3Final(&cx), iSize/8, SQLITE_TRANSIENT);
}

/* Compute a string using sqlite3_vsnprintf() with a maximum length
** of 50 bytes and add it to the hash.
*/
static void hash_step_vformat(
  SHA3Context *p,                 /* Add content to this context */
  const char *zFormat,
  ...
){
  va_list ap;
  int n;
  char zBuf[50];
  va_start(ap, zFormat);
  sqlite3_vsnprintf(sizeof(zBuf),zBuf,zFormat,ap);
  va_end(ap);
  n = (int)strlen(zBuf);
  SHA3Update(p, (unsigned char*)zBuf, n);
}

/*
** Implementation of the sha3_query(SQL,SIZE) function.
**
** This function compiles and runs the SQL statement(s) given in the
** argument. The results are hashed using a SIZE-bit SHA3.  The default
** size is 256.
**
** The format of the byte stream that is hashed is summarized as follows:
**
**       S<n>:<sql>
**       R
**       N
**       I<int>
**       F<ieee-float>
**       B<size>:<bytes>
**       T<size>:<text>
**
** <sql> is the original SQL text for each statement run and <n> is
** the size of that text.  The SQL text is UTF-8.  A single R character
** occurs before the start of each row.  N means a NULL value.
** I mean an 8-byte little-endian integer <int>.  F is a floating point
** number with an 8-byte little-endian IEEE floating point value <ieee-float>.
** B means blobs of <size> bytes.  T means text rendered as <size>
** bytes of UTF-8.  The <n> and <size> values are expressed as an ASCII
** text integers.
**
** For each SQL statement in the X input, there is one S segment.  Each
** S segment is followed by zero or more R segments, one for each row in the
** result set.  After each R, there are one or more N, I, F, B, or T segments,
** one for each column in the result set.  Segments are concatentated directly
** with no delimiters of any kind.
*/
static void sha3QueryFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3 *db = sqlite3_context_db_handle(context);
  const char *zSql = (const char*)sqlite3_value_text(argv[0]);
  sqlite3_stmt *pStmt = 0;
  int nCol;                   /* Number of columns in the result set */
  int i;                      /* Loop counter */
  int rc;
  int n;
  const char *z;
  SHA3Context cx;
  int iSize;

  if( argc==1 ){
    iSize = 256;
  }else{
    iSize = sqlite3_value_int(argv[1]);
    if( iSize!=224 && iSize!=256 && iSize!=384 && iSize!=512 ){
      sqlite3_result_error(context, "SHA3 size should be one of: 224 256 "
                                    "384 512", -1);
      return;
    }
  }
  if( zSql==0 ) return;
  SHA3Init(&cx, iSize);
  while( zSql[0] ){
    rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zSql);
    if( rc ){
      char *zMsg = sqlite3_mprintf("error SQL statement [%s]: %s",
                                   zSql, sqlite3_errmsg(db));
      sqlite3_finalize(pStmt);
      sqlite3_result_error(context, zMsg, -1);
      sqlite3_free(zMsg);
      return;
    }
    if( !sqlite3_stmt_readonly(pStmt) ){
      char *zMsg = sqlite3_mprintf("non-query: [%s]", sqlite3_sql(pStmt));
      sqlite3_finalize(pStmt);
      sqlite3_result_error(context, zMsg, -1);
      sqlite3_free(zMsg);
      return;
    }
    nCol = sqlite3_column_count(pStmt);
    z = sqlite3_sql(pStmt);
    n = (int)strlen(z);
    hash_step_vformat(&cx,"S%d:",n);
    SHA3Update(&cx,(unsigned char*)z,n);

    /* Compute a hash over the result of the query */
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      SHA3Update(&cx,(const unsigned char*)"R",1);
      for(i=0; i<nCol; i++){
        switch( sqlite3_column_type(pStmt,i) ){
          case SQLITE_NULL: {
            SHA3Update(&cx, (const unsigned char*)"N",1);
            break;
          }
          case SQLITE_INTEGER: {
            sqlite3_uint64 u;
            int j;
            unsigned char x[9];
            sqlite3_int64 v = sqlite3_column_int64(pStmt,i);
            memcpy(&u, &v, 8);
            for(j=8; j>=1; j--){
              x[j] = u & 0xff;
              u >>= 8;
            }
            x[0] = 'I';
            SHA3Update(&cx, x, 9);
            break;
          }
          case SQLITE_FLOAT: {
            sqlite3_uint64 u;
            int j;
            unsigned char x[9];
            double r = sqlite3_column_double(pStmt,i);
            memcpy(&u, &r, 8);
            for(j=8; j>=1; j--){
              x[j] = u & 0xff;
              u >>= 8;
            }
            x[0] = 'F';
            SHA3Update(&cx,x,9);
            break;
          }
          case SQLITE_TEXT: {
            int n2 = sqlite3_column_bytes(pStmt, i);
            const unsigned char *z2 = sqlite3_column_text(pStmt, i);
            hash_step_vformat(&cx,"T%d:",n2);
            SHA3Update(&cx, z2, n2);
            break;
          }
          case SQLITE_BLOB: {
            int n2 = sqlite3_column_bytes(pStmt, i);
            const unsigned char *z2 = sqlite3_column_blob(pStmt, i);
            hash_step_vformat(&cx,"B%d:",n2);
            SHA3Update(&cx, z2, n2);
            break;
          }
        }
      }
    }
    sqlite3_finalize(pStmt);
  }
  sqlite3_result_blob(context, SHA3Final(&cx), iSize/8, SQLITE_TRANSIENT);
}


#ifdef _WIN32

#endif
int sqlite3_shathree_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "sha3", 1, SQLITE_UTF8, 0,
                               sha3Func, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "sha3", 2, SQLITE_UTF8, 0,
                                 sha3Func, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "sha3_query", 1, SQLITE_UTF8, 0,
                                 sha3QueryFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "sha3_query", 2, SQLITE_UTF8, 0,
                                 sha3QueryFunc, 0, 0);
  }
  return rc;
}

/************************* End ../ext/misc/shathree.c ********************/
/************************* Begin ../ext/misc/fileio.c ******************/
/*
** 2014-06-13
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This SQLite extension implements SQL functions readfile() and
** writefile(), and eponymous virtual type "fsdir".
**
** WRITEFILE(FILE, DATA [, MODE [, MTIME]]):
**
**   If neither of the optional arguments is present, then this UDF
**   function writes blob DATA to file FILE. If successful, the number
**   of bytes written is returned. If an error occurs, NULL is returned.
**
**   If the first option argument - MODE - is present, then it must
**   be passed an integer value that corresponds to a POSIX mode
**   value (file type + permissions, as returned in the stat.st_mode
**   field by the stat() system call). Three types of files may
**   be written/created:
**
**     regular files:  (mode & 0170000)==0100000
**     symbolic links: (mode & 0170000)==0120000
**     directories:    (mode & 0170000)==0040000
**
**   For a directory, the DATA is ignored. For a symbolic link, it is
**   interpreted as text and used as the target of the link. For a
**   regular file, it is interpreted as a blob and written into the
**   named file. Regardless of the type of file, its permissions are
**   set to (mode & 0777) before returning.
**
**   If the optional MTIME argument is present, then it is interpreted
**   as an integer - the number of seconds since the unix epoch. The
**   modification-time of the target file is set to this value before
**   returning.
**
**   If three or more arguments are passed to this function and an
**   error is encountered, an exception is raised.
**
** READFILE(FILE):
**
**   Read and return the contents of file FILE (type blob) from disk.
**
** FSDIR:
**
**   Used as follows:
**
**     SELECT * FROM fsdir($path [, $dir]);
**
**   Parameter $path is an absolute or relative pathname. If the file that it
**   refers to does not exist, it is an error. If the path refers to a regular
**   file or symbolic link, it returns a single row. Or, if the path refers
**   to a directory, it returns one row for the directory, and one row for each
**   file within the hierarchy rooted at $path.
**
**   Each row has the following columns:
**
**     name:  Path to file or directory (text value).
**     mode:  Value of stat.st_mode for directory entry (an integer).
**     mtime: Value of stat.st_mtime for directory entry (an integer).
**     data:  For a regular file, a blob containing the file data. For a
**            symlink, a text value containing the text of the link. For a
**            directory, NULL.
**
**   If a non-NULL value is specified for the optional $dir parameter and
**   $path is a relative path, then $path is interpreted relative to $dir. 
**   And the paths returned in the "name" column of the table are also 
**   relative to directory $dir.
*/
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if !defined(_WIN32) && !defined(WIN32)
#  include <unistd.h>
#  include <dirent.h>
#  include <utime.h>
#  include <sys/time.h>
#else
#  include "windows.h"
#  include <io.h>
#  include <direct.h>
/* #  include "test_windirent.h" */
#  define dirent DIRENT
#  ifndef chmod
#    define chmod _chmod
#  endif
#  ifndef stat
#    define stat _stat
#  endif
#  define mkdir(path,mode) _mkdir(path)
#  define lstat(path,buf) stat(path,buf)
#endif
#include <time.h>
#include <errno.h>


#define FSDIR_SCHEMA "(name,mode,mtime,data,path HIDDEN,dir HIDDEN)"

/*
** Set the result stored by context ctx to a blob containing the 
** contents of file zName.
*/
static void readFileContents(sqlite3_context *ctx, const char *zName){
  FILE *in;
  long nIn;
  void *pBuf;

  in = fopen(zName, "rb");
  if( in==0 ) return;
  fseek(in, 0, SEEK_END);
  nIn = ftell(in);
  rewind(in);
  pBuf = sqlite3_malloc( nIn );
  if( pBuf && 1==fread(pBuf, nIn, 1, in) ){
    sqlite3_result_blob(ctx, pBuf, nIn, sqlite3_free);
  }else{
    sqlite3_free(pBuf);
  }
  fclose(in);
}

/*
** Implementation of the "readfile(X)" SQL function.  The entire content
** of the file named X is read and returned as a BLOB.  NULL is returned
** if the file does not exist or is unreadable.
*/
static void readfileFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName;
  (void)(argc);  /* Unused parameter */
  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 ) return;
  readFileContents(context, zName);
}

/*
** Set the error message contained in context ctx to the results of
** vprintf(zFmt, ...).
*/
static void ctxErrorMsg(sqlite3_context *ctx, const char *zFmt, ...){
  char *zMsg = 0;
  va_list ap;
  va_start(ap, zFmt);
  zMsg = sqlite3_vmprintf(zFmt, ap);
  sqlite3_result_error(ctx, zMsg, -1);
  sqlite3_free(zMsg);
  va_end(ap);
}

#if defined(_WIN32)
/*
** This function is designed to convert a Win32 FILETIME structure into the
** number of seconds since the Unix Epoch (1970-01-01 00:00:00 UTC).
*/
static sqlite3_uint64 fileTimeToUnixTime(
  LPFILETIME pFileTime
){
  SYSTEMTIME epochSystemTime;
  ULARGE_INTEGER epochIntervals;
  FILETIME epochFileTime;
  ULARGE_INTEGER fileIntervals;

  memset(&epochSystemTime, 0, sizeof(SYSTEMTIME));
  epochSystemTime.wYear = 1970;
  epochSystemTime.wMonth = 1;
  epochSystemTime.wDay = 1;
  SystemTimeToFileTime(&epochSystemTime, &epochFileTime);
  epochIntervals.LowPart = epochFileTime.dwLowDateTime;
  epochIntervals.HighPart = epochFileTime.dwHighDateTime;

  fileIntervals.LowPart = pFileTime->dwLowDateTime;
  fileIntervals.HighPart = pFileTime->dwHighDateTime;

  return (fileIntervals.QuadPart - epochIntervals.QuadPart) / 10000000;
}

/*
** This function attempts to normalize the time values found in the stat()
** buffer to UTC.  This is necessary on Win32, where the runtime library
** appears to return these values as local times.
*/
static void statTimesToUtc(
  const char *zPath,
  struct stat *pStatBuf
){
  HANDLE hFindFile;
  WIN32_FIND_DATAW fd;
  LPWSTR zUnicodeName;
  extern LPWSTR sqlite3_win32_utf8_to_unicode(const char*);
  zUnicodeName = sqlite3_win32_utf8_to_unicode(zPath);
  if( zUnicodeName ){
    memset(&fd, 0, sizeof(WIN32_FIND_DATAW));
    hFindFile = FindFirstFileW(zUnicodeName, &fd);
    if( hFindFile!=NULL ){
      pStatBuf->st_ctime = (time_t)fileTimeToUnixTime(&fd.ftCreationTime);
      pStatBuf->st_atime = (time_t)fileTimeToUnixTime(&fd.ftLastAccessTime);
      pStatBuf->st_mtime = (time_t)fileTimeToUnixTime(&fd.ftLastWriteTime);
      FindClose(hFindFile);
    }
    sqlite3_free(zUnicodeName);
  }
}
#endif

/*
** This function is used in place of stat().  On Windows, special handling
** is required in order for the included time to be returned as UTC.  On all
** other systems, this function simply calls stat().
*/
static int fileStat(
  const char *zPath,
  struct stat *pStatBuf
){
#if defined(_WIN32)
  int rc = stat(zPath, pStatBuf);
  if( rc==0 ) statTimesToUtc(zPath, pStatBuf);
  return rc;
#else
  return stat(zPath, pStatBuf);
#endif
}

/*
** This function is used in place of lstat().  On Windows, special handling
** is required in order for the included time to be returned as UTC.  On all
** other systems, this function simply calls lstat().
*/
static int fileLinkStat(
  const char *zPath,
  struct stat *pStatBuf
){
#if defined(_WIN32)
  int rc = lstat(zPath, pStatBuf);
  if( rc==0 ) statTimesToUtc(zPath, pStatBuf);
  return rc;
#else
  return lstat(zPath, pStatBuf);
#endif
}

/*
** Argument zFile is the name of a file that will be created and/or written
** by SQL function writefile(). This function ensures that the directory
** zFile will be written to exists, creating it if required. The permissions
** for any path components created by this function are set to (mode&0777).
**
** If an OOM condition is encountered, SQLITE_NOMEM is returned. Otherwise,
** SQLITE_OK is returned if the directory is successfully created, or
** SQLITE_ERROR otherwise.
*/
static int makeDirectory(
  const char *zFile,
  mode_t mode
){
  char *zCopy = sqlite3_mprintf("%s", zFile);
  int rc = SQLITE_OK;

  if( zCopy==0 ){
    rc = SQLITE_NOMEM;
  }else{
    int nCopy = (int)strlen(zCopy);
    int i = 1;

    while( rc==SQLITE_OK ){
      struct stat sStat;
      int rc2;

      for(; zCopy[i]!='/' && i<nCopy; i++);
      if( i==nCopy ) break;
      zCopy[i] = '\0';

      rc2 = fileStat(zCopy, &sStat);
      if( rc2!=0 ){
        if( mkdir(zCopy, mode & 0777) ) rc = SQLITE_ERROR;
      }else{
        if( !S_ISDIR(sStat.st_mode) ) rc = SQLITE_ERROR;
      }
      zCopy[i] = '/';
      i++;
    }

    sqlite3_free(zCopy);
  }

  return rc;
}

/*
** This function does the work for the writefile() UDF. Refer to 
** header comments at the top of this file for details.
*/
static int writeFile(
  sqlite3_context *pCtx,          /* Context to return bytes written in */
  const char *zFile,              /* File to write */
  sqlite3_value *pData,           /* Data to write */
  mode_t mode,                    /* MODE parameter passed to writefile() */
  sqlite3_int64 mtime             /* MTIME parameter (or -1 to not set time) */
){
#if !defined(_WIN32) && !defined(WIN32)
  if( S_ISLNK(mode) ){
    const char *zTo = (const char*)sqlite3_value_text(pData);
    if( symlink(zTo, zFile)<0 ) return 1;
  }else
#endif
  {
    if( S_ISDIR(mode) ){
      if( mkdir(zFile, mode) ){
        /* The mkdir() call to create the directory failed. This might not
        ** be an error though - if there is already a directory at the same
        ** path and either the permissions already match or can be changed
        ** to do so using chmod(), it is not an error.  */
        struct stat sStat;
        if( errno!=EEXIST
         || 0!=fileStat(zFile, &sStat)
         || !S_ISDIR(sStat.st_mode)
         || ((sStat.st_mode&0777)!=(mode&0777) && 0!=chmod(zFile, mode&0777))
        ){
          return 1;
        }
      }
    }else{
      sqlite3_int64 nWrite = 0;
      const char *z;
      int rc = 0;
      FILE *out = fopen(zFile, "wb");
      if( out==0 ) return 1;
      z = (const char*)sqlite3_value_blob(pData);
      if( z ){
        sqlite3_int64 n = fwrite(z, 1, sqlite3_value_bytes(pData), out);
        nWrite = sqlite3_value_bytes(pData);
        if( nWrite!=n ){
          rc = 1;
        }
      }
      fclose(out);
      if( rc==0 && mode && chmod(zFile, mode & 0777) ){
        rc = 1;
      }
      if( rc ) return 2;
      sqlite3_result_int64(pCtx, nWrite);
    }
  }

  if( mtime>=0 ){
#if defined(_WIN32)
    /* Windows */
    FILETIME lastAccess;
    FILETIME lastWrite;
    SYSTEMTIME currentTime;
    LONGLONG intervals;
    HANDLE hFile;
    LPWSTR zUnicodeName;
    extern LPWSTR sqlite3_win32_utf8_to_unicode(const char*);

    GetSystemTime(&currentTime);
    SystemTimeToFileTime(&currentTime, &lastAccess);
    intervals = Int32x32To64(mtime, 10000000) + 116444736000000000;
    lastWrite.dwLowDateTime = (DWORD)intervals;
    lastWrite.dwHighDateTime = intervals >> 32;
    zUnicodeName = sqlite3_win32_utf8_to_unicode(zFile);
    if( zUnicodeName==0 ){
      return 1;
    }
    hFile = CreateFileW(
      zUnicodeName, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, NULL
    );
    sqlite3_free(zUnicodeName);
    if( hFile!=INVALID_HANDLE_VALUE ){
      BOOL bResult = SetFileTime(hFile, NULL, &lastAccess, &lastWrite);
      CloseHandle(hFile);
      return !bResult;
    }else{
      return 1;
    }
#elif defined(AT_FDCWD) && 0 /* utimensat() is not universally available */
    /* Recent unix */
    struct timespec times[2];
    times[0].tv_nsec = times[1].tv_nsec = 0;
    times[0].tv_sec = time(0);
    times[1].tv_sec = mtime;
    if( utimensat(AT_FDCWD, zFile, times, AT_SYMLINK_NOFOLLOW) ){
      return 1;
    }
#else
    /* Legacy unix */
    struct timeval times[2];
    times[0].tv_usec = times[1].tv_usec = 0;
    times[0].tv_sec = time(0);
    times[1].tv_sec = mtime;
    if( utimes(zFile, times) ){
      return 1;
    }
#endif
  }

  return 0;
}

/*
** Implementation of the "writefile(W,X[,Y[,Z]]])" SQL function.  
** Refer to header comments at the top of this file for details.
*/
static void writefileFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zFile;
  mode_t mode = 0;
  int res;
  sqlite3_int64 mtime = -1;

  if( argc<2 || argc>4 ){
    sqlite3_result_error(context, 
        "wrong number of arguments to function writefile()", -1
    );
    return;
  }

  zFile = (const char*)sqlite3_value_text(argv[0]);
  if( zFile==0 ) return;
  if( argc>=3 ){
    mode = (mode_t)sqlite3_value_int(argv[2]);
  }
  if( argc==4 ){
    mtime = sqlite3_value_int64(argv[3]);
  }

  res = writeFile(context, zFile, argv[1], mode, mtime);
  if( res==1 && errno==ENOENT ){
    if( makeDirectory(zFile, mode)==SQLITE_OK ){
      res = writeFile(context, zFile, argv[1], mode, mtime);
    }
  }

  if( argc>2 && res!=0 ){
    if( S_ISLNK(mode) ){
      ctxErrorMsg(context, "failed to create symlink: %s", zFile);
    }else if( S_ISDIR(mode) ){
      ctxErrorMsg(context, "failed to create directory: %s", zFile);
    }else{
      ctxErrorMsg(context, "failed to write file: %s", zFile);
    }
  }
}

/*
** SQL function:   lsmode(MODE)
**
** Given a numberic st_mode from stat(), convert it into a human-readable
** text string in the style of "ls -l".
*/
static void lsModeFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int i;
  int iMode = sqlite3_value_int(argv[0]);
  char z[16];
  (void)argc;
  if( S_ISLNK(iMode) ){
    z[0] = 'l';
  }else if( S_ISREG(iMode) ){
    z[0] = '-';
  }else if( S_ISDIR(iMode) ){
    z[0] = 'd';
  }else{
    z[0] = '?';
  }
  for(i=0; i<3; i++){
    int m = (iMode >> ((2-i)*3));
    char *a = &z[1 + i*3];
    a[0] = (m & 0x4) ? 'r' : '-';
    a[1] = (m & 0x2) ? 'w' : '-';
    a[2] = (m & 0x1) ? 'x' : '-';
  }
  z[10] = '\0';
  sqlite3_result_text(context, z, -1, SQLITE_TRANSIENT);
}

#ifndef SQLITE_OMIT_VIRTUALTABLE

/* 
** Cursor type for recursively iterating through a directory structure.
*/
typedef struct fsdir_cursor fsdir_cursor;
typedef struct FsdirLevel FsdirLevel;

struct FsdirLevel {
  DIR *pDir;                 /* From opendir() */
  char *zDir;                /* Name of directory (nul-terminated) */
};

struct fsdir_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */

  int nLvl;                  /* Number of entries in aLvl[] array */
  int iLvl;                  /* Index of current entry */
  FsdirLevel *aLvl;          /* Hierarchy of directories being traversed */

  const char *zBase;
  int nBase;

  struct stat sStat;         /* Current lstat() results */
  char *zPath;               /* Path to current entry */
  sqlite3_int64 iRowid;      /* Current rowid */
};

typedef struct fsdir_tab fsdir_tab;
struct fsdir_tab {
  sqlite3_vtab base;         /* Base class - must be first */
};

/*
** Construct a new fsdir virtual table object.
*/
static int fsdirConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  fsdir_tab *pNew = 0;
  int rc;
  (void)pAux;
  (void)argc;
  (void)argv;
  (void)pzErr;
  rc = sqlite3_declare_vtab(db, "CREATE TABLE x" FSDIR_SCHEMA);
  if( rc==SQLITE_OK ){
    pNew = (fsdir_tab*)sqlite3_malloc( sizeof(*pNew) );
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  *ppVtab = (sqlite3_vtab*)pNew;
  return rc;
}

/*
** This method is the destructor for fsdir vtab objects.
*/
static int fsdirDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/*
** Constructor for a new fsdir_cursor object.
*/
static int fsdirOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  fsdir_cursor *pCur;
  (void)p;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  pCur->iLvl = -1;
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Reset a cursor back to the state it was in when first returned
** by fsdirOpen().
*/
static void fsdirResetCursor(fsdir_cursor *pCur){
  int i;
  for(i=0; i<=pCur->iLvl; i++){
    FsdirLevel *pLvl = &pCur->aLvl[i];
    if( pLvl->pDir ) closedir(pLvl->pDir);
    sqlite3_free(pLvl->zDir);
  }
  sqlite3_free(pCur->zPath);
  sqlite3_free(pCur->aLvl);
  pCur->aLvl = 0;
  pCur->zPath = 0;
  pCur->zBase = 0;
  pCur->nBase = 0;
  pCur->nLvl = 0;
  pCur->iLvl = -1;
  pCur->iRowid = 1;
}

/*
** Destructor for an fsdir_cursor.
*/
static int fsdirClose(sqlite3_vtab_cursor *cur){
  fsdir_cursor *pCur = (fsdir_cursor*)cur;

  fsdirResetCursor(pCur);
  sqlite3_free(pCur);
  return SQLITE_OK;
}

/*
** Set the error message for the virtual table associated with cursor
** pCur to the results of vprintf(zFmt, ...).
*/
static void fsdirSetErrmsg(fsdir_cursor *pCur, const char *zFmt, ...){
  va_list ap;
  va_start(ap, zFmt);
  pCur->base.pVtab->zErrMsg = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
}


/*
** Advance an fsdir_cursor to its next row of output.
*/
static int fsdirNext(sqlite3_vtab_cursor *cur){
  fsdir_cursor *pCur = (fsdir_cursor*)cur;
  mode_t m = pCur->sStat.st_mode;

  pCur->iRowid++;
  if( S_ISDIR(m) ){
    /* Descend into this directory */
    int iNew = pCur->iLvl + 1;
    FsdirLevel *pLvl;
    if( iNew>=pCur->nLvl ){
      int nNew = iNew+1;
      int nByte = nNew*sizeof(FsdirLevel);
      FsdirLevel *aNew = (FsdirLevel*)sqlite3_realloc(pCur->aLvl, nByte);
      if( aNew==0 ) return SQLITE_NOMEM;
      memset(&aNew[pCur->nLvl], 0, sizeof(FsdirLevel)*(nNew-pCur->nLvl));
      pCur->aLvl = aNew;
      pCur->nLvl = nNew;
    }
    pCur->iLvl = iNew;
    pLvl = &pCur->aLvl[iNew];
    
    pLvl->zDir = pCur->zPath;
    pCur->zPath = 0;
    pLvl->pDir = opendir(pLvl->zDir);
    if( pLvl->pDir==0 ){
      fsdirSetErrmsg(pCur, "cannot read directory: %s", pCur->zPath);
      return SQLITE_ERROR;
    }
  }

  while( pCur->iLvl>=0 ){
    FsdirLevel *pLvl = &pCur->aLvl[pCur->iLvl];
    struct dirent *pEntry = readdir(pLvl->pDir);
    if( pEntry ){
      if( pEntry->d_name[0]=='.' ){
       if( pEntry->d_name[1]=='.' && pEntry->d_name[2]=='\0' ) continue;
       if( pEntry->d_name[1]=='\0' ) continue;
      }
      sqlite3_free(pCur->zPath);
      pCur->zPath = sqlite3_mprintf("%s/%s", pLvl->zDir, pEntry->d_name);
      if( pCur->zPath==0 ) return SQLITE_NOMEM;
      if( fileLinkStat(pCur->zPath, &pCur->sStat) ){
        fsdirSetErrmsg(pCur, "cannot stat file: %s", pCur->zPath);
        return SQLITE_ERROR;
      }
      return SQLITE_OK;
    }
    closedir(pLvl->pDir);
    sqlite3_free(pLvl->zDir);
    pLvl->pDir = 0;
    pLvl->zDir = 0;
    pCur->iLvl--;
  }

  /* EOF */
  sqlite3_free(pCur->zPath);
  pCur->zPath = 0;
  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the series_cursor
** is currently pointing.
*/
static int fsdirColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  fsdir_cursor *pCur = (fsdir_cursor*)cur;
  switch( i ){
    case 0: { /* name */
      sqlite3_result_text(ctx, &pCur->zPath[pCur->nBase], -1, SQLITE_TRANSIENT);
      break;
    }

    case 1: /* mode */
      sqlite3_result_int64(ctx, pCur->sStat.st_mode);
      break;

    case 2: /* mtime */
      sqlite3_result_int64(ctx, pCur->sStat.st_mtime);
      break;

    case 3: { /* data */
      mode_t m = pCur->sStat.st_mode;
      if( S_ISDIR(m) ){
        sqlite3_result_null(ctx);
#if !defined(_WIN32) && !defined(WIN32)
      }else if( S_ISLNK(m) ){
        char aStatic[64];
        char *aBuf = aStatic;
        int nBuf = 64;
        int n;

        while( 1 ){
          n = readlink(pCur->zPath, aBuf, nBuf);
          if( n<nBuf ) break;
          if( aBuf!=aStatic ) sqlite3_free(aBuf);
          nBuf = nBuf*2;
          aBuf = sqlite3_malloc(nBuf);
          if( aBuf==0 ){
            sqlite3_result_error_nomem(ctx);
            return SQLITE_NOMEM;
          }
        }

        sqlite3_result_text(ctx, aBuf, n, SQLITE_TRANSIENT);
        if( aBuf!=aStatic ) sqlite3_free(aBuf);
#endif
      }else{
        readFileContents(ctx, pCur->zPath);
      }
    }
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row. In this implementation, the
** first row returned is assigned rowid value 1, and each subsequent
** row a value 1 more than that of the previous.
*/
static int fsdirRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  fsdir_cursor *pCur = (fsdir_cursor*)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int fsdirEof(sqlite3_vtab_cursor *cur){
  fsdir_cursor *pCur = (fsdir_cursor*)cur;
  return (pCur->zPath==0);
}

/*
** xFilter callback.
*/
static int fsdirFilter(
  sqlite3_vtab_cursor *cur, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  const char *zDir = 0;
  fsdir_cursor *pCur = (fsdir_cursor*)cur;
  (void)idxStr;
  fsdirResetCursor(pCur);

  if( idxNum==0 ){
    fsdirSetErrmsg(pCur, "table function fsdir requires an argument");
    return SQLITE_ERROR;
  }

  assert( argc==idxNum && (argc==1 || argc==2) );
  zDir = (const char*)sqlite3_value_text(argv[0]);
  if( zDir==0 ){
    fsdirSetErrmsg(pCur, "table function fsdir requires a non-NULL argument");
    return SQLITE_ERROR;
  }
  if( argc==2 ){
    pCur->zBase = (const char*)sqlite3_value_text(argv[1]);
  }
  if( pCur->zBase ){
    pCur->nBase = (int)strlen(pCur->zBase)+1;
    pCur->zPath = sqlite3_mprintf("%s/%s", pCur->zBase, zDir);
  }else{
    pCur->zPath = sqlite3_mprintf("%s", zDir);
  }

  if( pCur->zPath==0 ){
    return SQLITE_NOMEM;
  }
  if( fileLinkStat(pCur->zPath, &pCur->sStat) ){
    fsdirSetErrmsg(pCur, "cannot stat file: %s", pCur->zPath);
    return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the generate_series virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
**
** In this implementation idxNum is used to represent the
** query plan.  idxStr is unused.
**
** The query plan is represented by bits in idxNum:
**
**  (1)  start = $value  -- constraint exists
**  (2)  stop = $value   -- constraint exists
**  (4)  step = $value   -- constraint exists
**  (8)  output in descending order
*/
static int fsdirBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  int i;                 /* Loop over constraints */
  int idx4 = -1;
  int idx5 = -1;
  const struct sqlite3_index_constraint *pConstraint;

  (void)tab;
  pConstraint = pIdxInfo->aConstraint;
  for(i=0; i<pIdxInfo->nConstraint; i++, pConstraint++){
    if( pConstraint->usable==0 ) continue;
    if( pConstraint->op!=SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    if( pConstraint->iColumn==4 ) idx4 = i;
    if( pConstraint->iColumn==5 ) idx5 = i;
  }

  if( idx4<0 ){
    pIdxInfo->idxNum = 0;
    pIdxInfo->estimatedCost = (double)(((sqlite3_int64)1) << 50);
  }else{
    pIdxInfo->aConstraintUsage[idx4].omit = 1;
    pIdxInfo->aConstraintUsage[idx4].argvIndex = 1;
    if( idx5>=0 ){
      pIdxInfo->aConstraintUsage[idx5].omit = 1;
      pIdxInfo->aConstraintUsage[idx5].argvIndex = 2;
      pIdxInfo->idxNum = 2;
      pIdxInfo->estimatedCost = 10.0;
    }else{
      pIdxInfo->idxNum = 1;
      pIdxInfo->estimatedCost = 100.0;
    }
  }

  return SQLITE_OK;
}

/*
** Register the "fsdir" virtual table.
*/
static int fsdirRegister(sqlite3 *db){
  static sqlite3_module fsdirModule = {
    0,                         /* iVersion */
    0,                         /* xCreate */
    fsdirConnect,              /* xConnect */
    fsdirBestIndex,            /* xBestIndex */
    fsdirDisconnect,           /* xDisconnect */
    0,                         /* xDestroy */
    fsdirOpen,                 /* xOpen - open a cursor */
    fsdirClose,                /* xClose - close a cursor */
    fsdirFilter,               /* xFilter - configure scan constraints */
    fsdirNext,                 /* xNext - advance a cursor */
    fsdirEof,                  /* xEof - check for end of scan */
    fsdirColumn,               /* xColumn - read data */
    fsdirRowid,                /* xRowid - read data */
    0,                         /* xUpdate */
    0,                         /* xBegin */
    0,                         /* xSync */
    0,                         /* xCommit */
    0,                         /* xRollback */
    0,                         /* xFindMethod */
    0,                         /* xRename */
    0,                         /* xSavepoint */
    0,                         /* xRelease */
    0                          /* xRollbackTo */
  };

  int rc = sqlite3_create_module(db, "fsdir", &fsdirModule, 0);
  return rc;
}
#else         /* SQLITE_OMIT_VIRTUALTABLE */
# define fsdirRegister(x) SQLITE_OK
#endif

#ifdef _WIN32

#endif
int sqlite3_fileio_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "readfile", 1, SQLITE_UTF8, 0,
                               readfileFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "writefile", -1, SQLITE_UTF8, 0,
                                 writefileFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "lsmode", 1, SQLITE_UTF8, 0,
                                 lsModeFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = fsdirRegister(db);
  }
  return rc;
}

/************************* End ../ext/misc/fileio.c ********************/
/************************* Begin ../ext/misc/completion.c ******************/
/*
** 2017-07-10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements an eponymous virtual table that returns suggested
** completions for a partial SQL input.
**
** Suggested usage:
**
**     SELECT DISTINCT candidate COLLATE nocase
**       FROM completion($prefix,$wholeline)
**      ORDER BY 1;
**
** The two query parameters are optional.  $prefix is the text of the
** current word being typed and that is to be completed.  $wholeline is
** the complete input line, used for context.
**
** The raw completion() table might return the same candidate multiple
** times, for example if the same column name is used to two or more
** tables.  And the candidates are returned in an arbitrary order.  Hence,
** the DISTINCT and ORDER BY are recommended.
**
** This virtual table operates at the speed of human typing, and so there
** is no attempt to make it fast.  Even a slow implementation will be much
** faster than any human can type.
**
*/
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>
#include <ctype.h>

#ifndef SQLITE_OMIT_VIRTUALTABLE

/* completion_vtab is a subclass of sqlite3_vtab which will
** serve as the underlying representation of a completion virtual table
*/
typedef struct completion_vtab completion_vtab;
struct completion_vtab {
  sqlite3_vtab base;  /* Base class - must be first */
  sqlite3 *db;        /* Database connection for this completion vtab */
};

/* completion_cursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct completion_cursor completion_cursor;
struct completion_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  sqlite3 *db;               /* Database connection for this cursor */
  int nPrefix, nLine;        /* Number of bytes in zPrefix and zLine */
  char *zPrefix;             /* The prefix for the word we want to complete */
  char *zLine;               /* The whole that we want to complete */
  const char *zCurrentRow;   /* Current output row */
  int szRow;                 /* Length of the zCurrentRow string */
  sqlite3_stmt *pStmt;       /* Current statement */
  sqlite3_int64 iRowid;      /* The rowid */
  int ePhase;                /* Current phase */
  int j;                     /* inter-phase counter */
};

/* Values for ePhase:
*/
#define COMPLETION_FIRST_PHASE   1
#define COMPLETION_KEYWORDS      1
#define COMPLETION_PRAGMAS       2
#define COMPLETION_FUNCTIONS     3
#define COMPLETION_COLLATIONS    4
#define COMPLETION_INDEXES       5
#define COMPLETION_TRIGGERS      6
#define COMPLETION_DATABASES     7
#define COMPLETION_TABLES        8    /* Also VIEWs and TRIGGERs */
#define COMPLETION_COLUMNS       9
#define COMPLETION_MODULES       10
#define COMPLETION_EOF           11

/*
** The completionConnect() method is invoked to create a new
** completion_vtab that describes the completion virtual table.
**
** Think of this routine as the constructor for completion_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the completion_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against completion will look like.
*/
static int completionConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  completion_vtab *pNew;
  int rc;

  (void)(pAux);    /* Unused parameter */
  (void)(argc);    /* Unused parameter */
  (void)(argv);    /* Unused parameter */
  (void)(pzErr);   /* Unused parameter */

/* Column numbers */
#define COMPLETION_COLUMN_CANDIDATE 0  /* Suggested completion of the input */
#define COMPLETION_COLUMN_PREFIX    1  /* Prefix of the word to be completed */
#define COMPLETION_COLUMN_WHOLELINE 2  /* Entire line seen so far */
#define COMPLETION_COLUMN_PHASE     3  /* ePhase - used for debugging only */

  rc = sqlite3_declare_vtab(db,
      "CREATE TABLE x("
      "  candidate TEXT,"
      "  prefix TEXT HIDDEN,"
      "  wholeline TEXT HIDDEN,"
      "  phase INT HIDDEN"        /* Used for debugging only */
      ")");
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc( sizeof(*pNew) );
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    pNew->db = db;
  }
  return rc;
}

/*
** This method is the destructor for completion_cursor objects.
*/
static int completionDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/*
** Constructor for a new completion_cursor object.
*/
static int completionOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  completion_cursor *pCur;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  pCur->db = ((completion_vtab*)p)->db;
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Reset the completion_cursor.
*/
static void completionCursorReset(completion_cursor *pCur){
  sqlite3_free(pCur->zPrefix);   pCur->zPrefix = 0;  pCur->nPrefix = 0;
  sqlite3_free(pCur->zLine);     pCur->zLine = 0;    pCur->nLine = 0;
  sqlite3_finalize(pCur->pStmt); pCur->pStmt = 0;
  pCur->j = 0;
}

/*
** Destructor for a completion_cursor.
*/
static int completionClose(sqlite3_vtab_cursor *cur){
  completionCursorReset((completion_cursor*)cur);
  sqlite3_free(cur);
  return SQLITE_OK;
}

/*
** Advance a completion_cursor to its next row of output.
**
** The ->ePhase, ->j, and ->pStmt fields of the completion_cursor object
** record the current state of the scan.  This routine sets ->zCurrentRow
** to the current row of output and then returns.  If no more rows remain,
** then ->ePhase is set to COMPLETION_EOF which will signal the virtual
** table that has reached the end of its scan.
**
** The current implementation just lists potential identifiers and
** keywords and filters them by zPrefix.  Future enhancements should
** take zLine into account to try to restrict the set of identifiers and
** keywords based on what would be legal at the current point of input.
*/
static int completionNext(sqlite3_vtab_cursor *cur){
  completion_cursor *pCur = (completion_cursor*)cur;
  int eNextPhase = 0;  /* Next phase to try if current phase reaches end */
  int iCol = -1;       /* If >=0, step pCur->pStmt and use the i-th column */
  pCur->iRowid++;
  while( pCur->ePhase!=COMPLETION_EOF ){
    switch( pCur->ePhase ){
      case COMPLETION_KEYWORDS: {
        if( pCur->j >= sqlite3_keyword_count() ){
          pCur->zCurrentRow = 0;
          pCur->ePhase = COMPLETION_DATABASES;
        }else{
          sqlite3_keyword_name(pCur->j++, &pCur->zCurrentRow, &pCur->szRow);
        }
        iCol = -1;
        break;
      }
      case COMPLETION_DATABASES: {
        if( pCur->pStmt==0 ){
          sqlite3_prepare_v2(pCur->db, "PRAGMA database_list", -1,
                             &pCur->pStmt, 0);
        }
        iCol = 1;
        eNextPhase = COMPLETION_TABLES;
        break;
      }
      case COMPLETION_TABLES: {
        if( pCur->pStmt==0 ){
          sqlite3_stmt *pS2;
          char *zSql = 0;
          const char *zSep = "";
          sqlite3_prepare_v2(pCur->db, "PRAGMA database_list", -1, &pS2, 0);
          while( sqlite3_step(pS2)==SQLITE_ROW ){
            const char *zDb = (const char*)sqlite3_column_text(pS2, 1);
            zSql = sqlite3_mprintf(
               "%z%s"
               "SELECT name FROM \"%w\".sqlite_master",
               zSql, zSep, zDb
            );
            if( zSql==0 ) return SQLITE_NOMEM;
            zSep = " UNION ";
          }
          sqlite3_finalize(pS2);
          sqlite3_prepare_v2(pCur->db, zSql, -1, &pCur->pStmt, 0);
          sqlite3_free(zSql);
        }
        iCol = 0;
        eNextPhase = COMPLETION_COLUMNS;
        break;
      }
      case COMPLETION_COLUMNS: {
        if( pCur->pStmt==0 ){
          sqlite3_stmt *pS2;
          char *zSql = 0;
          const char *zSep = "";
          sqlite3_prepare_v2(pCur->db, "PRAGMA database_list", -1, &pS2, 0);
          while( sqlite3_step(pS2)==SQLITE_ROW ){
            const char *zDb = (const char*)sqlite3_column_text(pS2, 1);
            zSql = sqlite3_mprintf(
               "%z%s"
               "SELECT pti.name FROM \"%w\".sqlite_master AS sm"
                       " JOIN pragma_table_info(sm.name,%Q) AS pti"
               " WHERE sm.type='table'",
               zSql, zSep, zDb, zDb
            );
            if( zSql==0 ) return SQLITE_NOMEM;
            zSep = " UNION ";
          }
          sqlite3_finalize(pS2);
          sqlite3_prepare_v2(pCur->db, zSql, -1, &pCur->pStmt, 0);
          sqlite3_free(zSql);
        }
        iCol = 0;
        eNextPhase = COMPLETION_EOF;
        break;
      }
    }
    if( iCol<0 ){
      /* This case is when the phase presets zCurrentRow */
      if( pCur->zCurrentRow==0 ) continue;
    }else{
      if( sqlite3_step(pCur->pStmt)==SQLITE_ROW ){
        /* Extract the next row of content */
        pCur->zCurrentRow = (const char*)sqlite3_column_text(pCur->pStmt, iCol);
        pCur->szRow = sqlite3_column_bytes(pCur->pStmt, iCol);
      }else{
        /* When all rows are finished, advance to the next phase */
        sqlite3_finalize(pCur->pStmt);
        pCur->pStmt = 0;
        pCur->ePhase = eNextPhase;
        continue;
      }
    }
    if( pCur->nPrefix==0 ) break;
    if( pCur->nPrefix<=pCur->szRow
     && sqlite3_strnicmp(pCur->zPrefix, pCur->zCurrentRow, pCur->nPrefix)==0
    ){
      break;
    }
  }

  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the completion_cursor
** is currently pointing.
*/
static int completionColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  completion_cursor *pCur = (completion_cursor*)cur;
  switch( i ){
    case COMPLETION_COLUMN_CANDIDATE: {
      sqlite3_result_text(ctx, pCur->zCurrentRow, pCur->szRow,SQLITE_TRANSIENT);
      break;
    }
    case COMPLETION_COLUMN_PREFIX: {
      sqlite3_result_text(ctx, pCur->zPrefix, -1, SQLITE_TRANSIENT);
      break;
    }
    case COMPLETION_COLUMN_WHOLELINE: {
      sqlite3_result_text(ctx, pCur->zLine, -1, SQLITE_TRANSIENT);
      break;
    }
    case COMPLETION_COLUMN_PHASE: {
      sqlite3_result_int(ctx, pCur->ePhase);
      break;
    }
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int completionRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  completion_cursor *pCur = (completion_cursor*)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int completionEof(sqlite3_vtab_cursor *cur){
  completion_cursor *pCur = (completion_cursor*)cur;
  return pCur->ePhase >= COMPLETION_EOF;
}

/*
** This method is called to "rewind" the completion_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to completionColumn() or completionRowid() or 
** completionEof().
*/
static int completionFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  completion_cursor *pCur = (completion_cursor *)pVtabCursor;
  int iArg = 0;
  (void)(idxStr);   /* Unused parameter */
  (void)(argc);     /* Unused parameter */
  completionCursorReset(pCur);
  if( idxNum & 1 ){
    pCur->nPrefix = sqlite3_value_bytes(argv[iArg]);
    if( pCur->nPrefix>0 ){
      pCur->zPrefix = sqlite3_mprintf("%s", sqlite3_value_text(argv[iArg]));
      if( pCur->zPrefix==0 ) return SQLITE_NOMEM;
    }
    iArg = 1;
  }
  if( idxNum & 2 ){
    pCur->nLine = sqlite3_value_bytes(argv[iArg]);
    if( pCur->nLine>0 ){
      pCur->zLine = sqlite3_mprintf("%s", sqlite3_value_text(argv[iArg]));
      if( pCur->zLine==0 ) return SQLITE_NOMEM;
    }
  }
  if( pCur->zLine!=0 && pCur->zPrefix==0 ){
    int i = pCur->nLine;
    while( i>0 && (isalnum(pCur->zLine[i-1]) || pCur->zLine[i-1]=='_') ){
      i--;
    }
    pCur->nPrefix = pCur->nLine - i;
    if( pCur->nPrefix>0 ){
      pCur->zPrefix = sqlite3_mprintf("%.*s", pCur->nPrefix, pCur->zLine + i);
      if( pCur->zPrefix==0 ) return SQLITE_NOMEM;
    }
  }
  pCur->iRowid = 0;
  pCur->ePhase = COMPLETION_FIRST_PHASE;
  return completionNext(pVtabCursor);
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the completion virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
**
** There are two hidden parameters that act as arguments to the table-valued
** function:  "prefix" and "wholeline".  Bit 0 of idxNum is set if "prefix"
** is available and bit 1 is set if "wholeline" is available.
*/
static int completionBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  int i;                 /* Loop over constraints */
  int idxNum = 0;        /* The query plan bitmask */
  int prefixIdx = -1;    /* Index of the start= constraint, or -1 if none */
  int wholelineIdx = -1; /* Index of the stop= constraint, or -1 if none */
  int nArg = 0;          /* Number of arguments that completeFilter() expects */
  const struct sqlite3_index_constraint *pConstraint;

  (void)(tab);    /* Unused parameter */
  pConstraint = pIdxInfo->aConstraint;
  for(i=0; i<pIdxInfo->nConstraint; i++, pConstraint++){
    if( pConstraint->usable==0 ) continue;
    if( pConstraint->op!=SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    switch( pConstraint->iColumn ){
      case COMPLETION_COLUMN_PREFIX:
        prefixIdx = i;
        idxNum |= 1;
        break;
      case COMPLETION_COLUMN_WHOLELINE:
        wholelineIdx = i;
        idxNum |= 2;
        break;
    }
  }
  if( prefixIdx>=0 ){
    pIdxInfo->aConstraintUsage[prefixIdx].argvIndex = ++nArg;
    pIdxInfo->aConstraintUsage[prefixIdx].omit = 1;
  }
  if( wholelineIdx>=0 ){
    pIdxInfo->aConstraintUsage[wholelineIdx].argvIndex = ++nArg;
    pIdxInfo->aConstraintUsage[wholelineIdx].omit = 1;
  }
  pIdxInfo->idxNum = idxNum;
  pIdxInfo->estimatedCost = (double)5000 - 1000*nArg;
  pIdxInfo->estimatedRows = 500 - 100*nArg;
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the 
** completion virtual table.
*/
static sqlite3_module completionModule = {
  0,                         /* iVersion */
  0,                         /* xCreate */
  completionConnect,         /* xConnect */
  completionBestIndex,       /* xBestIndex */
  completionDisconnect,      /* xDisconnect */
  0,                         /* xDestroy */
  completionOpen,            /* xOpen - open a cursor */
  completionClose,           /* xClose - close a cursor */
  completionFilter,          /* xFilter - configure scan constraints */
  completionNext,            /* xNext - advance a cursor */
  completionEof,             /* xEof - check for end of scan */
  completionColumn,          /* xColumn - read data */
  completionRowid,           /* xRowid - read data */
  0,                         /* xUpdate */
  0,                         /* xBegin */
  0,                         /* xSync */
  0,                         /* xCommit */
  0,                         /* xRollback */
  0,                         /* xFindMethod */
  0,                         /* xRename */
  0,                         /* xSavepoint */
  0,                         /* xRelease */
  0                          /* xRollbackTo */
};

#endif /* SQLITE_OMIT_VIRTUALTABLE */

int sqlite3CompletionVtabInit(sqlite3 *db){
  int rc = SQLITE_OK;
#ifndef SQLITE_OMIT_VIRTUALTABLE
  rc = sqlite3_create_module(db, "completion", &completionModule, 0);
#endif
  return rc;
}

#ifdef _WIN32

#endif
int sqlite3_completion_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)(pzErrMsg);  /* Unused parameter */
#ifndef SQLITE_OMIT_VIRTUALTABLE
  rc = sqlite3CompletionVtabInit(db);
#endif
  return rc;
}

/************************* End ../ext/misc/completion.c ********************/
/************************* Begin ../ext/misc/appendvfs.c ******************/
/*
** 2017-10-20
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file implements a VFS shim that allows an SQLite database to be
** appended onto the end of some other file, such as an executable.
**
** A special record must appear at the end of the file that identifies the
** file as an appended database and provides an offset to page 1.  For
** best performance page 1 should be located at a disk page boundary, though
** that is not required.
**
** When opening a database using this VFS, the connection might treat
** the file as an ordinary SQLite database, or it might treat is as a
** database appended onto some other file.  Here are the rules:
**
**  (1)  When opening a new empty file, that file is treated as an ordinary
**       database.
**
**  (2)  When opening a file that begins with the standard SQLite prefix
**       string "SQLite format 3", that file is treated as an ordinary
**       database.
**
**  (3)  When opening a file that ends with the appendvfs trailer string
**       "Start-Of-SQLite3-NNNNNNNN" that file is treated as an appended
**       database.
**
**  (4)  If none of the above apply and the SQLITE_OPEN_CREATE flag is
**       set, then a new database is appended to the already existing file.
**
**  (5)  Otherwise, SQLITE_CANTOPEN is returned.
**
** To avoid unnecessary complications with the PENDING_BYTE, the size of
** the file containing the database is limited to 1GB.  This VFS will refuse
** to read or write past the 1GB mark.  This restriction might be lifted in
** future versions.  For now, if you need a large database, then keep the
** database in a separate file.
**
** If the file being opened is not an appended database, then this shim is
** a pass-through into the default underlying VFS.
**/
SQLITE_EXTENSION_INIT1
#include <string.h>
#include <assert.h>

/* The append mark at the end of the database is:
**
**     Start-Of-SQLite3-NNNNNNNN
**     123456789 123456789 12345
**
** The NNNNNNNN represents a 64-bit big-endian unsigned integer which is
** the offset to page 1.
*/
#define APND_MARK_PREFIX     "Start-Of-SQLite3-"
#define APND_MARK_PREFIX_SZ  17
#define APND_MARK_SIZE       25

/*
** Maximum size of the combined prefix + database + append-mark.  This
** must be less than 0x40000000 to avoid locking issues on Windows.
*/
#define APND_MAX_SIZE  (65536*15259)

/*
** Forward declaration of objects used by this utility
*/
typedef struct sqlite3_vfs ApndVfs;
typedef struct ApndFile ApndFile;

/* Access to a lower-level VFS that (might) implement dynamic loading,
** access to randomness, etc.
*/
#define ORIGVFS(p)  ((sqlite3_vfs*)((p)->pAppData))
#define ORIGFILE(p) ((sqlite3_file*)(((ApndFile*)(p))+1))

/* An open file */
struct ApndFile {
  sqlite3_file base;              /* IO methods */
  sqlite3_int64 iPgOne;           /* File offset to page 1 */
  sqlite3_int64 iMark;            /* Start of the append-mark */
};

/*
** Methods for ApndFile
*/
static int apndClose(sqlite3_file*);
static int apndRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int apndWrite(sqlite3_file*,const void*,int iAmt, sqlite3_int64 iOfst);
static int apndTruncate(sqlite3_file*, sqlite3_int64 size);
static int apndSync(sqlite3_file*, int flags);
static int apndFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int apndLock(sqlite3_file*, int);
static int apndUnlock(sqlite3_file*, int);
static int apndCheckReservedLock(sqlite3_file*, int *pResOut);
static int apndFileControl(sqlite3_file*, int op, void *pArg);
static int apndSectorSize(sqlite3_file*);
static int apndDeviceCharacteristics(sqlite3_file*);
static int apndShmMap(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
static int apndShmLock(sqlite3_file*, int offset, int n, int flags);
static void apndShmBarrier(sqlite3_file*);
static int apndShmUnmap(sqlite3_file*, int deleteFlag);
static int apndFetch(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
static int apndUnfetch(sqlite3_file*, sqlite3_int64 iOfst, void *p);

/*
** Methods for ApndVfs
*/
static int apndOpen(sqlite3_vfs*, const char *, sqlite3_file*, int , int *);
static int apndDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int apndAccess(sqlite3_vfs*, const char *zName, int flags, int *);
static int apndFullPathname(sqlite3_vfs*, const char *zName, int, char *zOut);
static void *apndDlOpen(sqlite3_vfs*, const char *zFilename);
static void apndDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void (*apndDlSym(sqlite3_vfs *pVfs, void *p, const char*zSym))(void);
static void apndDlClose(sqlite3_vfs*, void*);
static int apndRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int apndSleep(sqlite3_vfs*, int microseconds);
static int apndCurrentTime(sqlite3_vfs*, double*);
static int apndGetLastError(sqlite3_vfs*, int, char *);
static int apndCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int apndSetSystemCall(sqlite3_vfs*, const char*,sqlite3_syscall_ptr);
static sqlite3_syscall_ptr apndGetSystemCall(sqlite3_vfs*, const char *z);
static const char *apndNextSystemCall(sqlite3_vfs*, const char *zName);

static sqlite3_vfs apnd_vfs = {
  3,                            /* iVersion (set when registered) */
  0,                            /* szOsFile (set when registered) */
  1024,                         /* mxPathname */
  0,                            /* pNext */
  "apndvfs",                    /* zName */
  0,                            /* pAppData (set when registered) */ 
  apndOpen,                     /* xOpen */
  apndDelete,                   /* xDelete */
  apndAccess,                   /* xAccess */
  apndFullPathname,             /* xFullPathname */
  apndDlOpen,                   /* xDlOpen */
  apndDlError,                  /* xDlError */
  apndDlSym,                    /* xDlSym */
  apndDlClose,                  /* xDlClose */
  apndRandomness,               /* xRandomness */
  apndSleep,                    /* xSleep */
  apndCurrentTime,              /* xCurrentTime */
  apndGetLastError,             /* xGetLastError */
  apndCurrentTimeInt64,         /* xCurrentTimeInt64 */
  apndSetSystemCall,            /* xSetSystemCall */
  apndGetSystemCall,            /* xGetSystemCall */
  apndNextSystemCall            /* xNextSystemCall */
};

static const sqlite3_io_methods apnd_io_methods = {
  3,                              /* iVersion */
  apndClose,                      /* xClose */
  apndRead,                       /* xRead */
  apndWrite,                      /* xWrite */
  apndTruncate,                   /* xTruncate */
  apndSync,                       /* xSync */
  apndFileSize,                   /* xFileSize */
  apndLock,                       /* xLock */
  apndUnlock,                     /* xUnlock */
  apndCheckReservedLock,          /* xCheckReservedLock */
  apndFileControl,                /* xFileControl */
  apndSectorSize,                 /* xSectorSize */
  apndDeviceCharacteristics,      /* xDeviceCharacteristics */
  apndShmMap,                     /* xShmMap */
  apndShmLock,                    /* xShmLock */
  apndShmBarrier,                 /* xShmBarrier */
  apndShmUnmap,                   /* xShmUnmap */
  apndFetch,                      /* xFetch */
  apndUnfetch                     /* xUnfetch */
};



/*
** Close an apnd-file.
*/
static int apndClose(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xClose(pFile);
}

/*
** Read data from an apnd-file.
*/
static int apndRead(
  sqlite3_file *pFile, 
  void *zBuf, 
  int iAmt, 
  sqlite_int64 iOfst
){
  ApndFile *p = (ApndFile *)pFile;
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xRead(pFile, zBuf, iAmt, iOfst+p->iPgOne);
}

/*
** Add the append-mark onto the end of the file.
*/
static int apndWriteMark(ApndFile *p, sqlite3_file *pFile){
  int i;
  unsigned char a[APND_MARK_SIZE];
  memcpy(a, APND_MARK_PREFIX, APND_MARK_PREFIX_SZ);
  for(i=0; i<8; i++){
    a[APND_MARK_PREFIX_SZ+i] = (p->iPgOne >> (56 - i*8)) & 0xff;
  }
  return pFile->pMethods->xWrite(pFile, a, APND_MARK_SIZE, p->iMark);
}

/*
** Write data to an apnd-file.
*/
static int apndWrite(
  sqlite3_file *pFile,
  const void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
  int rc;
  ApndFile *p = (ApndFile *)pFile;
  pFile = ORIGFILE(pFile);
  if( iOfst+iAmt>=APND_MAX_SIZE ) return SQLITE_FULL;
  rc = pFile->pMethods->xWrite(pFile, zBuf, iAmt, iOfst+p->iPgOne);
  if( rc==SQLITE_OK &&  iOfst + iAmt + p->iPgOne > p->iMark ){
    sqlite3_int64 sz = 0;
    rc = pFile->pMethods->xFileSize(pFile, &sz);
    if( rc==SQLITE_OK ){
      p->iMark = sz - APND_MARK_SIZE;
      if( iOfst + iAmt + p->iPgOne > p->iMark ){
        p->iMark = p->iPgOne + iOfst + iAmt;
        rc = apndWriteMark(p, pFile);
      }
    }
  }
  return rc;
}

/*
** Truncate an apnd-file.
*/
static int apndTruncate(sqlite3_file *pFile, sqlite_int64 size){
  int rc;
  ApndFile *p = (ApndFile *)pFile;
  pFile = ORIGFILE(pFile);
  rc = pFile->pMethods->xTruncate(pFile, size+p->iPgOne+APND_MARK_SIZE);
  if( rc==SQLITE_OK ){
    p->iMark = p->iPgOne+size;
    rc = apndWriteMark(p, pFile);
  }
  return rc;
}

/*
** Sync an apnd-file.
*/
static int apndSync(sqlite3_file *pFile, int flags){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xSync(pFile, flags);
}

/*
** Return the current file-size of an apnd-file.
*/
static int apndFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
  ApndFile *p = (ApndFile *)pFile;
  int rc;
  pFile = ORIGFILE(p);
  rc = pFile->pMethods->xFileSize(pFile, pSize);
  if( rc==SQLITE_OK && p->iPgOne ){
    *pSize -= p->iPgOne + APND_MARK_SIZE;
  }
  return rc;
}

/*
** Lock an apnd-file.
*/
static int apndLock(sqlite3_file *pFile, int eLock){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xLock(pFile, eLock);
}

/*
** Unlock an apnd-file.
*/
static int apndUnlock(sqlite3_file *pFile, int eLock){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xUnlock(pFile, eLock);
}

/*
** Check if another file-handle holds a RESERVED lock on an apnd-file.
*/
static int apndCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xCheckReservedLock(pFile, pResOut);
}

/*
** File control method. For custom operations on an apnd-file.
*/
static int apndFileControl(sqlite3_file *pFile, int op, void *pArg){
  ApndFile *p = (ApndFile *)pFile;
  int rc;
  pFile = ORIGFILE(pFile);
  rc = pFile->pMethods->xFileControl(pFile, op, pArg);
  if( rc==SQLITE_OK && op==SQLITE_FCNTL_VFSNAME ){
    *(char**)pArg = sqlite3_mprintf("apnd(%lld)/%z", p->iPgOne, *(char**)pArg);
  }
  return rc;
}

/*
** Return the sector-size in bytes for an apnd-file.
*/
static int apndSectorSize(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xSectorSize(pFile);
}

/*
** Return the device characteristic flags supported by an apnd-file.
*/
static int apndDeviceCharacteristics(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xDeviceCharacteristics(pFile);
}

/* Create a shared memory file mapping */
static int apndShmMap(
  sqlite3_file *pFile,
  int iPg,
  int pgsz,
  int bExtend,
  void volatile **pp
){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmMap(pFile,iPg,pgsz,bExtend,pp);
}

/* Perform locking on a shared-memory segment */
static int apndShmLock(sqlite3_file *pFile, int offset, int n, int flags){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmLock(pFile,offset,n,flags);
}

/* Memory barrier operation on shared memory */
static void apndShmBarrier(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  pFile->pMethods->xShmBarrier(pFile);
}

/* Unmap a shared memory segment */
static int apndShmUnmap(sqlite3_file *pFile, int deleteFlag){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmUnmap(pFile,deleteFlag);
}

/* Fetch a page of a memory-mapped file */
static int apndFetch(
  sqlite3_file *pFile,
  sqlite3_int64 iOfst,
  int iAmt,
  void **pp
){
  ApndFile *p = (ApndFile *)pFile;
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xFetch(pFile, iOfst+p->iPgOne, iAmt, pp);
}

/* Release a memory-mapped page */
static int apndUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage){
  ApndFile *p = (ApndFile *)pFile;
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xUnfetch(pFile, iOfst+p->iPgOne, pPage);
}

/*
** Check to see if the file is an ordinary SQLite database file.
*/
static int apndIsOrdinaryDatabaseFile(sqlite3_int64 sz, sqlite3_file *pFile){
  int rc;
  char zHdr[16];
  static const char aSqliteHdr[] = "SQLite format 3";
  if( sz<512 ) return 0;
  rc = pFile->pMethods->xRead(pFile, zHdr, sizeof(zHdr), 0);
  if( rc ) return 0;
  return memcmp(zHdr, aSqliteHdr, sizeof(zHdr))==0;
}

/*
** Try to read the append-mark off the end of a file.  Return the
** start of the appended database if the append-mark is present.  If
** there is no append-mark, return -1;
*/
static sqlite3_int64 apndReadMark(sqlite3_int64 sz, sqlite3_file *pFile){
  int rc, i;
  sqlite3_int64 iMark;
  unsigned char a[APND_MARK_SIZE];

  if( sz<=APND_MARK_SIZE ) return -1;
  rc = pFile->pMethods->xRead(pFile, a, APND_MARK_SIZE, sz-APND_MARK_SIZE);
  if( rc ) return -1;
  if( memcmp(a, APND_MARK_PREFIX, APND_MARK_PREFIX_SZ)!=0 ) return -1;
  iMark = ((sqlite3_int64)(a[APND_MARK_PREFIX_SZ]&0x7f))<<56;
  for(i=1; i<8; i++){    
    iMark += (sqlite3_int64)a[APND_MARK_PREFIX_SZ+i]<<(56-8*i);
  }
  return iMark;
}

/*
** Open an apnd file handle.
*/
static int apndOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  ApndFile *p;
  sqlite3_file *pSubFile;
  sqlite3_vfs *pSubVfs;
  int rc;
  sqlite3_int64 sz;
  pSubVfs = ORIGVFS(pVfs);
  if( (flags & SQLITE_OPEN_MAIN_DB)==0 ){
    return pSubVfs->xOpen(pSubVfs, zName, pFile, flags, pOutFlags);
  }
  p = (ApndFile*)pFile;
  memset(p, 0, sizeof(*p));
  pSubFile = ORIGFILE(pFile);
  p->base.pMethods = &apnd_io_methods;
  rc = pSubVfs->xOpen(pSubVfs, zName, pSubFile, flags, pOutFlags);
  if( rc ) goto apnd_open_done;
  rc = pSubFile->pMethods->xFileSize(pSubFile, &sz);
  if( rc ){
    pSubFile->pMethods->xClose(pSubFile);
    goto apnd_open_done;
  }
  if( apndIsOrdinaryDatabaseFile(sz, pSubFile) ){
    memmove(pFile, pSubFile, pSubVfs->szOsFile);
    return SQLITE_OK;
  }
  p->iMark = 0;
  p->iPgOne = apndReadMark(sz, pFile);
  if( p->iPgOne>0 ){
    return SQLITE_OK;
  }
  if( (flags & SQLITE_OPEN_CREATE)==0 ){
    pSubFile->pMethods->xClose(pSubFile);
    rc = SQLITE_CANTOPEN;
  }
  p->iPgOne = (sz+0xfff) & ~(sqlite3_int64)0xfff;
apnd_open_done:
  if( rc ) pFile->pMethods = 0;
  return rc;
}

/*
** All other VFS methods are pass-thrus.
*/
static int apndDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
  return ORIGVFS(pVfs)->xDelete(ORIGVFS(pVfs), zPath, dirSync);
}
static int apndAccess(
  sqlite3_vfs *pVfs, 
  const char *zPath, 
  int flags, 
  int *pResOut
){
  return ORIGVFS(pVfs)->xAccess(ORIGVFS(pVfs), zPath, flags, pResOut);
}
static int apndFullPathname(
  sqlite3_vfs *pVfs, 
  const char *zPath, 
  int nOut, 
  char *zOut
){
  return ORIGVFS(pVfs)->xFullPathname(ORIGVFS(pVfs),zPath,nOut,zOut);
}
static void *apndDlOpen(sqlite3_vfs *pVfs, const char *zPath){
  return ORIGVFS(pVfs)->xDlOpen(ORIGVFS(pVfs), zPath);
}
static void apndDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
  ORIGVFS(pVfs)->xDlError(ORIGVFS(pVfs), nByte, zErrMsg);
}
static void (*apndDlSym(sqlite3_vfs *pVfs, void *p, const char *zSym))(void){
  return ORIGVFS(pVfs)->xDlSym(ORIGVFS(pVfs), p, zSym);
}
static void apndDlClose(sqlite3_vfs *pVfs, void *pHandle){
  ORIGVFS(pVfs)->xDlClose(ORIGVFS(pVfs), pHandle);
}
static int apndRandomness(sqlite3_vfs *pVfs, int nByte, char *zBufOut){
  return ORIGVFS(pVfs)->xRandomness(ORIGVFS(pVfs), nByte, zBufOut);
}
static int apndSleep(sqlite3_vfs *pVfs, int nMicro){
  return ORIGVFS(pVfs)->xSleep(ORIGVFS(pVfs), nMicro);
}
static int apndCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut){
  return ORIGVFS(pVfs)->xCurrentTime(ORIGVFS(pVfs), pTimeOut);
}
static int apndGetLastError(sqlite3_vfs *pVfs, int a, char *b){
  return ORIGVFS(pVfs)->xGetLastError(ORIGVFS(pVfs), a, b);
}
static int apndCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *p){
  return ORIGVFS(pVfs)->xCurrentTimeInt64(ORIGVFS(pVfs), p);
}
static int apndSetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_syscall_ptr pCall
){
  return ORIGVFS(pVfs)->xSetSystemCall(ORIGVFS(pVfs),zName,pCall);
}
static sqlite3_syscall_ptr apndGetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName
){
  return ORIGVFS(pVfs)->xGetSystemCall(ORIGVFS(pVfs),zName);
}
static const char *apndNextSystemCall(sqlite3_vfs *pVfs, const char *zName){
  return ORIGVFS(pVfs)->xNextSystemCall(ORIGVFS(pVfs), zName);
}

  
#ifdef _WIN32

#endif
/* 
** This routine is called when the extension is loaded.
** Register the new VFS.
*/
int sqlite3_appendvfs_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  sqlite3_vfs *pOrig;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;
  (void)db;
  pOrig = sqlite3_vfs_find(0);
  apnd_vfs.iVersion = pOrig->iVersion;
  apnd_vfs.pAppData = pOrig;
  apnd_vfs.szOsFile = pOrig->szOsFile + sizeof(ApndFile);
  rc = sqlite3_vfs_register(&apnd_vfs, 0);
#ifdef APPENDVFS_TEST
  if( rc==SQLITE_OK ){
    rc = sqlite3_auto_extension((void(*)(void))apndvfsRegister);
  }
#endif
  if( rc==SQLITE_OK ) rc = SQLITE_OK_LOAD_PERMANENTLY;
  return rc;
}

/************************* End ../ext/misc/appendvfs.c ********************/
#ifdef SQLITE_HAVE_ZLIB
/************************* Begin ../ext/misc/zipfile.c ******************/
/*
** 2017-12-26
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file implements a virtual table for reading and writing ZIP archive
** files.
**
** Usage example:
**
**     SELECT name, sz, datetime(mtime,'unixepoch') FROM zipfile($filename);
**
** Current limitations:
**
**    *  No support for encryption
**    *  No support for ZIP archives spanning multiple files
**    *  No support for zip64 extensions
**    *  Only the "inflate/deflate" (zlib) compression method is supported
*/
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <zlib.h>

#ifndef SQLITE_OMIT_VIRTUALTABLE

#ifndef SQLITE_AMALGAMATION

/* typedef sqlite3_int64 i64; */
/* typedef unsigned char u8; */
typedef unsigned short u16;
typedef unsigned long u32;
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#if defined(SQLITE_COVERAGE_TEST) || defined(SQLITE_MUTATION_TEST)
# define ALWAYS(X)      (1)
# define NEVER(X)       (0)
#elif !defined(NDEBUG)
# define ALWAYS(X)      ((X)?1:(assert(0),0))
# define NEVER(X)       ((X)?(assert(0),1):0)
#else
# define ALWAYS(X)      (X)
# define NEVER(X)       (X)
#endif

#endif   /* SQLITE_AMALGAMATION */

/*
** Definitions for mode bitmasks S_IFDIR, S_IFREG and S_IFLNK.
**
** In some ways it would be better to obtain these values from system 
** header files. But, the dependency is undesirable and (a) these
** have been stable for decades, (b) the values are part of POSIX and
** are also made explicit in [man stat], and (c) are part of the 
** file format for zip archives.
*/
#ifndef S_IFDIR
# define S_IFDIR 0040000
#endif
#ifndef S_IFREG
# define S_IFREG 0100000
#endif
#ifndef S_IFLNK
# define S_IFLNK 0120000
#endif

static const char ZIPFILE_SCHEMA[] = 
  "CREATE TABLE y("
    "name PRIMARY KEY,"  /* 0: Name of file in zip archive */
    "mode,"              /* 1: POSIX mode for file */
    "mtime,"             /* 2: Last modification time (secs since 1970)*/
    "sz,"                /* 3: Size of object */
    "rawdata,"           /* 4: Raw data */
    "data,"              /* 5: Uncompressed data */
    "method,"            /* 6: Compression method (integer) */
    "z HIDDEN"           /* 7: Name of zip file */
  ") WITHOUT ROWID;";

#define ZIPFILE_F_COLUMN_IDX 7    /* Index of column "file" in the above */
#define ZIPFILE_BUFFER_SIZE (64*1024)


/*
** Magic numbers used to read and write zip files.
**
** ZIPFILE_NEWENTRY_MADEBY:
**   Use this value for the "version-made-by" field in new zip file
**   entries. The upper byte indicates "unix", and the lower byte 
**   indicates that the zip file matches pkzip specification 3.0. 
**   This is what info-zip seems to do.
**
** ZIPFILE_NEWENTRY_REQUIRED:
**   Value for "version-required-to-extract" field of new entries.
**   Version 2.0 is required to support folders and deflate compression.
**
** ZIPFILE_NEWENTRY_FLAGS:
**   Value for "general-purpose-bit-flags" field of new entries. Bit
**   11 means "utf-8 filename and comment".
**
** ZIPFILE_SIGNATURE_CDS:
**   First 4 bytes of a valid CDS record.
**
** ZIPFILE_SIGNATURE_LFH:
**   First 4 bytes of a valid LFH record.
**
** ZIPFILE_SIGNATURE_EOCD
**   First 4 bytes of a valid EOCD record.
*/
#define ZIPFILE_EXTRA_TIMESTAMP   0x5455
#define ZIPFILE_NEWENTRY_MADEBY   ((3<<8) + 30)
#define ZIPFILE_NEWENTRY_REQUIRED 20
#define ZIPFILE_NEWENTRY_FLAGS    0x800
#define ZIPFILE_SIGNATURE_CDS     0x02014b50
#define ZIPFILE_SIGNATURE_LFH     0x04034b50
#define ZIPFILE_SIGNATURE_EOCD    0x06054b50

/*
** The sizes of the fixed-size part of each of the three main data 
** structures in a zip archive.
*/
#define ZIPFILE_LFH_FIXED_SZ      30
#define ZIPFILE_EOCD_FIXED_SZ     22
#define ZIPFILE_CDS_FIXED_SZ      46

/*
*** 4.3.16  End of central directory record:
***
***   end of central dir signature    4 bytes  (0x06054b50)
***   number of this disk             2 bytes
***   number of the disk with the
***   start of the central directory  2 bytes
***   total number of entries in the
***   central directory on this disk  2 bytes
***   total number of entries in
***   the central directory           2 bytes
***   size of the central directory   4 bytes
***   offset of start of central
***   directory with respect to
***   the starting disk number        4 bytes
***   .ZIP file comment length        2 bytes
***   .ZIP file comment       (variable size)
*/
typedef struct ZipfileEOCD ZipfileEOCD;
struct ZipfileEOCD {
  u16 iDisk;
  u16 iFirstDisk;
  u16 nEntry;
  u16 nEntryTotal;
  u32 nSize;
  u32 iOffset;
};

/*
*** 4.3.12  Central directory structure:
***
*** ...
***
***   central file header signature   4 bytes  (0x02014b50)
***   version made by                 2 bytes
***   version needed to extract       2 bytes
***   general purpose bit flag        2 bytes
***   compression method              2 bytes
***   last mod file time              2 bytes
***   last mod file date              2 bytes
***   crc-32                          4 bytes
***   compressed size                 4 bytes
***   uncompressed size               4 bytes
***   file name length                2 bytes
***   extra field length              2 bytes
***   file comment length             2 bytes
***   disk number start               2 bytes
***   internal file attributes        2 bytes
***   external file attributes        4 bytes
***   relative offset of local header 4 bytes
*/
typedef struct ZipfileCDS ZipfileCDS;
struct ZipfileCDS {
  u16 iVersionMadeBy;
  u16 iVersionExtract;
  u16 flags;
  u16 iCompression;
  u16 mTime;
  u16 mDate;
  u32 crc32;
  u32 szCompressed;
  u32 szUncompressed;
  u16 nFile;
  u16 nExtra;
  u16 nComment;
  u16 iDiskStart;
  u16 iInternalAttr;
  u32 iExternalAttr;
  u32 iOffset;
  char *zFile;                    /* Filename (sqlite3_malloc()) */
};

/*
*** 4.3.7  Local file header:
***
***   local file header signature     4 bytes  (0x04034b50)
***   version needed to extract       2 bytes
***   general purpose bit flag        2 bytes
***   compression method              2 bytes
***   last mod file time              2 bytes
***   last mod file date              2 bytes
***   crc-32                          4 bytes
***   compressed size                 4 bytes
***   uncompressed size               4 bytes
***   file name length                2 bytes
***   extra field length              2 bytes
***   
*/
typedef struct ZipfileLFH ZipfileLFH;
struct ZipfileLFH {
  u16 iVersionExtract;
  u16 flags;
  u16 iCompression;
  u16 mTime;
  u16 mDate;
  u32 crc32;
  u32 szCompressed;
  u32 szUncompressed;
  u16 nFile;
  u16 nExtra;
};

typedef struct ZipfileEntry ZipfileEntry;
struct ZipfileEntry {
  ZipfileCDS cds;            /* Parsed CDS record */
  u32 mUnixTime;             /* Modification time, in UNIX format */
  u8 *aExtra;                /* cds.nExtra+cds.nComment bytes of extra data */
  i64 iDataOff;              /* Offset to data in file (if aData==0) */
  u8 *aData;                 /* cds.szCompressed bytes of compressed data */
  ZipfileEntry *pNext;       /* Next element in in-memory CDS */
};

/* 
** Cursor type for zipfile tables.
*/
typedef struct ZipfileCsr ZipfileCsr;
struct ZipfileCsr {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  i64 iId;                   /* Cursor ID */
  u8 bEof;                   /* True when at EOF */
  u8 bNoop;                  /* If next xNext() call is no-op */

  /* Used outside of write transactions */
  FILE *pFile;               /* Zip file */
  i64 iNextOff;              /* Offset of next record in central directory */
  ZipfileEOCD eocd;          /* Parse of central directory record */

  ZipfileEntry *pFreeEntry;  /* Free this list when cursor is closed or reset */
  ZipfileEntry *pCurrent;    /* Current entry */
  ZipfileCsr *pCsrNext;      /* Next cursor on same virtual table */
};

typedef struct ZipfileTab ZipfileTab;
struct ZipfileTab {
  sqlite3_vtab base;         /* Base class - must be first */
  char *zFile;               /* Zip file this table accesses (may be NULL) */
  sqlite3 *db;               /* Host database connection */
  u8 *aBuffer;               /* Temporary buffer used for various tasks */

  ZipfileCsr *pCsrList;      /* List of cursors */
  i64 iNextCsrid;

  /* The following are used by write transactions only */
  ZipfileEntry *pFirstEntry; /* Linked list of all files (if pWriteFd!=0) */
  ZipfileEntry *pLastEntry;  /* Last element in pFirstEntry list */
  FILE *pWriteFd;            /* File handle open on zip archive */
  i64 szCurrent;             /* Current size of zip archive */
  i64 szOrig;                /* Size of archive at start of transaction */
};

/*
** Set the error message contained in context ctx to the results of
** vprintf(zFmt, ...).
*/
static void zipfileCtxErrorMsg(sqlite3_context *ctx, const char *zFmt, ...){
  char *zMsg = 0;
  va_list ap;
  va_start(ap, zFmt);
  zMsg = sqlite3_vmprintf(zFmt, ap);
  sqlite3_result_error(ctx, zMsg, -1);
  sqlite3_free(zMsg);
  va_end(ap);
}

/*
** If string zIn is quoted, dequote it in place. Otherwise, if the string
** is not quoted, do nothing.
*/
static void zipfileDequote(char *zIn){
  char q = zIn[0];
  if( q=='"' || q=='\'' || q=='`' || q=='[' ){
    int iIn = 1;
    int iOut = 0;
    if( q=='[' ) q = ']';
    while( ALWAYS(zIn[iIn]) ){
      char c = zIn[iIn++];
      if( c==q && zIn[iIn++]!=q ) break;
      zIn[iOut++] = c;
    }
    zIn[iOut] = '\0';
  }
}

/*
** Construct a new ZipfileTab virtual table object.
** 
**   argv[0]   -> module name  ("zipfile")
**   argv[1]   -> database name
**   argv[2]   -> table name
**   argv[...] -> "column name" and other module argument fields.
*/
static int zipfileConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  int nByte = sizeof(ZipfileTab) + ZIPFILE_BUFFER_SIZE;
  int nFile = 0;
  const char *zFile = 0;
  ZipfileTab *pNew = 0;
  int rc;

  /* If the table name is not "zipfile", require that the argument be
  ** specified. This stops zipfile tables from being created as:
  **
  **   CREATE VIRTUAL TABLE zzz USING zipfile();
  **
  ** It does not prevent:
  **
  **   CREATE VIRTUAL TABLE zipfile USING zipfile();
  */
  assert( 0==sqlite3_stricmp(argv[0], "zipfile") );
  if( (0!=sqlite3_stricmp(argv[2], "zipfile") && argc<4) || argc>4 ){
    *pzErr = sqlite3_mprintf("zipfile constructor requires one argument");
    return SQLITE_ERROR;
  }

  if( argc>3 ){
    zFile = argv[3];
    nFile = (int)strlen(zFile)+1;
  }

  rc = sqlite3_declare_vtab(db, ZIPFILE_SCHEMA);
  if( rc==SQLITE_OK ){
    pNew = (ZipfileTab*)sqlite3_malloc(nByte+nFile);
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, nByte+nFile);
    pNew->db = db;
    pNew->aBuffer = (u8*)&pNew[1];
    if( zFile ){
      pNew->zFile = (char*)&pNew->aBuffer[ZIPFILE_BUFFER_SIZE];
      memcpy(pNew->zFile, zFile, nFile);
      zipfileDequote(pNew->zFile);
    }
  }
  *ppVtab = (sqlite3_vtab*)pNew;
  return rc;
}

/*
** Free the ZipfileEntry structure indicated by the only argument.
*/
static void zipfileEntryFree(ZipfileEntry *p){
  if( p ){
    sqlite3_free(p->cds.zFile);
    sqlite3_free(p);
  }
}

/*
** Release resources that should be freed at the end of a write 
** transaction.
*/
static void zipfileCleanupTransaction(ZipfileTab *pTab){
  ZipfileEntry *pEntry;
  ZipfileEntry *pNext;

  if( pTab->pWriteFd ){
    fclose(pTab->pWriteFd);
    pTab->pWriteFd = 0;
  }
  for(pEntry=pTab->pFirstEntry; pEntry; pEntry=pNext){
    pNext = pEntry->pNext;
    zipfileEntryFree(pEntry);
  }
  pTab->pFirstEntry = 0;
  pTab->pLastEntry = 0;
  pTab->szCurrent = 0;
  pTab->szOrig = 0;
}

/*
** This method is the destructor for zipfile vtab objects.
*/
static int zipfileDisconnect(sqlite3_vtab *pVtab){
  zipfileCleanupTransaction((ZipfileTab*)pVtab);
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/*
** Constructor for a new ZipfileCsr object.
*/
static int zipfileOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCsr){
  ZipfileTab *pTab = (ZipfileTab*)p;
  ZipfileCsr *pCsr;
  pCsr = sqlite3_malloc(sizeof(*pCsr));
  *ppCsr = (sqlite3_vtab_cursor*)pCsr;
  if( pCsr==0 ){
    return SQLITE_NOMEM;
  }
  memset(pCsr, 0, sizeof(*pCsr));
  pCsr->iId = ++pTab->iNextCsrid;
  pCsr->pCsrNext = pTab->pCsrList;
  pTab->pCsrList = pCsr;
  return SQLITE_OK;
}

/*
** Reset a cursor back to the state it was in when first returned
** by zipfileOpen().
*/
static void zipfileResetCursor(ZipfileCsr *pCsr){
  ZipfileEntry *p;
  ZipfileEntry *pNext;

  pCsr->bEof = 0;
  if( pCsr->pFile ){
    fclose(pCsr->pFile);
    pCsr->pFile = 0;
    zipfileEntryFree(pCsr->pCurrent);
    pCsr->pCurrent = 0;
  }

  for(p=pCsr->pFreeEntry; p; p=pNext){
    pNext = p->pNext;
    zipfileEntryFree(p);
  }
}

/*
** Destructor for an ZipfileCsr.
*/
static int zipfileClose(sqlite3_vtab_cursor *cur){
  ZipfileCsr *pCsr = (ZipfileCsr*)cur;
  ZipfileTab *pTab = (ZipfileTab*)(pCsr->base.pVtab);
  ZipfileCsr **pp;
  zipfileResetCursor(pCsr);

  /* Remove this cursor from the ZipfileTab.pCsrList list. */
  for(pp=&pTab->pCsrList; *pp!=pCsr; pp=&((*pp)->pCsrNext));
  *pp = pCsr->pCsrNext;

  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/*
** Set the error message for the virtual table associated with cursor
** pCsr to the results of vprintf(zFmt, ...).
*/
static void zipfileTableErr(ZipfileTab *pTab, const char *zFmt, ...){
  va_list ap;
  va_start(ap, zFmt);
  sqlite3_free(pTab->base.zErrMsg);
  pTab->base.zErrMsg = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
}
static void zipfileCursorErr(ZipfileCsr *pCsr, const char *zFmt, ...){
  va_list ap;
  va_start(ap, zFmt);
  sqlite3_free(pCsr->base.pVtab->zErrMsg);
  pCsr->base.pVtab->zErrMsg = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
}

/*
** Read nRead bytes of data from offset iOff of file pFile into buffer
** aRead[]. Return SQLITE_OK if successful, or an SQLite error code
** otherwise. 
**
** If an error does occur, output variable (*pzErrmsg) may be set to point
** to an English language error message. It is the responsibility of the
** caller to eventually free this buffer using
** sqlite3_free().
*/
static int zipfileReadData(
  FILE *pFile,                    /* Read from this file */
  u8 *aRead,                      /* Read into this buffer */
  int nRead,                      /* Number of bytes to read */
  i64 iOff,                       /* Offset to read from */
  char **pzErrmsg                 /* OUT: Error message (from sqlite3_malloc) */
){
  size_t n;
  fseek(pFile, (long)iOff, SEEK_SET);
  n = fread(aRead, 1, nRead, pFile);
  if( (int)n!=nRead ){
    *pzErrmsg = sqlite3_mprintf("error in fread()");
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

static int zipfileAppendData(
  ZipfileTab *pTab,
  const u8 *aWrite,
  int nWrite
){
  size_t n;
  fseek(pTab->pWriteFd, (long)pTab->szCurrent, SEEK_SET);
  n = fwrite(aWrite, 1, nWrite, pTab->pWriteFd);
  if( (int)n!=nWrite ){
    pTab->base.zErrMsg = sqlite3_mprintf("error in fwrite()");
    return SQLITE_ERROR;
  }
  pTab->szCurrent += nWrite;
  return SQLITE_OK;
}

/*
** Read and return a 16-bit little-endian unsigned integer from buffer aBuf.
*/
static u16 zipfileGetU16(const u8 *aBuf){
  return (aBuf[1] << 8) + aBuf[0];
}

/*
** Read and return a 32-bit little-endian unsigned integer from buffer aBuf.
*/
static u32 zipfileGetU32(const u8 *aBuf){
  return ((u32)(aBuf[3]) << 24)
       + ((u32)(aBuf[2]) << 16)
       + ((u32)(aBuf[1]) <<  8)
       + ((u32)(aBuf[0]) <<  0);
}

/*
** Write a 16-bit little endiate integer into buffer aBuf.
*/
static void zipfilePutU16(u8 *aBuf, u16 val){
  aBuf[0] = val & 0xFF;
  aBuf[1] = (val>>8) & 0xFF;
}

/*
** Write a 32-bit little endiate integer into buffer aBuf.
*/
static void zipfilePutU32(u8 *aBuf, u32 val){
  aBuf[0] = val & 0xFF;
  aBuf[1] = (val>>8) & 0xFF;
  aBuf[2] = (val>>16) & 0xFF;
  aBuf[3] = (val>>24) & 0xFF;
}

#define zipfileRead32(aBuf) ( aBuf+=4, zipfileGetU32(aBuf-4) )
#define zipfileRead16(aBuf) ( aBuf+=2, zipfileGetU16(aBuf-2) )

#define zipfileWrite32(aBuf,val) { zipfilePutU32(aBuf,val); aBuf+=4; }
#define zipfileWrite16(aBuf,val) { zipfilePutU16(aBuf,val); aBuf+=2; }

/*
** Magic numbers used to read CDS records.
*/
#define ZIPFILE_CDS_NFILE_OFF        28
#define ZIPFILE_CDS_SZCOMPRESSED_OFF 20

/*
** Decode the CDS record in buffer aBuf into (*pCDS). Return SQLITE_ERROR
** if the record is not well-formed, or SQLITE_OK otherwise.
*/
static int zipfileReadCDS(u8 *aBuf, ZipfileCDS *pCDS){
  u8 *aRead = aBuf;
  u32 sig = zipfileRead32(aRead);
  int rc = SQLITE_OK;
  if( sig!=ZIPFILE_SIGNATURE_CDS ){
    rc = SQLITE_ERROR;
  }else{
    pCDS->iVersionMadeBy = zipfileRead16(aRead);
    pCDS->iVersionExtract = zipfileRead16(aRead);
    pCDS->flags = zipfileRead16(aRead);
    pCDS->iCompression = zipfileRead16(aRead);
    pCDS->mTime = zipfileRead16(aRead);
    pCDS->mDate = zipfileRead16(aRead);
    pCDS->crc32 = zipfileRead32(aRead);
    pCDS->szCompressed = zipfileRead32(aRead);
    pCDS->szUncompressed = zipfileRead32(aRead);
    assert( aRead==&aBuf[ZIPFILE_CDS_NFILE_OFF] );
    pCDS->nFile = zipfileRead16(aRead);
    pCDS->nExtra = zipfileRead16(aRead);
    pCDS->nComment = zipfileRead16(aRead);
    pCDS->iDiskStart = zipfileRead16(aRead);
    pCDS->iInternalAttr = zipfileRead16(aRead);
    pCDS->iExternalAttr = zipfileRead32(aRead);
    pCDS->iOffset = zipfileRead32(aRead);
    assert( aRead==&aBuf[ZIPFILE_CDS_FIXED_SZ] );
  }

  return rc;
}

/*
** Decode the LFH record in buffer aBuf into (*pLFH). Return SQLITE_ERROR
** if the record is not well-formed, or SQLITE_OK otherwise.
*/
static int zipfileReadLFH(
  u8 *aBuffer,
  ZipfileLFH *pLFH
){
  u8 *aRead = aBuffer;
  int rc = SQLITE_OK;

  u32 sig = zipfileRead32(aRead);
  if( sig!=ZIPFILE_SIGNATURE_LFH ){
    rc = SQLITE_ERROR;
  }else{
    pLFH->iVersionExtract = zipfileRead16(aRead);
    pLFH->flags = zipfileRead16(aRead);
    pLFH->iCompression = zipfileRead16(aRead);
    pLFH->mTime = zipfileRead16(aRead);
    pLFH->mDate = zipfileRead16(aRead);
    pLFH->crc32 = zipfileRead32(aRead);
    pLFH->szCompressed = zipfileRead32(aRead);
    pLFH->szUncompressed = zipfileRead32(aRead);
    pLFH->nFile = zipfileRead16(aRead);
    pLFH->nExtra = zipfileRead16(aRead);
  }
  return rc;
}


/*
** Buffer aExtra (size nExtra bytes) contains zip archive "extra" fields.
** Scan through this buffer to find an "extra-timestamp" field. If one
** exists, extract the 32-bit modification-timestamp from it and store
** the value in output parameter *pmTime.
**
** Zero is returned if no extra-timestamp record could be found (and so
** *pmTime is left unchanged), or non-zero otherwise.
**
** The general format of an extra field is:
**
**   Header ID    2 bytes
**   Data Size    2 bytes
**   Data         N bytes
*/
static int zipfileScanExtra(u8 *aExtra, int nExtra, u32 *pmTime){
  int ret = 0;
  u8 *p = aExtra;
  u8 *pEnd = &aExtra[nExtra];

  while( p<pEnd ){
    u16 id = zipfileRead16(p);
    u16 nByte = zipfileRead16(p);

    switch( id ){
      case ZIPFILE_EXTRA_TIMESTAMP: {
        u8 b = p[0];
        if( b & 0x01 ){     /* 0x01 -> modtime is present */
          *pmTime = zipfileGetU32(&p[1]);
          ret = 1;
        }
        break;
      }
    }

    p += nByte;
  }
  return ret;
}

/*
** Convert the standard MS-DOS timestamp stored in the mTime and mDate
** fields of the CDS structure passed as the only argument to a 32-bit
** UNIX seconds-since-the-epoch timestamp. Return the result.
**
** "Standard" MS-DOS time format:
**
**   File modification time:
**     Bits 00-04: seconds divided by 2
**     Bits 05-10: minute
**     Bits 11-15: hour
**   File modification date:
**     Bits 00-04: day
**     Bits 05-08: month (1-12)
**     Bits 09-15: years from 1980 
**
** https://msdn.microsoft.com/en-us/library/9kkf9tah.aspx
*/
static u32 zipfileMtime(ZipfileCDS *pCDS){
  int Y = (1980 + ((pCDS->mDate >> 9) & 0x7F));
  int M = ((pCDS->mDate >> 5) & 0x0F);
  int D = (pCDS->mDate & 0x1F);
  int B = -13;

  int sec = (pCDS->mTime & 0x1F)*2;
  int min = (pCDS->mTime >> 5) & 0x3F;
  int hr = (pCDS->mTime >> 11) & 0x1F;
  i64 JD;

  /* JD = INT(365.25 * (Y+4716)) + INT(30.6001 * (M+1)) + D + B - 1524.5 */

  /* Calculate the JD in seconds for noon on the day in question */
  if( M<3 ){
    Y = Y-1;
    M = M+12;
  }
  JD = (i64)(24*60*60) * (
      (int)(365.25 * (Y + 4716))
    + (int)(30.6001 * (M + 1))
    + D + B - 1524
  );

  /* Correct the JD for the time within the day */
  JD += (hr-12) * 3600 + min * 60 + sec;

  /* Convert JD to unix timestamp (the JD epoch is 2440587.5) */
  return (u32)(JD - (i64)(24405875) * 24*60*6);
}

/*
** The opposite of zipfileMtime(). This function populates the mTime and
** mDate fields of the CDS structure passed as the first argument according
** to the UNIX timestamp value passed as the second.
*/
static void zipfileMtimeToDos(ZipfileCDS *pCds, u32 mUnixTime){
  /* Convert unix timestamp to JD (2440588 is noon on 1/1/1970) */
  i64 JD = (i64)2440588 + mUnixTime / (24*60*60);

  int A, B, C, D, E;
  int yr, mon, day;
  int hr, min, sec;

  A = (int)((JD - 1867216.25)/36524.25);
  A = (int)(JD + 1 + A - (A/4));
  B = A + 1524;
  C = (int)((B - 122.1)/365.25);
  D = (36525*(C&32767))/100;
  E = (int)((B-D)/30.6001);

  day = B - D - (int)(30.6001*E);
  mon = (E<14 ? E-1 : E-13);
  yr = mon>2 ? C-4716 : C-4715;

  hr = (mUnixTime % (24*60*60)) / (60*60);
  min = (mUnixTime % (60*60)) / 60;
  sec = (mUnixTime % 60);

  if( yr>=1980 ){
    pCds->mDate = (u16)(day + (mon << 5) + ((yr-1980) << 9));
    pCds->mTime = (u16)(sec/2 + (min<<5) + (hr<<11));
  }else{
    pCds->mDate = pCds->mTime = 0;
  }

  assert( mUnixTime<315507600 
       || mUnixTime==zipfileMtime(pCds) 
       || ((mUnixTime % 2) && mUnixTime-1==zipfileMtime(pCds)) 
       /* || (mUnixTime % 2) */
  );
}

/*
** If aBlob is not NULL, then it is a pointer to a buffer (nBlob bytes in
** size) containing an entire zip archive image. Or, if aBlob is NULL,
** then pFile is a file-handle open on a zip file. In either case, this
** function creates a ZipfileEntry object based on the zip archive entry
** for which the CDS record is at offset iOff.
**
** If successful, SQLITE_OK is returned and (*ppEntry) set to point to
** the new object. Otherwise, an SQLite error code is returned and the
** final value of (*ppEntry) undefined.
*/
static int zipfileGetEntry(
  ZipfileTab *pTab,               /* Store any error message here */
  const u8 *aBlob,                /* Pointer to in-memory file image */
  int nBlob,                      /* Size of aBlob[] in bytes */
  FILE *pFile,                    /* If aBlob==0, read from this file */
  i64 iOff,                       /* Offset of CDS record */
  ZipfileEntry **ppEntry          /* OUT: Pointer to new object */
){
  u8 *aRead;
  char **pzErr = &pTab->base.zErrMsg;
  int rc = SQLITE_OK;

  if( aBlob==0 ){
    aRead = pTab->aBuffer;
    rc = zipfileReadData(pFile, aRead, ZIPFILE_CDS_FIXED_SZ, iOff, pzErr);
  }else{
    aRead = (u8*)&aBlob[iOff];
  }

  if( rc==SQLITE_OK ){
    int nAlloc;
    ZipfileEntry *pNew;

    int nFile = zipfileGetU16(&aRead[ZIPFILE_CDS_NFILE_OFF]);
    int nExtra = zipfileGetU16(&aRead[ZIPFILE_CDS_NFILE_OFF+2]);
    nExtra += zipfileGetU16(&aRead[ZIPFILE_CDS_NFILE_OFF+4]);

    nAlloc = sizeof(ZipfileEntry) + nExtra;
    if( aBlob ){
      nAlloc += zipfileGetU32(&aRead[ZIPFILE_CDS_SZCOMPRESSED_OFF]);
    }

    pNew = (ZipfileEntry*)sqlite3_malloc(nAlloc);
    if( pNew==0 ){
      rc = SQLITE_NOMEM;
    }else{
      memset(pNew, 0, sizeof(ZipfileEntry));
      rc = zipfileReadCDS(aRead, &pNew->cds);
      if( rc!=SQLITE_OK ){
        *pzErr = sqlite3_mprintf("failed to read CDS at offset %lld", iOff);
      }else if( aBlob==0 ){
        rc = zipfileReadData(
            pFile, aRead, nExtra+nFile, iOff+ZIPFILE_CDS_FIXED_SZ, pzErr
        );
      }else{
        aRead = (u8*)&aBlob[iOff + ZIPFILE_CDS_FIXED_SZ];
      }
    }

    if( rc==SQLITE_OK ){
      u32 *pt = &pNew->mUnixTime;
      pNew->cds.zFile = sqlite3_mprintf("%.*s", nFile, aRead); 
      pNew->aExtra = (u8*)&pNew[1];
      memcpy(pNew->aExtra, &aRead[nFile], nExtra);
      if( pNew->cds.zFile==0 ){
        rc = SQLITE_NOMEM;
      }else if( 0==zipfileScanExtra(&aRead[nFile], pNew->cds.nExtra, pt) ){
        pNew->mUnixTime = zipfileMtime(&pNew->cds);
      }
    }

    if( rc==SQLITE_OK ){
      static const int szFix = ZIPFILE_LFH_FIXED_SZ;
      ZipfileLFH lfh;
      if( pFile ){
        rc = zipfileReadData(pFile, aRead, szFix, pNew->cds.iOffset, pzErr);
      }else{
        aRead = (u8*)&aBlob[pNew->cds.iOffset];
      }

      rc = zipfileReadLFH(aRead, &lfh);
      if( rc==SQLITE_OK ){
        pNew->iDataOff =  pNew->cds.iOffset + ZIPFILE_LFH_FIXED_SZ;
        pNew->iDataOff += lfh.nFile + lfh.nExtra;
        if( aBlob && pNew->cds.szCompressed ){
          pNew->aData = &pNew->aExtra[nExtra];
          memcpy(pNew->aData, &aBlob[pNew->iDataOff], pNew->cds.szCompressed);
        }
      }else{
        *pzErr = sqlite3_mprintf("failed to read LFH at offset %d", 
            (int)pNew->cds.iOffset
        );
      }
    }

    if( rc!=SQLITE_OK ){
      zipfileEntryFree(pNew);
    }else{
      *ppEntry = pNew;
    }
  }

  return rc;
}

/*
** Advance an ZipfileCsr to its next row of output.
*/
static int zipfileNext(sqlite3_vtab_cursor *cur){
  ZipfileCsr *pCsr = (ZipfileCsr*)cur;
  int rc = SQLITE_OK;

  if( pCsr->pFile ){
    i64 iEof = pCsr->eocd.iOffset + pCsr->eocd.nSize;
    zipfileEntryFree(pCsr->pCurrent);
    pCsr->pCurrent = 0;
    if( pCsr->iNextOff>=iEof ){
      pCsr->bEof = 1;
    }else{
      ZipfileEntry *p = 0;
      ZipfileTab *pTab = (ZipfileTab*)(cur->pVtab);
      rc = zipfileGetEntry(pTab, 0, 0, pCsr->pFile, pCsr->iNextOff, &p);
      if( rc==SQLITE_OK ){
        pCsr->iNextOff += ZIPFILE_CDS_FIXED_SZ;
        pCsr->iNextOff += (int)p->cds.nExtra + p->cds.nFile + p->cds.nComment;
      }
      pCsr->pCurrent = p;
    }
  }else{
    if( !pCsr->bNoop ){
      pCsr->pCurrent = pCsr->pCurrent->pNext;
    }
    if( pCsr->pCurrent==0 ){
      pCsr->bEof = 1;
    }
  }

  pCsr->bNoop = 0;
  return rc;
}

static void zipfileFree(void *p) { 
  sqlite3_free(p); 
}

/*
** Buffer aIn (size nIn bytes) contains compressed data. Uncompressed, the
** size is nOut bytes. This function uncompresses the data and sets the
** return value in context pCtx to the result (a blob).
**
** If an error occurs, an error code is left in pCtx instead.
*/
static void zipfileInflate(
  sqlite3_context *pCtx,          /* Store result here */
  const u8 *aIn,                  /* Compressed data */
  int nIn,                        /* Size of buffer aIn[] in bytes */
  int nOut                        /* Expected output size */
){
  u8 *aRes = sqlite3_malloc(nOut);
  if( aRes==0 ){
    sqlite3_result_error_nomem(pCtx);
  }else{
    int err;
    z_stream str;
    memset(&str, 0, sizeof(str));

    str.next_in = (Byte*)aIn;
    str.avail_in = nIn;
    str.next_out = (Byte*)aRes;
    str.avail_out = nOut;

    err = inflateInit2(&str, -15);
    if( err!=Z_OK ){
      zipfileCtxErrorMsg(pCtx, "inflateInit2() failed (%d)", err);
    }else{
      err = inflate(&str, Z_NO_FLUSH);
      if( err!=Z_STREAM_END ){
        zipfileCtxErrorMsg(pCtx, "inflate() failed (%d)", err);
      }else{
        sqlite3_result_blob(pCtx, aRes, nOut, zipfileFree);
        aRes = 0;
      }
    }
    sqlite3_free(aRes);
    inflateEnd(&str);
  }
}

/*
** Buffer aIn (size nIn bytes) contains uncompressed data. This function
** compresses it and sets (*ppOut) to point to a buffer containing the
** compressed data. The caller is responsible for eventually calling
** sqlite3_free() to release buffer (*ppOut). Before returning, (*pnOut) 
** is set to the size of buffer (*ppOut) in bytes.
**
** If no error occurs, SQLITE_OK is returned. Otherwise, an SQLite error
** code is returned and an error message left in virtual-table handle
** pTab. The values of (*ppOut) and (*pnOut) are left unchanged in this
** case.
*/
static int zipfileDeflate(
  const u8 *aIn, int nIn,         /* Input */
  u8 **ppOut, int *pnOut,         /* Output */
  char **pzErr                    /* OUT: Error message */
){
  int nAlloc = (int)compressBound(nIn);
  u8 *aOut;
  int rc = SQLITE_OK;

  aOut = (u8*)sqlite3_malloc(nAlloc);
  if( aOut==0 ){
    rc = SQLITE_NOMEM;
  }else{
    int res;
    z_stream str;
    memset(&str, 0, sizeof(str));
    str.next_in = (Bytef*)aIn;
    str.avail_in = nIn;
    str.next_out = aOut;
    str.avail_out = nAlloc;

    deflateInit2(&str, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    res = deflate(&str, Z_FINISH);

    if( res==Z_STREAM_END ){
      *ppOut = aOut;
      *pnOut = (int)str.total_out;
    }else{
      sqlite3_free(aOut);
      *pzErr = sqlite3_mprintf("zipfile: deflate() error");
      rc = SQLITE_ERROR;
    }
    deflateEnd(&str);
  }

  return rc;
}


/*
** Return values of columns for the row at which the series_cursor
** is currently pointing.
*/
static int zipfileColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  ZipfileCsr *pCsr = (ZipfileCsr*)cur;
  ZipfileCDS *pCDS = &pCsr->pCurrent->cds;
  int rc = SQLITE_OK;
  switch( i ){
    case 0:   /* name */
      sqlite3_result_text(ctx, pCDS->zFile, -1, SQLITE_TRANSIENT);
      break;
    case 1:   /* mode */
      /* TODO: Whether or not the following is correct surely depends on
      ** the platform on which the archive was created.  */
      sqlite3_result_int(ctx, pCDS->iExternalAttr >> 16);
      break;
    case 2: { /* mtime */
      sqlite3_result_int64(ctx, pCsr->pCurrent->mUnixTime);
      break;
    }
    case 3: { /* sz */
      if( sqlite3_vtab_nochange(ctx)==0 ){
        sqlite3_result_int64(ctx, pCDS->szUncompressed);
      }
      break;
    }
    case 4:   /* rawdata */
      if( sqlite3_vtab_nochange(ctx) ) break;
    case 5: { /* data */
      if( i==4 || pCDS->iCompression==0 || pCDS->iCompression==8 ){
        int sz = pCDS->szCompressed;
        int szFinal = pCDS->szUncompressed;
        if( szFinal>0 ){
          u8 *aBuf;
          u8 *aFree = 0;
          if( pCsr->pCurrent->aData ){
            aBuf = pCsr->pCurrent->aData;
          }else{
            aBuf = aFree = sqlite3_malloc(sz);
            if( aBuf==0 ){
              rc = SQLITE_NOMEM;
            }else{
              FILE *pFile = pCsr->pFile;
              if( pFile==0 ){
                pFile = ((ZipfileTab*)(pCsr->base.pVtab))->pWriteFd;
              }
              rc = zipfileReadData(pFile, aBuf, sz, pCsr->pCurrent->iDataOff,
                  &pCsr->base.pVtab->zErrMsg
              );
            }
          }
          if( rc==SQLITE_OK ){
            if( i==5 && pCDS->iCompression ){
              zipfileInflate(ctx, aBuf, sz, szFinal);
            }else{
              sqlite3_result_blob(ctx, aBuf, sz, SQLITE_TRANSIENT);
            }
          }
          sqlite3_free(aFree);
        }else{
          /* Figure out if this is a directory or a zero-sized file. Consider
          ** it to be a directory either if the mode suggests so, or if
          ** the final character in the name is '/'.  */
          u32 mode = pCDS->iExternalAttr >> 16;
          if( !(mode & S_IFDIR) && pCDS->zFile[pCDS->nFile-1]!='/' ){
            sqlite3_result_blob(ctx, "", 0, SQLITE_STATIC);
          }
        }
      }
      break;
    }
    case 6:   /* method */
      sqlite3_result_int(ctx, pCDS->iCompression);
      break;
    default:  /* z */
      assert( i==7 );
      sqlite3_result_int64(ctx, pCsr->iId);
      break;
  }

  return rc;
}

/*
** Return TRUE if the cursor is at EOF.
*/
static int zipfileEof(sqlite3_vtab_cursor *cur){
  ZipfileCsr *pCsr = (ZipfileCsr*)cur;
  return pCsr->bEof;
}

/*
** If aBlob is not NULL, then it points to a buffer nBlob bytes in size
** containing an entire zip archive image. Or, if aBlob is NULL, then pFile
** is guaranteed to be a file-handle open on a zip file.
**
** This function attempts to locate the EOCD record within the zip archive
** and populate *pEOCD with the results of decoding it. SQLITE_OK is
** returned if successful. Otherwise, an SQLite error code is returned and
** an English language error message may be left in virtual-table pTab.
*/
static int zipfileReadEOCD(
  ZipfileTab *pTab,               /* Return errors here */
  const u8 *aBlob,                /* Pointer to in-memory file image */
  int nBlob,                      /* Size of aBlob[] in bytes */
  FILE *pFile,                    /* Read from this file if aBlob==0 */
  ZipfileEOCD *pEOCD              /* Object to populate */
){
  u8 *aRead = pTab->aBuffer;      /* Temporary buffer */
  int nRead;                      /* Bytes to read from file */
  int rc = SQLITE_OK;

  if( aBlob==0 ){
    i64 iOff;                     /* Offset to read from */
    i64 szFile;                   /* Total size of file in bytes */
    fseek(pFile, 0, SEEK_END);
    szFile = (i64)ftell(pFile);
    if( szFile==0 ){
      memset(pEOCD, 0, sizeof(ZipfileEOCD));
      return SQLITE_OK;
    }
    nRead = (int)(MIN(szFile, ZIPFILE_BUFFER_SIZE));
    iOff = szFile - nRead;
    rc = zipfileReadData(pFile, aRead, nRead, iOff, &pTab->base.zErrMsg);
  }else{
    nRead = (int)(MIN(nBlob, ZIPFILE_BUFFER_SIZE));
    aRead = (u8*)&aBlob[nBlob-nRead];
  }

  if( rc==SQLITE_OK ){
    int i;

    /* Scan backwards looking for the signature bytes */
    for(i=nRead-20; i>=0; i--){
      if( aRead[i]==0x50 && aRead[i+1]==0x4b 
       && aRead[i+2]==0x05 && aRead[i+3]==0x06 
      ){
        break;
      }
    }
    if( i<0 ){
      pTab->base.zErrMsg = sqlite3_mprintf(
          "cannot find end of central directory record"
      );
      return SQLITE_ERROR;
    }

    aRead += i+4;
    pEOCD->iDisk = zipfileRead16(aRead);
    pEOCD->iFirstDisk = zipfileRead16(aRead);
    pEOCD->nEntry = zipfileRead16(aRead);
    pEOCD->nEntryTotal = zipfileRead16(aRead);
    pEOCD->nSize = zipfileRead32(aRead);
    pEOCD->iOffset = zipfileRead32(aRead);
  }

  return rc;
}

/*
** Add object pNew to the linked list that begins at ZipfileTab.pFirstEntry 
** and ends with pLastEntry. If argument pBefore is NULL, then pNew is added
** to the end of the list. Otherwise, it is added to the list immediately
** before pBefore (which is guaranteed to be a part of said list).
*/
static void zipfileAddEntry(
  ZipfileTab *pTab, 
  ZipfileEntry *pBefore, 
  ZipfileEntry *pNew
){
  assert( (pTab->pFirstEntry==0)==(pTab->pLastEntry==0) );
  assert( pNew->pNext==0 );
  if( pBefore==0 ){
    if( pTab->pFirstEntry==0 ){
      pTab->pFirstEntry = pTab->pLastEntry = pNew;
    }else{
      assert( pTab->pLastEntry->pNext==0 );
      pTab->pLastEntry->pNext = pNew;
      pTab->pLastEntry = pNew;
    }
  }else{
    ZipfileEntry **pp;
    for(pp=&pTab->pFirstEntry; *pp!=pBefore; pp=&((*pp)->pNext));
    pNew->pNext = pBefore;
    *pp = pNew;
  }
}

static int zipfileLoadDirectory(ZipfileTab *pTab, const u8 *aBlob, int nBlob){
  ZipfileEOCD eocd;
  int rc;
  int i;
  i64 iOff;

  rc = zipfileReadEOCD(pTab, aBlob, nBlob, pTab->pWriteFd, &eocd);
  iOff = eocd.iOffset;
  for(i=0; rc==SQLITE_OK && i<eocd.nEntry; i++){
    ZipfileEntry *pNew = 0;
    rc = zipfileGetEntry(pTab, aBlob, nBlob, pTab->pWriteFd, iOff, &pNew);

    if( rc==SQLITE_OK ){
      zipfileAddEntry(pTab, 0, pNew);
      iOff += ZIPFILE_CDS_FIXED_SZ;
      iOff += (int)pNew->cds.nExtra + pNew->cds.nFile + pNew->cds.nComment;
    }
  }
  return rc;
}

/*
** xFilter callback.
*/
static int zipfileFilter(
  sqlite3_vtab_cursor *cur, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  ZipfileTab *pTab = (ZipfileTab*)cur->pVtab;
  ZipfileCsr *pCsr = (ZipfileCsr*)cur;
  const char *zFile = 0;          /* Zip file to scan */
  int rc = SQLITE_OK;             /* Return Code */
  int bInMemory = 0;              /* True for an in-memory zipfile */

  zipfileResetCursor(pCsr);

  if( pTab->zFile ){
    zFile = pTab->zFile;
  }else if( idxNum==0 ){
    zipfileCursorErr(pCsr, "zipfile() function requires an argument");
    return SQLITE_ERROR;
  }else if( sqlite3_value_type(argv[0])==SQLITE_BLOB ){
    const u8 *aBlob = (const u8*)sqlite3_value_blob(argv[0]);
    int nBlob = sqlite3_value_bytes(argv[0]);
    assert( pTab->pFirstEntry==0 );
    rc = zipfileLoadDirectory(pTab, aBlob, nBlob);
    pCsr->pFreeEntry = pTab->pFirstEntry;
    pTab->pFirstEntry = pTab->pLastEntry = 0;
    if( rc!=SQLITE_OK ) return rc;
    bInMemory = 1;
  }else{
    zFile = (const char*)sqlite3_value_text(argv[0]);
  }

  if( 0==pTab->pWriteFd && 0==bInMemory ){
    pCsr->pFile = fopen(zFile, "rb");
    if( pCsr->pFile==0 ){
      zipfileCursorErr(pCsr, "cannot open file: %s", zFile);
      rc = SQLITE_ERROR;
    }else{
      rc = zipfileReadEOCD(pTab, 0, 0, pCsr->pFile, &pCsr->eocd);
      if( rc==SQLITE_OK ){
        if( pCsr->eocd.nEntry==0 ){
          pCsr->bEof = 1;
        }else{
          pCsr->iNextOff = pCsr->eocd.iOffset;
          rc = zipfileNext(cur);
        }
      }
    }
  }else{
    pCsr->bNoop = 1;
    pCsr->pCurrent = pCsr->pFreeEntry ? pCsr->pFreeEntry : pTab->pFirstEntry;
    rc = zipfileNext(cur);
  }

  return rc;
}

/*
** xBestIndex callback.
*/
static int zipfileBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  int i;

  for(i=0; i<pIdxInfo->nConstraint; i++){
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    if( pCons->usable==0 ) continue;
    if( pCons->op!=SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    if( pCons->iColumn!=ZIPFILE_F_COLUMN_IDX ) continue;
    break;
  }

  if( i<pIdxInfo->nConstraint ){
    pIdxInfo->aConstraintUsage[i].argvIndex = 1;
    pIdxInfo->aConstraintUsage[i].omit = 1;
    pIdxInfo->estimatedCost = 1000.0;
    pIdxInfo->idxNum = 1;
  }else{
    pIdxInfo->estimatedCost = (double)(((sqlite3_int64)1) << 50);
    pIdxInfo->idxNum = 0;
  }

  return SQLITE_OK;
}

static ZipfileEntry *zipfileNewEntry(const char *zPath){
  ZipfileEntry *pNew;
  pNew = sqlite3_malloc(sizeof(ZipfileEntry));
  if( pNew ){
    memset(pNew, 0, sizeof(ZipfileEntry));
    pNew->cds.zFile = sqlite3_mprintf("%s", zPath);
    if( pNew->cds.zFile==0 ){
      sqlite3_free(pNew);
      pNew = 0;
    }
  }
  return pNew;
}

static int zipfileSerializeLFH(ZipfileEntry *pEntry, u8 *aBuf){
  ZipfileCDS *pCds = &pEntry->cds;
  u8 *a = aBuf;

  pCds->nExtra = 9;

  /* Write the LFH itself */
  zipfileWrite32(a, ZIPFILE_SIGNATURE_LFH);
  zipfileWrite16(a, pCds->iVersionExtract);
  zipfileWrite16(a, pCds->flags);
  zipfileWrite16(a, pCds->iCompression);
  zipfileWrite16(a, pCds->mTime);
  zipfileWrite16(a, pCds->mDate);
  zipfileWrite32(a, pCds->crc32);
  zipfileWrite32(a, pCds->szCompressed);
  zipfileWrite32(a, pCds->szUncompressed);
  zipfileWrite16(a, (u16)pCds->nFile);
  zipfileWrite16(a, pCds->nExtra);
  assert( a==&aBuf[ZIPFILE_LFH_FIXED_SZ] );

  /* Add the file name */
  memcpy(a, pCds->zFile, (int)pCds->nFile);
  a += (int)pCds->nFile;

  /* The "extra" data */
  zipfileWrite16(a, ZIPFILE_EXTRA_TIMESTAMP);
  zipfileWrite16(a, 5);
  *a++ = 0x01;
  zipfileWrite32(a, pEntry->mUnixTime);

  return a-aBuf;
}

static int zipfileAppendEntry(
  ZipfileTab *pTab,
  ZipfileEntry *pEntry,
  const u8 *pData,
  int nData
){
  u8 *aBuf = pTab->aBuffer;
  int nBuf;
  int rc;

  nBuf = zipfileSerializeLFH(pEntry, aBuf);
  rc = zipfileAppendData(pTab, aBuf, nBuf);
  if( rc==SQLITE_OK ){
    pEntry->iDataOff = pTab->szCurrent;
    rc = zipfileAppendData(pTab, pData, nData);
  }

  return rc;
}

static int zipfileGetMode(
  sqlite3_value *pVal, 
  int bIsDir,                     /* If true, default to directory */
  u32 *pMode,                     /* OUT: Mode value */
  char **pzErr                    /* OUT: Error message */
){
  const char *z = (const char*)sqlite3_value_text(pVal);
  u32 mode = 0;
  if( z==0 ){
    mode = (bIsDir ? (S_IFDIR + 0755) : (S_IFREG + 0644));
  }else if( z[0]>='0' && z[0]<='9' ){
    mode = (unsigned int)sqlite3_value_int(pVal);
  }else{
    const char zTemplate[11] = "-rwxrwxrwx";
    int i;
    if( strlen(z)!=10 ) goto parse_error;
    switch( z[0] ){
      case '-': mode |= S_IFREG; break;
      case 'd': mode |= S_IFDIR; break;
      case 'l': mode |= S_IFLNK; break;
      default: goto parse_error;
    }
    for(i=1; i<10; i++){
      if( z[i]==zTemplate[i] ) mode |= 1 << (9-i);
      else if( z[i]!='-' ) goto parse_error;
    }
  }
  if( ((mode & S_IFDIR)==0)==bIsDir ){
    /* The "mode" attribute is a directory, but data has been specified.
    ** Or vice-versa - no data but "mode" is a file or symlink.  */
    *pzErr = sqlite3_mprintf("zipfile: mode does not match data");
    return SQLITE_CONSTRAINT;
  }
  *pMode = mode;
  return SQLITE_OK;

 parse_error:
  *pzErr = sqlite3_mprintf("zipfile: parse error in mode: %s", z);
  return SQLITE_ERROR;
}

/*
** Both (const char*) arguments point to nul-terminated strings. Argument
** nB is the value of strlen(zB). This function returns 0 if the strings are
** identical, ignoring any trailing '/' character in either path.  */
static int zipfileComparePath(const char *zA, const char *zB, int nB){
  int nA = (int)strlen(zA);
  if( zA[nA-1]=='/' ) nA--;
  if( zB[nB-1]=='/' ) nB--;
  if( nA==nB && memcmp(zA, zB, nA)==0 ) return 0;
  return 1;
}

static int zipfileBegin(sqlite3_vtab *pVtab){
  ZipfileTab *pTab = (ZipfileTab*)pVtab;
  int rc = SQLITE_OK;

  assert( pTab->pWriteFd==0 );

  /* Open a write fd on the file. Also load the entire central directory
  ** structure into memory. During the transaction any new file data is 
  ** appended to the archive file, but the central directory is accumulated
  ** in main-memory until the transaction is committed.  */
  pTab->pWriteFd = fopen(pTab->zFile, "ab+");
  if( pTab->pWriteFd==0 ){
    pTab->base.zErrMsg = sqlite3_mprintf(
        "zipfile: failed to open file %s for writing", pTab->zFile
        );
    rc = SQLITE_ERROR;
  }else{
    fseek(pTab->pWriteFd, 0, SEEK_END);
    pTab->szCurrent = pTab->szOrig = (i64)ftell(pTab->pWriteFd);
    rc = zipfileLoadDirectory(pTab, 0, 0);
  }

  if( rc!=SQLITE_OK ){
    zipfileCleanupTransaction(pTab);
  }

  return rc;
}

/*
** Return the current time as a 32-bit timestamp in UNIX epoch format (like
** time(2)).
*/
static u32 zipfileTime(void){
  sqlite3_vfs *pVfs = sqlite3_vfs_find(0);
  u32 ret;
  if( pVfs->iVersion>=2 && pVfs->xCurrentTimeInt64 ){
    i64 ms;
    pVfs->xCurrentTimeInt64(pVfs, &ms);
    ret = (u32)((ms/1000) - ((i64)24405875 * 8640));
  }else{
    double day;
    pVfs->xCurrentTime(pVfs, &day);
    ret = (u32)((day - 2440587.5) * 86400);
  }
  return ret;
}

/*
** Return a 32-bit timestamp in UNIX epoch format.
**
** If the value passed as the only argument is either NULL or an SQL NULL,
** return the current time. Otherwise, return the value stored in (*pVal)
** cast to a 32-bit unsigned integer.
*/
static u32 zipfileGetTime(sqlite3_value *pVal){
  if( pVal==0 || sqlite3_value_type(pVal)==SQLITE_NULL ){
    return zipfileTime();
  }
  return (u32)sqlite3_value_int64(pVal);
}

/*
** Unless it is NULL, entry pOld is currently part of the pTab->pFirstEntry
** linked list.  Remove it from the list and free the object.
*/
static void zipfileRemoveEntryFromList(ZipfileTab *pTab, ZipfileEntry *pOld){
  if( pOld ){
    ZipfileEntry **pp;
    for(pp=&pTab->pFirstEntry; (*pp)!=pOld; pp=&((*pp)->pNext));
    *pp = (*pp)->pNext;
    zipfileEntryFree(pOld);
  }
}

/*
** xUpdate method.
*/
static int zipfileUpdate(
  sqlite3_vtab *pVtab, 
  int nVal, 
  sqlite3_value **apVal, 
  sqlite_int64 *pRowid
){
  ZipfileTab *pTab = (ZipfileTab*)pVtab;
  int rc = SQLITE_OK;             /* Return Code */
  ZipfileEntry *pNew = 0;         /* New in-memory CDS entry */

  u32 mode = 0;                   /* Mode for new entry */
  u32 mTime = 0;                  /* Modification time for new entry */
  i64 sz = 0;                     /* Uncompressed size */
  const char *zPath = 0;          /* Path for new entry */
  int nPath = 0;                  /* strlen(zPath) */
  const u8 *pData = 0;            /* Pointer to buffer containing content */
  int nData = 0;                  /* Size of pData buffer in bytes */
  int iMethod = 0;                /* Compression method for new entry */
  u8 *pFree = 0;                  /* Free this */
  char *zFree = 0;                /* Also free this */
  ZipfileEntry *pOld = 0;
  ZipfileEntry *pOld2 = 0;
  int bUpdate = 0;                /* True for an update that modifies "name" */
  int bIsDir = 0;
  u32 iCrc32 = 0;

  if( pTab->pWriteFd==0 ){
    rc = zipfileBegin(pVtab);
    if( rc!=SQLITE_OK ) return rc;
  }

  /* If this is a DELETE or UPDATE, find the archive entry to delete. */
  if( sqlite3_value_type(apVal[0])!=SQLITE_NULL ){
    const char *zDelete = (const char*)sqlite3_value_text(apVal[0]);
    int nDelete = (int)strlen(zDelete);
    if( nVal>1 ){
      const char *zUpdate = (const char*)sqlite3_value_text(apVal[1]);
      if( zUpdate && zipfileComparePath(zUpdate, zDelete, nDelete)!=0 ){
        bUpdate = 1;
      }
    }
    for(pOld=pTab->pFirstEntry; 1; pOld=pOld->pNext){
      if( zipfileComparePath(pOld->cds.zFile, zDelete, nDelete)==0 ){
        break;
      }
      assert( pOld->pNext );
    }
  }

  if( nVal>1 ){
    /* Check that "sz" and "rawdata" are both NULL: */
    if( sqlite3_value_type(apVal[5])!=SQLITE_NULL ){
      zipfileTableErr(pTab, "sz must be NULL");
      rc = SQLITE_CONSTRAINT;
    }
    if( sqlite3_value_type(apVal[6])!=SQLITE_NULL ){
      zipfileTableErr(pTab, "rawdata must be NULL"); 
      rc = SQLITE_CONSTRAINT;
    }

    if( rc==SQLITE_OK ){
      if( sqlite3_value_type(apVal[7])==SQLITE_NULL ){
        /* data=NULL. A directory */
        bIsDir = 1;
      }else{
        /* Value specified for "data", and possibly "method". This must be
        ** a regular file or a symlink. */
        const u8 *aIn = sqlite3_value_blob(apVal[7]);
        int nIn = sqlite3_value_bytes(apVal[7]);
        int bAuto = sqlite3_value_type(apVal[8])==SQLITE_NULL;

        iMethod = sqlite3_value_int(apVal[8]);
        sz = nIn;
        pData = aIn;
        nData = nIn;
        if( iMethod!=0 && iMethod!=8 ){
          zipfileTableErr(pTab, "unknown compression method: %d", iMethod);
          rc = SQLITE_CONSTRAINT;
        }else{
          if( bAuto || iMethod ){
            int nCmp;
            rc = zipfileDeflate(aIn, nIn, &pFree, &nCmp, &pTab->base.zErrMsg);
            if( rc==SQLITE_OK ){
              if( iMethod || nCmp<nIn ){
                iMethod = 8;
                pData = pFree;
                nData = nCmp;
              }
            }
          }
          iCrc32 = crc32(0, aIn, nIn);
        }
      }
    }

    if( rc==SQLITE_OK ){
      rc = zipfileGetMode(apVal[3], bIsDir, &mode, &pTab->base.zErrMsg);
    }

    if( rc==SQLITE_OK ){
      zPath = (const char*)sqlite3_value_text(apVal[2]);
      nPath = (int)strlen(zPath);
      mTime = zipfileGetTime(apVal[4]);
    }

    if( rc==SQLITE_OK && bIsDir ){
      /* For a directory, check that the last character in the path is a
      ** '/'. This appears to be required for compatibility with info-zip
      ** (the unzip command on unix). It does not create directories
      ** otherwise.  */
      if( zPath[nPath-1]!='/' ){
        zFree = sqlite3_mprintf("%s/", zPath);
        if( zFree==0 ){ rc = SQLITE_NOMEM; }
        zPath = (const char*)zFree;
        nPath++;
      }
    }

    /* Check that we're not inserting a duplicate entry -OR- updating an
    ** entry with a path, thereby making it into a duplicate. */
    if( (pOld==0 || bUpdate) && rc==SQLITE_OK ){
      ZipfileEntry *p;
      for(p=pTab->pFirstEntry; p; p=p->pNext){
        if( zipfileComparePath(p->cds.zFile, zPath, nPath)==0 ){
          switch( sqlite3_vtab_on_conflict(pTab->db) ){
            case SQLITE_IGNORE: {
              goto zipfile_update_done;
            }
            case SQLITE_REPLACE: {
              pOld2 = p;
              break;
            }
            default: {
              zipfileTableErr(pTab, "duplicate name: \"%s\"", zPath);
              rc = SQLITE_CONSTRAINT;
              break;
            }
          }
          break;
        }
      }
    }

    if( rc==SQLITE_OK ){
      /* Create the new CDS record. */
      pNew = zipfileNewEntry(zPath);
      if( pNew==0 ){
        rc = SQLITE_NOMEM;
      }else{
        pNew->cds.iVersionMadeBy = ZIPFILE_NEWENTRY_MADEBY;
        pNew->cds.iVersionExtract = ZIPFILE_NEWENTRY_REQUIRED;
        pNew->cds.flags = ZIPFILE_NEWENTRY_FLAGS;
        pNew->cds.iCompression = (u16)iMethod;
        zipfileMtimeToDos(&pNew->cds, mTime);
        pNew->cds.crc32 = iCrc32;
        pNew->cds.szCompressed = nData;
        pNew->cds.szUncompressed = (u32)sz;
        pNew->cds.iExternalAttr = (mode<<16);
        pNew->cds.iOffset = (u32)pTab->szCurrent;
        pNew->cds.nFile = (u16)nPath;
        pNew->mUnixTime = (u32)mTime;
        rc = zipfileAppendEntry(pTab, pNew, pData, nData);
        zipfileAddEntry(pTab, pOld, pNew);
      }
    }
  }

  if( rc==SQLITE_OK && (pOld || pOld2) ){
    ZipfileCsr *pCsr;
    for(pCsr=pTab->pCsrList; pCsr; pCsr=pCsr->pCsrNext){
      if( pCsr->pCurrent && (pCsr->pCurrent==pOld || pCsr->pCurrent==pOld2) ){
        pCsr->pCurrent = pCsr->pCurrent->pNext;
        pCsr->bNoop = 1;
      }
    }

    zipfileRemoveEntryFromList(pTab, pOld);
    zipfileRemoveEntryFromList(pTab, pOld2);
  }

zipfile_update_done:
  sqlite3_free(pFree);
  sqlite3_free(zFree);
  return rc;
}

static int zipfileSerializeEOCD(ZipfileEOCD *p, u8 *aBuf){
  u8 *a = aBuf;
  zipfileWrite32(a, ZIPFILE_SIGNATURE_EOCD);
  zipfileWrite16(a, p->iDisk);
  zipfileWrite16(a, p->iFirstDisk);
  zipfileWrite16(a, p->nEntry);
  zipfileWrite16(a, p->nEntryTotal);
  zipfileWrite32(a, p->nSize);
  zipfileWrite32(a, p->iOffset);
  zipfileWrite16(a, 0);        /* Size of trailing comment in bytes*/

  return a-aBuf;
}

static int zipfileAppendEOCD(ZipfileTab *pTab, ZipfileEOCD *p){
  int nBuf = zipfileSerializeEOCD(p, pTab->aBuffer);
  assert( nBuf==ZIPFILE_EOCD_FIXED_SZ );
  return zipfileAppendData(pTab, pTab->aBuffer, nBuf);
}

/*
** Serialize the CDS structure into buffer aBuf[]. Return the number
** of bytes written.
*/
static int zipfileSerializeCDS(ZipfileEntry *pEntry, u8 *aBuf){
  u8 *a = aBuf;
  ZipfileCDS *pCDS = &pEntry->cds;

  if( pEntry->aExtra==0 ){
    pCDS->nExtra = 9;
  }

  zipfileWrite32(a, ZIPFILE_SIGNATURE_CDS);
  zipfileWrite16(a, pCDS->iVersionMadeBy);
  zipfileWrite16(a, pCDS->iVersionExtract);
  zipfileWrite16(a, pCDS->flags);
  zipfileWrite16(a, pCDS->iCompression);
  zipfileWrite16(a, pCDS->mTime);
  zipfileWrite16(a, pCDS->mDate);
  zipfileWrite32(a, pCDS->crc32);
  zipfileWrite32(a, pCDS->szCompressed);
  zipfileWrite32(a, pCDS->szUncompressed);
  assert( a==&aBuf[ZIPFILE_CDS_NFILE_OFF] );
  zipfileWrite16(a, pCDS->nFile);
  zipfileWrite16(a, pCDS->nExtra);
  zipfileWrite16(a, pCDS->nComment);
  zipfileWrite16(a, pCDS->iDiskStart);
  zipfileWrite16(a, pCDS->iInternalAttr);
  zipfileWrite32(a, pCDS->iExternalAttr);
  zipfileWrite32(a, pCDS->iOffset);

  memcpy(a, pCDS->zFile, pCDS->nFile);
  a += pCDS->nFile;

  if( pEntry->aExtra ){
    int n = (int)pCDS->nExtra + (int)pCDS->nComment;
    memcpy(a, pEntry->aExtra, n);
    a += n;
  }else{
    assert( pCDS->nExtra==9 );
    zipfileWrite16(a, ZIPFILE_EXTRA_TIMESTAMP);
    zipfileWrite16(a, 5);
    *a++ = 0x01;
    zipfileWrite32(a, pEntry->mUnixTime);
  }

  return a-aBuf;
}

static int zipfileCommit(sqlite3_vtab *pVtab){
  ZipfileTab *pTab = (ZipfileTab*)pVtab;
  int rc = SQLITE_OK;
  if( pTab->pWriteFd ){
    i64 iOffset = pTab->szCurrent;
    ZipfileEntry *p;
    ZipfileEOCD eocd;
    int nEntry = 0;

    /* Write out all entries */
    for(p=pTab->pFirstEntry; rc==SQLITE_OK && p; p=p->pNext){
      int n = zipfileSerializeCDS(p, pTab->aBuffer);
      rc = zipfileAppendData(pTab, pTab->aBuffer, n);
      nEntry++;
    }

    /* Write out the EOCD record */
    eocd.iDisk = 0;
    eocd.iFirstDisk = 0;
    eocd.nEntry = (u16)nEntry;
    eocd.nEntryTotal = (u16)nEntry;
    eocd.nSize = (u32)(pTab->szCurrent - iOffset);
    eocd.iOffset = (u32)iOffset;
    rc = zipfileAppendEOCD(pTab, &eocd);

    zipfileCleanupTransaction(pTab);
  }
  return rc;
}

static int zipfileRollback(sqlite3_vtab *pVtab){
  return zipfileCommit(pVtab);
}

static ZipfileCsr *zipfileFindCursor(ZipfileTab *pTab, i64 iId){
  ZipfileCsr *pCsr;
  for(pCsr=pTab->pCsrList; pCsr; pCsr=pCsr->pCsrNext){
    if( iId==pCsr->iId ) break;
  }
  return pCsr;
}

static void zipfileFunctionCds(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  ZipfileCsr *pCsr;
  ZipfileTab *pTab = (ZipfileTab*)sqlite3_user_data(context);
  assert( argc>0 );

  pCsr = zipfileFindCursor(pTab, sqlite3_value_int64(argv[0]));
  if( pCsr ){
    ZipfileCDS *p = &pCsr->pCurrent->cds;
    char *zRes = sqlite3_mprintf("{"
        "\"version-made-by\" : %u, "
        "\"version-to-extract\" : %u, "
        "\"flags\" : %u, "
        "\"compression\" : %u, "
        "\"time\" : %u, "
        "\"date\" : %u, "
        "\"crc32\" : %u, "
        "\"compressed-size\" : %u, "
        "\"uncompressed-size\" : %u, "
        "\"file-name-length\" : %u, "
        "\"extra-field-length\" : %u, "
        "\"file-comment-length\" : %u, "
        "\"disk-number-start\" : %u, "
        "\"internal-attr\" : %u, "
        "\"external-attr\" : %u, "
        "\"offset\" : %u }",
        (u32)p->iVersionMadeBy, (u32)p->iVersionExtract,
        (u32)p->flags, (u32)p->iCompression,
        (u32)p->mTime, (u32)p->mDate,
        (u32)p->crc32, (u32)p->szCompressed,
        (u32)p->szUncompressed, (u32)p->nFile,
        (u32)p->nExtra, (u32)p->nComment,
        (u32)p->iDiskStart, (u32)p->iInternalAttr,
        (u32)p->iExternalAttr, (u32)p->iOffset
    );

    if( zRes==0 ){
      sqlite3_result_error_nomem(context);
    }else{
      sqlite3_result_text(context, zRes, -1, SQLITE_TRANSIENT);
      sqlite3_free(zRes);
    }
  }
}

/*
** xFindFunction method.
*/
static int zipfileFindFunction(
  sqlite3_vtab *pVtab,            /* Virtual table handle */
  int nArg,                       /* Number of SQL function arguments */
  const char *zName,              /* Name of SQL function */
  void (**pxFunc)(sqlite3_context*,int,sqlite3_value**), /* OUT: Result */
  void **ppArg                    /* OUT: User data for *pxFunc */
){
  if( sqlite3_stricmp("zipfile_cds", zName)==0 ){
    *pxFunc = zipfileFunctionCds;
    *ppArg = (void*)pVtab;
    return 1;
  }
  return 0;
}

typedef struct ZipfileBuffer ZipfileBuffer;
struct ZipfileBuffer {
  u8 *a;                          /* Pointer to buffer */
  int n;                          /* Size of buffer in bytes */
  int nAlloc;                     /* Byte allocated at a[] */
};

typedef struct ZipfileCtx ZipfileCtx;
struct ZipfileCtx {
  int nEntry;
  ZipfileBuffer body;
  ZipfileBuffer cds;
};

static int zipfileBufferGrow(ZipfileBuffer *pBuf, int nByte){
  if( pBuf->n+nByte>pBuf->nAlloc ){
    u8 *aNew;
    int nNew = pBuf->n ? pBuf->n*2 : 512;
    int nReq = pBuf->n + nByte;

    while( nNew<nReq ) nNew = nNew*2;
    aNew = sqlite3_realloc(pBuf->a, nNew);
    if( aNew==0 ) return SQLITE_NOMEM;
    pBuf->a = aNew;
    pBuf->nAlloc = nNew;
  }
  return SQLITE_OK;
}

/*
** xStep() callback for the zipfile() aggregate. This can be called in
** any of the following ways:
**
**   SELECT zipfile(name,data) ...
**   SELECT zipfile(name,mode,mtime,data) ...
**   SELECT zipfile(name,mode,mtime,data,method) ...
*/
void zipfileStep(sqlite3_context *pCtx, int nVal, sqlite3_value **apVal){
  ZipfileCtx *p;                  /* Aggregate function context */
  ZipfileEntry e;                 /* New entry to add to zip archive */

  sqlite3_value *pName = 0;
  sqlite3_value *pMode = 0;
  sqlite3_value *pMtime = 0;
  sqlite3_value *pData = 0;
  sqlite3_value *pMethod = 0;

  int bIsDir = 0;
  u32 mode;
  int rc = SQLITE_OK;
  char *zErr = 0;

  int iMethod = -1;               /* Compression method to use (0 or 8) */

  const u8 *aData = 0;            /* Possibly compressed data for new entry */
  int nData = 0;                  /* Size of aData[] in bytes */
  int szUncompressed = 0;         /* Size of data before compression */
  u8 *aFree = 0;                  /* Free this before returning */
  u32 iCrc32 = 0;                 /* crc32 of uncompressed data */

  char *zName = 0;                /* Path (name) of new entry */
  int nName = 0;                  /* Size of zName in bytes */
  char *zFree = 0;                /* Free this before returning */
  int nByte;

  memset(&e, 0, sizeof(e));
  p = (ZipfileCtx*)sqlite3_aggregate_context(pCtx, sizeof(ZipfileCtx));
  if( p==0 ) return;

  /* Martial the arguments into stack variables */
  if( nVal!=2 && nVal!=4 && nVal!=5 ){
    zErr = sqlite3_mprintf("wrong number of arguments to function zipfile()");
    rc = SQLITE_ERROR;
    goto zipfile_step_out;
  }
  pName = apVal[0];
  if( nVal==2 ){
    pData = apVal[1];
  }else{
    pMode = apVal[1];
    pMtime = apVal[2];
    pData = apVal[3];
    if( nVal==5 ){
      pMethod = apVal[4];
    }
  }

  /* Check that the 'name' parameter looks ok. */
  zName = (char*)sqlite3_value_text(pName);
  nName = sqlite3_value_bytes(pName);
  if( zName==0 ){
    zErr = sqlite3_mprintf("first argument to zipfile() must be non-NULL");
    rc = SQLITE_ERROR;
    goto zipfile_step_out;
  }

  /* Inspect the 'method' parameter. This must be either 0 (store), 8 (use
  ** deflate compression) or NULL (choose automatically).  */
  if( pMethod && SQLITE_NULL!=sqlite3_value_type(pMethod) ){
    iMethod = (int)sqlite3_value_int64(pMethod);
    if( iMethod!=0 && iMethod!=8 ){
      zErr = sqlite3_mprintf("illegal method value: %d", iMethod);
      rc = SQLITE_ERROR;
      goto zipfile_step_out;
    }
  }

  /* Now inspect the data. If this is NULL, then the new entry must be a
  ** directory.  Otherwise, figure out whether or not the data should
  ** be deflated or simply stored in the zip archive. */
  if( sqlite3_value_type(pData)==SQLITE_NULL ){
    bIsDir = 1;
    iMethod = 0;
  }else{
    aData = sqlite3_value_blob(pData);
    szUncompressed = nData = sqlite3_value_bytes(pData);
    iCrc32 = crc32(0, aData, nData);
    if( iMethod<0 || iMethod==8 ){
      int nOut = 0;
      rc = zipfileDeflate(aData, nData, &aFree, &nOut, &zErr);
      if( rc!=SQLITE_OK ){
        goto zipfile_step_out;
      }
      if( iMethod==8 || nOut<nData ){
        aData = aFree;
        nData = nOut;
        iMethod = 8;
      }else{
        iMethod = 0;
      }
    }
  }

  /* Decode the "mode" argument. */
  rc = zipfileGetMode(pMode, bIsDir, &mode, &zErr);
  if( rc ) goto zipfile_step_out;

  /* Decode the "mtime" argument. */
  e.mUnixTime = zipfileGetTime(pMtime);

  /* If this is a directory entry, ensure that there is exactly one '/'
  ** at the end of the path. Or, if this is not a directory and the path
  ** ends in '/' it is an error. */
  if( bIsDir==0 ){
    if( zName[nName-1]=='/' ){
      zErr = sqlite3_mprintf("non-directory name must not end with /");
      rc = SQLITE_ERROR;
      goto zipfile_step_out;
    }
  }else{
    if( zName[nName-1]!='/' ){
      zName = zFree = sqlite3_mprintf("%s/", zName);
      nName++;
      if( zName==0 ){
        rc = SQLITE_NOMEM;
        goto zipfile_step_out;
      }
    }else{
      while( nName>1 && zName[nName-2]=='/' ) nName--;
    }
  }

  /* Assemble the ZipfileEntry object for the new zip archive entry */
  e.cds.iVersionMadeBy = ZIPFILE_NEWENTRY_MADEBY;
  e.cds.iVersionExtract = ZIPFILE_NEWENTRY_REQUIRED;
  e.cds.flags = ZIPFILE_NEWENTRY_FLAGS;
  e.cds.iCompression = (u16)iMethod;
  zipfileMtimeToDos(&e.cds, (u32)e.mUnixTime);
  e.cds.crc32 = iCrc32;
  e.cds.szCompressed = nData;
  e.cds.szUncompressed = szUncompressed;
  e.cds.iExternalAttr = (mode<<16);
  e.cds.iOffset = p->body.n;
  e.cds.nFile = (u16)nName;
  e.cds.zFile = zName;

  /* Append the LFH to the body of the new archive */
  nByte = ZIPFILE_LFH_FIXED_SZ + e.cds.nFile + 9;
  if( (rc = zipfileBufferGrow(&p->body, nByte)) ) goto zipfile_step_out;
  p->body.n += zipfileSerializeLFH(&e, &p->body.a[p->body.n]);

  /* Append the data to the body of the new archive */
  if( nData>0 ){
    if( (rc = zipfileBufferGrow(&p->body, nData)) ) goto zipfile_step_out;
    memcpy(&p->body.a[p->body.n], aData, nData);
    p->body.n += nData;
  }

  /* Append the CDS record to the directory of the new archive */
  nByte = ZIPFILE_CDS_FIXED_SZ + e.cds.nFile + 9;
  if( (rc = zipfileBufferGrow(&p->cds, nByte)) ) goto zipfile_step_out;
  p->cds.n += zipfileSerializeCDS(&e, &p->cds.a[p->cds.n]);

  /* Increment the count of entries in the archive */
  p->nEntry++;

 zipfile_step_out:
  sqlite3_free(aFree);
  sqlite3_free(zFree);
  if( rc ){
    if( zErr ){
      sqlite3_result_error(pCtx, zErr, -1);
    }else{
      sqlite3_result_error_code(pCtx, rc);
    }
  }
  sqlite3_free(zErr);
}

/*
** xFinalize() callback for zipfile aggregate function.
*/
void zipfileFinal(sqlite3_context *pCtx){
  ZipfileCtx *p;
  ZipfileEOCD eocd;
  int nZip;
  u8 *aZip;

  p = (ZipfileCtx*)sqlite3_aggregate_context(pCtx, sizeof(ZipfileCtx));
  if( p==0 ) return;
  if( p->nEntry>0 ){
    memset(&eocd, 0, sizeof(eocd));
    eocd.nEntry = (u16)p->nEntry;
    eocd.nEntryTotal = (u16)p->nEntry;
    eocd.nSize = p->cds.n;
    eocd.iOffset = p->body.n;

    nZip = p->body.n + p->cds.n + ZIPFILE_EOCD_FIXED_SZ;
    aZip = (u8*)sqlite3_malloc(nZip);
    if( aZip==0 ){
      sqlite3_result_error_nomem(pCtx);
    }else{
      memcpy(aZip, p->body.a, p->body.n);
      memcpy(&aZip[p->body.n], p->cds.a, p->cds.n);
      zipfileSerializeEOCD(&eocd, &aZip[p->body.n + p->cds.n]);
      sqlite3_result_blob(pCtx, aZip, nZip, zipfileFree);
    }
  }

  sqlite3_free(p->body.a);
  sqlite3_free(p->cds.a);
}


/*
** Register the "zipfile" virtual table.
*/
static int zipfileRegister(sqlite3 *db){
  static sqlite3_module zipfileModule = {
    1,                         /* iVersion */
    zipfileConnect,            /* xCreate */
    zipfileConnect,            /* xConnect */
    zipfileBestIndex,          /* xBestIndex */
    zipfileDisconnect,         /* xDisconnect */
    zipfileDisconnect,         /* xDestroy */
    zipfileOpen,               /* xOpen - open a cursor */
    zipfileClose,              /* xClose - close a cursor */
    zipfileFilter,             /* xFilter - configure scan constraints */
    zipfileNext,               /* xNext - advance a cursor */
    zipfileEof,                /* xEof - check for end of scan */
    zipfileColumn,             /* xColumn - read data */
    0,                         /* xRowid - read data */
    zipfileUpdate,             /* xUpdate */
    zipfileBegin,              /* xBegin */
    0,                         /* xSync */
    zipfileCommit,             /* xCommit */
    zipfileRollback,           /* xRollback */
    zipfileFindFunction,       /* xFindMethod */
    0,                         /* xRename */
  };

  int rc = sqlite3_create_module(db, "zipfile"  , &zipfileModule, 0);
  if( rc==SQLITE_OK ) rc = sqlite3_overload_function(db, "zipfile_cds", -1);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "zipfile", -1, SQLITE_UTF8, 0, 0, 
        zipfileStep, zipfileFinal
    );
  }
  return rc;
}
#else         /* SQLITE_OMIT_VIRTUALTABLE */
# define zipfileRegister(x) SQLITE_OK
#endif

#ifdef _WIN32

#endif
int sqlite3_zipfile_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  return zipfileRegister(db);
}

/************************* End ../ext/misc/zipfile.c ********************/
/************************* Begin ../ext/misc/sqlar.c ******************/
/*
** 2017-12-17
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** Utility functions sqlar_compress() and sqlar_uncompress(). Useful
** for working with sqlar archives and used by the shell tool's built-in
** sqlar support.
*/
SQLITE_EXTENSION_INIT1
#include <zlib.h>

/*
** Implementation of the "sqlar_compress(X)" SQL function.
**
** If the type of X is SQLITE_BLOB, and compressing that blob using
** zlib utility function compress() yields a smaller blob, return the
** compressed blob. Otherwise, return a copy of X.
**
** SQLar uses the "zlib format" for compressed content.  The zlib format
** contains a two-byte identification header and a four-byte checksum at
** the end.  This is different from ZIP which uses the raw deflate format.
**
** Future enhancements to SQLar might add support for new compression formats.
** If so, those new formats will be identified by alternative headers in the
** compressed data.
*/
static void sqlarCompressFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  assert( argc==1 );
  if( sqlite3_value_type(argv[0])==SQLITE_BLOB ){
    const Bytef *pData = sqlite3_value_blob(argv[0]);
    uLong nData = sqlite3_value_bytes(argv[0]);
    uLongf nOut = compressBound(nData);
    Bytef *pOut;

    pOut = (Bytef*)sqlite3_malloc(nOut);
    if( pOut==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }else{
      if( Z_OK!=compress(pOut, &nOut, pData, nData) ){
        sqlite3_result_error(context, "error in compress()", -1);
      }else if( nOut<nData ){
        sqlite3_result_blob(context, pOut, nOut, SQLITE_TRANSIENT);
      }else{
        sqlite3_result_value(context, argv[0]);
      }
      sqlite3_free(pOut);
    }
  }else{
    sqlite3_result_value(context, argv[0]);
  }
}

/*
** Implementation of the "sqlar_uncompress(X,SZ)" SQL function
**
** Parameter SZ is interpreted as an integer. If it is less than or
** equal to zero, then this function returns a copy of X. Or, if
** SZ is equal to the size of X when interpreted as a blob, also
** return a copy of X. Otherwise, decompress blob X using zlib
** utility function uncompress() and return the results (another
** blob).
*/
static void sqlarUncompressFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  uLong nData;
  uLongf sz;

  assert( argc==2 );
  sz = sqlite3_value_int(argv[1]);

  if( sz<=0 || sz==(nData = sqlite3_value_bytes(argv[0])) ){
    sqlite3_result_value(context, argv[0]);
  }else{
    const Bytef *pData= sqlite3_value_blob(argv[0]);
    Bytef *pOut = sqlite3_malloc(sz);
    if( Z_OK!=uncompress(pOut, &sz, pData, nData) ){
      sqlite3_result_error(context, "error in uncompress()", -1);
    }else{
      sqlite3_result_blob(context, pOut, sz, SQLITE_TRANSIENT);
    }
    sqlite3_free(pOut);
  }
}


#ifdef _WIN32

#endif
int sqlite3_sqlar_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "sqlar_compress", 1, SQLITE_UTF8, 0,
                               sqlarCompressFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "sqlar_uncompress", 2, SQLITE_UTF8, 0,
                                 sqlarUncompressFunc, 0, 0);
  }
  return rc;
}

/************************* End ../ext/misc/sqlar.c ********************/
#endif
/************************* Begin ../ext/expert/sqlite3expert.h ******************/
/*
** 2017 April 07
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*/



typedef struct sqlite3expert sqlite3expert;

/*
** Create a new sqlite3expert object.
**
** If successful, a pointer to the new object is returned and (*pzErr) set
** to NULL. Or, if an error occurs, NULL is returned and (*pzErr) set to
** an English-language error message. In this case it is the responsibility
** of the caller to eventually free the error message buffer using
** sqlite3_free().
*/
sqlite3expert *sqlite3_expert_new(sqlite3 *db, char **pzErr);

/*
** Configure an sqlite3expert object.
**
** EXPERT_CONFIG_SAMPLE:
**   By default, sqlite3_expert_analyze() generates sqlite_stat1 data for
**   each candidate index. This involves scanning and sorting the entire
**   contents of each user database table once for each candidate index
**   associated with the table. For large databases, this can be 
**   prohibitively slow. This option allows the sqlite3expert object to
**   be configured so that sqlite_stat1 data is instead generated based on a
**   subset of each table, or so that no sqlite_stat1 data is used at all.
**
**   A single integer argument is passed to this option. If the value is less
**   than or equal to zero, then no sqlite_stat1 data is generated or used by
**   the analysis - indexes are recommended based on the database schema only.
**   Or, if the value is 100 or greater, complete sqlite_stat1 data is
**   generated for each candidate index (this is the default). Finally, if the
**   value falls between 0 and 100, then it represents the percentage of user
**   table rows that should be considered when generating sqlite_stat1 data.
**
**   Examples:
**
**     // Do not generate any sqlite_stat1 data
**     sqlite3_expert_config(pExpert, EXPERT_CONFIG_SAMPLE, 0);
**
**     // Generate sqlite_stat1 data based on 10% of the rows in each table.
**     sqlite3_expert_config(pExpert, EXPERT_CONFIG_SAMPLE, 10);
*/
int sqlite3_expert_config(sqlite3expert *p, int op, ...);

#define EXPERT_CONFIG_SAMPLE 1    /* int */

/*
** Specify zero or more SQL statements to be included in the analysis.
**
** Buffer zSql must contain zero or more complete SQL statements. This
** function parses all statements contained in the buffer and adds them
** to the internal list of statements to analyze. If successful, SQLITE_OK
** is returned and (*pzErr) set to NULL. Or, if an error occurs - for example
** due to a error in the SQL - an SQLite error code is returned and (*pzErr)
** may be set to point to an English language error message. In this case
** the caller is responsible for eventually freeing the error message buffer
** using sqlite3_free().
**
** If an error does occur while processing one of the statements in the
** buffer passed as the second argument, none of the statements in the
** buffer are added to the analysis.
**
** This function must be called before sqlite3_expert_analyze(). If a call
** to this function is made on an sqlite3expert object that has already
** been passed to sqlite3_expert_analyze() SQLITE_MISUSE is returned
** immediately and no statements are added to the analysis.
*/
int sqlite3_expert_sql(
  sqlite3expert *p,               /* From a successful sqlite3_expert_new() */
  const char *zSql,               /* SQL statement(s) to add */
  char **pzErr                    /* OUT: Error message (if any) */
);


/*
** This function is called after the sqlite3expert object has been configured
** with all SQL statements using sqlite3_expert_sql() to actually perform
** the analysis. Once this function has been called, it is not possible to
** add further SQL statements to the analysis.
**
** If successful, SQLITE_OK is returned and (*pzErr) is set to NULL. Or, if
** an error occurs, an SQLite error code is returned and (*pzErr) set to 
** point to a buffer containing an English language error message. In this
** case it is the responsibility of the caller to eventually free the buffer
** using sqlite3_free().
**
** If an error does occur within this function, the sqlite3expert object
** is no longer useful for any purpose. At that point it is no longer
** possible to add further SQL statements to the object or to re-attempt
** the analysis. The sqlite3expert object must still be freed using a call
** sqlite3_expert_destroy().
*/
int sqlite3_expert_analyze(sqlite3expert *p, char **pzErr);

/*
** Return the total number of statements loaded using sqlite3_expert_sql().
** The total number of SQL statements may be different from the total number
** to calls to sqlite3_expert_sql().
*/
int sqlite3_expert_count(sqlite3expert*);

/*
** Return a component of the report.
**
** This function is called after sqlite3_expert_analyze() to extract the
** results of the analysis. Each call to this function returns either a
** NULL pointer or a pointer to a buffer containing a nul-terminated string.
** The value passed as the third argument must be one of the EXPERT_REPORT_*
** #define constants defined below.
**
** For some EXPERT_REPORT_* parameters, the buffer returned contains 
** information relating to a specific SQL statement. In these cases that
** SQL statement is identified by the value passed as the second argument.
** SQL statements are numbered from 0 in the order in which they are parsed.
** If an out-of-range value (less than zero or equal to or greater than the
** value returned by sqlite3_expert_count()) is passed as the second argument
** along with such an EXPERT_REPORT_* parameter, NULL is always returned.
**
** EXPERT_REPORT_SQL:
**   Return the text of SQL statement iStmt.
**
** EXPERT_REPORT_INDEXES:
**   Return a buffer containing the CREATE INDEX statements for all recommended
**   indexes for statement iStmt. If there are no new recommeded indexes, NULL 
**   is returned.
**
** EXPERT_REPORT_PLAN:
**   Return a buffer containing the EXPLAIN QUERY PLAN output for SQL query
**   iStmt after the proposed indexes have been added to the database schema.
**
** EXPERT_REPORT_CANDIDATES:
**   Return a pointer to a buffer containing the CREATE INDEX statements 
**   for all indexes that were tested (for all SQL statements). The iStmt
**   parameter is ignored for EXPERT_REPORT_CANDIDATES calls.
*/
const char *sqlite3_expert_report(sqlite3expert*, int iStmt, int eReport);

/*
** Values for the third argument passed to sqlite3_expert_report().
*/
#define EXPERT_REPORT_SQL        1
#define EXPERT_REPORT_INDEXES    2
#define EXPERT_REPORT_PLAN       3
#define EXPERT_REPORT_CANDIDATES 4

/*
** Free an (sqlite3expert*) handle and all associated resources. There 
** should be one call to this function for each successful call to 
** sqlite3-expert_new().
*/
void sqlite3_expert_destroy(sqlite3expert*);



/************************* End ../ext/expert/sqlite3expert.h ********************/
/************************* Begin ../ext/expert/sqlite3expert.c ******************/
/*
** 2017 April 09
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*/
#include <assert.h>
#include <string.h>
#include <stdio.h>

#ifndef SQLITE_OMIT_VIRTUALTABLE 

/* typedef sqlite3_int64 i64; */
/* typedef sqlite3_uint64 u64; */

typedef struct IdxColumn IdxColumn;
typedef struct IdxConstraint IdxConstraint;
typedef struct IdxScan IdxScan;
typedef struct IdxStatement IdxStatement;
typedef struct IdxTable IdxTable;
typedef struct IdxWrite IdxWrite;

#define STRLEN  (int)strlen

/*
** A temp table name that we assume no user database will actually use.
** If this assumption proves incorrect triggers on the table with the
** conflicting name will be ignored.
*/
#define UNIQUE_TABLE_NAME "t592690916721053953805701627921227776"

/*
** A single constraint. Equivalent to either "col = ?" or "col < ?" (or
** any other type of single-ended range constraint on a column).
**
** pLink:
**   Used to temporarily link IdxConstraint objects into lists while
**   creating candidate indexes.
*/
struct IdxConstraint {
  char *zColl;                    /* Collation sequence */
  int bRange;                     /* True for range, false for eq */
  int iCol;                       /* Constrained table column */
  int bFlag;                      /* Used by idxFindCompatible() */
  int bDesc;                      /* True if ORDER BY <expr> DESC */
  IdxConstraint *pNext;           /* Next constraint in pEq or pRange list */
  IdxConstraint *pLink;           /* See above */
};

/*
** A single scan of a single table.
*/
struct IdxScan {
  IdxTable *pTab;                 /* Associated table object */
  int iDb;                        /* Database containing table zTable */
  i64 covering;                   /* Mask of columns required for cov. index */
  IdxConstraint *pOrder;          /* ORDER BY columns */
  IdxConstraint *pEq;             /* List of == constraints */
  IdxConstraint *pRange;          /* List of < constraints */
  IdxScan *pNextScan;             /* Next IdxScan object for same analysis */
};

/*
** Information regarding a single database table. Extracted from 
** "PRAGMA table_info" by function idxGetTableInfo().
*/
struct IdxColumn {
  char *zName;
  char *zColl;
  int iPk;
};
struct IdxTable {
  int nCol;
  char *zName;                    /* Table name */
  IdxColumn *aCol;
  IdxTable *pNext;                /* Next table in linked list of all tables */
};

/*
** An object of the following type is created for each unique table/write-op
** seen. The objects are stored in a singly-linked list beginning at
** sqlite3expert.pWrite.
*/
struct IdxWrite {
  IdxTable *pTab;
  int eOp;                        /* SQLITE_UPDATE, DELETE or INSERT */
  IdxWrite *pNext;
};

/*
** Each statement being analyzed is represented by an instance of this
** structure.
*/
struct IdxStatement {
  int iId;                        /* Statement number */
  char *zSql;                     /* SQL statement */
  char *zIdx;                     /* Indexes */
  char *zEQP;                     /* Plan */
  IdxStatement *pNext;
};


/*
** A hash table for storing strings. With space for a payload string
** with each entry. Methods are:
**
**   idxHashInit()
**   idxHashClear()
**   idxHashAdd()
**   idxHashSearch()
*/
#define IDX_HASH_SIZE 1023
typedef struct IdxHashEntry IdxHashEntry;
typedef struct IdxHash IdxHash;
struct IdxHashEntry {
  char *zKey;                     /* nul-terminated key */
  char *zVal;                     /* nul-terminated value string */
  char *zVal2;                    /* nul-terminated value string 2 */
  IdxHashEntry *pHashNext;        /* Next entry in same hash bucket */
  IdxHashEntry *pNext;            /* Next entry in hash */
};
struct IdxHash {
  IdxHashEntry *pFirst;
  IdxHashEntry *aHash[IDX_HASH_SIZE];
};

/*
** sqlite3expert object.
*/
struct sqlite3expert {
  int iSample;                    /* Percentage of tables to sample for stat1 */
  sqlite3 *db;                    /* User database */
  sqlite3 *dbm;                   /* In-memory db for this analysis */
  sqlite3 *dbv;                   /* Vtab schema for this analysis */
  IdxTable *pTable;               /* List of all IdxTable objects */
  IdxScan *pScan;                 /* List of scan objects */
  IdxWrite *pWrite;               /* List of write objects */
  IdxStatement *pStatement;       /* List of IdxStatement objects */
  int bRun;                       /* True once analysis has run */
  char **pzErrmsg;
  int rc;                         /* Error code from whereinfo hook */
  IdxHash hIdx;                   /* Hash containing all candidate indexes */
  char *zCandidates;              /* For EXPERT_REPORT_CANDIDATES */
};


/*
** Allocate and return nByte bytes of zeroed memory using sqlite3_malloc(). 
** If the allocation fails, set *pRc to SQLITE_NOMEM and return NULL.
*/
static void *idxMalloc(int *pRc, int nByte){
  void *pRet;
  assert( *pRc==SQLITE_OK );
  assert( nByte>0 );
  pRet = sqlite3_malloc(nByte);
  if( pRet ){
    memset(pRet, 0, nByte);
  }else{
    *pRc = SQLITE_NOMEM;
  }
  return pRet;
}

/*
** Initialize an IdxHash hash table.
*/
static void idxHashInit(IdxHash *pHash){
  memset(pHash, 0, sizeof(IdxHash));
}

/*
** Reset an IdxHash hash table.
*/
static void idxHashClear(IdxHash *pHash){
  int i;
  for(i=0; i<IDX_HASH_SIZE; i++){
    IdxHashEntry *pEntry;
    IdxHashEntry *pNext;
    for(pEntry=pHash->aHash[i]; pEntry; pEntry=pNext){
      pNext = pEntry->pHashNext;
      sqlite3_free(pEntry->zVal2);
      sqlite3_free(pEntry);
    }
  }
  memset(pHash, 0, sizeof(IdxHash));
}

/*
** Return the index of the hash bucket that the string specified by the
** arguments to this function belongs.
*/
static int idxHashString(const char *z, int n){
  unsigned int ret = 0;
  int i;
  for(i=0; i<n; i++){
    ret += (ret<<3) + (unsigned char)(z[i]);
  }
  return (int)(ret % IDX_HASH_SIZE);
}

/*
** If zKey is already present in the hash table, return non-zero and do
** nothing. Otherwise, add an entry with key zKey and payload string zVal to
** the hash table passed as the second argument. 
*/
static int idxHashAdd(
  int *pRc, 
  IdxHash *pHash, 
  const char *zKey,
  const char *zVal
){
  int nKey = STRLEN(zKey);
  int iHash = idxHashString(zKey, nKey);
  int nVal = (zVal ? STRLEN(zVal) : 0);
  IdxHashEntry *pEntry;
  assert( iHash>=0 );
  for(pEntry=pHash->aHash[iHash]; pEntry; pEntry=pEntry->pHashNext){
    if( STRLEN(pEntry->zKey)==nKey && 0==memcmp(pEntry->zKey, zKey, nKey) ){
      return 1;
    }
  }
  pEntry = idxMalloc(pRc, sizeof(IdxHashEntry) + nKey+1 + nVal+1);
  if( pEntry ){
    pEntry->zKey = (char*)&pEntry[1];
    memcpy(pEntry->zKey, zKey, nKey);
    if( zVal ){
      pEntry->zVal = &pEntry->zKey[nKey+1];
      memcpy(pEntry->zVal, zVal, nVal);
    }
    pEntry->pHashNext = pHash->aHash[iHash];
    pHash->aHash[iHash] = pEntry;

    pEntry->pNext = pHash->pFirst;
    pHash->pFirst = pEntry;
  }
  return 0;
}

/*
** If zKey/nKey is present in the hash table, return a pointer to the 
** hash-entry object.
*/
static IdxHashEntry *idxHashFind(IdxHash *pHash, const char *zKey, int nKey){
  int iHash;
  IdxHashEntry *pEntry;
  if( nKey<0 ) nKey = STRLEN(zKey);
  iHash = idxHashString(zKey, nKey);
  assert( iHash>=0 );
  for(pEntry=pHash->aHash[iHash]; pEntry; pEntry=pEntry->pHashNext){
    if( STRLEN(pEntry->zKey)==nKey && 0==memcmp(pEntry->zKey, zKey, nKey) ){
      return pEntry;
    }
  }
  return 0;
}

/*
** If the hash table contains an entry with a key equal to the string
** passed as the final two arguments to this function, return a pointer
** to the payload string. Otherwise, if zKey/nKey is not present in the
** hash table, return NULL.
*/
static const char *idxHashSearch(IdxHash *pHash, const char *zKey, int nKey){
  IdxHashEntry *pEntry = idxHashFind(pHash, zKey, nKey);
  if( pEntry ) return pEntry->zVal;
  return 0;
}

/*
** Allocate and return a new IdxConstraint object. Set the IdxConstraint.zColl
** variable to point to a copy of nul-terminated string zColl.
*/
static IdxConstraint *idxNewConstraint(int *pRc, const char *zColl){
  IdxConstraint *pNew;
  int nColl = STRLEN(zColl);

  assert( *pRc==SQLITE_OK );
  pNew = (IdxConstraint*)idxMalloc(pRc, sizeof(IdxConstraint) * nColl + 1);
  if( pNew ){
    pNew->zColl = (char*)&pNew[1];
    memcpy(pNew->zColl, zColl, nColl+1);
  }
  return pNew;
}

/*
** An error associated with database handle db has just occurred. Pass
** the error message to callback function xOut.
*/
static void idxDatabaseError(
  sqlite3 *db,                    /* Database handle */
  char **pzErrmsg                 /* Write error here */
){
  *pzErrmsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
}

/*
** Prepare an SQL statement.
*/
static int idxPrepareStmt(
  sqlite3 *db,                    /* Database handle to compile against */
  sqlite3_stmt **ppStmt,          /* OUT: Compiled SQL statement */
  char **pzErrmsg,                /* OUT: sqlite3_malloc()ed error message */
  const char *zSql                /* SQL statement to compile */
){
  int rc = sqlite3_prepare_v2(db, zSql, -1, ppStmt, 0);
  if( rc!=SQLITE_OK ){
    *ppStmt = 0;
    idxDatabaseError(db, pzErrmsg);
  }
  return rc;
}

/*
** Prepare an SQL statement using the results of a printf() formatting.
*/
static int idxPrintfPrepareStmt(
  sqlite3 *db,                    /* Database handle to compile against */
  sqlite3_stmt **ppStmt,          /* OUT: Compiled SQL statement */
  char **pzErrmsg,                /* OUT: sqlite3_malloc()ed error message */
  const char *zFmt,               /* printf() format of SQL statement */
  ...                             /* Trailing printf() arguments */
){
  va_list ap;
  int rc;
  char *zSql;
  va_start(ap, zFmt);
  zSql = sqlite3_vmprintf(zFmt, ap);
  if( zSql==0 ){
    rc = SQLITE_NOMEM;
  }else{
    rc = idxPrepareStmt(db, ppStmt, pzErrmsg, zSql);
    sqlite3_free(zSql);
  }
  va_end(ap);
  return rc;
}


/*************************************************************************
** Beginning of virtual table implementation.
*/
typedef struct ExpertVtab ExpertVtab;
struct ExpertVtab {
  sqlite3_vtab base;
  IdxTable *pTab;
  sqlite3expert *pExpert;
};

typedef struct ExpertCsr ExpertCsr;
struct ExpertCsr {
  sqlite3_vtab_cursor base;
  sqlite3_stmt *pData;
};

static char *expertDequote(const char *zIn){
  int n = STRLEN(zIn);
  char *zRet = sqlite3_malloc(n);

  assert( zIn[0]=='\'' );
  assert( zIn[n-1]=='\'' );

  if( zRet ){
    int iOut = 0;
    int iIn = 0;
    for(iIn=1; iIn<(n-1); iIn++){
      if( zIn[iIn]=='\'' ){
        assert( zIn[iIn+1]=='\'' );
        iIn++;
      }
      zRet[iOut++] = zIn[iIn];
    }
    zRet[iOut] = '\0';
  }

  return zRet;
}

/* 
** This function is the implementation of both the xConnect and xCreate
** methods of the r-tree virtual table.
**
**   argv[0]   -> module name
**   argv[1]   -> database name
**   argv[2]   -> table name
**   argv[...] -> column names...
*/
static int expertConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  sqlite3expert *pExpert = (sqlite3expert*)pAux;
  ExpertVtab *p = 0;
  int rc;

  if( argc!=4 ){
    *pzErr = sqlite3_mprintf("internal error!");
    rc = SQLITE_ERROR;
  }else{
    char *zCreateTable = expertDequote(argv[3]);
    if( zCreateTable ){
      rc = sqlite3_declare_vtab(db, zCreateTable);
      if( rc==SQLITE_OK ){
        p = idxMalloc(&rc, sizeof(ExpertVtab));
      }
      if( rc==SQLITE_OK ){
        p->pExpert = pExpert;
        p->pTab = pExpert->pTable;
        assert( sqlite3_stricmp(p->pTab->zName, argv[2])==0 );
      }
      sqlite3_free(zCreateTable);
    }else{
      rc = SQLITE_NOMEM;
    }
  }

  *ppVtab = (sqlite3_vtab*)p;
  return rc;
}

static int expertDisconnect(sqlite3_vtab *pVtab){
  ExpertVtab *p = (ExpertVtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int expertBestIndex(sqlite3_vtab *pVtab, sqlite3_index_info *pIdxInfo){
  ExpertVtab *p = (ExpertVtab*)pVtab;
  int rc = SQLITE_OK;
  int n = 0;
  IdxScan *pScan;
  const int opmask = 
    SQLITE_INDEX_CONSTRAINT_EQ | SQLITE_INDEX_CONSTRAINT_GT |
    SQLITE_INDEX_CONSTRAINT_LT | SQLITE_INDEX_CONSTRAINT_GE |
    SQLITE_INDEX_CONSTRAINT_LE;

  pScan = idxMalloc(&rc, sizeof(IdxScan));
  if( pScan ){
    int i;

    /* Link the new scan object into the list */
    pScan->pTab = p->pTab;
    pScan->pNextScan = p->pExpert->pScan;
    p->pExpert->pScan = pScan;

    /* Add the constraints to the IdxScan object */
    for(i=0; i<pIdxInfo->nConstraint; i++){
      struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
      if( pCons->usable 
       && pCons->iColumn>=0 
       && p->pTab->aCol[pCons->iColumn].iPk==0
       && (pCons->op & opmask) 
      ){
        IdxConstraint *pNew;
        const char *zColl = sqlite3_vtab_collation(pIdxInfo, i);
        pNew = idxNewConstraint(&rc, zColl);
        if( pNew ){
          pNew->iCol = pCons->iColumn;
          if( pCons->op==SQLITE_INDEX_CONSTRAINT_EQ ){
            pNew->pNext = pScan->pEq;
            pScan->pEq = pNew;
          }else{
            pNew->bRange = 1;
            pNew->pNext = pScan->pRange;
            pScan->pRange = pNew;
          }
        }
        n++;
        pIdxInfo->aConstraintUsage[i].argvIndex = n;
      }
    }

    /* Add the ORDER BY to the IdxScan object */
    for(i=pIdxInfo->nOrderBy-1; i>=0; i--){
      int iCol = pIdxInfo->aOrderBy[i].iColumn;
      if( iCol>=0 ){
        IdxConstraint *pNew = idxNewConstraint(&rc, p->pTab->aCol[iCol].zColl);
        if( pNew ){
          pNew->iCol = iCol;
          pNew->bDesc = pIdxInfo->aOrderBy[i].desc;
          pNew->pNext = pScan->pOrder;
          pNew->pLink = pScan->pOrder;
          pScan->pOrder = pNew;
          n++;
        }
      }
    }
  }

  pIdxInfo->estimatedCost = 1000000.0 / (n+1);
  return rc;
}

static int expertUpdate(
  sqlite3_vtab *pVtab, 
  int nData, 
  sqlite3_value **azData, 
  sqlite_int64 *pRowid
){
  (void)pVtab;
  (void)nData;
  (void)azData;
  (void)pRowid;
  return SQLITE_OK;
}

/* 
** Virtual table module xOpen method.
*/
static int expertOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor){
  int rc = SQLITE_OK;
  ExpertCsr *pCsr;
  (void)pVTab;
  pCsr = idxMalloc(&rc, sizeof(ExpertCsr));
  *ppCursor = (sqlite3_vtab_cursor*)pCsr;
  return rc;
}

/* 
** Virtual table module xClose method.
*/
static int expertClose(sqlite3_vtab_cursor *cur){
  ExpertCsr *pCsr = (ExpertCsr*)cur;
  sqlite3_finalize(pCsr->pData);
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/*
** Virtual table module xEof method.
**
** Return non-zero if the cursor does not currently point to a valid 
** record (i.e if the scan has finished), or zero otherwise.
*/
static int expertEof(sqlite3_vtab_cursor *cur){
  ExpertCsr *pCsr = (ExpertCsr*)cur;
  return pCsr->pData==0;
}

/* 
** Virtual table module xNext method.
*/
static int expertNext(sqlite3_vtab_cursor *cur){
  ExpertCsr *pCsr = (ExpertCsr*)cur;
  int rc = SQLITE_OK;

  assert( pCsr->pData );
  rc = sqlite3_step(pCsr->pData);
  if( rc!=SQLITE_ROW ){
    rc = sqlite3_finalize(pCsr->pData);
    pCsr->pData = 0;
  }else{
    rc = SQLITE_OK;
  }

  return rc;
}

/* 
** Virtual table module xRowid method.
*/
static int expertRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  (void)cur;
  *pRowid = 0;
  return SQLITE_OK;
}

/* 
** Virtual table module xColumn method.
*/
static int expertColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i){
  ExpertCsr *pCsr = (ExpertCsr*)cur;
  sqlite3_value *pVal;
  pVal = sqlite3_column_value(pCsr->pData, i);
  if( pVal ){
    sqlite3_result_value(ctx, pVal);
  }
  return SQLITE_OK;
}

/* 
** Virtual table module xFilter method.
*/
static int expertFilter(
  sqlite3_vtab_cursor *cur, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  ExpertCsr *pCsr = (ExpertCsr*)cur;
  ExpertVtab *pVtab = (ExpertVtab*)(cur->pVtab);
  sqlite3expert *pExpert = pVtab->pExpert;
  int rc;

  (void)idxNum;
  (void)idxStr;
  (void)argc;
  (void)argv;
  rc = sqlite3_finalize(pCsr->pData);
  pCsr->pData = 0;
  if( rc==SQLITE_OK ){
    rc = idxPrintfPrepareStmt(pExpert->db, &pCsr->pData, &pVtab->base.zErrMsg,
        "SELECT * FROM main.%Q WHERE sample()", pVtab->pTab->zName
    );
  }

  if( rc==SQLITE_OK ){
    rc = expertNext(cur);
  }
  return rc;
}

static int idxRegisterVtab(sqlite3expert *p){
  static sqlite3_module expertModule = {
    2,                            /* iVersion */
    expertConnect,                /* xCreate - create a table */
    expertConnect,                /* xConnect - connect to an existing table */
    expertBestIndex,              /* xBestIndex - Determine search strategy */
    expertDisconnect,             /* xDisconnect - Disconnect from a table */
    expertDisconnect,             /* xDestroy - Drop a table */
    expertOpen,                   /* xOpen - open a cursor */
    expertClose,                  /* xClose - close a cursor */
    expertFilter,                 /* xFilter - configure scan constraints */
    expertNext,                   /* xNext - advance a cursor */
    expertEof,                    /* xEof */
    expertColumn,                 /* xColumn - read data */
    expertRowid,                  /* xRowid - read data */
    expertUpdate,                 /* xUpdate - write data */
    0,                            /* xBegin - begin transaction */
    0,                            /* xSync - sync transaction */
    0,                            /* xCommit - commit transaction */
    0,                            /* xRollback - rollback transaction */
    0,                            /* xFindFunction - function overloading */
    0,                            /* xRename - rename the table */
    0,                            /* xSavepoint */
    0,                            /* xRelease */
    0,                            /* xRollbackTo */
  };

  return sqlite3_create_module(p->dbv, "expert", &expertModule, (void*)p);
}
/*
** End of virtual table implementation.
*************************************************************************/
/*
** Finalize SQL statement pStmt. If (*pRc) is SQLITE_OK when this function
** is called, set it to the return value of sqlite3_finalize() before
** returning. Otherwise, discard the sqlite3_finalize() return value.
*/
static void idxFinalize(int *pRc, sqlite3_stmt *pStmt){
  int rc = sqlite3_finalize(pStmt);
  if( *pRc==SQLITE_OK ) *pRc = rc;
}

/*
** Attempt to allocate an IdxTable structure corresponding to table zTab
** in the main database of connection db. If successful, set (*ppOut) to
** point to the new object and return SQLITE_OK. Otherwise, return an
** SQLite error code and set (*ppOut) to NULL. In this case *pzErrmsg may be
** set to point to an error string.
**
** It is the responsibility of the caller to eventually free either the
** IdxTable object or error message using sqlite3_free().
*/
static int idxGetTableInfo(
  sqlite3 *db,                    /* Database connection to read details from */
  const char *zTab,               /* Table name */
  IdxTable **ppOut,               /* OUT: New object (if successful) */
  char **pzErrmsg                 /* OUT: Error message (if not) */
){
  sqlite3_stmt *p1 = 0;
  int nCol = 0;
  int nTab = STRLEN(zTab);
  int nByte = sizeof(IdxTable) + nTab + 1;
  IdxTable *pNew = 0;
  int rc, rc2;
  char *pCsr = 0;

  rc = idxPrintfPrepareStmt(db, &p1, pzErrmsg, "PRAGMA table_info=%Q", zTab);
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(p1) ){
    const char *zCol = (const char*)sqlite3_column_text(p1, 1);
    nByte += 1 + STRLEN(zCol);
    rc = sqlite3_table_column_metadata(
        db, "main", zTab, zCol, 0, &zCol, 0, 0, 0
    );
    nByte += 1 + STRLEN(zCol);
    nCol++;
  }
  rc2 = sqlite3_reset(p1);
  if( rc==SQLITE_OK ) rc = rc2;

  nByte += sizeof(IdxColumn) * nCol;
  if( rc==SQLITE_OK ){
    pNew = idxMalloc(&rc, nByte);
  }
  if( rc==SQLITE_OK ){
    pNew->aCol = (IdxColumn*)&pNew[1];
    pNew->nCol = nCol;
    pCsr = (char*)&pNew->aCol[nCol];
  }

  nCol = 0;
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(p1) ){
    const char *zCol = (const char*)sqlite3_column_text(p1, 1);
    int nCopy = STRLEN(zCol) + 1;
    pNew->aCol[nCol].zName = pCsr;
    pNew->aCol[nCol].iPk = sqlite3_column_int(p1, 5);
    memcpy(pCsr, zCol, nCopy);
    pCsr += nCopy;

    rc = sqlite3_table_column_metadata(
        db, "main", zTab, zCol, 0, &zCol, 0, 0, 0
    );
    if( rc==SQLITE_OK ){
      nCopy = STRLEN(zCol) + 1;
      pNew->aCol[nCol].zColl = pCsr;
      memcpy(pCsr, zCol, nCopy);
      pCsr += nCopy;
    }

    nCol++;
  }
  idxFinalize(&rc, p1);

  if( rc!=SQLITE_OK ){
    sqlite3_free(pNew);
    pNew = 0;
  }else{
    pNew->zName = pCsr;
    memcpy(pNew->zName, zTab, nTab+1);
  }

  *ppOut = pNew;
  return rc;
}

/*
** This function is a no-op if *pRc is set to anything other than 
** SQLITE_OK when it is called.
**
** If *pRc is initially set to SQLITE_OK, then the text specified by
** the printf() style arguments is appended to zIn and the result returned
** in a buffer allocated by sqlite3_malloc(). sqlite3_free() is called on
** zIn before returning.
*/
static char *idxAppendText(int *pRc, char *zIn, const char *zFmt, ...){
  va_list ap;
  char *zAppend = 0;
  char *zRet = 0;
  int nIn = zIn ? STRLEN(zIn) : 0;
  int nAppend = 0;
  va_start(ap, zFmt);
  if( *pRc==SQLITE_OK ){
    zAppend = sqlite3_vmprintf(zFmt, ap);
    if( zAppend ){
      nAppend = STRLEN(zAppend);
      zRet = (char*)sqlite3_malloc(nIn + nAppend + 1);
    }
    if( zAppend && zRet ){
      if( nIn ) memcpy(zRet, zIn, nIn);
      memcpy(&zRet[nIn], zAppend, nAppend+1);
    }else{
      sqlite3_free(zRet);
      zRet = 0;
      *pRc = SQLITE_NOMEM;
    }
    sqlite3_free(zAppend);
    sqlite3_free(zIn);
  }
  va_end(ap);
  return zRet;
}

/*
** Return true if zId must be quoted in order to use it as an SQL
** identifier, or false otherwise.
*/
static int idxIdentifierRequiresQuotes(const char *zId){
  int i;
  for(i=0; zId[i]; i++){
    if( !(zId[i]=='_')
     && !(zId[i]>='0' && zId[i]<='9')
     && !(zId[i]>='a' && zId[i]<='z')
     && !(zId[i]>='A' && zId[i]<='Z')
    ){
      return 1;
    }
  }
  return 0;
}

/*
** This function appends an index column definition suitable for constraint
** pCons to the string passed as zIn and returns the result.
*/
static char *idxAppendColDefn(
  int *pRc,                       /* IN/OUT: Error code */
  char *zIn,                      /* Column defn accumulated so far */
  IdxTable *pTab,                 /* Table index will be created on */
  IdxConstraint *pCons
){
  char *zRet = zIn;
  IdxColumn *p = &pTab->aCol[pCons->iCol];
  if( zRet ) zRet = idxAppendText(pRc, zRet, ", ");

  if( idxIdentifierRequiresQuotes(p->zName) ){
    zRet = idxAppendText(pRc, zRet, "%Q", p->zName);
  }else{
    zRet = idxAppendText(pRc, zRet, "%s", p->zName);
  }

  if( sqlite3_stricmp(p->zColl, pCons->zColl) ){
    if( idxIdentifierRequiresQuotes(pCons->zColl) ){
      zRet = idxAppendText(pRc, zRet, " COLLATE %Q", pCons->zColl);
    }else{
      zRet = idxAppendText(pRc, zRet, " COLLATE %s", pCons->zColl);
    }
  }

  if( pCons->bDesc ){
    zRet = idxAppendText(pRc, zRet, " DESC");
  }
  return zRet;
}

/*
** Search database dbm for an index compatible with the one idxCreateFromCons()
** would create from arguments pScan, pEq and pTail. If no error occurs and 
** such an index is found, return non-zero. Or, if no such index is found,
** return zero.
**
** If an error occurs, set *pRc to an SQLite error code and return zero.
*/
static int idxFindCompatible(
  int *pRc,                       /* OUT: Error code */
  sqlite3* dbm,                   /* Database to search */
  IdxScan *pScan,                 /* Scan for table to search for index on */
  IdxConstraint *pEq,             /* List of == constraints */
  IdxConstraint *pTail            /* List of range constraints */
){
  const char *zTbl = pScan->pTab->zName;
  sqlite3_stmt *pIdxList = 0;
  IdxConstraint *pIter;
  int nEq = 0;                    /* Number of elements in pEq */
  int rc;

  /* Count the elements in list pEq */
  for(pIter=pEq; pIter; pIter=pIter->pLink) nEq++;

  rc = idxPrintfPrepareStmt(dbm, &pIdxList, 0, "PRAGMA index_list=%Q", zTbl);
  while( rc==SQLITE_OK && sqlite3_step(pIdxList)==SQLITE_ROW ){
    int bMatch = 1;
    IdxConstraint *pT = pTail;
    sqlite3_stmt *pInfo = 0;
    const char *zIdx = (const char*)sqlite3_column_text(pIdxList, 1);

    /* Zero the IdxConstraint.bFlag values in the pEq list */
    for(pIter=pEq; pIter; pIter=pIter->pLink) pIter->bFlag = 0;

    rc = idxPrintfPrepareStmt(dbm, &pInfo, 0, "PRAGMA index_xInfo=%Q", zIdx);
    while( rc==SQLITE_OK && sqlite3_step(pInfo)==SQLITE_ROW ){
      int iIdx = sqlite3_column_int(pInfo, 0);
      int iCol = sqlite3_column_int(pInfo, 1);
      const char *zColl = (const char*)sqlite3_column_text(pInfo, 4);

      if( iIdx<nEq ){
        for(pIter=pEq; pIter; pIter=pIter->pLink){
          if( pIter->bFlag ) continue;
          if( pIter->iCol!=iCol ) continue;
          if( sqlite3_stricmp(pIter->zColl, zColl) ) continue;
          pIter->bFlag = 1;
          break;
        }
        if( pIter==0 ){
          bMatch = 0;
          break;
        }
      }else{
        if( pT ){
          if( pT->iCol!=iCol || sqlite3_stricmp(pT->zColl, zColl) ){
            bMatch = 0;
            break;
          }
          pT = pT->pLink;
        }
      }
    }
    idxFinalize(&rc, pInfo);

    if( rc==SQLITE_OK && bMatch ){
      sqlite3_finalize(pIdxList);
      return 1;
    }
  }
  idxFinalize(&rc, pIdxList);

  *pRc = rc;
  return 0;
}

static int idxCreateFromCons(
  sqlite3expert *p,
  IdxScan *pScan,
  IdxConstraint *pEq, 
  IdxConstraint *pTail
){
  sqlite3 *dbm = p->dbm;
  int rc = SQLITE_OK;
  if( (pEq || pTail) && 0==idxFindCompatible(&rc, dbm, pScan, pEq, pTail) ){
    IdxTable *pTab = pScan->pTab;
    char *zCols = 0;
    char *zIdx = 0;
    IdxConstraint *pCons;
    unsigned int h = 0;
    const char *zFmt;

    for(pCons=pEq; pCons; pCons=pCons->pLink){
      zCols = idxAppendColDefn(&rc, zCols, pTab, pCons);
    }
    for(pCons=pTail; pCons; pCons=pCons->pLink){
      zCols = idxAppendColDefn(&rc, zCols, pTab, pCons);
    }

    if( rc==SQLITE_OK ){
      /* Hash the list of columns to come up with a name for the index */
      const char *zTable = pScan->pTab->zName;
      char *zName;                /* Index name */
      int i;
      for(i=0; zCols[i]; i++){
        h += ((h<<3) + zCols[i]);
      }
      zName = sqlite3_mprintf("%s_idx_%08x", zTable, h);
      if( zName==0 ){ 
        rc = SQLITE_NOMEM;
      }else{
        if( idxIdentifierRequiresQuotes(zTable) ){
          zFmt = "CREATE INDEX '%q' ON %Q(%s)";
        }else{
          zFmt = "CREATE INDEX %s ON %s(%s)";
        }
        zIdx = sqlite3_mprintf(zFmt, zName, zTable, zCols);
        if( !zIdx ){
          rc = SQLITE_NOMEM;
        }else{
          rc = sqlite3_exec(dbm, zIdx, 0, 0, p->pzErrmsg);
          idxHashAdd(&rc, &p->hIdx, zName, zIdx);
        }
        sqlite3_free(zName);
        sqlite3_free(zIdx);
      }
    }

    sqlite3_free(zCols);
  }
  return rc;
}

/*
** Return true if list pList (linked by IdxConstraint.pLink) contains
** a constraint compatible with *p. Otherwise return false.
*/
static int idxFindConstraint(IdxConstraint *pList, IdxConstraint *p){
  IdxConstraint *pCmp;
  for(pCmp=pList; pCmp; pCmp=pCmp->pLink){
    if( p->iCol==pCmp->iCol ) return 1;
  }
  return 0;
}

static int idxCreateFromWhere(
  sqlite3expert *p, 
  IdxScan *pScan,                 /* Create indexes for this scan */
  IdxConstraint *pTail            /* range/ORDER BY constraints for inclusion */
){
  IdxConstraint *p1 = 0;
  IdxConstraint *pCon;
  int rc;

  /* Gather up all the == constraints. */
  for(pCon=pScan->pEq; pCon; pCon=pCon->pNext){
    if( !idxFindConstraint(p1, pCon) && !idxFindConstraint(pTail, pCon) ){
      pCon->pLink = p1;
      p1 = pCon;
    }
  }

  /* Create an index using the == constraints collected above. And the
  ** range constraint/ORDER BY terms passed in by the caller, if any. */
  rc = idxCreateFromCons(p, pScan, p1, pTail);

  /* If no range/ORDER BY passed by the caller, create a version of the
  ** index for each range constraint.  */
  if( pTail==0 ){
    for(pCon=pScan->pRange; rc==SQLITE_OK && pCon; pCon=pCon->pNext){
      assert( pCon->pLink==0 );
      if( !idxFindConstraint(p1, pCon) && !idxFindConstraint(pTail, pCon) ){
        rc = idxCreateFromCons(p, pScan, p1, pCon);
      }
    }
  }

  return rc;
}

/*
** Create candidate indexes in database [dbm] based on the data in 
** linked-list pScan.
*/
static int idxCreateCandidates(sqlite3expert *p){
  int rc = SQLITE_OK;
  IdxScan *pIter;

  for(pIter=p->pScan; pIter && rc==SQLITE_OK; pIter=pIter->pNextScan){
    rc = idxCreateFromWhere(p, pIter, 0);
    if( rc==SQLITE_OK && pIter->pOrder ){
      rc = idxCreateFromWhere(p, pIter, pIter->pOrder);
    }
  }

  return rc;
}

/*
** Free all elements of the linked list starting at pConstraint.
*/
static void idxConstraintFree(IdxConstraint *pConstraint){
  IdxConstraint *pNext;
  IdxConstraint *p;

  for(p=pConstraint; p; p=pNext){
    pNext = p->pNext;
    sqlite3_free(p);
  }
}

/*
** Free all elements of the linked list starting from pScan up until pLast
** (pLast is not freed).
*/
static void idxScanFree(IdxScan *pScan, IdxScan *pLast){
  IdxScan *p;
  IdxScan *pNext;
  for(p=pScan; p!=pLast; p=pNext){
    pNext = p->pNextScan;
    idxConstraintFree(p->pOrder);
    idxConstraintFree(p->pEq);
    idxConstraintFree(p->pRange);
    sqlite3_free(p);
  }
}

/*
** Free all elements of the linked list starting from pStatement up 
** until pLast (pLast is not freed).
*/
static void idxStatementFree(IdxStatement *pStatement, IdxStatement *pLast){
  IdxStatement *p;
  IdxStatement *pNext;
  for(p=pStatement; p!=pLast; p=pNext){
    pNext = p->pNext;
    sqlite3_free(p->zEQP);
    sqlite3_free(p->zIdx);
    sqlite3_free(p);
  }
}

/*
** Free the linked list of IdxTable objects starting at pTab.
*/
static void idxTableFree(IdxTable *pTab){
  IdxTable *pIter;
  IdxTable *pNext;
  for(pIter=pTab; pIter; pIter=pNext){
    pNext = pIter->pNext;
    sqlite3_free(pIter);
  }
}

/*
** Free the linked list of IdxWrite objects starting at pTab.
*/
static void idxWriteFree(IdxWrite *pTab){
  IdxWrite *pIter;
  IdxWrite *pNext;
  for(pIter=pTab; pIter; pIter=pNext){
    pNext = pIter->pNext;
    sqlite3_free(pIter);
  }
}



/*
** This function is called after candidate indexes have been created. It
** runs all the queries to see which indexes they prefer, and populates
** IdxStatement.zIdx and IdxStatement.zEQP with the results.
*/
int idxFindIndexes(
  sqlite3expert *p,
  char **pzErr                         /* OUT: Error message (sqlite3_malloc) */
){
  IdxStatement *pStmt;
  sqlite3 *dbm = p->dbm;
  int rc = SQLITE_OK;

  IdxHash hIdx;
  idxHashInit(&hIdx);

  for(pStmt=p->pStatement; rc==SQLITE_OK && pStmt; pStmt=pStmt->pNext){
    IdxHashEntry *pEntry;
    sqlite3_stmt *pExplain = 0;
    idxHashClear(&hIdx);
    rc = idxPrintfPrepareStmt(dbm, &pExplain, pzErr,
        "EXPLAIN QUERY PLAN %s", pStmt->zSql
    );
    while( rc==SQLITE_OK && sqlite3_step(pExplain)==SQLITE_ROW ){
      /* int iId = sqlite3_column_int(pExplain, 0); */
      /* int iParent = sqlite3_column_int(pExplain, 1); */
      /* int iNotUsed = sqlite3_column_int(pExplain, 2); */
      const char *zDetail = (const char*)sqlite3_column_text(pExplain, 3);
      int nDetail = STRLEN(zDetail);
      int i;

      for(i=0; i<nDetail; i++){
        const char *zIdx = 0;
        if( memcmp(&zDetail[i], " USING INDEX ", 13)==0 ){
          zIdx = &zDetail[i+13];
        }else if( memcmp(&zDetail[i], " USING COVERING INDEX ", 22)==0 ){
          zIdx = &zDetail[i+22];
        }
        if( zIdx ){
          const char *zSql;
          int nIdx = 0;
          while( zIdx[nIdx]!='\0' && (zIdx[nIdx]!=' ' || zIdx[nIdx+1]!='(') ){
            nIdx++;
          }
          zSql = idxHashSearch(&p->hIdx, zIdx, nIdx);
          if( zSql ){
            idxHashAdd(&rc, &hIdx, zSql, 0);
            if( rc ) goto find_indexes_out;
          }
          break;
        }
      }

      if( zDetail[0]!='-' ){
        pStmt->zEQP = idxAppendText(&rc, pStmt->zEQP, "%s\n", zDetail);
      }
    }

    for(pEntry=hIdx.pFirst; pEntry; pEntry=pEntry->pNext){
      pStmt->zIdx = idxAppendText(&rc, pStmt->zIdx, "%s;\n", pEntry->zKey);
    }

    idxFinalize(&rc, pExplain);
  }

 find_indexes_out:
  idxHashClear(&hIdx);
  return rc;
}

static int idxAuthCallback(
  void *pCtx,
  int eOp,
  const char *z3,
  const char *z4,
  const char *zDb,
  const char *zTrigger
){
  int rc = SQLITE_OK;
  (void)z4;
  (void)zTrigger;
  if( eOp==SQLITE_INSERT || eOp==SQLITE_UPDATE || eOp==SQLITE_DELETE ){
    if( sqlite3_stricmp(zDb, "main")==0 ){
      sqlite3expert *p = (sqlite3expert*)pCtx;
      IdxTable *pTab;
      for(pTab=p->pTable; pTab; pTab=pTab->pNext){
        if( 0==sqlite3_stricmp(z3, pTab->zName) ) break;
      }
      if( pTab ){
        IdxWrite *pWrite;
        for(pWrite=p->pWrite; pWrite; pWrite=pWrite->pNext){
          if( pWrite->pTab==pTab && pWrite->eOp==eOp ) break;
        }
        if( pWrite==0 ){
          pWrite = idxMalloc(&rc, sizeof(IdxWrite));
          if( rc==SQLITE_OK ){
            pWrite->pTab = pTab;
            pWrite->eOp = eOp;
            pWrite->pNext = p->pWrite;
            p->pWrite = pWrite;
          }
        }
      }
    }
  }
  return rc;
}

static int idxProcessOneTrigger(
  sqlite3expert *p, 
  IdxWrite *pWrite, 
  char **pzErr
){
  static const char *zInt = UNIQUE_TABLE_NAME;
  static const char *zDrop = "DROP TABLE " UNIQUE_TABLE_NAME;
  IdxTable *pTab = pWrite->pTab;
  const char *zTab = pTab->zName;
  const char *zSql = 
    "SELECT 'CREATE TEMP' || substr(sql, 7) FROM sqlite_master "
    "WHERE tbl_name = %Q AND type IN ('table', 'trigger') "
    "ORDER BY type;";
  sqlite3_stmt *pSelect = 0;
  int rc = SQLITE_OK;
  char *zWrite = 0;

  /* Create the table and its triggers in the temp schema */
  rc = idxPrintfPrepareStmt(p->db, &pSelect, pzErr, zSql, zTab, zTab);
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSelect) ){
    const char *zCreate = (const char*)sqlite3_column_text(pSelect, 0);
    rc = sqlite3_exec(p->dbv, zCreate, 0, 0, pzErr);
  }
  idxFinalize(&rc, pSelect);

  /* Rename the table in the temp schema to zInt */
  if( rc==SQLITE_OK ){
    char *z = sqlite3_mprintf("ALTER TABLE temp.%Q RENAME TO %Q", zTab, zInt);
    if( z==0 ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_exec(p->dbv, z, 0, 0, pzErr);
      sqlite3_free(z);
    }
  }

  switch( pWrite->eOp ){
    case SQLITE_INSERT: {
      int i;
      zWrite = idxAppendText(&rc, zWrite, "INSERT INTO %Q VALUES(", zInt);
      for(i=0; i<pTab->nCol; i++){
        zWrite = idxAppendText(&rc, zWrite, "%s?", i==0 ? "" : ", ");
      }
      zWrite = idxAppendText(&rc, zWrite, ")");
      break;
    }
    case SQLITE_UPDATE: {
      int i;
      zWrite = idxAppendText(&rc, zWrite, "UPDATE %Q SET ", zInt);
      for(i=0; i<pTab->nCol; i++){
        zWrite = idxAppendText(&rc, zWrite, "%s%Q=?", i==0 ? "" : ", ", 
            pTab->aCol[i].zName
        );
      }
      break;
    }
    default: {
      assert( pWrite->eOp==SQLITE_DELETE );
      if( rc==SQLITE_OK ){
        zWrite = sqlite3_mprintf("DELETE FROM %Q", zInt);
        if( zWrite==0 ) rc = SQLITE_NOMEM;
      }
    }
  }

  if( rc==SQLITE_OK ){
    sqlite3_stmt *pX = 0;
    rc = sqlite3_prepare_v2(p->dbv, zWrite, -1, &pX, 0);
    idxFinalize(&rc, pX);
    if( rc!=SQLITE_OK ){
      idxDatabaseError(p->dbv, pzErr);
    }
  }
  sqlite3_free(zWrite);

  if( rc==SQLITE_OK ){
    rc = sqlite3_exec(p->dbv, zDrop, 0, 0, pzErr);
  }

  return rc;
}

static int idxProcessTriggers(sqlite3expert *p, char **pzErr){
  int rc = SQLITE_OK;
  IdxWrite *pEnd = 0;
  IdxWrite *pFirst = p->pWrite;

  while( rc==SQLITE_OK && pFirst!=pEnd ){
    IdxWrite *pIter;
    for(pIter=pFirst; rc==SQLITE_OK && pIter!=pEnd; pIter=pIter->pNext){
      rc = idxProcessOneTrigger(p, pIter, pzErr);
    }
    pEnd = pFirst;
    pFirst = p->pWrite;
  }

  return rc;
}


static int idxCreateVtabSchema(sqlite3expert *p, char **pzErrmsg){
  int rc = idxRegisterVtab(p);
  sqlite3_stmt *pSchema = 0;

  /* For each table in the main db schema:
  **
  **   1) Add an entry to the p->pTable list, and
  **   2) Create the equivalent virtual table in dbv.
  */
  rc = idxPrepareStmt(p->db, &pSchema, pzErrmsg,
      "SELECT type, name, sql, 1 FROM sqlite_master "
      "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%%' "
      " UNION ALL "
      "SELECT type, name, sql, 2 FROM sqlite_master "
      "WHERE type = 'trigger'"
      "  AND tbl_name IN(SELECT name FROM sqlite_master WHERE type = 'view') "
      "ORDER BY 4, 1"
  );
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSchema) ){
    const char *zType = (const char*)sqlite3_column_text(pSchema, 0);
    const char *zName = (const char*)sqlite3_column_text(pSchema, 1);
    const char *zSql = (const char*)sqlite3_column_text(pSchema, 2);

    if( zType[0]=='v' || zType[1]=='r' ){
      rc = sqlite3_exec(p->dbv, zSql, 0, 0, pzErrmsg);
    }else{
      IdxTable *pTab;
      rc = idxGetTableInfo(p->db, zName, &pTab, pzErrmsg);
      if( rc==SQLITE_OK ){
        int i;
        char *zInner = 0;
        char *zOuter = 0;
        pTab->pNext = p->pTable;
        p->pTable = pTab;

        /* The statement the vtab will pass to sqlite3_declare_vtab() */
        zInner = idxAppendText(&rc, 0, "CREATE TABLE x(");
        for(i=0; i<pTab->nCol; i++){
          zInner = idxAppendText(&rc, zInner, "%s%Q COLLATE %s", 
              (i==0 ? "" : ", "), pTab->aCol[i].zName, pTab->aCol[i].zColl
          );
        }
        zInner = idxAppendText(&rc, zInner, ")");

        /* The CVT statement to create the vtab */
        zOuter = idxAppendText(&rc, 0, 
            "CREATE VIRTUAL TABLE %Q USING expert(%Q)", zName, zInner
        );
        if( rc==SQLITE_OK ){
          rc = sqlite3_exec(p->dbv, zOuter, 0, 0, pzErrmsg);
        }
        sqlite3_free(zInner);
        sqlite3_free(zOuter);
      }
    }
  }
  idxFinalize(&rc, pSchema);
  return rc;
}

struct IdxSampleCtx {
  int iTarget;
  double target;                  /* Target nRet/nRow value */
  double nRow;                    /* Number of rows seen */
  double nRet;                    /* Number of rows returned */
};

static void idxSampleFunc(
  sqlite3_context *pCtx,
  int argc,
  sqlite3_value **argv
){
  struct IdxSampleCtx *p = (struct IdxSampleCtx*)sqlite3_user_data(pCtx);
  int bRet;

  (void)argv;
  assert( argc==0 );
  if( p->nRow==0.0 ){
    bRet = 1;
  }else{
    bRet = (p->nRet / p->nRow) <= p->target;
    if( bRet==0 ){
      unsigned short rnd;
      sqlite3_randomness(2, (void*)&rnd);
      bRet = ((int)rnd % 100) <= p->iTarget;
    }
  }

  sqlite3_result_int(pCtx, bRet);
  p->nRow += 1.0;
  p->nRet += (double)bRet;
}

struct IdxRemCtx {
  int nSlot;
  struct IdxRemSlot {
    int eType;                    /* SQLITE_NULL, INTEGER, REAL, TEXT, BLOB */
    i64 iVal;                     /* SQLITE_INTEGER value */
    double rVal;                  /* SQLITE_FLOAT value */
    int nByte;                    /* Bytes of space allocated at z */
    int n;                        /* Size of buffer z */
    char *z;                      /* SQLITE_TEXT/BLOB value */
  } aSlot[1];
};

/*
** Implementation of scalar function rem().
*/
static void idxRemFunc(
  sqlite3_context *pCtx,
  int argc,
  sqlite3_value **argv
){
  struct IdxRemCtx *p = (struct IdxRemCtx*)sqlite3_user_data(pCtx);
  struct IdxRemSlot *pSlot;
  int iSlot;
  assert( argc==2 );

  iSlot = sqlite3_value_int(argv[0]);
  assert( iSlot<=p->nSlot );
  pSlot = &p->aSlot[iSlot];

  switch( pSlot->eType ){
    case SQLITE_NULL:
      /* no-op */
      break;

    case SQLITE_INTEGER:
      sqlite3_result_int64(pCtx, pSlot->iVal);
      break;

    case SQLITE_FLOAT:
      sqlite3_result_double(pCtx, pSlot->rVal);
      break;

    case SQLITE_BLOB:
      sqlite3_result_blob(pCtx, pSlot->z, pSlot->n, SQLITE_TRANSIENT);
      break;

    case SQLITE_TEXT:
      sqlite3_result_text(pCtx, pSlot->z, pSlot->n, SQLITE_TRANSIENT);
      break;
  }

  pSlot->eType = sqlite3_value_type(argv[1]);
  switch( pSlot->eType ){
    case SQLITE_NULL:
      /* no-op */
      break;

    case SQLITE_INTEGER:
      pSlot->iVal = sqlite3_value_int64(argv[1]);
      break;

    case SQLITE_FLOAT:
      pSlot->rVal = sqlite3_value_double(argv[1]);
      break;

    case SQLITE_BLOB:
    case SQLITE_TEXT: {
      int nByte = sqlite3_value_bytes(argv[1]);
      if( nByte>pSlot->nByte ){
        char *zNew = (char*)sqlite3_realloc(pSlot->z, nByte*2);
        if( zNew==0 ){
          sqlite3_result_error_nomem(pCtx);
          return;
        }
        pSlot->nByte = nByte*2;
        pSlot->z = zNew;
      }
      pSlot->n = nByte;
      if( pSlot->eType==SQLITE_BLOB ){
        memcpy(pSlot->z, sqlite3_value_blob(argv[1]), nByte);
      }else{
        memcpy(pSlot->z, sqlite3_value_text(argv[1]), nByte);
      }
      break;
    }
  }
}

static int idxLargestIndex(sqlite3 *db, int *pnMax, char **pzErr){
  int rc = SQLITE_OK;
  const char *zMax = 
    "SELECT max(i.seqno) FROM "
    "  sqlite_master AS s, "
    "  pragma_index_list(s.name) AS l, "
    "  pragma_index_info(l.name) AS i "
    "WHERE s.type = 'table'";
  sqlite3_stmt *pMax = 0;

  *pnMax = 0;
  rc = idxPrepareStmt(db, &pMax, pzErr, zMax);
  if( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pMax) ){
    *pnMax = sqlite3_column_int(pMax, 0) + 1;
  }
  idxFinalize(&rc, pMax);

  return rc;
}

static int idxPopulateOneStat1(
  sqlite3expert *p,
  sqlite3_stmt *pIndexXInfo,
  sqlite3_stmt *pWriteStat,
  const char *zTab,
  const char *zIdx,
  char **pzErr
){
  char *zCols = 0;
  char *zOrder = 0;
  char *zQuery = 0;
  int nCol = 0;
  int i;
  sqlite3_stmt *pQuery = 0;
  int *aStat = 0;
  int rc = SQLITE_OK;

  assert( p->iSample>0 );

  /* Formulate the query text */
  sqlite3_bind_text(pIndexXInfo, 1, zIdx, -1, SQLITE_STATIC);
  while( SQLITE_OK==rc && SQLITE_ROW==sqlite3_step(pIndexXInfo) ){
    const char *zComma = zCols==0 ? "" : ", ";
    const char *zName = (const char*)sqlite3_column_text(pIndexXInfo, 0);
    const char *zColl = (const char*)sqlite3_column_text(pIndexXInfo, 1);
    zCols = idxAppendText(&rc, zCols, 
        "%sx.%Q IS rem(%d, x.%Q) COLLATE %s", zComma, zName, nCol, zName, zColl
    );
    zOrder = idxAppendText(&rc, zOrder, "%s%d", zComma, ++nCol);
  }
  sqlite3_reset(pIndexXInfo);
  if( rc==SQLITE_OK ){
    if( p->iSample==100 ){
      zQuery = sqlite3_mprintf(
          "SELECT %s FROM %Q x ORDER BY %s", zCols, zTab, zOrder
      );
    }else{
      zQuery = sqlite3_mprintf(
          "SELECT %s FROM temp."UNIQUE_TABLE_NAME" x ORDER BY %s", zCols, zOrder
      );
    }
  }
  sqlite3_free(zCols);
  sqlite3_free(zOrder);

  /* Formulate the query text */
  if( rc==SQLITE_OK ){
    sqlite3 *dbrem = (p->iSample==100 ? p->db : p->dbv);
    rc = idxPrepareStmt(dbrem, &pQuery, pzErr, zQuery);
  }
  sqlite3_free(zQuery);

  if( rc==SQLITE_OK ){
    aStat = (int*)idxMalloc(&rc, sizeof(int)*(nCol+1));
  }
  if( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pQuery) ){
    IdxHashEntry *pEntry;
    char *zStat = 0;
    for(i=0; i<=nCol; i++) aStat[i] = 1;
    while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pQuery) ){
      aStat[0]++;
      for(i=0; i<nCol; i++){
        if( sqlite3_column_int(pQuery, i)==0 ) break;
      }
      for(/*no-op*/; i<nCol; i++){
        aStat[i+1]++;
      }
    }

    if( rc==SQLITE_OK ){
      int s0 = aStat[0];
      zStat = sqlite3_mprintf("%d", s0);
      if( zStat==0 ) rc = SQLITE_NOMEM;
      for(i=1; rc==SQLITE_OK && i<=nCol; i++){
        zStat = idxAppendText(&rc, zStat, " %d", (s0+aStat[i]/2) / aStat[i]);
      }
    }

    if( rc==SQLITE_OK ){
      sqlite3_bind_text(pWriteStat, 1, zTab, -1, SQLITE_STATIC);
      sqlite3_bind_text(pWriteStat, 2, zIdx, -1, SQLITE_STATIC);
      sqlite3_bind_text(pWriteStat, 3, zStat, -1, SQLITE_STATIC);
      sqlite3_step(pWriteStat);
      rc = sqlite3_reset(pWriteStat);
    }

    pEntry = idxHashFind(&p->hIdx, zIdx, STRLEN(zIdx));
    if( pEntry ){
      assert( pEntry->zVal2==0 );
      pEntry->zVal2 = zStat;
    }else{
      sqlite3_free(zStat);
    }
  }
  sqlite3_free(aStat);
  idxFinalize(&rc, pQuery);

  return rc;
}

static int idxBuildSampleTable(sqlite3expert *p, const char *zTab){
  int rc;
  char *zSql;

  rc = sqlite3_exec(p->dbv,"DROP TABLE IF EXISTS temp."UNIQUE_TABLE_NAME,0,0,0);
  if( rc!=SQLITE_OK ) return rc;

  zSql = sqlite3_mprintf(
      "CREATE TABLE temp." UNIQUE_TABLE_NAME " AS SELECT * FROM %Q", zTab
  );
  if( zSql==0 ) return SQLITE_NOMEM;
  rc = sqlite3_exec(p->dbv, zSql, 0, 0, 0);
  sqlite3_free(zSql);

  return rc;
}

/*
** This function is called as part of sqlite3_expert_analyze(). Candidate
** indexes have already been created in database sqlite3expert.dbm, this
** function populates sqlite_stat1 table in the same database.
**
** The stat1 data is generated by querying the 
*/
static int idxPopulateStat1(sqlite3expert *p, char **pzErr){
  int rc = SQLITE_OK;
  int nMax =0;
  struct IdxRemCtx *pCtx = 0;
  struct IdxSampleCtx samplectx; 
  int i;
  i64 iPrev = -100000;
  sqlite3_stmt *pAllIndex = 0;
  sqlite3_stmt *pIndexXInfo = 0;
  sqlite3_stmt *pWrite = 0;

  const char *zAllIndex =
    "SELECT s.rowid, s.name, l.name FROM "
    "  sqlite_master AS s, "
    "  pragma_index_list(s.name) AS l "
    "WHERE s.type = 'table'";
  const char *zIndexXInfo = 
    "SELECT name, coll FROM pragma_index_xinfo(?) WHERE key";
  const char *zWrite = "INSERT INTO sqlite_stat1 VALUES(?, ?, ?)";

  /* If iSample==0, no sqlite_stat1 data is required. */
  if( p->iSample==0 ) return SQLITE_OK;

  rc = idxLargestIndex(p->dbm, &nMax, pzErr);
  if( nMax<=0 || rc!=SQLITE_OK ) return rc;

  rc = sqlite3_exec(p->dbm, "ANALYZE; PRAGMA writable_schema=1", 0, 0, 0);

  if( rc==SQLITE_OK ){
    int nByte = sizeof(struct IdxRemCtx) + (sizeof(struct IdxRemSlot) * nMax);
    pCtx = (struct IdxRemCtx*)idxMalloc(&rc, nByte);
  }

  if( rc==SQLITE_OK ){
    sqlite3 *dbrem = (p->iSample==100 ? p->db : p->dbv);
    rc = sqlite3_create_function(
        dbrem, "rem", 2, SQLITE_UTF8, (void*)pCtx, idxRemFunc, 0, 0
    );
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(
        p->db, "sample", 0, SQLITE_UTF8, (void*)&samplectx, idxSampleFunc, 0, 0
    );
  }

  if( rc==SQLITE_OK ){
    pCtx->nSlot = nMax+1;
    rc = idxPrepareStmt(p->dbm, &pAllIndex, pzErr, zAllIndex);
  }
  if( rc==SQLITE_OK ){
    rc = idxPrepareStmt(p->dbm, &pIndexXInfo, pzErr, zIndexXInfo);
  }
  if( rc==SQLITE_OK ){
    rc = idxPrepareStmt(p->dbm, &pWrite, pzErr, zWrite);
  }

  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pAllIndex) ){
    i64 iRowid = sqlite3_column_int64(pAllIndex, 0);
    const char *zTab = (const char*)sqlite3_column_text(pAllIndex, 1);
    const char *zIdx = (const char*)sqlite3_column_text(pAllIndex, 2);
    if( p->iSample<100 && iPrev!=iRowid ){
      samplectx.target = (double)p->iSample / 100.0;
      samplectx.iTarget = p->iSample;
      samplectx.nRow = 0.0;
      samplectx.nRet = 0.0;
      rc = idxBuildSampleTable(p, zTab);
      if( rc!=SQLITE_OK ) break;
    }
    rc = idxPopulateOneStat1(p, pIndexXInfo, pWrite, zTab, zIdx, pzErr);
    iPrev = iRowid;
  }
  if( rc==SQLITE_OK && p->iSample<100 ){
    rc = sqlite3_exec(p->dbv, 
        "DROP TABLE IF EXISTS temp." UNIQUE_TABLE_NAME, 0,0,0
    );
  }

  idxFinalize(&rc, pAllIndex);
  idxFinalize(&rc, pIndexXInfo);
  idxFinalize(&rc, pWrite);

  for(i=0; i<pCtx->nSlot; i++){
    sqlite3_free(pCtx->aSlot[i].z);
  }
  sqlite3_free(pCtx);

  if( rc==SQLITE_OK ){
    rc = sqlite3_exec(p->dbm, "ANALYZE sqlite_master", 0, 0, 0);
  }

  sqlite3_exec(p->db, "DROP TABLE IF EXISTS temp."UNIQUE_TABLE_NAME,0,0,0);
  return rc;
}

/*
** Allocate a new sqlite3expert object.
*/
sqlite3expert *sqlite3_expert_new(sqlite3 *db, char **pzErrmsg){
  int rc = SQLITE_OK;
  sqlite3expert *pNew;

  pNew = (sqlite3expert*)idxMalloc(&rc, sizeof(sqlite3expert));

  /* Open two in-memory databases to work with. The "vtab database" (dbv)
  ** will contain a virtual table corresponding to each real table in
  ** the user database schema, and a copy of each view. It is used to
  ** collect information regarding the WHERE, ORDER BY and other clauses
  ** of the user's query.
  */
  if( rc==SQLITE_OK ){
    pNew->db = db;
    pNew->iSample = 100;
    rc = sqlite3_open(":memory:", &pNew->dbv);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_open(":memory:", &pNew->dbm);
    if( rc==SQLITE_OK ){
      sqlite3_db_config(pNew->dbm, SQLITE_DBCONFIG_TRIGGER_EQP, 1, (int*)0);
    }
  }
  

  /* Copy the entire schema of database [db] into [dbm]. */
  if( rc==SQLITE_OK ){
    sqlite3_stmt *pSql;
    rc = idxPrintfPrepareStmt(pNew->db, &pSql, pzErrmsg, 
        "SELECT sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%%'"
        " AND sql NOT LIKE 'CREATE VIRTUAL %%'"
    );
    while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSql) ){
      const char *zSql = (const char*)sqlite3_column_text(pSql, 0);
      rc = sqlite3_exec(pNew->dbm, zSql, 0, 0, pzErrmsg);
    }
    idxFinalize(&rc, pSql);
  }

  /* Create the vtab schema */
  if( rc==SQLITE_OK ){
    rc = idxCreateVtabSchema(pNew, pzErrmsg);
  }

  /* Register the auth callback with dbv */
  if( rc==SQLITE_OK ){
    sqlite3_set_authorizer(pNew->dbv, idxAuthCallback, (void*)pNew);
  }

  /* If an error has occurred, free the new object and reutrn NULL. Otherwise,
  ** return the new sqlite3expert handle.  */
  if( rc!=SQLITE_OK ){
    sqlite3_expert_destroy(pNew);
    pNew = 0;
  }
  return pNew;
}

/*
** Configure an sqlite3expert object.
*/
int sqlite3_expert_config(sqlite3expert *p, int op, ...){
  int rc = SQLITE_OK;
  va_list ap;
  va_start(ap, op);
  switch( op ){
    case EXPERT_CONFIG_SAMPLE: {
      int iVal = va_arg(ap, int);
      if( iVal<0 ) iVal = 0;
      if( iVal>100 ) iVal = 100;
      p->iSample = iVal;
      break;
    }
    default:
      rc = SQLITE_NOTFOUND;
      break;
  }

  va_end(ap);
  return rc;
}

/*
** Add an SQL statement to the analysis.
*/
int sqlite3_expert_sql(
  sqlite3expert *p,               /* From sqlite3_expert_new() */
  const char *zSql,               /* SQL statement to add */
  char **pzErr                    /* OUT: Error message (if any) */
){
  IdxScan *pScanOrig = p->pScan;
  IdxStatement *pStmtOrig = p->pStatement;
  int rc = SQLITE_OK;
  const char *zStmt = zSql;

  if( p->bRun ) return SQLITE_MISUSE;

  while( rc==SQLITE_OK && zStmt && zStmt[0] ){
    sqlite3_stmt *pStmt = 0;
    rc = sqlite3_prepare_v2(p->dbv, zStmt, -1, &pStmt, &zStmt);
    if( rc==SQLITE_OK ){
      if( pStmt ){
        IdxStatement *pNew;
        const char *z = sqlite3_sql(pStmt);
        int n = STRLEN(z);
        pNew = (IdxStatement*)idxMalloc(&rc, sizeof(IdxStatement) + n+1);
        if( rc==SQLITE_OK ){
          pNew->zSql = (char*)&pNew[1];
          memcpy(pNew->zSql, z, n+1);
          pNew->pNext = p->pStatement;
          if( p->pStatement ) pNew->iId = p->pStatement->iId+1;
          p->pStatement = pNew;
        }
        sqlite3_finalize(pStmt);
      }
    }else{
      idxDatabaseError(p->dbv, pzErr);
    }
  }

  if( rc!=SQLITE_OK ){
    idxScanFree(p->pScan, pScanOrig);
    idxStatementFree(p->pStatement, pStmtOrig);
    p->pScan = pScanOrig;
    p->pStatement = pStmtOrig;
  }

  return rc;
}

int sqlite3_expert_analyze(sqlite3expert *p, char **pzErr){
  int rc;
  IdxHashEntry *pEntry;

  /* Do trigger processing to collect any extra IdxScan structures */
  rc = idxProcessTriggers(p, pzErr);

  /* Create candidate indexes within the in-memory database file */
  if( rc==SQLITE_OK ){
    rc = idxCreateCandidates(p);
  }

  /* Generate the stat1 data */
  if( rc==SQLITE_OK ){
    rc = idxPopulateStat1(p, pzErr);
  }

  /* Formulate the EXPERT_REPORT_CANDIDATES text */
  for(pEntry=p->hIdx.pFirst; pEntry; pEntry=pEntry->pNext){
    p->zCandidates = idxAppendText(&rc, p->zCandidates, 
        "%s;%s%s\n", pEntry->zVal, 
        pEntry->zVal2 ? " -- stat1: " : "", pEntry->zVal2
    );
  }

  /* Figure out which of the candidate indexes are preferred by the query
  ** planner and report the results to the user.  */
  if( rc==SQLITE_OK ){
    rc = idxFindIndexes(p, pzErr);
  }

  if( rc==SQLITE_OK ){
    p->bRun = 1;
  }
  return rc;
}

/*
** Return the total number of statements that have been added to this
** sqlite3expert using sqlite3_expert_sql().
*/
int sqlite3_expert_count(sqlite3expert *p){
  int nRet = 0;
  if( p->pStatement ) nRet = p->pStatement->iId+1;
  return nRet;
}

/*
** Return a component of the report.
*/
const char *sqlite3_expert_report(sqlite3expert *p, int iStmt, int eReport){
  const char *zRet = 0;
  IdxStatement *pStmt;

  if( p->bRun==0 ) return 0;
  for(pStmt=p->pStatement; pStmt && pStmt->iId!=iStmt; pStmt=pStmt->pNext);
  switch( eReport ){
    case EXPERT_REPORT_SQL:
      if( pStmt ) zRet = pStmt->zSql;
      break;
    case EXPERT_REPORT_INDEXES:
      if( pStmt ) zRet = pStmt->zIdx;
      break;
    case EXPERT_REPORT_PLAN:
      if( pStmt ) zRet = pStmt->zEQP;
      break;
    case EXPERT_REPORT_CANDIDATES:
      zRet = p->zCandidates;
      break;
  }
  return zRet;
}

/*
** Free an sqlite3expert object.
*/
void sqlite3_expert_destroy(sqlite3expert *p){
  if( p ){
    sqlite3_close(p->dbm);
    sqlite3_close(p->dbv);
    idxScanFree(p->pScan, 0);
    idxStatementFree(p->pStatement, 0);
    idxTableFree(p->pTable);
    idxWriteFree(p->pWrite);
    idxHashClear(&p->hIdx);
    sqlite3_free(p->zCandidates);
    sqlite3_free(p);
  }
}

#endif /* ifndef SQLITE_OMIT_VIRTUAL_TABLE */

/************************* End ../ext/expert/sqlite3expert.c ********************/

#if defined(SQLITE_ENABLE_SESSION)
/*
** State information for a single open session
*/
typedef struct OpenSession OpenSession;
struct OpenSession {
  char *zName;             /* Symbolic name for this session */
  int nFilter;             /* Number of xFilter rejection GLOB patterns */
  char **azFilter;         /* Array of xFilter rejection GLOB patterns */
  sqlite3_session *p;      /* The open session */
};
#endif

/*
** Shell output mode information from before ".explain on",
** saved so that it can be restored by ".explain off"
*/
typedef struct SavedModeInfo SavedModeInfo;
struct SavedModeInfo {
  int valid;          /* Is there legit data in here? */
  int mode;           /* Mode prior to ".explain on" */
  int showHeader;     /* The ".header" setting prior to ".explain on" */
  int colWidth[100];  /* Column widths prior to ".explain on" */
};

typedef struct ExpertInfo ExpertInfo;
struct ExpertInfo {
  sqlite3expert *pExpert;
  int bVerbose;
};

/* A single line in the EQP output */
typedef struct EQPGraphRow EQPGraphRow;
struct EQPGraphRow {
  int iEqpId;           /* ID for this row */
  int iParentId;        /* ID of the parent row */
  EQPGraphRow *pNext;   /* Next row in sequence */
  char zText[1];        /* Text to display for this row */
};

/* All EQP output is collected into an instance of the following */
typedef struct EQPGraph EQPGraph;
struct EQPGraph {
  EQPGraphRow *pRow;    /* Linked list of all rows of the EQP output */
  EQPGraphRow *pLast;   /* Last element of the pRow list */
  char zPrefix[100];    /* Graph prefix */
};

/*
** State information about the database connection is contained in an
** instance of the following structure.
*/
typedef struct ShellState ShellState;
struct ShellState {
  sqlite3 *db;           /* The database */
  u8 autoExplain;        /* Automatically turn on .explain mode */
  u8 autoEQP;            /* Run EXPLAIN QUERY PLAN prior to seach SQL stmt */
  u8 autoEQPtest;        /* autoEQP is in test mode */
  u8 statsOn;            /* True to display memory stats before each finalize */
  u8 scanstatsOn;        /* True to display scan stats before each finalize */
  u8 openMode;           /* SHELL_OPEN_NORMAL, _APPENDVFS, or _ZIPFILE */
  u8 doXdgOpen;          /* Invoke start/open/xdg-open in output_reset() */
  u8 nEqpLevel;          /* Depth of the EQP output graph */
  unsigned mEqpLines;    /* Mask of veritical lines in the EQP output graph */
  int outCount;          /* Revert to stdout when reaching zero */
  int cnt;               /* Number of records displayed so far */
  FILE *out;             /* Write results here */
  FILE *traceOut;        /* Output for sqlite3_trace() */
  int nErr;              /* Number of errors seen */
  int mode;              /* An output mode setting */
  int modePrior;         /* Saved mode */
  int cMode;             /* temporary output mode for the current query */
  int normalMode;        /* Output mode before ".explain on" */
  int writableSchema;    /* True if PRAGMA writable_schema=ON */
  int showHeader;        /* True to show column names in List or Column mode */
  int nCheck;            /* Number of ".check" commands run */
  unsigned shellFlgs;    /* Various flags */
  char *zDestTable;      /* Name of destination table when MODE_Insert */
  char *zTempFile;       /* Temporary file that might need deleting */
  char zTestcase[30];    /* Name of current test case */
  char colSeparator[20]; /* Column separator character for several modes */
  char rowSeparator[20]; /* Row separator character for MODE_Ascii */
  char colSepPrior[20];  /* Saved column separator */
  char rowSepPrior[20];  /* Saved row separator */
  int colWidth[100];     /* Requested width of each column when in column mode*/
  int actualWidth[100];  /* Actual width of each column */
  char nullValue[20];    /* The text to print when a NULL comes back from
                         ** the database */
  char outfile[FILENAME_MAX]; /* Filename for *out */
  const char *zDbFilename;    /* name of the database file */
  char *zFreeOnClose;         /* Filename to free when closing */
  const char *zVfs;           /* Name of VFS to use */
  sqlite3_stmt *pStmt;   /* Current statement if any. */
  FILE *pLog;            /* Write log output here */
  int (*gpkgEntryPoint)(sqlite3 *db, const char **pzErrMsg, const sqlite3_api_routines *pThunk);
  int *aiIndent;         /* Array of indents used in MODE_Explain */
  int nIndent;           /* Size of array aiIndent[] */
  int iIndent;           /* Index of current op in aiIndent[] */
  EQPGraph sGraph;       /* Information for the graphical EXPLAIN QUERY PLAN */
#if defined(SQLITE_ENABLE_SESSION)
  int nSession;             /* Number of active sessions */
  OpenSession aSession[4];  /* Array of sessions.  [0] is in focus. */
#endif
  ExpertInfo expert;        /* Valid if previous command was ".expert OPT..." */
};


/* Allowed values for ShellState.autoEQP
*/
#define AUTOEQP_off      0           /* Automatic EXPLAIN QUERY PLAN is off */
#define AUTOEQP_on       1           /* Automatic EQP is on */
#define AUTOEQP_trigger  2           /* On and also show plans for triggers */
#define AUTOEQP_full     3           /* Show full EXPLAIN */

/* Allowed values for ShellState.openMode
*/
#define SHELL_OPEN_UNSPEC     0      /* No open-mode specified */
#define SHELL_OPEN_NORMAL     1      /* Normal database file */
#define SHELL_OPEN_APPENDVFS  2      /* Use appendvfs */
#define SHELL_OPEN_ZIPFILE    3      /* Use the zipfile virtual table */
#define SHELL_OPEN_READONLY   4      /* Open a normal database read-only */

/*
** These are the allowed shellFlgs values
*/
#define SHFLG_Pagecache      0x00000001 /* The --pagecache option is used */
#define SHFLG_Lookaside      0x00000002 /* Lookaside memory is used */
#define SHFLG_Backslash      0x00000004 /* The --backslash option is used */
#define SHFLG_PreserveRowid  0x00000008 /* .dump preserves rowid values */
#define SHFLG_Newlines       0x00000010 /* .dump --newline flag */
#define SHFLG_CountChanges   0x00000020 /* .changes setting */
#define SHFLG_Echo           0x00000040 /* .echo or --echo setting */

/*
** Macros for testing and setting shellFlgs
*/
#define ShellHasFlag(P,X)    (((P)->shellFlgs & (X))!=0)
#define ShellSetFlag(P,X)    ((P)->shellFlgs|=(X))
#define ShellClearFlag(P,X)  ((P)->shellFlgs&=(~(X)))

/*
** These are the allowed modes.
*/
#define MODE_Line     0  /* One column per line.  Blank line between records */
#define MODE_Column   1  /* One record per line in neat columns */
#define MODE_List     2  /* One record per line with a separator */
#define MODE_Semi     3  /* Same as MODE_List but append ";" to each line */
#define MODE_Html     4  /* Generate an XHTML table */
#define MODE_Insert   5  /* Generate SQL "insert" statements */
#define MODE_Quote    6  /* Quote values as for SQL */
#define MODE_Tcl      7  /* Generate ANSI-C or TCL quoted elements */
#define MODE_Csv      8  /* Quote strings, numbers are plain */
#define MODE_Explain  9  /* Like MODE_Column, but do not truncate data */
#define MODE_Ascii   10  /* Use ASCII unit and record separators (0x1F/0x1E) */
#define MODE_Pretty  11  /* Pretty-print schemas */
#define MODE_EQP     12  /* Converts EXPLAIN QUERY PLAN output into a graph */

static const char *modeDescr[] = {
  "line",
  "column",
  "list",
  "semi",
  "html",
  "insert",
  "quote",
  "tcl",
  "csv",
  "explain",
  "ascii",
  "prettyprint",
  "eqp"
};

/*
** These are the column/row/line separators used by the various
** import/export modes.
*/
#define SEP_Column    "|"
#define SEP_Row       "\n"
#define SEP_Tab       "\t"
#define SEP_Space     " "
#define SEP_Comma     ","
#define SEP_CrLf      "\r\n"
#define SEP_Unit      "\x1F"
#define SEP_Record    "\x1E"

/*
** A callback for the sqlite3_log() interface.
*/
static void shellLog(void *pArg, int iErrCode, const char *zMsg){
  ShellState *p = (ShellState*)pArg;
  if( p->pLog==0 ) return;
  utf8_printf(p->pLog, "(%d) %s\n", iErrCode, zMsg);
  fflush(p->pLog);
}

/*
** SQL function:  shell_putsnl(X)
**
** Write the text X to the screen (or whatever output is being directed)
** adding a newline at the end, and then return X.
*/
static void shellPutsFunc(
  sqlite3_context *pCtx,
  int nVal,
  sqlite3_value **apVal
){
  ShellState *p = (ShellState*)sqlite3_user_data(pCtx);
  (void)nVal;
  utf8_printf(p->out, "%s\n", sqlite3_value_text(apVal[0]));
  sqlite3_result_value(pCtx, apVal[0]);
}

/*
** SQL function:   edit(VALUE)
**                 edit(VALUE,EDITOR)
**
** These steps:
**
**     (1) Write VALUE into a temporary file.
**     (2) Run program EDITOR on that temporary file.
**     (3) Read the temporary file back and return its content as the result.
**     (4) Delete the temporary file
**
** If the EDITOR argument is omitted, use the value in the VISUAL
** environment variable.  If still there is no EDITOR, through an error.
**
** Also throw an error if the EDITOR program returns a non-zero exit code.
*/
#ifndef SQLITE_NOHAVE_SYSTEM
static void editFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zEditor;
  char *zTempFile = 0;
  sqlite3 *db;
  char *zCmd = 0;
  int bBin;
  int rc;
  int hasCRNL = 0;
  FILE *f = 0;
  sqlite3_int64 sz;
  sqlite3_int64 x;
  unsigned char *p = 0;

  if( argc==2 ){
    zEditor = (const char*)sqlite3_value_text(argv[1]);
  }else{
    zEditor = getenv("VISUAL");
  }
  if( zEditor==0 ){
    sqlite3_result_error(context, "no editor for edit()", -1);
    return;
  }
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ){
    sqlite3_result_error(context, "NULL input to edit()", -1);
    return;
  }
  db = sqlite3_context_db_handle(context);
  zTempFile = 0;
  sqlite3_file_control(db, 0, SQLITE_FCNTL_TEMPFILENAME, &zTempFile);
  if( zTempFile==0 ){
    sqlite3_uint64 r = 0;
    sqlite3_randomness(sizeof(r), &r);
    zTempFile = sqlite3_mprintf("temp%llx", r);
    if( zTempFile==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
  }
  bBin = sqlite3_value_type(argv[0])==SQLITE_BLOB;
  /* When writing the file to be edited, do \n to \r\n conversions on systems
  ** that want \r\n line endings */
  f = fopen(zTempFile, bBin ? "wb" : "w");
  if( f==0 ){
    sqlite3_result_error(context, "edit() cannot open temp file", -1);
    goto edit_func_end;
  }
  sz = sqlite3_value_bytes(argv[0]);
  if( bBin ){
    x = fwrite(sqlite3_value_blob(argv[0]), 1, sz, f);
  }else{
    const char *z = (const char*)sqlite3_value_text(argv[0]);
    /* Remember whether or not the value originally contained \r\n */
    if( z && strstr(z,"\r\n")!=0 ) hasCRNL = 1;
    x = fwrite(sqlite3_value_text(argv[0]), 1, sz, f);
  }
  fclose(f);
  f = 0;
  if( x!=sz ){
    sqlite3_result_error(context, "edit() could not write the whole file", -1);
    goto edit_func_end;
  }
  zCmd = sqlite3_mprintf("%s \"%s\"", zEditor, zTempFile);
  if( zCmd==0 ){
    sqlite3_result_error_nomem(context);
    goto edit_func_end;
  }
  rc = system(zCmd);
  sqlite3_free(zCmd);
  if( rc ){
    sqlite3_result_error(context, "EDITOR returned non-zero", -1);
    goto edit_func_end;
  }
  f = fopen(zTempFile, "rb");
  if( f==0 ){
    sqlite3_result_error(context,
      "edit() cannot reopen temp file after edit", -1);
    goto edit_func_end;
  }
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  p = sqlite3_malloc64( sz+(bBin==0) );
  if( p==0 ){
    sqlite3_result_error_nomem(context);
    goto edit_func_end;
  }
  x = fread(p, 1, sz, f);
  fclose(f);
  f = 0;
  if( x!=sz ){
    sqlite3_result_error(context, "could not read back the whole file", -1);
    goto edit_func_end;
  }
  if( bBin ){
    sqlite3_result_blob64(context, p, sz, sqlite3_free);
  }else{
    int i, j;
    if( hasCRNL ){
      /* If the original contains \r\n then do no conversions back to \n */
      j = sz;
    }else{
      /* If the file did not originally contain \r\n then convert any new
      ** \r\n back into \n */
      for(i=j=0; i<sz; i++){
        if( p[i]=='\r' && p[i+1]=='\n' ) i++;
        p[j++] = p[i];
      }
      sz = j;
      p[sz] = 0;
    } 
    sqlite3_result_text64(context, (const char*)p, sz,
                          sqlite3_free, SQLITE_UTF8);
  }
  p = 0;

edit_func_end:
  if( f ) fclose(f);
  unlink(zTempFile);
  sqlite3_free(zTempFile);
  sqlite3_free(p);
}
#endif /* SQLITE_NOHAVE_SYSTEM */

/*
** Save or restore the current output mode
*/
static void outputModePush(ShellState *p){
  p->modePrior = p->mode;
  memcpy(p->colSepPrior, p->colSeparator, sizeof(p->colSeparator));
  memcpy(p->rowSepPrior, p->rowSeparator, sizeof(p->rowSeparator));
}
static void outputModePop(ShellState *p){
  p->mode = p->modePrior;
  memcpy(p->colSeparator, p->colSepPrior, sizeof(p->colSeparator));
  memcpy(p->rowSeparator, p->rowSepPrior, sizeof(p->rowSeparator));
}

/*
** Output the given string as a hex-encoded blob (eg. X'1234' )
*/
static void output_hex_blob(FILE *out, const void *pBlob, int nBlob){
  int i;
  char *zBlob = (char *)pBlob;
  raw_printf(out,"X'");
  for(i=0; i<nBlob; i++){ raw_printf(out,"%02x",zBlob[i]&0xff); }
  raw_printf(out,"'");
}

/*
** Find a string that is not found anywhere in z[].  Return a pointer
** to that string.
**
** Try to use zA and zB first.  If both of those are already found in z[]
** then make up some string and store it in the buffer zBuf.
*/
static const char *unused_string(
  const char *z,                    /* Result must not appear anywhere in z */
  const char *zA, const char *zB,   /* Try these first */
  char *zBuf                        /* Space to store a generated string */
){
  unsigned i = 0;
  if( strstr(z, zA)==0 ) return zA;
  if( strstr(z, zB)==0 ) return zB;
  do{
    sqlite3_snprintf(20,zBuf,"(%s%u)", zA, i++);
  }while( strstr(z,zBuf)!=0 );
  return zBuf;
}

/*
** Output the given string as a quoted string using SQL quoting conventions.
**
** See also: output_quoted_escaped_string()
*/
static void output_quoted_string(FILE *out, const char *z){
  int i;
  char c;
  setBinaryMode(out, 1);
  for(i=0; (c = z[i])!=0 && c!='\''; i++){}
  if( c==0 ){
    utf8_printf(out,"'%s'",z);
  }else{
    raw_printf(out, "'");
    while( *z ){
      for(i=0; (c = z[i])!=0 && c!='\''; i++){}
      if( c=='\'' ) i++;
      if( i ){
        utf8_printf(out, "%.*s", i, z);
        z += i;
      }
      if( c=='\'' ){
        raw_printf(out, "'");
        continue;
      }
      if( c==0 ){
        break;
      }
      z++;
    }
    raw_printf(out, "'");
  }
  setTextMode(out, 1);
}

/*
** Output the given string as a quoted string using SQL quoting conventions.
** Additionallly , escape the "\n" and "\r" characters so that they do not
** get corrupted by end-of-line translation facilities in some operating
** systems.
**
** This is like output_quoted_string() but with the addition of the \r\n
** escape mechanism.
*/
static void output_quoted_escaped_string(FILE *out, const char *z){
  int i;
  char c;
  setBinaryMode(out, 1);
  for(i=0; (c = z[i])!=0 && c!='\'' && c!='\n' && c!='\r'; i++){}
  if( c==0 ){
    utf8_printf(out,"'%s'",z);
  }else{
    const char *zNL = 0;
    const char *zCR = 0;
    int nNL = 0;
    int nCR = 0;
    char zBuf1[20], zBuf2[20];
    for(i=0; z[i]; i++){
      if( z[i]=='\n' ) nNL++;
      if( z[i]=='\r' ) nCR++;
    }
    if( nNL ){
      raw_printf(out, "replace(");
      zNL = unused_string(z, "\\n", "\\012", zBuf1);
    }
    if( nCR ){
      raw_printf(out, "replace(");
      zCR = unused_string(z, "\\r", "\\015", zBuf2);
    }
    raw_printf(out, "'");
    while( *z ){
      for(i=0; (c = z[i])!=0 && c!='\n' && c!='\r' && c!='\''; i++){}
      if( c=='\'' ) i++;
      if( i ){
        utf8_printf(out, "%.*s", i, z);
        z += i;
      }
      if( c=='\'' ){
        raw_printf(out, "'");
        continue;
      }
      if( c==0 ){
        break;
      }
      z++;
      if( c=='\n' ){
        raw_printf(out, "%s", zNL);
        continue;
      }
      raw_printf(out, "%s", zCR);
    }
    raw_printf(out, "'");
    if( nCR ){
      raw_printf(out, ",'%s',char(13))", zCR);
    }
    if( nNL ){
      raw_printf(out, ",'%s',char(10))", zNL);
    }
  }
  setTextMode(out, 1);
}

/*
** Output the given string as a quoted according to C or TCL quoting rules.
*/
static void output_c_string(FILE *out, const char *z){
  unsigned int c;
  fputc('"', out);
  while( (c = *(z++))!=0 ){
    if( c=='\\' ){
      fputc(c, out);
      fputc(c, out);
    }else if( c=='"' ){
      fputc('\\', out);
      fputc('"', out);
    }else if( c=='\t' ){
      fputc('\\', out);
      fputc('t', out);
    }else if( c=='\n' ){
      fputc('\\', out);
      fputc('n', out);
    }else if( c=='\r' ){
      fputc('\\', out);
      fputc('r', out);
    }else if( !isprint(c&0xff) ){
      raw_printf(out, "\\%03o", c&0xff);
    }else{
      fputc(c, out);
    }
  }
  fputc('"', out);
}

/*
** Output the given string with characters that are special to
** HTML escaped.
*/
static void output_html_string(FILE *out, const char *z){
  int i;
  if( z==0 ) z = "";
  while( *z ){
    for(i=0;   z[i]
            && z[i]!='<'
            && z[i]!='&'
            && z[i]!='>'
            && z[i]!='\"'
            && z[i]!='\'';
        i++){}
    if( i>0 ){
      utf8_printf(out,"%.*s",i,z);
    }
    if( z[i]=='<' ){
      raw_printf(out,"&lt;");
    }else if( z[i]=='&' ){
      raw_printf(out,"&amp;");
    }else if( z[i]=='>' ){
      raw_printf(out,"&gt;");
    }else if( z[i]=='\"' ){
      raw_printf(out,"&quot;");
    }else if( z[i]=='\'' ){
      raw_printf(out,"&#39;");
    }else{
      break;
    }
    z += i + 1;
  }
}

/*
** If a field contains any character identified by a 1 in the following
** array, then the string must be quoted for CSV.
*/
static const char needCsvQuote[] = {
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 0, 1, 0, 0, 0, 0, 1,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
};

/*
** Output a single term of CSV.  Actually, p->colSeparator is used for
** the separator, which may or may not be a comma.  p->nullValue is
** the null value.  Strings are quoted if necessary.  The separator
** is only issued if bSep is true.
*/
static void output_csv(ShellState *p, const char *z, int bSep){
  FILE *out = p->out;
  if( z==0 ){
    utf8_printf(out,"%s",p->nullValue);
  }else{
    int i;
    int nSep = strlen30(p->colSeparator);
    for(i=0; z[i]; i++){
      if( needCsvQuote[((unsigned char*)z)[i]]
         || (z[i]==p->colSeparator[0] &&
             (nSep==1 || memcmp(z, p->colSeparator, nSep)==0)) ){
        i = 0;
        break;
      }
    }
    if( i==0 ){
      char *zQuoted = sqlite3_mprintf("\"%w\"", z);
      utf8_printf(out, "%s", zQuoted);
      sqlite3_free(zQuoted);
    }else{
      utf8_printf(out, "%s", z);
    }
  }
  if( bSep ){
    utf8_printf(p->out, "%s", p->colSeparator);
  }
}

/*
** This routine runs when the user presses Ctrl-C
*/
static void interrupt_handler(int NotUsed){
  UNUSED_PARAMETER(NotUsed);
  seenInterrupt++;
  if( seenInterrupt>2 ) exit(1);
  if( globalDb ) sqlite3_interrupt(globalDb);
}

#if (defined(_WIN32) || defined(WIN32)) && !defined(_WIN32_WCE)
/*
** This routine runs for console events (e.g. Ctrl-C) on Win32
*/
static BOOL WINAPI ConsoleCtrlHandler(
  DWORD dwCtrlType /* One of the CTRL_*_EVENT constants */
){
  if( dwCtrlType==CTRL_C_EVENT ){
    interrupt_handler(0);
    return TRUE;
  }
  return FALSE;
}
#endif

#ifndef SQLITE_OMIT_AUTHORIZATION
/*
** When the ".auth ON" is set, the following authorizer callback is
** invoked.  It always returns SQLITE_OK.
*/
static int shellAuth(
  void *pClientData,
  int op,
  const char *zA1,
  const char *zA2,
  const char *zA3,
  const char *zA4
){
  ShellState *p = (ShellState*)pClientData;
  static const char *azAction[] = { 0,
     "CREATE_INDEX",         "CREATE_TABLE",         "CREATE_TEMP_INDEX",
     "CREATE_TEMP_TABLE",    "CREATE_TEMP_TRIGGER",  "CREATE_TEMP_VIEW",
     "CREATE_TRIGGER",       "CREATE_VIEW",          "DELETE",
     "DROP_INDEX",           "DROP_TABLE",           "DROP_TEMP_INDEX",
     "DROP_TEMP_TABLE",      "DROP_TEMP_TRIGGER",    "DROP_TEMP_VIEW",
     "DROP_TRIGGER",         "DROP_VIEW",            "INSERT",
     "PRAGMA",               "READ",                 "SELECT",
     "TRANSACTION",          "UPDATE",               "ATTACH",
     "DETACH",               "ALTER_TABLE",          "REINDEX",
     "ANALYZE",              "CREATE_VTABLE",        "DROP_VTABLE",
     "FUNCTION",             "SAVEPOINT",            "RECURSIVE"
  };
  int i;
  const char *az[4];
  az[0] = zA1;
  az[1] = zA2;
  az[2] = zA3;
  az[3] = zA4;
  utf8_printf(p->out, "authorizer: %s", azAction[op]);
  for(i=0; i<4; i++){
    raw_printf(p->out, " ");
    if( az[i] ){
      output_c_string(p->out, az[i]);
    }else{
      raw_printf(p->out, "NULL");
    }
  }
  raw_printf(p->out, "\n");
  return SQLITE_OK;
}
#endif

/*
** Print a schema statement.  Part of MODE_Semi and MODE_Pretty output.
**
** This routine converts some CREATE TABLE statements for shadow tables
** in FTS3/4/5 into CREATE TABLE IF NOT EXISTS statements.
*/
static void printSchemaLine(FILE *out, const char *z, const char *zTail){
  if( sqlite3_strglob("CREATE TABLE ['\"]*", z)==0 ){
    utf8_printf(out, "CREATE TABLE IF NOT EXISTS %s%s", z+13, zTail);
  }else{
    utf8_printf(out, "%s%s", z, zTail);
  }
}
static void printSchemaLineN(FILE *out, char *z, int n, const char *zTail){
  char c = z[n];
  z[n] = 0;
  printSchemaLine(out, z, zTail);
  z[n] = c;
}

/*
** Return true if string z[] has nothing but whitespace and comments to the
** end of the first line.
*/
static int wsToEol(const char *z){
  int i;
  for(i=0; z[i]; i++){
    if( z[i]=='\n' ) return 1;
    if( IsSpace(z[i]) ) continue;
    if( z[i]=='-' && z[i+1]=='-' ) return 1;
    return 0;
  }
  return 1;
}

/*
** Add a new entry to the EXPLAIN QUERY PLAN data
*/
static void eqp_append(ShellState *p, int iEqpId, int p2, const char *zText){
  EQPGraphRow *pNew;
  int nText = strlen30(zText);
  if( p->autoEQPtest ){
    utf8_printf(p->out, "%d,%d,%s\n", iEqpId, p2, zText);
  }
  pNew = sqlite3_malloc64( sizeof(*pNew) + nText );
  if( pNew==0 ) shell_out_of_memory();
  pNew->iEqpId = iEqpId;
  pNew->iParentId = p2;
  memcpy(pNew->zText, zText, nText+1);
  pNew->pNext = 0;
  if( p->sGraph.pLast ){
    p->sGraph.pLast->pNext = pNew;
  }else{
    p->sGraph.pRow = pNew;
  }
  p->sGraph.pLast = pNew;
}

/*
** Free and reset the EXPLAIN QUERY PLAN data that has been collected
** in p->sGraph.
*/
static void eqp_reset(ShellState *p){
  EQPGraphRow *pRow, *pNext;
  for(pRow = p->sGraph.pRow; pRow; pRow = pNext){
    pNext = pRow->pNext;
    sqlite3_free(pRow);
  }
  memset(&p->sGraph, 0, sizeof(p->sGraph));
}

/* Return the next EXPLAIN QUERY PLAN line with iEqpId that occurs after
** pOld, or return the first such line if pOld is NULL
*/
static EQPGraphRow *eqp_next_row(ShellState *p, int iEqpId, EQPGraphRow *pOld){
  EQPGraphRow *pRow = pOld ? pOld->pNext : p->sGraph.pRow;
  while( pRow && pRow->iParentId!=iEqpId ) pRow = pRow->pNext;
  return pRow;
}

/* Render a single level of the graph that has iEqpId as its parent.  Called
** recursively to render sublevels.
*/
static void eqp_render_level(ShellState *p, int iEqpId){
  EQPGraphRow *pRow, *pNext;
  int n = strlen30(p->sGraph.zPrefix);
  char *z;
  for(pRow = eqp_next_row(p, iEqpId, 0); pRow; pRow = pNext){
    pNext = eqp_next_row(p, iEqpId, pRow);
    z = pRow->zText;
    utf8_printf(p->out, "%s%s%s\n", p->sGraph.zPrefix, pNext ? "|--" : "`--", z);
    if( n<(int)sizeof(p->sGraph.zPrefix)-7 ){
      memcpy(&p->sGraph.zPrefix[n], pNext ? "|  " : "   ", 4);
      eqp_render_level(p, pRow->iEqpId);
      p->sGraph.zPrefix[n] = 0;
    }
  }
}

/*
** Display and reset the EXPLAIN QUERY PLAN data
*/
static void eqp_render(ShellState *p){
  EQPGraphRow *pRow = p->sGraph.pRow;
  if( pRow ){
    if( pRow->zText[0]=='-' ){
      if( pRow->pNext==0 ){
        eqp_reset(p);
        return;
      }
      utf8_printf(p->out, "%s\n", pRow->zText+3);
      p->sGraph.pRow = pRow->pNext;
      sqlite3_free(pRow);
    }else{
      utf8_printf(p->out, "QUERY PLAN\n");
    }
    p->sGraph.zPrefix[0] = 0;
    eqp_render_level(p, 0);
    eqp_reset(p);
  }
}

/*
** This is the callback routine that the shell
** invokes for each row of a query result.
*/
static int shell_callback(
  void *pArg,
  int nArg,        /* Number of result columns */
  char **azArg,    /* Text of each result column */
  char **azCol,    /* Column names */
  int *aiType      /* Column types */
){
  int i;
  ShellState *p = (ShellState*)pArg;

  if( azArg==0 ) return 0;
  switch( p->cMode ){
    case MODE_Line: {
      int w = 5;
      if( azArg==0 ) break;
      for(i=0; i<nArg; i++){
        int len = strlen30(azCol[i] ? azCol[i] : "");
        if( len>w ) w = len;
      }
      if( p->cnt++>0 ) utf8_printf(p->out, "%s", p->rowSeparator);
      for(i=0; i<nArg; i++){
        utf8_printf(p->out,"%*s = %s%s", w, azCol[i],
                azArg[i] ? azArg[i] : p->nullValue, p->rowSeparator);
      }
      break;
    }
    case MODE_Explain:
    case MODE_Column: {
      static const int aExplainWidths[] = {4, 13, 4, 4, 4, 13, 2, 13};
      const int *colWidth;
      int showHdr;
      char *rowSep;
      if( p->cMode==MODE_Column ){
        colWidth = p->colWidth;
        showHdr = p->showHeader;
        rowSep = p->rowSeparator;
      }else{
        colWidth = aExplainWidths;
        showHdr = 1;
        rowSep = SEP_Row;
      }
      if( p->cnt++==0 ){
        for(i=0; i<nArg; i++){
          int w, n;
          if( i<ArraySize(p->colWidth) ){
            w = colWidth[i];
          }else{
            w = 0;
          }
          if( w==0 ){
            w = strlenChar(azCol[i] ? azCol[i] : "");
            if( w<10 ) w = 10;
            n = strlenChar(azArg && azArg[i] ? azArg[i] : p->nullValue);
            if( w<n ) w = n;
          }
          if( i<ArraySize(p->actualWidth) ){
            p->actualWidth[i] = w;
          }
          if( showHdr ){
            utf8_width_print(p->out, w, azCol[i]);
            utf8_printf(p->out, "%s", i==nArg-1 ? rowSep : "  ");
          }
        }
        if( showHdr ){
          for(i=0; i<nArg; i++){
            int w;
            if( i<ArraySize(p->actualWidth) ){
               w = p->actualWidth[i];
               if( w<0 ) w = -w;
            }else{
               w = 10;
            }
            utf8_printf(p->out,"%-*.*s%s",w,w,
                   "----------------------------------------------------------"
                   "----------------------------------------------------------",
                    i==nArg-1 ? rowSep : "  ");
          }
        }
      }
      if( azArg==0 ) break;
      for(i=0; i<nArg; i++){
        int w;
        if( i<ArraySize(p->actualWidth) ){
           w = p->actualWidth[i];
        }else{
           w = 10;
        }
        if( p->cMode==MODE_Explain && azArg[i] && strlenChar(azArg[i])>w ){
          w = strlenChar(azArg[i]);
        }
        if( i==1 && p->aiIndent && p->pStmt ){
          if( p->iIndent<p->nIndent ){
            utf8_printf(p->out, "%*.s", p->aiIndent[p->iIndent], "");
          }
          p->iIndent++;
        }
        utf8_width_print(p->out, w, azArg[i] ? azArg[i] : p->nullValue);
        utf8_printf(p->out, "%s", i==nArg-1 ? rowSep : "  ");
      }
      break;
    }
    case MODE_Semi: {   /* .schema and .fullschema output */
      printSchemaLine(p->out, azArg[0], ";\n");
      break;
    }
    case MODE_Pretty: {  /* .schema and .fullschema with --indent */
      char *z;
      int j;
      int nParen = 0;
      char cEnd = 0;
      char c;
      int nLine = 0;
      assert( nArg==1 );
      if( azArg[0]==0 ) break;
      if( sqlite3_strlike("CREATE VIEW%", azArg[0], 0)==0
       || sqlite3_strlike("CREATE TRIG%", azArg[0], 0)==0
      ){
        utf8_printf(p->out, "%s;\n", azArg[0]);
        break;
      }
      z = sqlite3_mprintf("%s", azArg[0]);
      j = 0;
      for(i=0; IsSpace(z[i]); i++){}
      for(; (c = z[i])!=0; i++){
        if( IsSpace(c) ){
          if( z[j-1]=='\r' ) z[j-1] = '\n';
          if( IsSpace(z[j-1]) || z[j-1]=='(' ) continue;
        }else if( (c=='(' || c==')') && j>0 && IsSpace(z[j-1]) ){
          j--;
        }
        z[j++] = c;
      }
      while( j>0 && IsSpace(z[j-1]) ){ j--; }
      z[j] = 0;
      if( strlen30(z)>=79 ){
        for(i=j=0; (c = z[i])!=0; i++){  /* Copy changes from z[i] back to z[j] */
          if( c==cEnd ){
            cEnd = 0;
          }else if( c=='"' || c=='\'' || c=='`' ){
            cEnd = c;
          }else if( c=='[' ){
            cEnd = ']';
          }else if( c=='-' && z[i+1]=='-' ){
            cEnd = '\n';
          }else if( c=='(' ){
            nParen++;
          }else if( c==')' ){
            nParen--;
            if( nLine>0 && nParen==0 && j>0 ){
              printSchemaLineN(p->out, z, j, "\n");
              j = 0;
            }
          }
          z[j++] = c;
          if( nParen==1 && cEnd==0
           && (c=='(' || c=='\n' || (c==',' && !wsToEol(z+i+1)))
          ){
            if( c=='\n' ) j--;
            printSchemaLineN(p->out, z, j, "\n  ");
            j = 0;
            nLine++;
            while( IsSpace(z[i+1]) ){ i++; }
          }
        }
        z[j] = 0;
      }
      printSchemaLine(p->out, z, ";\n");
      sqlite3_free(z);
      break;
    }
    case MODE_List: {
      if( p->cnt++==0 && p->showHeader ){
        for(i=0; i<nArg; i++){
          utf8_printf(p->out,"%s%s",azCol[i],
                  i==nArg-1 ? p->rowSeparator : p->colSeparator);
        }
      }
      if( azArg==0 ) break;
      for(i=0; i<nArg; i++){
        char *z = azArg[i];
        if( z==0 ) z = p->nullValue;
        utf8_printf(p->out, "%s", z);
        if( i<nArg-1 ){
          utf8_printf(p->out, "%s", p->colSeparator);
        }else{
          utf8_printf(p->out, "%s", p->rowSeparator);
        }
      }
      break;
    }
    case MODE_Html: {
      if( p->cnt++==0 && p->showHeader ){
        raw_printf(p->out,"<TR>");
        for(i=0; i<nArg; i++){
          raw_printf(p->out,"<TH>");
          output_html_string(p->out, azCol[i]);
          raw_printf(p->out,"</TH>\n");
        }
        raw_printf(p->out,"</TR>\n");
      }
      if( azArg==0 ) break;
      raw_printf(p->out,"<TR>");
      for(i=0; i<nArg; i++){
        raw_printf(p->out,"<TD>");
        output_html_string(p->out, azArg[i] ? azArg[i] : p->nullValue);
        raw_printf(p->out,"</TD>\n");
      }
      raw_printf(p->out,"</TR>\n");
      break;
    }
    case MODE_Tcl: {
      if( p->cnt++==0 && p->showHeader ){
        for(i=0; i<nArg; i++){
          output_c_string(p->out,azCol[i] ? azCol[i] : "");
          if(i<nArg-1) utf8_printf(p->out, "%s", p->colSeparator);
        }
        utf8_printf(p->out, "%s", p->rowSeparator);
      }
      if( azArg==0 ) break;
      for(i=0; i<nArg; i++){
        output_c_string(p->out, azArg[i] ? azArg[i] : p->nullValue);
        if(i<nArg-1) utf8_printf(p->out, "%s", p->colSeparator);
      }
      utf8_printf(p->out, "%s", p->rowSeparator);
      break;
    }
    case MODE_Csv: {
      setBinaryMode(p->out, 1);
      if( p->cnt++==0 && p->showHeader ){
        for(i=0; i<nArg; i++){
          output_csv(p, azCol[i] ? azCol[i] : "", i<nArg-1);
        }
        utf8_printf(p->out, "%s", p->rowSeparator);
      }
      if( nArg>0 ){
        for(i=0; i<nArg; i++){
          output_csv(p, azArg[i], i<nArg-1);
        }
        utf8_printf(p->out, "%s", p->rowSeparator);
      }
      setTextMode(p->out, 1);
      break;
    }
    case MODE_Insert: {
      if( azArg==0 ) break;
      utf8_printf(p->out,"INSERT INTO %s",p->zDestTable);
      if( p->showHeader ){
        raw_printf(p->out,"(");
        for(i=0; i<nArg; i++){
          if( i>0 ) raw_printf(p->out, ",");
          if( quoteChar(azCol[i]) ){
            char *z = sqlite3_mprintf("\"%w\"", azCol[i]);
            utf8_printf(p->out, "%s", z);
            sqlite3_free(z);
          }else{
            raw_printf(p->out, "%s", azCol[i]);
          }
        }
        raw_printf(p->out,")");
      }
      p->cnt++;
      for(i=0; i<nArg; i++){
        raw_printf(p->out, i>0 ? "," : " VALUES(");
        if( (azArg[i]==0) || (aiType && aiType[i]==SQLITE_NULL) ){
          utf8_printf(p->out,"NULL");
        }else if( aiType && aiType[i]==SQLITE_TEXT ){
          if( ShellHasFlag(p, SHFLG_Newlines) ){
            output_quoted_string(p->out, azArg[i]);
          }else{
            output_quoted_escaped_string(p->out, azArg[i]);
          }
        }else if( aiType && aiType[i]==SQLITE_INTEGER ){
          utf8_printf(p->out,"%s", azArg[i]);
        }else if( aiType && aiType[i]==SQLITE_FLOAT ){
          char z[50];
          double r = sqlite3_column_double(p->pStmt, i);
          sqlite3_uint64 ur;
          memcpy(&ur,&r,sizeof(r));
          if( ur==0x7ff0000000000000LL ){
            raw_printf(p->out, "1e999");
          }else if( ur==0xfff0000000000000LL ){
            raw_printf(p->out, "-1e999");
          }else{
            sqlite3_snprintf(50,z,"%!.20g", r);
            raw_printf(p->out, "%s", z);
          }
        }else if( aiType && aiType[i]==SQLITE_BLOB && p->pStmt ){
          const void *pBlob = sqlite3_column_blob(p->pStmt, i);
          int nBlob = sqlite3_column_bytes(p->pStmt, i);
          output_hex_blob(p->out, pBlob, nBlob);
        }else if( isNumber(azArg[i], 0) ){
          utf8_printf(p->out,"%s", azArg[i]);
        }else if( ShellHasFlag(p, SHFLG_Newlines) ){
          output_quoted_string(p->out, azArg[i]);
        }else{
          output_quoted_escaped_string(p->out, azArg[i]);
        }
      }
      raw_printf(p->out,");\n");
      break;
    }
    case MODE_Quote: {
      if( azArg==0 ) break;
      if( p->cnt==0 && p->showHeader ){
        for(i=0; i<nArg; i++){
          if( i>0 ) raw_printf(p->out, ",");
          output_quoted_string(p->out, azCol[i]);
        }
        raw_printf(p->out,"\n");
      }
      p->cnt++;
      for(i=0; i<nArg; i++){
        if( i>0 ) raw_printf(p->out, ",");
        if( (azArg[i]==0) || (aiType && aiType[i]==SQLITE_NULL) ){
          utf8_printf(p->out,"NULL");
        }else if( aiType && aiType[i]==SQLITE_TEXT ){
          output_quoted_string(p->out, azArg[i]);
        }else if( aiType && aiType[i]==SQLITE_INTEGER ){
          utf8_printf(p->out,"%s", azArg[i]);
        }else if( aiType && aiType[i]==SQLITE_FLOAT ){
          char z[50];
          double r = sqlite3_column_double(p->pStmt, i);
          sqlite3_snprintf(50,z,"%!.20g", r);
          raw_printf(p->out, "%s", z);
        }else if( aiType && aiType[i]==SQLITE_BLOB && p->pStmt ){
          const void *pBlob = sqlite3_column_blob(p->pStmt, i);
          int nBlob = sqlite3_column_bytes(p->pStmt, i);
          output_hex_blob(p->out, pBlob, nBlob);
        }else if( isNumber(azArg[i], 0) ){
          utf8_printf(p->out,"%s", azArg[i]);
        }else{
          output_quoted_string(p->out, azArg[i]);
        }
      }
      raw_printf(p->out,"\n");
      break;
    }
    case MODE_Ascii: {
      if( p->cnt++==0 && p->showHeader ){
        for(i=0; i<nArg; i++){
          if( i>0 ) utf8_printf(p->out, "%s", p->colSeparator);
          utf8_printf(p->out,"%s",azCol[i] ? azCol[i] : "");
        }
        utf8_printf(p->out, "%s", p->rowSeparator);
      }
      if( azArg==0 ) break;
      for(i=0; i<nArg; i++){
        if( i>0 ) utf8_printf(p->out, "%s", p->colSeparator);
        utf8_printf(p->out,"%s",azArg[i] ? azArg[i] : p->nullValue);
      }
      utf8_printf(p->out, "%s", p->rowSeparator);
      break;
    }
    case MODE_EQP: {
      eqp_append(p, atoi(azArg[0]), atoi(azArg[1]), azArg[3]);
      break;
    }
  }
  return 0;
}

/*
** This is the callback routine that the SQLite library
** invokes for each row of a query result.
*/
static int callback(void *pArg, int nArg, char **azArg, char **azCol){
  /* since we don't have type info, call the shell_callback with a NULL value */
  return shell_callback(pArg, nArg, azArg, azCol, NULL);
}

/*
** This is the callback routine from sqlite3_exec() that appends all
** output onto the end of a ShellText object.
*/
static int captureOutputCallback(void *pArg, int nArg, char **azArg, char **az){
  ShellText *p = (ShellText*)pArg;
  int i;
  UNUSED_PARAMETER(az);
  if( azArg==0 ) return 0;
  if( p->n ) appendText(p, "|", 0);
  for(i=0; i<nArg; i++){
    if( i ) appendText(p, ",", 0);
    if( azArg[i] ) appendText(p, azArg[i], 0);
  }
  return 0;
}

/*
** Generate an appropriate SELFTEST table in the main database.
*/
static void createSelftestTable(ShellState *p){
  char *zErrMsg = 0;
  sqlite3_exec(p->db,
    "SAVEPOINT selftest_init;\n"
    "CREATE TABLE IF NOT EXISTS selftest(\n"
    "  tno INTEGER PRIMARY KEY,\n"   /* Test number */
    "  op TEXT,\n"                   /* Operator:  memo run */
    "  cmd TEXT,\n"                  /* Command text */
    "  ans TEXT\n"                   /* Desired answer */
    ");"
    "CREATE TEMP TABLE [_shell$self](op,cmd,ans);\n"
    "INSERT INTO [_shell$self](rowid,op,cmd)\n"
    "  VALUES(coalesce((SELECT (max(tno)+100)/10 FROM selftest),10),\n"
    "         'memo','Tests generated by --init');\n"
    "INSERT INTO [_shell$self]\n"
    "  SELECT 'run',\n"
    "    'SELECT hex(sha3_query(''SELECT type,name,tbl_name,sql "
                                 "FROM sqlite_master ORDER BY 2'',224))',\n"
    "    hex(sha3_query('SELECT type,name,tbl_name,sql "
                          "FROM sqlite_master ORDER BY 2',224));\n"
    "INSERT INTO [_shell$self]\n"
    "  SELECT 'run',"
    "    'SELECT hex(sha3_query(''SELECT * FROM \"' ||"
    "        printf('%w',name) || '\" NOT INDEXED'',224))',\n"
    "    hex(sha3_query(printf('SELECT * FROM \"%w\" NOT INDEXED',name),224))\n"
    "  FROM (\n"
    "    SELECT name FROM sqlite_master\n"
    "     WHERE type='table'\n"
    "       AND name<>'selftest'\n"
    "       AND coalesce(rootpage,0)>0\n"
    "  )\n"
    " ORDER BY name;\n"
    "INSERT INTO [_shell$self]\n"
    "  VALUES('run','PRAGMA integrity_check','ok');\n"
    "INSERT INTO selftest(tno,op,cmd,ans)"
    "  SELECT rowid*10,op,cmd,ans FROM [_shell$self];\n"
    "DROP TABLE [_shell$self];"
    ,0,0,&zErrMsg);
  if( zErrMsg ){
    utf8_printf(stderr, "SELFTEST initialization failure: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
  }
  sqlite3_exec(p->db, "RELEASE selftest_init",0,0,0);
}


/*
** Set the destination table field of the ShellState structure to
** the name of the table given.  Escape any quote characters in the
** table name.
*/
static void set_table_name(ShellState *p, const char *zName){
  int i, n;
  char cQuote;
  char *z;

  if( p->zDestTable ){
    free(p->zDestTable);
    p->zDestTable = 0;
  }
  if( zName==0 ) return;
  cQuote = quoteChar(zName);
  n = strlen30(zName);
  if( cQuote ) n += n+2;
  z = p->zDestTable = malloc( n+1 );
  if( z==0 ) shell_out_of_memory();
  n = 0;
  if( cQuote ) z[n++] = cQuote;
  for(i=0; zName[i]; i++){
    z[n++] = zName[i];
    if( zName[i]==cQuote ) z[n++] = cQuote;
  }
  if( cQuote ) z[n++] = cQuote;
  z[n] = 0;
}


/*
** Execute a query statement that will generate SQL output.  Print
** the result columns, comma-separated, on a line and then add a
** semicolon terminator to the end of that line.
**
** If the number of columns is 1 and that column contains text "--"
** then write the semicolon on a separate line.  That way, if a
** "--" comment occurs at the end of the statement, the comment
** won't consume the semicolon terminator.
*/
static int run_table_dump_query(
  ShellState *p,           /* Query context */
  const char *zSelect,     /* SELECT statement to extract content */
  const char *zFirstRow    /* Print before first row, if not NULL */
){
  sqlite3_stmt *pSelect;
  int rc;
  int nResult;
  int i;
  const char *z;
  rc = sqlite3_prepare_v2(p->db, zSelect, -1, &pSelect, 0);
  if( rc!=SQLITE_OK || !pSelect ){
    utf8_printf(p->out, "/**** ERROR: (%d) %s *****/\n", rc,
                sqlite3_errmsg(p->db));
    if( (rc&0xff)!=SQLITE_CORRUPT ) p->nErr++;
    return rc;
  }
  rc = sqlite3_step(pSelect);
  nResult = sqlite3_column_count(pSelect);
  while( rc==SQLITE_ROW ){
    if( zFirstRow ){
      utf8_printf(p->out, "%s", zFirstRow);
      zFirstRow = 0;
    }
    z = (const char*)sqlite3_column_text(pSelect, 0);
    utf8_printf(p->out, "%s", z);
    for(i=1; i<nResult; i++){
      utf8_printf(p->out, ",%s", sqlite3_column_text(pSelect, i));
    }
    if( z==0 ) z = "";
    while( z[0] && (z[0]!='-' || z[1]!='-') ) z++;
    if( z[0] ){
      raw_printf(p->out, "\n;\n");
    }else{
      raw_printf(p->out, ";\n");
    }
    rc = sqlite3_step(pSelect);
  }
  rc = sqlite3_finalize(pSelect);
  if( rc!=SQLITE_OK ){
    utf8_printf(p->out, "/**** ERROR: (%d) %s *****/\n", rc,
                sqlite3_errmsg(p->db));
    if( (rc&0xff)!=SQLITE_CORRUPT ) p->nErr++;
  }
  return rc;
}

/*
** Allocate space and save off current error string.
*/
static char *save_err_msg(
  sqlite3 *db            /* Database to query */
){
  int nErrMsg = 1+strlen30(sqlite3_errmsg(db));
  char *zErrMsg = sqlite3_malloc64(nErrMsg);
  if( zErrMsg ){
    memcpy(zErrMsg, sqlite3_errmsg(db), nErrMsg);
  }
  return zErrMsg;
}

#ifdef __linux__
/*
** Attempt to display I/O stats on Linux using /proc/PID/io
*/
static void displayLinuxIoStats(FILE *out){
  FILE *in;
  char z[200];
  sqlite3_snprintf(sizeof(z), z, "/proc/%d/io", getpid());
  in = fopen(z, "rb");
  if( in==0 ) return;
  while( fgets(z, sizeof(z), in)!=0 ){
    static const struct {
      const char *zPattern;
      const char *zDesc;
    } aTrans[] = {
      { "rchar: ",                  "Bytes received by read():" },
      { "wchar: ",                  "Bytes sent to write():"    },
      { "syscr: ",                  "Read() system calls:"      },
      { "syscw: ",                  "Write() system calls:"     },
      { "read_bytes: ",             "Bytes read from storage:"  },
      { "write_bytes: ",            "Bytes written to storage:" },
      { "cancelled_write_bytes: ",  "Cancelled write bytes:"    },
    };
    int i;
    for(i=0; i<ArraySize(aTrans); i++){
      int n = strlen30(aTrans[i].zPattern);
      if( strncmp(aTrans[i].zPattern, z, n)==0 ){
        utf8_printf(out, "%-36s %s", aTrans[i].zDesc, &z[n]);
        break;
      }
    }
  }
  fclose(in);
}
#endif

/*
** Display a single line of status using 64-bit values.
*/
static void displayStatLine(
  ShellState *p,            /* The shell context */
  char *zLabel,             /* Label for this one line */
  char *zFormat,            /* Format for the result */
  int iStatusCtrl,          /* Which status to display */
  int bReset                /* True to reset the stats */
){
  sqlite3_int64 iCur = -1;
  sqlite3_int64 iHiwtr = -1;
  int i, nPercent;
  char zLine[200];
  sqlite3_status64(iStatusCtrl, &iCur, &iHiwtr, bReset);
  for(i=0, nPercent=0; zFormat[i]; i++){
    if( zFormat[i]=='%' ) nPercent++;
  }
  if( nPercent>1 ){
    sqlite3_snprintf(sizeof(zLine), zLine, zFormat, iCur, iHiwtr);
  }else{
    sqlite3_snprintf(sizeof(zLine), zLine, zFormat, iHiwtr);
  }
  raw_printf(p->out, "%-36s %s\n", zLabel, zLine);
}

/*
** Display memory stats.
*/
static int display_stats(
  sqlite3 *db,                /* Database to query */
  ShellState *pArg,           /* Pointer to ShellState */
  int bReset                  /* True to reset the stats */
){
  int iCur;
  int iHiwtr;
  FILE *out;
  if( pArg==0 || pArg->out==0 ) return 0;
  out = pArg->out;

  if( pArg->pStmt && (pArg->statsOn & 2) ){
    int nCol, i, x;
    sqlite3_stmt *pStmt = pArg->pStmt;
    char z[100];
    nCol = sqlite3_column_count(pStmt);
    raw_printf(out, "%-36s %d\n", "Number of output columns:", nCol);
    for(i=0; i<nCol; i++){
      sqlite3_snprintf(sizeof(z),z,"Column %d %nname:", i, &x);
      utf8_printf(out, "%-36s %s\n", z, sqlite3_column_name(pStmt,i));
#ifndef SQLITE_OMIT_DECLTYPE
      sqlite3_snprintf(30, z+x, "declared type:");
      utf8_printf(out, "%-36s %s\n", z, sqlite3_column_decltype(pStmt, i));
#endif
#ifdef SQLITE_ENABLE_COLUMN_METADATA
      sqlite3_snprintf(30, z+x, "database name:");
      utf8_printf(out, "%-36s %s\n", z, sqlite3_column_database_name(pStmt,i));
      sqlite3_snprintf(30, z+x, "table name:");
      utf8_printf(out, "%-36s %s\n", z, sqlite3_column_table_name(pStmt,i));
      sqlite3_snprintf(30, z+x, "origin name:");
      utf8_printf(out, "%-36s %s\n", z, sqlite3_column_origin_name(pStmt,i));
#endif
    }
  }

  displayStatLine(pArg, "Memory Used:",
     "%lld (max %lld) bytes", SQLITE_STATUS_MEMORY_USED, bReset);
  displayStatLine(pArg, "Number of Outstanding Allocations:",
     "%lld (max %lld)", SQLITE_STATUS_MALLOC_COUNT, bReset);
  if( pArg->shellFlgs & SHFLG_Pagecache ){
    displayStatLine(pArg, "Number of Pcache Pages Used:",
       "%lld (max %lld) pages", SQLITE_STATUS_PAGECACHE_USED, bReset);
  }
  displayStatLine(pArg, "Number of Pcache Overflow Bytes:",
     "%lld (max %lld) bytes", SQLITE_STATUS_PAGECACHE_OVERFLOW, bReset);
  displayStatLine(pArg, "Largest Allocation:",
     "%lld bytes", SQLITE_STATUS_MALLOC_SIZE, bReset);
  displayStatLine(pArg, "Largest Pcache Allocation:",
     "%lld bytes", SQLITE_STATUS_PAGECACHE_SIZE, bReset);
#ifdef YYTRACKMAXSTACKDEPTH
  displayStatLine(pArg, "Deepest Parser Stack:",
     "%lld (max %lld)", SQLITE_STATUS_PARSER_STACK, bReset);
#endif

  if( db ){
    if( pArg->shellFlgs & SHFLG_Lookaside ){
      iHiwtr = iCur = -1;
      sqlite3_db_status(db, SQLITE_DBSTATUS_LOOKASIDE_USED,
                        &iCur, &iHiwtr, bReset);
      raw_printf(pArg->out,
              "Lookaside Slots Used:                %d (max %d)\n",
              iCur, iHiwtr);
      sqlite3_db_status(db, SQLITE_DBSTATUS_LOOKASIDE_HIT,
                        &iCur, &iHiwtr, bReset);
      raw_printf(pArg->out, "Successful lookaside attempts:       %d\n",
              iHiwtr);
      sqlite3_db_status(db, SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE,
                        &iCur, &iHiwtr, bReset);
      raw_printf(pArg->out, "Lookaside failures due to size:      %d\n",
              iHiwtr);
      sqlite3_db_status(db, SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL,
                        &iCur, &iHiwtr, bReset);
      raw_printf(pArg->out, "Lookaside failures due to OOM:       %d\n",
              iHiwtr);
    }
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_USED, &iCur, &iHiwtr, bReset);
    raw_printf(pArg->out, "Pager Heap Usage:                    %d bytes\n",
            iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_HIT, &iCur, &iHiwtr, 1);
    raw_printf(pArg->out, "Page cache hits:                     %d\n", iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_MISS, &iCur, &iHiwtr, 1);
    raw_printf(pArg->out, "Page cache misses:                   %d\n", iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_WRITE, &iCur, &iHiwtr, 1);
    raw_printf(pArg->out, "Page cache writes:                   %d\n", iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_SPILL, &iCur, &iHiwtr, 1);
    raw_printf(pArg->out, "Page cache spills:                   %d\n", iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_SCHEMA_USED, &iCur, &iHiwtr, bReset);
    raw_printf(pArg->out, "Schema Heap Usage:                   %d bytes\n",
            iCur);
    iHiwtr = iCur = -1;
    sqlite3_db_status(db, SQLITE_DBSTATUS_STMT_USED, &iCur, &iHiwtr, bReset);
    raw_printf(pArg->out, "Statement Heap/Lookaside Usage:      %d bytes\n",
            iCur);
  }

  if( pArg->pStmt ){
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_FULLSCAN_STEP,
                               bReset);
    raw_printf(pArg->out, "Fullscan Steps:                      %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_SORT, bReset);
    raw_printf(pArg->out, "Sort Operations:                     %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_AUTOINDEX,bReset);
    raw_printf(pArg->out, "Autoindex Inserts:                   %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_VM_STEP, bReset);
    raw_printf(pArg->out, "Virtual Machine Steps:               %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_REPREPARE, bReset);
    raw_printf(pArg->out, "Reprepare operations:                %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_RUN, bReset);
    raw_printf(pArg->out, "Number of times run:                 %d\n", iCur);
    iCur = sqlite3_stmt_status(pArg->pStmt, SQLITE_STMTSTATUS_MEMUSED, bReset);
    raw_printf(pArg->out, "Memory used by prepared stmt:        %d\n", iCur);
  }

#ifdef __linux__
  displayLinuxIoStats(pArg->out);
#endif

  /* Do not remove this machine readable comment: extra-stats-output-here */

  return 0;
}

/*
** Display scan stats.
*/
static void display_scanstats(
  sqlite3 *db,                    /* Database to query */
  ShellState *pArg                /* Pointer to ShellState */
){
#ifndef SQLITE_ENABLE_STMT_SCANSTATUS
  UNUSED_PARAMETER(db);
  UNUSED_PARAMETER(pArg);
#else
  int i, k, n, mx;
  raw_printf(pArg->out, "-------- scanstats --------\n");
  mx = 0;
  for(k=0; k<=mx; k++){
    double rEstLoop = 1.0;
    for(i=n=0; 1; i++){
      sqlite3_stmt *p = pArg->pStmt;
      sqlite3_int64 nLoop, nVisit;
      double rEst;
      int iSid;
      const char *zExplain;
      if( sqlite3_stmt_scanstatus(p, i, SQLITE_SCANSTAT_NLOOP, (void*)&nLoop) ){
        break;
      }
      sqlite3_stmt_scanstatus(p, i, SQLITE_SCANSTAT_SELECTID, (void*)&iSid);
      if( iSid>mx ) mx = iSid;
      if( iSid!=k ) continue;
      if( n==0 ){
        rEstLoop = (double)nLoop;
        if( k>0 ) raw_printf(pArg->out, "-------- subquery %d -------\n", k);
      }
      n++;
      sqlite3_stmt_scanstatus(p, i, SQLITE_SCANSTAT_NVISIT, (void*)&nVisit);
      sqlite3_stmt_scanstatus(p, i, SQLITE_SCANSTAT_EST, (void*)&rEst);
      sqlite3_stmt_scanstatus(p, i, SQLITE_SCANSTAT_EXPLAIN, (void*)&zExplain);
      utf8_printf(pArg->out, "Loop %2d: %s\n", n, zExplain);
      rEstLoop *= rEst;
      raw_printf(pArg->out,
          "         nLoop=%-8lld nRow=%-8lld estRow=%-8lld estRow/Loop=%-8g\n",
          nLoop, nVisit, (sqlite3_int64)(rEstLoop+0.5), rEst
      );
    }
  }
  raw_printf(pArg->out, "---------------------------\n");
#endif
}

/*
** Parameter azArray points to a zero-terminated array of strings. zStr
** points to a single nul-terminated string. Return non-zero if zStr
** is equal, according to strcmp(), to any of the strings in the array.
** Otherwise, return zero.
*/
static int str_in_array(const char *zStr, const char **azArray){
  int i;
  for(i=0; azArray[i]; i++){
    if( 0==strcmp(zStr, azArray[i]) ) return 1;
  }
  return 0;
}

/*
** If compiled statement pSql appears to be an EXPLAIN statement, allocate
** and populate the ShellState.aiIndent[] array with the number of
** spaces each opcode should be indented before it is output.
**
** The indenting rules are:
**
**     * For each "Next", "Prev", "VNext" or "VPrev" instruction, indent
**       all opcodes that occur between the p2 jump destination and the opcode
**       itself by 2 spaces.
**
**     * For each "Goto", if the jump destination is earlier in the program
**       and ends on one of:
**          Yield  SeekGt  SeekLt  RowSetRead  Rewind
**       or if the P1 parameter is one instead of zero,
**       then indent all opcodes between the earlier instruction
**       and "Goto" by 2 spaces.
*/
static void explain_data_prepare(ShellState *p, sqlite3_stmt *pSql){
  const char *zSql;               /* The text of the SQL statement */
  const char *z;                  /* Used to check if this is an EXPLAIN */
  int *abYield = 0;               /* True if op is an OP_Yield */
  int nAlloc = 0;                 /* Allocated size of p->aiIndent[], abYield */
  int iOp;                        /* Index of operation in p->aiIndent[] */

  const char *azNext[] = { "Next", "Prev", "VPrev", "VNext", "SorterNext", 0 };
  const char *azYield[] = { "Yield", "SeekLT", "SeekGT", "RowSetRead",
                            "Rewind", 0 };
  const char *azGoto[] = { "Goto", 0 };

  /* Try to figure out if this is really an EXPLAIN statement. If this
  ** cannot be verified, return early.  */
  if( sqlite3_column_count(pSql)!=8 ){
    p->cMode = p->mode;
    return;
  }
  zSql = sqlite3_sql(pSql);
  if( zSql==0 ) return;
  for(z=zSql; *z==' ' || *z=='\t' || *z=='\n' || *z=='\f' || *z=='\r'; z++);
  if( sqlite3_strnicmp(z, "explain", 7) ){
    p->cMode = p->mode;
    return;
  }

  for(iOp=0; SQLITE_ROW==sqlite3_step(pSql); iOp++){
    int i;
    int iAddr = sqlite3_column_int(pSql, 0);
    const char *zOp = (const char*)sqlite3_column_text(pSql, 1);

    /* Set p2 to the P2 field of the current opcode. Then, assuming that
    ** p2 is an instruction address, set variable p2op to the index of that
    ** instruction in the aiIndent[] array. p2 and p2op may be different if
    ** the current instruction is part of a sub-program generated by an
    ** SQL trigger or foreign key.  */
    int p2 = sqlite3_column_int(pSql, 3);
    int p2op = (p2 + (iOp-iAddr));

    /* Grow the p->aiIndent array as required */
    if( iOp>=nAlloc ){
      if( iOp==0 ){
        /* Do further verfication that this is explain output.  Abort if
        ** it is not */
        static const char *explainCols[] = {
           "addr", "opcode", "p1", "p2", "p3", "p4", "p5", "comment" };
        int jj;
        for(jj=0; jj<ArraySize(explainCols); jj++){
          if( strcmp(sqlite3_column_name(pSql,jj),explainCols[jj])!=0 ){
            p->cMode = p->mode;
            sqlite3_reset(pSql);
            return;
          }
        }
      }
      nAlloc += 100;
      p->aiIndent = (int*)sqlite3_realloc64(p->aiIndent, nAlloc*sizeof(int));
      if( p->aiIndent==0 ) shell_out_of_memory();
      abYield = (int*)sqlite3_realloc64(abYield, nAlloc*sizeof(int));
      if( abYield==0 ) shell_out_of_memory();
    }
    abYield[iOp] = str_in_array(zOp, azYield);
    p->aiIndent[iOp] = 0;
    p->nIndent = iOp+1;

    if( str_in_array(zOp, azNext) ){
      for(i=p2op; i<iOp; i++) p->aiIndent[i] += 2;
    }
    if( str_in_array(zOp, azGoto) && p2op<p->nIndent
     && (abYield[p2op] || sqlite3_column_int(pSql, 2))
    ){
      for(i=p2op; i<iOp; i++) p->aiIndent[i] += 2;
    }
  }

  p->iIndent = 0;
  sqlite3_free(abYield);
  sqlite3_reset(pSql);
}

/*
** Free the array allocated by explain_data_prepare().
*/
static void explain_data_delete(ShellState *p){
  sqlite3_free(p->aiIndent);
  p->aiIndent = 0;
  p->nIndent = 0;
  p->iIndent = 0;
}

/*
** Disable and restore .wheretrace and .selecttrace settings.
*/
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_SELECTTRACE)
extern int sqlite3SelectTrace;
static int savedSelectTrace;
#endif
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_WHERETRACE)
extern int sqlite3WhereTrace;
static int savedWhereTrace;
#endif
static void disable_debug_trace_modes(void){
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_SELECTTRACE)
  savedSelectTrace = sqlite3SelectTrace;
  sqlite3SelectTrace = 0;
#endif
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_WHERETRACE)
  savedWhereTrace = sqlite3WhereTrace;
  sqlite3WhereTrace = 0;
#endif
}
static void restore_debug_trace_modes(void){
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_SELECTTRACE)
  sqlite3SelectTrace = savedSelectTrace;
#endif
#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_WHERETRACE)
  sqlite3WhereTrace = savedWhereTrace;
#endif
}

/*
** Run a prepared statement
*/
static void exec_prepared_stmt(
  ShellState *pArg,                                /* Pointer to ShellState */
  sqlite3_stmt *pStmt                              /* Statment to run */
){
  int rc;

  /* perform the first step.  this will tell us if we
  ** have a result set or not and how wide it is.
  */
  rc = sqlite3_step(pStmt);
  /* if we have a result set... */
  if( SQLITE_ROW == rc ){
    /* allocate space for col name ptr, value ptr, and type */
    int nCol = sqlite3_column_count(pStmt);
    void *pData = sqlite3_malloc64(3*nCol*sizeof(const char*) + 1);
    if( !pData ){
      rc = SQLITE_NOMEM;
    }else{
      char **azCols = (char **)pData;      /* Names of result columns */
      char **azVals = &azCols[nCol];       /* Results */
      int *aiTypes = (int *)&azVals[nCol]; /* Result types */
      int i, x;
      assert(sizeof(int) <= sizeof(char *));
      /* save off ptrs to column names */
      for(i=0; i<nCol; i++){
        azCols[i] = (char *)sqlite3_column_name(pStmt, i);
      }
      do{
        /* extract the data and data types */
        for(i=0; i<nCol; i++){
          aiTypes[i] = x = sqlite3_column_type(pStmt, i);
          if( x==SQLITE_BLOB && pArg && pArg->cMode==MODE_Insert ){
            azVals[i] = "";
          }else{
            azVals[i] = (char*)sqlite3_column_text(pStmt, i);
          }
          if( !azVals[i] && (aiTypes[i]!=SQLITE_NULL) ){
            rc = SQLITE_NOMEM;
            break; /* from for */
          }
        } /* end for */

        /* if data and types extracted successfully... */
        if( SQLITE_ROW == rc ){
          /* call the supplied callback with the result row data */
          if( shell_callback(pArg, nCol, azVals, azCols, aiTypes) ){
            rc = SQLITE_ABORT;
          }else{
            rc = sqlite3_step(pStmt);
          }
        }
      } while( SQLITE_ROW == rc );
      sqlite3_free(pData);
    }
  }
}

#ifndef SQLITE_OMIT_VIRTUALTABLE
/*
** This function is called to process SQL if the previous shell command
** was ".expert". It passes the SQL in the second argument directly to
** the sqlite3expert object.
**
** If successful, SQLITE_OK is returned. Otherwise, an SQLite error
** code. In this case, (*pzErr) may be set to point to a buffer containing
** an English language error message. It is the responsibility of the
** caller to eventually free this buffer using sqlite3_free().
*/
static int expertHandleSQL(
  ShellState *pState, 
  const char *zSql, 
  char **pzErr
){
  assert( pState->expert.pExpert );
  assert( pzErr==0 || *pzErr==0 );
  return sqlite3_expert_sql(pState->expert.pExpert, zSql, pzErr);
}

/*
** This function is called either to silently clean up the object
** created by the ".expert" command (if bCancel==1), or to generate a 
** report from it and then clean it up (if bCancel==0).
**
** If successful, SQLITE_OK is returned. Otherwise, an SQLite error
** code. In this case, (*pzErr) may be set to point to a buffer containing
** an English language error message. It is the responsibility of the
** caller to eventually free this buffer using sqlite3_free().
*/
static int expertFinish(
  ShellState *pState,
  int bCancel,
  char **pzErr
){
  int rc = SQLITE_OK;
  sqlite3expert *p = pState->expert.pExpert;
  assert( p );
  assert( bCancel || pzErr==0 || *pzErr==0 );
  if( bCancel==0 ){
    FILE *out = pState->out;
    int bVerbose = pState->expert.bVerbose;

    rc = sqlite3_expert_analyze(p, pzErr);
    if( rc==SQLITE_OK ){
      int nQuery = sqlite3_expert_count(p);
      int i;

      if( bVerbose ){
        const char *zCand = sqlite3_expert_report(p,0,EXPERT_REPORT_CANDIDATES);
        raw_printf(out, "-- Candidates -----------------------------\n");
        raw_printf(out, "%s\n", zCand);
      }
      for(i=0; i<nQuery; i++){
        const char *zSql = sqlite3_expert_report(p, i, EXPERT_REPORT_SQL);
        const char *zIdx = sqlite3_expert_report(p, i, EXPERT_REPORT_INDEXES);
        const char *zEQP = sqlite3_expert_report(p, i, EXPERT_REPORT_PLAN);
        if( zIdx==0 ) zIdx = "(no new indexes)\n";
        if( bVerbose ){
          raw_printf(out, "-- Query %d --------------------------------\n",i+1);
          raw_printf(out, "%s\n\n", zSql);
        }
        raw_printf(out, "%s\n", zIdx);
        raw_printf(out, "%s\n", zEQP);
      }
    }
  }
  sqlite3_expert_destroy(p);
  pState->expert.pExpert = 0;
  return rc;
}

/*
** Implementation of ".expert" dot command.
*/
static int expertDotCommand(
  ShellState *pState,             /* Current shell tool state */
  char **azArg,                   /* Array of arguments passed to dot command */
  int nArg                        /* Number of entries in azArg[] */
){
  int rc = SQLITE_OK;
  char *zErr = 0;
  int i;
  int iSample = 0;

  assert( pState->expert.pExpert==0 );
  memset(&pState->expert, 0, sizeof(ExpertInfo));

  for(i=1; rc==SQLITE_OK && i<nArg; i++){
    char *z = azArg[i];
    int n;
    if( z[0]=='-' && z[1]=='-' ) z++;
    n = strlen30(z);
    if( n>=2 && 0==strncmp(z, "-verbose", n) ){
      pState->expert.bVerbose = 1;
    }
    else if( n>=2 && 0==strncmp(z, "-sample", n) ){
      if( i==(nArg-1) ){
        raw_printf(stderr, "option requires an argument: %s\n", z);
        rc = SQLITE_ERROR;
      }else{
        iSample = (int)integerValue(azArg[++i]);
        if( iSample<0 || iSample>100 ){
          raw_printf(stderr, "value out of range: %s\n", azArg[i]);
          rc = SQLITE_ERROR;
        }
      }
    }
    else{
      raw_printf(stderr, "unknown option: %s\n", z);
      rc = SQLITE_ERROR;
    }
  }

  if( rc==SQLITE_OK ){
    pState->expert.pExpert = sqlite3_expert_new(pState->db, &zErr);
    if( pState->expert.pExpert==0 ){
      raw_printf(stderr, "sqlite3_expert_new: %s\n", zErr);
      rc = SQLITE_ERROR;
    }else{
      sqlite3_expert_config(
          pState->expert.pExpert, EXPERT_CONFIG_SAMPLE, iSample
      );
    }
  }

  return rc;
}
#endif /* ifndef SQLITE_OMIT_VIRTUALTABLE */

/*
** Execute a statement or set of statements.  Print
** any result rows/columns depending on the current mode
** set via the supplied callback.
**
** This is very similar to SQLite's built-in sqlite3_exec()
** function except it takes a slightly different callback
** and callback data argument.
*/
static int shell_exec(
  ShellState *pArg,                         /* Pointer to ShellState */
  const char *zSql,                         /* SQL to be evaluated */
  char **pzErrMsg                           /* Error msg written here */
){
  sqlite3_stmt *pStmt = NULL;     /* Statement to execute. */
  int rc = SQLITE_OK;             /* Return Code */
  int rc2;
  const char *zLeftover;          /* Tail of unprocessed SQL */
  sqlite3 *db = pArg->db;

  if( pzErrMsg ){
    *pzErrMsg = NULL;
  }

#ifndef SQLITE_OMIT_VIRTUALTABLE
  if( pArg->expert.pExpert ){
    rc = expertHandleSQL(pArg, zSql, pzErrMsg);
    return expertFinish(pArg, (rc!=SQLITE_OK), pzErrMsg);
  }
#endif

  while( zSql[0] && (SQLITE_OK == rc) ){
    static const char *zStmtSql;
    rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zLeftover);
    if( SQLITE_OK != rc ){
      if( pzErrMsg ){
        *pzErrMsg = save_err_msg(db);
      }
    }else{
      if( !pStmt ){
        /* this happens for a comment or white-space */
        zSql = zLeftover;
        while( IsSpace(zSql[0]) ) zSql++;
        continue;
      }
      zStmtSql = sqlite3_sql(pStmt);
      if( zStmtSql==0 ) zStmtSql = "";
      while( IsSpace(zStmtSql[0]) ) zStmtSql++;

      /* save off the prepared statment handle and reset row count */
      if( pArg ){
        pArg->pStmt = pStmt;
        pArg->cnt = 0;
      }

      /* echo the sql statement if echo on */
      if( pArg && ShellHasFlag(pArg, SHFLG_Echo) ){
        utf8_printf(pArg->out, "%s\n", zStmtSql ? zStmtSql : zSql);
      }

      /* Show the EXPLAIN QUERY PLAN if .eqp is on */
      if( pArg && pArg->autoEQP && sqlite3_strlike("EXPLAIN%",zStmtSql,0)!=0 ){
        sqlite3_stmt *pExplain;
        char *zEQP;
        int triggerEQP = 0;
        disable_debug_trace_modes();
        sqlite3_db_config(db, SQLITE_DBCONFIG_TRIGGER_EQP, -1, &triggerEQP);
        if( pArg->autoEQP>=AUTOEQP_trigger ){
          sqlite3_db_config(db, SQLITE_DBCONFIG_TRIGGER_EQP, 1, 0);
        }
        zEQP = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", zStmtSql);
        rc = sqlite3_prepare_v2(db, zEQP, -1, &pExplain, 0);
        if( rc==SQLITE_OK ){
          while( sqlite3_step(pExplain)==SQLITE_ROW ){
            const char *zEQPLine = (const char*)sqlite3_column_text(pExplain,3);
            int iEqpId = sqlite3_column_int(pExplain, 0);
            int iParentId = sqlite3_column_int(pExplain, 1);
            if( zEQPLine[0]=='-' ) eqp_render(pArg);
            eqp_append(pArg, iEqpId, iParentId, zEQPLine);
          }
          eqp_render(pArg);
        }
        sqlite3_finalize(pExplain);
        sqlite3_free(zEQP);
        if( pArg->autoEQP>=AUTOEQP_full ){
          /* Also do an EXPLAIN for ".eqp full" mode */
          zEQP = sqlite3_mprintf("EXPLAIN %s", zStmtSql);
          rc = sqlite3_prepare_v2(db, zEQP, -1, &pExplain, 0);
          if( rc==SQLITE_OK ){
            pArg->cMode = MODE_Explain;
            explain_data_prepare(pArg, pExplain);
            exec_prepared_stmt(pArg, pExplain);
            explain_data_delete(pArg);
          }
          sqlite3_finalize(pExplain);
          sqlite3_free(zEQP);
        }
        if( pArg->autoEQP>=AUTOEQP_trigger && triggerEQP==0 ){
          sqlite3_db_config(db, SQLITE_DBCONFIG_TRIGGER_EQP, 0, 0);
          /* Reprepare pStmt before reactiving trace modes */
          sqlite3_finalize(pStmt);
          sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
          if( pArg ) pArg->pStmt = pStmt;
        }
        restore_debug_trace_modes();
      }

      if( pArg ){
        pArg->cMode = pArg->mode;
        if( pArg->autoExplain ){
          if( sqlite3_column_count(pStmt)==8
           && sqlite3_strlike("EXPLAIN%", zStmtSql,0)==0
          ){
            pArg->cMode = MODE_Explain;
          }
          if( sqlite3_column_count(pStmt)==4
           && sqlite3_strlike("EXPLAIN QUERY PLAN%", zStmtSql,0)==0 ){
            pArg->cMode = MODE_EQP;
          }
        }

        /* If the shell is currently in ".explain" mode, gather the extra
        ** data required to add indents to the output.*/
        if( pArg->cMode==MODE_Explain ){
          explain_data_prepare(pArg, pStmt);
        }
      }

      exec_prepared_stmt(pArg, pStmt);
      explain_data_delete(pArg);
      eqp_render(pArg);

      /* print usage stats if stats on */
      if( pArg && pArg->statsOn ){
        display_stats(db, pArg, 0);
      }

      /* print loop-counters if required */
      if( pArg && pArg->scanstatsOn ){
        display_scanstats(db, pArg);
      }

      /* Finalize the statement just executed. If this fails, save a
      ** copy of the error message. Otherwise, set zSql to point to the
      ** next statement to execute. */
      rc2 = sqlite3_finalize(pStmt);
      if( rc!=SQLITE_NOMEM ) rc = rc2;
      if( rc==SQLITE_OK ){
        zSql = zLeftover;
        while( IsSpace(zSql[0]) ) zSql++;
      }else if( pzErrMsg ){
        *pzErrMsg = save_err_msg(db);
      }

      /* clear saved stmt handle */
      if( pArg ){
        pArg->pStmt = NULL;
      }
    }
  } /* end while */

  return rc;
}

/*
** Release memory previously allocated by tableColumnList().
*/
static void freeColumnList(char **azCol){
  int i;
  for(i=1; azCol[i]; i++){
    sqlite3_free(azCol[i]);
  }
  /* azCol[0] is a static string */
  sqlite3_free(azCol);
}

/*
** Return a list of pointers to strings which are the names of all
** columns in table zTab.   The memory to hold the names is dynamically
** allocated and must be released by the caller using a subsequent call
** to freeColumnList().
**
** The azCol[0] entry is usually NULL.  However, if zTab contains a rowid
** value that needs to be preserved, then azCol[0] is filled in with the
** name of the rowid column.
**
** The first regular column in the table is azCol[1].  The list is terminated
** by an entry with azCol[i]==0.
*/
static char **tableColumnList(ShellState *p, const char *zTab){
  char **azCol = 0;
  sqlite3_stmt *pStmt;
  char *zSql;
  int nCol = 0;
  int nAlloc = 0;
  int nPK = 0;       /* Number of PRIMARY KEY columns seen */
  int isIPK = 0;     /* True if one PRIMARY KEY column of type INTEGER */
  int preserveRowid = ShellHasFlag(p, SHFLG_PreserveRowid);
  int rc;

  zSql = sqlite3_mprintf("PRAGMA table_info=%Q", zTab);
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
  sqlite3_free(zSql);
  if( rc ) return 0;
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    if( nCol>=nAlloc-2 ){
      nAlloc = nAlloc*2 + nCol + 10;
      azCol = sqlite3_realloc(azCol, nAlloc*sizeof(azCol[0]));
      if( azCol==0 ) shell_out_of_memory();
    }
    azCol[++nCol] = sqlite3_mprintf("%s", sqlite3_column_text(pStmt, 1));
    if( sqlite3_column_int(pStmt, 5) ){
      nPK++;
      if( nPK==1
       && sqlite3_stricmp((const char*)sqlite3_column_text(pStmt,2),
                          "INTEGER")==0
      ){
        isIPK = 1;
      }else{
        isIPK = 0;
      }
    }
  }
  sqlite3_finalize(pStmt);
  if( azCol==0 ) return 0;
  azCol[0] = 0;
  azCol[nCol+1] = 0;

  /* The decision of whether or not a rowid really needs to be preserved
  ** is tricky.  We never need to preserve a rowid for a WITHOUT ROWID table
  ** or a table with an INTEGER PRIMARY KEY.  We are unable to preserve
  ** rowids on tables where the rowid is inaccessible because there are other
  ** columns in the table named "rowid", "_rowid_", and "oid".
  */
  if( preserveRowid && isIPK ){
    /* If a single PRIMARY KEY column with type INTEGER was seen, then it
    ** might be an alise for the ROWID.  But it might also be a WITHOUT ROWID
    ** table or a INTEGER PRIMARY KEY DESC column, neither of which are
    ** ROWID aliases.  To distinguish these cases, check to see if
    ** there is a "pk" entry in "PRAGMA index_list".  There will be
    ** no "pk" index if the PRIMARY KEY really is an alias for the ROWID.
    */
    zSql = sqlite3_mprintf("SELECT 1 FROM pragma_index_list(%Q)"
                           " WHERE origin='pk'", zTab);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
    if( rc ){
      freeColumnList(azCol);
      return 0;
    }
    rc = sqlite3_step(pStmt);
    sqlite3_finalize(pStmt);
    preserveRowid = rc==SQLITE_ROW;
  }
  if( preserveRowid ){
    /* Only preserve the rowid if we can find a name to use for the
    ** rowid */
    static char *azRowid[] = { "rowid", "_rowid_", "oid" };
    int i, j;
    for(j=0; j<3; j++){
      for(i=1; i<=nCol; i++){
        if( sqlite3_stricmp(azRowid[j],azCol[i])==0 ) break;
      }
      if( i>nCol ){
        /* At this point, we know that azRowid[j] is not the name of any
        ** ordinary column in the table.  Verify that azRowid[j] is a valid
        ** name for the rowid before adding it to azCol[0].  WITHOUT ROWID
        ** tables will fail this last check */
        rc = sqlite3_table_column_metadata(p->db,0,zTab,azRowid[j],0,0,0,0,0);
        if( rc==SQLITE_OK ) azCol[0] = azRowid[j];
        break;
      }
    }
  }
  return azCol;
}

/*
** Toggle the reverse_unordered_selects setting.
*/
static void toggleSelectOrder(sqlite3 *db){
  sqlite3_stmt *pStmt = 0;
  int iSetting = 0;
  char zStmt[100];
  sqlite3_prepare_v2(db, "PRAGMA reverse_unordered_selects", -1, &pStmt, 0);
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    iSetting = sqlite3_column_int(pStmt, 0);
  }
  sqlite3_finalize(pStmt);
  sqlite3_snprintf(sizeof(zStmt), zStmt,
       "PRAGMA reverse_unordered_selects(%d)", !iSetting);
  sqlite3_exec(db, zStmt, 0, 0, 0);
}

/*
** This is a different callback routine used for dumping the database.
** Each row received by this callback consists of a table name,
** the table type ("index" or "table") and SQL to create the table.
** This routine should print text sufficient to recreate the table.
*/
static int dump_callback(void *pArg, int nArg, char **azArg, char **azNotUsed){
  int rc;
  const char *zTable;
  const char *zType;
  const char *zSql;
  ShellState *p = (ShellState *)pArg;

  UNUSED_PARAMETER(azNotUsed);
  if( nArg!=3 || azArg==0 ) return 0;
  zTable = azArg[0];
  zType = azArg[1];
  zSql = azArg[2];

  if( strcmp(zTable, "sqlite_sequence")==0 ){
    raw_printf(p->out, "DELETE FROM sqlite_sequence;\n");
  }else if( sqlite3_strglob("sqlite_stat?", zTable)==0 ){
    raw_printf(p->out, "ANALYZE sqlite_master;\n");
  }else if( strncmp(zTable, "sqlite_", 7)==0 ){
    return 0;
  }else if( strncmp(zSql, "CREATE VIRTUAL TABLE", 20)==0 ){
    char *zIns;
    if( !p->writableSchema ){
      raw_printf(p->out, "PRAGMA writable_schema=ON;\n");
      p->writableSchema = 1;
    }
    zIns = sqlite3_mprintf(
       "INSERT INTO sqlite_master(type,name,tbl_name,rootpage,sql)"
       "VALUES('table','%q','%q',0,'%q');",
       zTable, zTable, zSql);
    utf8_printf(p->out, "%s\n", zIns);
    sqlite3_free(zIns);
    return 0;
  }else{
    printSchemaLine(p->out, zSql, ";\n");
  }

  if( strcmp(zType, "table")==0 ){
    ShellText sSelect;
    ShellText sTable;
    char **azCol;
    int i;
    char *savedDestTable;
    int savedMode;

    azCol = tableColumnList(p, zTable);
    if( azCol==0 ){
      p->nErr++;
      return 0;
    }

    /* Always quote the table name, even if it appears to be pure ascii,
    ** in case it is a keyword. Ex:  INSERT INTO "table" ... */
    initText(&sTable);
    appendText(&sTable, zTable, quoteChar(zTable));
    /* If preserving the rowid, add a column list after the table name.
    ** In other words:  "INSERT INTO tab(rowid,a,b,c,...) VALUES(...)"
    ** instead of the usual "INSERT INTO tab VALUES(...)".
    */
    if( azCol[0] ){
      appendText(&sTable, "(", 0);
      appendText(&sTable, azCol[0], 0);
      for(i=1; azCol[i]; i++){
        appendText(&sTable, ",", 0);
        appendText(&sTable, azCol[i], quoteChar(azCol[i]));
      }
      appendText(&sTable, ")", 0);
    }

    /* Build an appropriate SELECT statement */
    initText(&sSelect);
    appendText(&sSelect, "SELECT ", 0);
    if( azCol[0] ){
      appendText(&sSelect, azCol[0], 0);
      appendText(&sSelect, ",", 0);
    }
    for(i=1; azCol[i]; i++){
      appendText(&sSelect, azCol[i], quoteChar(azCol[i]));
      if( azCol[i+1] ){
        appendText(&sSelect, ",", 0);
      }
    }
    freeColumnList(azCol);
    appendText(&sSelect, " FROM ", 0);
    appendText(&sSelect, zTable, quoteChar(zTable));

    savedDestTable = p->zDestTable;
    savedMode = p->mode;
    p->zDestTable = sTable.z;
    p->mode = p->cMode = MODE_Insert;
    rc = shell_exec(p, sSelect.z, 0);
    if( (rc&0xff)==SQLITE_CORRUPT ){
      raw_printf(p->out, "/****** CORRUPTION ERROR *******/\n");
      toggleSelectOrder(p->db);
      shell_exec(p, sSelect.z, 0);
      toggleSelectOrder(p->db);
    }
    p->zDestTable = savedDestTable;
    p->mode = savedMode;
    freeText(&sTable);
    freeText(&sSelect);
    if( rc ) p->nErr++;
  }
  return 0;
}

/*
** Run zQuery.  Use dump_callback() as the callback routine so that
** the contents of the query are output as SQL statements.
**
** If we get a SQLITE_CORRUPT error, rerun the query after appending
** "ORDER BY rowid DESC" to the end.
*/
static int run_schema_dump_query(
  ShellState *p,
  const char *zQuery
){
  int rc;
  char *zErr = 0;
  rc = sqlite3_exec(p->db, zQuery, dump_callback, p, &zErr);
  if( rc==SQLITE_CORRUPT ){
    char *zQ2;
    int len = strlen30(zQuery);
    raw_printf(p->out, "/****** CORRUPTION ERROR *******/\n");
    if( zErr ){
      utf8_printf(p->out, "/****** %s ******/\n", zErr);
      sqlite3_free(zErr);
      zErr = 0;
    }
    zQ2 = malloc( len+100 );
    if( zQ2==0 ) return rc;
    sqlite3_snprintf(len+100, zQ2, "%s ORDER BY rowid DESC", zQuery);
    rc = sqlite3_exec(p->db, zQ2, dump_callback, p, &zErr);
    if( rc ){
      utf8_printf(p->out, "/****** ERROR: %s ******/\n", zErr);
    }else{
      rc = SQLITE_CORRUPT;
    }
    sqlite3_free(zErr);
    free(zQ2);
  }
  return rc;
}

/*
** Text of a help message
*/
static char zHelp[] =
#if defined(SQLITE_HAVE_ZLIB) && !defined(SQLITE_OMIT_VIRTUALTABLE)
  ".archive ...           Manage SQL archives: \".archive --help\" for details\n"
#endif
#ifndef SQLITE_OMIT_AUTHORIZATION
  ".auth ON|OFF           Show authorizer callbacks\n"
#endif
  ".backup ?DB? FILE      Backup DB (default \"main\") to FILE\n"
  "                         Add \"--append\" to open using appendvfs.\n"
  ".bail on|off           Stop after hitting an error.  Default OFF\n"
  ".binary on|off         Turn binary output on or off.  Default OFF\n"
  ".cd DIRECTORY          Change the working directory to DIRECTORY\n"
  ".changes on|off        Show number of rows changed by SQL\n"
  ".check GLOB            Fail if output since .testcase does not match\n"
  ".clone NEWDB           Clone data into NEWDB from the existing database\n"
  ".databases             List names and files of attached databases\n"
  ".dbconfig ?op? ?val?   List or change sqlite3_db_config() options\n"
  ".dbinfo ?DB?           Show status information about the database\n"
  ".dump ?TABLE? ...      Dump the database in an SQL text format\n"
  "                         If TABLE specified, only dump tables matching\n"
  "                         LIKE pattern TABLE.\n"
  ".echo on|off           Turn command echo on or off\n"
  ".eqp on|off|full       Enable or disable automatic EXPLAIN QUERY PLAN\n"
  ".excel                 Display the output of next command in a spreadsheet\n"
  ".exit                  Exit this program\n"
  ".expert                EXPERIMENTAL. Suggest indexes for specified queries\n"
/* Because explain mode comes on automatically now, the ".explain" mode
** is removed from the help screen.  It is still supported for legacy, however */
/*".explain ?on|off|auto? Turn EXPLAIN output mode on or off or to automatic\n"*/
  ".fullschema ?--indent? Show schema and the content of sqlite_stat tables\n"
  ".headers on|off        Turn display of headers on or off\n"
  ".help                  Show this message\n"
  ".import FILE TABLE     Import data from FILE into TABLE\n"
#ifndef SQLITE_OMIT_TEST_CONTROL
  ".imposter INDEX TABLE  Create imposter table TABLE on index INDEX\n"
#endif
  ".indexes ?TABLE?       Show names of all indexes\n"
  "                         If TABLE specified, only show indexes for tables\n"
  "                         matching LIKE pattern TABLE.\n"
#ifdef SQLITE_ENABLE_IOTRACE
  ".iotrace FILE          Enable I/O diagnostic logging to FILE\n"
#endif
  ".limit ?LIMIT? ?VAL?   Display or change the value of an SQLITE_LIMIT\n"
  ".lint OPTIONS          Report potential schema issues. Options:\n"
  "                         fkey-indexes     Find missing foreign key indexes\n"
#ifndef SQLITE_OMIT_LOAD_EXTENSION
  ".load FILE ?ENTRY?     Load an extension library\n"
#endif
  ".log FILE|off          Turn logging on or off.  FILE can be stderr/stdout\n"
  ".mode MODE ?TABLE?     Set output mode where MODE is one of:\n"
  "                         ascii    Columns/rows delimited by 0x1F and 0x1E\n"
  "                         csv      Comma-separated values\n"
  "                         column   Left-aligned columns.  (See .width)\n"
  "                         html     HTML <table> code\n"
  "                         insert   SQL insert statements for TABLE\n"
  "                         line     One value per line\n"
  "                         list     Values delimited by \"|\"\n"
  "                         quote    Escape answers as for SQL\n"
  "                         tabs     Tab-separated values\n"
  "                         tcl      TCL list elements\n"
  ".nullvalue STRING      Use STRING in place of NULL values\n"
  ".once (-e|-x|FILE)     Output for the next SQL command only to FILE\n"
  "                         or invoke system text editor (-e) or spreadsheet (-x)\n"
  "                         on the output.\n"
  ".open ?OPTIONS? ?FILE? Close existing database and reopen FILE\n"
  "                         The --new option starts with an empty file\n"
  "                         Other options: --readonly --append --zip\n"
  ".output ?FILE?         Send output to FILE or stdout\n"
  ".print STRING...       Print literal STRING\n"
  ".prompt MAIN CONTINUE  Replace the standard prompts\n"
  ".quit                  Exit this program\n"
  ".read FILENAME         Execute SQL in FILENAME\n"
  ".restore ?DB? FILE     Restore content of DB (default \"main\") from FILE\n"
  ".save FILE             Write in-memory database into FILE\n"
  ".scanstats on|off      Turn sqlite3_stmt_scanstatus() metrics on or off\n"
  ".schema ?PATTERN?      Show the CREATE statements matching PATTERN\n"
  "                          Add --indent for pretty-printing\n"
  ".selftest ?--init?     Run tests defined in the SELFTEST table\n"
  ".separator COL ?ROW?   Change the column separator and optionally the row\n"
  "                         separator for both the output mode and .import\n"
#if defined(SQLITE_ENABLE_SESSION)
  ".session CMD ...       Create or control sessions\n"
#endif
  ".sha3sum ?OPTIONS...?  Compute a SHA3 hash of database content\n"
#ifndef SQLITE_NOHAVE_SYSTEM
  ".shell CMD ARGS...     Run CMD ARGS... in a system shell\n"
#endif
  ".show                  Show the current values for various settings\n"
  ".stats ?on|off?        Show stats or turn stats on or off\n"
#ifndef SQLITE_NOHAVE_SYSTEM
  ".system CMD ARGS...    Run CMD ARGS... in a system shell\n"
#endif
  ".tables ?TABLE?        List names of tables\n"
  "                         If TABLE specified, only list tables matching\n"
  "                         LIKE pattern TABLE.\n"
  ".testcase NAME         Begin redirecting output to 'testcase-out.txt'\n"
  ".timeout MS            Try opening locked tables for MS milliseconds\n"
  ".timer on|off          Turn SQL timer on or off\n"
  ".trace FILE|off        Output each SQL statement as it is run\n"
  ".vfsinfo ?AUX?         Information about the top-level VFS\n"
  ".vfslist               List all available VFSes\n"
  ".vfsname ?AUX?         Print the name of the VFS stack\n"
  ".width NUM1 NUM2 ...   Set column widths for \"column\" mode\n"
  "                         Negative values right-justify\n"
;

#if defined(SQLITE_ENABLE_SESSION)
/*
** Print help information for the ".sessions" command
*/
void session_help(ShellState *p){
  raw_printf(p->out,
    ".session ?NAME? SUBCOMMAND ?ARGS...?\n"
    "If ?NAME? is omitted, the first defined session is used.\n"
    "Subcommands:\n"
    "   attach TABLE             Attach TABLE\n"
    "   changeset FILE           Write a changeset into FILE\n"
    "   close                    Close one session\n"
    "   enable ?BOOLEAN?         Set or query the enable bit\n"
    "   filter GLOB...           Reject tables matching GLOBs\n"
    "   indirect ?BOOLEAN?       Mark or query the indirect status\n"
    "   isempty                  Query whether the session is empty\n"
    "   list                     List currently open session names\n"
    "   open DB NAME             Open a new session on DB\n"
    "   patchset FILE            Write a patchset into FILE\n"
  );
}
#endif


/* Forward reference */
static int process_input(ShellState *p, FILE *in);

static int update_prompt_callback(void *data, int nbCols, char** colValues, char** colNames) {
    char *spatialdbtype = colValues[0];
    size_t len = strlen(spatialdbtype);
    
    sqlite3_snprintf(sizeof(mainPrompt), mainPrompt, "%s> ", spatialdbtype);
    sqlite3_snprintf(sizeof(continuePrompt), continuePrompt,"%*s> ", len, "...");
    return SQLITE_ABORT;
}

static void update_prompt(sqlite3 *db) {
    sqlite3_exec(db, "SELECT GPKG_SpatialDBType()", update_prompt_callback, NULL, NULL);
}

/*
** Read the content of file zName into memory obtained from sqlite3_malloc64()
** and return a pointer to the buffer. The caller is responsible for freeing
** the memory.
**
** If parameter pnByte is not NULL, (*pnByte) is set to the number of bytes
** read.
**
** For convenience, a nul-terminator byte is always appended to the data read
** from the file before the buffer is returned. This byte is not included in
** the final value of (*pnByte), if applicable.
**
** NULL is returned if any error is encountered. The final value of *pnByte
** is undefined in this case.
*/
static char *readFile(const char *zName, int *pnByte){
  FILE *in = fopen(zName, "rb");
  long nIn;
  size_t nRead;
  char *pBuf;
  if( in==0 ) return 0;
  fseek(in, 0, SEEK_END);
  nIn = ftell(in);
  rewind(in);
  pBuf = sqlite3_malloc64( nIn+1 );
  if( pBuf==0 ) return 0;
  nRead = fread(pBuf, nIn, 1, in);
  fclose(in);
  if( nRead!=1 ){
    sqlite3_free(pBuf);
    return 0;
  }
  pBuf[nIn] = 0;
  if( pnByte ) *pnByte = nIn;
  return pBuf;
}

#if defined(SQLITE_ENABLE_SESSION)
/*
** Close a single OpenSession object and release all of its associated
** resources.
*/
static void session_close(OpenSession *pSession){
  int i;
  sqlite3session_delete(pSession->p);
  sqlite3_free(pSession->zName);
  for(i=0; i<pSession->nFilter; i++){
    sqlite3_free(pSession->azFilter[i]);
  }
  sqlite3_free(pSession->azFilter);
  memset(pSession, 0, sizeof(OpenSession));
}
#endif

/*
** Close all OpenSession objects and release all associated resources.
*/
#if defined(SQLITE_ENABLE_SESSION)
static void session_close_all(ShellState *p){
  int i;
  for(i=0; i<p->nSession; i++){
    session_close(&p->aSession[i]);
  }
  p->nSession = 0;
}
#else
# define session_close_all(X)
#endif

/*
** Implementation of the xFilter function for an open session.  Omit
** any tables named by ".session filter" but let all other table through.
*/
#if defined(SQLITE_ENABLE_SESSION)
static int session_filter(void *pCtx, const char *zTab){
  OpenSession *pSession = (OpenSession*)pCtx;
  int i;
  for(i=0; i<pSession->nFilter; i++){
    if( sqlite3_strglob(pSession->azFilter[i], zTab)==0 ) return 0;
  }
  return 1;
}
#endif

/*
** Try to deduce the type of file for zName based on its content.  Return
** one of the SHELL_OPEN_* constants.
**
** If the file does not exist or is empty but its name looks like a ZIP
** archive and the dfltZip flag is true, then assume it is a ZIP archive.
** Otherwise, assume an ordinary database regardless of the filename if
** the type cannot be determined from content.
*/
int deduceDatabaseType(const char *zName, int dfltZip){
  FILE *f = fopen(zName, "rb");
  size_t n;
  int rc = SHELL_OPEN_UNSPEC;
  char zBuf[100];
  if( f==0 ){
    if( dfltZip && sqlite3_strlike("%.zip",zName,0)==0 ){
       return SHELL_OPEN_ZIPFILE;
    }else{
       return SHELL_OPEN_NORMAL;
    }
  }
  fseek(f, -25, SEEK_END);
  n = fread(zBuf, 25, 1, f);
  if( n==1 && memcmp(zBuf, "Start-Of-SQLite3-", 17)==0 ){
    rc = SHELL_OPEN_APPENDVFS;
  }else{
    fseek(f, -22, SEEK_END);
    n = fread(zBuf, 22, 1, f);
    if( n==1 && zBuf[0]==0x50 && zBuf[1]==0x4b && zBuf[2]==0x05
       && zBuf[3]==0x06 ){
      rc = SHELL_OPEN_ZIPFILE;
    }else if( n==0 && dfltZip && sqlite3_strlike("%.zip",zName,0)==0 ){
      rc = SHELL_OPEN_ZIPFILE;
    }
  }
  fclose(f);
  return rc;  
}

/* Flags for open_db().
**
** The default behavior of open_db() is to exit(1) if the database fails to
** open.  The OPEN_DB_KEEPALIVE flag changes that so that it prints an error
** but still returns without calling exit.
**
** The OPEN_DB_ZIPFILE flag causes open_db() to prefer to open files as a
** ZIP archive if the file does not exist or is empty and its name matches
** the *.zip pattern.
*/
#define OPEN_DB_KEEPALIVE   0x001   /* Return after error if true */
#define OPEN_DB_ZIPFILE     0x002   /* Open as ZIP if name matches *.zip */

/*
** Make sure the database is open.  If it is not, then open it.  If
** the database fails to open, print an error message and exit.
*/
static void open_db(ShellState *p, int openFlags){
  if( p->db==0 ){
    if( p->openMode==SHELL_OPEN_UNSPEC ){
      if( p->zDbFilename==0 || p->zDbFilename[0]==0 ){
        p->openMode = SHELL_OPEN_NORMAL;
      }else{
        p->openMode = (u8)deduceDatabaseType(p->zDbFilename, 
                             (openFlags & OPEN_DB_ZIPFILE)!=0);
      }
    }
    switch( p->openMode ){
      case SHELL_OPEN_APPENDVFS: {
        sqlite3_open_v2(p->zDbFilename, &p->db, 
           SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, "apndvfs");
        break;
      }
      case SHELL_OPEN_ZIPFILE: {
        sqlite3_open(":memory:", &p->db);
        break;
      }
      case SHELL_OPEN_READONLY: {
        sqlite3_open_v2(p->zDbFilename, &p->db, SQLITE_OPEN_READONLY, 0);
        break;
      }
      case SHELL_OPEN_UNSPEC:
      case SHELL_OPEN_NORMAL: {
        sqlite3_open(p->zDbFilename, &p->db);
        break;
      }
    }
    globalDb = p->db;
    if( p->db==0 || SQLITE_OK!=sqlite3_errcode(p->db) ){
      utf8_printf(stderr,"Error: unable to open database \"%s\": %s\n",
          p->zDbFilename, sqlite3_errmsg(p->db));
      if( openFlags & OPEN_DB_KEEPALIVE ) return;
      exit(1);
    }

    if (p->gpkgEntryPoint) {
      p->gpkgEntryPoint(p->db, NULL, NULL);
    }
      
#ifndef SQLITE_OMIT_LOAD_EXTENSION
    sqlite3_enable_load_extension(p->db, 1);
#endif
    sqlite3_fileio_init(p->db, 0, 0);
    update_prompt(p->db);
    sqlite3_shathree_init(p->db, 0, 0);
    sqlite3_completion_init(p->db, 0, 0);
#ifdef SQLITE_HAVE_ZLIB
    sqlite3_zipfile_init(p->db, 0, 0);
    sqlite3_sqlar_init(p->db, 0, 0);
#endif
    sqlite3_create_function(p->db, "shell_add_schema", 3, SQLITE_UTF8, 0,
                            shellAddSchemaName, 0, 0);
    sqlite3_create_function(p->db, "shell_module_schema", 1, SQLITE_UTF8, 0,
                            shellModuleSchema, 0, 0);
    sqlite3_create_function(p->db, "shell_putsnl", 1, SQLITE_UTF8, p,
                            shellPutsFunc, 0, 0);
#ifndef SQLITE_NOHAVE_SYSTEM
    sqlite3_create_function(p->db, "edit", 1, SQLITE_UTF8, 0,
                            editFunc, 0, 0);
    sqlite3_create_function(p->db, "edit", 2, SQLITE_UTF8, 0,
                            editFunc, 0, 0);
#endif
    if( p->openMode==SHELL_OPEN_ZIPFILE ){
      char *zSql = sqlite3_mprintf(
         "CREATE VIRTUAL TABLE zip USING zipfile(%Q);", p->zDbFilename);
      sqlite3_exec(p->db, zSql, 0, 0, 0);
      sqlite3_free(zSql);
    }
  }
}

/*
** Attempt to close the databaes connection.  Report errors.
*/
void close_db(sqlite3 *db){
  int rc = sqlite3_close(db);
  if( rc ){
    utf8_printf(stderr, "Error: sqlite3_close() returns %d: %s\n",
        rc, sqlite3_errmsg(db));
  } 
}

#if HAVE_READLINE || HAVE_EDITLINE
/*
** Readline completion callbacks
*/
static char *readline_completion_generator(const char *text, int state){
  static sqlite3_stmt *pStmt = 0;
  char *zRet;
  if( state==0 ){
    char *zSql;
    sqlite3_finalize(pStmt);
    zSql = sqlite3_mprintf("SELECT DISTINCT candidate COLLATE nocase"
                           "  FROM completion(%Q) ORDER BY 1", text);
    sqlite3_prepare_v2(globalDb, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
  }
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    zRet = strdup((const char*)sqlite3_column_text(pStmt, 0));
  }else{
    sqlite3_finalize(pStmt);
    pStmt = 0;
    zRet = 0;
  }
  return zRet;
}
static char **readline_completion(const char *zText, int iStart, int iEnd){
  rl_attempted_completion_over = 1;
  return rl_completion_matches(zText, readline_completion_generator);
}

#elif HAVE_LINENOISE
/*
** Linenoise completion callback
*/
static void linenoise_completion(const char *zLine, linenoiseCompletions *lc){
  int nLine = strlen30(zLine);
  int i, iStart;
  sqlite3_stmt *pStmt = 0;
  char *zSql;
  char zBuf[1000];

  if( nLine>sizeof(zBuf)-30 ) return;
  if( zLine[0]=='.' || zLine[0]=='#') return;
  for(i=nLine-1; i>=0 && (isalnum(zLine[i]) || zLine[i]=='_'); i--){}
  if( i==nLine-1 ) return;
  iStart = i+1;
  memcpy(zBuf, zLine, iStart);
  zSql = sqlite3_mprintf("SELECT DISTINCT candidate COLLATE nocase"
                         "  FROM completion(%Q,%Q) ORDER BY 1",
                         &zLine[iStart], zLine);
  sqlite3_prepare_v2(globalDb, zSql, -1, &pStmt, 0);
  sqlite3_free(zSql);
  sqlite3_exec(globalDb, "PRAGMA page_count", 0, 0, 0); /* Load the schema */
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    const char *zCompletion = (const char*)sqlite3_column_text(pStmt, 0);
    int nCompletion = sqlite3_column_bytes(pStmt, 0);
    if( iStart+nCompletion < sizeof(zBuf)-1 ){
      memcpy(zBuf+iStart, zCompletion, nCompletion+1);
      linenoiseAddCompletion(lc, zBuf);
    }
  }
  sqlite3_finalize(pStmt);
}
#endif

/*
** Do C-language style dequoting.
**
**    \a    -> alarm
**    \b    -> backspace
**    \t    -> tab
**    \n    -> newline
**    \v    -> vertical tab
**    \f    -> form feed
**    \r    -> carriage return
**    \s    -> space
**    \"    -> "
**    \'    -> '
**    \\    -> backslash
**    \NNN  -> ascii character NNN in octal
*/
static void resolve_backslashes(char *z){
  int i, j;
  char c;
  while( *z && *z!='\\' ) z++;
  for(i=j=0; (c = z[i])!=0; i++, j++){
    if( c=='\\' && z[i+1]!=0 ){
      c = z[++i];
      if( c=='a' ){
        c = '\a';
      }else if( c=='b' ){
        c = '\b';
      }else if( c=='t' ){
        c = '\t';
      }else if( c=='n' ){
        c = '\n';
      }else if( c=='v' ){
        c = '\v';
      }else if( c=='f' ){
        c = '\f';
      }else if( c=='r' ){
        c = '\r';
      }else if( c=='"' ){
        c = '"';
      }else if( c=='\'' ){
        c = '\'';
      }else if( c=='\\' ){
        c = '\\';
      }else if( c>='0' && c<='7' ){
        c -= '0';
        if( z[i+1]>='0' && z[i+1]<='7' ){
          i++;
          c = (c<<3) + z[i] - '0';
          if( z[i+1]>='0' && z[i+1]<='7' ){
            i++;
            c = (c<<3) + z[i] - '0';
          }
        }
      }
    }
    z[j] = c;
  }
  if( j<i ) z[j] = 0;
}

/*
** Interpret zArg as either an integer or a boolean value.  Return 1 or 0
** for TRUE and FALSE.  Return the integer value if appropriate.
*/
static int booleanValue(const char *zArg){
  int i;
  if( zArg[0]=='0' && zArg[1]=='x' ){
    for(i=2; hexDigitValue(zArg[i])>=0; i++){}
  }else{
    for(i=0; zArg[i]>='0' && zArg[i]<='9'; i++){}
  }
  if( i>0 && zArg[i]==0 ) return (int)(integerValue(zArg) & 0xffffffff);
  if( sqlite3_stricmp(zArg, "on")==0 || sqlite3_stricmp(zArg,"yes")==0 ){
    return 1;
  }
  if( sqlite3_stricmp(zArg, "off")==0 || sqlite3_stricmp(zArg,"no")==0 ){
    return 0;
  }
  utf8_printf(stderr, "ERROR: Not a boolean value: \"%s\". Assuming \"no\".\n",
          zArg);
  return 0;
}

/*
** Set or clear a shell flag according to a boolean value.
*/
static void setOrClearFlag(ShellState *p, unsigned mFlag, const char *zArg){
  if( booleanValue(zArg) ){
    ShellSetFlag(p, mFlag);
  }else{
    ShellClearFlag(p, mFlag);
  }
}

/*
** Close an output file, assuming it is not stderr or stdout
*/
static void output_file_close(FILE *f){
  if( f && f!=stdout && f!=stderr ) fclose(f);
}

/*
** Try to open an output file.   The names "stdout" and "stderr" are
** recognized and do the right thing.  NULL is returned if the output
** filename is "off".
*/
static FILE *output_file_open(const char *zFile, int bTextMode){
  FILE *f;
  if( strcmp(zFile,"stdout")==0 ){
    f = stdout;
  }else if( strcmp(zFile, "stderr")==0 ){
    f = stderr;
  }else if( strcmp(zFile, "off")==0 ){
    f = 0;
  }else{
    f = fopen(zFile, bTextMode ? "w" : "wb");
    if( f==0 ){
      utf8_printf(stderr, "Error: cannot open \"%s\"\n", zFile);
    }
  }
  return f;
}

#if !defined(SQLITE_OMIT_TRACE) && !defined(SQLITE_OMIT_FLOATING_POINT)
/*
** A routine for handling output from sqlite3_trace().
*/
static int sql_trace_callback(
  unsigned mType,
  void *pArg,
  void *pP,
  void *pX
){
  FILE *f = (FILE*)pArg;
  UNUSED_PARAMETER(mType);
  UNUSED_PARAMETER(pP);
  if( f ){
    const char *z = (const char*)pX;
    int i = strlen30(z);
    while( i>0 && z[i-1]==';' ){ i--; }
    utf8_printf(f, "%.*s;\n", i, z);
  }
  return 0;
}
#endif

/*
** A no-op routine that runs with the ".breakpoint" doc-command.  This is
** a useful spot to set a debugger breakpoint.
*/
static void test_breakpoint(void){
  static int nCall = 0;
  nCall++;
}

/*
** An object used to read a CSV and other files for import.
*/
typedef struct ImportCtx ImportCtx;
struct ImportCtx {
  const char *zFile;  /* Name of the input file */
  FILE *in;           /* Read the CSV text from this input stream */
  char *z;            /* Accumulated text for a field */
  int n;              /* Number of bytes in z */
  int nAlloc;         /* Space allocated for z[] */
  int nLine;          /* Current line number */
  int bNotFirst;      /* True if one or more bytes already read */
  int cTerm;          /* Character that terminated the most recent field */
  int cColSep;        /* The column separator character.  (Usually ",") */
  int cRowSep;        /* The row separator character.  (Usually "\n") */
};

/* Append a single byte to z[] */
static void import_append_char(ImportCtx *p, int c){
  if( p->n+1>=p->nAlloc ){
    p->nAlloc += p->nAlloc + 100;
    p->z = sqlite3_realloc64(p->z, p->nAlloc);
    if( p->z==0 ) shell_out_of_memory();
  }
  p->z[p->n++] = (char)c;
}

/* Read a single field of CSV text.  Compatible with rfc4180 and extended
** with the option of having a separator other than ",".
**
**   +  Input comes from p->in.
**   +  Store results in p->z of length p->n.  Space to hold p->z comes
**      from sqlite3_malloc64().
**   +  Use p->cSep as the column separator.  The default is ",".
**   +  Use p->rSep as the row separator.  The default is "\n".
**   +  Keep track of the line number in p->nLine.
**   +  Store the character that terminates the field in p->cTerm.  Store
**      EOF on end-of-file.
**   +  Report syntax errors on stderr
*/
static char *SQLITE_CDECL csv_read_one_field(ImportCtx *p){
  int c;
  int cSep = p->cColSep;
  int rSep = p->cRowSep;
  p->n = 0;
  c = fgetc(p->in);
  if( c==EOF || seenInterrupt ){
    p->cTerm = EOF;
    return 0;
  }
  if( c=='"' ){
    int pc, ppc;
    int startLine = p->nLine;
    int cQuote = c;
    pc = ppc = 0;
    while( 1 ){
      c = fgetc(p->in);
      if( c==rSep ) p->nLine++;
      if( c==cQuote ){
        if( pc==cQuote ){
          pc = 0;
          continue;
        }
      }
      if( (c==cSep && pc==cQuote)
       || (c==rSep && pc==cQuote)
       || (c==rSep && pc=='\r' && ppc==cQuote)
       || (c==EOF && pc==cQuote)
      ){
        do{ p->n--; }while( p->z[p->n]!=cQuote );
        p->cTerm = c;
        break;
      }
      if( pc==cQuote && c!='\r' ){
        utf8_printf(stderr, "%s:%d: unescaped %c character\n",
                p->zFile, p->nLine, cQuote);
      }
      if( c==EOF ){
        utf8_printf(stderr, "%s:%d: unterminated %c-quoted field\n",
                p->zFile, startLine, cQuote);
        p->cTerm = c;
        break;
      }
      import_append_char(p, c);
      ppc = pc;
      pc = c;
    }
  }else{
    /* If this is the first field being parsed and it begins with the
    ** UTF-8 BOM  (0xEF BB BF) then skip the BOM */
    if( (c&0xff)==0xef && p->bNotFirst==0 ){
      import_append_char(p, c);
      c = fgetc(p->in);
      if( (c&0xff)==0xbb ){
        import_append_char(p, c);
        c = fgetc(p->in);
        if( (c&0xff)==0xbf ){
          p->bNotFirst = 1;
          p->n = 0;
          return csv_read_one_field(p);
        }
      }
    }
    while( c!=EOF && c!=cSep && c!=rSep ){
      import_append_char(p, c);
      c = fgetc(p->in);
    }
    if( c==rSep ){
      p->nLine++;
      if( p->n>0 && p->z[p->n-1]=='\r' ) p->n--;
    }
    p->cTerm = c;
  }
  if( p->z ) p->z[p->n] = 0;
  p->bNotFirst = 1;
  return p->z;
}

/* Read a single field of ASCII delimited text.
**
**   +  Input comes from p->in.
**   +  Store results in p->z of length p->n.  Space to hold p->z comes
**      from sqlite3_malloc64().
**   +  Use p->cSep as the column separator.  The default is "\x1F".
**   +  Use p->rSep as the row separator.  The default is "\x1E".
**   +  Keep track of the row number in p->nLine.
**   +  Store the character that terminates the field in p->cTerm.  Store
**      EOF on end-of-file.
**   +  Report syntax errors on stderr
*/
static char *SQLITE_CDECL ascii_read_one_field(ImportCtx *p){
  int c;
  int cSep = p->cColSep;
  int rSep = p->cRowSep;
  p->n = 0;
  c = fgetc(p->in);
  if( c==EOF || seenInterrupt ){
    p->cTerm = EOF;
    return 0;
  }
  while( c!=EOF && c!=cSep && c!=rSep ){
    import_append_char(p, c);
    c = fgetc(p->in);
  }
  if( c==rSep ){
    p->nLine++;
  }
  p->cTerm = c;
  if( p->z ) p->z[p->n] = 0;
  return p->z;
}

/*
** Try to transfer data for table zTable.  If an error is seen while
** moving forward, try to go backwards.  The backwards movement won't
** work for WITHOUT ROWID tables.
*/
static void tryToCloneData(
  ShellState *p,
  sqlite3 *newDb,
  const char *zTable
){
  sqlite3_stmt *pQuery = 0;
  sqlite3_stmt *pInsert = 0;
  char *zQuery = 0;
  char *zInsert = 0;
  int rc;
  int i, j, n;
  int nTable = strlen30(zTable);
  int k = 0;
  int cnt = 0;
  const int spinRate = 10000;

  zQuery = sqlite3_mprintf("SELECT * FROM \"%w\"", zTable);
  rc = sqlite3_prepare_v2(p->db, zQuery, -1, &pQuery, 0);
  if( rc ){
    utf8_printf(stderr, "Error %d: %s on [%s]\n",
            sqlite3_extended_errcode(p->db), sqlite3_errmsg(p->db),
            zQuery);
    goto end_data_xfer;
  }
  n = sqlite3_column_count(pQuery);
  zInsert = sqlite3_malloc64(200 + nTable + n*3);
  if( zInsert==0 ) shell_out_of_memory();
  sqlite3_snprintf(200+nTable,zInsert,
                   "INSERT OR IGNORE INTO \"%s\" VALUES(?", zTable);
  i = strlen30(zInsert);
  for(j=1; j<n; j++){
    memcpy(zInsert+i, ",?", 2);
    i += 2;
  }
  memcpy(zInsert+i, ");", 3);
  rc = sqlite3_prepare_v2(newDb, zInsert, -1, &pInsert, 0);
  if( rc ){
    utf8_printf(stderr, "Error %d: %s on [%s]\n",
            sqlite3_extended_errcode(newDb), sqlite3_errmsg(newDb),
            zQuery);
    goto end_data_xfer;
  }
  for(k=0; k<2; k++){
    while( (rc = sqlite3_step(pQuery))==SQLITE_ROW ){
      for(i=0; i<n; i++){
        switch( sqlite3_column_type(pQuery, i) ){
          case SQLITE_NULL: {
            sqlite3_bind_null(pInsert, i+1);
            break;
          }
          case SQLITE_INTEGER: {
            sqlite3_bind_int64(pInsert, i+1, sqlite3_column_int64(pQuery,i));
            break;
          }
          case SQLITE_FLOAT: {
            sqlite3_bind_double(pInsert, i+1, sqlite3_column_double(pQuery,i));
            break;
          }
          case SQLITE_TEXT: {
            sqlite3_bind_text(pInsert, i+1,
                             (const char*)sqlite3_column_text(pQuery,i),
                             -1, SQLITE_STATIC);
            break;
          }
          case SQLITE_BLOB: {
            sqlite3_bind_blob(pInsert, i+1, sqlite3_column_blob(pQuery,i),
                                            sqlite3_column_bytes(pQuery,i),
                                            SQLITE_STATIC);
            break;
          }
        }
      } /* End for */
      rc = sqlite3_step(pInsert);
      if( rc!=SQLITE_OK && rc!=SQLITE_ROW && rc!=SQLITE_DONE ){
        utf8_printf(stderr, "Error %d: %s\n", sqlite3_extended_errcode(newDb),
                        sqlite3_errmsg(newDb));
      }
      sqlite3_reset(pInsert);
      cnt++;
      if( (cnt%spinRate)==0 ){
        printf("%c\b", "|/-\\"[(cnt/spinRate)%4]);
        fflush(stdout);
      }
    } /* End while */
    if( rc==SQLITE_DONE ) break;
    sqlite3_finalize(pQuery);
    sqlite3_free(zQuery);
    zQuery = sqlite3_mprintf("SELECT * FROM \"%w\" ORDER BY rowid DESC;",
                             zTable);
    rc = sqlite3_prepare_v2(p->db, zQuery, -1, &pQuery, 0);
    if( rc ){
      utf8_printf(stderr, "Warning: cannot step \"%s\" backwards", zTable);
      break;
    }
  } /* End for(k=0...) */

end_data_xfer:
  sqlite3_finalize(pQuery);
  sqlite3_finalize(pInsert);
  sqlite3_free(zQuery);
  sqlite3_free(zInsert);
}


/*
** Try to transfer all rows of the schema that match zWhere.  For
** each row, invoke xForEach() on the object defined by that row.
** If an error is encountered while moving forward through the
** sqlite_master table, try again moving backwards.
*/
static void tryToCloneSchema(
  ShellState *p,
  sqlite3 *newDb,
  const char *zWhere,
  void (*xForEach)(ShellState*,sqlite3*,const char*)
){
  sqlite3_stmt *pQuery = 0;
  char *zQuery = 0;
  int rc;
  const unsigned char *zName;
  const unsigned char *zSql;
  char *zErrMsg = 0;

  zQuery = sqlite3_mprintf("SELECT name, sql FROM sqlite_master"
                           " WHERE %s", zWhere);
  rc = sqlite3_prepare_v2(p->db, zQuery, -1, &pQuery, 0);
  if( rc ){
    utf8_printf(stderr, "Error: (%d) %s on [%s]\n",
                    sqlite3_extended_errcode(p->db), sqlite3_errmsg(p->db),
                    zQuery);
    goto end_schema_xfer;
  }
  while( (rc = sqlite3_step(pQuery))==SQLITE_ROW ){
    zName = sqlite3_column_text(pQuery, 0);
    zSql = sqlite3_column_text(pQuery, 1);
    printf("%s... ", zName); fflush(stdout);
    sqlite3_exec(newDb, (const char*)zSql, 0, 0, &zErrMsg);
    if( zErrMsg ){
      utf8_printf(stderr, "Error: %s\nSQL: [%s]\n", zErrMsg, zSql);
      sqlite3_free(zErrMsg);
      zErrMsg = 0;
    }
    if( xForEach ){
      xForEach(p, newDb, (const char*)zName);
    }
    printf("done\n");
  }
  if( rc!=SQLITE_DONE ){
    sqlite3_finalize(pQuery);
    sqlite3_free(zQuery);
    zQuery = sqlite3_mprintf("SELECT name, sql FROM sqlite_master"
                             " WHERE %s ORDER BY rowid DESC", zWhere);
    rc = sqlite3_prepare_v2(p->db, zQuery, -1, &pQuery, 0);
    if( rc ){
      utf8_printf(stderr, "Error: (%d) %s on [%s]\n",
                      sqlite3_extended_errcode(p->db), sqlite3_errmsg(p->db),
                      zQuery);
      goto end_schema_xfer;
    }
    while( (rc = sqlite3_step(pQuery))==SQLITE_ROW ){
      zName = sqlite3_column_text(pQuery, 0);
      zSql = sqlite3_column_text(pQuery, 1);
      printf("%s... ", zName); fflush(stdout);
      sqlite3_exec(newDb, (const char*)zSql, 0, 0, &zErrMsg);
      if( zErrMsg ){
        utf8_printf(stderr, "Error: %s\nSQL: [%s]\n", zErrMsg, zSql);
        sqlite3_free(zErrMsg);
        zErrMsg = 0;
      }
      if( xForEach ){
        xForEach(p, newDb, (const char*)zName);
      }
      printf("done\n");
    }
  }
end_schema_xfer:
  sqlite3_finalize(pQuery);
  sqlite3_free(zQuery);
}

/*
** Open a new database file named "zNewDb".  Try to recover as much information
** as possible out of the main database (which might be corrupt) and write it
** into zNewDb.
*/
static void tryToClone(ShellState *p, const char *zNewDb){
  int rc;
  sqlite3 *newDb = 0;
  if( access(zNewDb,0)==0 ){
    utf8_printf(stderr, "File \"%s\" already exists.\n", zNewDb);
    return;
  }
  rc = sqlite3_open(zNewDb, &newDb);
  if( rc ){
    utf8_printf(stderr, "Cannot create output database: %s\n",
            sqlite3_errmsg(newDb));
  }else{
    sqlite3_exec(p->db, "PRAGMA writable_schema=ON;", 0, 0, 0);
    sqlite3_exec(newDb, "BEGIN EXCLUSIVE;", 0, 0, 0);
    tryToCloneSchema(p, newDb, "type='table'", tryToCloneData);
    tryToCloneSchema(p, newDb, "type!='table'", 0);
    sqlite3_exec(newDb, "COMMIT;", 0, 0, 0);
    sqlite3_exec(p->db, "PRAGMA writable_schema=OFF;", 0, 0, 0);
  }
  close_db(newDb);
}

/*
** Change the output file back to stdout.
**
** If the p->doXdgOpen flag is set, that means the output was being
** redirected to a temporary file named by p->zTempFile.  In that case,
** launch start/open/xdg-open on that temporary file.
*/
static void output_reset(ShellState *p){
  if( p->outfile[0]=='|' ){
#ifndef SQLITE_OMIT_POPEN
    pclose(p->out);
#endif
  }else{
    output_file_close(p->out);
#ifndef SQLITE_NOHAVE_SYSTEM
    if( p->doXdgOpen ){
      const char *zXdgOpenCmd =
#if defined(_WIN32)
      "start";
#elif defined(__APPLE__)
      "open";
#else
      "xdg-open";
#endif
      char *zCmd;
      zCmd = sqlite3_mprintf("%s %s", zXdgOpenCmd, p->zTempFile);
      if( system(zCmd) ){
        utf8_printf(stderr, "Failed: [%s]\n", zCmd);
      }
      sqlite3_free(zCmd);
      outputModePop(p);
      p->doXdgOpen = 0;
    }
#endif /* !defined(SQLITE_NOHAVE_SYSTEM) */
  }
  p->outfile[0] = 0;
  p->out = stdout;
}

/*
** Run an SQL command and return the single integer result.
*/
static int db_int(ShellState *p, const char *zSql){
  sqlite3_stmt *pStmt;
  int res = 0;
  sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
  if( pStmt && sqlite3_step(pStmt)==SQLITE_ROW ){
    res = sqlite3_column_int(pStmt,0);
  }
  sqlite3_finalize(pStmt);
  return res;
}

/*
** Convert a 2-byte or 4-byte big-endian integer into a native integer
*/
static unsigned int get2byteInt(unsigned char *a){
  return (a[0]<<8) + a[1];
}
static unsigned int get4byteInt(unsigned char *a){
  return (a[0]<<24) + (a[1]<<16) + (a[2]<<8) + a[3];
}

/*
** Implementation of the ".info" command.
**
** Return 1 on error, 2 to exit, and 0 otherwise.
*/
static int shell_dbinfo_command(ShellState *p, int nArg, char **azArg){
  static const struct { const char *zName; int ofst; } aField[] = {
     { "file change counter:",  24  },
     { "database page count:",  28  },
     { "freelist page count:",  36  },
     { "schema cookie:",        40  },
     { "schema format:",        44  },
     { "default cache size:",   48  },
     { "autovacuum top root:",  52  },
     { "incremental vacuum:",   64  },
     { "text encoding:",        56  },
     { "user version:",         60  },
     { "application id:",       68  },
     { "software version:",     96  },
  };
  static const struct { const char *zName; const char *zSql; } aQuery[] = {
     { "number of tables:",
       "SELECT count(*) FROM %s WHERE type='table'" },
     { "number of indexes:",
       "SELECT count(*) FROM %s WHERE type='index'" },
     { "number of triggers:",
       "SELECT count(*) FROM %s WHERE type='trigger'" },
     { "number of views:",
       "SELECT count(*) FROM %s WHERE type='view'" },
     { "schema size:",
       "SELECT total(length(sql)) FROM %s" },
  };
  int i;
  unsigned iDataVersion;
  char *zSchemaTab;
  char *zDb = nArg>=2 ? azArg[1] : "main";
  sqlite3_stmt *pStmt = 0;
  unsigned char aHdr[100];
  open_db(p, 0);
  if( p->db==0 ) return 1;
  sqlite3_prepare_v2(p->db,"SELECT data FROM sqlite_dbpage(?1) WHERE pgno=1",
                     -1, &pStmt, 0);
  sqlite3_bind_text(pStmt, 1, zDb, -1, SQLITE_STATIC);
  if( sqlite3_step(pStmt)==SQLITE_ROW
   && sqlite3_column_bytes(pStmt,0)>100
  ){
    memcpy(aHdr, sqlite3_column_blob(pStmt,0), 100);
    sqlite3_finalize(pStmt);
  }else{
    raw_printf(stderr, "unable to read database header\n");
    sqlite3_finalize(pStmt);
    return 1;
  }
  i = get2byteInt(aHdr+16);
  if( i==1 ) i = 65536;
  utf8_printf(p->out, "%-20s %d\n", "database page size:", i);
  utf8_printf(p->out, "%-20s %d\n", "write format:", aHdr[18]);
  utf8_printf(p->out, "%-20s %d\n", "read format:", aHdr[19]);
  utf8_printf(p->out, "%-20s %d\n", "reserved bytes:", aHdr[20]);
  for(i=0; i<ArraySize(aField); i++){
    int ofst = aField[i].ofst;
    unsigned int val = get4byteInt(aHdr + ofst);
    utf8_printf(p->out, "%-20s %u", aField[i].zName, val);
    switch( ofst ){
      case 56: {
        if( val==1 ) raw_printf(p->out, " (utf8)");
        if( val==2 ) raw_printf(p->out, " (utf16le)");
        if( val==3 ) raw_printf(p->out, " (utf16be)");
      }
    }
    raw_printf(p->out, "\n");
  }
  if( zDb==0 ){
    zSchemaTab = sqlite3_mprintf("main.sqlite_master");
  }else if( strcmp(zDb,"temp")==0 ){
    zSchemaTab = sqlite3_mprintf("%s", "sqlite_temp_master");
  }else{
    zSchemaTab = sqlite3_mprintf("\"%w\".sqlite_master", zDb);
  }
  for(i=0; i<ArraySize(aQuery); i++){
    char *zSql = sqlite3_mprintf(aQuery[i].zSql, zSchemaTab);
    int val = db_int(p, zSql);
    sqlite3_free(zSql);
    utf8_printf(p->out, "%-20s %d\n", aQuery[i].zName, val);
  }
  sqlite3_free(zSchemaTab);
  sqlite3_file_control(p->db, zDb, SQLITE_FCNTL_DATA_VERSION, &iDataVersion);
  utf8_printf(p->out, "%-20s %u\n", "data version", iDataVersion);
  return 0;
}

/*
** Print the current sqlite3_errmsg() value to stderr and return 1.
*/
static int shellDatabaseError(sqlite3 *db){
  const char *zErr = sqlite3_errmsg(db);
  utf8_printf(stderr, "Error: %s\n", zErr);
  return 1;
}

/*
** Compare the pattern in zGlob[] against the text in z[].  Return TRUE
** if they match and FALSE (0) if they do not match.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
**      '#'       Matches any sequence of one or more digits with an
**                optional + or - sign in front
**
**      ' '       Any span of whitespace matches any other span of
**                whitespace.
**
** Extra whitespace at the end of z[] is ignored.
*/
static int testcase_glob(const char *zGlob, const char *z){
  int c, c2;
  int invert;
  int seen;

  while( (c = (*(zGlob++)))!=0 ){
    if( IsSpace(c) ){
      if( !IsSpace(*z) ) return 0;
      while( IsSpace(*zGlob) ) zGlob++;
      while( IsSpace(*z) ) z++;
    }else if( c=='*' ){
      while( (c=(*(zGlob++))) == '*' || c=='?' ){
        if( c=='?' && (*(z++))==0 ) return 0;
      }
      if( c==0 ){
        return 1;
      }else if( c=='[' ){
        while( *z && testcase_glob(zGlob-1,z)==0 ){
          z++;
        }
        return (*z)!=0;
      }
      while( (c2 = (*(z++)))!=0 ){
        while( c2!=c ){
          c2 = *(z++);
          if( c2==0 ) return 0;
        }
        if( testcase_glob(zGlob,z) ) return 1;
      }
      return 0;
    }else if( c=='?' ){
      if( (*(z++))==0 ) return 0;
    }else if( c=='[' ){
      int prior_c = 0;
      seen = 0;
      invert = 0;
      c = *(z++);
      if( c==0 ) return 0;
      c2 = *(zGlob++);
      if( c2=='^' ){
        invert = 1;
        c2 = *(zGlob++);
      }
      if( c2==']' ){
        if( c==']' ) seen = 1;
        c2 = *(zGlob++);
      }
      while( c2 && c2!=']' ){
        if( c2=='-' && zGlob[0]!=']' && zGlob[0]!=0 && prior_c>0 ){
          c2 = *(zGlob++);
          if( c>=prior_c && c<=c2 ) seen = 1;
          prior_c = 0;
        }else{
          if( c==c2 ){
            seen = 1;
          }
          prior_c = c2;
        }
        c2 = *(zGlob++);
      }
      if( c2==0 || (seen ^ invert)==0 ) return 0;
    }else if( c=='#' ){
      if( (z[0]=='-' || z[0]=='+') && IsDigit(z[1]) ) z++;
      if( !IsDigit(z[0]) ) return 0;
      z++;
      while( IsDigit(z[0]) ){ z++; }
    }else{
      if( c!=(*(z++)) ) return 0;
    }
  }
  while( IsSpace(*z) ){ z++; }
  return *z==0;
}


/*
** Compare the string as a command-line option with either one or two
** initial "-" characters.
*/
static int optionMatch(const char *zStr, const char *zOpt){
  if( zStr[0]!='-' ) return 0;
  zStr++;
  if( zStr[0]=='-' ) zStr++;
  return strcmp(zStr, zOpt)==0;
}

/*
** Delete a file.
*/
int shellDeleteFile(const char *zFilename){
  int rc;
#ifdef _WIN32
  wchar_t *z = sqlite3_win32_utf8_to_unicode(zFilename);
  rc = _wunlink(z);
  sqlite3_free(z);
#else
  rc = unlink(zFilename);
#endif
  return rc;
}

/*
** Try to delete the temporary file (if there is one) and free the
** memory used to hold the name of the temp file.
*/
static void clearTempFile(ShellState *p){
  if( p->zTempFile==0 ) return;
  if( p->doXdgOpen ) return;
  if( shellDeleteFile(p->zTempFile) ) return;
  sqlite3_free(p->zTempFile);
  p->zTempFile = 0;
}

/*
** Create a new temp file name with the given suffix.
*/
static void newTempFile(ShellState *p, const char *zSuffix){
  clearTempFile(p);
  sqlite3_free(p->zTempFile);
  p->zTempFile = 0;
  if( p->db ){
    sqlite3_file_control(p->db, 0, SQLITE_FCNTL_TEMPFILENAME, &p->zTempFile);
  }
  if( p->zTempFile==0 ){
    sqlite3_uint64 r;
    sqlite3_randomness(sizeof(r), &r);
    p->zTempFile = sqlite3_mprintf("temp%llx.%s", r, zSuffix);
  }else{
    p->zTempFile = sqlite3_mprintf("%z.%s", p->zTempFile, zSuffix);
  }
  if( p->zTempFile==0 ){
    raw_printf(stderr, "out of memory\n");
    exit(1);
  }
}


/*
** The implementation of SQL scalar function fkey_collate_clause(), used
** by the ".lint fkey-indexes" command. This scalar function is always
** called with four arguments - the parent table name, the parent column name,
** the child table name and the child column name.
**
**   fkey_collate_clause('parent-tab', 'parent-col', 'child-tab', 'child-col')
**
** If either of the named tables or columns do not exist, this function
** returns an empty string. An empty string is also returned if both tables
** and columns exist but have the same default collation sequence. Or,
** if both exist but the default collation sequences are different, this
** function returns the string " COLLATE <parent-collation>", where
** <parent-collation> is the default collation sequence of the parent column.
*/
static void shellFkeyCollateClause(
  sqlite3_context *pCtx,
  int nVal,
  sqlite3_value **apVal
){
  sqlite3 *db = sqlite3_context_db_handle(pCtx);
  const char *zParent;
  const char *zParentCol;
  const char *zParentSeq;
  const char *zChild;
  const char *zChildCol;
  const char *zChildSeq = 0;  /* Initialize to avoid false-positive warning */
  int rc;

  assert( nVal==4 );
  zParent = (const char*)sqlite3_value_text(apVal[0]);
  zParentCol = (const char*)sqlite3_value_text(apVal[1]);
  zChild = (const char*)sqlite3_value_text(apVal[2]);
  zChildCol = (const char*)sqlite3_value_text(apVal[3]);

  sqlite3_result_text(pCtx, "", -1, SQLITE_STATIC);
  rc = sqlite3_table_column_metadata(
      db, "main", zParent, zParentCol, 0, &zParentSeq, 0, 0, 0
  );
  if( rc==SQLITE_OK ){
    rc = sqlite3_table_column_metadata(
        db, "main", zChild, zChildCol, 0, &zChildSeq, 0, 0, 0
    );
  }

  if( rc==SQLITE_OK && sqlite3_stricmp(zParentSeq, zChildSeq) ){
    char *z = sqlite3_mprintf(" COLLATE %s", zParentSeq);
    sqlite3_result_text(pCtx, z, -1, SQLITE_TRANSIENT);
    sqlite3_free(z);
  }
}


/*
** The implementation of dot-command ".lint fkey-indexes".
*/
static int lintFkeyIndexes(
  ShellState *pState,             /* Current shell tool state */
  char **azArg,                   /* Array of arguments passed to dot command */
  int nArg                        /* Number of entries in azArg[] */
){
  sqlite3 *db = pState->db;       /* Database handle to query "main" db of */
  FILE *out = pState->out;        /* Stream to write non-error output to */
  int bVerbose = 0;               /* If -verbose is present */
  int bGroupByParent = 0;         /* If -groupbyparent is present */
  int i;                          /* To iterate through azArg[] */
  const char *zIndent = "";       /* How much to indent CREATE INDEX by */
  int rc;                         /* Return code */
  sqlite3_stmt *pSql = 0;         /* Compiled version of SQL statement below */

  /*
  ** This SELECT statement returns one row for each foreign key constraint
  ** in the schema of the main database. The column values are:
  **
  ** 0. The text of an SQL statement similar to:
  **
  **      "EXPLAIN QUERY PLAN SELECT 1 FROM child_table WHERE child_key=?"
  **
  **    This SELECT is similar to the one that the foreign keys implementation
  **    needs to run internally on child tables. If there is an index that can
  **    be used to optimize this query, then it can also be used by the FK
  **    implementation to optimize DELETE or UPDATE statements on the parent
  **    table.
  **
  ** 1. A GLOB pattern suitable for sqlite3_strglob(). If the plan output by
  **    the EXPLAIN QUERY PLAN command matches this pattern, then the schema
  **    contains an index that can be used to optimize the query.
  **
  ** 2. Human readable text that describes the child table and columns. e.g.
  **
  **       "child_table(child_key1, child_key2)"
  **
  ** 3. Human readable text that describes the parent table and columns. e.g.
  **
  **       "parent_table(parent_key1, parent_key2)"
  **
  ** 4. A full CREATE INDEX statement for an index that could be used to
  **    optimize DELETE or UPDATE statements on the parent table. e.g.
  **
  **       "CREATE INDEX child_table_child_key ON child_table(child_key)"
  **
  ** 5. The name of the parent table.
  **
  ** These six values are used by the C logic below to generate the report.
  */
  const char *zSql =
  "SELECT "
    "     'EXPLAIN QUERY PLAN SELECT 1 FROM ' || quote(s.name) || ' WHERE '"
    "  || group_concat(quote(s.name) || '.' || quote(f.[from]) || '=?' "
    "  || fkey_collate_clause("
    "       f.[table], COALESCE(f.[to], p.[name]), s.name, f.[from]),' AND ')"
    ", "
    "     'SEARCH TABLE ' || s.name || ' USING COVERING INDEX*('"
    "  || group_concat('*=?', ' AND ') || ')'"
    ", "
    "     s.name  || '(' || group_concat(f.[from],  ', ') || ')'"
    ", "
    "     f.[table] || '(' || group_concat(COALESCE(f.[to], p.[name])) || ')'"
    ", "
    "     'CREATE INDEX ' || quote(s.name ||'_'|| group_concat(f.[from], '_'))"
    "  || ' ON ' || quote(s.name) || '('"
    "  || group_concat(quote(f.[from]) ||"
    "        fkey_collate_clause("
    "          f.[table], COALESCE(f.[to], p.[name]), s.name, f.[from]), ', ')"
    "  || ');'"
    ", "
    "     f.[table] "
    "FROM sqlite_master AS s, pragma_foreign_key_list(s.name) AS f "
    "LEFT JOIN pragma_table_info AS p ON (pk-1=seq AND p.arg=f.[table]) "
    "GROUP BY s.name, f.id "
    "ORDER BY (CASE WHEN ? THEN f.[table] ELSE s.name END)"
  ;
  const char *zGlobIPK = "SEARCH TABLE * USING INTEGER PRIMARY KEY (rowid=?)";

  for(i=2; i<nArg; i++){
    int n = strlen30(azArg[i]);
    if( n>1 && sqlite3_strnicmp("-verbose", azArg[i], n)==0 ){
      bVerbose = 1;
    }
    else if( n>1 && sqlite3_strnicmp("-groupbyparent", azArg[i], n)==0 ){
      bGroupByParent = 1;
      zIndent = "    ";
    }
    else{
      raw_printf(stderr, "Usage: %s %s ?-verbose? ?-groupbyparent?\n",
          azArg[0], azArg[1]
      );
      return SQLITE_ERROR;
    }
  }

  /* Register the fkey_collate_clause() SQL function */
  rc = sqlite3_create_function(db, "fkey_collate_clause", 4, SQLITE_UTF8,
      0, shellFkeyCollateClause, 0, 0
  );


  if( rc==SQLITE_OK ){
    rc = sqlite3_prepare_v2(db, zSql, -1, &pSql, 0);
  }
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pSql, 1, bGroupByParent);
  }

  if( rc==SQLITE_OK ){
    int rc2;
    char *zPrev = 0;
    while( SQLITE_ROW==sqlite3_step(pSql) ){
      int res = -1;
      sqlite3_stmt *pExplain = 0;
      const char *zEQP = (const char*)sqlite3_column_text(pSql, 0);
      const char *zGlob = (const char*)sqlite3_column_text(pSql, 1);
      const char *zFrom = (const char*)sqlite3_column_text(pSql, 2);
      const char *zTarget = (const char*)sqlite3_column_text(pSql, 3);
      const char *zCI = (const char*)sqlite3_column_text(pSql, 4);
      const char *zParent = (const char*)sqlite3_column_text(pSql, 5);

      rc = sqlite3_prepare_v2(db, zEQP, -1, &pExplain, 0);
      if( rc!=SQLITE_OK ) break;
      if( SQLITE_ROW==sqlite3_step(pExplain) ){
        const char *zPlan = (const char*)sqlite3_column_text(pExplain, 3);
        res = (
              0==sqlite3_strglob(zGlob, zPlan)
           || 0==sqlite3_strglob(zGlobIPK, zPlan)
        );
      }
      rc = sqlite3_finalize(pExplain);
      if( rc!=SQLITE_OK ) break;

      if( res<0 ){
        raw_printf(stderr, "Error: internal error");
        break;
      }else{
        if( bGroupByParent
        && (bVerbose || res==0)
        && (zPrev==0 || sqlite3_stricmp(zParent, zPrev))
        ){
          raw_printf(out, "-- Parent table %s\n", zParent);
          sqlite3_free(zPrev);
          zPrev = sqlite3_mprintf("%s", zParent);
        }

        if( res==0 ){
          raw_printf(out, "%s%s --> %s\n", zIndent, zCI, zTarget);
        }else if( bVerbose ){
          raw_printf(out, "%s/* no extra indexes required for %s -> %s */\n",
              zIndent, zFrom, zTarget
          );
        }
      }
    }
    sqlite3_free(zPrev);

    if( rc!=SQLITE_OK ){
      raw_printf(stderr, "%s\n", sqlite3_errmsg(db));
    }

    rc2 = sqlite3_finalize(pSql);
    if( rc==SQLITE_OK && rc2!=SQLITE_OK ){
      rc = rc2;
      raw_printf(stderr, "%s\n", sqlite3_errmsg(db));
    }
  }else{
    raw_printf(stderr, "%s\n", sqlite3_errmsg(db));
  }

  return rc;
}

/*
** Implementation of ".lint" dot command.
*/
static int lintDotCommand(
  ShellState *pState,             /* Current shell tool state */
  char **azArg,                   /* Array of arguments passed to dot command */
  int nArg                        /* Number of entries in azArg[] */
){
  int n;
  n = (nArg>=2 ? strlen30(azArg[1]) : 0);
  if( n<1 || sqlite3_strnicmp(azArg[1], "fkey-indexes", n) ) goto usage;
  return lintFkeyIndexes(pState, azArg, nArg);

 usage:
  raw_printf(stderr, "Usage %s sub-command ?switches...?\n", azArg[0]);
  raw_printf(stderr, "Where sub-commands are:\n");
  raw_printf(stderr, "    fkey-indexes\n");
  return SQLITE_ERROR;
}

#if !defined(SQLITE_OMIT_VIRTUALTABLE) && defined(SQLITE_HAVE_ZLIB)
/*********************************************************************************
** The ".archive" or ".ar" command.
*/
static void shellPrepare(
  sqlite3 *db, 
  int *pRc, 
  const char *zSql, 
  sqlite3_stmt **ppStmt
){
  *ppStmt = 0;
  if( *pRc==SQLITE_OK ){
    int rc = sqlite3_prepare_v2(db, zSql, -1, ppStmt, 0);
    if( rc!=SQLITE_OK ){
      raw_printf(stderr, "sql error: %s (%d)\n", 
          sqlite3_errmsg(db), sqlite3_errcode(db)
      );
      *pRc = rc;
    }
  }
}

static void shellPreparePrintf(
  sqlite3 *db, 
  int *pRc, 
  sqlite3_stmt **ppStmt,
  const char *zFmt, 
  ...
){
  *ppStmt = 0;
  if( *pRc==SQLITE_OK ){
    va_list ap;
    char *z;
    va_start(ap, zFmt);
    z = sqlite3_vmprintf(zFmt, ap);
    if( z==0 ){
      *pRc = SQLITE_NOMEM;
    }else{
      shellPrepare(db, pRc, z, ppStmt);
      sqlite3_free(z);
    }
  }
}

static void shellFinalize(
  int *pRc, 
  sqlite3_stmt *pStmt
){
  if( pStmt ){
    sqlite3 *db = sqlite3_db_handle(pStmt);
    int rc = sqlite3_finalize(pStmt);
    if( *pRc==SQLITE_OK ){
      if( rc!=SQLITE_OK ){
        raw_printf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
      }
      *pRc = rc;
    }
  }
}

static void shellReset(
  int *pRc, 
  sqlite3_stmt *pStmt
){
  int rc = sqlite3_reset(pStmt);
  if( *pRc==SQLITE_OK ){
    if( rc!=SQLITE_OK ){
      sqlite3 *db = sqlite3_db_handle(pStmt);
      raw_printf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
    }
    *pRc = rc;
  }
}
/*
** Structure representing a single ".ar" command.
*/
typedef struct ArCommand ArCommand;
struct ArCommand {
  u8 eCmd;                        /* An AR_CMD_* value */
  u8 bVerbose;                    /* True if --verbose */
  u8 bZip;                        /* True if the archive is a ZIP */
  u8 bDryRun;                     /* True if --dry-run */
  u8 bAppend;                     /* True if --append */
  u8 fromCmdLine;                 /* Run from -A instead of .archive */
  int nArg;                       /* Number of command arguments */
  char *zSrcTable;                /* "sqlar", "zipfile($file)" or "zip" */
  const char *zFile;              /* --file argument, or NULL */
  const char *zDir;               /* --directory argument, or NULL */
  char **azArg;                   /* Array of command arguments */
  ShellState *p;                  /* Shell state */
  sqlite3 *db;                    /* Database containing the archive */
};

/*
** Print a usage message for the .ar command to stderr and return SQLITE_ERROR.
*/
static int arUsage(FILE *f){
  raw_printf(f,
"\n"
"Usage: .ar [OPTION...] [FILE...]\n"
"The .ar command manages sqlar archives.\n"
"\n"
"Examples:\n"
"  .ar -cf archive.sar foo bar    # Create archive.sar from files foo and bar\n"
"  .ar -tf archive.sar            # List members of archive.sar\n"
"  .ar -xvf archive.sar           # Verbosely extract files from archive.sar\n"
"\n"
"Each command line must feature exactly one command option:\n"
"  -c, --create               Create a new archive\n"
"  -u, --update               Update or add files to an existing archive\n"
"  -t, --list                 List contents of archive\n"
"  -x, --extract              Extract files from archive\n"
"\n"
"And zero or more optional options:\n"
"  -v, --verbose              Print each filename as it is processed\n"
"  -f FILE, --file FILE       Operate on archive FILE (default is current db)\n"
"  -a FILE, --append FILE     Operate on FILE opened using the apndvfs VFS\n"
"  -C DIR, --directory DIR    Change to directory DIR to read/extract files\n"
"  -n, --dryrun               Show the SQL that would have occurred\n"
"\n"
"See also: http://sqlite.org/cli.html#sqlar_archive_support\n"
"\n"
);
  return SQLITE_ERROR;
}

/*
** Print an error message for the .ar command to stderr and return 
** SQLITE_ERROR.
*/
static int arErrorMsg(ArCommand *pAr, const char *zFmt, ...){
  va_list ap;
  char *z;
  va_start(ap, zFmt);
  z = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
  utf8_printf(stderr, "Error: %s\n", z);
  if( pAr->fromCmdLine ){
    utf8_printf(stderr, "Use \"-A\" for more help\n");
  }else{
    utf8_printf(stderr, "Use \".archive --help\" for more help\n");
  }
  sqlite3_free(z);
  return SQLITE_ERROR;
}

/*
** Values for ArCommand.eCmd.
*/
#define AR_CMD_CREATE       1
#define AR_CMD_EXTRACT      2
#define AR_CMD_LIST         3
#define AR_CMD_UPDATE       4
#define AR_CMD_HELP         5

/*
** Other (non-command) switches.
*/
#define AR_SWITCH_VERBOSE     6
#define AR_SWITCH_FILE        7
#define AR_SWITCH_DIRECTORY   8
#define AR_SWITCH_APPEND      9
#define AR_SWITCH_DRYRUN     10

static int arProcessSwitch(ArCommand *pAr, int eSwitch, const char *zArg){
  switch( eSwitch ){
    case AR_CMD_CREATE:
    case AR_CMD_EXTRACT:
    case AR_CMD_LIST:
    case AR_CMD_UPDATE:
    case AR_CMD_HELP:
      if( pAr->eCmd ){
        return arErrorMsg(pAr, "multiple command options");
      }
      pAr->eCmd = eSwitch;
      break;

    case AR_SWITCH_DRYRUN:
      pAr->bDryRun = 1;
      break;
    case AR_SWITCH_VERBOSE:
      pAr->bVerbose = 1;
      break;
    case AR_SWITCH_APPEND:
      pAr->bAppend = 1;
      /* Fall thru into --file */
    case AR_SWITCH_FILE:
      pAr->zFile = zArg;
      break;
    case AR_SWITCH_DIRECTORY:
      pAr->zDir = zArg;
      break;
  }

  return SQLITE_OK;
}

/*
** Parse the command line for an ".ar" command. The results are written into
** structure (*pAr). SQLITE_OK is returned if the command line is parsed
** successfully, otherwise an error message is written to stderr and 
** SQLITE_ERROR returned.
*/
static int arParseCommand(
  char **azArg,                   /* Array of arguments passed to dot command */
  int nArg,                       /* Number of entries in azArg[] */
  ArCommand *pAr                  /* Populate this object */
){
  struct ArSwitch {
    const char *zLong;
    char cShort;
    u8 eSwitch;
    u8 bArg;
  } aSwitch[] = {
    { "create",    'c', AR_CMD_CREATE,       0 },
    { "extract",   'x', AR_CMD_EXTRACT,      0 },
    { "list",      't', AR_CMD_LIST,         0 },
    { "update",    'u', AR_CMD_UPDATE,       0 },
    { "help",      'h', AR_CMD_HELP,         0 },
    { "verbose",   'v', AR_SWITCH_VERBOSE,   0 },
    { "file",      'f', AR_SWITCH_FILE,      1 },
    { "append",    'a', AR_SWITCH_APPEND,    1 },
    { "directory", 'C', AR_SWITCH_DIRECTORY, 1 },
    { "dryrun",    'n', AR_SWITCH_DRYRUN,    0 },
  };
  int nSwitch = sizeof(aSwitch) / sizeof(struct ArSwitch);
  struct ArSwitch *pEnd = &aSwitch[nSwitch];

  if( nArg<=1 ){
    return arUsage(stderr);
  }else{
    char *z = azArg[1];
    if( z[0]!='-' ){
      /* Traditional style [tar] invocation */
      int i;
      int iArg = 2;
      for(i=0; z[i]; i++){
        const char *zArg = 0;
        struct ArSwitch *pOpt;
        for(pOpt=&aSwitch[0]; pOpt<pEnd; pOpt++){
          if( z[i]==pOpt->cShort ) break;
        }
        if( pOpt==pEnd ){
          return arErrorMsg(pAr, "unrecognized option: %c", z[i]);
        }
        if( pOpt->bArg ){
          if( iArg>=nArg ){
            return arErrorMsg(pAr, "option requires an argument: %c",z[i]);
          }
          zArg = azArg[iArg++];
        }
        if( arProcessSwitch(pAr, pOpt->eSwitch, zArg) ) return SQLITE_ERROR;
      }
      pAr->nArg = nArg-iArg;
      if( pAr->nArg>0 ){
        pAr->azArg = &azArg[iArg];
      }
    }else{
      /* Non-traditional invocation */
      int iArg;
      for(iArg=1; iArg<nArg; iArg++){
        int n;
        z = azArg[iArg];
        if( z[0]!='-' ){
          /* All remaining command line words are command arguments. */
          pAr->azArg = &azArg[iArg];
          pAr->nArg = nArg-iArg;
          break;
        }
        n = strlen30(z);

        if( z[1]!='-' ){
          int i;
          /* One or more short options */
          for(i=1; i<n; i++){
            const char *zArg = 0;
            struct ArSwitch *pOpt;
            for(pOpt=&aSwitch[0]; pOpt<pEnd; pOpt++){
              if( z[i]==pOpt->cShort ) break;
            }
            if( pOpt==pEnd ){
              return arErrorMsg(pAr, "unrecognized option: %c", z[i]);
            }
            if( pOpt->bArg ){
              if( i<(n-1) ){
                zArg = &z[i+1];
                i = n;
              }else{
                if( iArg>=(nArg-1) ){
                  return arErrorMsg(pAr, "option requires an argument: %c",z[i]);
                }
                zArg = azArg[++iArg];
              }
            }
            if( arProcessSwitch(pAr, pOpt->eSwitch, zArg) ) return SQLITE_ERROR;
          }
        }else if( z[2]=='\0' ){
          /* A -- option, indicating that all remaining command line words
          ** are command arguments.  */
          pAr->azArg = &azArg[iArg+1];
          pAr->nArg = nArg-iArg-1;
          break;
        }else{
          /* A long option */
          const char *zArg = 0;             /* Argument for option, if any */
          struct ArSwitch *pMatch = 0;      /* Matching option */
          struct ArSwitch *pOpt;            /* Iterator */
          for(pOpt=&aSwitch[0]; pOpt<pEnd; pOpt++){
            const char *zLong = pOpt->zLong;
            if( (n-2)<=strlen30(zLong) && 0==memcmp(&z[2], zLong, n-2) ){
              if( pMatch ){
                return arErrorMsg(pAr, "ambiguous option: %s",z);
              }else{
                pMatch = pOpt;
              }
            }
          }

          if( pMatch==0 ){
            return arErrorMsg(pAr, "unrecognized option: %s", z);
          }
          if( pMatch->bArg ){
            if( iArg>=(nArg-1) ){
              return arErrorMsg(pAr, "option requires an argument: %s", z);
            }
            zArg = azArg[++iArg];
          }
          if( arProcessSwitch(pAr, pMatch->eSwitch, zArg) ) return SQLITE_ERROR;
        }
      }
    }
  }

  return SQLITE_OK;
}

/*
** This function assumes that all arguments within the ArCommand.azArg[]
** array refer to archive members, as for the --extract or --list commands. 
** It checks that each of them are present. If any specified file is not
** present in the archive, an error is printed to stderr and an error
** code returned. Otherwise, if all specified arguments are present in
** the archive, SQLITE_OK is returned.
**
** This function strips any trailing '/' characters from each argument.
** This is consistent with the way the [tar] command seems to work on
** Linux.
*/
static int arCheckEntries(ArCommand *pAr){
  int rc = SQLITE_OK;
  if( pAr->nArg ){
    int i, j;
    sqlite3_stmt *pTest = 0;

    shellPreparePrintf(pAr->db, &rc, &pTest,
        "SELECT name FROM %s WHERE name=$name", 
        pAr->zSrcTable
    );
    j = sqlite3_bind_parameter_index(pTest, "$name");
    for(i=0; i<pAr->nArg && rc==SQLITE_OK; i++){
      char *z = pAr->azArg[i];
      int n = strlen30(z);
      int bOk = 0;
      while( n>0 && z[n-1]=='/' ) n--;
      z[n] = '\0';
      sqlite3_bind_text(pTest, j, z, -1, SQLITE_STATIC);
      if( SQLITE_ROW==sqlite3_step(pTest) ){
        bOk = 1;
      }
      shellReset(&rc, pTest);
      if( rc==SQLITE_OK && bOk==0 ){
        utf8_printf(stderr, "not found in archive: %s\n", z);
        rc = SQLITE_ERROR;
      }
    }
    shellFinalize(&rc, pTest);
  }
  return rc;
}

/*
** Format a WHERE clause that can be used against the "sqlar" table to
** identify all archive members that match the command arguments held
** in (*pAr). Leave this WHERE clause in (*pzWhere) before returning.
** The caller is responsible for eventually calling sqlite3_free() on
** any non-NULL (*pzWhere) value.
*/
static void arWhereClause(
  int *pRc, 
  ArCommand *pAr, 
  char **pzWhere                  /* OUT: New WHERE clause */
){
  char *zWhere = 0;
  if( *pRc==SQLITE_OK ){
    if( pAr->nArg==0 ){
      zWhere = sqlite3_mprintf("1");
    }else{
      int i;
      const char *zSep = "";
      for(i=0; i<pAr->nArg; i++){
        const char *z = pAr->azArg[i];
        zWhere = sqlite3_mprintf(
          "%z%s name = '%q' OR substr(name,1,%d) = '%q/'", 
          zWhere, zSep, z, strlen30(z)+1, z
        );
        if( zWhere==0 ){
          *pRc = SQLITE_NOMEM;
          break;
        }
        zSep = " OR ";
      }
    }
  }
  *pzWhere = zWhere;
}

/*
** Implementation of .ar "lisT" command. 
*/
static int arListCommand(ArCommand *pAr){
  const char *zSql = "SELECT %s FROM %s WHERE %s"; 
  const char *azCols[] = {
    "name",
    "lsmode(mode), sz, datetime(mtime, 'unixepoch'), name"
  };

  char *zWhere = 0;
  sqlite3_stmt *pSql = 0;
  int rc;

  rc = arCheckEntries(pAr);
  arWhereClause(&rc, pAr, &zWhere);

  shellPreparePrintf(pAr->db, &rc, &pSql, zSql, azCols[pAr->bVerbose],
                     pAr->zSrcTable, zWhere);
  if( pAr->bDryRun ){
    utf8_printf(pAr->p->out, "%s\n", sqlite3_sql(pSql));
  }else{
    while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSql) ){
      if( pAr->bVerbose ){
        utf8_printf(pAr->p->out, "%s % 10d  %s  %s\n",
            sqlite3_column_text(pSql, 0),
            sqlite3_column_int(pSql, 1), 
            sqlite3_column_text(pSql, 2),
            sqlite3_column_text(pSql, 3)
        );
      }else{
        utf8_printf(pAr->p->out, "%s\n", sqlite3_column_text(pSql, 0));
      }
    }
  }
  shellFinalize(&rc, pSql);
  sqlite3_free(zWhere);
  return rc;
}


/*
** Implementation of .ar "eXtract" command. 
*/
static int arExtractCommand(ArCommand *pAr){
  const char *zSql1 = 
    "SELECT "
    " ($dir || name),"
    " writefile(($dir || name), %s, mode, mtime) "
    "FROM %s WHERE (%s) AND (data IS NULL OR $dirOnly = 0)"
    " AND name NOT GLOB '*..[/\\]*'";

  const char *azExtraArg[] = { 
    "sqlar_uncompress(data, sz)",
    "data"
  };

  sqlite3_stmt *pSql = 0;
  int rc = SQLITE_OK;
  char *zDir = 0;
  char *zWhere = 0;
  int i, j;

  /* If arguments are specified, check that they actually exist within
  ** the archive before proceeding. And formulate a WHERE clause to
  ** match them.  */
  rc = arCheckEntries(pAr);
  arWhereClause(&rc, pAr, &zWhere);

  if( rc==SQLITE_OK ){
    if( pAr->zDir ){
      zDir = sqlite3_mprintf("%s/", pAr->zDir);
    }else{
      zDir = sqlite3_mprintf("");
    }
    if( zDir==0 ) rc = SQLITE_NOMEM;
  }

  shellPreparePrintf(pAr->db, &rc, &pSql, zSql1, 
      azExtraArg[pAr->bZip], pAr->zSrcTable, zWhere
  );

  if( rc==SQLITE_OK ){
    j = sqlite3_bind_parameter_index(pSql, "$dir");
    sqlite3_bind_text(pSql, j, zDir, -1, SQLITE_STATIC);

    /* Run the SELECT statement twice. The first time, writefile() is called
    ** for all archive members that should be extracted. The second time,
    ** only for the directories. This is because the timestamps for
    ** extracted directories must be reset after they are populated (as
    ** populating them changes the timestamp).  */
    for(i=0; i<2; i++){
      j = sqlite3_bind_parameter_index(pSql, "$dirOnly");
      sqlite3_bind_int(pSql, j, i);
      if( pAr->bDryRun ){
        utf8_printf(pAr->p->out, "%s\n", sqlite3_sql(pSql));
      }else{
        while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSql) ){
          if( i==0 && pAr->bVerbose ){
            utf8_printf(pAr->p->out, "%s\n", sqlite3_column_text(pSql, 0));
          }
        }
      }
      shellReset(&rc, pSql);
    }
    shellFinalize(&rc, pSql);
  }

  sqlite3_free(zDir);
  sqlite3_free(zWhere);
  return rc;
}

/*
** Run the SQL statement in zSql.  Or if doing a --dryrun, merely print it out.
*/
static int arExecSql(ArCommand *pAr, const char *zSql){
  int rc;
  if( pAr->bDryRun ){
    utf8_printf(pAr->p->out, "%s\n", zSql);
    rc = SQLITE_OK;
  }else{
    char *zErr = 0;
    rc = sqlite3_exec(pAr->db, zSql, 0, 0, &zErr);
    if( zErr ){
      utf8_printf(stdout, "ERROR: %s\n", zErr);
      sqlite3_free(zErr);
    }
  }
  return rc;
}


/*
** Implementation of .ar "create" and "update" commands.
**
** Create the "sqlar" table in the database if it does not already exist.
** Then add each file in the azFile[] array to the archive. Directories
** are added recursively. If argument bVerbose is non-zero, a message is
** printed on stdout for each file archived.
**
** The create command is the same as update, except that it drops
** any existing "sqlar" table before beginning.
*/
static int arCreateOrUpdateCommand(
  ArCommand *pAr,                 /* Command arguments and options */
  int bUpdate                     /* true for a --create.  false for --update */
){
  const char *zCreate = 
      "CREATE TABLE IF NOT EXISTS sqlar(\n"
      "  name TEXT PRIMARY KEY,  -- name of the file\n"
      "  mode INT,               -- access permissions\n"
      "  mtime INT,              -- last modification time\n"
      "  sz INT,                 -- original file size\n"
      "  data BLOB               -- compressed content\n"
      ")";
  const char *zDrop = "DROP TABLE IF EXISTS sqlar";
  const char *zInsertFmt[2] = {
     "REPLACE INTO %s(name,mode,mtime,sz,data)\n"
     "  SELECT\n"
     "    %s,\n"
     "    mode,\n"
     "    mtime,\n"
     "    CASE substr(lsmode(mode),1,1)\n"
     "      WHEN '-' THEN length(data)\n"
     "      WHEN 'd' THEN 0\n"
     "      ELSE -1 END,\n"
     "    sqlar_compress(data)\n"
     "  FROM fsdir(%Q,%Q)\n"
     "  WHERE lsmode(mode) NOT LIKE '?%%';",
     "REPLACE INTO %s(name,mode,mtime,data)\n"
     "  SELECT\n"
     "    %s,\n"
     "    mode,\n"
     "    mtime,\n"
     "    data\n"
     "  FROM fsdir(%Q,%Q)\n"
     "  WHERE lsmode(mode) NOT LIKE '?%%';"
  };
  int i;                          /* For iterating through azFile[] */
  int rc;                         /* Return code */
  const char *zTab = 0;           /* SQL table into which to insert */
  char *zSql;
  char zTemp[50];

  arExecSql(pAr, "PRAGMA page_size=512");
  rc = arExecSql(pAr, "SAVEPOINT ar;");
  if( rc!=SQLITE_OK ) return rc;
  zTemp[0] = 0; 
  if( pAr->bZip ){
    /* Initialize the zipfile virtual table, if necessary */
    if( pAr->zFile ){
      sqlite3_uint64 r;
      sqlite3_randomness(sizeof(r),&r);
      sqlite3_snprintf(sizeof(zTemp),zTemp,"zip%016llx",r);
      zTab = zTemp;
      zSql = sqlite3_mprintf(
         "CREATE VIRTUAL TABLE temp.%s USING zipfile(%Q)",
         zTab, pAr->zFile
      );
      rc = arExecSql(pAr, zSql);
      sqlite3_free(zSql);
    }else{
      zTab = "zip";
    }
  }else{
    /* Initialize the table for an SQLAR */
    zTab = "sqlar";
    if( bUpdate==0 ){
      rc = arExecSql(pAr, zDrop);
      if( rc!=SQLITE_OK ) goto end_ar_transaction;
    }
    rc = arExecSql(pAr, zCreate);
  }
  for(i=0; i<pAr->nArg && rc==SQLITE_OK; i++){
    char *zSql2 = sqlite3_mprintf(zInsertFmt[pAr->bZip], zTab,
        pAr->bVerbose ? "shell_putsnl(name)" : "name",
        pAr->azArg[i], pAr->zDir);
    rc = arExecSql(pAr, zSql2);
    sqlite3_free(zSql2);
  }
end_ar_transaction:
  if( rc!=SQLITE_OK ){
    arExecSql(pAr, "ROLLBACK TO ar; RELEASE ar;");
  }else{
    rc = arExecSql(pAr, "RELEASE ar;");
    if( pAr->bZip && pAr->zFile ){
      zSql = sqlite3_mprintf("DROP TABLE %s", zTemp);
      arExecSql(pAr, zSql);
      sqlite3_free(zSql);
    }
  }
  return rc;
}

/*
** Implementation of ".ar" dot command.
*/
static int arDotCommand(
  ShellState *pState,             /* Current shell tool state */
  int fromCmdLine,                /* True if -A command-line option, not .ar cmd */
  char **azArg,                   /* Array of arguments passed to dot command */
  int nArg                        /* Number of entries in azArg[] */
){
  ArCommand cmd;
  int rc;
  memset(&cmd, 0, sizeof(cmd));
  cmd.fromCmdLine = fromCmdLine;
  rc = arParseCommand(azArg, nArg, &cmd);
  if( rc==SQLITE_OK ){
    int eDbType = SHELL_OPEN_UNSPEC;
    cmd.p = pState;
    cmd.db = pState->db;
    if( cmd.zFile ){
      eDbType = deduceDatabaseType(cmd.zFile, 1);
    }else{
      eDbType = pState->openMode;
    }
    if( eDbType==SHELL_OPEN_ZIPFILE ){
      if( cmd.eCmd==AR_CMD_EXTRACT || cmd.eCmd==AR_CMD_LIST ){
        if( cmd.zFile==0 ){
          cmd.zSrcTable = sqlite3_mprintf("zip");
        }else{
          cmd.zSrcTable = sqlite3_mprintf("zipfile(%Q)", cmd.zFile);
        }
      }
      cmd.bZip = 1;
    }else if( cmd.zFile ){
      int flags;
      if( cmd.bAppend ) eDbType = SHELL_OPEN_APPENDVFS;
      if( cmd.eCmd==AR_CMD_CREATE || cmd.eCmd==AR_CMD_UPDATE ){
        flags = SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE;
      }else{
        flags = SQLITE_OPEN_READONLY;
      }
      cmd.db = 0;
      if( cmd.bDryRun ){
        utf8_printf(pState->out, "-- open database '%s'%s\n", cmd.zFile,
             eDbType==SHELL_OPEN_APPENDVFS ? " using 'apndvfs'" : "");
      }
      rc = sqlite3_open_v2(cmd.zFile, &cmd.db, flags, 
             eDbType==SHELL_OPEN_APPENDVFS ? "apndvfs" : 0);
      if( rc!=SQLITE_OK ){
        utf8_printf(stderr, "cannot open file: %s (%s)\n", 
            cmd.zFile, sqlite3_errmsg(cmd.db)
        );
        goto end_ar_command;
      }
      sqlite3_fileio_init(cmd.db, 0, 0);
      sqlite3_sqlar_init(cmd.db, 0, 0);
      sqlite3_create_function(cmd.db, "shell_putsnl", 1, SQLITE_UTF8, cmd.p,
                              shellPutsFunc, 0, 0);

    }
    if( cmd.zSrcTable==0 && cmd.bZip==0 && cmd.eCmd!=AR_CMD_HELP ){
      if( cmd.eCmd!=AR_CMD_CREATE
       && sqlite3_table_column_metadata(cmd.db,0,"sqlar","name",0,0,0,0,0)
      ){
        utf8_printf(stderr, "database does not contain an 'sqlar' table\n");
        rc = SQLITE_ERROR;
        goto end_ar_command;
      }
      cmd.zSrcTable = sqlite3_mprintf("sqlar");
    }

    switch( cmd.eCmd ){
      case AR_CMD_CREATE:
        rc = arCreateOrUpdateCommand(&cmd, 0);
        break;

      case AR_CMD_EXTRACT:
        rc = arExtractCommand(&cmd);
        break;

      case AR_CMD_LIST:
        rc = arListCommand(&cmd);
        break;

      case AR_CMD_HELP:
        arUsage(pState->out);
        break;

      default:
        assert( cmd.eCmd==AR_CMD_UPDATE );
        rc = arCreateOrUpdateCommand(&cmd, 1);
        break;
    }
  }
end_ar_command:
  if( cmd.db!=pState->db ){
    close_db(cmd.db);
  }
  sqlite3_free(cmd.zSrcTable);

  return rc;
}
/* End of the ".archive" or ".ar" command logic
**********************************************************************************/
#endif /* !defined(SQLITE_OMIT_VIRTUALTABLE) && defined(SQLITE_HAVE_ZLIB) */


/*
** If an input line begins with "." then invoke this routine to
** process that line.
**
** Return 1 on error, 2 to exit, and 0 otherwise.
*/
static int do_meta_command(char *zLine, ShellState *p){
  int h = 1;
  int nArg = 0;
  int n, c;
  int rc = 0;
  char *azArg[50];

#ifndef SQLITE_OMIT_VIRTUALTABLE
  if( p->expert.pExpert ){
    expertFinish(p, 1, 0);
  }
#endif

  /* Parse the input line into tokens.
  */
  while( zLine[h] && nArg<ArraySize(azArg) ){
    while( IsSpace(zLine[h]) ){ h++; }
    if( zLine[h]==0 ) break;
    if( zLine[h]=='\'' || zLine[h]=='"' ){
      int delim = zLine[h++];
      azArg[nArg++] = &zLine[h];
      while( zLine[h] && zLine[h]!=delim ){
        if( zLine[h]=='\\' && delim=='"' && zLine[h+1]!=0 ) h++;
        h++;
      }
      if( zLine[h]==delim ){
        zLine[h++] = 0;
      }
      if( delim=='"' ) resolve_backslashes(azArg[nArg-1]);
    }else{
      azArg[nArg++] = &zLine[h];
      while( zLine[h] && !IsSpace(zLine[h]) ){ h++; }
      if( zLine[h] ) zLine[h++] = 0;
      resolve_backslashes(azArg[nArg-1]);
    }
  }

  /* Process the input line.
  */
  if( nArg==0 ) return 0; /* no tokens, no error */
  n = strlen30(azArg[0]);
  c = azArg[0][0];
  clearTempFile(p);

#ifndef SQLITE_OMIT_AUTHORIZATION
  if( c=='a' && strncmp(azArg[0], "auth", n)==0 ){
    if( nArg!=2 ){
      raw_printf(stderr, "Usage: .auth ON|OFF\n");
      rc = 1;
      goto meta_command_exit;
    }
    open_db(p, 0);
    if( booleanValue(azArg[1]) ){
      sqlite3_set_authorizer(p->db, shellAuth, p);
    }else{
      sqlite3_set_authorizer(p->db, 0, 0);
    }
  }else
#endif

#if !defined(SQLITE_OMIT_VIRTUALTABLE) && defined(SQLITE_HAVE_ZLIB)
  if( c=='a' && strncmp(azArg[0], "archive", n)==0 ){
    open_db(p, 0);
    rc = arDotCommand(p, 0, azArg, nArg);
  }else
#endif

  if( (c=='b' && n>=3 && strncmp(azArg[0], "backup", n)==0)
   || (c=='s' && n>=3 && strncmp(azArg[0], "save", n)==0)
  ){
    const char *zDestFile = 0;
    const char *zDb = 0;
    sqlite3 *pDest;
    sqlite3_backup *pBackup;
    int j;
    const char *zVfs = 0;
    for(j=1; j<nArg; j++){
      const char *z = azArg[j];
      if( z[0]=='-' ){
        if( z[1]=='-' ) z++;
        if( strcmp(z, "-append")==0 ){
          zVfs = "apndvfs";
        }else
        {
          utf8_printf(stderr, "unknown option: %s\n", azArg[j]);
          return 1;
        }
      }else if( zDestFile==0 ){
        zDestFile = azArg[j];
      }else if( zDb==0 ){
        zDb = zDestFile;
        zDestFile = azArg[j];
      }else{
        raw_printf(stderr, "Usage: .backup ?DB? ?--append? FILENAME\n");
        return 1;
      }
    }
    if( zDestFile==0 ){
      raw_printf(stderr, "missing FILENAME argument on .backup\n");
      return 1;
    }
    if( zDb==0 ) zDb = "main";
    rc = sqlite3_open_v2(zDestFile, &pDest, 
                  SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, zVfs);
    if( rc!=SQLITE_OK ){
      utf8_printf(stderr, "Error: cannot open \"%s\"\n", zDestFile);
      close_db(pDest);
      return 1;
    }
    open_db(p, 0);
    pBackup = sqlite3_backup_init(pDest, "main", p->db, zDb);
    if( pBackup==0 ){
      utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(pDest));
      close_db(pDest);
      return 1;
    }
    while(  (rc = sqlite3_backup_step(pBackup,100))==SQLITE_OK ){}
    sqlite3_backup_finish(pBackup);
    if( rc==SQLITE_DONE ){
      rc = 0;
    }else{
      utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(pDest));
      rc = 1;
    }
    close_db(pDest);
  }else

  if( c=='b' && n>=3 && strncmp(azArg[0], "bail", n)==0 ){
    if( nArg==2 ){
      bail_on_error = booleanValue(azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .bail on|off\n");
      rc = 1;
    }
  }else

  if( c=='b' && n>=3 && strncmp(azArg[0], "binary", n)==0 ){
    if( nArg==2 ){
      if( booleanValue(azArg[1]) ){
        setBinaryMode(p->out, 1);
      }else{
        setTextMode(p->out, 1);
      }
    }else{
      raw_printf(stderr, "Usage: .binary on|off\n");
      rc = 1;
    }
  }else

  if( c=='c' && strcmp(azArg[0],"cd")==0 ){
    if( nArg==2 ){
#if defined(_WIN32) || defined(WIN32)
      wchar_t *z = sqlite3_win32_utf8_to_unicode(azArg[1]);
      rc = !SetCurrentDirectoryW(z);
      sqlite3_free(z);
#else
      rc = chdir(azArg[1]);
#endif
      if( rc ){
        utf8_printf(stderr, "Cannot change to directory \"%s\"\n", azArg[1]);
        rc = 1;
      }
    }else{
      raw_printf(stderr, "Usage: .cd DIRECTORY\n");
      rc = 1;
    }
  }else

  /* The undocumented ".breakpoint" command causes a call to the no-op
  ** routine named test_breakpoint().
  */
  if( c=='b' && n>=3 && strncmp(azArg[0], "breakpoint", n)==0 ){
    test_breakpoint();
  }else

  if( c=='c' && n>=3 && strncmp(azArg[0], "changes", n)==0 ){
    if( nArg==2 ){
      setOrClearFlag(p, SHFLG_CountChanges, azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .changes on|off\n");
      rc = 1;
    }
  }else

  /* Cancel output redirection, if it is currently set (by .testcase)
  ** Then read the content of the testcase-out.txt file and compare against
  ** azArg[1].  If there are differences, report an error and exit.
  */
  if( c=='c' && n>=3 && strncmp(azArg[0], "check", n)==0 ){
    char *zRes = 0;
    output_reset(p);
    if( nArg!=2 ){
      raw_printf(stderr, "Usage: .check GLOB-PATTERN\n");
      rc = 2;
    }else if( (zRes = readFile("testcase-out.txt", 0))==0 ){
      raw_printf(stderr, "Error: cannot read 'testcase-out.txt'\n");
      rc = 2;
    }else if( testcase_glob(azArg[1],zRes)==0 ){
      utf8_printf(stderr,
                 "testcase-%s FAILED\n Expected: [%s]\n      Got: [%s]\n",
                 p->zTestcase, azArg[1], zRes);
      rc = 1;
    }else{
      utf8_printf(stdout, "testcase-%s ok\n", p->zTestcase);
      p->nCheck++;
    }
    sqlite3_free(zRes);
  }else

  if( c=='c' && strncmp(azArg[0], "clone", n)==0 ){
    if( nArg==2 ){
      tryToClone(p, azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .clone FILENAME\n");
      rc = 1;
    }
  }else

  if( c=='d' && n>1 && strncmp(azArg[0], "databases", n)==0 ){
    ShellState data;
    char *zErrMsg = 0;
    open_db(p, 0);
    memcpy(&data, p, sizeof(data));
    data.showHeader = 0;
    data.cMode = data.mode = MODE_List;
    sqlite3_snprintf(sizeof(data.colSeparator),data.colSeparator,": ");
    data.cnt = 0;
    sqlite3_exec(p->db, "SELECT name, file FROM pragma_database_list",
                 callback, &data, &zErrMsg);
    if( zErrMsg ){
      utf8_printf(stderr,"Error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
      rc = 1;
    }
  }else

  if( c=='d' && n>=3 && strncmp(azArg[0], "dbconfig", n)==0 ){
    static const struct DbConfigChoices {const char *zName; int op;} aDbConfig[] = {
        { "enable_fkey",      SQLITE_DBCONFIG_ENABLE_FKEY            },
        { "enable_trigger",   SQLITE_DBCONFIG_ENABLE_TRIGGER         },
        { "fts3_tokenizer",   SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER  },
        { "load_extension",   SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION  },
        { "no_ckpt_on_close", SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE       },
        { "enable_qpsg",      SQLITE_DBCONFIG_ENABLE_QPSG            },
        { "trigger_eqp",      SQLITE_DBCONFIG_TRIGGER_EQP            },
        { "reset_database",   SQLITE_DBCONFIG_RESET_DATABASE         },
    };
    int ii, v;
    open_db(p, 0);
    for(ii=0; ii<ArraySize(aDbConfig); ii++){
      if( nArg>1 && strcmp(azArg[1], aDbConfig[ii].zName)!=0 ) continue;
      if( nArg>=3 ){
        sqlite3_db_config(p->db, aDbConfig[ii].op, booleanValue(azArg[2]), 0);
      }
      sqlite3_db_config(p->db, aDbConfig[ii].op, -1, &v);
      utf8_printf(p->out, "%18s %s\n", aDbConfig[ii].zName, v ? "on" : "off");
      if( nArg>1 ) break;
    }
    if( nArg>1 && ii==ArraySize(aDbConfig) ){
      utf8_printf(stderr, "Error: unknown dbconfig \"%s\"\n", azArg[1]);
      utf8_printf(stderr, "Enter \".dbconfig\" with no arguments for a list\n");
    }   
  }else

  if( c=='d' && n>=3 && strncmp(azArg[0], "dbinfo", n)==0 ){
    rc = shell_dbinfo_command(p, nArg, azArg);
  }else

  if( c=='d' && strncmp(azArg[0], "dump", n)==0 ){
    const char *zLike = 0;
    int i;
    int savedShowHeader = p->showHeader;
    int savedShellFlags = p->shellFlgs;
    ShellClearFlag(p, SHFLG_PreserveRowid|SHFLG_Newlines|SHFLG_Echo);
    for(i=1; i<nArg; i++){
      if( azArg[i][0]=='-' ){
        const char *z = azArg[i]+1;
        if( z[0]=='-' ) z++;
        if( strcmp(z,"preserve-rowids")==0 ){
#ifdef SQLITE_OMIT_VIRTUALTABLE
          raw_printf(stderr, "The --preserve-rowids option is not compatible"
                             " with SQLITE_OMIT_VIRTUALTABLE\n");
          rc = 1;
          goto meta_command_exit;
#else
          ShellSetFlag(p, SHFLG_PreserveRowid);
#endif
        }else
        if( strcmp(z,"newlines")==0 ){
          ShellSetFlag(p, SHFLG_Newlines);
        }else
        {
          raw_printf(stderr, "Unknown option \"%s\" on \".dump\"\n", azArg[i]);
          rc = 1;
          goto meta_command_exit;
        }
      }else if( zLike ){
        raw_printf(stderr, "Usage: .dump ?--preserve-rowids? "
                           "?--newlines? ?LIKE-PATTERN?\n");
        rc = 1;
        goto meta_command_exit;
      }else{
        zLike = azArg[i];
      }
    }
    open_db(p, 0);
    /* When playing back a "dump", the content might appear in an order
    ** which causes immediate foreign key constraints to be violated.
    ** So disable foreign-key constraint enforcement to prevent problems. */
    raw_printf(p->out, "PRAGMA foreign_keys=OFF;\n");
    raw_printf(p->out, "BEGIN TRANSACTION;\n");
    p->writableSchema = 0;
    p->showHeader = 0;
    /* Set writable_schema=ON since doing so forces SQLite to initialize
    ** as much of the schema as it can even if the sqlite_master table is
    ** corrupt. */
    sqlite3_exec(p->db, "SAVEPOINT dump; PRAGMA writable_schema=ON", 0, 0, 0);
    p->nErr = 0;
    if( zLike==0 ){
      run_schema_dump_query(p,
        "SELECT name, type, sql FROM sqlite_master "
        "WHERE sql NOT NULL AND type=='table' AND name!='sqlite_sequence'"
      );
      run_schema_dump_query(p,
        "SELECT name, type, sql FROM sqlite_master "
        "WHERE name=='sqlite_sequence'"
      );
      run_table_dump_query(p,
        "SELECT sql FROM sqlite_master "
        "WHERE sql NOT NULL AND type IN ('index','trigger','view')", 0
      );
    }else{
      char *zSql;
      zSql = sqlite3_mprintf(
        "SELECT name, type, sql FROM sqlite_master "
        "WHERE tbl_name LIKE %Q AND type=='table'"
        "  AND sql NOT NULL", zLike);
      run_schema_dump_query(p,zSql);
      sqlite3_free(zSql);
      zSql = sqlite3_mprintf(
        "SELECT sql FROM sqlite_master "
        "WHERE sql NOT NULL"
        "  AND type IN ('index','trigger','view')"
        "  AND tbl_name LIKE %Q", zLike);
      run_table_dump_query(p, zSql, 0);
      sqlite3_free(zSql);
    }
    if( p->writableSchema ){
      raw_printf(p->out, "PRAGMA writable_schema=OFF;\n");
      p->writableSchema = 0;
    }
    sqlite3_exec(p->db, "PRAGMA writable_schema=OFF;", 0, 0, 0);
    sqlite3_exec(p->db, "RELEASE dump;", 0, 0, 0);
    raw_printf(p->out, p->nErr ? "ROLLBACK; -- due to errors\n" : "COMMIT;\n");
    p->showHeader = savedShowHeader;
    p->shellFlgs = savedShellFlags;
  }else

  if( c=='e' && strncmp(azArg[0], "echo", n)==0 ){
    if( nArg==2 ){
      setOrClearFlag(p, SHFLG_Echo, azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .echo on|off\n");
      rc = 1;
    }
  }else

  if( c=='e' && strncmp(azArg[0], "eqp", n)==0 ){
    if( nArg==2 ){
      p->autoEQPtest = 0;
      if( strcmp(azArg[1],"full")==0 ){
        p->autoEQP = AUTOEQP_full;
      }else if( strcmp(azArg[1],"trigger")==0 ){
        p->autoEQP = AUTOEQP_trigger;
      }else if( strcmp(azArg[1],"test")==0 ){
        p->autoEQP = AUTOEQP_on;
        p->autoEQPtest = 1;
      }else{
        p->autoEQP = (u8)booleanValue(azArg[1]);
      }
    }else{
      raw_printf(stderr, "Usage: .eqp off|on|trigger|full\n");
      rc = 1;
    }
  }else

  if( c=='e' && strncmp(azArg[0], "exit", n)==0 ){
    if( nArg>1 && (rc = (int)integerValue(azArg[1]))!=0 ) exit(rc);
    rc = 2;
  }else

  /* The ".explain" command is automatic now.  It is largely pointless.  It
  ** retained purely for backwards compatibility */
  if( c=='e' && strncmp(azArg[0], "explain", n)==0 ){
    int val = 1;
    if( nArg>=2 ){
      if( strcmp(azArg[1],"auto")==0 ){
        val = 99;
      }else{
        val =  booleanValue(azArg[1]);
      }
    }
    if( val==1 && p->mode!=MODE_Explain ){
      p->normalMode = p->mode;
      p->mode = MODE_Explain;
      p->autoExplain = 0;
    }else if( val==0 ){
      if( p->mode==MODE_Explain ) p->mode = p->normalMode;
      p->autoExplain = 0;
    }else if( val==99 ){
      if( p->mode==MODE_Explain ) p->mode = p->normalMode;
      p->autoExplain = 1;
    }
  }else

#ifndef SQLITE_OMIT_VIRTUALTABLE
  if( c=='e' && strncmp(azArg[0], "expert", n)==0 ){
    open_db(p, 0);
    expertDotCommand(p, azArg, nArg);
  }else
#endif

  if( c=='f' && strncmp(azArg[0], "fullschema", n)==0 ){
    ShellState data;
    char *zErrMsg = 0;
    int doStats = 0;
    memcpy(&data, p, sizeof(data));
    data.showHeader = 0;
    data.cMode = data.mode = MODE_Semi;
    if( nArg==2 && optionMatch(azArg[1], "indent") ){
      data.cMode = data.mode = MODE_Pretty;
      nArg = 1;
    }
    if( nArg!=1 ){
      raw_printf(stderr, "Usage: .fullschema ?--indent?\n");
      rc = 1;
      goto meta_command_exit;
    }
    open_db(p, 0);
    rc = sqlite3_exec(p->db,
       "SELECT sql FROM"
       "  (SELECT sql sql, type type, tbl_name tbl_name, name name, rowid x"
       "     FROM sqlite_master UNION ALL"
       "   SELECT sql, type, tbl_name, name, rowid FROM sqlite_temp_master) "
       "WHERE type!='meta' AND sql NOTNULL AND name NOT LIKE 'sqlite_%' "
       "ORDER BY rowid",
       callback, &data, &zErrMsg
    );
    if( rc==SQLITE_OK ){
      sqlite3_stmt *pStmt;
      rc = sqlite3_prepare_v2(p->db,
               "SELECT rowid FROM sqlite_master"
               " WHERE name GLOB 'sqlite_stat[134]'",
               -1, &pStmt, 0);
      doStats = sqlite3_step(pStmt)==SQLITE_ROW;
      sqlite3_finalize(pStmt);
    }
    if( doStats==0 ){
      raw_printf(p->out, "/* No STAT tables available */\n");
    }else{
      raw_printf(p->out, "ANALYZE sqlite_master;\n");
      sqlite3_exec(p->db, "SELECT 'ANALYZE sqlite_master'",
                   callback, &data, &zErrMsg);
      data.cMode = data.mode = MODE_Insert;
      data.zDestTable = "sqlite_stat1";
      shell_exec(&data, "SELECT * FROM sqlite_stat1", &zErrMsg);
      data.zDestTable = "sqlite_stat3";
      shell_exec(&data, "SELECT * FROM sqlite_stat3", &zErrMsg);
      data.zDestTable = "sqlite_stat4";
      shell_exec(&data, "SELECT * FROM sqlite_stat4", &zErrMsg);
      raw_printf(p->out, "ANALYZE sqlite_master;\n");
    }
  }else

  if( c=='h' && strncmp(azArg[0], "headers", n)==0 ){
    if( nArg==2 ){
      p->showHeader = booleanValue(azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .headers on|off\n");
      rc = 1;
    }
  }else

  if( c=='h' && strncmp(azArg[0], "help", n)==0 ){
    utf8_printf(p->out, "%s", zHelp);
  }else

  if( c=='i' && strncmp(azArg[0], "import", n)==0 ){
    char *zTable;               /* Insert data into this table */
    char *zFile;                /* Name of file to extra content from */
    sqlite3_stmt *pStmt = NULL; /* A statement */
    int nCol;                   /* Number of columns in the table */
    int nByte;                  /* Number of bytes in an SQL string */
    int i, j;                   /* Loop counters */
    int needCommit;             /* True to COMMIT or ROLLBACK at end */
    int nSep;                   /* Number of bytes in p->colSeparator[] */
    char *zSql;                 /* An SQL statement */
    ImportCtx sCtx;             /* Reader context */
    char *(SQLITE_CDECL *xRead)(ImportCtx*); /* Func to read one value */
    int (SQLITE_CDECL *xCloser)(FILE*);      /* Func to close file */

    if( nArg!=3 ){
      raw_printf(stderr, "Usage: .import FILE TABLE\n");
      goto meta_command_exit;
    }
    zFile = azArg[1];
    zTable = azArg[2];
    seenInterrupt = 0;
    memset(&sCtx, 0, sizeof(sCtx));
    open_db(p, 0);
    nSep = strlen30(p->colSeparator);
    if( nSep==0 ){
      raw_printf(stderr,
                 "Error: non-null column separator required for import\n");
      return 1;
    }
    if( nSep>1 ){
      raw_printf(stderr, "Error: multi-character column separators not allowed"
                      " for import\n");
      return 1;
    }
    nSep = strlen30(p->rowSeparator);
    if( nSep==0 ){
      raw_printf(stderr, "Error: non-null row separator required for import\n");
      return 1;
    }
    if( nSep==2 && p->mode==MODE_Csv && strcmp(p->rowSeparator, SEP_CrLf)==0 ){
      /* When importing CSV (only), if the row separator is set to the
      ** default output row separator, change it to the default input
      ** row separator.  This avoids having to maintain different input
      ** and output row separators. */
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Row);
      nSep = strlen30(p->rowSeparator);
    }
    if( nSep>1 ){
      raw_printf(stderr, "Error: multi-character row separators not allowed"
                      " for import\n");
      return 1;
    }
    sCtx.zFile = zFile;
    sCtx.nLine = 1;
    if( sCtx.zFile[0]=='|' ){
#ifdef SQLITE_OMIT_POPEN
      raw_printf(stderr, "Error: pipes are not supported in this OS\n");
      return 1;
#else
      sCtx.in = popen(sCtx.zFile+1, "r");
      sCtx.zFile = "<pipe>";
      xCloser = pclose;
#endif
    }else{
      sCtx.in = fopen(sCtx.zFile, "rb");
      xCloser = fclose;
    }
    if( p->mode==MODE_Ascii ){
      xRead = ascii_read_one_field;
    }else{
      xRead = csv_read_one_field;
    }
    if( sCtx.in==0 ){
      utf8_printf(stderr, "Error: cannot open \"%s\"\n", zFile);
      return 1;
    }
    sCtx.cColSep = p->colSeparator[0];
    sCtx.cRowSep = p->rowSeparator[0];
    zSql = sqlite3_mprintf("SELECT * FROM %s", zTable);
    if( zSql==0 ){
      xCloser(sCtx.in);
      shell_out_of_memory();
    }
    nByte = strlen30(zSql);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    import_append_char(&sCtx, 0);    /* To ensure sCtx.z is allocated */
    if( rc && sqlite3_strglob("no such table: *", sqlite3_errmsg(p->db))==0 ){
      char *zCreate = sqlite3_mprintf("CREATE TABLE %s", zTable);
      char cSep = '(';
      while( xRead(&sCtx) ){
        zCreate = sqlite3_mprintf("%z%c\n  \"%w\" TEXT", zCreate, cSep, sCtx.z);
        cSep = ',';
        if( sCtx.cTerm!=sCtx.cColSep ) break;
      }
      if( cSep=='(' ){
        sqlite3_free(zCreate);
        sqlite3_free(sCtx.z);
        xCloser(sCtx.in);
        utf8_printf(stderr,"%s: empty file\n", sCtx.zFile);
        return 1;
      }
      zCreate = sqlite3_mprintf("%z\n)", zCreate);
      rc = sqlite3_exec(p->db, zCreate, 0, 0, 0);
      sqlite3_free(zCreate);
      if( rc ){
        utf8_printf(stderr, "CREATE TABLE %s(...) failed: %s\n", zTable,
                sqlite3_errmsg(p->db));
        sqlite3_free(sCtx.z);
        xCloser(sCtx.in);
        return 1;
      }
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    }
    sqlite3_free(zSql);
    if( rc ){
      if (pStmt) sqlite3_finalize(pStmt);
      utf8_printf(stderr,"Error: %s\n", sqlite3_errmsg(p->db));
      xCloser(sCtx.in);
      return 1;
    }
    nCol = sqlite3_column_count(pStmt);
    sqlite3_finalize(pStmt);
    pStmt = 0;
    if( nCol==0 ) return 0; /* no columns, no error */
    zSql = sqlite3_malloc64( nByte*2 + 20 + nCol*2 );
    if( zSql==0 ){
      xCloser(sCtx.in);
      shell_out_of_memory();
    }
    sqlite3_snprintf(nByte+20, zSql, "INSERT INTO \"%w\" VALUES(?", zTable);
    j = strlen30(zSql);
    for(i=1; i<nCol; i++){
      zSql[j++] = ',';
      zSql[j++] = '?';
    }
    zSql[j++] = ')';
    zSql[j] = 0;
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
    if( rc ){
      utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(p->db));
      if (pStmt) sqlite3_finalize(pStmt);
      xCloser(sCtx.in);
      return 1;
    }
    needCommit = sqlite3_get_autocommit(p->db);
    if( needCommit ) sqlite3_exec(p->db, "BEGIN", 0, 0, 0);
    do{
      int startLine = sCtx.nLine;
      for(i=0; i<nCol; i++){
        char *z = xRead(&sCtx);
        /*
        ** Did we reach end-of-file before finding any columns?
        ** If so, stop instead of NULL filling the remaining columns.
        */
        if( z==0 && i==0 ) break;
        /*
        ** Did we reach end-of-file OR end-of-line before finding any
        ** columns in ASCII mode?  If so, stop instead of NULL filling
        ** the remaining columns.
        */
        if( p->mode==MODE_Ascii && (z==0 || z[0]==0) && i==0 ) break;
        sqlite3_bind_text(pStmt, i+1, z, -1, SQLITE_TRANSIENT);
        if( i<nCol-1 && sCtx.cTerm!=sCtx.cColSep ){
          utf8_printf(stderr, "%s:%d: expected %d columns but found %d - "
                          "filling the rest with NULL\n",
                          sCtx.zFile, startLine, nCol, i+1);
          i += 2;
          while( i<=nCol ){ sqlite3_bind_null(pStmt, i); i++; }
        }
      }
      if( sCtx.cTerm==sCtx.cColSep ){
        do{
          xRead(&sCtx);
          i++;
        }while( sCtx.cTerm==sCtx.cColSep );
        utf8_printf(stderr, "%s:%d: expected %d columns but found %d - "
                        "extras ignored\n",
                        sCtx.zFile, startLine, nCol, i);
      }
      if( i>=nCol ){
        sqlite3_step(pStmt);
        rc = sqlite3_reset(pStmt);
        if( rc!=SQLITE_OK ){
          utf8_printf(stderr, "%s:%d: INSERT failed: %s\n", sCtx.zFile,
                      startLine, sqlite3_errmsg(p->db));
        }
      }
    }while( sCtx.cTerm!=EOF );

    xCloser(sCtx.in);
    sqlite3_free(sCtx.z);
    sqlite3_finalize(pStmt);
    if( needCommit ) sqlite3_exec(p->db, "COMMIT", 0, 0, 0);
  }else

#ifndef SQLITE_UNTESTABLE
  if( c=='i' && strncmp(azArg[0], "imposter", n)==0 ){
    char *zSql;
    char *zCollist = 0;
    sqlite3_stmt *pStmt;
    int tnum = 0;
    int i;
    if( !(nArg==3 || (nArg==2 && sqlite3_stricmp(azArg[1],"off")==0)) ){
      utf8_printf(stderr, "Usage: .imposter INDEX IMPOSTER\n"
                          "       .imposter off\n");
      rc = 1;
      goto meta_command_exit;
    }
    open_db(p, 0);
    if( nArg==2 ){
      sqlite3_test_control(SQLITE_TESTCTRL_IMPOSTER, p->db, "main", 0, 1);
      goto meta_command_exit;
    }
    zSql = sqlite3_mprintf("SELECT rootpage FROM sqlite_master"
                           " WHERE name='%q' AND type='index'", azArg[1]);
    sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
    if( sqlite3_step(pStmt)==SQLITE_ROW ){
      tnum = sqlite3_column_int(pStmt, 0);
    }
    sqlite3_finalize(pStmt);
    if( tnum==0 ){
      utf8_printf(stderr, "no such index: \"%s\"\n", azArg[1]);
      rc = 1;
      goto meta_command_exit;
    }
    zSql = sqlite3_mprintf("PRAGMA index_xinfo='%q'", azArg[1]);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    sqlite3_free(zSql);
    i = 0;
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      char zLabel[20];
      const char *zCol = (const char*)sqlite3_column_text(pStmt,2);
      i++;
      if( zCol==0 ){
        if( sqlite3_column_int(pStmt,1)==-1 ){
          zCol = "_ROWID_";
        }else{
          sqlite3_snprintf(sizeof(zLabel),zLabel,"expr%d",i);
          zCol = zLabel;
        }
      }
      if( zCollist==0 ){
        zCollist = sqlite3_mprintf("\"%w\"", zCol);
      }else{
        zCollist = sqlite3_mprintf("%z,\"%w\"", zCollist, zCol);
      }
    }
    sqlite3_finalize(pStmt);
    zSql = sqlite3_mprintf(
          "CREATE TABLE \"%w\"(%s,PRIMARY KEY(%s))WITHOUT ROWID",
          azArg[2], zCollist, zCollist);
    sqlite3_free(zCollist);
    rc = sqlite3_test_control(SQLITE_TESTCTRL_IMPOSTER, p->db, "main", 1, tnum);
    if( rc==SQLITE_OK ){
      rc = sqlite3_exec(p->db, zSql, 0, 0, 0);
      sqlite3_test_control(SQLITE_TESTCTRL_IMPOSTER, p->db, "main", 0, 0);
      if( rc ){
        utf8_printf(stderr, "Error in [%s]: %s\n", zSql, sqlite3_errmsg(p->db));
      }else{
        utf8_printf(stdout, "%s;\n", zSql);
        raw_printf(stdout,
           "WARNING: writing to an imposter table will corrupt the index!\n"
        );
      }
    }else{
      raw_printf(stderr, "SQLITE_TESTCTRL_IMPOSTER returns %d\n", rc);
      rc = 1;
    }
    sqlite3_free(zSql);
  }else
#endif /* !defined(SQLITE_OMIT_TEST_CONTROL) */

#ifdef SQLITE_ENABLE_IOTRACE
  if( c=='i' && strncmp(azArg[0], "iotrace", n)==0 ){
    SQLITE_API extern void (SQLITE_CDECL *sqlite3IoTrace)(const char*, ...);
    if( iotrace && iotrace!=stdout ) fclose(iotrace);
    iotrace = 0;
    if( nArg<2 ){
      sqlite3IoTrace = 0;
    }else if( strcmp(azArg[1], "-")==0 ){
      sqlite3IoTrace = iotracePrintf;
      iotrace = stdout;
    }else{
      iotrace = fopen(azArg[1], "w");
      if( iotrace==0 ){
        utf8_printf(stderr, "Error: cannot open \"%s\"\n", azArg[1]);
        sqlite3IoTrace = 0;
        rc = 1;
      }else{
        sqlite3IoTrace = iotracePrintf;
      }
    }
  }else
#endif

  if( c=='l' && n>=5 && strncmp(azArg[0], "limits", n)==0 ){
    static const struct {
       const char *zLimitName;   /* Name of a limit */
       int limitCode;            /* Integer code for that limit */
    } aLimit[] = {
      { "length",                SQLITE_LIMIT_LENGTH                    },
      { "sql_length",            SQLITE_LIMIT_SQL_LENGTH                },
      { "column",                SQLITE_LIMIT_COLUMN                    },
      { "expr_depth",            SQLITE_LIMIT_EXPR_DEPTH                },
      { "compound_select",       SQLITE_LIMIT_COMPOUND_SELECT           },
      { "vdbe_op",               SQLITE_LIMIT_VDBE_OP                   },
      { "function_arg",          SQLITE_LIMIT_FUNCTION_ARG              },
      { "attached",              SQLITE_LIMIT_ATTACHED                  },
      { "like_pattern_length",   SQLITE_LIMIT_LIKE_PATTERN_LENGTH       },
      { "variable_number",       SQLITE_LIMIT_VARIABLE_NUMBER           },
      { "trigger_depth",         SQLITE_LIMIT_TRIGGER_DEPTH             },
      { "worker_threads",        SQLITE_LIMIT_WORKER_THREADS            },
    };
    int i, n2;
    open_db(p, 0);
    if( nArg==1 ){
      for(i=0; i<ArraySize(aLimit); i++){
        printf("%20s %d\n", aLimit[i].zLimitName,
               sqlite3_limit(p->db, aLimit[i].limitCode, -1));
      }
    }else if( nArg>3 ){
      raw_printf(stderr, "Usage: .limit NAME ?NEW-VALUE?\n");
      rc = 1;
      goto meta_command_exit;
    }else{
      int iLimit = -1;
      n2 = strlen30(azArg[1]);
      for(i=0; i<ArraySize(aLimit); i++){
        if( sqlite3_strnicmp(aLimit[i].zLimitName, azArg[1], n2)==0 ){
          if( iLimit<0 ){
            iLimit = i;
          }else{
            utf8_printf(stderr, "ambiguous limit: \"%s\"\n", azArg[1]);
            rc = 1;
            goto meta_command_exit;
          }
        }
      }
      if( iLimit<0 ){
        utf8_printf(stderr, "unknown limit: \"%s\"\n"
                        "enter \".limits\" with no arguments for a list.\n",
                         azArg[1]);
        rc = 1;
        goto meta_command_exit;
      }
      if( nArg==3 ){
        sqlite3_limit(p->db, aLimit[iLimit].limitCode,
                      (int)integerValue(azArg[2]));
      }
      printf("%20s %d\n", aLimit[iLimit].zLimitName,
             sqlite3_limit(p->db, aLimit[iLimit].limitCode, -1));
    }
  }else

  if( c=='l' && n>2 && strncmp(azArg[0], "lint", n)==0 ){
    open_db(p, 0);
    lintDotCommand(p, azArg, nArg);
  }else

#ifndef SQLITE_OMIT_LOAD_EXTENSION
  if( c=='l' && strncmp(azArg[0], "load", n)==0 ){
    const char *zFile, *zProc;
    char *zErrMsg = 0;
    if( nArg<2 ){
      raw_printf(stderr, "Usage: .load FILE ?ENTRYPOINT?\n");
      rc = 1;
      goto meta_command_exit;
    }
    zFile = azArg[1];
    zProc = nArg>=3 ? azArg[2] : 0;
    open_db(p, 0);
    rc = sqlite3_load_extension(p->db, zFile, zProc, &zErrMsg);
    if( rc!=SQLITE_OK ){
      utf8_printf(stderr, "Error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
      rc = 1;
    }
  }else
#endif

  if( c=='l' && strncmp(azArg[0], "log", n)==0 ){
    if( nArg!=2 ){
      raw_printf(stderr, "Usage: .log FILENAME\n");
      rc = 1;
    }else{
      const char *zFile = azArg[1];
      output_file_close(p->pLog);
      p->pLog = output_file_open(zFile, 0);
    }
  }else

  if( c=='m' && strncmp(azArg[0], "mode", n)==0 ){
    const char *zMode = nArg>=2 ? azArg[1] : "";
    int n2 = strlen30(zMode);
    int c2 = zMode[0];
    if( c2=='l' && n2>2 && strncmp(azArg[1],"lines",n2)==0 ){
      p->mode = MODE_Line;
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Row);
    }else if( c2=='c' && strncmp(azArg[1],"columns",n2)==0 ){
      p->mode = MODE_Column;
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Row);
    }else if( c2=='l' && n2>2 && strncmp(azArg[1],"list",n2)==0 ){
      p->mode = MODE_List;
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Column);
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Row);
    }else if( c2=='h' && strncmp(azArg[1],"html",n2)==0 ){
      p->mode = MODE_Html;
    }else if( c2=='t' && strncmp(azArg[1],"tcl",n2)==0 ){
      p->mode = MODE_Tcl;
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Space);
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Row);
    }else if( c2=='c' && strncmp(azArg[1],"csv",n2)==0 ){
      p->mode = MODE_Csv;
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Comma);
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_CrLf);
    }else if( c2=='t' && strncmp(azArg[1],"tabs",n2)==0 ){
      p->mode = MODE_List;
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Tab);
    }else if( c2=='i' && strncmp(azArg[1],"insert",n2)==0 ){
      p->mode = MODE_Insert;
      set_table_name(p, nArg>=3 ? azArg[2] : "table");
    }else if( c2=='q' && strncmp(azArg[1],"quote",n2)==0 ){
      p->mode = MODE_Quote;
    }else if( c2=='a' && strncmp(azArg[1],"ascii",n2)==0 ){
      p->mode = MODE_Ascii;
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Unit);
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_Record);
    }else if( nArg==1 ){
      raw_printf(p->out, "current output mode: %s\n", modeDescr[p->mode]);
    }else{
      raw_printf(stderr, "Error: mode should be one of: "
         "ascii column csv html insert line list quote tabs tcl\n");
      rc = 1;
    }
    p->cMode = p->mode;
  }else

  if( c=='n' && strncmp(azArg[0], "nullvalue", n)==0 ){
    if( nArg==2 ){
      sqlite3_snprintf(sizeof(p->nullValue), p->nullValue,
                       "%.*s", (int)ArraySize(p->nullValue)-1, azArg[1]);
    }else{
      raw_printf(stderr, "Usage: .nullvalue STRING\n");
      rc = 1;
    }
  }else

  if( c=='o' && strncmp(azArg[0], "open", n)==0 && n>=2 ){
    char *zNewFilename;  /* Name of the database file to open */
    int iName = 1;       /* Index in azArg[] of the filename */
    int newFlag = 0;     /* True to delete file before opening */
    /* Close the existing database */
    session_close_all(p);
    close_db(p->db);
    p->db = 0;
    p->zDbFilename = 0;
    sqlite3_free(p->zFreeOnClose);
    p->zFreeOnClose = 0;
    p->openMode = SHELL_OPEN_UNSPEC;
    /* Check for command-line arguments */
    for(iName=1; iName<nArg && azArg[iName][0]=='-'; iName++){
      const char *z = azArg[iName];
      if( optionMatch(z,"new") ){
        newFlag = 1;
#ifdef SQLITE_HAVE_ZLIB
      }else if( optionMatch(z, "zip") ){
        p->openMode = SHELL_OPEN_ZIPFILE;
#endif
      }else if( optionMatch(z, "append") ){
        p->openMode = SHELL_OPEN_APPENDVFS;
      }else if( optionMatch(z, "readonly") ){
        p->openMode = SHELL_OPEN_READONLY;
      }else if( z[0]=='-' ){
        utf8_printf(stderr, "unknown option: %s\n", z);
        rc = 1;
        goto meta_command_exit;
      }
    }
    /* If a filename is specified, try to open it first */
    zNewFilename = nArg>iName ? sqlite3_mprintf("%s", azArg[iName]) : 0;
    if( zNewFilename ){
      if( newFlag ) shellDeleteFile(zNewFilename);
      p->zDbFilename = zNewFilename;
      open_db(p, OPEN_DB_KEEPALIVE);
      if( p->db==0 ){
        utf8_printf(stderr, "Error: cannot open '%s'\n", zNewFilename);
        sqlite3_free(zNewFilename);
      }else{
        p->zFreeOnClose = zNewFilename;
      }
    }
    if( p->db==0 ){
      /* As a fall-back open a TEMP database */
      p->zDbFilename = 0;
      open_db(p, 0);
    }
  }else

  if( (c=='o'
        && (strncmp(azArg[0], "output", n)==0||strncmp(azArg[0], "once", n)==0))
   || (c=='e' && n==5 && strcmp(azArg[0],"excel")==0)
  ){
    const char *zFile = nArg>=2 ? azArg[1] : "stdout";
    int bTxtMode = 0;
    if( azArg[0][0]=='e' ){
      /* Transform the ".excel" command into ".once -x" */
      nArg = 2;
      azArg[0] = "once";
      zFile = azArg[1] = "-x";
      n = 4;
    }
    if( nArg>2 ){
      utf8_printf(stderr, "Usage: .%s [-e|-x|FILE]\n", azArg[0]);
      rc = 1;
      goto meta_command_exit;
    }
    if( n>1 && strncmp(azArg[0], "once", n)==0 ){
      if( nArg<2 ){
        raw_printf(stderr, "Usage: .once (-e|-x|FILE)\n");
        rc = 1;
        goto meta_command_exit;
      }
      p->outCount = 2;
    }else{
      p->outCount = 0;
    }
    output_reset(p);
    if( zFile[0]=='-' && zFile[1]=='-' ) zFile++;
#ifndef SQLITE_NOHAVE_SYSTEM
    if( strcmp(zFile, "-e")==0 || strcmp(zFile, "-x")==0 ){
      p->doXdgOpen = 1;
      outputModePush(p);
      if( zFile[1]=='x' ){
        newTempFile(p, "csv");
        p->mode = MODE_Csv;
        sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator, SEP_Comma);
        sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator, SEP_CrLf);
      }else{
        newTempFile(p, "txt");
        bTxtMode = 1;
      }
      zFile = p->zTempFile;
    }
#endif /* SQLITE_NOHAVE_SYSTEM */
    if( zFile[0]=='|' ){
#ifdef SQLITE_OMIT_POPEN
      raw_printf(stderr, "Error: pipes are not supported in this OS\n");
      rc = 1;
      p->out = stdout;
#else
      p->out = popen(zFile + 1, "w");
      if( p->out==0 ){
        utf8_printf(stderr,"Error: cannot open pipe \"%s\"\n", zFile + 1);
        p->out = stdout;
        rc = 1;
      }else{
        sqlite3_snprintf(sizeof(p->outfile), p->outfile, "%s", zFile);
      }
#endif
    }else{
      p->out = output_file_open(zFile, bTxtMode);
      if( p->out==0 ){
        if( strcmp(zFile,"off")!=0 ){
          utf8_printf(stderr,"Error: cannot write to \"%s\"\n", zFile);
        }
        p->out = stdout;
        rc = 1;
      } else {
        sqlite3_snprintf(sizeof(p->outfile), p->outfile, "%s", zFile);
      }
    }
  }else

  if( c=='p' && n>=3 && strncmp(azArg[0], "print", n)==0 ){
    int i;
    for(i=1; i<nArg; i++){
      if( i>1 ) raw_printf(p->out, " ");
      utf8_printf(p->out, "%s", azArg[i]);
    }
    raw_printf(p->out, "\n");
  }else

  if( c=='p' && strncmp(azArg[0], "prompt", n)==0 ){
    if( nArg >= 2) {
      strncpy(mainPrompt,azArg[1],(int)ArraySize(mainPrompt)-1);
    }
    if( nArg >= 3) {
      strncpy(continuePrompt,azArg[2],(int)ArraySize(continuePrompt)-1);
    }
  }else

  if( c=='q' && strncmp(azArg[0], "quit", n)==0 ){
    rc = 2;
  }else

  if( c=='r' && n>=3 && strncmp(azArg[0], "read", n)==0 ){
    FILE *alt;
    if( nArg!=2 ){
      raw_printf(stderr, "Usage: .read FILE\n");
      rc = 1;
      goto meta_command_exit;
    }
    alt = fopen(azArg[1], "rb");
    if( alt==0 ){
      utf8_printf(stderr,"Error: cannot open \"%s\"\n", azArg[1]);
      rc = 1;
    }else{
      rc = process_input(p, alt);
      fclose(alt);
    }
  }else

  if( c=='r' && n>=3 && strncmp(azArg[0], "restore", n)==0 ){
    const char *zSrcFile;
    const char *zDb;
    sqlite3 *pSrc;
    sqlite3_backup *pBackup;
    int nTimeout = 0;

    if( nArg==2 ){
      zSrcFile = azArg[1];
      zDb = "main";
    }else if( nArg==3 ){
      zSrcFile = azArg[2];
      zDb = azArg[1];
    }else{
      raw_printf(stderr, "Usage: .restore ?DB? FILE\n");
      rc = 1;
      goto meta_command_exit;
    }
    rc = sqlite3_open(zSrcFile, &pSrc);
    if( rc!=SQLITE_OK ){
      utf8_printf(stderr, "Error: cannot open \"%s\"\n", zSrcFile);
      close_db(pSrc);
      return 1;
    }
    open_db(p, 0);
    pBackup = sqlite3_backup_init(p->db, zDb, pSrc, "main");
    if( pBackup==0 ){
      utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(p->db));
      close_db(pSrc);
      return 1;
    }
    while( (rc = sqlite3_backup_step(pBackup,100))==SQLITE_OK
          || rc==SQLITE_BUSY  ){
      if( rc==SQLITE_BUSY ){
        if( nTimeout++ >= 3 ) break;
        sqlite3_sleep(100);
      }
    }
    sqlite3_backup_finish(pBackup);
    if( rc==SQLITE_DONE ){
      rc = 0;
    }else if( rc==SQLITE_BUSY || rc==SQLITE_LOCKED ){
      raw_printf(stderr, "Error: source database is busy\n");
      rc = 1;
    }else{
      utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(p->db));
      rc = 1;
    }
    close_db(pSrc);
  }else

  if( c=='s' && strncmp(azArg[0], "scanstats", n)==0 ){
    if( nArg==2 ){
      p->scanstatsOn = (u8)booleanValue(azArg[1]);
#ifndef SQLITE_ENABLE_STMT_SCANSTATUS
      raw_printf(stderr, "Warning: .scanstats not available in this build.\n");
#endif
    }else{
      raw_printf(stderr, "Usage: .scanstats on|off\n");
      rc = 1;
    }
  }else

  if( c=='s' && strncmp(azArg[0], "schema", n)==0 ){
    ShellText sSelect;
    ShellState data;
    char *zErrMsg = 0;
    const char *zDiv = "(";
    const char *zName = 0;
    int iSchema = 0;
    int bDebug = 0;
    int ii;

    open_db(p, 0);
    memcpy(&data, p, sizeof(data));
    data.showHeader = 0;
    data.cMode = data.mode = MODE_Semi;
    initText(&sSelect);
    for(ii=1; ii<nArg; ii++){
      if( optionMatch(azArg[ii],"indent") ){
        data.cMode = data.mode = MODE_Pretty;
      }else if( optionMatch(azArg[ii],"debug") ){
        bDebug = 1;
      }else if( zName==0 ){
        zName = azArg[ii];
      }else{
        raw_printf(stderr, "Usage: .schema ?--indent? ?LIKE-PATTERN?\n");
        rc = 1;
        goto meta_command_exit;
      }
    }
    if( zName!=0 ){
      int isMaster = sqlite3_strlike(zName, "sqlite_master", '\\')==0;
      if( isMaster || sqlite3_strlike(zName,"sqlite_temp_master", '\\')==0 ){
        char *new_argv[2], *new_colv[2];
        new_argv[0] = sqlite3_mprintf(
                      "CREATE TABLE %s (\n"
                      "  type text,\n"
                      "  name text,\n"
                      "  tbl_name text,\n"
                      "  rootpage integer,\n"
                      "  sql text\n"
                      ")", isMaster ? "sqlite_master" : "sqlite_temp_master");
        new_argv[1] = 0;
        new_colv[0] = "sql";
        new_colv[1] = 0;
        callback(&data, 1, new_argv, new_colv);
        sqlite3_free(new_argv[0]);
      }
    }
    if( zDiv ){
      sqlite3_stmt *pStmt = 0;
      rc = sqlite3_prepare_v2(p->db, "SELECT name FROM pragma_database_list",
                              -1, &pStmt, 0);
      if( rc ){
        utf8_printf(stderr, "Error: %s\n", sqlite3_errmsg(p->db));
        sqlite3_finalize(pStmt);
        rc = 1;
        goto meta_command_exit;
      }
      appendText(&sSelect, "SELECT sql FROM", 0);
      iSchema = 0;
      while( sqlite3_step(pStmt)==SQLITE_ROW ){
        const char *zDb = (const char*)sqlite3_column_text(pStmt, 0);
        char zScNum[30];
        sqlite3_snprintf(sizeof(zScNum), zScNum, "%d", ++iSchema);
        appendText(&sSelect, zDiv, 0);
        zDiv = " UNION ALL ";
        appendText(&sSelect, "SELECT shell_add_schema(sql,", 0);
        if( sqlite3_stricmp(zDb, "main")!=0 ){
          appendText(&sSelect, zDb, '"');
        }else{
          appendText(&sSelect, "NULL", 0);
        }
        appendText(&sSelect, ",name) AS sql, type, tbl_name, name, rowid,", 0);
        appendText(&sSelect, zScNum, 0);
        appendText(&sSelect, " AS snum, ", 0);
        appendText(&sSelect, zDb, '\'');
        appendText(&sSelect, " AS sname FROM ", 0);
        appendText(&sSelect, zDb, '"');
        appendText(&sSelect, ".sqlite_master", 0);
      }
      sqlite3_finalize(pStmt);
#ifdef SQLITE_INTROSPECTION_PRAGMAS
      if( zName ){
        appendText(&sSelect,
           " UNION ALL SELECT shell_module_schema(name),"
           " 'table', name, name, name, 9e+99, 'main' FROM pragma_module_list", 0);
      }
#endif
      appendText(&sSelect, ") WHERE ", 0);
      if( zName ){
        char *zQarg = sqlite3_mprintf("%Q", zName);
        int bGlob = strchr(zName, '*') != 0 || strchr(zName, '?') != 0 ||
                    strchr(zName, '[') != 0;
        if( strchr(zName, '.') ){
          appendText(&sSelect, "lower(printf('%s.%s',sname,tbl_name))", 0);
        }else{
          appendText(&sSelect, "lower(tbl_name)", 0);
        }
        appendText(&sSelect, bGlob ? " GLOB " : " LIKE ", 0);
        appendText(&sSelect, zQarg, 0);
        if( !bGlob ){
          appendText(&sSelect, " ESCAPE '\\' ", 0);
        }
        appendText(&sSelect, " AND ", 0);
        sqlite3_free(zQarg);
      }
      appendText(&sSelect, "type!='meta' AND sql IS NOT NULL"
                           " ORDER BY snum, rowid", 0);
      if( bDebug ){
        utf8_printf(p->out, "SQL: %s;\n", sSelect.z);
      }else{
        rc = sqlite3_exec(p->db, sSelect.z, callback, &data, &zErrMsg);
      }
      freeText(&sSelect);
    }
    if( zErrMsg ){
      utf8_printf(stderr,"Error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
      rc = 1;
    }else if( rc != SQLITE_OK ){
      raw_printf(stderr,"Error: querying schema information\n");
      rc = 1;
    }else{
      rc = 0;
    }
  }else

#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_SELECTTRACE)
  if( c=='s' && n==11 && strncmp(azArg[0], "selecttrace", n)==0 ){
    sqlite3SelectTrace = (int)integerValue(azArg[1]);
  }else
#endif

#if defined(SQLITE_ENABLE_SESSION)
  if( c=='s' && strncmp(azArg[0],"session",n)==0 && n>=3 ){
    OpenSession *pSession = &p->aSession[0];
    char **azCmd = &azArg[1];
    int iSes = 0;
    int nCmd = nArg - 1;
    int i;
    if( nArg<=1 ) goto session_syntax_error;
    open_db(p, 0);
    if( nArg>=3 ){
      for(iSes=0; iSes<p->nSession; iSes++){
        if( strcmp(p->aSession[iSes].zName, azArg[1])==0 ) break;
      }
      if( iSes<p->nSession ){
        pSession = &p->aSession[iSes];
        azCmd++;
        nCmd--;
      }else{
        pSession = &p->aSession[0];
        iSes = 0;
      }
    }

    /* .session attach TABLE
    ** Invoke the sqlite3session_attach() interface to attach a particular
    ** table so that it is never filtered.
    */
    if( strcmp(azCmd[0],"attach")==0 ){
      if( nCmd!=2 ) goto session_syntax_error;
      if( pSession->p==0 ){
        session_not_open:
        raw_printf(stderr, "ERROR: No sessions are open\n");
      }else{
        rc = sqlite3session_attach(pSession->p, azCmd[1]);
        if( rc ){
          raw_printf(stderr, "ERROR: sqlite3session_attach() returns %d\n", rc);
          rc = 0;
        }
      }
    }else

    /* .session changeset FILE
    ** .session patchset FILE
    ** Write a changeset or patchset into a file.  The file is overwritten.
    */
    if( strcmp(azCmd[0],"changeset")==0 || strcmp(azCmd[0],"patchset")==0 ){
      FILE *out = 0;
      if( nCmd!=2 ) goto session_syntax_error;
      if( pSession->p==0 ) goto session_not_open;
      out = fopen(azCmd[1], "wb");
      if( out==0 ){
        utf8_printf(stderr, "ERROR: cannot open \"%s\" for writing\n", azCmd[1]);
      }else{
        int szChng;
        void *pChng;
        if( azCmd[0][0]=='c' ){
          rc = sqlite3session_changeset(pSession->p, &szChng, &pChng);
        }else{
          rc = sqlite3session_patchset(pSession->p, &szChng, &pChng);
        }
        if( rc ){
          printf("Error: error code %d\n", rc);
          rc = 0;
        }
        if( pChng
          && fwrite(pChng, szChng, 1, out)!=1 ){
          raw_printf(stderr, "ERROR: Failed to write entire %d-byte output\n",
                  szChng);
        }
        sqlite3_free(pChng);
        fclose(out);
      }
    }else

    /* .session close
    ** Close the identified session
    */
    if( strcmp(azCmd[0], "close")==0 ){
      if( nCmd!=1 ) goto session_syntax_error;
      if( p->nSession ){
        session_close(pSession);
        p->aSession[iSes] = p->aSession[--p->nSession];
      }
    }else

    /* .session enable ?BOOLEAN?
    ** Query or set the enable flag
    */
    if( strcmp(azCmd[0], "enable")==0 ){
      int ii;
      if( nCmd>2 ) goto session_syntax_error;
      ii = nCmd==1 ? -1 : booleanValue(azCmd[1]);
      if( p->nSession ){
        ii = sqlite3session_enable(pSession->p, ii);
        utf8_printf(p->out, "session %s enable flag = %d\n",
                    pSession->zName, ii);
      }
    }else

    /* .session filter GLOB ....
    ** Set a list of GLOB patterns of table names to be excluded.
    */
    if( strcmp(azCmd[0], "filter")==0 ){
      int ii, nByte;
      if( nCmd<2 ) goto session_syntax_error;
      if( p->nSession ){
        for(ii=0; ii<pSession->nFilter; ii++){
          sqlite3_free(pSession->azFilter[ii]);
        }
        sqlite3_free(pSession->azFilter);
        nByte = sizeof(pSession->azFilter[0])*(nCmd-1);
        pSession->azFilter = sqlite3_malloc( nByte );
        if( pSession->azFilter==0 ){
          raw_printf(stderr, "Error: out or memory\n");
          exit(1);
        }
        for(ii=1; ii<nCmd; ii++){
          pSession->azFilter[ii-1] = sqlite3_mprintf("%s", azCmd[ii]);
        }
        pSession->nFilter = ii-1;
      }
    }else

    /* .session indirect ?BOOLEAN?
    ** Query or set the indirect flag
    */
    if( strcmp(azCmd[0], "indirect")==0 ){
      int ii;
      if( nCmd>2 ) goto session_syntax_error;
      ii = nCmd==1 ? -1 : booleanValue(azCmd[1]);
      if( p->nSession ){
        ii = sqlite3session_indirect(pSession->p, ii);
        utf8_printf(p->out, "session %s indirect flag = %d\n",
                    pSession->zName, ii);
      }
    }else

    /* .session isempty
    ** Determine if the session is empty
    */
    if( strcmp(azCmd[0], "isempty")==0 ){
      int ii;
      if( nCmd!=1 ) goto session_syntax_error;
      if( p->nSession ){
        ii = sqlite3session_isempty(pSession->p);
        utf8_printf(p->out, "session %s isempty flag = %d\n",
                    pSession->zName, ii);
      }
    }else

    /* .session list
    ** List all currently open sessions
    */
    if( strcmp(azCmd[0],"list")==0 ){
      for(i=0; i<p->nSession; i++){
        utf8_printf(p->out, "%d %s\n", i, p->aSession[i].zName);
      }
    }else

    /* .session open DB NAME
    ** Open a new session called NAME on the attached database DB.
    ** DB is normally "main".
    */
    if( strcmp(azCmd[0],"open")==0 ){
      char *zName;
      if( nCmd!=3 ) goto session_syntax_error;
      zName = azCmd[2];
      if( zName[0]==0 ) goto session_syntax_error;
      for(i=0; i<p->nSession; i++){
        if( strcmp(p->aSession[i].zName,zName)==0 ){
          utf8_printf(stderr, "Session \"%s\" already exists\n", zName);
          goto meta_command_exit;
        }
      }
      if( p->nSession>=ArraySize(p->aSession) ){
        raw_printf(stderr, "Maximum of %d sessions\n", ArraySize(p->aSession));
        goto meta_command_exit;
      }
      pSession = &p->aSession[p->nSession];
      rc = sqlite3session_create(p->db, azCmd[1], &pSession->p);
      if( rc ){
        raw_printf(stderr, "Cannot open session: error code=%d\n", rc);
        rc = 0;
        goto meta_command_exit;
      }
      pSession->nFilter = 0;
      sqlite3session_table_filter(pSession->p, session_filter, pSession);
      p->nSession++;
      pSession->zName = sqlite3_mprintf("%s", zName);
    }else
    /* If no command name matches, show a syntax error */
    session_syntax_error:
    session_help(p);
  }else
#endif

#ifdef SQLITE_DEBUG
  /* Undocumented commands for internal testing.  Subject to change
  ** without notice. */
  if( c=='s' && n>=10 && strncmp(azArg[0], "selftest-", 9)==0 ){
    if( strncmp(azArg[0]+9, "boolean", n-9)==0 ){
      int i, v;
      for(i=1; i<nArg; i++){
        v = booleanValue(azArg[i]);
        utf8_printf(p->out, "%s: %d 0x%x\n", azArg[i], v, v);
      }
    }
    if( strncmp(azArg[0]+9, "integer", n-9)==0 ){
      int i; sqlite3_int64 v;
      for(i=1; i<nArg; i++){
        char zBuf[200];
        v = integerValue(azArg[i]);
        sqlite3_snprintf(sizeof(zBuf),zBuf,"%s: %lld 0x%llx\n", azArg[i],v,v);
        utf8_printf(p->out, "%s", zBuf);
      }
    }
  }else
#endif

  if( c=='s' && n>=4 && strncmp(azArg[0],"selftest",n)==0 ){
    int bIsInit = 0;         /* True to initialize the SELFTEST table */
    int bVerbose = 0;        /* Verbose output */
    int bSelftestExists;     /* True if SELFTEST already exists */
    int i, k;                /* Loop counters */
    int nTest = 0;           /* Number of tests runs */
    int nErr = 0;            /* Number of errors seen */
    ShellText str;           /* Answer for a query */
    sqlite3_stmt *pStmt = 0; /* Query against the SELFTEST table */

    open_db(p,0);
    for(i=1; i<nArg; i++){
      const char *z = azArg[i];
      if( z[0]=='-' && z[1]=='-' ) z++;
      if( strcmp(z,"-init")==0 ){
        bIsInit = 1;
      }else
      if( strcmp(z,"-v")==0 ){
        bVerbose++;
      }else
      {
        utf8_printf(stderr, "Unknown option \"%s\" on \"%s\"\n",
                    azArg[i], azArg[0]);
        raw_printf(stderr, "Should be one of: --init -v\n");
        rc = 1;
        goto meta_command_exit;
      }
    }
    if( sqlite3_table_column_metadata(p->db,"main","selftest",0,0,0,0,0,0)
           != SQLITE_OK ){
      bSelftestExists = 0;
    }else{
      bSelftestExists = 1;
    }
    if( bIsInit ){
      createSelftestTable(p);
      bSelftestExists = 1;
    }
    initText(&str);
    appendText(&str, "x", 0);
    for(k=bSelftestExists; k>=0; k--){
      if( k==1 ){
        rc = sqlite3_prepare_v2(p->db,
            "SELECT tno,op,cmd,ans FROM selftest ORDER BY tno",
            -1, &pStmt, 0);
      }else{
        rc = sqlite3_prepare_v2(p->db,
          "VALUES(0,'memo','Missing SELFTEST table - default checks only',''),"
          "      (1,'run','PRAGMA integrity_check','ok')",
          -1, &pStmt, 0);
      }
      if( rc ){
        raw_printf(stderr, "Error querying the selftest table\n");
        rc = 1;
        sqlite3_finalize(pStmt);
        goto meta_command_exit;
      }
      for(i=1; sqlite3_step(pStmt)==SQLITE_ROW; i++){
        int tno = sqlite3_column_int(pStmt, 0);
        const char *zOp = (const char*)sqlite3_column_text(pStmt, 1);
        const char *zSql = (const char*)sqlite3_column_text(pStmt, 2);
        const char *zAns = (const char*)sqlite3_column_text(pStmt, 3);

        k = 0;
        if( bVerbose>0 ){
          char *zQuote = sqlite3_mprintf("%q", zSql);
          printf("%d: %s %s\n", tno, zOp, zSql);
          sqlite3_free(zQuote);
        }
        if( strcmp(zOp,"memo")==0 ){
          utf8_printf(p->out, "%s\n", zSql);
        }else
        if( strcmp(zOp,"run")==0 ){
          char *zErrMsg = 0;
          str.n = 0;
          str.z[0] = 0;
          rc = sqlite3_exec(p->db, zSql, captureOutputCallback, &str, &zErrMsg);
          nTest++;
          if( bVerbose ){
            utf8_printf(p->out, "Result: %s\n", str.z);
          }
          if( rc || zErrMsg ){
            nErr++;
            rc = 1;
            utf8_printf(p->out, "%d: error-code-%d: %s\n", tno, rc, zErrMsg);
            sqlite3_free(zErrMsg);
          }else if( strcmp(zAns,str.z)!=0 ){
            nErr++;
            rc = 1;
            utf8_printf(p->out, "%d: Expected: [%s]\n", tno, zAns);
            utf8_printf(p->out, "%d:      Got: [%s]\n", tno, str.z);
          }
        }else
        {
          utf8_printf(stderr,
            "Unknown operation \"%s\" on selftest line %d\n", zOp, tno);
          rc = 1;
          break;
        }
      } /* End loop over rows of content from SELFTEST */
      sqlite3_finalize(pStmt);
    } /* End loop over k */
    freeText(&str);
    utf8_printf(p->out, "%d errors out of %d tests\n", nErr, nTest);
  }else

  if( c=='s' && strncmp(azArg[0], "separator", n)==0 ){
    if( nArg<2 || nArg>3 ){
      raw_printf(stderr, "Usage: .separator COL ?ROW?\n");
      rc = 1;
    }
    if( nArg>=2 ){
      sqlite3_snprintf(sizeof(p->colSeparator), p->colSeparator,
                       "%.*s", (int)ArraySize(p->colSeparator)-1, azArg[1]);
    }
    if( nArg>=3 ){
      sqlite3_snprintf(sizeof(p->rowSeparator), p->rowSeparator,
                       "%.*s", (int)ArraySize(p->rowSeparator)-1, azArg[2]);
    }
  }else

  if( c=='s' && n>=4 && strncmp(azArg[0],"sha3sum",n)==0 ){
    const char *zLike = 0;   /* Which table to checksum. 0 means everything */
    int i;                   /* Loop counter */
    int bSchema = 0;         /* Also hash the schema */
    int bSeparate = 0;       /* Hash each table separately */
    int iSize = 224;         /* Hash algorithm to use */
    int bDebug = 0;          /* Only show the query that would have run */
    sqlite3_stmt *pStmt;     /* For querying tables names */
    char *zSql;              /* SQL to be run */
    char *zSep;              /* Separator */
    ShellText sSql;          /* Complete SQL for the query to run the hash */
    ShellText sQuery;        /* Set of queries used to read all content */
    open_db(p, 0);
    for(i=1; i<nArg; i++){
      const char *z = azArg[i];
      if( z[0]=='-' ){
        z++;
        if( z[0]=='-' ) z++;
        if( strcmp(z,"schema")==0 ){
          bSchema = 1;
        }else
        if( strcmp(z,"sha3-224")==0 || strcmp(z,"sha3-256")==0
         || strcmp(z,"sha3-384")==0 || strcmp(z,"sha3-512")==0
        ){
          iSize = atoi(&z[5]);
        }else
        if( strcmp(z,"debug")==0 ){
          bDebug = 1;
        }else
        {
          utf8_printf(stderr, "Unknown option \"%s\" on \"%s\"\n",
                      azArg[i], azArg[0]);
          raw_printf(stderr, "Should be one of: --schema"
                             " --sha3-224 --sha3-256 --sha3-384 --sha3-512\n");
          rc = 1;
          goto meta_command_exit;
        }
      }else if( zLike ){
        raw_printf(stderr, "Usage: .sha3sum ?OPTIONS? ?LIKE-PATTERN?\n");
        rc = 1;
        goto meta_command_exit;
      }else{
        zLike = z;
        bSeparate = 1;
        if( sqlite3_strlike("sqlite\\_%", zLike, '\\')==0 ) bSchema = 1;
      }
    }
    if( bSchema ){
      zSql = "SELECT lower(name) FROM sqlite_master"
             " WHERE type='table' AND coalesce(rootpage,0)>1"
             " UNION ALL SELECT 'sqlite_master'"
             " ORDER BY 1 collate nocase";
    }else{
      zSql = "SELECT lower(name) FROM sqlite_master"
             " WHERE type='table' AND coalesce(rootpage,0)>1"
             " AND name NOT LIKE 'sqlite_%'"
             " ORDER BY 1 collate nocase";
    }
    sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
    initText(&sQuery);
    initText(&sSql);
    appendText(&sSql, "WITH [sha3sum$query](a,b) AS(",0);
    zSep = "VALUES(";
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      const char *zTab = (const char*)sqlite3_column_text(pStmt,0);
      if( zLike && sqlite3_strlike(zLike, zTab, 0)!=0 ) continue;
      if( strncmp(zTab, "sqlite_",7)!=0 ){
        appendText(&sQuery,"SELECT * FROM ", 0);
        appendText(&sQuery,zTab,'"');
        appendText(&sQuery," NOT INDEXED;", 0);
      }else if( strcmp(zTab, "sqlite_master")==0 ){
        appendText(&sQuery,"SELECT type,name,tbl_name,sql FROM sqlite_master"
                           " ORDER BY name;", 0);
      }else if( strcmp(zTab, "sqlite_sequence")==0 ){
        appendText(&sQuery,"SELECT name,seq FROM sqlite_sequence"
                           " ORDER BY name;", 0);
      }else if( strcmp(zTab, "sqlite_stat1")==0 ){
        appendText(&sQuery,"SELECT tbl,idx,stat FROM sqlite_stat1"
                           " ORDER BY tbl,idx;", 0);
      }else if( strcmp(zTab, "sqlite_stat3")==0
             || strcmp(zTab, "sqlite_stat4")==0 ){
        appendText(&sQuery, "SELECT * FROM ", 0);
        appendText(&sQuery, zTab, 0);
        appendText(&sQuery, " ORDER BY tbl, idx, rowid;\n", 0);
      }
      appendText(&sSql, zSep, 0);
      appendText(&sSql, sQuery.z, '\'');
      sQuery.n = 0;
      appendText(&sSql, ",", 0);
      appendText(&sSql, zTab, '\'');
      zSep = "),(";
    }
    sqlite3_finalize(pStmt);
    if( bSeparate ){
      zSql = sqlite3_mprintf(
          "%s))"
          " SELECT lower(hex(sha3_query(a,%d))) AS hash, b AS label"
          "   FROM [sha3sum$query]",
          sSql.z, iSize);
    }else{
      zSql = sqlite3_mprintf(
          "%s))"
          " SELECT lower(hex(sha3_query(group_concat(a,''),%d))) AS hash"
          "   FROM [sha3sum$query]",
          sSql.z, iSize);
    }
    freeText(&sQuery);
    freeText(&sSql);
    if( bDebug ){
      utf8_printf(p->out, "%s\n", zSql);
    }else{
      shell_exec(p, zSql, 0);
    }
    sqlite3_free(zSql);
  }else

#ifndef SQLITE_NOHAVE_SYSTEM
  if( c=='s'
   && (strncmp(azArg[0], "shell", n)==0 || strncmp(azArg[0],"system",n)==0)
  ){
    char *zCmd;
    int i, x;
    if( nArg<2 ){
      raw_printf(stderr, "Usage: .system COMMAND\n");
      rc = 1;
      goto meta_command_exit;
    }
    zCmd = sqlite3_mprintf(strchr(azArg[1],' ')==0?"%s":"\"%s\"", azArg[1]);
    for(i=2; i<nArg; i++){
      zCmd = sqlite3_mprintf(strchr(azArg[i],' ')==0?"%z %s":"%z \"%s\"",
                             zCmd, azArg[i]);
    }
    x = system(zCmd);
    sqlite3_free(zCmd);
    if( x ) raw_printf(stderr, "System command returns %d\n", x);
  }else
#endif /* !defined(SQLITE_NOHAVE_SYSTEM) */

  if( c=='s' && strncmp(azArg[0], "show", n)==0 ){
    static const char *azBool[] = { "off", "on", "trigger", "full"};
    int i;
    if( nArg!=1 ){
      raw_printf(stderr, "Usage: .show\n");
      rc = 1;
      goto meta_command_exit;
    }
    utf8_printf(p->out, "%12.12s: %s\n","echo",
                                  azBool[ShellHasFlag(p, SHFLG_Echo)]);
    utf8_printf(p->out, "%12.12s: %s\n","eqp", azBool[p->autoEQP&3]);
    utf8_printf(p->out, "%12.12s: %s\n","explain",
         p->mode==MODE_Explain ? "on" : p->autoExplain ? "auto" : "off");
    utf8_printf(p->out,"%12.12s: %s\n","headers", azBool[p->showHeader!=0]);
    utf8_printf(p->out, "%12.12s: %s\n","mode", modeDescr[p->mode]);
    utf8_printf(p->out, "%12.12s: ", "nullvalue");
      output_c_string(p->out, p->nullValue);
      raw_printf(p->out, "\n");
    utf8_printf(p->out,"%12.12s: %s\n","output",
            strlen30(p->outfile) ? p->outfile : "stdout");
    utf8_printf(p->out,"%12.12s: ", "colseparator");
      output_c_string(p->out, p->colSeparator);
      raw_printf(p->out, "\n");
    utf8_printf(p->out,"%12.12s: ", "rowseparator");
      output_c_string(p->out, p->rowSeparator);
      raw_printf(p->out, "\n");
    utf8_printf(p->out, "%12.12s: %s\n","stats", azBool[p->statsOn!=0]);
    utf8_printf(p->out, "%12.12s: ", "width");
    for (i=0;i<(int)ArraySize(p->colWidth) && p->colWidth[i] != 0;i++) {
      raw_printf(p->out, "%d ", p->colWidth[i]);
    }
    raw_printf(p->out, "\n");
    utf8_printf(p->out, "%12.12s: %s\n", "filename",
                p->zDbFilename ? p->zDbFilename : "");
  }else

  if( c=='s' && strncmp(azArg[0], "stats", n)==0 ){
    if( nArg==2 ){
      p->statsOn = (u8)booleanValue(azArg[1]);
    }else if( nArg==1 ){
      display_stats(p->db, p, 0);
    }else{
      raw_printf(stderr, "Usage: .stats ?on|off?\n");
      rc = 1;
    }
  }else

  if( (c=='t' && n>1 && strncmp(azArg[0], "tables", n)==0)
   || (c=='i' && (strncmp(azArg[0], "indices", n)==0
                 || strncmp(azArg[0], "indexes", n)==0) )
  ){
    sqlite3_stmt *pStmt;
    char **azResult;
    int nRow, nAlloc;
    int ii;
    ShellText s;
    initText(&s);
    open_db(p, 0);
    rc = sqlite3_prepare_v2(p->db, "PRAGMA database_list", -1, &pStmt, 0);
    if( rc ){
      sqlite3_finalize(pStmt);
      return shellDatabaseError(p->db);
    }

    if( nArg>2 && c=='i' ){
      /* It is an historical accident that the .indexes command shows an error
      ** when called with the wrong number of arguments whereas the .tables
      ** command does not. */
      raw_printf(stderr, "Usage: .indexes ?LIKE-PATTERN?\n");
      rc = 1;
      sqlite3_finalize(pStmt);
      goto meta_command_exit;
    }
    for(ii=0; sqlite3_step(pStmt)==SQLITE_ROW; ii++){
      const char *zDbName = (const char*)sqlite3_column_text(pStmt, 1);
      if( zDbName==0 ) continue;
      if( s.z && s.z[0] ) appendText(&s, " UNION ALL ", 0);
      if( sqlite3_stricmp(zDbName, "main")==0 ){
        appendText(&s, "SELECT name FROM ", 0);
      }else{
        appendText(&s, "SELECT ", 0);
        appendText(&s, zDbName, '\'');
        appendText(&s, "||'.'||name FROM ", 0);
      }
      appendText(&s, zDbName, '"');
      appendText(&s, ".sqlite_master ", 0);
      if( c=='t' ){
        appendText(&s," WHERE type IN ('table','view')"
                      "   AND name NOT LIKE 'sqlite_%'"
                      "   AND name LIKE ?1", 0);
      }else{
        appendText(&s," WHERE type='index'"
                      "   AND tbl_name LIKE ?1", 0);
      }
    }
    rc = sqlite3_finalize(pStmt);
    appendText(&s, " ORDER BY 1", 0);
    rc = sqlite3_prepare_v2(p->db, s.z, -1, &pStmt, 0);
    freeText(&s);
    if( rc ) return shellDatabaseError(p->db);

    /* Run the SQL statement prepared by the above block. Store the results
    ** as an array of nul-terminated strings in azResult[].  */
    nRow = nAlloc = 0;
    azResult = 0;
    if( nArg>1 ){
      sqlite3_bind_text(pStmt, 1, azArg[1], -1, SQLITE_TRANSIENT);
    }else{
      sqlite3_bind_text(pStmt, 1, "%", -1, SQLITE_STATIC);
    }
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      if( nRow>=nAlloc ){
        char **azNew;
        int n2 = nAlloc*2 + 10;
        azNew = sqlite3_realloc64(azResult, sizeof(azResult[0])*n2);
        if( azNew==0 ) shell_out_of_memory();
        nAlloc = n2;
        azResult = azNew;
      }
      azResult[nRow] = sqlite3_mprintf("%s", sqlite3_column_text(pStmt, 0));
      if( 0==azResult[nRow] ) shell_out_of_memory();
      nRow++;
    }
    if( sqlite3_finalize(pStmt)!=SQLITE_OK ){
      rc = shellDatabaseError(p->db);
    }

    /* Pretty-print the contents of array azResult[] to the output */
    if( rc==0 && nRow>0 ){
      int len, maxlen = 0;
      int i, j;
      int nPrintCol, nPrintRow;
      for(i=0; i<nRow; i++){
        len = strlen30(azResult[i]);
        if( len>maxlen ) maxlen = len;
      }
      nPrintCol = 80/(maxlen+2);
      if( nPrintCol<1 ) nPrintCol = 1;
      nPrintRow = (nRow + nPrintCol - 1)/nPrintCol;
      for(i=0; i<nPrintRow; i++){
        for(j=i; j<nRow; j+=nPrintRow){
          char *zSp = j<nPrintRow ? "" : "  ";
          utf8_printf(p->out, "%s%-*s", zSp, maxlen,
                      azResult[j] ? azResult[j]:"");
        }
        raw_printf(p->out, "\n");
      }
    }

    for(ii=0; ii<nRow; ii++) sqlite3_free(azResult[ii]);
    sqlite3_free(azResult);
  }else

  /* Begin redirecting output to the file "testcase-out.txt" */
  if( c=='t' && strcmp(azArg[0],"testcase")==0 ){
    output_reset(p);
    p->out = output_file_open("testcase-out.txt", 0);
    if( p->out==0 ){
      raw_printf(stderr, "Error: cannot open 'testcase-out.txt'\n");
    }
    if( nArg>=2 ){
      sqlite3_snprintf(sizeof(p->zTestcase), p->zTestcase, "%s", azArg[1]);
    }else{
      sqlite3_snprintf(sizeof(p->zTestcase), p->zTestcase, "?");
    }
  }else

#ifndef SQLITE_UNTESTABLE
  if( c=='t' && n>=8 && strncmp(azArg[0], "testctrl", n)==0 ){
    static const struct {
       const char *zCtrlName;   /* Name of a test-control option */
       int ctrlCode;            /* Integer code for that option */
       const char *zUsage;      /* Usage notes */
    } aCtrl[] = {
      { "always",             SQLITE_TESTCTRL_ALWAYS,        "BOOLEAN"            },
      { "assert",             SQLITE_TESTCTRL_ASSERT,        "BOOLEAN"            },
    /*{ "benign_malloc_hooks",SQLITE_TESTCTRL_BENIGN_MALLOC_HOOKS, ""          },*/
    /*{ "bitvec_test",        SQLITE_TESTCTRL_BITVEC_TEST,   ""                },*/
      { "byteorder",          SQLITE_TESTCTRL_BYTEORDER,     ""                   },
    /*{ "fault_install",      SQLITE_TESTCTRL_FAULT_INSTALL, ""                }, */
      { "imposter",           SQLITE_TESTCTRL_IMPOSTER,   "SCHEMA ON/OFF ROOTPAGE"},
      { "localtime_fault",    SQLITE_TESTCTRL_LOCALTIME_FAULT,"BOOLEAN"           },
      { "never_corrupt",      SQLITE_TESTCTRL_NEVER_CORRUPT, "BOOLEAN"            },
      { "optimizations",      SQLITE_TESTCTRL_OPTIMIZATIONS, "DISABLE-MASK"       },
#ifdef YYCOVERAGE
      { "parser_coverage",    SQLITE_TESTCTRL_PARSER_COVERAGE, ""                 },
#endif
      { "pending_byte",       SQLITE_TESTCTRL_PENDING_BYTE,  "OFFSET  "           },
      { "prng_reset",         SQLITE_TESTCTRL_PRNG_RESET,    ""                   },
      { "prng_restore",       SQLITE_TESTCTRL_PRNG_RESTORE,  ""                   },
      { "prng_save",          SQLITE_TESTCTRL_PRNG_SAVE,     ""                   },
      { "reserve",            SQLITE_TESTCTRL_RESERVE,       "BYTES-OF-RESERVE"   },
    };
    int testctrl = -1;
    int iCtrl = -1;
    int rc2 = 0;    /* 0: usage.  1: %d  2: %x  3: no-output */
    int isOk = 0;
    int i, n2;
    const char *zCmd = 0;

    open_db(p, 0);
    zCmd = nArg>=2 ? azArg[1] : "help";

    /* The argument can optionally begin with "-" or "--" */
    if( zCmd[0]=='-' && zCmd[1] ){
      zCmd++;
      if( zCmd[0]=='-' && zCmd[1] ) zCmd++;
    }

    /* --help lists all test-controls */
    if( strcmp(zCmd,"help")==0 ){
      utf8_printf(p->out, "Available test-controls:\n");
      for(i=0; i<ArraySize(aCtrl); i++){
        utf8_printf(p->out, "  .testctrl %s %s\n",
                    aCtrl[i].zCtrlName, aCtrl[i].zUsage);
      }
      rc = 1;
      goto meta_command_exit;
    }

    /* convert testctrl text option to value. allow any unique prefix
    ** of the option name, or a numerical value. */
    n2 = strlen30(zCmd);
    for(i=0; i<ArraySize(aCtrl); i++){
      if( strncmp(zCmd, aCtrl[i].zCtrlName, n2)==0 ){
        if( testctrl<0 ){
          testctrl = aCtrl[i].ctrlCode;
          iCtrl = i;
        }else{
          utf8_printf(stderr, "Error: ambiguous test-control: \"%s\"\n"
                              "Use \".testctrl --help\" for help\n", zCmd);
          rc = 1;
          goto meta_command_exit;
        }
      }
    }
    if( testctrl<0 ){
      utf8_printf(stderr,"Error: unknown test-control: %s\n"
                         "Use \".testctrl --help\" for help\n", zCmd);
    }else{
      switch(testctrl){

        /* sqlite3_test_control(int, db, int) */
        case SQLITE_TESTCTRL_OPTIMIZATIONS:
        case SQLITE_TESTCTRL_RESERVE:
          if( nArg==3 ){
            int opt = (int)strtol(azArg[2], 0, 0);
            rc2 = sqlite3_test_control(testctrl, p->db, opt);
            isOk = 3;
          }
          break;

        /* sqlite3_test_control(int) */
        case SQLITE_TESTCTRL_PRNG_SAVE:
        case SQLITE_TESTCTRL_PRNG_RESTORE:
        case SQLITE_TESTCTRL_PRNG_RESET:
        case SQLITE_TESTCTRL_BYTEORDER:
          if( nArg==2 ){
            rc2 = sqlite3_test_control(testctrl);
            isOk = testctrl==SQLITE_TESTCTRL_BYTEORDER ? 1 : 3;
          }
          break;

        /* sqlite3_test_control(int, uint) */
        case SQLITE_TESTCTRL_PENDING_BYTE:
          if( nArg==3 ){
            unsigned int opt = (unsigned int)integerValue(azArg[2]);
            rc2 = sqlite3_test_control(testctrl, opt);
            isOk = 3;
          }
          break;

        /* sqlite3_test_control(int, int) */
        case SQLITE_TESTCTRL_ASSERT:
        case SQLITE_TESTCTRL_ALWAYS:
          if( nArg==3 ){
            int opt = booleanValue(azArg[2]);
            rc2 = sqlite3_test_control(testctrl, opt);
            isOk = 1;
          }
          break;

        /* sqlite3_test_control(int, int) */
        case SQLITE_TESTCTRL_LOCALTIME_FAULT:
        case SQLITE_TESTCTRL_NEVER_CORRUPT:
          if( nArg==3 ){
            int opt = booleanValue(azArg[2]);
            rc2 = sqlite3_test_control(testctrl, opt);
            isOk = 3;
          }
          break;

        case SQLITE_TESTCTRL_IMPOSTER:
          if( nArg==5 ){
            rc2 = sqlite3_test_control(testctrl, p->db,
                          azArg[2],
                          integerValue(azArg[3]),
                          integerValue(azArg[4]));
            isOk = 3;
          }
          break;

#ifdef YYCOVERAGE
        case SQLITE_TESTCTRL_PARSER_COVERAGE:
          if( nArg==2 ){
            sqlite3_test_control(testctrl, p->out);
            isOk = 3;
          }
#endif
      }
    }
    if( isOk==0 && iCtrl>=0 ){
      utf8_printf(p->out, "Usage: .testctrl %s %s\n", zCmd, aCtrl[iCtrl].zUsage);
      rc = 1;
    }else if( isOk==1 ){
      raw_printf(p->out, "%d\n", rc2);
    }else if( isOk==2 ){
      raw_printf(p->out, "0x%08x\n", rc2);
    }
  }else
#endif /* !defined(SQLITE_UNTESTABLE) */

  if( c=='t' && n>4 && strncmp(azArg[0], "timeout", n)==0 ){
    open_db(p, 0);
    sqlite3_busy_timeout(p->db, nArg>=2 ? (int)integerValue(azArg[1]) : 0);
  }else

  if( c=='t' && n>=5 && strncmp(azArg[0], "timer", n)==0 ){
    if( nArg==2 ){
      enableTimer = booleanValue(azArg[1]);
      if( enableTimer && !HAS_TIMER ){
        raw_printf(stderr, "Error: timer not available on this system.\n");
        enableTimer = 0;
      }
    }else{
      raw_printf(stderr, "Usage: .timer on|off\n");
      rc = 1;
    }
  }else

  if( c=='t' && strncmp(azArg[0], "trace", n)==0 ){
    open_db(p, 0);
    if( nArg!=2 ){
      raw_printf(stderr, "Usage: .trace FILE|off\n");
      rc = 1;
      goto meta_command_exit;
    }
    output_file_close(p->traceOut);
    p->traceOut = output_file_open(azArg[1], 0);
#if !defined(SQLITE_OMIT_TRACE) && !defined(SQLITE_OMIT_FLOATING_POINT)
    if( p->traceOut==0 ){
      sqlite3_trace_v2(p->db, 0, 0, 0);
    }else{
      sqlite3_trace_v2(p->db, SQLITE_TRACE_STMT, sql_trace_callback,p->traceOut);
    }
#endif
  }else

#if SQLITE_USER_AUTHENTICATION
  if( c=='u' && strncmp(azArg[0], "user", n)==0 ){
    if( nArg<2 ){
      raw_printf(stderr, "Usage: .user SUBCOMMAND ...\n");
      rc = 1;
      goto meta_command_exit;
    }
    open_db(p, 0);
    if( strcmp(azArg[1],"login")==0 ){
      if( nArg!=4 ){
        raw_printf(stderr, "Usage: .user login USER PASSWORD\n");
        rc = 1;
        goto meta_command_exit;
      }
      rc = sqlite3_user_authenticate(p->db, azArg[2], azArg[3], strlen30(azArg[3]));
      if( rc ){
        utf8_printf(stderr, "Authentication failed for user %s\n", azArg[2]);
        rc = 1;
      }
    }else if( strcmp(azArg[1],"add")==0 ){
      if( nArg!=5 ){
        raw_printf(stderr, "Usage: .user add USER PASSWORD ISADMIN\n");
        rc = 1;
        goto meta_command_exit;
      }
      rc = sqlite3_user_add(p->db, azArg[2], azArg[3], strlen30(azArg[3]),
                            booleanValue(azArg[4]));
      if( rc ){
        raw_printf(stderr, "User-Add failed: %d\n", rc);
        rc = 1;
      }
    }else if( strcmp(azArg[1],"edit")==0 ){
      if( nArg!=5 ){
        raw_printf(stderr, "Usage: .user edit USER PASSWORD ISADMIN\n");
        rc = 1;
        goto meta_command_exit;
      }
      rc = sqlite3_user_change(p->db, azArg[2], azArg[3], strlen30(azArg[3]),
                              booleanValue(azArg[4]));
      if( rc ){
        raw_printf(stderr, "User-Edit failed: %d\n", rc);
        rc = 1;
      }
    }else if( strcmp(azArg[1],"delete")==0 ){
      if( nArg!=3 ){
        raw_printf(stderr, "Usage: .user delete USER\n");
        rc = 1;
        goto meta_command_exit;
      }
      rc = sqlite3_user_delete(p->db, azArg[2]);
      if( rc ){
        raw_printf(stderr, "User-Delete failed: %d\n", rc);
        rc = 1;
      }
    }else{
      raw_printf(stderr, "Usage: .user login|add|edit|delete ...\n");
      rc = 1;
      goto meta_command_exit;
    }
  }else
#endif /* SQLITE_USER_AUTHENTICATION */

  if( c=='v' && strncmp(azArg[0], "version", n)==0 ){
    utf8_printf(p->out, "SQLite %s %s\n" /*extra-version-info*/,
        sqlite3_libversion(), sqlite3_sourceid());
#if SQLITE_HAVE_ZLIB
    utf8_printf(p->out, "zlib version %s\n", zlibVersion());
#endif
#define CTIMEOPT_VAL_(opt) #opt
#define CTIMEOPT_VAL(opt) CTIMEOPT_VAL_(opt)
#if defined(__clang__) && defined(__clang_major__)
    utf8_printf(p->out, "clang-" CTIMEOPT_VAL(__clang_major__) "."
                    CTIMEOPT_VAL(__clang_minor__) "."
                    CTIMEOPT_VAL(__clang_patchlevel__) "\n");
#elif defined(_MSC_VER)
    utf8_printf(p->out, "msvc-" CTIMEOPT_VAL(_MSC_VER) "\n");
#elif defined(__GNUC__) && defined(__VERSION__)
    utf8_printf(p->out, "gcc-" __VERSION__ "\n");
#endif
  }else

  if( c=='v' && strncmp(azArg[0], "vfsinfo", n)==0 ){
    const char *zDbName = nArg==2 ? azArg[1] : "main";
    sqlite3_vfs *pVfs = 0;
    if( p->db ){
      sqlite3_file_control(p->db, zDbName, SQLITE_FCNTL_VFS_POINTER, &pVfs);
      if( pVfs ){
        utf8_printf(p->out, "vfs.zName      = \"%s\"\n", pVfs->zName);
        raw_printf(p->out, "vfs.iVersion   = %d\n", pVfs->iVersion);
        raw_printf(p->out, "vfs.szOsFile   = %d\n", pVfs->szOsFile);
        raw_printf(p->out, "vfs.mxPathname = %d\n", pVfs->mxPathname);
      }
    }
  }else

  if( c=='v' && strncmp(azArg[0], "vfslist", n)==0 ){
    sqlite3_vfs *pVfs;
    sqlite3_vfs *pCurrent = 0;
    if( p->db ){
      sqlite3_file_control(p->db, "main", SQLITE_FCNTL_VFS_POINTER, &pCurrent);
    }
    for(pVfs=sqlite3_vfs_find(0); pVfs; pVfs=pVfs->pNext){
      utf8_printf(p->out, "vfs.zName      = \"%s\"%s\n", pVfs->zName,
           pVfs==pCurrent ? "  <--- CURRENT" : "");
      raw_printf(p->out, "vfs.iVersion   = %d\n", pVfs->iVersion);
      raw_printf(p->out, "vfs.szOsFile   = %d\n", pVfs->szOsFile);
      raw_printf(p->out, "vfs.mxPathname = %d\n", pVfs->mxPathname);
      if( pVfs->pNext ){
        raw_printf(p->out, "-----------------------------------\n");
      }
    }
  }else

  if( c=='v' && strncmp(azArg[0], "vfsname", n)==0 ){
    const char *zDbName = nArg==2 ? azArg[1] : "main";
    char *zVfsName = 0;
    if( p->db ){
      sqlite3_file_control(p->db, zDbName, SQLITE_FCNTL_VFSNAME, &zVfsName);
      if( zVfsName ){
        utf8_printf(p->out, "%s\n", zVfsName);
        sqlite3_free(zVfsName);
      }
    }
  }else

#if defined(SQLITE_DEBUG) && defined(SQLITE_ENABLE_WHERETRACE)
  if( c=='w' && strncmp(azArg[0], "wheretrace", n)==0 ){
    sqlite3WhereTrace = nArg>=2 ? booleanValue(azArg[1]) : 0xff;
  }else
#endif

  if( c=='w' && strncmp(azArg[0], "width", n)==0 ){
    int j;
    assert( nArg<=ArraySize(azArg) );
    for(j=1; j<nArg && j<ArraySize(p->colWidth); j++){
      p->colWidth[j-1] = (int)integerValue(azArg[j]);
    }
  }else

  {
    utf8_printf(stderr, "Error: unknown command or invalid arguments: "
      " \"%s\". Enter \".help\" for help\n", azArg[0]);
    rc = 1;
  }

meta_command_exit:
  if( p->outCount ){
    p->outCount--;
    if( p->outCount==0 ) output_reset(p);
  }
  return rc;
}

/*
** Return TRUE if a semicolon occurs anywhere in the first N characters
** of string z[].
*/
static int line_contains_semicolon(const char *z, int N){
  int i;
  for(i=0; i<N; i++){  if( z[i]==';' ) return 1; }
  return 0;
}

/*
** Test to see if a line consists entirely of whitespace.
*/
static int _all_whitespace(const char *z){
  for(; *z; z++){
    if( IsSpace(z[0]) ) continue;
    if( *z=='/' && z[1]=='*' ){
      z += 2;
      while( *z && (*z!='*' || z[1]!='/') ){ z++; }
      if( *z==0 ) return 0;
      z++;
      continue;
    }
    if( *z=='-' && z[1]=='-' ){
      z += 2;
      while( *z && *z!='\n' ){ z++; }
      if( *z==0 ) return 1;
      continue;
    }
    return 0;
  }
  return 1;
}

/*
** Return TRUE if the line typed in is an SQL command terminator other
** than a semi-colon.  The SQL Server style "go" command is understood
** as is the Oracle "/".
*/
static int line_is_command_terminator(const char *zLine){
  while( IsSpace(zLine[0]) ){ zLine++; };
  if( zLine[0]=='/' && _all_whitespace(&zLine[1]) ){
    return 1;  /* Oracle */
  }
  if( ToLower(zLine[0])=='g' && ToLower(zLine[1])=='o'
         && _all_whitespace(&zLine[2]) ){
    return 1;  /* SQL Server */
  }
  return 0;
}

/*
** We need a default sqlite3_complete() implementation to use in case
** the shell is compiled with SQLITE_OMIT_COMPLETE.  The default assumes
** any arbitrary text is a complete SQL statement.  This is not very
** user-friendly, but it does seem to work.
*/
#ifdef SQLITE_OMIT_COMPLETE
int sqlite3_complete(const char *zSql){ return 1; }
#endif

/*
** Return true if zSql is a complete SQL statement.  Return false if it
** ends in the middle of a string literal or C-style comment.
*/
static int line_is_complete(char *zSql, int nSql){
  int rc;
  if( zSql==0 ) return 1;
  zSql[nSql] = ';';
  zSql[nSql+1] = 0;
  rc = sqlite3_complete(zSql);
  zSql[nSql] = 0;
  return rc;
}

/*
** Run a single line of SQL.  Return the number of errors.
*/
static int runOneSqlLine(ShellState *p, char *zSql, FILE *in, int startline){
  int rc;
  char *zErrMsg = 0;

  open_db(p, 0);
  if( ShellHasFlag(p,SHFLG_Backslash) ) resolve_backslashes(zSql);
  BEGIN_TIMER;
  rc = shell_exec(p, zSql, &zErrMsg);
  END_TIMER;
  if( rc || zErrMsg ){
    char zPrefix[100];
    if( in!=0 || !stdin_is_interactive ){
      sqlite3_snprintf(sizeof(zPrefix), zPrefix,
                       "Error: near line %d:", startline);
    }else{
      sqlite3_snprintf(sizeof(zPrefix), zPrefix, "Error:");
    }
    if( zErrMsg!=0 ){
      utf8_printf(stderr, "%s %s\n", zPrefix, zErrMsg);
      sqlite3_free(zErrMsg);
      zErrMsg = 0;
    }else{
      utf8_printf(stderr, "%s %s\n", zPrefix, sqlite3_errmsg(p->db));
    }
    return 1;
  }else if( ShellHasFlag(p, SHFLG_CountChanges) ){
    raw_printf(p->out, "changes: %3d   total_changes: %d\n",
            sqlite3_changes(p->db), sqlite3_total_changes(p->db));
  }
  return 0;
}


/*
** Read input from *in and process it.  If *in==0 then input
** is interactive - the user is typing it it.  Otherwise, input
** is coming from a file or device.  A prompt is issued and history
** is saved only if input is interactive.  An interrupt signal will
** cause this routine to exit immediately, unless input is interactive.
**
** Return the number of errors.
*/
static int process_input(ShellState *p, FILE *in){
  char *zLine = 0;          /* A single input line */
  char *zSql = 0;           /* Accumulated SQL text */
  int nLine;                /* Length of current line */
  int nSql = 0;             /* Bytes of zSql[] used */
  int nAlloc = 0;           /* Allocated zSql[] space */
  int nSqlPrior = 0;        /* Bytes of zSql[] used by prior line */
  int rc;                   /* Error code */
  int errCnt = 0;           /* Number of errors seen */
  int lineno = 0;           /* Current line number */
  int startline = 0;        /* Line number for start of current input */

  while( errCnt==0 || !bail_on_error || (in==0 && stdin_is_interactive) ){
    fflush(p->out);
    zLine = one_input_line(in, zLine, nSql>0);
    if( zLine==0 ){
      /* End of input */
      if( in==0 && stdin_is_interactive ) printf("\n");
      break;
    }
    if( seenInterrupt ){
      if( in!=0 ) break;
      seenInterrupt = 0;
    }
    lineno++;
    if( nSql==0 && _all_whitespace(zLine) ){
      if( ShellHasFlag(p, SHFLG_Echo) ) printf("%s\n", zLine);
      continue;
    }
    if( zLine && (zLine[0]=='.' || zLine[0]=='#') && nSql==0 ){
      if( ShellHasFlag(p, SHFLG_Echo) ) printf("%s\n", zLine);
      if( zLine[0]=='.' ){
        rc = do_meta_command(zLine, p);
        if( rc==2 ){ /* exit requested */
          break;
        }else if( rc ){
          errCnt++;
        }
      }
      continue;
    }
    if( line_is_command_terminator(zLine) && line_is_complete(zSql, nSql) ){
      memcpy(zLine,";",2);
    }
    nLine = strlen30(zLine);
    if( nSql+nLine+2>=nAlloc ){
      nAlloc = nSql+nLine+100;
      zSql = realloc(zSql, nAlloc);
      if( zSql==0 ) shell_out_of_memory();
    }
    nSqlPrior = nSql;
    if( nSql==0 ){
      int i;
      for(i=0; zLine[i] && IsSpace(zLine[i]); i++){}
      assert( nAlloc>0 && zSql!=0 );
      memcpy(zSql, zLine+i, nLine+1-i);
      startline = lineno;
      nSql = nLine-i;
    }else{
      zSql[nSql++] = '\n';
      memcpy(zSql+nSql, zLine, nLine+1);
      nSql += nLine;
    }
    if( nSql && line_contains_semicolon(&zSql[nSqlPrior], nSql-nSqlPrior)
                && sqlite3_complete(zSql) ){
      errCnt += runOneSqlLine(p, zSql, in, startline);
      nSql = 0;
      if( p->outCount ){
        output_reset(p);
        p->outCount = 0;
      }else{
        clearTempFile(p);
      }
    }else if( nSql && _all_whitespace(zSql) ){
      if( ShellHasFlag(p, SHFLG_Echo) ) printf("%s\n", zSql);
      nSql = 0;
    }
  }
  if( nSql && !_all_whitespace(zSql) ){
    errCnt += runOneSqlLine(p, zSql, in, startline);
  }
  free(zSql);
  free(zLine);
  return errCnt>0;
}

/*
** Return a pathname which is the user's home directory.  A
** 0 return indicates an error of some kind.
*/
static char *find_home_dir(int clearFlag){
  static char *home_dir = NULL;
  if( clearFlag ){
    free(home_dir);
    home_dir = 0;
    return 0;
  }
  if( home_dir ) return home_dir;

#if !defined(_WIN32) && !defined(WIN32) && !defined(_WIN32_WCE) \
     && !defined(__RTP__) && !defined(_WRS_KERNEL)
  {
    struct passwd *pwent;
    uid_t uid = getuid();
    if( (pwent=getpwuid(uid)) != NULL) {
      home_dir = pwent->pw_dir;
    }
  }
#endif

#if defined(_WIN32_WCE)
  /* Windows CE (arm-wince-mingw32ce-gcc) does not provide getenv()
   */
  home_dir = "/";
#else

#if defined(_WIN32) || defined(WIN32)
  if (!home_dir) {
    home_dir = getenv("USERPROFILE");
  }
#endif

  if (!home_dir) {
    home_dir = getenv("HOME");
  }

#if defined(_WIN32) || defined(WIN32)
  if (!home_dir) {
    char *zDrive, *zPath;
    int n;
    zDrive = getenv("HOMEDRIVE");
    zPath = getenv("HOMEPATH");
    if( zDrive && zPath ){
      n = strlen30(zDrive) + strlen30(zPath) + 1;
      home_dir = malloc( n );
      if( home_dir==0 ) return 0;
      sqlite3_snprintf(n, home_dir, "%s%s", zDrive, zPath);
      return home_dir;
    }
    home_dir = "c:\\";
  }
#endif

#endif /* !_WIN32_WCE */

  if( home_dir ){
    int n = strlen30(home_dir) + 1;
    char *z = malloc( n );
    if( z ) memcpy(z, home_dir, n);
    home_dir = z;
  }

  return home_dir;
}

/*
** Read input from the file given by sqliterc_override.  Or if that
** parameter is NULL, take input from ~/.sqliterc
**
** Returns the number of errors.
*/
static void process_sqliterc(
  ShellState *p,                  /* Configuration data */
  const char *sqliterc_override   /* Name of config file. NULL to use default */
){
  char *home_dir = NULL;
  const char *sqliterc = sqliterc_override;
  char *zBuf = 0;
  FILE *in = NULL;

  if (sqliterc == NULL) {
    home_dir = find_home_dir(0);
    if( home_dir==0 ){
      raw_printf(stderr, "-- warning: cannot find home directory;"
                      " cannot read ~/.sqliterc\n");
      return;
    }
    zBuf = sqlite3_mprintf("%s/.sqliterc",home_dir);
    sqliterc = zBuf;
  }
  in = fopen(sqliterc,"rb");
  if( in ){
    if( stdin_is_interactive ){
      utf8_printf(stderr,"-- Loading resources from %s\n",sqliterc);
    }
    process_input(p,in);
    fclose(in);
  }
  sqlite3_free(zBuf);
}

/*
** Show available command line options
*/
static const char zOptions[] =
#if defined(SQLITE_HAVE_ZLIB) && !defined(SQLITE_OMIT_VIRTUALTABLE)
  "   -A ARGS...           run \".archive ARGS\" and exit\n"
#endif
  "   -append              append the database to the end of the file\n"
  "   -ascii               set output mode to 'ascii'\n"
  "   -bail                stop after hitting an error\n"
  "   -batch               force batch I/O\n"
  "   -column              set output mode to 'column'\n"
  "   -cmd COMMAND         run \"COMMAND\" before reading stdin\n"
  "   -csv                 set output mode to 'csv'\n"
  "   -echo                print commands before execution\n"
  "   -init FILENAME       read/process named file\n"
  "   -gpkg                use the latest GeoPackage database schema\n"
  "   -gpkg10              use the GeoPackage 1.0 database schema\n"
  "   -gpkg11              use the GeoPackage 1.1 database schema\n"
  "   -gpkg12              use the GeoPackage 1.2 database schema\n"
  "   -[no]header          turn headers on or off\n"
#if defined(SQLITE_ENABLE_MEMSYS3) || defined(SQLITE_ENABLE_MEMSYS5)
  "   -heap SIZE           Size of heap for memsys3 or memsys5\n"
#endif
  "   -help                show this message\n"
  "   -html                set output mode to HTML\n"
  "   -interactive         force interactive I/O\n"
  "   -line                set output mode to 'line'\n"
  "   -list                set output mode to 'list'\n"
  "   -lookaside SIZE N    use N entries of SZ bytes for lookaside memory\n"
  "   -mmap N              default mmap size set to N\n"
#ifdef SQLITE_ENABLE_MULTIPLEX
  "   -multiplex           enable the multiplexor VFS\n"
#endif
  "   -newline SEP         set output row separator. Default: '\\n'\n"
  "   -nullvalue TEXT      set text string for NULL values. Default ''\n"
  "   -pagecache SIZE N    use N slots of SZ bytes each for page cache memory\n"
  "   -quote               set output mode to 'quote'\n"
  "   -readonly            open the database read-only\n"
  "   -separator SEP       set output column separator. Default: '|'\n"
  "   -spl3                use the Spatialite 3.x database schema\n"
  "   -spl4                use the Spatialite 4.x database schema\n"
#ifdef SQLITE_ENABLE_SORTER_REFERENCES
  "   -sorterref SIZE      sorter references threshold size\n"
#endif
  "   -stats               print memory stats before each finalize\n"
  "   -version             show SQLite version\n"
  "   -vfs NAME            use NAME as the default VFS\n"
#ifdef SQLITE_ENABLE_VFSTRACE
  "   -vfstrace            enable tracing of all VFS calls\n"
#endif
#ifdef SQLITE_HAVE_ZLIB
  "   -zip                 open the file as a ZIP Archive\n"
#endif
;
static void usage(int showDetail){
  utf8_printf(stderr,
      "Usage: %s [OPTIONS] FILENAME [SQL]\n"
      "FILENAME is the name of an SQLite database. A new database is created\n"
      "if the file does not previously exist.\n", Argv0);
  if( showDetail ){
    utf8_printf(stderr, "OPTIONS include:\n%s", zOptions);
  }else{
    raw_printf(stderr, "Use the -help option for additional information\n");
  }
  exit(1);
}

/*
** Internal check:  Verify that the SQLite is uninitialized.  Print a
** error message if it is initialized.
*/
static void verify_uninitialized(void){
  if( sqlite3_config(-1)==SQLITE_MISUSE ){
    utf8_printf(stdout, "WARNING: attempt to configure SQLite after"
                        " initialization.\n");
  }
}

/*
** Initialize the state information in data
*/
static void main_init(ShellState *data) {
  memset(data, 0, sizeof(*data));
  data->normalMode = data->cMode = data->mode = MODE_List;
  data->autoExplain = 1;
  memcpy(data->colSeparator,SEP_Column, 2);
  memcpy(data->rowSeparator,SEP_Row, 2);
  data->showHeader = 0;
  data->shellFlgs = SHFLG_Lookaside;
  verify_uninitialized();
  sqlite3_config(SQLITE_CONFIG_URI, 1);
  sqlite3_config(SQLITE_CONFIG_LOG, shellLog, data);
  sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
  data->gpkgEntryPoint = sqlite3_gpkg_auto_init;
  sqlite3_snprintf(sizeof(mainPrompt), mainPrompt,"sqlite> ");
  sqlite3_snprintf(sizeof(continuePrompt), continuePrompt,"   ...> ");
}

/*
** Output text to the console in a font that attracts extra attention.
*/
#ifdef _WIN32
static void printBold(const char *zText){
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO defaultScreenInfo;
  GetConsoleScreenBufferInfo(out, &defaultScreenInfo);
  SetConsoleTextAttribute(out,
         FOREGROUND_RED|FOREGROUND_INTENSITY
  );
  printf("%s", zText);
  SetConsoleTextAttribute(out, defaultScreenInfo.wAttributes);
}
#else
static void printBold(const char *zText){
  printf("\033[1m%s\033[0m", zText);
}
#endif

/*
** Get the argument to an --option.  Throw an error and die if no argument
** is available.
*/
static char *cmdline_option_value(int argc, char **argv, int i){
  if( i==argc ){
    utf8_printf(stderr, "%s: Error: missing argument to %s\n",
            argv[0], argv[argc-1]);
    exit(1);
  }
  return argv[i];
}

#ifndef SQLITE_SHELL_IS_UTF8
#  if (defined(_WIN32) || defined(WIN32)) && defined(_MSC_VER)
#    define SQLITE_SHELL_IS_UTF8          (0)
#  else
#    define SQLITE_SHELL_IS_UTF8          (1)
#  endif
#endif

#if SQLITE_SHELL_IS_UTF8
int SQLITE_CDECL main(int argc, char **argv){
#else
int SQLITE_CDECL wmain(int argc, wchar_t **wargv){
  char **argv;
#endif
  char *zErrMsg = 0;
  ShellState data;
  const char *zInitFile = 0;
  int i;
  int rc = 0;
  int warnInmemoryDb = 0;
  int readStdin = 1;
  int nCmd = 0;
  char **azCmd = 0;
  const char *zVfs = 0;           /* Value of -vfs command-line option */
#if !SQLITE_SHELL_IS_UTF8
  char **argvToFree = 0;
  int argcToFree = 0;
#endif

  setBinaryMode(stdin, 0);
  setvbuf(stderr, 0, _IONBF, 0); /* Make sure stderr is unbuffered */
  stdin_is_interactive = isatty(0);
  stdout_is_console = isatty(1);

#if !defined(_WIN32_WCE)
  if( getenv("SQLITE_DEBUG_BREAK") ){
    if( isatty(0) && isatty(2) ){
      fprintf(stderr,
          "attach debugger to process %d and press any key to continue.\n",
          GETPID());
      fgetc(stdin);
    }else{
#if defined(_WIN32) || defined(WIN32)
      DebugBreak();
#elif defined(SIGTRAP)
      raise(SIGTRAP);
#endif
    }
  }
#endif

#if USE_SYSTEM_SQLITE+0!=1
  if( strncmp(sqlite3_sourceid(),SQLITE_SOURCE_ID,60)!=0 ){
    utf8_printf(stderr, "SQLite header and source version mismatch\n%s\n%s\n",
            sqlite3_sourceid(), SQLITE_SOURCE_ID);
    exit(1);
  }
#endif
  main_init(&data);

  /* On Windows, we must translate command-line arguments into UTF-8.
  ** The SQLite memory allocator subsystem has to be enabled in order to
  ** do this.  But we want to run an sqlite3_shutdown() afterwards so that
  ** subsequent sqlite3_config() calls will work.  So copy all results into
  ** memory that does not come from the SQLite memory allocator.
  */
#if !SQLITE_SHELL_IS_UTF8
  sqlite3_initialize();
  argvToFree = malloc(sizeof(argv[0])*argc*2);
  argcToFree = argc;
  argv = argvToFree + argc;
  if( argv==0 ) shell_out_of_memory();
  for(i=0; i<argc; i++){
    char *z = sqlite3_win32_unicode_to_utf8(wargv[i]);
    int n;
    if( z==0 ) shell_out_of_memory();
    n = (int)strlen(z);
    argv[i] = malloc( n+1 );
    if( argv[i]==0 ) shell_out_of_memory();
    memcpy(argv[i], z, n+1);
    argvToFree[i] = argv[i];
    sqlite3_free(z);
  }
  sqlite3_shutdown();
#endif

  assert( argc>=1 && argv && argv[0] );
  Argv0 = argv[0];

  /* Make sure we have a valid signal handler early, before anything
  ** else is done.
  */
#ifdef SIGINT
  signal(SIGINT, interrupt_handler);
#elif (defined(_WIN32) || defined(WIN32)) && !defined(_WIN32_WCE)
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

#ifdef SQLITE_SHELL_DBNAME_PROC
  {
    /* If the SQLITE_SHELL_DBNAME_PROC macro is defined, then it is the name
    ** of a C-function that will provide the name of the database file.  Use
    ** this compile-time option to embed this shell program in larger
    ** applications. */
    extern void SQLITE_SHELL_DBNAME_PROC(const char**);
    SQLITE_SHELL_DBNAME_PROC(&data.zDbFilename);
    warnInmemoryDb = 0;
  }
#endif

  /* Do an initial pass through the command-line argument to locate
  ** the name of the database file, the name of the initialization file,
  ** the size of the alternative malloc heap,
  ** and the first command to execute.
  */
  verify_uninitialized();
  for(i=1; i<argc; i++){
    char *z;
    z = argv[i];
    if( z[0]!='-' ){
      if( data.zDbFilename==0 ){
        data.zDbFilename = z;
      }else{
        /* Excesss arguments are interpreted as SQL (or dot-commands) and
        ** mean that nothing is read from stdin */
        readStdin = 0;
        nCmd++;
        azCmd = realloc(azCmd, sizeof(azCmd[0])*nCmd);
        if( azCmd==0 ) shell_out_of_memory();
        azCmd[nCmd-1] = z;
      }
    }
    if( z[1]=='-' ) z++;
    if( strcmp(z,"-separator")==0
     || strcmp(z,"-nullvalue")==0
     || strcmp(z,"-newline")==0
     || strcmp(z,"-cmd")==0
    ){
      (void)cmdline_option_value(argc, argv, ++i);
    }else if( strcmp(z,"-init")==0 ){
      zInitFile = cmdline_option_value(argc, argv, ++i);
    }else if( strcmp(z,"-batch")==0 ){
      /* Need to check for batch mode here to so we can avoid printing
      ** informational messages (like from process_sqliterc) before
      ** we do the actual processing of arguments later in a second pass.
      */
      stdin_is_interactive = 0;
    }else if( strcmp(z,"-heap")==0 ){
#if defined(SQLITE_ENABLE_MEMSYS3) || defined(SQLITE_ENABLE_MEMSYS5)
      const char *zSize;
      sqlite3_int64 szHeap;

      zSize = cmdline_option_value(argc, argv, ++i);
      szHeap = integerValue(zSize);
      if( szHeap>0x7fff0000 ) szHeap = 0x7fff0000;
      sqlite3_config(SQLITE_CONFIG_HEAP, malloc((int)szHeap), (int)szHeap, 64);
#else
      (void)cmdline_option_value(argc, argv, ++i);
#endif
    }else if( strcmp(z,"-gpkg")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_init;
    }else if( strcmp(z,"-gpkg10")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_1_0_init;
    }else if( strcmp(z,"-gpkg11")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_1_1_init;
    }else if( strcmp(z,"-gpkg12")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_1_2_init;
    }else if( strcmp(z,"-spl3")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_spl3_init;
    }else if( strcmp(z,"-spl4")==0 ){
        data.gpkgEntryPoint = sqlite3_gpkg_spl4_init;
    }else if( strcmp(z,"-pagecache")==0 ){
      int n, sz;
      sz = (int)integerValue(cmdline_option_value(argc,argv,++i));
      if( sz>70000 ) sz = 70000;
      if( sz<0 ) sz = 0;
      n = (int)integerValue(cmdline_option_value(argc,argv,++i));
      sqlite3_config(SQLITE_CONFIG_PAGECACHE,
                    (n>0 && sz>0) ? malloc(n*sz) : 0, sz, n);
      data.shellFlgs |= SHFLG_Pagecache;
    }else if( strcmp(z,"-lookaside")==0 ){
      int n, sz;
      sz = (int)integerValue(cmdline_option_value(argc,argv,++i));
      if( sz<0 ) sz = 0;
      n = (int)integerValue(cmdline_option_value(argc,argv,++i));
      if( n<0 ) n = 0;
      sqlite3_config(SQLITE_CONFIG_LOOKASIDE, sz, n);
      if( sz*n==0 ) data.shellFlgs &= ~SHFLG_Lookaside;
#ifdef SQLITE_ENABLE_VFSTRACE
    }else if( strcmp(z,"-vfstrace")==0 ){
      extern int vfstrace_register(
         const char *zTraceName,
         const char *zOldVfsName,
         int (*xOut)(const char*,void*),
         void *pOutArg,
         int makeDefault
      );
      vfstrace_register("trace",0,(int(*)(const char*,void*))fputs,stderr,1);
#endif
#ifdef SQLITE_ENABLE_MULTIPLEX
    }else if( strcmp(z,"-multiplex")==0 ){
      extern int sqlite3_multiple_initialize(const char*,int);
      sqlite3_multiplex_initialize(0, 1);
#endif
    }else if( strcmp(z,"-mmap")==0 ){
      sqlite3_int64 sz = integerValue(cmdline_option_value(argc,argv,++i));
      sqlite3_config(SQLITE_CONFIG_MMAP_SIZE, sz, sz);
#ifdef SQLITE_ENABLE_SORTER_REFERENCES
    }else if( strcmp(z,"-sorterref")==0 ){
      sqlite3_int64 sz = integerValue(cmdline_option_value(argc,argv,++i));
      sqlite3_config(SQLITE_CONFIG_SORTERREF_SIZE, (int)sz);
#endif
    }else if( strcmp(z,"-vfs")==0 ){
      zVfs = cmdline_option_value(argc, argv, ++i);
#ifdef SQLITE_HAVE_ZLIB
    }else if( strcmp(z,"-zip")==0 ){
      data.openMode = SHELL_OPEN_ZIPFILE;
#endif
    }else if( strcmp(z,"-append")==0 ){
      data.openMode = SHELL_OPEN_APPENDVFS;
    }else if( strcmp(z,"-readonly")==0 ){
      data.openMode = SHELL_OPEN_READONLY;
#if !defined(SQLITE_OMIT_VIRTUALTABLE) && defined(SQLITE_HAVE_ZLIB)
    }else if( strncmp(z, "-A",2)==0 ){
      /* All remaining command-line arguments are passed to the ".archive"
      ** command, so ignore them */
      break;
#endif
    }
  }
  verify_uninitialized();


#ifdef SQLITE_SHELL_INIT_PROC
  {
    /* If the SQLITE_SHELL_INIT_PROC macro is defined, then it is the name
    ** of a C-function that will perform initialization actions on SQLite that
    ** occur just before or after sqlite3_initialize(). Use this compile-time
    ** option to embed this shell program in larger applications. */
    extern void SQLITE_SHELL_INIT_PROC(void);
    SQLITE_SHELL_INIT_PROC();
  }
#else
  /* All the sqlite3_config() calls have now been made. So it is safe
  ** to call sqlite3_initialize() and process any command line -vfs option. */
  sqlite3_initialize();
#endif

  if( zVfs ){
    sqlite3_vfs *pVfs = sqlite3_vfs_find(zVfs);
    if( pVfs ){
      sqlite3_vfs_register(pVfs, 1);
    }else{
      utf8_printf(stderr, "no such VFS: \"%s\"\n", argv[i]);
      exit(1);
    }
  }

  if( data.zDbFilename==0 ){
#ifndef SQLITE_OMIT_MEMORYDB
    data.zDbFilename = ":memory:";
    warnInmemoryDb = argc==1;
#else
    utf8_printf(stderr,"%s: Error: no database filename specified\n", Argv0);
    return 1;
#endif
  }
  data.out = stdout;
  sqlite3_appendvfs_init(0,0,0);

  /* Go ahead and open the database file if it already exists.  If the
  ** file does not exist, delay opening it.  This prevents empty database
  ** files from being created if a user mistypes the database name argument
  ** to the sqlite command-line tool.
  */
  if( strcmp(":memory:", data.zDbFilename) == 0 || access(data.zDbFilename, 0)==0 ){
    open_db(&data, 0);
  }

  /* Process the initialization file if there is one.  If no -init option
  ** is given on the command line, look for a file named ~/.sqliterc and
  ** try to process it.
  */
  process_sqliterc(&data,zInitFile);

  /* Make a second pass through the command-line argument and set
  ** options.  This second pass is delayed until after the initialization
  ** file is processed so that the command-line arguments will override
  ** settings in the initialization file.
  */
  for(i=1; i<argc; i++){
    char *z = argv[i];
    if( z[0]!='-' ) continue;
    if( z[1]=='-' ){ z++; }
    if( strcmp(z,"-init")==0 ){
      i++;
    }else if( strcmp(z,"-html")==0 ){
      data.mode = MODE_Html;
    }else if( strcmp(z,"-list")==0 ){
      data.mode = MODE_List;
    }else if( strcmp(z,"-quote")==0 ){
      data.mode = MODE_Quote;
    }else if( strcmp(z,"-line")==0 ){
      data.mode = MODE_Line;
    }else if( strcmp(z,"-column")==0 ){
      data.mode = MODE_Column;
    }else if( strcmp(z,"-csv")==0 ){
      data.mode = MODE_Csv;
      memcpy(data.colSeparator,",",2);
#ifdef SQLITE_HAVE_ZLIB
    }else if( strcmp(z,"-zip")==0 ){
      data.openMode = SHELL_OPEN_ZIPFILE;
#endif
    }else if( strcmp(z,"-append")==0 ){
      data.openMode = SHELL_OPEN_APPENDVFS;
    }else if( strcmp(z,"-readonly")==0 ){
      data.openMode = SHELL_OPEN_READONLY;
    }else if( strcmp(z,"-ascii")==0 ){
      data.mode = MODE_Ascii;
      sqlite3_snprintf(sizeof(data.colSeparator), data.colSeparator,
                       SEP_Unit);
      sqlite3_snprintf(sizeof(data.rowSeparator), data.rowSeparator,
                       SEP_Record);
    }else if( strcmp(z,"-separator")==0 ){
      sqlite3_snprintf(sizeof(data.colSeparator), data.colSeparator,
                       "%s",cmdline_option_value(argc,argv,++i));
    }else if( strcmp(z,"-newline")==0 ){
      sqlite3_snprintf(sizeof(data.rowSeparator), data.rowSeparator,
                       "%s",cmdline_option_value(argc,argv,++i));
    }else if( strcmp(z,"-nullvalue")==0 ){
      sqlite3_snprintf(sizeof(data.nullValue), data.nullValue,
                       "%s",cmdline_option_value(argc,argv,++i));
    }else if( strcmp(z,"-header")==0 ){
      data.showHeader = 1;
    }else if( strcmp(z,"-noheader")==0 ){
      data.showHeader = 0;
    }else if( strcmp(z,"-echo")==0 ){
      ShellSetFlag(&data, SHFLG_Echo);
    }else if( strcmp(z,"-eqp")==0 ){
      data.autoEQP = AUTOEQP_on;
    }else if( strcmp(z,"-eqpfull")==0 ){
      data.autoEQP = AUTOEQP_full;
    }else if( strcmp(z,"-stats")==0 ){
      data.statsOn = 1;
    }else if( strcmp(z,"-scanstats")==0 ){
      data.scanstatsOn = 1;
    }else if( strcmp(z,"-backslash")==0 ){
      /* Undocumented command-line option: -backslash
      ** Causes C-style backslash escapes to be evaluated in SQL statements
      ** prior to sending the SQL into SQLite.  Useful for injecting
      ** crazy bytes in the middle of SQL statements for testing and debugging.
      */
      ShellSetFlag(&data, SHFLG_Backslash);
    }else if( strcmp(z,"-bail")==0 ){
      bail_on_error = 1;
    }else if( strcmp(z,"-version")==0 ){
      printf("%s %s\n", sqlite3_libversion(), sqlite3_sourceid());
      return 0;
    }else if( strcmp(z,"-interactive")==0 ){
      stdin_is_interactive = 1;
    }else if( strcmp(z,"-batch")==0 ){
      stdin_is_interactive = 0;
    }else if( strcmp(z,"-heap")==0 ){
      i++;
    }else if( strcmp(z,"-pagecache")==0 ){
      i+=2;
    }else if( strcmp(z,"-lookaside")==0 ){
      i+=2;
    }else if( strcmp(z,"-mmap")==0 ){
      i++;
#ifdef SQLITE_ENABLE_SORTER_REFERENCES
    }else if( strcmp(z,"-sorterref")==0 ){
      i++;
#endif
    }else if( strcmp(z,"-vfs")==0 ){
      i++;
#ifdef SQLITE_ENABLE_VFSTRACE
    }else if( strcmp(z,"-vfstrace")==0 ){
      i++;
#endif
#ifdef SQLITE_ENABLE_MULTIPLEX
    }else if( strcmp(z,"-multiplex")==0 ){
      i++;
#endif
    }else if( strcmp(z,"-help")==0 ){
      usage(1);
    }else if( strcmp(z,"-cmd")==0 ){
      /* Run commands that follow -cmd first and separately from commands
      ** that simply appear on the command-line.  This seems goofy.  It would
      ** be better if all commands ran in the order that they appear.  But
      ** we retain the goofy behavior for historical compatibility. */
      if( i==argc-1 ) break;
      z = cmdline_option_value(argc,argv,++i);
      if( z[0]=='.' ){
        rc = do_meta_command(z, &data);
        if( rc && bail_on_error ) return rc==2 ? 0 : rc;
      }else{
        open_db(&data, 0);
        rc = shell_exec(&data, z, &zErrMsg);
        if( zErrMsg!=0 ){
          utf8_printf(stderr,"Error: %s\n", zErrMsg);
          if( bail_on_error ) return rc!=0 ? rc : 1;
        }else if( rc!=0 ){
          utf8_printf(stderr,"Error: unable to process SQL \"%s\"\n", z);
          if( bail_on_error ) return rc;
        }
      }
#if !defined(SQLITE_OMIT_VIRTUALTABLE) && defined(SQLITE_HAVE_ZLIB)
    }else if( strncmp(z, "-A", 2)==0 ){
      if( nCmd>0 ){
        utf8_printf(stderr, "Error: cannot mix regular SQL or dot-commands"
                            " with \"%s\"\n", z);
        return 1;
      }
      open_db(&data, OPEN_DB_ZIPFILE);
      if( z[2] ){
        argv[i] = &z[2];
        arDotCommand(&data, 1, argv+(i-1), argc-(i-1));
      }else{
        arDotCommand(&data, 1, argv+i, argc-i);
      }
      readStdin = 0;
      break;
#endif
    }else if( strcmp(z,"-gpkg")==0 || strcmp(z,"-spl3")==0 || strcmp(z,"-spl4")==0 ){
      i++;
    }else{
      utf8_printf(stderr,"%s: Error: unknown option: %s\n", Argv0, z);
      raw_printf(stderr,"Use -help for a list of options.\n");
      return 1;
    }
    data.cMode = data.mode;
  }

  if( !readStdin ){
    /* Run all arguments that do not begin with '-' as if they were separate
    ** command-line inputs, except for the argToSkip argument which contains
    ** the database filename.
    */
    for(i=0; i<nCmd; i++){
      if( azCmd[i][0]=='.' ){
        rc = do_meta_command(azCmd[i], &data);
        if( rc ) return rc==2 ? 0 : rc;
      }else{
        open_db(&data, 0);
        rc = shell_exec(&data, azCmd[i], &zErrMsg);
        if( zErrMsg!=0 ){
          utf8_printf(stderr,"Error: %s\n", zErrMsg);
          return rc!=0 ? rc : 1;
        }else if( rc!=0 ){
          utf8_printf(stderr,"Error: unable to process SQL: %s\n", azCmd[i]);
          return rc;
        }
      }
    }
    free(azCmd);
  }else{
    /* Run commands received from standard input
    */
    if( stdin_is_interactive ){
      char *zHome;
      char *zHistory = 0;
      int nHistory;
      printf(
        "SQLite version %s %.19s\n" /*extra-version-info*/
        "libgpkg version %s\n"
        "Enter \".help\" for usage hints.\n",
        sqlite3_libversion(), sqlite3_sourceid(),
        gpkg_libversion()
      );
      if( warnInmemoryDb ){
        printf("Connected to a ");
        printBold("transient in-memory database");
        printf(".\nUse \".open FILENAME\" to reopen on a "
               "persistent database.\n");
      }
      zHome = find_home_dir(0);
      if( zHome ){
        nHistory = strlen30(zHome) + 20;
        if( (zHistory = malloc(nHistory))!=0 ){
          sqlite3_snprintf(nHistory, zHistory,"%s/.sqlite_history", zHome);
        }
      }
      if( zHistory ){ shell_read_history(zHistory); }
#if HAVE_READLINE || HAVE_EDITLINE
      rl_attempted_completion_function = readline_completion;
#elif HAVE_LINENOISE
      linenoiseSetCompletionCallback(linenoise_completion);
#endif
      rc = process_input(&data, 0);
      if( zHistory ){
        shell_stifle_history(2000);
        shell_write_history(zHistory);
        free(zHistory);
      }
    }else{
      rc = process_input(&data, stdin);
    }
  }
  set_table_name(&data, 0);
  if( data.db ){
    session_close_all(&data);
    close_db(data.db);
  }
  sqlite3_free(data.zFreeOnClose);
  find_home_dir(1);
  output_reset(&data);
  data.doXdgOpen = 0;
  clearTempFile(&data);
#if !SQLITE_SHELL_IS_UTF8
  for(i=0; i<argcToFree; i++) free(argvToFree[i]);
  free(argvToFree);
#endif
  /* Clear the global data structure so that valgrind will detect memory
  ** leaks */
  memset(&data, 0, sizeof(data));
  return rc;
}

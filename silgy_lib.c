/* --------------------------------------------------------------------------
   Silgy Web App Engine
   Jurek Muszynski
   silgy.com
   Some parts are Public Domain from other authors, see below
-----------------------------------------------------------------------------
   General purpose library
-------------------------------------------------------------------------- */


#include "silgy.h"


static char M_df=0;         /* date format */
static char M_tsep=' ';     /* thousand separator */
static char M_dsep='.';     /* decimal separator */


static char *unescstring(char *src, int srclen, char *dest, int maxlen);
static int xctod(int c);
static void minify_1(char *dest, const char *src);
static int minify_2(char *dest, const char *src);
static void set_param(const char *label, const char *value);



/* --------------------------------------------------------------------------
   Calculate elapsed time
-------------------------------------------------------------------------- */
double lib_elapsed(struct timespec *start)
{
struct timespec end;
    double      elapsed;
    clock_gettime(MONOTONIC_CLOCK_NAME, &end);
    elapsed = (end.tv_sec - start->tv_sec) * 1000.0;
    elapsed += (end.tv_nsec - start->tv_nsec) / 1000000.0;
    return elapsed;
}


/* --------------------------------------------------------------------------
   Log the memory footprint
-------------------------------------------------------------------------- */
void lib_log_memory()
{
    long        mem_used;
    char        mem_used_kb[32];
    char        mem_used_mb[32];
    char        mem_used_gb[32];

    mem_used = lib_get_memory();

    amt(mem_used_kb, mem_used);
    amtd(mem_used_mb, (double)mem_used/1024);
    amtd(mem_used_gb, (double)mem_used/1024/1024);

    ALWAYS("Memory: %s kB (%s MB / %s GB)", mem_used_kb, mem_used_mb, mem_used_gb);
}


/* --------------------------------------------------------------------------
   For lib_memory
-------------------------------------------------------------------------- */
static int mem_parse_line(const char* line)
{
    long    ret=0;
    int     i=0;
    char    strret[64];
    const char* p=line;

    while (!isdigit(*p)) ++p;       /* skip non-digits */

    while (isdigit(*p)) strret[i++] = *p++;
        
    strret[i] = EOS;

/*  DBG("mem_parse_line: line [%s]", line);
    DBG("mem_parse_line: strret [%s]", strret);*/

    ret = atol(strret);

    return ret;
}


/* --------------------------------------------------------------------------
   Return currently used memory (high water mark) by current process in kB
-------------------------------------------------------------------------- */
long lib_get_memory()
{
    long result=0;

#ifdef __linux__

    char line[128];
    FILE* file = fopen("/proc/self/status", "r");

    if ( !file )
    {
        ERR("fopen(\"/proc/self/status\" failed, errno = %d (%s)", errno, strerror(errno));
        return result;
    }

    while ( fgets(line, 128, file) != NULL )
    {
        if ( strncmp(line, "VmHWM:", 6) == 0 )
        {
            result = mem_parse_line(line);
            break;
        }
    }

    fclose(file);

#else   /* UNIX */

struct rusage usage;

    getrusage(RUSAGE_SELF, &usage);
    result = usage.ru_maxrss;

#endif

    return result;
}


/* --------------------------------------------------------------------------
   Add spaces to make string to have len length
-------------------------------------------------------------------------- */
char *lib_add_spaces(const char *src, int len)
{
static char ret[256];
    int     src_len;
    int     spaces;
    int     i;

    src_len = strlen(src);

    spaces = len - src_len;

    if ( spaces < 0 ) spaces = 0;

    strcpy(ret, src);

    for ( i=src_len; i<len; ++i )
        ret[i] = ' ';

    ret[i] = EOS;

    return ret;
}


/* --------------------------------------------------------------------------
   Send error description as plain, pipe-delimited text
-------------------------------------------------------------------------- */
void lib_send_ajax_msg(int ci, int errcode)
{
    char    id[4]="msg";        /* HTML id */
    char    msg[256];
    char    cat='E';            /* category = 'Error' by default */

    if ( errcode == OK )
    {
        strcpy(id, "0");
        cat = 'I';
    }
//  else if ( errcode < 0 )     /* server error */
//  {
//  }
    else if ( errcode > 0 && errcode < 20 ) /* login */
    {
        strcpy(id, "loe");
    }
    else if ( errcode < 30 )    /* email */
    {
        strcpy(id, "eme");
    }
    else if ( errcode < 40 )    /* password */
    {
        strcpy(id, "pae");
    }
    else if ( errcode < 50 )    /* repeat password */
    {
        strcpy(id, "pre");
    }
    else if ( errcode < 60 )    /* old password */
    {
        strcpy(id, "poe");
    }
//  else if ( errcode < 100 )   /* other error */
//  {
//  }
    else if ( errcode < 200 )   /* warning (yellow) */
    {
        cat = 'W';
    }
    else if ( errcode < 1000 )  /* info (green) */
    {
        cat = 'I';
    }
//  else    /* app error */
//  {
//  }

#ifndef ASYNC_SERVICE
    eng_get_msg_str(ci, msg, errcode);
    OUT("%s|%s|%c", id, msg, cat);

    DBG("lib_send_ajax_msg: [%s]", G_tmp);

    conn[ci].ctype = RES_TEXT;
    RES_DONT_CACHE;
#endif
}


/* --------------------------------------------------------------------------
  determine resource type by its extension
-------------------------------------------------------------------------- */
char get_res_type(const char *fname)
{
    char    *ext=NULL;
    char    uext[8]="";

//  DBG("name: [%s]", fname);

    if ( (ext=(char*)strrchr(fname, '.')) == NULL )     /* no dot */
        return RES_TEXT;

    if ( ext-fname == strlen(fname)-1 )             /* dot is the last char */
        return RES_TEXT;

    ++ext;

    if ( strlen(ext) > 4 )                          /* extension too long */
        return RES_TEXT;

//  DBG("ext: [%s]", ext);

    strcpy(uext, upper(ext));

    if ( 0==strcmp(uext, "HTML") || 0==strcmp(uext, "HTM") )
        return RES_HTML;
    else if ( 0==strcmp(uext, "CSS") )
        return RES_CSS;
    else if ( 0==strcmp(uext, "JS") )
        return RES_JS;
    else if ( 0==strcmp(uext, "PDF") )
        return RES_PDF;
    else if ( 0==strcmp(uext, "GIF") )
        return RES_GIF;
    else if ( 0==strcmp(uext, "JPG") )
        return RES_JPG;
    else if ( 0==strcmp(uext, "ICO") )
        return RES_ICO;
    else if ( 0==strcmp(uext, "PNG") )
        return RES_PNG;
    else if ( 0==strcmp(uext, "BMP") )
        return RES_BMP;
    else if ( 0==strcmp(uext, "MP3") )
        return RES_AMPEG;
    else if ( 0==strcmp(uext, "EXE") )
        return RES_EXE;
    else if ( 0==strcmp(uext, "ZIP") )
        return RES_ZIP;

}


/* --------------------------------------------------------------------------
  convert URI (YYYY-MM-DD) date to tm struct
-------------------------------------------------------------------------- */
void date_str2rec(const char *str, date_t *rec)
{
    int     len;
    int     i;
    int     j=0;
    char    part='Y';
    char    strtmp[8];

    len = strlen(str);

    /* empty or invalid date => return today */

    if ( len != 10 || str[4] != '-' || str[7] != '-' )
    {
        DBG("date_str2rec: empty or invalid date in URI, returning today");
        rec->year = G_ptm->tm_year+1900;
        rec->month = G_ptm->tm_mon+1;
        rec->day = G_ptm->tm_mday;
        return;
    }

    for (i=0; i<len; ++i)
    {
        if ( str[i] != '-' )
            strtmp[j++] = str[i];
        else    /* end of part */
        {
            strtmp[j] = EOS;
            if ( part == 'Y' )  /* year */
            {
                rec->year = atoi(strtmp);
                part = 'M';
            }
            else if ( part == 'M' ) /* month */
            {
                rec->month = atoi(strtmp);
                part = 'D';
            }
            j = 0;
        }
    }

    /* day */

    strtmp[j] = EOS;
    rec->day = atoi(strtmp);
}


/* --------------------------------------------------------------------------
   Convert date_t date to YYYY-MM-DD string
-------------------------------------------------------------------------- */
void date_rec2str(char *str, date_t *rec)
{
    sprintf(str, "%d-%02d-%02d", rec->year, rec->month, rec->day);
}


/* --------------------------------------------------------------------------
   Convert HTTP time to epoch
   Tue, 18 Oct 2016 13:13:03 GMT
   Thu, 24 Nov 2016 21:19:40 GMT
-------------------------------------------------------------------------- */
time_t time_http2epoch(const char *str)
{
    time_t  epoch;
    char    tmp[8];
struct tm   tm;
//  char    *temp;  // temporarily

    // temporarily
//  DBG("time_http2epoch in:  [%s]", str);

    if ( strlen(str) != 29 )
        return 0;

    /* day */

    strncpy(tmp, str+5, 2);
    tmp[2] = EOS;
    tm.tm_mday = atoi(tmp);

    /* month */

    strncpy(tmp, str+8, 3);
    tmp[3] = EOS;
    if ( 0==strcmp(tmp, "Feb") )
        tm.tm_mon = 1;
    else if ( 0==strcmp(tmp, "Mar") )
        tm.tm_mon = 2;
    else if ( 0==strcmp(tmp, "Apr") )
        tm.tm_mon = 3;
    else if ( 0==strcmp(tmp, "May") )
        tm.tm_mon = 4;
    else if ( 0==strcmp(tmp, "Jun") )
        tm.tm_mon = 5;
    else if ( 0==strcmp(tmp, "Jul") )
        tm.tm_mon = 6;
    else if ( 0==strcmp(tmp, "Aug") )
        tm.tm_mon = 7;
    else if ( 0==strcmp(tmp, "Sep") )
        tm.tm_mon = 8;
    else if ( 0==strcmp(tmp, "Oct") )
        tm.tm_mon = 9;
    else if ( 0==strcmp(tmp, "Nov") )
        tm.tm_mon = 10;
    else if ( 0==strcmp(tmp, "Dec") )
        tm.tm_mon = 11;
    else    /* January */
        tm.tm_mon = 0;

    /* year */

    strncpy(tmp, str+12, 4);
    tmp[4] = EOS;
    tm.tm_year = atoi(tmp) - 1900;

    /* hour */

    strncpy(tmp, str+17, 2);
    tmp[2] = EOS;
    tm.tm_hour = atoi(tmp);

    /* minute */

    strncpy(tmp, str+20, 2);
    tmp[2] = EOS;
    tm.tm_min = atoi(tmp);

    /* second */

    strncpy(tmp, str+23, 2);
    tmp[2] = EOS;
    tm.tm_sec = atoi(tmp);

//  DBG("%d-%02d-%02d %02d:%02d:%02d", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

#ifdef __linux__
    epoch = timegm(&tm);
#else
    epoch = mktime(&tm);
#endif

    // temporarily
//  char *temp = time_epoch2http(epoch);
//  DBG("time_http2epoch out: [%s]", temp);

    return epoch;
}


/* --------------------------------------------------------------------------
   Convert db time to epoch
   2016-12-25 12:15:00
-------------------------------------------------------------------------- */
time_t time_db2epoch(const char *str)
{
    time_t  epoch;
    char    tmp[8];
struct tm   tm;

    if ( strlen(str) != 19 )
        return 0;

    /* year */

    strncpy(tmp, str, 4);
    tmp[4] = EOS;
    tm.tm_year = atoi(tmp) - 1900;

    /* month */

    strncpy(tmp, str+5, 2);
    tmp[2] = EOS;
    tm.tm_mon = atoi(tmp) - 1;

    /* day */

    strncpy(tmp, str+8, 2);
    tmp[2] = EOS;
    tm.tm_mday = atoi(tmp);

    /* hour */

    strncpy(tmp, str+11, 2);
    tmp[2] = EOS;
    tm.tm_hour = atoi(tmp);

    /* minute */

    strncpy(tmp, str+14, 2);
    tmp[2] = EOS;
    tm.tm_min = atoi(tmp);

    /* second */

    strncpy(tmp, str+17, 2);
    tmp[2] = EOS;
    tm.tm_sec = atoi(tmp);

#ifdef __linux__
    epoch = timegm(&tm);
#else
    epoch = mktime(&tm);
#endif

    return epoch;
}


/* --------------------------------------------------------------------------
   Convert epoch to HTTP time
-------------------------------------------------------------------------- */
char *time_epoch2http(time_t epoch)
{
static char str[32];
struct tm   *ptm;

    ptm = gmtime(&epoch);
    strftime(str, 32, "%a, %d %b %Y %X %Z", ptm);
//  DBG("time_epoch2http: [%s]", str);
    return str;
}


/* --------------------------------------------------------------------------
   Set decimal & thousand separator
---------------------------------------------------------------------------*/
void lib_set_datetime_formats(const char *lang)
{
    char ulang[8];

    DBG("lib_set_datetime_formats, lang [%s]", lang);

    strcpy(ulang, upper(lang));

    // date format

    if ( 0==strcmp(ulang, "EN-US") )
        M_df = 1;
    else if ( 0==strcmp(ulang, "EN-GB") || 0==strcmp(ulang, "EN-AU") || 0==strcmp(ulang, "FR-FR") || 0==strcmp(ulang, "EN-IE") || 0==strcmp(ulang, "ES-ES") || 0==strcmp(ulang, "IT-IT") || 0==strcmp(ulang, "PT-PT") || 0==strcmp(ulang, "PT-BR") || 0==strcmp(ulang, "ES-AR") )
        M_df = 2;
    else if ( 0==strcmp(ulang, "PL-PL") || 0==strcmp(ulang, "RU-RU") || 0==strcmp(ulang, "DE-CH") || 0==strcmp(ulang, "FR-CH") )
        M_df = 3;
    else
        M_df = 0;

    // amount format

    if ( 0==strcmp(ulang, "EN-US") || 0==strcmp(ulang, "EN-GB") || 0==strcmp(ulang, "EN-AU") || 0==strcmp(ulang, "TH-TH") )
    {
        M_tsep = ',';
        M_dsep = '.';
    }
    else if ( 0==strcmp(ulang, "PL-PL") || 0==strcmp(ulang, "IT-IT") || 0==strcmp(ulang, "NB-NO") || 0==strcmp(ulang, "ES-ES") )
    {
        M_tsep = '.';
        M_dsep = ',';
    }
    else
    {
        M_tsep = ' ';
        M_dsep = ',';
    }
}


/* --------------------------------------------------------------------------
   Format amount
---------------------------------------------------------------------------*/
void amt(char *stramt, long in_amt)
{
    char    in_stramt[64];
    int     len;
    int     i, j=0;
    bool    minus=FALSE;

    sprintf(in_stramt, "%ld", in_amt);

    if ( in_stramt[0] == '-' )  /* change to proper UTF-8 minus sign */
    {
        strcpy(stramt, "− ");
        j = 4;
        minus = TRUE;
    }

    len = strlen(in_stramt);

/*  DBG("----- len = %d", len); */

    for ( i=(j?1:0); i<len; ++i, ++j )
    {
        if ( ((!minus && i) || (minus && i>1)) && !((len-i)%3) )
            stramt[j++] = M_tsep;
        stramt[j] = in_stramt[i];
    }

    stramt[j] = EOS;
}


/* --------------------------------------------------------------------------
   Format double amount
---------------------------------------------------------------------------*/
void amtd(char *stramt, double in_amt)
{
    char    in_stramt[64];
    int     len;
    int     i, j=0;
    bool    minus=FALSE;

    sprintf(in_stramt, "%0.2lf", in_amt);

    if ( in_stramt[0] == '-' )  /* change to proper UTF-8 minus sign */
    {
        strcpy(stramt, "− ");
        j = 4;
        minus = TRUE;
    }

    len = strlen(in_stramt);

    for ( i=(j?1:0); i<len; ++i, ++j )
    {
        if ( in_stramt[i]=='.' && M_dsep!='.' )
        {
            stramt[j] = M_dsep;
            continue;
        }
        else if ( ((!minus && i) || (minus && i>1)) && !((len-i)%3) && len-i > 3 && in_stramt[i] != ' ' && in_stramt[i-1] != ' ' && in_stramt[i-1] != '-' )
        {
            stramt[j++] = M_tsep;   /* extra character */
        }
        stramt[j] = in_stramt[i];
    }

    stramt[j] = EOS;
}


/* --------------------------------------------------------------------------
   Format double amount string to string
---------------------------------------------------------------------------*/
void samts(char *stramt, const char *in_amt)
{
    double  d;

    sscanf(in_amt, "%lf", &d);
    amtd(stramt, d);
}


/* --------------------------------------------------------------------------
   Format time (add separators between parts)
---------------------------------------------------------------------------*/
void ftm(char *strtm, long in_tm)
{
    char    in_strtm[16];
    int     i, j=0;
const char  sep=':';

    sprintf(in_strtm, "%06ld", in_tm);

    for ( i=0; i<6; ++i, ++j )
    {
        if ( i == 2 || i == 4 )
            strtm[j++] = sep;
        strtm[j] = in_strtm[i];
    }

    strtm[j] = EOS;
}


/* --------------------------------------------------------------------------
   Format date depending on M_df
---------------------------------------------------------------------------*/
char *fmt_date(short year, short month, short day)
{
static char date[16];

    if ( M_df == 1 )
        sprintf(date, "%02d/%02d/%d", month, day, year);
    else if ( M_df == 2 )
        sprintf(date, "%02d/%02d/%d", day, month, year);
    else if ( M_df == 3 )
        sprintf(date, "%02d.%02d.%d", day, month, year);
    else    /* M_df == 0 */
        sprintf(date, "%d-%02d-%02d", year, month, day);

    return date;
}


/* --------------------------------------------------------------------------
  Get incoming request data. TRUE if found.
-------------------------------------------------------------------------- */
bool get_qs_param(int ci, const char *fieldname, char *retbuf)
{
#ifndef ASYNC_SERVICE
    int     fnamelen;
    char    *p, *p2, *p3;
    int     len1;       /* fieldname len */
    int     len2;       /* value len */
    char    *querystring;
    int     vallen;

    fnamelen = strlen(fieldname);

    if ( conn[ci].post )
        querystring = conn[ci].data;
    else
        querystring = strchr(conn[ci].uri, '?');

    if ( querystring == NULL ) return FALSE;    /* no question mark => no values */

    if ( !conn[ci].post )
        ++querystring;      /* skip the question mark */

    for ( p=querystring; *p!=EOS; )
    {
        p2 = strchr(p, '=');    /* end of field name */
        p3 = strchr(p, '&');    /* end of value */

        if ( p3 != NULL )   /* more than one field */
            len2 = p3 - p;
        else            /* only one field in URI */
            len2 = strlen(p);

        if ( p2 == NULL || p3 != NULL && p2 > p3 )
        {
            /* no '=' present in this field */
            p3 += len2;
            continue;
        }

        len1 = p2 - p;  /* field name length */

        if ( len1 == fnamelen && strncmp(fieldname, p, len1) == 0 )
        {
            /* found it */

            vallen = len2 - len1 - 1;   /* value length before decoding */

            unescstring(p2+1, vallen, retbuf, MAX_URI_VAL_LEN);

            return TRUE;
        }

        /* try next value */

        p += len2;      /* skip current value */
        if ( *p == '&' ) ++p;   /* skip & */
    }

    /* not found */

    retbuf[0] = EOS;
#endif
    return FALSE;
}


/* --------------------------------------------------------------------------
  Get incoming request data -- long string version. TRUE if found.
  One of the exceptions from DRY rule for the performance reasons.
-------------------------------------------------------------------------- */
bool get_qs_param_long(int ci, const char *fieldname, char *retbuf)
{
#ifndef ASYNC_SERVICE
    int     fnamelen;
    char    *p, *p2, *p3;
    int     len1;       /* fieldname len */
    int     len2;       /* value len */
    char    *querystring;
    int     vallen;

    fnamelen = strlen(fieldname);

    if ( conn[ci].post )
        querystring = conn[ci].data;
    else
        querystring = strchr(conn[ci].uri, '?');

    if ( querystring == NULL ) return FALSE;    /* no question mark => no values */

    if ( !conn[ci].post )
        ++querystring;      /* skip the question mark */

    for ( p=querystring; *p!=EOS; )
    {
        p2 = strchr(p, '=');    /* end of field name */
        p3 = strchr(p, '&');    /* end of value */

        if ( p3 != NULL )   /* more than one field */
            len2 = p3 - p;
        else            /* only one field in URI */
            len2 = strlen(p);

        if ( p2 == NULL || p3 != NULL && p2 > p3 )
        {
            /* no '=' present in this field */
            p3 += len2;
            continue;
        }

        len1 = p2 - p;  /* field name length */

        if ( len1 == fnamelen && strncmp(fieldname, p, len1) == 0 )
        {
            /* found it */

            vallen = len2 - len1 - 1;   /* value length before decoding */

            unescstring(p2+1, vallen, retbuf, MAX_LONG_URI_VAL_LEN);

            return TRUE;
        }

        /* try next value */

        p += len2;      /* skip current value */
        if ( *p == '&' ) ++p;   /* skip & */
    }

    /* not found */

    retbuf[0] = EOS;
#endif
    return FALSE;
}


/* --------------------------------------------------------------------------
  Get text value from multipart-form-data
-------------------------------------------------------------------------- */
bool get_qs_param_multipart_txt(int ci, const char *fieldname, char *retbuf)
{
    char    *p;
    long    len;
    
    p = get_qs_param_multipart(ci, fieldname, &len, NULL);
    
    if ( !p ) return FALSE;

    if ( len > MAX_URI_VAL_LEN ) return FALSE;

    strncpy(retbuf, p, len);
    retbuf[len] = EOS;
    
    return TRUE;
}


/* --------------------------------------------------------------------------
  Experimental multipart-form-data receipt
  return length or -1 if error
  if retfname is not NULL then assume binary data and it must be the last
  data element
-------------------------------------------------------------------------- */
char *get_qs_param_multipart(int ci, const char *fieldname, long *retlen, char *retfname)
{
    int     blen;           /* boundary length */
    char    *cp;            /* current pointer */
#ifndef ASYNC_SERVICE
    char    *p;             /* tmp pointer */
    long    b;              /* tmp bytes count */
    char    fn[MAX_LABEL_LEN+1];    /* field name */
    char    *end;
    long    len;

    /* Couple of checks to make sure it's properly formatted multipart content */

    if ( conn[ci].in_ctype != CONTENT_TYPE_MULTIPART )
    {
        WAR("This is not multipart/form-data");
        return NULL;
    }

    if ( conn[ci].clen < 10 )
    {
        WAR("Content length seems to be too small for multipart (%ld)", conn[ci].clen);
        return NULL;
    }

    cp = conn[ci].data;

    if ( !conn[ci].boundary[0] )    /* find first end of line -- that would be end of boundary */
    {
        if ( NULL == (p=strchr(cp, '\n')) )
        {
            WAR("Request syntax error");
            return NULL;
        }

        b = p - cp - 2;     /* skip -- */

        if ( b < 2 )
        {
            WAR("Boundary appears to be too short (%ld)", b);
            return NULL;
        }
        else if ( b > 255 )
        {
            WAR("Boundary appears to be too long (%ld)", b);
            return NULL;
        }

        strncpy(conn[ci].boundary, cp+2, b);
        if ( conn[ci].boundary[b-1] == '\r' )
            conn[ci].boundary[b-1] = EOS;
        else
            conn[ci].boundary[b] = EOS;
    }

    blen = strlen(conn[ci].boundary);

    if ( conn[ci].data[conn[ci].clen-4] != '-' || conn[ci].data[conn[ci].clen-3] != '-' )
    {
        WAR("Content doesn't end with '--'");
        return NULL;
    }

    while (TRUE)    /* find the right section */
    {
        if ( NULL == (p=strstr(cp, conn[ci].boundary)) )
        {
            WAR("No (next) boundary found");
            return NULL;
        }

        b = p - cp + blen;
        cp += b;

        if ( NULL == (p=strstr(cp, "Content-Disposition: form-data;")) )
        {
            WAR("No Content-Disposition label");
            return NULL;
        }

        b = p - cp + 30;
        cp += b;

        if ( NULL == (p=strstr(cp, "name=\"")) )
        {
            WAR("No field name");
            return NULL;
        }

        b = p - cp + 6;
        cp += b;

//      DBG("field name starts from: [%s]", cp);

        if ( NULL == (p=strchr(cp, '"')) )
        {
            WAR("No field name closing quote");
            return NULL;
        }

        b = p - cp;

        if ( b > MAX_LABEL_LEN )
        {
            WAR("Field name too long (%ld)", b);
            return NULL;
        }

        strncpy(fn, cp, b);
        fn[b] = EOS;

//      DBG("fn: [%s]", fn);

        if ( 0==strcmp(fn, fieldname) )     /* found */
            break;

        cp += b;
    }

    /* find a file name */

    if ( retfname )
    {
        if ( NULL == (p=strstr(cp, "filename=\"")) )
        {
            WAR("No file name");
            return NULL;
        }

        b = p - cp + 10;
        cp += b;

    //  DBG("file name starts from: [%s]", cp);

        if ( NULL == (p=strchr(cp, '"')) )
        {
            WAR("No file name closing quote");
            return NULL;
        }

        b = p - cp;

        if ( b > 255 )
        {
            WAR("File name too long (%ld)", b);
            return NULL;
        }

        strncpy(fn, cp, b);
        fn[b] = EOS;        /* fn now contains file name */

        cp += b;
    }

    /* now look for the section header end where the actual data begins */

    if ( NULL == (p=strstr(cp, "\r\n\r\n")) )
    {
        WAR("No section header end");
        return NULL;
    }

    b = p - cp + 4;
    cp += b;        /* cp now points to the actual data */

    /* find out data length */

    if ( !retfname )    /* text */
    {
        if ( NULL == (end=strstr(cp, conn[ci].boundary)) )
        {
            WAR("No closing boundary found");
            return NULL;
        }

        len = end - cp - 4;     /* minus CRLF-- */
    }
    else    /* potentially binary content -- calculate rather than use strstr */
    {
        len = conn[ci].clen - (cp - conn[ci].data) - blen - 8;  /* fast version */
                                                                /* Note that the file content must come as last! */
    }

    if ( len < 0 )
    {
        WAR("Ooops, something went terribly wrong! Data length = %ld", len);
        return NULL;
    }

    /* everything looks good so far */

    *retlen = len;

    if ( retfname )
        strcpy(retfname, fn);
#endif
    return cp;
}


/* --------------------------------------------------------------------------
  decode src
-------------------------------------------------------------------------- */
static char *unescstring(char *src, int srclen, char *dest, int maxlen)
{
    char    *endp=src+srclen;
    char    *srcp;
    char    *destp=dest;
    int     nwrote=0;

    for ( srcp=src; srcp<endp; ++srcp )
    {
        if ( *srcp == '+' )
            *destp++ = ' ';
        else if ( *srcp == '%' )
        {
            *destp++ = 16 * xctod(*(srcp+1)) + xctod(*(srcp+2));
            srcp += 2;
        }
        else    /* copy as it is */
            *destp++ = *srcp;
        ++nwrote;
        if ( nwrote == maxlen )
        {
            DBG("URI val truncated");
            break;
        }
    }

    *destp = EOS;

    return dest;
}


/* --------------------------------------------------------------------------
 decode character
-------------------------------------------------------------------------- */
static int xctod(int c)
{
    if ( isdigit(c) )
        return c - '0';
    else if ( isupper(c) )
        return c - 'A' + 10;
    else if ( islower(c) )
        return c - 'a' + 10;
    else
        return 0;
}


/* --------------------------------------------------------------------------
   Sanitize user input
-------------------------------------------------------------------------- */
char const *san(const char *str)
{
static char san[1024];
    int     i=0, j=0;

    while ( str[i] && j<1022 )
    {
        if ( str[i] == '\'' )
        {
            san[j++] = '\'';
            san[j++] = '\'';
        }
        else if ( str[i] != '\r' && str[i] != '\n' && str[i] != '\\' && str[i] != '|' )
            san[j++] = str[i];

        ++i;
    }

    san[j] = EOS;

    return san;
}


/* --------------------------------------------------------------------------
  sanitize user input for database queries
-------------------------------------------------------------------------- */
char *san_long(const char *str)
{
static char tmp[MAX_LONG_URI_VAL_LEN+1];
    int     i=0, j=0;

    while ( str[i] != EOS )
    {
        if ( j > MAX_LONG_URI_VAL_LEN-5 )
            break;
        else if ( str[i] == '\'' )
        {
            tmp[j++] = '\'';
            tmp[j++] = '\'';
        }
        else if ( str[i] != '\\' )
            tmp[j++] = str[i];
        ++i;
    }

    tmp[j] = EOS;

//  strcpy(str, tmp);

    return tmp;
}


/* --------------------------------------------------------------------------
  sanitize user input for large text blocks with possible HTML tags
-------------------------------------------------------------------------- */
char *san_noparse(char *str)
{
static char tmp[MAX_LONG_URI_VAL_LEN+1];
    int     i=0, j=0;

    while ( str[i] != EOS )
    {
        if ( j > MAX_LONG_URI_VAL_LEN-5 )
            break;
        else if ( str[i] == '\'' )
        {
            tmp[j++] = '\'';
            tmp[j++] = '\'';
        }
        else if ( str[i] == '\\' )
        {
            tmp[j++] = '\\';
            tmp[j++] = '\\';
        }
        else if ( str[i] == '<' )
        {
            tmp[j++] = '&';
            tmp[j++] = 'l';
            tmp[j++] = 't';
            tmp[j++] = ';';
        }
        else if ( str[i] == '>' )
        {
            tmp[j++] = '&';
            tmp[j++] = 'g';
            tmp[j++] = 't';
            tmp[j++] = ';';
        }
        else if ( str[i] == '\n' )
        {
            tmp[j++] = '<';
            tmp[j++] = 'b';
            tmp[j++] = 'r';
            tmp[j++] = '>';
        }
        else if ( str[i] != '\r' )
            tmp[j++] = str[i];
        ++i;
    }

    tmp[j] = EOS;

    strcpy(str, tmp);

    return str;
}


/* --------------------------------------------------------------------------
  un-sanitize user input
-------------------------------------------------------------------------- */
void unsan(char *dst, const char *str)
{
    int     i=0, j=0;

    while ( str[i] != EOS )
    {
        if ( j > MAX_LONG_URI_VAL_LEN-1 )
            break;
        else if ( str[i] == '\'' )
        {
            dst[j++] = '\\';
            dst[j++] = '\'';
        }
        else if ( str[i] == '\\' )
        {
            dst[j++] = '\\';
            dst[j++] = '\\';
        }
        else
            dst[j++] = str[i];
        ++i;
    }

    dst[j] = EOS;
}


/* --------------------------------------------------------------------------
  un-sanitize user input for large text blocks with possible HTML tags
-------------------------------------------------------------------------- */
void unsan_noparse(char *dst, const char *str)
{
    int     i=0, j=0;

    while ( str[i] != EOS )
    {
        if ( j > MAX_LONG_URI_VAL_LEN-1 )
            break;
        else if ( str[i] == '\'' )
        {
            dst[j++] = '\\';
            dst[j++] = '\'';
        }
        else if ( i > 2
                    && str[i-3]=='&'
                    && str[i-2]=='l'
                    && str[i-1]=='t'
                    && str[i]==';' )
        {
            j -= 3;
            dst[j++] = '<';
        }
        else if ( i > 2
                    && str[i-3]=='&'
                    && str[i-2]=='g'
                    && str[i-1]=='t'
                    && str[i]==';' )
        {
            j -= 3;
            dst[j++] = '>';
        }
        else if ( i > 2
                    && str[i-3]=='<'
                    && str[i-2]=='b'
                    && str[i-1]=='r'
                    && str[i]=='>' )
        {
            j -= 3;
            dst[j++] = '\n';
        }
        else
            dst[j++] = str[i];
        ++i;
    }

    dst[j] = EOS;
}


/* --------------------------------------------------------------------------
   Convert string to upper
---------------------------------------------------------------------------*/
/*void str2upper(char *dest, const char *src)
{
    int i;

    for ( i=0; src[i]; ++i )
    {
        if ( src[i] >= 97 && src[i] <= 122 )
            dest[i] = src[i] - 32;
        else
            dest[i] = src[i];
    }

    dest[i] = EOS;
}*/


/* --------------------------------------------------------------------------
   Convert string to upper
---------------------------------------------------------------------------*/
char *upper(const char *str)
{
static char upper[1024];
    int     i;

    for ( i=0; str[i] && i<1023; ++i )
    {
        if ( str[i] >= 97 && str[i] <= 122 )
            upper[i] = str[i] - 32;
        else
            upper[i] = str[i];
    }

    upper[i] = EOS;

    return upper;
}


/* --------------------------------------------------------------------------
   Strip trailing spaces from string
-------------------------------------------------------------------------- */
char *stp_right(char *str)
{
    char *p;

    for ( p = str + strlen(str) - 1;
          p >= str && (*p == ' ' || *p == '\t');
          p-- )
          *p = 0;

    return str;
}


/* --------------------------------------------------------------------------
   Return TRUE if digits only
---------------------------------------------------------------------------*/
bool strdigits(const char *src)
{
    int i;

    for ( i=0; src[i]; ++i )
    {
        if ( !isdigit(src[i]) )
            return FALSE;
    }

    return TRUE;
}


/* --------------------------------------------------------------------------
   Copy string without spaces and tabs
---------------------------------------------------------------------------*/
char *nospaces(char *dst, const char *src)
{
    const char  *p=src;
    int     i=0;

    while ( *p )
    {
        if ( *p != ' ' && *p != '\t' )
            dst[i++] = *p;
        ++p;
    }

    dst[i] = EOS;

    return dst;
}


/* --------------------------------------------------------------------------
   Generate random string
-------------------------------------------------------------------------- */
void get_random_str(char *dest, int len)
{
const char  *chars="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static unsigned long req=0;
    int     max;
    int     i;

    max = strlen(chars);

    srand(time(NULL)*G_pid+req);

    ++req;

    for ( i=0; i<len; ++i )
        dest[i] = chars[rand() % max];

    dest[i] = EOS;
}


/* --------------------------------------------------------------------------
  sleep for n miliseconds
  n must be less than 1 second (< 1000)!
-------------------------------------------------------------------------- */
void msleep(long n)
{
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = n * 1000;
    select(0, NULL, NULL, NULL, &tv);
}


/* --------------------------------------------------------------------------
  check system's endianness
-------------------------------------------------------------------------- */
void get_byteorder32()
{
        union {
                long l;
                char c[4];
        } test;

        DBG("Checking 32-bit endianness...");

        memcpy(&test, 0, sizeof(test));

        test.l = 1;

        if ( test.c[3] && !test.c[2] && !test.c[1] && !test.c[0] )
        {
            DBG("This is big endian");
                return;
        }

        if ( !test.c[3] && !test.c[2] && !test.c[1] && test.c[0] )
        {
            DBG("This is little endian");
                return;
        }

        DBG("Unknown Endianness!");
}


/* --------------------------------------------------------------------------
  check system's endianness
-------------------------------------------------------------------------- */
void get_byteorder64()
{
        union {
                long l;
                char c[8];
        } test;

        DBG("Checking 64-bit endianness...");

        memcpy(&test, 0, sizeof(test));

        test.l = 1;

        if ( test.c[7] && !test.c[3] && !test.c[2] && !test.c[1] && !test.c[0] )
        {
            DBG("This is big endian");
                return;
        }

        if ( !test.c[7] && !test.c[3] && !test.c[2] && !test.c[1] && test.c[0] )
        {
            DBG("This is little endian");
                return;
        }

        DBG("Unknown Endianness!");
}


/* --------------------------------------------------------------------------
  convert database datetime to epoch time
-------------------------------------------------------------------------- */
time_t db2epoch(const char *str)
{

    int     i;
    int     j=0;
    char    part='Y';
    char    strtmp[8];
struct tm   t={0};

/*  DBG("db2epoch: str: [%s]", str); */

    for ( i=0; str[i]; ++i )
    {
        if ( isdigit(str[i]) )
            strtmp[j++] = str[i];
        else    /* end of part */
        {
            strtmp[j] = EOS;
            if ( part == 'Y' )  /* year */
            {
                t.tm_year = atoi(strtmp) - 1900;
                part = 'M';
            }
            else if ( part == 'M' ) /* month */
            {
                t.tm_mon = atoi(strtmp) - 1;
                part = 'D';
            }
            else if ( part == 'D' ) /* day */
            {
                t.tm_mday = atoi(strtmp);
                part = 'H';
            }
            else if ( part == 'H' ) /* hour */
            {
                t.tm_hour = atoi(strtmp);
                part = 'm';
            }
            else if ( part == 'm' ) /* minutes */
            {
                t.tm_min = atoi(strtmp);
                part = 's';
            }
            j = 0;
        }
    }

    /* seconds */

    strtmp[j] = EOS;
    t.tm_sec = atoi(strtmp);

    return mktime(&t);
}


/* --------------------------------------------------------------------------
  send an email
-------------------------------------------------------------------------- */
bool sendemail(int ci, const char *to, const char *subject, const char *message)
{
#ifndef ASYNC_SERVICE
    char    sender[256];
    char    *colon;
    char    comm[256];

    sprintf(sender, "%s <noreply@%s>", conn[ci].website, conn[ci].host);

    /* happens when using non-standard port */

    if ( G_test && (colon=strchr(sender, ':')) )
    {
        *colon = '>';
        *(++colon) = EOS;
        DBG("sender truncated to [%s]", sender);
    }

    sprintf(comm, "/usr/lib/sendmail -t -f \"%s\"", sender);

    FILE *mailpipe = popen(comm, "w");

    if ( mailpipe == NULL )
    {
        ERR("Failed to invoke sendmail");
        return FALSE;
    }
    else
    {
        DBG("Sending email to: [%s], subject: [%s]", to, subject);
        fprintf(mailpipe, "To: %s\n", to);
        fprintf(mailpipe, "From: %s\n", sender);
        fprintf(mailpipe, "Subject: %s\n\n", subject);
        fwrite(message, 1, strlen(message), mailpipe);
        fwrite("\n.\n", 1, 3, mailpipe);
        pclose(mailpipe);
    }
#endif
    return TRUE;
}


/* --------------------------------------------------------------------------
  minify CSS/JS -- new version
  remove all white spaces and new lines unless in quotes
  also remove // style comments
  add a space after some keywords
  return new length
-------------------------------------------------------------------------- */
int lib_minify(char *dest, const char *src)
{
    minify_1(dest, src);
    return minify_2(dest, dest);
}


/* --------------------------------------------------------------------------
  First pass -- only remove comments
-------------------------------------------------------------------------- */
static void minify_1(char *dest, const char *src)
{
    int     len;
    int     i;
    int     j=0;
    bool    opensq=FALSE;       /* single quote */
    bool    opendq=FALSE;       /* double quote */
    bool    openco=FALSE;       /* comment */
    bool    opensc=FALSE;       /* star comment */

    len = strlen(src);

    for ( i=0; i<len; ++i )
    {
        if ( !openco && !opensc && !opensq && src[i]=='"' && (i==0 || (i>0 && src[i-1]!='\\')) )
        {
            if ( !opendq )
                opendq = TRUE;
            else
                opendq = FALSE;
        }
        else if ( !openco && !opensc && !opendq && src[i]=='\'' )
        {
            if ( !opensq )
                opensq = TRUE;
            else
                opensq = FALSE;
        }
        else if ( !opensq && !opendq && !openco && !opensc && src[i]=='/' && src[i+1] == '/' )
        {
            openco = TRUE;
        }
        else if ( !opensq && !opendq && !openco && !opensc && src[i]=='/' && src[i+1] == '*' )
        {
            opensc = TRUE;
        }
        else if ( openco && src[i]=='\n' )
        {
            openco = FALSE;
        }
        else if ( opensc && src[i]=='*' && src[i+1]=='/' )
        {
            opensc = FALSE;
            i += 2;
        }

        if ( !openco && !opensc )       /* unless it's a comment ... */
            dest[j++] = src[i];
    }

    dest[j] = EOS;
}


/* --------------------------------------------------------------------------
  return new length
-------------------------------------------------------------------------- */
static int minify_2(char *dest, const char *src)
{
    int     len;
    int     i;
    int     j=0;
    bool    opensq=FALSE;       /* single quote */
    bool    opendq=FALSE;       /* double quote */
    bool    openbr=FALSE;       /* curly braces */
    bool    openwo=FALSE;       /* word */
    bool    opencc=FALSE;       /* colon */
    bool    skip_ws=FALSE;      /* skip white spaces */
    char    word[256]="";
    int     wi=0;               /* word index */

    len = strlen(src);

    for ( i=0; i<len; ++i )
    {
        if ( !opensq && src[i]=='"' && (i==0 || (i>0 && src[i-1]!='\\')) )
        {
            if ( !opendq )
                opendq = TRUE;
            else
                opendq = FALSE;
        }
        else if ( !opendq && src[i]=='\'' )
        {
            if ( !opensq )
                opensq = TRUE;
            else
                opensq = FALSE;
        }
        else if ( !opensq && !opendq && !openbr && src[i]=='{' )
        {
            openbr = TRUE;
            openwo = FALSE;
            wi = 0;
            skip_ws = TRUE;
        }
        else if ( !opensq && !opendq && openbr && src[i]=='}' )
        {
            openbr = FALSE;
            openwo = FALSE;
            wi = 0;
            skip_ws = TRUE;
        }
        else if ( !opensq && !opendq && openbr && !opencc && src[i]==':' )
        {
            opencc = TRUE;
            openwo = FALSE;
            wi = 0;
            skip_ws = TRUE;
        }
        else if ( !opensq && !opendq && opencc && src[i]==';' )
        {
            opencc = FALSE;
            openwo = FALSE;
            wi = 0;
            skip_ws = TRUE;
        }
        else if ( !opensq && !opendq && !opencc && !openwo && (isalpha(src[i]) || src[i]=='|' || src[i]=='&') ) /* word is starting */
        {
            openwo = TRUE;
        }
        else if ( !opensq && !opendq && openwo && !isalnum(src[i]) && src[i]!='_' && src[i]!='|' && src[i]!='&' )   /* end of word */
        {
            word[wi] = EOS;
            if ( 0==strcmp(word, "var")
                    || (0==strcmp(word, "function") && src[i]!='(')
                    || (0==strcmp(word, "else") && src[i]!='{')
                    || 0==strcmp(word, "new")
                    || (0==strcmp(word, "return") && src[i]!=';')
                    || 0==strcmp(word, "||")
                    || 0==strcmp(word, "&&") )
                dest[j++] = ' ';
            openwo = FALSE;
            wi = 0;
            skip_ws = TRUE;
        }

        if ( opensq || opendq
                || src[i+1] == '|' || src[i+1] == '&'
                || (src[i] != ' ' && src[i] != '\t' && src[i] != '\n' && src[i] != '\r')
                || opencc )
            dest[j++] = src[i];
        
        if ( openwo )
            word[wi++] = src[i];

        if ( skip_ws )
        {
            while ( src[i+1] && (src[i+1]==' ' || src[i+1]=='\t' || src[i+1]=='\n' || src[i+1]=='\r') ) ++i;
            skip_ws = FALSE;
        }
    }

    dest[j] = EOS;
    
    return j;
}


/* --------------------------------------------------------------------------
  add script to HTML head
-------------------------------------------------------------------------- */
void add_script(int ci, const char *fname, bool first)
{
#ifndef ASYNC_SERVICE
    if ( first )
    {
        DBG("first = TRUE; Defining ld()");
        OUT("function ld(n){var f=document.createElement('script');f.setAttribute(\"type\",\"text/javascript\");f.setAttribute(\"src\",n);document.getElementsByTagName(\"head\")[0].appendChild(f);}");
        first = FALSE;
    }
    OUT("ld('%s');", fname);
#endif
}


/* --------------------------------------------------------------------------
  increment date by 'days' days. Return day of week as well.
  Format: YYYY-MM-DD
-------------------------------------------------------------------------- */
void date_inc(char *str, int days, int *dow)
{
    char    full[32];
    time_t  told, tnew;

    sprintf(full, "%s 00:00:00", str);

    told = db2epoch(full);

    tnew = told + 3600*24*days;

    G_ptm = gmtime(&tnew);
    sprintf(str, "%d-%02d-%02d", G_ptm->tm_year+1900, G_ptm->tm_mon+1, G_ptm->tm_mday);
    *dow = G_ptm->tm_wday;

    G_ptm = gmtime(&G_now); /* set it back */

}


/* --------------------------------------------------------------------------
  compare the dates
  Format: YYYY-MM-DD
-------------------------------------------------------------------------- */
int date_cmp(const char *str1, const char *str2)
{
    char    full[32];
    time_t  t1, t2;

    sprintf(full, "%s 00:00:00", str1);
    t1 = db2epoch(full);

    sprintf(full, "%s 00:00:00", str2);
    t2 = db2epoch(full);

    return t1 - t2;
}


/* --------------------------------------------------------------------------
   Read & parse conf file and set global parameters
-------------------------------------------------------------------------- */
bool read_conf()
{
    char    default_conf_path[]="silgy.conf";
    char    *p_conf_path=NULL;
    FILE    *h_file=NULL;
    int     c=0;
    int     i=0;
    char    now_label=1;
    char    now_value=0;
    char    now_comment=0;
    char    label[64]="";
    char    value[256]="";

    /* set defaults */

    G_logLevel = 2;
    G_httpPort = 80;
    G_httpsPort = 443;
    G_certFile[0] = EOS;
    G_certChainFile[0] = EOS;
    G_keyFile[0] = EOS;
    G_dbName[0] = EOS;
    G_dbUser[0] = EOS;
    G_dbPassword[0] = EOS;
    G_blockedIPList[0] = EOS;
    G_test = 0;

    /* get the conf file path & name */

    if ( NULL == (p_conf_path=getenv("SILGY_CONF")) )
    {
        printf("SILGY_CONF not set, trying %s...\n", default_conf_path);
        p_conf_path = default_conf_path;
    }

    /* open the conf file */

    if ( NULL == (h_file=fopen(p_conf_path, "r")) )
    {
        printf("Error opening %s, using defaults.\n", p_conf_path);
        return FALSE;
    }

    /* parse the conf file */

    while ( EOF != (c=fgetc(h_file)) )
    {
        if ( c == '\r' ) continue;

        if ( !now_value && (c == ' ' || c == '\t') ) continue;  /* omit whitespaces */

        if ( c == '\n' )    /* end of value or end of comment or empty line */
        {
            if ( now_value )    /* end of value */
            {
                value[i] = EOS;
                set_param(label, value);
            }
            now_label = 1;
            now_value = 0;
            now_comment = 0;
            i = 0;
        }
        else if ( now_comment )
        {
            continue;
        }
        else if ( c == '=' )    /* end of label */
        {
            now_label = 0;
            now_value = 1;
            label[i] = EOS;
            i = 0;
        }
        else if ( c == '#' )    /* possible end of value */
        {
            if ( now_value )    /* end of value */
            {
                value[i] = EOS;
                set_param(label, value);
            }
            now_label = 0;
            now_value = 0;
            now_comment = 1;
            i = 0;
        }
        else if ( now_label )   /* label */
        {
            label[i] = c;
            ++i;
        }
        else if ( now_value )   /* value */
        {
            value[i] = c;
            ++i;
        }
    }

    if ( now_value )    /* end of value */
    {
        value[i] = EOS;
        set_param(label, value);
    }

    if ( NULL != h_file )
        fclose(h_file);

    return TRUE;
}


/* --------------------------------------------------------------------------
   Set global parameters read from conf file
-------------------------------------------------------------------------- */
static void set_param(const char *label, const char *value)
{
    if ( PARAM("logLevel") )
        G_logLevel = atoi(value);
    else if ( PARAM("httpPort") )
        G_httpPort = atoi(value);
    else if ( PARAM("httpsPort") )
        G_httpsPort = atoi(value);
    else if ( PARAM("cipherList") )
        strcpy(G_cipherList, value);
    else if ( PARAM("certFile") )
        strcpy(G_certFile, value);
    else if ( PARAM("certChainFile") )
        strcpy(G_certChainFile, value);
    else if ( PARAM("keyFile") )
        strcpy(G_keyFile, value);
//  else if ( PARAM("dbHost") )
//      strcpy(G_dbHost, value);
//  else if ( PARAM("dbPort") )
//      G_dbPort = atoi(value);
    else if ( PARAM("dbName") )
        strcpy(G_dbName, value);
    else if ( PARAM("dbUser") )
        strcpy(G_dbUser, value);
    else if ( PARAM("dbPassword") )
        strcpy(G_dbPassword, value);
    else if ( PARAM("blockedIPList") )
        strcpy(G_blockedIPList, value);
    else if ( PARAM("test") )
        G_test = atoi(value);
#ifndef ASYNC_SERVICE
    app_set_param(label, value);
#endif
}


/* --------------------------------------------------------------------------
  start a log. uses global G_log as file handler
-------------------------------------------------------------------------- */
bool log_start(bool test)
{
    char    prefix[256];
    char    file_name[512];

    sprintf(prefix, "%s/logs/%d%02d%02d_%02d%02d", G_appdir, G_ptm->tm_year+1900, G_ptm->tm_mon+1, G_ptm->tm_mday, G_ptm->tm_hour, G_ptm->tm_min);

    if ( test )
        sprintf(file_name, "%s_t.log", prefix);
    else
        sprintf(file_name, "%s.log", prefix);

    if ( NULL == (G_log=fopen(file_name, "a")) )
    {
        printf("ERROR: Couldn't open log file. Make sure %s is defined in your environment and there is a `logs' directory there.\n", APP_DIR);
        return FALSE;
    }

    if ( fprintf(G_log, "----------------------------------------------------------------------------------------------\n") < 0 )
    {
        perror("fprintf");
        return FALSE;
    }

    ALWAYS(" %s  Starting %s's log. Server version: %s, app version: %s", G_dt, APP_WEBSITE, WEB_SERVER_VERSION, APP_VERSION);

    fprintf(G_log, "----------------------------------------------------------------------------------------------\n\n");

    return TRUE;
}


/* --------------------------------------------------------------------------
   Write to log with date/time. Uses global G_log as file handler
-------------------------------------------------------------------------- */
void log_write_time(int level, const char *message, ...)
{
    va_list     plist;
static char     buffer[MAX_LOG_STR_LEN+1+64];   /* don't use stack */

    if ( level > G_logLevel ) return;

    /* output timestamp */

    fprintf(G_log, "[%s] ", G_dt);

    if ( LOG_ERR == level )
        fprintf(G_log, "ERROR: ");
    else if ( LOG_WAR == level )
        fprintf(G_log, "WARNING: ");

    /* compile message with arguments into buffer */

    va_start(plist, message);
    vsprintf(buffer, message, plist);
    va_end(plist);

    /* write to log file */

    fprintf(G_log, "%s\n", buffer);

#ifdef DUMP
    fflush(G_log);
#else
    if ( G_logLevel >= LOG_DBG ) fflush(G_log);
#endif
}


/* --------------------------------------------------------------------------
   Write to log. Uses global G_log as file handler
-------------------------------------------------------------------------- */
void log_write(int level, const char *message, ...)
{
    va_list     plist;
static char     buffer[MAX_LOG_STR_LEN+1+64];   /* don't use stack */

    if ( level > G_logLevel ) return;

    if ( LOG_ERR == level )
        fprintf(G_log, "ERROR: ");
    else if ( LOG_WAR == level )
        fprintf(G_log, "WARNING: ");

    /* compile message with arguments into buffer */

    va_start(plist, message);
    vsprintf(buffer, message, plist);
    va_end(plist);

    /* write to log file */

    fprintf(G_log, "%s\n", buffer);

#ifdef DUMP
    fflush(G_log);
#else
    if ( G_logLevel >= LOG_DBG ) fflush(G_log);
#endif
}


/* --------------------------------------------------------------------------
  write looong string to a log or --
  its first (MAX_LOG_STR_LEN-50) part if it's longer
-------------------------------------------------------------------------- */
void log_long(const char *str, long len, const char *desc)
{
static char log_buffer[MAX_LOG_STR_LEN+1];

    if ( len < MAX_LOG_STR_LEN-50 )
        DBG("%s:\n\n[%s]\n", desc, str);
    else
    {
        strncpy(log_buffer, str, MAX_LOG_STR_LEN-50);
        strcpy(log_buffer+MAX_LOG_STR_LEN-50, " (...)");
        DBG("%s:\n\n[%s]\n", desc, log_buffer);
    }
}


/* --------------------------------------------------------------------------
  close log. uses global G_log as file handler
-------------------------------------------------------------------------- */
void log_finish()
{
    if ( !G_log ) return;

    ALWAYS("Closing log");
    fclose(G_log);
}


/* --------------------------------------------------------------------------
  Znowu chamskie kopiowanie
  Jurek Muszynski
-------------------------------------------------------------------------- */
void maz2utf(char* output, const char* input)
{
    unsigned char* iPtr=(unsigned char*)input;
    unsigned char* oPtr=(unsigned char*)output;
//  int i=0;

    while (*iPtr)
    {
        switch (*iPtr)
        {
            case 0x8f: /* A */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x84;
                break;
            case 0x95: /* C */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x86;
                break; 
            case 0x90: /* E */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x98;
                break; 
            case 0x9c: /* L */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x81;
                break; 
            case 0xa5: /* N */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x83;
                break;
            case 0xa3: /* O */
                *oPtr=0xc3; ++oPtr;
                *oPtr=0x93;
                break; 
            case 0x98: /* S */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x9a;
                break;
            case 0xa0: /* Z */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0xb9;
                break; 
            case 0xa1: /* Z */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0xbb;
                break; 
            case 0x86: /* a */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x85;
                break; 
            case 0x8d: /* c */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x87;
                break; 
            case 0x91: /* e */
                *oPtr=0xc4; ++oPtr;
                *oPtr=0x99;
                break; 
            case 0x92: /* l */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x82;
                break; 
            case 0xa4: /* n */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x84;
                break; 
            case 0xa2: /* o */
                *oPtr=0xc3; ++oPtr;
                *oPtr=0xb3;
                break; 
            case 0x9e: /* s */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0x9b;
                break; 
            case 0xa6: /* z */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0xba;
                break; 
            case 0xa7: /* z */
                *oPtr=0xc5; ++oPtr;
                *oPtr=0xbc;
                break; 

            default:
                *oPtr = *iPtr;
        }
        ++oPtr;
        ++iPtr;
//      ++i;
    }
    *oPtr=0;
}



/*
 * Copyright (c) 2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/* ====================================================================
 * Copyright (c) 1995-1999 The Apache Group.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * 4. The names "Apache Server" and "Apache Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache"
 *    nor may "Apache" appear in their names without prior written
 *    permission of the Apache Group.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * THIS SOFTWARE IS PROVIDED BY THE APACHE GROUP ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE APACHE GROUP OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Group and was originally based
 * on public domain software written at the National Center for
 * Supercomputing Applications, University of Illinois, Urbana-Champaign.
 * For more information on the Apache Group and the Apache HTTP server
 * project, please see <http://www.apache.org/>.
 *
 */

/* Base64 encoder/decoder. Originally Apache file ap_base64.c
 */

/* aaaack but it's fast and const should make it shared text page. */
static const unsigned char pr2six[256] =
{
    /* ASCII table */
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

int Base64decode_len(const char *bufcoded)
{
    int nbytesdecoded;
    register const unsigned char *bufin;
    register int nprbytes;

    bufin = (const unsigned char *) bufcoded;
    while (pr2six[*(bufin++)] <= 63);

    nprbytes = (bufin - (const unsigned char *) bufcoded) - 1;
    nbytesdecoded = ((nprbytes + 3) / 4) * 3;

    return nbytesdecoded + 1;
}

int Base64decode(char *bufplain, const char *bufcoded)
{
    int nbytesdecoded;
    register const unsigned char *bufin;
    register unsigned char *bufout;
    register int nprbytes;

    bufin = (const unsigned char *) bufcoded;
    while (pr2six[*(bufin++)] <= 63);
    nprbytes = (bufin - (const unsigned char *) bufcoded) - 1;
    nbytesdecoded = ((nprbytes + 3) / 4) * 3;

    bufout = (unsigned char *) bufplain;
    bufin = (const unsigned char *) bufcoded;

    while (nprbytes > 4) {
    *(bufout++) =
        (unsigned char) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    *(bufout++) =
        (unsigned char) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    *(bufout++) =
        (unsigned char) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    bufin += 4;
    nprbytes -= 4;
    }

    /* Note: (nprbytes == 1) would be an error, so just ingore that case */
    if (nprbytes > 1) {
    *(bufout++) =
        (unsigned char) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    }
    if (nprbytes > 2) {
    *(bufout++) =
        (unsigned char) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    }
    if (nprbytes > 3) {
    *(bufout++) =
        (unsigned char) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    }

    *(bufout++) = '\0';
    nbytesdecoded -= (4 - nprbytes) & 3;
    return nbytesdecoded;
}

static const char basis_64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int Base64encode_len(int len)
{
    return ((len + 2) / 3 * 4) + 1;
}

int Base64encode(char *encoded, const char *string, int len)
{
    int i;
    char *p;

    p = encoded;
    for (i = 0; i < len - 2; i += 3) {
    *p++ = basis_64[(string[i] >> 2) & 0x3F];
    *p++ = basis_64[((string[i] & 0x3) << 4) |
                    ((int) (string[i + 1] & 0xF0) >> 4)];
    *p++ = basis_64[((string[i + 1] & 0xF) << 2) |
                    ((int) (string[i + 2] & 0xC0) >> 6)];
    *p++ = basis_64[string[i + 2] & 0x3F];
    }
    if (i < len) {
    *p++ = basis_64[(string[i] >> 2) & 0x3F];
    if (i == (len - 1)) {
        *p++ = basis_64[((string[i] & 0x3) << 4)];
        *p++ = '=';
    }
    else {
        *p++ = basis_64[((string[i] & 0x3) << 4) |
                        ((int) (string[i + 1] & 0xF0) >> 4)];
        *p++ = basis_64[((string[i + 1] & 0xF) << 2)];
    }
    *p++ = '=';
    }

    *p++ = '\0';
    return p - encoded;
}




/*
SHA-1 in C
By Steve Reid <sreid@sea-to-sky.net>
100% Public Domain

-----------------
Modified 7/98
By James H. Brown <jbrown@burgoyne.com>
Still 100% Public Domain

Corrected a problem which generated improper hash values on 16 bit machines
Routine SHA1Update changed from
    void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned int
len)
to
    void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned
long len)

The 'len' parameter was declared an int which works fine on 32 bit machines.
However, on 16 bit machines an int is too small for the shifts being done
against
it.  This caused the hash function to generate incorrect values if len was
greater than 8191 (8K - 1) due to the 'len << 3' on line 3 of SHA1Update().

Since the file IO in main() reads 16K at a time, any file 8K or larger would
be guaranteed to generate the wrong hash (e.g. Test Vector #3, a million
"a"s).

I also changed the declaration of variables i & j in SHA1Update to
unsigned long from unsigned int for the same reason.

These changes should make no difference to any 32 bit implementations since
an
int and a long are the same size in those environments.

--
I also corrected a few compiler warnings generated by Borland C.
1. Added #include <process.h> for exit() prototype
2. Removed unused variable 'j' in SHA1Final
3. Changed exit(0) to return(0) at end of main.

ALL changes I made can be located by searching for comments containing 'JHB'
-----------------
Modified 8/98
By Steve Reid <sreid@sea-to-sky.net>
Still 100% public domain

1- Removed #include <process.h> and used return() instead of exit()
2- Fixed overwriting of finalcount in SHA1Final() (discovered by Chris Hall)
3- Changed email address from steve@edmweb.com to sreid@sea-to-sky.net

-----------------
Modified 4/01
By Saul Kravitz <Saul.Kravitz@celera.com>
Still 100% PD
Modified to run on Compaq Alpha hardware.

-----------------
Modified 07/2002
By Ralph Giles <giles@ghostscript.com>
Still 100% public domain
modified for use with stdint types, autoconf
code cleanup, removed attribution comments
switched SHA1Final() argument order for consistency
use SHA1_ prefix for public api
move public api to sha1.h
*/

/*
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/

/*#define WORDS_BIGENDIAN        on AIX only! */

#define SHA1HANDSOFF

static void SHA1_Transform2(uint32_t state[5], const uint8_t buffer[64]);

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
/* FIXME: can we do this in an endian-proof way? */
#ifdef WORDS_BIGENDIAN
#define blk0(i) block->l[i]
#else
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in libSHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);


static void libSHA1_Init(SHA1_CTX* context);
static void libSHA1_Update(SHA1_CTX* context, const uint8_t* data, const size_t len);
static void libSHA1_Final(SHA1_CTX* context, uint8_t digest[SHA1_DIGEST_SIZE]);



/* Hash a single 512-bit block. This is the core of the algorithm. */
static void SHA1_Transform2(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t a, b, c, d, e;
    typedef union {
        uint8_t c[64];
        uint32_t l[16];
    } CHAR64LONG16;
    CHAR64LONG16* block;

#ifdef SHA1HANDSOFF
    static uint8_t workspace[64];
    block = (CHAR64LONG16*)workspace;
    memcpy(block, buffer, 64);
#else
    block = (CHAR64LONG16*)buffer;
#endif

    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    /* Wipe variables */
    a = b = c = d = e = 0;
}


/* SHA1Init - Initialize new context */
static void libSHA1_Init(SHA1_CTX* context)
{
    /* libSHA1 initialization constants */
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}


/* Run your data through this. */
static void libSHA1_Update(SHA1_CTX* context, const uint8_t* data, const size_t len)
{
    size_t i, j;

    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
    context->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&context->buffer[j], data, (i = 64-j));
        SHA1_Transform2(context->state, context->buffer);
        for ( ; i + 63 < len; i += 64) {
            SHA1_Transform2(context->state, data + i);
        }
        j = 0;
    }
    else i = 0;
    memcpy(&context->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */
static void libSHA1_Final(SHA1_CTX* context, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint32_t i;
    uint8_t  finalcount[8];

    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)]
         >> ((3-(i & 3)) * 8) ) & 255);  /* Endian independent */
    }
    libSHA1_Update(context, (uint8_t *)"\200", 1);
    while ((context->count[0] & 504) != 448) {
        libSHA1_Update(context, (uint8_t *)"\0", 1);
    }
    libSHA1_Update(context, finalcount, 8);  /* Should cause a SHA1_Transform() */
    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        digest[i] = (uint8_t)
         ((context->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
    }

    /* Wipe variables */
    i = 0;
    memset(context->buffer, 0, 64);
    memset(context->state, 0, 20);
    memset(context->count, 0, 8);
    memset(finalcount, 0, 8);   /* SWR */

#ifdef SHA1HANDSOFF  /* make SHA1Transform overwrite its own static vars */
    SHA1_Transform2(context->state, context->buffer);
#endif
}


void libSHA1(unsigned char *ptr, unsigned int size, unsigned char *outbuf)
{
  SHA1_CTX ctx;

  libSHA1_Init(&ctx);
  libSHA1_Update(&ctx, ptr, size);
  libSHA1_Final(&ctx, outbuf);
}



void digest_to_hex(const uint8_t digest[SHA1_DIGEST_SIZE], char *output)
{
    int i,j;
    char *c = output;

    for (i = 0; i < SHA1_DIGEST_SIZE/4; i++) {
        for (j = 0; j < 4; j++) {
            sprintf(c,"%02X", digest[i*4+j]);
            c += 2;
        }
        sprintf(c, " ");
        c += 1;
    }
    *(c - 1) = '\0';
}

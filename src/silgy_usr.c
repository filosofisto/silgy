/* --------------------------------------------------------------------------
   Silgy Web App Engine
   Jurek Muszynski
   silgy.com
-----------------------------------------------------------------------------
   Logged in users' functions
-------------------------------------------------------------------------- */


#include "silgy.h"


static bool valid_username(const char *login);
static bool valid_email(const char *email);
static bool start_new_luses(int ci, long uid, const char *login, const char *email, const char *name, const char *about, const char *sesid);
static int user_exists(const char *login);
static int email_exists(const char *email);
static int do_login(int ci, long uid, char *p_login, char *p_email, char *p_name, char *p_about, long visits, const char *sesid);
static void doit(char *result1, char *result2, const char *usr, const char *email, const char *src);


/* --------------------------------------------------------------------------
  return TRUE if user name contains only valid characters
-------------------------------------------------------------------------- */
static bool valid_username(const char *login)
{
    int i;

    for ( i=0; login[i] != EOS; ++i )
    {
        if ( !isalnum(login[i]) && login[i] != '.' && login[i] != '_' && login[i] != '-' && login[i] != '\'' )
            return FALSE;
    }

    return TRUE;
}


/* --------------------------------------------------------------------------
  return TRUE if email has valid format
-------------------------------------------------------------------------- */
static bool valid_email(const char *email)
{
    int     len;
    char    *at;
    int     i;

    len = strlen(email);

    if ( len < 3 ) return FALSE;

    at = strchr(email, '@');

    if ( !at ) return FALSE;                /* no @ */
    if ( at==email ) return FALSE;          /* @ is first */
    if ( at==email+len-1 ) return FALSE;    /* @ is last */

    for ( i=0; i<len; ++i )
    {
        if ( !isalnum(email[i]) && email[i] != '@' && email[i] != '.' && email[i] != '_' && email[i] != '-' )
            return FALSE;
    }

    return TRUE;
}


/* --------------------------------------------------------------------------
   Start new logged in user session
-------------------------------------------------------------------------- */
static bool start_new_luses(int ci, long uid, const char *login, const char *email, const char *name, const char *about, const char *sesid)
{
    DBG("start_new_luses");

    if ( !conn[ci].usi && !eng_uses_start(ci) )    /* no anonymous session -- try to start one */
    {
        return FALSE;
    }

    DBG("Upgrading anonymous session to logged in, usi=%d, sesid [%s]", conn[ci].usi, sesid);
    strcpy(conn[ci].cookie_out_a, "x");     /* no longer needed */
    strcpy(conn[ci].cookie_out_a_exp, G_last_modified);     /* to be removed by browser */

    US.logged = TRUE;
    strcpy(US.sesid, sesid);
    strcpy(US.login, login);
    strcpy(US.email, email);
    strcpy(US.name, name);
    strcpy(US.about, about);
    strcpy(US.login_tmp, login);
    strcpy(US.email_tmp, email);
    strcpy(US.name_tmp, name);
    strcpy(US.about_tmp, about);
    US.uid = uid;

    return TRUE;
}


/* --------------------------------------------------------------------------
  verify IP & User-Agent against uid and sesid in uses (logged in users)
  set user session array index (usi) if all ok
-------------------------------------------------------------------------- */
int libusr_l_usession_ok(int ci)
{
    int         i;
    char        sanuagent[DB_UAGENT_LEN+1];
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;
    long        uid;
    time_t      created;

    DBG("libusr_l_usession_ok");

    /* try in hot sessions first */

    for ( i=1; i<=MAX_SESSIONS; ++i )
    {
        if ( uses[i].sesid[0] && uses[i].logged && 0==strcmp(conn[ci].cookie_in_l, uses[i].sesid)
/*              && 0==strcmp(conn[ci].ip, uses[i].ip) */
                && 0==strcmp(conn[ci].uagent, uses[i].uagent) )
        {
            DBG("Logged in session found in cache, usi=%d, sesid [%s]", i, uses[i].sesid);
            conn[ci].usi = i;
            return OK;
        }
    }

    /* not found in memory -- try database */

    strncpy(sanuagent, silgy_sql_esc(conn[ci].uagent), DB_UAGENT_LEN);
    sanuagent[DB_UAGENT_LEN] = EOS;
    if ( sanuagent[DB_UAGENT_LEN-1]=='\'' && sanuagent[DB_UAGENT_LEN-2]!='\'' )
        sanuagent[DB_UAGENT_LEN-1] = EOS;

    sprintf(sql_query, "SELECT user_id, created FROM users_logins WHERE sesid='%s' AND uagent='%s'", silgy_sql_esc(conn[ci].cookie_in_l), sanuagent);
    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users_logins: %lu record(s) found", sql_records);

    if ( 0 == sql_records )     /* no such session in database */
    {
        mysql_free_result(result);
        DBG("No logged in session in database [%s]", conn[ci].cookie_in_l);
        strcpy(conn[ci].cookie_out_l, "x");
        strcpy(conn[ci].cookie_out_l_exp, G_last_modified);     /* expire ls cookie */
        return ERR_SESSION_EXPIRED;
    }

    /* we've got some user login cookie remembered */

    sql_row = mysql_fetch_row(result);

    uid = atol(sql_row[0]);

    /* Verify time. If created more than 30 days ago -- refuse */

    created = db2epoch(sql_row[1]);

    if ( created < G_now - 3600*24*30 )
    {
        DBG("Closing old logged in session, usi=%d, sesid [%s], created %s", conn[ci].usi, conn[ci].cookie_in_l, sql_row[1]);

        mysql_free_result(result);

        sprintf(sql_query, "DELETE FROM users_logins WHERE sesid='%s' AND uagent='%s'", conn[ci].cookie_in_l, sanuagent);
        DBG("sql_query: %s", sql_query);

        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }

        /* tell browser we're logging out */

        strcpy(conn[ci].cookie_out_l, "x");
        strcpy(conn[ci].cookie_out_l_exp, G_last_modified);     /* expire ls cookie */

        return ERR_SESSION_EXPIRED;
    }

    mysql_free_result(result);

    /* cookie has not expired -- log user in */

    DBG("Logged in session found in database");

    sprintf(sql_query, "UPDATE users_logins SET last_used='%s' WHERE sesid='%s' AND uagent='%s'", G_dt, conn[ci].cookie_in_l, sanuagent);
    DBG("sql_query: %s", sql_query);
    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    return do_login(ci, uid, NULL, NULL, NULL, NULL, 0, conn[ci].cookie_in_l);
}


/* --------------------------------------------------------------------------
  close timeouted logged in user sessions
-------------------------------------------------------------------------- */
void libusr_close_luses_timeout()
{
    int     i;
    time_t  last_allowed;

    last_allowed = G_now - LUSES_TIMEOUT;

    for (i=1; i<=G_sessions; ++i)
    {
        if ( uses[i].logged && uses[i].last_activity < last_allowed )
            libusr_close_l_uses(-1, i);
    }
}


/* --------------------------------------------------------------------------
   Close  / downgrade logged in user session
   If ci != -1 it was on user demand
   otherwise it's just memory housekeeping
-------------------------------------------------------------------------- */
void libusr_close_l_uses(int ci, int usi)
{
    char    sql_query[MAX_SQL_QUERY_LEN+1];

    if ( ci != -1 )     /* explicit user logout -- downgrade to anonymous */
    {
        DBG("Downgrading logged in session to anonymous, usi=%d, sesid [%s]", usi, uses[usi].sesid);

        sprintf(sql_query, "DELETE FROM users_logins WHERE user_id=%ld", uses[usi].uid);
        DBG("sql_query: %s", sql_query);

        if ( mysql_query(G_dbconn, sql_query) )
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));

        strcpy(conn[ci].cookie_out_l, "x");
        strcpy(conn[ci].cookie_out_l_exp, G_last_modified);     /* in the past => to be removed by browser straight away */

        strcpy(conn[ci].cookie_out_a, uses[usi].sesid);

        uses[usi].logged = FALSE;
        uses[usi].uid = 0;
        uses[usi].login[0] = EOS;
        uses[usi].email[0] = EOS;
        uses[usi].name[0] = EOS;
        uses[usi].about[0] = EOS;
        uses[usi].login_tmp[0] = EOS;
        uses[usi].email_tmp[0] = EOS;
        uses[usi].name_tmp[0] = EOS;
        uses[usi].about_tmp[0] = EOS;
    }
    else    /* timeout'ed */
    {
        DBG("Closing logged in session, usi=%d, sesid [%s]", usi, uses[usi].sesid);
        eng_uses_close(usi);
    }
}


/* --------------------------------------------------------------------------
  check whether user exists in database
-------------------------------------------------------------------------- */
static int user_exists(const char *login)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    long        records;

    DBG("user_exists, login [%s]", login);

//  if ( 0==strcmp(sanlogin, "ADMIN") )
//      return ERR_USERNAME_TAKEN;

    sprintf(sql_query, "SELECT id FROM users WHERE UPPER(login)='%s'", upper(login));

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    records = mysql_num_rows(result);

    DBG("users: %ld record(s) found", records);

    mysql_free_result(result);

    if ( 0 != records )
        return ERR_USERNAME_TAKEN;

    return OK;
}


/* --------------------------------------------------------------------------
  check whether email exists in database
-------------------------------------------------------------------------- */
static int email_exists(const char *email)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    long        records;

    DBG("email_exists, email [%s]", email);

    sprintf(sql_query, "SELECT id FROM users WHERE UPPER(email)='%s'", upper(email));

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    records = mysql_num_rows(result);

    DBG("users: %ld record(s) found", records);

    mysql_free_result(result);

    if ( 0 != records )
        return ERR_EMAIL_TAKEN;

    return OK;
}


/* --------------------------------------------------------------------------
  log user in -- called either by l_usession_ok or libusr_do_login
  Authentication has already been done prior to calling this
-------------------------------------------------------------------------- */
static int do_login(int ci, long uid, char *p_login, char *p_email, char *p_name, char *p_about, long visits, const char *sesid)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;
    char        login[LOGIN_LEN+1];
    char        email[EMAIL_LEN+1];
    char        name[UNAME_LEN+1];
    char        about[256];

    DBG("do_login");

    /* get user record by id */

    if ( !p_login )  /* login from cookie */
    {
        sprintf(sql_query, "SELECT login,email,name,about,visits FROM users WHERE id=%ld", uid);
        DBG("sql_query: %s", sql_query);
        mysql_query(G_dbconn, sql_query);
        result = mysql_store_result(G_dbconn);
        if ( !result )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }

        sql_records = mysql_num_rows(result);

        DBG("users: %lu record(s) found", sql_records);

        if ( 0 == sql_records )
        {
            mysql_free_result(result);
            WAR("Cookie sesid does not match user id");
            return ERR_INVALID_LOGIN;   /* invalid user and/or password */
        }

        /* user found */

        sql_row = mysql_fetch_row(result);

        strcpy(login, sql_row[0]?sql_row[0]:"");
        strcpy(email, sql_row[1]?sql_row[1]:"");
        strcpy(name, sql_row[2]?sql_row[2]:"");
        strcpy(about, sql_row[3]?sql_row[3]:"");
        visits = atol(sql_row[4]);

        mysql_free_result(result);
    }
    else
    {
        strcpy(login, p_login);
        strcpy(email, p_email);
        strcpy(name, p_name);
        strcpy(about, p_about);
    }

    /* admin? */
#ifdef USERSBYEMAIL
    if ( 0==strcmp(email, APP_ADMIN_EMAIL) )
        strcpy(login, "admin");
#endif
    /* add record to uses */

    if ( !start_new_luses(ci, uid, login, email, name, about, sesid) )
        return ERR_SERVER_TOOBUSY;

    /* update user record */

    sprintf(sql_query, "UPDATE users SET visits=%ld, last_login='%s' WHERE id=%ld", visits+1, G_dt, uid);
    DBG("sql_query: %s", sql_query);
    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    /* init app user session */

    app_luses_init(ci);

#ifdef USERSBYEMAIL
    INF("User [%s] logged in", US.email);
#else
    INF("User [%s] logged in", US.login);
#endif

    return OK;
}



/* ------------------------------------------------------------------------------------------------------------
    Public user functions
------------------------------------------------------------------------------------------------------------ */

/* --------------------------------------------------------------------------
  log user in / explicit from Log In page
  return OK or
  ERR_INVALID_REQUEST
  ERR_INT_SERVER_ERROR
  ERR_INVALID_LOGIN
  and through do_login:
  ERR_SERVER_TOOBUSY
-------------------------------------------------------------------------- */
int libusr_do_login(int ci)
{
    QSVAL       login;
    QSVAL       email;
    char        name[UNAME_LEN+1];
    char        about[256];
    QSVAL       passwd;
    QSVAL       keep;
    char        ulogin[MAX_VALUE_LEN*2+1];
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    char        p1[32], p2[32];
    char        str1[32], str2[32];
    long        ula_cnt;
    char        ula_time[32];
    time_t      ula_time_epoch;
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;
    long        uid;
    char        sesid[SESID_LEN+1]="";
    long        new_ula_cnt;
    char        sanuagent[DB_UAGENT_LEN+1];
    time_t      sometimeahead;
    long        visits;
    char        deleted[4];

    DBG("libusr_do_login");

#ifdef USERSBYEMAIL

    if ( !QS_HTML_ESCAPE("email", email) || !QS_HTML_ESCAPE("passwd", passwd) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }
    stp_right(email);
    sprintf(sql_query, "SELECT id,login,email,name,passwd1,passwd2,about,ula_time,ula_cnt,visits,deleted FROM users WHERE UPPER(email)='%s'", upper(email));

#else    /* by login */

    if ( !QS_HTML_ESCAPE("login", login) || !QS_HTML_ESCAPE("passwd", passwd) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }
    stp_right(login);
    strcpy(ulogin, upper(login));
    sprintf(sql_query, "SELECT id,login,email,name,passwd1,passwd2,about,ula_time,ula_cnt,visits,deleted FROM users WHERE (UPPER(login)='%s' OR UPPER(email)='%s')", ulogin, ulogin);

#endif

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users: %lu record(s) found", sql_records);

    if ( 0 == sql_records )     /* no records */
    {
        mysql_free_result(result);
        return ERR_INVALID_LOGIN;   /* invalid user and/or password */
    }

    /* user name found */

    sql_row = mysql_fetch_row(result);

    uid = atol(sql_row[0]);
    strcpy(login, sql_row[1]?sql_row[1]:"");
    strcpy(email, sql_row[2]?sql_row[2]:"");
    strcpy(name, sql_row[3]?sql_row[3]:"");
    strcpy(p1, sql_row[4]);
    strcpy(p2, sql_row[5]);
    strcpy(about, sql_row[6]?sql_row[6]:"");
    strcpy(ula_time, sql_row[7]?sql_row[7]:"");
    ula_cnt = atol(sql_row[8]);
    visits = atol(sql_row[9]);
    strcpy(deleted, sql_row[10]?sql_row[10]:"N");

    mysql_free_result(result);

    if ( deleted[0]=='Y' )
    {
        WAR("User deleted");
        return ERR_INVALID_LOGIN;   /* invalid user and/or password */
    }

    /* check ULA (Unsuccessful Login Attempts) info to prevent brute-force password attacks */

    ula_time_epoch = db2epoch(ula_time);

    if ( (ula_cnt > 5 && ula_time_epoch > G_now-3600)           /* 3600 secs = 1 hour */
            || (ula_cnt == 5 && ula_time_epoch > G_now-600)     /* 600 secs = 10 mins */
            || (ula_cnt == 4 && ula_time_epoch > G_now-60) )    /* 60 secs = 1 min */
    {
        return WAR_ULA; /* wait before the next attempt */
    }

    /* now check username/email and password pairs as they should be */

#ifdef USERSBYEMAIL
    doit(str1, str2, email, email, passwd);
#else
    doit(str1, str2, login, email[0]?email:STR_005, passwd);
#endif

    /* are these as expected? */

    if ( 0 != strcmp(str1, p1) || (email[0] && 0 != strcmp(str2, p2)) ) /* passwd1, passwd2 */
    {
        DBG("Invalid password");
        new_ula_cnt = ula_cnt + 1;
        sprintf(sql_query, "UPDATE users SET ula_cnt=%ld, ula_time='%s' WHERE id=%ld", new_ula_cnt, G_dt, uid);
        DBG("sql_query: %s", sql_query);
        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }
        return ERR_INVALID_LOGIN;   /* invalid user and/or password */
    }

    /* successful login ------------------------------------------------------------ */

    if ( ula_cnt )  /* clear it */
    {
        DBG("Clearing ula_cnt");
        sprintf(sql_query, "UPDATE users SET ula_cnt=0 WHERE id=%ld", uid);
        DBG("sql_query: %s", sql_query);
        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }
    }

    /* generate sesid */

//  silgy_random(sesid, SESID_LEN);
//  DBG("Generated new sesid: [%s]", sesid);

    /* use anonymous sesid */

    strcpy(sesid, US.sesid);
    DBG("Using current sesid [%s]", sesid);

    /* save new session to users_logins and set the cookie */

    strncpy(sanuagent, silgy_sql_esc(conn[ci].uagent), DB_UAGENT_LEN);
    sanuagent[DB_UAGENT_LEN] = EOS;
    if ( sanuagent[DB_UAGENT_LEN-1]=='\'' && sanuagent[DB_UAGENT_LEN-2]!='\'' )
        sanuagent[DB_UAGENT_LEN-1] = EOS;

    sprintf(sql_query, "INSERT INTO users_logins (ip,uagent,sesid,user_id,created,last_used) VALUES ('%s','%s','%s',%ld,'%s','%s')", conn[ci].ip, sanuagent, sesid, uid, G_dt, G_dt);
    DBG("sql_query: %s", sql_query);
    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    /* set cookie */

    strcpy(conn[ci].cookie_out_l, sesid);

    /* Keep me logged in -- set cookie expiry date */

    if ( QS_HTML_ESCAPE("keep", keep) && 0==strcmp(keep, "on") )
    {
        DBG("keep is ON!");
        sometimeahead = G_now + 3600*24*30; /* 30 days */
        G_ptm = gmtime(&sometimeahead);
        strftime(conn[ci].cookie_out_l_exp, 32, "%a, %d %b %Y %T GMT", G_ptm);
//      DBG("conn[ci].cookie_out_l_exp: [%s]", conn[ci].cookie_out_l_exp);
        G_ptm = gmtime(&G_now); /* make sure G_ptm is always up to date */
    }

    /* finish logging user in */

    return do_login(ci, uid, login, email, name, about, visits, sesid);
}


/* --------------------------------------------------------------------------
   Create user account
   Return OK or:
   ERR_INVALID_REQUEST
   ERR_WEBSITE_FIRST_LETTER
   ERR_USERNAME_TOO_SHORT
   ERR_USER_NAME_CHARS
   ERR_USERNAME_TAKEN
   ERR_EMAIL_FORMAT_OR_EMPTY
   ERR_PASSWORD_TOO_SHORT
   ERR_PASSWORD_DIFFERENT
   ERR_INT_SERVER_ERROR
-------------------------------------------------------------------------- */
int libusr_do_create_acc(int ci)
{
    int     ret=OK;
    QSVAL   login;
    QSVAL   email;
    QSVAL   name;
    QSVAL   passwd;
    QSVAL   rpasswd;
    QSVAL   about;
    QSVAL   message;
    int     plen;
    char    sql_query[MAX_SQL_QUERY_LEN+1];
    char    str1[32], str2[32];

    DBG("libusr_do_create_acc");

    if ( QS_HTML_ESCAPE("login", login) )
    {
        stp_right(login);
        strcpy(US.login, login);
    }

#ifndef USERSBYEMAIL
    if ( !login[0] )
    {
        WAR("Invalid request (login missing)");
        return ERR_INVALID_REQUEST;
    }
#endif

    if ( QS_HTML_ESCAPE("email", email) )
    {
        stp_right(email);
        strcpy(US.email, email);
    }

#ifdef USERSBYEMAIL
    if ( !email[0] )
    {
        WAR("Invalid request (email missing)");
        return ERR_INVALID_REQUEST;
    }
#endif

    /* regardless of auth method */

    if ( !QS_HTML_ESCAPE("passwd", passwd)
            || !QS_HTML_ESCAPE("rpasswd", rpasswd) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

    /* optional */

    if ( QS_HTML_ESCAPE("name", name) )
    {
        stp_right(name);
        strcpy(US.name, name);
    }

    if ( QS_HTML_ESCAPE("about", about) )
    {
        stp_right(about);
        strcpy(US.about, about);
    }

    /* ----------------------------------------------------------------- */

    plen = strlen(passwd);

    if ( QS_HTML_ESCAPE("message", message) && message[0] )
        return ERR_ROBOT;

#ifdef USERSBYEMAIL
        if ( !email[0] )                            /* email empty */
            return ERR_EMAIL_EMPTY;
        else if ( !valid_email(email) )             /* invalid email format */
            return ERR_EMAIL_FORMAT;
#else
        if ( strlen(login) < MIN_USER_NAME_LEN )    /* user name too short */
            return ERR_USERNAME_TOO_SHORT;
        else if ( !valid_username(login) )          /* only certain chars are allowed in user name */
            return ERR_USER_NAME_CHARS;
        else if ( OK != (ret=user_exists(login)) )  /* user name taken */
            return ret;
        else if ( email[0] && !valid_email(email) )     /* invalid email format */
            return ERR_EMAIL_FORMAT_OR_EMPTY;
#endif

    if ( email[0] && OK != (ret=email_exists(email)) )  /* email in use */
        return ret;
    else if ( plen < MIN_PASSWD_LEN )               /* password too short */
        return ERR_PASSWORD_TOO_SHORT;
    else if ( 0 != strcmp(passwd, rpasswd) )        /* passwords differ */
        return ERR_PASSWORD_DIFFERENT;

    /* welcome! -- and generate password hashes ------------------------------------------------------- */

//    strcpy(login_san, silgy_sql_esc(login));
//    strcpy(email_san, silgy_sql_esc(email));
//    strcpy(name_san, silgy_sql_esc(name));

#ifdef USERSBYEMAIL
    doit(str1, str2, email, email, passwd);
#else
    doit(str1, str2, login, email[0]?email:STR_005, passwd);
#endif

    sprintf(sql_query, "INSERT INTO users (id,login,email,name,passwd1,passwd2,about,status,created,visits,settings,ula_cnt,deleted) VALUES (0,'%s','%s','%s','%s','%s','%s',0,'%s',0,0,0,'N')", login, email, name, str1, str2, about, G_dt);

    DBG("sql_query: INSERT INTO users (id,login,email,name,...) VALUES (0,'%s','%s','%s',...)", login, email, name);

    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    US.uid = mysql_insert_id(G_dbconn);

    return OK;

}


/* --------------------------------------------------------------------------
   Save user message
-------------------------------------------------------------------------- */
int libusr_do_contact(int ci)
{
static char message[MAX_LONG_URI_VAL_LEN+1];
static char sanmessage[MAX_LONG_URI_VAL_LEN+1];
    QSVAL   email;
static char sql_query[MAX_LONG_URI_VAL_LEN*2];

    DBG("libusr_do_contact");

    if ( !get_qs_param_long(ci, "msg_box", message) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

    if ( QS_HTML_ESCAPE("email", email) )
        stp_right(email);

    strcpy(sanmessage, silgy_html_esc(message));

    /* remember user details in case of error or warning to correct */

    strcpy(US.email_tmp, email);

    sprintf(sql_query, "INSERT INTO messages (user_id,msg_id,email,message,created) VALUES (%ld,%ld,'%s','%s','%s')", US.uid, libusr_get_max(ci, "messages")+1, email, sanmessage, G_dt);
    DBG("sql_query: INSERT INTO messages (user_id,msg_id,email,...) VALUES (%ld,libusr_get_max(),'%s',...)", US.uid, email);

    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    /* send an email to admin */

    sendemail(ci, APP_CONTACT_EMAIL, "New message!", message);

    return OK;
}


/* --------------------------------------------------------------------------
  save changes to user account
-------------------------------------------------------------------------- */
int libusr_do_save_myacc(int ci)
{
    int         ret=OK;
    QSVAL       login;
    QSVAL       email;
    QSVAL       name;
    QSVAL       passwd;
    QSVAL       rpasswd;
    QSVAL       about;
    QSVAL       opasswd;
    QSVAL       uemail_old;
    QSVAL       uemail_new;
    QSVAL       strdelete;
    QSVAL       strdelconf;
    QSVAL       save;
    int         plen;
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    char        str1[32], str2[32];
    MYSQL_RES   *result;
unsigned long   sql_records;

    DBG("libusr_do_save_myacc");

    if ( !QS_HTML_ESCAPE("opasswd", opasswd)
#ifndef USERSBYEMAIL
            || !QS_HTML_ESCAPE("login", login)
#endif
            || !QS_HTML_ESCAPE("email", email)
            || !QS_HTML_ESCAPE("passwd", passwd)
            || !QS_HTML_ESCAPE("rpasswd", rpasswd) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

#ifdef USERSBYEMAIL
    if ( QS_HTML_ESCAPE("login", login) )     /* try to get login anyway */
        stp_right(login);
#endif

    stp_right(email);   /* always present but can be empty */

    if ( QS_HTML_ESCAPE("name", name) ) /* optional */
        stp_right(name);

    if ( QS_HTML_ESCAPE("about", about) ) /* optional */
        stp_right(about);

    /* remember form fields */
    /* US.email contains old email */

    strcpy(US.login_tmp, login);
    strcpy(US.email_tmp, email);
    strcpy(US.name_tmp, name);
    strcpy(US.about_tmp, about);

    DBG("login_tmp: [%s]", US.login_tmp);
    DBG("email_tmp: [%s]", US.email_tmp);
    DBG("name_tmp: [%s]", US.name_tmp);
    DBG("about_tmp: [%s]", US.about_tmp);

    /* basic validation */

    plen = strlen(passwd);

#ifdef USERSBYEMAIL
    if ( !email[0] )
        return ERR_EMAIL_EMPTY;
    else if ( !valid_email(email) )
        return ERR_EMAIL_FORMAT;
#else
    if ( email[0] && !valid_email(email) )
        return ERR_EMAIL_FORMAT_OR_EMPTY;
#endif
    else if ( plen && plen < MIN_PASSWD_LEN )
        return ERR_PASSWORD_TOO_SHORT;
    else if ( plen && 0 != strcmp(passwd, rpasswd) )
        return ERR_PASSWORD_DIFFERENT;

    /* if email change, check if the new one has not already been registered */

    strcpy(uemail_old, upper(US.email));
    strcpy(uemail_new, upper(email));

    if ( uemail_new[0] && strcmp(uemail_old, uemail_new) != 0 && (ret=libusr_email_exists(ci)) != OK )
        return ret;

    /* verify existing password against login/email/passwd1 */

#ifdef USERSBYEMAIL
    doit(str1, str2, email, email, opasswd);
    sprintf(sql_query, "SELECT id FROM users WHERE UPPER(email)='%s' AND passwd1='%s'", upper(email), str1);
    DBG("sql_query: SELECT id FROM users WHERE UPPER(email)='%s' AND passwd1=...", upper(email));
#else
    doit(str1, str2, login, login, opasswd);
    sprintf(sql_query, "SELECT id FROM users WHERE UPPER(login)='%s' AND passwd1='%s'", upper(login), str1);
    DBG("sql_query: SELECT id FROM users WHERE UPPER(login)='%s' AND passwd1=...", upper(login));
#endif

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    mysql_free_result(result);

    if ( 0 == sql_records )
    {
        ERR("Invalid old password");
        return ERR_OLD_PASSWORD;
    }

    /* Old password OK ---------------------------------------- */

    DBG("Old password OK");

    if ( QS_HTML_ESCAPE("delete", strdelete) && 0==strcmp(strdelete, "on") )    /* delete user account */
    {
        if ( !QS_HTML_ESCAPE("delconf", strdelconf) || 0 != strcmp(strdelconf, "1") )
            return WAR_BEFORE_DELETE;
        else
        {
            sprintf(sql_query, "UPDATE users SET deleted='Y' WHERE id=%ld", US.uid);
            DBG("sql_query: %s", sql_query);
            if ( mysql_query(G_dbconn, sql_query) )
            {
                ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
                return ERR_INT_SERVER_ERROR;
            }

            libusr_close_l_uses(ci, conn[ci].usi);  /* log user out */

            return MSG_ACCOUNT_DELETED;
        }
    }

    /* anything else than deleting account -- changin email and/or name and/or password */

#ifdef USERSBYEMAIL
    doit(str1, str2, email, email, plen?passwd:opasswd);
#else
    doit(str1, str2, login, email[0]?email:STR_005, plen?passwd:opasswd);
#endif

    sprintf(sql_query, "UPDATE users SET login='%s', email='%s', name='%s', passwd1='%s', passwd2='%s', about='%s' WHERE id=%ld", login, email, name, str1, str2, about, US.uid);
    DBG("sql_query: UPDATE users SET login='%s', email='%s', name='%s',... WHERE id=%ld", login, email, name, US.uid);

    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    DBG("Updating login, email & name in user session");

    strcpy(US.login, US.login_tmp);
    strcpy(US.email, US.email_tmp);
    strcpy(US.name, US.name_tmp);
    strcpy(US.about, US.about_tmp);

    return OK;
}


/* --------------------------------------------------------------------------
  email taken?
-------------------------------------------------------------------------- */
int libusr_email_exists(int ci)
{
    QSVAL   email;

    DBG("libusr_email_exists");

    if ( !QS_HTML_ESCAPE("email", email) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

    stp_right(email);

    return email_exists(email);
}


/* --------------------------------------------------------------------------
  send an email with password reset link
-------------------------------------------------------------------------- */
int libusr_do_send_passwd_reset_email(int ci)
{
    QSVAL       email;
    QSVAL       submit;
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;
    long        uid;
    char        login_name[LOGIN_LEN+1];
    char        linkkey[32];
    char        subject[64];
    char        message[1024];

    DBG("libusr_do_forgot");

    if ( !QS_HTML_ESCAPE("email", email) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

    stp_right(email);

    if ( !valid_email(email) )      /* invalid email format */
        return ERR_EMAIL_FORMAT;

#ifdef USERSBYEMAIL
    sprintf(sql_query, "SELECT id, name FROM users WHERE UPPER(email)='%s'", upper(email));
#else
    sprintf(sql_query, "SELECT id, login FROM users WHERE UPPER(email)='%s'", upper(email));
#endif

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users: %lu record(s) found", sql_records);

    if ( sql_records )
    {
        sql_row = mysql_fetch_row(result);

        uid = atol(sql_row[0]);     /* user id */
        strcpy(login_name, sql_row[1]);

        mysql_free_result(result);

        /* generate a key */

        silgy_random(linkkey, PASSWD_RESET_KEY_LEN);

        sprintf(sql_query, "INSERT INTO users_p_resets (user_id,linkkey,created) VALUES (%ld,'%s','%s')", uid, linkkey, G_dt);
        DBG("sql_query: %s", sql_query);

        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }

        /* send an email */

        sprintf(subject, "%s Password Reset", conn[ci].website);

        sprintf(message, "Dear %s,\n\nYou have requested to have your password reset for your account at %s. Please visit this URL to reset your password:\n\n", login_name, conn[ci].host);
        sprintf(message, "%s%s://%s/preset?k=%s\n\n", message, PROTOCOL, conn[ci].host, linkkey);
        sprintf(message, "%sPlease keep in mind that this link will only be valid for the next 24 hours.\n\n", message);
        sprintf(message, "%sIf you did this by mistake or it wasn't you, you can safely ignore this email.\n\n", message);
        sprintf(message, "%sIn case you needed any help, please contact us at %s.\n\n", message, APP_CONTACT_EMAIL);
        sprintf(message, "%sKind Regards\n%s\n", message, conn[ci].website);

        if ( !sendemail(ci, email, subject, message) )
            return ERR_INT_SERVER_ERROR;
    }
    else
    {
        mysql_free_result(result);
    }

    return OK;
}


/* --------------------------------------------------------------------------
  save new password after reset
-------------------------------------------------------------------------- */
int libusr_do_passwd_reset(int ci)
{
    int         ret;
    QSVAL       linkkey;
    QSVAL       email;
    QSVAL       passwd;
    QSVAL       rpasswd;
    QSVAL       submit;
    int         plen;
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    char        str1[32], str2[32];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;

    DBG("libusr_do_passwd_reset");

    strcpy(linkkey, US.additional); /* from here instead of URI */

    if ( !QS_HTML_ESCAPE("email", email)
            || !QS_HTML_ESCAPE("passwd", passwd)
            || !QS_HTML_ESCAPE("rpasswd", rpasswd) )
    {
        WAR("Invalid request (URI val missing?)");
        return ERR_INVALID_REQUEST;
    }

    stp_right(email);

    plen = strlen(passwd);

    /* remember form fields */

    strcpy(US.email, email);

    /* general validation */

    if ( !valid_email(email) )
        return ERR_EMAIL_FORMAT;
    else if ( plen < MIN_PASSWD_LEN )       /* password too short */
        return ERR_PASSWORD_TOO_SHORT;
    else if ( 0 != strcmp(passwd, rpasswd) )    /* passwords differ */
        return ERR_PASSWORD_DIFFERENT;

    /* verify the email-key pair */

#ifdef USERSBYEMAIL
    sprintf(sql_query, "SELECT name, email FROM users WHERE id=%ld", US.uid);
#else
    sprintf(sql_query, "SELECT login, email FROM users WHERE id=%ld", US.uid);
#endif

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users: %lu record(s) found", sql_records);

    if ( 0 == sql_records )     /* password reset link expired or invalid email */
    {
        mysql_free_result(result);
        return ERR_LINK_EXPIRED;
    }

    sql_row = mysql_fetch_row(result);

    if ( 0 != strcmp(sql_row[1], email) )   /* emails different */
    {
        mysql_free_result(result);
        return ERR_LINK_EXPIRED;    /* password reset link expired or invalid email */
    }


    /* everything's OK -- update password -------------------------------- */

#ifdef USERSBYEMAIL
    doit(str1, str2, email, email, passwd);
#else
    doit(str1, str2, sql_row[0], email, passwd);
#endif

    mysql_free_result(result);

    sprintf(sql_query, "UPDATE users SET passwd1='%s', passwd2='%s' WHERE id=%ld", str1, str2, US.uid);
// !!!!!!   DBG("sql_query: %s", sql_query);
    if ( mysql_query(G_dbconn, sql_query) )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    return OK;
}


/* --------------------------------------------------------------------------
  verify the link key for password reset
-------------------------------------------------------------------------- */
int libusr_valid_linkkey(int ci, char *linkkey, long *uid)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;

    DBG("libusr_valid_linkkey");

    if ( strlen(linkkey) != PASSWD_RESET_KEY_LEN )
        return ERR_LINK_BROKEN;

    sprintf(sql_query, "SELECT user_id, created FROM users_p_resets WHERE linkkey='%s'", silgy_sql_esc(linkkey));
    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users_p_resets: %lu row(s) found", sql_records);

    if ( !sql_records )     /* no records with this key in users_p_resets -- link broken? */
    {
        mysql_free_result(result);
        return ERR_LINK_MAY_BE_EXPIRED;
    }

    sql_row = mysql_fetch_row(result);

    /* validate expiry time */

    if ( db2epoch(sql_row[1]) < G_now-3600*24 ) /* older than 24 hours? */
    {
        DBG("Key created more than 24 hours ago");
        mysql_free_result(result);
        return ERR_LINK_MAY_BE_EXPIRED;
    }

    /* otherwise everything's OK */

    *uid = atol(sql_row[0]);

    mysql_free_result(result);

    DBG("Key ok, uid = %ld", *uid);

    return OK;
}


/* --------------------------------------------------------------------------
  log user out
-------------------------------------------------------------------------- */
void libusr_log_out(int ci)
{
    DBG("libusr_log_out");
    libusr_close_l_uses(ci, conn[ci].usi);
}


/* --------------------------------------------------------------------------
   Generate password hashes
-------------------------------------------------------------------------- */
static void doit(char *result1, char *result2, const char *login, const char *email, const char *src)
{
    char    tmp[512];
    unsigned char digest[SHA1_DIGEST_SIZE];
    int     i, j=0;

    sprintf(tmp, "%s%s%s%s", STR_001, upper(login), STR_002, src); /* login */
    libSHA1((unsigned char*)tmp, strlen(tmp), digest);
    Base64encode(tmp, (char*)digest, SHA1_DIGEST_SIZE);
    for ( i=0; tmp[i] != EOS; ++i ) /* drop non-alphanumeric characters */
    {
        if ( isalnum(tmp[i]) )
            result1[j++] = tmp[i];
    }
    result1[j] = EOS;

    j = 0;

    sprintf(tmp, "%s%s%s%s", STR_003, upper(email), STR_004, src); /* email */
    libSHA1((unsigned char*)tmp, strlen(tmp), digest);
    Base64encode(tmp, (char*)digest, SHA1_DIGEST_SIZE);
    for ( i=0; tmp[i] != EOS; ++i ) /* drop non-alphanumeric characters */
    {
        if ( isalnum(tmp[i]) )
            result2[j++] = tmp[i];
    }
    result2[j] = EOS;
}


/* --------------------------------------------------------------------------
   Save user string setting
-------------------------------------------------------------------------- */
int libusr_sets(int ci, const char *us_key, const char *us_val)
{
    int         ret=OK;
    char        sql_query[MAX_SQL_QUERY_LEN+1];

    ret = libusr_gets(ci, us_key, NULL);

    if ( ret == ERR_NOT_FOUND )
    {
        sprintf(sql_query, "INSERT INTO users_settings (user_id,us_key,us_val) VALUES (%ld,'%s','%s')", US.uid, us_key, us_val);

        DBG("sql_query: %s", sql_query);

        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }
    }
    else if ( ret != OK )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }
    else
    {
        sprintf(sql_query, "UPDATE users_settings SET us_val='%s' WHERE user_id=%ld AND us_key='%s'", us_val, US.uid, us_key);

        DBG("sql_query: %s", sql_query);

        if ( mysql_query(G_dbconn, sql_query) )
        {
            ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
            return ERR_INT_SERVER_ERROR;
        }
    }

    return OK;
}


/* --------------------------------------------------------------------------
   Read user string setting
-------------------------------------------------------------------------- */
int libusr_gets(int ci, const char *us_key, char *us_val)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
unsigned long   sql_records;

    sprintf(sql_query, "SELECT us_val FROM users_settings WHERE user_id=%ld AND us_key='%s'", US.uid, us_key);

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_records = mysql_num_rows(result);

    DBG("users_settings: %lu record(s) found", sql_records);

    if ( 0 == sql_records )
    {
        mysql_free_result(result);
        return ERR_NOT_FOUND;
    }

    sql_row = mysql_fetch_row(result);

    if ( us_val )
        strcpy(us_val, sql_row[0]);

    mysql_free_result(result);

    return OK;
}


/* --------------------------------------------------------------------------
   Save user number setting
-------------------------------------------------------------------------- */
int libusr_setn(int ci, const char *us_key, long us_val)
{
    char    val[32];

    sprintf(val, "%ld", us_val);
    return libusr_sets(ci, us_key, val);
}


/* --------------------------------------------------------------------------
   Read user number setting
-------------------------------------------------------------------------- */
int libusr_getn(int ci, const char *us_key, long *us_val)
{
    int     ret;
    char    val[32];

    if ( (ret=libusr_gets(ci, us_key, val)) == OK )
        *us_val = atol(val);

    return ret;
}


/* --------------------------------------------------------------------------
   Get MAX id
-------------------------------------------------------------------------- */
long libusr_get_max(int ci, const char *table)
{
    char        sql_query[MAX_SQL_QUERY_LEN+1];
    MYSQL_RES   *result;
    MYSQL_ROW   sql_row;
    long        max=0;

    /* US.uid = 0 for anonymous session */

    if ( 0==strcmp(table, "messages") )
        sprintf(sql_query, "SELECT MAX(msg_id) FROM messages WHERE user_id=%ld", US.uid);
    else
        return 0;

    DBG("sql_query: %s", sql_query);

    mysql_query(G_dbconn, sql_query);

    result = mysql_store_result(G_dbconn);

    if ( !result )
    {
        ERR("Error %u: %s", mysql_errno(G_dbconn), mysql_error(G_dbconn));
        return ERR_INT_SERVER_ERROR;
    }

    sql_row = mysql_fetch_row(result);

    if ( sql_row[0] != NULL )
        max = atol(sql_row[0]);

    mysql_free_result(result);

    DBG("libusr_get_max for uid=%ld  max = %ld", US.uid, max);

    return max;
}


/* --------------------------------------------------------------------------
  get error description for user
  called from eng_get_msg_str()
-------------------------------------------------------------------------- */
void libusr_get_msg_str(int ci, char *dest, int errcode)
{
    if ( errcode == ERR_INVALID_LOGIN )
        strcpy(dest, "Invalid login and/or password");
    else if ( errcode == MSG_WELCOME )
        sprintf(dest, "Welcome to %s! You can now log in:", conn[ci].website);
    else if ( errcode == MSG_USER_LOGGED_OUT )
        strcpy(dest, "You've been successfully logged out.");
    else if ( errcode == ERR_INVALID_PASSWD )
        sprintf(dest, "Please enter your existing password");
    else if ( errcode == WAR_NO_EMAIL )
        strcpy(dest, "You didn't provide your email address. This is fine, however please remember that in case you forget your password, there's no way for us to send you reset link.");
    else if ( errcode == ERR_SESSION_EXPIRED )
        strcpy(dest, "Your session has expired. Please log in to continue:");
    else if ( errcode == MSG_CHANGES_SAVED )
        strcpy(dest, "Your changes have been saved.");
    else if ( errcode == ERR_USERNAME_TOO_SHORT )
        sprintf(dest, "User name must be at least %d characters long", MIN_USER_NAME_LEN);
    else if ( errcode == ERR_USER_NAME_CHARS )
        sprintf(dest, "User name may only contain letters, digits, dots, hyphens, underscores or apostrophes");
    else if ( errcode == ERR_USERNAME_TAKEN )
        strcpy(dest, "Unfortunately this login has already been taken!");
    else if ( errcode == ERR_EMAIL_FORMAT_OR_EMPTY )
        strcpy(dest, "Please enter valid email address or leave this field empty");
    else if ( errcode == ERR_PASSWORD_TOO_SHORT )
        sprintf(dest, "Password must be at least %d characters long", MIN_PASSWD_LEN);
    else if ( errcode == ERR_PASSWORD_DIFFERENT )
        strcpy(dest, "Please retype password exactly like in the previous field");
    else if ( errcode == ERR_OLD_PASSWORD )
        strcpy(dest, "Please enter your existing password");
    else if ( errcode == ERR_ROBOT )
        strcpy(dest, "I'm afraid you are a robot?");
    else if ( errcode == ERR_WEBSITE_FIRST_LETTER )
        sprintf(dest, "The first letter of this website's name should be %c", conn[ci].website[0]);
    else if ( errcode == ERR_EMAIL_EMPTY )
        strcpy(dest, "Your email address can't be empty");
    else if ( errcode == ERR_EMAIL_FORMAT )
        strcpy(dest, "Please enter valid email address");
    else if ( errcode == ERR_EMAIL_TAKEN )
        strcpy(dest, "This email address has already been registered");
    else if ( errcode == MSG_REQUEST_SENT )
        sprintf(dest, "Your request has been sent. Please check your mailbox for a message from %s.", conn[ci].website);
    else if ( errcode == ERR_LINK_BROKEN )
        strcpy(dest, "It looks like this password-reset link is broken. If you clicked on the link you've received from us in email, you can try to copy and paste it in your browser's address instead.");
    else if ( errcode == ERR_LINK_MAY_BE_EXPIRED )
        strcpy(dest, "Your password-reset link is invalid or may be expired. In this case you can <a href=\"forgot\">request password reset link again</a>.");
    else if ( errcode == ERR_LINK_EXPIRED )
        strcpy(dest, "It looks like you entered email that doesn't exist in our database or your password-reset link has expired. In this case you can <a href=\"forgot\">request password reset link again</a>.");
    else if ( errcode == MSG_PASSWORD_CHANGED )
        strcpy(dest, "Your password has been changed. You can now log in:");
    else if ( errcode == MSG_MESSAGE_SENT )
        strcpy(dest, "Your message has been sent.");
    else if ( errcode == WAR_ULA )
        strcpy(dest, "Someone tried to log in to this account unsuccessfully more than 3 times. To protect your account from brute-force attack, this system requires some wait: 1 minute, then 10 minutes, then 1 hour before trying again.");
    else if ( errcode == WAR_BEFORE_DELETE )
        sprintf(dest, "You are about to delete your %s's account. All your details and plans will be removed from our database. If you are sure you want this, enter your password and click 'Delete my account'.", conn[ci].website);
    else if ( errcode == MSG_ACCOUNT_DELETED )
        sprintf(dest, "Your user account has been deleted. Thank you for trying %s!", conn[ci].website);
    else
        sprintf(dest, "Unknown error (%d)", errcode);
}

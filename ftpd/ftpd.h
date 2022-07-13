/*
 *  FTP Server Information
 */

#ifndef _LWIP_FTPD_H
#define _LWIP_FTPD_H

#ifdef __cplusplus
extern "C" {
#endif

#define FTPD_CONTROL_PORT   21

/*
 * Various buffer sizes
 */
enum
{
    FTPD_BUFSIZE  = 256,                        /* Size for temporary buffers */
    FTPD_DATASIZE = 4*1024,                     /* Size for file transfer buffers */
    FTPD_STACKSIZE = (8*1024) + FTPD_DATASIZE   /* Tasks stack size */
};

/* FTPD access control flags */
enum
{
    FTPD_NO_WRITE = 0x1,
    FTPD_NO_READ  = 0x2,
    FTPD_NO_RW    = FTPD_NO_WRITE | FTPD_NO_READ
};

typedef int (*ftpd_hook_func)(char *, size_t);

typedef int (*ftpd_login_func)(const char *user, char *password);

struct ftpd_hook
{
    char           *filename;
    ftpd_hook_func hook_function;
};

struct ftpd_configuration
{
    int               priority;             /* FTPD task priority  */
    unsigned long     max_hook_filesize;    /* Maximum buffersize for hooks */
    int               port;                 /* Well-known port     */
    struct ftpd_hook *hooks;                /* List of hooks       */
    char const       *root;                 /* Root for FTPD or 0 for / */
    int              tasks_count;           /* Max. connections    */
    int              idle;                  /* Idle timeout in seoconds or 0 for no (inf) timeout */
    int              access;                /* 0 - r/w, 1 - read-only, 2 - write-only, 3 - browse-only */
    ftpd_login_func  login;                 /* Login check or 0 to ignore user/passwd. */
};

/*
 * Reply codes.
 */
#define PRELIM          1       /* positive preliminary */
#define COMPLETE        2       /* positive completion */
#define CONTINUE        3       /* positive intermediate */
#define TRANSIENT       4       /* transient negative completion */
#define ERROR           5       /* permanent negative completion */

/*
 * Type codes
 */
#define	TYPE_A		1	/* ASCII */
#define	TYPE_E		2	/* EBCDIC */
#define	TYPE_I		3	/* image */
#define	TYPE_L		4	/* local byte size */

//-------------------------------------------------------------------------------------------------

int lwip_initialize_ftpd(void);

#ifdef __cplusplus
}
#endif

#endif  /* _LWIP_FTPD_H */



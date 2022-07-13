/*
 * start_ftp_server.c
 */

#include "bsp.h"

#ifdef USE_FTPD

#include "ftpd.h"

#include "ls1x_nand.h"
#include "../yaffs2/port/ls1x_yaffs.h"

extern void ls1x_initialize_lwip(unsigned char *ip0, unsigned char *ip1);

//-------------------------------------------------------------------------------------------------
// start ftpd server
//-------------------------------------------------------------------------------------------------

struct ftpd_configuration _ftpd_configuration =
{
#if defined(OS_RTTHREAD)
    .priority          = 12,            /* FTPD task priority */
#elif defined(OS_UCOS)
    .priority          = 82,
#elif defined(OS_FREERTOS)
    .priority          = 15,
#else
#error "Not support bare programming now."
#endif
    .max_hook_filesize = 0x1000,                /* Maximum buffersize for hooks */
    .port              = 21,                    /* Well-known port */
    .hooks             = NULL,                  /* List of hooks */
    .root              = RYFS_MOUNTED_FS_NAME,  /* Root for FTPD or 0 for / */
    .tasks_count       = 0,                     /* Max. connections */
    .idle              = 0,                     /* Idle timeout in seoconds or 0 for no (inf) timeout */
    .access            = 0,                     /* 0 - r/w, 1 - read-only, 2 - write-only, 3 - browse-only */
    .login             = NULL,                  /* Login check or 0 to ignore user/passwd. */
};

static int is_ftpd_started = 0;

int start_ftpd_server(void)
{
#ifdef USE_FTPD
    if (is_ftpd_started)
        return 0;

    #if defined(BSP_USE_NAND)
        ls1x_nand_init(devNAND, NULL);
    #endif

    #ifdef USE_YAFFS2
        yaffs_startup_and_mount(RYFS_MOUNTED_FS_NAME);
    #endif

	#ifdef BSP_USE_GMAC0
        ls1x_initialize_lwip(NULL, NULL);
    #endif

    if (lwip_initialize_ftpd() == 0)
    {
        is_ftpd_started = 1;
        return 0;
    }
#endif

    return -1;
}

#endif // #ifdef USE_FTPD


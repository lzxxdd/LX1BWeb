/*
 *  FTP Server Daemon
 */

#include "bsp.h"

#ifdef USE_FTPD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

#include <time.h>
#include <sys/stat.h>

#include "ls1x_nand.h"

#include "../yaffs2/port/ls1x_yaffs.h"
#include "../yaffs2/direct/yportenv.h"
#include "../yaffs2/direct/yaffsfs.h"

#if defined(OS_RTTHREAD)
#include "rtthread.h"
#elif defined(OS_UCOS)
#include "os.h"
#define FTPD_MUTEX_PRIO     81
#elif defined(OS_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#else
#error "Not support bare programming now!"
#endif

#include "lwip/tcp.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/debug.h"

#include "ftpd.h"

//-------------------------------------------------------------------------------------

#if 0
#define FTPD_DBG(...)       printk(__VA_ARGS__)
#else
#define FTPD_DBG(...)
#endif

#define PRINTF              printk

#define FTPD_SERVER_MESSAGE "LWIP FTP server (Version 1.1-JWJ) ready."

#define FTPD_SYSTYPE        "UNIX Type: L8"

/*
 * Event to be used by session tasks for waiting
 */
#define FTPD_CLIENT_EVENT   0x1000

//-------------------------------------------------------------------------------------

/*
 * sConfiguration table
 */
extern struct ftpd_configuration _ftpd_configuration;

/*
 * SessionInfo structure.
 *
 * The following structure is allocated for each session.
 */
typedef struct
{
    struct sockaddr_in  ctrl_addr;          /* Control connection self address */
    struct sockaddr_in  data_addr;          /* Data address set by PORT command */
    struct sockaddr_in  def_addr;           /* Default address for data */
    int                 use_default;        /* 1 - use default address for data */
    int                 ctrl_socket;        /* Socket for ctrl connection */
    int                 pasv_socket;        /* Socket for PASV connection */
    int                 data_socket;        /* Socket for data connection */
    int                 idle;               /* Timeout in seconds */
    int                 xfer_mode;          /* Transfer mode (ASCII/binary) */
    
#if defined(OS_RTTHREAD)
    rt_thread_t         tid;
    rt_event_t          event;
#elif defined(OS_UCOS)
    unsigned char       tid;                /* UCOS task priority */
    OS_FLAG_GRP        *event;
    OS_STK             *stack;              /* UCOS task stack align 4 */
#elif defined(OS_FREERTOS)
    TaskHandle_t        tid;
    EventGroupHandle_t  event;
#endif

    char                *user;              /* user name (0 if not supplied) */
    char                *pass;              /* password (0 if not supplied) */
    bool                auth;               /* true if user/pass was valid, false if not or not supplied */
    
    char                saved_RNFR[256];    /* 保存待改名的文件名 */
    char                fs_cur_dir[256];    /* 保存文件系统当前路径 */

} FTPD_SessionInfo_t;

/*
 * TaskPool structure.
 */
typedef struct
{
    FTPD_SessionInfo_t *info;
    FTPD_SessionInfo_t **queue;
    int                 count;
    int                 head;
    int                 tail;
#if defined(OS_RTTHREAD)
    rt_mutex_t          mutex;
    rt_sem_t            sem;
#elif defined(OS_UCOS)
    OS_EVENT           *mutex;
    OS_EVENT           *sem;
#elif defined(OS_FREERTOS)
    SemaphoreHandle_t   mutex;
    SemaphoreHandle_t   sem;
#endif

} FTPD_TaskPool_t;

//-------------------------------------------------------------------------------------

/*
 * Task pool instance.
 */
static FTPD_TaskPool_t task_pool;

/*
 * Root directory
 */
static char const* ftpd_root = "/";

/*
 * Default idle timeout for sockets in seconds.
 */
static int ftpd_timeout = 0;

/*
 * Global access flags.
 */
static int ftpd_access = 0;

//-------------------------------------------------------------------------------------
// glue yaffs2 functions
//-------------------------------------------------------------------------------------

#define yaffs2_read         yaffs_read
#define yaffs2_write        yaffs_write
#define yaffs2_fstat        yaffs_fstat
#define yaffs2_close        yaffs_close
#define yaffs2_readdir      yaffs_readdir
#define yaffs2_closedir     yaffs_closedir

/*
 * 文件名处理后再调用
 */
static int yaffs2_real_path(FTPD_SessionInfo_t *info, char *pbuf, const char *fname)
{
    if ((pbuf == NULL) || (fname == NULL))
        return -1;

    strcpy(pbuf, ftpd_root);                    /* 根目录 */
    
    strncat(pbuf, info->fs_cur_dir, 254);       /* 当前目录 */

    if (fname[0] == '/')
    {
        if (fname[1] != '\0')
            strncat(pbuf, fname, 254);          /* 目录名 */
        return 0;
    }

    if (fname[0] == '.')
    {
        if (fname[1] == '/')
            strncat(pbuf, fname + 2, 254);      /* 当前目录下的目录名或者文件名 */
        return 0;
    }

    strncat(pbuf, fname, 254);                  /* 直接加上? */

    return 0;
}

//-------------------------------------------------------------------------------------

static int yaffs2_open(FTPD_SessionInfo_t *info, const char *filename, int oflag, int mode)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, filename);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, filename, fn_buf);
    
    return yaffs_open(fn_buf, oflag, mode);
}

static int yaffs2_chmod(FTPD_SessionInfo_t *info, const char *filename, mode_t mode)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, filename);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, filename, fn_buf);

    return yaffs_chmod(fn_buf, mode);
}

static int yaffs2_lstat(FTPD_SessionInfo_t *info, const char *f_name, struct yaffs_stat *st)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, f_name);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, f_name, fn_buf);
    
    return yaffs_lstat(fn_buf, st);
}

static int yaffs2_delete(FTPD_SessionInfo_t *info, const char *filename)
{
    char fn_buf[256];
    struct yaffs_stat st;
    
    yaffs2_real_path(info, fn_buf, filename);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, filename, fn_buf);
    
    yaffs_lstat(fn_buf, &st);
    if (S_ISDIR(st.st_mode))
        return yaffs_rmdir(fn_buf);
    else
        return yaffs_unlink(fn_buf);
}

static int yaffs2_mkdir(FTPD_SessionInfo_t *info, const char *dirname, mode_t mode)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, dirname);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, dirname, fn_buf);
    
    return yaffs_mkdir(fn_buf, mode);
}

static int yaffs2_rmdir(FTPD_SessionInfo_t *info, const char *dirname)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, dirname);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, dirname, fn_buf);
    
    return yaffs_rmdir(fn_buf);
}

static yaffs_DIR *yaffs2_opendir(FTPD_SessionInfo_t *info, const char *dirname)
{
    char fn_buf[256];

    yaffs2_real_path(info, fn_buf, dirname);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, dirname, fn_buf);
    
    return yaffs_opendir(fn_buf);
}

//-------------------------------------------------------------------------------------------------

int yaffs2_exists(FTPD_SessionInfo_t *info, char *f_name)
{
    char fn_buf[256];
    struct yaffs_stat st;

    yaffs2_real_path(info, fn_buf, f_name);

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, f_name, fn_buf);

    return yaffs_lstat(fn_buf, &st);
}

int yaffs2_rename(FTPD_SessionInfo_t *info, char *newname)
{
    char old_buf[256], new_buf[256];
    char *oldname = info->saved_RNFR;

    yaffs2_real_path(info, old_buf, oldname);
    yaffs2_real_path(info, new_buf, newname);

    FTPD_DBG("%s(%s, %s) ==> %s, %s\r\n", __func__, oldname, newname, old_buf, new_buf);
    
    return yaffs_rename(old_buf, new_buf);
}

//-------------------------------------------------------------------------------------------------

static int yaffs2_chdir(FTPD_SessionInfo_t *info, const char *path)
{
    if (path[0] == '/')
        strncpy(info->fs_cur_dir, path, strlen(path) + 1);     /* 直接复制 */
    else
        strncat(info->fs_cur_dir, path, strlen(path) + 1);     /* 接在后面 */

    FTPD_DBG("%s(%s) ==> %s\r\n", __func__, path, info->fs_cur_dir);

    return 0;
}

static char *yaffs2_getcwd(char *buf, size_t size)
{
    strncpy(buf, ftpd_root, 254);

    FTPD_DBG("%s() ==> %s\r\n", __func__, buf);
    
    return buf;
}

/*
 * chroot("/") 时: "/ndd"
 */
static int yaffs2_chroot(const char *path)
{
    return 0;
}

//-------------------------------------------------------------------------------------
// Implements
//-------------------------------------------------------------------------------------

/*
 * Return error string corresponding to current 'errno'.
 */
static char const* serr(void)
{
    int err = errno;
    errno = 0;
    return strerror(err);
}

/*
 * Utility routines for access control.
 */
static int can_read(void)
{
    return (ftpd_access & FTPD_NO_READ) == 0;
}

static int can_write(void)
{
    return (ftpd_access & FTPD_NO_WRITE) == 0;
}

//-------------------------------------------------------------------------------------
// Task pool management routines
//-------------------------------------------------------------------------------------

/*
 * task_pool_done
 *
 * Cleanup task pool.
 *
 * Input parameters:
 *   count - number of entries in task pool to cleanup
 *
 * Output parameters:
 *   NONE
 *
 */
static void task_pool_done(int count)
{
    int i;
    
#if defined(OS_RTTHREAD)
    for (i = 0; i < count; ++i)
        rt_thread_delete(task_pool.info[i].tid);

    if (task_pool.mutex != NULL)
        rt_mutex_delete(task_pool.mutex);
    task_pool.mutex = NULL;

    if (task_pool.sem != NULL)
        rt_sem_delete(task_pool.sem);
    task_pool.sem = NULL;

#elif defined(OS_UCOS)
    unsigned char err;
    
    for (i = 0; i < count; ++i)
    {
        aligned_free(task_pool.info[i].stack);
        OSTaskDel(task_pool.info[i].tid);
    }

    if (task_pool.mutex != NULL)
        OSMutexDel(task_pool.mutex, OS_DEL_ALWAYS, &err);
    task_pool.mutex = NULL;

    if (task_pool.sem != NULL)
        OSSemDel(task_pool.sem, OS_DEL_ALWAYS, &err);
    task_pool.sem = NULL;

#elif defined(OS_FREERTOS)
    for (i = 0; i < count; ++i)
        vTaskDelete(task_pool.info[i].tid);

    if (task_pool.mutex != NULL)
        vSemaphoreDelete(task_pool.mutex);
    task_pool.mutex = NULL;

    if (task_pool.sem != NULL)
        vSemaphoreDelete(task_pool.sem);
    task_pool.sem = NULL;

#endif

    if (task_pool.info)
        mem_free(task_pool.info);
    task_pool.info = NULL;
    
    if (task_pool.queue)
        mem_free(task_pool.queue);
    task_pool.queue = NULL;

    task_pool.count = 0;
}

/*
 * task_pool_init
 *
 * Initialize task pool.
 *
 * Input parameters:
 *   count    - number of entries in task pool to create
 *   priority - priority tasks are started with
 *
 * Output parameters:
 *   returns 1 on success, 0 on failure.
 *
 */
static void session(void *arg);     /* Forward declare */

static int task_pool_init(int count, int priority)
{
    int i, sc;
    char id[8] = "FTPa";

    task_pool.count = 0;
    task_pool.head = task_pool.tail = 0;
    
#if defined(OS_RTTHREAD)
    task_pool.mutex = rt_mutex_create("FTPM", RT_IPC_FLAG_FIFO);
    task_pool.sem = rt_sem_create("FTPS", 1, RT_IPC_FLAG_FIFO);
    
#elif defined(OS_UCOS)
    unsigned char err;
    task_pool.mutex = OSMutexCreate(FTPD_MUTEX_PRIO, &err);
    task_pool.sem = OSSemCreate(1);

#elif defined(OS_FREERTOS)
    task_pool.mutex = xSemaphoreCreateMutex();
    vSemaphoreCreateBinary(task_pool.sem);

#endif

    if ((NULL == task_pool.mutex) || (NULL == task_pool.sem))
    {
        task_pool_done(0);
        PRINTF("ftpd: Can not create semaphores.\r\n");
        return 0;
    }

    task_pool.info = (FTPD_SessionInfo_t *)mem_malloc(sizeof(FTPD_SessionInfo_t) * count);
    task_pool.queue = (FTPD_SessionInfo_t **)mem_malloc(sizeof(FTPD_SessionInfo_t*) * count);
    if (NULL == task_pool.info || NULL == task_pool.queue)
    {
        task_pool_done(0);
        PRINTF("ftpd: Not enough memory.\r\n");
        return 0;
    }

    for (i = 0; i < count; ++i)
    {
        FTPD_SessionInfo_t *info = &task_pool.info[i];

#if defined(OS_RTTHREAD)
        info->event = rt_event_create((const char *)id, 0);
        
        info->tid = rt_thread_create((const char *)id,
                                     session,
                                     (void *)info,              // parameter
                                     (FTPD_STACKSIZE),          // bytes
                                     (rt_uint8_t)priority + 1,  // +1 ?
                                     10);

#elif defined(OS_UCOS)
        
        info->event = OSFlagCreate(0, &err);

        info->tid = 0;
        info->stack = (OS_STK *)aligned_malloc(FTPD_STACKSIZE, 4);
        if (NULL != info->stack)
        {
            err = OSTaskCreate(session,
                               (void *)info,                    // parameter
                        #if OS_STK_GROWTH == 1
                               (void *)(info->stack + (FTPD_STACKSIZE) / 4 - 1), // dwords
                        #else
                               (void *)info->stack,
                        #endif
                               priority + i + 1);

            if (OS_ERR_NONE == err)
                info->tid = priority + i + 1;
        }

#elif defined(OS_FREERTOS)
        info->event = xEventGroupCreate();

        xTaskCreate(session,
                    (const char *)id,
                    FTPD_STACKSIZE / 4,                     // dwrods
                    (void *)info,                           // parameter
                    priority - 1,                           // -1 ?
                    &info->tid);

#endif

#if defined(OS_UCOS)
        if ((0 == info->tid) || (NULL == info->event))
#else
        if ((NULL == info->tid) || (NULL == info->event))
#endif
        {
            task_pool_done(i + 1);
            PRINTF("ftpd: Could not create/start FTPD session: %i\r\n", sc);
            return 0;
        }

#if defined(OS_RTTHREAD)
        rt_thread_startup(info->tid);
#endif

        task_pool.queue[i] = task_pool.info + i;
        if (++id[3] > 'z')
            id[3] = 'a';
    }
    
    task_pool.count = count;
    return 1;
}

/*
 * task_pool_obtain
 *
 * Obtain free task from task pool.
 *
 * Input parameters:
 *   NONE
 *
 * Output parameters:
 *   returns pointer to the corresponding SessionInfo structure on success,
 *           NULL if there are no free tasks in the pool.
 *
 */
static FTPD_SessionInfo_t* task_pool_obtain(void)
{
    FTPD_SessionInfo_t* info = NULL;
    
#if defined(OS_RTTHREAD)
    if (RT_EOK != rt_sem_take(task_pool.sem, 10))
        return NULL;
        
    rt_mutex_take(task_pool.mutex, RT_WAITING_FOREVER);
    
#elif defined(OS_UCOS)
    unsigned char err;
    OSSemPend(task_pool.sem, 10, &err);
    if (OS_ERR_NONE != err)
        return NULL;
    
    OSMutexPend(task_pool.mutex, ~0, &err);

#elif defined(OS_FREERTOS)
    if (xSemaphoreTake(task_pool.sem, 10) != pdTRUE)    /* wait 10 ms? */
        return NULL;

    xSemaphoreTake(task_pool.mutex, portMAX_DELAY);
    
#endif
 
    info = task_pool.queue[task_pool.head];
    if (++task_pool.head >= task_pool.count)
        task_pool.head = 0;

#if defined(OS_RTTHREAD)
    rt_mutex_release(task_pool.mutex);

#elif defined(OS_UCOS)
    OSMutexPost(task_pool.mutex);
    
#elif defined(OS_FREERTOS)
    xSemaphoreGive(task_pool.mutex);

#endif

    return info;
}

/*
 * task_pool_release
 *
 * Return task obtained by 'obtain()' back to the task pool.
 *
 * Input parameters:
 *   info  - pointer to corresponding SessionInfo structure.
 *
 * Output parameters:
 *   NONE
 *
 */
static void task_pool_release(FTPD_SessionInfo_t* info)
{
#if defined(OS_RTTHREAD)
    rt_mutex_take(task_pool.mutex, RT_WAITING_FOREVER);
    
#elif defined(OS_UCOS)
    unsigned char err;
    OSMutexPend(task_pool.mutex, ~0, &err);
    
#elif defined(OS_FREERTOS)
    xSemaphoreTake(task_pool.mutex, portMAX_DELAY);

#endif

    task_pool.queue[task_pool.tail] = info;
    if (++task_pool.tail >= task_pool.count)
        task_pool.tail = 0;

#if defined(OS_RTTHREAD)
    rt_mutex_release(task_pool.mutex);
    rt_sem_release(task_pool.sem);
    
#elif defined(OS_UCOS)
    OSMutexPost(task_pool.mutex);
    OSSemPost(task_pool.sem);

#elif defined(OS_FREERTOS)
    xSemaphoreGive(task_pool.mutex);
	xSemaphoreGive(task_pool.sem);

#endif
}

/*
 * End of task pool routines
 */

/*
 * Function: send_reply
 *
 *    This procedure sends a reply to the client via the control
 *    connection.
 *
 * Input parameters:
 *   code  - 3-digit reply code.
 *   text  - Reply text.
 *
 * Output parameters:
 *   NONE
 */
static void send_reply(FTPD_SessionInfo_t *info, int code, char *text)
{
    int buflen;
    static char buf[FTPD_DATASIZE+FTPD_BUFSIZE];

    text = text != NULL ? text : "";
    buflen = snprintf(buf, FTPD_DATASIZE+FTPD_BUFSIZE-1, "%d %.70s\r\n", code, text);
    lwip_write(info->ctrl_socket, buf, buflen);
}

/*
 * close_socket
 *
 * Close socket.
 *
 * Input parameters:
 *   s - socket descriptor.
 *   seconds - number of seconds the timeout should be,
 *             if >= 0 - infinite timeout (no timeout).
 *
 * Output parameters:
 *   returns 1 on success, 0 on failure.
 */
static int set_socket_timeout(int s, int seconds)
{
    int res = 0;
    struct timeval tv;
    int len = sizeof(tv);

    if (seconds < 0)
        seconds = 0;
    tv.tv_usec = 0;
    tv.tv_sec  = seconds;
    
    if (0 != lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, len))
    {
        PRINTF("ftpd: Can't set send timeout on socket: %s.\r\n", serr());
    }
    else if (0 != lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, len))
    {
        PRINTF("ftpd: Can't set receive timeout on socket: %s.\r\n", serr());
    }
    else
        res = 1;
        
    return res;
}

/*
 * close_socket
 *
 * Close socket.
 *
 * Input parameters:
 *   s - socket descriptor to be closed.
 *
 * Output parameters:
 *   returns 1 on success, 0 on failure
 */
static int close_socket(int s)
{
    if (0 <= s)
    {
        if (0 != lwip_close(s))
        {
            lwip_shutdown(s, 2);
            if (0 != lwip_close(s))
                return 0;
        }
    }

    return 1;
}

/*
 * data_socket
 *
 * Create data socket for session.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   returns socket descriptor, or -1 if failure
 *
 */
static int data_socket(FTPD_SessionInfo_t *info)
{
    int s = info->pasv_socket;
    
    if (0 > s)
    {
        int on = 1;
        s = lwip_socket(PF_INET, SOCK_STREAM, 0);
        if (0 > s)
        {
            send_reply(info, 425, "Can't create data socket.");
        }
        else if (0 > lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        {
            close_socket(s);
            s = -1;
        }
        else
        {
            struct sockaddr_in data_source;
            int tries;

            /* anchor socket to avoid multi-homing problems */
            data_source = info->ctrl_addr;
            data_source.sin_port = htons(20);   /* ftp-data port */
            for (tries = 1; tries < 10; ++tries)
            {
                errno = 0;
                if (lwip_bind(s, (struct sockaddr *)&data_source, sizeof(data_source)) >= 0)
                    break;
                    
                if (errno != EADDRINUSE)
                    tries = 10;
                else
#if defined(OS_RTTHREAD)
                    rt_thread_delay(tries * 10);
#elif defined(OS_UCOS)
                    OSTimeDly(tries * 10);
#elif defined(OS_FREERTOS)
                    vTaskDelay(tries * 10);
#endif
            }
            
            if (tries >= 10)
            {
                send_reply(info, 425, "Can't bind data socket.");
                close_socket(s);
                s = -1;
            }
            else
            {
                struct sockaddr_in *data_dest = (info->use_default) ? &info->def_addr : &info->data_addr;
                if (0 > lwip_connect(s, (struct sockaddr *)data_dest, sizeof(*data_dest)))
                {
                    send_reply(info, 425, "Can't connect data socket.");
                    close_socket(s);
                    s = -1;
                }
            }
        }
    }
    
    info->data_socket = s;
    info->use_default = 1;
    if (s >= 0)
        set_socket_timeout(s, info->idle);
        
    return s;
}

/*
 * close_data_socket
 *
 * Close data socket for session.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   NONE
 *
 */
static void close_data_socket(FTPD_SessionInfo_t *info)
{
    /* As at most one data socket could be open simultaneously and in some cases
       data_socket == pasv_socket, we select socket to close, then close it. */
    int s = info->data_socket;
    if (0 > s)
        s = info->pasv_socket;
        
    if (!close_socket(s))
    {
        PRINTF("ftpd: Error closing data socket.\r\n");
    }
    
    info->data_socket = -1;
    info->pasv_socket = -1;
    info->use_default = 1;
}

/*
 * close_stream
 *
 * Close control stream of session.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   NONE
 *
 */
static void close_stream(FTPD_SessionInfo_t* info)
{
    if (!close_socket(info->ctrl_socket))
    {
        PRINTF("ftpd: Could not close control socket: %s\r\n", serr());
    }

    info->ctrl_socket = -1;
}

/*
 * send_mode_reply
 *
 * Sends BINARY/ASCII reply string depending on current transfer mode.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   NONE
 *
 */
static void send_mode_reply(FTPD_SessionInfo_t *info)
{
    if (info->xfer_mode == TYPE_I)
        send_reply(info, 150, "Opening BINARY mode data connection.");
    else
        send_reply(info, 150, "Opening ASCII mode data connection.");
}

/*
 * command_retrieve
 *
 * Perform the "RETR" command (send file to client).
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   char *filename  - source filename.
 *
 * Output parameters:
 *   NONE
 *
 */
static void command_retrieve(FTPD_SessionInfo_t *info, char const *filename)
{
    int               s = -1;
    int               fd = -1;
    char              buf[FTPD_DATASIZE];
    struct yaffs_stat st;
    int               res = 0;

    if (!can_read() || !info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    if (0 > (fd = yaffs2_open(info, filename, O_RDWR/*O_RDONLY*/, 0777)))
    {
        send_reply(info, 550, "Error opening file.");
        return;
    }

    if (yaffs2_fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
    {
        if (-1 != fd)
            yaffs2_close(fd);
        send_reply(info, 550, "Is a directory.");
        return;
    }

    send_mode_reply(info);

    s = data_socket(info);
    if (0 <= s)
    {
        int n = -1;

        if (info->xfer_mode == TYPE_I)
        {
            while ((n = yaffs2_read(fd, buf, FTPD_DATASIZE)) > 0)
            {
                if (lwip_send(s, buf, n, 0) != n)
                    break;
            }
        }
        else if (info->xfer_mode == TYPE_A)
        {
            int rest = 0;
            while (rest == 0 && (n = yaffs2_read(fd, buf, FTPD_DATASIZE)) > 0)
            {
                char const* e = buf;
                char const* b;
                int i;
                rest = n;
                do
                {
                    char lf = '\0';

                    b = e;
                    for (i = 0; i < rest; ++i, ++e)
                    {
                        if (*e == '\n')
                        {
                            lf = '\n';
                            break;
                        }
                    }
                    if (lwip_send(s, b, i, 0) != i)
                        break;
                    if (lf == '\n')
                    {
                        if (lwip_send(s, "\r\n", 2, 0) != 2)
                            break;
                        ++e;
                        ++i;
                    }
                } while ((rest -= i) > 0);
            }
        }

        if (0 == n)
        {
            if (0 == yaffs2_close(fd))
            {
                fd = -1;
                res = 1;
            }
        }
    }

    if (-1 != fd)
        yaffs2_close(fd);

    if (0 == res)
        send_reply(info, 451, "File read error.");
    else
        send_reply(info, 226, "Transfer complete.");

    close_data_socket(info);

    return;
}

/*
 * discard
 *
 * Analog of `write' routine that just discards passed data
 *
 * Input parameters:
 *   fd    - file descriptor (ignored)
 *   buf   - data to write (ignored)
 *   count - number of bytes in `buf'
 *
 * Output parameters:
 *   returns `count'
 *
 */
#if defined(USE_YAFFS2)
static int discard(int fd, const void * buf, unsigned int count)
#else
static ssize_t discard(int fd, void const* buf, size_t count)
#endif
{
    (void)fd;
    (void)buf;
    return count;
}

/*
 * command_store
 *
 * Performs the "STOR" command (receive data from client).
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   char *filename   - Destination filename.
 *
 * Output parameters:
 *   NONE
 */
static void command_store(FTPD_SessionInfo_t *info, char const *filename)
{
    int               s;
    int               n;
    unsigned long     size = 0;
    struct ftpd_hook *usehook = NULL;
    char              buf[FTPD_DATASIZE];
    int               res = 1;
    int               bare_lfs = 0;
    int               null = 0;

#if defined(USE_YAFFS2)
    typedef int (*WriteProc)(int, const void *, unsigned int);
#else
    typedef ssize_t (*WriteProc)(int, void const*, size_t);
#endif

    WriteProc         wrt = &yaffs2_write;

    if (!can_write() || !info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    send_mode_reply(info);

    s = data_socket(info);
    if (0 > s)
        return;

    null = !strcmp("/null", filename);
    if (null)
    {
        /* File "/null" just throws data away.
         *  FIXME: this is hack.  Using `/null' filesystem entry would be
         *  better.
         */
        wrt = &discard;
    }

    if (!null && _ftpd_configuration.hooks != NULL)
    {
        /* Search our list of hooks to see if we need to do something special. */
        struct ftpd_hook *hook;
        int i;

        i = 0;
        hook = &_ftpd_configuration.hooks[i++];
        while (hook->filename != NULL)
        {
            if (!strcmp(hook->filename, filename))
            {
                usehook = hook;
                break;
            }
            
            hook = &_ftpd_configuration.hooks[i++];
        }
    }

    if (usehook != NULL)
    {
        /*
         * OSV: FIXME: Small buffer could be used and hook routine
         * called multiple times instead.  Alternatively, the support could be
         * removed entirely in favor of configuring RTEMS pseudo-device with
         * given name.
         */
        char *bigBufr;
        size_t filesize = _ftpd_configuration.max_hook_filesize + 1;

        /*
         * Allocate space for our "file".
         */
        bigBufr = (char *)mem_malloc(filesize);
        if (bigBufr == NULL)
        {
            send_reply(info, 451, "Local resource failure: malloc.");
            close_data_socket(info);
            return;
        }

        /*
         * Retrieve the file into our buffer space.
         */
        size = 0;
        while ((n = lwip_recv(s, bigBufr + size, filesize - size, 0)) > 0)
        {
            size += n;
        }
        
        if (size >= filesize)
        {
            send_reply(info, 451, "File too long: buffer size exceeded.");
            mem_free(bigBufr);
            close_data_socket(info);
            return;
        }

        /*
         * Call our hook.
         */
        res = (usehook->hook_function)(bigBufr, size) == 0;
        mem_free(bigBufr);
        if (!res)
        {
            send_reply(info, 451, "File processing failed.");
            close_data_socket(info);
            return;
        }
    }
    else
    {
        /* Data transfer to regular file or /null. */
        int fd = 0;

        if (!null)
        {
            fd = yaffs2_open(info, filename, 0100/*O_CREAT*/ | O_RDWR, 0777); /* 创建文件 */
        }

        if (0 > fd)
        {
            send_reply(info, 550, "Error creating file.");
            close_data_socket(info);
            return;
        }

        if (info->xfer_mode == TYPE_I)
        {
            while ((n = lwip_recv(s, buf, FTPD_DATASIZE, 0)) > 0)
            {
                if (wrt(fd, buf, n) != n)
                {
                    res = 0;
                    break;
                }
            }
        }
        else if (info->xfer_mode == TYPE_A)
        {
            int rest = 0;
            int pended_cr = 0;
            
            while (res && rest == 0 && (n = lwip_recv(s, buf, FTPD_DATASIZE, 0)) > 0)
            {
                char const* e = buf;
                char const* b;
                int i;

                rest = n;
                if (pended_cr && *e != '\n')
                {
                    char const lf = '\r';
                    pended_cr = 0;
                    if (wrt(fd, &lf, 1) != 1)
                    {
                        res = 0;
                        break;
                    }
                }
                do
                {
                    int count;
                    int sub = 0;

                    b = e;
                    for (i = 0; i < rest; ++i, ++e)
                    {
                        int pcr = pended_cr;
                        pended_cr = 0;
                        if (*e == '\r')
                        {
                            pended_cr = 1;
                        }
                        else if (*e == '\n')
                        {
                            if (pcr)
                            {
                                sub = 2;
                                ++i;
                                ++e;
                                break;
                            }
                            ++bare_lfs;
                        }
                    }
                    
                    if (res == 0)
                        break;
                        
                    count = i - sub - pended_cr;
                    if (count > 0 && wrt(fd, b, count) != count)
                    {
                        res = 0;
                        break;
                    }
                    
                    if (sub == 2 && wrt(fd, e - 1, 1) != 1)
                        res = 0;
                        
                } while((rest -= i) > 0);
            }
        }

        if (0 > yaffs2_close(fd) || res == 0)
        {
            send_reply(info, 452, "Error writing file.");
            close_data_socket(info);
            return;
        }
    }

    if (bare_lfs > 0)
    {
        snprintf(buf, FTPD_BUFSIZE,
                 "Transfer complete. WARNING! %d bare linefeeds received in ASCII mode.",
                 bare_lfs);
        send_reply(info, 226, buf);
    }
    else
        send_reply(info, 226, "Transfer complete.");

    close_data_socket(info);
}

/*
 * send_dirline
 *
 * Sends one line of LIST command reply corresponding to single file.
 *
 * Input parameters:
 *   s - socket descriptor to send data to
 *   wide - if 0, send only file name.  If not 0, send 'stat' info as well in
 *          "ls -l" format.
 *   curTime - current time
 *   path - path to be prepended to what is given by 'add'
 *   add  - path to be appended to what is given by 'path', the resulting path
 *          is then passed to 'stat()' routine
 *   name - file name to be reported in output
 *   buf  - buffer for temporary data
 *
 * Output parameters:
 *   returns 0 on failure, 1 on success
 *
 */
static int send_dirline(FTPD_SessionInfo_t *info,
                        int s,
                        int wide,
                        time_t curTime,
                        char const* path,
                        char const* add,
                        char const* fname,
                        char* buf)
{
    if (wide)
    {
        struct yaffs_stat st;

        int plen = strlen(path);
        int alen = strlen(add);
        if (plen == 0)
        {
            buf[plen++] = '/';
            buf[plen] = '\0';
        }
        else
        {
            strcpy(buf, path);
            if (alen > 0 && buf[plen - 1] != '/')
            {
                buf[plen++] = '/';
                if (plen >= FTPD_BUFSIZE)
                    return 0;
                buf[plen] = '\0';
            }
        }
        
        if (plen + alen >= FTPD_BUFSIZE)
            return 0;
        strcpy(buf + plen, add);

        if (yaffs2_lstat(info, buf, &st) == 0)
        {
            int len;
            struct tm bt;
            time_t tf = st.yst_mtime;
            enum { SIZE = 80 };
            time_t SIX_MONTHS = (365L*24L*60L*60L)/2L;
            char timeBuf[SIZE];

            gmtime_r(&tf, &bt);
            if (curTime > tf + SIX_MONTHS || tf > curTime + SIX_MONTHS)
                strftime(timeBuf, SIZE, "%b %d  %Y", &bt);
            else
                strftime(timeBuf, SIZE, "%b %d %H:%M", &bt);

            len = snprintf(buf, FTPD_BUFSIZE,
                           "%c%c%c%c%c%c%c%c%c%c  1 %5d %5d %11u %s %s\r\n",
                           (S_ISLNK(st.st_mode)   ? ('l'):
                           (S_ISDIR(st.st_mode)   ? ('d'):('-'))),
                           (st.st_mode & S_IRUSR) ? ('r'):('-'),
                           (st.st_mode & S_IWUSR) ? ('w'):('-'),
                           (st.st_mode & S_IXUSR) ? ('x'):('-'),
                           (st.st_mode & S_IRGRP) ? ('r'):('-'),
                           (st.st_mode & S_IWGRP) ? ('w'):('-'),
                           (st.st_mode & S_IXGRP) ? ('x'):('-'),
                           (st.st_mode & S_IROTH) ? ('r'):('-'),
                           (st.st_mode & S_IWOTH) ? ('w'):('-'),
                           (st.st_mode & S_IXOTH) ? ('x'):('-'),
                           (int)st.st_uid,
                           (int)st.st_gid,
                           (int)st.st_size,
                           timeBuf,
                           fname
                          );

            if (lwip_send(s, buf, len, 0) != len)
                return 0;
        }
    }
    else
    {
        int len = snprintf(buf, FTPD_BUFSIZE, "%s\r\n", fname);
        if (lwip_send(s, buf, len, 0) != len)
            return 0;
    }

    return 1;
}

/*
 * command_list
 *
 * Send file list to client.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   char *fname  - File (or directory) to list.
 *
 * Output parameters:
 *   NONE
 */
static void command_list(FTPD_SessionInfo_t *info, char const *fname, int wide)
{
    int               s;
    yaffs_DIR         *dirp = 0;
    yaffs_dirent      *dp = 0;
    struct yaffs_stat st;
    char              buf[FTPD_BUFSIZE];
    time_t            curTime;
    int               sc = 1;

    if (!info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    send_reply(info, 150, "Opening ASCII mode data connection for LIST.");

    s = data_socket(info);
    if (0 > s)
    {
        PRINTF("ftpd: Error connecting to data socket.\r\n");
        return;
    }

    if (fname[0] == '\0')
        fname = ".";

    if (0 > yaffs2_lstat(info, fname, &st))
    {
        snprintf(buf, FTPD_BUFSIZE, "%s: No such file or directory.\r\n", fname);
        lwip_send(s, buf, strlen(buf), 0);
    }
    else if (S_ISDIR(st.st_mode) &&
            (NULL == (dirp = yaffs2_opendir(info, fname))))     /* 打开目录 */
    {
        snprintf(buf, FTPD_BUFSIZE, "%s: Can not open directory.\r\n", fname);
        lwip_send(s, buf, strlen(buf), 0);
    }
    else
    {
        time(&curTime);
        
        if (!dirp && *fname)
        {
            sc = sc && send_dirline(info, s, wide, curTime, fname, "", fname, buf);
        }
        else
        {
            /* FIXME: need "." and ".." only when '-a' option is given
             */
            sc = sc && send_dirline(info, s, wide, curTime, fname, "", ".", buf);

            sc = sc && send_dirline(info, s, wide, curTime, fname,
                                    (strcmp(fname, ftpd_root) ? ".." : ""), "..", buf);

            /*
             * 读目录
             */
            while (sc && (dp = yaffs2_readdir(dirp)) != NULL)
            {
                sc = sc && send_dirline(info, s, wide, curTime, fname, dp->d_name, dp->d_name, buf);
            }
        }
    }

    if (dirp)
        yaffs2_closedir(dirp);       /* 关闭目录 */
    close_data_socket(info);

    if (sc)
        send_reply(info, 226, "Transfer complete.");
    else
        send_reply(info, 426, "Connection aborted.");
}

/*
 * command_cwd
 *
 * Change current working directory.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   dir  - directory name passed in CWD command
 *
 * Output parameters:
 *   NONE
 *
 */
static void command_cwd(FTPD_SessionInfo_t *info, char *dir)
{
    if (!info->auth)
    {
        FTPD_DBG("%s(), %s\r\n", __func__, "info->auth = 0");
        send_reply(info, 550, "Access denied.");
        return;
    }

    if (yaffs2_chdir(info, dir) == 0)
        send_reply(info, 250, "CWD command successful.");
    else
        send_reply(info, 550, "CWD command failed.");
}

/*
 * command_pwd
 *
 * Send current working directory to client.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   NONE
 */
static void command_pwd(FTPD_SessionInfo_t *info)
{
    char buf[FTPD_BUFSIZE];
    char const* cwd;
    errno = 0;
    buf[0] = '"';

    if (!info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    cwd = yaffs2_getcwd(buf + 1, FTPD_BUFSIZE - 4);
    if (cwd)
    {
        int len = strlen(cwd);
        static char const txt[] = "\" is the current directory.";
        int size = sizeof(txt);
        if (len + size + 1 >= FTPD_BUFSIZE)
            size = FTPD_BUFSIZE - len - 2;
        memcpy(buf + len + 1, txt, size);
        buf[len + size] = '\0';
        send_reply(info, 250, buf);
    }
    else
    {
        snprintf(buf, FTPD_BUFSIZE, "Error: %s.", serr());
        send_reply(info, 452, buf);
    }
}

/*
 * command_mdtm
 *
 * Handle FTP MDTM command (send file modification time to client)/
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   fname - file name passed in MDTM command
 *
 * Output parameters:
 *   info->cwd is set to new CWD value.
 */
static void command_mdtm(FTPD_SessionInfo_t *info, char const* fname)
{
    struct yaffs_stat st;
    char buf[FTPD_BUFSIZE];

    if (!info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    if (0 > yaffs2_lstat(info, fname, &st))
    {
        snprintf(buf, FTPD_BUFSIZE, "%s: %s.", fname, serr());
        send_reply(info, 550, buf);
    }
    else
    {
        struct tm *t = gmtime((const time_t *)&st.yst_mtime);   // st.st_mtime
        
        snprintf(buf, FTPD_BUFSIZE, "%04d%02d%02d%02d%02d%02d",
                 1900 + t->tm_year,
                 t->tm_mon+1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        send_reply(info, 213, buf);
    }
}

static void command_size(FTPD_SessionInfo_t *info, char const* fname)
{
    struct yaffs_stat st;
    char buf[FTPD_BUFSIZE];

    if (!info->auth)
    {
        send_reply(info, 550, "Access denied.");
        return;
    }

    if (info->xfer_mode != TYPE_I || 0 > yaffs2_lstat(info, fname, &st) || st.st_size < 0)
    {
        send_reply(info, 550, "Could not get file size.");
    }
    else
    {
        snprintf(buf, FTPD_BUFSIZE, "%u", (unsigned int)st.st_size);
        send_reply(info, 213, buf);
    }
}

/*
 * command_port
 *
 * This procedure fills address for data connection given the IP address and
 * port of the remote machine.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   args - arguments to the "PORT" command.
 *
 * Output parameters:
 *   info->data_addr is set according to arguments of the PORT command.
 *   info->use_default is set to 0 on success, 1 on failure.
 */
static void command_port(FTPD_SessionInfo_t *info, char const *args)
{
    enum { NUM_FIELDS = 6 };
    unsigned int a[NUM_FIELDS];
    int n;

    close_data_socket(info);

    n = sscanf(args, "%u,%u,%u,%u,%u,%u", a+0, a+1, a+2, a+3, a+4, a+5);
    
    if (NUM_FIELDS == n)
    {
        int i;
        union
        {
            unsigned char b[NUM_FIELDS];
            struct
            {
                unsigned int ip;
                unsigned short port;
            } u ;
        } ip_info;

        for (i = 0; i < NUM_FIELDS; ++i)
        {
            if (a[i] > 255)
                break;
            ip_info.b[i] = (unsigned char)a[i];
        }

        if (i == NUM_FIELDS)
        {
            /* Note: while it contradicts with RFC959, we don't allow PORT command
             * to specify IP address different than those of the originating client
             * for the sake of safety. */
            if (ip_info.u.ip == info->def_addr.sin_addr.s_addr)
            {
                info->data_addr.sin_addr.s_addr = ip_info.u.ip;
                info->data_addr.sin_port        = ip_info.u.port;
                info->data_addr.sin_family      = AF_INET;
                memset(info->data_addr.sin_zero, 0, sizeof(info->data_addr.sin_zero));

                info->use_default = 0;
                send_reply(info, 200, "PORT command successful.");
                return; /* success */
            }
            else
            {
                send_reply(info, 425, "Address doesn't match peer's IP.");
                return;
            }
        }
    }

    send_reply(info, 501, "Syntax error.");
}

/*
 * command_pasv
 *
 * Handle FTP PASV command.
 * Open socket, listen for and accept connection on it.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *
 * Output parameters:
 *   info->pasv_socket is set to the descriptor of the data socket
 */
static void command_pasv(FTPD_SessionInfo_t *info)
{
    int s = -1;
    int err = 1;

    close_data_socket(info);

    s = lwip_socket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        PRINTF("ftpd: Error creating PASV socket: %s\r\n", serr());
    }
    else
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);

        addr = info->ctrl_addr;
        addr.sin_port = htons(0);

        if (0 > lwip_bind(s, (struct sockaddr *)&addr, addrLen))
        {
            PRINTF("ftpd: Error binding PASV socket: %s\r\n", serr());
        }
        else if (0 > lwip_listen(s, 1))
        {
            PRINTF("ftpd: Error listening on PASV socket: %s\r\n", serr());
        }
        else if (set_socket_timeout(s, info->idle))
        {
            char buf[FTPD_BUFSIZE];
            unsigned char const *ip, *p;

            lwip_getsockname(s, (struct sockaddr *)&addr, &addrLen);
            ip = (unsigned char const *)&(addr.sin_addr);
            p  = (unsigned char const *)&(addr.sin_port);
            snprintf(buf, FTPD_BUFSIZE, "Entering passive mode (%u,%u,%u,%u,%u,%u).",
                     ip[0], ip[1], ip[2], ip[3], p[0], p[1]);
            send_reply(info, 227, buf);

            info->pasv_socket = lwip_accept(s, (struct sockaddr *)&addr, &addrLen);
            if (0 > info->pasv_socket)
            {
                PRINTF("ftpd: Error accepting PASV connection: %s\r\n", serr());
            }
            else
            {
                close_socket(s);
                s = -1;
                err = 0;
            }
        }
    }
    
    if (err)
    {
        /* (OSV) The note is from FreeBSD FTPD.
         * Note: a response of 425 is not mentioned as a possible response to
         * the PASV command in RFC959.  However, it has been blessed as a
         * legitimate response by Jon Postel in a telephone conversation
         * with Rick Adams on 25 Jan 89. */
        send_reply(info, 425, "Can't open passive connection.");
        close_socket(s);
    }
}

/*
 * skip_options
 *
 * Utility routine to skip options (if any) from input command.
 *
 * Input parameters:
 *   p  - pointer to pointer to command
 *
 * Output parameters:
 *   p  - is changed to point to first non-option argument
 */
static void skip_options(char **p)
{
    char* buf = *p;
    char* last = NULL;
    
    while (1)
    {
        while (isspace((unsigned char)*buf))
            ++buf;

        if (*buf == '-')
        {
            if (*++buf == '-')   /* `--' should terminate options */
            {
                if (isspace((unsigned char)*++buf))
                {
                    last = buf;
                    do ++buf;
                    while (isspace((unsigned char)*buf));
                    break;
                }
            }
            
            while (*buf && !isspace((unsigned char)*buf))
                ++buf;
            last = buf;
        }
        else
            break;
    }
    
    if (last)
        *last = '\0';
    *p = buf;
}

/*
 * split_command
 *
 * Split command into command itself, options, and arguments. Command itself
 * is converted to upper case.
 *
 * Input parameters:
 *   buf - initial command string
 *
 * Output parameter:
 *   buf  - is modified by inserting '\0' at ends of split entities
 *   cmd  - upper-cased command code
 *   opts - string containing all the options
 *   args - string containing all the arguments
 */
static void split_command(char *buf, char **cmd, char **opts, char **args)
{
    char* eoc;
    char* p = buf;

    while (isspace((unsigned char)*p))
        ++p;
    *cmd = p;
    
    while (*p && !isspace((unsigned char)*p))
    {
        *p = toupper((unsigned char)*p);
        ++p;
    }
    eoc = p;
    if (*p)
        *p++ = '\0';
    while (isspace((unsigned char)*p))
        ++p;
    *opts = p;
    
    skip_options(&p);
    *args = p;
    if (*opts == p)
        *opts = eoc;
    while (*p && *p != '\r' && *p != '\n')
        ++p;
    if (*p)
        *p++ = '\0';
}

/*
 * exec_command
 *
 * Parse and execute FTP command.
 *
 * FIXME: This section is somewhat of a hack.  We should have a better
 *        way to parse commands.
 *
 * Input parameters:
 *   info - corresponding SessionInfo structure
 *   cmd  - command to be executed (upper-case)
 *   args - arguments of the command
 *
 * Output parameters:
 *    NONE
 */
static void exec_command(FTPD_SessionInfo_t *info, char* cmd, char* args)
{
    char fname[FTPD_BUFSIZE];
    int wrong_command = 0;

    fname[0] = '\0';

    FTPD_DBG("cmd=%s ; arg=%s \r\n", cmd, args);

    if (!strcmp("PORT", cmd))
    {
        command_port(info, args);
    }
    else if (!strcmp("PASV", cmd))
    {
        command_pasv(info);
    }
    else if (!strcmp("RETR", cmd))
    {
        strncpy(fname, args, 254);
        command_retrieve(info, fname);
    }
    else if (!strcmp("STOR", cmd))
    {
        strncpy(fname, args, 254);
        command_store(info, fname);
    }
    else if (!strcmp("LIST", cmd))
    {
        strncpy(fname, args, 254);
        command_list(info, fname, 1);
    }
    else if (!strcmp("NLST", cmd))
    {
        strncpy(fname, args, 254);
        command_list(info, fname, 0);
    }
    else if (!strcmp("MDTM", cmd))
    {
        strncpy(fname, args, 254);
        command_mdtm(info, fname);
    }
    else if (!strcmp("SIZE", cmd))
    {
        strncpy(fname, args, 254);
        command_size(info, fname);
    }
    else if (!strcmp("SYST", cmd))
    {
        send_reply(info, 215, FTPD_SYSTYPE);
    }
    else if (!strcmp("TYPE", cmd))
    {
        if (args[0] == 'I')
        {
            info->xfer_mode = TYPE_I;
            send_reply(info, 200, "Type set to I.");
        }
        else if (args[0] == 'A')
        {
            info->xfer_mode = TYPE_A;
            send_reply(info, 200, "Type set to A.");
        }
        else
        {
            info->xfer_mode = TYPE_I;
            send_reply(info, 504, "Type not implemented.  Set to I.");
        }
    }
    else if (!strcmp("USER", cmd))
    {
        sscanf(args, "%254s", fname);
        if (info->user)
            mem_free(info->user);
        if (info->pass)
            mem_free(info->pass);
        info->pass = NULL;
        info->user = strdup(fname);
        if (_ftpd_configuration.login && !_ftpd_configuration.login(info->user, NULL))
        {
            info->auth = false;
            send_reply(info, 331, "User name okay, need password.");
        }
        else
        {
            info->auth = true;
            send_reply(info, 230, "User logged in.");
        }
    }
    else if (!strcmp("PASS", cmd))
    {
        sscanf(args, "%254s", fname);
        if (info->pass)
            mem_free(info->pass);
        info->pass = strdup(fname);
        if (!info->user)
        {
            send_reply(info, 332, "Need account to log in");
        }
        else
        {
            if (_ftpd_configuration.login && !_ftpd_configuration.login(info->user, info->pass))
            {
                info->auth = false;
                send_reply(info, 530, "Not logged in.");
            }
            else
            {
                info->auth = true;
                send_reply(info, 230, "User logged in.");
            }
        }
    }
    else if (!strcmp("DELE", cmd))
    {
        if (!can_write() || !info->auth)
        {
            send_reply(info, 550, "Access denied.");
        }
        else if (strncpy(fname, args, 254) &&
                 yaffs2_delete(info, fname) == 0)       /* 删除文件 */
        {
            send_reply(info, 257, "DELE successful.");
        }
        else
        {
            send_reply(info, 550, "DELE failed.");
        }
    }
    
    else if (!strcmp("RNFR", cmd))
    {
        if (!can_write() || !info->auth)
        {
            send_reply(info, 550, "Access denied.");
        }
        else if (strncpy(fname, args, 254) &&
                 yaffs2_exists(info, fname) == 0)       /* 文件存在 */
        {
            strncpy(info->saved_RNFR, args, 254);       /* 保存 */
            send_reply(info, 350, "RNFR ready.");
        }
        else
        {
            send_reply(info, 550, "RNFR failed.");
        }
    }
    else if (!strcmp("RNTO", cmd))
    {
        if (!can_write() || !info->auth)
        {
            send_reply(info, 550, "Access denied.");
        }
        else if (strncpy(fname, args, 254) &&
                 yaffs2_rename(info, fname) == 0)       /* 重命名 */
        {
            send_reply(info, 250, "RNTO successful.");
        }
        else
        {
            send_reply(info, 553, "RNFR failed.");
        }
        
        info->saved_RNFR[0] = '\0';                     /* 清理 */
    }

    else if (!strcmp("SITE", cmd))
    {
        char* opts;
        split_command(args, &cmd, &opts, &args);
        
        if (!strcmp("CHMOD", cmd))
        {
            int mask;

            if (!can_write() || !info->auth)
            {
                send_reply(info, 550, "Access denied.");
            }
            else
            {
                char *c;
                c = strchr(args, ' ');
                if ((c != NULL) &&
                    (sscanf(args, "%o", &mask) == 1) &&
                     strncpy(fname, c+1, 254) &&
                    (yaffs2_chmod(info, fname, (mode_t)mask) == 0)) /* 修改文件属性 */
                    send_reply(info, 257, "CHMOD successful.");
                else
                    send_reply(info, 550, "CHMOD failed.");
            }
        }
        else
            wrong_command = 1;
    }
    else if (!strcmp("RMD", cmd))
    {
        if (!can_write() || !info->auth)
        {
            send_reply(info, 550, "Access denied.");
        }
        else if (strncpy(fname, args, 254) &&
                 yaffs2_rmdir(info, fname) == 0)            /* 删除目录 */
        {
            send_reply(info, 257, "RMD successful.");
        }
        else
        {
            send_reply(info, 550, "RMD failed.");
        }
    }
    else if (!strcmp("MKD", cmd))
    {
        if (!can_write() || !info->auth)
        {
            send_reply(info, 550, "Access denied.");
        }
        else if (strncpy(fname, args, 254) &&
                 yaffs2_mkdir(info, fname, S_IFDIR) == 0)   /* 新建目录 */
        {
            send_reply(info, 257, "MKD successful.");
        }
        else
        {
            send_reply(info, 550, "MKD failed.");
        }
    }
    else if (!strcmp("CWD", cmd))
    {
        strncpy(fname, args, 254);
        command_cwd(info, fname);
    }
    else if (!strcmp("CDUP", cmd))
    {
        command_cwd(info, "..");
    }
    else if (!strcmp("PWD", cmd))
    {
        command_pwd(info);
    }
    else
    {
        wrong_command = 1;
    }

    if (wrong_command)
    {
        send_reply(info, 500, "Command not understood.");
    }
}

/*
 * session
 *
 * This task handles single session.  It is waked up when the FTP daemon gets a
 * service request from a remote machine.  Here, we watch for commands that
 * will come through the control connection.  These commands are then parsed
 * and executed until the connection is closed, either unintentionally or
 * intentionally with the "QUIT" command.
 *
 * Input parameters:
 *   arg - pointer to corresponding SessionInfo.
 *
 * Output parameters:
 *   NONE
 */
static void session(void *arg)
{
    FTPD_SessionInfo_t *const info = (FTPD_SessionInfo_t *)arg;
    int chroot_made = 0;

    /*
     * chroot() can fail here because the directory may not exist yet.
     */
    chroot_made = yaffs2_chroot(ftpd_root) == 0;

    while (1)
    {
        int rv;
        
#if defined(OS_RTTHREAD)
	    unsigned int recv = 0;
        rt_event_recv(info->event,
                      FTPD_CLIENT_EVENT,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER,
                      &recv);

        if (recv != FTPD_CLIENT_EVENT)
	        continue;
	        
#elif defined(OS_UCOS)
        unsigned char  err;
        unsigned short recv;
        recv = OSFlagPend(info->event,
                          (OS_FLAGS)FTPD_CLIENT_EVENT,
                          OS_FLAG_WAIT_SET_ALL |
                          OS_FLAG_CONSUME,
                          0,
                          &err);

        if (recv != FTPD_CLIENT_EVENT)
	        continue;
	        
#elif defined(OS_FREERTOS)
        unsigned int recv;
        recv = xEventGroupWaitBits(info->event,
                                   FTPD_CLIENT_EVENT,
                                   pdTRUE,
                                   pdTRUE,
                                   portMAX_DELAY);

        if (recv != FTPD_CLIENT_EVENT)
	        continue;
	        
#endif

        chroot_made = chroot_made || yaffs2_chroot(ftpd_root) == 0;
        rv = chroot_made ? yaffs2_chdir(info, "/") : -1;
        errno = 0;

        if (rv == 0)
        {
            send_reply(info, 220, FTPD_SERVER_MESSAGE);

            while (1)
            {
                char buf[FTPD_BUFSIZE];
                char *cmd, *opts, *args;

                if (lwip_read(info->ctrl_socket, buf, FTPD_BUFSIZE) == 0)
                {
                    FTPD_DBG("ftpd: Connection aborted.\r\n");
                    break;
                }

                split_command(buf, &cmd, &opts, &args);

                if (!strcmp("QUIT", cmd))
                {
                    send_reply(info, 221, "Goodbye.");
                    break;
                }
                else
                {
                    exec_command(info, cmd, args);
                }

                /*
                 * throw out 1 ms?
                 */
#if defined(OS_RTTHREAD)
                rt_thread_delay(1);
#elif defined(OS_UCOS)
                OSTimeDly(1);
#elif defined(OS_FREERTOS)
                vTaskDelay(1);
#endif
            }
        }
        else
        {
            send_reply(info, 421, "Service not available, closing control connection.");
        }

        /*
         * Go back to the root directory.  A use case is to release a current
         * directory in a mounted file system on dynamic media, e.g. USB stick.
         * The return value can be ignored since the next session will try do the
         * operation again and an error check is performed in this case.
         */
        yaffs2_chdir(info, "/");

        /* Close connection and put ourselves back into the task pool. */
        close_data_socket(info);
        close_stream(info);
        mem_free(info->user);
        mem_free(info->pass);
        task_pool_release(info);
    }
}

/*
 * daemon
 *
 * This task runs forever.  It waits for service requests on the FTP port
 * (port 21 by default).  When a request is received, it opens a new session
 * to handle those requests until the connection is closed.
 *
 * Input parameters:
 *   NONE
 *
 * Output parameters:
 *   NONE
 */

#if defined(OS_RTTHREAD)
static rt_thread_t daemon_thread = NULL;
#elif defined(OS_UCOS)
static unsigned char daemon_thread = 0;
static OS_STK ftpd_stack[DEFAULT_THREAD_STACKSIZE];
#elif defined(OS_FREERTOS)
static TaskHandle_t daemon_thread = NULL;
#endif

static void daemon(void *args)
{
    int                 s;
    socklen_t	        addrLen;
    struct sockaddr_in  addr;
    FTPD_SessionInfo_t *info = NULL;

    s = lwip_socket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        PRINTF("ftpd: Error creating socket: %s\r\n", serr());
    }

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(_ftpd_configuration.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

    if (0 > lwip_bind(s, (struct sockaddr *)&addr, sizeof(addr)))
    {
        PRINTF("ftpd: Error binding control socket: %s\r\n", serr());
    }
    else if (0 > lwip_listen(s, 1))
    {
        PRINTF("ftpd: Error listening on control socket: %s\r\n", serr());
    }
    else
    {
        while (1)
        {
            int ss;
            addrLen = sizeof(addr);

            ss = lwip_accept(s, (struct sockaddr *)&addr, &addrLen);
            
            if (0 > ss)
            {
                PRINTF("ftpd: Error accepting control connection: %s\r\n", serr());
            }
            else if (!set_socket_timeout(ss, ftpd_timeout))
            {
                close_socket(ss);
            }
            else
            {
                info = task_pool_obtain();
                if (NULL == info)
                {
                    close_socket(ss);
                }
                else
                {
                    info->ctrl_socket = ss;

                    /* Initialize corresponding SessionInfo structure */
                    info->def_addr = addr;
                    if (0 > lwip_getsockname(ss, (struct sockaddr *)&addr, &addrLen))
                    {
                        PRINTF("ftpd: getsockname(): %s\r\n", serr());
                        close_stream(info);
                        task_pool_release(info);
                    }
                    else
                    {
                        info->use_default = 1;
                        info->ctrl_addr  = addr;
                        info->pasv_socket = -1;
                        info->data_socket = -1;
                        info->xfer_mode   = TYPE_A;
                        info->data_addr.sin_port = htons(ntohs(info->ctrl_addr.sin_port) - 1);
                        info->idle = ftpd_timeout;
                        info->user = NULL;
                        info->pass = NULL;
                        if (_ftpd_configuration.login)
                            info->auth = false;
                        else
                            info->auth = true;
                            
                        /* Wakeup the session task.  The task will call task_pool_release
                           after it closes connection. */
#if defined(OS_RTTHREAD)
                        rt_event_send(info->event, FTPD_CLIENT_EVENT);
#elif defined(OS_UCOS)
                        unsigned char err;
                        OSFlagPost(info->event, (OS_FLAGS)FTPD_CLIENT_EVENT, OS_FLAG_SET, &err);
#elif defined(OS_FREERTOS)
                        xEventGroupSetBits(info->event, FTPD_CLIENT_EVENT);
#endif
                    }
                }
            }
        }
    }

#if defined(OS_RTTHREAD)
    rt_thread_delete(daemon_thread);
#elif defined(OS_UCOS)
    OSTaskDel(daemon_thread);
#elif defined(OS_FREERTOS)
    vTaskDelete(daemon_thread);
#endif
}

/*
 * lwip_initialize_ftpd
 *
 * Here, we start the FTPD task which waits for FTP requests and services
 * them.  This procedure returns to its caller once the task is started.
 *
 *
 * Input parameters:
 *
 * Output parameters:
 *    returns 0 on successful start of the daemon.
 */
int lwip_initialize_ftpd(void)
{
    int priority;
    int count;

    if (_ftpd_configuration.port == 0)
    {
        _ftpd_configuration.port = FTPD_CONTROL_PORT;
    }

    if (_ftpd_configuration.priority == 0)
    {
        _ftpd_configuration.priority = 40;
    }
    priority = _ftpd_configuration.priority;

    ftpd_timeout = _ftpd_configuration.idle;
    if (ftpd_timeout < 0)
        ftpd_timeout = 0;
    _ftpd_configuration.idle = ftpd_timeout;

    ftpd_access = _ftpd_configuration.access;

    ftpd_root = "/";
    if (_ftpd_configuration.root && _ftpd_configuration.root[0] == '/' )
        ftpd_root = _ftpd_configuration.root;

    _ftpd_configuration.root = ftpd_root;

    if (_ftpd_configuration.tasks_count <= 0)
        _ftpd_configuration.tasks_count = 1;
    count = _ftpd_configuration.tasks_count;

    if (!task_pool_init(count, priority))
    {
        PRINTF("ftpd: Could not initialize task pool.\r\n");
        return -1;
    }

#if defined(OS_RTTHREAD)
    daemon_thread = rt_thread_create("FTPD",
                                     daemon,
                                     NULL,
                                     DEFAULT_THREAD_STACKSIZE * 4,
                                     priority,
                                     10);

#elif defined(OS_UCOS)
    unsigned char err;

    err = OSTaskCreate(daemon,
                       NULL,
                    #if OS_STK_GROWTH == 1
                       (void *)&ftpd_stack[DEFAULT_THREAD_STACKSIZE - 1],
                    #else
                       (void *)&ftpd_stack[0],
                    #endif
                       priority);

    if (OS_ERR_NONE == err)
        daemon_thread = priority;

#elif defined(OS_FREERTOS)
    xTaskCreate(daemon,
                "FTPD",
                DEFAULT_THREAD_STACKSIZE,
                NULL,
                priority,
                &daemon_thread);

#endif

#if defined(OS_UCOS)
    if (0 == daemon_thread)
#else
    if (NULL == daemon_thread)
#endif
    {
        task_pool_done(count);
        PRINTF("ftpd: Could not create/start FTP daemon: %i\r\n", serr());
        return -1;
    }

#if defined(OS_RTTHREAD)
    rt_thread_startup(daemon_thread);
#endif

    PRINTF("ftpd: FTP daemon started (%d session%s max)\r\n",
            count, ((count > 1) ? "s" : ""));

    return 0;
}

#endif // #ifdef USE_FTPD



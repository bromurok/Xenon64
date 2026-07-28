/*
 * osal_mkdirp no se usa en este build y se omite.
 */

#include "files.h"
#include <string.h>
#include <stdio.h>
#include <xtl.h>

int osal_mkdirp(const char *dirpath, int mode)
{
    (void)mode;
    (void)dirpath;
    return 0;
}

const char * osal_get_shared_filepath(const char *filename, const char *firstsearch, const char *secondsearch)
{
        static char buffer[512];
        (void)firstsearch;
        (void)secondsearch;
        _snprintf(buffer, sizeof(buffer), "game:\\%s", filename);
        return buffer;
}

const char * osal_get_user_configpath(void)
{
        static const char *path = "game:\\mupen64-360\\";
        osal_mkdirp(path, 0700);
        return path;
}

const char * osal_get_user_datapath(void)
{
        return osal_get_user_configpath();
}

const char * osal_get_user_cachepath(void)
{
        static const char *path = "game:\\mupen64-360\\cache\\";
        osal_mkdirp(path, 0700);
        return path;
}

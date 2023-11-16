//
//  log.c
//  usbmuxd2
//
//  Created by tihmstar on 17.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "log.h"
#include <stdarg.h>
#include <libgeneral/macros.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>

static int log_syslog = 0;

#ifdef DEBUG
enum loglevel log_level = LL_DEBUG;
#else
enum loglevel log_level = LL_INFO;
#endif

void log_enable_syslog(void){
    if (!log_syslog) {
        openlog("usbmuxd", LOG_PID, 0);
        log_syslog = 1;
    }
}

void log_disable_syslog(void){
    if (log_syslog) {
        closelog();
        log_syslog = 0;
    }
}

static int level_to_syslog_level(int level){
    int result = level + LOG_CRIT;
    if (result > LOG_DEBUG) {
        result = LOG_DEBUG;
    }
    return result;
}

void usbmuxd_log(enum loglevel level, const char *fmt, ...){
    int err = 0;
    va_list ap;
    char *fs = NULL;
    
    
    //don't log if below log level. Note: this is not an error
    cassure(level <= log_level);
    
    cassure(fs = malloc(20 + strlen(fmt)));
    
    
    if(log_syslog) {
        sprintf(fs, "[%d] %s\n", level, fmt);
    } else {
        struct timeval ts = {};
        struct tm *tp = NULL;
        gettimeofday(&ts, NULL);
        tp = localtime(&ts.tv_sec);
        strftime(fs, 10, "[%H:%M:%S", tp);
        sprintf(fs+9, ".%03d][%d] %s\n", (int)(ts.tv_usec / 1000), level, fmt);
    }
    
    va_start(ap, fmt);
    if (log_syslog) {
        vsyslog(level_to_syslog_level(level), fs, ap);
    }else{
        vfprintf((level > LL_WARNING) ? stdout : stderr, fs,ap);
    }
    va_end(ap);
    
    
error:
    safeFree(fs);
    return;
}

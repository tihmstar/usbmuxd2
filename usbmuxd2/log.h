//
//  log.h
//  usbmuxd2
//
//  Created by tihmstar on 17.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef log_h
#define log_h


#ifdef __cplusplus
extern "C"{
#endif
    
#include <stdio.h>
    
#define notice(a ...) usbmuxd_log(LL_NOTICE,a)
#define info(a ...) usbmuxd_log(LL_INFO,a)
#define warning(a ...) usbmuxd_log(LL_WARNING,a)
#define error(a ...) usbmuxd_log(LL_ERROR,a)
#define fatal(a ...) usbmuxd_log(LL_FATAL,a)
    
#ifdef DEBUG
#   define debug(a ...) usbmuxd_log(LL_DEBUG,a)
#else
#   define debug(a ...)
#endif
    
    enum loglevel {
        LL_FATAL = 0,
        LL_ERROR,
        LL_WARNING,
        LL_INFO,
        LL_NOTICE,
        LL_DEBUG
    };
    
    extern unsigned int log_level;
    
    void log_enable_syslog(void);
    void log_disable_syslog(void);
    
    void usbmuxd_log(enum loglevel level, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
    
    
#ifdef __cplusplus
}
#endif
#endif /* log_h */

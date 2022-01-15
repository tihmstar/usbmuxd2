//
//  main.cpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include <iostream>
#include <libgeneral/macros.h>
#include "Muxer.hpp"
#include <future>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include "sysconf/sysconf.hpp"
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>

extern "C"{
#ifdef HAVE_LIBIMOBILEDEVICE
#include <libimobiledevice/libimobiledevice.h>
#endif
};

#undef error //errors will be printed as fatal for this file
#define error(a ...) usbmuxd_log(LL_FATAL,a)


static const char *lockfile = "/var/run/usbmuxd.pid";

static pthread_mutex_t mlck = {};
static Config *gConfig = nullptr;
static Muxer *mux = nullptr;
static int exit_signal = 0;
static int report_to_parent = 0;
static int daemon_pipe = 0;

#ifdef DEBUG
static int verbose = LL_DEBUG;
#else
static int verbose = 0;
#endif


static void handle_signal(int sig) noexcept{
    int err = 0;
    static int ctrlcCounter = 0;
    if (sig != SIGUSR1 && sig != SIGUSR2) {
        info("Caught signal %d, exiting", sig);
        if (ctrlcCounter++ == 5){
            fatal("forcefully terminating program!");
            exit(2);
        }
        cassure(!pthread_mutex_unlock(&mlck));
    }else{
        if(gConfig->enableExit) {
            if (sig == SIGUSR1) {
                info("Caught SIGUSR1, checking if we can terminate (no more devices attached)...");
                if (mux->devices_cnt() > 0) {
                    // we can't quit, there are still devices attached.
                    notice("Refusing to terminate, there are still devices attached. Kill me with signal 15 (TERM) to force quit.");
                } else {
                    // it's safe to quit
                    info("No more devices attached, exiting!");
                    cassure(!pthread_mutex_unlock(&mlck));
                }
            }
        } else {
            info("Caught SIGUSR1/2 but this instance was not started with \"--enable-exit\", ignoring.");
        }
    }
    return;
error:
    fatal("failed to properly handle signal! Terminating!!");
    exit(1);
}

static void set_signal_handlers(void){
    assure(signal(SIGINT, handle_signal)  != SIG_ERR);
    assure(signal(SIGQUIT, handle_signal) != SIG_ERR);
    assure(signal(SIGTERM, handle_signal) != SIG_ERR);
    assure(signal(SIGUSR1, handle_signal) != SIG_ERR);
    assure(signal(SIGUSR2, handle_signal) != SIG_ERR);

    assure(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
}


/**
 * make this program run detached from the current console
 */
static int daemonize(void) noexcept{
    int err = 0;
    ssize_t res = 0;
    pid_t pid = 0;
    pid_t sid = 0;
    int pfd[2] = {};

    // already a daemon
    if (getppid() == 1)
        return 0;

    cretassure(!(res = pipe(pfd)), "pipe() failed");
    cretassure((pid = fork()) >=0, "fork() failed");

    if (pid > 0) {
        // exit parent process
        int status;
        close(pfd[1]);

        if((res = read(pfd[0],&status,sizeof(int))) != sizeof(int)) {
            fprintf(stderr, "usbmuxd: ERROR: Failed to get init status from child, check syslog for messages.\n");
            exit(1);
        }
        if(status != 0)
            fprintf(stderr, "usbmuxd: ERROR: Child process exited with error %d, check syslog for messages.\n", status);
        exit(status);
    }
    // At this point we are executing as the child process
    // but we need to do one more fork

    daemon_pipe = pfd[1];
    close(pfd[0]);
    report_to_parent = 1;

    // Create a new SID for the child process
    cretassure((sid = setsid()) >=0, "setsid() failed");
    
    cretassure((pid = fork()) >=0, "fork() failed (second)");

    if (pid > 0) {
        // exit parent process
        close(daemon_pipe);
        exit(0);
    }

    cretassure((res = chdir("/")) >=0, "chdir() failed");

    cretassure(freopen("/dev/null", "r", stdin), "Redirection of stdin failed");
    cretassure(freopen("/dev/null", "r", stdout), "Redirection of stdout failed");

error:
    return -err;
}


static void notify_parent(int status){
    report_to_parent = 0;
    assure(write(daemon_pipe, &status, sizeof(int)) == sizeof(int));
    close(daemon_pipe);
    retassure(freopen("/dev/null", "w", stderr),"Redirection of stderr failed.");
}

static void usage(){
    printf("Usage: %s [OPTIONS]\n", PACKAGE_NAME);
    printf("Expose a socket to multiplex connections from and to iOS devices.\n\n");
    printf("  -h, --help\t\tPrint this message.\n");
    printf("  -v, --verbose\t\tBe verbose (use twice or more to increase).\n");
    printf("  -V, --version\t\tPrint version information and exit.\n");
    printf("  -d, --daemonize\tDo daemonize\n");
    printf("  -U, --user USER\tChange to this user after startup (needs USB privileges).\n");
    printf("  -z, --enable-exit\tEnable \"--exit\" request from other instances and exit\n");
    printf("                   \tautomatically if no device is attached.\n");
#ifdef WANT_SYSTEMD
    printf("  -s, --systemd\t\tRun in systemd operation mode (implies -z and -f).\n");
#endif
    printf("  -x, --exit\t\tNotify a running instance to exit if there are no devices\n");
    printf("            \t\tconnected (sends SIGUSR1 to running instance) and exit.\n");
    printf("  -X, --force-exit\tNotify a running instance to exit even if there are still\n");
    printf("                  \tdevices connected (always works) and exit.\n");
    printf("  -l, --logfile=LOGFILE\tLog (append) to LOGFILE instead of stderr or syslog.\n");
    printf("      --nowifi\t do not start WIFIDeviceManager\n");
    printf("      --nousb\t do not start USBDeviceManager\n");
    printf("      --debug\t enable debug logging\n");
    printf("\n");
}



static void parse_opts(int argc, const char **argv){
    static struct option longopts[] = {
        {"help", no_argument, NULL, 'h'},
        {"verbose", no_argument, NULL, 'v'},
        {"version", no_argument, NULL, 'V'},
        {"daemonize", no_argument, NULL, 'd'},
        {"enable-exit", no_argument, NULL, 'z'},
#ifdef WANT_SYSTEMD
        {"systemd", no_argument, NULL, 's'},
#endif
        {"exit", no_argument, NULL, 'x'},
        {"force-exit", no_argument, NULL, 'X'},
        {"logfile", required_argument, NULL, 'l'},
        {"user", required_argument, NULL, 'U'},
        {"nowifi", optional_argument, NULL, '0'},
        {"nousb", optional_argument, NULL, '1'},
        {"debug", no_argument, NULL, 2},
        {NULL, 0, NULL, 0}
    };
    int c;

#ifdef WANT_SYSTEMD
    const char* opts_spec = "hvVdzsxXl:U:";
#else
    const char* opts_spec = "hvVdzxXl:U:";
#endif
    
    while (1) {
        c = getopt_long(argc, (char*const*)argv, opts_spec, longopts, (int *) 0);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage();
            exit(0);
        case 'd':
            gConfig->daemonize = true;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            printf("%s\n", VERSION_STRING);
            exit(0);
        case 'U':
            gConfig->dropUser = optarg;
            break;
#ifdef WANT_SYSTEMD
        case 's':
            gConfig->enableExit = true;
            gConfig->daemonize = false;
            break;
#endif
        case 'z':
            gConfig->enableExit = true;
            break;
        case '0': //nowifi
            info("Manually disableing WIFIDeviceManager");
            gConfig->enableWifiDeviceManager = (!optarg) ? false : atoi(optarg);
            break;
        case '1': //nousb
            info("Manually disableing USBDeviceManager");
            gConfig->enableUSBDeviceManager = (!optarg) ? false : atoi(optarg);
            break;
        case 'x':
            exit_signal = SIGUSR1;
            break;
        case 'X':
            exit_signal = SIGTERM;
            break;
        case 'l':
            if (!*optarg) {
                fatal("ERROR: --logfile requires a non-empty filename");
                usage();
                exit(2);
            }
            if (gConfig->useLogfile) {
                fatal("ERROR: --logfile cannot be used multiple times");
                exit(2);
            }
            if (freopen(optarg, "a", stderr)) {
                fatal("ERROR: fdreopen: %s", strerror(errno));
            } else {
                gConfig->useLogfile = 1;
            }
            break;
        case 2: //debug
                gConfig->debugLevel++;
                break;
        default:
            usage();
            exit(2);
        }
    }
}


int main(int argc, const char * argv[]) {
    int err = 0;
    int lfd = -1;
    struct flock lock = {};

    
    info("starting %s", VERSION_STRING);
    cassure(!pthread_mutex_init(&mlck, NULL));
    cassure(!pthread_mutex_lock(&mlck));
    debug("mlck inited");

    gConfig = new Config();
    try{
        gConfig->load();
    }catch(tihmstar::exception &e){
        fatal("Could not load config with error=%d (%s)",e.code(),e.what());
        creterror("failed to load config!");
    }

    parse_opts(argc,argv);

    if (gConfig->debugLevel) {
        info("debuglevel set to %d",gConfig->debugLevel);
#ifdef HAVE_LIBIMOBILEDEVICE
        idevice_set_debug_level(gConfig->debugLevel);
#endif
    }
    
    if (gConfig->daemonize && !gConfig->useLogfile) {
        verbose += LL_INFO;
        debug("enabling syslog");
        log_enable_syslog();
    } else {
        verbose += LL_NOTICE;
    }

    // set log level to specified verbosity
    log_level = verbose;
    info("starting %s", VERSION_STRING);


    {
        // set number of file descriptors to higher value
        struct rlimit rlim;
        getrlimit(RLIMIT_NOFILE, &rlim);
        rlim.rlim_max = 65536;
        setrlimit(RLIMIT_NOFILE, (const struct rlimit*)&rlim);   
    }
    set_signal_handlers();

    cretassure((lfd = open(lockfile, O_RDONLY|O_CREAT, 0644)) != -1, "Could not open lockfile");
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = 0;
    fcntl(lfd, F_GETLK, &lock);
    close(lfd); lfd = -1;

    if (lock.l_type != F_UNLCK) {
        cretassure(exit_signal,"Another instance is already running (pid %d). exiting.", lock.l_pid);

        cretassure(lock.l_pid,"Could not determine pid of the other running instance!");

        notice("Sending signal %d to instance with pid %d", exit_signal, lock.l_pid);
        cretassure(!kill(lock.l_pid, exit_signal),"Could not deliver signal %d to pid %d", exit_signal, lock.l_pid);
        goto error;
    }
    unlink(lockfile);

    cretassure(!exit_signal,"No running instance found, none killed. Exiting.");


    if (gConfig->daemonize) {
        if (daemonize() < 0) {
            fprintf(stderr, "usbmuxd: FATAL: Could not daemonize!\n");
            creterror("Could not daemonize!");
        }
    }

    cretassure((lfd = open(lockfile, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644)) != -1, "Could not open lockfile");
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    cretassure(fcntl(lfd, F_SETLK, &lock) >=0, "Lockfile locking failed!");
    
    {
        char pids[10];
        cassure(snprintf(pids, sizeof(pids), "%d", getpid()) < sizeof(pids));
        cretassure(write(lfd, pids, strlen(pids)) == strlen(pids), "Could not write pidfile!");
    }

    if (!gConfig->doPreflight){
        info("Preflight disabled by config!");
    }
    
    //starting    
    mux = new Muxer(gConfig->doPreflight);

    try{
        mux->spawnClientManager();
        info("Inited ClientManager");
    }catch (tihmstar::exception &e){
        fatal("failed to spawnClientManager with error=%d (%s)",e.code(),e.what());
        fatal("Terminating since a ClientManager is require to operate");
        cassure(0);
    }
    

    // drop elevated privileges
    if (gConfig->dropUser.size() && (getuid() == 0 || geteuid() == 0)) {
        struct passwd *pw = NULL; // don't free this
        
        cretassure(pw = getpwnam(gConfig->dropUser.c_str()), "Dropping privileges failed, check if user '%s' exists!", gConfig->dropUser.c_str());

        if (pw->pw_uid == 0) {
            info("Not dropping privileges to root");
        } else {
            try{
                sysconf_fix_permissions(pw->pw_uid, pw->pw_gid);
            }catch (tihmstar::exception &e){
                creterror("failed to fix permissions with error=%d (%s)",e.code(),e.what());
            }

            cretassure(initgroups(gConfig->dropUser.c_str(), pw->pw_gid) >=0, "Failed to drop privileges (cannot set supplementary groups)");
            
            cretassure(setgid(pw->pw_gid) >=0, "Failed to drop privileges (cannot set group ID to %d)", pw->pw_gid);
            
            cretassure(setuid(pw->pw_uid) >=0, "Failed to drop privileges (cannot set user ID to %d)", pw->pw_uid);

            cretassure(setuid(0) == -1, "Failed to drop privileges properly!");

            cretassure(getuid() == pw->pw_uid || getgid() == pw->pw_gid, "Failed to drop privileges properly!");
            notice("Successfully dropped privileges to '%s'", gConfig->dropUser.c_str());
        }
    }

    if (gConfig->enableUSBDeviceManager){
        try{
            mux->spawnUSBDeviceManager();
            info("Inited USBDeviceManager");
        }catch (tihmstar::exception &e){
            fatal("failed to spawnUSBDeviceManager with error=%d (%s)",e.code(),e.what());
        }
    }

    if (gConfig->enableWifiDeviceManager){
        try{
            mux->spawnWIFIDeviceManager();
            info("Inited WIFIDeviceManager");
        }catch (tihmstar::exception &e){
            fatal("failed to spawnWIFIDeviceManager with error=%d (%s)",e.code(),e.what());
        }
    }
    
    if (!mux->hasDeviceManager()){
        fatal("failed to spawn any DeviceManager");
        fatal("Terminating since at least one DeviceManager is require to operate");
        cassure(0);
    }
    

    notice("Initialization complete");
    if (report_to_parent){
        try{
            notify_parent(0);
        }catch(tihmstar::exception &e){
            creterror("notify_parent failed with error=%d (%s)",e.code(),e.what());
        }
    }

    if (gConfig->enableExit) {
        notice("Enabled exit on SIGUSR1 if no devices are attached. Start a new instance with \"--exit\" to trigger.");
    }


    //block thread
    pthread_mutex_lock(&mlck);

error:
    if (err){
        if (report_to_parent){
            try{
                notify_parent(0);
            }catch(tihmstar::exception &e){
                creterror("notify_parent failed with error=%d (%s)",e.code(),e.what());
            }
        }
    }
    notice("main reached cleanup");
    if (mux){
        delete mux;
    }
    if (gConfig){
        Config *cfg = gConfig; gConfig = nullptr;
        delete cfg;
    }
    if (lfd > 0){
        close(lfd);
    }
    notice("done!");
    return err;
}

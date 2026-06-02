//
//  libc_replacement.c
//  ios_system
//
//  Created by Nicolas Holzschuch on 30/04/2018.
//  Copyright © 2018 Nicolas Holzschuch. All rights reserved.
//
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <dlfcn.h>  // for dlopen()/dlsym()/dlclose()

#include "ios_error.h"
#undef write
#undef fwrite
#undef puts
#undef fputs
#undef fputc
#undef putw
#undef putp
#undef fflush
#undef getenv
#undef setenv
#undef unsetenv

// in order to run webAssembly commands sequentially, we first stack them, then run them in command line order:
// At this point, this could just be a mutex.

int preparingWebAssemblyCommands = 0;
int orderOfWebAssemblyCommands = 0;
void startedPreparingWebAssemblyCommand(void) {
    preparingWebAssemblyCommands += 1;
    orderOfWebAssemblyCommands += 1;
}

int webAssemblyCommandOrder(void) {
    return orderOfWebAssemblyCommands;
}

void finishedPreparingWebAssemblyCommand(void) {
    if (preparingWebAssemblyCommands > 0)
        preparingWebAssemblyCommands -= 1;
}

static void executeWebAssemblyCommandsInOrder(void) {
    static void (*function)(void) = NULL;
    if (function == NULL) {
        function = dlsym(RTLD_MAIN_ONLY, "executeWebAssemblyCommands");
    }
    if (function == NULL) {
        // more extensive search, but more expensive too.
        function = dlsym(RTLD_DEFAULT, "executeWebAssemblyCommands");
    }
    if (function != NULL) {
        while (preparingWebAssemblyCommands > 0) {
            // Empty loops create problems in Release mode.
            if (thread_stdout != NULL) { fflush(thread_stdout); }
            if (thread_stderr != NULL) { fflush(thread_stderr); }
        }
        function();
    }
    orderOfWebAssemblyCommands = 0;
}


int printf (const char *format, ...) {
    va_list arg;
    int done;
    
    va_start (arg, format);
    done = vfprintf (thread_stdout, format, arg);
    va_end (arg);
    
    return done;
}

// #define debugPrint
int fprintf(FILE * restrict stream, const char * restrict format, ...) {
    va_list arg;
    int done;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (thread_stdout == NULL) thread_stdout = stdout;

    va_start (arg, format);
    if (fileno(stream) == STDOUT_FILENO) done = vfprintf (thread_stdout, format, arg);
#ifndef debugPrint
    else if (fileno(stream) == STDERR_FILENO) done = vfprintf (thread_stderr, format, arg);
#else
    // iOS, debug:
    else if ((fileno(stream) == STDERR_FILENO) || (fileno(stream) == fileno(thread_stderr))) done = vfprintf (stderr, format, arg);
#endif
    else done = vfprintf (stream, format, arg);
    va_end (arg);
    
    return done;
}
int scanf (const char *format, ...) {
    int             count;
    va_list ap;
    
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stdin == NULL) thread_stdin = stdin;

    fflush(thread_stdout);
    fflush(thread_stderr);
    va_start (ap, format);
    count = vfscanf (thread_stdin, format, ap);
    va_end (ap);
    return (count);
}
int ios_fflush(FILE *stream) {
    if (stream == NULL) return 0;
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;

    if (fileno(stream) == STDOUT_FILENO) return fflush(thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fflush(thread_stderr);
    return fflush(stream);
}
ssize_t ios_write(int fildes, const void *buf, size_t nbyte) {
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fildes == STDOUT_FILENO) return write(fileno(thread_stdout), buf, nbyte);
    if (fildes == STDERR_FILENO) return write(fileno(thread_stderr), buf, nbyte);
    return write(fildes, buf, nbyte);
}
size_t ios_fwrite(const void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
#ifdef debugPrint
    return fwrite(ptr, size, nitems, stderr);
#endif
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fwrite(ptr, size, nitems, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fwrite(ptr, size, nitems, thread_stderr);
    return fwrite(ptr, size, nitems, stream);
}
int ios_puts(const char *s) {
    if (thread_stdout == NULL) thread_stdout = stdout;
    // puts adds a newline at the end.
    int returnValue = fputs(s, thread_stdout);
    fputc('\n', thread_stdout);
    return returnValue;
}
int ios_fputs(const char* s, FILE *stream) {
#ifdef debugPrint
    return fputs(s, stderr);
#endif
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fputs(s, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fputs(s, thread_stderr);
    return fputs(s, stream);
}
int ios_fputc(int c, FILE *stream) {
#ifdef debugPrint
    return fputc(c, stderr);
#endif
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fputc(c, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fputc(c, thread_stderr);
    return fputc(c, stream);
}

#include <assert.h>

int ios_putw(int w, FILE *stream) {
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return putw(w, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return putw(w, thread_stderr);
    return putw(w, stream);
}

// Fake process IDs to go with fake forking:
// You will still need to edit your code to make sure you go through both branches.
#define IOS_MAX_THREADS 128
static pthread_t thread_ids[IOS_MAX_THREADS];
static int numVariablesSet[IOS_MAX_THREADS];
static char** environment[IOS_MAX_THREADS];
static char** copyEnvironment[IOS_MAX_THREADS];
static char previousDirectory[IOS_MAX_THREADS][MAXPATHLEN];
static int previousPid[IOS_MAX_THREADS];

static int pid_overflow = 0;
static pid_t current_pid = 0;
// We need to lock current_pid during operations
pthread_mutex_t pid_mtx = PTHREAD_MUTEX_INITIALIZER;
_Atomic(int) cleanup_counter = 0;
static pid_t last_allocated_pid = 0;


void makeGlobal(void) {
    copyEnvironment[current_pid] = environment[current_pid];
    environment[current_pid] = NULL; // makes it really global
}
void makeLocal(void) {
    environment[current_pid] = copyEnvironment[current_pid];
    copyEnvironment[current_pid] = NULL;
}

inline pthread_t ios_getThreadId(pid_t pid) {
    // return ios_getLastThreadId(); // previous behaviour
    if (pid >= IOS_MAX_THREADS) { return -1; }
    return thread_ids[pid];
}

void newPreviousDirectory(void) {
    // Called when a command calls "cd". Actually changes the directory for that command.
    getwd(previousDirectory[current_pid]);
}

// We do not recycle process ids too quickly to avoid collisions.
void storeEnvironment(char* envp[]);
static inline const pid_t ios_nextAvailablePid(void) {
    while (cleanup_counter > 0) { } // Don't start a command while another is ending.
    // fprintf(stderr, "Locking in ios_nextAvailablePid\n");
    pthread_mutex_lock(&pid_mtx);
    char** currentEnvironment = environmentVariables(current_pid);
    int previousPidId = current_pid;
    if (!pid_overflow && (last_allocated_pid < IOS_MAX_THREADS - 1)
        && (thread_ids[last_allocated_pid+1] == 0)) {
        current_pid = last_allocated_pid + 1;
        last_allocated_pid = current_pid;
        thread_ids[current_pid] = -1; // Not yet started
        numVariablesSet[current_pid] = 0;
        environment[current_pid] = NULL;
        storeEnvironment(currentEnvironment); // duplicate the environment variables
        getwd(previousDirectory[current_pid]); // store current working directory
        previousPid[current_pid] = previousPidId;
        // fprintf(stderr, "Returning from ios_nextAvailablePid, pid= %d\n", current_pid);
        return current_pid;
    }
    // We've already started more than IOS_MAX_THREADS threads.
    if (!pid_overflow) current_pid = 0; // first time over the limit
    pid_overflow = 1;
    while (1) {
        current_pid = last_allocated_pid + 1;
        last_allocated_pid = current_pid;
        if (current_pid >= IOS_MAX_THREADS) {
            current_pid = 1;
            last_allocated_pid = 1;
        }
        pthread_t thread_pid = ios_getThreadId(current_pid);
        if (thread_pid == 0) { // We found a not-active pid
            thread_ids[current_pid] = -1; // Not yet started
            numVariablesSet[current_pid] = 0;
            environment[current_pid] = NULL;
            storeEnvironment(currentEnvironment); // duplicate the environment variables
            getwd(previousDirectory[current_pid]); // store current working directory
            previousPid[current_pid] = previousPidId;
            // fprintf(stderr, "Returning from ios_nextAvailablePid, pid= %d\n", current_pid);
            return current_pid;
        }
        // Dangerous: if the process is already killed, this wil crash
        /*
        if (pthread_kill(thread_pid, 0) != 0) {
            thread_ids[current_pid] = 0;
            return current_pid; // not running anymore
        }
        */
    }
}

inline void ios_storeThreadId(pthread_t thread) {
    // To avoid issues when a command starts a command without forking,
    // we only store thread IDs for the first thread of the "process".
    // fprintf(stderr, "Unlocking pid %d, storing thread %x current value: %x\n", current_pid, thread,  thread_ids[current_pid]);
    if (thread == NULL) {
        // Callers in ios_system.m use `ios_storeThreadId(0)` on early-out
        // error paths (empty argv, popen failure, NULL command, "command not
        // found") AFTER `ios_nextAvailablePid` has already allocated a pid
        // and bumped `current_pid` + `last_allocated_pid`. The legacy
        // behavior just wrote `thread_ids[current_pid] = 0`, which left the
        // pid in a half-released state: the slot was no longer findable by
        // `ios_releaseThread` (because no real thread was ever stored, and
        // -1 / NULL don't match a live pthread_t), but `current_pid` was
        // never restored to the parent. Every subsequent
        // `ios_nextAvailablePid` call inherits this orphaned `current_pid`
        // as `previousPidId`, snapshots `environment[current_pid]` instead
        // of `environ`, and the host's libc setenv() updates become
        // invisible to all future ios_system children — observable in the
        // embedding app as `printenv X` returning empty after the host
        // calls `setenv("X", ...)`.
        //
        // Fix: treat `ios_storeThreadId(NULL)` as an explicit release. Mark
        // the slot free (thread_ids = 0) and restore `current_pid` to the
        // parent pid that was active before this allocation, mirroring what
        // `ios_releaseThread` would do for a real thread.
        //
        // Guard the rewind on `thread_ids[current_pid] == -1`: that is the
        // exact "allocated by ios_nextAvailablePid, not yet started" marker.
        // Only such a pid was just handed out under the lock we are about to
        // release, so only then is rewinding current_pid to its parent
        // correct. Any other state (a real thread already stored, or an
        // already-freed 0 slot) means this is NOT a post-fork early-out, and
        // we must not clobber current_pid. The unlock below stays
        // unconditional to preserve the ios_nextAvailablePid/storeThreadId
        // lock pairing.
        if (thread_ids[current_pid] == -1) {
            thread_ids[current_pid] = 0;
            pid_t parent = previousPid[current_pid];
            if (parent >= 0 && parent < IOS_MAX_THREADS) {
                current_pid = parent;
            }
        }
        pthread_mutex_unlock(&pid_mtx);
        return;
    }
    if (thread_ids[current_pid] == -1) {
        thread_ids[current_pid] = thread;
    }
    pthread_mutex_unlock(&pid_mtx);
}

char* libc_getenv(const char* variableName) {
    if (environment[current_pid] != NULL) {
        if (variableName == NULL) { return NULL; }
        // fprintf(stderr, "libc_getenv: %s\n", variableName); fflush(stderr);
        char** envp = environment[current_pid];
        unsigned long varNameLen = strlen(variableName);
        if (varNameLen == 0) { return NULL; }
        for (int i = 0; i < numVariablesSet[current_pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if (strlen(envp[i]) < varNameLen) { continue; }
            if (strncmp(variableName, envp[i], varNameLen) == 0) {
                if (strlen(envp[i]) > varNameLen) {
                    if (envp[i][varNameLen] == '=') {
                        return envp[i] + varNameLen + 1;
                    }
                }
            }
            /*
            char* position = strchr(envp[i],'=');
            if (strncmp(variableName, envp[i], position - envp[i]) == 0) {
                char* value = position + 1;
                return value;
            }
             */
        }
        return NULL;
    } else {
        return getenv(variableName);
    }
}

extern void set_session_errno(int n);
int ios_setenv_pid(const char* variableName, const char* value, int overwrite, int pid) {
    if (environment[pid] != NULL) {
        if (variableName == NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        if (strlen(variableName) == 0) {
            set_session_errno(EINVAL);
            return -1;
        }
        char* position = strchr(variableName,'=');
        if (position != NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        char** envp = environment[pid];
        unsigned long varNameLen = strlen(variableName);
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if (strncmp(variableName, envp[i], varNameLen) == 0) {
                if (strlen(envp[i]) > varNameLen) {
                    if (envp[i][varNameLen] == '=') {
                        // This variable is defined in the current environment:
                        if (overwrite == 0) { return 0; }
                        envp[i] = realloc(envp[i], strlen(variableName) + strlen(value) + 2);
                        sprintf(envp[i], "%s=%s", variableName, value);
                        return 0;
                    }
                }
            }
        }
        // Not found so far, add it to the list:
        int pos = numVariablesSet[pid];
        environment[pid] = realloc(envp, (numVariablesSet[pid] + 2) * sizeof(char*));
        environment[pid][pos] = malloc(strlen(variableName) + strlen(value) + 2);
        environment[pid][pos + 1] = NULL;
        sprintf(environment[pid][pos], "%s=%s", variableName, value);
        numVariablesSet[pid] += 1;
        return 0;
    } else {
        return setenv(variableName, value, overwrite);
    }
}

int ios_setenv_parent(const char* variableName, const char* value, int overwrite) {
    return ios_setenv_pid(variableName, value, overwrite, previousPid[current_pid]);
}

int ios_setenv(const char* variableName, const char* value, int overwrite) {
    return ios_setenv_pid(variableName, value, overwrite, current_pid);
}

int ios_putenv(char* string) {
    if (environment[current_pid] != NULL) {
        unsigned length;
        char     *temp;

        /*  Find the length of the "NAME="  */
        temp = strchr(string,'=');
        if ( temp == 0 ) {
            set_session_errno(EINVAL);
            return( -1 );
        }
        length = (unsigned) (temp - string + 1);

        /*  Scan through the environment looking for "NAME="  */
        char** envp = environment[current_pid];

        for (int i = 0; i < numVariablesSet[current_pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if ( strncmp( string, envp[i], length ) == 0 ) {
                // Found it. Copy in place.
                envp[i] = realloc(envp[i], strlen(string) + 1);
                memcpy(envp[i], string, strlen(string) + 1);
                return 0;
            }
        }
        // Not found so far, add it to the list:
        int pos = numVariablesSet[current_pid];
        environment[current_pid] = realloc(envp, (numVariablesSet[current_pid] + 2) * sizeof(char*));
        environment[current_pid][pos] = malloc(strlen(string) + 1);
        environment[current_pid][pos + 1] = NULL;
        memcpy(environment[current_pid][pos], string, strlen(string) + 1);
        numVariablesSet[current_pid] += 1;
        return 0;
    } else {
        return putenv(string);
    }
}

int ios_unsetenv_pid(const char* variableName, int pid) {
    // Someone calls unsetenv once the process has been terminated.
    // Best thing to do is erase the environment and return
    if (environment[pid] != NULL) {
        if (variableName == NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        if (strlen(variableName) == 0) {
            set_session_errno(EINVAL);
            return -1;
        }
        char* position = strchr(variableName,'=');
        if (position != NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        char** envp = environment[pid];
        unsigned long varNameLen = strlen(variableName);
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if (strncmp(variableName, envp[i], varNameLen) == 0) {
                if (strlen(envp[i]) > varNameLen) {
                    if (envp[i][varNameLen] == '=') {
                        // This variable is defined in the current environment:
                        free(envp[i]);
                        envp[i] = NULL;
                        if (i < numVariablesSet[pid] - 1) {
                            for (int j = i; j < numVariablesSet[pid] - 1; j++) {
                                envp[j] = envp[j+1];
                            }
                            envp[numVariablesSet[pid] - 1] = NULL;
                        }
                        numVariablesSet[pid] -= 1;
                        environment[pid] = realloc(envp, (numVariablesSet[pid] + 1) * sizeof(char*));
                        return 0;
                    }
                }
            }
        }
        /*
         for (int i = 0; i < numVariablesSet[pid]; i++) {
         char* position = strstr(envp[i],"=");
         if (strncmp(variableName, envp[i], position - envp[i]) == 0) {
         }
         } */
        // Not found:
        return 0;
    } else {
        return unsetenv(variableName);
    }
}

int ios_unsetenv_parent(const char* variableName) {
    return ios_unsetenv_pid(variableName, previousPid[current_pid]);
}

int ios_unsetenv(const char* variableName) {
    return ios_unsetenv_pid(variableName, current_pid);
}


// store environment variables (called from execve)
// Copy the entire environment:
extern char** environ;
void resetEnvironment(pid_t pid);
void storeEnvironment(char* envp[]) {
    if (environment[current_pid] != NULL) {
        // We already allocated one environment. Let's clean it:
        resetEnvironment(current_pid);
    }
    int i = 0;
    while (envp[i] != NULL) {
        i++;
    }
    numVariablesSet[current_pid] = i;
    environment[current_pid] = malloc((numVariablesSet[current_pid] + 1) * sizeof(char*));
    for (int i = 0; i < numVariablesSet[current_pid]; i++) {
        if (envp[i] != NULL)
            environment[current_pid][i] = strdup(envp[i]);
        else
            environment[current_pid][i] = NULL;
    }
    // Keep NULL-termination:
    environment[current_pid][numVariablesSet[current_pid]] = NULL;
}

// when the command is terminated, release the environment variables that were added.
void resetEnvironment(pid_t pid) {
    if (environment[pid] != NULL) {
        // Free the variables allocated:
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (environment[pid][i] == NULL) { continue; }
            free(environment[pid][i]);
            environment[pid][i] = NULL;
        }
        free(environment[pid]);
        environment[pid] = NULL;
        numVariablesSet[pid] = 0;
    }
}

// Used by "env -i": clear all environment variables, but don't clear the environment itself
void clearEnvironment(pid_t pid) {
    if (environment[pid] != NULL) {
        // Free the variables allocated:
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (environment[pid][i] == NULL) { continue; }
            free(environment[pid][i]);
            environment[pid][i] = NULL;
        }
        numVariablesSet[pid] = 0;
    }
}


char** environmentVariables(pid_t pid) {
    // pid 0 represents the embedding host app's root context. Swift /
    // Objective-C code in the host updates the process environment via
    // libc setenv() (writing to the global `environ`). If we cached a
    // snapshot into environment[0] — which happens whenever sh's
    // fork-exec path runs `execve(...)` while current_pid == 0 (the
    // typical case for `||` / `&&` / `;` short-circuit operators in
    // user commands) — every subsequently spawned child pid would copy
    // that frozen snapshot via storeEnvironment, and host-side setenv
    // updates would be silently masked.
    //
    // Always pass through to `environ` for pid 0 so the host env and
    // ios_system's view stay in sync. In practice environment[0] stays NULL:
    // storeEnvironment() only ever writes the child pid that
    // ios_nextAvailablePid just allocated (always >= 1), so libc_getenv /
    // ios_setenv_pid at pid 0 — which read environment[current_pid] directly —
    // also fall through to environ. This pid==0 shortcut just makes that
    // invariant explicit for the snapshot path below.
    if (pid == 0) {
        return environ;
    }
    if (environment[pid] != NULL) {
        return environment[pid];
    } else {
        return environ;
    }
}

extern int chdir_nolock(const char* path); // defined in ios_system.m
void ios_releaseThread(pthread_t thread) {
    if (thread == NULL) {
        return;
    }
    // Iterate in REVERSE pid order (LIFO release discipline).
    //
    // Why: sh's mini-shell does NOT fork — when sh runs `echo a || echo b`
    // and dispatches the inner `echo a` through `execve` → `ios_execve` →
    // `ios_system`, the inner command runs on the **same OS thread** as
    // sh. Both sh's pid slot and echo's pid slot end up with
    // `thread_ids[p] == thread` simultaneously. The inner command's
    // cleanup fires first; with forward iteration we'd match sh's slot
    // (the lower pid number) instead of echo's, corrupting current_pid
    // and leaking an active-looking environment[pid] slot for echo that
    // outlives the command. Subsequent commands then read a frozen env
    // snapshot through that leaked slot and miss host-side setenv()
    // updates.
    //
    // Reverse iteration matches the most-recently-allocated pid first,
    // which under non-overflow conditions is always the child whose
    // cleanup is firing. After overflow (pid_overflow == 1) pids wrap
    // and strict LIFO is no longer guaranteed, but that path was
    // already broken; we don't make it worse.
    // TODO: this is inefficient. Replace with NSMutableArray?
    for (int p = IOS_MAX_THREADS - 1; p >= 0; p--) {
        if (thread_ids[p] == thread) {
            // fprintf(stderr, "Found Id %d\n", p);
            // Don't reset the environment; sometimes, commands try to change the environment while it is being erased.
            // resetEnvironment(p);
            // fprintf(stderr, "Reset current directory to %s because process %d terminates\n", previousDirectory[p], p);
            current_pid = previousPid[p];
            thread_ids[p] = NULL;
            chdir_nolock(previousDirectory[p]);
            return;
        }
    }
    // fprintf(stderr, "Not found\n");
}

void ios_releaseBackgroundThread(pthread_t thread) {
    // Same as ios_releaseThread, but do not reset the directory.
    // Same LIFO-release reasoning — see ios_releaseThread for the why.
    for (int p = IOS_MAX_THREADS - 1; p >= 0; p--) {
        if (thread_ids[p] == thread) {
            // fprintf(stderr, "Found Id %d\n", p);
            current_pid = previousPid[p];
            thread_ids[p] = NULL;
            return;
        }
    }
    // fprintf(stderr, "Not found\n");
}

void ios_releaseThreadId(pid_t pid) {
    // Don't reset the environment; sometimes, commands try to change the environment while it is being erased.
    // resetEnvironment(pid);
    if (thread_ids[pid] != 0) {
        // fprintf(stderr, "Locking for pid %d in ios_releaseThreadId\n", pid);
        // fprintf(stderr, "Reset current directory to %s because process %d terminates\n", previousDirectory[pid], pid);
        chdir_nolock(previousDirectory[pid]);
        current_pid = previousPid[pid];
        thread_ids[pid] = 0;
        // fprintf(stderr, "Unlocking for pid %d in ios_releaseThreadId\n", pid);
    } else {
        // fprintf(stderr, "ios_releaseThreadId: pid %d was already terminated.\n", pid);
    }
}

pid_t ios_currentPid(void) {
    return current_pid;
}

// Note to self: do not redefine getpid() unless you have a way to make it consistent even when a "process" starts a new thread.
// 0MQ and asyncio rely on this.
pid_t fork(void) { return ios_nextAvailablePid(); } // increases current_pid by 1.
pid_t ios_fork(void) { return ios_nextAvailablePid(); } // increases current_pid by 1.
pid_t vfork(void) { return ios_nextAvailablePid(); }

// simple replacement of waitpid for swift programs
// We use "optnone" to prevent optimization, otherwise the while loops never end.
__attribute__ ((optnone)) void ios_waitpid(pid_t pid) {
    
    executeWebAssemblyCommandsInOrder();
    
    pthread_t threadToWaitFor;
    // Old system: no explicit pid, just store last thread Id.
    if ((pid == -1) || (pid == 0)) {
        threadToWaitFor = ios_getLastThreadId();
        while (threadToWaitFor != 0) {
            threadToWaitFor = ios_getLastThreadId();
        }
        return;
    }
    // New system: thread Id is store with pid:
    threadToWaitFor = ios_getThreadId(pid);
    while (threadToWaitFor != 0) {
        // -1: not started, >0 started, not finished, 0: finished
        threadToWaitFor = ios_getThreadId(pid);
    }
    // fprintf(stderr, "Returning from ios_waitpid for %d \n", pid);
    return;
}

__attribute__ ((optnone)) pid_t waitpid(pid_t pid, int *stat_loc, int options) {
    // pthread_join won't work,  because the thread might have been detached.
    // (and you can't re-join a detached thread).
    // -1 = the call waits for any child process (not good yet)
    //  0 = the call waits for any child process in the process group of the caller
    
    if (options && WNOHANG) {
        executeWebAssemblyCommandsInOrder(); // start executing webAssembly commands
        // WNOHANG: just check that the process is still running:
        pthread_t threadToWaitFor;
        if ((pid == -1) || (pid == 0)) threadToWaitFor = ios_getLastThreadId();
        else threadToWaitFor = ios_getThreadId(pid);
        if (threadToWaitFor != 0) // the process is still running
            return 0;
        else {
            if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
            fflush(thread_stdout);
            fflush(thread_stderr);
            return pid; // was "-1". See man page and https://github.com/holzschu/ios_system/issues/89
        }
    } else {
        // Wait until the process is terminated:
        ios_waitpid(pid);
        if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
        return pid;
    }
}

// The previous function is designed to override the standard version of waitpid.
// With Python 3.13, it doesn't work and the standard version is called instead.
// When that happens, we explicitly call this function (a copy of the previous one).
// Also useful in Swift calls
__attribute__ ((optnone)) pid_t ios_full_waitpid(pid_t pid, int *stat_loc, int options) {
    // pthread_join won't work,  because the thread might have been detached.
    // (and you can't re-join a detached thread).
    // -1 = the call waits for any child process (not good yet)
    //  0 = the call waits for any child process in the process group of the caller
    
    if (options && WNOHANG) {
        executeWebAssemblyCommandsInOrder(); // start executing webAssembly commands
        // WNOHANG: just check that the process is still running:
        pthread_t threadToWaitFor;
        if ((pid == -1) || (pid == 0)) threadToWaitFor = ios_getLastThreadId();
        else threadToWaitFor = ios_getThreadId(pid);
        if (threadToWaitFor != 0) // the process is still running
            return 0;
        else {
            if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
            fflush(thread_stdout);
            fflush(thread_stderr);
            return pid; // was "-1". See man page and https://github.com/holzschu/ios_system/issues/89
        }
    } else {
        // Wait until the process is terminated:
        ios_waitpid(pid);
        if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
        return pid;
    }
}



//
void vwarn(const char *fmt, va_list args)
{
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    if (fmt != NULL)
    {
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, args);
    }
    fputs(": ", thread_stderr);
    fputs(strerror(errno), thread_stderr);
    putc('\n', thread_stderr);
}

void vwarnx(const char *fmt, va_list args)
{
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    fputs(": ", thread_stderr);
    if (fmt != NULL)
        vfprintf(thread_stderr, fmt, args);
    putc('\n', thread_stderr);
}
// void err(int eval, const char *fmt, ...);
void err(int eval, const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
    ios_exit(eval);
}
// void errc(int eval, int errorcode, const char *fmt, ...);
void errc(int eval, int errorcode, const char *fmt, ...) {
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        fputs(ios_progname(), thread_stderr);
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, argptr);
        fputs(": ", thread_stderr);
        fputs(strerror(errorcode), thread_stderr);
        putc('\n', thread_stderr);
        va_end(argptr);
    }
    ios_exit(eval);
}
//     void errx(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarnx(fmt, argptr);
        va_end(argptr);
    }
    ios_exit(eval);
}
//   void warn(const char *fmt, ...);
void warn(const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
}
//   void warnx(const char *fmt, ...);
void warnx(const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarnx(fmt, argptr);
        va_end(argptr);
    }
}
// void warnc(int code, const char *fmt, ...);
void warnc(int code, const char *fmt, ...) {
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    if (fmt != NULL)
    {
        va_list argptr;
        va_start(argptr, fmt);
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, argptr);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
    fputs(": ", thread_stderr);
    fputs(strerror(code), thread_stderr);
    putc('\n', thread_stderr);
}

// Copyright (C) 2010 Michael Homer <http://mwh.geek.nz>
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#define VERSION "0.2"

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <fnmatch.h>

#include <sys/inotify.h>
#define EVENT_SIZE (sizeof (struct inotify_event))
#define BUFLEN (1024 * (EVENT_SIZE + 16))

#include "util.h"

struct filechange {
    char *name;
    time_t first_change;
    time_t last_change;
    struct filechange *next;
};

strarray *files;

strarray *skips;

struct watch {
    int wd;
    char *path;
    struct watch *next;
};

struct watch *watches = NULL;

int verbose = 0;

enum bool {false, true};
enum SYNCTYPE {
    S_RSYNC,
    S_SCP,
    S_CUSTOM,
};

int debug = 0;

struct destination {
    int type;
    char *dest;
    int push_last_delay;
    int push_first_delay;
    struct filechange *changed;
    strarray *deleted;
    time_t deleted_time;
    int delete;
    char *delete_cmd;
    char *sync_complete_cmd;
    struct destination *next;
};

struct destination *dests = NULL;

int help() {
    puts("Usage: fiss [-f] [-V] [-d settletime] [-D safetytime] local dest [dest...]");
    puts("");
    puts("-f	\t\tRun in foreground with debugging output.");
    puts("-V	\t\tVerbose.");
    puts("-d N	\t\tSync file when last change more than N seconds ago (60).");
    puts("-D N	\t\tSync file if changed and not synced for N seconds (300).");
    puts("--scp	\t\tUse scp.");
    puts("--rsync	\t\tUse rsync.");
    puts("--custom	\tUse custom command, dest = CMD.");
    puts("--delete	\tForward deletions to server.");
    puts("--delete-cmd CMD	Use CMD as deletion command.");
    puts("--skip GLOB	\tSkip files matching GLOB.");
    puts("--clear-skips	\tClear the list of skipped patterns.");
    puts("--sync-complete-cmd CMD\tExecute CMD after a completed sync.");
    puts("--help	\t\tThis help text.");
    puts("");
    puts("Monitors a directory tree for changes and synchronises modified");
    puts("files with remote server. By default, uses rsync for the transfer.");
    puts("When multiple destinations are specified, each gets the settings");
    puts("most recently defined (left to right).");
    puts("");
    puts("CMD can include several pattern variables:");
    puts("  #p\tlocal path");
    puts("  #d\tdestination");
    puts("  #h\tpart of destination before first :");
    puts("  #r\tpart of destination after first :");
    puts("");
    puts("With --custom, dest is treated as a CMD pattern to be executed for");
    puts("each file. #d, #h, and #r do not make sense in this pattern.");
    return 0;
}

int version() {
    puts("fiss " VERSION " - daemon to synchronise tree");
    puts("Copyright (C) 2010 Michael Homer <http://mwh.geek.nz>");
    puts("This program comes with ABSOLUTELY NO WARRANTY.");
    puts("This is free software, and you are welcome to redistribute it under");
    puts("certain conditions; see LICENCE file in the source for details.");
    return 0;
}

void putchange(char *name) {
    time_t now = time(NULL);
    if (debug)
        printf("%i: %s\n", now, name+2);
    struct destination *d = dests;
    while (d != NULL) {
        if (d->changed == NULL) {
            d->changed = malloc(sizeof(struct filechange));
            d->changed->first_change = now;
            d->changed->last_change = now;
            d->changed->name = calloc(strlen(name)+1, 1);
            strcpy(d->changed->name, name);
            d->changed->next = NULL;
        } else {
            struct filechange *chng = d->changed;
            while (chng->next != NULL) {
                if (strcmp(name, chng->name) == 0) {
                    chng->last_change = now;
                    d = d->next;
                    continue;
                }
                chng = chng->next;
            }
            if (strcmp(name, chng->name) == 0) {
                chng->last_change = now;
                d = d->next;
                continue;
            }
            
            struct filechange *newch = malloc(sizeof(struct filechange));
            newch->name = calloc(strlen(name)+1, 1);
            strcpy(newch->name, name);
            newch->first_change = now;
            newch->last_change = now;
            newch->next = NULL;
            chng->next = newch;
        }
        d = d->next;
    }
}

void putchange_recursive(char *dir) {
    DIR *dp;
    struct dirent *ep;
    struct stat stat_struct;
    dp = opendir(dir);
    if (dp == NULL)
        return;
    while (ep = readdir (dp)) {
        if (strcmp(".", ep->d_name) == 0)
            continue;
        if (strcmp("..", ep->d_name) == 0)
            continue;
        if (stat(dir, &stat_struct))
            continue;
        char *npath = malloc(strlen(dir) + strlen(ep->d_name) + 2);
        npath[0] = 0;
        strcat(npath, dir);
        strcat(npath, "/");
        strcat(npath, ep->d_name);
        putchange(npath);
        if (S_ISDIR(stat_struct.st_mode)) {
            putchange_recursive(npath);
        }
        free(npath);
    }
    closedir (dp);
}

void add_watch(char *path, int inotify_fd) {
    int wd;
    if (have_watch(path))
        return;
    if (debug)
        printf("Watching dir %s\n", path + 2);
    int mask = IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE;
    wd = inotify_add_watch(inotify_fd, path, mask);
    struct watch *nw = malloc(sizeof(struct watch));
    nw->wd = wd;
    nw->path = strcop(path);
    nw->next = watches;
    watches = nw;
}

int have_watch(char *path) {
    struct watch *w = watches;
    while (w != NULL) {
        if (strcmp(w->path, path) == 0)
            return true;
        w = w->next;
    }
    return false;
}

void get_watch_buf(char *path, int wd) {
    struct watch *w = watches;
    path[0] = 0;
    while (w != NULL) {
        if (w->wd == wd) {
            strcat(path, w->path);
            return;
        }
        w = w->next;
    }
}

void watch_dirs(char *dir, int inotify_fd) {
    DIR *dp;
    struct dirent *ep;
    struct stat stat_struct;
    dp = opendir(dir);
    if (dp == NULL)
        return;
    add_watch(dir, inotify_fd);
    while (ep = readdir (dp)) {
        if (strcmp(".", ep->d_name) == 0)
            continue;
        if (strcmp("..", ep->d_name) == 0)
            continue;
        if (stat(dir, &stat_struct))
            continue;
        if (S_ISDIR(stat_struct.st_mode)) {
            char *npath = malloc(strlen(dir) + strlen(ep->d_name) + 2);
            npath[0] = 0;
            strcat(npath, dir);
            strcat(npath, "/");
            strcat(npath, ep->d_name);
            watch_dirs(npath, inotify_fd);
            free(npath);
        }
    }
    closedir (dp);
}

void handle(char *name) {
    struct stat stat_struct;
    if (stat(name, &stat_struct))
        return;
    strarray_push(files, name);
}

void fillpattern(char *buf, char *path, char *pat, struct destination *d) {
    int j, k, o;
    o = 0;
    for (j=0; j<strlen(pat); j++) {
        if (pat[j] != '#') {
            buf[j+o] = pat[j];
            buf[j+o+1] = 0;
        } else {
            if (pat[j+1] == 'd') {
                buf[j+o] = 0;
                strcat(buf, d->dest);
                o += strlen(d->dest) - 2;
            } else if (pat[j+1] == 'h') {
                buf[j+o] = 0;
                strcat(buf, d->dest);
                for (k=j+o; k<strlen(buf); k++) {
                    if (buf[k] == ':')
                        o = k - j - 2;
                }
            } else if (pat[j+1] == 'r') {
                buf[j+o] = 0;
                for (k=0; k<strlen(d->dest); k++) {
                    if (d->dest[k] == ':')
                        break;
                }
                strcat(buf, d->dest + k + 1);
                o += strlen(d->dest) - k - 3;
            } else if (pat[j+1] == 'p') {
                buf[j+o] = 0;
                strcat(buf, path+2);
                o += strlen(path) - 4;
            }
            j++;
        }
    }
    buf[j+o] = 0;
}

void sync_rsync(struct destination *d) {
    int pid, status;
    int i = 0;
    strarray *com = strarray_create(files->num + 4);
    strarray_set(com, 0, "rsync");
    strarray_set(com, 1, "-Ruxzq");
    for (i=0; i<files->num; i++) {
        if (debug && verbose)
            printf("  %s\n", strarray_get(files, i) + 2);
        strarray_set(com, i+2, strarray_get(files, i));
    }
    strarray_set(com, i+2, d->dest);
    pid = fork();
    if (pid == 0)
        execvp("rsync", com->array);
    else
        waitpid(pid, &status, WUNTRACED);
    strarray_clear(com);
    free(com);
}

void sync_custom_fun(char *path, struct destination *d) {
    //struct destination *d = (struct destination*)dd;
    if (debug && verbose)
        printf("  %s\n", path+2);
    char cmd[8192];
    fillpattern(cmd, path, d->dest, d);
    system(cmd);
}

void sync_custom(struct destination *d) {
    strarray_each1(files, sync_custom_fun, d);
    return;
}

void sync_scp_fun(char *path, struct destination *d, char destEnd) {
    int pid, status;
    char rpath[1024];
    char *com[5];
    com[0] = "scp";
    com[1] = "-q";
    rpath[0] = 0;
    if (debug && verbose)
        printf("  %s\n", path+2);
    strcat(rpath, d->dest);
    if (destEnd != ':' && destEnd != '/')
        strcat(rpath, "/");
    strcat(rpath, path + 2);
    com[2] = path;
    com[3] = rpath;
    com[4] = NULL;
    pid = fork();
    if (pid == 0)
        execvp("scp", com);
    else
        waitpid(pid, &status, WUNTRACED);
}

void sync_scp(struct destination *d) {
    strarray_each2(files, sync_scp_fun, d, d->dest[strlen(d->dest) - 1]);
    return;
}

void handled(struct destination *d) {
    if (!files->num)
        return;
    if (debug && verbose)
        printf("fiss: will sync %i file%s to %s:\n", files->num,
                (files->num==1?"":"s"), d->dest);
    if (d->type == S_RSYNC)
        sync_rsync(d);
    else if (d->type == S_SCP)
        sync_scp(d);
    else if (d->type == S_CUSTOM)
        sync_custom(d);
    else
        fprintf(stderr, "fiss: unknown desttype %i\n", d->type);
    strarray_clear(files);
    if (strcmp(d->sync_complete_cmd, "") != 0) {
        char cmd[1024];
        fillpattern(cmd, strarray_get(files, 0), d->sync_complete_cmd, d);
        system(cmd);
    }
    if (debug && verbose)
        printf("done.\n");
}

void deletions() {
    time_t now = time(NULL);
    int i;
    char cmd[1024];
    struct destination *d = dests;
    for (; d != NULL; d = d->next) {
        if (!d->deleted->num)
            continue;
        if (now - d->deleted_time < d->push_last_delay)
            continue;
        if (debug)
            printf("Processing %i deleted files.\n", d->deleted->num);
        for (i=0; i<d->deleted->num; i++) {
            fillpattern(cmd, strarray_get(d->deleted, i), d->delete_cmd, d);
            if (debug)
                printf("  %s\n", cmd);
            system(cmd);
        }
        if (debug)
            printf("done.\n");
        strarray_clear(d->deleted);
    }
}

void changes() {
    struct destination *d = dests;
    for (; d != NULL; d = d->next) {
        if (d->changed == NULL)
            continue;
        struct filechange *prev = NULL;
        struct filechange *chng = d->changed;
        int i = 0;
        time_t now = time(NULL);
        while (chng != NULL) {
            int removed = false;
            time_t since_last = now - chng->last_change;
            time_t since_first = now - chng->first_change;
            if (since_last >= d->push_last_delay
                    || since_first >= d->push_first_delay) {
                handle(chng->name);
                struct filechange* next = chng->next;
                if (prev == NULL)
                    d->changed = next;
                else
                    prev->next = next;
                removed = true;
                free(chng->name);
                free(chng);
                chng = next;
            }
            if (!removed) {
                prev = chng;
                chng = chng->next;
            }
        }
        handled(d);
    }
}

void add_deleted(char *path) {
    struct destination *d = dests;
    for (; d != NULL; d=d->next) {
        if (!d->delete)
            continue;
        strarray_push(d->deleted, path);
        d->deleted_time = time(NULL);
    }
}

void start() {
    int inotify_fd;
    int wd;
    int len;
    int i;
    char buffer[BUFLEN];
    char path[BUFLEN];
    int enter_loop = 0;
    struct stat stat_struct;
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        fprintf(stderr, "inotify_init failed");
        exit(1);
        return;
    }
    char *root = ".";
    watch_dirs(root, inotify_fd);
    struct pollfd pfd;
    pfd.fd = inotify_fd;
    pfd.events = POLLIN;
    while (true) {
        int rv = poll(&pfd, 1, 1000);
        if (rv) {
            len = read(inotify_fd, buffer, BUFLEN);
            i = 0;
            while (i < len) {
                struct inotify_event *event = (struct inotify_event *) &buffer[i];
                int ret;
                i += EVENT_SIZE + event->len;
                if (! event->len)
                    continue;
                get_watch_buf(path, event->wd);
                strcat(path, "/");
                strcat(path, event->name);
                if (skip(path))
                    continue;
                if (event->mask & IN_DELETE) {
                    add_deleted(path);
                    continue;
                }
                if (stat(path, &stat_struct))
                    continue;
                if (S_ISDIR(stat_struct.st_mode) && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                    watch_dirs(path, inotify_fd);
                    if (event->mask & IN_MOVED_TO)
                        putchange_recursive(path);
                } else if (event->mask & IN_CREATE) {
                    continue;
                }
                putchange(path);
            }
        }
        changes();
        deletions();
    }
}

void create_destination(int desttype, char *dest, int push_last_delay,
        int push_first_delay, int delete, char *delete_cmd,
        char *sync_complete_cmd) {
    struct destination *nd = malloc(sizeof(struct destination));
    nd->type = desttype;
    nd->dest = dest;
    nd->push_last_delay = push_last_delay;
    nd->push_first_delay = push_first_delay;
    nd->changed = NULL;
    nd->deleted = strarray_create(8);
    nd->deleted_time = 0;
    nd->delete = delete;
    nd->delete_cmd = delete_cmd;
    nd->sync_complete_cmd = sync_complete_cmd;
    nd->next = dests;
    dests = nd;
}

int skip(char *name) {
    int i;
    for (i=0; i<skips->num; i++) {
        if (0 == fnmatch(skips->array[i], name, 0))
            return true;
    }
    return false;
}

int main(int argc, char **argv) {
    int desttype = S_RSYNC;
    char *dest;
    int push_last_delay = 60;
    int push_first_delay = 300;
    int delete = 0;
    char *delete_cmd = "echo fiss: Deletion command unset, did not delete #d #p.";
    char *sync_complete_cmd = "";
    char rundir[128] = "";
    int pid;
    skips = strarray_create(8);
    strarray_push(skips, "*~");
    strarray_push(skips, "#*#");
    if ((argc == 2) && ((strcmp("-h", argv[1]) == 0)
                        || (strcmp("--help", argv[1]) == 0)) || argc == 1) {
        return help();
    } else if ((argc == 2) && ((strcmp("-v", argv[1]) == 0)
                               || (strcmp("--version", argv[1]) == 0))) {
        return version();
    }
    char *root = NULL;
    int i;
    for (i=1; i<argc; i++) {
        if (strcmp("-d", argv[i]) == 0) {
            push_last_delay = atoi(argv[++i]);
        } else if (strcmp("-D", argv[i]) == 0) {
            push_first_delay = atoi(argv[++i]);
        } else if (strcmp("-V", argv[i]) == 0) {
            verbose = true;
        } else if (strcmp("-f", argv[i]) == 0) {
            debug = true;
        } else if (strcmp("--rsync", argv[i]) == 0) {
            desttype = S_RSYNC;
        } else if (strcmp("--scp", argv[i]) == 0) {
            desttype = S_SCP;
        } else if (strcmp("--custom", argv[i]) == 0) {
            desttype = S_CUSTOM;
        } else if (strcmp("--delete-cmd", argv[i]) == 0) {
            delete_cmd = argv[++i];
        } else if (strcmp("--delete", argv[i]) == 0) {
            delete = true;
        } else if (strcmp("--no-delete", argv[i]) == 0) {
            delete = false;
        } else if (strcmp("--skip", argv[i]) == 0) {
            strarray_push(skips, argv[++i]);
        } else if (strcmp("--clear-skip", argv[i]) == 0) {
            strarray_clear(skips);
        } else if (strcmp("--sync-complete-cmd", argv[i]) == 0) {
            sync_complete_cmd = argv[++i];
        } else if (root == NULL) {
            root = argv[i];
            if (chdir(root)) {
                fprintf(stderr, "fiss: could not chdir to %s\n", root);
                exit(1);
            }
        } else {
            dest = argv[i];
            create_destination(desttype, dest, push_last_delay,
                    push_first_delay, delete, delete_cmd, sync_complete_cmd);
        }
    }
    if (verbose) {
        char *stype;
        struct destination *d = dests;
        while (d != NULL) {
            if (d->type == S_RSYNC)
                stype = "rsync";
            else if (d->type == S_SCP)
                stype = "scp";
            printf("Configuration %s:\n", d->dest);
            printf(" local-root:        %s\n", root);
            printf(" destination:       %s\n", d->dest);
            printf(" type:              %s\n", stype);
            printf(" since-last-change: %i\n", d->push_last_delay);
            printf(" max-last-push:     %i\n", d->push_first_delay);
            printf(" monitor-deletes:   %s\n", (d->delete ? "yes" : "no"));
            printf(" delete-command:    %s\n", (d->delete_cmd));
            printf(" skips:             ");
            for (i=0; i<skips->num; i++)
                printf("%s ", strarray_get(skips, i));
            printf("\n");
            d = d->next;
        }
    }
    if (dests == NULL) {
        fprintf(stderr, "fiss: must provide at least one destination.\n");
        exit(1);
    }
    if ((debug == 0) && (pid = fork())) {
        if (verbose)
            printf(" forked pid:        %i\n", pid);
        return 0;
    }
    files = strarray_create(8);
    start();
}

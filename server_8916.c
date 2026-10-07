#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REG     "IT23858916"
#define PORT    (6000 + 8916)
#define NID     " NID:8589\n"            /* ends every OK / ERR line */
#define MAXC    20
#define MAXROOM 10
#define MAXFILE 10000000                 /* 10 MB */

struct client { int used, fd; char name[32]; } cl[MAXC];
struct room   { char name[32]; int member[MAXC]; } rooms[MAXROOM];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
FILE *lg;

/* ---------- small helpers ---------- */

void logit(const char *fmt, ...) {               /* timestamped log line */
    char msg[300], ts[32]; time_t t = time(NULL); struct tm tm;
    va_list a; va_start(a, fmt); vsnprintf(msg, sizeof msg, fmt, a); va_end(a);
    localtime_r(&t, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(lg, "[%s] %s\n", ts, msg); fflush(lg);
    printf("[%s] %s\n", ts, msg);
}

void sendf(int fd, const char *fmt, ...) {       /* printf() into a socket */
    char b[2048]; va_list a;
    va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    send(fd, b, strlen(b), MSG_NOSIGNAL);
}

int read_line(int fd, char *buf, int max) {      /* framing: one line, or -1 */
    int n = 0; char c;
    while (1) {
        if (recv(fd, &c, 1, 0) <= 0) return -1;  /* 1 byte at a time, so we */
        if (c == '\n') break;                    /* never read past the line */
        if (c != '\r' && n < max - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

int recv_bytes(int fd, long size, FILE *f) {     /* read EXACTLY size bytes */
    char buf[1024];
    while (size > 0) {
        long want = size < (long)sizeof buf ? size : (long)sizeof buf;
        int n = recv(fd, buf, want, 0);
        if (n <= 0) return -1;                   /* client died mid-file */
        if (f) fwrite(buf, 1, n, f);             /* f == NULL: just discard */
        size -= n;
    }
    return 0;
}

char *split(char *s) {            /* cut s after its first word, return rest */
    char *p = strchr(s, ' ');
    if (!p) return s + strlen(s);
    *p = 0;
    return p + 1;
}

int okname(const char *s) {       /* letters, digits, _  (1..20 chars) */
    if (!*s || strlen(s) > 20) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '_') return 0;
    return 1;
}

int find(const char *name) {                     /* user index or -1 */
    for (int i = 0; i < MAXC; i++)
        if (cl[i].used && cl[i].name[0] && !strcmp(cl[i].name, name)) return i;
    return -1;
}

int find_room(const char *name) {                /* room index or -1 */
    for (int r = 0; r < MAXROOM; r++)
        if (rooms[r].name[0] && !strcmp(rooms[r].name, name)) return r;
    return -1;
}

void tell_all(int me, const char *line) {        /* to every registered user but me */
    for (int i = 0; i < MAXC; i++)
        if (i != me && cl[i].used && cl[i].name[0]) sendf(cl[i].fd, "%s", line);
}

/* ---------- SENDFILE <target> <filename> <size> + raw bytes ---------- */

void forward(int fd, const char *path) {         /* copy a stored file to fd */
    char buf[1024]; int n; FILE *g = fopen(path, "rb");
    if (!g) return;
    while ((n = fread(buf, 1, sizeof buf, g)) > 0) send(fd, buf, n, MSG_NOSIGNAL);
    fclose(g);
}

void do_sendfile(int me, char *target) {
    int fd = cl[me].fd, to, room;
    char *info = split(target), fname[100] = "", path[300];
    long size = -1;

    if (!cl[me].name[0]) { sendf(fd, "ERR 007 NOT_REGISTERED" NID); return; }
    if (sscanf(info, "%99s %ld", fname, &size) != 2 || size < 0) {
        sendf(fd, "ERR 012 BAD_SYNTAX" NID); return;
    }
    pthread_mutex_lock(&lock);
    to = find(target); room = find_room(target);
    pthread_mutex_unlock(&lock);

    /* on any error we must still swallow the <size> bytes the client sends */
    if (fname[0] == '.' || strchr(fname, '/')) {
        sendf(fd, "ERR 005 BAD_FILENAME" NID); recv_bytes(fd, size, NULL); return;
    }
    if (size > MAXFILE) {
        sendf(fd, "ERR 004 FILE_TOO_LARGE" NID); recv_bytes(fd, size, NULL); return;
    }
    if (to < 0 && room < 0) {
        sendf(fd, "ERR 002 USER_NOT_FOUND" NID); recv_bytes(fd, size, NULL); return;
    }

    mkdir("storage", 0755);                      /* ./storage/<regno>/<sender>/ */
    mkdir("storage/" REG, 0755);
    snprintf(path, sizeof path, "storage/" REG "/%s", cl[me].name);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "storage/" REG "/%s/%s", cl[me].name, fname);

    FILE *f = fopen(path, "wb");
    if (!f) { sendf(fd, "ERR 014 STORAGE_ERROR" NID); recv_bytes(fd, size, NULL); return; }
    int r = recv_bytes(fd, size, f);
    fclose(f);
    if (r < 0) { remove(path); logit("FILE incomplete, removed %s", path); return; }

    sendf(fd, "OK FILE_RECEIVED %s" NID, fname);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAXC; i++) {
        if (i == me || !cl[i].used || !cl[i].name[0]) continue;
        int hit = (to >= 0) ? (i == to) : rooms[room].member[i];
        if (!hit) continue;
        sendf(cl[i].fd, "MSG FILE %s %s %ld\n", cl[me].name, fname, size);
        forward(cl[i].fd, path);
    }
    pthread_mutex_unlock(&lock);
    logit("FILE %s -> %s name=%s size=%ld stored=%s", cl[me].name, target, fname, size, path);
}

/* ---------- one thread per client ---------- */

void *handle(void *p) {
    int me = (int)(long)p, fd = cl[me].fd;
    char line[1024], out[1200], list[800];
    char *name = cl[me].name;

    while (read_line(fd, line, sizeof line) >= 0) {
        char *cmd = line, *rest = split(line);   /* "PMSG bob hi" -> cmd, "bob hi" */
        int quit = 0;

        if (!strcmp(cmd, "SENDFILE")) { do_sendfile(me, rest); continue; }

        pthread_mutex_lock(&lock);
        if (!strcmp(cmd, "REGISTER")) {
            if (name[0])                sendf(fd, "ERR 009 ALREADY_REGISTERED" NID);
            else if (!okname(rest))     sendf(fd, "ERR 012 BAD_SYNTAX" NID);
            else if (find(rest) >= 0)   sendf(fd, "ERR 001 USERNAME_TAKEN" NID);
            else {
                strcpy(name, rest);
                sendf(fd, "OK REGISTERED %s" NID, name);
                snprintf(out, sizeof out, "MSG USER_JOINED %s\n", name);
                tell_all(me, out);
                logit("REGISTER %s", name);
            }
        } else if (!strcmp(cmd, "QUIT")) {
            sendf(fd, "OK BYE" NID);
            quit = 1;
        } else if (!name[0]) {
            sendf(fd, "ERR 007 NOT_REGISTERED" NID);
        } else if (!strcmp(cmd, "LIST")) {
            list[0] = 0;
            for (int i = 0; i < MAXC; i++)
                if (cl[i].used && cl[i].name[0]) {
                    if (list[0]) strcat(list, ",");
                    strcat(list, cl[i].name);
                }
            sendf(fd, "OK USERS %s" NID, list);
        } else if (!strcmp(cmd, "BCAST")) {
            if (!*rest) sendf(fd, "ERR 012 BAD_SYNTAX" NID);
            else {
                snprintf(out, sizeof out, "MSG BCAST %s %s\n", name, rest);
                tell_all(me, out);
                sendf(fd, "OK SENT" NID);
                logit("BCAST %s: %s", name, rest);
            }
        } else if (!strcmp(cmd, "PMSG")) {
            char *msg = split(rest);             /* rest = target user */
            int t = find(rest);
            if (!*rest || !*msg) sendf(fd, "ERR 012 BAD_SYNTAX" NID);
            else if (t < 0)      sendf(fd, "ERR 002 USER_NOT_FOUND" NID);
            else {
                sendf(cl[t].fd, "MSG PRIV %s %s\n", name, msg);
                sendf(fd, "OK SENT" NID);
                logit("PMSG %s -> %s: %s", name, rest, msg);
            }
        } else if (!strcmp(cmd, "JOIN")) {
            int r = find_room(rest);
            if (!okname(rest)) sendf(fd, "ERR 012 BAD_SYNTAX" NID);
            else {
                for (int k = 0; r < 0 && k < MAXROOM; k++)   /* create room */
                    if (!rooms[k].name[0]) { r = k; strcpy(rooms[k].name, rest); }
                if (r < 0) sendf(fd, "ERR 013 SERVER_FULL" NID);
                else {
                    rooms[r].member[me] = 1;
                    sendf(fd, "OK JOINED %s" NID, rest);
                    logit("JOIN %s -> room %s", name, rest);
                }
            }
        } else if (!strcmp(cmd, "LEAVE")) {
            int r = find_room(rest);
            if (r < 0 || !rooms[r].member[me]) sendf(fd, "ERR 003 ROOM_NOT_FOUND" NID);
            else {
                rooms[r].member[me] = 0;
                sendf(fd, "OK LEFT %s" NID, rest);
                logit("LEAVE %s <- room %s", name, rest);
            }
        } else if (!strcmp(cmd, "ROOMS")) {
            list[0] = 0;
            for (int r = 0; r < MAXROOM; r++)
                if (rooms[r].name[0]) {
                    if (list[0]) strcat(list, ",");
                    strcat(list, rooms[r].name);
                }
            sendf(fd, "OK ROOMS %s" NID, list);
        } else if (!strcmp(cmd, "RMSG")) {
            char *msg = split(rest);             /* rest = room name */
            int r = find_room(rest);
            if (!*rest || !*msg) sendf(fd, "ERR 012 BAD_SYNTAX" NID);
            else if (r < 0)      sendf(fd, "ERR 003 ROOM_NOT_FOUND" NID);
            else {
                for (int i = 0; i < MAXC; i++)
                    if (i != me && cl[i].used && rooms[r].member[i])
                        sendf(cl[i].fd, "MSG ROOM %s %s %s\n", rest, name, msg);
                sendf(fd, "OK SENT" NID);
                logit("RMSG %s -> room %s: %s", name, rest, msg);
            }
        } else {
            sendf(fd, "ERR 008 UNKNOWN_COMMAND" NID);
        }
        pthread_mutex_unlock(&lock);
        if (quit) break;
    }

    /* disconnect (QUIT, Ctrl+C, crash, ...): clean up shared state */
    pthread_mutex_lock(&lock);
    logit("DISCONNECT %s", name[0] ? name : "(unregistered)");
    if (name[0]) {
        snprintf(out, sizeof out, "MSG USER_LEFT %s\n", name);
        tell_all(me, out);
    }
    for (int r = 0; r < MAXROOM; r++) rooms[r].member[me] = 0;
    close(fd);
    memset(&cl[me], 0, sizeof cl[me]);
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* ---------- main: socket, bind, listen, accept loop ---------- */

int main(void) {
    signal(SIGPIPE, SIG_IGN);                    /* dead socket must not kill us */
    lg = fopen("netmsg_" REG ".log", "a");
    if (!lg) { perror("log"); return 1; }

    int s = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(PORT);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 5) < 0) {
        perror("bind/listen"); return 1;
    }
    logit("SERVER started on port %d, tag NID:8589", PORT);

    while (1) {
        struct sockaddr_in ca; socklen_t l = sizeof ca;
        int fd = accept(s, (struct sockaddr *)&ca, &l);
        if (fd < 0) continue;

        pthread_mutex_lock(&lock);
        int i = 0;
        while (i < MAXC && cl[i].used) i++;      /* find a free slot */
        if (i == MAXC) {
            pthread_mutex_unlock(&lock);
            sendf(fd, "ERR 013 SERVER_FULL" NID);
            close(fd);
            continue;
        }
        memset(&cl[i], 0, sizeof cl[i]);
        cl[i].used = 1; cl[i].fd = fd;
        pthread_mutex_unlock(&lock);

        logit("CONNECT %s:%d", inet_ntoa(ca.sin_addr), ntohs(ca.sin_port));
        pthread_t t;
        pthread_create(&t, NULL, handle, (void *)(long)i);
        pthread_detach(t);
    }
}

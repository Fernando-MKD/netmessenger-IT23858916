
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT (6000 + 8916)
int sock;

int read_line(int fd, char *buf, int max) {      /* one line, or -1 */
    int n = 0; char c;
    while (1) {
        if (recv(fd, &c, 1, 0) <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r' && n < max - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

int recv_bytes(int fd, long size, FILE *f) {     /* read size bytes */
    char buf[1024];
    while (size > 0) {
        long want = size < (long)sizeof buf ? size : (long)sizeof buf;
        int n = recv(fd, buf, want, 0);
        if (n <= 0) return -1;
        if (f) fwrite(buf, 1, n, f);
        size -= n;
    }
    return 0;
}

void *reader(void *arg) {                        /* prints from the server */
    char line[1024], who[32], name[100], path[200];
    long size;
    (void)arg;
    while (read_line(sock, line, sizeof line) >= 0) {
        if (sscanf(line, "MSG FILE %31s %99s %ld", who, name, &size) == 3) {
            mkdir("downloads", 0755);
            snprintf(path, sizeof path, "downloads/%s", name);
            FILE *f = fopen(path, "wb");
            recv_bytes(sock, size, f);           /* raw file bytes follow the line */
            if (f) fclose(f);
            printf("[file from %s saved: %s]\n", who, path);
        } else
            printf("%s\n", line);
    }
    printf("[disconnected]\n");
    exit(0);
}

int main(int argc, char **argv) {
    char line[1024];
    struct sockaddr_in a = {0};

    setbuf(stdout, NULL);
    signal(SIGPIPE, SIG_IGN);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    inet_pton(AF_INET, argc > 1 ? argv[1] : "127.0.0.1", &a.sin_addr);
    if (connect(sock, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); return 1; }
    printf("Connected (port %d). Start with: REGISTER <name>\n", PORT);

    pthread_t t;
    pthread_create(&t, NULL, reader, NULL);

    while (fgets(line, sizeof line, stdin)) {    
        if (strncmp(line, "/sendfile ", 10) == 0) {
            char target[32], path[200], buf[1024];
            if (sscanf(line + 10, "%31s %199s", target, path) != 2) continue;
            FILE *f = fopen(path, "rb");
            if (!f) { printf("cannot open %s\n", path); continue; }
            fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
            char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            snprintf(buf, sizeof buf, "SENDFILE %s %s %ld\n", target, base, size);
            send(sock, buf, strlen(buf), 0);     /* the command line */
            int n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0)
                send(sock, buf, n, 0);           /* then exactly size */
            fclose(f);
        } else
            send(sock, line, strlen(line), 0);
    }
    send(sock, "QUIT\n", 5, 0);                  
    sleep(1);
    return 0;
}

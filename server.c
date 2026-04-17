#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

/* ==================== KONFIGURIMI ==================== */
#define PORT        5000
#define HTTP_PORT   8080
#define MAX_CONN    10
#define TIMEOUT_SEC 60
#define BUF_SIZE    4096
#define SERVER_DIR  "server_files"

/* ==================== STATISTIKAT ==================== */
typedef struct {
    int  active_connections;
    char client_ips[MAX_CONN][INET_ADDRSTRLEN];
    int  ip_count;
    int  total_messages;
    char last_messages[MAX_CONN][256];
    int  msg_count;
} Stats;

static Stats            stats;
static CRITICAL_SECTION stats_lock;
static int              admin_assigned = 0;

/* ==================== STRUKTURA E KLIENTIT ==================== */
typedef struct {
    SOCKET fd;
    char   ip[INET_ADDRSTRLEN];
    char   privilege[8];
} ClientInfo;

/* ====================================================
 *  NDIHMES: Dergo string
 * ==================================================== */
static int send_str(SOCKET fd, const char *msg) {
    return send(fd, msg, (int)strlen(msg), 0);
}

/* ====================================================
 *  KOMANDA: /list
 * ==================================================== */
static void cmd_list(SOCKET fd) {
    WIN32_FIND_DATA ffd;
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", SERVER_DIR);
    HANDLE hFind = FindFirstFile(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        send_str(fd, "Gabim: Nuk mund te hapet direktoria.\n");
        return;
    }
    send_str(fd, "File-t ne server:\n");
    int found = 0;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char line[300];
        snprintf(line, sizeof(line), "  - %s\n", ffd.cFileName);
        send_str(fd, line);
        found = 1;
    } while (FindNextFile(hFind, &ffd));
    FindClose(hFind);
    if (!found) send_str(fd, "  (asnje file)\n");
}

/* ====================================================
 *  KOMANDA: /read <filename>
 * ==================================================== */
static void cmd_read(SOCKET fd, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s", SERVER_DIR, filename);
    FILE *f = fopen(path, "r");
    if (!f) { send_str(fd, "Gabim: File-i nuk ekziston.\n"); return; }
    char header[300];
    snprintf(header, sizeof(header), "=== %s ===\n", filename);
    send_str(fd, header);
    char buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf)-1, f)) > 0) {
        buf[n] = '\0';
        send_str(fd, buf);
    }
    fclose(f);
    send_str(fd, "\n");
}

/* ====================================================
 *  KOMANDA: /search <keyword>
 * ==================================================== */
static void cmd_search(SOCKET fd, const char *keyword) {
    WIN32_FIND_DATA ffd;
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", SERVER_DIR);
    HANDLE hFind = FindFirstFile(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        send_str(fd, "Gabim: Nuk mund te hapet direktoria.\n");
        return;
    }
    char header[256];
    snprintf(header, sizeof(header), "Rezultatet per '%s':\n", keyword);
    send_str(fd, header);
    int found = 0;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
        if (strstr(ffd.cFileName, keyword) != NULL) {
            char line[300];
            snprintf(line, sizeof(line), "  - %s\n", ffd.cFileName);
            send_str(fd, line);
            found = 1;
        }
    } while (FindNextFile(hFind, &ffd));
    FindClose(hFind);
    if (!found) send_str(fd, "  Nuk u gjet asnje file.\n");
}

/* ====================================================
 *  KOMANDA: /info <filename>
 * ==================================================== */
static void cmd_info(SOCKET fd, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s", SERVER_DIR, filename);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fa)) {
        send_str(fd, "Gabim: File-i nuk ekziston.\n");
        return;
    }
    LARGE_INTEGER size;
    size.HighPart = fa.nFileSizeHigh;
    size.LowPart  = fa.nFileSizeLow;
    SYSTEMTIME stCreate, stModify;
    FileTimeToSystemTime(&fa.ftCreationTime,  &stCreate);
    FileTimeToSystemTime(&fa.ftLastWriteTime, &stModify);
    char buf[512];
    snprintf(buf, sizeof(buf),
        "Informacioni per '%s':\n"
        "  Madhesia  : %lld bytes\n"
        "  Krijuar   : %04d-%02d-%02d %02d:%02d:%02d\n"
        "  Modifikuar: %04d-%02d-%02d %02d:%02d:%02d\n",
        filename, (long long)size.QuadPart,
        stCreate.wYear, stCreate.wMonth,  stCreate.wDay,
        stCreate.wHour, stCreate.wMinute, stCreate.wSecond,
        stModify.wYear, stModify.wMonth,  stModify.wDay,
        stModify.wHour, stModify.wMinute, stModify.wSecond);
    send_str(fd, buf);
}

/* ====================================================
 *  KOMANDA: /download <filename>
 * ==================================================== */
static void cmd_download(SOCKET fd, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s", SERVER_DIR, filename);
    FILE *f = fopen(path, "rb");
    if (!f) { send_str(fd, "Gabim: File-i nuk ekziston.\n"); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char header[256];
    snprintf(header, sizeof(header), "DOWNLOAD_START:%s:%ld\n", filename, sz);
    send_str(fd, header);
    char buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        send(fd, buf, (int)n, 0);
    fclose(f);
    send_str(fd, "\nDOWNLOAD_END\n");
}

/* ====================================================
 *  KOMANDA: /upload <filename> (vetem admin)
 * ==================================================== */
static void cmd_upload(SOCKET fd, const char *filename) {
    char msg[256];
    snprintf(msg, sizeof(msg), "GATI_PER_UPLOAD:%s\n", filename);
    send_str(fd, msg);
    char header[128] = {0};
    int i = 0; char c;
    while (i < (int)sizeof(header)-1) {
        if (recv(fd, &c, 1, 0) <= 0) break;
        header[i++] = c;
        if (c == '\n') break;
    }
    if (strncmp(header, "SIZE:", 5) != 0) { send_str(fd, "Gabim ne protokoll.\n"); return; }
    long size = atol(header + 5);
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s", SERVER_DIR, filename);
    FILE *f = fopen(path, "wb");
    if (!f) { send_str(fd, "Gabim: Nuk mund te krijohet file-i.\n"); return; }
    long received = 0;
    char buf[BUF_SIZE];
    while (received < size) {
        int toread = (int)((size - received) < BUF_SIZE ? (size - received) : BUF_SIZE);
        int n = recv(fd, buf, toread, 0);
        if (n <= 0) break;
        fwrite(buf, 1, n, f);
        received += n;
    }
    fclose(f);
    snprintf(msg, sizeof(msg), "File-i '%s' u ngarkua (%ld bytes).\n", filename, received);
    send_str(fd, msg);
}

/* ====================================================
 *  KOMANDA: /delete <filename> (vetem admin)
 * ==================================================== */
static void cmd_delete(SOCKET fd, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s", SERVER_DIR, filename);
    if (DeleteFile(path)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "File-i '%s' u fshi me sukses.\n", filename);
        send_str(fd, msg);
    } else {
        send_str(fd, "Gabim: File-i nuk ekziston ose nuk mund te fshihet.\n");
    }
}

/* ====================================================
 *  KOMANDA: /help
 * ==================================================== */
static void cmd_help(SOCKET fd, const char *privilege) {
    send_str(fd,
        "Komandat e disponueshme:\n"
        "  /list                - Listo file-t ne server\n"
        "  /read <filename>     - Lexo permbajtjen e nje file-i\n"
        "  /download <filename> - Shkarko nje file\n"
        "  /search <keyword>    - Kerko file sipas emrit\n"
        "  /info <filename>     - Info per nje file\n"
    );
    if (strcmp(privilege, "admin") == 0)
        send_str(fd,
            "  /upload <filename>   - Ngarko nje file ne server\n"
            "  /delete <filename>   - Fshi nje file\n"
        );
    send_str(fd, "  /help               - Shfaq kete liste\n");
}

/* ====================================================
 *  PROCESIMI I KOMANDES
 * ==================================================== */
static void process_command(SOCKET fd, const char *line, const char *privilege) {
    char cmd[64] = {0}, arg[512] = {0};
    sscanf(line, "%63s %511[^\n]", cmd, arg);

    if      (strcmp(cmd, "/list")     == 0) cmd_list(fd);
    else if (strcmp(cmd, "/read")     == 0) { if (arg[0]) cmd_read(fd,arg);     else send_str(fd,"Perdorimi: /read <filename>\n"); }
    else if (strcmp(cmd, "/search")   == 0) { if (arg[0]) cmd_search(fd,arg);   else send_str(fd,"Perdorimi: /search <keyword>\n"); }
    else if (strcmp(cmd, "/info")     == 0) { if (arg[0]) cmd_info(fd,arg);     else send_str(fd,"Perdorimi: /info <filename>\n"); }
    else if (strcmp(cmd, "/download") == 0) { if (arg[0]) cmd_download(fd,arg); else send_str(fd,"Perdorimi: /download <filename>\n"); }
    else if (strcmp(cmd, "/upload")   == 0) {
        if (strcmp(privilege,"admin")!=0) { send_str(fd,"Gabim: Nuk keni privilegje.\n"); return; }
        if (arg[0]) cmd_upload(fd,arg); else send_str(fd,"Perdorimi: /upload <filename>\n");
    }
    else if (strcmp(cmd, "/delete")   == 0) {
        if (strcmp(privilege,"admin")!=0) { send_str(fd,"Gabim: Nuk keni privilegje.\n"); return; }
        if (arg[0]) cmd_delete(fd,arg); else send_str(fd,"Perdorimi: /delete <filename>\n");
    }
    else if (strcmp(cmd, "/help") == 0) cmd_help(fd, privilege);
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Komanda e panjohur: %s. Shkruani /help.\n", cmd);
        send_str(fd, msg);
    }
}

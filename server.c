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
#define TIMEOUT_SEC 600
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
/* ====================================================
 *  THREAD I KLIENTIT
 * ==================================================== */
static DWORD WINAPI client_thread(LPVOID arg) {
    ClientInfo *ci = (ClientInfo *)arg;
    SOCKET fd = ci->fd;

    printf("[+] Klient: %s | Privilegji: %s\n", ci->ip, ci->privilege);

    /* regjistro ne statistika */
    EnterCriticalSection(&stats_lock);
    stats.active_connections++;
    if (stats.ip_count < MAX_CONN)
        strncpy(stats.client_ips[stats.ip_count++], ci->ip, INET_ADDRSTRLEN-1);
    LeaveCriticalSection(&stats_lock);

    /* vendos timeout */
    DWORD tv = TIMEOUT_SEC * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    /* mesazhi i mireseardhjeve */
    char welcome[128];
    snprintf(welcome, sizeof(welcome),
        "Miresevini! Privilegji juaj: %s\nShkruani /help per komandat.\n",
        ci->privilege);
    send_str(fd, welcome);

    /* loop kryesor: merr dhe proceso komandat */
    char buf[BUF_SIZE];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int n = recv(fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) {
            printf("[!] Shkepotje/Timeout per %s\n", ci->ip);
            break;
        }
        buf[strcspn(buf, "\r\n")] = '\0';
        if (strlen(buf) == 0) continue;

        printf("[%s] %s\n", ci->ip, buf);

        /* perditeso statistikat */
        EnterCriticalSection(&stats_lock);
        stats.total_messages++;
        if (stats.msg_count < MAX_CONN)
            strncpy(stats.last_messages[stats.msg_count++], buf, 255);
        LeaveCriticalSection(&stats_lock);

        process_command(fd, buf, ci->privilege);
    }

    /* çregjistro klientin */
    EnterCriticalSection(&stats_lock);
    stats.active_connections--;
    LeaveCriticalSection(&stats_lock);

    printf("[-] Klienti u shkeput: %s\n", ci->ip);
    closesocket(fd);
    free(ci);
    return 0;
}

/* ====================================================
 *  HTTP SERVER THREAD (port 8080 -> /stats)
 * ==================================================== */
static DWORD WINAPI http_thread(LPVOID arg) {
    (void)arg;
    SOCKET sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(HTTP_PORT);
    bind(sfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(sfd, 5);
    printf("[HTTP] Duke degjuar ne port %d -> /stats\n", HTTP_PORT);

    while (1) {
        SOCKET cfd = accept(sfd, NULL, NULL);
        if (cfd == INVALID_SOCKET) continue;

        /* lexo request */
        char req[512] = {0};
        recv(cfd, req, sizeof(req)-1, 0);

        /* nderto pergjigjen JSON */
        char json[4096] = {0};
        EnterCriticalSection(&stats_lock);
        int pos = 0;
        pos += snprintf(json+pos, sizeof(json)-pos,
            "{\n"
            "  \"lidhjet_aktive\": %d,\n"
            "  \"total_mesazhe\": %d,\n"
            "  \"ip_adresat\": [",
            stats.active_connections, stats.total_messages);
        for (int i = 0; i < stats.ip_count; i++)
            pos += snprintf(json+pos, sizeof(json)-pos,
                "%s\"%s\"", i?",":"", stats.client_ips[i]);
        pos += snprintf(json+pos, sizeof(json)-pos,
            "],\n  \"mesazhet_e_fundit\": [");
        for (int i = 0; i < stats.msg_count; i++)
            pos += snprintf(json+pos, sizeof(json)-pos,
                "%s\"%s\"", i?",":"", stats.last_messages[i]);
        pos += snprintf(json+pos, sizeof(json)-pos, "]\n}\n");
        LeaveCriticalSection(&stats_lock);

        /* dergo pergjigjen HTTP */
        char response[5120];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n%s",
            (int)strlen(json), json);
        send(cfd, response, (int)strlen(response), 0);
        closesocket(cfd);
    }
    return 0;
}

/* ====================================================
 *  MAIN - SERVERI KRYESOR
 * ==================================================== */
int main(void) {
    /* Inicializo Winsock */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Gabim: WSAStartup deshtoi.\n");
        return 1;
    }

    /* Krijo direktorine e serverit */
    CreateDirectory(SERVER_DIR, NULL);

    /* Inicializo mutex */
    InitializeCriticalSection(&stats_lock);

    /* Fillo HTTP thread */
    HANDLE ht = CreateThread(NULL, 0, http_thread, NULL, 0, NULL);
    CloseHandle(ht);

    /* Krijo socket kryesor TCP */
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("Gabim: socket() deshtoi.\n");
        WSACleanup(); return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Gabim: bind() deshtoi.\n"); WSACleanup(); return 1;
    }
    if (listen(server_fd, MAX_CONN) == SOCKET_ERROR) {
        printf("Gabim: listen() deshtoi.\n"); WSACleanup(); return 1;
    }

    printf("[SERVER] Duke degjuar ne port %d\n", PORT);
    printf("[SERVER] Monitorim: http://localhost:%d/stats\n", HTTP_PORT);
    printf("[SERVER] Max lidhje: %d | Timeout: %ds\n", MAX_CONN, TIMEOUT_SEC);
    printf("--------------------------------------------------\n");

    /* Loop kryesor: prano lidhje te reja */
    while (1) {
        struct sockaddr_in cli_addr;
        int cli_len = sizeof(cli_addr);
        SOCKET cli_fd = accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (cli_fd == INVALID_SOCKET) continue;

        /* kontrollo nese serveri eshte plot */
        EnterCriticalSection(&stats_lock);
        int aktive = stats.active_connections;
        LeaveCriticalSection(&stats_lock);

        if (aktive >= MAX_CONN) {
            send_str(cli_fd, "Serveri eshte plot. Provo me vone.\n");
            closesocket(cli_fd); continue;
        }

        /* cakto privilegjet */
        ClientInfo *ci = malloc(sizeof(ClientInfo));
        ci->fd = cli_fd;
        inet_ntop(AF_INET, &cli_addr.sin_addr, ci->ip, sizeof(ci->ip));

        if (!admin_assigned) {
            strcpy(ci->privilege, "admin");
            admin_assigned = 1;
        } else {
            strcpy(ci->privilege, "read");
        }

        /* fillo thread per klientin */
        HANDLE tid = CreateThread(NULL, 0, client_thread, ci, 0, NULL);
        CloseHandle(tid);
    }

    closesocket(server_fd);
    DeleteCriticalSection(&stats_lock);
    WSACleanup();
    return 0;
}
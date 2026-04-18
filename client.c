#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 5000
#define BUF_SIZE 4096

void handle_download(SOCKET sock, char *header){
    char filename[128];
    long size;

    sscanf(header, "DOWNLOAD_START:%[^:]:%ld", filename, &size);

    FILE *f = fopen(filename, "wb");
    if(!f){
        printf("Gabim ne krijimin e file-it\n");
        return;
    }
    char buf[BUF_SIZE];
    long received =0;

    while(received < size){
        int n = recv(sock, buf, sizeof(buf), 0);
        if(n <= 0) break;

        fwrite(buf , 1, n, f);
        received += n;
    }
    fclose(f);
    printf("File u shkarkua: %s (%ld bytes)\n", filename, received);
}
 
void handle_upload(SOCKET sock, char *filename){
    FILE *f = fopen(filename, "rb");
    if(!f){
        printf("File nuk ekziston\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char header[64];
    sprintf(header, "SIZE:%ld\n", size);
    send(sock,header,strlen(header),0);

    char buf[BUF_SIZE];
    int n;

    while((n=fread(buf,1,sizeof(buf),f))>0){
        send(sock,buf,n,0);
    }
    fclose(f);
    printf("Upload perfundoi\n");
}

int main() {
WSADATA wsa;
SOCKET sock;
struct sockaddr_in server;
char buffer[BUF_SIZE];

if(WSAStartup(MAKEWORD(2,2), &wsa)!=0){
    printf("Gabim me WSAStartup\n");
    return 1;
}

sock = socket(AF_INET, SOCK_STREAM, 0);
if(sock==INVALID_SOCKET){
    printf("Gabim ne socket!\n");
    WSACleanup();
    return 1;
}

server.sin_family = AF_INET;
server.sin_port = htons(PORT);
inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

if(connect(sock, (struct sockaddr*)&server, sizeof(server))<0){
    printf("Nuk u lidh me serverin\n");
    closesocket(sock);
    WSACleanup();
    return 1;
};

printf("U lidh me serverin\n");

int n = recv(sock, buffer, sizeof(buffer)-1, 0);
if(n>0){
buffer[n] = '\0';
printf("%s\n", buffer);
}

while (1) {
    printf("> ");
    fgets(buffer, sizeof(buffer), stdin);

    if (strncmp(buffer, "/exit", 5) == 0) {
        break;
    }

    send(sock, buffer, strlen(buffer), 0);

    while ((n = recv(sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[n] = '\0';

        // DOWNLOAD
        if (strncmp(buffer, "DOWNLOAD_START", 14) == 0) {
            handle_download(sock, buffer);
            break;  
        }

        // UPLOAD
        if (strncmp(buffer, "GATI_PER_UPLOAD", 15) == 0) {
            char filename[128];
            sscanf(buffer, "GATI_PER_UPLOAD:%s", filename);
            handle_upload(sock, filename);
            break;  
        }

        printf("%s", buffer);

        if (n < BUF_SIZE - 1) break;
    }

    if (n <= 0) {
        printf("\nServeri u shkeput.\n");
        break;
    }
}

closesocket(sock);
WSACleanup();
return 0;
}
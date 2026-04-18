#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 5000
#define BUF_SIZE 4096

void handle_download(SOCKET sock, char *header){
    char filename[128];
    long size;

    sscanf(header, "DOWNLOAD_START:%[^:]:%ld",filename,&size);

    FILE *f = fopen(filename, "wb");
    if(!f){
        printf("Gabim ne krijimin e file-it\n");
        return;
    }
    char buf[BUF_SIZE];
    long received =0;

    while(received<size){
        int n = recv(sock, buf, sizeof(buf),0);
        if(n<=0) break;

        fwrite(buf,1,n,f);
        received +=n;
    }
    fclose(f);
    printf("File u shkarkua: %s (%ld bytes)\n",filename,received);
}
 
void handle_upload(SOCKET sock, char *filename){
    FILE *f = fopen(filename, "rb");
    if(!f){
        printf("File nuk ekziston\n");
        return;
    }
    fseek(f,0,SEEK_END);
    long size = ftell(f);
    rewind(f);

    char header[64];
    sprintf(header, "SIZE%ld\n",size);
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

WSAStartup(MAKEWORD(2,2), &wsa);

sock = socket(AF_INET, SOCK_STREAM, 0);

server.sin_family = AF_INET;
server.sin_port = htons(PORT);
inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

connect(sock, (struct sockaddr*)&server, sizeof(server));

printf("U lidh me serverin\n");

int n = recv(sock, buffer, sizeof(buffer)-1, 0);
buffer[n] = '\0';
printf("%s\n", buffer);

while (1) {
printf("> ");
fgets(buffer, sizeof(buffer), stdin);

send(sock, buffer, strlen(buffer), 0);

n = recv(sock, buffer, sizeof(buffer)-1, 0);
if (n <= 0) break;

buffer[n] = '\0';

// DOWNLOAD
if (strncmp(buffer, "DOWNLOAD_START", 14) == 0) {
handle_download(sock, buffer);
continue;
}

// UPLOAD
if (strncmp(buffer, "GATI_PER_UPLOAD", 15) == 0) {
char filename[128];
sscanf(buffer, "GATI_PER_UPLOAD:%s", filename);
handle_upload(sock, filename);
continue;
}

printf("%s\n", buffer);
}

closesocket(sock);
WSACleanup();
return 0;
}
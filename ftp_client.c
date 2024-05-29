#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#define MAX 80
#define PORT 8080
#define SA struct sockaddr

void func(int sock) {
    char buff[MAX];
    int n;
    for (;;) {
        bzero(buff, sizeof(buff));
        printf("ftp> ");
        n = 0;
        while ((buff[n++] = getchar()) != '\n');
        send(sock, buff, sizeof(buff), 0);
        bzero(buff, sizeof(buff));
        recv(sock, buff, sizeof(buff), 0);
        printf("%s", buff);
        if ((strncmp(buff, "221", 3)) == 0) {
            printf("Client Exit...\n");
            break;
        }
    }
}

int main() {
    int sock;
    struct sockaddr_in servaddr, cli;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("socket creation failed...\n");
        exit(0);
    } else {
        printf("Socket successfully created..\n");
    }
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    if (connect(sock, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    } else {
        printf("connected to the server..\n");
    }
    func(sock);
    close(sock);
}

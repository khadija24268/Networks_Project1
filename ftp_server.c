#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <dirent.h>
#define MAX 80
#define PORT 8080
#define SA struct sockaddr

typedef struct {
    char username[MAX];
    char password[MAX];
} User;

User users[100];
int user_count = 0;

void load_users() {
    FILE *file = fopen("users.txt", "r");
    if (!file) {
        printf("Could not open users.txt\n");
        exit(1);
    }

    while (fscanf(file, "%s %s", users[user_count].username, users[user_count].password) != EOF) {
        user_count++;
    }

    fclose(file);
}

int authenticate(char *username, char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(username, users[i].username) == 0 && strcmp(password, users[i].password) == 0) {
            return 1;
        }
    }
    return 0;
}

void send_response(int client_socket, const char *response) {
    send(client_socket, response, strlen(response), 0);
}

void handle_data_connection(int client_socket, char *command, char *argument);
void func(int client_socket) {
    char buff[MAX];
    int n;
    int logged_in = 0;
    char username[MAX];

    for (;;) {
        bzero(buff, MAX);
        recv(client_socket, buff, sizeof(buff), 0);
        printf("From client: %s\n", buff);

        char *command = strtok(buff, " ");
        char *argument = strtok(NULL, " ");

        if (strncmp(command, "USER", 4) == 0) {
            strcpy(username, argument);
            send_response(client_socket, "331 Username OK, need password.\n");
        } else if (strncmp(command, "PASS", 4) == 0) {
            if (authenticate(username, argument)) {
                logged_in = 1;
                send_response(client_socket, "230 User logged in, proceed.\n");
            } else {
                send_response(client_socket, "530 Not logged in.\n");
            }
        } else if (logged_in) {
            if (strncmp(command, "PORT", 4) == 0) {
                send_response(client_socket, "200 PORT command successful.\n");
            } else if (strncmp(command, "RETR", 4) == 0 || strncmp(command, "STOR", 4) == 0 || strncmp(command, "LIST", 4) == 0) {
                handle_data_connection(client_socket, command, argument);
            } else if (strncmp(command, "QUIT", 4) == 0) {
                send_response(client_socket, "221 Service closing control connection.\n");
                break;
            } else {
                send_response(client_socket, "202 Command not implemented.\n");
            }
        } else {
            send_response(client_socket, "530 Not logged in.\n");
        }
    }
}

void handle_data_connection(int client_socket, char *command, char *argument) {
    int data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (data_socket == -1) {
        printf("data socket creation failed...\n");
        send_response(client_socket, "425 Can't open data connection.\n");
        return;
    }

    struct sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = htons(PORT + 1);

    if (bind(data_socket, (SA*)&data_addr, sizeof(data_addr)) != 0) {
        printf("data socket bind failed...\n");
        send_response(client_socket, "425 Can't open data connection.\n");
        close(data_socket);
        return;
    }

    if (listen(data_socket, 1) != 0) {
        printf("data socket listen failed...\n");
        send_response(client_socket, "425 Can't open data connection.\n");
        close(data_socket);
        return;
    }

    send_response(client_socket, "150 File status okay; about to open data connection.\n");

    struct sockaddr_in client;
    int len = sizeof(client);
    int data_client_socket = accept(data_socket, (SA*)&client, &len);
    if (data_client_socket < 0) {
        printf("data socket accept failed...\n");
        send_response(client_socket, "425 Can't open data connection.\n");
        close(data_socket);
        return;
    }

    if (strncmp(command, "RETR", 4) == 0) {
        FILE *file = fopen(argument, "rb");
        if (file == NULL) {
            send_response(client_socket, "550 No such file or directory.\n");
        } else {
            char file_buffer[1024];
            size_t bytes_read;
            while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
                send(data_client_socket, file_buffer, bytes_read, 0);
            }
            fclose(file);
            send_response(client_socket, "226 Transfer completed.\n");
        }
    } else if (strncmp(command, "STOR", 4) == 0) {
        FILE *file = fopen(argument, "wb");
        if (file == NULL) {
            send_response(client_socket, "550 No such file or directory.\n");
        } else {
            char file_buffer[1024];
            size_t bytes_received;
            while ((bytes_received = recv(data_client_socket, file_buffer, sizeof(file_buffer), 0)) > 0) {
                fwrite(file_buffer, 1, bytes_received, file);
            }
            fclose(file);
            send_response(client_socket, "226 Transfer completed.\n");
        }
    } else if (strncmp(command, "LIST", 4) == 0) {
        DIR *dir = opendir(".");
        if (dir == NULL) {
            send_response(client_socket, "550 No such file or directory.\n");
        } else {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                send(data_client_socket, entry->d_name, strlen(entry->d_name), 0);
                send(data_client_socket, "\n", 1, 0);
            }
            closedir(dir);
            send_response(client_socket, "226 Transfer completed.\n");
        }
    }

    close(data_client_socket);
    close(data_socket);
}

int main() {
    load_users();

    int server_socket, client_socket, len;
    struct sockaddr_in servaddr, cli;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        printf("socket creation failed...\n");
        exit(0);
    } else {
        printf("Socket successfully created..\n");
    }
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    if ((bind(server_socket, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed...\n");
        exit(0);
    } else {
        printf("Socket successfully binded..\n");
    }
    if ((listen(server_socket, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    } else {
        printf("Server listening..\n");
    }
    len = sizeof(cli);
    client_socket = accept(server_socket, (SA*)&cli, &len);
    if (client_socket < 0) {
        printf("server accept failed...\n");
        exit(0);
    } else {
        printf("server accept the client...\n");
    }
    func(client_socket);
    close(server_socket);
}

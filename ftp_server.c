#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>

#define MAX 80
#define PORT 8080
#define MAX_CLIENTS 30
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
        perror("Could not open users.txt");
        exit(1);
    }

    while (fscanf(file, "%s %s", users[user_count].username, users[user_count].password) != EOF && user_count < 100) {
        printf("Loaded user: %s\n", users[user_count].username);
        user_count++;
    }

    fclose(file);
}

void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

int check_username(char *username) {
    trim_whitespace(username);
    trim_newline(username);

    for (int i = 0; i < user_count; i++) {
        if (strcmp(username, users[i].username) == 0) {
            return i;
        }
    }
    return -1;
}

int authenticate(int user_index, char *password) {
    trim_whitespace(password);
    trim_newline(password);
    if (strcmp(users[user_index].password, password) == 0) {
        return 1;
    }
    return 0;
}

void send_response(int client_socket, const char *response) {
    send(client_socket, response, strlen(response), 0);
}

int start_data_connection(char *client_ip, int client_port) {
    int data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (data_socket == -1) {
        perror("Failed to create data socket");
        return -1;
    }

    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = inet_addr(client_ip);
    data_addr.sin_port = htons(client_port);

    for (int attempt = 0; attempt < 5; attempt++) {
        if (connect(data_socket, (SA*)&data_addr, sizeof(data_addr)) == 0) {
            return data_socket;
        }
        perror("Failed to connect data socket, retrying");
        sleep(1);
    }

    close(data_socket);
    return -1;
}

void handle_retr(int client_socket, int data_socket, char *filename) {
    trim_whitespace(filename);
    trim_newline(filename);
    FILE *file = fopen(filename, "rb");
    if (!file) {
        send_response(client_socket, "550 File not found.\n");
        return;
    }

    send_response(client_socket, "150 Opening binary mode data connection for file transfer.\n");

    char buffer[1024];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(data_socket, buffer, bytes_read, 0);
    }
    fclose(file);
    close(data_socket);
    send_response(client_socket, "226 Transfer complete.\n");
}

void handle_stor(int client_socket, int data_socket, char *filename) {
    trim_whitespace(filename);
    trim_newline(filename);
    FILE *file = fopen(filename, "wb");
    if (!file) {
        send_response(client_socket, "550 Could not open file.\n");
        return;
    }

    send_response(client_socket, "150 Opening binary mode data connection for file reception.\n");

    char buffer[1024];
    int bytes_received;
    while ((bytes_received = recv(data_socket, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
    }
    fclose(file);
    close(data_socket);
    send_response(client_socket, "226 Transfer complete.\n");
}

void handle_cwd(int client_socket, char *directory) {
    trim_whitespace(directory);
    trim_newline(directory);
    if (chdir(directory) == 0) {
        send_response(client_socket, "250 Directory successfully changed.\r\n");
    } else {
        send_response(client_socket, "550 Failed to change directory.\r\n");
    }
}

void handle_pwd(int client_socket) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char msg[1060];
        sprintf(msg, "257 \"%s\" is the current directory.\r\n", cwd);
        send_response(client_socket, msg);
    } else {
        send_response(client_socket, "550 Get current directory failed.\r\n");
    }
}

void handle_list(int client_socket, int data_socket) {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (d) {
        send_response(client_socket, "150 File status okay; about to open data connection.\r\n");
        while ((dir = readdir(d)) != NULL) {
            send(data_socket, dir->d_name, strlen(dir->d_name), 0);
            send(data_socket, "\r\n", 2, 0);
        }
        closedir(d);
        close(data_socket);
        send_response(client_socket, "226 Transfer completed.\r\n");
    } else {
        send_response(client_socket, "550 Failed to open directory.\r\n");
    }
}

void handle_client(int client_socket, int logged_in, int user_index) {
    char buff[MAX];
    int data_socket = -1;
    char client_ip[16];
    int client_port = -1;

    while (1) {
        bzero(buff, MAX);
        int recv_length = recv(client_socket, buff, sizeof(buff), 0);
        if (recv_length <= 0) {
            printf("Failed to read data from client or client disconnected\n");
            close(client_socket);
            return;
        }

        printf("From client: %s\n", buff);

        char *command = strtok(buff, " ");
        char *argument = strtok(NULL, " ");

        if (command == NULL) {
            send_response(client_socket, "500 Syntax error, command unrecognized.\n");
            continue;
        }

        trim_newline(command);
        if (argument) {
            trim_newline(argument);
        }

        if (strcmp(command, "QUIT") == 0) {
            send_response(client_socket, "221 Service closing control connection.\n");
            close(client_socket);
            return;
        }

        if (!logged_in) {
            if (strcmp(command, "USER") == 0) {
                if (argument) {
                    user_index = check_username(argument);
                    if (user_index != -1) {
                        send_response(client_socket, "331 Username OK, need password.\n");
                    } else {
                        send_response(client_socket, "530 User not found.\n");
                    }
                } else {
                    send_response(client_socket, "501 Syntax error in parameters or arguments.\n");
                }
            } else if (strcmp(command, "PASS") == 0) {
                if (user_index != -1 && argument) {
                    if (authenticate(user_index, argument)) {
                        logged_in = 1;
                        send_response(client_socket, "230 User logged in, proceed.\n");
                    } else {
                        send_response(client_socket, "530 Wrong password.\n");
                    }
                } else {
                    send_response(client_socket, "503 Bad sequence of commands.\n");
                }
            } else {
                send_response(client_socket, "530 Please login with USER and PASS.\n");
            }
        } else {
            if (strcmp(command, "PORT") == 0) {
                if (argument) {
                    int h1, h2, h3, h4, p1, p2;
                    sscanf(argument, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
                    sprintf(client_ip, "%d.%d.%d.%d", h1, h2, h3, h4);
                    client_port = p1 * 256 + p2;
                    printf("Client IP: %s, Client Port: %d\n", client_ip, client_port);
                    data_socket = start_data_connection(client_ip, client_port);
                    if (data_socket != -1) {
                        send_response(client_socket, "200 PORT command successful.\n");
                    } else {
                        send_response(client_socket, "425 Can't open data connection.\n");
                    }
                } else {
                    send_response(client_socket, "501 Syntax error in parameters or arguments.\n");
                }
            } else if (strcmp(command, "RETR") == 0) {
                if (data_socket != -1 && argument) {
                    if (fork() == 0) {
                        handle_retr(client_socket, data_socket, argument);
                        exit(0);
                    }
                    close(data_socket);
                    data_socket = -1;
                } else {
                    send_response(client_socket, "425 Use PORT command to specify data connection first.\n");
                }
            } else if (strcmp(command, "STOR") == 0) {
                if (data_socket != -1 && argument) {
                    if (fork() == 0) {
                        handle_stor(client_socket, data_socket, argument);
                        exit(0);
                    }
                    close(data_socket);
                    data_socket = -1;
                } else {
                    send_response(client_socket, "425 Use PORT command to specify data connection first.\n");
                }
            } else if (strcmp(command, "LIST") == 0) {
                if (data_socket != -1) {
                    if (fork() == 0) {
                        handle_list(client_socket, data_socket);
                        exit(0);
                    }
                    close(data_socket);
                    data_socket = -1;
                } else {
                    send_response(client_socket, "425 Use PORT command to specify data connection first.\n");
                }
            } else if (strcmp(command, "CWD") == 0) {
                if (argument) {
                    handle_cwd(client_socket, argument);
                } else {
                    send_response(client_socket, "501 Syntax error in parameters or arguments.\n");
                }
            } else if (strcmp(command, "PWD") == 0) {
                handle_pwd(client_socket);
            } else {
                send_response(client_socket, "502 Command not implemented.\n");
            }
        }
    }
}

int main() {
    load_users();

    int server_socket, client_socket[MAX_CLIENTS], max_sd, sd, activity, i;
    struct sockaddr_in servaddr, cli;
    fd_set readfds;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully created..\n");

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if ((bind(server_socket, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        perror("socket bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully binded..\n");

    listen(server_socket, 5);
    printf("Server listening..\n");

    for (i = 0; i < MAX_CLIENTS; i++) {
        client_socket[i] = 0;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        max_sd = server_socket;

        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error");
        }

        if (FD_ISSET(server_socket, &readfds)) {
            int len = sizeof(cli);
            int new_socket = accept(server_socket, (SA*)&cli, &len);
            if (new_socket < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            printf("New connection, socket fd is %d, ip is : %s, port : %d \n", new_socket, inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    printf("Adding to list of sockets as %d\n", i);
                    break;
                }
            }
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];

            if (FD_ISSET(sd, &readfds)) {
                int logged_in = 0;
                int user_index = -1;

                // Create a new process to handle the client
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    exit(EXIT_FAILURE);
                }

                if (pid == 0) {
                    // Child process
                    close(server_socket); // Child doesn't need the listener
                    handle_client(sd, logged_in, user_index);
                    close(sd);
                    exit(0);
                } else {
                    // Parent process
                    close(sd); // Parent doesn't need this
                    client_socket[i] = 0;
                }
            }
        }

        // Clean up any zombie processes
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
    return 0;
}
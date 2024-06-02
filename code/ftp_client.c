#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX 80
#define PORT 8080
#define SA struct sockaddr

void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

int read_response(int sock, char *response) {
    char buffer[MAX];
    int bytes_read = 0;
    while (1) {
        bzero(buffer, MAX);
        int bytes = recv(sock, buffer, MAX, 0);
        if (bytes <= 0) {
            break;
        }
        printf("%s", buffer);
        if (response != NULL) {
            strcat(response, buffer);
        }
        bytes_read += bytes;
        if (bytes < MAX || buffer[bytes - 1] == '\n') {
            break;
        }
    }
    return bytes_read;
}

int send_command_and_wait(int sock, const char *command, char *response) {
    char full_command[MAX + 2];
    snprintf(full_command, sizeof(full_command), "%s\r\n", command);
    send(sock, full_command, strlen(full_command), 0);
    bzero(response, MAX);
    return read_response(sock, response);
}

int setup_data_connection(int *data_socket, int *client_port) {
    *data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (*data_socket == -1) {
        printf("Data socket creation failed...\n");
        return -1;
    }

    struct sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    for (*client_port = 8081; *client_port < 8100; (*client_port)++) {
        data_addr.sin_port = htons(*client_port);
        if (bind(*data_socket, (SA*)&data_addr, sizeof(data_addr)) == 0) {
            if (listen(*data_socket, 1) == 0) {
                return 0;
            }
        }
    }

    close(*data_socket);
    return -1;
}

int handle_data_connection(int data_socket, const char *filename, int is_retr, int is_list) {
    struct sockaddr_in cli;
    int len = sizeof(cli);
    int conn = accept(data_socket, (SA*)&cli, &len);
    if (conn < 0) {
        printf("Failed to accept data connection\n");
        return -1;
    }

    if (is_list) {
        char buffer[MAX];
        int bytes;
        while ((bytes = recv(conn, buffer, sizeof(buffer), 0)) > 0) {
            buffer[bytes] = '\0';
            printf("%s", buffer);
        }
    } else {
        FILE *file = NULL;
        if (is_retr) {
            file = fopen(filename, "wb");
            if (!file) {
                printf("550 No Such File or Directory.\n");
                close(conn);
                return -1;
            }
            char buffer[MAX];
            int bytes;
            while ((bytes = recv(conn, buffer, sizeof(buffer), 0)) > 0) {
                fwrite(buffer, 1, bytes, file);
            }
            fclose(file);
        } else {
            file = fopen(filename, "rb");
            if (!file) {
                close(conn);
                return -1;
            }
            char buffer[MAX];
            int bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                send(conn, buffer, bytes, 0);
            }
            fclose(file);
        }
    }

    close(conn);
    close(data_socket);

    return 0;
}

void func(int sock) {
    char buff[MAX];
    char response[MAX];
    int data_socket = -1, client_port = -1;

    int authenticated = 0; // Add this flag to track if the user is authenticated

    for (;;) {
        printf("ftp> ");
        bzero(buff, MAX);
        fgets(buff, MAX, stdin);
        trim_newline(buff);

        char* command = strtok(buff, " ");
        char* argument = strtok(NULL, " ");

        if (command != NULL) {
            int is_retr = 0;
            int is_stor = 0;
            int is_list = 0;

            if (strcmp(command, "RETR") == 0) {
                is_retr = 1;
            } else if (strcmp(command, "STOR") == 0) {
                is_stor = 1;
            } else if (strcmp(command, "LIST") == 0) {
                is_list = 1;
            } else if (strcmp(command, "!PWD") == 0) {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    printf("257 \"%s\" is the current directory.\r\n", cwd);
                } else {
                    printf("550 Get current directory failed.\r\n");
                }
                continue;
            } else if (strcmp(command, "!LIST") == 0) {
                system("ls");
                continue;
            } else if (strcmp(command, "!CWD") == 0) {
                if (argument) {
                    if (chdir(argument) == 0) {
                        printf("250 Directory successfully changed.\r\n");
                    } else {
                        printf("550 No Such File or Directory.\n");
                    }
                } else {
                    printf("501 Syntax error in parameters or arguments.\r\n");
                }
                continue;
            }

            if (is_retr || is_stor || is_list) {
                if (!authenticated) {
                    printf("Please login with USER and PASS first.\n");
                    continue;
                }
                if (setup_data_connection(&data_socket, &client_port) != -1) {
                    char port_command[MAX];
                    sprintf(port_command, "PORT 127,0,0,1,%d,%d", client_port / 256, client_port % 256);
                    send_command_and_wait(sock, port_command, response);
                } else {
                    printf("Failed to setup data connection\n");
                    continue;
                }
            }

            char command_with_args[MAX + 5];
            if (argument) {
                snprintf(command_with_args, sizeof(command_with_args), "%s %s", command, argument);
            } else {
                strncpy(command_with_args, command, sizeof(command_with_args));
            }
            send_command_and_wait(sock, command_with_args, response);

            // Check for successful login
            if (strcmp(command, "USER") == 0 || strcmp(command, "PASS") == 0) {
                if (strstr(response, "230 User logged in") != NULL) {
                    authenticated = 1; // Set the flag to indicate successful authentication
                }
            }

            if (strncmp(buff, "QUIT", 4) == 0) {
                printf("Client Exit...\n");
                break;
            }

            if (data_socket != -1) {
                if (is_retr) {
                    handle_data_connection(data_socket, argument, 1, 0);
                } else if (is_stor) {
                    handle_data_connection(data_socket, argument, 0, 0);
                } else if (is_list) {
                    handle_data_connection(data_socket, NULL, 0, 1);
                }
                read_response(sock, NULL);  // Read the final response after data connection handling
                data_socket = -1;
            }
        }
    }
}

int main() {
    int sock;
    struct sockaddr_in servaddr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Socket creation failed...\n");
        exit(0);
    } else {
        printf("Socket successfully created..\n");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(PORT);

    if (connect(sock, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("Connection with the server failed...\n");
        exit(0);
    } else {
        printf("Connected to the server..\n");
    }

    func(sock);
    close(sock);
    return 0;
}

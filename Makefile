CC = gcc
SRC_DIR = code
SERVER_OUTPUT_DIR = server
CLIENT_OUTPUT_DIR = client

all: ftp_server ftp_client

ftp_server: $(SRC_DIR)/ftp_server.c
	$(CC) -o $(SERVER_OUTPUT_DIR)/ftp_server $(SRC_DIR)/ftp_server.c

ftp_client: $(SRC_DIR)/ftp_client.c
	$(CC) -o $(CLIENT_OUTPUT_DIR)/ftp_client $(SRC_DIR)/ftp_client.c

clean:
	rm -f $(SERVER_OUTPUT_DIR)/ftp_server $(SERVER_OUTPUT_DIR)/ftp_client

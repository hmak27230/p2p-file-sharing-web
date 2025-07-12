#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_PEERS 100
#define MAX_CONTENT 20
#define BUFFER_SIZE 256
#define MAX_NAME_LENGTH 255 // Increased size for names

typedef struct {
    char peer_name[MAX_NAME_LENGTH]; // Increased size
    char content_name[MAX_NAME_LENGTH]; // Increased size
    struct sockaddr_in address; // Peer address
} ContentInfo;

ContentInfo content_list[MAX_PEERS][MAX_CONTENT]; // List of contents registered by peers
int content_count[MAX_PEERS]; // Count of contents for each peer

int sockfd; // Global variable for the socket file descriptor
pthread_mutex_t mutex; // Mutex for thread safety

void *handle_peer(void *arg);
void register_content(char *peer_name, char *content_name, struct sockaddr_in *addr);
void deregister_content(char *peer_name, char *content_name);
void search_content(char *content_name, struct sockaddr_in *client_addr);
void handle_list(struct sockaddr_in *client_addr, char *peer_name);
void send_error(struct sockaddr_in *client_addr, char *error_msg);
void handle_download(char *content_name, struct sockaddr_in *client_addr);

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // Initialize content count
    memset(content_count, 0, sizeof(content_count));
    pthread_mutex_init(&mutex, NULL); // Initialize the mutex

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Index Server is running on port %d\n", PORT);

    while (1) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);

        // Create a new thread to handle the peer request
        pthread_t thread_id;
        struct {
            char buffer[BUFFER_SIZE];
            struct sockaddr_in addr;
        } *args = malloc(sizeof(*args)); // Allocate memory for arguments
        strncpy(args->buffer, buffer, BUFFER_SIZE);
        args->addr = client_addr;

        pthread_create(&thread_id, NULL, handle_peer, (void *)args);
        pthread_detach(thread_id);
    }

    close(sockfd);
    pthread_mutex_destroy(&mutex); // Clean up the mutex
    return 0;
}

void *handle_peer(void *arg) {
    struct {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in addr;
    } *args = (void *)arg;

    char *buffer = args->buffer;
    struct sockaddr_in client_addr = args->addr;
    char command[2];
    char peer_name[MAX_NAME_LENGTH]; // Increased size
    char content_name[MAX_NAME_LENGTH]; // Increased size

    // Read the command and names
    sscanf(buffer, "%1s %s %s", command, peer_name, content_name);

    if (strcmp(command, "R") == 0) {
        // Register content
        register_content(peer_name, content_name, &client_addr);
    } else if (strcmp(command, "Q") == 0) {
        // Deregister content
        deregister_content(peer_name, content_name);
    } else if (strcmp(command, "D") == 0) {
        // Handle download request
        handle_download(content_name, &client_addr);
    } else if (strcmp(command, "L") == 0) {
    // Handle list command
        handle_list(&client_addr, peer_name); // Pass peer_name
    } else if (strcmp(command, "S") == 0) {
        // Handle search command
        search_content(content_name, &client_addr);
    } else {
        send_error(&client_addr, "Invalid command");
    }

    free(args); // Free allocated memory for arguments
    return NULL;
}

void search_content(char *content_name, struct sockaddr_in *client_addr) {
    pthread_mutex_lock(&mutex); // Lock for thread safety

    char response[BUFFER_SIZE];
    int found = 0;

    for (int i = 0; i < MAX_PEERS; i++) {
        for (int j = 0; j < content_count[i]; j++) {
            if (strcmp(content_list[i][j].content_name, content_name) == 0) {
                // Content found, prepare response
                sprintf(response, "Content '%s' found at %s:%d", content_name,
                        inet_ntoa(content_list[i][j].address.sin_addr),
                        ntohs(content_list[i][j].address.sin_port));
                sendto(sockfd, response, strlen(response), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
                printf("Sent search response to client: %s\n", response);
                found = 1;
                pthread_mutex_unlock(&mutex); // Unlock before returning
                return;
            }
        }
    }

    // If content not found
    if (!found) {
        send_error(client_addr, "Content not found");
    }

    pthread_mutex_unlock(&mutex); // Unlock
}

void handle_list(struct sockaddr_in *client_addr, char *peer_name) {
    pthread_mutex_lock(&mutex); // Lock for thread safety

    char response[BUFFER_SIZE];
    strcpy(response, "Registered content:\n");

    int found_content = 0;
    int peer_index = -1;

    // Find the peer index based on the peer_name
    for (int i = 0; i < MAX_PEERS; i++) {
        if (strcmp(content_list[i][0].peer_name, peer_name) == 0) {
            peer_index = i;
            break;
        }
    }

    if (peer_index != -1) {
        for (int j = 0; j < content_count[peer_index]; j++) {
            strcat(response, content_list[peer_index][j].content_name);
            strcat(response, "\n");
            found_content = 1;
        }
    }

    if (!found_content) {
        strcat(response, "No content registered.");
    }

    sendto(sockfd, response, strlen(response), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
    printf("Sent content list to client: %s\n", response);

    pthread_mutex_unlock(&mutex); // Unlock
}
void register_content(char *peer_name, char *content_name, struct sockaddr_in *addr) {
    pthread_mutex_lock(&mutex); // Lock for thread safety

    // Find the peer index
    int peer_index = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (strcmp(content_list[i][0].peer_name, peer_name) == 0) {
            peer_index = i;
            break;
        }
    }

    // If peer is not found, add it
    if (peer_index == -1) {
        for (int i = 0; i < MAX_PEERS; i++) {
            if (content_list[i][0].peer_name[0] == '\0') { // Empty slot
                peer_index = i;
                strcpy(content_list[i][0].peer_name, peer_name);
                break;
            }
        }
    }

    // Register content
    if (peer_index != -1 && content_count[peer_index] < MAX_CONTENT) {
        strcpy(content_list[peer_index][content_count[peer_index]].content_name, content_name);
        content_list[peer_index][content_count[peer_index]].address = *addr;

        // Now read the file data from the socket
        char file_path[MAX_NAME_LENGTH];
        sprintf(file_path, "./%s", content_name); // Save the file in the current directory

        // Open the file for writing
        FILE *file = fopen(file_path, "wb");
        if (!file) {
            send_error(addr, "Failed to create file");
            pthread_mutex_unlock(&mutex); // Unlock
            return;
        }

        // Read the file data from the socket
        char buffer[BUFFER_SIZE];
        ssize_t bytes_received;
        while ((bytes_received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL)) > 0) {
            fwrite(buffer, 1, bytes_received, file);
        }

        // Check if the loop exited due to an error
        if (bytes_received < 0) {
            perror("Failed to receive file data");
            fclose(file);
            send_error(addr, "Failed to receive file data");
            pthread_mutex_unlock(&mutex); // Unlock
            return;
        }

        fclose(file);
        content_count[peer_index]++;
        printf("Registered content '%s' for peer '%s'\n", content_name, peer_name);
    } else {
        send_error(addr, "Content registration failed");
    }

    pthread_mutex_unlock(&mutex); // Unlock
}

void deregister_content(char *peer_name, char *content_name) {
    pthread_mutex_lock(&mutex); // Lock for thread safety

    for (int i = 0; i < MAX_PEERS; i++) {
        if (strcmp(content_list[i][0].peer_name, peer_name) == 0) {
            for (int j = 0; j < content_count[i]; j++) {
                if (strcmp(content_list[i][j].content_name, content_name) == 0) {
                    // Remove content
                    for (int k = j; k < content_count[i] - 1; k++) {
 content_list[i][k] = content_list[i][k + 1];
                    }
                    content_count[i]--;
                    printf("Deregistered content '%s' for peer '%s'\n", content_name, peer_name);
                    pthread_mutex_unlock(&mutex); // Unlock
                    return;
                }
            }
        }
    }

    pthread_mutex_unlock(&mutex); // Unlock
}

void handle_download(char *content_name, struct sockaddr_in *client_addr) {
    pthread_mutex_lock(&mutex); // Lock for thread safety

    for (int i = 0; i < MAX_PEERS; i++) {
        for (int j = 0; j < content_count[i]; j++) {
            if (strcmp(content_list[i][j].content_name, content_name) == 0) {
                // Send the content file to the client
                char response[BUFFER_SIZE];
                sprintf(response, "Content found at %s:%d", inet_ntoa(content_list[i][j].address.sin_addr), ntohs(content_list[i][j].address.sin_port));
                sendto(sockfd, response, strlen(response), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
                printf("Sent response to client: %s\n", response);

                // Now send the actual file over a TCP connection
                int tcp_sock;
                struct sockaddr_in content_server_addr;
                char file_path[MAX_NAME_LENGTH];
                sprintf(file_path, "./%s", content_name); // Assuming files are stored in the current directory

                // Create TCP socket for sending the file
                if ((tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    perror("TCP socket creation failed");
                    pthread_mutex_unlock(&mutex); // Unlock
                    return;
                }

                // Prepare content server address
                memset(&content_server_addr, 0, sizeof(content_server_addr));
                content_server_addr.sin_family = AF_INET;
                content_server_addr.sin_addr = content_list[i][j].address.sin_addr; // Correctly copy the address
                content_server_addr.sin_port = content_list[i][j].address.sin_port; // Use the same port as the peer

                // Bind the TCP socket to a port
                if (bind(tcp_sock, (struct sockaddr *)&content_server_addr, sizeof(content_server_addr)) < 0) {
                    perror("Bind failed for TCP socket");
                    close(tcp_sock);
                    pthread_mutex_unlock(&mutex); // Unlock
                    return;
                }

                // Listen for incoming connections
                listen(tcp_sock, 1);
                printf("Waiting for a connection to send file '%s'...\n", content_name);

                // Accept the incoming connection
                int new_sock;
                socklen_t addr_len = sizeof(content_server_addr);
                if ((new_sock = accept(tcp_sock, (struct sockaddr *)&content_server_addr, &addr_len)) < 0) {
                    perror("Failed to accept connection");
                    close(tcp_sock);
                    pthread_mutex_unlock(&mutex); // Unlock
                    return;
                }

                // Open the file to send
                FILE *file = fopen(file_path, "rb");
                if (!file) {
                    perror("File not found");
                    close(new_sock);
                    close(tcp_sock);
                    pthread_mutex_unlock(&mutex); // Unlock
                    return;
                }

                // Send the file
                char file_buffer[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
                    send(new_sock, file_buffer, bytes_read, 0);
                }

                fclose(file);
                close(new_sock);
                close(tcp_sock);
                printf("File '%s' sent successfully.\n", content_name);
                pthread_mutex_unlock(&mutex); // Unlock
                return;
            }
        }
    }

    send_error(client_addr, "Content not found");
    pthread_mutex_unlock(&mutex); // Unlock
}

void send_error(struct sockaddr_in *client_addr, char *error_msg) {
    sendto(sockfd, error_msg, strlen(error_msg), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
    printf("Sent error to client: %s\n", error_msg);
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 256
#define MAX_CONTENT_NAME 20
#define MAX_PEER_NAME 20

void register_content(const char *peer_name, const char *content_name, int sockfd, struct sockaddr_in *server_addr);
void deregister_content(const char *peer_name, int sockfd, struct sockaddr_in *server_addr);
void search_content(const char *content_name, int sockfd, struct sockaddr_in *server_addr);
void send_download_request(const char *content_name, const char *server_ip, int server_port);
void list_content(int sockfd, struct sockaddr_in *server_addr);
int send_to_server(int sockfd, struct sockaddr_in *server_addr, const char *message);

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char peer_name[MAX_PEER_NAME + 1];
    char command[2];
    char content_name[MAX_CONTENT_NAME + 1];

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Change to server IP if needed
    server_addr.sin_port = htons(SERVER_PORT);

    // Get peer name
    printf("Enter your peer name (max %d characters): ", MAX_PEER_NAME);
    fgets(peer_name, sizeof(peer_name), stdin);
    peer_name[strcspn(peer_name, "\n")] = 0; // Remove newline character

    while (1) {
        printf("\n--- Peer-to-Peer Menu ---\n");
        printf("R: Register content\n");
        printf("D: Download content\n");
        printf("S: Search for content\n");
        printf("L: List available content\n");
        printf("Q: Quit (and deregister)\n");
        printf("Enter command: ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline character

        // Wait for the user to press Enter
        printf("Press Enter to continue...\n");
        getchar(); // Wait for the user to press Enter

        switch (command[0]) {
            case 'R':
                printf("Enter content name to register (max %d characters): ", MAX_CONTENT_NAME);
                fgets(content_name, sizeof(content_name), stdin);
                content_name[strcspn(content_name, "\n")] = 0; // Remove newline character

                // Check if content_name is empty
                if (strlen(content_name) == 0) {
                    printf("Content name cannot be empty. Please try again.\n");
                    break;
                }

                register_content(peer_name, content_name, sockfd, &server_addr);
                break;
            case 'D':
                printf("Enter content name to download (max %d characters): ", MAX_CONTENT_NAME);
                fgets(content_name, sizeof(content_name), stdin);
                content_name[strcspn(content_name, "\n")] = 0; // Remove newline character
                send_download_request(content_name, "127.0.0.1", SERVER_PORT); // Replace with actual server IP and port
                break;
            case 'S':
                printf("Enter content name to search (max %d characters): ", MAX_CONTENT_NAME);
                fgets(content_name, sizeof(content_name), stdin);
                content_name[strcspn(content_name, "\n")] = 0; // Remove newline character
                search_content(content_name, sockfd, &server_addr);
                break;
            case 'L':
                list_content(sockfd, &server_addr);
                break;
            case 'Q':
                printf("Deregistering content and exiting...\n");
                deregister_content(peer_name, sockfd, &server_addr); // Deregister without specific content
                close(sockfd);
                return 0;
            default:
                printf("Invalid command. Please try again.\n");
                break;
        }
    }

    close(sockfd);
    return 0;
}

int send_to_server(int sockfd, struct sockaddr_in *server_addr, const char *message) {
    if (sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) <  0) {
        perror("Failed to send message");
        return -1;
    }
    return 0;
}

void register_content(const char *peer_name, const char *content_name, int sockfd, struct sockaddr_in *server_addr) {
    char buffer[BUFFER_SIZE];
    char file_path[BUFFER_SIZE];

    // Prompt for the file path
    printf("Enter the file path to register for content '%s': ", content_name);
    fgets(file_path, sizeof(file_path), stdin);
    file_path[strcspn(file_path, "\n")] = 0; // Remove newline character

    // Open the file
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    // Send the registration request
    sprintf(buffer, "R %s %s", peer_name, content_name);
    if (send_to_server(sockfd, server_addr, buffer) != 0) {
        fclose(file);
        return;
    }

    // Read the file and send its contents to the server
    while (1) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
        if (bytes_read > 0) {
            if (send_to_server(sockfd, server_addr, buffer) < 0) {
                perror("Failed to send file data to server");
                break;
            }
        }
        if (bytes_read < BUFFER_SIZE) {
            // End of file or error
            if (feof(file)) {
                printf("End of file reached.\n");
                break;
            }
            if (ferror(file)) {
                perror("Error reading file");
                break;
            }
        }
    }

    fclose(file);
    printf("Registered content '%s' from peer '%s'\n", content_name, peer_name);
}

void deregister_content(const char *peer_name, int sockfd, struct sockaddr_in *server_addr) {
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "Q %s", peer_name); // Use 'Q' for deregistration
    if (send_to_server(sockfd, server_addr, buffer) == 0) {
        printf("Deregistered content from peer '%s'\n", peer_name);
    }
}

void search_content(const char *content_name, int sockfd, struct sockaddr_in *server_addr) {
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "S %s", content_name);
    if (send_to_server(sockfd, server_addr, buffer) < 0) {
        return;
    }

    // Receive response from index server
    socklen_t addr_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)server_addr, &addr_len);
    if (n < 0) {
        perror("Failed to receive search response");
        return;
    }
    buffer[n] = '\0';
    
    // Debug print to see the full response
    printf("Received from server: %s\n", buffer);
    
    // If content is found, initiate download
    if (strncmp(buffer, "Content", 7) == 0) { // Check if response starts with "Content"
        char server_ip[INET_ADDRSTRLEN];
        int server_port;

        // Parse the server IP and port from the response
        if (sscanf(buffer, "Content '%*s' found at %s:%d", server_ip, &server_port) == 2) {
            send_download_request(content_name, server_ip, server_port);
        } else {
            printf("Invalid response format from server.\n");
        }
    } else {
        printf("Search response: %s\n", buffer);
    }
}

void send_download_request(const char *content_name, const char *server_ip, int server_port) {
    int tcp_sock;
    struct sockaddr_in content_server_addr;
    char buffer[BUFFER_SIZE];

    // Create TCP socket for downloading content
    if ((tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        return;
    }

    // Prepare content server address
    memset(&content_server_addr, 0, sizeof(content_server_addr));
    content_server_addr.sin_family = AF_INET;
    content_server_addr.sin_addr.s_addr = inet_addr(server_ip);
    content_server_addr.sin_port = htons(server_port);

    // Connect to the content server
    if (connect(tcp_sock, (struct sockaddr *)&content_server_addr, sizeof(content_server_addr)) < 0) {
        perror("Connection to content server failed");
        close(tcp_sock);
        return;
    }

    // Send download request
    sprintf(buffer, "D %s", content_name);
    if (send(tcp_sock, buffer, strlen(buffer), 0) < 0) {
        perror("Failed to send download request");
        close(tcp_sock);
        return;
    }
    printf("Download request sent for content '%s'\n", content_name);

    // Receive content data and save to file
    FILE *file = fopen(content_name, "wb");
    if (!file) {
        perror("File creation failed");
        close(tcp_sock);
        return;
    }

    while (1) {
        int bytes_received = recv(tcp_sock, buffer, BUFFER_SIZE, 0);
        if (bytes_received < 0) {
            perror("Error receiving data");
            break; // Connection closed or error
        }
        if (bytes_received == 0) {
            break; // Connection closed
        }
        fwrite(buffer, 1, bytes_received, file);
    }

    fclose(file);
    close(tcp_sock);
    printf("Download completed for content '%s'\n", content_name);
}

void list_content(int sockfd, struct sockaddr_in *server_addr) {
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "L"); // Send 'L' to request the list of content
    if (send_to_server(sockfd, server_addr, buffer) < 0) {
        return;
    }

    // Receive response from the index server
    socklen_t addr_len = sizeof(*server_addr);
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)server_addr, &addr_len);
    if (n <  0) {
        perror("Failed to receive content list");
        return;
    }
    buffer[n] = '\0';

    if (n > 0) {
        printf("Available content:\n%s\n", buffer);
    } else {
        printf("Failed to retrieve content list.\n");
    }
}
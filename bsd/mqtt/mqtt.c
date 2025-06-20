/*
 * Simple MQTT Client for PDP-11/83 to Report CPU Usage
 * Written in ANSI C (K&R compatible) for 211BSD
 * Implements MQTT CONNECT with Username and Password Authentication
 * Parses command-line options using getopt()
 * Parses command-line options using getopt()
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>     /* For getopt() */     /* For getopt() */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>  /* For inet_addr */

/* Define default MQTT broker details */
#define DEFAULT_HOSTNAME "localhost"
#define DEFAULT_PORT     1883
#define DEFAULT_TOPIC    "pdp11/cpu_usage"
#define DEFAULT_VALUE    "42.5%"
#define CLIENT_ID        "pdp11"

/* Function prototypes */
int connect_to_broker(char *broker_ip, int port);
int send_mqtt_connect(int sock, char *username, char *password);
int send_mqtt_connect(int sock, char *username, char *password);
int receive_connack(int sock);
int publish_message(int sock, char *topic, char *message);
int send_disconnect(int sock);
int encode_remaining_length(int length, unsigned char *buffer);
int send_all(int sock, unsigned char *buffer, int length);
int NumberOfUsers(void);
int CPUUsage(void);

/* Custom memmove implementation if bcopy is unavailable */
#ifndef HAVE_BCOPY
#ifndef HAVE_BCOPY
void my_memmove(char *dest, char *src, int n) {
    int i;
    if (src < dest) {
        for (i = n - 1; i >= 0; i--) {
            dest[i] = src[i];
        }
    } else {
        for (i = 0; i < n; i++) {
            dest[i] = src[i];
        }
    }
}
#endif
#endif

int main(int argc, char *argv[]) {
    int sock;
    char *hostname;
    char *username = "";
    char *password = "";
    char *topic = DEFAULT_TOPIC;
    char *value = DEFAULT_VALUE;
    int rc;
    int opt;
    extern char *optarg;
    extern int optind;

    /* Check if no arguments are provided */
    if (argc == 1) {
        fprintf(stderr, "Usage: %s [-h hostname] [-u username] [-p password] [-t topic] [-v value]\n", argv[0]);
        exit(1);
    }

    /* Initialize hostname to default */
    hostname = DEFAULT_HOSTNAME;

    /* Parse command-line options */
    while ((opt = getopt(argc, argv, "h:u:p:t:v:")) != EOF) {
        switch (opt) {
            case 'h':
                hostname = optarg;
                break;
            case 'u':
                username = optarg;
                break;
            case 'p':
                password = optarg;
                break;
            case 't':
                topic = optarg;
                break;
            case 'v':
                value = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-h hostname] [-u username] [-p password] [-t topic] [-v value]\n", argv[0]);
                exit(1);
        }
    }

    /* Ensure mandatory arguments are provided */
    if (username[0] == '\0' || password[0] == '\0') {
        fprintf(stderr, "Error: Username and password are required.\n");
        fprintf(stderr, "Usage: %s [-h hostname] [-u username] [-p password] [-t topic] [-v value]\n", argv[0]);
        exit(1);
    }

    /* Connect to MQTT broker */
    sock = connect_to_broker(hostname, DEFAULT_PORT);
    sock = connect_to_broker(hostname, DEFAULT_PORT);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to MQTT broker.\n");
        exit(1);
    }

    /* Send MQTT CONNECT packet */
    rc = send_mqtt_connect(sock, username, password);
    if (rc != 0) {
        fprintf(stderr, "Failed to send MQTT CONNECT packet.\n");
        close(sock);
        exit(1);
    }

    /* Receive CONNACK packet */
    rc = receive_connack(sock);
    if (rc != 0) {
        fprintf(stderr, "Failed to receive valid CONNACK from broker.\n");
        close(sock);
        exit(1);
    }

    /* Publish the message */
    rc = publish_message(sock, topic, value);
    if (rc != 0) {
        fprintf(stderr, "Failed to publish message.\n");
        close(sock);
        exit(1);
    } else {
        printf("Published: %s\n", value);
    }

    /* Send MQTT DISCONNECT packet */
    rc = send_disconnect(sock);
    if (rc != 0) {
        fprintf(stderr, "Failed to send DISCONNECT packet.\n");
    }

    /* Close the socket */
    close(sock);

    return 0;
}


/*
 * Function to establish a TCP connection to the MQTT broker.
 */
int connect_to_broker(char *broker_ip, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    /* Create socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }

    /* Zero out the server address structure */
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    /* Convert IP address from string to binary */
    serv_addr.sin_addr.s_addr = inet_addr(broker_ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "ERROR, invalid IP address: %s\n", broker_ip);
        close(sockfd);
        return -1;
    }

    /* Connect to the broker */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(sockfd);
        return -1;
    }

    printf("Connected to MQTT broker at %s:%d\n", broker_ip, port);
    return sockfd;
}

/*
 * Function to encode the "Remaining Length" field in MQTT packets.
 * Returns the number of bytes used for encoding.
 */
int encode_remaining_length(int length, uint8_t *buffer) {
    int index = 0;
    int encoded_length = 0;

    do {
        uint8_t encoded_byte = length % 128;
        length /= 128;
        /* if there are more digits to encode, set the top bit of this digit */
        if (length > 0) {
            encoded_byte |= 128;
        }
        buffer[index++] = encoded_byte;
        encoded_length++;
    } while (length > 0 && index < 4); /* MQTT Remaining Length can be up to 4 bytes */

    return encoded_length;
}

/*
 * Function to send all bytes in a buffer over a socket.
 * Ensures that all data is sent.
 */
int send_all(int sock, uint8_t *buffer, int length) {
    int total_sent = 0;
    int bytes_left = length;
    int n;

    while (total_sent < length) {
        n = send(sock, buffer + total_sent, bytes_left, 0);
        if (n <= 0) {
            perror("ERROR sending data");
            return -1;
        }
        total_sent += n;
        bytes_left -= n;
    }

    return 0;
}

/*
 * Function to send an MQTT CONNECT packet with authentication.
 */
int send_mqtt_connect(int sock, char * USERNAME, char * PASSWORD) {
    uint8_t connect_packet[256];
    int index;
    int remaining_length;
    uint8_t remaining_length_encoded[4];
    int rl_len;

    /* Initialize index */
    index = 0;

    /* Fixed Header: MQTT Control Packet type (CONNECT) and flags */
    connect_packet[index++] = 0x10; /* CONNECT packet type */

    /* Variable Header and Payload preparation */

    /* Variable Header */

    /* Protocol Name */
    {
        char *protocol_name = "MQTT";
        int protocol_name_length = strlen(protocol_name);
        connect_packet[index++] = (protocol_name_length >> 8) & 0xFF; /* MSB */
        connect_packet[index++] = protocol_name_length & 0xFF;        /* LSB */
        memcpy(&connect_packet[index], protocol_name, protocol_name_length);
        index += protocol_name_length;
    }

    /* Protocol Level */
    connect_packet[index++] = 0x04; /* MQTT version 3.1.1 */

    /* Connect Flags */
    {
        uint8_t connect_flags = 0;
        connect_flags |= 0x02; /* Clean Session */
        if (USERNAME[0] != '\0') {
            connect_flags |= 0x80; /* Username Flag */
        }
        if (PASSWORD[0] != '\0') {
            connect_flags |= 0x40; /* Password Flag */
        }
        connect_packet[index++] = connect_flags;
    }

    /* Keep Alive */
    {
        uint16_t keep_alive = 60; /* seconds */
        connect_packet[index++] = (keep_alive >> 8) & 0xFF;
        connect_packet[index++] = keep_alive & 0xFF;
    }

    /* Payload */

    /* Client ID */
    {
        char *client_id = CLIENT_ID;
        int client_id_length = strlen(client_id);
        connect_packet[index++] = (client_id_length >> 8) & 0xFF;
        connect_packet[index++] = client_id_length & 0xFF;
        memcpy(&connect_packet[index], client_id, client_id_length);
        index += client_id_length;
    }

    /* Username */
    if (USERNAME[0] != '\0') {
        int username_length = strlen(USERNAME);
        connect_packet[index++] = (username_length >> 8) & 0xFF;
        connect_packet[index++] = username_length & 0xFF;
        memcpy(&connect_packet[index], USERNAME, username_length);
        index += username_length;
    }

    /* Password */
    if (PASSWORD[0] != '\0') {
        int password_length = strlen(PASSWORD);
        connect_packet[index++] = (password_length >> 8) & 0xFF;
        connect_packet[index++] = password_length & 0xFF;
        memcpy(&connect_packet[index], PASSWORD, password_length);
        index += password_length;
    }

    /* Calculate Remaining Length */
    remaining_length = index - 1; /* Exclude the fixed header byte */
    rl_len = encode_remaining_length(remaining_length, remaining_length_encoded);

    /* Shift the variable header and payload to make room for the remaining length */
    /* Use bcopy instead of memmove */
    bcopy(&connect_packet[1], &connect_packet[1 + rl_len], remaining_length);
    /* Insert the encoded Remaining Length */
    bcopy(remaining_length_encoded, &connect_packet[1], rl_len);
    /* Update the total length */
    {
        int total_length = 1 + rl_len + remaining_length;
        /* Send the CONNECT packet */
        if (send_all(sock, connect_packet, total_length) != 0) {
            fprintf(stderr, "Failed to send MQTT CONNECT packet.\n");
            return -1;
        }
    }

    printf("Sent MQTT CONNECT packet.\n");
    return 0;
}

/*
 * Function to receive and validate the CONNACK packet from the broker.
 */
int receive_connack(int sock) {
    uint8_t connack[4];
    int n;
    int i;

    /* CONNACK is 4 bytes: Fixed Header (2 bytes) + Variable Header (2 bytes) */
    for (i = 0; i < 4; i++) {
        n = recv(sock, &connack[i], 1, 0);
        if (n <= 0) {
            fprintf(stderr, "Failed to receive CONNACK byte %d.\n", i + 1);
            return -1;
        }
    }

    /* Validate Fixed Header */
    if (connack[0] != 0x20) { /* CONNACK packet type */
        fprintf(stderr, "Invalid CONNACK packet type: 0x%02X\n", connack[0]);
        return -1;
    }

    /* Validate Remaining Length */
    if (connack[1] != 0x02) {
        fprintf(stderr, "Invalid CONNACK remaining length: %d\n", connack[1]);
        return -1;
    }

    /* Check Connection Acknowledgment Flags and Return Code */
    {
        uint8_t session_present = connack[2] & 0x01;
        uint8_t return_code = connack[3];

        if (return_code != 0x00) {
            fprintf(stderr, "Connection refused, return code: %d\n", return_code);
            return -1;
        }
    }

    printf("Received CONNACK, connection successful.\n");
    return 0;
}

/*
 * Function to publish a message to a specified MQTT topic.
 */
int publish_message(int sock, char *topic, char *message) {
    uint8_t publish_packet[512];
    int index;
    int remaining_length;
    uint8_t remaining_length_encoded[4];
    int rl_len;

    /* Initialize index */
    index = 0;

    /* Fixed Header: PUBLISH packet type and flags */
    publish_packet[index++] = 0x30; /* PUBLISH with QoS 0 */

    /* Variable Header and Payload preparation */

    /* Topic Name */
    {
        int topic_length = strlen(topic);
        publish_packet[index++] = (topic_length >> 8) & 0xFF;
        publish_packet[index++] = topic_length & 0xFF;
        memcpy(&publish_packet[index], topic, topic_length);
        index += topic_length;
    }

    /* Payload */
    {
        char *payload = message;
        int payload_length = strlen(payload);
        memcpy(&publish_packet[index], payload, payload_length);
        index += payload_length;
    }

    /* Calculate Remaining Length */
    remaining_length = (strlen(topic) + strlen(message));
    rl_len = encode_remaining_length(remaining_length, remaining_length_encoded);

    /* Shift the variable header and payload to make room for the remaining length */
    /* Use bcopy instead of memmove */
    bcopy(&publish_packet[1], &publish_packet[1 + rl_len], remaining_length);
    /* Insert the encoded Remaining Length */
    bcopy(remaining_length_encoded, &publish_packet[1], rl_len);
    /* Update the total length */
    {
        int total_length = 1 + rl_len + remaining_length;
        /* Send the PUBLISH packet */
        if (send_all(sock, publish_packet, total_length) != 0) {
            fprintf(stderr, "Failed to send MQTT PUBLISH packet.\n");
            return -1;
        }
    }

    printf("Sent MQTT PUBLISH packet.\n");
    return 0;
}

/*
 * Function to send an MQTT DISCONNECT packet.
 */
int send_disconnect(int sock) {
    uint8_t disconnect_packet[2];
    int n;

    disconnect_packet[0] = 0xE0; /* DISCONNECT packet type */
    disconnect_packet[1] = 0x00; /* Remaining Length */

    if (send_all(sock, disconnect_packet, 2) != 0) {
        fprintf(stderr, "Failed to send MQTT DISCONNECT packet.\n");
        return -1;
    }

    printf("Sent MQTT DISCONNECT packet.\n");
    return 0;
}

/*
 * Dummy function to retrieve CPU usage.
 * This should be implemented to fetch actual CPU usage.
 */
float get_cpu_usage(void) {
    /* Placeholder for CPU usage retrieval */
    /* For demonstration, return a fixed value */
    return 42.5;
}

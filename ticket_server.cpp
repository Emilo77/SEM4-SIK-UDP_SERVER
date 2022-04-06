#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>
#include <unistd.h>
#include <cmath>

#include <map>

using std::string;

#define WRONG_PARAMETERS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4

#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define MAX_DESCRIPTION_SIZE 81
#define COOKIE_SIZE 48

#define BUFFER_SIZE 65507
char shared_buffer[BUFFER_SIZE];

#define PRINT_ERRNO()                                                  \
    do {                                                               \
        if (errno != 0) {                                              \
            fprintf(stderr, "Error: errno %d in %s at %s:%d\n%s\n",    \
              errno, __func__, __FILE__, __LINE__, strerror(errno));   \
            exit(EXIT_FAILURE);                                        \
        }                                                              \
    } while (0)


// Set `errno` to 0 and evaluate `x`. If `errno` changed, describe it and exit.
#define CHECK_ERRNO(x)                                                             \
    do {                                                                           \
        errno = 0;                                                                 \
        (void) (x);                                                                \
        PRINT_ERRNO();                                                             \
    } while (0)

#define ENSURE(x)                                                         \
    do {                                                                  \
        bool result = (x);                                                \
        if (!result) {                                                    \
            fprintf(stderr, "Error: %s was false in %s at %s:%d\n",       \
                #x, __func__, __FILE__, __LINE__);                        \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    } while (0)

#define PRINT_ERRNO()                                                  \
    do {                                                               \
        if (errno != 0) {                                              \
            fprintf(stderr, "Error: errno %d in %s at %s:%d\n%s\n",    \
              errno, __func__, __FILE__, __LINE__, strerror(errno));   \
            exit(EXIT_FAILURE);                                        \
        }                                                              \
    } while (0)

int bind_socket(uint16_t port) {
	int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // creating IPv4 UDP socket
	ENSURE(socket_fd > 0);
	// after socket() call; we should close(sock) on any execution path;

	struct sockaddr_in server_address{};
	server_address.sin_family = AF_INET; // IPv4
	server_address.sin_addr.s_addr = htonl(
			INADDR_ANY); // listening on all interfaces
	server_address.sin_port = htons(port);

	// bind the socket to a concrete address
	CHECK_ERRNO(bind(socket_fd, (struct sockaddr *) &server_address,
	                 (socklen_t) sizeof(server_address)));

	return socket_fd;
}

void send_message(int socket_fd, const struct sockaddr_in *client_address,
                  const char *message, size_t length) {
	socklen_t address_length = (socklen_t) sizeof(*client_address);
	int flags = 0;
	ssize_t sent_length = sendto(socket_fd, message, length, flags,
	                             (struct sockaddr *) client_address,
	                             address_length);
	ENSURE(sent_length == (ssize_t) length);
}

size_t
read_message(int socket_fd, struct sockaddr_in *client_address, char *buffer,
             size_t max_length) {
	socklen_t address_length = (socklen_t) sizeof(*client_address);
	int flags = 0; // we do not request anything special
	errno = 0;
	ssize_t len = recvfrom(socket_fd, buffer, max_length, flags,
	                       (struct sockaddr *) client_address, &address_length);
	if (len < 0) {
		PRINT_ERRNO();
	}
	return (size_t) len;
}

void exit_program(int status) {
	std::string message;
	switch (status) {
		case WRONG_PARAMETERS:
			message = "WRONG SERVER PARAMETERS";
			break;
		case WRONG_PATH:
			message = "WRONG PATH TO FILE PARAMETER";
			break;
		case WRONG_PORT:
			message = "WRONG PORT NUMBER PARAMETER";
			break;
		case WRONG_TIMEOUT:
			message = "WRONG TIMEOUT PARAMETER";
			break;
		default:
			message = "UNKNOWN";
	}

	message.append("\n");
	fprintf(stderr, "%s", message.c_str());
	exit(1);
}

void check_file_path(char *path) {
	if (access(path, F_OK) != 0) {
		exit_program(WRONG_PATH);
	}
}

void
check_port(char *port_str,
           int *port_ptr) { //może sprawdzić port 0, czyli errno dla strtoul?
	size_t port = strtoul(port_str, nullptr, 10);
	if (port <= 0 || port > UINT16_MAX) {
		exit_program(WRONG_PORT);
	}
	*port_ptr = (int) port;
}

void check_timeout(char *timeout_str, int *timeout_ptr) {
	size_t timeout = strtoul(timeout_str, nullptr, 10);
	if (timeout < 1 || timeout > 86400) {
		exit_program(WRONG_TIMEOUT);
	}
	*timeout_ptr = (int) timeout;
}

void check_parameters(int argc, char *argv[], int *port_ptr, int *timeout_ptr,
                      int *file_index) {
	if ((argc < 3 || argc > 7) || argc % 2 == 0)
		exit_program(WRONG_PARAMETERS);

	bool flag_file_occured = false;
	bool flag_port_occured = false;
	bool flag_timeout_occured = false;

	for (int i = 1; i < argc; i += 2) {
		if (strcmp(argv[i], "-f") == 0 && !flag_file_occured) {
			flag_file_occured = true;
			check_file_path(argv[i + 1]);
			*file_index = (i + 1);

		} else if (strcmp(argv[i], "-p") == 0 && !flag_port_occured) {
			flag_port_occured = true;
			check_port(argv[i + 1], port_ptr);

		} else if (strcmp(argv[i], "-t") == 0 && !flag_timeout_occured) {
			flag_timeout_occured = true;
			check_timeout(argv[i + 1], timeout_ptr);

		} else {
			exit_program(WRONG_PARAMETERS);
		}
	}

	if (argc >= 3 && !flag_file_occured)
		exit_program(WRONG_PARAMETERS);

	if ((argc >= 5 && (!flag_port_occured && !flag_timeout_occured)))
		exit_program(WRONG_PARAMETERS);

	if ((argc >= 7 && (!flag_port_occured || !flag_timeout_occured)))
		exit_program(WRONG_PARAMETERS);

}

int parse_id_from_buffer() {
	int event_id_arr[4];
	int result = 0;

	for (int i = 1; i < 5; i++) {
		event_id_arr[4 - i] = (int) (unsigned char) shared_buffer[i];
	}
	for (int i = 0; i < 4; i++) {
		result += event_id_arr[i] * (int) pow(256, i);
	}

	return result;
}

int parse_ticket_count_from_buffer() {
	int ticket_count_arr[2];
	int result = 0;

	ticket_count_arr[0] = (int) (unsigned char) shared_buffer[6];
	ticket_count_arr[1] = (int) (unsigned char) shared_buffer[5];

	for (int i = 0; i < 2; i++) {
		result += ticket_count_arr[i] * (int) pow(256, i);
	}

	return result;
}

char *parse_cookie_from_buffer() {
	char *cookie = (char *) malloc(sizeof(char) * COOKIE_SIZE);
	for (int i = 0; i < COOKIE_SIZE; i++) {
		cookie[i] = shared_buffer[i + 5];
	}

	return cookie;
}


void confirm_reservation(int event_id, int ticket_count) {

}

void send_events_to_client() {

}

void send_tickets_or_bad_request(int reservation_id, char *cookie) {

}

void send_information_to_client() {
	char message_id = shared_buffer[0];
	switch (message_id) {
		case '1': {
			send_events_to_client();
		}
		case '3': {
			int event_id = parse_id_from_buffer();
			int ticket_count = parse_ticket_count_from_buffer();
			confirm_reservation(event_id, ticket_count);
		}
		case '5': {
			int reservation_id = parse_id_from_buffer();
			char *cookie = parse_cookie_from_buffer();
			send_tickets_or_bad_request(reservation_id, cookie);
			free(cookie);
		}
		default: {
			return;
		}
	}
}

struct event {
	int tickets_available;
	char description[MAX_DESCRIPTION_SIZE];
} event;

int main(int argc, char *argv[]) {

	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;
	int file_index = -1;

	check_parameters(argc, argv, &port, &timeout, &file_index);


	char *file_path = argv[file_index];
	std::map<int, struct event> events_map;


	printf("Listening on port %u\n", port);

	int socket_fd = bind_socket(port);
	struct sockaddr_in client_address;
	size_t read_length;
	size_t counter = 0;

	char message_id;

	do {
		memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
		read_length = read_message(socket_fd, &client_address, shared_buffer,
		                           sizeof(shared_buffer));

		send_information_to_client();


	} while (read_length > 0);
}
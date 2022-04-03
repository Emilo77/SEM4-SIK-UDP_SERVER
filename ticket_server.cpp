#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

using std::string;

#define WRONG_PARAMETERS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4

#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

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

inline static void
send_message(int socket_fd, const void *message, size_t length, int flags) {
	errno = 0;
	ssize_t sent_length = send(socket_fd, message, length, flags);
	if (sent_length < 0) {
		PRINT_ERRNO();
	}
	ENSURE(sent_length == (ssize_t) length);
}

inline static size_t
receive_message(int socket_fd, void *buffer, size_t max_length, int flags) {
	errno = 0;
	ssize_t received_length = recv(socket_fd, buffer, max_length, flags);
	if (received_length < 0) {
		PRINT_ERRNO();
	}
	return (size_t) received_length;
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

bool check_file_path(char *path) {
	return  access( path, F_OK ) == 0;
}

bool
check_port(char *port_str) { //może sprawdzić port 0, czyli errno dla strtoul?
	size_t port = strtoul(port_str, nullptr, 10);
	return (port > 0 && port <= UINT16_MAX);
}

bool check_timeout(char *timeout_str) {
	size_t timeout = strtoul(timeout_str, nullptr, 10);
	return (timeout >= 1 && timeout <= 86400);
}

void check_parameters(int argc, char *argv[]) { //czy standardowe wyj
	if ((argc < 3 || argc > 7) || argc % 2 == 0)
		exit_program(WRONG_PARAMETERS);

	if (strcmp(argv[1], "-f") != 0)
		exit_program(WRONG_PARAMETERS);

	if (argc >= 5 && strcmp(argv[3], "-p") != 0)
		exit_program(WRONG_PARAMETERS);

	if (argc == 7 && strcmp(argv[5], "-t") != 0)
		exit_program(WRONG_PARAMETERS);

	if (!check_file_path(argv[2]))
		exit_program(WRONG_PATH);

	if (argc >= 5 && !check_port(argv[4]))
		exit_program(WRONG_PORT);

	if (argc >= 7 && !check_timeout(argv[6]))
		exit_program(WRONG_TIMEOUT);

}

int main(int argc, char *argv[]) {

	check_parameters(argc, argv);

	char *file_path = argv[2];
	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;

	if (argc >= 5)
		port = (int) strtoul(argv[4], nullptr, 10);
	if (argc >= 7)
		timeout = (int) strtoul(argv[6], nullptr, 10);




}
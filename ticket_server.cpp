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
#include <sys/stat.h>
#include <vector>

using std::string;

#define WRONG_PARAMETERS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4

#define GET_EVENTS 1
#define EVENTS 2
#define GET_RESERVATION 3
#define RESERVATION 4
#define GET_TICKETS 5
#define TICKETS 6
#define BAD_REQUEST 255

#define ONE_OCTET
#define TICKET_OCTETS 7


#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define MAX_DESCRIPTION_SIZE 81
#define COOKIE_SIZE 48
#define CONST_OCTETS 7
#define COUNTING_BASE 256

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

struct event {
	char description[MAX_DESCRIPTION_SIZE];
	int description_length;
	int tickets_available;
} event;

struct reservation {
	int event_id;
	int ticket_count;
	std::vector<char> cookie;
	size_t expiration_time;
} reservation;

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
	struct stat buffer;
	if (stat(path, &buffer) != 0) {
		exit_program(WRONG_PATH);
	}
}

void
check_port(char *port_str,
           int *port_ptr) {
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

std::vector<char> parse_cookie_from_buffer() {
	std::vector<char> cookie(COOKIE_SIZE, 0);
	for (int i = 0; i < COOKIE_SIZE; i++) {
		cookie[i] = shared_buffer[i + 5];
	}

	return cookie;
}

int event_size_in_buffer(struct event e) {
	return e.description_length + CONST_OCTETS;
}

std::vector<int> numbers_to_binary(int number, int octet_size) {
	std::vector<int> result(octet_size, 0);
	for (int i = octet_size - 1; i >= 0; i--) {
		result[i] = number % COUNTING_BASE;
		number /= COUNTING_BASE;
	}
	return result;
}

bool check_reservation(int event_id, int ticket_count,
                       std::map<int, struct event> &events_map,
                       std::map<int, struct reservation> &reservations_map) {

	if (reservations_map.find(event_id) == reservations_map.end())
		return false;

	if (ticket_count == 0)
		return false;

	if (events_map.at(event_id).tickets_available < ticket_count)
		return false;

	if (CONST_OCTETS + ticket_count * TICKET_OCTETS > BUFFER_SIZE)
		return false;   //sytuacja przepełnienia bufora

	return true;
}

std::vector<char> generate_cookie() { //todo!
	std::vector<char> cookie;
	return cookie;
}

void make_reservation_fill_buffer(int event_id, int ticket_count,
                                  std::map<int, struct event> &events_map,
                                  std::map<int, struct reservation> &reservations_map) {

	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer

	if (check_reservation(event_id, ticket_count, events_map,
	                      reservations_map)) {

		struct reservation new_reservation;
		new_reservation.event_id = event_id;
		new_reservation.ticket_count = ticket_count;
		new_reservation.expiration_time = 0; //todo!
		new_reservation.cookie = generate_cookie(); //todo!

	} else {

	}


}

void insert_event_to_buffer(struct event eve, int event_id, int counter) {
	std::vector<int> event_id_bin = numbers_to_binary(event_id, 4);
	std::vector<int> ticket_count_bin = numbers_to_binary(eve.tickets_available,
	                                                      2);

	for (int i = 0; i < 4; i++) {
		shared_buffer[counter + i] = (char) event_id_bin[i];
	}
	counter += 4;
	for (int i = 0; i < 2; i++) {
		shared_buffer[counter + i] = (char) ticket_count_bin[i];
	}
	counter += 2;
	shared_buffer[counter] = (char) eve.description_length;
	counter++;

	for (int i = 0; i < eve.description_length; i++) {
		shared_buffer[counter + i] = (char) eve.description[i];
	}
}


void fill_buffer_events(std::map<int, struct event> &events_map) {

	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
	shared_buffer[0] = (char) EVENTS;
	int counter = 1;

	for (auto element: events_map) {
		int event_id = element.first;
		struct event eve = element.second;
		int eve_size = event_size_in_buffer(eve);

		if (counter + eve_size > BUFFER_SIZE) {
			break;
		} else {
			insert_event_to_buffer(eve, event_id, counter);
			counter += eve_size;
		}
	}
}

void fill_buffer_tickets(int reservation_id, std::vector<char> &cookie) {

}

void
parse_from_file(char *file_path, std::map<int, struct event> &events_map) {
	FILE *fp = fopen(file_path, "r+");
	int event_id = 0;
	char description[MAX_DESCRIPTION_SIZE];
	char tickets_str[MAX_DESCRIPTION_SIZE];
	int description_length = 0;

	while (fgets(description, sizeof(description), fp)
	       && fgets(tickets_str, sizeof(tickets_str), fp)) {

		description_length = (int) strlen(description);

		struct event new_event;
		strcpy(new_event.description, description);
		new_event.description_length = description_length - 1;
		new_event.tickets_available = (int) strtoul(tickets_str, nullptr,
		                                            10);

		events_map.insert({event_id, new_event});

		event_id++;
	}
	fclose(fp);
}

void print_events(std::map<int, struct event> &events_map) {
	for (auto element: events_map) {
		printf("---------------------------------------\n");
		printf("Event_id: %d\n", element.first);
		printf("Description: ");
		for (int i = 0; i < element.second.description_length; i++) {
			printf("%c", element.second.description[i]);
		}
		printf("\nDescription ASCII: ");
		for (int i = 0; i < element.second.description_length; i++) {
			printf("%d ", element.second.description[i]);
		}
		printf("\nDescription length: %d\n",
		       element.second.description_length);
		printf("Tickets available: %d\n", element.second.tickets_available);
	}
}


int main(int argc, char *argv[]) {

	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;
	int file_index = -1;

	check_parameters(argc, argv, &port, &timeout, &file_index);


	char *file_path = argv[file_index];
	std::map<int, struct event> events_map;
	std::map<int, struct reservation> reservations_map;

	parse_from_file(file_path, events_map);

	printf("Listening on port %u\n", port);

	int socket_fd = bind_socket(port);
	struct sockaddr_in client_address;
	size_t read_length;
	bool ignore_message = false;

	do {
		memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
		read_length = read_message(socket_fd, &client_address,
		                           shared_buffer,
		                           sizeof(shared_buffer));

		char message_id = shared_buffer[0];
		switch (message_id) {
			case '1': {
				fill_buffer_events(events_map);
				break;
			}
			case '3': {
				int event_id = parse_id_from_buffer();
				int ticket_count = parse_ticket_count_from_buffer();

				make_reservation_fill_buffer(event_id, ticket_count, events_map,
				                             reservations_map);
				break;
			}
			case '5': {
				int reservation_id = parse_id_from_buffer();
				std::vector<char> cookie = parse_cookie_from_buffer();
				fill_buffer_tickets(reservation_id, cookie);
				break;
			}
			default: {
				ignore_message = true;
			}
		}

		if (!ignore_message) {
			send_message(socket_fd, &client_address, shared_buffer,
			             sizeof(shared_buffer));
		}


	} while (read_length > 0);
}
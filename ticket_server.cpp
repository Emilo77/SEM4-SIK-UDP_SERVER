#include <iostream>
#include <random>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cmath>
#include <utility>
#include <vector>
#include <string>
#include <map>

using std::string;
using std::vector;
using eventMap = std::map<int, struct event>;
using reservationMap = std::map<int, struct reservation>;

#define WRONG_FLAGS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4

#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define GET_EVENTS (char) 1
#define EVENTS (char) 2
#define GET_RESERVATION (char) 3
#define RESERVATION (char) 4
#define GET_TICKETS (char) 5
#define TICKETS (char) 6
#define BAD_REQUEST (char) 255

#define ONE_OCT 1
#define TWO_OCT 2
#define FOUR_OCT 4
#define EIGHT_OCT 8

#define COOKIE_POS 5
#define CONST_OCTETS 7
#define TICKET_OCTETS 7
#define MAX_DESCRIPTION_SIZE 80
#define COOKIE_SIZE 48
#define COUNTING_BASE 256

#define GET_EVENTS_MSG_LENGTH 1
#define GET_RESERVATION_MSG_LENGTH 7
#define GET_TICKETS_MSG_LENGTH 53
#define RESERVATION_MSG_LENGTH 67
#define BAD_REQUEST_MSG_LENGTH 5

#define BUFFER_SIZE 65507
char shared_buffer[BUFFER_SIZE];
static char ticket_charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int ticket_current_id = 0;
int reservation_current_id = 0;

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

struct event {
	string description; //można zamienić na vector
	uint8_t description_length;
	uint16_t tickets_available;

	event(string description, uint8_t descriptionLength,
	      uint16_t ticketsAvailable) : description(std::move(description)),
	                                   description_length(descriptionLength),
	                                   tickets_available(ticketsAvailable) {}
};

struct reservation {
	uint32_t event_id;
	uint16_t ticket_count;
	time_t expiration_time;
	string cookie;
	bool achieved{false};
	vector<string> tickets;

	reservation(uint32_t eventId, uint16_t ticketCount, time_t expirationTime,
	            string cookie) : event_id(eventId),
	                             ticket_count(ticketCount),
	                             expiration_time(expirationTime),
	                             cookie(std::move(cookie)) {}

};

inline static char *get_ip(struct sockaddr_in *address) {
	return inet_ntoa(address->sin_addr);
}

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
	auto address_length = (socklen_t) sizeof(*client_address);
	int flags = 0;
	ssize_t sent_length = sendto(socket_fd, message, length, flags,
	                             (struct sockaddr *) client_address,
	                             address_length);
	ENSURE(sent_length == (ssize_t) length);
}

size_t
read_message(int socket_fd, struct sockaddr_in *client_address, char *buffer,
             size_t max_length) {
	auto address_length = (socklen_t) sizeof(*client_address);
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
	string message;
	switch (status) {
		case WRONG_FLAGS:
			message = "WRONG SERVER FLAGS";
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
			message = "WRONG PARAMETERS";
	}

	message.append("\n");
	printf("Usage: {argv[0]} -f <path to events file> "
	       "[-p <port>] [-t <timeout>]\n");
	fprintf(stderr, "%s", message.c_str());
	exit(1);
}

void check_file_path(char *path) {
	struct stat buffer{};
	if (stat(path, &buffer) != 0) {
		exit_program(WRONG_PATH);
	}
}

void check_port(char *port_str,
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
		exit_program(WRONG_FLAGS);

	bool flag_file_occurred = false;

	for (int i = 1; i < argc; i += 2) {
		if (strcmp(argv[i], "-f") == 0) {
			flag_file_occurred = true;
			check_file_path(argv[i + 1]);
			*file_index = (i + 1);

		} else if (strcmp(argv[i], "-p") == 0) {
			check_port(argv[i + 1], port_ptr);

		} else if (strcmp(argv[i], "-t") == 0) {
			check_timeout(argv[i + 1], timeout_ptr);

		} else {
			exit_program(WRONG_FLAGS);
		}
	}

	if (!flag_file_occurred)
		exit_program(WRONG_FLAGS);
}

int new_reservation_id() {
	return reservation_current_id++;
}

int new_ticket_id() {
	return ticket_current_id++;
}

int parse_number_from_buffer(int index, int size) {
	int arr[size];
	int result = 0;

	for (int i = 0; i < size; i++) {
		arr[size - 1 - i] = (int) (unsigned char) shared_buffer[index + i];
	}
	for (int i = 0; i < size; i++) {
		result += arr[i] * (int) pow(COUNTING_BASE, i);
	}
	return result;
}

char *parse_cookie_from_buffer() {
	static char cookie[COOKIE_SIZE];
	for (int i = 0; i < COOKIE_SIZE; i++) {
		cookie[i] = shared_buffer[i + COOKIE_POS];
	}
	return cookie;
}

bool check_reservation(int event_id, int ticket_count, eventMap &events_map,
                       reservationMap &reservations_map) {

	if (ticket_count == 0) {
		return false;
	}
	if (events_map.find(event_id) == events_map.end()) {
		return false;
	}
	if (events_map.at(event_id).tickets_available < ticket_count) {
		return false;
	}
	if ((TICKET_OCTETS + ticket_count * TICKET_OCTETS) > BUFFER_SIZE) {
		return false;//sytuacja przepełnienia bufora
	}

	return true;
}

string generate_cookie() {

	string cookie(COOKIE_SIZE, 33);
	std::random_device rd;   // non-deterministic generator
	std::mt19937 gen(rd());  // to seed mersenne twister.
	std::uniform_int_distribution<> dist(33, 126);
	// distribute results between 33 and 126 inclusive.
	for (int i = 0; i < COOKIE_SIZE; i++) {
		cookie[i] = (char) dist(gen);
	}
//	std::cout << cookie + "\n";
	return cookie;
}

string generate_ticket() {
	int ticket_id = new_ticket_id();
	string ticket(TICKET_OCTETS, 'A');
	size_t charset_size = strlen(ticket_charset);

	for (int i = TICKET_OCTETS - 1; i >= 0; i--) {
		ticket[i] = (ticket_charset[ticket_id % charset_size]);
		ticket_id /= (int) charset_size;
	}
	return ticket;
}

template<typename T>
void move_to_buff(vector<T> vec, int *index) {
	size_t size = vec.size();
	for (int i = 0; i < size; i++) {
		shared_buffer[*index + i] = (char) vec[i];
	}
	*index += (int) size;
}

template<typename T>
void move_to_buff(T number, int *index) {
	int octet_size = sizeof(T);
	vector<size_t> result(octet_size, 0);
	for (int i = octet_size - 1; i >= 0; i--) {
		result[i] = number % COUNTING_BASE;
		number = number >> 8;
	}
	move_to_buff(result, index);
}

void move_to_buff(string s, int *index) {
	size_t length = s.length();
	for (int i = 0; i < length; i++) {
		shared_buffer[*index + i] = (char) s[i];
	}
	*index += (int) length;
}

void insert_bad_request_to_buffer(int id) {
	int index = 0;

	move_to_buff(BAD_REQUEST, &index);
	move_to_buff(id, &index);
}

void insert_reservation_to_buffer(int reservation_id, reservation &r) {
	int index = 0;

	move_to_buff(RESERVATION, &index);
	move_to_buff(reservation_id, &index);
	move_to_buff(r.event_id, &index);
	move_to_buff(r.ticket_count, &index);
	move_to_buff(r.cookie, &index);
	move_to_buff(r.expiration_time, &index);
}

void send_reservation_or_bad(time_t expiration_time, eventMap &events_map,
                             reservationMap &reservations_map,
                             int *message_length) {

	int event_id = parse_number_from_buffer(1, FOUR_OCT);
	int ticket_count = parse_number_from_buffer(5, TWO_OCT);

	if (check_reservation(event_id, ticket_count, events_map,
	                      reservations_map)) {

		int reservation_id = new_reservation_id();

		reservation new_reservation = reservation(event_id, ticket_count,
		                                          expiration_time,
		                                          generate_cookie());

		reservations_map.insert({reservation_id, new_reservation});
		events_map.at(event_id).tickets_available -= ticket_count;

		insert_reservation_to_buffer(reservation_id, new_reservation);
		*message_length = RESERVATION_MSG_LENGTH;

	} else {
		insert_bad_request_to_buffer(event_id);
		*message_length = BAD_REQUEST_MSG_LENGTH;
	}
}

void insert_event_to_buffer(event &e, int event_id, int *index) {
	move_to_buff(event_id, index);
	move_to_buff(e.tickets_available, index);
	move_to_buff(e.description_length, index);
	move_to_buff(e.description, index);
}

void send_events(eventMap &events_map, int *message_length) {
	int index = 0;
	move_to_buff(EVENTS, &index);

	for (auto &element: events_map) {
		int event_id = element.first;
		event &eve = element.second;
		int eve_size = eve.description_length + CONST_OCTETS;

		if (index + eve_size > BUFFER_SIZE) {
			break;
		} else {
			insert_event_to_buffer(eve, event_id, &index);
		}
	}
	*message_length = index;
}

void fill_buffer_tickets(int reservation_id, const reservation &r, int *index) {
	move_to_buff(TICKETS, index);
	move_to_buff(reservation_id, index);
	move_to_buff(r.ticket_count, index);
	for (const string &ticket: r.tickets) {
		move_to_buff(ticket, index);
	}
}

bool check_tickets(int reservation_id, string &cookie, time_t current_time,
                   reservationMap &reservations_map) {
	if (reservations_map.find(reservation_id) == reservations_map.end())
		return false;
	reservation &r = reservations_map.at(reservation_id);
	if (r.cookie != cookie)
		return false;
	if (!r.achieved && (r.expiration_time < current_time))
		return false;
	return true;
}

void send_tickets_or_bad(time_t current_time, reservationMap &reservations_map,
                         int *message_length) {

	int reservation_id = parse_number_from_buffer(1, FOUR_OCT);
	string cookie = parse_cookie_from_buffer();
	if (check_tickets(reservation_id, cookie, current_time, reservations_map)) {
		reservation &r = reservations_map.at(reservation_id);
		if (!r.achieved) {
			r.achieved = true;
			int count = r.ticket_count;
			for (int i = 0; i < count; i++) {
				r.tickets.emplace_back(generate_ticket());
			}
		}
		fill_buffer_tickets(reservation_id, r, message_length);

	} else {
		insert_bad_request_to_buffer(reservation_id);
		*message_length = BAD_REQUEST_MSG_LENGTH;
	}
}

void parse_from_file(char *file_path, eventMap &events_map) {
	FILE *fp = fopen(file_path, "r+");
	int event_id = 0;
	char description[MAX_DESCRIPTION_SIZE + 2];
	char tickets_str[MAX_DESCRIPTION_SIZE + 2];

	while (fgets(description, MAX_DESCRIPTION_SIZE + 2, fp)
	       && fgets(tickets_str, MAX_DESCRIPTION_SIZE + 2, fp)) {

		int description_length = (int) strlen(description) - 1;
		string description_str = string(description, description_length);
		event new_event = event(description_str, description_length,
		                        strtoul(tickets_str,
		                                nullptr, 10));
		events_map.insert({event_id, new_event});
		event_id++;
	}

	fclose(fp);
}

void remove_expired_reservations(eventMap &events_map,
                                 reservationMap &reservations_map,
                                 time_t &current_time) {
	vector<int> reservations_to_remove;
	for (const auto &element: reservations_map) {
		int r_id = element.first;
		reservation const &r = element.second;
		if (!r.achieved && (r.expiration_time < current_time)) {
			reservations_to_remove.push_back(r_id);
		}
	}
	for (auto &r_id: reservations_to_remove) {
		reservation &r = reservations_map.at(r_id);
		events_map.at(r.event_id).tickets_available += r.ticket_count;
		reservations_map.erase(r_id);
	}
}

bool check_if_message_valid(size_t read_length) {
	int message_id = (int) (unsigned char) shared_buffer[0];
	if ((message_id == 1) && (read_length == GET_EVENTS_MSG_LENGTH))
		return true;
	if ((message_id == 3) && (read_length == GET_RESERVATION_MSG_LENGTH))
		return true;
	if ((message_id == 5) && (read_length == GET_TICKETS_MSG_LENGTH))
		return true;
	return false;
}

void execute_command_send_message(int socket_fd,
                                  const struct sockaddr_in *client_address,
                                  eventMap &events_map,
                                  reservationMap &reservations_map,
                                  time_t current_time, int timeout,
                                  int *send_length) {

	int message_id = (int) (unsigned char) shared_buffer[0];
	*send_length = 0;
	switch (message_id) {
		case GET_EVENTS: {
			send_events(events_map, send_length);
			break;
		}
		case GET_RESERVATION: {
			send_reservation_or_bad(current_time + timeout,
			                        events_map, reservations_map, send_length);
			break;
		}
		case GET_TICKETS: {
			send_tickets_or_bad(current_time, reservations_map, send_length);
			break;
		}
		default: {
			printf("Message has an unexpected type.\n");
		}
	}
	send_message(socket_fd, client_address, shared_buffer, *send_length);

	int sent_message_id = (int) (unsigned char) shared_buffer[0];
	if (sent_message_id != BAD_REQUEST) {
		printf("Server followed the instructions from received message.\n");
	} else {
		printf("Server received the message with incorrect data.\n");
	}
	printf("Server sent message with message_id = %d.\n", sent_message_id);
}


int main(int argc, char *argv[]) {

	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;
	int file_index;

	check_parameters(argc, argv, &port, &timeout, &file_index);


	char *file_path = argv[file_index];
	eventMap events_map;
	reservationMap reservations_map;

	parse_from_file(file_path, events_map);

	printf("Listening on port %u\n", port);

	int socket_fd = bind_socket(port);

	struct sockaddr_in client_address{};
	char *ip = get_ip(&client_address);

	size_t read_length;
	bool is_valid;
	int send_length;
	time_t current_time;
	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer

	do {
		read_length = read_message(socket_fd, &client_address,
		                           shared_buffer,
		                           sizeof(shared_buffer));

		printf("Received message from [%s:%d].\n", ip, port);

		current_time = time(nullptr);
		remove_expired_reservations(events_map, reservations_map, current_time);

		is_valid = check_if_message_valid(read_length);
		if (is_valid) {
			execute_command_send_message(socket_fd, &client_address,
			                             events_map, reservations_map,
			                             current_time, timeout, &send_length);
			memset(shared_buffer, 0, send_length);
		} else {
			printf("Received message does not have correct parameters.\n"
			       "Server ignored the message\n");
		}

	} while (read_length > 0);
}
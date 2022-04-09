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
#include <ctime>

#include <map>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

using std::string;

#define WRONG_PARAMETERS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4

#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define GET_EVENTS 1
#define EVENTS 2
#define GET_RESERVATION 3
#define RESERVATION 4
#define GET_TICKETS 5
#define TICKETS 6
#define BAD_REQUEST 255


#define TICKET_OCTETS 7

#define MAX_DESCRIPTION_SIZE 81
#define COOKIE_SIZE 48
#define CONST_OCTETS 7
#define COUNTING_BASE 256

#define RESERVATION_MESSAGE_LENGTH 67
#define BAD_REQUEST_MESSAGE_LENGTH 5

#define BUFFER_SIZE 65507
char shared_buffer[BUFFER_SIZE];
int reservation_current_id = 0;
int ticket_current_id = 0;

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

struct event_struct {
	char description[MAX_DESCRIPTION_SIZE]; //można zamienić na vector
	int description_length;
	int tickets_available;
} event;

struct reservation {
	bool achieved{false};
	int event_id;
	int ticket_count;
	time_t expiration_time;
	string cookie;
	std::vector<string> tickets;
} reservation;

void print_buffer(int n) {
	for (int i = 0; i < n; i++) {
		printf("%d %d %c\n", i, shared_buffer[i], shared_buffer[i]);
	}
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
	string message;
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

int new_reservation_id() {
	return reservation_current_id++;
}

int new_ticket_id() {
	return ticket_current_id++;
}

int parse_number_from_buffer(int index, int size) { //sprawdzić
	int arr[size];
	int result = 0;

	for (int i = 0; i < size; i++) {
		arr[size - 1 - i] = (int) (unsigned char) shared_buffer[index + i];
	}
	for (int i = 0; i < size; i++) {
		result += arr[i] * (int) pow(256, i);
	}
	return result;
}

string parse_cookie_from_buffer() {
	string cookie(COOKIE_SIZE, 0);
	for (int i = 0; i < COOKIE_SIZE; i++) {
		cookie[i] = shared_buffer[i + 5];
	}
	return cookie;
}

int event_size_in_buffer(event_struct e) {
	return e.description_length + CONST_OCTETS;
}

bool check_reservation(int event_id, int ticket_count,
                       std::map<int, event_struct> &events_map,
                       std::map<int, struct reservation> &reservations_map) {

	if (ticket_count == 0) {
		return false;
	}
	if (events_map.at(event_id).tickets_available < ticket_count) {
		return false;
	}
	if (9 + ticket_count * TICKET_OCTETS > BUFFER_SIZE) {
		return false;//sytuacja przepełnienia bufora
	}

	return true;
}

string generate_cookie(int reservation_id) { //todo!

	std::string str(COOKIE_SIZE, (int) 32);
	for (int i = COOKIE_SIZE - 1; i >= 0; i--) {
		str[i] = (char) (reservation_id % 94 + 33);
		reservation_id /= 94;
	}
	return str;
}

string generate_ticket() { //todo!
	int ticket_id = new_ticket_id();
	char set[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int length = 7;
	string ticket(length, set[0]);

	for (int i = COOKIE_SIZE - 1; i >= 0; i--) {
		ticket[i] = (char) (set[ticket_id % strlen(set)]);
		ticket_id /= (int) strlen(set);
	}
	return ticket;
}

template<typename T>
void move_to_buff(std::vector<T> vec, int *index) {
	for (int i = 0; i < vec.size(); i++) {
		shared_buffer[*index + i] = (char) vec[i];
	}
	*index += (int) vec.size();
}

void move_to_buff(size_t number, int octet_size, int *index) {
	std::vector<size_t> result(octet_size, 0);
	for (int i = octet_size - 1; i >= 0; i--) {
		result[i] = number % COUNTING_BASE;
		number /= COUNTING_BASE;
	}
	move_to_buff(result, index);
}

void move_to_buff(const char *str, int str_size, int *index) {
	for (int i = 0; i < str_size; i++) {
		shared_buffer[*index + i] = (char) str[i];
	}
	*index += str_size;
}

void move_to_buff(string s, int *index) {
	for (int i = 0; i < s.length(); i++) {
		shared_buffer[*index + i] = (char) s[i];
	}
	*index += (int) s.length();
}

void insert_bad_request_to_buffer(int id) {
	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
	int index = 0;

	move_to_buff(BAD_REQUEST, 1, &index);
	move_to_buff(id, 4, &index);
}

void insert_reservation_to_buffer(struct reservation &r, int reservation_id) {
	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
	int index = 0;

	move_to_buff(RESERVATION, 1, &index);
	move_to_buff(reservation_id, 4, &index);
	move_to_buff(r.event_id, 4, &index);
	move_to_buff(r.ticket_count, 2, &index);
	move_to_buff(r.cookie, &index);
	move_to_buff(r.expiration_time, 8, &index);
}

void make_reservation(time_t expiration_time,
                      std::map<int, event_struct> &events_map,
                      std::map<int, struct reservation> &reservations_map,
                      int *message_length) {

	int event_id = parse_number_from_buffer(1, 4);
	int ticket_count = parse_number_from_buffer(5, 2);

	if (check_reservation(event_id, ticket_count, events_map,
	                      reservations_map)) {

		int reservation_id = new_reservation_id();

		struct reservation new_reservation;
		new_reservation.event_id = event_id;
		new_reservation.ticket_count = ticket_count;
		new_reservation.expiration_time = expiration_time;
		new_reservation.cookie = generate_cookie(reservation_id); //todo!
		new_reservation.achieved = false;

		reservations_map.insert({reservation_id, new_reservation});
		events_map.at(event_id).tickets_available -= ticket_count;

		insert_reservation_to_buffer(new_reservation, reservation_id);
		*message_length = RESERVATION_MESSAGE_LENGTH;

	} else {
		insert_bad_request_to_buffer(event_id);
		*message_length = BAD_REQUEST_MESSAGE_LENGTH;
	}
}

void insert_event_to_buffer(event_struct eve, int event_id, int *counter) {
	move_to_buff(event_id, 4, counter);
	move_to_buff(eve.tickets_available, 2, counter);
	move_to_buff(eve.description_length, 1, counter);
	move_to_buff(eve.description, eve.description_length, counter);
}

void fill_buffer_events(std::map<int, event_struct> &events_map,
                        int *message_length) {
	memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
	int index = 0;
	move_to_buff(EVENTS, 1, &index);

	for (auto element: events_map) {
		int event_id = element.first;
		event_struct eve = element.second;
		int eve_size = event_size_in_buffer(eve);

		if (index + eve_size > BUFFER_SIZE) {
			break;
		} else {
			insert_event_to_buffer(eve, event_id, &index);
		}
	}
	*message_length = index;
}

void fill_buffer_tickets(int reservation_id, const struct reservation &r,
                         int *index) {

	move_to_buff(TICKETS, 1, index);
	move_to_buff(reservation_id, 4, index);
	move_to_buff(r.ticket_count, 2, index);
	for (const string &ticket: r.tickets) {
		move_to_buff(ticket, index);
	}
}

bool check_tickets(int reservation_id, string &cookie,
                   time_t current_time,
                   std::map<int, struct reservation> &reservations_map) {
	if (reservations_map.find(reservation_id) == reservations_map.end())
		return false;
	struct reservation r = reservations_map.at(reservation_id);
	if (r.cookie != cookie)
		return false;
	if (!r.achieved && r.expiration_time < current_time)
		return false;
	return true;
}

void make_tickets(time_t current_time,
                  std::map<int, struct reservation> &reservations_map,
                  int *message_length) {

	int reservation_id = parse_number_from_buffer(1, 4);
	string cookie = parse_cookie_from_buffer();
	if (check_tickets(reservation_id, cookie, current_time, reservations_map)) {
		printf("ESSAA\n");
		struct reservation r = reservations_map.at(reservation_id);
		printf("ESSAA2\n");
		if (!r.achieved) {
			r.achieved = true;
			printf("%d\n", r.ticket_count);
			int count = r.ticket_count;
			for (int i = 0; i < count; i++) {
				if(i < 10) {
					printf("%d\n", r.ticket_count);
				}
				r.tickets.push_back(generate_ticket());
			}
		}
		printf("ES\n");
		fill_buffer_tickets(reservation_id, r, message_length);
		printf("ES\n");

	} else {
		insert_bad_request_to_buffer(reservation_id);
		*message_length = BAD_REQUEST_MESSAGE_LENGTH;
	}

	printf("ES\n");

}

void parse_from_file(char *file_path, std::map<int, event_struct> &events_map) {
	FILE *fp = fopen(file_path, "r+");
	int event_id = 0;
	char description[MAX_DESCRIPTION_SIZE];
	char tickets_str[MAX_DESCRIPTION_SIZE];
	int description_length;

	while (fgets(description, sizeof(description), fp)
	       && fgets(tickets_str, sizeof(tickets_str), fp)) {

		description_length = (int) strlen(description);

		event_struct new_event;
		strcpy(new_event.description, description);
		new_event.description_length = description_length - 1;
		new_event.tickets_available = (int) strtoul(tickets_str,
		                                            nullptr, 10);

		events_map.insert({event_id, new_event});

		event_id++;
	}
	fclose(fp);
}

void remove_expired_reservations(std::map<int, struct event_struct> &events_map,
                                 std::map<int, struct reservation> &reservations_map,
                                 time_t current_time) {
	for (const auto &element: reservations_map) {
		int r_id = element.first;
		struct reservation r = element.second;
		if (!r.achieved && r.expiration_time < current_time) {
			events_map.at(r.event_id).tickets_available += r.ticket_count;
			reservations_map.erase(r_id);
		}
	}
}


int main(int argc, char *argv[]) {

	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;
	int file_index = -1;

	check_parameters(argc, argv, &port, &timeout, &file_index);

	char *file_path = argv[file_index];
	std::map<int, struct event_struct> events_map;
	std::map<int, struct reservation> reservations_map;

	parse_from_file(file_path, events_map);

	printf("Listening on port %u\n", port);

	int socket_fd = bind_socket(port);
	struct sockaddr_in client_address;

	size_t read_length;
	bool ignore_message;
	int message_length;

	do {
		memset(shared_buffer, 0, sizeof(shared_buffer)); // clean the buffer
		read_length = read_message(socket_fd, &client_address,
		                           shared_buffer,
		                           sizeof(shared_buffer));


		time_t current_time = time(nullptr);
		remove_expired_reservations(events_map, reservations_map, current_time);

		int message_id = (int) (unsigned char) shared_buffer[0];
		ignore_message = false;
		message_length = 0;

		switch (message_id) {
			case GET_EVENTS: {
				fill_buffer_events(events_map, &message_length);
				break;
			}
			case GET_RESERVATION: {
				make_reservation(current_time + timeout,
				                 events_map, reservations_map, &message_length);
				break;
			}
			case GET_TICKETS: {
				printf("Halio1\n");
				make_tickets(current_time, reservations_map, &message_length);
				printf("Halio2\n");
				break;
			}
			default: {
				ignore_message = true;
			}
		}

//		for (int i = 0; i < 50; i++) {
//			printf("%d %d %c\n", i, shared_buffer[i], shared_buffer[i]);
//		}
//		printf("\n");

		if (!ignore_message) {
			send_message(socket_fd, &client_address, shared_buffer,
			             message_length);
		}


	} while (read_length > 0);
}
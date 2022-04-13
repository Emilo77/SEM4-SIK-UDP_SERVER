#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

using std::string;
using std::vector;
using eventMap = std::map<int, struct event>;
using reservationMap = std::map<int, struct reservation>;

#define WRONG_FLAGS 1
#define WRONG_PATH 2
#define WRONG_PORT 3
#define WRONG_TIMEOUT 4
#define WRONG_ARGS_NUMBER 5

#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define GET_EVENTS (uint8_t)1
#define EVENTS (uint8_t)2
#define GET_RESERVATION (uint8_t)3
#define RESERVATION (uint8_t)4
#define GET_TICKETS (uint8_t)5
#define TICKETS (uint8_t)6
#define BAD_REQUEST (uint8_t)255

#define TWO_OCT 2
#define FOUR_OCT 4

#define MIN_COOKIE_CHAR 33
#define MAX_COOKIE_CHAR 126

#define COOKIE_POS 5
#define CONST_OCTETS 7
#define TICKET_OCTETS 7
#define MAX_DESCRIPTION_SIZE 80
#define COOKIE_SIZE 48
#define COUNTING_BASE 256

#define GET_EVENTS_MSG_LENGTH 1
#define GET_RESERVATION_MSG_LENGTH 7
#define GET_TICKETS_MSG_LENGTH 53

#define BAD_REQUEST_MSG_LENGTH 5

#define BUFFER_SIZE 65507
static char ticket_charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int ticket_current_id = 0;
int reservation_current_id = 0;

#define PRINT_ERRNO()                                                          \
  do {                                                                         \
    if (errno != 0) {                                                          \
      fprintf(stderr, "Error: errno %d in %s at %s:%d\n%s\n", errno, __func__, \
              __FILE__, __LINE__, strerror(errno));                            \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define CHECK_ERRNO(x)                                                         \
  do {                                                                         \
    errno = 0;                                                                 \
    (void)(x);                                                                 \
    PRINT_ERRNO();                                                             \
  } while (0)

#define ENSURE(x)                                                              \
  do {                                                                         \
    bool result = (x);                                                         \
    if (!result) {                                                             \
      fprintf(stderr, "Error: %s was false in %s at %s:%d\n", #x, __func__,    \
              __FILE__, __LINE__);                                             \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

int new_reservation_id() { return reservation_current_id++; }

int new_ticket_id() { return ticket_current_id++; }

struct event {
  string description;
  uint8_t description_length;
  uint16_t tickets_available;

  event(string description, uint8_t descriptionLength,
        uint16_t ticketsAvailable)
      : description(std::move(description)),
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

  reservation(uint32_t eventId, uint16_t ticketCount, time_t expirationTime)
      : event_id(eventId), ticket_count(ticketCount),
        expiration_time(expirationTime), cookie(std::move(generate_cookie())) {}

  void generate_ticket() {
    int ticket_id = new_ticket_id();
    string ticket(TICKET_OCTETS, 'A');
    size_t charset_size = strlen(ticket_charset);

    for (int i = TICKET_OCTETS - 1; i >= 0; i--) {
      ticket[i] = (ticket_charset[ticket_id % charset_size]);
      ticket_id /= (int)charset_size;
    }
    tickets.push_back(ticket);
  }

  static string generate_cookie() {
    string cookie(COOKIE_SIZE, MIN_COOKIE_CHAR);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(MIN_COOKIE_CHAR, MAX_COOKIE_CHAR);
    for (int i = 0; i < COOKIE_SIZE; i++) {
      cookie[i] = (char)dist(gen);
    }
    return cookie;
  }
};
class Data {

public:
  Data(int argc, char *argv[]) {
    this->port = DEFAULT_PORT;
    this->timeout = DEFAULT_TIMEOUT;
    check_parameters(argc, argv);
  }

  void remove_expired_reservations(const Data &data, time_t &current_time) {
    vector<int> reservations_to_remove;
    for (const auto &element : reservations_map) {
      int r_id = element.first;
      reservation const &r = element.second;
      if (!r.achieved && (r.expiration_time < current_time)) {
        reservations_to_remove.push_back(r_id);
      }
    }
    for (auto &r_id : reservations_to_remove) {
      reservation &r = reservations_map.at(r_id);
      events_map.at((int)r.event_id).tickets_available += r.ticket_count;
      reservations_map.erase(r_id);
    }
  }

  bool validate_tickets(int reservation_id, string &expected_cookie,
                        time_t current_time) {
    if (reservations_map.find(reservation_id) == reservations_map.end())
      return false;
    reservation &r = reservations_map.at(reservation_id);
    return !((r.cookie != expected_cookie) ||
             (!r.achieved && (r.expiration_time < current_time)));
  }

  bool validate_reservation(int event_id, int ticket_count) {
    return !((ticket_count == 0) ||
             (events_map.find(event_id) == events_map.end()) ||
             (events_map.at(event_id).tickets_available < ticket_count) ||
             ((TICKET_OCTETS + ticket_count * TICKET_OCTETS) > BUFFER_SIZE));
  }

private:
  static void exit_program(int status) {
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
    case WRONG_ARGS_NUMBER:
      message = "WRONG ARGUMENTS NUMBER";
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

  void bind_socket() {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ENSURE(socket_fd > 0);

    struct sockaddr_in server_address {};
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr =
        htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port);

    CHECK_ERRNO(bind(socket_fd, (struct sockaddr *)&server_address,
                     (socklen_t)sizeof(server_address)));
  }

  void check_file_path(char *path) {
    struct stat buffer {};
    if (stat(path, &buffer) != 0) {
      exit_program(WRONG_PATH);
    }
    file_path = path;
  }

  int get_port(char *port_str) {
    port = (int)strtoul(port_str, nullptr, 10);
    if (port <= 0 || port > UINT16_MAX) {
      exit_program(WRONG_PORT);
    }
    return port;
  }

  int get_timeout(char *timeout_str) {
    timeout = (int)strtoul(timeout_str, nullptr, 10);
    if (timeout < 1 || timeout > 86400) {
      exit_program(WRONG_TIMEOUT);
    }
    return timeout;
  }

  void parse_from_file() {
    FILE *fp = fopen(file_path, "r+");
    int event_id = 0;
    char description[MAX_DESCRIPTION_SIZE + 2];
    char tickets_str[MAX_DESCRIPTION_SIZE + 2];

    while (fgets(description, MAX_DESCRIPTION_SIZE + 2, fp) &&
           fgets(tickets_str, MAX_DESCRIPTION_SIZE + 2, fp)) {

      int description_length = (int)strlen(description) - 1;
      string description_str = string(description, description_length);
      event new_event = event(description_str, description_length,
                              strtoul(tickets_str, nullptr, 10));
      events_map.insert({event_id, new_event});
      event_id++;
    }

    fclose(fp);
  }

  void check_parameters(int argc, char *argv[]) {
    if ((argc < 3 || argc > 7) || argc % 2 == 0)
      exit_program(WRONG_ARGS_NUMBER);

    bool flag_file_occurred = false;
    for (int i = 1; i < argc; i += 2) {
      if (strcmp(argv[i], "-f") == 0) {
        flag_file_occurred = true;
        check_file_path(argv[i + 1]);
        file_path = argv[i + 1];

      } else if (strcmp(argv[i], "-p") == 0) {
        this->port = get_port(argv[i + 1]);

      } else if (strcmp(argv[i], "-t") == 0) {
        this->timeout = get_timeout(argv[i + 1]);

      } else {
        exit_program(WRONG_FLAGS);
      }
    }
    if (!flag_file_occurred)
      exit_program(WRONG_FLAGS);
  }

public:
  void init() {
    bind_socket();
    parse_from_file();
  }

  [[nodiscard]] char *get_ip() const {
    return inet_ntoa(this->client_address.sin_addr);
  }

  [[nodiscard]] int get_socket() const { return socket_fd; }

  [[nodiscard]] int get_port() const { return port; }

  [[nodiscard]] int get_timeout() const { return timeout; }

  [[nodiscard]] eventMap &get_events_map() { return events_map; }

  reservationMap &getReservationsMap() { return reservations_map; }

  sockaddr_in *get_client_address() { return &client_address; } // może źle

private:
  int port;
  int timeout;
  int socket_fd{};
  char *file_path{};
  struct sockaddr_in client_address {};
  eventMap events_map;
  reservationMap reservations_map;
};

class Buffer {

private:
  template <typename T> void move_to_buff(vector<T> vec) {
    size_t size = vec.size();
    for (int i = 0; i < size; i++) {
      buffer[current_size + i] = (char)vec[i];
    }
    set_current_size(current_size + size);
  }

  template <typename T> void move_to_buff(T number) {
    int octet_size = sizeof(T);
    vector<size_t> result(octet_size, 0);
    for (int i = octet_size - 1; i >= 0; i--) {
      result[i] = number % COUNTING_BASE;
      number = number >> 8;
    }
    move_to_buff(result);
  }

  void move_to_buff(string s) {
    size_t length = s.length();
    for (int i = 0; i < length; i++) {
      buffer[current_size + i] = s[i];
    }
    current_size += length;
  }

public:
  void insert_single_event(const event &e, int event_id) {
    move_to_buff(event_id);
    move_to_buff(e.tickets_available);
    move_to_buff(e.description_length);
    move_to_buff(e.description);
  }

  void insert_tickets(int reservation_id, const reservation &r) {
    set_size_to_zero();

    move_to_buff(TICKETS);
    move_to_buff(reservation_id);
    move_to_buff(r.ticket_count);
    for (const string &ticket : r.tickets) {
      move_to_buff(ticket);
    }
  }

  void insert_reservation(int reservation_id, reservation &r) {
    set_size_to_zero();

    move_to_buff(RESERVATION);
    move_to_buff(reservation_id);
    move_to_buff(r.event_id);
    move_to_buff(r.ticket_count);
    move_to_buff(r.cookie);
    move_to_buff(r.expiration_time);
  }

  void insert_bad_request(int id) {
    set_size_to_zero();

    move_to_buff(BAD_REQUEST);
    move_to_buff(id);
  }

  void insert_events(Data &data) {
    set_size_to_zero();

    move_to_buff(EVENTS);
    for (auto &element : data.get_events_map()) {
      int event_id = element.first;
      event const &eve = element.second;
      int eve_size = eve.description_length + CONST_OCTETS;

      if (current_size + eve_size > BUFFER_SIZE) {
        break;
      } else {
        insert_single_event(eve, event_id);
      }
    }
  }

  int parse_number(int size, int *index) {
    int arr[size];
    int result = 0;

    for (int i = 0; i < size; i++) {
      arr[size - 1 - i] = (int)(unsigned char)buffer[*index + i];
    }
    for (int i = 0; i < size; i++) {
      result += arr[i] * (int)pow(COUNTING_BASE, i);
    }
    *index += size;
    return result;
  }

  void try_inserting_reservation(Data &data, time_t time) {
    int index = 1;
    int event_id = parse_number(FOUR_OCT, &index);
    int ticket_count = parse_number(TWO_OCT, &index);
    if (data.validate_reservation(event_id, ticket_count)) {

      int reservation_id = new_reservation_id();
      reservation new_reservation =
          reservation(event_id, ticket_count, time + data.get_timeout());
      data.getReservationsMap().insert({reservation_id, new_reservation});
      data.get_events_map().at(event_id).tickets_available -= ticket_count;
      insert_reservation(reservation_id, new_reservation);

    } else {
      insert_bad_request(event_id);
      set_current_size(BAD_REQUEST_MSG_LENGTH); // check
    }
  }

  void try_inserting_tickets(Data &data, time_t time) {
    int index = 1;
    int reservation_id = parse_number(FOUR_OCT, &index);
    string cookie = parse_cookie();
    if (data.validate_tickets(reservation_id, cookie, time)) {
      reservation &r = data.getReservationsMap().at(reservation_id);
      if (!r.achieved) {
        r.achieved = true;
        for (int i = 0; i < r.ticket_count; i++) {
          r.generate_ticket();
        }
      }
      insert_tickets(reservation_id, r);

    } else {
      insert_bad_request(reservation_id);
      set_current_size(BAD_REQUEST_MSG_LENGTH); // check
    }
  }

  string parse_cookie() { return {buffer + COOKIE_POS, COOKIE_SIZE}; }

  [[nodiscard]] size_t get_size() const { return current_size; }

  void set_size_to_zero() { current_size = 0; }

  void set_current_size(size_t number) { current_size = number; }

  char get_message_id() { return buffer[0]; }

  char *get() { return buffer; }

private:
  char buffer[BUFFER_SIZE]{};
  size_t current_size{0};
};

class Server {
public:
  Server(Data data, const Buffer &buffer)
      : data(std::move(data)), buffer(buffer) {}

private:
  void send_message() {
    auto address_length = (socklen_t)sizeof(*data.get_client_address());
    int flags = 0;
    sent_length =
        sendto(data.get_socket(), buffer.get(), buffer.get_size(), flags,
               (struct sockaddr *)data.get_client_address(), address_length);
    ENSURE(sent_length == (ssize_t)buffer.get_size());
  }

  void read_message() {
    auto address_length = (socklen_t)sizeof(*data.get_client_address());
    int flags = 0;
    errno = 0;
    read_length =
        recvfrom(data.get_socket(), buffer.get(), BUFFER_SIZE, flags,
                 (struct sockaddr *)data.get_client_address(), &address_length);
    if (read_length < 0) {
      PRINT_ERRNO();
    }
    time_after_read = time(nullptr);
    printf("Received message from [%s:%d].\n", data.get_ip(), data.get_port());
  }

  bool first_validate() {
    uint8_t message_id = buffer.get_message_id();
    return ((message_id == 1) && (read_length == GET_EVENTS_MSG_LENGTH)) ||
           ((message_id == 3) && (read_length == GET_RESERVATION_MSG_LENGTH)) ||
           ((message_id == 5) && (read_length == GET_TICKETS_MSG_LENGTH));
  }

  void show_information() {
    uint8_t sent_message_id = buffer.get_message_id();
    if (sent_message_id != BAD_REQUEST) {
      printf("Server followed the instructions from received message.\n");
    } else {
      printf("Server received the message with incorrect data.\n");
    }
    printf("Server sent message with message_id = %d.\n", sent_message_id);
  }

  void execute_command() {
    data.remove_expired_reservations(data, time_after_read);
    if (!first_validate()) {
      printf("Received message does not have correct parameters.\n"
             "Server ignored the message\n");
      return;
    }

    switch (buffer.get_message_id()) {
    case GET_EVENTS:
      buffer.insert_events(data);
      break;

    case GET_RESERVATION:
      buffer.try_inserting_reservation(data, time_after_read);
      break;

    case GET_TICKETS:
      buffer.try_inserting_tickets(data, time_after_read);
      break;

    default:
      printf("Message has an unexpected type.\n");
    }

  }

public:
  void run() {
    printf("Listening on port %u\n", data.get_port());
    while (true) {
      read_message();
      execute_command();
      show_information();
      send_message();
    }
  }

private:
  Data data;
  Buffer buffer;
  time_t time_after_read{time(nullptr)};
  size_t read_length{0};
  size_t sent_length{0};
};

int main(int argc, char *argv[]) {
  Data data = Data(argc, argv);
  data.init();
  Buffer shared_buffer = Buffer();
  Server tcp_server = Server(data, shared_buffer);
  tcp_server.run();
}

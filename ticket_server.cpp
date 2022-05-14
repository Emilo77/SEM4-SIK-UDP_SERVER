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
#include <unistd.h>
#include <utility>
#include <vector>

// Aliases for commonly used types
using std::string;
using eventMap = std::map<int, struct event>;
using reservationMap = std::map<int, struct reservation>;

// Global variables
#define DEFAULT_PORT 2022
#define DEFAULT_TIMEOUT 5

#define GET_EVENTS (uint8_t)1
#define EVENTS (uint8_t)2
#define GET_RESERVATION (uint8_t)3
#define RESERVATION (uint8_t)4
#define GET_TICKETS (uint8_t)5
#define TICKETS (uint8_t)6
#define BAD_REQUEST (uint8_t)255

#define MIN_COOKIE_CHAR 33
#define MAX_COOKIE_CHAR 126

#define EVENT_CONST_OCTETS 7
#define TICKET_OCTETS 7
#define COOKIE_SIZE 48
#define MAX_DESCRIPTION_SIZE 80

#define GET_EVENTS_MSG_LENGTH 1
#define GET_RESERVATION_MSG_LENGTH 7
#define GET_TICKETS_MSG_LENGTH 53

#define BUFFER_SIZE 65507
static char ticket_charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

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

#ifdef NDEBUG
const bool debug = false;
#else
const bool debug = true;
#endif

// Structs used for server communication
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
  std::vector<string> tickets;
  inline static size_t ticket_current_id;
  inline static size_t reservation_current_id;

  reservation(uint32_t eventId, uint16_t ticketCount, time_t expirationTime)
      : event_id(eventId), ticket_count(ticketCount),
        expiration_time(expirationTime), cookie(std::move(generate_cookie())) {}

  static void initialize_ids() {
    reservation::ticket_current_id = 0;
    reservation::reservation_current_id = 1000000;
  }

  static size_t new_reservation_id() { return reservation_current_id++; }

  static size_t new_ticket_id() { return ticket_current_id++; }

  void generate_ticket() {
    size_t ticket_id = new_ticket_id();
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

// Class containing given parameters to server
class ServerParameters {
private:
  enum WRONG_PARAMETERS {
    WRONG_FLAGS = 1,
    WRONG_PATH = 2,
    WRONG_PORT = 3,
    WRONG_TIMEOUT = 4,
    WRONG_ARGS_NUMBER = 5,
    NO_FILE_PATH = 6,
  };

public:
  ServerParameters(int argc, char *argv[]) {
    port = DEFAULT_PORT;
    timeout = DEFAULT_TIMEOUT;
    bin_file = argv[0];
    check_parameters(argc, argv);
  }

private:
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
    case WRONG_ARGS_NUMBER:
      message = "WRONG ARGUMENTS NUMBER";
      break;
    case NO_FILE_PATH:
      message = "FILEPATH NOT FOUND";
      break;
    default:
      message = "WRONG PARAMETERS";
    }
    message.append("\n");
    fprintf(stderr,
            "Usage: %s -f <path to events file> [-p <port>] [-t <timeout>]\n",
            bin_file);
    fprintf(stderr, "%s", message.c_str());
    exit(1);
  }

  void check_file_path(char *path) {
    struct stat buffer {};
    if (stat(path, &buffer) != 0) {
      exit_program(WRONG_PATH);
    }
    file_path = path;
  }

  int check_port(char *port_str) {
    port = (int)strtoul(port_str, nullptr, 10);
    if (port < 0 || port > UINT16_MAX ||
        std::any_of(port_str, port_str + strlen(port_str),
                    [](char c) { return !isdigit(c); })) {

      exit_program(WRONG_PORT);
    }
    return port;
  }

  int check_timeout(char *timeout_str) {
    timeout = (int)strtoul(timeout_str, nullptr, 10);
    if (timeout < 1 || timeout > 86400) {
      exit_program(WRONG_TIMEOUT);
    }
    return timeout;
  }

  void check_parameters(int argc, char *argv[]) {
    if ((argc < 3 || argc > 7) || argc % 2 == 0)
      exit_program(WRONG_ARGS_NUMBER);

    bool flag_file_occurred = false;

    const char *flags = "-f:p:t:";
    int opt;
    while ((opt = getopt(argc, argv, flags)) != -1)
      switch (opt) {
      case 'f':
        flag_file_occurred = true;
        check_file_path(optarg);
        break;
      case 'p':
        port = check_port(optarg);
        break;
      case 't':
        timeout = check_timeout(optarg);
        break;
      default:
        exit_program(NO_FILE_PATH);
      }
    if (!flag_file_occurred)
      exit_program(WRONG_FLAGS);
  }

public:
  [[nodiscard]] int get_port() const { return port; }

  [[nodiscard]] char *get_file_path() const { return file_path; }

  [[nodiscard]] int get_timeout() const { return timeout; }

private:
  int port;
  int timeout;
  char *bin_file;
  char *file_path{};
};

// Class for server data: events, reservations, etc.
class Data {

public:
  explicit Data(const ServerParameters &parameters) : parameters(parameters) {
    reservation::initialize_ids();
    parse_from_file();
  }

private:
  void parse_from_file() {
    FILE *fp = fopen(parameters.get_file_path(), "r");
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

public:
  void remove_expired_reservations(time_t &current_time) {
    std::vector<int> reservations_to_remove;
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

  [[nodiscard]] eventMap &get_events_map() { return events_map; }

  reservationMap &getReservationsMap() { return reservations_map; }

private:
  ServerParameters parameters;
  eventMap events_map;
  reservationMap reservations_map;
};

// Class for operations on buffer, mostly converting data to proper format
class Buffer {

private:
  template <typename T> T convert_to_send(T number) {
    switch (sizeof(T)) {
    case 1:
      return number;
    case 2:
      return htobe16(number);
    case 4:
      return htobe32(number);
    default:
      return htobe64(number);
    }
  }

  template <typename T> T convert_to_receive(T number) {
    switch (sizeof(T)) {
    case 1:
      return number;
    case 2:
      return be16toh(number);
    case 4:
      return be32toh(number);
    default:
      return be64toh(number);
    }
  }

  template <typename T> void insert(T number) {
    int size = sizeof(T);
    number = convert_to_send(number);
    memcpy(buffer + send_index, &number, size);
    send_index += size;
  }

  void insert(const string &str) {
    size_t size = str.size();
    memcpy(buffer + send_index, str.c_str(), size);
    send_index += size;
  }

  template <typename T> void receive_number(T &number) {
    int size = sizeof(T);
    memcpy(&number, buffer + read_index, size);
    read_index += size;
    number = convert_to_receive(number);
  }

  string receive_cookie() { return {buffer + read_index, COOKIE_SIZE}; }

  void insert_single_event(const event &e, int event_id) {
    insert(event_id);
    insert(e.tickets_available);
    insert(e.description_length);
    insert(e.description);
  }

  void insert_tickets(int reservation_id, const reservation &r) {
    reset_send_index();

    insert(TICKETS);
    insert(reservation_id);
    insert(r.ticket_count);
    for (const string &ticket : r.tickets) {
      insert(ticket);
    }
  }

  void insert_reservation(int reservation_id, reservation &r) {
    reset_send_index();

    insert(RESERVATION);
    insert(reservation_id);
    insert(r.event_id);
    insert(r.ticket_count);
    insert(r.cookie);
    insert(r.expiration_time);
  }

  void insert_bad_request(int id) {
    reset_send_index();

    insert(BAD_REQUEST);
    insert(id);
  }

  // we begin from 1, because we already know the value of buffer[0]- message_id
  void reset_read_index() { read_index = 1; }

  void reset_send_index() { send_index = 0; }

public:
  void insert_events(Data &data) {
    reset_send_index();

    insert(EVENTS);
    for (auto &element : data.get_events_map()) {
      int event_id = element.first;
      event const &eve = element.second;
      int eve_size = eve.description_length + EVENT_CONST_OCTETS;

      if (send_index + eve_size > BUFFER_SIZE) {
        break;
      } else {
        insert_single_event(eve, event_id);
      }
    }
  }

  void try_to_insert_reservation(Data &data, time_t time, int timeout) {
    read_index = 1;
    uint32_t event_id;
    uint16_t ticket_count;
    receive_number(event_id);
    receive_number(ticket_count);
    if (data.validate_reservation((int)event_id, ticket_count)) {

      int reservation_id = (int)reservation::new_reservation_id();
      reservation new_reservation =
          reservation(event_id, ticket_count, time + timeout);
      data.getReservationsMap().insert({reservation_id, new_reservation});
      data.get_events_map().at((int)event_id).tickets_available -= ticket_count;
      insert_reservation(reservation_id, new_reservation);

    } else {
      insert_bad_request((int)event_id);
    }
  }

  void try_to_insert_tickets(Data &data, time_t time) {
    reset_read_index();

    int reservation_id;
    receive_number(reservation_id);
    string cookie = receive_cookie();
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
    }
  }

  [[nodiscard]] size_t get_size() const { return send_index; }

  char get_message_id() { return buffer[0]; }

  char *get() { return buffer; }

private:
  char buffer[BUFFER_SIZE]{};
  size_t send_index{0};
  size_t read_index{1};
};

// Class implementing server operations: receiving, sending and processing
class Server {
public:
  Server(ServerParameters parameters, Data data, const Buffer &buffer)
      : parameters(parameters), data(std::move(data)), buffer(buffer) {}

  virtual ~Server() {
    CHECK_ERRNO(close(socket_fd));
    if (debug) {
      fprintf(stderr, "Server closed\n");
    }
  }

private:
  [[nodiscard]] char *get_ip() const {
    return inet_ntoa(client_address.sin_addr);
  }

  void bind_socket() {
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ENSURE(socket_fd > 0);

    struct sockaddr_in server_address {};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(parameters.get_port());

    CHECK_ERRNO(bind(socket_fd, (struct sockaddr *)&server_address,
                     (socklen_t)sizeof(server_address)));
  }

  void send_message() {
    auto address_length = (socklen_t)sizeof(client_address);
    int flags = 0;
    sent_length = sendto(socket_fd, buffer.get(), buffer.get_size(), flags,
                         (struct sockaddr *)&client_address, address_length);
    ENSURE(sent_length == (ssize_t)buffer.get_size());
  }

  void read_message() {
    auto address_length = (socklen_t)sizeof(client_address);
    int flags = 0;
    errno = 0;
    read_length = recvfrom(socket_fd, buffer.get(), BUFFER_SIZE, flags,
                           (struct sockaddr *)&client_address, &address_length);
    if (read_length < 0) {
      PRINT_ERRNO();
    }
    time_after_read = time(nullptr);
    if (debug) {
      fprintf(stderr, "Received message from [%s:%d].\n", get_ip(),
              parameters.get_port());
    }
  }

  bool first_validate() {
    uint8_t message_id = buffer.get_message_id();
    return ((message_id == 1) && (read_length == GET_EVENTS_MSG_LENGTH)) ||
           ((message_id == 3) && (read_length == GET_RESERVATION_MSG_LENGTH)) ||
           ((message_id == 5) && (read_length == GET_TICKETS_MSG_LENGTH));
  }

  void show_information() {
    uint8_t sent_message_id = buffer.get_message_id();
    if (debug) {
      if (sent_message_id != BAD_REQUEST) {
        fprintf(stderr,
                "Server followed the instructions from received message.\n");
      } else {
        fprintf(stderr, "Server received the message with incorrect data.\n");
      }
      fprintf(stderr, "Server sent message with message_id = %d.\n",
              sent_message_id);
    }
  }

  void execute_command() {
    data.remove_expired_reservations(time_after_read);
    if (debug && !first_validate()) {
      fprintf(stderr, "Received message does not have correct parameters.\n"
                      "Server ignored the message\n");
      return;
    }

    switch (buffer.get_message_id()) {
    case GET_EVENTS:
      buffer.insert_events(data);
      break;

    case GET_RESERVATION:
      buffer.try_to_insert_reservation(data, time_after_read,
                                       parameters.get_timeout());
      break;

    case GET_TICKETS:
      buffer.try_to_insert_tickets(data, time_after_read);
      break;

    default:
      if (debug) {
        fprintf(stderr, "Message has an unexpected type.\n");
      }
    }
  }

public:
  void run() {
    bind_socket();
    if (debug) {
      fprintf(stderr, "Listening on port %u\n", parameters.get_port());
    }

    while (true) {
      read_message();
      execute_command();
      show_information();
      send_message();
    }
  }

private:
  ServerParameters parameters;
  Data data;
  Buffer buffer;
  time_t time_after_read{time(nullptr)};
  ssize_t read_length{0};
  ssize_t sent_length{0};
  int socket_fd{};
  struct sockaddr_in client_address {};
};

int main(int argc, char *argv[]) {
  ServerParameters parameters = ServerParameters(argc, argv);
  Data data = Data(parameters);
  Buffer shared_buffer = Buffer();
  Server udp_server = Server(parameters, data, shared_buffer);
  udp_server.run();
}

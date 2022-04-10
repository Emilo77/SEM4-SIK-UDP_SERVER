from basic_client import Client, Response255Exception
from server_wrap import start_server
from event_files.generate_file import generate_file

MAX_DATAGRAM_SIZE = 65507 # dla IPv4
MAX_EVENT_COUNT = int(1e6)
MAX_DESCRIPTION_LENGTH = 80
MAX_TICKET_COUNT = (1 << 16) - 1

# message_id = 1, reservation_id = 4, ticket_count = 2, total = 7
# ticket = 7
MAX_TICKETS_PER_RESERVATION = (MAX_DATAGRAM_SIZE - 7) // 7

def max_event_count(file):
    for i in range(MAX_EVENT_COUNT):
        for j in range(MAX_DESCRIPTION_LENGTH):
            file.write('a')
        file.write('\n' + str(MAX_TICKET_COUNT) + '\n')

def test_limits():
    server = start_server(generate_file(max_event_count))
    client = Client()

    events = client.get_events()

    # event_id = 4, ticket_count = 2, 
    # description_length = 1, description = 80
    event_info_size = 4 + 2 + 1 + MAX_DESCRIPTION_LENGTH

    assert len(events) == (MAX_DATAGRAM_SIZE - 1) // event_info_size

    for e in events:
        r = client.get_reservation(e.event_id, MAX_TICKETS_PER_RESERVATION)
        client.get_tickets(r.reservation_id, r.cookie)
    
        try:
            r = client.get_reservation(e.event_id, MAX_TICKETS_PER_RESERVATION + 1)
            assert False
        except Response255Exception:
            pass

    server.terminate()
    server.communicate()

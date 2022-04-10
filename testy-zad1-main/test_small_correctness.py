from basic_client import Client
from server_wrap import start_server

import time

def test_small_correctness():
    server = start_server('event_files/events_example')
    client = Client()

    events = client.get_events()
    assert len(events) == 3
    for i in range(len(events)):
        p = (events[i].description, events[i].ticket_count)
        assert p == ('fajny koncert', 123) or p == ('film o kotach', 32) or p == ('ZOO', 0)
        for j in range(i):
            assert events[i].event_id != events[j].event_id
            assert events[i].description != events[j].description
    event_id_of_cats = -1
    for e in events:
        if e.description == 'film o kotach':
            event_id_of_cats = e.event_id
    assert event_id_of_cats != -1

    reservation_info = client.get_reservation(event_id_of_cats, 20)
    assert reservation_info.event_id == event_id_of_cats
    assert reservation_info.ticket_count == 20
    assert abs(reservation_info.expiration_time - (int(time.time()) + 5)) <= 1

    for e in client.get_events():
        if e.event_id == event_id_of_cats:
            assert e.ticket_count == 32 - 20

    tickets_info = client.get_tickets(reservation_info.reservation_id, reservation_info.cookie)
    assert tickets_info.reservation_id == reservation_info.reservation_id
    assert tickets_info.ticket_count == reservation_info.ticket_count
    for i in range(len(tickets_info.tickets)):
        for j in range(i):
            assert tickets_info.tickets[i] != tickets_info.tickets[j]

    for ticket in client.get_tickets(reservation_info.reservation_id, reservation_info.cookie).tickets:
        assert ticket in tickets_info.tickets

    try:
        client.get_tickets(reservation_info.reservation_id, reservation_info.cookie[:-3] + '232')
        assert False
    except:
        pass

    try:
        client.get_tickets(323, reservation_info.cookie)
        assert False
    except:
        pass

    for e in client.get_events():
        if e.event_id == event_id_of_cats:
            assert e.ticket_count == 32 - 20

    server.terminate()
    server.communicate()

if __name__ == '__main__':
    test_small_correctness()

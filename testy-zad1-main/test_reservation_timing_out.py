from basic_client import Client, Response255Exception
from server_wrap import start_server
import time

def get_event(client):
    for e in client.get_events():
        if e.ticket_count != 0:
            return e

def test_are_tickets_returned(client):
    event = get_event(client)
    client.get_reservation(event.event_id, event.ticket_count)

    time.sleep(1)
    try:
        client.get_reservation(event.event_id, event.ticket_count)
        assert False
    except Response255Exception:
        pass

    time.sleep(2)
    client.get_reservation(event.event_id, event.ticket_count)

def test_tickets_received(client):
    event = get_event(client)
    r = client.get_reservation(event.event_id, event.ticket_count)
    old_tickets = client.get_tickets(r.reservation_id, r.cookie)

    time.sleep(3) 
    try:
        client.get_reservation(event.event_id, event.ticket_count)
        assert False
    except Response255Exception:
        pass

    new_tickets = client.get_tickets(r.reservation_id, r.cookie)
    assert old_tickets.reservation_id == new_tickets.reservation_id
    assert old_tickets.ticket_count == new_tickets.ticket_count
    assert old_tickets.tickets == new_tickets.tickets

def test_receive_after_timeout(client):
    event = get_event(client)
    r = client.get_reservation(event.event_id, event.ticket_count)

    time.sleep(3) 
    try:
        client.get_tickets(r.reservation_id, r.cookie)
        assert False
    except Response255Exception:
        pass

def test_reservation_timing_out():
    server = start_server('event_files/simple_events', timeout=2)
    client = Client()

    test_are_tickets_returned(client)
    test_tickets_received(client)
    test_receive_after_timeout(client)

    server.terminate()

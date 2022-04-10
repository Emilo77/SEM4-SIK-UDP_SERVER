from basic_client import Client, Response255Exception
from server_wrap import start_server
from event_files.generate_file import generate_file

import random, time

class CorrectnessClient:
    def __init__(self):
        self.basic_client = Client()
        self.events = self.basic_client.get_events()

        for i in range(len(self.events)):
            for j in range(i):
                assert self.events[i].event_id != self.events[j].event_id
                assert self.events[i].description != self.events[j].description

        self.used_reservation_ids = {int(1e7) + 42}
        self.used_cookies = {'abcd' * (48 // 4)}
        self.possible_reservations = []
        self.already_claimed_tickets = {}
        self.used_tickets = set()

        random.seed(0)

    def get_events(self):
        got_events = self.basic_client.get_events()
        for e_info in got_events:
            assert list(filter(lambda e: e.event_id == e_info.event_id, self.events))[0] == e_info
        return got_events

    def do_reservation(self, event_id, ticket_count):
        expecting_255 = False
        try:
            event_info = list(filter(lambda e: e.event_id == event_id, self.events))[0]
            if ticket_count == 0 or ticket_count > event_info.ticket_count:
                expecting_255 = True
        except:
            expecting_255 = True

        if expecting_255:
            try:
                self.basic_client.get_reservation(event_id, ticket_count)
                assert False
            except Response255Exception:
                pass
        else:
            reservation_info = self.basic_client.get_reservation(event_id, ticket_count)
            assert reservation_info.reservation_id not in self.used_reservation_ids
            self.used_reservation_ids.add(reservation_info.reservation_id)
            assert reservation_info.event_id == event_id
            assert reservation_info.ticket_count == ticket_count
            assert reservation_info.cookie not in self.used_cookies
            assert abs(reservation_info.expiration_time - (int(time.time()) + 50)) <= 1
            self.used_cookies.add(reservation_info.cookie)
            event_info.ticket_count -= ticket_count
            self.possible_reservations.append(reservation_info)

    def claim_tickets(self, reservation_id, cookie):
        try:
            reservation_info = list(filter(lambda r: r.reservation_id == reservation_id and r.cookie == cookie, self.possible_reservations))[0]
        except:
            try:
                self.basic_client.get_tickets(reservation_id, cookie)
                assert False
            except Response255Exception:
                return

        tickets_info = self.basic_client.get_tickets(reservation_id, cookie)
        if reservation_id in self.already_claimed_tickets:
            assert tickets_info.reservation_id == self.already_claimed_tickets[reservation_id].reservation_id
            assert tickets_info.ticket_count == self.already_claimed_tickets[reservation_id].ticket_count
            assert tickets_info.tickets == self.already_claimed_tickets[reservation_id].tickets
        else:
            assert tickets_info.reservation_id == reservation_id
            assert tickets_info.ticket_count == reservation_info.ticket_count
            for ticket in tickets_info.tickets:
                assert ticket not in self.used_tickets
                self.used_tickets.add(ticket)
            self.already_claimed_tickets[reservation_id] = tickets_info

    def do_random_operation(self):
        if random.randint(0, 1):
            if random.randint(0, 1):
                event_info = random.choice(self.events)
                self.do_reservation(event_info.event_id, random.randint(0, event_info.ticket_count + 1))
            else:
                self.do_reservation(random.randint(0, 100), random.randint(0, 10))
        else:
            if random.randint(0, 1):
                reservation_info = random.choice(self.possible_reservations)
                self.claim_tickets(reservation_info.reservation_id, reservation_info.cookie)
            else:
                self.claim_tickets(random.choice(list(self.used_reservation_ids)), random.choice(list(self.used_cookies)))

def events_various_ticket_count(file):
    random.seed(0)
    for i in range(int(100)):
        file.write(str(i) + '\n' + str(random.randint(0, 5000)) + '\n')

def test_big_correctness():
    server = start_server(generate_file(events_various_ticket_count), timeout = 50)
    client = CorrectnessClient()
    for i in range(7500):
        client.do_random_operation()
    server.terminate()
    server.communicate()

if __name__ == '__main__':
    test_big_correctness()

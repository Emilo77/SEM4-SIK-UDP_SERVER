import socket, struct
from server_wrap import DEFAULT_PORT

class Printable:
    def __str__(self):
        return type(self).__name__ + ': ' + str(self.__dict__)

class Response255Exception(Exception): pass

class Client:
    def __init__(self, server_ip='localhost', server_port=DEFAULT_PORT):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server_addr = (server_ip, server_port)

    def send_message(self, message):
        self.socket.sendto(message, self.server_addr)

    def receive_message(self):
        return self.socket.recvfrom(1 << 16)[0]

    def get_events(self):
        self.send_message(struct.pack('!B', 1))
        data = self.receive_message()
        assert struct.unpack('!B', data[0:1])[0] == 2
        data = data[1:]
        ret = []
        while data:
            class EventInfo(Printable): pass
            info = EventInfo()
            info.event_id, info.ticket_count, description_length = struct.unpack('!IHB', data[0:7])
            info.description = data[7 : 7 + description_length].decode('utf-8')
            data = data[7 + description_length:]
            ret.append(info)
        return ret

    def get_reservation(self, event_id, ticket_count):
        self.send_message(struct.pack('!BIH', 3, event_id, ticket_count))
        data = self.receive_message()
        message_type = struct.unpack('!B', data[0:1])[0]
        assert message_type == 4 or message_type == 255
        if message_type == 255:
            assert struct.unpack('!I', data[1:])[0] == event_id
            raise Response255Exception(event_id)
        assert len(data) == 1 + 4 + 4 + 2 + 48 + 8

        class ReservationInfo(Printable): pass
        info = ReservationInfo()
        info.reservation_id, info.event_id, info.ticket_count, info.cookie, info.expiration_time = struct.unpack('!IIH48sQ', data[1:])
        for c in info.cookie:
            assert 33 <= c and c <= 126
        info.cookie = info.cookie.decode('utf-8')
        return info

    def get_tickets(self, reservation_id, cookie):
        self.send_message(struct.pack('!BI48s', 5, reservation_id, cookie.encode()))
        data = self.receive_message()
        message_type = struct.unpack('!B', data[0:1])[0]
        assert message_type == 6 or message_type == 255
        if message_type == 255:
            assert struct.unpack('!I', data[1:])[0] == reservation_id
            raise Response255Exception(reservation_id)

        class TicketsInfo(Printable): pass
        info = TicketsInfo()
        info.reservation_id, info.ticket_count = struct.unpack('!IH', data[1:7])
        info.tickets = []
        for ticket in struct.unpack('!' + (info.ticket_count * '7s'), data[7:]):
            for c in ticket:
                assert (ord('A') <= c and c <= ord('Z')) or (ord('0') <= c and c <= ord('9'))
            info.tickets.append(ticket.decode('utf-8'))
        return info

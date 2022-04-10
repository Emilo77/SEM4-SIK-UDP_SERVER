import subprocess, time, psutil, sys

EXECUTABLE = '../ticket_server'
DEFAULT_PORT = 2022

def is_port_in_use(port):
    for connection in psutil.net_connections():
        if connection.laddr.port == port:
            return True
    return False

def current_time_ms():
    return round(time.time() * 1000)

class ClosedException(Exception): pass
class TimeoutException(Exception): pass

# tests can be run with flag --debug
def is_debug():
    for arg in sys.argv:
        if arg == "-d" or arg == "--debug":
            return True
    return False

# server_kill_timeout is in ms
def start_server_with_params(params, server_kill_timeout=5000):
    debug = is_debug()
    args = [EXECUTABLE] + params

    if debug:
        print("[TESTS] starting server:", args)

    port = DEFAULT_PORT
    for i in range(len(params) - 1):
        if params[i] == '-p' and params[i + 1].isnumeric():
            port = int(params[i + 1])

    if is_port_in_use(port):
        raise Exception("port " + str(port) + " is already in use")

    if debug:
        server = subprocess.Popen(args)
    else:
        server = subprocess.Popen(args,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    start_time = current_time_ms() 
    while True:
        if is_port_in_use(port):
            return server
        if current_time_ms() > start_time + server_kill_timeout:
            raise TimeoutException("waiting for server start timed out")
        elif server.poll() is not None:
            raise ClosedException("server returned", server.returncode)

def start_server(events_filename, timeout=5, port=DEFAULT_PORT):
    return start_server_with_params(['-f', events_filename, '-p', str(port), '-t', str(timeout)])

def get_return_code_of_server_with_params(params):
    try:
        server = start_server_with_params(params)
        server.terminate()
        return 0
    except ClosedException as err:
        _, return_code = err.args
        return return_code
    except TimeoutException as err:
        # server did not start, did not return 1
        return 2

if __name__ == '__main__':
    server = start_server("event_files/events_example")
    time.sleep(0.2)
    server.terminate()

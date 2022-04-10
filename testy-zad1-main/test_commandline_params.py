from server_wrap import get_return_code_of_server_with_params

def assert_returns_0(params):
    assert get_return_code_of_server_with_params(params) == 0

def assert_returns_1(params):
    assert get_return_code_of_server_with_params(params) == 1

def test_commandline_params():
    assert_returns_0(['-f', 'event_files/events_example'])
    assert_returns_1(['-f', ''])
    assert_returns_1(['-f', 'nonexisting_file'])
    assert_returns_1(['-ff', 'event_files/events_example'])
    assert_returns_1(['-', 'event_files/events_example'])
    assert_returns_1([])
    assert_returns_1(['-f'])
    assert_returns_0(['-f', 'event_files/events_example', '-f', 'event_files/events_example'])
    assert_returns_1(['-f', '-f'])

    assert_returns_0(['-f', 'event_files/events_example', '-p', '2022'])
    assert_returns_1(['-f', 'event_files/events_example', '-p'])
    assert_returns_1(['-f', 'event_files/events_example', '-pp', '2022'])
    assert_returns_1(['-f', 'event_files/events_example', '-', '2022'])
    assert_returns_1(['-f', 'event_files/events_example', '-p', '-1'])
    assert_returns_1(['-f', 'event_files/events_example', '-p', 'a'])
    assert_returns_1(['-f', 'event_files/events_example', '-p', '65536'])
    assert_returns_0(['-f', 'event_files/events_example', '-p', '2022', '-p', '2022'])
    assert_returns_0(['-f', 'event_files/events_example', '-p', '65535'])
    assert_returns_0(['-f', 'event_files/events_example', '-p', '1024'])
    assert_returns_1(['-f', 'event_files/events_example', '-p', '1'])

    assert_returns_0(['-f', 'event_files/events_example', '-t', '5'])
    assert_returns_1(['-f', 'event_files/events_example', '-t'])
    assert_returns_1(['-f', 'event_files/events_example', '-tt', '5'])
    assert_returns_1(['-f', 'event_files/events_example', '-', '5'])
    assert_returns_1(['-f', 'event_files/events_example', '-t', '-1'])
    assert_returns_1(['-f', 'event_files/events_example', '-t', 'a'])
    assert_returns_1(['-f', 'event_files/events_example', '-t', '0'])
    assert_returns_1(['-f', 'event_files/events_example', '-t', '86401'])
    assert_returns_0(['-f', 'event_files/events_example', '-t', '86400'])
    assert_returns_0(['-f', 'event_files/events_example', '-t', '1'])
    assert_returns_0(['-f', 'event_files/events_example', '-t', '5', '-t', '5'])

    assert_returns_0(['-f', 'event_files/events_example', '-p', '2022', '-t', '5'])
    assert_returns_1(['-f', 'event_files/events_example', '-x', '5'])
    assert_returns_1(['f', 'event_files/events_example'])

if __name__ == '__main__':
    test_commandline_params()

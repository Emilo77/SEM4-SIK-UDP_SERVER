from test_commandline_params import test_commandline_params
from test_small_correctness import test_small_correctness
from test_big_correctness import test_big_correctness
from test_limits import test_limits
from test_reservation_timing_out import test_reservation_timing_out
import os

if __name__ == '__main__':
    print("\n[Reklama] https://forms.gle/4WTiFSM1xQDfhfiW9 \n")
    print("Każdy test powinien działać poniżej 20 sekund.")
    tests = [
        test_commandline_params,
        test_small_correctness,
        test_big_correctness,
        test_limits,
        test_reservation_timing_out,
    ]
    
    try:
        for t in tests:
            print(t.__name__ + '...')
            t()
        print('Tests passed!')
    except Exception as ex:
        os.system("pkill ticket_server")
        raise ex
        

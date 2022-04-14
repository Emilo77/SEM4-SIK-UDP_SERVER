cd ..
g++ -o ticket_server ticket_server.cpp -Wall -Wextra -Wno-implicit-fallthrough -std=c++17 -O2
cd testy-zad1-main
time python3 test.py 

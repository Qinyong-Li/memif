sudo rm -rf /usr/local/lib/libmemif.so
cmake ..
make
sudo cp lib/libmemif.so /usr/local/lib
rm -rf ~/vpp/run/my.socket
gcc client.c -I ../src -L lib -lmemif -o c

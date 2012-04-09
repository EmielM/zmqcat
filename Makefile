zmqcat: zmqcat.c
	cc -g -I/usr/local/include -lzmq -o $@ $<

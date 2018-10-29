/*
 * (C) Emiel Mols, 2012. Released under the Simplified BSD License.
 * Attribution is very much appreciated.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <getopt.h>

#include <zmq.h>

#define SEND_BUFFER_SIZE 8192

void zmqcat_recv(void* socket, int type, int verbose) {

	if (type == ZMQ_PUSH || type == ZMQ_PUB)
		return;

	int ok;

	int64_t rcvmore = 0;
	size_t rcvmore_size = sizeof(rcvmore);

	zmq_msg_t msg;
	ok = zmq_msg_init(&msg);
	if (ok < 0) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return;
	}

	// Read single message (all frames) and dump to stdout.
	do {
		ok = zmq_msg_recv(&msg, socket, 0);
		if (ok < 0) {
			fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
			return;
		}

		zmq_getsockopt(socket, ZMQ_RCVMORE, &rcvmore, &rcvmore_size);

		size_t size = zmq_msg_size(&msg);
		if (verbose)
			fprintf(stderr, "receiving %ld bytes\n", size);

		fwrite(zmq_msg_data(&msg), 1, size, stdout);
	} while (rcvmore);
}

void zmqcat_send(void* socket, int type, int verbose) {
	char *sbuf, *tbuf;

	if (type == ZMQ_PULL || type == ZMQ_SUB)
		return;

	// Read stdin into a forward linked buffer. First
	// chunk is on the stack, subsequent chunks are
	// allocated on the heap when necessary.

	char stack_buffer[SEND_BUFFER_SIZE + sizeof(char *)];
	*(char **)(stack_buffer+SEND_BUFFER_SIZE) = NULL;

	char* buffer = stack_buffer;

	size_t total = 0;
	while (1) {
		size_t read = fread(buffer, 1, SEND_BUFFER_SIZE, stdin);
		total += read;

		if (read != SEND_BUFFER_SIZE)
			break;

		char *new_buffer = malloc(SEND_BUFFER_SIZE + sizeof(char *));
		*((char **)&new_buffer[SEND_BUFFER_SIZE]) = NULL;
		*((char **)&buffer[SEND_BUFFER_SIZE]) = new_buffer;
		buffer = new_buffer;
	}

	int ok;

	zmq_msg_t msg;
	ok = zmq_msg_init_size(&msg, total);
	if (ok < 0) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		goto dealloc;
	}

	char *msg_at = (char *)zmq_msg_data(&msg);
	char *msg_end = msg_at + total;

	buffer = stack_buffer;
	while (buffer != NULL) {
		size_t chunk_size = SEND_BUFFER_SIZE;
		if (msg_at + chunk_size >= msg_end)
			chunk_size = msg_end - msg_at;

		memcpy(msg_at, buffer, chunk_size);

		buffer = *(char **)&buffer[SEND_BUFFER_SIZE];
		msg_at += chunk_size;
	}

	if (verbose)
		fprintf(stderr, "sending %ld bytes\n", total);

	ok = zmq_msg_send(&msg, socket, 0);
	if (ok < 0)
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));

dealloc:
	sbuf = *(char **)&stack_buffer[SEND_BUFFER_SIZE];
	while (sbuf) {
		tbuf = sbuf;
		sbuf = *(char **)&sbuf[SEND_BUFFER_SIZE];
		free(tbuf);
	}
}

int main(int argc, char *argv[]) {
	
	int type = ZMQ_PUSH;
	char *endpoint = NULL;
	int bind = 0;
	int verbose = 0;

	char c;
	while ((c = getopt(argc, argv, "t:e:bv")) != -1) {
		if (c == 't') {
			if (!strcasecmp(optarg, "pull"))
				type = ZMQ_PULL;
			else if (!strcasecmp(optarg, "req"))
				type = ZMQ_REQ;
			else if (!strcasecmp(optarg, "rep"))
				type = ZMQ_REP;
			else if (!strcasecmp(optarg, "pub"))
				type = ZMQ_PUB;
			else if (!strcasecmp(optarg, "sub"))
				type = ZMQ_PUB;
		}
		else if (c == 'e') {
			endpoint = optarg;
		}
		else if (c == 'b') {
			bind = 1;
		}
		else if (c == 'v') {
			verbose = 1;
		}
	}

	if (!endpoint) {
		fprintf(stderr, "usage: %s [-t type] -e endpoint [-b] [-v]\n", argv[0]);
		fprintf(stderr, "  -t : PUSH | PULL | REQ | REP | PUB | SUB\n");
		fprintf(stderr, "  -e : endpoint, e.g. \"tcp://127.0.0.1:5000\"\n");
		fprintf(stderr, "  -b : bind instead of connect\n");
		fprintf(stderr, "  -v : verbose output to stderr\n");
		return 254;
	}

	void *ctx = zmq_init(1);
	if (ctx == NULL) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return 1;
	}

	void *socket = zmq_socket(ctx, type);
	if (socket == NULL) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return 1;
	}

	int ok;

	if (bind)
		ok = zmq_bind(socket, endpoint);
	else
		ok = zmq_connect(socket, endpoint);

	if (ok < 0) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return 1;
	}
	
	if (verbose)
		fprintf(stderr, "%s to %s\n", (bind ? "bound" : "connecting"), endpoint);

	if (type == ZMQ_REP) {
		zmqcat_recv(socket, type, verbose);
		zmqcat_send(socket, type, verbose);
	}
	else {
		zmqcat_send(socket, type, verbose);
		zmqcat_recv(socket, type, verbose);
	}

	ok = zmq_close(socket);
	if (ok < 0) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return 1;
	}

	ok = zmq_term(ctx);
	if (ok < 0) {
		fprintf(stderr, "error %d: %s\n", errno, zmq_strerror(errno));
		return 1;
	}

	return 0;
}

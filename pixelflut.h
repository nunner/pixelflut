#pragma once

typedef struct {
	int from, to;
	int offset_x, offset_y;
	int *quit;
} pixelflut_thread_args;

typedef struct {
	char *server;
	char *port;
	char *image;
	int offset_x;
	int offset_y;
	int num_threads;
} pixelflut_args_t;

#pragma once

typedef struct {
	int from, to;
	int offset_x, offset_y;
	int *quit;
} pixelflut_thread_args;

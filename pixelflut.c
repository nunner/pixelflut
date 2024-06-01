#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <png.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "pixelflut.h"

static char *address = NULL;
static char *server = NULL;
static char *port = NULL;
static char *image = NULL;
static char **cmds;
static int offset_x = -1;
static int offset_y = -1;
static int image_width = -1;
static int image_height = -1;
static int num_threads = 1;

static int quit = 0;
static int cmd_count = 0;

void
handle_signal(int signal)
{
	quit = 1;
}

int
setup_connection(void)
{
	int sockfd;
	int err;
	struct addrinfo hints, localhints;
	struct addrinfo *res, *localaddr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	memset(&localhints, 0, sizeof(hints));
	localhints.ai_family = AF_INET;
	localhints.ai_socktype = SOCK_STREAM;
	localhints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
	localhints.ai_protocol = 0;

	if ((err = getaddrinfo(server, port, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo: %s", gai_strerror(err));
		exit(EXIT_FAILURE);
	}

	if ((err = getaddrinfo(address, "0", &localhints, &localaddr)) != 0) {
		fprintf(stderr, "getaddrinfo: %s", gai_strerror(err));
		exit(EXIT_FAILURE);
	}

	if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
		fprintf(stderr, "socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (bind(sockfd, localaddr->ai_addr, localaddr->ai_addrlen) == -1) {
		fprintf(stderr, "bind: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	const int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1) {
		fprintf(stderr, "setsockopt: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
		fprintf(stderr, "socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return sockfd;
}

void
read_image(char *path)
{
	// https://gist.github.com/niw/5963798
	FILE *fp = fopen(path, "rb");

	if (fp == NULL) {
		fprintf(stderr, "fopen: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	png_structp png_ptr;
	if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "png_create_read_struct: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	png_infop info_ptr;
	if ((info_ptr = png_create_info_struct(png_ptr)) == NULL) {
		fprintf(stderr, "png_create_info_struct: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	png_init_io(png_ptr, fp);
	png_read_info(png_ptr, info_ptr);

	image_width = png_get_image_width(png_ptr, info_ptr);
	image_height = png_get_image_height(png_ptr, info_ptr);

	png_byte color_type = png_get_color_type(png_ptr, info_ptr);
	png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

	if(bit_depth == 16)
		png_set_strip_16(png_ptr);

	if(color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);

	if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png_ptr);

	if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);

	if(color_type == PNG_COLOR_TYPE_RGB ||
			color_type == PNG_COLOR_TYPE_GRAY ||
			color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

	if(color_type == PNG_COLOR_TYPE_GRAY ||
			color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);

	png_read_update_info(png_ptr, info_ptr);

	png_bytepp row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * image_height);
	for (int i = 0; i < image_height; i += 1){
		row_pointers[i] = (png_byte*) malloc(png_get_rowbytes(png_ptr, info_ptr));
	}

	png_read_image(png_ptr, row_pointers);

    fclose(fp);

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	cmds = malloc(image_width * image_height * sizeof(char *));
	for (int y = 0; y < image_height; y++) {
		png_byte* row = row_pointers[y];

		for (int x = 0; x < image_width; x++) {
			png_byte* ptr = &(row[x * 4]);			
			if (ptr[3] != 0) {
				asprintf(&cmds[cmd_count], "PX %d %d %02x%02x%02x\n", x, y, ptr[0], ptr[1], ptr[2]);
				cmd_count++;
			}
		}
	}
}

void *
send_pixels(void *ptr)
{
	int sockfd = setup_connection();
	pixelflut_thread_args *args = (pixelflut_thread_args *) ptr;
	FILE *f = fdopen(sockfd, "w+");

	if (f == NULL) {
		fprintf(stderr, "fdopen: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	fprintf(f, "OFFSET %d %d\n", offset_x, offset_y);

	while (!*args->quit) {
		for (int i = args->from; i < args->to && i < cmd_count; i++) {
			fprintf(f, "%s", cmds[i]);
		}
	}

	fclose(f);
	exit(EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
	struct sigaction sa = { .sa_handler = handle_signal };
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	char c;

	while ((c = getopt(argc, argv, "a:s:p:f:x:y:t:h")) != -1) {
		switch(c) {
			case 'a':
				address = optarg;
				break;
			case 's':
				server = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			case 'f':
				image = optarg;
				break;
			case 'x':
				offset_x = strtol(optarg, NULL, 0);
				break;
			case 'y':
				offset_y = strtol(optarg, NULL, 0);
				break;
			case 't':
				num_threads = strtol(optarg, NULL, 0);
				break;
			case 'h':
			default: 
				fprintf(stderr, "Usage: %s -a address -s server -p port -f file -x offset_x -y offset_y -t threads\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (address == NULL || server == NULL || port == NULL || image == NULL || offset_x == -1 || offset_y == -1) {
			fprintf(stderr, "Not all parameters were specified.\n");
			fprintf(stderr, "Usage: %s -a address -h server -p port -f file -x offset_x -y offset_y\n", argv[0]);
			exit(EXIT_FAILURE);
	}

	read_image(image);

	pthread_t threads[num_threads];
	pixelflut_thread_args args[num_threads];
	int steps = cmd_count/num_threads;

	for (int i = 0; i < num_threads; i++) {
		fprintf(stdout, "Starting thread %d\n", i);

		args[i].from = i*steps;
		if (i == num_threads-1) {
			args[i].to = cmd_count;
		} else {
			args[i].to = (i+1)*steps;
		}
		args[i].offset_x = offset_x;
		args[i].offset_y = offset_y;
		args[i].quit = &quit;
		pthread_create(&threads[i], NULL, *send_pixels, &args[i]);
	}

	while(!quit) ;

	for (int i = 0; i < num_threads; i++) {
		pthread_join(threads[i], NULL);
	}

	return 0;
}

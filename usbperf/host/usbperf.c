/*
 * Libusb performance Test for g_zero gadget
 *
 * This file may be used by anyone for any purpose and may be used as a
 * starting point making your own application using g_zero gadget.
 */

/* C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <stdbool.h>

/* Unix */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <pthread.h>
#include <wchar.h>

/* GNU / LibUSB */
#include "libusb.h"

#include <sys/time.h>
#include <signal.h>
static libusb_context *context = NULL;
libusb_device_handle *handle;
unsigned int packets;
unsigned long long bytes;
struct timeval start;
int seq = -1;
int test_case = 1;

int buflen = 64; /* Overwritten by command line param */

void int_handler(int signal) {
	struct timeval end;
	struct timeval res;
	double elapsed;
	
	gettimeofday(&end, NULL);
	
	timersub(&end, &start, &res);
	elapsed = res.tv_sec + (double) res.tv_usec / 1000000.0;
	
	printf("\nelapsed: %.6f seconds\n", elapsed);
	printf("packets: %u\n", packets);
	printf("packets/sec: %f\n", (double)packets/elapsed);
	printf("bytes/sec: %f\n", (double)bytes/elapsed);
	printf("MBit/sec: %f\n", (double)bytes/elapsed * 8 / 1000000);

	libusb_attach_kernel_driver(handle, 0);
        libusb_close(handle);
        libusb_exit(context);

	exit(1);
}

static void read_callback(struct libusb_transfer *transfer)
{
	int res;
	
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		unsigned char pkt_seq = transfer->buffer[0];

		packets++;
		bytes += transfer->actual_length;
		
		seq = pkt_seq;
	}
	else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		printf("Cancelled\n");
		return;
	}
	else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		printf("No Device\n");
		return;
	}
	else if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT) {
		printf("Timeout (normal)\n");
	}
	else {
		printf("Unknown transfer code: %d\n", transfer->status);
	}

	/* Re-submit the transfer object. */
	res = libusb_submit_transfer(transfer);
	if (res != 0) {
		printf("Unable to submit URB. libusb error code: %d\n", res);
	}
}


static struct libusb_transfer *
create_transfer(libusb_device_handle *handle, size_t length, bool out)
{
	struct libusb_transfer *transfer;
	unsigned char *buf;

	/* Set up the transfer object. */
	buf = calloc(1, length);
	transfer = libusb_alloc_transfer(0);
	if (out) {
		/* Bulk OUT */
		libusb_fill_bulk_transfer(transfer,
			handle,
			0x01 /*ep*/,
			buf,
			length,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);
	}
	else {
		/* Bulk IN */
		libusb_fill_bulk_transfer(transfer,
			handle,
			0x81 /*ep*/,
			buf,
			length,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);
	}
	return transfer;
}

int main(int argc, char **argv)
{
	int i, c, res = 0;
	bool show_help = false;
#if 0
	bool send = false;
#endif
	unsigned char *buf;
	int actual_length;

	/* Parse options */
	while ((c = getopt(argc, argv, "cohl:")) > 0) {
		switch (c) {
		case 'c':
			test_case = atoi(optarg);
			break;
		case 'l':
			buflen = atoi(optarg);
			if (buflen <= 0) {
				fprintf(stderr, "Invalid length\n");
				return 1;
			}
			break;
#if 0
		case 'o':
			send = true;
			break;
#endif
		case 'h':
			show_help = true;
			break;
		default:
			printf("Invalid option -%c\n", c);
			show_help = true;
			break;
		};

		if (show_help)
			break;
	}

	if (optind < argc && !show_help) {
		fprintf(stderr, "Invalid arguments:\n");
		while (optind < argc)
			fprintf(stderr, "\t%s", argv[optind++]);
		printf("\n");

		return 1;
	}
	
	if (show_help) {
		fprintf(stderr,
			"%s: Test USB Device Performance\n"
			"\t-l <length>           length of transfer\n"
			"\t-h                    show help\n"
			"\t-c <case>             test case\n"
			"\t\t1: async bulk out\n"
			"\t\t2: sync bulk out\n",
			argv[0]);
		return 1;
	}

	/* Init Libusb */
	if (libusb_init(&context))
		return -1;

	handle = libusb_open_device_with_vid_pid(NULL, 0x0525, 0xa4a0);
	if (!handle) {
		fprintf(stderr, "libusb_open failed\n");
		return 1;
	}

	libusb_detach_kernel_driver(handle, 0);
	res = libusb_set_configuration(handle, 3);
	if (res < 0) {
		perror("set configuration");
		return 1;
	}

	res = libusb_claim_interface(handle, 0);
	if (res < 0) {
		perror("claim interface");
		return 1;
	}

	res = libusb_set_interface_alt_setting(handle, 0, 1);
	if (res < 0) {
		perror("set alt interface");
		return 1;
	}

	gettimeofday(&start, NULL);
	signal(SIGINT, int_handler);
	
	printf("TEST CASE %d\n", test_case);
	printf("Using transfer size of: %d\n", buflen);

	switch (test_case) {
	case 1: /* async bulk out */
		for (i = 0; i < 32; i++) {
			struct libusb_transfer *transfer =
				create_transfer(handle, buflen, true);
			libusb_submit_transfer(transfer);
		}

		while (1) {
			res = libusb_handle_events(context);
			if (res < 0) {
				/* There was an error. */
				printf("read_thread(): libusb reports error # %d\n", res);

				/* Break out of this loop only on fatal error.*/
				if (res != LIBUSB_ERROR_BUSY &&
				    res != LIBUSB_ERROR_TIMEOUT &&
				    res != LIBUSB_ERROR_OVERFLOW &&
				    res != LIBUSB_ERROR_INTERRUPTED) {
					break;
				}
			}
		}

		break;

	case 2: /* sync bulk out */
		buf = calloc(1, buflen);

		do {
			res = libusb_bulk_transfer(handle, 0x01,
				buf, buflen, &actual_length, 100000);
			if (res < 0) {
				fprintf(stderr, "bulk transfer (out): %s\n", libusb_error_name(res));
				return 1;
			}
#if 0
			}
			else {
				/* Receive data from the device */
				res = libusb_bulk_transfer(handle, 0x81,
					buf, buflen, &actual_length, 100000);
				if (res < 0) {
					fprintf(stderr, "bulk transfer (in): %s\n", libusb_error_name(res));
					return 1;
				}
			}
#endif
			packets++;
			bytes += actual_length;
		} while (res >= 0);

		break;
	default:
		perror("unknown test case");
		break;
	}

	return 0;
}
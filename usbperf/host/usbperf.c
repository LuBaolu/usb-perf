/*
 * Libusb performance Test for g_zero gadget
 *
 * This file may be used by anyone for any purpose and may be used as a
 * starting point making your own application using g_zero gadget.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <pthread.h>
#include <wchar.h>
#include <sys/time.h>
#include <signal.h>
#include <linux/usb/functionfs.h>

#include "libusb.h"

static libusb_context *context = NULL;
libusb_device_handle *handle;
unsigned int packets;
unsigned long long bytes;
struct timeval start;
struct timeval end;
unsigned int testcase = 1;
unsigned int buflen = 64;
unsigned int iterations = 100;
unsigned int isoc_qlen = 1;
unsigned int bulk_qlen = 32;

#define ISOC_LATENCY_MAGIC	0xabadbeef
struct isoc_time_stamp {
	__u32		magic;
	__u32		out;	/* 0 - isoc in; 1 - isoc out */
	struct timeval	cp_01;	/* application check point */
	struct timeval	cp_02;	/* usbcore check point */
	struct timeval	cp_03;	/* firmware check point */
};

static void reinit_iso_out_data(struct libusb_transfer *transfer)
{
	struct  isoc_time_stamp *stamp;

	stamp = (struct isoc_time_stamp *)transfer->buffer;
	stamp->magic = ISOC_LATENCY_MAGIC;
	stamp->out = 1;
	gettimeofday(&stamp->cp_01, NULL);
}

static void check_iso_in_data(struct libusb_transfer *transfer)
{
	struct  isoc_time_stamp *stamp;
	struct  timeval delta;

	stamp = (struct isoc_time_stamp *)transfer->buffer;
	gettimeofday(&stamp->cp_01, NULL);
	timersub(&stamp->cp_01, &stamp->cp_03, &delta);
	printf("isoc_in: magic %x out %d elapsed(us) %ld\n",
		stamp->magic,
		stamp->out,
		delta.tv_sec * 1000000 + delta.tv_usec);
}

static void do_result(double elapsed) {
	printf("\nelapsed: %.6f seconds\n", elapsed);
	printf("packets: %u\n", packets);
	printf("packets/sec: %f\n", (double)packets/elapsed);
	printf("bytes/sec: %f\n", (double)bytes/elapsed);
	printf("MBit/sec: %f\n", (double)bytes/elapsed * 8 / 1000000);
	printf("Mbytes/sec: %f\n", (double)bytes/elapsed / 1000000);
}

static void handle_async_events(void)
{
	int res;

	while (1) {
		res = libusb_handle_events(context);
		if (res < 0) {
			/* There was an error. */
			printf("read_thread(): libusb reports error # %d\n", res);

			/* Break out of this loop only on fatal error.*/
			if (res != LIBUSB_ERROR_BUSY &&
			    res != LIBUSB_ERROR_TIMEOUT &&
			    res != LIBUSB_ERROR_OVERFLOW &&
			    res != LIBUSB_ERROR_INTERRUPTED)
				break;
		}
	}

	return;
}

void int_handler(int signal) {
	struct timeval res;
	double elapsed;
	
	gettimeofday(&end, NULL);
	
	timersub(&end, &start, &res);
	elapsed = res.tv_sec + (double) res.tv_usec / 1000000.0;
	do_result(elapsed);

	exit(1);
}

static void read_callback(struct libusb_transfer *transfer)
{
	int res;

	switch (transfer->endpoint) {
	case 0x02: /* isoc out */
		reinit_iso_out_data(transfer);
		break;
	case 0x82: /* isoc in */
		check_iso_in_data(transfer);
		break;
	default:
		break;
	}
	
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		packets++;
		bytes += transfer->actual_length;
	} else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		printf("Cancelled\n");
		return;
	} else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		printf("No Device\n");
		return;
	} else if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT) {
		printf("Timeout (normal)\n");
	} else {
		printf("Unknown transfer code: %d\n", transfer->status);
	}

	/* Re-submit the transfer object. */
	res = libusb_submit_transfer(transfer);
	if (res != 0) {
		printf("Unable to submit URB. libusb error code: %d\n", res);
	}
}


static struct libusb_transfer *
create_transfer(libusb_device_handle *handle,
			size_t length,
			bool out)
{
	struct libusb_transfer *transfer;
	unsigned char *buf;

	/* Set up the transfer object. */
	buf = calloc(1, length);
	transfer = libusb_alloc_transfer(0);
	if (out)
		libusb_fill_bulk_transfer(transfer,
			handle,
			0x01 /*ep*/,
			buf,
			length,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);
	else
		libusb_fill_bulk_transfer(transfer,
			handle,
			0x81 /*ep*/,
			buf,
			length,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);

	return transfer;
}

static struct libusb_transfer *
create_isoc_transfer(libusb_device_handle *handle,
			size_t length,
			bool out,
			int isoc_packets)
{
	struct libusb_transfer *transfer;
	unsigned char *buf;

	/* Set up the transfer object. */
	buf = calloc(1, length);
	transfer = libusb_alloc_transfer(isoc_packets);
	if (out)
		libusb_fill_iso_transfer(transfer,
			handle,
			0x02,
			buf,
			length,
			isoc_packets,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);
	else
		libusb_fill_iso_transfer(transfer,
			handle,
			0x82,
			buf,
			length,
			isoc_packets,
			read_callback,
			NULL/*cb data*/,
			5000/*timeout*/);

	libusb_set_iso_packet_lengths(transfer, length/isoc_packets);
	return transfer;
}

static int parse_num(unsigned *num, const char *str)
{
	unsigned long val;
	char *end;

	errno = 0;
	val = strtoul(str, &end, 0);
	if (errno || *end || val > UINT_MAX)
		return -1;
	*num = val;
	return 0;
}

int main(int argc, char **argv)
{
	int i, c, res = 0;
	bool show_help = false;
	unsigned char *buf;
	int actual_length;
	struct timeval delta;
	double elapsed;

	/* Parse options */
	while ((c = getopt(argc, argv, "i:c:h:l:")) != EOF) {
		switch (c) {
		case 'i':
			res = parse_num(&iterations, optarg);
			if (res < 0)
				show_help = true;
			break;
		case 'c':
			res = parse_num(&testcase, optarg);
			if (res < 0)
				show_help = true;
			break;
		case 'l':
			res = parse_num(&buflen, optarg);
			if (res < 0)
				show_help = true;
			break;
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
			"\t-i <interations>      interations",
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

	printf("TEST CASE %d\n", testcase);
	printf("Using transfer size of: %d\n", buflen);

	switch (testcase) {
	case 1: /* async bulk out */
		gettimeofday(&start, NULL);
		signal(SIGINT, int_handler);
		for (i = 0; i < bulk_qlen; i++) {
			struct libusb_transfer *transfer =
				create_transfer(handle, buflen, true);
			libusb_submit_transfer(transfer);
		}

		handle_async_events();
		break;
	case 2: /* sync bulk out */
		buf = calloc(1, buflen);
		gettimeofday(&start, NULL);

		do {
			res = libusb_bulk_transfer(handle, 0x01,
				buf, buflen, &actual_length, 100000);
			if (res < 0) {
				fprintf(stderr, "bulk transfer (out): %s\n",
					libusb_error_name(res));
				return 1;
			}

			packets++;
			bytes += actual_length;
			iterations--;
		} while (iterations > 0);

		gettimeofday(&end, NULL);
		timersub(&end, &start, &delta);
		elapsed = delta.tv_sec + (double) delta.tv_usec / 1000000.0;
		do_result(elapsed);

		break;
	case 3: /* async bulk in */
		gettimeofday(&start, NULL);
		signal(SIGINT, int_handler);
		for (i = 0; i < bulk_qlen; i++) {
			struct libusb_transfer *transfer =
				create_transfer(handle, buflen, false);
			libusb_submit_transfer(transfer);
		}

		handle_async_events();
		break;
	case 4: /* sync bulk in */
		buf = calloc(1, buflen);
		gettimeofday(&start, NULL);

		do {
			res = libusb_bulk_transfer(handle, 0x81,
				buf, buflen, &actual_length, 100000);
			if (res < 0) {
				fprintf(stderr, "bulk transfer (in): %s\n",
					libusb_error_name(res));
				return 1;
			}

			packets++;
			bytes += actual_length;
			iterations--;
		} while (iterations > 0);

		gettimeofday(&end, NULL);
		timersub(&end, &start, &delta);
		elapsed = delta.tv_sec + (double) delta.tv_usec / 1000000.0;
		do_result(elapsed);

		break;
	case 5: /* isoc out */
		gettimeofday(&start, NULL);
		signal(SIGINT, int_handler);
		for (i = 0; i < isoc_qlen; i++) {
			struct libusb_transfer *transfer =
				create_isoc_transfer(handle, buflen, true, 1);
			reinit_iso_out_data(transfer);
			libusb_submit_transfer(transfer);
		}

		handle_async_events();

		break;
	case 6: /* isoc in */
		gettimeofday(&start, NULL);
		signal(SIGINT, int_handler);
		for (i = 0; i < isoc_qlen; i++) {
			struct libusb_transfer *transfer =
				create_isoc_transfer(handle, buflen, false, 1);
			libusb_submit_transfer(transfer);
		}

		handle_async_events();
		break;
	default:
		perror("unknown test case");
		break;
	}

	libusb_attach_kernel_driver(handle, 0);
	libusb_close(handle);
	libusb_exit(context);

	return 0;
}

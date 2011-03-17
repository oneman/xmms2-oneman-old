#define RINGBUFTEST TRUE
#include "src/includepriv/xmmspriv/xmms_ringbuf.h"

#include "src/xmms/ringbuf.c"

#include <stdio.h>
#include <glib.h>




void *ringbuf_filler(void *arg);
void *ringbuf_reader(void *arg);
void ringbuf_info(xmms_ringbuf_t *ringbuffer, char *caller);

main() {

	printf("XMMS2 Ringbuffer Test\n\n");

	xmms_ringbuf_t *ringbuffer;
	int size, free, used;
	char *name = "Main";
	GThread *filler_thread;
	GThread *reader_thread;
	g_thread_init (NULL);


	static GMainLoop *mainloop;


	size = 8;

	printf("Creating Ringbuffer\n");
	ringbuffer = xmms_ringbuf_new (size);
	ringbuf_info(ringbuffer, name);

	printf("Creating Threads\n");
	filler_thread = g_thread_create (ringbuf_filler, ringbuffer, TRUE, NULL);
	reader_thread = g_thread_create (ringbuf_reader, ringbuffer, TRUE, NULL);

	mainloop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (mainloop);
	//xmms_ringbuf_destroy (ringbuffer);

}

void *
ringbuf_filler(void *arg) {

	xmms_ringbuf_t *ringbuffer = (xmms_ringbuf_t *)arg;
	GMutex *filler_mutex;
	char buf[1];
	int cnt, wrote;
	char *name = "Filler";
	//gulong sleeptime;

	cnt = sizeof(buf);
	//sleeptime = 555555;

	filler_mutex = g_mutex_new ();
	g_mutex_lock(filler_mutex);

	while(1) {
		ringbuf_info(ringbuffer, name);
		wrote = xmms_ringbuf_write (ringbuffer, buf, cnt);
		printf("Wrote %d\n", wrote);
		if(wrote == 0)
			xmms_ringbuf_wait_free (ringbuffer, cnt, filler_mutex);
	}
}


void *
ringbuf_reader(void *arg) {

	xmms_ringbuf_t *ringbuffer = (xmms_ringbuf_t *)arg;
	GMutex *read_mutex;
	int cnt, read;
	char readbuf[16];
	cnt = 1;
	char *name = "Reader";


	read_mutex = g_mutex_new ();
	g_mutex_lock(read_mutex);

	while(1) {
		xmms_ringbuf_wait_used (ringbuffer, cnt, read_mutex);
		ringbuf_info(ringbuffer, name);
		read = xmms_ringbuf_read (ringbuffer, readbuf, cnt);
		printf("Read %d\n", read);
		ringbuf_info(ringbuffer, name);
	}
}


void 
ringbuf_info(xmms_ringbuf_t *ringbuffer, char *caller) {

	printf("%s Ringbuffer: Size: %d Free: %d Used: %d\n", 
			caller,
			xmms_ringbuf_size(ringbuffer), 
			xmms_ringbuf_bytes_free(ringbuffer), 
			xmms_ringbuf_bytes_used(ringbuffer));

}


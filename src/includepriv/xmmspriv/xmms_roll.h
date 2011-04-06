#include "xmmspriv/xmms_output.h"
#include "xmmspriv/xmms_ringbuf.h"


#include <samplerate.h>

/* oneman was here */

typedef struct {

	SRC_STATE *resampler;

	gint winsize;
	gint channels;
	gint bufsize;

	xmms_sample_t *iobuf;
	gfloat *resbuf;
	GString *outbuf;

	gfloat pitch;
	SRC_DATA resdata;
	gboolean enabled;
} xmms_roll_data_t;


 xmms_roll_data_t *xmms_roll_init (xmms_roll_data_t *data);
 void xmms_roll_destroy (xmms_roll_data_t *data);

int xmms_roll_read (xmms_roll_data_t *data, xmms_ringbuf_t *ringbuf, xmms_sample_t *buffer, gint len, gfloat rat);


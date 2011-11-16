#ifndef A_SHN_ENCODE
#define A_SHN_ENCODE
#ifndef STANDALONE
#include <Python.h>
#endif

#include <stdint.h>
#include "../bitstream.h"
#include "../array2.h"

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2011  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************/

typedef enum {OK, ERROR, RESIDUAL_OVERFLOW} status;

struct alac_encoding_options {
    unsigned block_size;
    unsigned initial_history;
    unsigned history_multiplier;
    unsigned maximum_k;
    unsigned minimum_interlacing_leftweight;
    unsigned maximum_interlacing_leftweight;
    unsigned minimum_interlacing_shift;
    unsigned maximum_interlacing_shift;
};

/*this is a container for encoding options and reusable data buffers*/
struct alac_context {
    struct alac_encoding_options options;

    unsigned bits_per_sample;

    unsigned frame_byte_size;
    unsigned mdat_byte_size;
    array_ia* frame_log;

    BitstreamWriter *best_frame;
    BitstreamWriter *current_frame;
};

enum {LOG_SAMPLE_SIZE, LOG_BYTE_SIZE, LOG_FILE_OFFSET};

/*initializes all the temporary buffers in encoder*/
void
alacenc_init_encoder(struct alac_context* encoder);

/*deallocates all the temporary buffers in encoder*/
void
alacenc_free_encoder(struct alac_context* encoder);

#ifndef STANDALONE
PyObject
*alac_log_output(struct alac_context *encoder);
#endif

void
alac_byte_counter(uint8_t byte, void* counter);

/*writes a full set of ALAC frames,
  complete with trailing stop '111' bits and byte-aligned*/
status
alac_write_frameset(BitstreamWriter *bs,
                    struct alac_context* encoder,
                    const array_ia* channels);

/*write a single ALAC frame, compressed or uncompressed as necessary*/
status
alac_write_frame(BitstreamWriter *bs,
                 struct alac_context* encoder,
                 const array_ia* channels);

/*writes a single uncompressed ALAC frame, not including the channel count*/
status
alac_write_uncompressed_frame(BitstreamWriter *bs,
                              struct alac_context* encoder,
                              const array_ia* channels);


/* status */
/* alac_write_uncompressed_frame(BitstreamWriter *bs, */
/*                               int block_size, */
/*                               int bits_per_sample, */
/*                               struct ia_array *samples); */

/* status */
/* alac_write_compressed_frame(BitstreamWriter *bs, */
/*                             struct alac_encoding_options *options, */
/*                             int bits_per_sample, */
/*                             struct ia_array *samples); */

/* status */
/* alac_write_interlaced_frame(BitstreamWriter *bs, */
/*                             struct alac_encoding_options *options, */
/*                             int interlacing_shift, */
/*                             int interlacing_leftweight, */
/*                             int bits_per_sample, */
/*                             struct ia_array *samples); */

/* status */
/* alac_correlate_channels(struct ia_array *output, */
/*                         struct ia_array *input, */
/*                         int interlacing_shift, */
/*                         int interlacing_leftweight); */

/* status */
/* alac_encode_subframe(struct i_array *residuals, */
/*                      struct i_array *samples, */
/*                      struct i_array *coefficients, */
/*                      int predictor_quantitization); */

/* /\*writes a single unsigned residal to the current bitstream*\/ */
/* void */
/* alac_write_residual(BitstreamWriter *bs, */
/*                     int residual, */
/*                     int k, */
/*                     int bits_per_sample); */

/* status */
/* alac_write_residuals(BitstreamWriter *bs, */
/*                      struct i_array *residuals, */
/*                      int bits_per_sample, */
/*                      struct alac_encoding_options *options); */

/* void */
/* alac_error(const char* message); */

/* void */
/* alac_warning(const char* message); */

#endif

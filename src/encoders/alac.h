#ifndef A_SHN_ENCODE
#define A_SHN_ENCODE

#include <Python.h>

#include <stdint.h>
#include "../bitstream_w.h"
#include "../array.h"
#include "../pcmreader.h"

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2010  Brian Langenberger

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

typedef enum {OK,ERROR} status;

/*this output log is a set of things Python-based ALAC atom writers will need
  in order to populate metadata*/
struct alac_encode_log {
  int frame_byte_size;
  int mdat_byte_size;
  struct ia_array frame_log;
};

struct alac_encoding_options {
  int block_size;
  int initial_history;
  int history_multiplier;
  int maximum_k;
};

enum {LOG_SAMPLE_SIZE,LOG_BYTE_SIZE,LOG_FILE_OFFSET};

void alac_log_init(struct alac_encode_log *log);
void alac_log_free(struct alac_encode_log *log);
PyObject *alac_log_output(struct alac_encode_log *log);

void ALACEncoder_byte_counter(unsigned int byte, void* counter);

status ALACEncoder_write_frame(Bitstream *bs,
			       struct alac_encode_log *log,
			       long starting_offset,
			       struct alac_encoding_options *options,
			       int bits_per_sample,
			       struct ia_array *samples);

status ALACEncoder_write_uncompressed_frame(Bitstream *bs,
					    int block_size,
					    int bits_per_sample,
					    struct ia_array *samples);

#endif

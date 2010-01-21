#include "flac.h"
#include "flac_lpc.h"
#include "../pcmreader.h"
#include <openssl/md5.h>

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

#define VERSION_STRING_(x) #x
#define VERSION_STRING(x) VERSION_STRING_(x)
const static char* AUDIOTOOLS_VERSION = VERSION_STRING(VERSION);

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#ifndef STANDALONE
PyObject* encoders_encode_flac(PyObject *dummy,
			       PyObject *args, PyObject *keywds) {
  char *filename;
  FILE *file;
  Bitstream *stream;
  PyObject *pcmreader_obj;
  struct pcm_reader *reader;
  struct flac_STREAMINFO streaminfo;
  char version_string[0xFF];
  static char *kwlist[] = {"filename","pcmreader",
			   "block_size",
			   "max_lpc_order",
			   "min_residual_partition_order",
			   "max_residual_partition_order",NULL};
  MD5_CTX md5sum;

  struct ia_array samples;

  /*extract a filename, PCMReader-compatible object and encoding options:
    blocksize int*/
  if (!PyArg_ParseTupleAndKeywords(args,keywds,"sOiiii",
				   kwlist,
				   &filename,
				   &pcmreader_obj,
				   &(streaminfo.options.block_size),
				   &(streaminfo.options.max_lpc_order),
				   &(streaminfo.options.min_residual_partition_order),
				   &(streaminfo.options.max_residual_partition_order)))
    return NULL;

  if (streaminfo.options.block_size <= 0) {
    PyErr_SetString(PyExc_ValueError,"blocksize must be positive");
    return NULL;
  }

  streaminfo.options.qlp_coeff_precision = FlacEncoder_qlp_coeff_precision(streaminfo.options.block_size);

  /*open the given filename for writing*/
  if ((file = fopen(filename,"wb")) == NULL) {
    PyErr_SetFromErrnoWithFilename(PyExc_IOError,filename);
    return NULL;
  }

  /*transform the Python PCMReader-compatible object to a pcm_reader struct*/
  if ((reader = pcmr_open(pcmreader_obj)) == NULL) {
    fclose(file);
    Py_DECREF(pcmreader_obj);
    return NULL;
  }

#else

  int encoders_encode_flac(char *filename,
                           FILE *input,
			   int block_size,
			   int max_lpc_order,
			   int min_residual_partition_order,
			   int max_residual_partition_order) {
  FILE *file;
  Bitstream *stream;
  struct pcm_reader *reader;
  struct flac_STREAMINFO streaminfo;
  char version_string[0xFF];
  MD5_CTX md5sum;

  struct ia_array samples;

  streaminfo.options.block_size = block_size;
  streaminfo.options.max_lpc_order = max_lpc_order;
  streaminfo.options.min_residual_partition_order = min_residual_partition_order;
  streaminfo.options.max_residual_partition_order = max_residual_partition_order;
  streaminfo.options.qlp_coeff_precision = FlacEncoder_qlp_coeff_precision(block_size);

  file = fopen(filename,"wb");
  reader = pcmr_open(input,44100,2,16); /*FIXME - assume CD quality for now*/

#endif

  sprintf(version_string,"Python Audio Tools %s",AUDIOTOOLS_VERSION);
  MD5_Init(&md5sum);
  pcmr_add_callback(reader,md5_update,&md5sum);

  stream = bs_open(file);
  bs_add_callback(stream,flac_crc8,&(streaminfo.crc8));
  bs_add_callback(stream,flac_crc16,&(streaminfo.crc16));

  /*fill streaminfo with some placeholder values*/
  streaminfo.minimum_block_size = 0xFFFF;
  streaminfo.maximum_block_size = 0;
  streaminfo.minimum_frame_size = 0xFFFFFF;
  streaminfo.maximum_frame_size = 0;
  streaminfo.sample_rate = reader->sample_rate;
  streaminfo.channels = reader->channels;
  streaminfo.bits_per_sample = reader->bits_per_sample;
  streaminfo.total_samples = 0;
  memcpy(streaminfo.md5sum,
	 "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	 16);
  streaminfo.crc8 = streaminfo.crc16 = 0;
  streaminfo.total_frames = 0;

  /*write FLAC stream header*/
  stream->write_bits(stream,32,0x664C6143);

  /*write metadata header*/
  stream->write_bits(stream,1,0);
  stream->write_bits(stream,7,0);
  stream->write_bits(stream,24,34);

  /*write placeholder STREAMINFO*/
  FlacEncoder_write_streaminfo(stream,streaminfo);

  /*write VORBIS_COMMENT*/
  stream->write_bits(stream,1,1);
  stream->write_bits(stream,7,4);
  stream->write_bits(stream,24,4 + strlen(version_string) + 4);

  /*this is a hack to fake little-endian output*/
  stream->write_bits(stream,8,strlen(version_string));
  stream->write_bits(stream,24,0);
  fputs(version_string,file);
  stream->write_bits(stream,32,0);

  /*build frames until reader is empty,
    which updates STREAMINFO in the process*/
  iaa_init(&samples,reader->channels,streaminfo.options.block_size);

  if (!pcmr_read(reader,streaminfo.options.block_size,&samples))
    goto error;

  while (iaa_getitem(&samples,0)->size > 0) {
    FlacEncoder_write_frame(stream,&streaminfo,&samples);

    if (!pcmr_read(reader,streaminfo.options.block_size,&samples))
      goto error;
  }

  /*go back and re-write STREAMINFO with complete values*/
  MD5_Final(streaminfo.md5sum,&md5sum);
  fseek(stream->file, 4 + 4, SEEK_SET);
  FlacEncoder_write_streaminfo(stream,streaminfo);

  iaa_free(&samples); /*deallocate the temporary samples block*/
  pcmr_close(reader); /*close the pcm_reader object
			which calls pcmreader.close() in the process*/
  bs_close(stream);     /*close the output file*/
#ifndef STANDALONE
  Py_INCREF(Py_None);
  return Py_None;
 error:
  /*an error result does everything a regular result does
    but returns NULL instead of Py_None*/
  iaa_free(&samples);
  pcmr_close(reader);
  bs_close(stream);
  return NULL;
}
#else
 return 1;
 error:
 iaa_free(&samples);
 pcmr_close(reader);
 bs_close(stream);
 return 0;
}
#endif


void FlacEncoder_write_streaminfo(Bitstream *bs,
				  struct flac_STREAMINFO streaminfo) {
  int i;

  bs->write_bits(bs,16,streaminfo.minimum_block_size);
  bs->write_bits(bs,16,streaminfo.maximum_block_size);
  bs->write_bits(bs,24,streaminfo.minimum_frame_size);
  bs->write_bits(bs,24,streaminfo.maximum_frame_size);
  bs->write_bits(bs,20,streaminfo.sample_rate);
  bs->write_bits(bs,3,streaminfo.channels - 1);
  bs->write_bits(bs,5,streaminfo.bits_per_sample - 1);
  bs->write_bits64(bs,36,streaminfo.total_samples);
  for (i = 0; i < 16; i++)
    bs->write_bits(bs,8,streaminfo.md5sum[i]);
}

void FlacEncoder_write_frame(Bitstream *bs,
			     struct flac_STREAMINFO *streaminfo,
			     struct ia_array *samples) {
  uint32_t i;
  long startpos;
  long framesize;
  int channel_assignment;

  streaminfo->crc8 = streaminfo->crc16 = 0;

  startpos = ftell(bs->file);

  channel_assignment = samples->size - 1;
  FlacEncoder_write_frame_header(bs,streaminfo,samples,channel_assignment);

  /*for each channel in samples, write a subframe*/

  /*first, try independent  subframes*/
  for (i = 0; i < samples->size; i++) {
    FlacEncoder_write_subframe(bs,
			       &(streaminfo->options),
			       streaminfo->bits_per_sample,
			       iaa_getitem(samples,i));
  }

  /* if (samples->size == 2) { */
  /*   next_subframe = bbw_open(samples->size); */

  /*   /\*if 2 channels, try difference subframes*\/ */
  /*   channel_assignment = 0x8; */
  /*   ia_sub(iaa_getitem(samples,1), */
  /* 	   iaa_getitem(samples,0),iaa_getitem(samples,1)); */
  /*   FlacEncoder_write_subframe(next_subframe, */
  /* 			       &(streaminfo->options), */
  /* 			       streaminfo->bits_per_sample, */
  /* 			       iaa_getitem(samples,0)); */
  /*   FlacEncoder_write_subframe(next_subframe, */
  /* 			       &(streaminfo->options), */
  /* 			       streaminfo->bits_per_sample + 1, */
  /* 			       iaa_getitem(samples,1)); */

  /*   if (next_subframe->bits_written < best_subframe->bits_written) { */
  /*     bbw_close(best_subframe); */
  /*     best_subframe = next_subframe; */
  /*   } else { */
  /*     channel_assignment = 1; */
  /*     bbw_close(next_subframe); */
  /*   } */
  /* } */

  bs->byte_align(bs);

  /*write CRC-16*/
  bs->write_bits(bs, 16, streaminfo->crc16);

  /*update streaminfo with new values*/
  framesize = ftell(bs->file) - startpos;

  streaminfo->minimum_block_size = MIN(streaminfo->minimum_block_size,
				       iaa_getitem(samples,0)->size);
  streaminfo->maximum_block_size = MAX(streaminfo->maximum_block_size,
				       iaa_getitem(samples,0)->size);
  streaminfo->minimum_frame_size = MIN(streaminfo->minimum_frame_size,
				       framesize);
  streaminfo->maximum_frame_size = MAX(streaminfo->maximum_frame_size,
				       framesize);
  streaminfo->total_samples += iaa_getitem(samples,0)->size;
  streaminfo->total_frames++;
}

void FlacEncoder_write_frame_header(Bitstream *bs,
				    struct flac_STREAMINFO *streaminfo,
				    struct ia_array *samples,
				    int channel_assignment) {
  int block_size_bits;
  int sample_rate_bits;
  int bits_per_sample_bits;
  int block_size = iaa_getitem(samples,0)->size;

  /*determine the block size bits from the given amount of samples*/
  switch (block_size) {
  case 192:   block_size_bits = 0x1; break;
  case 576:   block_size_bits = 0x2; break;
  case 1152:  block_size_bits = 0x3; break;
  case 2304:  block_size_bits = 0x4; break;
  case 4608:  block_size_bits = 0x5; break;
  case 256:   block_size_bits = 0x8; break;
  case 512:   block_size_bits = 0x9; break;
  case 1024:  block_size_bits = 0xA; break;
  case 2048:  block_size_bits = 0xB; break;
  case 4096:  block_size_bits = 0xC; break;
  case 8192:  block_size_bits = 0xD; break;
  case 16384: block_size_bits = 0xE; break;
  case 32768: block_size_bits = 0xF; break;
  default:
    if (iaa_getitem(samples,0)->size < (0xFF + 1))
      block_size_bits = 0x6;
    else if (iaa_getitem(samples,0)->size < (0xFFFF + 1))
      block_size_bits = 0x7;
    else
      block_size_bits = 0x0;
    break;
  }

  /*determine sample rate bits from streaminfo*/
  switch (streaminfo->sample_rate) {
  case 88200:  sample_rate_bits = 0x1; break;
  case 176400: sample_rate_bits = 0x2; break;
  case 192000: sample_rate_bits = 0x3; break;
  case 8000:   sample_rate_bits = 0x4; break;
  case 16000:  sample_rate_bits = 0x5; break;
  case 22050:  sample_rate_bits = 0x6; break;
  case 24000:  sample_rate_bits = 0x7; break;
  case 32000:  sample_rate_bits = 0x8; break;
  case 44100:  sample_rate_bits = 0x9; break;
  case 48000:  sample_rate_bits = 0xA; break;
  case 96000:  sample_rate_bits = 0xB; break;
  default:
    if ((streaminfo->sample_rate <= 255000) &&
	((streaminfo->sample_rate % 1000) == 0))
      sample_rate_bits = 0xC;
    else if ((streaminfo->sample_rate <= 655350) &&
	     ((streaminfo->sample_rate % 10) == 0))
      sample_rate_bits = 0xE;
    else if (streaminfo->sample_rate <= 0xFFFF)
      sample_rate_bits = 0xF;
    else
      sample_rate_bits = 0x0;
    break;
  }

  /*determine bits-per-sample bits from streaminfo*/
  switch (streaminfo->bits_per_sample) {
  case 8:  bits_per_sample_bits = 0x1; break;
  case 12: bits_per_sample_bits = 0x2; break;
  case 16: bits_per_sample_bits = 0x4; break;
  case 20: bits_per_sample_bits = 0x5; break;
  case 24: bits_per_sample_bits = 0x6; break;
  default: bits_per_sample_bits = 0x0; break;
  }

  /*once the four bits-encoded fields are set, write the actual header*/
  bs->write_bits(bs, 14, 0x3FFE);              /*sync code*/
  bs->write_bits(bs, 1, 0);                    /*reserved*/
  bs->write_bits(bs, 1, 0);                    /*blocking strategy*/
  bs->write_bits(bs, 4, block_size_bits);      /*block size*/
  bs->write_bits(bs, 4, sample_rate_bits);     /*sample rate*/
  bs->write_bits(bs, 4, channel_assignment);   /*channel assignment*/
  bs->write_bits(bs, 3, bits_per_sample_bits); /*bits per sample*/
  bs->write_bits(bs, 1, 0);                    /*padding*/

  /*frame number is taken from total_frames in streaminfo*/
  write_utf8(bs, streaminfo->total_frames);

  /*if block_size_bits are 0x6 or 0x7, write a PCM frames field*/
  if (block_size_bits == 0x6)
    bs->write_bits(bs, 8, block_size - 1);
  else if (block_size_bits == 0x7)
    bs->write_bits(bs, 16, block_size - 1);

  /*if sample rate is unusual, write one of the three sample rate fields*/
  if (sample_rate_bits == 0xC)
    bs->write_bits(bs, 8, streaminfo->sample_rate / 1000);
  else if (sample_rate_bits == 0xD)
    bs->write_bits(bs, 16, streaminfo->sample_rate);
  else if (sample_rate_bits == 0xE)
    bs->write_bits(bs, 16, streaminfo->sample_rate / 10);

  /*write CRC-8*/
  bs->write_bits(bs, 8, streaminfo->crc8);
}

void FlacEncoder_write_subframe(Bitstream *bs,
				struct flac_encoding_options *options,
				int bits_per_sample,
				struct i_array *samples) {
  uint32_t i;
  int32_t first_sample;

  if (samples->size < 2) {
    FlacEncoder_write_constant_subframe(bs,
					bits_per_sample,
					ia_getitem(samples,0));
    return;
  }

  /*check for a constant subframe*/
  first_sample = ia_getitem(samples,0);
  for (i = 1; i < samples->size; i++) {
    if (ia_getitem(samples,i) != first_sample)
      break;
  }
  if (i == samples->size) {
    FlacEncoder_write_constant_subframe(bs,
					bits_per_sample,
					first_sample);
    return;
  }

  /*otherwise, write verbatim subframe - FIXME*/
  FlacEncoder_write_verbatim_subframe(bs,
				      bits_per_sample,
				      samples);

  /* fixed_subframe = bbw_open(samples->size); */
  /* FlacEncoder_write_fixed_subframe(fixed_subframe, */
  /* 				   options, */
  /* 				   bits_per_sample, */
  /* 				   samples, */
  /* 				   FlacEncoder_compute_best_fixed_predictor_order(samples)); */

  /* lpc_subframe = bbw_open(samples->size); */
  /* ia_init(&lpc_coeffs,1); */
  /* FlacEncoder_compute_best_lpc_coeffs(options,bits_per_sample, */
  /* 				      samples, */
  /* 				      &lpc_coeffs, */
  /* 				      &lpc_shift_needed); */

  /* FlacEncoder_write_lpc_subframe(lpc_subframe, */
  /* 				 options, */
  /* 				 bits_per_sample, */
  /* 				 samples, */
  /* 				 &lpc_coeffs, */
  /* 				 lpc_shift_needed); */

  /* ia_free(&lpc_coeffs); */

  /* if (fixed_subframe->bits_written <= lpc_subframe->bits_written) */
  /*   bbw_append(bbw,fixed_subframe); */
  /* else */
  /*   bbw_append(bbw,lpc_subframe); */

  /* bbw_close(fixed_subframe); */
  /* bbw_close(lpc_subframe); */
}

void FlacEncoder_write_constant_subframe(Bitstream *bs,
					 int bits_per_sample,
					 int32_t sample) {
  /*write subframe header*/
  bs->write_bits(bs, 1, 0);
  bs->write_bits(bs, 6, 0);
  bs->write_bits(bs, 1, 0);

  /*write subframe sample*/
  bs->write_signed_bits(bs, bits_per_sample, sample);
}

void FlacEncoder_write_verbatim_subframe(Bitstream *bs,
					 int bits_per_sample,
					 struct i_array *samples) {
  uint32_t i;

  /*write subframe header*/
  bs->write_bits(bs, 1, 0);
  bs->write_bits(bs, 6, 1);
  bs->write_bits(bs, 1, 0);

  /*write subframe samples*/
  for (i = 0; i < samples->size; i++) {
    bs->write_signed_bits(bs, bits_per_sample, ia_getitem(samples,i));
  }
}

/* void FlacEncoder_write_fixed_subframe(BitbufferW *bbw, */
/* 				      struct flac_encoding_options *options, */
/* 				      int bits_per_sample, */
/* 				      struct i_array *samples, */
/* 				      int predictor_order) { */
/*   uint32_t i; */
/*   struct i_array residual; */

/*   /\*write subframe header*\/ */
/*   bbw_write_bits(bbw, 1, 0); */
/*   bbw_write_bits(bbw, 6, 0x8 | predictor_order); */
/*   bbw_write_bits(bbw, 1, 0); /\*FIXME - handle wasted bits-per-sample*\/ */

/*   /\*write warm-up samples*\/ */
/*   for (i = 0; i < predictor_order; i++) */
/*     bbw_write_signed_bits(bbw, bits_per_sample, ia_getitem(samples,i)); */

/*   /\*calculate residual values based on predictor order*\/ */
/*   ia_init(&residual,samples->size); */
/*   switch (predictor_order) { */
/*   case 0: */
/*     for (i = 0; i < samples->size; i++) */
/*       ia_append(&residual,ia_getitem(samples,i)); */
/*     break; */
/*   case 1: */
/*     for (i = 1; i < samples->size; i++) */
/*       ia_append(&residual,ia_getitem(samples,i) - ia_getitem(samples,i - 1)); */
/*     break; */
/*   case 2: */
/*     for (i = 2; i < samples->size; i++) */
/*       ia_append(&residual,ia_getitem(samples,i) - */
/* 		((2 * ia_getitem(samples,i - 1)) - ia_getitem(samples,i - 2))); */
/*     break; */
/*   case 3: */
/*     for (i = 3; i < samples->size; i++) */
/*       ia_append(&residual,ia_getitem(samples,i) - */
/* 		((3 * ia_getitem(samples,i - 1)) - */
/* 		 (3 * ia_getitem(samples,i - 2)) + */
/* 		 ia_getitem(samples,i - 3))); */
/*     break; */
/*   case 4: */
/*     for (i = 4; i < samples->size; i++) */
/*       ia_append(&residual,ia_getitem(samples,i) - */
/* 		((4 * ia_getitem(samples,i - 1)) - */
/* 		 (6 * ia_getitem(samples,i - 2)) + */
/* 		 (4 * ia_getitem(samples,i - 3)) - */
/* 		 ia_getitem(samples,i - 4))); */
/*     break; */
/*   } */

/*   /\*write residual*\/ */
/*   FlacEncoder_write_best_residual(bbw, options, predictor_order, &residual); */
/*   ia_free(&residual); */
/* } */

/* void FlacEncoder_write_lpc_subframe(BitbufferW *bbw, */
/* 				    struct flac_encoding_options *options, */
/* 				    int bits_per_sample, */
/* 				    struct i_array *samples, */
/* 				    struct i_array *coeffs, */
/* 				    int shift_needed) { */
/*   int predictor_order = coeffs->size; */
/*   int qlp_precision = ia_reduce(coeffs,2,maximum_bits_size); */
/*   struct i_array residual; */
/*   int64_t accumulator; */
/*   int i,j; */

/*   uint32_t samples_size; */
/*   int32_t *samples_data; */
/*   int32_t *coeffs_data; */

/*   /\*write subframe header*\/ */
/*   bbw_write_bits(bbw,1,0); */
/*   bbw_write_bits(bbw,6,0x20 | (predictor_order - 1)); */
/*   bbw_write_bits(bbw,1,0);  /\*FIXME - handle wasted bits-per-sample*\/ */

/*   /\*write warm-up samples*\/ */
/*   for (i = 0; i < predictor_order; i++) { */
/*     bbw_write_signed_bits(bbw,bits_per_sample,ia_getitem(samples,i)); */
/*   } */

/*   /\*write QLP Precision*\/ */
/*   bbw_write_bits(bbw,4,qlp_precision - 1); */

/*   /\*write QLP Shift Needed*\/ */
/*   bbw_write_bits(bbw,5,shift_needed); */

/*   /\*write QLP Coefficients*\/ */
/*   for (i = 0; i < predictor_order; i++) { */
/*     bbw_write_signed_bits(bbw,qlp_precision,ia_getitem(coeffs,i)); */
/*   } */

/*   /\*calculate residual values*\/ */
/*   samples_size = samples->size; */
/*   samples_data = samples->data; */
/*   coeffs_data = coeffs->data; */

/*   ia_init(&residual,samples_size); */
/*   for (i = predictor_order; i < samples_size; i++) { */
/*     accumulator = 0; */
/*     for (j = 0; j < predictor_order; j++) { */
/*       accumulator += (int64_t)samples_data[i - j - 1] * (int64_t)coeffs_data[j]; */
/*     } */
/*     ia_append(&residual, */
/* 	      samples_data[i] - (int32_t)(accumulator >> shift_needed)); */
/*   } */

/*   /\*write residual*\/ */
/*   FlacEncoder_write_best_residual(bbw, options, predictor_order, &residual); */

/*   ia_free(&residual); */
/* } */

/* void FlacEncoder_write_best_residual(BitbufferW *bbw, */
/* 				     struct flac_encoding_options *options, */
/* 				     int predictor_order, */
/* 				     struct i_array *residuals) { */
/*   struct i_array rice_parameters; */
/*   int block_size; */
/*   int min_partition_order; */
/*   int max_partition_order; */
/*   int partition_order; */
/*   BitbufferW *current_best; */
/*   BitbufferW *potential_residual; */
/*   struct i_array remaining_residuals; */
/*   struct i_array partition_residuals; */
/*   uint32_t partitions; */
/*   uint32_t partition; */

/*   /\*keep dividing block_size by 2 until its no longer divisible by 2 */
/*     to determine the maximum partition order */
/*     since there are 2 ^ partition_order number of partitions */
/*     and the residuals must be evenly distributed between them*\/ */
/*   for (block_size = predictor_order + residuals->size,max_partition_order = 0; */
/*        (block_size > 1) && ((block_size % 2) == 0); */
/*        max_partition_order++) */
/*     block_size /= 2; */

/*   /\*although if the user-specified max_partition_order is smaller, */
/*     use that instead*\/ */
/*   max_partition_order = MIN(options->max_residual_partition_order, */
/* 			    max_partition_order); */

/*   min_partition_order = MIN(options->min_residual_partition_order, */
/* 			    max_partition_order); */

/*   block_size = predictor_order + residuals->size; */

/*   /\*initialize working space to try different residual sizes*\/ */
/*   current_best = NULL; */
/*   potential_residual = bbw_open(residuals->size); */
/*   ia_init(&rice_parameters,1 << max_partition_order); */

/*   /\*for each partition_order possibility*\/ */
/*   for (partition_order = min_partition_order; */
/*        partition_order <= max_partition_order; */
/*        partition_order++) { */
/*     ia_reset(&rice_parameters); */

/*     /\*chop the residuals into 2 ^ partition_order number of partitions*\/ */
/*     ia_link(&remaining_residuals,residuals); */
/*     partitions = 1 << partition_order; */
/*     for (partition = 0; partition < partitions; partition++) { */
/*       if (partition == 0) { */
/* 	/\*first partition contains (block_size / 2 ^ partition_order) - order */
/* 	  number of residuals*\/ */

/* 	ia_split(&partition_residuals, */
/* 		 &remaining_residuals, */
/* 		 &remaining_residuals, */
/* 		 (block_size / (1 << partition_order)) - predictor_order); */
/*       } else { */
/* 	/\*subsequence partitions contain (block_size / 2 ^ partition_order) */
/* 	  number of residuals*\/ */

/* 	ia_split(&partition_residuals, */
/* 		 &remaining_residuals, */
/* 		 &remaining_residuals, */
/* 		 block_size / (1 << partition_order)); */
/*       } */

/*       /\*for each partition, determine the Rice parameter*\/ */
/*       /\*and append that parameter to the parameter list*\/ */
/*       ia_append(&rice_parameters, */
/* 		FlacEncoder_compute_best_rice_parameter(&partition_residuals)); */
/*     } */

/*     /\*once the parameter list is set, */
/*       write a complete residual block to potential_residual*\/ */
/*     FlacEncoder_write_residual(potential_residual, */
/* 			       predictor_order, */
/* 			       0, /\*FIXME - make coding method dynamic?*\/ */
/* 			       &rice_parameters, */
/* 			       residuals); */

/*     /\*and if potential_residual is better than current_best (or no current_best) */
/*       swap current_best for potential_residual*\/ */
/*     if (current_best == NULL) { */
/*       current_best = potential_residual; */
/*       potential_residual = bbw_open(residuals->size); */
/*     } else if (potential_residual->bits_written < current_best->bits_written) { */
/*       bbw_swap(current_best,potential_residual); */
/*       bbw_reset(potential_residual); */
/*     } else { */
/*     /\*otherwise, dump potential_residual*\/ */
/*       bbw_reset(potential_residual); */
/*     } */
/*   } */

/*   /\*finally, send the best possible residual to the bitbuffer*\/ */
/*   bbw_append(bbw,current_best); */
/*   bbw_close(current_best); */
/*   bbw_close(potential_residual); */
/*   ia_free(&rice_parameters); */
/* } */

/* int FlacEncoder_compute_best_rice_parameter(struct i_array *residuals) { */
/*   uint64_t sum = 0; */
/*   int i; */

/*   for (i = 0; i < residuals->size; i++) */
/*     sum += abs(ia_getitem(residuals,i)); */

/*   for (i = 0; (residuals->size * (1 << i)) < sum; i++) */
/*     /\*do nothing*\/; */

/*   return i; */
/* } */


/* void FlacEncoder_write_residual(BitbufferW *bbw, */
/* 				int predictor_order, */
/* 				int coding_method, */
/* 				struct i_array *rice_parameters, */
/* 				struct i_array *residuals) { */
/*   uint32_t partition_order; */
/*   int32_t partitions = rice_parameters->size; */
/*   int32_t partition; */
/*   int32_t block_size = predictor_order + residuals->size; */
/*   struct i_array remaining_residuals; */
/*   struct i_array partition_residuals; */

/*   /\*derive the partition_order value*\/ */
/*   for (partition_order = 0; partitions > 1; partition_order++) */
/*     partitions /= 2; */
/*   partitions = rice_parameters->size; */

/*   bbw_write_bits(bbw, 2, coding_method); */
/*   bbw_write_bits(bbw, 4, partition_order); */

/*   /\*for each rice_parameter, write a residual partition*\/ */
/*   ia_link(&remaining_residuals,residuals); */

/*   for (partition = 0; partition < partitions; partition++) { */
/*     if (partition == 0) { */
/*       /\*the first partition contains (block_size / 2 ^ partition_order) - order */
/* 	number of residuals*\/ */
/*       ia_split(&partition_residuals, */
/* 	       &remaining_residuals, */
/* 	       &remaining_residuals, */
/* 	       (block_size / (1 << partition_order)) - predictor_order); */
/*     } else { */
/*       /\*subsequence partitions contain (block_size / 2 ^ partition_order) */
/* 	number of residuals*\/ */
/*       ia_split(&partition_residuals, */
/* 	       &remaining_residuals, */
/* 	       &remaining_residuals, */
/* 	       block_size / (1 << partition_order)); */
/*     } */
/*     FlacEncoder_write_residual_partition(bbw, */
/* 					 coding_method, */
/* 					 ia_getitem(rice_parameters, */
/* 						    partition), */
/* 					 &partition_residuals); */
/*   } */

/* } */

/* void FlacEncoder_write_residual_partition(BitbufferW *bbw, */
/* 					  int coding_method, */
/* 					  int rice_parameter, */
/* 					  struct i_array *residuals) { */
/*   uint32_t i; */
/*   int32_t residual; */
/*   int32_t msb; */
/*   int32_t lsb; */

/*   uint32_t residuals_size; */
/*   int32_t *residuals_data; */

/*   residuals_size = residuals->size; */
/*   residuals_data = residuals->data; */

/*   /\*write the 4-5 bit Rice parameter header (depending on coding method)*\/ */
/*   bbw_write_bits(bbw, coding_method == 0 ? 4 : 5, rice_parameter); */

/*   /\*for each residual, write a unary/unsigned bits pair */
/*     whose breakpoint depends on "rice_parameter"*\/ */
/*   for (i = 0; i < residuals_size; i++) { */
/*     residual = residuals_data[i]; */
/*     if (residual >= 0) { */
/*       residual <<= 1; */
/*     } else { */
/*       residual = ((-residual - 1) << 1) | 1; */
/*     } */
/*     msb = residual >> rice_parameter; */
/*     lsb = residual - (msb << rice_parameter); */
/*     bbw_write_unary(bbw,1,msb); */
/*     bbw_write_bits(bbw,rice_parameter,lsb); */
/*   } */
/* } */

/* int FlacEncoder_compute_best_fixed_predictor_order(struct i_array *samples) { */
/*   struct i_array delta0; */
/*   struct i_array delta1; */
/*   struct i_array delta2; */
/*   struct i_array delta3; */
/*   struct i_array delta4; */
/*   struct i_array subtract; */
/*   uint64_t delta0_sum; */
/*   uint64_t delta1_sum; */
/*   uint64_t delta2_sum; */
/*   uint64_t delta3_sum; */
/*   uint64_t delta4_sum; */
/*   uint32_t i; */

/*   if (samples->size < 5) */
/*     return 0; */

/*   delta0.data = NULL; /\*to elimate a "used without being defined" warning*\/ */
/*   ia_tail(&delta0,samples,samples->size - 1); */
/*   for (delta0_sum = 0,i = 3; i < delta0.size; i++) */
/*     delta0_sum += abs(ia_getitem(&delta0,i)); */

/*   ia_init(&delta1,samples->size); */
/*   ia_tail(&subtract,&delta0,delta0.size - 1); */
/*   ia_sub(&delta1,&delta0,&subtract); */
/*   for (delta1_sum = 0,i = 2; i < delta1.size; i++) */
/*     delta1_sum += abs(ia_getitem(&delta1,i)); */

/*   ia_init(&delta2,samples->size); */
/*   ia_tail(&subtract,&delta1,delta1.size - 1); */
/*   ia_sub(&delta2,&delta1,&subtract); */
/*   for (delta2_sum = 0,i = 2; i < delta2.size; i++) */
/*     delta2_sum += abs(ia_getitem(&delta2,i)); */

/*   ia_init(&delta3,samples->size); */
/*   ia_tail(&subtract,&delta2,delta2.size - 1); */
/*   ia_sub(&delta3,&delta2,&subtract); */
/*   for (delta3_sum = 0,i = 1; i < delta3.size; i++) */
/*     delta3_sum += abs(ia_getitem(&delta3,i)); */

/*   ia_init(&delta4,samples->size); */
/*   ia_tail(&subtract,&delta3,delta3.size - 1); */
/*   ia_sub(&delta4,&delta3,&subtract); */
/*   for (delta4_sum = 0,i = 0; i < delta4.size; i++) */
/*     delta4_sum += abs(ia_getitem(&delta4,i)); */

/*   ia_free(&delta1); */
/*   ia_free(&delta2); */
/*   ia_free(&delta3); */
/*   ia_free(&delta4); */

/*   if (delta0_sum < MIN(delta1_sum,MIN(delta2_sum,MIN(delta3_sum,delta4_sum)))) */
/*     return 0; */
/*   else if (delta1_sum < MIN(delta2_sum,MIN(delta3_sum,delta4_sum))) */
/*     return 1; */
/*   else if (delta2_sum < MIN(delta3_sum,delta4_sum)) */
/*     return 2; */
/*   else if (delta3_sum < delta4_sum) */
/*     return 3; */
/*   else */
/*     return 4; */
/* } */

void write_utf8(Bitstream *stream, unsigned int value) {
  if ((value >= 0) && (value <= 0x7F)) {
    /*1 byte UTF-8 sequence*/
    stream->write_bits(stream,8,value);
  } else if ((value >= 0x80) && (value <= 0x7FF)) {
    /*2 byte UTF-8 sequence*/
    stream->write_unary(stream,0,2);
    stream->write_bits(stream,5,value >> 6);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,value & 0x3F);
  } else if ((value >= 0x800) && (value <= 0xFFFF)) {
    /*3 byte UTF-8 sequence*/
    stream->write_unary(stream,0,3);
    stream->write_bits(stream,4,value >> 12);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,(value >> 6) & 0x3F);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,value & 0x3F);
  } else if ((value >= 0x10000) && (value <= 0xFFFFF)) {
    /*4 byte UTF-8 sequence*/
    stream->write_unary(stream,0,4);
    stream->write_bits(stream,3,value >> 18);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,(value >> 12) & 0x3F);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,(value >> 6) & 0x3F);
    stream->write_unary(stream,0,1);
    stream->write_bits(stream,6,value & 0x3F);
  }
}

int FlacEncoder_qlp_coeff_precision(int block_size) {
  if (block_size <= 192)
    return 7;
  else if (block_size <= 384)
    return 8;
  else if (block_size <= 576)
    return 9;
  else if (block_size <= 1152)
    return 10;
  else if (block_size <= 2304)
    return 11;
  else if (block_size <= 4608)
    return 12;
  else
    return 13;
}

void md5_update(void *data, unsigned char *buffer, unsigned long len) {
  MD5_Update((MD5_CTX*)data, (const void*)buffer, len);
}

int maximum_bits_size(int value, int current_maximum) {
  int bits = 1;
  if (value < 0) {
    value = -value;
  }
  for (;value > 0; value >>= 1)
    bits++;

  if (bits > current_maximum)
    return bits;
  else
    return current_maximum;
}

#include "flac_crc.c"

#ifdef STANDALONE

int main(int argc, char *argv[]) {
  encoders_encode_flac(argv[1],
		       stdin,
		       4096,12,0,6);

  return 0;
}
#endif

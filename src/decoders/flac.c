#include "flac.h"
#include "../pcm.h"

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

int FlacDecoder_init(decoders_FlacDecoder *self,
		     PyObject *args, PyObject *kwds) {
  char* filename;
  int i;

  self->filename = NULL;
  self->file = NULL;
  self->bitstream = NULL;

  if (!PyArg_ParseTuple(args, "si", &filename, &(self->channel_mask)))
    return -1;

  /*open the flac file*/
  self->file = fopen(filename,"rb");
  if (self->file == NULL) {
    PyErr_SetFromErrnoWithFilename(PyExc_IOError,filename);
    return -1;
  } else {
    self->bitstream = bs_open(self->file);
  }

  self->filename = strdup(filename);

  /*read the STREAMINFO block and setup the total number of samples to read*/
  if (FlacDecoder_read_metadata(self) == ERROR) {
    return -1;
  }

  self->remaining_samples = self->streaminfo.total_samples;

  /*add callbacks for CRC8 and CRC16 calculation*/
  bs_add_callback(self->bitstream,flac_crc8,&(self->crc8));
  bs_add_callback(self->bitstream,flac_crc16,&(self->crc16));

  /*setup a bunch of temporary buffers*/
  for (i = 0; i < self->streaminfo.channels; i++) {
    ia_init(&(self->subframe_data[i]),
	    self->streaminfo.maximum_block_size);
  }
  ia_init(&(self->residuals),self->streaminfo.maximum_block_size);
  ia_init(&(self->qlp_coeffs),1);
  self->data = NULL;
  self->data_size = 0;

  return 0;
}

PyObject *FLACDecoder_close(decoders_FlacDecoder* self,
			    PyObject *args) {
  self->remaining_samples = 0;
  Py_INCREF(Py_None);
  return Py_None;
}

void FlacDecoder_dealloc(decoders_FlacDecoder *self) {
  int i;

  for (i = 0; i < self->streaminfo.channels; i++) {
    ia_free(&(self->subframe_data[i]));
  }
  ia_free(&(self->residuals));
  ia_free(&(self->qlp_coeffs));

  if (self->filename != NULL)
    free(self->filename);

  bs_close(self->bitstream);
  if (self->data != NULL)
    free(self->data);

  Py_TYPE(self)->tp_free((PyObject*)self);
}

PyObject *FlacDecoder_new(PyTypeObject *type,
			  PyObject *args, PyObject *kwds) {
  decoders_FlacDecoder *self;

  self = (decoders_FlacDecoder *)type->tp_alloc(type, 0);

  return (PyObject *)self;
}

status FlacDecoder_read_metadata(decoders_FlacDecoder *self) {
  unsigned int last_block;
  unsigned int block_type;
  unsigned int block_length;

  if (read_bits(self->bitstream,32) != 0x664C6143u) {
    PyErr_SetString(PyExc_ValueError,"not a FLAC file");
    return ERROR;
  }

  last_block = read_bits(self->bitstream,1);
  block_type = read_bits(self->bitstream,7);
  block_length = read_bits(self->bitstream,24);

  if (block_type == 0) {
    self->streaminfo.minimum_block_size = read_bits(self->bitstream,16);
    self->streaminfo.maximum_block_size = read_bits(self->bitstream,16);
    self->streaminfo.minimum_frame_size = read_bits(self->bitstream,24);
    self->streaminfo.maximum_frame_size = read_bits(self->bitstream,24);
    self->streaminfo.sample_rate = read_bits(self->bitstream,20);
    self->streaminfo.channels = read_bits(self->bitstream,3) + 1;
    self->streaminfo.bits_per_sample = read_bits(self->bitstream,5) + 1;
    self->streaminfo.total_samples = read_bits64(self->bitstream,36);
    if (fread(self->streaminfo.md5sum,sizeof(unsigned char),16,self->file)
	!= 16) {
      PyErr_SetString(PyExc_ValueError,"unable to read md5sum");
      return ERROR;
    }
  } else {
    PyErr_SetString(PyExc_ValueError,"STREAMINFO not first metadata block");
    return ERROR;
  }

  while (!last_block) {
    last_block = read_bits(self->bitstream,1);
    block_type = read_bits(self->bitstream,7);
    block_length = read_bits(self->bitstream,24);
    fseek(self->file,block_length,SEEK_CUR);
  }

  return OK;
}

static PyObject *FlacDecoder_sample_rate(decoders_FlacDecoder *self,
					 void *closure) {
  return Py_BuildValue("i",self->streaminfo.sample_rate);
}

static PyObject *FlacDecoder_bits_per_sample(decoders_FlacDecoder *self,
					     void *closure) {
  return Py_BuildValue("i",self->streaminfo.bits_per_sample);
}

static PyObject *FlacDecoder_channels(decoders_FlacDecoder *self,
				      void *closure) {
  return Py_BuildValue("i",self->streaminfo.channels);
}

static PyObject *FlacDecoder_channel_mask(decoders_FlacDecoder *self,
					  void *closure) {
  return Py_BuildValue("i",self->channel_mask);
}

PyObject *FLACDecoder_read(decoders_FlacDecoder* self,
			   PyObject *args) {
  int bytes;
  struct flac_frame_header frame_header;
  int channel;
  int data_size;

  PyObject *pcm = NULL;
  pcm_FrameList *framelist;
  struct i_array channel_data;

  int32_t i,j;
  int64_t mid;
  int32_t side;

  if (!PyArg_ParseTuple(args, "i", &bytes))
    return NULL;
  if (bytes < 0) {
    PyErr_SetString(PyExc_ValueError,"number of bytes must be positive");
    return NULL;
  }

  /*if all samples have been read, return an empty FrameList*/
  if (self->remaining_samples < 1) {
    if ((pcm = PyImport_ImportModule("audiotools.pcm")) == NULL)
      goto error;
    framelist = (pcm_FrameList*)PyObject_CallMethod(pcm,"__blank__",NULL);
    framelist->channels = self->streaminfo.channels;
    framelist->bits_per_sample = self->streaminfo.bits_per_sample;
    Py_DECREF(pcm);
    return (PyObject*)framelist;
  }

  self->crc8 = self->crc16 = 0;

  if (FlacDecoder_read_frame_header(self,&frame_header) == ERROR) {
    Py_DECREF(pcm);
    return NULL;
  }

  data_size = frame_header.block_size * frame_header.bits_per_sample *
    frame_header.channel_count / 8;
  if (data_size > self->data_size) {
    self->data = realloc(self->data,data_size);
    self->data_size = data_size;
  }

  for (channel = 0; channel < frame_header.channel_count; channel++) {
    if (((frame_header.channel_assignment == 0x8) &&
	 (channel == 1)) ||
	((frame_header.channel_assignment == 0x9) &&
	 (channel == 0)) ||
	((frame_header.channel_assignment == 0xA) &&
	 (channel == 1))) {
      if (FlacDecoder_read_subframe(self,
				    frame_header.block_size,
				    frame_header.bits_per_sample + 1,
				    &(self->subframe_data[channel])) == ERROR)
	goto error;
    } else {
      if (FlacDecoder_read_subframe(self,
				    frame_header.block_size,
				    frame_header.bits_per_sample,
				    &(self->subframe_data[channel])) == ERROR)
	goto error;
    }
  }

  /*handle difference channels, if any*/
  switch (frame_header.channel_assignment) {
  case 0x8:
    /*left-difference*/
    ia_sub(&(self->subframe_data[1]),
	   &(self->subframe_data[0]),&(self->subframe_data[1]));
    break;
  case 0x9:
    /*difference-right*/
    ia_add(&(self->subframe_data[0]),
	   &(self->subframe_data[0]),&(self->subframe_data[1]));
    break;
  case 0xA:
    /*mid-side*/
    for (i = 0; i < frame_header.block_size; i++) {
      mid = ia_getitem(&(self->subframe_data[0]),i);
      side = ia_getitem(&(self->subframe_data[1]),i);
      mid = (mid << 1) | (side & 1);
      ia_setitem(&(self->subframe_data[0]),i,(mid + side) >> 1);
      ia_setitem(&(self->subframe_data[1]),i,(mid - side) >> 1);
    }
    break;
  default:
    /*do nothing for independent channels*/
    break;
  }

  /*check CRC-16*/
  byte_align_r(self->bitstream);
  read_bits(self->bitstream,16);
  if (self->crc16 != 0) {
    PyErr_SetString(PyExc_ValueError,"invalid checksum in frame");
    goto error;
  }

  /*transform subframe data into a pcm_FrameList object*/
  if ((pcm = PyImport_ImportModuleNoBlock("audiotools.pcm")) == NULL)
    goto error;
  framelist = (pcm_FrameList*)PyObject_CallMethod(pcm,"__blank__",NULL);
  Py_DECREF(pcm);

  framelist->frames = frame_header.block_size;
  framelist->channels = frame_header.channel_count;
  framelist->bits_per_sample = frame_header.bits_per_sample;
  framelist->samples_length = framelist->frames * framelist->channels;
  framelist->samples = realloc(framelist->samples,
			       sizeof(ia_data_t) * framelist->samples_length);

  for (channel = 0; channel < frame_header.channel_count; channel++) {
    channel_data = self->subframe_data[channel];
    for (i = channel,j = 0; j < channel_data.size;
  	 i += frame_header.channel_count,j++)
      framelist->samples[i] = ia_getitem(&channel_data,j);
  }

  /*decrement remaining samples*/
  self->remaining_samples -= frame_header.block_size;

  /*return pcm_FrameList*/
  return (PyObject*)framelist;
 error:
  Py_XDECREF(pcm);
  return NULL;
}

PyObject *FLACDecoder_seekpoints(decoders_FlacDecoder* self,
				 PyObject *args) {
  PyObject *seekpoints = PyList_New(0);
  struct flac_frame_header frame_header;
  int data_size;
  int channel;
  int sample_number = 0;
  long frame_offset;

  while (self->remaining_samples > 0) {
    self->crc8 = self->crc16 = 0;

    frame_offset = ftell(self->file);
    if (FlacDecoder_read_frame_header(self,&frame_header) == ERROR)
      goto error;

    if (PyList_Append(seekpoints,Py_BuildValue("(i,i,i)",
					       sample_number,
					       frame_offset,
					       frame_header.block_size)) == -1)
      goto error;

    sample_number += frame_header.block_size;

    data_size = frame_header.block_size * frame_header.bits_per_sample *
      frame_header.channel_count / 8;

    for (channel = 0; channel < frame_header.channel_count; channel++) {
      if (((frame_header.channel_assignment == 0x8) &&
	   (channel == 1)) ||
	  ((frame_header.channel_assignment == 0x9) &&
	   (channel == 0)) ||
	  ((frame_header.channel_assignment == 0xA) &&
	   (channel == 1))) {
	if (FlacDecoder_skip_subframe(self,
				      frame_header.block_size,
				      frame_header.bits_per_sample + 1) == ERROR)
	  goto error;
      } else {
	if (FlacDecoder_skip_subframe(self,
				      frame_header.block_size,
				      frame_header.bits_per_sample) == ERROR)
	  goto error;
      }
    }

    /*check CRC-16*/
    byte_align_r(self->bitstream);
    read_bits(self->bitstream,16);
    if (self->crc16 != 0) {
      PyErr_SetString(PyExc_ValueError,"invalid checksum in frame");
      goto error;
    }

    /*decrement remaining samples*/
    self->remaining_samples -= frame_header.block_size;
  }

  return seekpoints;
 error:
  Py_DECREF(seekpoints);
  return NULL;
}

status FlacDecoder_read_frame_header(decoders_FlacDecoder *self,
				     struct flac_frame_header *header) {
  Bitstream *bitstream = self->bitstream;
  uint32_t block_size_bits;
  uint32_t sample_rate_bits;

  /*read and verify sync code*/
  if (read_bits(bitstream,14) != 0x3FFE) {
    PyErr_SetString(PyExc_ValueError,"invalid sync code");
    return ERROR;
  }

  /*read and verify reserved bit*/
  if (read_bits(bitstream,1) != 0) {
    PyErr_SetString(PyExc_ValueError,"invalid reserved bit");
    return ERROR;
  }

  header->blocking_strategy = read_bits(bitstream,1);

  block_size_bits = read_bits(bitstream,4);
  sample_rate_bits = read_bits(bitstream,4);
  header->channel_assignment = read_bits(bitstream,4);
  switch (header->channel_assignment) {
  case 0x8:
  case 0x9:
  case 0xA:
    header->channel_count = 2;
    break;
  default:
    header->channel_count = header->channel_assignment + 1;
    break;
  }

  switch (read_bits(bitstream,3)) {
  case 0:
    header->bits_per_sample = self->streaminfo.bits_per_sample; break;
  case 1:
    header->bits_per_sample = 8; break;
  case 2:
    header->bits_per_sample = 12; break;
  case 4:
    header->bits_per_sample = 16; break;
  case 5:
    header->bits_per_sample = 20; break;
  case 6:
    header->bits_per_sample = 24; break;
  default:
    PyErr_SetString(PyExc_ValueError,"invalid bits per sample");
    return ERROR;
  }
  read_bits(bitstream,1); /*padding*/

  header->frame_number = read_utf8(bitstream);

  switch (block_size_bits) {
  case 0x0: header->block_size = self->streaminfo.maximum_block_size; break;
  case 0x1: header->block_size = 192; break;
  case 0x2: header->block_size = 576; break;
  case 0x3: header->block_size = 1152; break;
  case 0x4: header->block_size = 2304; break;
  case 0x5: header->block_size = 4608; break;
  case 0x6: header->block_size = read_bits(bitstream,8) + 1; break;
  case 0x7: header->block_size = read_bits(bitstream,16) + 1; break;
  case 0x8: header->block_size = 256; break;
  case 0x9: header->block_size = 512; break;
  case 0xA: header->block_size = 1024; break;
  case 0xB: header->block_size = 2048; break;
  case 0xC: header->block_size = 4096; break;
  case 0xD: header->block_size = 8192; break;
  case 0xE: header->block_size = 16384; break;
  case 0xF: header->block_size = 32768; break;
  }

  switch (sample_rate_bits) {
  case 0x0: header->sample_rate = self->streaminfo.sample_rate; break;
  case 0x1: header->sample_rate = 88200; break;
  case 0x2: header->sample_rate = 176400; break;
  case 0x3: header->sample_rate = 192000; break;
  case 0x4: header->sample_rate = 8000; break;
  case 0x5: header->sample_rate = 16000; break;
  case 0x6: header->sample_rate = 22050; break;
  case 0x7: header->sample_rate = 24000; break;
  case 0x8: header->sample_rate = 32000; break;
  case 0x9: header->sample_rate = 44100; break;
  case 0xA: header->sample_rate = 48000; break;
  case 0xB: header->sample_rate = 96000; break;
  case 0xC: header->sample_rate = read_bits(bitstream,8) * 1000; break;
  case 0xD: header->sample_rate = read_bits(bitstream,16); break;
  case 0xE: header->sample_rate = read_bits(bitstream,16) * 10; break;
  case 0xF:
    PyErr_SetString(PyExc_ValueError,"invalid sample rate");
    return ERROR;
  }

  /*check for valid CRC-8 value*/
  read_bits(bitstream,8);
  if (self->crc8 != 0) {
    PyErr_SetString(PyExc_ValueError,"invalid checksum in frame header");
    return ERROR;
  }

  return OK;
}

status FlacDecoder_read_subframe(decoders_FlacDecoder *self,
				 uint32_t block_size,
				 uint8_t bits_per_sample,
				 struct i_array *samples) {
  struct flac_subframe_header subframe_header;
  uint32_t i;

  if (FlacDecoder_read_subframe_header(self,&subframe_header) == ERROR)
    return ERROR;

  /*account for wasted bits-per-sample*/
  if (subframe_header.wasted_bits_per_sample > 0)
    bits_per_sample -= subframe_header.wasted_bits_per_sample;

  switch (subframe_header.type) {
  case FLAC_SUBFRAME_CONSTANT:
    if (FlacDecoder_read_constant_subframe(self, block_size, bits_per_sample,
					   samples) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_VERBATIM:
    if (FlacDecoder_read_verbatim_subframe(self, block_size, bits_per_sample,
					   samples) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_FIXED:
    if (FlacDecoder_read_fixed_subframe(self, subframe_header.order,
					block_size, bits_per_sample,
					samples) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_LPC:
    if (FlacDecoder_read_lpc_subframe(self, subframe_header.order,
				      block_size, bits_per_sample,
				      samples) == ERROR)
      return ERROR;
    break;
  }

  /*reinsert wasted bits-per-sample, if necessary*/
  if (subframe_header.wasted_bits_per_sample > 0)
    for (i = 0; i < block_size; i++)
      ia_setitem(samples,i,ia_getitem(samples,i) << subframe_header.wasted_bits_per_sample);

  return OK;
}

status FlacDecoder_read_subframe_header(decoders_FlacDecoder *self,
					struct flac_subframe_header *subframe_header) {
  Bitstream *bitstream = self->bitstream;
  uint8_t subframe_type;

  read_bits(bitstream,1);  /*padding*/
  subframe_type = read_bits(bitstream,6);
  if (subframe_type == 0) {
    subframe_header->type = FLAC_SUBFRAME_CONSTANT;
    subframe_header->order = 0;
  } else if (subframe_type == 1) {
    subframe_header->type = FLAC_SUBFRAME_VERBATIM;
    subframe_header->order = 0;
  } else if ((subframe_type & 0x38) == 0x08) {
    subframe_header->type = FLAC_SUBFRAME_FIXED;
    subframe_header->order = subframe_type & 0x07;
  } else if ((subframe_type & 0x20) == 0x20) {
    subframe_header->type = FLAC_SUBFRAME_LPC;
    subframe_header->order = (subframe_type & 0x1F) + 1;
  } else {
    PyErr_SetString(PyExc_ValueError,"invalid subframe type");
    return ERROR;
  }

  if (read_bits(bitstream,1) == 0) {
    subframe_header->wasted_bits_per_sample = 0;
  } else {
    subframe_header->wasted_bits_per_sample = read_unary(bitstream,1) + 1;
  }

  return OK;
}

status FlacDecoder_read_constant_subframe(decoders_FlacDecoder *self,
					  uint32_t block_size,
					  uint8_t bits_per_sample,
					  struct i_array *samples) {
  int32_t value = read_signed_bits(self->bitstream,bits_per_sample);
  int32_t i;

  ia_reset(samples);

  for (i = 0; i < block_size; i++)
    ia_append(samples,value);

  return OK;
}

status FlacDecoder_read_verbatim_subframe(decoders_FlacDecoder *self,
					  uint32_t block_size,
					  uint8_t bits_per_sample,
					  struct i_array *samples) {
  int32_t i;

  ia_reset(samples);
  for (i = 0; i < block_size; i++)
    ia_append(samples,read_signed_bits(self->bitstream,bits_per_sample));

  return OK;
}

status FlacDecoder_read_fixed_subframe(decoders_FlacDecoder *self,
				       uint8_t order,
				       uint32_t block_size,
				       uint8_t bits_per_sample,
				       struct i_array *samples) {
  int32_t i;
  Bitstream *bitstream = self->bitstream;
  struct i_array *residuals = &(self->residuals);

  ia_reset(residuals);
  ia_reset(samples);

  /*read "order" number of warm-up samples*/
  for (i = 0; i < order; i++) {
    ia_append(samples,read_signed_bits(bitstream,bits_per_sample));
  }

  /*read the residual*/
  if (FlacDecoder_read_residual(self,order,block_size,residuals) == ERROR)
    return ERROR;

  /*calculate subframe samples from warm-up samples and residual*/
  switch (order) {
  case 0:
    for (i = 0; i < residuals->size; i++) {
      ia_append(samples,
	       ia_getitem(residuals,i));
    }
    break;
  case 1:
    for (i = 0; i < residuals->size; i++) {
      ia_append(samples,
	       ia_getitem(samples,-1) +
	       ia_getitem(residuals,i));
    }
    break;
  case 2:
    for (i = 0; i < residuals->size; i++) {
      ia_append(samples,
	       (2 * ia_getitem(samples,-1)) -
	       ia_getitem(samples,-2) +
	       ia_getitem(residuals,i));
    }
    break;
  case 3:
    for (i = 0; i < residuals->size; i++) {
      ia_append(samples,
	       (3 * ia_getitem(samples,-1)) -
	       (3 * ia_getitem(samples,-2)) +
	       ia_getitem(samples,-3) +
	       ia_getitem(residuals,i));
    }
    break;
  case 4:
    for (i = 0; i < residuals->size; i++) {
      ia_append(samples,
	       (4 * ia_getitem(samples,-1)) -
	       (6 * ia_getitem(samples,-2)) +
	       (4 * ia_getitem(samples,-3)) -
	       ia_getitem(samples,-4) +
	       ia_getitem(residuals,i));
    }
    break;
  default:
    PyErr_SetString(PyExc_ValueError,"invalid FIXED subframe order");
    return ERROR;
  }

  return OK;
}

status FlacDecoder_read_lpc_subframe(decoders_FlacDecoder *self,
				     uint8_t order,
				     uint32_t block_size,
				     uint8_t bits_per_sample,
				     struct i_array *samples) {
  int i,j;
  Bitstream *bitstream = self->bitstream;
  uint32_t qlp_precision;
  int32_t qlp_shift_needed;
  struct i_array tail;
  int64_t accumulator;

  struct i_array *qlp_coeffs = &(self->qlp_coeffs);
  struct i_array *residuals = &(self->residuals);

  ia_reset(residuals);
  ia_reset(samples);
  ia_reset(qlp_coeffs);

  /*read order number of warm-up samples*/
  for (i = 0; i < order; i++) {
    ia_append(samples,read_signed_bits(bitstream,bits_per_sample));
  }

  /*read QLP precision*/
  qlp_precision = read_bits(bitstream,4) + 1;

  /*read QLP shift needed*/
  qlp_shift_needed = read_signed_bits(bitstream,5);

  /*read order number of QLP coefficients of size qlp_precision*/
  for (i = 0; i < order; i++) {
    ia_append(qlp_coeffs,read_signed_bits(bitstream,qlp_precision));
  }
  ia_reverse(qlp_coeffs);

  /*read the residual*/
  if (FlacDecoder_read_residual(self,order,block_size,residuals) == ERROR)
    return ERROR;

  /*calculate subframe samples from warm-up samples and residual*/
  for (i = 0; i < residuals->size; i++) {
    accumulator = 0;
    ia_tail(&tail,samples,order);
    for (j = 0; j < order; j++) {
      accumulator += (int64_t)ia_getitem(&tail,j) * (int64_t)ia_getitem(qlp_coeffs,j);
    }
    ia_append(samples,
	     (accumulator >> qlp_shift_needed) + ia_getitem(residuals,i));
  }

  return OK;
}

status FlacDecoder_read_residual(decoders_FlacDecoder *self,
				 uint8_t order,
				 uint32_t block_size,
				 struct i_array *residuals) {
  Bitstream *bitstream = self->bitstream;
  uint32_t coding_method = read_bits(bitstream,2);
  uint32_t partition_order = read_bits(bitstream,4);
  int total_partitions = 1 << partition_order;
  int partition;
  uint32_t rice_parameter;
  uint32_t partition_samples;
  uint32_t i;
  int32_t msb;
  int32_t lsb;
  int32_t value;

  ia_reset(residuals);

  /*read 2^partition_order number of partitions*/
  for (partition = 0; partition < total_partitions; partition++) {
    /*each partition after the first contains
      block_size / (2 ^ partition_order) number of residual values*/
    if (partition == 0) {
      partition_samples = (block_size / (1 << partition_order)) - order;
    } else {
      partition_samples = block_size / (1 << partition_order);
    }

    switch (coding_method) {
    case 0:
      rice_parameter = read_bits(bitstream,4);
      break;
    case 1:
      rice_parameter = read_bits(bitstream,5);
      break;
    default:
      PyErr_SetString(PyExc_ValueError,"invalid partition coding method");
      return ERROR;
    }

    for (i = 0; i < partition_samples; i++) {
      msb = read_unary(bitstream,1);
      lsb = read_bits(bitstream,rice_parameter);
      value = (msb << rice_parameter) | lsb;
      if (value & 1) {
	value = -(value >> 1) - 1;
      } else {
	value = value >> 1;
      }

      ia_append(residuals,value);
    }
  }

  return OK;
}


status FlacDecoder_skip_subframe(decoders_FlacDecoder *self,
				 uint32_t block_size,
				 uint8_t bits_per_sample) {
  struct flac_subframe_header subframe_header;

  if (FlacDecoder_read_subframe_header(self,&subframe_header) == ERROR)
    return ERROR;

  /*account for wasted bits-per-sample*/
  if (subframe_header.wasted_bits_per_sample > 0)
    bits_per_sample -= subframe_header.wasted_bits_per_sample;

  switch (subframe_header.type) {
  case FLAC_SUBFRAME_CONSTANT:
    if (FlacDecoder_skip_constant_subframe(self, block_size,
					   bits_per_sample) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_VERBATIM:
    if (FlacDecoder_skip_verbatim_subframe(self, block_size,
					   bits_per_sample) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_FIXED:
    if (FlacDecoder_skip_fixed_subframe(self, subframe_header.order,
					block_size,
					bits_per_sample) == ERROR)
      return ERROR;
    break;
  case FLAC_SUBFRAME_LPC:
    if (FlacDecoder_skip_lpc_subframe(self, subframe_header.order,
				      block_size,
				      bits_per_sample) == ERROR)
      return ERROR;
    break;
  }

  return OK;
}

status FlacDecoder_skip_constant_subframe(decoders_FlacDecoder *self,
					  uint32_t block_size,
					  uint8_t bits_per_sample) {
  read_signed_bits(self->bitstream,bits_per_sample);
  return OK;
}

status FlacDecoder_skip_verbatim_subframe(decoders_FlacDecoder *self,
					  uint32_t block_size,
					  uint8_t bits_per_sample) {
  int32_t i;

  for (i = 0; i < block_size; i++)
    read_signed_bits(self->bitstream,bits_per_sample);

  return OK;
}

status FlacDecoder_skip_fixed_subframe(decoders_FlacDecoder *self,
				       uint8_t order,
				       uint32_t block_size,
				       uint8_t bits_per_sample) {
  Bitstream *bitstream = self->bitstream;
  int32_t i;

  /*read "order" number of warm-up samples*/
  for (i = 0; i < order; i++) {
    read_signed_bits(bitstream,bits_per_sample);
  }

  /*read the residual*/
  if (FlacDecoder_skip_residual(self,order,block_size) == ERROR)
    return ERROR;

  return OK;
}

status FlacDecoder_skip_lpc_subframe(decoders_FlacDecoder *self,
				     uint8_t order,
				     uint32_t block_size,
				     uint8_t bits_per_sample) {
  Bitstream *bitstream = self->bitstream;
  int i;
  uint32_t qlp_precision;

  /*read order number of warm-up samples*/
  for (i = 0; i < order; i++) {
    read_signed_bits(bitstream,bits_per_sample);
  }

  /*read QLP precision*/
  qlp_precision = read_bits(bitstream,4) + 1;

  /*read QLP shift needed*/
  read_signed_bits(bitstream,5);

  /*read order number of QLP coefficients of size qlp_precision*/
  for (i = 0; i < order; i++) {
    read_signed_bits(bitstream,qlp_precision);
  }

  /*read the residual*/
  if (FlacDecoder_skip_residual(self,order,block_size) == ERROR)
    return ERROR;

  return OK;
}

status FlacDecoder_skip_residual(decoders_FlacDecoder *self,
				 uint8_t order,
				 uint32_t block_size) {
  Bitstream *bitstream = self->bitstream;
  uint32_t coding_method = read_bits(bitstream,2);
  uint32_t partition_order = read_bits(bitstream,4);
  int total_partitions = 1 << partition_order;
  int partition;
  uint32_t rice_parameter;
  uint32_t partition_samples;
  uint32_t i;

  /*read 2^partition_order number of partitions*/
  for (partition = 0; partition < total_partitions; partition++) {
    /*each partition after the first contains
      block_size / (2 ^ partition_order) number of residual values*/
    if (partition == 0) {
      partition_samples = (block_size / (1 << partition_order)) - order;
    } else {
      partition_samples = block_size / (1 << partition_order);
    }

    switch (coding_method) {
    case 0:
      rice_parameter = read_bits(bitstream,4);
      break;
    case 1:
      rice_parameter = read_bits(bitstream,5);
      break;
    default:
      PyErr_SetString(PyExc_ValueError,"invalid partition coding method");
      return ERROR;
    }

    for (i = 0; i < partition_samples; i++) {
      read_unary(bitstream,1);
      read_bits(bitstream,rice_parameter);
    }
  }

  return OK;
}

uint32_t read_utf8(Bitstream *stream) {
  uint32_t total_bytes = read_unary(stream,0);
  uint32_t value = read_bits(stream,7 - total_bytes);
  for (;total_bytes > 1;total_bytes--) {
    value = (value << 6) | (read_bits(stream,8) & 0x3F);
  }

  return value;
}

#include "flac_crc.c"


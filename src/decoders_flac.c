int FlacDecoder_init(decoders_FlacDecoder *self,
		     PyObject *args, PyObject *kwds) {
  char* filename;

  if (!PyArg_ParseTuple(args, "s", &filename))
    return -1;

  self->filename = NULL;
  self->file = NULL;
  self->bitstream = NULL;

  /*open the flac file*/
  self->file = fopen(filename,"rb");
  if (self->file == NULL) {
    PyErr_SetFromErrnoWithFilename(PyExc_IOError,filename);
    return -1;
  } else {
    self->bitstream = bs_open(self->file);
  }

  self->filename = strdup(filename);

  if (!FlacDecoder_read_metadata(self)) {
    return -1;
  }

  return 0;
}

void FlacDecoder_dealloc(decoders_FlacDecoder *self) {
  if (self->filename != NULL)
    free(self->filename);

  bs_close(self->bitstream);

  Py_TYPE(self)->tp_free((PyObject*)self);
}

PyObject *FlacDecoder_new(PyTypeObject *type,
			  PyObject *args, PyObject *kwds) {
  decoders_FlacDecoder *self;

  self = (decoders_FlacDecoder *)type->tp_alloc(type, 0);

  return (PyObject *)self;
}

int FlacDecoder_read_metadata(decoders_FlacDecoder *self) {
  unsigned int last_block;
  unsigned int block_type;
  unsigned int block_length;

  if (read_bits(self->bitstream,32) != 0x664C6143u) {
    PyErr_SetString(PyExc_ValueError,"not a FLAC file");
    return 0;
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
      return 0;
    }
  } else {
    PyErr_SetString(PyExc_ValueError,"STREAMINFO not first metadata block");
    return 0;
  }

  while (!last_block) {
    last_block = read_bits(self->bitstream,1);
    block_type = read_bits(self->bitstream,7);
    block_length = read_bits(self->bitstream,24);
    fseek(self->file,block_length,SEEK_CUR);
  }

  return 1;
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

PyObject *FLACDecoder_read(decoders_FlacDecoder* self,
			   PyObject *args) {
  int bytes;
  struct flac_frame_header frame_header;

  if (!PyArg_ParseTuple(args, "i", &bytes))
    return NULL;
  if (bytes < 0) {
    PyErr_SetString(PyExc_ValueError,"number of bytes must be positive");
    return NULL;
  }

  if (!FlacDecoder_read_frame_header(self,&frame_header))
    return NULL;

  /*FIXME - read one subframe per channel*/
  if (!FlacDecoder_read_subframe(self,
				 frame_header.block_size,
				 frame_header.bits_per_sample))
    return NULL;

  /*FIXME - transform subframe data into string*/

  /*FIXME - return string*/

  return Py_BuildValue("(i,i,i,i,i)",
		       frame_header.block_size,
		       frame_header.sample_rate,
		       frame_header.channel_assignment,
		       frame_header.bits_per_sample,
		       frame_header.frame_number);
}

int FlacDecoder_read_frame_header(decoders_FlacDecoder *self,
				  struct flac_frame_header *header) {
  Bitstream *bitstream = self->bitstream;
  uint32_t block_size_bits;
  uint32_t sample_rate_bits;

  /*read and verify sync code*/
  if (read_bits(bitstream,14) != 0x3FFE) {
    PyErr_SetString(PyExc_ValueError,"invalid sync code");
    return 0;
  }

  /*read and verify reserved bit*/
  if (read_bits(bitstream,1) != 0) {
    PyErr_SetString(PyExc_ValueError,"invalid reserved bit");
    return 0;
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
  default:
    header->channel_count = header->channel_assignment + 1;
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
    return 0;
  }
  read_bits(bitstream,1); /*padding*/

  header->frame_number = read_bits(bitstream,8); /*FIXME - must be UTF-8*/

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
    return 0;
  }

  read_bits(bitstream,8); /*CRC-8*/
  /*FIXME - check for valid CRC-8 value*/

  return 1;
}

int FlacDecoder_read_subframe(decoders_FlacDecoder *self,
			      uint32_t block_size,
			      uint8_t bits_per_sample) {
  struct flac_subframe_header subframe_header;

  if (!FlacDecoder_read_subframe_header(self,&subframe_header))
    return 0;

  switch (subframe_header.type) {
  case FLAC_SUBFRAME_CONSTANT:
    PyErr_SetString(PyExc_ValueError,"subframe type not yet supported");
    return 0;
  case FLAC_SUBFRAME_VERBATIM:
    PyErr_SetString(PyExc_ValueError,"subframe type not yet supported");
    return 0;
  case FLAC_SUBFRAME_FIXED:
    /*FIXME - account for wasted bits-per-sample*/
    /*FIXME - account for difference channels*/
    if (!FlacDecoder_read_fixed_subframe(self, subframe_header.order,
					 block_size, bits_per_sample))
      return 0;
    break;
  case FLAC_SUBFRAME_LPC:
    PyErr_SetString(PyExc_ValueError,"subframe type not yet supported");
    return 0;
  }

  return 1;
}

int FlacDecoder_read_subframe_header(decoders_FlacDecoder *self,
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
    subframe_header->order = subframe_type & 0x1F;
  } else {
    PyErr_SetString(PyExc_ValueError,"invalid subframe type");
    return 0;
  }

  if (read_bits(bitstream,1) == 0) {
    subframe_header->wasted_bits_per_sample = 0;
  } else {
    /*FIXME - need to check this*/
    subframe_header->wasted_bits_per_sample = read_unary(bitstream,1);
  }

  return 1;
}

int FlacDecoder_read_fixed_subframe(decoders_FlacDecoder *self,
				    uint8_t order,
				    uint32_t block_size,
				    uint8_t bits_per_sample) {
  int i;
  Bitstream *bitstream = self->bitstream;

  /*read "order" number of warm-up samples*/
  for (i = 0; i < order; i++) {
    printf("read warm-up (%d) %d\n",bits_per_sample,
	   read_signed_bits(bitstream,bits_per_sample));
  }

  /*read the residual*/
  if (!FlacDecoder_read_residual(self,order,block_size))
    return 0;

  /*calculate subframe samples from warm-up samples and residual*/


  return 1;
}

int FlacDecoder_read_residual(decoders_FlacDecoder *self,
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
  int32_t msb;
  int32_t lsb;
  int32_t value;

  for (partition = 0; partition < total_partitions; partition++) {
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
      return 0;
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
      /*FIXME - place value in residual list*/
    }
  }

  return 1;
}

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

  if (read_bits(bitstream,14) != 0x3FFE) {
    PyErr_SetString(PyExc_ValueError,"invalid sync code");
    return 0;
  }
  if (read_bits(bitstream,1) != 0) {
    PyErr_SetString(PyExc_ValueError,"invalid reserved bit");
    return 0;
  }
  header->blocking_strategy = read_bits(bitstream,1);

  block_size_bits = read_bits(bitstream,4);
  sample_rate_bits = read_bits(bitstream,4);
  header->channel_assignment = read_bits(bitstream,4);
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

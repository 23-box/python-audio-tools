#include <Python.h>

#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

/*a convenience structure for PCM info*/
typedef struct {
  int sample_rate;
  int channels;
  int bits_per_sample;
} PCMInfo;

/*a blob of PCM data, with information about its format*/
typedef struct {
  PCMInfo info;
  int *data;       /*an array of PCM samples, as ints*/
                   /*(assumes the system supports at least 32-bit ints)*/
  int data_length; /*the total length of the array*/
} PCMData;

/*the PCMConverter Python object*/
typedef struct {
  PyObject_HEAD
  PyObject *pcmreader;
  PCMInfo input_pcm;
  PCMInfo output_pcm;
} pcmconversion_PCMConverter;


static void PCMConverter_dealloc(pcmconversion_PCMConverter* self);

static PyObject *PCMConverter_new(PyTypeObject *type, 
				  PyObject *args, PyObject *kwds);

static int PCMConverter_init(pcmconversion_PCMConverter *self, 
			     PyObject *args, PyObject *kwds);

static PyObject *PCMConverter_close(pcmconversion_PCMConverter* self);

static PyObject *PCMConverter_read(pcmconversion_PCMConverter* self, 
				   PyObject *args);

static PyObject *PCMConverter_get_sample_rate(pcmconversion_PCMConverter *self,
					      void *closure);

static PyObject *PCMConverter_get_channels(pcmconversion_PCMConverter *self,
					   void *closure);

static PyObject *PCMConverter_get_bits_per_sample(pcmconversion_PCMConverter *self,
						  void *closure);


/*returns a newly allocated PCMData struct*/
PCMData *new_pcm_data(int sample_rate, int channels, int bits_per_sample,
		      int total_samples);

/*frees the PCMData struct created by new_pcm_data()*/
void free_pcm_data(PCMData *data);

/*Copies as many PCM samples as possible from pcm_string
  to a newly-created PCMData struct.
  "final_offset" indicates our final location in the pcm_string*/
PCMData *char_to_pcm_data(unsigned char *pcm_string, int pcm_string_length,
			  int sample_rate, int channels, int bits_per_sample,
			  int *final_offset);

/*Takes PCMData and converts it to a chunk of PCM data as a blob of bytes.
  The newly malloc()ed data is returned (which must be freed later)
  and the length of the new data is stored in "length".*/
unsigned char *pcm_data_to_char(PCMData *data, int *length);

int char_to_16bit(unsigned char *s);
void _16bit_to_char(int i, unsigned char *s);


static PyMethodDef module_methods[] = {
  {NULL}
};


static PyMethodDef PCMConverter_methods[] = {
  {"close", (PyCFunction)PCMConverter_close,
   METH_NOARGS,"Closes the PCMConverter and internal PCMReader"},
  {"read", (PyCFunction)PCMConverter_read,
   METH_VARARGS,"Reads converted samples from the internal PCMReader"},
  {NULL}
};

static PyGetSetDef PCMConverter_getseters[] = {
    {"sample_rate", 
     (getter)PCMConverter_get_sample_rate, 0,
     "sample rate",
     NULL},
    {"channels", 
     (getter)PCMConverter_get_channels, 0,
     "channels",
     NULL},
    {"bits_per_sample", 
     (getter)PCMConverter_get_bits_per_sample, 0,
     "bits per sample",
     NULL},
    {NULL}  /* Sentinel */
};

static PyTypeObject pcmconversion_PCMConverterType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "pcmconversion.PCMConverter", /*tp_name*/
    sizeof(pcmconversion_PCMConverter), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)PCMConverter_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "PCMConverter objects",    /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    PCMConverter_methods,      /* tp_methods */
    0,                         /* tp_members */
    PCMConverter_getseters,     /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)PCMConverter_init, /* tp_init */
    0,                         /* tp_alloc */
    PCMConverter_new,       /* tp_new */
};



#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC initpcmconversion(void) {
    PyObject* m;

    pcmconversion_PCMConverterType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&pcmconversion_PCMConverterType) < 0)
        return;

    m = Py_InitModule3("pcmconversion", module_methods,
                       "A PCM stream conversion module.");

    Py_INCREF(&pcmconversion_PCMConverterType);
    PyModule_AddObject(m, "PCMConverter", 
		       (PyObject *)&pcmconversion_PCMConverterType);
}

static PyObject *PCMConverter_new(PyTypeObject *type, 
				  PyObject *args, PyObject *kwds) {
  pcmconversion_PCMConverter *self;

  self = (pcmconversion_PCMConverter *)type->tp_alloc(type, 0);

  return (PyObject *)self;
}

static int PCMConverter_init(pcmconversion_PCMConverter *self, 
			     PyObject *args, PyObject *kwds) {
  PyObject *pcmreader = NULL;

  PyObject *input_sample_rate = NULL;
  PyObject *input_channels = NULL;
  PyObject *input_bits_per_sample = NULL;

  int output_sample_rate = 0;
  int output_channels = 0;
  int output_bits_per_sample = 0;

  self->pcmreader = NULL;

  if (!PyArg_ParseTuple(args, "Oiii", 
			&pcmreader,
			&output_sample_rate,
			&output_channels,
			&output_bits_per_sample))
    return -1;

  Py_INCREF(pcmreader);
  self->pcmreader = pcmreader;

  self->output_pcm.sample_rate = output_sample_rate;
  self->output_pcm.channels = output_channels;
  self->output_pcm.bits_per_sample = output_bits_per_sample;

  input_sample_rate = PyObject_GetAttrString(pcmreader,"sample_rate");
  if (input_sample_rate == NULL) return -1;

  input_channels = PyObject_GetAttrString(pcmreader,"channels");
  if (input_channels == NULL) return -1;

  input_bits_per_sample = PyObject_GetAttrString(pcmreader,"bits_per_sample");
  if (input_bits_per_sample == NULL) return -1;

  self->input_pcm.sample_rate = (int)PyInt_AsLong(input_sample_rate);
  if ((self->input_pcm.sample_rate == -1) &&
      PyErr_Occurred()) return -1;

  self->input_pcm.channels = (int)PyInt_AsLong(input_channels);
  if ((self->input_pcm.channels == -1) &&
      PyErr_Occurred()) return -1;

  self->input_pcm.bits_per_sample = (int)PyInt_AsLong(input_bits_per_sample);
  if ((self->input_pcm.bits_per_sample == -1) &&
      PyErr_Occurred()) return -1;

  Py_DECREF(input_sample_rate);
  Py_DECREF(input_channels);
  Py_DECREF(input_bits_per_sample);

  return 0;
}

static void PCMConverter_dealloc(pcmconversion_PCMConverter* self)
{
  Py_XDECREF(self->pcmreader);
  self->ob_type->tp_free((PyObject*)self);
}

static PyObject *PCMConverter_close(pcmconversion_PCMConverter* self) {
  return PyObject_CallMethod(self->pcmreader,"close",NULL);
}

static PyObject *PCMConverter_read(pcmconversion_PCMConverter* self, 
				   PyObject *args) {
  static long int read_amount;
  PyObject *input_string;
  char *input_pcm;
  Py_ssize_t input_pcm_length;

  PCMData *input_data;
  int unread_pcm;

  char *output_pcm;
  int output_pcm_length;
  PyObject *output_string;

  if (!PyArg_ParseTuple(args,"l",&read_amount))
    return NULL;

  input_string = PyObject_CallMethod(self->pcmreader,"read","l",read_amount);

  if (PyString_AsStringAndSize(input_string, &input_pcm, 
			       &input_pcm_length) == -1) {
    Py_DECREF(input_string);
    return NULL;
  }

  input_data = char_to_pcm_data((unsigned char *)input_pcm, input_pcm_length,
				self->input_pcm.sample_rate,
				self->input_pcm.channels,
				self->input_pcm.bits_per_sample,
				&unread_pcm);
  

  output_pcm = (char *)pcm_data_to_char(input_data, 
					&output_pcm_length);

  output_string = PyString_FromStringAndSize(output_pcm,
					     (Py_ssize_t)output_pcm_length);
  
  free_pcm_data(input_data);
  free(output_pcm);
  Py_DECREF(input_string);

  return output_string;
}


static PyObject *PCMConverter_get_sample_rate(pcmconversion_PCMConverter *self,
					      void *closure) {
  PyObject *sample_rate;
  sample_rate = Py_BuildValue("i",self->output_pcm.sample_rate);
  return sample_rate;
}

static PyObject *PCMConverter_get_channels(pcmconversion_PCMConverter *self,
					   void *closure) {
  PyObject *channels;
  channels = Py_BuildValue("i",self->output_pcm.channels);
  return channels;
}

static PyObject *PCMConverter_get_bits_per_sample(pcmconversion_PCMConverter *self,
						  void *closure) {
  PyObject *bits_per_sample;
  bits_per_sample = Py_BuildValue("i",self->output_pcm.bits_per_sample);
  return bits_per_sample;
}


PCMData *new_pcm_data(int sample_rate, int channels, int bits_per_sample,
		      int total_samples) {
  PCMData *data;

  data = (PCMData *)malloc(sizeof(PCMData));
  data->info.sample_rate = sample_rate;
  data->info.channels = channels;
  data->info.bits_per_sample = bits_per_sample;

  data->data = (int *)malloc(sizeof(int) * total_samples);
  data->data_length = total_samples;

  return data;
}

void free_pcm_data(PCMData *data) {
  free(data->data);
  free(data);
}

PCMData *char_to_pcm_data(unsigned char *pcm_string, int pcm_string_length,
			  int sample_rate, int channels, int bits_per_sample,
			  int *final_offset) {
  PCMData *data;
  int input = 0;
  int output = 0;

  data = new_pcm_data(sample_rate, channels, bits_per_sample,
		      pcm_string_length / (bits_per_sample / 8));

  if (bits_per_sample == 16) {
    for (input = 0,output=0; 
	 (input < pcm_string_length) && (output < data->data_length);
	 input += 2,output++) {
      data->data[output] = char_to_16bit(pcm_string + input);
    }
  }

  *final_offset = input;
  return data;
}

unsigned char *pcm_data_to_char(PCMData *data, int *length) {
  unsigned char *output_pcm;
  int output_length = 0;
  int input = 0;
  int output = 0;

  output_length = data->data_length * (data->info.bits_per_sample / 8);
  output_pcm = (unsigned char *)calloc(output_length,sizeof(unsigned char));

  if (data->info.bits_per_sample == 16) {
    for (input = 0,output = 0;
	 (input < data->data_length) && (output < output_length);
	 input++,output += 2)
      _16bit_to_char(data->data[input],output_pcm + output);
  }
  
  *length = output_length;
  return output_pcm;
}

int char_to_16bit(unsigned char *s) {
  if ((s[1] & 0x80) != 0)
    return -(0x10000 - ((s[1] << 8) | s[0])); /*negative*/
  else
    return (s[1] << 8) | s[0];                /*positive*/
						
}

void _16bit_to_char(int i, unsigned char *s) {
  s[0] = i & 0x00FF;
  s[1] = (i & 0xFF00) >> 8;
}

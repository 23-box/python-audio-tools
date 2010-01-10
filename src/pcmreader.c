#include "pcmreader.h"

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

struct pcm_reader* pcmr_open(PyObject *pcmreader) {
  struct pcm_reader *reader = malloc(sizeof(struct pcm_reader));
  PyObject *attr;

  reader->callback = NULL;

  if ((attr = PyObject_GetAttrString(pcmreader,"sample_rate")) == NULL)
    goto error;
  reader->sample_rate = PyInt_AsLong(attr);
  Py_DECREF(attr);
  if ((reader->sample_rate == -1) && (PyErr_Occurred()))
    goto error;

  if ((attr = PyObject_GetAttrString(pcmreader,"bits_per_sample")) == NULL)
    goto error;
  reader->bits_per_sample = PyInt_AsLong(attr);
  Py_DECREF(attr);
  if ((reader->bits_per_sample == -1) && (PyErr_Occurred()))
    goto error;

  if ((attr = PyObject_GetAttrString(pcmreader,"channels")) == NULL)
    goto error;
  reader->channels = PyInt_AsLong(attr);
  Py_DECREF(attr);
  if ((reader->channels == -1) && (PyErr_Occurred()))
    goto error;

  if ((reader->read = PyObject_GetAttrString(pcmreader,"read")) == NULL)
    goto error;
  if (!PyCallable_Check(reader->read)) {
    Py_DECREF(reader->read);
    PyErr_SetString(PyExc_TypeError,"read parameter must be callable");
    goto error;
  }
  if ((reader->close = PyObject_GetAttrString(pcmreader,"close")) == NULL)
    goto error;
  if (!PyCallable_Check(reader->close)) {
    Py_DECREF(reader->read);
    Py_DECREF(reader->close);
    PyErr_SetString(PyExc_TypeError,"close parameter must be callable");
    goto error;
  }

  return reader;
 error:
  free(reader);
  return NULL;
}

int pcmr_close(struct pcm_reader *reader) {
  PyObject *result;
  int returnval;
  struct pcmr_callback *callback;
  struct pcmr_callback *next;

  result = PyEval_CallObject(reader->close,NULL);
  if (result == NULL)
    returnval = 0;
  else {
    Py_DECREF(result);
    returnval = 1;
  }

  for (callback = reader->callback; callback != NULL; callback = next) {
    next = callback->next;
    free(callback);
  }

  Py_DECREF(reader->read);
  Py_DECREF(reader->close);
  free(reader);
  return returnval;
}

void pcmr_add_callback(struct pcm_reader *reader,
		       void (*callback)(void*, unsigned char*, unsigned long),
		       void *data) {
  struct pcmr_callback *callback_node = malloc(sizeof(struct pcmr_callback));
  callback_node->callback = callback;
  callback_node->data = data;
  callback_node->next = reader->callback;
  reader->callback = callback_node;
}

int pcmr_read(struct pcm_reader *reader,
	      long sample_count,
	      struct ia_array *samples) {
  uint32_t i;
  PyObject *args;
  PyObject *result;

  unsigned char *buffer;
  Py_ssize_t buffer_length;

  struct pcmr_callback *node;
  struct pcmr_callback *next;

  args = Py_BuildValue("(l)", sample_count *
		              reader->bits_per_sample * samples->size / 8);
  result = PyEval_CallObject(reader->read,args);
  Py_DECREF(args);
  if (result == NULL)
    return 0;
  if (PyString_AsStringAndSize(result,(char **)(&buffer),&buffer_length) == -1){
    Py_DECREF(result);
    return 0;
  }

  for (node = reader->callback; node != NULL; node = next) {
    next = node->next;
    node->callback(node->data,buffer,(unsigned long)buffer_length);
  }

  for (i = 0; i < reader->channels; i++) {
    ia_reset(iaa_getitem(samples,i));
    switch (reader->bits_per_sample) {
    case 8:
      ia_char_to_U8(iaa_getitem(samples,i),
		    buffer,(int)buffer_length,i,samples->size);
      break;
    case 16:
      ia_char_to_SL16(iaa_getitem(samples,i),
		      buffer,(int)buffer_length,i,samples->size);
      break;
    case 24:
      ia_char_to_SL24(iaa_getitem(samples,i),
		      buffer,(int)buffer_length,i,samples->size);
      break;
    default:
      PyErr_SetString(PyExc_ValueError,"unsupported bits per sample");
      Py_DECREF(result);
      return 0;
    }
  }

  Py_DECREF(result);
  return 1;
}


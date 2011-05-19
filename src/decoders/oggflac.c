#include "oggflac.h"
#include "../pcm.h"
#include "pcm.h"

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

PyObject*
OggFlacDecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    decoders_OggFlacDecoder *self;

    self = (decoders_OggFlacDecoder *)type->tp_alloc(type, 0);

    return (PyObject *)self;
}

void
OggFlacDecoder_dealloc(decoders_OggFlacDecoder *self) {
    self->packet->close(self->packet);
    if (self->ogg_stream != NULL)
        oggreader_close(self->ogg_stream);
    if (self->ogg_file != NULL)
        fclose(self->ogg_file);

    self->ob_type->tp_free((PyObject*)self);
}

int
OggFlacDecoder_init(decoders_OggFlacDecoder *self,
                    PyObject *args, PyObject *kwds) {
    char* filename;
    ogg_status result;
    uint16_t header_packets;

    self->ogg_stream = NULL;
    self->ogg_file = NULL;
    self->packet = bs_substream_new(BS_BIG_ENDIAN);

    if (!PyArg_ParseTuple(args, "s", &filename))
        goto error;
    self->ogg_file = fopen(filename, "rb");
    if (self->ogg_file == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
        goto error;
    } else {
        self->ogg_stream = oggreader_open(self->ogg_file);
    }

    /*the first packet should be the FLAC's STREAMINFO*/
    if ((result = oggreader_next_packet(self->ogg_stream,
                                        self->packet)) == OGG_OK) {
        if (!oggflac_read_streaminfo(self->packet,
                                     &(self->streaminfo),
                                     &header_packets))
            goto error;
    } else {
        PyErr_SetString(ogg_exception(result), ogg_strerror(result));
        goto error;
    }

    /*skip subsequent header packets*/
    for (; header_packets > 0; header_packets--) {
        if ((result = oggreader_next_packet(self->ogg_stream,
                                            self->packet)) != OGG_OK) {
            PyErr_SetString(ogg_exception(result), ogg_strerror(result));
            goto error;
        }
    }

    /*setup a bunch of temporary buffers*/
    iaa_init(&(self->subframe_data),
             self->streaminfo.channels,
             self->streaminfo.maximum_block_size);
    ia_init(&(self->residuals), self->streaminfo.maximum_block_size);
    ia_init(&(self->qlp_coeffs), 1);

    return 0;

 error:
    /*setup some dummy buffers for dealloc to free*/
    iaa_init(&(self->subframe_data), 1, 1);
    ia_init(&(self->residuals), 1);
    ia_init(&(self->qlp_coeffs), 1);

    return -1;
}

static PyObject*
OggFlacDecoder_sample_rate(decoders_OggFlacDecoder *self, void *closure) {
    return Py_BuildValue("i", self->streaminfo.sample_rate);
}

static PyObject*
OggFlacDecoder_bits_per_sample(decoders_OggFlacDecoder *self, void *closure) {
    return Py_BuildValue("i", self->streaminfo.bits_per_sample);
}

static PyObject*
OggFlacDecoder_channels(decoders_OggFlacDecoder *self, void *closure) {
    return Py_BuildValue("i", self->streaminfo.channels);
}

static PyObject*
OggFlacDecoder_channel_mask(decoders_OggFlacDecoder *self, void *closure) {
    /*FIXME*/

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
OggFlacDecoder_read(decoders_OggFlacDecoder *self, PyObject *args) {
    ogg_status ogg_status;
    flac_status flac_status;
    struct flac_frame_header frame_header;
    int channel;
    PyObject *framelist;

    iaa_reset(&(self->subframe_data));

    ogg_status = oggreader_next_packet(self->ogg_stream, self->packet);

    if (ogg_status == OGG_OK) {
        /*decode the next FrameList from the stream*/
        /*FIXME - support threading here*/

        if (!setjmp(*bs_try(self->packet))) {
            /*read frame header*/
            if ((flac_status = FlacDecoder_read_frame_header(
                                                    self->packet,
                                                    &(self->streaminfo),
                                                    &frame_header)) != OK) {
                PyErr_SetString(PyExc_ValueError,
                                FlacDecoder_strerror(flac_status));
                bs_etry(self->packet);
                return NULL;
            }

            /*read 1 subframe per channel*/
            for (channel = 0; channel < frame_header.channel_count; channel++)
                if ((flac_status = FlacDecoder_read_subframe(
                        self->packet,
                        &(self->qlp_coeffs),
                        &(self->residuals),
                        frame_header.block_size,
                        FlacDecoder_subframe_bits_per_sample(&frame_header,
                                                             channel),
                        &(self->subframe_data.arrays[channel]))) != OK) {
                    PyErr_SetString(PyExc_ValueError,
                                    FlacDecoder_strerror(flac_status));
                    bs_etry(self->packet);
                    return NULL;
                }

            bs_etry(self->packet);

            /*handle difference channels, if any*/
            FlacDecoder_decorrelate_channels(&frame_header,
                                             &(self->subframe_data));

            /*check CRC-16*/
            /*FIXME*/

            framelist = ia_array_to_framelist(&(self->subframe_data),
                                              frame_header.bits_per_sample);

            /*update MD5 sum*/
            /*FIXME*/

            /*return pcm.FrameList Python object*/
            return framelist;
        } else {
            /*read error decoding FLAC frame*/
            PyErr_SetString(PyExc_IOError, "I/O error decoding FLAC frame");
            bs_etry(self->packet);
            return NULL;
        }
    } else if (ogg_status == OGG_STREAM_FINISHED) {
        /*Ogg stream is finished so verify stream's MD5 sum
          then return an empty FrameList if it matches correctly*/
        /*FIXME - verify stream MD5SUM here*/

        return ia_array_to_framelist(&(self->subframe_data),
                                     self->streaminfo.bits_per_sample);
    } else {
        /*error reading the next Ogg packet,
          so raise the appropriate exception*/
        PyErr_SetString(ogg_exception(ogg_status), ogg_strerror(ogg_status));
        return NULL;
    }
}

static PyObject*
OggFlacDecoder_analyze_frame(decoders_OggFlacDecoder *self, PyObject *args) {
    /*FIXME*/

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
OggFlacDecoder_close(decoders_OggFlacDecoder *self, PyObject *args) {
    /*FIXME*/

    Py_INCREF(Py_None);
    return Py_None;
}

int
oggflac_read_streaminfo(Bitstream *packet,
                        struct flac_STREAMINFO *streaminfo,
                        uint16_t *header_packets) {
    int i;

    if (!setjmp(*bs_try(packet))) {
        if (packet->read(packet, 8) != 0x7F) {
            PyErr_SetString(PyExc_ValueError, "invalid packet byte");
            goto error;
        }
        if (packet->read_64(packet, 32) != 0x464C4143) {
            PyErr_SetString(PyExc_ValueError, "invalid Ogg signature");
            goto error;
        }
        if (packet->read(packet, 8) != 1) {
            PyErr_SetString(PyExc_ValueError, "invalid major version");
            goto error;
        }
        if (packet->read(packet, 8) != 0) {
            PyErr_SetString(PyExc_ValueError, "invalid minor version");
            goto error;
        }
        *header_packets = packet->read(packet, 16);
        if (packet->read_64(packet, 32) != 0x664C6143) {
            PyErr_SetString(PyExc_ValueError, "invalid fLaC signature");
            goto error;
        }
        packet->read(packet, 1); /*last block*/
        if (packet->read(packet, 7) != 0) {
            PyErr_SetString(PyExc_ValueError, "invalid block type");
            goto error;
        }
        packet->read(packet, 24); /*block length*/

        streaminfo->minimum_block_size = packet->read(packet, 16);
        streaminfo->maximum_block_size = packet->read(packet, 16);
        streaminfo->minimum_frame_size = packet->read(packet, 24);
        streaminfo->maximum_frame_size = packet->read(packet, 24);
        streaminfo->sample_rate = packet->read(packet, 20);
        streaminfo->channels = packet->read(packet, 3) + 1;
        streaminfo->bits_per_sample = packet->read(packet, 5) + 1;
        streaminfo->total_samples = packet->read_64(packet, 36);
        for (i = 0; i < 16; i++) {
            streaminfo->md5sum[i] = packet->read(packet, 8);
        }
    } else {
        PyErr_SetString(PyExc_IOError,
                        "EOF while reading STREAMINFO block");
        goto error;
    }

    bs_etry(packet);
    return 1;
 error:
    bs_etry(packet);
    return 0;
}

#include "pcm.c"

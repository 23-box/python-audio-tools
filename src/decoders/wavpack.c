#include "wavpack.h"

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

int
WavPackDecoder_init(decoders_WavPackDecoder *self,
                    PyObject *args, PyObject *kwds) {
    char* filename;
    struct wavpack_block_header block_header;

    self->filename = NULL;
    self->bitstream = NULL;
    self->file = NULL;
    ia_init(&(self->decorr_terms), 8);
    ia_init(&(self->decorr_deltas), 8);
    ia_init(&(self->decorr_weights_A), 8);
    ia_init(&(self->decorr_weights_B), 8);
    iaa_init(&(self->decorr_samples_A), 16, 8);
    iaa_init(&(self->decorr_samples_B), 16, 8);

    if (!PyArg_ParseTuple(args, "s", &filename))
        return -1;

    /*open the WavPack file*/
    self->file = fopen(filename, "rb");
    if (self->file == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
        return -1;
    } else {
        self->bitstream = bs_open(self->file, BS_LITTLE_ENDIAN);
    }

    self->filename = strdup(filename);

    /*read as many block headers as necessary
      to determine channel count and channel mask*/
    self->sample_rate = 0;
    self->bits_per_sample = 0;
    self->channels = 0;
    self->channel_mask = 0;
    self->remaining_samples = -1;

    /*FIXME - check for EOF here*/
    do {
        if (WavPackDecoder_read_block_header(self->bitstream,
                                             &block_header) == ERROR)
            return -1;
        else {
            if (self->remaining_samples == -1)
                self->remaining_samples = block_header.total_samples;

            self->sample_rate = block_header.sample_rate;
            self->bits_per_sample = block_header.bits_per_sample;
            self->channels += (block_header.mono_output ? 1 : 2);
            fseek(self->file, block_header.block_size - 24, SEEK_CUR);
            /*FIXME - determining channel mask requires sub-block parsing*/
        }
    } while (block_header.final_block_in_sequence == 0);

    fseek(self->file, 0, SEEK_SET);

    /*setup a bunch of temporary buffers*/

    return 0;
}

void
WavPackDecoder_dealloc(decoders_WavPackDecoder *self) {
    ia_free(&(self->decorr_terms));
    ia_free(&(self->decorr_deltas));
    ia_free(&(self->decorr_weights_A));
    ia_free(&(self->decorr_weights_B));
    iaa_free(&(self->decorr_samples_A));
    iaa_free(&(self->decorr_samples_B));

    if (self->filename != NULL)
        free(self->filename);

    bs_close(self->bitstream);

    self->ob_type->tp_free((PyObject*)self);
}

static PyObject*
WavPackDecoder_new(PyTypeObject *type,
                   PyObject *args, PyObject *kwds) {
    decoders_WavPackDecoder *self;

    self = (decoders_WavPackDecoder *)type->tp_alloc(type, 0);

    return (PyObject *)self;
}

static PyObject*
WavPackDecoder_sample_rate(decoders_WavPackDecoder *self, void *closure) {
    return Py_BuildValue("i", self->sample_rate);
}

static PyObject*
WavPackDecoder_bits_per_sample(decoders_WavPackDecoder *self, void *closure) {
    return Py_BuildValue("i", self->bits_per_sample);
}

static PyObject*
WavPackDecoder_channels(decoders_WavPackDecoder *self, void *closure) {
    return Py_BuildValue("i", self->channels);
}

static PyObject*
WavPackDecoder_channel_mask(decoders_WavPackDecoder *self, void *closure) {
    return Py_BuildValue("i", self->channel_mask);
}

static PyObject*
WavPackDecoder_close(decoders_WavPackDecoder* self, PyObject *args) {
    self->remaining_samples = 0;
    Py_INCREF(Py_None);
    return Py_None;
}

status
WavPackDecoder_read_block_header(Bitstream* bitstream,
                                 struct wavpack_block_header* header) {
    /*read and verify block ID*/
    if (bitstream->read(bitstream, 32) != 0x6B707677) {
        PyErr_SetString(PyExc_ValueError, "invalid block ID");
        return ERROR;
    }

    header->block_size = bitstream->read(bitstream, 32);
    header->version = bitstream->read(bitstream, 16);
    header->track_number = bitstream->read(bitstream, 8);
    header->index_number = bitstream->read(bitstream, 8);
    header->total_samples = bitstream->read_signed(bitstream, 32);
    header->block_index = bitstream->read(bitstream, 32);
    header->block_samples = bitstream->read(bitstream, 32);

    switch (bitstream->read(bitstream, 2)) {
    case 0: header->bits_per_sample = 8; break;
    case 1: header->bits_per_sample = 16; break;
    case 2: header->bits_per_sample = 24; break;
    case 3: header->bits_per_sample = 32; break;
    default: break; /*can't happen*/
    }

    header->mono_output = bitstream->read(bitstream, 1);
    header->hybrid_mode = bitstream->read(bitstream, 1);
    header->joint_stereo = bitstream->read(bitstream, 1);
    header->cross_channel_decorrelation = bitstream->read(bitstream, 1);
    header->hybrid_noise_shaping = bitstream->read(bitstream, 1);
    header->floating_point_data = bitstream->read(bitstream, 1);
    header->extended_size_integers = bitstream->read(bitstream, 1);
    header->hybrid_parameters_control_bitrate = bitstream->read(bitstream, 1);
    header->hybrid_noise_balanced = bitstream->read(bitstream, 1);
    header->initial_block_in_sequence = bitstream->read(bitstream, 1);
    header->final_block_in_sequence = bitstream->read(bitstream, 1);
    header->left_shift = bitstream->read(bitstream, 5);
    header->maximum_data_magnitude = bitstream->read(bitstream, 5);

    switch (bitstream->read(bitstream, 4)) {
    case 0x0: header->sample_rate =   6000; break;
    case 0x1: header->sample_rate =   8000; break;
    case 0x2: header->sample_rate =   9600; break;
    case 0x3: header->sample_rate =  11025; break;
    case 0x4: header->sample_rate =  12000; break;
    case 0x5: header->sample_rate =  16000; break;
    case 0x6: header->sample_rate =  22050; break;
    case 0x7: header->sample_rate =  24000; break;
    case 0x8: header->sample_rate =  32000; break;
    case 0x9: header->sample_rate =  41000; break;
    case 0xA: header->sample_rate =  48000; break;
    case 0xB: header->sample_rate =  64000; break;
    case 0xC: header->sample_rate =  88200; break;
    case 0xD: header->sample_rate =  96000; break;
    case 0xE: header->sample_rate = 192000; break;
    case 0xF: header->sample_rate =      0; break; /*reserved*/
    }

    bitstream->read(bitstream, 2);
    header->use_IIR = bitstream->read(bitstream, 1);
    header->false_stereo = bitstream->read(bitstream, 1);

    if (bitstream->read(bitstream, 1) != 0) {
        PyErr_SetString(PyExc_ValueError, "invalid reserved bit");
        return ERROR;
    }

    header->crc = bitstream->read(bitstream, 32);

    return OK;
}

void
WavPackDecoder_read_subblock_header(Bitstream* bitstream,
                                    struct wavpack_subblock_header* header) {
    header->metadata_function = bitstream->read(bitstream, 5);
    header->nondecoder_data = bitstream->read(bitstream, 1);
    header->actual_size_1_less = bitstream->read(bitstream, 1);
    header->large_block = bitstream->read(bitstream, 1);
    header->block_size = bitstream->read(bitstream,
                                         header->large_block ? 24 : 8);
}

status
WavPackDecoder_read_decorr_terms(Bitstream* bitstream,
                                 struct wavpack_subblock_header* header,
                                 struct i_array* decorr_terms,
                                 struct i_array* decorr_deltas) {
    int term_count = (header->block_size * 2) -
        (header->actual_size_1_less ? 1 : 0);
    int decorr_term;

    if (term_count > MAXIMUM_TERM_COUNT) {
        PyErr_SetString(PyExc_ValueError, "excessive term count");
        return ERROR;
    }

    ia_reset(decorr_terms);
    ia_reset(decorr_deltas);

    for (; term_count > 0; term_count--) {
        decorr_term = bitstream->read(bitstream, 5) - 5;
        switch (decorr_term) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 17:
        case 18:
        case -1:
        case -2:
        case -3:
            /* valid terms */
            break;
        default:
            /* anything else is invalid*/
            PyErr_SetString(PyExc_ValueError, "invalid decorrelation term");
            return ERROR;
        }
        ia_append(decorr_terms, decorr_term);
        ia_append(decorr_deltas, bitstream->read(bitstream, 3));
    }

    if (header->actual_size_1_less)
        bitstream->read(bitstream, 8);

    ia_reverse(decorr_terms);
    ia_reverse(decorr_deltas);

    return OK;
}

int
WavPackDecoder_restore_weight(int weight) {
    if (weight > 0) {
        return (weight << 3) + (((weight << 3) + 64) >> 7);
    } else {
        return weight << 3;
    }
}

status
WavPackDecoder_read_decorr_weights(Bitstream* bitstream,
                                   struct wavpack_subblock_header* header,
                                   int block_channel_count,
                                   int term_count,
                                   struct i_array* weights_A,
                                   struct i_array* weights_B) {
    int weight_pairs = (((header->block_size * 2) -
                         (header->actual_size_1_less ? 1 : 0)) /
                        block_channel_count);

    ia_reset(weights_A);
    ia_reset(weights_B);

    for(; weight_pairs > 0; weight_pairs--, term_count--) {
        ia_append(weights_A,
                  WavPackDecoder_restore_weight(
                        bitstream->read_signed(bitstream, 8)));
        if (block_channel_count > 1)
            ia_append(weights_B,
                      WavPackDecoder_restore_weight(
                            bitstream->read_signed(bitstream, 8)));
        else
            ia_append(weights_B, 0);
    }

    if (header->actual_size_1_less)
        bitstream->read(bitstream, 8);

    for(; term_count > 0; term_count--) {
        ia_append(weights_A, 0);
        ia_append(weights_B, 0);
    }

    ia_reverse(weights_A);
    ia_reverse(weights_B);

    return OK;
}

static int wavpack_exp2(int log) {
    int value;
    static const uint8_t exp2_table[] = {
        0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05,
        0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x10,
        0x11, 0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x16,
        0x17, 0x18, 0x19, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
        0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x24, 0x25, 0x26, 0x27, 0x28, 0x28, 0x29,
        0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x3a, 0x3a, 0x3b, 0x3c, 0x3d,
        0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44,
        0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b,
        0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x51, 0x52,
        0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5b, 0x5c, 0x5d, 0x5e, 0x5e, 0x5f, 0x60, 0x61,
        0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
        0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
        0x82, 0x83, 0x84, 0x85, 0x87, 0x88, 0x89, 0x8a,
        0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92,
        0x93, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
        0x9c, 0x9d, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0xa5, 0xa6, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad,
        0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb6, 0xb7,
        0xb8, 0xb9, 0xba, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
        0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc8, 0xc9, 0xca,
        0xcb, 0xcd, 0xce, 0xcf, 0xd0, 0xd2, 0xd3, 0xd4,
        0xd6, 0xd7, 0xd8, 0xd9, 0xdb, 0xdc, 0xdd, 0xde,
        0xe0, 0xe1, 0xe2, 0xe4, 0xe5, 0xe6, 0xe8, 0xe9,
        0xea, 0xec, 0xed, 0xee, 0xf0, 0xf1, 0xf2, 0xf4,
        0xf5, 0xf6, 0xf8, 0xf9, 0xfa, 0xfc, 0xfd, 0xff};

    if (log < 0) {
        return -wavpack_exp2(-log);
    }

    value = exp2_table[log & 0xFF] | 0x100;
    log >>= 8;
    if (log <= 9) {
        return (value >> (9 - log));
    } else {
        return (value << (log - 9));
    }
}

status
WavPackDecoder_read_decorr_samples(Bitstream* bitstream,
                                   struct wavpack_subblock_header* header,
                                   int block_channel_count,
                                   struct i_array* decorr_terms,
                                   struct ia_array* samples_A,
                                   struct ia_array* samples_B) {
    int i;
    int values_remaining;
    int term;
    int j;

    iaa_reset(samples_A);
    iaa_reset(samples_B);

    for (i = decorr_terms->size - 1, values_remaining = header->block_size;
         (i >= 0) && (values_remaining > 0);
         i--) {
        switch (term = decorr_terms->data[i]) {
        case 18:
        case 17:
            ia_append(&(samples_A->arrays[i]),
                      wavpack_exp2(bitstream->read_signed(bitstream, 16)));
            ia_append(&(samples_A->arrays[i]),
                      wavpack_exp2(bitstream->read_signed(bitstream, 16)));
            values_remaining -= 2;
            if (block_channel_count > 1) {
                ia_append(&(samples_B->arrays[i]),
                      wavpack_exp2(bitstream->read_signed(bitstream, 16)));
                ia_append(&(samples_B->arrays[i]),
                          wavpack_exp2(bitstream->read_signed(bitstream, 16)));
                values_remaining -= 2;
            } else {
                ia_append(&(samples_B->arrays[i]), 0);
                ia_append(&(samples_B->arrays[i]), 0);
            }
            for (j = 2; j < 8; j++) {
                /*pad samples_A and samples_B with 0s*/
                ia_append(&(samples_A->arrays[i]), 0);
                ia_append(&(samples_B->arrays[i]), 0);
            }
            break;
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            for (j = 0; j < term; j++) {
                ia_append(&(samples_A->arrays[i]),
                          wavpack_exp2(bitstream->read_signed(bitstream, 16)));
                values_remaining--;
                if (block_channel_count > 1) {
                    ia_append(&(samples_B->arrays[i]),
                          wavpack_exp2(bitstream->read_signed(bitstream, 16)));
                    values_remaining--;
                }
            }
            for (;j < 8; j++) {
                ia_append(&(samples_A->arrays[i]), 0);
                ia_append(&(samples_B->arrays[i]), 0);
            }
            break;
        case -1:
        case -2:
        case -3:
            ia_append(&(samples_A->arrays[i]),
                      wavpack_exp2(bitstream->read_signed(bitstream, 16)));
            ia_append(&(samples_B->arrays[i]),
                      wavpack_exp2(bitstream->read_signed(bitstream, 16)));
            values_remaining -= 2;
            for (j = 1; j < 8; j++) {
                /*pad samples_A and samples_B with 0s*/
                ia_append(&(samples_A->arrays[i]), 0);
                ia_append(&(samples_B->arrays[i]), 0);
            }
            break;
        default:
            PyErr_SetString(PyExc_ValueError, "invalid decorrelation term");
            return ERROR;
        }
    }

    /*pad remaining terms with 0s*/
    for (; i >= 0; i--) {
        for (j = 0; j < 8; j++) {
            ia_append(&(samples_A->arrays[i]), 0);
            ia_append(&(samples_B->arrays[i]), 0);
        }
    }

    return OK;
}

static PyObject*
i_array_to_list(struct i_array *list)
{
    PyObject* toreturn;
    PyObject* item;
    ia_size_t i;

    if ((toreturn = PyList_New(0)) == NULL)
        return NULL;
    else {
        for (i = 0; i < list->size; i++) {
            item = PyInt_FromLong(list->data[i]);
            PyList_Append(toreturn, item);
            Py_DECREF(item);
        }
        return toreturn;
    }
}

static PyObject*
ia_array_to_list(struct ia_array *list)
{
    PyObject* toreturn;
    PyObject* item;
    ia_size_t i;

    if ((toreturn = PyList_New(0)) == NULL)
        return NULL;
    else {
        for (i = 0; i < list->size; i++) {
            item = i_array_to_list(&(list->arrays[i]));
            PyList_Append(toreturn, item);
            Py_DECREF(item);
        }
        return toreturn;
    }
}

/*as with Shorten, whose analyze_frame() returns the next command
  (likely only part of a total collection of PCM frames),
  this returns a single block which may be only one of several
  needed to reconstruct a multichannel set of audio*/

static PyObject*
WavPackDecoder_analyze_frame(decoders_WavPackDecoder* self, PyObject *args) {
    struct wavpack_block_header block_header;
    long block_end;
    PyObject* subblocks = PyList_New(0);
    long position;

    if (self->remaining_samples > 0) {
        position = ftell(self->bitstream->file);

        /*FIXME - check for EOFs here*/
        if (WavPackDecoder_read_block_header(self->bitstream,
                                             &block_header) == OK) {
            block_end = ftell(self->bitstream->file) +
                block_header.block_size - 24;
            while (ftell(self->bitstream->file) < block_end) {
                PyList_Append(subblocks,
                              WavPackDecoder_analyze_subblock(self,
                                                              &block_header));
            }

            self->remaining_samples -= block_header.block_samples;
            return Py_BuildValue(
                    "{sl sI sI si si si sI sI "
                    "si si si si si si si si si si si si si si si "
                    "si si sI sO}",
                    "offset", position,
                    "block_size", block_header.block_size,
                    "version", block_header.version,
                    "track_number", block_header.track_number,
                    "index_number", block_header.index_number,
                    "total_samples", block_header.total_samples,
                    "block_index", block_header.block_index,
                    "block_samples", block_header.block_samples,

                    "bits_per_sample", block_header.bits_per_sample,
                    "mono_output", block_header.mono_output,
                    "hybrid_mode", block_header.hybrid_mode,
                    "joint_stereo", block_header.joint_stereo,
                    "cross_channel_decorrelation",
                    block_header.cross_channel_decorrelation,
                    "hybrid_noise_shaping",
                    block_header.hybrid_noise_shaping,
                    "floating_point_data",
                    block_header.floating_point_data,
                    "extended_size_integers",
                    block_header.extended_size_integers,
                    "hybrid_parameters_control_bitrate",
                    block_header.hybrid_parameters_control_bitrate,
                    "hybrid_noise_balanced",
                    block_header.hybrid_noise_balanced,
                    "initial_block_in_sequence",
                    block_header.initial_block_in_sequence,
                    "final_block_in_sequence",
                    block_header.final_block_in_sequence,
                    "left_shift", block_header.left_shift,
                    "maximum_data_magnitude",
                    block_header.maximum_data_magnitude,
                    "sample_rate", block_header.sample_rate,

                    "use_IIR", block_header.use_IIR,
                    "false_stereo", block_header.false_stereo,
                    "crc", block_header.crc,
                    "sub_blocks", subblocks);

        } else {
            return NULL;
        }
    } else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

PyObject*
WavPackDecoder_analyze_subblock(decoders_WavPackDecoder* self,
                                struct wavpack_block_header* block_header) {
    Bitstream* bitstream = self->bitstream;
    struct wavpack_subblock_header header;
    unsigned char* subblock_data = NULL;
    size_t data_size;
    PyObject* subblock_data_obj;

    /*FIXME - catch EOF here*/
    WavPackDecoder_read_subblock_header(bitstream, &header);

    switch (header.metadata_function | (header.nondecoder_data << 5)) {
    case WV_DECORR_TERMS:
        if (WavPackDecoder_read_decorr_terms(bitstream,
                                             &header,
                                             &(self->decorr_terms),
                                             &(self->decorr_deltas)) == OK) {
            subblock_data_obj = Py_BuildValue(
                                    "{sO sO}",
                                    "decorr_terms",
                                    i_array_to_list(&(self->decorr_terms)),
                                    "decorr_deltas",
                                    i_array_to_list(&(self->decorr_deltas)));
        } else
            return NULL;
        break;
    case WV_DECORR_WEIGHTS:
        if (WavPackDecoder_read_decorr_weights(
                                bitstream,
                                &header,
                                block_header->mono_output ? 1 : 2,
                                self->decorr_terms.size,
                                &(self->decorr_weights_A),
                                &(self->decorr_weights_B)) == OK) {
            subblock_data_obj = Py_BuildValue(
                                "{sO sO}",
                                "decorr_weights_A",
                                i_array_to_list(&(self->decorr_weights_A)),
                                "decorr_weights_B",
                                i_array_to_list(&(self->decorr_weights_B)));
        } else
            return NULL;
        break;
    case WV_DECORR_SAMPLES:
        if (WavPackDecoder_read_decorr_samples(
                                bitstream,
                                &header,
                                block_header->mono_output ? 1 : 2,
                                &(self->decorr_terms),
                                &(self->decorr_samples_A),
                                &(self->decorr_samples_B)) == OK) {
            subblock_data_obj = Py_BuildValue(
                                "{sO sO}",
                                "decorr_samples_A",
                                ia_array_to_list(&(self->decorr_samples_A)),
                                "decorr_samples_B",
                                ia_array_to_list(&(self->decorr_samples_B)));
        } else
            return NULL;
        break;
    default:
        /*return a binary string for unknown subblock types*/
        data_size = header.block_size * 2;
        subblock_data = malloc(data_size);
        if (fread(subblock_data,
                  sizeof(unsigned char),
                  data_size,
                  bitstream->file) != data_size) {
            PyErr_SetString(PyExc_IOError, "I/O error reading stream");
            free(subblock_data);
            return NULL;
        } else {
            subblock_data_obj = PyString_FromStringAndSize(
                        (char*)subblock_data,
                        data_size - (header.actual_size_1_less ? 1 : 0));
            free(subblock_data);
        }
    }


    return Py_BuildValue("{sI sI sI sI sI sO}",
                         "metadata_function", header.metadata_function,
                         "nondecoder_data", header.nondecoder_data,
                         "actual_size_1_less", header.actual_size_1_less,
                         "large_block", header.large_block,
                         "sub_block_size", header.block_size,
                         "sub_block_data", subblock_data_obj);
}

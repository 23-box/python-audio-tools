#include "alac.h"
#include "../pcm.h"

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

int
ALACDecoder_init(decoders_ALACDecoder *self,
                 PyObject *args, PyObject *kwds)
{
    char *filename;
    static char *kwlist[] = {"filename", NULL};
    unsigned i;

    self->filename = NULL;
    self->file = NULL;
    self->bitstream = NULL;
    self->audiotools_pcm = NULL;

    self->frameset_channels = array_ia_new();
    self->frame_channels = array_ia_new();
    self->uncompressed_LSBs = array_i_new();
    self->residuals = array_i_new();

    for (i = 0; i < MAX_CHANNELS; i++) {
        self->subframe_headers[i].qlp_coeff = array_i_new();
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &filename))
        return -1;

    /*open the alac file as a BitstreamReader*/
    if ((self->file = fopen(filename, "rb")) == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
        return -1;
    } else {
        self->bitstream = br_open(self->file, BS_BIG_ENDIAN);
    }
    self->filename = strdup(filename);

    self->bitstream->mark(self->bitstream);

    if (alacdec_parse_decoding_parameters(self)) {
        self->bitstream->unmark(self->bitstream);
        return -1;
    } else {
        self->bitstream->rewind(self->bitstream);
    }

    /*seek to the 'mdat' atom, which contains the ALAC stream*/
    if (alacdec_seek_mdat(self->bitstream) == ERROR) {
        self->bitstream->unmark(self->bitstream);
        PyErr_SetString(PyExc_ValueError,
                        "Unable to locate 'mdat' atom in stream");
        return -1;
    } else {
        self->bitstream->unmark(self->bitstream);
    }

    /*setup a framelist generator function*/
    if ((self->audiotools_pcm =
         PyImport_ImportModule("audiotools.pcm")) == NULL)
        return -1;

    return 0;
}

void
ALACDecoder_dealloc(decoders_ALACDecoder *self)
{
    int i;

    if (self->filename != NULL)
        free(self->filename);

    if (self->bitstream != NULL)
        /*this closes self->file also*/
        self->bitstream->close(self->bitstream);

    for (i = 0; i < MAX_CHANNELS; i++)
        self->subframe_headers[i].qlp_coeff->del(
            self->subframe_headers[i].qlp_coeff);

    self->frameset_channels->del(self->frameset_channels);
    self->frame_channels->del(self->frame_channels);
    self->uncompressed_LSBs->del(self->uncompressed_LSBs);
    self->residuals->del(self->residuals);

    Py_XDECREF(self->audiotools_pcm);

    self->ob_type->tp_free((PyObject*)self);
}

int
alacdec_parse_decoding_parameters(decoders_ALACDecoder *self)
{
    BitstreamReader* mdia_atom = br_substream_new(BS_BIG_ENDIAN);
    BitstreamReader* stsd_atom = br_substream_new(BS_BIG_ENDIAN);
    BitstreamReader* mdhd_atom = br_substream_new(BS_BIG_ENDIAN);
    uint32_t mdia_atom_size;
    uint32_t stsd_atom_size;
    uint32_t mdhd_atom_size;
    unsigned int total_frames;

    /*find the mdia atom, which is the parent to stsd and mdhd*/
    if (find_sub_atom(self->bitstream, mdia_atom, &mdia_atom_size,
                      "moov", "trak", "mdia", NULL)) {
        PyErr_SetString(PyExc_ValueError, "unable to find mdia atom");
        goto error;
    } else {
        /*mark the mdia atom so we can parse
          two different trees from it*/
        mdia_atom->mark(mdia_atom);
    }

    /*find the stsd atom, which contains the alac atom*/
    if (find_sub_atom(mdia_atom, stsd_atom, &stsd_atom_size,
                      "minf", "stbl", "stsd", NULL)) {
        mdia_atom->unmark(mdia_atom);
        PyErr_SetString(PyExc_ValueError, "unable to find sdsd atom");
        goto error;
    }

    /*parse the alac atom, which contains lots of crucial decoder details*/
    switch (read_alac_atom(stsd_atom,
                           &(self->max_samples_per_frame),
                           &(self->bits_per_sample),
                           &(self->history_multiplier),
                           &(self->initial_history),
                           &(self->maximum_k),
                           &(self->channels),
                           &(self->sample_rate))) {
    case 1:
        mdia_atom->unmark(mdia_atom);
        PyErr_SetString(PyExc_IOError, "I/O error reading alac atom");
        goto error;
    case 2:
        mdia_atom->unmark(mdia_atom);
        PyErr_SetString(PyExc_ValueError, "invalid alac atom");
        goto error;
    default:
        break;
    }

    /*find the mdhd atom*/
    mdia_atom->rewind(mdia_atom);
    if (find_sub_atom(mdia_atom, mdhd_atom, &mdhd_atom_size,
                      "mdhd", NULL)) {
        mdia_atom->unmark(mdia_atom);
        PyErr_SetString(PyExc_ValueError, "unable to find mdhd atom");
        goto error;
    } else {
        mdia_atom->unmark(mdia_atom);
    }

    /*parse the mdhd atom, which contains our total frame count*/
    switch (read_mdhd_atom(mdhd_atom, &total_frames)) {
    case 1:
        PyErr_SetString(PyExc_IOError, "I/O error reading mdhd atom");
        goto error;
    case 2:
        PyErr_SetString(PyExc_ValueError, "invalid mdhd atom");
        goto error;
    default:
        self->remaining_frames = total_frames;
        break;
    }

    mdia_atom->close(mdia_atom);
    stsd_atom->close(stsd_atom);
    mdhd_atom->close(mdhd_atom);

    return 0;

 error:
    mdia_atom->close(mdia_atom);
    stsd_atom->close(stsd_atom);
    mdhd_atom->close(mdhd_atom);
    return -1;
}

PyObject*
ALACDecoder_new(PyTypeObject *type,
                PyObject *args, PyObject *kwds)
{
    decoders_ALACDecoder *self;

    self = (decoders_ALACDecoder *)type->tp_alloc(type, 0);

    return (PyObject *)self;
}

static PyObject*
ALACDecoder_sample_rate(decoders_ALACDecoder *self, void *closure)
{
    return Py_BuildValue("I", self->sample_rate);
}

static PyObject*
ALACDecoder_bits_per_sample(decoders_ALACDecoder *self, void *closure)
{
    return Py_BuildValue("I", self->bits_per_sample);
}

static PyObject*
ALACDecoder_channels(decoders_ALACDecoder *self, void *closure)
{
    return Py_BuildValue("I", self->channels);
}

static PyObject*
ALACDecoder_channel_mask(decoders_ALACDecoder *self, void *closure)
{
    switch (self->channels) {
    case 1:
        return Py_BuildValue("I", 0x4);
    case 2:
        return Py_BuildValue("I", 0x3);
    default:
        return Py_BuildValue("I", 0x0);
    }
}


static PyObject*
ALACDecoder_read(decoders_ALACDecoder* self, PyObject *args)
{
    unsigned channel_count;
    BitstreamReader* mdat = self->bitstream;
    array_ia* frameset_channels = self->frameset_channels;
    PyThreadState *thread_state;
    pcm_FrameList *framelist;

    /*return an empty framelist if total samples are exhausted*/
    if (self->remaining_frames == 0) {
        framelist = (pcm_FrameList*)PyObject_CallMethod(self->audiotools_pcm,
                                                        "__blank__", NULL);
        framelist->frames = 0;
        framelist->channels = self->channels;
        framelist->bits_per_sample = self->bits_per_sample;
        framelist->samples_length = 0;
        return (PyObject*)framelist;
    }

    thread_state = PyEval_SaveThread();

    if (!setjmp(*br_try(mdat))) {
        frameset_channels->reset(frameset_channels);

        /*get initial frame's channel count*/
        channel_count = mdat->read(mdat, 3) + 1;
        while (channel_count != 8) {
            /*read a frame from the frameset into "channels"*/
            if (alacdec_read_frame(self, mdat,
                                   frameset_channels, channel_count) != OK) {
                br_etry(mdat);
                PyEval_RestoreThread(thread_state);
                return NULL;
            } else {
                /*ensure all frames have the same sample count*/
                /*FIXME*/

                /*read the channel count of the next frame
                  in the frameset, if any*/
                channel_count = mdat->read(mdat, 3) + 1;
            }
        }

        /*once all the frames in the frameset are read,
          byte-align the output stream*/
        mdat->byte_align(mdat);
        br_etry(mdat);
        PyEval_RestoreThread(thread_state);

        /*decrement the remaining sample count*/
        self->remaining_frames -= MIN(self->remaining_frames,
                                      frameset_channels->data[0]->size);

        /*finally, build and return framelist object from the sample data*/
        framelist = (pcm_FrameList*)PyObject_CallMethod(self->audiotools_pcm,
                                                        "__blank__", NULL);
        if (framelist != NULL) {
            unsigned channel;
            unsigned sample;
            array_i* channel_data;

            framelist->frames = frameset_channels->data[0]->size;
            framelist->channels = frameset_channels->size;
            framelist->bits_per_sample = self->bits_per_sample;
            framelist->samples_length = (framelist->frames *
                                         framelist->channels);
            framelist->samples = realloc(framelist->samples,
                                         framelist->samples_length *
                                         sizeof(int));

            for (channel = 0; channel < frameset_channels->size; channel++) {
                channel_data = frameset_channels->data[channel];
                for (sample = 0; sample < channel_data->size; sample++) {
                    framelist->samples[(sample * frameset_channels->size) +
                                       channel] =
                        channel_data->data[sample];
                }
            }

            return (PyObject*)framelist;
        } else {
            return NULL;
        }
    } else {
        br_etry(mdat);
        PyEval_RestoreThread(thread_state);
        PyErr_SetString(PyExc_IOError, "EOF during frame reading");
        return NULL;
    }
}

status
alacdec_read_frame(decoders_ALACDecoder *self,
                   BitstreamReader* mdat,
                   array_ia* frameset_channels,
                   unsigned channel_count)
{
    unsigned has_sample_count;
    unsigned uncompressed_LSBs;
    unsigned not_compressed;
    unsigned sample_count;

    /*read frame header*/
    mdat->skip(mdat, 16);                   /*unused*/
    has_sample_count = mdat->read(mdat, 1);
    uncompressed_LSBs = mdat->read(mdat, 2);
    not_compressed = mdat->read(mdat, 1);
    if (has_sample_count == 0)
        sample_count = self->max_samples_per_frame;
    else
        sample_count = mdat->read(mdat, 32);


    if (not_compressed == 1) {
        unsigned channel;
        unsigned i;
        array_ia* frame_channels = self->frame_channels;

        /*if uncompressed, read and return a bunch of verbatim samples*/

        frame_channels->reset(frame_channels);
        for (channel = 0; channel < channel_count; channel++)
            frame_channels->append(frame_channels);

        for (i = 0; i < sample_count; i++) {
            for (channel = 0; channel < channel_count; channel++) {
                frame_channels->data[channel]->append(
                    frame_channels->data[channel],
                    mdat->read_signed(mdat, self->bits_per_sample));
            }
        }

        frameset_channels->extend(frameset_channels, frame_channels);

        return OK;
    } else {
        unsigned interlacing_shift;
        unsigned interlacing_leftweight;
        unsigned channel;
        unsigned i;
        array_i* LSBs;
        array_i* residuals = self->residuals;
        array_ia* frame_channels = self->frame_channels;
        array_i* channel_data;

        frame_channels->reset(frame_channels);

        /*if compressed, read interlacing shift and leftweight*/
        interlacing_shift = mdat->read(mdat, 8);
        interlacing_leftweight = mdat->read(mdat, 8);

        /*read a subframe header per channel*/
        for (channel = 0; channel < channel_count; channel++) {
            alacdec_read_subframe_header(mdat,
                                         &(self->subframe_headers[channel]));
        }

        /*if uncompressed LSBs, read a block of partial samples to prepend*/
        if (uncompressed_LSBs > 0) {
            LSBs = self->uncompressed_LSBs;
            LSBs->reset(LSBs);
            for (i = 0; i < (channel_count * sample_count); i++)
                LSBs->append(LSBs,
                             mdat->read(mdat, uncompressed_LSBs * 8));
        }

        /*read a residual block per channel
          and calculate the subframe's samples*/
        for (channel = 0; channel < channel_count; channel++) {
            residuals->reset(residuals);
            alacdec_read_residuals(
                mdat,
                residuals,
                sample_count,
                self->bits_per_sample -
                (uncompressed_LSBs * 8) +
                (channel_count - 1),
                self->initial_history,
                self->history_multiplier,
                self->maximum_k);

            alacdec_decode_subframe(
                frame_channels->append(frame_channels),
                residuals,
                self->subframe_headers[channel].qlp_coeff,
                self->subframe_headers[channel].qlp_shift_needed);
        }

        /*if stereo, decorrelate channels
          according to interlacing shift and interlacing leftweight*/
        if ((channel_count == 2) && (interlacing_leftweight > 0)) {
            alacdec_decorrelate_channels(frame_channels->data[0],
                                         frame_channels->data[1],
                                         interlacing_shift,
                                         interlacing_leftweight);
        }

        /*if uncompressed LSBs, prepend partial samples to output*/
        if (uncompressed_LSBs > 0) {
            for (channel = 0; channel < channel_count; channel++) {
                channel_data = frame_channels->data[channel];
                for (i = 0; i < sample_count; i++) {
                    channel_data->data[i] = ((channel_data->data[i] <<
                                              uncompressed_LSBs * 8) |
                                             LSBs->data[(i * channel_count) +
                                                        channel]);
                }
            }
        }

        /*finally, return frame's channel data*/
        frameset_channels->extend(frameset_channels, frame_channels);

        return OK;
    }
}

static PyObject*
ALACDecoder_close(decoders_ALACDecoder* self, PyObject *args)
{
    Py_INCREF(Py_None);
    return Py_None;
}

status
alacdec_seek_mdat(BitstreamReader* alac_stream)
{
    unsigned int atom_size;
    uint8_t atom_type[4];

    if (!setjmp(*br_try(alac_stream))) {
        alac_stream->parse(alac_stream, "32u 4b", &atom_size, atom_type);
        while (memcmp(atom_type, "mdat", 4)) {
            alac_stream->skip_bytes(alac_stream, atom_size - 8);
            alac_stream->parse(alac_stream, "32u 4b", &atom_size, atom_type);
        }
        br_etry(alac_stream);
        return OK;
    } else {
        br_etry(alac_stream);
        return ERROR;
    }
}

void
alacdec_read_subframe_header(BitstreamReader *bs,
                             struct alac_subframe_header *subframe_header)
{
    unsigned predictor_coef_num;
    unsigned i;

    subframe_header->prediction_type = bs->read(bs, 4);
    subframe_header->qlp_shift_needed = bs->read(bs, 4);
    subframe_header->rice_modifier = bs->read(bs, 3);
    predictor_coef_num = bs->read(bs, 5);

    subframe_header->qlp_coeff->reset(subframe_header->qlp_coeff);
    for (i = 0; i < predictor_coef_num; i++)
        subframe_header->qlp_coeff->append(subframe_header->qlp_coeff,
                                           bs->read_signed(bs, 16));
}

/*this is the slow version*/
/*
  static inline int LOG2(int value) {
  double newvalue = trunc(log((double)value) / log((double)2));

  return (int)(newvalue);
  }
*/

/*the fast version used by ffmpeg and the "alac" decoder
  subtracts MSB zero bits from total bit size - 1,
  essentially counting the number of LSB non-zero bits, -1*/

/*my version just counts the number of non-zero bits and subtracts 1
  which is good enough for now*/
static inline int
LOG2(int value)
{
    int bits = -1;
    while (value) {
        bits++;
        value >>= 1;
    }
    return bits;
}

void
alacdec_read_residuals(BitstreamReader *bs,
                       array_i* residuals,
                       unsigned int residual_count,
                       unsigned int sample_size,
                       unsigned int initial_history,
                       unsigned int history_multiplier,
                       unsigned int maximum_k)
{
    int history = initial_history;
    unsigned int sign_modifier = 0;
    unsigned int unsigned_residual;
    unsigned int zero_block_size;
    int i, j;

    residuals->reset(residuals);
    residuals->resize(residuals, residual_count);

    for (i = 0; i < residual_count; i++) {
        /*get an unsigned residual based on "history"
          and on "sample_size" as a last resort*/
        unsigned_residual = alacdec_read_residual(
            bs,
            MIN(LOG2((history >> 9) + 3), maximum_k),
            sample_size) + sign_modifier;

        /*clear out old sign modifier, if any */
        sign_modifier = 0;

        /*change unsigned residual into a signed residual
          and append it to "residuals"*/
        if (unsigned_residual & 1) {
            a_append(residuals, -((unsigned_residual + 1) >> 1));
        } else {
            a_append(residuals, unsigned_residual >> 1);
        }

        /*then use our old unsigned residual to update "history"*/
        if (unsigned_residual > 0xFFFF)
            history = 0xFFFF;
        else
            history += ((unsigned_residual * history_multiplier) -
                        ((history * history_multiplier) >> 9));

        /*if history gets too small, we may have a block of 0 samples
          which can be compressed more efficiently*/
        if ((history < 128) && ((i + 1) < residual_count)) {
            zero_block_size = alacdec_read_residual(
                bs,
                MIN(7 - LOG2(history) + ((history + 16) / 64), maximum_k),
                16);
            if (zero_block_size > 0) {
                /*block of 0s found, so write them out*/
                for (j = 0; j < zero_block_size; j++) {
                    a_append(residuals, 0);
                    i++;
                }
            }

            history = 0;

            if (zero_block_size <= 0xFFFF) {
                sign_modifier = 1;
            }
        }
    }
}

#define RICE_THRESHOLD 8

unsigned
alacdec_read_residual(BitstreamReader *bs,
                      unsigned int k,
                      unsigned int sample_size)
{
    int msb;
    unsigned int lsb;

    /*read a unary 0 value to a maximum of RICE_THRESHOLD (8)*/
    if ((msb = bs->read_limited_unary(bs, 0, RICE_THRESHOLD + 1)) == -1) {
        /*we've exceeded the maximum number of 1 bits,
          so return an unencoded value*/
        return bs->read(bs, sample_size);
    } else if (k == 0) {
        /*no least-significant bits to read, so return most-significant bits*/
        return (unsigned int)msb;
    } else {
        /*read a set of least-significant bits*/
        lsb = bs->read(bs, k);
        if (lsb > 1) {
            /*if > 1, combine with MSB and return*/
            return (msb * ((1 << k) - 1)) + (lsb - 1);
        } else if (lsb == 1) {
            /*if = 1, unread single 1 bit and return shifted MSB*/
            bs->unread(bs, 1);
            return msb * ((1 << k) - 1);
        } else {
            /*if = 0, unread single 0 bit and return shifted MSB*/
            bs->unread(bs, 0);
            return msb * ((1 << k) - 1);
        }
    }
}

static inline int
SIGN_ONLY(int value)
{
    if (value > 0)
        return 1;
    else if (value < 0)
        return -1;
    else
        return 0;
}

void
alacdec_decode_subframe(array_i* samples,
                        array_i* residuals,
                        array_i* qlp_coeff,
                        uint8_t qlp_shift_needed)
{
    int* residuals_data = residuals->data;
    int base_sample;
    int residual;
    int64_t lpc_sum;
    int output_value;
    int diff;
    int sign;
    int i = 0;
    int j;

    samples->reset(samples);

    /*first sample always copied verbatim*/
    samples->append(samples, residuals_data[i++]);

    /*grab a number of warm-up samples equal to coefficients' length*/
    for (j = 0; j < qlp_coeff->size; j++) {
        /*these are adjustments to the previous sample
          rather than copied verbatim*/
        samples->append(samples, residuals_data[i] + samples->data[i - 1]);
        i++;
    }

    /*then calculate a new sample per remaining residual*/
    for (; i < residuals->size; i++) {
        residual = residuals_data[i];
        lpc_sum = 1 << (qlp_shift_needed - 1);

        /*Note that base_sample gets stripped from previously encoded samples
          then re-added prior to adding the next sample.
          It's a watermark sample, of sorts.*/
        base_sample = samples->data[i - (qlp_coeff->size + 1)];

        for (j = 0; j < qlp_coeff->size; j++) {
            lpc_sum += ((int64_t)qlp_coeff->data[j] *
                        (int64_t)(samples->data[i - j - 1] - base_sample));
        }

        /*sample = ((sum + 2 ^ (quant - 1)) / (2 ^ quant)) +
          residual + base_sample*/
        lpc_sum >>= qlp_shift_needed;
        lpc_sum += base_sample;
        output_value = (int)(residual + lpc_sum);
        samples->append(samples, output_value);

        /*At this point, except for base_sample, everything looks a lot like
          a FLAC LPC subframe.
          We're not done yet, though.
          ALAC's adaptive algorithm then adjusts the QLP coefficients
          up or down 1 step based on previously decoded samples
          and the residual*/

        if (residual > 0) {
            for (j = 0; j < qlp_coeff->size; j++) {
                diff = base_sample - samples->data[i - qlp_coeff->size + j];
                sign = SIGN_ONLY(diff);
                qlp_coeff->data[qlp_coeff->size - j - 1] -= sign;
                residual -= (((diff * sign) >> qlp_shift_needed) *
                             (j + 1));
                if (residual <= 0)
                    break;
            }
        } else if (residual < 0) {
            for (j = 0; j < qlp_coeff->size; j++) {
                diff = base_sample - samples->data[i - qlp_coeff->size + j];
                sign = SIGN_ONLY(diff);
                qlp_coeff->data[qlp_coeff->size - j - 1] += sign;
                residual -= (((diff * -sign) >> qlp_shift_needed) *
                             (j + 1));
                if (residual >= 0)
                    break;
            }
        }
    }
}

void
alacdec_decorrelate_channels(array_i* left,
                             array_i* right,
                             unsigned interlacing_shift,
                             unsigned interlacing_leftweight)
{
    unsigned size = left->size;
    unsigned i;
    int ch0_s;
    int ch1_s;
    int left_s;
    int right_s;

    for (i = 0; i < size; i++) {
        ch0_s = left->data[i];
        ch1_s = right->data[i];

        right_s = ch0_s - ((ch1_s * (int)interlacing_leftweight) >>
                           (int)interlacing_shift);
        left_s = ch1_s + right_s;

        left->data[i]  = left_s;
        right->data[i] = right_s;
    }
}

int
find_atom(BitstreamReader* parent,
          BitstreamReader* sub_atom, uint32_t* sub_atom_size,
          const char* sub_atom_name)
{
    uint32_t atom_size;
    uint8_t atom_name[4];

    if (!setjmp(*br_try(parent))) {
        atom_size = parent->read(parent, 32);
        parent->read_bytes(parent, atom_name, 4);
        while (memcmp(atom_name, sub_atom_name, 4)) {
            parent->skip_bytes(parent, atom_size - 8);
            atom_size = parent->read(parent, 32);
            parent->read_bytes(parent, atom_name, 4);
        }

        parent->substream_append(parent, sub_atom, atom_size - 8);
        *sub_atom_size = atom_size - 8;

        br_etry(parent);
        return 0;
    } else {
        br_etry(parent);
        return 1;
    }
}

int
find_sub_atom(BitstreamReader* parent,
              BitstreamReader* sub_atom, uint32_t* sub_atom_size,
              ...)
{
    va_list ap;
    char* sub_atom_name;
    BitstreamReader* parent_atom;
    BitstreamReader* child_atom;
    uint32_t child_atom_size;

    va_start(ap, sub_atom_size);

    sub_atom_name = va_arg(ap, char*);
    if (sub_atom_name == NULL) {
        /*no sub-atoms at all, so return 1 rather than 0*/
        va_end(ap);
        return 1;
    } else {
        /*at least 1 sub-atom*/
        parent_atom = br_substream_new(BS_BIG_ENDIAN);
        child_atom = br_substream_new(BS_BIG_ENDIAN);

        /*first, try to find the sub-atom from our original parent*/
        if (find_atom(parent, child_atom, &child_atom_size, sub_atom_name)) {
            child_atom->close(child_atom);
            parent_atom->close(parent_atom);

            va_end(ap);
            return 1;
        }

        /*then, so long as there's still sub-atom names*/
        for (sub_atom_name = va_arg(ap, char*);
             sub_atom_name != NULL;
             sub_atom_name = va_arg(ap, char*)) {
            swap_readers(&parent_atom, &child_atom);
            br_substream_reset(child_atom);

            /*recursively find sub-atoms*/
            if (find_atom(parent_atom, child_atom, &child_atom_size,
                           sub_atom_name)) {
                /*unless one of the sub-atoms is not found*/
                child_atom->close(child_atom);
                parent_atom->close(parent_atom);

                va_end(ap);
                return 1;
            }
        }

        /*otherwise, return the found atom*/
        child_atom->substream_append(child_atom, sub_atom, child_atom_size);
        *sub_atom_size = child_atom_size;
        va_end(ap);
        return 0;
    }
}

void
swap_readers(BitstreamReader** a, BitstreamReader** b)
{
    BitstreamReader* c = *a;
    *a = *b;
    *b = c;
}

int
read_alac_atom(BitstreamReader* stsd_atom,
               unsigned int* max_samples_per_frame,
               unsigned int* bits_per_sample,
               unsigned int* history_multiplier,
               unsigned int* initial_history,
               unsigned int* maximum_k,
               unsigned int* channels,
               unsigned int* sample_rate)
{
    unsigned int stsd_version;
    unsigned int stsd_descriptions;
    uint8_t alac1[4];
    uint8_t alac2[4];

    if (!setjmp(*br_try(stsd_atom))) {
        stsd_atom->parse(stsd_atom,
                         "8u 24p 32u"
                         "32p 4b 6P 16p 16p 16p 4P 16p 16p 16p 16p 4P"
                         "32p 4b 4P 32u 8p 8u 8u 8u 8u 8u 16p 32p 32p 32u",
                         &stsd_version, &stsd_descriptions, alac1, alac2,
                         max_samples_per_frame, bits_per_sample,
                         history_multiplier, initial_history,
                         maximum_k, channels, sample_rate);
        br_etry(stsd_atom);

        if (memcmp(alac1, "alac", 4) || memcmp(alac2, "alac", 4))
            return 2;
        else
            return 0;
    } else {
        br_etry(stsd_atom);
        return 1;
    }
}

int
read_mdhd_atom(BitstreamReader* mdhd_atom,
               unsigned int* total_frames)
{
    unsigned int version;

    if (!setjmp(*br_try(mdhd_atom))) {
        mdhd_atom->parse(mdhd_atom, "8u 24p", &version);

        if (version == 0) {
            mdhd_atom->parse(mdhd_atom, "32p 32p 32p 32u 2P 16p", total_frames);
            br_etry(mdhd_atom);
            return 0;
        } else {
            br_etry(mdhd_atom);
            return 2;
        }
    } else {
        br_etry(mdhd_atom);
        return 1;
    }
}

#include "tta.h"
#include "../common/tta_crc.h"
#include "../pcm_conv.h"
#include "../pcmconv.h"
#include "../framelist.h"
#include <string.h>

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2015  Brian Langenberger

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

/*returns x/y rounded up
  each argument is evaluated twice*/
#ifndef DIV_CEIL
#define DIV_CEIL(x, y) ((x) / (y) + (((x) % (y)) ? 1 : 0))
#endif

#ifndef STANDALONE
PyObject*
TTADecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    decoders_TTADecoder *self;

    self = (decoders_TTADecoder *)type->tp_alloc(type, 0);

    return (PyObject *)self;
}

int
TTADecoder_init(decoders_TTADecoder *self, PyObject *args, PyObject *kwds) {
    PyObject *file;

    /*initialize temporary buffers*/
    self->total_tta_frames = 0;
    self->current_tta_frame = 0;
    self->seektable = NULL;

    init_cache(&(self->cache));

    self->closed = 1;

    self->bitstream = NULL;
    self->framelist = aa_int_new();

    self->audiotools_pcm = NULL;

    self->frames_start = NULL;

    if (!PyArg_ParseTuple(args, "O", &file)) {
        return -1;
    } else {
        Py_INCREF(file);
    }

    /*open the TTA file*/
    self->bitstream = br_open_external(
        file,
        BS_LITTLE_ENDIAN,
        4096,
        (ext_read_f)br_read_python,
        (ext_setpos_f)bs_setpos_python,
        (ext_getpos_f)bs_getpos_python,
        (ext_free_pos_f)bs_free_pos_python,
        (ext_seek_f)bs_fseek_python,
        (ext_close_f)bs_close_python,
        (ext_free_f)bs_free_python_decref);

    switch (read_header(self->bitstream,
                        &(self->header.channels),
                        &(self->header.bits_per_sample),
                        &(self->header.sample_rate),
                        &(self->header.total_pcm_frames))) {
    default:
        self->remaining_pcm_frames = self->header.total_pcm_frames;
        break;
    case INVALID_SIGNATURE:
        PyErr_SetString(PyExc_ValueError, "invalid header signature");
        return -1;
    case UNSUPPORTED_FORMAT:
        PyErr_SetString(PyExc_ValueError, "unsupported TTA format");
        return -1;
    case CRCMISMATCH:
        PyErr_SetString(PyExc_ValueError, "CRC error reading header");
        return -1;
    case IOERROR:
        PyErr_SetString(PyExc_IOError, "I/O error reading header");
        return -1;
    }

    /*determine the default block size*/
    self->block_size = (self->header.sample_rate * 256) / 245;

    /*determine the total number of TTA frames*/
    self->total_tta_frames = DIV_CEIL(self->header.total_pcm_frames,
                                      self->block_size);

    self->seektable = malloc(sizeof(unsigned int) * self->total_tta_frames);

    switch (read_seektable(self->bitstream,
                           self->total_tta_frames,
                           self->seektable)) {
    default:
        /*do nothing*/
        break;
    case CRCMISMATCH:
        PyErr_SetString(PyExc_ValueError, "CRC error reading seektable");
        return -1;
    case IOERROR:
        PyErr_SetString(PyExc_IOError, "I/O error reading seektable");
        return -1;
    }

    /*setup a framelist generator function*/
    if ((self->audiotools_pcm = open_audiotools_pcm()) == NULL)
        return -1;

    /*place a mark after the seektable for possible rewinding
      if the stream is file-based*/
    if (!setjmp(*br_try(self->bitstream))) {
        self->frames_start = self->bitstream->getpos(self->bitstream);
        br_etry(self->bitstream);
    } else {
        br_etry(self->bitstream);
        PyErr_SetString(PyExc_IOError, "I/O error getting stream position");
        return -1;
    }

    /*mark stream as not closed and ready for reading*/
    self->closed = 0;

    return 0;
}

void
TTADecoder_dealloc(decoders_TTADecoder *self) {
    if (self->seektable != NULL)
        free(self->seektable);

    free_cache(&(self->cache));

    if (self->frames_start != NULL) {
        self->frames_start->del(self->frames_start);
    }

    if (self->bitstream != NULL) {
        self->bitstream->free(self->bitstream);
    }

    self->framelist->del(self->framelist);

    Py_XDECREF(self->audiotools_pcm);

    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
TTADecoder_sample_rate(decoders_TTADecoder *self, void *closure)
{
    return Py_BuildValue("i", self->header.sample_rate);
}

static PyObject*
TTADecoder_bits_per_sample(decoders_TTADecoder *self, void *closure)
{
    return Py_BuildValue("i", self->header.bits_per_sample);
}

static PyObject*
TTADecoder_channels(decoders_TTADecoder *self, void *closure)
{
    return Py_BuildValue("i", self->header.channels);
}

static PyObject*
TTADecoder_channel_mask(decoders_TTADecoder *self, void *closure)
{
    switch (self->header.channels) {
    case 1:
        return Py_BuildValue("i", 0x4);
    case 2:
        return Py_BuildValue("i", 0x3);
    default:
        return Py_BuildValue("i", 0);
    }
}

PyObject*
TTADecoder_read(decoders_TTADecoder* self, PyObject *args)
{
    if (self->closed) {
        PyErr_SetString(PyExc_ValueError, "cannot read closed stream");
        return NULL;
    } else if (self->remaining_pcm_frames == 0) {
        return empty_FrameList(self->audiotools_pcm,
                               self->header.channels,
                               self->header.bits_per_sample);
    } else {
        const unsigned frame_size = self->seektable[self->current_tta_frame++];
        const unsigned block_size = MIN(self->remaining_pcm_frames,
                                        self->block_size);
        BitstreamReader *frame;
        status status;
        uint32_t frame_crc = 0xFFFFFFFF;

        /*read "frame_size" - 4 bytes of data from stream*/
        self->bitstream->add_callback(self->bitstream,
                                      (bs_callback_f)tta_crc32,
                                      &frame_crc);

        if (!setjmp(*br_try(self->bitstream))) {
            frame = self->bitstream->substream(self->bitstream,
                                               frame_size - 4);
            br_etry(self->bitstream);
            self->bitstream->pop_callback(self->bitstream, NULL);
        } else {
            /*read error attempting to populate frame with bytes*/
            br_etry(self->bitstream);
            self->bitstream->pop_callback(self->bitstream, NULL);
            PyErr_SetString(PyExc_IOError, "I/O error reading frame");
            return NULL;
        }

        /*check CRC32 at end of frame prior to decoding*/
        if (!setjmp(*br_try(self->bitstream))) {
            const unsigned crc32 = self->bitstream->read(self->bitstream, 32);

            br_etry(self->bitstream);

            if ((frame_crc ^ 0xFFFFFFFF) != crc32) {
                /*CRC32 mismatch*/
                frame->close(frame);
                PyErr_SetString(PyExc_ValueError, "CRC mismatch reading frame");
                return NULL;
            }
        } else {
            /*I/O error reading CRC32*/
            frame->close(frame);
            br_etry(self->bitstream);
            PyErr_SetString(PyExc_IOError, "I/O error reading frame");
            return NULL;
        }

        status = read_frame(frame,
                            &(self->cache),
                            block_size,
                            self->header.channels,
                            self->header.bits_per_sample,
                            self->framelist);

        frame->close(frame);

        if (status != IOERROR) {
            self->remaining_pcm_frames -= block_size;
            return aa_int_to_FrameList(self->audiotools_pcm,
                                       self->framelist,
                                       self->header.bits_per_sample);
        } else {
            PyErr_SetString(PyExc_ValueError, "I/O error during frame read");
            return NULL;
        }
    }
}

static PyObject*
TTADecoder_seek(decoders_TTADecoder *self, PyObject *args)
{
    long long seeked_offset;

    if (self->closed) {
        PyErr_SetString(PyExc_ValueError, "cannot seek closed stream");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "L", &seeked_offset))
        return NULL;

    if (seeked_offset < 0) {
        PyErr_SetString(PyExc_ValueError, "cannot seek to negative value");
        return NULL;
    }

    if (!setjmp(*br_try(self->bitstream))) {
        unsigned current_pcm_frame = 0;

        /*rewind to start of TTA blocks*/
        self->bitstream->setpos(self->bitstream, self->frames_start);

        /*skip frames until we reach the requested one
          or run out of frames entirely
          and adjust both current TTA frame and
          remaining number of PCM frames according to new position*/
        self->current_tta_frame = 0;
        self->remaining_pcm_frames = self->header.total_pcm_frames;

        while ((current_pcm_frame + self->block_size) < seeked_offset) {
            const unsigned block_size = MIN(self->remaining_pcm_frames,
                                            self->block_size);
            if (block_size > 0) {
                const unsigned frame_size =
                    self->seektable[self->current_tta_frame];

                self->bitstream->seek(self->bitstream,
                                      (long)frame_size,
                                      BS_SEEK_CUR);

                current_pcm_frame += block_size;
                self->current_tta_frame++;
                self->remaining_pcm_frames -= block_size;
            } else {
                /*no additional frames to seek to*/
                break;
            }
        }

        br_etry(self->bitstream);

        /*return PCM offset actually seeked to*/
        return Py_BuildValue("I", current_pcm_frame);
    } else {
        br_etry(self->bitstream);
        PyErr_SetString(PyExc_IOError, "I/O error seeking in stream");
        return NULL;
    }

}

PyObject*
TTADecoder_close(decoders_TTADecoder* self, PyObject *args)
{
    self->closed = 1;

    self->bitstream->close_internal_stream(self->bitstream);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
TTADecoder_enter(decoders_TTADecoder* self, PyObject *args)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject*
TTADecoder_exit(decoders_TTADecoder* self, PyObject *args)
{
    self->closed = 1;

    self->bitstream->close_internal_stream(self->bitstream);

    Py_INCREF(Py_None);
    return Py_None;
}
#endif

static void
init_cache(struct tta_cache *cache)
{
    cache->k0 = a_int_new();
    cache->sum0 = a_int_new();
    cache->k1 = a_int_new();
    cache->sum1 = a_int_new();
    cache->residual = aa_int_new();
    cache->filtered = aa_int_new();
    cache->predicted = aa_int_new();
}

static void
free_cache(struct tta_cache *cache)
{
    cache->k0->del(cache->k0);
    cache->sum0->del(cache->sum0);
    cache->k1->del(cache->k1);
    cache->sum1->del(cache->sum1);
    cache->residual->del(cache->residual);
    cache->filtered->del(cache->filtered);
    cache->predicted->del(cache->predicted);
}

static status
read_header(BitstreamReader* bitstream,
            unsigned* channels,
            unsigned* bits_per_sample,
            unsigned* sample_rate,
            unsigned* total_pcm_frames)
{
    if (!setjmp(*br_try(bitstream))) {
        uint8_t signature[4];
        unsigned format;
        uint32_t header_crc = 0xFFFFFFFF;

        /*read the file header*/
        bitstream->add_callback(bitstream,
                                (bs_callback_f)tta_crc32,
                                &header_crc);
        bitstream->parse(bitstream,
                         "4b 16u 16u 16u 32u 32u",
                         signature,
                         &format,
                         channels,
                         bits_per_sample,
                         sample_rate,
                         total_pcm_frames);

        if (memcmp(signature, "TTA1", 4)) {
            br_etry(bitstream);
            return INVALID_SIGNATURE;
        } else if (format != 1) {
            br_etry(bitstream);
            return UNSUPPORTED_FORMAT;
        }

        /*check header's CRC for correctness*/
        bitstream->pop_callback(bitstream, NULL);
        if ((header_crc ^ 0xFFFFFFFF) !=
            bitstream->read(bitstream, 32)) {
            br_etry(bitstream);
            return CRCMISMATCH;
        }

        br_etry(bitstream);
        return OK;
    } else {
        br_etry(bitstream);
        return IOERROR;
    }
}

static status
read_seektable(BitstreamReader* bitstream,
               unsigned total_tta_frames,
               unsigned seektable[])
{
    if (!setjmp(*br_try(bitstream))) {
        uint32_t seektable_crc = 0xFFFFFFFF;
        unsigned i;

        /*read the seektable*/
        bitstream->add_callback(bitstream,
                                (bs_callback_f)tta_crc32,
                                &seektable_crc);
        for (i = 0; i < total_tta_frames; i++) {
            seektable[i] = bitstream->read(bitstream, 32);
        }

        /*check seektable's CRC for correctness*/
        bitstream->pop_callback(bitstream, NULL);
        if ((seektable_crc ^ 0xFFFFFFFF) != bitstream->read(bitstream, 32)) {
            br_etry(bitstream);
            return CRCMISMATCH;
        }

        br_etry(bitstream);
        return OK;
    } else {
        br_etry(bitstream);
        return IOERROR;
    }
}

static status
read_frame(BitstreamReader* frame,
           struct tta_cache* cache,
           unsigned block_size,
           unsigned channels,
           unsigned bits_per_sample,
           aa_int* framelist)
{
    unsigned c;
    a_int* k0 = cache->k0;
    a_int* sum0 = cache->sum0;
    a_int* k1 = cache->k1;
    a_int* sum1 = cache->sum1;
    aa_int* residual = cache->residual;
    aa_int* filtered = cache->filtered;
    aa_int* predicted = cache->predicted;

    k0->mset(k0, channels, 10);
    sum0->mset(sum0, channels, 1 << 14);
    k1->mset(k1, channels, 10);
    sum1->mset(sum1, channels, 1 << 14);

    /*initialize residuals for each channel*/
    residual->reset(residual);
    for (c = 0; c < channels; c++) {
        a_int* residual_ch = residual->append(residual);
        residual_ch->resize(residual_ch, block_size);
    }

    /*read residuals from bitstream*/
    if (!setjmp(*br_try(frame))) {
        unsigned i;

        for (i = 0; i < block_size; i++) {
            for (c = 0; c < channels; c++) {
                const unsigned MSB = frame->read_unary(frame, 0);
                unsigned u;

                if (MSB == 0) {
                    u = frame->read(frame, k0->_[c]);
                } else {
                    const unsigned LSB = frame->read(frame, k1->_[c]);
                    const unsigned unshifted = ((MSB - 1) << k1->_[c]) + LSB;
                    u = unshifted + (1 << k0->_[c]);
                    sum1->_[c] += unshifted - (sum1->_[c] >> 4);
                    if ((k1->_[c] > 0) &&
                        (sum1->_[c] < (1 << (k1->_[c] + 4)))) {
                        k1->_[c] -= 1;
                    } else if (sum1->_[c] > (1 << (k1->_[c] + 5))) {
                        k1->_[c] += 1;
                    }
                }
                sum0->_[c] += u - (sum0->_[c] >> 4);
                if ((k0->_[c] > 0) &&
                    (sum0->_[c] < (1 << (k0->_[c] + 4)))) {
                    k0->_[c] -= 1;
                } else if (sum0->_[c] > (1 << (k0->_[c] + 5))) {
                    k0->_[c] += 1;
                }

                if (u % 2) {
                    a_append(residual->_[c], (int)((u + 1) / 2));
                } else {
                    a_append(residual->_[c], -(int)(u / 2));
                }
            }
        }

        br_etry(frame);
    } else {
        br_etry(frame);
        return IOERROR;
    }

    filtered->reset(filtered);
    predicted->reset(predicted);
    if (channels > 1) {
        for (c = 0; c < channels; c++) {
            /*run hybrid filter*/
            hybrid_filter(residual->_[c],
                          bits_per_sample,
                          filtered->append(filtered));

            /*run fixed order prediction*/
            fixed_prediction(filtered->_[c],
                             bits_per_sample,
                             predicted->append(predicted));
        }

        /*perform channel decorrelation*/
        decorrelate_channels(predicted, framelist);
    } else {
        framelist->reset(framelist);

        /*run hybid filter*/
        hybrid_filter(residual->_[0],
                      bits_per_sample,
                      filtered->append(filtered));

        /*run fixed order prediction*/
        fixed_prediction(filtered->_[0],
                         bits_per_sample,
                         framelist->append(framelist));
    }

    return OK;
}

static void
hybrid_filter(const a_int* residual,
              unsigned bits_per_sample,
              a_int* filtered)
{
    const unsigned block_size = residual->len;
    const int32_t shift = (bits_per_sample == 16) ? 9 : 10;
    const int32_t round = (1 << (shift - 1));
    unsigned i;
    int32_t qm[] = {0, 0, 0, 0, 0, 0, 0, 0};
    int32_t dx[] = {0, 0, 0, 0, 0, 0, 0, 0};
    int32_t dl[] = {0, 0, 0, 0, 0, 0, 0, 0};

    filtered->reset_for(filtered, block_size);

    for (i = 0; i < block_size; i++) {
        int f;

        if (i == 0) {
            f = residual->_[0] + (round >> shift);
        } else {
            int64_t sum;

            if (residual->_[i - 1] < 0) {
                qm[0] -= dx[0];
                qm[1] -= dx[1];
                qm[2] -= dx[2];
                qm[3] -= dx[3];
                qm[4] -= dx[4];
                qm[5] -= dx[5];
                qm[6] -= dx[6];
                qm[7] -= dx[7];
            } else if (residual->_[i - 1] > 0) {
                qm[0] += dx[0];
                qm[1] += dx[1];
                qm[2] += dx[2];
                qm[3] += dx[3];
                qm[4] += dx[4];
                qm[5] += dx[5];
                qm[6] += dx[6];
                qm[7] += dx[7];
            }

            sum = round + (dl[0] * qm[0]) +
                          (dl[1] * qm[1]) +
                          (dl[2] * qm[2]) +
                          (dl[3] * qm[3]) +
                          (dl[4] * qm[4]) +
                          (dl[5] * qm[5]) +
                          (dl[6] * qm[6]) +
                          (dl[7] * qm[7]);

            f = residual->_[i] + (int)(sum >> shift);
        }
        a_append(filtered, f);

        dx[0] = dx[1];
        dx[1] = dx[2];
        dx[2] = dx[3];
        dx[3] = dx[4];
        dx[4] = (dl[4] >= 0) ? 1 : -1;
        dx[5] = (dl[5] >= 0) ? 2 : -2;
        dx[6] = (dl[6] >= 0) ? 2 : -2;
        dx[7] = (dl[7] >= 0) ? 4 : -4;
        dl[0] = dl[1];
        dl[1] = dl[2];
        dl[2] = dl[3];
        dl[3] = dl[4];
        dl[4] = -dl[5] + (-dl[6] + (f - dl[7]));
        dl[5] = -dl[6] + (f - dl[7]);
        dl[6] = f - dl[7];
        dl[7] = f;
    }
}

static void
fixed_prediction(const a_int* filtered,
                 unsigned bits_per_sample,
                 a_int* predicted)
{
    const unsigned block_size = filtered->len;
    const unsigned shift = (bits_per_sample == 8) ? 4 : 5;
    unsigned i;

    predicted->reset_for(predicted, block_size);
    a_append(predicted, filtered->_[0]);
    for (i = 1; i < block_size; i++) {
        const int64_t v = ((((int64_t)predicted->_[i - 1]) << shift) -
                           predicted->_[i - 1]);
        a_append(predicted, filtered->_[i] + (int)(v >> shift));
    }
}

static void
decorrelate_channels(const aa_int* predicted,
                     aa_int* decorrelated)
{
    const unsigned channels = predicted->len;
    const unsigned block_size = predicted->_[0]->len;

    decorrelated->reset(decorrelated);

    if (channels == 1) {
        unsigned i;
        a_int* decorrelated_ch = decorrelated->append(decorrelated);
        decorrelated_ch->resize(decorrelated_ch, block_size);

        for (i = 0; i < block_size; i++) {
            a_append(decorrelated_ch, predicted->_[0]->_[i]);
        }
    } else if (channels > 1) {
        unsigned i;
        unsigned j;
        unsigned c = channels - 1;
        a_int* decorrelated_ch = decorrelated->append(decorrelated);
        decorrelated_ch->resize(decorrelated_ch, block_size);

        for (i = 0; i < block_size; i++) {
            a_append(decorrelated_ch,
                     predicted->_[c]->_[i] +
                     (predicted->_[c - 1]->_[i] / 2));
        }

        for (j = 1; j < channels; j++) {
            decorrelated_ch = decorrelated->append(decorrelated);
            decorrelated_ch->resize(decorrelated_ch, block_size);
            c = channels - j - 1;
            for (i = 0; i < block_size; i++) {
                a_append(decorrelated_ch,
                         decorrelated->_[j - 1]->_[i] - predicted->_[c]->_[i]);
            }
        }
    }

    decorrelated->reverse(decorrelated);
}

#ifdef STANDALONE
#include <errno.h>

int main(int argc, char* argv[]) {
    FILE* file;
    BitstreamReader* bitstream;
    BitstreamReader* frame;
    struct tta_cache cache;
    aa_int* framelist;
    unsigned channels;
    unsigned bits_per_sample;
    unsigned sample_rate;
    unsigned remaining_pcm_frames;
    unsigned total_tta_frames;
    unsigned current_tta_frame = 0;
    unsigned block_size;
    unsigned* seektable = NULL;
    int_to_pcm_f converter;
    unsigned output_data_size;
    unsigned char* output_data;
    unsigned bytes_per_sample;

    /*open input file for reading*/
    if ((file = fopen(argv[1], "rb")) == NULL) {
        fprintf(stderr, "*** %s: %s\n", argv[1], strerror(errno));
        return 1;
    } else {
        /*open bitstream and setup cache*/
        bitstream = br_open(file, BS_LITTLE_ENDIAN);
        init_cache(&cache);
        framelist = aa_int_new();
        output_data_size = 1;
        output_data = malloc(output_data_size);
    }

    /*read header*/
    switch (read_header(bitstream,
                        &channels,
                        &bits_per_sample,
                        &sample_rate,
                        &remaining_pcm_frames)) {
    default:
        bytes_per_sample = bits_per_sample / 8;
        break;
    case INVALID_SIGNATURE:
        fprintf(stderr, "invalid header signature\n");
        goto error;
    case UNSUPPORTED_FORMAT:
        fprintf(stderr, "unsupported TTA format\n");
        goto error;
    case CRCMISMATCH:
        fprintf(stderr, "CRC error reading header\n");
        goto error;
    case IOERROR:
        fprintf(stderr, "I/O error reading header\n");
        goto error;
    }


    /*determine the default block size*/
    block_size = (sample_rate * 256) / 245;

    /*determine the total number of TTA frames*/
    total_tta_frames = DIV_CEIL(remaining_pcm_frames, block_size);

    seektable = malloc(sizeof(unsigned int) * total_tta_frames);

    /*read seektable*/
    switch (read_seektable(bitstream,
                           total_tta_frames,
                           seektable)) {
    default:
        /*do nothing*/
        break;
    case CRCMISMATCH:
        fprintf(stderr, "CRC error reading seektable\n");
        goto error;
    case IOERROR:
        fprintf(stderr, "I/O error reading seektable\n");
        goto error;
    }

    /*setup a framelist converter function*/
    converter = int_to_pcm_converter(bits_per_sample, 0, 1);

    /*read TTA frames*/
    while (remaining_pcm_frames) {
        const unsigned frame_block_size = MIN(block_size, remaining_pcm_frames);
        unsigned pcm_size;
        unsigned c;
        unsigned f;
        status status;

        if (!setjmp(*br_try(bitstream))) {
            frame = bitstream->substream(bitstream,
                                         seektable[current_tta_frame++]);
            br_etry(bitstream);
        } else {
            br_etry(bitstream);
            fprintf(stderr, "I/O error reading frame\n");
            goto error;
        }

        status = read_frame(frame,
                            &cache,
                            frame_block_size,
                            channels,
                            bits_per_sample,
                            framelist);

        frame->close(frame);

        switch (status) {
        default:
            /*convert framelist to string*/
            pcm_size = (bits_per_sample / 8) * frame_block_size * channels;
            if (pcm_size > output_data_size) {
                output_data_size = pcm_size;
                output_data = realloc(output_data, output_data_size);
            }
            for (c = 0; c < channels; c++) {
                const a_int* channel_data = framelist->_[c];
                for (f = 0; f < frame_block_size; f++) {
                    converter(channel_data->_[f],
                              output_data +
                              ((f * channels) + c) *
                              bytes_per_sample);
                }
            }

            /*output framelist as string to stdout*/
            fwrite(output_data, sizeof(unsigned char), pcm_size, stdout);

            /*deduct block size from total*/
            remaining_pcm_frames -= frame_block_size;
            break;
        case IOERROR:
            fprintf(stderr, "I/O error during frame read\n");
            goto error;
        case CRCMISMATCH:
            fprintf(stderr, "CRC mismatch reading frame\n");
            goto error;
        }
    }

    /*close bitstream and free cache*/
    bitstream->close(bitstream);
    free(output_data);
    free_cache(&cache);
    framelist->del(framelist);
    if (seektable != NULL)
        free(seektable);

    return 0;
error:
    bitstream->close(bitstream);
    free(output_data);
    free_cache(&cache);
    framelist->del(framelist);
    if (seektable != NULL)
        free(seektable);

    return 1;
}

#include "../common/tta_crc.c"
#endif

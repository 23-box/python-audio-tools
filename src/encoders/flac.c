#include "flac.h"
/* #include "flac_lpc.h" */
#include "../pcmreader2.h"
#include "../common/md5.h"
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <assert.h>

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

#define VERSION_STRING_(x) #x
#define VERSION_STRING(x) VERSION_STRING_(x)
const static char* AUDIOTOOLS_VERSION = VERSION_STRING(VERSION);

/* #define DEFAULT_PADDING_SIZE 4096 */
#define DEFAULT_PADDING_SIZE 0

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef STANDALONE
PyObject*
encoders_encode_flac(PyObject *dummy, PyObject *args, PyObject *keywds)
{
    char *filename;
    FILE *output_file;
    BitstreamWriter* output_stream;
    struct flac_context encoder;
    PyObject *pcmreader_obj;
    struct pcm_reader2* reader;
    char version_string[0xFF];
    static char *kwlist[] = {"filename",
                             "pcmreader",
                             "block_size",
                             "max_lpc_order",
                             "min_residual_partition_order",
                             "max_residual_partition_order",
                             "mid_side",
                             "adaptive_mid_side",
                             "exhaustive_model_search",

                             "disable_verbatim_subframes",
                             "disable_constant_subframes",
                             "disable_fixed_subframes",
                             "disable_lpc_subframes",
                             NULL};
    audiotools__MD5Context md5sum;

    array_ia* samples;

    PyObject *frame_offsets = NULL;
    PyObject *offset = NULL;

    unsigned block_size = 0;

    encoder.options.mid_side = 0;
    encoder.options.adaptive_mid_side = 0;
    encoder.options.exhaustive_model_search = 0;

    encoder.options.no_verbatim_subframes = 0;
    encoder.options.no_constant_subframes = 0;
    encoder.options.no_fixed_subframes = 0;
    encoder.options.no_lpc_subframes = 0;

    /*extract a filename, PCMReader-compatible object and encoding options:
      blocksize int*/
    if (!PyArg_ParseTupleAndKeywords(
            args, keywds, "sOIIII|iiiiiii",
            kwlist,
            &filename,
            &pcmreader_obj,
            &(encoder.options.block_size),
            &(encoder.options.max_lpc_order),
            &(encoder.options.min_residual_partition_order),
            &(encoder.options.max_residual_partition_order),
            &(encoder.options.mid_side),
            &(encoder.options.adaptive_mid_side),
            &(encoder.options.exhaustive_model_search),

            &(encoder.options.no_verbatim_subframes),
            &(encoder.options.no_constant_subframes),
            &(encoder.options.no_fixed_subframes),
            &(encoder.options.no_lpc_subframes)))
        return NULL;

    block_size = encoder.options.block_size;

    /*open the given filename for writing*/
    if ((output_file = fopen(filename, "wb")) == NULL) {
        PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);
        return NULL;
    }

    /*transform the Python PCMReader-compatible object to a pcm_reader struct*/
    if ((reader = pcmr_open2(pcmreader_obj)) == NULL) {
        fclose(output_file);
        return NULL;
    }

    /*build a list for frame offset info*/
    frame_offsets = PyList_New(0);

#else

int
encoders_encode_flac(char *filename,
                     FILE *input,
                     unsigned block_size,
                     unsigned max_lpc_order,
                     unsigned min_residual_partition_order,
                     unsigned max_residual_partition_order,
                     int mid_side,
                     int adaptive_mid_side,
                     int exhaustive_model_search) {
    FILE* output_file;
    BitstreamWriter* output_stream;
    struct flac_context encoder;
    struct pcm_reader2* reader;
    char version_string[0xFF];
    audiotools__MD5Context md5sum;
    array_ia* samples;

    /*set user-defined encoding options*/
    encoder.options.block_size = block_size;
    encoder.options.min_residual_partition_order =
        min_residual_partition_order;
    encoder.options.max_residual_partition_order =
        max_residual_partition_order;
    encoder.options.max_lpc_order = max_lpc_order;
    encoder.options.exhaustive_model_search = exhaustive_model_search;
    encoder.options.mid_side = mid_side;
    encoder.options.adaptive_mid_side = adaptive_mid_side;

    encoder.options.no_verbatim_subframes = 0;
    encoder.options.no_constant_subframes = 0;
    encoder.options.no_fixed_subframes = 0;
    encoder.options.no_lpc_subframes = 0;

    output_file = fopen(filename, "wb");
    /*FIXME - assume CD quality for now*/
    reader = pcmr_open2(input, 44100, 2, 0x3, 16, 0, 1);

#endif

    /*set derived encoding options*/
    if (block_size <= 192)
        encoder.options.qlp_coeff_precision = 7;
    else if (block_size <= 384)
        encoder.options.qlp_coeff_precision = 8;
    else if (block_size <= 576)
        encoder.options.qlp_coeff_precision = 9;
    else if (block_size <= 1152)
        encoder.options.qlp_coeff_precision = 10;
    else if (block_size <= 2304)
        encoder.options.qlp_coeff_precision = 11;
    else if (block_size <= 4608)
        encoder.options.qlp_coeff_precision = 12;
    else
        encoder.options.qlp_coeff_precision = 13;

    if (reader->bits_per_sample <= 16) {
        encoder.options.max_rice_parameter = 0xE;
    } else {
        encoder.options.max_rice_parameter = 0x1E;
    }

    sprintf(version_string, "Python Audio Tools %s", AUDIOTOOLS_VERSION);
    audiotools__MD5Init(&md5sum);
    pcmr_add_callback2(reader, md5_update, &md5sum, 1, 1);

    output_stream = bw_open(output_file, BS_BIG_ENDIAN);

    /*fill streaminfo with some placeholder values*/
    encoder.streaminfo.minimum_block_size = block_size;
    encoder.streaminfo.maximum_block_size = block_size;
    encoder.streaminfo.minimum_frame_size = 0xFFFFFF;
    encoder.streaminfo.maximum_frame_size = 0;
    encoder.streaminfo.sample_rate = reader->sample_rate;
    encoder.streaminfo.channels = reader->channels;
    encoder.streaminfo.bits_per_sample = reader->bits_per_sample;
    encoder.streaminfo.total_samples = 0;
    memcpy(encoder.streaminfo.md5sum,
           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
           16);

    encoder.total_flac_frames = 0;
    flacenc_init_encoder(&encoder);

    /*write FLAC stream header*/
    output_stream->write_64(output_stream, 32, 0x664C6143);

    /*write metadata header*/
    output_stream->write(output_stream, 1, 0);
    output_stream->write(output_stream, 7, 0);
    output_stream->write(output_stream, 24, 34);

    /*write placeholder STREAMINFO*/
    flacenc_write_streaminfo(output_stream, &(encoder.streaminfo));

    /*write VORBIS_COMMENT*/
    output_stream->write(output_stream, 1, 0);
    output_stream->write(output_stream, 7, 4);
    output_stream->write(output_stream, 24,
                         (unsigned int)(4 + strlen(version_string) + 4));

    /*write VORBIS_COMMENT fields as little-endian output*/
    output_stream->set_endianness(output_stream, BS_LITTLE_ENDIAN);
    output_stream->write(output_stream, 32, (unsigned)strlen(version_string));
    output_stream->write_bytes(output_stream,
                               (uint8_t*)version_string,
                               (unsigned)strlen(version_string));
    output_stream->write(output_stream, 32, 0);
    output_stream->set_endianness(output_stream, BS_BIG_ENDIAN);

    /*write PADDING*/
    output_stream->write(output_stream, 1, 1);
    output_stream->write(output_stream, 7, 1);
    output_stream->write(output_stream, 24, DEFAULT_PADDING_SIZE);
    output_stream->write(output_stream, DEFAULT_PADDING_SIZE * 8, 0);

    /*build frames until reader is empty,
      which updates STREAMINFO in the process*/
    samples = array_ia_new(reader->channels);

    if (!pcmr_read2(reader, block_size, samples))
        goto error;

    while (samples->data[0]->size > 0) {
#ifndef STANDALONE
        offset = Py_BuildValue("(i, i)",
                               bw_ftell(output_stream),
                               samples->data[0]->size);
        PyList_Append(frame_offsets, offset);
        Py_DECREF(offset);

        Py_BEGIN_ALLOW_THREADS
#endif
        bw_reset_recorder(encoder.frame);
        flacenc_write_frame(encoder.frame, &encoder, samples);
        encoder.streaminfo.total_samples += samples->data[0]->size;
        encoder.streaminfo.minimum_frame_size =
            MIN(encoder.streaminfo.minimum_frame_size,
                encoder.frame->bits_written(encoder.frame) / 8);
        encoder.streaminfo.maximum_frame_size =
            MAX(encoder.streaminfo.maximum_frame_size,
                encoder.frame->bits_written(encoder.frame) / 8);
        bw_rec_copy(output_stream, encoder.frame);
#ifndef STANDALONE
        Py_END_ALLOW_THREADS
#endif

        if (!pcmr_read2(reader, block_size, samples))
            goto error;
    }

    /*go back and re-write STREAMINFO with complete values*/
    audiotools__MD5Final(encoder.streaminfo.md5sum, &md5sum);
    fseek(output_stream->output.file, 4 + 4, SEEK_SET);
    flacenc_write_streaminfo(output_stream, &encoder.streaminfo);

    samples->del(samples); /*deallocate the temporary samples block*/
    pcmr_close2(reader); /*close the pcm_reader object
                           which calls pcmreader.close() in the process*/
    flacenc_free_encoder(&encoder);
    output_stream->close(output_stream); /*close the output file*/
#ifndef STANDALONE
    return frame_offsets;
 error:
    /*an error result does everything a regular result does
      but returns NULL instead of Py_None*/
    Py_XDECREF(frame_offsets);
    samples->del(samples);
    pcmr_close2(reader);
    flacenc_free_encoder(&encoder);
    output_stream->close(output_stream); /*close the output file*/
    return NULL;
}
#else
    return 1;
 error:
    samples->del(samples);
    pcmr_close2(reader);
    flacenc_free_encoder(&encoder);
    output_stream->close(output_stream); /*close the output file*/
    return 0;
}
#endif


void
flacenc_init_encoder(struct flac_context* encoder)
{
    encoder->frame = bw_open_recorder(BS_BIG_ENDIAN);
    encoder->fixed_subframe = bw_open_recorder(BS_BIG_ENDIAN);
    encoder->fixed_subframe_orders = array_ia_new(5);
    encoder->truncated_order = array_li_new();

    encoder->lpc_subframe = bw_open_recorder(BS_BIG_ENDIAN);
    encoder->tukey_window = array_f_new(1);
    encoder->windowed_signal = array_f_new(1);
    encoder->autocorrelation_values = array_f_new(1);
    encoder->lp_coefficients = array_fa_new(1);
    encoder->lp_error = array_f_new(1);
    encoder->qlp_coefficients = array_i_new(1);
    encoder->lpc_residual = array_i_new(1);

    encoder->best_partition_sizes = array_i_new(1);
    encoder->best_rice_parameters = array_i_new(1);
    encoder->partition_sizes = array_i_new(1);
    encoder->rice_parameters = array_i_new(1);
    encoder->remaining_residuals = array_li_new();
    encoder->residual_partition = array_li_new();
}

void
flacenc_free_encoder(struct flac_context* encoder)
{
    encoder->frame->close(encoder->frame);
    encoder->fixed_subframe->close(encoder->fixed_subframe);
    encoder->fixed_subframe_orders->del(encoder->fixed_subframe_orders);
    encoder->truncated_order->del(encoder->truncated_order);

    encoder->lpc_subframe->close(encoder->lpc_subframe);
    encoder->tukey_window->del(encoder->tukey_window);
    encoder->windowed_signal->del(encoder->windowed_signal);
    encoder->autocorrelation_values->del(encoder->autocorrelation_values);
    encoder->lp_coefficients->del(encoder->lp_coefficients);
    encoder->lp_error->del(encoder->lp_error);
    encoder->qlp_coefficients->del(encoder->qlp_coefficients);
    encoder->lpc_residual->del(encoder->lpc_residual);

    encoder->best_partition_sizes->del(encoder->best_partition_sizes);
    encoder->best_rice_parameters->del(encoder->best_rice_parameters);
    encoder->partition_sizes->del(encoder->partition_sizes);
    encoder->rice_parameters->del(encoder->rice_parameters);
    encoder->remaining_residuals->del(encoder->remaining_residuals);
    encoder->residual_partition->del(encoder->residual_partition);
}

void
flacenc_write_streaminfo(BitstreamWriter* bs,
                         const struct flac_STREAMINFO* streaminfo)
{
    int i;

    bs->write(bs, 16, MAX(MIN(streaminfo->minimum_block_size,
                              (1 << 16) - 1), 0));

    bs->write(bs, 16, MAX(MIN(streaminfo->maximum_block_size,
                              (1 << 16) - 1),0));

    bs->write(bs, 24, MAX(MIN(streaminfo->minimum_frame_size,
                              (1 << 24) - 1), 0));

    bs->write(bs, 24, MAX(MIN(streaminfo->maximum_frame_size,
                              (1 << 24) - 1), 0));

    bs->write(bs, 20, MAX(MIN(streaminfo->sample_rate,
                              (1 << 20) - 1), 0));

    bs->write(bs, 3, MAX(MIN(streaminfo->channels - 1,
                             (1 << 3) - 1), 0));

    bs->write(bs, 5, MAX(MIN(streaminfo->bits_per_sample - 1,
                             (1 << 5) - 1), 0));

    assert(streaminfo->total_samples >= 0);
    assert(streaminfo->total_samples < (int64_t)(1ll << 36));
    bs->write_64(bs, 36, streaminfo->total_samples);

    for (i = 0; i < 16; i++)
        bs->write(bs, 8, streaminfo->md5sum[i]);
}


void
flacenc_write_frame_header(BitstreamWriter *bs,
                           const struct flac_STREAMINFO *streaminfo,
                           unsigned block_size,
                           unsigned channel_assignment,
                           unsigned frame_number)
{
    unsigned block_size_bits;
    unsigned sample_rate_bits;
    unsigned bits_per_sample_bits;
    int crc8 = 0;

    bw_add_callback(bs, flac_crc8, &crc8);

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
        if (block_size < (0xFF + 1))
            block_size_bits = 0x6;
        else if (block_size < (0xFFFF + 1))
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
            sample_rate_bits = 0xD;
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
    bs->write(bs, 14, 0x3FFE);              /*sync code*/
    bs->write(bs, 1, 0);                    /*reserved*/
    bs->write(bs, 1, 0);                    /*blocking strategy*/
    bs->write(bs, 4, block_size_bits);      /*block size*/
    bs->write(bs, 4, sample_rate_bits);     /*sample rate*/
    bs->write(bs, 4, channel_assignment);   /*channel assignment*/
    bs->write(bs, 3, bits_per_sample_bits); /*bits per sample*/
    bs->write(bs, 1, 0);                    /*padding*/

    /*frame number is taken from total_frames in streaminfo*/
    write_utf8(bs, frame_number);

    /*if block_size_bits are 0x6 or 0x7, write a PCM frames field*/
    if (block_size_bits == 0x6)
        bs->write(bs, 8, block_size - 1);
    else if (block_size_bits == 0x7)
        bs->write(bs, 16, block_size - 1);

    /*if sample rate is unusual, write one of the three sample rate fields*/
    if (sample_rate_bits == 0xC)
        bs->write(bs, 8, streaminfo->sample_rate / 1000);
    else if (sample_rate_bits == 0xD)
        bs->write(bs, 16, streaminfo->sample_rate);
    else if (sample_rate_bits == 0xE)
        bs->write(bs, 16, streaminfo->sample_rate / 10);

    /*write CRC-8*/
    bw_pop_callback(bs, NULL);
    bs->write(bs, 8, crc8);
}

void
flacenc_write_frame(BitstreamWriter* bs,
                    struct flac_context* encoder,
                    const array_ia* samples)
{
    unsigned block_size = samples->data[0]->size;
    unsigned channel_count = samples->size;
    unsigned channel;
    int crc16 = 0;

    bw_add_callback(bs, flac_crc16, &crc16);

    /*FIXME - check side frames here and determine which is best*/

    flacenc_write_frame_header(bs,
                               &(encoder->streaminfo),
                               block_size,
                               channel_count - 1,
                               encoder->total_flac_frames++);

    for (channel = 0; channel < channel_count; channel++)
        flacenc_write_subframe(bs,
                               encoder,
                               encoder->streaminfo.bits_per_sample,
                               samples->data[channel]);

    bs->byte_align(bs);
    bw_pop_callback(bs, NULL);
    bs->write(bs, 16, crc16);
}

void
flacenc_write_subframe(BitstreamWriter* bs,
                       struct flac_context* encoder,
                       unsigned bits_per_sample,
                       const array_i* samples)
{
    int try_VERBATIM = !encoder->options.no_verbatim_subframes;
    int try_CONSTANT = !encoder->options.no_constant_subframes;
    int try_FIXED = !encoder->options.no_fixed_subframes;
    int try_LPC = ((!encoder->options.no_fixed_subframes) &&
                   (encoder->options.max_lpc_order > 0));
    unsigned wasted_bps;

    /*check for CONSTANT subframe and return one, if allowed*/
    if (try_CONSTANT && flacenc_all_identical(samples)) {
        flacenc_write_constant_subframe(bs, bits_per_sample, 0,
                                        samples->data[0]);
    } else {
        /*extract wasted bits-per-sample, if any*/
        /*FIXME*/
        wasted_bps = 0;

        /*build FIXED subframe, if allowed*/
        if (try_FIXED) {
            bw_reset_recorder(encoder->fixed_subframe);
            flacenc_write_fixed_subframe(encoder->fixed_subframe,
                                         encoder,
                                         bits_per_sample,
                                         wasted_bps,
                                         samples);
        }

        /*build LPC subframe, if allowed*/
        if (try_LPC) {
            bw_reset_recorder(encoder->lpc_subframe);
            flacenc_write_lpc_subframe(bs,
                                       encoder,
                                       bits_per_sample,
                                       wasted_bps,
                                       samples);
        }

        /*if FIXED = n , LPC = n , VERBATIM = n
          return VERBATIM subframe anyway*/

        /*if FIXED = y , LPC = n , VERBATIM = n
          return FIXED subframe*/

        /*if FIXED = n , LPC = y , VERBATIM = n
          return LPC subframe*/

        /*if FIXED = y , LPC = y , VERBATIM = n
          return min(FIXED, LPC) subframes*/

        /*if FIXED = n , LPC = n , VERBATIM = y
          return VERBATIM subframe*/

        /*if FIXED = y , LPC = n , VERBATIM = y
          return min(FIXED, VERBATIM) subframes*/

        /*if FIXED = n , LPC = y , VERBATIM = y
          return min(LPC, VERBATIM) subframes*/

        /*if FIXED = y , LPC = y , VERBATIM = y
          return min(FIXED, LPC, VERBATIM) subframes*/

        /* flacenc_write_verbatim_subframe(bs, */
        /*                                 bits_per_sample, */
        /*                                 0, */
        /*                                 samples); */
    }
}

void
flacenc_write_constant_subframe(BitstreamWriter *bs,
                                unsigned bits_per_sample,
                                unsigned wasted_bits_per_sample,
                                int sample)
{
    /*write subframe header*/
    bs->write(bs, 1, 0);
    bs->write(bs, 6, 0);
    if (wasted_bits_per_sample) {
        bs->write(bs, 1, 1);
        bs->write_unary(bs, 1, wasted_bits_per_sample - 1);
    } else
        bs->write(bs, 1, 0);

    /*write subframe sample*/
    bs->write_signed(bs, bits_per_sample, sample);
}

void
flacenc_write_verbatim_subframe(BitstreamWriter *bs,
                                unsigned bits_per_sample,
                                unsigned wasted_bits_per_sample,
                                const array_i* samples)
{
    unsigned i;

    /*write subframe header*/
    bs->write(bs, 1, 0);
    bs->write(bs, 6, 1);
    if (wasted_bits_per_sample) {
        bs->write(bs, 1, 1);
        bs->write_unary(bs, 1, wasted_bits_per_sample - 1);
    } else
        bs->write(bs, 1, 0);

    /*write subframe samples*/
    for (i = 0; i < samples->size; i++) {
        bs->write_signed(bs, bits_per_sample, samples->data[i]);
    }
}

void
flacenc_write_fixed_subframe(BitstreamWriter* bs,
                             struct flac_context* encoder,
                             unsigned bits_per_sample,
                             unsigned wasted_bits_per_sample,
                             const array_i* samples)
{
    array_ia* fixed_subframe_orders = encoder->fixed_subframe_orders;
    array_i* order;
    array_li* truncated_order = encoder->truncated_order;

    uint64_t best_order_abs_sum;
    unsigned best_order;
    uint64_t order_abs_sum;
    unsigned i;

    fixed_subframe_orders->reset(fixed_subframe_orders);
    order = fixed_subframe_orders->append(fixed_subframe_orders);
    order->extend(order, samples);  /*order 0*/
    order->link(order, truncated_order);

    truncated_order->de_head(truncated_order, 4, truncated_order);
    best_order_abs_sum = flacenc_abs_sum(truncated_order);
    best_order = 0;

    if (samples->size > 4) {
        for (i = 0; i < MAX_FIXED_ORDER; i++) {
            /*orders 1 - 4*/
            order = fixed_subframe_orders->append(fixed_subframe_orders);
            flacenc_next_fixed_order(fixed_subframe_orders->data[i], order);
            order->link(order, truncated_order);
            truncated_order->de_head(truncated_order, 4 - (i + 1),
                                     truncated_order);
            order_abs_sum = flacenc_abs_sum(truncated_order);
            if (order_abs_sum < best_order_abs_sum) {
                best_order_abs_sum = order_abs_sum;
                best_order = i + 1;
            }
        }
    }

    bs->write(bs, 1, 0);               /*pad*/
    bs->write(bs, 3, 1);               /*FIXED subframe type*/
    bs->write(bs, 3, best_order);      /*FIXED subframe order*/
    if (wasted_bits_per_sample > 0) {  /*wasted bits-per-sample*/
        bs->write(bs, 1, 1);
        bs->write_unary(bs, 1, wasted_bits_per_sample - 1);
    } else {
        bs->write(bs, 1, 0);
    }

    for (i = 0; i < best_order; i++)   /*warm-up samples*/
        bs->write_signed(bs, bits_per_sample - wasted_bits_per_sample,
                         samples->data[i]);

    flacenc_encode_residuals(bs,
                             encoder,
                             samples->size,
                             best_order,
                             encoder->fixed_subframe_orders->data[best_order]);
}

void
flacenc_next_fixed_order(const array_i* order, array_i* next_order)
{
    unsigned i;
    unsigned order_size = order->size;
    int* order_data = order->data;

    assert(order_size > 1);
    next_order->resize(next_order, order_size - 1);
    next_order->reset(next_order);
    for (i = 1; i < order_size; i++) {
        a_append(next_order, order_data[i] - order_data[i - 1]);
    }
}

void
flacenc_write_lpc_subframe(BitstreamWriter* bs,
                           struct flac_context* encoder,
                           unsigned bits_per_sample,
                           unsigned wasted_bits_per_sample,
                           const array_i* samples)
{
    array_i* qlp_coefficients = encoder->qlp_coefficients;
    array_i* lpc_residual = encoder->lpc_residual;
    unsigned qlp_precision;
    int qlp_shift_needed;
    unsigned order;
    int64_t accumulator;
    unsigned i;
    unsigned j;

    flacenc_best_lpc_coefficients(bits_per_sample,
                                  encoder,
                                  samples,
                                  qlp_coefficients,
                                  &qlp_precision,
                                  &qlp_shift_needed);

    assert(qlp_coefficients->size > 0);
    order = qlp_coefficients->size;

    bs->write(bs, 1, 0);               /*pad*/
    bs->write(bs, 1, 1);               /*subframe type*/
    bs->write(bs, 5, order - 1);       /*subframe order*/
    if (wasted_bits_per_sample > 0) {  /*wasted bits-per-sample*/
        bs->write(bs, 1, 1);
        bs->write_unary(bs, 1, wasted_bits_per_sample - 1);
    } else {
        bs->write(bs, 1, 0);
    }

    for (i = 0; i < order; i++)        /*warm-up samples*/
        bs->write_signed(bs, bits_per_sample - wasted_bits_per_sample,
                         samples->data[i]);

    bs->write(bs, 4, qlp_precision - 1);
    bs->write_signed(bs, 5, qlp_shift_needed);

    for (i = 0; i < order; i++)        /*QLP coefficients*/
        bs->write_signed(bs, qlp_precision, qlp_coefficients->data[i]);

    /*calculate signed residuals*/
    lpc_residual->reset(lpc_residual);
    lpc_residual->resize(lpc_residual, samples->size - order);
    for (i = 0; i < samples->size - order; i++) {
        accumulator = 0;
        for (j = 0; j < order; j++)
            accumulator += ((int64_t)qlp_coefficients->data[j] *
                            (int64_t)samples->data[i + order - j - 1]);
        accumulator >>= qlp_shift_needed;
        a_append(lpc_residual,
                 samples->data[i + order] - (int)accumulator);
    }

    /*write residual block*/
    flacenc_encode_residuals(bs,
                             encoder,
                             samples->size,
                             order,
                             lpc_residual);
}

void
flacenc_best_lpc_coefficients(unsigned bits_per_sample,
                              struct flac_context* encoder,
                              const array_i* samples,
                              array_i* qlp_coefficients,
                              unsigned* qlp_precision,
                              int* qlp_shift_needed)
{
    array_f* windowed_signal = encoder->windowed_signal;
    array_f* autocorrelation_values = encoder->autocorrelation_values;
    array_fa* lp_coefficients = encoder->lp_coefficients;
    array_f* lp_error = encoder->lp_error;
    unsigned best_order;

    if (samples->size > (encoder->options.max_lpc_order + 1)) {
        /*window signal*/
        flacenc_window_signal(samples,
                              encoder->tukey_window,
                              windowed_signal);

        /*transform windowed signal to autocorrelation values*/
        flacenc_autocorrelate(encoder->options.max_lpc_order,
                              windowed_signal,
                              autocorrelation_values);

        /*calculate LP coefficients from autocorrelation values*/
        flacenc_compute_lp_coefficients(encoder->options.max_lpc_order,
                                        autocorrelation_values,
                                        lp_coefficients,
                                        lp_error);

        if (!encoder->options.exhaustive_model_search) {
            /*if not performing exhaustive model search
              estimate the best order from the error values*/

            best_order =
                flacenc_estimate_best_lpc_order(
                    bits_per_sample,
                    encoder->options.qlp_coeff_precision,
                    encoder->options.max_lpc_order,
                    samples->size,
                    lp_error);

            flacenc_quantize_coefficients(lp_coefficients,
                                          best_order,
                                          encoder->options.qlp_coeff_precision,
                                          qlp_coefficients,
                                          qlp_shift_needed);

            *qlp_precision = encoder->options.qlp_coeff_precision;
        } else {
            assert(0);
            /*FIXME*/

            /*otherwise, build LPC subframe from each set of LP coefficients*/
            /*FIXME*/

            /*and return the parameters of the one which is smallest*/
            /*FIXME*/
        }
    } else {
        /*use a set of dummy coefficients*/
        qlp_coefficients->reset(qlp_coefficients);
        qlp_coefficients->vappend(qlp_coefficients, 1, 1);
        *qlp_precision = 2;
        *qlp_shift_needed = 0;
    }
}

void
flacenc_window_signal(const array_i* samples,
                      array_f* tukey_window,
                      array_f* windowed_signal)
{
    unsigned N = samples->size;
    unsigned n;
    double alpha = 0.5;
    unsigned window1;
    unsigned window2;

    if (tukey_window->size != samples->size) {
        tukey_window->resize(tukey_window, samples->size);
        tukey_window->reset(tukey_window);

        window1 = (unsigned)(alpha * (N - 1)) / 2;
        window2 = (unsigned)((N - 1) * (1.0 - (alpha / 2.0)));

        for (n = 0; n < N; n++) {
            if (n <= window1) {
                a_append(tukey_window,
                         0.5 *
                         (1.0 +
                          cos(M_PI * (((2 * n) / (alpha * (N - 1))) - 1.0))));
            } else if (n <= window2) {
                a_append(tukey_window, 1.0);
            } else {
                a_append(tukey_window,
                         0.5 *
                         (1.0 +
                          cos(M_PI * (((2.0 * n) / (alpha * (N - 1))) -
                                      (2.0 / alpha) + 1.0))));
            }
        }
    }

    windowed_signal->resize(windowed_signal, samples->size);
    windowed_signal->reset(windowed_signal);
    for (n = 0; n < N; n++) {
        a_append(windowed_signal, samples->data[n] * tukey_window->data[n]);
    }
}

void
flacenc_autocorrelate(unsigned max_lpc_order,
                      const array_f* windowed_signal,
                      array_f* autocorrelation_values)
{
    unsigned lag;
    unsigned i;
    double accumulator;

    autocorrelation_values->reset(autocorrelation_values);

    for (lag = 0; lag <= max_lpc_order; lag++) {
        accumulator = 0.0;
        assert((windowed_signal->size - lag) > 0);
        for (i = 0; i < windowed_signal->size - lag; i++)
            accumulator += (windowed_signal->data[i] *
                            windowed_signal->data[i + lag]);
        autocorrelation_values->append(autocorrelation_values, accumulator);
    }
}

void
flacenc_compute_lp_coefficients(unsigned max_lpc_order,
                                const array_f* autocorrelation,
                                array_fa* lp_coefficients,
                                array_f* lp_error)
{
    unsigned i;
    unsigned j;
    array_f* lp_coeff;
    double k;
    double q;

    assert(autocorrelation->size == (max_lpc_order + 1));

    lp_coefficients->reset(lp_coefficients);
    lp_error->reset(lp_error);

    k = autocorrelation->data[1] / autocorrelation->data[0];
    lp_coeff = lp_coefficients->append(lp_coefficients);
    lp_coeff->append(lp_coeff, k);
    lp_error->append(lp_error,
                     autocorrelation->data[0] * (1.0 - (k * k)));

    for (i = 1; i < max_lpc_order; i++) {
        q = autocorrelation->data[i + 1];
        for (j = 0; j < i; j++)
            q -= (lp_coefficients->data[i - 1]->data[j] *
                  autocorrelation->data[i - j]);

        k = q / lp_error->data[i - 1];

        lp_coeff = lp_coefficients->append(lp_coefficients);
        for (j = 0; j < i; j++) {
            lp_coeff->append(lp_coeff,
                             lp_coefficients->data[i - 1]->data[j] -
                             (k *
                              lp_coefficients->data[i - 1]->data[i - j - 1]));
        }
        lp_coeff->append(lp_coeff, k);

        lp_error->append(lp_error, lp_error->data[i - 1] * (1.0 - (k * k)));
    }
}

unsigned
flacenc_estimate_best_lpc_order(unsigned bits_per_sample,
                                unsigned qlp_precision,
                                unsigned max_lpc_order,
                                unsigned block_size,
                                const array_f* lp_error)
{
    double error_scale = (M_LN2 * M_LN2) / ((double)block_size * 2.0);
    unsigned best_order = 0;
    double best_subframe_bits = DBL_MAX;
    unsigned header_bits;
    double bits_per_residual;
    double estimated_subframe_bits;
    unsigned i;
    unsigned order;

    assert(block_size > 0);

    for (i = 0; i < max_lpc_order; i++) {
        order = i + 1;
        if (lp_error->data[i] > 0.0) {
            header_bits = order * (bits_per_sample + qlp_precision);
            bits_per_residual = MAX(log(lp_error->data[i] * error_scale) /
                                    (M_LN2 * 2), 0.0);
            estimated_subframe_bits =
                (header_bits + bits_per_residual * (block_size - order));

            if (estimated_subframe_bits < best_subframe_bits) {
                best_order = order;
                best_subframe_bits = estimated_subframe_bits;
            }
        } else {
            return order;
        }
    }

    assert(best_order > 0);
    return best_order;
}

void
flacenc_quantize_coefficients(const array_fa* lp_coefficients,
                              unsigned order,
                              unsigned qlp_precision,
                              array_i* qlp_coefficients,
                              int* qlp_shift_needed)
{
    array_f* lp_coeffs = lp_coefficients->data[order - 1];
    double l = DBL_MIN;
    int log2cmax;
    unsigned i;
    int qlp_max;
    int qlp_min;
    double error;
    int error_i;

    qlp_coefficients->reset(qlp_coefficients);

    for (i = 0; i < lp_coeffs->size; i++)
        l = MAX(fabs(lp_coeffs->data[i]), l);

    frexp(l, &log2cmax);

    *qlp_shift_needed = (int)(qlp_precision - 1) - (log2cmax - 1) - 1;
    *qlp_shift_needed = MAX(*qlp_shift_needed, -(1 << 4));
    *qlp_shift_needed = MIN(*qlp_shift_needed, (1 << 4) - 1);

    qlp_max = (1 << (qlp_precision - 1)) - 1;
    qlp_min = -(1 << (qlp_precision - 1));

    error = 0.0;

    if (*qlp_shift_needed >= 0) {
        for (i = 0; i < order; i++) {
            error += (lp_coeffs->data[i] * (1 << *qlp_shift_needed));
            error_i = (int)round(error);
            qlp_coefficients->append(qlp_coefficients,
                                     MIN(MAX(error_i, qlp_min), qlp_max));
            error -= (double)error_i;
        }
    } else {
        /*negative shifts are not allowed, so shrink coefficients*/
        for (i = 0; i < order; i++) {
            error += (lp_coeffs->data[i] / (1 << *qlp_shift_needed));
            error_i = (int)round(error);
            qlp_coefficients->append(qlp_coefficients,
                                     MIN(MAX(error_i, qlp_min), qlp_max));
            error -= (double)error_i;
        }
        *qlp_shift_needed = 0;
    }
}

void
flacenc_encode_residuals(BitstreamWriter* bs,
                         struct flac_context* encoder,
                         unsigned block_size,
                         unsigned order,
                         const array_i* residuals)
{
    unsigned coding_method;
    unsigned partition_order;
    unsigned best_partition_order = 0;
    unsigned partition;

    /*local links to the cached arrays*/
    array_i* best_partition_sizes = encoder->best_partition_sizes;
    array_i* best_rice_parameters = encoder->best_rice_parameters;
    array_i* partition_sizes = encoder->partition_sizes;
    array_i* rice_parameters = encoder->rice_parameters;
    array_li* remaining_residuals = encoder->remaining_residuals;
    array_li* residual_partition = encoder->residual_partition;

    uint64_t abs_partition_sum;
    unsigned rice_parameter;
    unsigned partition_size;

    best_partition_sizes->reset(best_partition_sizes);
    best_rice_parameters->reset(best_rice_parameters);
    best_partition_sizes->append(best_partition_sizes, INT_MAX);

    for (partition_order = 0;
         partition_order <= encoder->options.max_residual_partition_order;
         partition_order++) {

        if (block_size % (1 << partition_order))
            /*stop once block_size is no longer equally divisible by
              2 ^ partition_order*/
            break;

        residuals->link(residuals, remaining_residuals);
        partition_sizes->reset(partition_sizes);
        rice_parameters->reset(rice_parameters);

        for (partition = 0; partition < (1 << partition_order); partition++) {
            if (partition == 0)
                remaining_residuals->split(remaining_residuals,
                                           (block_size /
                                            (1 << partition_order)) - order,
                                           residual_partition,
                                           remaining_residuals);
            else
                remaining_residuals->split(remaining_residuals,
                                           (block_size /
                                            (1 << partition_order)),
                                           residual_partition,
                                           remaining_residuals);

            abs_partition_sum = flacenc_abs_sum(residual_partition);
            rice_parameter =
                flacenc_best_rice_parameter(encoder,
                                            abs_partition_sum,
                                            residual_partition->size);
            partition_size =
                flacenc_estimate_partition_size(rice_parameter,
                                                abs_partition_sum,
                                                residual_partition->size);
            rice_parameters->append(rice_parameters, rice_parameter);
            partition_sizes->append(partition_sizes, partition_size);
        }

        if (partition_sizes->sum(partition_sizes) <
            best_partition_sizes->sum(best_partition_sizes)) {
            best_partition_order = partition_order;
            partition_sizes->swap(partition_sizes,
                                  best_partition_sizes);
            rice_parameters->swap(rice_parameters,
                                  best_rice_parameters);
        }
    }

    assert(remaining_residuals->size == 0);
    assert(best_rice_parameters->size == best_partition_sizes->size);

    if (best_rice_parameters->max(best_rice_parameters) > 14)
        coding_method = 1;
    else
        coding_method = 0;

    bs->write(bs, 2, coding_method);
    bs->write(bs, 4, best_partition_order);

    residuals->link(residuals, remaining_residuals);
    for (partition = 0; partition < (1 << best_partition_order); partition++) {
        if (partition == 0)
            remaining_residuals->split(remaining_residuals,
                                       (block_size /
                                        (1 << best_partition_order)) - order,
                                       residual_partition,
                                       remaining_residuals);
        else
            remaining_residuals->split(remaining_residuals,
                                       (block_size /
                                        (1 << best_partition_order)),
                                       residual_partition,
                                       remaining_residuals);

        if (coding_method == 0)
            bs->write(bs, 4, best_rice_parameters->data[partition]);
        else
            bs->write(bs, 5, best_rice_parameters->data[partition]);

        flacenc_encode_residual_partition(bs,
                                          best_rice_parameters->data[partition],
                                          residual_partition);
    }
}

void
flacenc_encode_residual_partition(BitstreamWriter* bs,
                                  unsigned rice_parameter,
                                  const array_li* residual_partition)
{
    unsigned partition_size = residual_partition->size;
    const int* residuals = residual_partition->data;
    unsigned value;
    unsigned msb;
    unsigned lsb;
    unsigned i;

    void (*write)(struct BitstreamWriter_s* bs, unsigned int count,
                  unsigned int value);

    void (*write_unary)(struct BitstreamWriter_s* bs, int stop_bit,
                        unsigned int value);

    write = bs->write;
    write_unary = bs->write_unary;

    for (i = 0; i < partition_size; i++) {
        if (residuals[i] >= 0)
            value = residuals[i] << 1;
        else
            value = ((-residuals[i] - 1) << 1) | 1;
        msb = value >> rice_parameter;
        lsb = value - (msb << rice_parameter);
        write_unary(bs, 1, msb);
        write(bs, rice_parameter, lsb);
    }
}

unsigned
flacenc_best_rice_parameter(const struct flac_context* encoder,
                            uint64_t abs_partition_sum,
                            unsigned partition_size)
{
    unsigned rice_parameter = 0;

    while ((partition_size * (1 << rice_parameter)) < abs_partition_sum)
        rice_parameter++;

    return rice_parameter;
}

unsigned
flacenc_estimate_partition_size(unsigned rice_parameter,
                                uint64_t abs_partition_sum,
                                unsigned partition_size)
{
    if (rice_parameter > 0) {
        return (4 + /*4 bit partition header*/
                /*residual MSBs minus sign bit*/
                (unsigned)(abs_partition_sum >> (rice_parameter - 1)) +
                /*residual LSBs plus stop bit*/
                ((1 + rice_parameter) * partition_size) -
                (partition_size / 2));
    } else {
        return (4 + /*4 bit partition header*/
                /*residual MSBs minus sign bit*/
                (unsigned)(abs_partition_sum << 1) +
                /*residual LSBs plus stop bit*/
                ((1 + rice_parameter) * partition_size) -
                (partition_size / 2));
    }
}


void
write_utf8(BitstreamWriter* bs, unsigned int value) {
    unsigned int total_bytes = 0;
    int shift;

    if (value <= 0x7F) {
        /*1 byte only*/
        bs->write(bs, 8, value);
    } else {
        /*more than 1 byte*/

        if (value <= 0x7FF) {
            total_bytes = 2;
        } else if (value <= 0xFFFF) {
            total_bytes = 3;
        } else if (value <= 0x1FFFFF) {
            total_bytes = 4;
        } else if (value <= 0x3FFFFFF) {
            total_bytes = 5;
        } else if (value <= 0x7FFFFFFF) {
            total_bytes = 6;
        }

        shift = (total_bytes - 1) * 6;
        /*send out the initial unary + leftover most-significant bits*/
        bs->write_unary(bs, 0, total_bytes);
        bs->write(bs, 7 - total_bytes, value >> shift);

        /*then send the least-significant bits,
          6 at a time with a unary 1 value appended*/
        for (shift -= 6; shift >= 0; shift -= 6) {
            bs->write_unary(bs, 0, 1);
            bs->write(bs, 6, (value >> shift) & 0x3F);
        }
    }
}



void
md5_update(void *data, unsigned char *buffer, unsigned long len)
{
    audiotools__MD5Update((audiotools__MD5Context*)data,
                          (const void*)buffer,
                          len);
}

int
flacenc_max_wasted_bits_per_sample(const array_i* samples)
{
    unsigned i;
    int sample;
    int wasted_bits;
    int wasted_bits_per_sample = INT_MAX;

    for (i = 0; i < samples->size; i++) {
        sample = samples->data[i];
        if (sample != 0) {
            for (wasted_bits = 0;
                 ((sample & 1) == 0) && (sample != 0);
                 sample >>= 1)
                wasted_bits++;
            wasted_bits_per_sample = MIN(wasted_bits_per_sample,
                                         wasted_bits);
            if (wasted_bits_per_sample == 0)
                return 0;
        }
    }

    if (wasted_bits_per_sample == INT_MAX) {
        return 0;
    } else {
        return wasted_bits_per_sample;
    }
}

int
flacenc_all_identical(const array_i* samples)
{
    int first;
    unsigned i;

    if (samples->size > 1) {
        first = samples->data[0];
        for (i = 1; i < samples->size; i++)
            if (samples->data[i] != first)
                return 0;

        return 1;
    } else {
        return 1;
    }
}

uint64_t
flacenc_abs_sum(const array_li* data)
{
    uint64_t accumulator = 0;
    unsigned i;
    for (i = 0; i < data->size; i++)
        accumulator += abs(data->data[i]);

    return accumulator;
}

#ifdef STANDALONE

int main(int argc, char *argv[]) {
    encoders_encode_flac(argv[1],
                         stdin,
                         4096, 12, 0, 6, 1, 1, 0);

    return 0;
}
#endif

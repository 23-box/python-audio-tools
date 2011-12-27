#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007-2011  Brian Langenberger

#This program is free software; you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
#the Free Software Foundation; either version 2 of the License, or
#(at your option) any later version.

#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.

#You should have received a copy of the GNU General Public License
#along with this program; if not, write to the Free Software
#Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

from audiotools.bitstream import BitstreamWriter
from audiotools.bitstream import BitstreamRecorder
from audiotools import BufferedPCMReader
from hashlib import md5


class EncoderContext:
    def __init__(self, pcmreader, block_parameters):
        self.pcmreader = pcmreader
        self.block_parameters = block_parameters
        self.first_block_written = False
        self.block_offsets = []


class CorrelationParameters:
    def __init__(self, term, delta, weights, samples):
        """term is a signed integer
        delta is an unsigned integer
        weights[c] is a weight value per channel c
        samples[c][s] is sample "s" for channel "c"
        """

        #FIXME - sanity check these

        self.term = term
        self.delta = delta
        self.weights = weights
        self.samples = samples

    def __repr__(self):
        return "CorrelationParameters(%s, %s, %s, %s)" % \
            (self.term, self.delta, self.weights, self.samples)


class EncodingParameters:
    def __init__(self, channel_count,
                 correlation_parameters,
                 entropy_variables):
        """channel_count is 1 or 2
        correlation_parameters is a list CorrelationParameters objects
        entropy_variables is a list of 3 ints per channel
        """

        assert((channel_count == 1) or (channel_count == 2))
        assert(len(entropy_variables) == 2)
        assert(len(entropy_variables[0]) == 3)
        assert(len(entropy_variables[1]) == 3)

        self.channel_count = channel_count
        self.correlation_parameters = correlation_parameters
        self.entropy_variables = entropy_variables

    def __repr__(self):
        return "EncodingParameters(%s, %s, %s)" % \
            (self.channel_count,
             self.correlation_parameters,
             self.entropy_variables)

    @classmethod
    def new(cls, channel_count, correlation_passes):
        assert((channel_count == 1) or (channel_count == 2))

        if (channel_count == 1):
            if (correlation_passes == 0):
                correlation_parameters = []
            elif (correlation_passes == 1):
                correlation_parameters = [
                    CorrelationParameters(18, 2, [0], [[0, 0]])]
            elif (correlation_passes == 2):
                correlation_parameters = [
                    CorrelationParameters(17, 2, [0], [[0, 0]]),
                    CorrelationParameters(18, 2, [0], [[0, 0]])]
            elif (correlation_passes in (5, 10, 16)):
                correlation_parameters = [
                    CorrelationParameters(3, 2, [0], [[0, 0, 0]]),
                    CorrelationParameters(17, 2, [0], [[0, 0]]),
                    CorrelationParameters(2, 2, [0], [[0, 0]]),
                    CorrelationParameters(18, 2, [0], [[0, 0]]),
                    CorrelationParameters(18, 2, [0], [[0, 0]])]
            else:
                raise ValueError("invalid correlation pass count")
        elif (channel_count == 2):
            if (correlation_passes == 0):
                correlation_parameters = []
            elif (correlation_passes == 1):
                correlation_parameters = [
                    CorrelationParameters(18, 2, [0, 0], [[0, 0],
                                                          [0, 0]])]
            elif (correlation_passes == 2):
                correlation_parameters = [
                    CorrelationParameters(17, 2, [0, 0], [[0, 0],
                                                          [0, 0]]),
                    CorrelationParameters(18, 2, [0, 0], [[0, 0],
                                                          [0, 0]])]
            elif (correlation_passes == 5):
                correlation_parameters = [
                    CorrelationParameters(3, 2, [0, 0], [[0, 0, 0],
                                                         [0, 0, 0]]),
                    CorrelationParameters(17, 2, [0, 0], [[0, 0],
                                                          [0, 0]]),
                    CorrelationParameters(2, 2, [0, 0], [[0, 0],
                                                         [0, 0]]),
                    CorrelationParameters(18, 2, [0, 0], [[0, 0],
                                                          [0, 0]]),
                    CorrelationParameters(18, 2, [0, 0], [[0, 0],
                                                          [0, 0]])]
            elif (correlation_passes == 10):
                raise NotImplementedError()
            elif (correlation_passes == 16):
                raise NotImplementedError()
            else:
                raise ValueError("invalid correlation pass count")


        entropy_variables = [[0, 0, 0], [0, 0, 0]]

        return cls(channel_count, correlation_parameters, entropy_variables)


def block_parameters(channel_count, channel_mask, correlation_passes):
    if (channel_count == 1):
        return [EncodingParameters.new(1, correlation_passes)]
    elif (channel_count == 2):
        return [EncodingParameters.new(2, correlation_passes)]
    elif ((channel_count == 3) and (channel_mask == 0x7)):
        #front left, front right, front center
        return [EncodingParameters.new(2, correlation_passes),
                EncodingParameters.new(1, correlation_passes)]
    elif ((channel_count == 4) and (channel_mask == 0x33)):
        #front left, front right, back left, back right
        return [EncodingParameters.new(2, correlation_passes),
                EncodingParameters.new(2, correlation_passes)]
    elif ((channel_count == 4) and (channel_mask == 0x107)):
        #front left, front right, front center, back center
        return [EncodingParameters.new(2, correlation_passes),
                EncodingParameters.new(1, correlation_passes),
                EncodingParameters.new(1, correlation_passes)]
    elif ((channel_count == 5) and (channel_mask == 0x37)):
        #front left, front right, front center, back left, back right
        return [EncodingParameters.new(2, correlation_passes),
                EncodingParameters.new(1, correlation_passes),
                EncodingParameters.new(2, correlation_passes)]
    elif ((channel_count == 6) and (channel_mask == 0x3F)):
        #front left, front right, front center, LFE, back left, back right
        return [EncodingParameters.new(2, correlation_passes),
                EncodingParameters.new(1, correlation_passes),
                EncodingParameters.new(1, correlation_passes),
                EncodingParameters.new(2, correlation_passes)]
    else:
        return [EncodingParameters.new(1, correlation_passes)
                for c in xrange(channel_count)]


def encode_wavpack(filename, pcmreader, block_size,
                   correlation_passes=0):
    pcmreader = BufferedPCMReader(pcmreader)
    output_file = open(filename, "wb")
    writer = BitstreamWriter(output_file, 1)
    context = EncoderContext(pcmreader, block_parameters(pcmreader.channels,
                                                         pcmreader.channel_mask,
                                                         correlation_passes))

    block_index = 0

    #walk through PCM reader's FrameLists
    frame = pcmreader.read(block_size *
                           (pcmreader.bits_per_sample / 8) *
                           pcmreader.channels)
    while (len(frame) > 0):
        #FIXME - update MD5 sum with framelist data

        c = 0
        for parameters in context.block_parameters:
            if (parameters.channel_count == 1):
                channel_data = [list(frame.channel(c))]
            else:
                channel_data = [list(frame.channel(c)),
                                list(frame.channel(c + 1))]

            first_block = parameters is context.block_parameters[0]
            last_block = parameters is context.block_parameters[-1]

            context.block_offsets.append(output_file.tell())
            write_block(writer, context, channel_data, block_index,
                        first_block, last_block, parameters)

            c += parameters.channel_count

        block_index += frame.frames
        frame = pcmreader.read(block_size *
                               (pcmreader.bits_per_sample / 8) *
                               pcmreader.channels)

    #FIXME - write final block containing Wave footer and MD5 sum
    #FIXME - update Wave haeder's "data" chunk size, if generated

    #go back and populate block headers with total samples
    for block_offset in context.block_offsets:
        output_file.seek(block_offset + 12, 0)
        writer.write(32, block_index)

    writer.close()


def write_block(writer, context, channels, block_index,
                first_block, last_block, encoding_parameters):
    assert((len(channels) == 1) or (len(channels) == 2))

    if ((len(channels) == 1) or (channels[0] == channels[1])):
        #1 channel block
        if (len(channels) == 1):
            false_stereo = 0
        else:
            false_stereo = 1
        #calculate maximum magnitude of channel_0
        magnitude = max(map(bits, channels[0]))

        #determine wasted bits
        wasted = min(map(wasted_bps, channels[0]))

        #if wasted bits, remove them from channel_0
        if ((wasted > 0) and (wasted != INFINITY)):
            shifted = [[s >> wasted] for s in channels[0]]
        else:
            shifted = channels

        #calculate CRC of shifted_0
        crc = calculate_crc(shifted)
    else:
        #2 channel block
        false_stereo = 0

        #calculate maximum magnitude of channel_0/channel_1
        magnitude = max(max(map(bits, channels[0])),
                        max(map(bits, channels[1])))

        #determine wasted bits
        wasted = min(min(map(wasted_bps, channels[0])),
                     min(map(wasted_bps, channels[1])))

        #if wasted bits, remove them from channel_0/channel_1
        if ((wasted > 0) and (wasted != INFINITY)):
            shifted = [[s >> wasted for s in channels[0]],
                       [s >> wasted for s in channels[1]]]
        else:
            shifted = channels

        #calculate CRC of shifted_0/shifted_1
        crc = calculate_crc(shifted)

        #joint stereo conversion of shifted_0/shifted_1 to mid/side channels
        mid_side = joint_stereo(shifted[0], shifted[1])

    sub_blocks = BitstreamRecorder(1)
    sub_block = BitstreamRecorder(1)

    #FIXME - if first block in file, write Wave header

    #FIXME - if correlation passes, write three sub blocks of pass data
    if (len(encoding_parameters.correlation_parameters) > 0):
        raise NotImplementedError()

    #FIXME - if wasted bits, write extended integers sub block
    if (wasted > 0):
        raise NotImplementedError()

    #FIXME - if channel count > 2, write channel info sub block

    #FIXME - if nonstandard sample rate, write sample rate sub block

    if ((len(channels) == 1) or (channels[0] == channels[1])):
        #1 channel block

        #FIXME - correlate shifted_0 with terms/deltas/weights/samples

        #write entropy variables sub block
        sub_block.reset()
        write_entropy_variables(sub_block, channels,
                                encoding_parameters.entropy_variables)
        write_sub_block(sub_blocks, 5, 0, sub_block)


        #write bitstream sub block
        sub_block.reset()
        write_bitstream(sub_block, channels,
                        encoding_parameters.entropy_variables)
        write_sub_block(sub_blocks, 10, 0, sub_block)
    else:
        #2 channel block

        #FIXME - correlate shifted_0/shifted_1 with terms/deltas/weights/samples

        #write entropy variables sub block
        sub_block.reset()
        write_entropy_variables(sub_block, shifted,
                                encoding_parameters.entropy_variables)
        write_sub_block(sub_blocks, 5, 0, sub_block)

        #FIXME - write bitstream sub block
        sub_block.reset()
        write_bitstream(sub_block, mid_side,
                        encoding_parameters.entropy_variables)
        write_sub_block(sub_blocks, 10, 0, sub_block)

    #write block header with size of all sub blocks
    write_block_header(writer,
                       sub_blocks.bytes(),
                       block_index,
                       len(channels[0]),
                       context.pcmreader.bits_per_sample,
                       len(channels),
                       (len(channels) == 2) and (false_stereo == 0),
                       len(encoding_parameters.correlation_parameters),
                       wasted,
                       first_block,
                       last_block,
                       magnitude,
                       context.pcmreader.sample_rate,
                       false_stereo,
                       crc)

    #write sub block data to stream
    sub_blocks.copy(writer)

    #FIXME - update correlation weights/samples and entropy variables


def bits(sample):
    sample = abs(sample)
    total = 0
    while (sample > 0):
        total += 1
        sample >>= 1
    return total

INFINITY = 2 ** 32

def wasted_bps(sample):
    if (sample == 0):
        return INFINITY
    else:
        total = 0
        while ((sample % 2) == 0):
            total += 1
            sample /= 2
        return total


def calculate_crc(samples):
    crc = 0xFFFFFFFF

    for frame in zip(*samples):
        for s in frame:
            crc = 3 * crc + s

    if (crc >= 0):
        return crc % 0x100000000
    else:
        return (2 ** 32 - (-crc)) % 0x100000000


def joint_stereo(left, right):
    from itertools import izip

    mid = []
    side = []
    for (l, r) in izip(left, right):
        mid.append(l - r)
        side.append((l + r) / 2)
    return [mid, side]


def write_block_header(writer,
                       sub_blocks_size,
                       block_index,
                       block_samples,
                       bits_per_sample,
                       channel_count,
                       joint_stereo,
                       decorrelation_passes,
                       wasted_bps,
                       initial_block_in_sequence,
                       final_block_in_sequence,
                       maximum_magnitude,
                       sample_rate,
                       false_stereo,
                       CRC):
    writer.write_bytes("wvpk")             #block ID
    writer.write(32, sub_blocks_size + 24) #block size
    writer.write(16, 0x0410)               #version
    writer.write(8, 0)                     #track number
    writer.write(8, 0)                     #index number
    writer.write(32, 0xFFFFFFFF)           #total samples
    writer.write(32, block_index)
    writer.write(32, block_samples)
    writer.write(2, (bits_per_sample / 8) - 1)
    writer.write(1, 2 - channel_count)
    writer.write(1, 0)                     #hybrid mode
    writer.write(1, joint_stereo)
    if (decorrelation_passes > 5):         #cross-channel decorrelation
        writer.write(1, 1)
    else:
        writer.write(1, 0)
    writer.write(1, 0)                     #hybrid noise shaping
    writer.write(1, 0)                     #floating point data
    if (wasted_bps > 0):                   #extended size integers
        writer.write(1, 1)
    else:
        writer.write(1, 0)
    writer.write(1, 0)                     #hybrid controls bitrate
    writer.write(1, 0)                     #hybrid noise balanced
    writer.write(1, initial_block_in_sequence)
    writer.write(1, final_block_in_sequence)
    writer.write(5, 0)                     #left shift data
    writer.write(5, maximum_magnitude)
    writer.write(4, {6000:0,
                     8000:1,
                     9600:2,
                     11025:3,
                     12000:4,
                     16000:5,
                     22050:6,
                     24000:7,
                     32000:8,
                     44100:9,
                     48000:10,
                     64000:11,
                     88200:12,
                     96000:13,
                     192000:14}.get(sample_rate, 15))
    writer.write(2, 0)                     #reserved
    writer.write(1, 0)                     #use IIR
    writer.write(1, false_stereo)
    writer.write(1, 0)                     #reserved
    writer.write(32, CRC)

def write_sub_block(writer, function, nondecoder_data, recorder):
    recorder.byte_align()

    actual_size_1_less = recorder.bytes() % 2

    writer.build("5u 1u 1u",
                 (function,
                  nondecoder_data,
                  actual_size_1_less))

    if (recorder.bytes() > (255 * 2)):
        writer.write(1, 1)
        writer.write(24, (recorder.bytes() / 2) + actual_size_1_less)
    else:
        writer.write(1, 0)
        writer.write(8, (recorder.bytes() / 2) + actual_size_1_less)

    recorder.copy(writer)
    if (recorder.bytes() % 2):
        writer.write(8, 0)


def correlation_pass_1ch(uncorrelated_samples,
                         term, delta, weight, correlation_samples):
    if (term == 18):
        assert(len(correlation_samples) == 2)
        uncorrelated = ([correlation_samples[1],
                         correlation_samples[0]] +
                        uncorrelated_samples)
        correlated = []
        for i in xrange(2, len(uncorrelated)):
            temp = (3 * uncorrelated[i - 1] - uncorrelated[i - 2]) / 2
            correlated.append(uncorrelated[i] - apply_weight(weight, temp))
            weight += update_weight(temp, correlated[i - 2], delta)
        return correlated
    elif (term == 17):
        assert(len(correlation_samples) == 2)
        uncorrelated = ([correlation_samples[1],
                         correlation_samples[0]] +
                        uncorrelated_samples)
        correlated = []
        for i in xrange(2, len(uncorrelated)):
            temp = 2 * uncorrelated[i - 1] - uncorrelated[i - 2]
            correlated.append(uncorrelated[i] - apply_weight(weight, temp))
            weight += update_weight(temp, correlated[i - 2], delta)
        return correlated
    elif ((1 <= term) and (term <= 8)):
        assert(len(correlation_samples) == term)
        uncorrelated = correlation_samples[:] + uncorrelated_samples
        correlated = []
        for i in xrange(term, len(uncorrelated)):
            correlated.append(uncorrelated[i] -
                              apply_weight(weight, uncorrelated[i - term]))
            weight += update_weight(uncorrelated[i - term],
                                    correlated[i - term], delta)
        return correlated
    else:
        raise ValueError("unsupported term")


def correlation_pass_2ch(uncorrelated_samples,
                         term, delta, weights, correlation_samples):
    assert(len(uncorrelated_samples) == 2)
    assert(len(uncorrelated_samples[0]) == len(uncorrelated_samples[1]))
    assert(len(weights) == 2)

    if (((17 <= term) and (term <= 18)) or ((1 <= term) and (term <= 8))):
        return (correlation_pass_1ch(uncorrelated_samples[0],
                                     term, delta, weights[0],
                                     correlation_samples[0]),
                correlation_pass_1ch(uncorrelated_samples[1],
                                     term, delta, weights[1],
                                     correlation_samples[1]))
    elif ((-3 <= term) and (term <= -1)):
        assert(len(correlation_samples[0]) == 1)
        assert(len(correlation_samples[1]) == 1)
        uncorrelated = (correlation_samples[1] + uncorrelated_samples[0],
                        correlation_samples[0] + uncorrelated_samples[1])
        correlated = [[], []]
        weights = list(weights)
        if (term == -1):
            for i in xrange(1, len(uncorrelated[0])):
                correlated[0].append(uncorrelated[0][i] -
                                     apply_weight(weights[0],
                                                  uncorrelated[1][i - 1]))
                correlated[1].append(uncorrelated[1][i] -
                                     apply_weight(weights[1],
                                                  uncorrelated[0][i]))
                weights[0] += update_weight(uncorrelated[1][i - 1],
                                            correlated[0][-1],
                                            delta)
                weights[1] += update_weight(uncorrelated[0][i],
                                            correlated[1][-1],
                                            delta)
                weights[0] = max(min(weights[0], 1024), -1024)
                weights[1] = max(min(weights[1], 1024), -1024)
        elif (term == -2):
            for i in xrange(1, len(uncorrelated[0])):
                correlated[0].append(uncorrelated[0][i] -
                                     apply_weight(weights[0],
                                                  uncorrelated[1][i]))
                correlated[1].append(uncorrelated[1][i] -
                                     apply_weight(weights[1],
                                                  uncorrelated[0][i - 1]))
                weights[0] += update_weight(uncorrelated[1][i],
                                            correlated[0][-1],
                                            delta)
                weights[1] += update_weight(uncorrelated[0][i - 1],
                                            correlated[1][-1],
                                            delta)
                weights[0] = max(min(weights[0], 1024), -1024)
                weights[1] = max(min(weights[1], 1024), -1024)
        elif (term == -3):
            for i in xrange(1, len(uncorrelated[0])):
                correlated[0].append(uncorrelated[0][i] -
                                     apply_weight(weights[0],
                                                  uncorrelated[1][i - 1]))
                correlated[1].append(uncorrelated[1][i] -
                                     apply_weight(weights[1],
                                                  uncorrelated[0][i - 1]))
                weights[0] += update_weight(uncorrelated[1][i - 1],
                                            correlated[0][-1],
                                            delta)
                weights[1] += update_weight(uncorrelated[0][i - 1],
                                            correlated[1][-1],
                                            delta)
                weights[0] = max(min(weights[0], 1024), -1024)
                weights[1] = max(min(weights[1], 1024), -1024)

        return correlated
    else:
        raise ValueError("unsupported term")


def apply_weight(weight, sample):
    return ((weight * sample) + 512) >> 10


def update_weight(source, result, delta):
    if ((source == 0) or (result == 0)):
        return 0
    elif ((source ^ result) >= 0):
        return delta
    else:
        return -delta


def store_weight(w):
    raise NotImplementedError()


def restore_weight(v):
    raise NotImplementedError()


LOG2 = [0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x09, 0x0a,
        0x0b, 0x0d, 0x0e, 0x10, 0x11, 0x12, 0x14, 0x15,
        0x16, 0x18, 0x19, 0x1a, 0x1c, 0x1d, 0x1e, 0x20,
        0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a,
        0x2c, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34,
        0x36, 0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3d, 0x3e,
        0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48,
        0x49, 0x4a, 0x4b, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
        0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
        0x64, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
        0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d,
        0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
        0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
        0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
        0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9b, 0x9c,
        0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
        0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xa9, 0xaa, 0xab,
        0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb2,
        0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xb9,
        0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc0,
        0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc6, 0xc7,
        0xc8, 0xc9, 0xca, 0xcb, 0xcb, 0xcc, 0xcd, 0xce,
        0xcf, 0xd0, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd4,
        0xd5, 0xd6, 0xd7, 0xd8, 0xd8, 0xd9, 0xda, 0xdb,
        0xdc, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe0, 0xe1,
        0xe2, 0xe3, 0xe4, 0xe4, 0xe5, 0xe6, 0xe7, 0xe7,
        0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xee,
        0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3, 0xf4,
        0xf4, 0xf5, 0xf6, 0xf7, 0xf7, 0xf8, 0xf9, 0xf9,
        0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfe, 0xff, 0xff]


def wv_log2(value):
    from math import log

    a = abs(value) + (abs(value) / 2 ** 9)
    if (a != 0):
        c = int(log(a)  / log(2)) + 1
    else:
        c = 0
    if (value >= 0):
        if ((0 <= a) and (a < 256)):
            return c * 2 ** 8 + LOG2[(a * 2 ** (9 - c)) % 256]
        else:
            return c * 2 ** 8 + LOG2[(a / 2 ** (c - 9)) % 256]
    else:
        if ((0 <= a) and (a < 256)):
            return -(c * 2 ** 8 + LOG2[(a * 2 ** (9 - c)) % 256])
        else:
            return -(c * 2 ** 8 + LOG2[(a / 2 ** (c - 9)) % 256])


EXP2 = [0x100, 0x101, 0x101, 0x102, 0x103, 0x103, 0x104, 0x105,
        0x106, 0x106, 0x107, 0x108, 0x108, 0x109, 0x10a, 0x10b,
        0x10b, 0x10c, 0x10d, 0x10e, 0x10e, 0x10f, 0x110, 0x110,
        0x111, 0x112, 0x113, 0x113, 0x114, 0x115, 0x116, 0x116,
        0x117, 0x118, 0x119, 0x119, 0x11a, 0x11b, 0x11c, 0x11d,
        0x11d, 0x11e, 0x11f, 0x120, 0x120, 0x121, 0x122, 0x123,
        0x124, 0x124, 0x125, 0x126, 0x127, 0x128, 0x128, 0x129,
        0x12a, 0x12b, 0x12c, 0x12c, 0x12d, 0x12e, 0x12f, 0x130,
        0x130, 0x131, 0x132, 0x133, 0x134, 0x135, 0x135, 0x136,
        0x137, 0x138, 0x139, 0x13a, 0x13a, 0x13b, 0x13c, 0x13d,
        0x13e, 0x13f, 0x140, 0x141, 0x141, 0x142, 0x143, 0x144,
        0x145, 0x146, 0x147, 0x148, 0x148, 0x149, 0x14a, 0x14b,
        0x14c, 0x14d, 0x14e, 0x14f, 0x150, 0x151, 0x151, 0x152,
        0x153, 0x154, 0x155, 0x156, 0x157, 0x158, 0x159, 0x15a,
        0x15b, 0x15c, 0x15d, 0x15e, 0x15e, 0x15f, 0x160, 0x161,
        0x162, 0x163, 0x164, 0x165, 0x166, 0x167, 0x168, 0x169,
        0x16a, 0x16b, 0x16c, 0x16d, 0x16e, 0x16f, 0x170, 0x171,
        0x172, 0x173, 0x174, 0x175, 0x176, 0x177, 0x178, 0x179,
        0x17a, 0x17b, 0x17c, 0x17d, 0x17e, 0x17f, 0x180, 0x181,
        0x182, 0x183, 0x184, 0x185, 0x187, 0x188, 0x189, 0x18a,
        0x18b, 0x18c, 0x18d, 0x18e, 0x18f, 0x190, 0x191, 0x192,
        0x193, 0x195, 0x196, 0x197, 0x198, 0x199, 0x19a, 0x19b,
        0x19c, 0x19d, 0x19f, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a4,
        0x1a5, 0x1a6, 0x1a8, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad,
        0x1af, 0x1b0, 0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b6, 0x1b7,
        0x1b8, 0x1b9, 0x1ba, 0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0,
        0x1c2, 0x1c3, 0x1c4, 0x1c5, 0x1c6, 0x1c8, 0x1c9, 0x1ca,
        0x1cb, 0x1cd, 0x1ce, 0x1cf, 0x1d0, 0x1d2, 0x1d3, 0x1d4,
        0x1d6, 0x1d7, 0x1d8, 0x1d9, 0x1db, 0x1dc, 0x1dd, 0x1de,
        0x1e0, 0x1e1, 0x1e2, 0x1e4, 0x1e5, 0x1e6, 0x1e8, 0x1e9,
        0x1ea, 0x1ec, 0x1ed, 0x1ee, 0x1f0, 0x1f1, 0x1f2, 0x1f4,
        0x1f5, 0x1f6, 0x1f8, 0x1f9, 0x1fa, 0x1fc, 0x1fd, 0x1ff]


def wv_exp2(value):
    if ((-32768 <= value) and (value < -2304)):
        return -(EXP2[-value & 0xFF] << ((-value >> 8) - 9))
    elif ((-2304 <= value) and (value < 0)):
        return -(EXP2[-value & 0xFF] >> (9 - (-value >> 8)))
    elif ((0 <= value) and (value <= 2304)):
        return EXP2[value & 0xFF] >> (9 - (value >> 8))
    elif ((2304 < value) and (value <= 32767)):
        return EXP2[value & 0xFF] << ((value >> 8) - 9)
    else:
        raise ValueError("%s not a signed 16-bit value" % (value))


def write_entropy_variables(writer, channels, entropies):
    if (len(channels) == 2):
        for e in entropies[0]:
            writer.write(16, wv_log2(e))
        for e in entropies[1]:
            writer.write(16, wv_log2(e))
    else:
        for e in entropies[0]:
            writer.write(16, wv_log2(e))


def write_bitstream(writer, channels, entropies):
    from math import log

    assert((len(channels) == 1) or (len(channels) == 2))
    assert(len(set(map(len, channels))) == 1)

    #residual_(i - 1)
    residual = None

    #u_(i - 2)
    u = None

    i = 0
    total_samples = len(channels) * len(channels[0])
    while (i < total_samples):
        sample = channels[i % len(channels)][i / len(channels)]
        if ((u is None) and (entropies[0][0] < 2) and (entropies[1][0] < 2)):
            #handle long run of 0 residuals

            if (residual is not None):
                residual.flush(writer, None, 0)

            zeroes = 0
            while ((sample == 0) and (i < total_samples)):
                zeroes += 1
                i += 1
                sample = channels[i % len(channels)][i / len(channels)]
            write_egc(writer, zeroes)
            if (zeroes > 0):
                entropies = [[0, 0, 0], [0, 0, 0]]
            (u, residual) = write_residual(writer, sample,
                                           entropies[i % (len(channels))],
                                           u, residual)
            i += 1
        else:
            (u, residual) = write_residual(writer, sample,
                                           entropies[i % (len(channels))],
                                           u, residual)
            i += 1

    if (residual is not None):
        residual.flush(writer, u, 0)

    return entropies


def write_residual(writer, sample, entropy, prev_u, prev_residual):
    if (sample >= 0):
        unsigned = sample
        sign = 0
    else:
        unsigned = -sample - 1
        sign = 1

    medians = [e / 2 ** 4 + 1 for e in entropy]

    if (unsigned < medians[0]):
        m = 0
        offset = unsigned
        add = medians[0] - 1
        entropy[0] -= ((entropy[0] + 126) / 128) * 2
    elif ((unsigned - medians[0]) < medians[1]):
        m = 1
        offset = unsigned - medians[0]
        add = medians[1] - 1
        entropy[0] += ((entropy[0] + 128) / 128) * 5
        entropy[1] -= ((entropy[1] + 62) / 64) * 2
    elif ((unsigned - (medians[0] + medians[1])) < medians[2]):
        m = 2
        offset = unsigned - (medians[0] + medians[1])
        add = medians[2] - 1
        entropy[0] += ((entropy[0] + 128) / 128) * 5
        entropy[1] += ((entropy[1] + 64) / 64) * 5
        entropy[2] -= ((entropy[2] + 30) / 32) * 2
    else:
        m = ((unsigned - (medians[0] + medians[1])) / medians[2]) + 2
        offset = unsigned - (medians[0] + medians[1] + ((m - 2) * medians[2]))
        add = medians[2] - 1
        entropy[0] += ((entropy[0] + 128) / 128) * 5
        entropy[1] += ((entropy[1] + 64) / 64) * 5
        entropy[2] += ((entropy[2] + 32) / 32) * 5

    if (prev_residual is not None):
        u = prev_residual.flush(writer, prev_u, m)
    else:
        u = None

    return (u, Residual(m, offset, add, sign))


class Residual:
    def __init__(self, m, offset, add, sign):
        self.m = m
        self.offset = offset
        self.add = add
        self.sign = sign

    def flush(self, writer, prev_u, next_m):
        return flush_residual(writer, prev_u, self.m, next_m,
                              self.offset, self.add, self.sign)


def flush_residual(writer, prev_u, m, next_m, offset, add, sign):
    """given u_(i - 1), m_(i) and m_(i + 1)
    along with offset_(i), add_(i) and sign_(i) values,
    writes the given residual to the output stream
    and returns u_(i)
    """

    from math import log

    #determine u_(i)
    if ((m > 0) and (next_m > 0)):
        #positive m to positive m
        if ((prev_u is None) or (prev_u % 2 == 0)):
            u_i = (m * 2) + 1
        else:
            #passing 1 from previous u
            u_i = (m * 2) - 1
    elif ((m == 0) and (next_m > 0)):
        #zero m to positive m
        if ((prev_u is None) or (prev_u % 2 == 1)):
            u_i = 1
        else:
            #passing 0 from previous u
            u_i = None
    elif ((m > 0) and (next_m == 0)):
        #positive m to zero m
        if ((prev_u is None) or (prev_u % 2 == 0)):
            u_i = m * 2
        else:
            #passing 1 from previous u
            u_i = (m - 1) * 2
    elif ((m == 0) and (next_m == 0)):
        #zero m to zero m
        if ((prev_u is None) or (prev_u % 2 == 1)):
            u_i = 0
        else:
            #passing 0 from previous u
            u_i = None
    else:
        raise ValueError("invalid m")

    if (u_i is not None):
        if (u_i < 16):
            writer.unary(0, u_i)
        else:
            writer.unary(0, 16)
            write_egc(writer, u_i - 16)

    if (add > 0):
        p = int(log(add) / log(2))
        e = 2 ** (p + 1) - add - 1
        if (offset < e):
            writer.write(p, offset)
        else:
            writer.write(p, (offset + e) / 2)
            writer.write(1, (offset + e % 2))

    writer.write(1, sign)

    return u_i


def write_egc(writer, value):
    from math import log

    assert(value >= 0)

    if (value > 1):
        t = int(log(value) / log(2)) + 1
        writer.unary(0, t)
        writer.write(t - 1, value % (2 ** (t - 1)))
    else:
        writer.unary(0, value)


if (__name__ == '__main__'):
    from audiotools.bitstream import BitstreamWriter
    import sys

    w = BitstreamWriter(open(sys.argv[1], "wb"), 1)

    medians = [[118, 194, 322], [118, 176, 212]]
    # medians = [[0, 0, 0], [0, 0, 0]]
    channels = [[-61, -33, -18, 1, 20, 35, 50, 62, 68, 71],
                [31, 32, 36, 37, 35, 31, 25, 18, 10, 0]]

    write_bitstream(w, channels, medians)
    w.byte_align()
    w.close()

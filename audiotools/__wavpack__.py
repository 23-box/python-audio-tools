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


from audiotools import (AudioFile, InvalidFile, subprocess, BIN,
                        open_files, os, ReplayGain, ignore_sigint,
                        transfer_data, transfer_framelist_data,
                        BufferedPCMReader, Image, MetaData, sheet_to_unicode,
                        calculate_replay_gain, ApeTagItem,
                        EncodingError, DecodingError, PCMReaderError,
                        PCMReader, ChannelMask,
                        InvalidWave, __default_quality__,
                        WaveContainer, to_pcm_progress)
from __wav__ import WaveAudio
from __ape__ import ApeTaggedAudio, ApeTag, __number_pair__
import gettext

gettext.install("audiotools", unicode=True)


class InvalidWavPack(InvalidFile):
    pass

def __riff_chunk_ids__(data_size, data):
    data_size = __Counter__(data_size)
    data.add_callback(data_size.callback)
    (riff, size, wave) = data.parse("4b 32u 4b")
    if (riff != "RIFF"):
        return
    elif (wave != "WAVE"):
        return

    while (int(data_size) > 0):
        (chunk_id, chunk_size) = data.parse("4b 32u")
        if ((chunk_size % 2) == 1):
            chunk_size += 1
        yield chunk_id
        if (chunk_id != 'data'):
            data.skip_bytes(chunk_size)

class __Counter__:
    def __init__(self, value):
        self.value = value

    def callback(self, byte):
        self.value -= 1

    def __int__(self):
        return self.value


#######################
#WavPack APEv2
#######################


class WavPackAPEv2(ApeTag):
    """A WavPack-specific APEv2 implementation with minor differences."""

    def __init__(self, tags, tag_length=None, frame_count=0):
        """Constructs an ApeTag from a list of ApeTagItem objects.

        tag_length is an optional total length integer.
        frame_count is an optional number of PCM frames
        to be used by cuesheets."""

        ApeTag.__init__(self, tags=tags, tag_length=tag_length)
        self.frame_count = frame_count

    def __comment_pairs__(self):
        return filter(lambda pair: pair[0] != 'Cuesheet',
                      ApeTag.__comment_pairs__(self))

    def __unicode__(self):
        if ('Cuesheet' not in self.keys()):
            return ApeTag.__unicode__(self)
        else:
            import cue

            try:
                return u"%s%sCuesheet:\n%s" % \
                    (MetaData.__unicode__(self),
                     os.linesep * 2,
                     sheet_to_unicode(
                            cue.parse(
                                cue.tokens(unicode(self['Cuesheet']).encode(
                                        'ascii', 'replace'))),
                            self.frame_count))
            except cue.CueException:
                return ApeTag.__unicode__(self)

    @classmethod
    def converted(cls, metadata):
        """Converts a MetaData object to a WavPackAPEv2 object."""

        if ((metadata is None) or (isinstance(metadata, WavPackAPEv2))):
            return metadata
        elif (isinstance(metadata, ApeTag)):
            return WavPackAPEv2(metadata.tags)
        else:
            return WavPackAPEv2(ApeTag.converted(metadata).tags)

WavePackAPEv2 = WavPackAPEv2

#######################
#WavPack
#######################


class WavPackAudio(ApeTaggedAudio, WaveContainer):
    """A WavPack audio file."""

    SUFFIX = "wv"
    NAME = SUFFIX
    DEFAULT_COMPRESSION = "standard"
    COMPRESSION_MODES = ("veryfast", "fast", "standard", "high", "veryhigh")
    COMPRESSION_DESCRIPTIONS = {"veryfast": _(u"fastest encode/decode, " +
                                              u"worst compression"),
                                "veryhigh": _(u"slowest encode/decode, " +
                                              u"best compression")}

    APE_TAG_CLASS = WavPackAPEv2

    BITS_PER_SAMPLE = (8, 16, 24, 32)
    SAMPLING_RATE = (6000,  8000,  9600,   11025,
                     12000, 16000, 22050,  24000,
                     32000, 44100, 48000,  64000,
                     88200, 96000, 192000, 0)

    __options__ = {"veryfast": {"block_size": 44100,
                                "joint_stereo": True,
                                "false_stereo": True,
                                "wasted_bits": True,
                                "decorrelation_passes": 1},
                   "fast": {"block_size": 44100,
                            "joint_stereo": True,
                            "false_stereo": True,
                            "wasted_bits": True,
                            "decorrelation_passes": 2},
                   "standard": {"block_size": 44100,
                                "joint_stereo": True,
                                "false_stereo": True,
                                "wasted_bits": True,
                                "decorrelation_passes": 5},
                   "high": {"block_size": 44100,
                            "joint_stereo": True,
                            "false_stereo": True,
                            "wasted_bits": True,
                            "decorrelation_passes": 10},
                   "veryhigh": {"block_size": 44100,
                                "joint_stereo": True,
                                "false_stereo": True,
                                "wasted_bits": True,
                                "decorrelation_passes": 16}}

    def __init__(self, filename):
        """filename is a plain string."""

        self.filename = filename
        self.__samplerate__ = 0
        self.__channels__ = 0
        self.__bitspersample__ = 0
        self.__total_frames__ = 0

        try:
            self.__read_info__()
        except IOError, msg:
            raise InvalidWavPack(str(msg))

    @classmethod
    def is_type(cls, file):
        """Returns True if the given file object describes this format.

        Takes a seekable file pointer rewound to the start of the file."""

        return file.read(4) == 'wvpk'

    def lossless(self):
        """Returns True."""

        return True

    def channel_mask(self):
        """Returns a ChannelMask object of this track's channel layout."""

        return self.__channel_mask__

    def get_metadata(self):
        """Returns a MetaData object, or None.

        Raises IOError if unable to read the file."""

        metadata = ApeTaggedAudio.get_metadata(self)
        if (metadata is not None):
            metadata.frame_count = self.total_frames()
        return metadata

    def has_foreign_riff_chunks(self):
        """Returns True if the audio file contains non-audio RIFF chunks.

        During transcoding, if the source audio file has foreign RIFF chunks
        and the target audio format supports foreign RIFF chunks,
        conversion should be routed through .wav conversion
        to avoid losing those chunks."""

        for (sub_header, nondecoder, data_size, data) in self.sub_blocks():
            if ((sub_header == 1) and nondecoder):
                if (set(__riff_chunk_ids__(data_size, data)) !=
                    set(['fmt ', 'data'])):
                    return True
            elif ((sub_header == 2) and nondecoder):
                return True
        else:
            return False

    def blocks(self, reader=None):
        """yields (length, reader) tuples of WavPack frames

        length is the total length of all the substreams
        reader is a BitstreamReader which can be parsed
        """

        if (reader is None):
            from .bitstream import BitstreamReader

            reader = BitstreamReader(file(self.filename), 1)
            try:
                for block in self.__blocks__(reader):
                    yield block
            finally:
                reader.close()
        else:
            for block in self.__blocks__(reader):
                yield block

    def __blocks__(self, reader):
        try:
            while (True):
                (wvpk, block_size) = reader.parse("4b 32u 192p")
                if (wvpk == 'wvpk'):
                    yield (block_size - 24,
                           reader.substream(block_size - 24))
                else:
                    return
        except IOError:
            return

    def sub_blocks(self, reader=None):
        """yields (function, nondecoder, data_size, data) tuples

        function is an integer
        nondecoder is a boolean indicating non-decoder data
        data is a BitstreamReader which can be parsed
        """

        for (frame_size, frame_data) in self.blocks(reader):
            frame_size = __Counter__(frame_size)
            frame_data.add_callback(frame_size.callback)
            while (int(frame_size) > 0):
                (metadata_function,
                 nondecoder_data,
                 actual_size_1_less,
                 large_block) = frame_data.parse("5u 1u 1u 1u")

                block_size = frame_data.read(24 if large_block else 8)
                if (actual_size_1_less):
                    yield (metadata_function,
                           nondecoder_data,
                           block_size * 2 - 1,
                           frame_data.substream(block_size * 2 - 1))
                    frame_data.skip(8)
                else:
                    yield (metadata_function,
                           nondecoder_data,
                           block_size * 2,
                           frame_data.substream(block_size * 2))


    def __read_info__(self):
        from .bitstream import BitstreamReader

        reader = BitstreamReader(file(self.filename, "rb"), 1)
        reader.mark()
        try:
            (block_id,
             total_samples,
             bits_per_sample,
             mono_output,
             initial_block,
             final_block,
             sample_rate) = reader.parse(
                "4b 64p 32u 64p 2u 1u 8p 1u 1u 5p 5p 4u 37p")

            if (block_id != 'wvpk'):
                raise InvalidWavPack(_(u'WavPack header ID invalid'))

            if (sample_rate != 0xF):
                self.__samplerate__ = WavPackAudio.SAMPLING_RATE[sample_rate]
            else:
                #if unknown, pull from SAMPLE_RATE sub-block
                for (block_id,
                     nondecoder,
                     data_size,
                     data) in self.sub_blocks(reader):
                    if ((block_id == 0x7) and nondecoder):
                        self.__samplerate__ = data.read(data_size * 8)
                        break
                else:
                    #no SAMPLE RATE sub-block found
                    #so pull info from FMT chunk
                    reader.rewind()
                    (self.__samplerate__,) = self.fmt_chunk(reader).parse(
                        "32p 32u")

            self.__bitspersample__ = [8,16,24,32][bits_per_sample]
            self.__total_frames__ = total_samples

            if (initial_block and final_block):
                if (mono_output):
                    self.__channels__ = 1
                    self.__channel_mask__ = ChannelMask(0x4)
                else:
                    self.__channels__ = 2
                    self.__channel_mask__ = ChannelMask(0x3)
            else:
                #if not mono or stereo, pull from CHANNEL INFO sub-block
                reader.rewind()
                for (block_id,
                     nondecoder,
                     data_size,
                     data) in self.sub_blocks(reader):
                    if ((block_id == 0xD) and not nondecoder):
                        self.__channels__ = data.read(8)
                        self.__channel_mask__ = ChannelMask(
                            data.read((data_size - 1) * 8))
                        break
                else:
                    #no CHANNEL INFO sub-block found
                    #so pull info from FMT chunk
                    reader.rewind()
                    fmt = self.fmt_chunk(reader)
                    compression_code = fmt.read(16)
                    self.__channels__ = fmt.read(16)
                    if (compression_code == 1):
                        #this is theoretically possible
                        #with very old .wav files,
                        #but shouldn't happen in practice
                        self.__channel_mask__ = {
                            1:ChannelMask.from_fields(
                                front_center=True),
                            2:ChannelMask.from_fields(
                                front_left=True, front_right=True),
                            3:ChannelMask.from_fields(
                                front_left=True, front_right=True,
                                front_center=True),
                            4:ChannelMask.from_fields(
                                front_left=True, front_right=True,
                                back_left=True, back_right=True),
                            5:ChannelMask.from_fields(
                                front_left=True, front_right=True,
                                back_left=True, back_right=True,
                                front_center=True),
                            6:ChannelMask.from_fields(
                                front_left=True, front_right=True,
                                back_left=True, back_right=True,
                                front_center=True, low_frequency=True)
                            }.get(self.__channels__, ChannelMask(0))
                    elif (compression_code == 0xFFFE):
                        fmt.skip(128)
                        mask = fmt.read(32)
                        self.__channel_mask__ = ChannelMask(mask)
                    else:
                        raise InvalidWavPack(_(u"unsupported FMT compression"))

        finally:
            reader.unmark()
            reader.close()

    def bits_per_sample(self):
        """Returns an integer number of bits-per-sample this track contains."""

        return self.__bitspersample__

    def channels(self):
        """Returns an integer number of channels this track contains."""

        return self.__channels__

    def total_frames(self):
        """Returns the total PCM frames of the track as an integer."""

        return self.__total_frames__

    def sample_rate(self):
        """Returns the rate of the track's audio as an integer number of Hz."""

        return self.__samplerate__

    @classmethod
    def from_pcm(cls, filename, pcmreader, compression=None):
        """Encodes a new file from PCM data.

        Takes a filename string, PCMReader object
        and optional compression level string.
        Encodes a new audio file from pcmreader's data
        at the given filename with the specified compression level
        and returns a new WavPackAudio object."""

        from . import encoders

        if ((compression is None) or
            (compression not in cls.COMPRESSION_MODES)):
            compression = __default_quality__(cls.NAME)

        try:
            encoders.encode_wavpack(filename,
                                    BufferedPCMReader(pcmreader),
                                    **cls.__options__[compression])

            return cls(filename)
        except (ValueError, IOError), msg:
            cls.__unlink__(filename)
            raise EncodingError(str(msg))
        except Exception, err:
            cls.__unlink__(filename)
            raise err

    def to_wave(self, wave_filename, progress=None):
        """Writes the contents of this file to the given .wav filename string.

        Raises EncodingError if some error occurs during decoding."""

        from . import decoders

        try:
            f = open(wave_filename, 'wb')
        except IOError, msg:
            raise EncodingError(str(msg))

        (head, tail) = self.pcm_split()

        try:
            f.write(head)
            total_frames = self.total_frames()
            current_frames = 0
            decoder = decoders.WavPackDecoder(self.filename)
            frame = decoder.read(4096)
            while (len(frame) > 0):
                f.write(frame.to_bytes(False, self.bits_per_sample() > 8))
                current_frames += frame.frames
                if (progress is not None):
                    progress(current_frames, total_frames)
                frame = decoder.read(4096)
            f.write(tail)
            f.close()
        except IOError, msg:
            self.__unlink__(wave_filename)
            raise EncodingError(str(msg))

    def to_pcm(self):
        """Returns a PCMReader object containing the track's PCM data."""

        from . import decoders

        try:
            return decoders.WavPackDecoder(self.filename,
                                           self.__samplerate__)
        except (IOError, ValueError), msg:
            return PCMReaderError(error_message=str(msg),
                                  sample_rate=self.__samplerate__,
                                  channels=self.__channels__,
                                  channel_mask=int(self.channel_mask()),
                                  bits_per_sample=self.__bitspersample__)

    @classmethod
    def from_wave(cls, filename, wave_filename, compression=None,
                  progress=None):
        """Encodes a new AudioFile from an existing .wav file.

        Takes a filename string, wave_filename string
        of an existing WaveAudio file
        and an optional compression level string.
        Encodes a new audio file from the wave's data
        at the given filename with the specified compression level
        and returns a new WavPackAudio object."""

        from . import encoders

        if ((compression is None) or
            (compression not in cls.COMPRESSION_MODES)):
            compression = __default_quality__(cls.NAME)

        wave = WaveAudio(wave_filename)

        (head, tail) = wave.pcm_split()

        try:
            encoders.encode_wavpack(filename,
                                    to_pcm_progress(wave, progress),
                                    wave_header=head,
                                    wave_footer=tail,
                                    **cls.__options__[compression])

            return cls(filename)
        except (ValueError, IOError), msg:
            cls.__unlink__(filename)
            raise EncodingError(str(msg))
        except Exception, err:
            cls.__unlink__(filename)
            raise err

    def pcm_split(self):
        """Returns a pair of data strings before and after PCM data."""

        head = ""
        tail = ""

        for (sub_block_id, nondecoder, data_size, data) in self.sub_blocks():
            if ((sub_block_id == 1) and nondecoder):
                head = data.read_bytes(data_size)
            elif ((sub_block_id == 2) and nondecoder):
                tail = data.read_bytes(data_size)

        return (head, tail)

    def fmt_chunk(self, reader=None):
        """Returns the 'fmt' chunk as a BitstreamReader"""

        for (block_id,
             nondecoder,
             data_size,
             data) in self.sub_blocks(reader):
            if ((block_id == 1) and nondecoder):
                (riff, wave) = data.parse("4b 32p 4b")
                if ((riff != 'RIFF') or (wave != 'WAVE')):
                    raise InvalidWavPack(_(u'invalid FMT chunk'))
                else:
                    while (True):
                        (chunk_id, chunk_size) = data.parse("4b 32u")
                        if (chunk_id == 'fmt '):
                            return data.substream(chunk_size)
                        elif (chunk_id == 'data'):
                            raise InvalidWavPack(_(u'invalid FMT chunk'))
                        else:
                            data.skip_bytes(chunk_size)
        else:
            raise InvalidWavPack(_(u'FMT chunk not found in WavPack'))



    @classmethod
    def add_replay_gain(cls, filenames, progress=None):
        """Adds ReplayGain values to a list of filename strings.

        All the filenames must be of this AudioFile type.
        Raises ValueError if some problem occurs during ReplayGain application.
        """

        tracks = [track for track in open_files(filenames) if
                  isinstance(track, cls)]

        if (len(tracks) > 0):
            for (track,
                 track_gain,
                 track_peak,
                 album_gain,
                 album_peak) in calculate_replay_gain(tracks, progress):
                metadata = track.get_metadata()
                if (metadata is None):
                    metadata = WavPackAPEv2([])
                metadata["replaygain_track_gain"] = ApeTagItem.string(
                    "replaygain_track_gain",
                    u"%+1.2f dB" % (track_gain))
                metadata["replaygain_track_peak"] = ApeTagItem.string(
                    "replaygain_track_peak",
                    u"%1.6f" % (track_peak))
                metadata["replaygain_album_gain"] = ApeTagItem.string(
                    "replaygain_album_gain",
                    u"%+1.2f dB" % (album_gain))
                metadata["replaygain_album_peak"] = ApeTagItem.string(
                    "replaygain_album_peak",
                    u"%1.6f" % (album_peak))
                track.set_metadata(metadata)

    @classmethod
    def can_add_replay_gain(cls):
        """Returns True."""

        return True

    @classmethod
    def lossless_replay_gain(cls):
        """Returns True."""

        return True

    def replay_gain(self):
        """Returns a ReplayGain object of our ReplayGain values.

        Returns None if we have no values."""

        metadata = self.get_metadata()
        if (metadata is None):
            return None

        if (set(['replaygain_track_gain', 'replaygain_track_peak',
                 'replaygain_album_gain', 'replaygain_album_peak']).issubset(
                metadata.keys())):  # we have ReplayGain data
            try:
                return ReplayGain(
                    unicode(metadata['replaygain_track_gain'])[0:-len(" dB")],
                    unicode(metadata['replaygain_track_peak']),
                    unicode(metadata['replaygain_album_gain'])[0:-len(" dB")],
                    unicode(metadata['replaygain_album_peak']))
            except ValueError:
                return None
        else:
            return None

    def get_cuesheet(self):
        """Returns the embedded Cuesheet-compatible object, or None.

        Raises IOError if a problem occurs when reading the file."""

        import cue

        metadata = self.get_metadata()

        if ((metadata is not None) and ('Cuesheet' in metadata.keys())):
            try:
                return cue.parse(cue.tokens(
                        unicode(metadata['Cuesheet']).encode('utf-8',
                                                             'replace')))
            except cue.CueException:
                #unlike FLAC, just because a cuesheet is embedded
                #does not mean it is compliant
                return None
        else:
            return None

    def set_cuesheet(self, cuesheet):
        """Imports cuesheet data from a Cuesheet-compatible object.

        This are objects with catalog(), ISRCs(), indexes(), and pcm_lengths()
        methods.  Raises IOError if an error occurs setting the cuesheet."""

        import os.path
        import cue

        if (cuesheet is None):
            return

        metadata = self.get_metadata()
        if (metadata is None):
            metadata = WavPackAPEv2.converted(MetaData())

        metadata['Cuesheet'] = WavPackAPEv2.ITEM.string('Cuesheet',
                                                        cue.Cuesheet.file(
                cuesheet,
                os.path.basename(self.filename)).decode('ascii', 'replace'))
        self.set_metadata(metadata)

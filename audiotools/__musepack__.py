#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007  Brian Langenberger

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


from audiotools import AudioFile,InvalidFile,InvalidFormat,PCMReader,Con,subprocess,BIN,ApeTaggedAudio
from __wav__ import WaveAudio

#######################
#Musepack Audio
#######################

class NutValue(Con.Struct):
    def __init__(self, name):
        Con.Struct.__init__(
            self,name,
            Con.RepeatUntil(lambda obj,ctx: (obj & 0x80) == 0x00,
                            Con.UBInt8('data')),
            Con.Value("value",lambda ctx:NutValue.parse_data(ctx['data'])))

    @classmethod
    def parse_data(cls, data):
        i = 0
        for x in data:
            i = (i << 7) | (x & 0x7F)
        return i

    @classmethod
    def build_data(cls, value):
        data = [value & 0x7F]
        value = value >> 7

        while (value != 0):
            data.append(0x80 | (value & 0x7F))
            value = value >> 7

        data.reverse()
        return data

class NutStreamReader:
    NUT_HEADER = Con.Struct('nut_header',
                            Con.String('key',2),
                            NutValue('length'))
    
    def __init__(self, stream):
        self.stream = stream

    #iterates over a bunch of (key,data) tuples
    def packets(self):
        import string
        
        UPPERCASE = frozenset(string.ascii_uppercase)
        
        while (True):
            try:
                frame_header = self.NUT_HEADER.parse_stream(self.stream)
            except Con.core.FieldError:
                break
         
            if (not frozenset(frame_header.key).issubset(UPPERCASE)):
                break
         
            yield (frame_header.key,
                   self.stream.read(frame_header.length.value - 2 - \
                                    len(frame_header.length.data)))


class MusepackAudio(ApeTaggedAudio,AudioFile):
    SUFFIX = "mpc"
    DEFAULT_COMPRESSION = "standard"
    COMPRESSION_MODES = ("thumb","radio","standard","extreme","insane")
    BINARIES = ('mppdec','mppenc')

    MUSEPACK8_HEADER = Con.Struct('musepack8_header',
                                  Con.UBInt32('crc32'),
                                  Con.Byte('bitstream_version'),
                                  NutValue('sample_count'),
                                  NutValue('beginning_silence'),
                                  Con.Embed(Con.BitStruct(
        'flags',
        Con.Bits('sample_frequency',3),
        Con.Bits('max_used_bands',5),
        Con.Bits('channel_count',4),
        Con.Flag('mid_side_used'),
        Con.Bits('audio_block_frames',3))))
        

    #not sure about some of the flag locations
    #Musepack's header is very unusual
    MUSEPACK7_HEADER = Con.Struct('musepack7_header',
                                 Con.Const(Con.String('signature',3),'MP+'),
                                 Con.Byte('version'),
                                 Con.ULInt32('frame_count'),
                                 Con.ULInt16('max_level'),
                                 Con.Embed(
        Con.BitStruct('flags',
                      Con.Bits('profile',4),
                      Con.Bits('link',2),
                      Con.Bits('sample_frequency',2),
                      Con.Flag('intensity_stereo'),
                      Con.Flag('midside_stereo'),
                      Con.Bits('maxband',6))),
                                 Con.ULInt16('title_gain'),
                                 Con.ULInt16('title_peak'),
                                 Con.ULInt16('album_gain'),
                                 Con.ULInt16('album_peak'),
                                 Con.Embed(
        Con.BitStruct('more_flags',
                      Con.Bits('unused1',16),
                      Con.Bits('last_frame_length_low',4),
                      Con.Flag('true_gapless'),
                      Con.Bits('unused2',3),
                      Con.Flag('fast_seeking'),
                      Con.Bits('last_frame_length_high',7))),
                                 Con.Bytes('unknown',3),
                                 Con.Byte('encoder_version'))

    def __init__(self, filename):
        AudioFile.__init__(self, filename)
        f = file(filename,'rb')
        try:
            try:
                header = MusepackAudio.MUSEPACK7_HEADER.parse_stream(f)
            except Con.ConstError:
                raise InvalidFile('musepack signature incorrect')
        finally:
            f.close()

        header.last_frame_length = (header.last_frame_length_high << 4) | \
                                   header.last_frame_length_low

        self.__sample_rate__ = (44100,48000,
                                37800,32000)[header.sample_frequency]
        self.__total_samples__ = ((header.frame_count - 1 ) * 1152) + \
                                 header.last_frame_length

    def to_pcm(self):
        sub = subprocess.Popen([BIN['mppdec'],'--silent',
                                '--raw-le',
                                self.filename,'-'],
                               stdout=subprocess.PIPE)
        return PCMReader(sub.stdout,
                         sample_rate=self.sample_rate(),
                         channels=self.channels(),
                         bits_per_sample=self.bits_per_sample(),
                         process=sub)

    @classmethod
    def from_pcm(cls, filename, pcmreader, compression=None):
        import tempfile

        if (str(compression) not in cls.COMPRESSION_MODES):
            compression = cls.DEFAULT_COMPRESSION

        if (pcmreader.sample_rate not in (44100,48000,37800,32000)):
            raise InvalidFormat(
                "Musepack only supports sample rates 44100, 48000, 37800 and 32000")

        f = tempfile.NamedTemporaryFile(suffix=".wav")
        w = WaveAudio.from_pcm(f.name, pcmreader)
        sub = subprocess.Popen([BIN['mppenc'],
                                "--silent",
                                "--overwrite",
                                "--%s" % (compression),
                                w.filename,
                                filename])
        sub.wait()
        del(w)
        f.close()
        return MusepackAudio(filename)

    @classmethod
    def is_type(cls, file):
        return file.read(4) == 'MP+\x07'

    def sample_rate(self):
        return self.__sample_rate__

    def total_samples(self):
        return self.__total_samples__

    def channels(self):
        return 2

    def bits_per_sample(self):
        return 16

    def lossless(self):
        return False

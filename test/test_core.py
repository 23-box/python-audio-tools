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

import unittest
import audiotools
import random
import tempfile
import decimal
import os
import os.path
import test_streams
from hashlib import md5

from test_reorg import (parser, Variable_Reader, BLANK_PCM_Reader,
                        EXACT_BLANK_PCM_Reader, SHORT_PCM_COMBINATIONS,
                        MD5_Reader,
                        MiniFrameReader, Combinations,
                        TEST_COVER1, TEST_COVER2, TEST_COVER3, HUGE_BMP)

def do_nothing(self):
    pass

#add a bunch of decorator metafunctions like LIB_CORE
#which can be wrapped around individual tests as needed
for section in parser.sections():
    for option in parser.options(section):
        if (parser.getboolean(section, option)):
            vars()["%s_%s" % (section.upper(),
                              option.upper())] = lambda function: function
        else:
            vars()["%s_%s" % (section.upper(),
                              option.upper())] = lambda function: do_nothing


class BufferedPCMReader(unittest.TestCase):
    @LIB_CORE
    def test_pcm(self):
        def frame_lengths(reader, bytes):
            frame = reader.read(bytes)
            while (len(frame) > 0):
                yield frame.frames
                frame = reader.read(bytes)
            else:
                reader.close()

        #ensure our reader is generating randomly-sized frames
        reader = Variable_Reader(EXACT_BLANK_PCM_Reader(4096 * 100))
        self.assert_(len(set(frame_lengths(reader, 4096))) > 1)

        #then, ensure that wrapped our reader in a BufferedPCMReader
        #results in equal-sized frames
        reader = audiotools.BufferedPCMReader(
            Variable_Reader(EXACT_BLANK_PCM_Reader(4096 * 100)))
        #(make sure to account for bps/channels in frame_lengths())
        self.assertEqual(set(frame_lengths(reader, 4096 * 4)), set([4096]))

        #check that sample_rate, bits_per_sample, channel_mask and channels
        #pass-through properly
        for sample_rate in [32000, 44100, 48000, 192000]:
            for bits_per_sample in [8, 16, 24]:
                for (channels, channel_mask) in [(1, 0x4),
                                                 (2, 0x3),
                                                 (4, 0x33),
                                                 (6, 0x3F)]:
                    reader = BLANK_PCM_Reader(1,
                                              sample_rate=sample_rate,
                                              channels=channels,
                                              bits_per_sample=bits_per_sample,
                                              channel_mask=channel_mask)
                    reader2 = audiotools.BufferedPCMReader(reader)
                    self.assertEqual(reader.sample_rate, sample_rate)
                    self.assertEqual(reader.channels, channels)
                    self.assertEqual(reader.bits_per_sample, bits_per_sample)
                    self.assertEqual(reader.channel_mask, channel_mask)

                    self.assertEqual(reader2.sample_rate, sample_rate)
                    self.assertEqual(reader2.channels, channels)
                    self.assertEqual(reader2.bits_per_sample, bits_per_sample)
                    self.assertEqual(reader2.channel_mask, channel_mask)


        #finally, ensure that random-sized reads also work okay
        total_frames = 4096 * 1000
        reader = audiotools.BufferedPCMReader(
            Variable_Reader(EXACT_BLANK_PCM_Reader(total_frames)))
        while (total_frames > 0):
            frames = min(total_frames, random.choice(range(1, 1000)))
            frame = reader.read(frames * 4)
            self.assertEqual(frame.frames, frames)
            total_frames -= frame.frames


class CDDA(unittest.TestCase):
    @LIB_CUSTOM
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.bin = os.path.join(self.temp_dir, "Test.BIN")
        self.cue = os.path.join(self.temp_dir, "Test.CUE")

        bin_file = open(self.bin, "wb")
        # self.reader = MD5_Reader(EXACT_BLANK_PCM_Reader(69470436))
        self.reader = test_streams.Sine16_Stereo(69470436, 44100,
                                                 441.0, 0.50,
                                                 4410.0, 0.49, 1.0)
        audiotools.transfer_framelist_data(

            self.reader, bin_file.write)
        bin_file.close()

        f = open(self.cue, "w")
        f.write("""eJydkF1LwzAUQN8L/Q+X/oBxk6YfyVtoM4mu68iy6WudQ8qkHbNu+u9NneCc1IdCnk649xyuUQXk
epnpHGiOMU2Q+Z5xMCuLQs0tBOq92nTy7alus3b/AUeccL5/ZIHvZdLKWXkDjKcpIg2RszjxvYUy
09IUykCwanZNe2pAHrr6tXMjVtuZ+uG27l62Dk91T03VPG8np+oYwL1cK98DsEZmd4AE5CrXZU8c
O++wh2qzQxKc4X/S/l8vTQa3i7V2kWEap/iN57l66Pcjiq93IaWDUjpOyn9LETAVyASh1y0OR4Il
Fy3hYEs4qiXB6wOQULBQkOhCygalbISUUvrnACQVERfIr1scI4K5lk9od5+/""".decode('base64').decode('zlib'))
        f.close()

        self.sample_offset = audiotools.config.get_default("System",
                                                           "cdrom_read_offset",
                                                           "0")

    @LIB_CUSTOM
    def tearDown(self):
        for f in os.listdir(self.temp_dir):
            os.unlink(os.path.join(self.temp_dir, f))
        os.rmdir(self.temp_dir)

        audiotools.config.set_default("System",
                                      "cdrom_read_offset",
                                      self.sample_offset)

    @LIB_CUSTOM
    def test_cdda(self):
        cdda = audiotools.CDDA(self.cue)
        self.assertEqual(len(cdda), 4)
        checksum = md5()
        audiotools.transfer_framelist_data(
            audiotools.PCMCat(iter(cdda)),
            checksum.update)
        self.assertEqual(self.reader.hexdigest(),
                         checksum.hexdigest())

    @LIB_CUSTOM
    def test_cdda_positive_offset(self):
        audiotools.config.set_default("System",
                                      "cdrom_read_offset",
                                      str(10))
        cdda = audiotools.CDDA(self.cue)
        reader_checksum = md5()
        cdrom_checksum = md5()
        audiotools.transfer_framelist_data(
            audiotools.PCMCat(iter(cdda)),
            cdrom_checksum.update)
        self.reader.reset()
        audiotools.transfer_framelist_data(
            audiotools.PCMReaderWindow(self.reader,
                                       10,
                                       69470436),
            reader_checksum.update)
        self.assertEqual(reader_checksum.hexdigest(),
                         cdrom_checksum.hexdigest())

    @LIB_CUSTOM
    def test_cdda_negative_offset(self):
        audiotools.config.set_default("System",
                                      "cdrom_read_offset",
                                      str(-10))
        cdda = audiotools.CDDA(self.cue)
        reader_checksum = md5()
        cdrom_checksum = md5()
        audiotools.transfer_framelist_data(
            audiotools.PCMCat(iter(cdda)),
            cdrom_checksum.update)
        self.reader.reset()
        audiotools.transfer_framelist_data(
            audiotools.PCMReaderWindow(self.reader,
                                       -10,
                                       69470436),
            reader_checksum.update)
        self.assertEqual(reader_checksum.hexdigest(),
                         cdrom_checksum.hexdigest())


class ChannelMask(unittest.TestCase):
    @LIB_CORE
    def test_mask(self):
        mask = audiotools.ChannelMask.from_fields()
        self.assert_(not mask.defined())
        self.assert_(mask.undefined())
        self.assertEqual(len(mask), 0)
        self.assertEqual(set([]), set(mask.channels()))
        mask2 = audiotools.ChannelMask(int(mask))
        self.assertEqual(mask, mask2)

        mask_fields = audiotools.ChannelMask.SPEAKER_TO_MASK.keys()
        for count in xrange(1, len(mask_fields) + 1):
            for fields in Combinations(mask_fields, count):
                #build a mask from fields
                mask = audiotools.ChannelMask.from_fields(
                    **dict([(field, True) for field in fields]))
                self.assert_(mask.defined())
                self.assert_(not mask.undefined())
                self.assertEqual(len(mask), len(fields))
                self.assertEqual(set(fields), set(mask.channels()))
                mask2 = audiotools.ChannelMask(int(mask))
                self.assertEqual(mask, mask2)


class ImageJPEG(unittest.TestCase):
    @LIB_CORE
    def setUp(self):
        self.image = """/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYF
BgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoK
CgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAAVAAwDAREA
AhEBAxEB/8QAGAAAAgMAAAAAAAAAAAAAAAAAAAgGBwn/xAAfEAACAgMAAwEBAAAAAAAAAAACAwQG
AQUHCBITABn/xAAUAQEAAAAAAAAAAAAAAAAAAAAA/8QAFBEBAAAAAAAAAAAAAAAAAAAAAP/aAAwD
AQACEQMRAD8A1/qnmzp6JO6PSvLudoqjZKDsZE6HB1TZEllhrLpABrNnCiYApEhrTcuAUZAuPM8M
pXgsuQJhaPDbB1q18n0tn7pQIdUtOxjFJ2lZhbIZmNV7sIlRWPDOVtetWVg0lESvqLPmZh6mQLNd
eO/02mVjy4qMeLpYXONsnb+Pe131ehvCws+2vm53hPE2SB1c1aMw1RvVJemSn5Brh1jIQNJyq32q
90ODZrvzPZU/bOJy9hXdrLjyGxWKcas5FsZhrao/T6LPGcESmBkwWeSWISH8B+D/2Q==""".decode('base64')
        self.md5sum = "f8c43ff52c53aff1625979de47a04cec"
        self.width = 12
        self.height = 21
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/jpeg"

    @LIB_CORE
    def tearDown(self):
        pass

    @LIB_CORE
    def test_checksum(self):
        self.assertEqual(md5(self.image).hexdigest(), self.md5sum)

    @LIB_CORE
    def test_image(self):
        img = audiotools.Image.new(self.image, u"Description", 1)
        self.assertEqual(img.data, self.image)
        self.assertEqual(img.mime_type, self.mime_type)
        self.assertEqual(img.width, self.width)
        self.assertEqual(img.height, self.height)
        self.assertEqual(img.color_depth, self.bpp)
        self.assertEqual(img.color_count, self.colors)
        self.assertEqual(img.description, u"Description")
        self.assertEqual(img.type, 1)


class ImagePNG(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = """iVBORw0KGgoAAAANSUhEUgAAAAwAAAAVCAIAAAD9zpjjAAAAAXNSR0IArs4c6QAAAAlwSFlzAAAL
EwAACxMBAJqcGAAAAAd0SU1FB9kGBQA7LTgWUZgAAAAIdEVYdENvbW1lbnQA9syWvwAAANFJREFU
KM+9UrERgzAMfCUddy4pvIZZQPTsQOkBGAAxBgMwBBUTqGMHZqBSCuc4cO6SFLmokuT3698ymRk+
xQ1fxHegdV3btn092LZtHMdnse97WZYxRrtG13VN06QcZqaqIYQMBODIKdXDMADo+z7RE9HF9QFn
ZmY2sxCCqp5ZLzeIiJkBLMtycZFJKYpimqasmTOZWS7o/JhVVakqABFJPvJxInLmF5FzB2YWY3TO
ZTpExHuf8jsROefmec7Wwsx1XXvvAVCa+H7B9Of/9DPQAzSV43jVGYrtAAAAAElFTkSuQmCC""".decode('base64')
        self.md5sum = "31c4c5224327d5869aa6059bcda84d2e"
        self.width = 12
        self.height = 21
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/png"


class ImageCover1(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = TEST_COVER1
        self.md5sum = "dbb6a01eca6336381754346de71e052e"
        self.width = 500
        self.height = 500
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/jpeg"


class ImageCover2(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = TEST_COVER2
        self.md5sum = "2d348cf729c840893d672dd69476955c"
        self.width = 500
        self.height = 500
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/png"


class ImageCover3(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = TEST_COVER3
        self.md5sum = "534b107e88d3830eac7ce814fc5d0279"
        self.width = 100
        self.height = 100
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/jpeg"


class ImageGIF(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = """R0lGODdhDAAVAIQSAAAAAAoKCg0NDRUVFRkZGTIyMkBAQExMTF5eXmdnZ3Nzc4CAgJiYmKWlpc3N
zdPT0+bm5vn5+f///////////////////////////////////////////////////////ywAAAAA
DAAVAAAFPKAkjmRpnuiDmBAjRkNSKsfoFCVQLsuomwaDpOBAAYIoUaCR1P1MRAnP1BtNRwnBjiC6
loqSZ3JMLpvNIQA7""".decode('base64')
        self.md5sum = "1d4d36801b53c41d01086cbf9d0cb471"
        self.width = 12
        self.height = 21
        self.bpp = 8
        self.colors = 32
        self.mime_type = "image/gif"


class ImageBMP(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = """Qk0qAwAAAAAAADYAAAAoAAAADAAAABUAAAABABgAAAAAAPQCAAATCwAAEwsAAAAAAAAAAAAA////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////AAAA////////////////////////////////////////////gICAgICA////////////
////////////////zc3N////////////Z2dnDQ0N////////////////////gICAGRkZ////////
////////gICA////////////////gICAgICA////////////////////////MjIyzc3N////gICA
gICA////////////////////////////////AAAA////AAAA////////////////////////////
////////////CgoKpaWl////////////////////////////////////AAAAQEBAQEBA////////
////////////////////////QEBAQEBA////MjIyzc3N////////////////////////gICAgICA
////////////AAAA////////////////////zc3NMjIy////////////////////AAAA////////
////+fn5FRUVZ2dn////////////////////c3NzTExM////////09PTXl5e////////////////
////////5ubmmJiY////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////""".decode('base64')
        self.md5sum = "cb6ef2f7a458ab1d315c329f72ec9898"
        self.width = 12
        self.height = 21
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/x-ms-bmp"


class ImageTIFF(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = """SUkqAPwCAAD/////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
///T09NeXl7////////////////////////m5uaYmJj////////5+fkVFRVnZ2f/////////////
//////9zc3NMTEz////////////Nzc0yMjL///////////////////8AAAD/////////////////
//+AgICAgID///////////8AAAD///////////////////////////9AQEBAQED///8yMjLNzc3/
//////////////////////////////8AAABAQEBAQED/////////////////////////////////
//////8KCgqlpaX///////////////////////////////////8AAAD///8AAAD/////////////
//////////////////8yMjLNzc3///+AgICAgID///////////////////////+AgID/////////
//////+AgICAgID///////////////9nZ2cNDQ3///////////////////+AgIAZGRn///////+A
gICAgID////////////////////////////Nzc3///////8AAAD/////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//////////////////////////////8QAP4ABAABAAAAAAAAAAABAwABAAAADAAAAAEBAwABAAAA
FQAAAAIBAwADAAAAwgMAAAMBAwABAAAAAQAAAAYBAwABAAAAAgAAAA0BAgAzAAAAyAMAABEBBAAB
AAAACAAAABIBAwABAAAAAQAAABUBAwABAAAAAwAAABYBAwABAAAAQAAAABcBBAABAAAA9AIAABoB
BQABAAAA/AMAABsBBQABAAAABAQAABwBAwABAAAAAQAAACgBAwABAAAAAgAAAAAAAAAIAAgACAAv
aG9tZS9icmlhbi9EZXZlbG9wbWVudC9hdWRpb3Rvb2xzL3Rlc3QvaW1hZ2UudGlmZgAAAAAASAAA
AAEAAABIAAAAAQ==""".decode('base64')
        self.md5sum = "192ceb086d217421a5f151cc0afa3f05"
        self.width = 12
        self.height = 21
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/tiff"


class ImageHugeBMP(ImageJPEG):
    @LIB_CORE
    def setUp(self):
        self.image = HUGE_BMP.decode('bz2')
        self.md5sum = "558d875195829de829059fd4952fed46"
        self.width = 2366
        self.height = 2366
        self.bpp = 24
        self.colors = 0
        self.mime_type = "image/x-ms-bmp"


class PCMConverter(unittest.TestCase):
    @LIB_CORE
    def setUp(self):
        self.tempwav = tempfile.NamedTemporaryFile(suffix=".wav")

    @LIB_CORE
    def tearDown(self):
        self.tempwav.close()

    @LIB_CORE
    def test_conversions(self):
        for ((i_sample_rate,
              i_channels,
              i_channel_mask,
              i_bits_per_sample),
             (o_sample_rate,
              o_channels,
              o_channel_mask,
              o_bits_per_sample)) in Combinations(SHORT_PCM_COMBINATIONS, 2):

            # print "(%s,%s,%s,%s) -> (%s,%s,%s,%s)" % \
            #     (i_sample_rate,
            #      i_channels,
            #      i_channel_mask,
            #      i_bits_per_sample,
            #      o_sample_rate,
            #      o_channels,
            #      o_channel_mask,
            #      o_bits_per_sample)
            reader = BLANK_PCM_Reader(5,
                                      sample_rate=i_sample_rate,
                                      channels=i_channels,
                                      bits_per_sample=i_bits_per_sample,
                                      channel_mask=i_channel_mask)

            converter = audiotools.PCMConverter(reader,
                                                sample_rate=o_sample_rate,
                                                channels=o_channels,
                                                bits_per_sample=o_bits_per_sample,
                                                channel_mask=o_channel_mask)
            wave = audiotools.WaveAudio.from_pcm(self.tempwav.name, converter)
            converter.close()

            self.assertEqual(wave.sample_rate(), o_sample_rate)
            self.assertEqual(wave.channels(), o_channels)
            self.assertEqual(wave.bits_per_sample(), o_bits_per_sample)
            self.assertEqual(wave.channel_mask(), o_channel_mask)
            self.assertEqual(
                (decimal.Decimal(wave.cd_frames()) / 75).to_integral(),
                5)


class PCMReaderWindow(unittest.TestCase):
    @LIB_CORE
    def setUp(self):
        self.channels = [range(0, 20),
                         range(20, 0, -1)]

    def __test_reader__(self, pcmreader, channels):
        framelist = pcmreader.read(1024)
        output_channels = [[] for i in xrange(len(channels))]
        while (len(framelist) > 0):
            for c in xrange(framelist.channels):
                output_channels[c].extend(framelist.channel(c))
            framelist = pcmreader.read(1024)
        self.assertEqual(channels, output_channels)

    @LIB_CORE
    def test_basic(self):
        self.__test_reader__(MiniFrameReader(self.channels,
                                             44100, 3, 16),
                             [range(0, 20), range(20, 0, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), 0, 20),
                             [range(0, 20), range(20, 0, -1)])

    @LIB_CORE
    def test_crop(self):
        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), 0, 15),
                             [range(0, 15), range(20, 5, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), 5, 15),
                             [range(5, 20), range(15, 0, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), 5, 10),
                             [range(5, 15), range(15, 5, -1)])

    @LIB_CORE
    def test_extend(self):
        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), -5, 25),
                             [[0] * 5 + range(0, 20),
                              [0] * 5 + range(20, 0, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), 0, 25),
                             [range(0, 20) + [0] * 5,
                              range(20, 0, -1) + [0] * 5])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), -5, 20),
                             [[0] * 5 + range(0, 15),
                              [0] * 5 + range(20, 5, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), -5, 15),
                             [[0] * 5 + range(0, 10),
                              [0] * 5 + range(20, 10, -1)])

        self.__test_reader__(audiotools.PCMReaderWindow(
                MiniFrameReader(self.channels, 44100, 3, 16), -5, 30),
                             [[0] * 5 + range(0, 20) + [0] * 5,
                              [0] * 5 + range(20, 0, -1) + [0] * 5])


class Test_open(unittest.TestCase):
    @LIB_CORE
    def setUp(self):
        self.dummy1 = tempfile.NamedTemporaryFile()
        self.dummy2 = tempfile.NamedTemporaryFile()
        self.dummy3 = tempfile.NamedTemporaryFile()
        self.dummy1.write("12345" * 1000)
        self.dummy1.flush()
        self.dummy2.write("54321" * 1000)
        self.dummy2.flush()

        data = open("flac-allframes.flac", "rb").read()
        self.dummy3.write(data[0:0x6 + 1] + chr(0x21) +
                          data[0x8:0x34 + 1] + data[0x36:])
        self.dummy3.flush()

    @LIB_CORE
    def tearDown(self):
        self.dummy1.close()
        self.dummy2.close()

    @LIB_CORE
    def test_open(self):
        #ensure open on dummy file raises UnsupportedFile
        self.assertRaises(audiotools.UnsupportedFile,
                          audiotools.open,
                          self.dummy1.name)

        #ensure open on nonexistent file raises IOError
        self.assertRaises(IOError,
                          audiotools.open,
                          "/dev/null/foo")

        #ensure open on directory raises IOError
        self.assertRaises(IOError,
                          audiotools.open,
                          "/")

        #ensure open on unreadable file raises IOError
        os.chmod(self.dummy1.name, 0)
        try:
            self.assertRaises(IOError,
                              audiotools.open,
                              self.dummy1.name)
        finally:
            os.chmod(self.dummy1.name, 0600)

        #ensure a file whose __init__ method triggers InvalidFile
        #raises UnsupportedFile
        self.assertRaises(audiotools.InvalidFile,
                          audiotools.open,
                          self.dummy3.name)

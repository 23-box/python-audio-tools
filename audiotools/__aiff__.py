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

from audiotools import AudioFile,InvalidFile,FrameReader,Con,transfer_data

#######################
#AIFF
#######################

class AiffAudio(AudioFile):
    SUFFIX = "aiff"

    def __init__(self, filename):
        import aifc

        AudioFile.__init__(self, filename)

        try:
            f = aifc.open(filename,"r")
            (self.__channels__,
             bytes_per_sample,
             self.__sample_rate__,
             self.__total_samples__,
             self.comptype,
             self.compname) = f.getparams()
            self.__bits_per_sample__ = bytes_per_sample * 8
            f.close()
        except aifc.Error,msg:
            raise InvalidFile(str(msg))

    @classmethod
    def is_type(cls, file):
        header = file.read(12)
        
        return ((header[0:4] == 'FORM') and
                (header[8:12] == 'AIFF'))

    def lossless(self):
        return True

    def bits_per_sample(self):
        return self.__bits_per_sample__

    def channels(self):
        return self.__channels__

    def sample_rate(self):
        return self.__sample_rate__

    def total_samples(self):
        return self.__total_samples__


    def to_pcm(self):
        import aifc

        return FrameReader(aifc.open(self.filename,"r"),
                           self.sample_rate(),
                           self.channels(),
                           self.bits_per_sample())

    @classmethod
    def from_pcm(cls, filename, pcmreader, compression=None):
        import aifc

        f = aifc.open(filename,"w")

        f.setparams((pcmreader.channels,
                     pcmreader.bits_per_sample / 8,
                     pcmreader.sample_rate,
                     0,
                     'NONE',
                     'not compressed'))

        transfer_data(pcmreader.read,f.writeframes)
        pcmreader.close()
        f.close()

        return AiffAudio(filename)

    @classmethod
    def track_name(cls, track_number, track_metadata):
        return "track%(track_number)2.2d.cdda.aiff" % \
            {"track_number":track_number}

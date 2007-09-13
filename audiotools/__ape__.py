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


from audiotools import AudioFile,InvalidFile,PCMReader,Con,transfer_data,subprocess,BIN,MetaData

#######################
#MONKEY'S AUDIO
#######################

class ApeTag(MetaData,dict):
    APEv2_FLAGS = Con.BitStruct("APEv2_FLAGS",
      Con.Bits("undefined1",5),
      Con.Flag("read_only"),
      Con.Bits("encoding",2),
      Con.Bits("undefined2",16),
      Con.Flag("contains_header"),
      Con.Flag("contains_no_footer"),
      Con.Flag("is_header"),
      Con.Bits("undefined3",5))

    APEv2_FOOTER = Con.Struct("APEv2",
      Con.String("preamble",8),
      Con.ULInt32("version_number"),
      Con.ULInt32("tag_size"),
      Con.ULInt32("item_count"),
      Con.Embed(APEv2_FLAGS),
      Con.ULInt64("reserved"))

    APEv2_TAG = Con.Struct("APEv2_TAG",
      Con.ULInt32("length"),
      Con.Embed(APEv2_FLAGS),
      Con.CString("key"),
      Con.MetaField("value",
        lambda ctx: ctx["length"]))

    ATTRIBUTE_MAP = {'track_name':'Title',
                     'track_number':'Track',
                     'album_name':'Album',
                     'artist_name':'Composer',
                     'performer_name':'Artist',
                     'copyright':'Copyright',
                     'year':'Year'}

    ITEM_MAP = dict(map(reversed,ATTRIBUTE_MAP.items()))

    def __init__(self, tag_dict, tag_length=None):
        MetaData.__init__(self,
                          track_name=tag_dict.get('Title',u''),
                          track_number=int(tag_dict.get('Track',u'0')),
                          album_name=tag_dict.get('Album',u''),
                          artist_name=tag_dict.get('Composer',u''),
                          performer_name=tag_dict.get('Artist',u''),
                          copyright=tag_dict.get('Copyright',u''),
                          year=tag_dict.get('Year',u'')
                          )
        dict.__init__(self, tag_dict)
        self.tag_length = tag_length

    #if an attribute is updated (e.g. self.track_name)
    #make sure to update the corresponding dict pair
    def __setattr__(self, key, value):
        self.__dict__[key] = value
        
        if (self.ATTRIBUTE_MAP.has_key(key)):
            if (key != 'track_number'):
                self[self.ATTRIBUTE_MAP[key]] = value
            else:
                self[self.ATTRIBUTE_MAP[key]] = unicode(value)

    #if a dict pair is updated (e.g. self['Title'])
    #make sure to update the corresponding attribute
    def __setitem__(self, key, value):
        dict.__setitem__(self, key, value)
        
        if (self.ITEM_MAP.has_key(key)):
            if (key != 'Track'):
                self.__dict__[self.ITEM_MAP[key]] = value
            else:
                self.__dict__[self.ITEM_MAP[key]] = int(value)

    @classmethod
    def converted(cls, metadata):
        if ((metadata is None) or (isinstance(metadata,ApeTag))):
            return metadata
        else:
            tags = {}
            for (key,field) in cls.ITEM_MAP.items():
                field = unicode(getattr(metadata,field))
                if (field != u''):
                    tags[key] = field
                
            return ApeTag(tags)

    def __comment_name__(self):
        return u'APEv2'

    #takes two (key,value) apetag pairs
    #returns cmp on the weighted set of them
    #(title first, then artist, album, tracknumber)
    @classmethod
    def __by_pair__(cls, pair1, pair2):
        KEY_MAP = {"Title":1,
                   "Album":2,
                   "Track":3,
                   "Composer":4,
                   "Artist":5,
                   "Copyright":7,
                   "Year":6}
        
        return cmp((KEY_MAP.get(pair1[0],8),pair1[0],pair1[1]),
                   (KEY_MAP.get(pair2[0],8),pair2[0],pair2[1]))

    def __comment_pairs__(self):
        items = []

        for (key,value) in self.items():
            if (isinstance(value,unicode)):
                items.append((key,value))
            else:
                if (len(value) <= 20):
                    items.append((key,value.encode('hex')))
                else:
                    items.append((key,value.encode('hex')[0:39].upper() + u"\u2026"))
        
        return sorted(items,ApeTag.__by_pair__)
        

    #Takes a file object of a Monkey's Audio file 
    #and returns a tuple.
    #That tuple contains the dict of its APE tag info
    #and the total tag size.
    @classmethod
    def read_ape_tag(cls, apefile):
        apefile.seek(-32,2)
        footer = cls.APEv2_FOOTER.parse(apefile.read(32))

        if (footer.preamble != 'APETAGEX'):
            return ({},0)

        apefile.seek(-(footer.tag_size),2)

        apev2tag = {}

        for tag in Con.StrictRepeater(footer.item_count, 
                                      cls.APEv2_TAG).parse(apefile.read()):
            if (tag.encoding == 0):
                apev2tag[tag.key] = tag.value.rstrip("\0").decode('utf-8',
                                                                  'replace')
            else:
                apev2tag[tag.key] = tag.value

        if (footer.contains_header):
            return (apev2tag,
                    footer.tag_size + ApeTag.APEv2_FOOTER.sizeof())
        else:
            return (apev2tag,
                    footer.tag_size)

    def ape_tag_data(self):
        header = Con.Container()
        header.preamble = 'APETAGEX'
        header.version_number = 0x07D0
        header.tag_size = 0
        header.item_count = len(self.keys())
        
        header.undefined1 = header.undefined2 = header.undefined3 = 0
        header.read_only = False
        header.encoding = 0
        header.contains_header = True
        header.contains_no_footer = False
        header.is_header = True

        header.reserved = 0l

        footer = Con.Container()
        footer.preamble = header.preamble
        footer.version_number = header.version_number
        footer.tag_size = 0
        footer.item_count = len(self.keys())

        footer.undefined1 = footer.undefined2 = footer.undefined3 = 0
        footer.read_only = False
        footer.encoding = 0
        footer.contains_header = True
        footer.contains_no_footer = False
        footer.is_header = False

        footer.reserved = 0l

        tags = []
        for (key,value) in self.items():
            tag = Con.Container()
            
            if (isinstance(value,unicode)):
                value = value.encode('utf-8')
                tag.encoding = 0
            else:
                tag.encoding = 1
                
            tag.length = len(value)
            tag.key = key
            tag.value = value

            tag.undefined1 = tag.undefined2 = tag.undefined3 = 0
            tag.read_only = False
            tag.contains_header = False
            tag.contains_no_footer = False
            tag.is_header = False

            tags.append(ApeTag.APEv2_TAG.build(tag))
        tags = "".join(tags)

        footer.tag_size = header.tag_size = \
          len(tags) + len(ApeTag.APEv2_FOOTER.build(footer))

        return ApeTag.APEv2_FOOTER.build(header) + \
               tags + \
               ApeTag.APEv2_FOOTER.build(footer)

#This is a split-off version of get_metadata() and set_metadata()
#for formats with an appended APEv2 tag.
#This class presumes there will be a filename attribute which
#can be opened and checked for tags, or written if necessary.
class ApeTaggedAudio:
    def get_metadata(self):
        f = file(self.filename,'rb')
        try:
            (info,tag_length) = ApeTag.read_ape_tag(f)
            if (len(info) > 0):
                return ApeTag(info,tag_length)
            else:
                return None
        finally:
            f.close()

    def set_metadata(self, metadata):
        apetag = ApeTag.converted(metadata)
        
        if (apetag is None): return
        
        current_metadata = self.get_metadata()
        if (current_metadata != None):  #there's existing tags to delete
            f = file(self.filename,"rb")
            untagged_data = f.read()[0:-current_metadata.tag_length]
            f.close()
            f = file(self.filename,"wb")
            f.write(untagged_data)
            f.write(apetag.ape_tag_data())
            f.close()
        else:                           #no existing tags
            f = file(self.filename,"ab")
            f.write(apetag.ape_tag_data())
            f.close()


class ApeAudio(ApeTaggedAudio,AudioFile):
    SUFFIX = "ape"
    DEFAULT_COMPRESSION = "5000"
    COMPRESSION_MODES = tuple([str(x * 1000) for x in range(1,6)]); del(x)
    BINARIES = ("mac",)

    FILE_HEAD = Con.Struct("ape_head",
                           Con.String('id',4),
                           Con.ULInt16('version'))

    #version >= 3.98
    APE_DESCRIPTOR = Con.Struct("ape_descriptor",
                                Con.ULInt16('padding'),
                                Con.ULInt32('descriptor_bytes'),
                                Con.ULInt32('header_bytes'),
                                Con.ULInt32('seektable_bytes'),
                                Con.ULInt32('header_data_bytes'),
                                Con.ULInt32('frame_data_bytes'),
                                Con.ULInt32('frame_data_bytes_high'),
                                Con.ULInt32('terminating_data_bytes'),
                                Con.String('md5',16))

    APE_HEADER = Con.Struct("ape_header",
                            Con.ULInt16('compression_level'),
                            Con.ULInt16('format_flags'),
                            Con.ULInt32('blocks_per_frame'),
                            Con.ULInt32('final_frame_blocks'),
                            Con.ULInt32('total_frames'),
                            Con.ULInt16('bits_per_sample'),
                            Con.ULInt16('number_of_channels'),
                            Con.ULInt32('sample_rate'))

    #version <= 3.97
    APE_HEADER_OLD = Con.Struct("ape_header_old",
                                Con.ULInt16('compression_level'),
                                Con.ULInt16('format_flags'),
                                Con.ULInt16('number_of_channels'),
                                Con.ULInt32('sample_rate'),
                                Con.ULInt32('header_bytes'),
                                Con.ULInt32('terminating_bytes'),
                                Con.ULInt32('total_frames'),
                                Con.ULInt32('final_frame_blocks'))

    def __init__(self, filename):
        AudioFile.__init__(self, filename)
        
        (self.__samplespersec__,
         self.__channels__,
         self.__bitspersample__,
         self.__totalsamples__) = ApeAudio.__ape_info__(filename)

    @classmethod
    def is_type(cls, file):
        return file.read(4) == "MAC "

    def lossless(self):
        return True

    def bits_per_sample(self):
        return self.__bitspersample__

    def channels(self):
        return self.__channels__

    def total_samples(self):
        return self.__totalsamples__

    def sample_rate(self):
        return self.__samplespersec__
    

    @classmethod
    def __ape_info__(cls, filename):
        f = file(filename,'rb')
        try:
            file_head = cls.FILE_HEAD.parse_stream(f)

            if (file_head.id != 'MAC '):
                raise InvalidFile("invalid Monkey's Audio header")

            if (file_head.version >= 3980): #the latest APE file type
                descriptor = cls.APE_DESCRIPTOR.parse_stream(f)
                header = cls.APE_HEADER.parse_stream(f)

                return (header.sample_rate,
                        header.number_of_channels,
                        header.bits_per_sample,
                        ((header.total_frames - 1) * \
                         header.blocks_per_frame) + \
                         header.final_frame_blocks)
            else:                           #old-style APE file (obsolete)
                header = cls.APE_HEADER_OLD.parse_stream(f)

                if (file_head.version >= 3950):
                    blocks_per_frame = 0x48000
                elif ((file_head.version >= 3900) or
                      ((file_head.version >= 3800) and
                       (header.compression_level == 4000))):
                    blocks_per_frame = 0x12000
                else:
                    blocks_per_frame = 0x2400

                if (header.format_flags & 0x01):
                    bits_per_sample = 8
                elif (header.format_flags & 0x08):
                    bits_per_sample = 24
                else:
                    bits_per_sample = 16

                return (header.sample_rate,
                        header.number_of_channels,
                        bits_per_sample,
                        ((header.total_frames - 1) * \
                         blocks_per_frame) + \
                         header.final_frame_blocks)
                
        finally:
            f.close()

    def to_pcm(self):
        import tempfile

        f = tempfile.NamedTemporaryFile(suffix=".wav")
        devnull = file(os.devnull,"wb")
        sub = subprocess.Popen([BIN['mac'],
                                self.filename,
                                f.name,
                                '-d'],
                               stdout=devnull,
                               stderr=devnull)
        sub.wait()
        devnull.close()
        f.seek(0,0)
        return TempWaveReader(f)

    @classmethod
    def from_pcm(cls, filename, pcmreader, compression=None):
        import tempfile

        if (str(compression) not in cls.COMPRESSION_MODES):
            compression = cls.DEFAULT_COMPRESSION

        f = tempfile.NamedTemporaryFile(suffix=".wav")
        w = WaveAudio.from_pcm(f.name, pcmreader)
        devnull = file(os.devnull,"wb")
        sub = subprocess.Popen([BIN['mac'],
                                w.filename,
                                filename,
                                "-c%s" % (compression)],
                               stdout=devnull,
                               stderr=devnull)
        sub.wait()
        devnull.close()
        del(w)
        f.close()
        return ApeAudio(filename)

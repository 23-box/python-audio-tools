#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007-2008  Brian Langenberger

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

from audiotools import MetaData,Con,re,os,cStringIO,Image,InvalidImage

class Syncsafe32(Con.Adapter):
    def __init__(self, name):
        Con.Adapter.__init__(self,
                             Con.StrictRepeater(4,Con.UBInt8(name)))

    def _encode(self, value, context):
        data = []
        for i in xrange(4):
            data.append(value & 0x7F)
            value = value >> 7
        data.reverse()
        return data

    def _decode(self, obj, context):
        i = 0
        for x in obj:
            i = (i << 7) | (x & 0x7F)
        return i

class __BitstructToInt__(Con.Adapter):
    def _encode(self, value, context):
        return Con.Container(size=value)

    def _decode(self, obj, context):
        return obj.size

#UTF16CString and UTF16BECString implement a null-terminated string
#of UTF-16 characters by reading them as unsigned 16-bit integers,
#looking for the null terminator (0x0000) and then converting the integers
#back before decoding.  It's a little half-assed, but it seems to work.
#Even large UTF-16 characters with surrogate pairs (those above U+FFFF)
#shouldn't have embedded 0x0000 bytes in them,
#which ID3v2.2/2.3 aren't supposed to use anyway since they're limited
#to UCS-2 encoding.

class WidecharCStringAdapter(Con.Adapter):
    def __init__(self,obj,encoding):
        Con.Adapter.__init__(self,obj)
        self.encoding = encoding

    def _encode(self,obj,context):
        return Con.GreedyRepeater(Con.UBInt16("s")).parse(obj.encode(
                self.encoding)) + [0]

    def _decode(self,obj,context):
        c = Con.UBInt16("s")

        return "".join([c.build(s) for s in obj[0:-1]]).decode(self.encoding)

def UTF16CString(name):
    return WidecharCStringAdapter(Con.RepeatUntil(lambda obj, ctx: obj == 0x0,
                                                  Con.UBInt16(name)),
                                  encoding='utf-16')



def UTF16BECString(name):
    return WidecharCStringAdapter(Con.RepeatUntil(lambda obj, ctx: obj == 0x0,
                                                  Con.UBInt16(name)),
                                  encoding='utf-16be')

class ID3v22Frame:
    VALID_FRAME_ID = re.compile(r'[A-Z0-9]{4}')

    FRAME = Con.Struct("id3v22_frame",
                       Con.Bytes("frame_id",3),
                       Con.PascalString("data",
                                        length_field=__BitstructToInt__(
                Con.BitStruct("size",Con.Bits("size",24)))))

    def __init__(self,frame_id,data):
        self.id = frame_id
        self.data = data

    def build(self):
        return self.FRAME.build(Con.Container(frame_id=self.id,
                                              data=self.data))

    @classmethod
    def parse(cls,container):
        if (container.frame_id.startswith('T')):
            encoding_byte = ord(container.data[0])
            return ID3v22TextFrame(container.frame_id,
                                   encoding_byte,
                                   container.data[1:].decode(
                    ID3v22TextFrame.ENCODING[encoding_byte]))
        elif (container.frame_id == 'PIC'):
            pic = ID3v22PicFrame.FRAME.parse(container.data)
            return ID3v22PicFrame(
                pic.data,
                pic.format.decode('ascii','replace'),
                pic.description,
                pic.picture_type)
        else:
            return cls(frame_id=container.frame_id,
                       data=container.data)

class ID3v22TextFrame(ID3v22Frame):
    ENCODING = {0x00:"latin-1",
                0x01:"utf-16"}

    #encoding is an encoding byte
    #s is a unicode string
    def __init__(self,frame_id,encoding,s):
        self.id = frame_id
        self.encoding = encoding
        self.string = s

    def __unicode__(self):
        return self.string

    def __int__(self):
        try:
            return int(re.findall(r'\d+',self.string)[0])
        except IndexError:
            return 0

    @classmethod
    def from_unicode(cls,frame_id,s):
        for encoding in 0x00,0x01:
            try:
                s.encode(cls.ENCODING[encoding])
                return cls(frame_id,encoding,s)
            except UnicodeEncodeError:
                continue

    def build(self):
        return self.FRAME.build(Con.Container(
                frame_id=self.id,
                data=chr(self.encoding) + \
                    self.string.encode(self.ENCODING[self.encoding],
                                       'replace')))

class ID3v22PicFrame(ID3v22Frame,Image):
    FRAME = Con.Struct('pic_frame',
                       Con.Byte('text_encoding'),
                       Con.String('format',3),
                       Con.Byte('picture_type'),
                       Con.Switch("description",
                                  lambda ctx: ctx.text_encoding,
                                  {0x00: Con.CString("s",encoding='latin-1'),
                                   0x01: UTF16CString("s")}),
                       Con.StringAdapter(
            Con.GreedyRepeater(Con.Field('data',1))))

    #format and description are unicode strings
    #pic_type is an int
    #data is a string
    def __init__(self, data, format, description, pic_type):
        ID3v22Frame.__init__(self,'PIC',None)

        img = Image.new(data,u'',0)

        self.pic_type = pic_type
        self.format = format
        Image.__init__(self,
                       data=data,
                       mime_type=Image.new(data,u'',0).mime_type,
                       width=img.width,
                       height=img.height,
                       color_depth=img.color_depth,
                       color_count=img.color_count,
                       description=description,
                       type={3:0,4:1,5:2,6:3}.get(pic_type,4))

    def build(self):
        try:
            self.description.encode('latin-1')
            text_encoding = 0
        except UnicodeEncodeError:
            text_encoding = 1

        return ID3v22Frame.FRAME.build(
            Con.Container(frame_id='PIC',
                          data=self.FRAME.build(
                    Con.Container(text_encoding=text_encoding,
                                  format=self.format.encode('ascii'),
                                  picture_type=self.pic_type,
                                  description=self.description,
                                  data=self.data))))

    @classmethod
    def converted(cls, image):
        return cls(data=image.data,
                   format={u"image/png":u"PNG",
                           u"image/jpeg":u"JPG",
                           u"image/jpg":u"JPG",
                           u"image/x-ms-bmp":u"BMP",
                           u"image/gif":u"GIF",
                           u"image/tiff":u"TIF"}.get(image.mime_type,
                                                     u"JPG"),
                   description=image.description,
                   pic_type={0:3,1:4,2:5,3:6}.get(image.type,0))

class ID3v22Comment(MetaData):
    Frame = ID3v22Frame
    TextFrame = ID3v22TextFrame
    PictureFrame = ID3v22PicFrame

    TAG_HEADER = Con.Struct("id3v22_header",
                            Con.Const(Con.Bytes("file_id",3),'ID3'),
                            Con.Const(Con.Byte("version_major"),0x02),
                            Con.Const(Con.Byte("version_minor"),0x00),
                            Con.Embed(Con.BitStruct("flags",
                                                    Con.Flag("unsync"),
                                                    Con.Flag("compression"),
                                                    Con.Padding(6))),
                            Syncsafe32("length"))

    TAG_FRAMES = Con.GreedyRepeater(Frame.FRAME)

    ATTRIBUTE_MAP = {'track_name':'TT2',
                     'track_number':'TRK',
                     'album_name':'TAL',
                     'artist_name':'TP1',
                     'performer_name':'TP2',
                     'conductor_name':'TP3',
                     'composer_name':'TCM',
                     'media':'TMT',
                     'ISRC':'TRC',
                     'copyright':'TCR',
                     'publisher':'TPB',
                     'year':'TYE',
                     'date':'TRD',
                     'album_number':'TPA'}
                     #'comment':'COM'}

    ITEM_MAP = dict(map(reversed,ATTRIBUTE_MAP.items()))

    INTEGER_ITEMS = ('TRK','TPA')

    #frames should be a list of ID3v22Frame-compatible objects
    def __init__(self,frames):
        self.frames = {}  #a frame_id->[frame list] mapping

        for frame in frames:
            self.frames.setdefault(frame.id,[]).append(frame)

        attribs = {}
        for key in self.frames.keys():
            if (key in self.ITEM_MAP.keys()):
                if (key not in self.INTEGER_ITEMS):
                    attribs[self.ITEM_MAP[key]] = unicode(self.frames[key][0])
                else:
                    attribs[self.ITEM_MAP[key]] = int(self.frames[key][0])

        MetaData.__init__(self,**attribs)

    def __comment_name__(self):
        return u'ID3v2.2'

    def __comment_pairs__(self):
        pairs = []

        for (key,values) in self.frames.items():
            for value in values:
                pairs.append(('    ' + key,unicode(value)))

        return pairs

    #if an attribute is updated (e.g. self.track_name)
    #make sure to update the corresponding dict pair
    def __setattr__(self, key, value):
        self.__dict__[key] = value

        if (key in self.ATTRIBUTE_MAP):
            self.frames[self.ATTRIBUTE_MAP[key]] = [
                self.TextFrame.from_unicode(self.ATTRIBUTE_MAP[key],value)]

    def add_image(self, image):
        image = self.picture_frame.converted(image)
        self.frames.setdefault('PIC',[]).append(image)

    def delete_image(self, image):
        del(self.frames['PIC'][self['PIC'].index(image)])

    def images(self):
        if ('PIC' in self.frames.keys()):
            return self.frames['PIC']
        else:
            return []

    #FIXME - lots of stuff expects ID3v2 comments to act as dicts
    #implement keys(),values(),items(),__getitem__(),__setitem__(),len()
    #such that the assumption still holds

    @classmethod
    def parse(cls, stream):
        header = cls.TAG_HEADER.parse_stream(stream)

        #read in the whole tag, and strip any padding
        stream = cStringIO.StringIO(stream.read(header.length).rstrip(chr(0)))

        #read in a collection of parsed Frame containers
        return cls([cls.Frame.parse(container) for container in
                    cls.TAG_FRAMES.parse_stream(stream)])

    @classmethod
    def converted(cls, metadata):
        if ((metadata is None) or (isinstance(metadata,cls))):
            return metadata

        frames = []

        for (key,field) in cls.ITEM_MAP.items():
            value = getattr(metadata,field)
            if (key not in cls.INTEGER_ITEMS):
                if (len(value.strip()) > 0):
                    frames.append(cls.TextFrame.from_unicode(key,value))
            else:
                if (value != 0):
                    frames.append(cls.TextFrame.from_unicode(key,unicode(value)))

        for image in metadata.images():
            frames.append(cls.PictureFrame.converted(image))

        return cls(frames)

    def build(self):
        subframes = "".join(["".join([value.build() for value in values])
                             for values in self.frames.values()])

        return self.TAG_HEADER.build(
            Con.Container(file_id='ID3',
                          version_major=0x02,
                          version_minor=0x00,
                          unsync=False,
                          compression=False,
                          length=len(subframes))) + subframes

    #takes a file stream
    #checks that stream for an ID3v2 comment
    #if found, repositions the stream past it
    #if not, leaves the stream in the current location
    @classmethod
    def skip(cls, file):
        if (file.read(3) == 'ID3'):
            file.seek(0,0)
            #parse the header
            h = cls.TAG_HEADER.parse_stream(file)
            #seek to the end of its length
            file.seek(h.length,1)
            #skip any null bytes after the ID3v2 tag
            c = file.read(1)
            while (c == '\x00'):
                c = file.read(1)
            file.seek(-1,1)
        else:
            try:
                file.seek(-3,1)
            except IOError:
                pass

    #takes a filename
    #returns an ID3v2Comment-based object
    @classmethod
    def read_id3v2_comment(cls, filename):
        import cStringIO

        f = file(filename,"rb")

        try:
             f.seek(0,0)
             try:
                 header = ID3v2Comment.TAG_HEADER.parse_stream(f)
             except Con.ConstError:
                 raise UnsupportedID3v2Version()
             #if (header.version_major == 0x04):
             #    comment_class = ID3v2Comment
             #elif (header.version_major == 0x03):
             #    comment_class = ID3v2_3Comment
             if (header.version_major == 0x02):
                 comment_class = ID3v22Comment
             else:
                 raise UnsupportedID3v2Version()

             f.seek(0,0)
             return comment_class.parse(f)
        finally:
            f.close()


ID3v2Comment = ID3v22Comment

from __id3v1__ import *


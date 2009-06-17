#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007-2009  Brian Langenberger

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


from audiotools import MetaData,Con,VERSION,re

class VorbisComment(MetaData,dict):
    VORBIS_COMMENT = Con.Struct("vorbis_comment",
                                Con.PascalString("vendor_string",
                                                 length_field=Con.ULInt32("length")),
                                Con.PrefixedArray(
                                       length_field=Con.ULInt32("length"),
                                       subcon=Con.PascalString("value",
                                                             length_field=Con.ULInt32("length"))),
                                Con.Const(Con.Byte("framing"),1))

    ATTRIBUTE_MAP = {'track_name':'TITLE',
                     'track_number':'TRACKNUMBER',
                     'track_total':'TRACKTOTAL',
                     'album_name':'ALBUM',
                     'artist_name':'ARTIST',
                     'performer_name':'PERFORMER',
                     'composer_name':'COMPOSER',
                     'conductor_name':'CONDUCTOR',
                     'media':'SOURCE MEDIUM',
                     'ISRC':'ISRC',
                     'catalog':'CATALOG',
                     'copyright':'COPYRIGHT',
                     'publisher':'PUBLISHER',
                     'year':'DATE',
                     'album_number':'DISCNUMBER',
                     'album_total':'DISCTOTAL',
                     'comment':'COMMENT'}

    ITEM_MAP = dict(map(reversed,ATTRIBUTE_MAP.items()))

    #vorbis_data is a key->[value1,value2,...] dict of the original
    #Vorbis comment data.  keys should be upper case
    def __init__(self, vorbis_data, vendor_string=u""):
        try:
            track_number = int(vorbis_data.get('TRACKNUMBER',['0'])[0])
        except ValueError:
            track_number = 0

        try:
            track_total = int(vorbis_data.get('TRACKTOTAL',['0'])[0])
        except ValueError:
            track_total = 0

        try:
            album_number = int(vorbis_data.get('DISCNUMBER',['0'])[0])
        except ValueError:
            album_number = 0

        try:
            album_total = int(vorbis_data.get('DISCTOTAL',['0'])[0])
        except ValueError:
            album_total = 0

        MetaData.__init__(
            self,
            track_name = vorbis_data.get('TITLE',[u''])[0],
            track_number = track_number,
            track_total = track_total,
            album_name = vorbis_data.get('ALBUM',[u''])[0],
            artist_name = vorbis_data.get('ARTIST',[u''])[0],
            performer_name = vorbis_data.get('PERFORMER',[u''])[0],
            composer_name = vorbis_data.get('COMPOSER',[u''])[0],
            conductor_name = vorbis_data.get('CONDUCTOR',[u''])[0],
            media = vorbis_data.get('SOURCE MEDIUM',[u''])[0],
            ISRC = vorbis_data.get('ISRC',[u''])[0],
            catalog = vorbis_data.get('CATALOG',[u''])[0],
            copyright = vorbis_data.get('COPYRIGHT',[u''])[0],
            publisher = vorbis_data.get('PUBLISHER',[u''])[0],
            year = vorbis_data.get('DATE',[u''])[0],
            date = u"",
            album_number = album_number,
            album_total = album_total,
            comment = vorbis_data.get('COMMENT',[u''])[0])

        dict.__init__(self,vorbis_data)
        self.vendor_string = vendor_string

    @classmethod
    def supports_images(cls):
        return False

    #if an attribute is updated (e.g. self.track_name)
    #make sure to update the corresponding dict pair
    def __setattr__(self, key, value):
        self.__dict__[key] = value

        if (key in self.ATTRIBUTE_MAP):
            if (key not in MetaData.__INTEGER_FIELDS__):
                self[self.ATTRIBUTE_MAP[key]] = [value]
            else:
                self[self.ATTRIBUTE_MAP[key]] = [unicode(value)]

    #if a dict pair is updated (e.g. self['TITLE'])
    #make sure to update the corresponding attribute
    def __setitem__(self, key, value):
        if (self.ITEM_MAP.has_key(key)):
            if (key in ('TRACKNUMBER','TRACKTOTAL')):
                match = re.match(r'^\d+$',value[0])
                if (match):
                    dict.__setitem__(self, key, value)
                    self.__dict__[self.ITEM_MAP[key]] = int(match.group(0))
                else:
                    match = re.match(r'^(\d+)/(\d+)$',value[0])
                    if (match):
                        self.__dict__["track_number"] = int(match.group(1))
                        self.__dict__["track_total"] = int(match.group(2))
                        dict.__setitem__(self,"TRACKNUMBER",
                                         [unicode(match.group(1))])
                        dict.__setitem__(self,"TRACKTOTAL",
                                         [unicode(match.group(2))])
                    else:
                        dict.__setitem__(self, key, value)
            elif (key in ('DISCNUMBER','DISCTOTAL')):
                match = re.match(r'^\d+$',value[0])
                if (match):
                    dict.__setitem__(self, key, value)
                    self.__dict__[self.ITEM_MAP[key]] = int(match.group(0))
                else:
                    match = re.match(r'^(\d+)/(\d+)$',value[0])
                    if (match):
                        self.__dict__["album_number"] = int(match.group(1))
                        self.__dict__["album_total"] = int(match.group(2))
                        dict.__setitem__(self,"DISCNUMBER",
                                         [unicode(match.group(1))])
                        dict.__setitem__(self,"DISCTOTAL",
                                         [unicode(match.group(2))])
                    else:
                        dict.__setitem__(self, key, value)
            else:
                dict.__setitem__(self, key, value)
                self.__dict__[self.ITEM_MAP[key]] = value[0]

    @classmethod
    def converted(cls, metadata):
        if ((metadata is None) or (isinstance(metadata,VorbisComment))):
            return metadata
        elif (hasattr(metadata,'vorbis_comment')):
            #This is a hack to support FlacMetaData.
            #We can't use isinstance() because FlacMetaData contains
            #FlacVorbisComment, and both are defined in __flac__
            #which must be defined *after* __vorbiscomment__ since
            #FlacVorbisComment is a subclass of VorbisComment.
            return metadata.vorbis_comment
        else:
            values = {}
            for key in cls.ATTRIBUTE_MAP.keys():
                if (key in cls.__INTEGER_FIELDS__):
                    if (getattr(metadata,key) != 0):
                        values[cls.ATTRIBUTE_MAP[key]] = \
                            [unicode(getattr(metadata,key))]
                elif (getattr(metadata,key) != u""):
                    values[cls.ATTRIBUTE_MAP[key]] = \
                        [unicode(getattr(metadata,key))]

            return VorbisComment(values)

    def merge(self, metadata):
        metadata = self.__class__.converted(metadata)
        if (metadata is None):
            return

        for (key,values) in metadata.items():
            if ((len(values) > 0) and
                (len(self.get(key,[])) == 0)):
                self[key] = values

    def __comment_name__(self):
        return u'Vorbis'

    #takes two (key,value) vorbiscomment pairs
    #returns cmp on the weighted set of them
    #(title first, then artist, album, tracknumber, ... , replaygain)
    @classmethod
    def __by_pair__(cls, pair1, pair2):
        KEY_MAP = {"TITLE":1,
                   "ALBUM":2,
                   "TRACKNUMBER":3,
                   "TRACKTOTAL":4,
                   "DISCNUMBER":5,
                   "DISCTOTAL":6,
                   "ARTIST":7,
                   "PERFORMER":8,
                   "COMPOSER":9,
                   "CONDUCTOR":10,
                   "CATALOG":11,
                   "PUBLISHER":12,
                   "ISRC":13,
                   "SOURCE MEDIUM":14,
                   #"YEAR":15,
                   "DATE":16,
                   "COPYRIGHT":17,
                   "REPLAYGAIN_ALBUM_GAIN":19,
                   "REPLAYGAIN_ALBUM_PEAK":19,
                   "REPLAYGAIN_TRACK_GAIN":19,
                   "REPLAYGAIN_TRACK_PEAK":19,
                   "REPLAYGAIN_REFERENCE_LOUDNESS":20}
        return cmp((KEY_MAP.get(pair1[0].upper(),18),pair1[0].upper(),pair1[1]),
                   (KEY_MAP.get(pair2[0].upper(),18),pair2[0].upper(),pair2[1]))

    def __comment_pairs__(self):
        pairs = []
        for (key,values) in self.items():
            for value in values:
                pairs.append((key,value))

        pairs.sort(VorbisComment.__by_pair__)
        return pairs

    #returns this VorbisComment as a binary string
    def build(self):
        comment = Con.Container(vendor_string = self.vendor_string,
                                framing = 1,
                                value = [])

        for (key,values) in self.items():
            for value in values:
                if (value != u""):
                    comment.value.append("%s=%s" % (key,
                                                    value.encode('utf-8')))
        return self.VORBIS_COMMENT.build(comment)

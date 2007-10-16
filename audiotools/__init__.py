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

import sys

if (sys.version_info < (2,4,4,'final',0)):
    print >>sys.stderr,"*** Python 2.4.4 or better required"
    sys.exit(1)

import subprocess
import re
import cStringIO
import os
import os.path
import ConfigParser
import struct
from itertools import izip

try:
    import construct as Con
except ImportError:
    try:
        import Construct as Con
    except ImportError:
        print >>sys.stderr,"*** construct module not found"
        print >>sys.stderr,"""To remedy this: \"make construct_install\"
from the audiotools source directory to install the Construct module."""
        sys.exit(1)
    

class RawConfigParser(ConfigParser.RawConfigParser):
    def get_default(self, section, option, default):
        try:
            return self.get(section, option)
        except ConfigParser.NoSectionError:
            return default
        except ConfigParser.NoOptionError:
            return default

    def getint_default(self, section, option, default):
        try:
            return self.getint(section, option)
        except ConfigParser.NoSectionError:
            return default
        except ConfigParser.NoOptionError:
            return default

config = RawConfigParser()
config.read([os.path.join(sys.prefix,"etc","audiotools.cfg"),
             os.path.expanduser('~/.audiotools.cfg')])

BUFFER_SIZE = 0x100000

class __system_binaries__:
    def __init__(self, config):
        self.config = config

    def __getitem__(self, command):
        try:
            return self.config.get("Binaries",command)
        except ConfigParser.NoSectionError:
            return command
        except ConfigParser.NoOptionError:
            return command

    def can_execute(self, command):
        if (os.sep in command):
            return os.access(command,os.X_OK)
        else:
            for path in os.environ.get('PATH',os.defpath).split(os.pathsep):
                if (os.access(os.path.join(path,command),os.X_OK)):
                    return True
            return False

BIN = __system_binaries__(config)

DEFAULT_CDROM = config.get_default("System","cdrom","/dev/cdrom")

FREEDB_SERVER = config.get_default("FreeDB","server","us.freedb.org")
FREEDB_PORT = config.getint_default("FreeDB","port",80)

VERSION = "2.4"

FILENAME_FORMAT = config.get_default(
    "Filenames","format",
    '%(track_number)2.2d - %(track_name)s.%(suffix)s')

FS_ENCODING = sys.getfilesystemencoding()
if (FS_ENCODING == None):
    FS_ENCODING = 'UTF-8'
IO_ENCODING = "UTF-8"

try:
    import cpucount
    MAX_CPUS = cpucount.cpucount()
except ImportError:
    MAX_CPUS = 1

if (config.has_option("System","maximum_jobs")):
    MAX_JOBS = config.getint_default("System","maximum_jobs",1)
else:
    MAX_JOBS = MAX_CPUS

BIG_ENDIAN = (struct.pack("=I",0x100) == struct.pack(">I",0x100))

#raised by open() if the file cannot be identified or opened correctly
class UnsupportedFile(Exception): pass

#raised if an audio file cannot be initialized correctly
class InvalidFile(Exception): pass

#raised if an audio file cannot be created correctly from from_pcm() 
class InvalidFormat(Exception): pass

#takes a filename string
#returns a valid AudioFile object based on the file data or extension
#or raises UnsupportedFile if it's not a file we support
def open(filename):
    available_types = frozenset(TYPE_MAP.values())

    try:
        f = file(filename,"rb")
        try:
            for audioclass in TYPE_MAP.values():
                f.seek(0,0)
                if (audioclass.is_type(f)):
                    return audioclass(filename)
            else:
                raise UnsupportedFile(filename)
        
        finally:
            f.close()
    except IOError:
        raise UnsupportedFile(filename)

#takes a list of filenames
#returns a list of AudioFile objects, sorted by track_number()
#any unsupported files are filtered out
def open_files(filename_list, sorted=True):
    toreturn = []
    for filename in filename_list:
        try:
            toreturn.append(open(filename))
        except UnsupportedFile:
            pass
        except InvalidFile,msg:
            print >>sys.stderr,"*** %s: %s" % (filename,msg)

    if (sorted):
        toreturn.sort(lambda x,y: cmp(x.track_number(),
                                      y.track_number()))
    return toreturn

#a class that generates PCM audio data
#sample rate, channels and bits per sample are integers
#the data is assumed to be signed, little-endian strings
#as generated by WAV files
class PCMReader:
    def __init__(self, file,
                 sample_rate, channels, bits_per_sample,
                 process=None):
        self.file = file
        self.sample_rate = sample_rate
        self.channels = channels
        self.bits_per_sample = bits_per_sample
        self.process = process

    #Try to read a string of size "bytes".
    #This is *not* guaranteed to read exactly that number of bytes.
    #It may return less (at the end of the stream, especially).
    #It may return more.
    #However, it must always return a non-empty string until the
    #end of the PCM stream is reached.
    def read(self, bytes):
        return self.file.read(bytes)

    def close(self):
        self.file.close()
        
        if (self.process != None):
            self.process.wait()
        


#sends BUFFER_SIZE strings from from_function to to_function
#until the string is empty
def transfer_data(from_function, to_function):
    s = from_function(BUFFER_SIZE)
    while (len(s) > 0):
        to_function(s)
        s = from_function(BUFFER_SIZE)


def threaded_transfer_data(from_function, to_function):
    import threading,Queue

    def send_data(from_function, queue):
        s = from_function(BUFFER_SIZE)
        while (len(s) > 0):
            queue.put(s)
            s = from_function(BUFFER_SIZE)
        queue.put(None)

    data_queue = Queue.Queue(10)
    #thread.start_new_thread(send_data,(from_function,data_queue))
    thread = threading.Thread(target=send_data,
                              args=(from_function,data_queue))
    thread.setDaemon(True)
    thread.start()
    s = data_queue.get()
    while (s is not None):
        to_function(s)
        s = data_queue.get()

#takes a wave-compatible object with a readframes() method
#maps it to something PCMReader compatible
class FrameReader(PCMReader):
    def __init__(self, framefile, 
                 sample_rate, channels, bits_per_sample,
                 process=None):
        PCMReader.__init__(self,
                           file=framefile,
                           sample_rate=sample_rate, 
                           channels=channels, 
                           bits_per_sample=bits_per_sample,
                           process=process)
        self.framefile = framefile
        self.bytes_per_sample = framefile.getnchannels() * \
                                framefile.getsampwidth()

    def read(self, bytes):
        return self.framefile.readframes(bytes / self.bytes_per_sample)

    def close(self):
        self.framefile.close()
        

#returns True if the PCM data in pcmreader1 equals pcmreader2
#False if there is any data mismatch
#the readers must be closed independently of this checker
def pcm_cmp(pcmreader1, pcmreader2):
    if ((pcmreader1.sample_rate != pcmreader2.sample_rate) or
        (pcmreader1.channels != pcmreader2.channels) or
        (pcmreader1.bits_per_sample != pcmreader2.bits_per_sample)):
        return False

    #rather than do byte-per-byte comparisons,
    #we'll do a length/shasum comparison
    #since pcmreader.read() doesn't necessarily
    #return BUFFER_SIZE number of bytes per read
    import sha

    data1 = sha.new()
    data2 = sha.new()
    len1 = 0
    len2 = 0

    d = pcmreader1.read(BUFFER_SIZE)
    while (len(d) > 0):
        data1.update(d)
        len1 += len(d)
        d = pcmreader1.read(BUFFER_SIZE)

    d = pcmreader2.read(BUFFER_SIZE)
    while (len(d) > 0):
        data2.update(d)
        len2 += len(d)
        d = pcmreader2.read(BUFFER_SIZE)

    return ((len1 == len2) and (data1.digest() == data2.digest()))

#returns True if the PCM data in pcmreader1 equals pcmreader2
#not counting any 0x00 bytes at the beginning and end
#of each reader
def stripped_pcm_cmp(pcmreader1, pcmreader2):
    if ((pcmreader1.sample_rate != pcmreader2.sample_rate) or
        (pcmreader1.channels != pcmreader2.channels) or
        (pcmreader1.bits_per_sample != pcmreader2.bits_per_sample)):
        return False

    import sha

    data = cStringIO.StringIO()
    
    d = pcmreader1.read(BUFFER_SIZE)
    while (len(d) > 0):
        data.write(d)
        d = pcmreader1.read(BUFFER_SIZE)

    sum1 = sha.new(data.getvalue().strip(chr(0x00)))

    data = cStringIO.StringIO()
    
    d = pcmreader2.read(BUFFER_SIZE)
    while (len(d) > 0):
        data.write(d)
        d = pcmreader2.read(BUFFER_SIZE)
    
    sum2 = sha.new(data.getvalue().strip(chr(0x00)))

    del(data)

    return sum1.digest() == sum2.digest()


class PCMCat(PCMReader):
    #takes an iterator of PCMReader objects
    #returns their data concatted together
    def __init__(self, pcmreaders):
        self.reader_queue = pcmreaders
        
        try:
            self.first = self.reader_queue.next()
        except StopIteration:
            raise ValueError("You must have at least one PCMReader")

        self.sample_rate = self.first.sample_rate
        self.channels = self.first.channels
        self.bits_per_sample = self.first.bits_per_sample
    
    def read(self, bytes):
        try:
            s = self.first.read(bytes)
            if (len(s) > 0):
                return s
            else:
                self.first.close()
                self.first = self.reader_queue.next()
                return self.read(bytes)
        except StopIteration:
            return ""

    def close(self):
        pass


#takes a PCMReader and a list of reader lengths (in PCM samples)
#returns an iterator of PCMReader-compatible objects, each limited
#to the given lengths.
#The reader is closed upon completion
def pcm_split(reader, pcm_lengths):
    import tempfile

    def chunk_sizes(total_size,chunk_size):
        while (total_size > chunk_size):
            total_size -= chunk_size
            yield chunk_size
        yield total_size

    full_data = tempfile.TemporaryFile()
    try:
        #This dumps the whole contents of reader into a temp PCM file
        #which we pull apart in the appropriate sizes to make PCMReaders.
        #It is a very stupid approach, but it does work.
        #Pulling only the data we need from reader into each PCMReader
        #would be better, but the variable size of PCMReader.read()
        #makes it difficult to get the exact amount of data into each sub-reader
        transfer_data(reader.read,full_data.write)
        reader.close()
        full_data.seek(0,0)

        for byte_length in [i * reader.channels * reader.bits_per_sample / 8
                            for i in pcm_lengths]:
            if (byte_length > (BUFFER_SIZE * 10)):
                #if the sub-file length is somewhat large, use a temporary file
                sub_file = tempfile.TemporaryFile()
                for size in chunk_sizes(byte_length,BUFFER_SIZE):
                    sub_file.write(full_data.read(size))
                sub_file.seek(0,0)
            else:
                #if the sub-file length is very small, use StringIO
                sub_file = cStringIO.StringIO(full_data.read(byte_length))
            yield PCMReader(sub_file,
                            reader.sample_rate,
                            reader.channels,
                            reader.bits_per_sample)
    finally:
        full_data.close()


class PCMConverter(PCMReader):
    def __init__(self, pcmreader,
                 sample_rate, channels, bits_per_sample):
        import pcmstream

        self.input = pcmreader
        self.reader = pcmstream.PCMStreamReader(pcmreader,
                                                pcmreader.bits_per_sample / 8)
        PCMReader.__init__(self,None, sample_rate, channels, bits_per_sample)

        self.bytes_per_sample = self.bits_per_sample / 8
        
        self.leftover_samples = []


        self.conversions = []
        if (self.input.channels != self.channels):
            self.conversions.append(self.convert_channels)
            
        if (self.input.sample_rate != self.sample_rate):
            self.resampler = pcmstream.Resampler(
                self.channels,
                float(self.sample_rate) / float(self.input.sample_rate),
                0)
            
            self.unresampled = []

            #if we're converting sample rate and bits-per-sample
            #at the same time, short-circuit the conversion to do both at once
            #which can be sped up somewhat
            if (self.input.bits_per_sample != self.bits_per_sample):
                self.conversions.append(self.convert_sample_rate_and_bits_per_sample)
            else:
                self.conversions.append(self.convert_sample_rate)
        
        else:
            if (self.input.bits_per_sample != self.bits_per_sample):
                self.conversions.append(self.convert_bits_per_sample)
        
        
    def read(self, bytes):
        (frame_list,self.leftover_samples) = FrameList.from_samples(
            self.leftover_samples + self.reader.read(bytes),
            self.input.channels)
        
        for converter in self.conversions:
            frame_list = converter(frame_list)
            
        return pcmstream.pcm_to_string(frame_list,self.bytes_per_sample)

    def close(self):
        self.reader.close()

    def convert_bits_per_sample(self, frame_list):
        #This modifies the bytes of "frame_list" in-place
        #rather than build a whole new array and return it.
        #Since our chained converters will overwrite the old frame_list
        #anyway, this should speed up the conversion without
        #damaging anything.
        #Just be careful when using this routine elsewhere.
        
        difference = self.bits_per_sample - self.input.bits_per_sample
        
        if (difference < 0):   #removing bits per sample
            bits_difference = -difference

            #add some white noise when dithering the signal
            #to make it sound better, assuming we have at least 16 bits
            if (self.bits_per_sample == 0):
                random_bytes = map(ord, os.urandom((len(frame_list) / 8) + 1))
                white_noise = [(random_bytes[i / 8] & (1 << (i % 8))) >> (i % 8)
                               for i in xrange(len(frame_list))]
            else:
                white_noise = [0] * len(frame_list)
                

            return [(s >> bits_difference) ^ w for (s,w) in izip(frame_list,
                                                                 white_noise)]

        elif (difference > 0): #adding bits per sample
            bits_difference = difference

            return [(s << bits_difference) for s in frame_list]

        return frame_list

    def convert_channels(self, frame_list):
        difference = self.channels - self.input.channels

        if (difference < 0): #removing channels
            channels = []
            for i in xrange(self.channels):
                channels.append(frame_list.channel(i))
            
            return FrameList.from_channels(channels)
        else:                #adding new channels
            channels = list(frame_list.channels())
            for i in xrange(difference):
                channels.append(channels[0])

            return FrameList.from_channels(channels)

    def convert_sample_rate(self, frame_list):
        divider = 1 << (self.input.bits_per_sample - 1)
        multiplier = 1 << (self.bits_per_sample - 1)
        
        (output,self.unresampled) = self.resampler.process(
            self.unresampled + [float(s) / divider for s in frame_list],
            (len(frame_list) == 0) and (len(self.unresampled) == 0))
        
        return [int(round(s * multiplier)) for s in output]


    #though this method name is huge, it is also unambiguous
    def convert_sample_rate_and_bits_per_sample(self, frame_list):
        divider = 1 << (self.input.bits_per_sample - 1)
        multiplier = 1 << (self.bits_per_sample - 1)

        #turn our PCM samples into floats and resample them,
        #which removes bits-per-sample
        (output,self.unresampled) = self.resampler.process(
            self.unresampled + [(float(s) / divider) for s in frame_list],
            (len(frame_list) == 0) and (len(self.unresampled) == 0))

        frame_list = FrameList(output,frame_list.total_channels)

        #turn our PCM samples back into ints, which re-adds bits-per-sample
        if (self.bits_per_sample - self.input.bits_per_sample < 0):
            #add some white noise when dithering the signal
            #to make it sound better
            if (self.bits_per_sample >= 16):
                random_bytes = map(ord, os.urandom((len(frame_list) / 8) + 1))
                white_noise = [(random_bytes[i / 8] & (1 << (i % 8))) >> (i % 8)
                               for i in xrange(len(frame_list))]
            else:
                white_noise = [0] * len(frame_list)
            
            return [int(round(s * multiplier)) ^ w
                    for (s,w) in izip(frame_list,white_noise)]

        else:
            return [int(round(s * multiplier)) for s in frame_list]


        return frame_list

class FrameList(list):
    #l should be a list-compatible collection of PCM integers
    #channels is how many channels there are per frame
    #A "Frame" is a collection of PCM samples which covers all channels
    #(2 channel audio has 2 PCM samples per frame, for example).
    #There should not be any partial frames in l.
    def __init__(self, l, total_channels):
        if ((len(l) % total_channels) != 0):
            raise ValueError("partial frames are invalid")
        list.__init__(self,l)
        self.total_channels = total_channels

    def __repr__(self):
        l_repr = list.__repr__(self)
        if (len(l_repr) > 20):
            l_repr = l_repr[0:17] + "..."
        
        return "FrameList(%s,%s)" % (l_repr,
                                     repr(self.total_channels))

    #returns a list of all samples in channel "i"
    def channel(self, i):
        if (i < self.total_channels):
            return self[i::self.total_channels]
        else:
            raise IndexError("invalid channel number")

    def channels(self):
        for i in xrange(self.total_channels):
            yield self.channel(i)

    #returns a list of all samples at frame "i"
    def frame(self, i):
        return self[self.total_channels * i:self.total_channels * (i + 1)]

    def total_frames(self):
        return len(self) / self.total_channels

    def frames(self):
        for i in xrange(self.total_frames()):
            yield self.frame(i)

    #takes a list of PCM sample integers and number of channels
    #returns a (FrameList,remaining_samples) tuple
    @classmethod
    def from_samples(cls, samples, channels):
        remainder_count = len(samples) % channels
        if (remainder_count != 0):
            return (FrameList(samples[0:-remainder_count],channels),
                    samples[-remainder_count:])
        else:
            return (FrameList(samples,channels),list())

    #takes a list of channel lists, each containing PCM sample integers
    #returns a FrameList with the appropriate number of channels
    #all channel lists must be the same length
    @classmethod
    def from_channels(cls, channels):
        import operator

        if ((len(channels) > 1) and not reduce(operator.eq,map(len,channels))):
            raise ValueError("all channels must be the same length")

        data = [None] * len(channels) * len(channels[0])

        for (i,c) in enumerate(channels):
            data[i::len(channels)] = c
            
        return FrameList(data,len(channels))
        

    #takes a list of frame lists,
    #each containing one PCM sample per channel
    #returns a FrameList with the appropriate number of channels
    @classmethod
    def from_frames(cls, frames):
        import operator

        return FrameList(reduce(operator.concat,frames),
                         len(frames[0]))

#ensures all the directories leading to "destination_path" are created
#if necessary
def make_dirs(destination_path):
    dirname = os.path.dirname(destination_path)
    if ((dirname != '') and (not os.path.isdir(dirname))):
        os.makedirs(dirname)

#######################
#Generic MetaData
#######################

class MetaData:
    #track_name, album_name, artist_name, performer_name, copyright and year
    #should be unicode strings
    #track_number should be an integer
    def __init__(self,
                 track_name=u"",    #the name of this individual track
                 track_number=0,    #the number of this track
                 album_name=u"",    #the name of the album this track belongs to
                 artist_name=u"",   #the song's original creator/composer
                 performer_name=u"",#the song's performing artist
                 copyright=u"",     #the song's copyright information
                 year=u""           #the album's release year
                 ):
        #we're avoiding self.foo = foo because
        #__setattr__ might need to be redefined
        #which could lead to unwelcome side-effects
        self.__dict__['track_name'] = track_name
        self.__dict__['track_number'] = track_number
        self.__dict__['album_name'] = album_name
        self.__dict__['artist_name'] = artist_name
        if (performer_name != u''):
            self.__dict__['performer_name'] = performer_name
        else:
            self.__dict__['performer_name'] = artist_name

        self.__dict__['copyright'] = copyright
        self.__dict__['year'] = year

    def __repr__(self):
        return "MetaData(%s,%s,%s,%s,%s,%s,%s)" % \
               (repr(self.track_name),
                repr(self.track_number),
                repr(self.album_name),
                repr(self.artist_name),
                repr(self.performer_name),
                repr(self.copyright),
                repr(self.year))

    #returns the type of comment this is, as a unicode string
    def __comment_name__(self):
        return u'MetaData'

    #returns a list of (key,value) tuples
    def __comment_pairs__(self):
        return zip(("Title","Artist","Performer","Album",
                    "Number","Year","Copyright"),
                   (self.track_name,
                    self.artist_name,
                    self.performer_name,
                    self.album_name,
                    str(self.track_number),
                    self.year,
                    self.copyright))

    def __unicode__(self):
        comment_pairs = self.__comment_pairs__()
        if (len(comment_pairs) > 0):
            max_key_length = max([len(pair[0]) for pair in comment_pairs])
            line_template = u"%%(key)%(length)d.%(length)ds : %%(value)s" % \
                            {"length":max_key_length}

            base_comment = unicode(os.linesep.join(
                [u"%s Comment:" % (self.__comment_name__())] + \
                [line_template % {"key":key,"value":value} for
                 (key,value) in comment_pairs]))
        else:
            base_comment = u""

        if (isinstance(self,ImageMetaData) and (len(self.images()) > 0)):
            return u"%s\n\n%s" % \
                   (base_comment,
                    "\n".join([unicode(p) for p in self.images()]))
        else:
            return base_comment

    def __eq__(self, metadata):
        import operator
        
        if (metadata is not None):
            return reduce(operator.and_,
                          [(getattr(self,attr) == getattr(metadata,attr))
                           for attr in
                           ("track_name","artist_name","performer_name",
                            "album_name","track_number","year",
                            "copyright")],True)
        else:
            return False

    def __ne__(self, metadata):
        return not self.__eq__(metadata)

    #takes a MetaData-compatible object (or None)
    #returns a new MetaData subclass with the data fields converted
    #or None if metadata is None or conversion isn't possible
    #For instance, VorbisComment.converted() returns a VorbisComment
    #class.  This way, AudioFiles can offload metadata conversions.
    @classmethod
    def converted(cls, metadata):
        return metadata

class AlbumMetaData(dict):
    def __init__(self, metadata_iter):
        dict.__init__(self,
                      dict([(m.track_number,m) for m in
                            metadata_iter]))


#######################
#Image MetaData
#######################

#A simple image data container
class Image:
    #data is a string of the actual image data file
    #mime_type is a unicode string of the image's MIME type
    #width and height are integers of the images' dimensions
    #color_depth is the full depth of the image in bits
    #(24 for JPEG, 8 for GIF, etc.)
    #color_count is the number of colors used for palette images, or 0
    #description is a unicode string
    #type is an int
    #0 = front cover
    #1 = back cover
    #2 = leaflet page
    #3 = media
    #4 = other
    def __init__(self, data, mime_type, width, height,
                 color_depth, color_count, description, type):
        self.data = data
        self.mime_type = mime_type
        self.width = width
        self.height = height
        self.color_depth = color_depth
        self.color_count = color_count
        self.description = description
        self.type = type

    def suffix(self):
        return {"image/jpeg":"jpg",
                "image/jpg":"jpg",
                "image/gif":"gif",
                "image/png":"png",
                "image/x-ms-bmp":"bmp",
                "image/tiff":"tiff"}.get(self.mime_type,"bin")

    def type_string(self):
        return {0:"Front Cover",
                1:"Back Cover",
                2:"Leaflet Page",
                3:"Media",
                4:"Other"}.get(self.type,"Other")

    def __repr__(self):
        return "Image(mime_type=%s,width=%s,height=%s,color_depth=%s,color_count=%s,description=%s,type=%s,...)" % \
               (repr(self.mime_type),repr(self.width),repr(self.height),
                repr(self.color_depth),repr(self.color_count),
                repr(self.description),repr(self.type))

    def __unicode__(self):
        return u"Picture : %s (%d\u00D7%d,'%s')" % \
               (self.type_string(),
                self.width,self.height,self.mime_type)

    #returns a new Image object from the data, description and type
    #raises InvalidImage if there is some error initializing
    @classmethod
    def new(cls, image_data, description, type):
        img = image_metrics(image_data)

        return Image(data=image_data,
                     mime_type=img.mime_type,
                     width=img.width,
                     height=img.height,
                     color_depth=img.bits_per_pixel,
                     color_count=img.color_count,
                     description=description,
                     type=type)

    def __eq__(self, image):
        import operator

        if (image is not None):
            return reduce(operator.and_,
                          [(getattr(self,attr) == getattr(image,attr))
                           for attr in
                           ("data","mime_type","width","height",
                            "color_depth","color_count","description",
                            "type")],True)
        else:
            return False

    def __ne__(self, image):
        return not self.__eq__(image)

#A container for multiple Images
class ImageMetaData:
    def __init__(self, images):
        self.__dict__['__images__'] = list(images)

    def images(self):
        #must return a copy of our internal array
        #otherwise this will likely not act as expected when deleting
        return self.__images__[:]

    def front_covers(self):
        return [i for i in self.images() if i.type == 0]

    def back_covers(self):
        return [i for i in self.images() if i.type == 1]

    def leaflet_pages(self):
        return [i for i in self.images() if i.type == 2]

    def media(self):
        return [i for i in self.images() if i.type == 3]

    def other_images(self):
        return [i for i in self.images() if i.type == 4]

    #image should be an Image object
    #this method should also affect the underlying metadata value
    #(e.g. adding a new Image to FlacMetaData should add another
    # METADATA_BLOCK_PICTURE block to the metadata)
    def add_image(self, image):
        self.__images__.append(image)

    #image should be an existing Image object
    #this method should also affect the underlying metadata value
    #(e.g. removing an existing Image from FlacMetaData should
    # remove that same METADATA_BLOCK_PICTURE block from the metadata)
    def delete_image(self, image):
        self.__images__.pop(self.__images__.index(image))



#######################
#Generic Audio File
#######################

class NotYetImplemented(Exception): pass

class AudioFile:
    SUFFIX = ""
    DEFAULT_COMPRESSION = ""
    COMPRESSION_MODES = ("",)
    BINARIES = tuple()

    def __init__(self, filename):
        self.filename = filename

    #takes a seekable file pointer rewound to the start of the file
    #returns True if that header describes this format
    #returns False if not
    @classmethod
    def is_type(cls, file):
        return False

    def bits_per_sample(self):
        raise NotYetImplemented()

    def channels(self):
        raise NotYetImplemented()

    def lossless(self):
        raise NotYetImplemented()

    def set_metadata(self, metadata):
        pass

    def get_metadata(self):
        return None

    def total_samples(self):
        raise NotYetImplemented()

    #returns the length of the audio in CD frames (1/75 of a second)
    def length(self):
        try:
            return (self.total_samples() * 75) / self.sample_rate()
        except ZeroDivisionError:
            return 0

    def sample_rate(self):
        raise NotYetImplemented()


    def to_pcm(self):
        raise NotYetImplemented()

    @classmethod
    def from_pcm(cls, filename, pcmreader, compression=None):
        raise NotYetImplemented()

    #writes the contents of this AudioFile to the given RIFF WAVE filename
    def to_wave(self, wave_filename):
        WaveAudio.from_pcm(wave_filename, self.to_pcm())

    #writes a new "filename" from the given RIFF WAVE filename
    #and at the given compression
    @classmethod
    def from_wave(cls, filename, wave_filename, compression=None):
        return cls.from_pcm(
            filename, WaveAudio(wave_filename).to_pcm(),compression)

    #This method should return True if the format supports storing
    #non-audio RIFF chunks during compression/decompression.
    #If this returns True on both ends of a track conversion,
    #we should route our data though a WAVE file so that such
    #foreign chunks are not lost in the process.
    @classmethod
    def supports_foreign_riff_chunks(cls):
        return False

    #returns this track's number
    #first checking metadata
    #and then making our best-guess from the filename
    #if we come up empty, returns 0
    def track_number(self):
        metadata = self.get_metadata()
        if ((metadata != None) and (metadata.track_number > 0)):
            return metadata.track_number
        else:
            try:
                return int(re.findall(r'\d{2}',self.filename)[0])
            except IndexError:
                return 0

    @classmethod
    def track_name(cls, track_number, track_metadata):
        if (track_metadata != None):
            return (FILENAME_FORMAT % \
                    {"track_number":track_number,
                     "suffix":cls.SUFFIX,
                     "track_name":track_metadata.track_name.replace('/','-'),
                     "album_name":track_metadata.album_name.replace('/','-'),
                     "artist_name":track_metadata.artist_name.replace('/','-'),
                     "performer_name":track_metadata.performer_name.replace('/','-'),
                     "copyright":track_metadata.copyright.replace('/','-'),
                     "year":track_metadata.year.replace('/','-')
                     }).encode(FS_ENCODING)
        else:
            return "%(track_number)2.2d.%(suffix)s" % \
                   {"track_number":track_number,
                    "suffix":cls.SUFFIX}

    #takes a list of filenames matching this AudioFile type
    #and adds the proper ReplayGain values to them
    @classmethod
    def add_replay_gain(cls, filenames):
        track_names = [track.filename for track in
                       open_files(filenames) if
                       isinstance(track,cls)]

    #returns True if we have the necessary binaries to add ReplayGain
    #returns False if not
    @classmethod
    def can_add_replay_gain(cls):
        return False

    def __eq__(self, audiofile):
        if (isinstance(audiofile, AudioFile)):
            p1 = self.to_pcm()
            p2 = audiofile.to_pcm()
            try:
                return pcm_cmp(p1,p2)
            finally:
                p1.close()
                p2.close()
        else:
            return False

    def __ne__(self, audiofile):
        return not self.__eq__(audiofile)

    #returns True if all of this AudioFile's required binaries are present
    #checks the __system_binaries__ class for which path to check on
    @classmethod
    def has_binaries(cls, system_binaries):
        import operator

        return reduce(operator.and_,
                      [system_binaries.can_execute(system_binaries[command])
                       for command in cls.BINARIES],
                      True)


class DummyAudioFile(AudioFile):
    def __init__(self, length, metadata):
        self.__length__ = length
        self.__metadata__ = metadata
        
        AudioFile.__init__(self,"")

    def get_metadata(self):
        return self.__metadata__

    def length(self):
        return self.__length__


from __wav__ import *
from __aiff__ import *
from __au__ import *
from __vorbiscomment__ import *
from __flac__ import *
from __id3__ import *
from __mp3__ import *
from __ape__ import *
from __vorbis__ import *        
from __m4a__ import *
from __wavpack__ import *
from __musepack__ import *
from __speex__ import *
    
#######################
#CD data
#######################

#keep in mind the whole of CD reading isn't remotely thread-safe
#due to the linear nature of CD access,
#reading from more than one track of a given CD at the same time
#is something code should avoid at all costs!
#there's simply no way to accomplish that cleanly

class CDDA:
    def __init__(self, device_name, speed=None):
        import cdio
        self.cdda = cdio.CDDA(device_name)
        self.total_tracks = self.cdda.total_tracks()
        if (speed != None):
            self.cdda.set_speed(speed)

    def __len__(self):
        return self.total_tracks

    def __getitem__(self, key):
        if ((key < 1) or (key > self.total_tracks)):
            raise IndexError(key)
        else:
            return CDTrackReader(self.cdda,int(key))

    def __iter__(self):
        for i in range(1,self.total_tracks + 1):
            yield self[i]

    def length(self):
        return self.cdda.length_in_seconds() * 75


class CDTrackLog(dict):
    #PARANOIA_CB_READ 	Read off adjust ???
    #PARANOIA_CB_VERIFY 	Verifying jitter
    #PARANOIA_CB_FIXUP_EDGE 	Fixed edge jitter
    #PARANOIA_CB_FIXUP_ATOM 	Fixed atom jitter
    #PARANOIA_CB_SCRATCH 	Unsupported
    #PARANOIA_CB_REPAIR 	Unsupported
    #PARANOIA_CB_SKIP 	Skip exhausted retry
    #PARANOIA_CB_DRIFT 	Skip exhausted retry
    #PARANOIA_CB_BACKOFF 	Unsupported
    #PARANOIA_CB_OVERLAP 	Dynamic overlap adjust
    #PARANOIA_CB_FIXUP_DROPPED 	Fixed dropped bytes
    #PARANOIA_CB_FIXUP_DUPED 	Fixed duplicate bytes
    #PARANOIA_CB_READERR 	Hard read error

    #log format is similar to cdda2wav's
    def __str__(self):
        return ", ".join(["%%(%s)d %s" % (field,field)
                          for field in 
                          ("rderr","skip","atom","edge",
                           "drop","dup","drift")]) % \
                           {"edge":self.get(2,0),
                            "atom":self.get(3,0),
                            "skip":self.get(6,0),
                            "drift":self.get(7,0),
                            "drop":self.get(10,0),
                            "dup":self.get(11,0),
                            "rderr":self.get(12,0)}


class CDTrackReader(PCMReader):
    #cdda is a cdio.CDDA object
    #track_number is which track this is from the disc, starting from 1
    def __init__(self, cdda, track_number):
        PCMReader.__init__(self, None,
                           sample_rate=44100,
                           channels=2,
                           bits_per_sample=16,
                           process=None)

        self.cdda = cdda
        self.track_number = track_number

        (self.__start__,self.__end__) = cdda.track_offsets(track_number)


        self.__position__ = self.__start__
        self.__cursor_placed__ = False

        self.rip_log = CDTrackLog()

    def offset(self):
        return self.__start__ + 150

    def length(self):
        return self.__end__ - self.__start__ + 1

    def log(self, i, v):
        if v in self.rip_log:
            self.rip_log[v] += 1
        else:
            self.rip_log[v] = 1


    def __read_sectors__(self, sectors):
        #if we haven't moved CDDA to the track start yet, do it now
        if (not self.__cursor_placed__):
            self.cdda.seek(self.__start__)
            cdio.set_read_callback(self.log)

            self.__position__ = self.__start__
            self.__cursor_placed__ = True

        if (self.__position__ <= self.__end__):
            s = self.cdda.read_sectors(min(sectors,
                                           self.__end__ - self.__position__ + 1))
            self.__position__ += sectors
            return s
        else:
            return ""

    def read(self, bytes):
        #returns a sector-aligned number of bytes
        #(divisible by 2352 bytes, basically)
        #or at least 1 sector's worth, if "bytes" is too small
        return self.__read_sectors__(max(bytes / 2352,1))
            

    def close(self):
        pass

#returns the value in item_list which occurs most often
def __most_numerous__(item_list):
    counts = {}

    for item in item_list:
        counts.setdefault(item,[]).append(item)

    return sorted([(item,len(counts[item])) for item in counts.keys()],
                  lambda x,y: cmp(x[1],y[1]))[-1][0]

from __freedb__ import *
from __image__ import *

#######################
#Multiple Jobs Handling
#######################

class ExecQueue:
    def __init__(self):
        self.todo = []

    #function is a Python function to apply
    #args is a tuple of arguments
    #kwargs, if not None, is a dict of additional keyword arguments
    def execute(self, function, args, kwargs=None):
        self.todo.append((function,args,kwargs))

    def __run__(self, function, args, kwargs):
        pid = os.fork()
        if (pid > 0):  #parent
            return pid
        else:          #child
            if (kwargs != None):
                function(*args,**kwargs)
            else:
                function(*args)
            sys.exit(0)

    #performs the queued actions in seperate subprocesses
    #"max_processes" number of times until the todo list is empty
    def run(self, max_processes=1):
        process_pool = set([])

        #fill the process_pool to the limit
        while ((len(self.todo) > 0) and (len(process_pool) < max_processes)):
            (function,args,kwargs) = self.todo.pop(0)
            process_pool.add(self.__run__(function,args,kwargs))
            #print "Filling %s" % (repr(process_pool))
        
        #as processes end, keep adding new ones to the pool
        #until we run out of queued jobs
        while (len(self.todo) > 0):
            try:
                process_pool.remove(os.waitpid(0,0)[0])
                (function,args,kwargs) = self.todo.pop(0)
                process_pool.add(self.__run__(function,args,kwargs))
                #print "Resuming %s" % (repr(process_pool))
            except KeyError:
                continue

        #finally, wait for the running jobs to finish
        while (len(process_pool) > 0):
            try:
                process_pool.remove(os.waitpid(0,0)[0])
                #print "Emptying %s" % (repr(process_pool))
            except KeyError:
                continue

#***ApeAudio temporarily removed***
#Without a legal alternative to mac-port, I shall have to re-implement
#Monkey's Audio with my own code in order to make it available again.
#Yet another reason to avoid that unpleasant file format...

AVAILABLE_TYPES = (FlacAudio,OggFlacAudio,
                   MP3Audio,MP2Audio,WaveAudio,
                   VorbisAudio,SpeexAudio,MusepackAudio,
                   AiffAudio,AuAudio,M4AAudio,WavPackAudio)

TYPE_MAP = dict([(track_type.SUFFIX,track_type) 
                 for track_type in AVAILABLE_TYPES
                 if track_type.has_binaries(BIN)]); del(track_type)

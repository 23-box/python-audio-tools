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


from audiotools import Con,re,os,pcm

class DVDAudio:
    """An object representing an entire DVD-Audio disc.

    A DVDAudio object contains one or more DVDATitle objects
    (accessible via the .titlesets attribute).
    Typically, only the first DVDTitle is interesting.
    Each DVDATitle then contains one or more DVDATrack objects.
    """

    SECTOR_SIZE = 2048
    PTS_PER_SECOND = 90000

    AUDIO_TS_IFO = Con.Struct(
        "AUDIO_TS_IFO",
        Con.Const(Con.Bytes("identifier", 12), "DVDAUDIO-AMG"),
        Con.UBInt32("AMG_start_sector"),
        Con.Padding(12),
        Con.UBInt32("AMGI_end_sector"),
        Con.UBInt16("DVD_version"),
        Con.Padding(4),
        Con.UBInt16("volume_count"),
        Con.UBInt16("volume_number"),
        Con.UBInt8("disc_side"),
        Con.Padding(4),
        Con.UBInt8("autoplay"),
        Con.UBInt32("ts_to_sv"),
        Con.Padding(10),
        Con.UBInt8("video_titlesets"),
        Con.UBInt8("audio_titlesets"),
        Con.Bytes("provider_identifier", 40))

    ATS_XX_S1 = Con.Struct(
        "ATS_XX",
        Con.Const(Con.String("identifier", 12), "DVDAUDIO-ATS"),
        Con.UBInt32("ATS_end_sector"),
        Con.Padding(12),
        Con.UBInt32("ATSI_end_sector"),
        Con.UBInt16("DVD_specification_version"),
        Con.UBInt32("VTS_category"),
        Con.Padding(90),
        Con.UBInt32("ATSI_MAT_end_sector"),
        Con.Padding(60),
        Con.UBInt32("VTSM_VOBS_start_sector"),
        Con.UBInt32("ATST_AOBS_start_sector"),
        Con.UBInt32("VTS_PTT_SRPT_start_sector"),
        Con.UBInt32("ATS_PGCI_UT_start_sector"),
        Con.UBInt32("VTSM_PGCI_UT_start_sector"),
        Con.UBInt32("VTS_TMAPT_start_sector"),
        Con.UBInt32("VTSM_C_ADT_start_sector"),
        Con.UBInt32("VTSM_VOBU_ADMA_start_sector"),
        Con.UBInt32("VTS_C_ADT_start_sector"),
        Con.UBInt32("VTS_VOBU_ADMAP_start_sector"),
        Con.Padding(24))

    ATS_XX_S2 = Con.Struct(
        "ATS_XX2",
        Con.UBInt16("title_count"),
        Con.Padding(2),
        Con.UBInt32("last_byte_address"),
        Con.StrictRepeater(
            lambda ctx: ctx['title_count'],
            Con.Struct('titles',
                       Con.UBInt16("unknown1"),
                       Con.UBInt16("unknown2"),
                       Con.UBInt32("byte_offset"))))

    ATS_TITLE = Con.Struct(
        "ATS_title",
        Con.Bytes("unknown1", 2),
        Con.UBInt8("tracks"),
        Con.UBInt8("indexes"),
        Con.UBInt32("track_length"),
        Con.Bytes("unknown2", 4),
        Con.UBInt16("sector_pointers_table"),
        Con.Bytes("unknown3", 2),
        Con.StrictRepeater(
            lambda ctx: ctx["tracks"],
            Con.Struct("timestamps",
                       Con.Bytes("unknown1", 2),
                       Con.Bytes("unknown2", 2),
                       Con.UBInt8("track_number"),
                       Con.Bytes("unknown3", 1),
                       Con.UBInt32("first_pts"),
                       Con.UBInt32("pts_length"),
                       Con.Padding(6))))

    ATS_SECTOR_POINTER = Con.Struct(
        "sector_pointer",
        Con.Const(Con.Bytes("unknown", 4),
                  '\x01\x00\x00\x00'),
        Con.UBInt32("first_sector"),
        Con.UBInt32("last_sector"))

    PACK_HEADER = Con.Struct(
        "pack_header",
        Con.Const(Con.UBInt32("sync_bytes"), 0x1BA),
        Con.Embed(Con.BitStruct(
                "markers",
                Con.Const(Con.Bits("marker1", 2), 1),
                Con.Bits("system_clock_high", 3),
                Con.Const(Con.Bits("marker2", 1), 1),
                Con.Bits("system_clock_mid", 15),
                Con.Const(Con.Bits("marker3", 1), 1),
                Con.Bits("system_clock_low", 15),
                Con.Const(Con.Bits("marker4", 1), 1),
                Con.Bits("scr_extension", 9),
                Con.Const(Con.Bits("marker5", 1), 1),
                Con.Bits("bit_rate", 22),
                Con.Const(Con.Bits("marker6", 2), 3),
                Con.Bits("reserved", 5),
                Con.Bits("stuffing_length", 3))),
        Con.StrictRepeater(lambda ctx: ctx["stuffing_length"],
                           Con.UBInt8("stuffing")))

    PES_HEADER = Con.Struct(
        "pes_header",
        Con.Const(Con.Bytes("start_code", 3), "\x00\x00\x01"),
        Con.UBInt8("stream_id"),
        Con.UBInt16("packet_length"))

    PACKET_HEADER = Con.Struct(
        "packet_header",
        Con.UBInt16("unknown1"),
        Con.Byte("pad1_size"),
        Con.StrictRepeater(lambda ctx: ctx["pad1_size"],
                           Con.Byte("pad1")),
        Con.Byte("stream_id"),
        Con.Byte("crc"),
        Con.Byte("padding"),
        Con.Switch("info",
                   lambda ctx: ctx["stream_id"],
                   {0xA0:Con.Struct(   #PCM info
                    "pcm",
                    Con.Byte("pad2_size"),
                    Con.UBInt16("first_audio_frame"),
                    Con.UBInt8("padding2"),
                    Con.Embed(Con.BitStruct(
                            "flags",
                            Con.Bits("group1_bps", 4),
                            Con.Bits("group2_bps", 4),
                            Con.Bits("group1_sample_rate", 4),
                            Con.Bits("group2_sample_rate", 4))),
                    Con.UBInt8("padding3"),
                    Con.UBInt8("channel_assignment")),

                    0xA1:Con.Struct(   #MLP info
                    "mlp",
                    Con.Byte("pad2_size"),
                    Con.StrictRepeater(lambda ctx: ctx["pad2_size"],
                                       Con.Byte("pad2")),
                    Con.Bytes("mlp_size", 4),
                    Con.Const(Con.Bytes("sync_words", 3), "\xF8\x72\x6F"),
                    Con.Const(Con.UBInt8("stream_type"), 0xBB),
                    Con.Embed(Con.BitStruct(
                            "flags",
                            Con.Bits("group1_bps", 4),
                            Con.Bits("group2_bps", 4),
                            Con.Bits("group1_sample_rate", 4),
                            Con.Bits("group2_sample_rate", 4),
                            Con.Bits("unknown1", 11),
                            Con.Bits("channel_assignment", 5),
                            Con.Bits("unknown2", 48))))}))

    def __init__(self, audio_ts_path):
        """A DVD-A which contains PCMReader-compatible track objects."""

        #an inventory of AUDIO_TS files converted to uppercase keys
        self.files = dict([(name.upper(),
                            os.path.join(audio_ts_path, name))
                           for name in os.listdir(audio_ts_path)])

        titleset_numbers = list(self.__titlesets__())

        #for each titleset, read an ATS_XX_0.IFO file
        #each titleset contains one or more DVDATitle objects
        #and each DVDATitle object contains one or more DVDATrack objects
        self.titlesets = [self.__titles__(titleset) for titleset in
                          titleset_numbers]

        #for each titleset, calculate the lengths of the corresponding AOBs
        #in terms of 2048 byte sectors
        self.aob_sectors = []
        for titleset in titleset_numbers:
            aob_re = re.compile("ATS_%2.2d_\\d\\.AOB" % (titleset))
            titleset_aobs = dict([(key, value) for (key, value) in
                                  self.files.items()
                                  if (aob_re.match(key))])
            for aob_length in [os.path.getsize(titleset_aobs[key]) /
                               DVDAudio.SECTOR_SIZE
                               for key in sorted(titleset_aobs.keys())]:
                if (len(self.aob_sectors) == 0):
                    self.aob_sectors.append(
                        (0, aob_length))
                else:
                    self.aob_sectors.append(
                        (self.aob_sectors[-1][1],
                         self.aob_sectors[-1][1] + aob_length))

    def __titlesets__(self):
        """return valid audio titleset integers from AUDIO_TS.IFO"""

        try:
            f = open(self.files['AUDIO_TS.IFO'], 'rb')
        except KeyError,IOError:
            raise InvalidDVDA(u"unable to open AUDIO_TS.IFO")
        try:
            try:
                for titleset in xrange(
                    1,
                    DVDAudio.AUDIO_TS_IFO.parse_stream(f).audio_titlesets + 1):
                    #ensure there are IFO files and AOBs
                    #for each valid titleset
                    if (("ATS_%2.2d_0.IFO" % (titleset) in
                         self.files.keys()) and
                        ("ATS_%2.2d_1.AOB" % (titleset) in
                         self.files.keys())):
                        yield titleset

            except Con.ConstError:
                raise InvalidDVDA(u"invalid AUDIO_TS.IFO")
        finally:
            f.close()

    def __titles__(self, titleset):
        """returns a list of DVDATitle objects for the given titleset"""

        try:
            f = open(self.files['ATS_%2.2d_0.IFO' % (titleset)], 'rb')
        except KeyError,IOError:
            raise InvalidDVDA(
                u"unable to open ATS_%2.2d_0.IFO" % (titleset))
        try:
            try:
                #the first sector contains little of interest
                #but we'll read it to check the identifier string
                DVDAudio.ATS_XX_S1.parse_stream(f)
            except Con.ConstError:
                raise InvalidDVDA(u"invalid ATS_%2.2d_0.IFO" % (titleset))

            #then move to the second sector and continue parsing
            f.seek(DVDAudio.SECTOR_SIZE, os.SEEK_SET)

            #may contain one or more titles
            title_records = DVDAudio.ATS_XX_S2.parse_stream(f)

            titles = []

            for (title_number,
                 title_offset) in enumerate(title_records.titles):
                f.seek(DVDAudio.SECTOR_SIZE +
                       title_offset.byte_offset,
                       os.SEEK_SET)
                title = DVDAudio.ATS_TITLE.parse_stream(f)

                f.seek(DVDAudio.SECTOR_SIZE +
                       title_offset.byte_offset +
                       title.sector_pointers_table,
                       os.SEEK_SET)

                titles.append(DVDATitle(
                        length=title.track_length,
                        tracks=[DVDATrack(dvdaudio=self,
                                          titleset=titleset,
                                          title=title_number + 1,
                                          track=timestamp.track_number,
                                          first_pts=timestamp.first_pts,
                                          pts_length=timestamp.pts_length,
                                          first_sector=pointers.first_sector,
                                          last_sector=pointers.last_sector)
                                for (timestamp, pointers) in
                                zip(title.timestamps, [
                                    DVDAudio.ATS_SECTOR_POINTER.parse_stream(f)
                                    for i in xrange(title.tracks)])]))

            return titles


        finally:
            f.close()


class InvalidDVDA(Exception):
    pass

class DVDATitle:
    """An object representing a DVD-Audio title.

    Contains one or more DVDATrack objects
    which may are accessible via the .tracks attribute.
    """

    def __init__(self, length, tracks):
        """length is in PTS ticks, tracks is a list of DVDATrack objects"""

        self.length = length
        self.tracks = tracks

    def __repr__(self):
        return "DVDATitle(%s, %s)" % (repr(self.length), repr(self.tracks))

class DVDATrack:
    """An object representing an individual DVD-Audio track."""

    SAMPLE_RATE = [48000, 96000, 192000, 0, 0, 0, 0, 0,
                   44100, 88200, 176400, 0, 0, 0, 0, 0]
    CHANNELS = [1, 2, 3, 4, 3, 4, 5, 3, 4, 5, 4, 5, 6, 4, 5, 4, 5, 6, 5, 5, 6]
    CHANNEL_MASK = [  0x4,  0x3, 0x103,  0x33,  0xB, 0x10B, 0x3B, 0x7,
                    0x107, 0x37,   0xF, 0x10F, 0x3F, 0x107, 0x37, 0xF,
                    0x10F, 0x3F,  0x3B,  0x37, 0x3F]
    BITS_PER_SAMPLE = [16, 20, 24] + [0] * 13

    def __init__(self, dvdaudio,
                 titleset, title, track,
                 first_pts, pts_length,
                 first_sector, last_sector):
        self.dvdaudio = dvdaudio
        self.titleset = titleset
        self.title = title
        self.track = track
        self.first_pts = first_pts
        self.pts_length = pts_length
        self.first_sector = first_sector
        self.last_sector = last_sector

    def __repr__(self):
        return "DVDATrack(%s)" % \
            (", ".join(["%s=%s" % (attr, getattr(self, attr))
                        for attr in ["titleset",
                                     "title",
                                     "track",
                                     "first_pts",
                                     "pts_length",
                                     "first_sector",
                                     "last_sector"]]))

    def sectors(self):
        """iterates (aob_file, start_sector, end_sector)

        for each AOB file necessary to extract the track's data
        in the order in which they should be read."""

        track_sectors = Rangeset(self.first_sector,
                                 self.last_sector + 1)

        for (i, (start_sector,
                 end_sector)) in enumerate(self.dvdaudio.aob_sectors):
            aob_sectors = Rangeset(start_sector, end_sector)
            intersection = aob_sectors & track_sectors
            if (len(intersection)):
                yield (self.dvdaudio.files["ATS_%2.2d_%d.AOB" % \
                                               (self.titleset, i + 1)],
                       intersection.start - start_sector,
                       intersection.end - start_sector)

    def packets(self):
        """iterates a packet payload string for each packet of track data

        This does not include the PES header data."""

        def payload(packet):
            pad1_len = ord(packet[2])
            pad2_len = ord(packet[3 + pad1_len + 3])
            return packet[3 + pad1_len + 4 + pad2_len:]

        SECTOR_SIZE = DVDAudio.SECTOR_SIZE
        PACK_HEADER = DVDAudio.PACK_HEADER
        PES_HEADER = DVDAudio.PES_HEADER

        for (aob_file, start_sector, end_sector) in self.sectors():
            aob = open(aob_file, 'rb')
            try:
                aob.seek(start_sector * DVDAudio.SECTOR_SIZE, os.SEEK_SET)
                while (start_sector < end_sector):
                    packet_size = SECTOR_SIZE
                    pack_header = PACK_HEADER.parse_stream(aob)
                    packet_size -= (14 + len(pack_header.stuffing))
                    while (packet_size > 0):
                        pes_header = PES_HEADER.parse_stream(aob)
                        packet_size -= 6
                        if (pes_header.stream_id == 0xBD):
                            yield payload(aob.read(pes_header.packet_length))
                        else:
                            aob.read(pes_header.packet_length)
                        packet_size -= pes_header.packet_length

                    start_sector += 1
            finally:
                aob.close()

    def info(self):
        """returns (sample_rate, channels, channel_mask, bits_per_sample, type)

        taken from the track's first sector
        where type 0xA0 is PCM and type 0xA1 is MLP"""

        (aob_file, start_sector, end_sector) = self.sectors().next()
        aob = open(aob_file, 'rb')
        try:
            aob.seek(start_sector * DVDAudio.SECTOR_SIZE, os.SEEK_SET)
            packet_size = DVDAudio.SECTOR_SIZE
            pack_header = DVDAudio.PACK_HEADER.parse_stream(aob)
            packet_size -= (14 + len(pack_header.stuffing))
            while (packet_size > 0):
                pes_header = DVDAudio.PES_HEADER.parse_stream(aob)
                packet_size -= 6
                if (pes_header.stream_id == 0xBD):
                    #the first packet of audio data

                    header = DVDAudio.PACKET_HEADER.parse_stream(aob)
                    return (DVDATrack.SAMPLE_RATE[
                              header.info.group1_sample_rate],
                            DVDATrack.CHANNELS[
                              header.info.channel_assignment],
                            DVDATrack.CHANNEL_MASK[
                              header.info.channel_assignment],
                            DVDATrack.BITS_PER_SAMPLE[
                              header.info.group1_bps],
                            header.stream_id)
                else:
                    aob.read(pes_header.packet_length)
                packet_size -= pes_header.packet_length
            else:
                raise InvalidDVDA(u"No audio data found in first sector")
        finally:
            aob.close()

    def to_pcm(self):
        (sample_rate,
         channels,
         channel_mask,
         bits_per_sample,
         track_type) = self.info()
        if (track_type == 0xA0):
            return DVDAPCMReader(packets=self.packets(),
                                 sample_rate=sample_rate,
                                 channels=channels,
                                 channel_mask=channel_mask,
                                 bits_per_sample=bits_per_sample)
        elif (track_type == 0xA1):
            from audiotools.decoders import MLPDecoder

            return MLPDecoder(__PacketReader__(self.packets()))
        else:
            raise ValueError(_(u"unsupported DVD-A track type"))

class __PacketReader__:
    def __init__(self, packets):
        self.packets = packets

    def read(self, bytes):
        if (self.packets is not None):
            try:
                return self.packets.next()
            except StopIteration:
                self.packets = None
                return ""

    def close(self):
        self.packets = None

PacketReader = __PacketReader__

class DVDAPCMReader:
    def __init__(self, packets,
                 sample_rate, channels, channel_mask, bits_per_sample):
        self.packets = __PacketReader__(packets)
        self.sample_rate = sample_rate
        self.channels = channels
        self.channel_mask = channel_mask
        self.bits_per_sample = bits_per_sample

    def read(self, bytes):
        #FIXME - reorder channels if > 2
        #though I've yet to see a PCM DVD-Audio with more than 2 channels

        #FIXME - PCM apparently needs some additional post-processing

        return pcm.FrameList(self.packets.read(bytes),
                             self.channels,
                             self.bits_per_sample,
                             True,
                             True)

    def close(self):
        self.packets.close()


class Rangeset:
    """An optimized combination of range() and set()"""

    #The purpose of this class is for finding the subset of
    #two Rangesets, such as with:
    #
    # >>> Rangeset(1, 10) & Rangeset(5, 15)
    # Rangeset(5, 10)
    #
    #which returns another Rangeset object.
    #This is preferable to performing:
    #
    # >>> set(range(1, 10)) & set(range(5, 15))
    # set([8, 9, 5, 6, 7])
    #
    #which allocates lots of unnecessary values
    #when all we're interested in is the min and max.

    def __init__(self, start, end):
        self.start = start
        self.end = end

    def __repr__(self):
        return "Rangeset(%s, %s)" % (repr(self.start), repr(self.end))

    def __len__(self):
        return self.end - self.start

    def __getitem__(self, i):
        if (i >= 0):
            if (i < len(self)):
                return self.start + i
            else:
                raise IndexError(i)
        else:
            if (-i - 1 < len(self)):
                return self.end + i
            else:
                raise IndexError(i)

    def __and__(self, rangeset):
        min_point = max(self.start, rangeset.start)
        max_point = min(self.end, rangeset.end)

        if (min_point <= max_point):
            return Rangeset(min_point, max_point)
        else:
            return Rangeset(0, 0)

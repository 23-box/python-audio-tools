#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2007-2013  Brian Langenberger

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

if (sys.version_info < (2, 7, 0, 'final', 0)):
    print >> sys.stderr, "*** Python 2.7.0 or better required"
    sys.exit(1)

import os
import os.path
import re
import subprocess
from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext as _build_ext
from ConfigParser import (ConfigParser, NoSectionError, NoOptionError)

def get_library_availability(config, library):
    """given ConfigParser object and library name string
    returns True if library is present,
    False if library is not present,
    None if one should probe for the library

    default is None"""

    try:
        if (config.get("Libraries", library) == "probe"):
            return None
        else:
            try:
                return config.getboolean("Libraries", library)
            except ValueError:
                return None
    except NoSectionError:
        return None
    except NoOptionError:
        return None

parser = ConfigParser()
parser.read(["setup.cfg"])

HAS_LIBCDIO = get_library_availability(parser, "libcdio")
HAS_LIBPULSE = get_library_availability(parser, "libpulse")
HAS_LIBALSA = get_library_availability(parser, "alsa")
HAS_MPG123 = get_library_availability(parser, "libmpg123")
HAS_VORBISFILE = get_library_availability(parser, "libvorbisfile")
HAS_LAME = get_library_availability(parser, "libmp3lame")
HAS_TWOLAME = get_library_availability(parser, "libtwolame")

VERSION = re.search(r'VERSION\s*=\s"(.+?)"',
                    open(os.path.join(
                        os.path.dirname(sys.argv[0]),
                        "audiotools/__init__.py")).read()).group(1)


class build_ext(_build_ext):
    def build_extensions(self):
        _build_ext.build_extensions(self)
        print "=" * 60
        print "Python Audio Tools %s Setup" % (VERSION)
        print "=" * 60

        if (HAS_LIBCDIO is None):
            #indicate whether libcdio is found or where to get it
            if (len([e for e in self.extensions if
                     isinstance(e, audiotools_cdio)]) > 0):
                print "--- libcdio found"
            else:
                print "*** libcdio not found"
                print "    for CDDA reading support, install libcdio from:"
                print ""
                print "    http://www.gnu.org/software/libcdio/"
                print ""
                print "    or your system's package manager"
                print ""

        output = [e for e in self.extensions if
                  isinstance(e, audiotools_output)][0]

        if ('linux' in sys.platform):
            #if on Linux, indicate whether PulseAudio and ALSA are found
            #or where to get them

            if (output.has_pulseaudio):
                print "--- libpulse found"
            else:
                print "*** libpulse not found"
                print "    for PulseAudio output support, install libpulse from:"
                print ""
                print "    http://www.freedesktop.org/wiki/Software/PulseAudio/"
                print ""
                print "    or your system's package manager"
                print ""

            if (output.has_alsa):
                print "--- libasound found"
            else:
                print "*** libasound not found"
                print "    for ALSA output support, install libasound from:"
                print ""
                print "    http://alsa-project.org"
                print ""
                print "    or your system's package manager"
                print ""

        if (output.has_coreaudio):
            print "--- CoreAudio found"
        else:
            #if CoreAudio not found, we must not be on a Mac OS X machine
            #so no reason to tell the user to get it
            pass

        decoders = [e for e in self.extensions if
                    isinstance(e, audiotools_decoders)][0]

        if (decoders.has_mpg123):
            print "--- libmpg123 found"
        else:
            print "*** libmpg123 not found"
            print "    for MP3 and MP2 support, install libmpg123 from:"
            print ""
            print "    http://www.mpg123.org"
            print ""
            print "    or your system's package manager"
            print ""

        if (decoders.has_vorbis):
            print "--- libvorbisfile found"
        else:
            print "*** libvorbisfile not found"
            print "    for Ogg Vorbis support, install libvorbisfile from:"
            print ""
            print "    http://www.xiph.org"
            print ""
            print "    or your system's package manager"
            print ""

        encoders = [e for e in self.extensions if
                    isinstance(e, audiotools_encoders)][0]

        if (encoders.has_lame):
            print "--- libmp3lame found"
        else:
            print "*** libmp3lame not found"
            print "    for MP3 and MP2 support, install libmp3lame from:"
            print ""
            print "    http://lame.sourceforge.net"
            print ""
            print "    or your system's package manager"
            print ""

        if (encoders.has_twolame):
            print "--- libtwolame found"
        else:
            print "*** libtwolame not found"
            print "    for MP2 support, install libtwolame from:"
            print ""
            print "    http://twolame.sourceforge.net"
            print ""
            print "    or your system's package manager"
            print ""

        if (encoders.has_vorbis):
            print "--- libvorbisenc found"
        else:
            print "*** libvorbisenc not found"
            print "    for Vorbis support, install libvorbisenc from:"
            print ""
            print "    http://www.xiph.org"
            print ""
            print "    or your system's package manager"
            print ""


class audiotools_cdio(Extension):
    def __init__(self, extra_link_args=None):
        """extra_link_args is a list of argument strings
        from pkg-config, or None if we're to use the standard
        libcdio libraries"""

        Extension.__init__(
            self,
            'audiotools.cdio',
            sources=['src/cdiomodule.c'],
            libraries=(['cdio',
                        'cdio_paranoia',
                        'cdio_cdda',
                        'm'] if extra_link_args is None else []),
            extra_link_args=(extra_link_args if
                             extra_link_args is not None else []))


class audiotools_pcm(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools.pcm',
                           sources=['src/pcm.c'])


class audiotools_pcmconverter(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools.pcmconverter',
                           sources=['src/pcmconverter.c',
                                    'src/pcmconv.c',
                                    'src/array.c',
                                    'src/bitstream.c',
                                    'src/buffer.c',
                                    'src/func_io.c',
                                    'src/samplerate/samplerate.c',
                                    'src/samplerate/src_sinc.c',
                                    'src/samplerate/src_zoh.c',
                                    'src/samplerate/src_linear.c'])


class audiotools_replaygain(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools.replaygain',
                           sources=['src/replaygain.c',
                                    'src/pcmconv.c',
                                    'src/array.c',
                                    'src/bitstream.c',
                                    'src/buffer.c',
                                    'src/func_io.c'])


class audiotools_decoders(Extension):
    def __init__(self, has_mp3=None, has_vorbisfile=None):
        defines = [("VERSION", VERSION)]
        sources = ['src/array.c',
                   'src/pcmconv.c',
                   'src/common/md5.c',
                   'src/bitstream.c',
                   'src/buffer.c',
                   'src/func_io.c',
                   'src/huffman.c',
                   'src/decoders/flac.c',
                   'src/decoders/oggflac.c',
                   'src/common/flac_crc.c',
                   'src/ogg.c',
                   'src/ogg_crc.c',
                   'src/common/tta_crc.c',
                   'src/decoders/shn.c',
                   'src/decoders/alac.c',
                   'src/decoders/wavpack.c',
                   'src/decoders/tta.c',
                   'src/decoders/mlp.c',
                   'src/decoders/aobpcm.c',
                   'src/decoders/aob.c',
                   'src/decoders/sine.c',
                   'src/decoders/mod_cppm.c',
                   'src/decoders.c']
        libraries = []
        link_args = []

        def has_mpg123():
            try:
                mpg123 = subprocess.Popen(
                    ["mpg123", "--version"],
                    stdout=open(os.devnull, "wb"),
                    stderr=open(os.devnull, "wb"))
                mpg123.wait()

                return True
            except OSError:
                return False

        if (has_mp3 is None):
            #probe for libmpg123 via pkg-config
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "libmpg123"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                mpg123_stdout = pkg_config.stdout.read().strip()
                mpg123_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libmpg123 found via pkg-config
                    #so append pkg-config's results to link arguments as-is
                    defines.append(("HAS_MP3", None))
                    sources.append("src/decoders/mp3.c")
                    link_args.extend(mpg123_stdout.split())
                    self.has_mpg123 = True
                else:
                    #libmpg123 not found via pkg-config
                    self.has_mpg123 = False
            except OSError:
                #pkg-config not found
                #so see if mpg123 binary is present
                try:
                    mpg123 = subprocess.Popen(
                        ["mpg123", "--version"],
                        stdout=open(os.devnull, "wb"),
                        stderr=open(os.devnull, "wb"))
                    mpg123.wait()

                    defines.append(("HAS_MP3", None))
                    sources.append("src/decoders/mp3.c")
                    libraries.append("mpg123")
                    self.has_mpg123 = True
                except OSError:
                    self.has_mpg123 = False
        elif (has_mp3):
            #user promises libmpg123 is present on system
            defines.append(("HAS_MP3", None))
            sources.append("src/decoders/mp3.c")
            libraries.append("mpg123")
            self.has_mpg123 = True
        else:
            self.has_mpg123 = False

        if (has_vorbisfile is None):
            #probe for vorbisfile via pkg-config
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "vorbisfile"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                vorbisfile_stdout = pkg_config.stdout.read().strip()
                vorbisfile_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #vorbisfile found via pkg-config
                    #so append pkg-config's results to link arguments as-is
                    defines.append(("HAS_VORBIS", None))
                    sources.append("src/decoders/vorbis.c")
                    link_args.extend(vorbisfile_stdout.split())
                    self.has_vorbis = True
                else:
                    #vorbisfile not found via pkg-config
                    self.has_vorbis = False
            except OSError:
                #pkg-config not found
                #so see if oggdec binary is present
                try:
                    oggdec = subprocess.Popen(
                        ["oggdec", "--version"],
                        stdout=open(os.devnull, "wb"),
                        stderr=open(os.devnull, "wb"))
                    oggdec.wait()

                    defines.append(("HAS_VORBIS", None))
                    sources.append("src/decoders/vorbis.c")
                    libraries.extend(["vorbisfile", "vorbis", "ogg"])
                    self.has_vorbis = True
                except OSError:
                    self.has_vorbis = False

        elif (has_vorbisfile):
            #user promises vorbisfile is present on system
            defines.append(("HAS_VORBIS", None))
            sources.append("src/decoders/vorbis.c")
            libraries.extend(["vorbisfile", "vorbis", "ogg"])
            self.has_vorbis = True
        else:
            self.has_vorbis = False

        if (sys.platform == 'linux2'):
            defines.extend([('DVD_STRUCT_IN_LINUX_CDROM_H', None),
                            ('HAVE_LINUX_DVD_STRUCT', None),
                            ('HAS_UNPROT', None)])
            sources.extend(['src/decoders/cppm.c',
                            'src/decoders/ioctl.c',
                            'src/decoders/dvd_css.c'])

        Extension.__init__(self,
                           'audiotools.decoders',
                           sources=sources,
                           define_macros=defines,
                           libraries=libraries,
                           extra_link_args=link_args)


class audiotools_encoders(Extension):
    def __init__(self, has_mp3=None, has_mp2=None, has_vorbis=None):
        defines = [("VERSION", VERSION)]
        sources = ['src/array.c',
                   'src/pcmconv.c',
                   'src/bitstream.c',
                   'src/buffer.c',
                   'src/func_io.c',
                   'src/common/md5.c',
                   'src/encoders/flac.c',
                   'src/common/flac_crc.c',
                   'src/common/tta_crc.c',
                   'src/encoders/shn.c',
                   'src/encoders/alac.c',
                   'src/encoders/wavpack.c',
                   'src/encoders/tta.c',
                   'src/encoders.c']
        libraries = []
        link_args = []

        if (has_mp3 is None):
            #probe for libmp3lame via pkg-config
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "libmp3lame"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                lame_stdout = pkg_config.stdout.read().strip()
                lame_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libmp3lame found via pkg-config
                    #so append pkg-config's results to link arguments as-is
                    defines.append(("HAS_MP3", None))
                    sources.append("src/encoders/mp3.c")
                    link_args.extend(lame_stdout.split())
                    self.has_lame = True
                else:
                    #lame not found via pkg-config
                    self.has_lame = False
            except OSError:
                #pkg-config not found
                #so see if lame binary is present
                try:
                    lame = subprocess.Popen(
                        ["lame", "--version"],
                        stdout=open(os.devnull, "wb"),
                        stderr=open(os.devnull, "wb"))
                    lame.wait()

                    defines.append(("HAS_MP3", None))
                    sources.append("src/encoders/mp3.c")
                    libraries.append("mp3lame")
                    self.has_lame = True
                except OSError:
                    self.has_lame = False
        elif (has_mp3):
            #user promises libmp3lame is present on system
            defines.append(("HAS_MP3", None))
            sources.append("src/encoders/mp3.c")
            libraries.append("mp3lame")
            self.has_lame = True
        else:
            self.has_lame = False

        if (has_mp2 is None):
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "twolame"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                twolame_stdout = pkg_config.stdout.read().strip()
                twolame_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libtwolame found via pkg-config
                    #so append pkg-config's results to link arguments as-is
                    defines.append(("HAS_MP2", None))
                    sources.append("src/encoders/mp2.c")
                    link_args.extend(twolame_stdout.split())
                    self.has_twolame = True
                else:
                    #twolame not found via pkg-config
                    self.has_twolame = False
            except OSError:
                #pkg-config not found
                #so see if twolame binary is present
                try:
                    twolame = subprocess.Popen(
                        ["twolame", "--version"],
                        stdout=open(os.devnull, "wb"),
                        stderr=open(os.devnull, "wb"))
                    twolame.wait()

                    defines.append(("HAS_MP2", None))
                    sources.append("src/encoders/mp2.c")
                    libraries.append("twolame")
                    self.has_twolame = True
                except OSError:
                    self.has_twolame = False
        elif (has_mp2):
            #user promises libtwolame is present on system
            defines.append(("HAS_MP2", None))
            sources.append("src/encoders/mp2.c")
            libraries.append("twolame")
            self.has_twolame = True
        else:
            self.has_twolame = False

        if (has_vorbis is None):
            #probe for vorbis encoding libraries via pkg-config
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "vorbis", "ogg", "vorbisenc"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                vorbis_stdout = pkg_config.stdout.read().strip()
                vorbis_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libvorbisenc found via pkg-config
                    #so append pkg-config's results to link arguments as-is
                    defines.append(("HAS_VORBIS", None))
                    sources.append("src/encoders/vorbis.c")
                    link_args.extend(vorbis_stdout.split())
                    self.has_vorbis = True
                else:
                    #libvorbisenc not found via pkg-config
                    self.has_vorbis = False
            except OSError:
                #pkg-config not found
                #so see if oggenc binary is present
                try:
                    oggenc = subprocess.Popen(
                        ["oggenc", "--version"],
                        stdout=open(os.devnull, "wb"),
                        stderr=open(os.devnull, "wb"))
                    oggenc.wait()

                    defines.append(("HAS_VORBIS", None))
                    sources.append("src/encoders/vorbis.c")
                    libraries.extend(["vorbis", "ogg", "vorbisenc"])
                    self.has_vorbis = True
                except OSError:
                    self.has_vorbis = False
        elif (has_vorbis):
            #user promises libvorbis is present on system
            defines.append(("HAS_VORBIS", None))
            sources.append("src/encoders/vorbis.c")
            libraries.extend(["vorbis", "ogg", "vorbisenc"])
            self.has_vorbis = True
        else:
            self.has_vorbis = False

        Extension.__init__(self,
                           'audiotools.encoders',
                           sources=sources,
                           define_macros=defines,
                           libraries=libraries,
                           extra_link_args=link_args)


class audiotools_bitstream(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools.bitstream',
                           sources=['src/mod_bitstream.c',
                                    'src/bitstream.c',
                                    'src/buffer.c',
                                    'src/func_io.c',
                                    'src/huffman.c'])


class audiotools_verify(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools.verify',
                           sources=['src/verify.c',
                                    'src/bitstream.c',
                                    'src/buffer.c',
                                    'src/func_io.c'])


class audiotools_ogg(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools._ogg',
                           sources=['src/ogg.c',
                                    'src/ogg_crc.c',
                                    'src/mod_ogg.c',
                                    'src/bitstream.c',
                                    'src/func_io.c',
                                    'src/buffer.c'])


class audiotools_accuraterip(Extension):
    def __init__(self):
        Extension.__init__(self,
                           'audiotools._accuraterip',
                           sources=['src/accuraterip.c'])


class audiotools_output(Extension):
    def __init__(self, has_pulseaudio=None, has_alsa=None):
        """has_pulseaudio and has_alsa may be True, False or None

        True or False guarantees the given libraries are present
        or not present, while None indicates we should probe the system
        """

        sources = ['src/output.c']
        libraries = []
        defines = []
        link_args = []

        #assume MacOS X always has CoreAudio
        if (sys.platform == 'darwin'):
            sources.append('src/output/core_audio.c')
            defines.append(("CORE_AUDIO", "1"))
            link_args.extend(["-framework", "AudioToolbox",
                              "-framework", "AudioUnit",
                              "-framework", "CoreServices"])
            self.has_coreaudio = True
        else:
            self.has_coreaudio = False

        if (has_pulseaudio is None):
            #detect PulseAudio's presence using pkg-config, if possible
            try:
                pkg_config = subprocess.Popen(
                    ["pkg-config", "--libs", "libpulse"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)

                libpulse_stdout = pkg_config.stdout.read().strip()
                libpulse_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libpulse found via pkg-config
                    #so append pkg-config's results
                    sources.append("src/output/pulseaudio.c")
                    defines.append(("PULSEAUDIO", "1"))
                    link_args.extend(libpulse_stdout.split())
                    self.has_pulseaudio = True
                else:
                    #libpulse not found in pkg-config
                    self.has_pulseaudio = False
            except OSError:
                #pkg-config not found
                self.has_pulseaudio = False
        elif (has_pulseaudio):
            #user promises libpulse is present on system
            libraries.append("pulse")
            self.has_pulseaudio = True

        if (has_alsa is None):
            #detense ALSA's present using pkg-config, if possible
            try:
                pkg_config = subprocess.Popen(["pkg-config", "--libs", "alsa"],
                                              stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE)

                libalsa_stdout = pkg_config.stdout.read().strip()
                libalsa_stderr = pkg_config.stderr.read()
                if (pkg_config.wait() == 0):
                    #libalsa found via pkg-config
                    #so append pkg-config's results

                    if ("src/pcmconv.c" not in sources):
                        #only include pcmconv.c once
                        sources.append("src/pcmconv.c")

                    sources.append("src/output/alsa.c")
                    defines.append(("ALSA", "1"))
                    link_args.extend(libalsa_stdout.split())
                    self.has_alsa = True
                else:
                    #libalsa not found in pkg-config
                    self.has_alsa = False
            except OSError:
                #pkg-config not found
                self.has_alsa = False
        elif (has_alsa):
            #user promises libasound is present on system
            libraries.append("asound")
            self.has_alsa = True

        Extension.__init__(self,
                           'audiotools.output',
                           libraries=libraries,
                           sources=sources,
                           define_macros=defines,
                           extra_link_args=link_args)


ext_modules = [audiotools_pcm(),
               audiotools_pcmconverter(),
               audiotools_replaygain(),
               audiotools_decoders(has_mp3=HAS_MPG123,
                                   has_vorbisfile=HAS_VORBISFILE),
               audiotools_encoders(has_mp3=HAS_LAME,
                                   has_mp2=HAS_TWOLAME,
                                   has_vorbis=True),  #FIXME
               audiotools_bitstream(),
               audiotools_ogg(),
               audiotools_verify(),
               audiotools_accuraterip(),
               audiotools_output(has_pulseaudio=HAS_LIBPULSE,
                                 has_alsa=HAS_LIBALSA)]


scripts = ["audiotools-config",
           "coverdump",
           "covertag",
           "coverview",
           "dvda2track",
           "dvdainfo",
           "track2cd",
           "track2track",
           "trackcat",
           "trackcmp",
           "trackinfo",
           "tracklength",
           "tracklint",
           "trackplay",
           "trackrename",
           "tracksplit",
           "tracktag",
           "trackverify"]


if (HAS_LIBCDIO is None):
    #probe for libcdio via pkg-config
    try:
        pkg_config = subprocess.Popen(
            ["pkg-config", "--libs",
             "libcdio", "libcdio_cdda", "libcdio_paranoia"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

        libcdio_stdout = pkg_config.stdout.read().strip()
        libcdio_stderr = pkg_config.stderr.read()
        if (pkg_config.wait() == 0):
            #libcdio found via pkg-config
            #so use pkg-config's results
            ext_modules.append(audiotools_cdio(libcdio_stdout.split()))

            scripts.extend(["cd2track",
                            "cdinfo",
                            "cdplay"])
        else:
            #libcdio not found in pkg-config
            #so look for one of libcdio's accompanying executables
            try:
                cd_info = subprocess.Popen(
                    ["cd-info", "--version"],
                    stdout=open(os.devnull, "wb"),
                    stderr=open(os.devnull, "wb"))
                cd_info.wait()

                ext_modules.append(audiotools_cdio())

                scripts.extend(["cd2track",
                                "cdinfo",
                                "cdplay"])
            except OSError:
                #cd-info not found either
                pass
    except OSError:
        #pkg-config not found
        #so look for one of libcdio's accompanying executables
        try:
            cd_info = subprocess.Popen(
                ["cd-info", "--version"],
                stdout=open(os.devnull, "wb"),
                stderr=open(os.devnull, "wb"))
            ext_modules.append(audiotools_cdio())

            scripts.extend(["cd2track",
                            "cdinfo",
                            "cdplay"])
        except OSError:
            #cd-info not found either
            pass
elif (HAS_LIBCDIO):
    ext_modules.append(audiotools_cdio())

    scripts.extend(["cd2track",
                    "cdinfo",
                    "cdplay"])

setup(name='Python Audio Tools',
      version=VERSION,
      description='A collection of audio handling utilities',
      author='Brian Langenberger',
      author_email='tuffy@users.sourceforge.net',
      url='http://audiotools.sourceforge.net',
      packages=["audiotools",
                "audiotools.py_decoders",
                "audiotools.py_encoders"],
      ext_modules=ext_modules,
      cmdclass={"build_ext": build_ext},
      data_files=[("/etc", ["audiotools.cfg"])],
      scripts=scripts)

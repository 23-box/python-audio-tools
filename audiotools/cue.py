#!/usr/bin/python

#Audio Tools, a module and set of tools for manipulating audio data
#Copyright (C) 2008  Brian Langenberger

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

import re

SPACE = 0x0
TAG = 0x1
NUMBER = 0x2
EOL = 0x4
STRING = 0x8
ISRC = 0x10
TIMESTAMP = 0x20

def tokens(cuedata):
    full_length = len(cuedata)
    cuedata = cuedata.lstrip("\xEF\xBB\xBF")
    line_number = 1

    #This isn't completely accurate since the whitespace requirements
    #between tokens aren't enforced.
    TOKENS = [(re.compile("^(%s)" % (s)),element) for (s,element) in
              [(r'[A-Z]{2}[A-Za-z0-9]{3}[0-9]{7}',ISRC),
               (r'[0-9]{1,2}:[0-9]{1,2}:[0-9]{1,2}',TIMESTAMP),
               (r'[0-9]+',NUMBER),
               (r'[\r\n]+',EOL),
               (r'".+?"',STRING),
               (r'\S+',STRING),
               (r'[ ]+',SPACE)]]

    TAGMATCH = re.compile(r'^[A-Z]+$')

    while (True):
        for (token,element) in TOKENS:
            t = token.search(cuedata)
            if (t is not None):
                cuedata = cuedata[len(t.group()):]
                if (element == SPACE):
                    break
                elif (element == NUMBER):
                    yield (int(t.group()),element,line_number)
                elif (element == EOL):
                    line_number += 1
                    yield (t.group(),element,line_number)
                elif (element == STRING):
                    if (TAGMATCH.match(t.group())):
                        yield (t.group(),TAG,line_number)
                    else:
                        yield (t.group().strip('"'),element,line_number)
                elif (element == TIMESTAMP):
                    (m,s,f) = map(int,t.group().split(":"))
                    yield (((m * 60 * 75) + (s * 75) + f),
                           element,line_number)
                else:
                    yield (t.group(),element,line_number)
                break
        else:
            break
                
    if (len(cuedata) > 0):
        raise ValueError("invalid token at char %d" % \
                             (full_length - len(cuedata)))

#tokens is the token iterator
#accept is an "or"ed list of all the tokens we'll accept
#error is the string to prepend to the error message
#returns the gotten value which matches
#or throws ValueError if it does not
def get_value(tokens, accept, error):
    (token,element,line_number) = tokens.next()
    if ((element & accept) != 0):
        return token
    else:
        raise ValueError("%s at line %d" % (error,line_number))

#takes an iterator of tokens
#parses the cuesheet lines (usually <TAG> <DATA> ... <EOL> formatted)
#returns a Cuesheet object?
def parse(tokens):
    def skip_to_eol(tokens):
        (token,element,line_number) = tokens.next()
        while (element != EOL):
            (token,element,line_number) = tokens.next()

    cuesheet = Cuesheet()
    track = None

    try:
        while (True):
            (token,element,line_number) = tokens.next()
            if (element == TAG):

                #ignore comment lines
                if (token == "REM"):
                    skip_to_eol(tokens)

                #we're moving to a new track
                elif (token == 'TRACK'):
                    if (track is not None):
                        cuesheet.tracks[track.number] = track
                    
                    track = Track(get_value(tokens,NUMBER,
                                            "invalid track number"),
                                  get_value(tokens,TAG | STRING,
                                            "invalid track type"))

                    get_value(tokens,EOL,"excess data")

                #if we haven't started on track data yet,
                #add attributes to the main cue sheet
                elif (track is None):
                    if (token in ('CATALOG','CDTEXTFILE',
                                  'PERFORMER','SONGWRITER',
                                  'TITLE')):
                        cuesheet.attribs[token] = get_value(
                            tokens,
                            STRING | TAG | NUMBER | ISRC,
                            "missing value")
                        
                        get_value(tokens,EOL,"excess data")

                    elif (token == 'FILE'):
                        filename = get_value(tokens,STRING,"missing filename")
                        filetype = get_value(tokens,STRING | TAG,
                                             "missing file type")
                        
                        cuesheet.attribs[token] = (filename,filetype)

                        get_value(tokens,EOL,"excess data")

                    else:
                        raise ValueError("invalid tag %s at line %d" % \
                                             (token,line_number))
                #otherwise, we're adding data to the current track
                else:
                    if (token in ('ISRC','PERFORMER',
                                  'SONGWRITER','TITLE')):
                        track.attribs[token] = get_value(
                            tokens,
                            STRING | TAG | NUMBER | ISRC,
                            "missing value")

                        get_value(tokens,EOL,"excess data")

                    elif (token == 'FLAGS'):
                        flags = []
                        s = get_value(tokens,STRING | TAG | EOL,
                                      "invalid flag")
                        while (('\n' not in s) and ('\r' not in s)):
                            flags.append(s)
                            s = get_value(tokens,STRING | TAG | EOL,
                                          "invalid flag")
                        track.attribs[token] = ",".join(flags)

                    elif (token in ('POSTGAP','PREGAP')):
                        track.attribs[token] = get_value(tokens,TIMESTAMP,
                                                         "invalid timestamp")
                        get_value(tokens,EOL,"excess data")

                    elif (token == 'INDEX'):
                        index_number = get_value(tokens,NUMBER,
                                                 "invalid index number")
                        index_timestamp = get_value(tokens,TIMESTAMP,
                                                    "invalid timestamp")
                        track.indexes[index_number] = index_timestamp
                        
                        get_value(tokens,EOL,"excess data")

                    elif (token in ('FILE',)):
                        skip_to_eol(tokens)

                    else:
                        raise ValueError("invalid tag %s at line %d" % \
                                             (token,line_number))
                    
            else:
                raise ValueError("missing tag at line %d" % (line_number))
    except StopIteration:
        if (track is not None):
            cuesheet.tracks[track.number] = track
        return cuesheet


class Cuesheet:
    def __init__(self):
        self.attribs = {}
        self.tracks = {}

    def __repr__(self):
        return "Cuesheet(attribs=%s,tracks=%s)" % \
            (repr(self.attribs),repr(self.tracks))

    #returns a list of PCM lengths for all audio tracks within the cuesheet
    def pcm_lengths(self):
        previous = None

        for key in sorted(self.tracks.keys()):
            current = self.tracks[key].indexes
            if (previous is None):
                previous = current
            else:
                yield (current[max(current.keys())] - \
                           previous[max(previous.keys())]) * (44100 / 75)
                previous = current

class Track:
    def __init__(self, number, type):
        self.number = number
        self.type = type
        self.attribs = {}
        self.indexes = {}

    def __repr__(self):
        return "Track(%s,%s,attribs=%s,indexes=%s)" % \
            (repr(self.number),repr(self.type),
             repr(self.attribs),repr(self.indexes))



:mod:`audiotools` --- the Base Python Audio Tools Module
========================================================

.. module:: audiotools
   :synopsis: the Base Python Audio Tools Module


The :mod:`audiotools` module contains a number of useful base
classes and functions upon which all of the other modules depend.

.. function:: open(filename)

   Opens the given filename string and returns an :class:`AudioFile`-compatible
   object.

AudioFile Objects
-----------------

.. class:: AudioFile()

   The :class:`AudioFile` class represents an audio file on disk,
   such as a FLAC file, MP3 file, WAVE file and so forth.
   It is not meant to be instatiated directly, but returned from functions
   such as :func:`open` which will return an :class:`AudioFile`-compatible
   object implementing the following methods and attributes.

.. classmethod:: AudioFile.is_type(file)

   Takes a file-like object with :meth:`read` and :meth:`seek` methods.
   Returns ``True`` if the file is determined to be of the same type
   as this particular :class:`AudioFile` implementation.
   Returns ``False`` if not.

.. method:: AudioFile.bits_per_sample()

   Returns the number of bits-per-sample in this audio file as a positive
   integer.

.. method:: AudioFile.channels()

   Returns the number of channels in this audio file as a positive integer.

.. method:: AudioFile.channel_mask()

   Returns a :class:`ChannelMask` object representing the channel assignment
   of this audio file.
   If the channel assignment is unknown or undefined, that :class:`ChannelMask`
   object may have an undefined value.

.. method:: AudioFile.sample_rate()

   Returns the sample rate of this audio file, in Hz, as a positive integer.

.. method:: AudioFile.total_frames()

   Returns the total number of PCM frames in this audio file,
   as a non-negative integer.

.. method:: AudioFile.cd_frames()

   Returns the total number of CD frames in this audio file,
   as a non-negative integer.
   Each CD frame is 1/75th of a second.

.. method:: AudioFile.lossless()

   Returns ``True`` if the data in the audio file has been stored losslessly.
   Returns ``False`` if not.

.. method:: AudioFile.set_metadata(metadata)

   Takes a :class:`MetaData`-compatible object and sets this audio file's
   metadata to that value, if possible.
   Raises :exc:`IOError` if a problem occurs when writing the file.

.. method:: AudioFile.get_metadata()

   Returns a :class:`MetaData`-compatible object representing this
   audio file's metadata, or ``None`` if this file contains no
   metadata.
   Raises :exc:`IOError` if a problem occurs when reading the file.

.. method:: AudioFile.delete_metadata()

   Deletes the audio file's metadata, removing or unsetting tags
   as necessary.
   Raises :exc:`IOError` if a problem occurs when writing the file.

.. method:: AudioFile.to_pcm()

   Returns this audio file's PCM data as a :class:`PCMReader`-compatible
   object.

.. classmethod:: AudioFile.from_pcm(filename, pcmreader[, compression=None])

   Takes a filename string, :class:`PCMReader`-compatible object
   and optional compression level string.
   Creates a new audio file as the same format as this audio class
   and returns a new :class:`AudioFile`-compatible object.
   Raises :exc:`EncodingError` if a problem occurs during encoding.

Transcoding an Audio File
^^^^^^^^^^^^^^^^^^^^^^^^^

In this example, we'll transcode ``track.flac`` to ``track.mp3``
at the default compression level:

   >>> audiotools.MP3Audio.from_pcm("track.mp3",
   ...                              audiotools.open("track.flac").to_pcm())

.. method:: AudioFile.to_wave(wave_filename)

   Takes a filename string and creates a new RIFF WAVE file
   at that location.
   Raises :exc:`EncodingError` if a problem occurs during encoding.

.. classmethod:: AudioFile.from_wave(filename, wave_filename[, compression=None])

   Takes a filename string of our new file, a wave_filename string of
   an existing RIFF WAVE file and an optional compression level string.
   Creates a new audio file as the same format as this audio class
   and returns a new :class:`AudioFile`-compatible object.
   Raises :exc:`EncodingError` if a problem occurs during encoding.

.. classmethod:: AudioFile.supports_foreign_riff_chunks()

   Returns ``True`` if this :class:`AudioFile` implementation supports storing
   non audio RIFF WAVE chunks.  Returns ``False`` if not.

.. method:: AudioFile.has_foreign_riff_chunks()

   Returns ``True`` if this audio file contains non audio RIFF WAVE chunks.
   Returns ``False`` if not.

.. method:: AudioFile.track_number()

   Returns this audio file's track number as a non-negative integer.
   This method first checks the file's metadata values.
   If unable to find one, it then tries to determine a track number
   from the track's filename.
   If that method is also unsuccessful, it returns 0.

.. method:: AudioFile.album_number()

   Returns this audio file's album number as a non-negative integer.
   This method first checks the file's metadata values.
   If unable to find one, it then tries to determine an album number
   from the track's filename.
   If that method is also unsuccessful, it returns 0.

.. classmethod:: AudioFile.track_name(track_number, track_metadata[, album_number = 0[, format = FORMAT_STRING]])

    Given a track number integer, :class:`MetaData`-compatible object
    (or ``None``) and optional album number integer and optional
    Python-formatted format string, returns a filename string with
    the format string fields filled-in.
    Raises :exc:`UnsupportedTracknameField` if the format string contains
    unsupported fields.

.. classmethod:: AudioFile.add_replay_gain(filenames)

   Given a list of filename strings of the same class as this
   :class:`AudioFile` class, calculates and adds ReplayGain metadata
   to those files.
   Raises :exc:`ValueError` if some problem occurs during ReplayGain
   calculation or application.

.. classmethod:: AudioFile.can_add_replay_gain()

   Returns ``True`` if this audio class supports ReplayGain
   and we have the necessary binaries to apply it.
   Returns ``False`` if not.

.. classmethod:: AudioFile.lossless_replay_gain()

   Returns ``True`` if this audio class applies ReplayGain via a
   lossless process - such as by adding a metadata tag of some sort.
   Returns ``False`` if applying metadata modifies the audio file
   data itself.

.. method:: AudioFile.replay_gain()

   Returns this audio file's ReplayGain values as a
   :class:`ReplayGain` object, or ``None`` if this audio file has no values.

.. method:: AudioFile.set_cuesheet(cuesheet)

   Takes a cuesheet-compatible object with :meth:`catalog`,
   :meth:`IRSCs`, :meth:`indexes` and :meth:`pcm_lengths` methods
   and sets this audio file's embedded cuesheet to those values, if possible.
   Raises :exc:`IOError` if this :class:`AudioFile` supports embedded
   cuesheets but some error occurred when writing the file.

.. method:: AudioFile.get_cuesheet()

   Returns a cuesheet-compatible object with :meth:`catalog`,
   :meth:`IRSCs`, :meth:`indexes` and :meth:`pcm_lengths` methods
   or ``None`` if no cuesheet is embedded.
   Raises :exc:`IOError` if some error occurs when reading the file.

.. classmethod:: AudioFile.has_binaries()

   Returns ``True`` if all the binaries necessary to implement
   this :class:`AudioFile`-compatible class are present and executable.
   Returns ``False`` if not.

MetaData Objects
----------------

.. class:: MetaData([track_name[, track_number[, track_total[, album_name[, artist_name[, performer_name[, composer_name[, conductor_name[, media[, ISRC[, catalog[, copyright[, publisher[, year[, data[, album_number[, album_total[, comment[, images]]]]]]]]]]]]]]]]]]])

   The :class:`MetaData` class represents an :class:`AudioFile`'s
   non-technical metadata.
   It can be instantiated directly for use by the :meth:`set_metadata`
   method.
   However, the :meth:`get_metadata` method will typically return
   :class:`MetaData`-compatible objects corresponding to the audio file's
   low-level metadata implementation rather than actual :class:`MetaData`
   objects.
   Modifying fields within a :class:`MetaData`-compatible object
   will modify its underlying representation and those changes
   will take effect should :meth:`set_metadata` be called with
   that updated object.

   The ``images`` argument, if given, should be an iterable collection
   of :class:`Image`-compatible objects.

.. data:: MetaData.track_name

   This individual track's name as a Unicode string.

.. data:: MetaData.track_number

   This track's number within the album as an integer.

.. data:: MetaData.track_total

   The total number of tracks on the album as an integer.

.. data:: MetaData.album_name

   The name of this track's album as a Unicode string.

.. data:: MetaData.artist_name

   The name of this track's original creator/composer as a Unicode string.

.. data:: MetaData.performer_name

   The name of this track's performing artist as a Unicode string.

.. data:: MetaData.composer_name

   The name of this track's composer as a Unicode string.

.. data:: MetaData.conductor_name

   The name of this track's conductor as a Unicode string.

.. data:: MetaData.media

   The album's media type, such as u"CD", u"tape", u"LP", etc.
   as a Unicode string.

.. data:: ISRC

   This track's ISRC value as a Unicode string.

.. data:: catalog

   This track's album catalog number as a Unicode string.

.. data:: year

   This track's album release year as a Unicode string.

.. data:: date

   This track's album recording date as a Unicode string.

.. data:: album_number

   This track's album number if it is one of a series of albums,
   as an integer.

.. data:: album_total

   The total number of albums within the set, as an integer.

.. data:: comment

   This track's comment as a Unicode string.

.. classmethod:: MetaData.converted(metadata)

   Takes a :class:`MetaData`-compatible object (or ``None``)
   and returns a new :class:`MetaData` object of the same class, or ``None``.
   For instance, ``VorbisComment.converted()`` returns ``VorbisComment``
   objects.
   The purpose of this classmethod is to offload metadata conversion
   to the metadata classes themselves.
   Therefore, by using the ``VorbisComment.converted()`` classmethod,
   the ``VorbisAudio`` class only needs to know how to handle
   ``VorbisComment`` metadata.

   Why not simply handle all metadata using this high-level representation
   and avoid conversion altogether?
   The reason is that :class:`MetaData` is often only a subset of
   what the low-level implementation can support.
   For example, a ``VorbisComment`` may contain the ``'FOO'`` tag
   which has no analogue in :class:`MetaData`'s list of fields.
   But when passed through the ``VorbisComment.converted()`` classmethod,
   that ``'FOO'`` tag will be preserved as one would expect.

   The key is that performing:

   >>> track.set_metadata(track.get_metadata())

   should always round-trip properly and not lose any metadata values.

.. classmethod:: MetaData.supports_images()

   Returns ``True`` if this :class:`MetaData` implementation supports images.
   Returns ``False`` if not.

.. method:: MetaData.images()

   Returns a list of :class:`Image`-compatible objects this metadata contains.

.. method:: MetaData.front_covers()

   Returns a subset of :meth:`images` which are marked as front covers.

.. method:: MetaData.back_covers()

   Returns a subset of :meth:`images` which are marked as back covers.

.. method:: MetaData.leaflet_pages()

   Returns a subset of :meth:`images` which are marked as leaflet pages.

.. method:: MetaData.media_images()

   Returns a subset of :meth:`images` which are marked as media.

.. method:: MetaData.other_images()

   Returns a subset of :meth:`images` which are marked as other.

.. method:: MetaData.add_image(image)

   Takes a :class:`Image`-compatible object and adds it to this
   metadata's list of images.

.. method:: MetaData.delete_image(image)

   Takes an :class:`Image` from this class, as returned by :meth:`images`,
   and removes it from this metadata's list of images.

.. method:: MetaData.merge(new_metadata)

   Updates this metadata by replacing empty fields with those
   from ``new_metadata``.  Non-empty fields are left as-is.


Meta Data Formats
=================

Although it's more convenient to manipulate the high-level
:class:`audiotools.MetaData` base class, one sometimes needs to be
able to view and modify the low-level implementation also.

ApeTag
------

.. class:: ApeTag(tags[, tag_length])

   This is an APEv2_ tag used by the WavPack, Monkey's Audio
   and Musepack formats, among others.
   During initialization, it takes a list of :class:`ApeTagItem` objects
   and an optional length integer (typically set only by
   :func:`get_metadata` methods which already know the tag's total length).
   It can then be manipulated like a regular Python dict
   with keys as strings and values as :class:`ApeTagItem` objects.
   Note that this is also a :class:`audiotools.MetaData` subclass
   with all of the same methods.

   For example:

   >>> tag = ApeTag([ApeTagItem(0,False,'Title',u'Track Title'.encode('utf-8'))])
   >>> tag.track_name
   u'Track Title'
   >>> tag['Title']
   ApeTagItem(0,False,'Title','Track Title')
   >>> tag['Title'] = ApeTagItem(0,False,'Title',u'New Title'.encode('utf-8'))
   >>> tag.track_name
   u'New Title'
   >>> tag.track_name = u'Yet Another Title'
   >>> tag['Title']
   ApeTagItem(0,False,'Title','Yet Another Title')

   The fields are mapped between :class:`ApeTag` and
   :class:`audiotools.MetaData` as follows:

   =============== ================================
   APEv2           Metadata
   --------------- --------------------------------
   ``Title``       ``track_name``
   ``Track``       ``track_number``/``track_total``
   ``Media``       ``album_number``/``album_total``
   ``Album``       ``album_name``
   ``Artist``      ``artist_name``
   ``Performer``   ``performer_name``
   ``Composer``    ``composer_name``
   ``Conductor``   ``conductor_name``
   ``ISRC``        ``ISRC``
   ``Catalog``     ``catalog``
   ``Copyright``   ``copyright``
   ``Publisher``   ``publisher``
   ``Year``        ``year``
   ``Record Date`` ``date``
   ``Comment``     ``comment``
   =============== ================================

   Note that ``Track`` and ``Media`` may be "/"-separated integer values
   where the first is the current number and the second is the total number.

   >>> tag = ApeTag([ApeTagItem(0,False,'Track',u'1'.encode('utf-8'))])
   >>> tag.track_number
   1
   >>> tag.track_total
   0
   >>> tag = ApeTag([ApeTagItem(0,False,'Track',u'2/3'.encode('utf-8'))])
   >>> tag.track_number
   2
   >>> tag.track_total
   3

.. classmethod:: ApeTag.read(file)

   Takes an open file object and returns an :class:`ApeTag` object
   of that file's APEv2 data, or ``None`` if the tag cannot be found.

.. method:: ApeTag.build()

   Returns this tag's complete APEv2 data as a string.

.. class:: ApeTagItem(item_type, read_only, key, data)

   This is the container for :class:``ApeTag`` data.
   ``item_type`` is an integer with one of the following values:

   = =============
   1 UTF-8 data
   2 binary data
   3 external data
   4 reserved
   = =============

   ``read_only`` is a boolean set to ``True`` if the tag-item is read-only.
   ``key`` is an ASCII string.
   ``data`` is a regular Python string (not unicode).

.. method:: ApeTagItem.build()

   Returns this tag item's data as a string.

.. classmethod:: ApeTagItem.binary(key, data)

   A convenience classmethod which takes strings of key and value data
   and returns a populated :class:`ApeTagItem` object of the
   appropriate type.

.. classmethod:: ApeTagItem.external(key, data)

   A convenience classmethod which takes strings of key and value data
   and returns a populated :class:`ApeTagItem` object of the
   appropriate type.

.. classmethod:: ApeTagItem.string(key, unicode)

   A convenience classmethod which takes a key string and value unicode
   and returns a populated :class:`ApeTagItem` object of the
   appropriate type.

.. .. class:: FlacMetaData

.. .. class:: ID3v1Comment

.. .. class:: ID3v22Comment

.. .. class:: ID3v23Comment

.. .. class:: ID3v24Comment

.. .. class:: ID3CommentPair

.. .. class:: M4AMetaData

Vorbis Comment
--------------

.. class:: VorbisComment(vorbis_data[, vendor_string])

   This is a VorbisComment_ tag used by FLAC, Ogg FLAC, Ogg Vorbis,
   Ogg Speex and other formats in the Ogg family.
   During initialization ``vorbis_data`` is a dictionary
   whose keys are unicode strings and whose values are lists
   of unicode strings - since each key in a Vorbis Comment may
   occur multiple times with different values.
   The optional ``vendor_string`` unicode string is typically
   handled by :func:`get_metadata` and :func:`set_metadata`
   methods, but it can also be accessed via the ``vendor_string`` attribute.
   Once initialized, :class:`VorbisComment` can be manipulated like a
   regular Python dict in addition to its standard
   :class:`audiotools.MetaData` methods.

   For example:

   >>> tag = VorbisComment({u'TITLE':[u'Track Title']})
   >>> tag.track_name
   u'Track Title'
   >>> tag[u'TITLE']
   [u'New Title']
   >>> tag[u'TITLE'] = [u'New Title']
   >>> tag.track_name
   u'New Title'

   Fields are mapped between :class:`VorbisComment` and
   :class:`audiotools.MetaData` as follows:

   ================= ==================
   VorbisComment     Metadata
   ----------------- ------------------
   ``TITLE``         ``track_name``
   ``TRACKNUMBER``   ``track_number``
   ``TRACKTOTAL``    ``track_total``
   ``DISCNUMBER``    ``album_number``
   ``DISCTOTAL``     ``album_total``
   ``ALBUM``         ``album_name``
   ``ARTIST``        ``artist_name``
   ``PERFORMER``     ``performer_name``
   ``COMPOSER``      ``composer_name``
   ``CONDUCTOR``     ``conductor_name``
   ``SOURCE MEDIUM`` ``media``
   ``ISRC``          ``ISRC``
   ``CATALOG``       ``catalog``
   ``COPYRIGHT``     ``copyright``
   ``PUBLISHER``     ``publisher``
   ``DATE``          ``year``
   ``COMMENT``       ``comment``
   ================= ==================

   Note that if the same key is used multiple times,
   the metadata attribute only indicates the first one:

   >>> tag = VorbisComment({u'TITLE':[u'Title1',u'Title2']})
   >>> tag.track_name
   u'Title1'


.. method:: build()

   Returns this object's complete Vorbis Comment data as a string.

.. _APEv2: http://wiki.hydrogenaudio.org/index.php?title=APEv2

.. _VorbisComment: http://www.xiph.org/vorbis/doc/v-comment.html

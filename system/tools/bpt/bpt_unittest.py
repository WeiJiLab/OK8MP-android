#!/usr/bin/python

# Copyright 2016, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


"""Unit-test for bpttool."""


import imp
import json
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
bpttool = imp.load_source('bpttool', './bpttool')


class FakeGuidGenerator(object):
  """A GUID generator that dispenses predictable GUIDs.

  This is used in place of the usual GUID generator in bpttool which
  is generating random GUIDs as per RFC 4122.
  """

  def dispense_guid(self, partition_number):
    """Dispenses a new GUID."""

    uuid = '01234567-89ab-cdef-0123-%012x' % partition_number
    return uuid

class PatternPartition(object):
  """A partition image file containing a predictable pattern.

  This holds file data about a partition image file for binary pattern.
  testing.
  """
  def __init__(self, char='', file=None, partition_name=None, obj=None):
    self.char = char
    self.file = file
    self.partition_name = partition_name
    self.obj = obj

class RoundToMultipleTest(unittest.TestCase):
  """Unit tests for the RoundToMultiple() function."""

  def testRoundUp(self):
    """Checks that we round up correctly."""
    self.assertEqual(bpttool.RoundToMultiple(100, 10), 100)
    self.assertEqual(bpttool.RoundToMultiple(189, 10), 190)
    self.assertEqual(bpttool.RoundToMultiple(190, 10), 190)
    self.assertEqual(bpttool.RoundToMultiple(191, 10), 200)
    self.assertEqual(bpttool.RoundToMultiple(199, 10), 200)
    self.assertEqual(bpttool.RoundToMultiple(200, 10), 200)
    self.assertEqual(bpttool.RoundToMultiple(201, 10), 210)
    self.assertEqual(bpttool.RoundToMultiple(-18, 10), -10)
    self.assertEqual(bpttool.RoundToMultiple(-19, 10), -10)
    self.assertEqual(bpttool.RoundToMultiple(-20, 10), -20)
    self.assertEqual(bpttool.RoundToMultiple(-21, 10), -20)

  def testRoundDown(self):
    """Checks that we round down correctly."""
    self.assertEqual(bpttool.RoundToMultiple(100, 10, True), 100)
    self.assertEqual(bpttool.RoundToMultiple(189, 10, True), 180)
    self.assertEqual(bpttool.RoundToMultiple(190, 10, True), 190)
    self.assertEqual(bpttool.RoundToMultiple(191, 10, True), 190)
    self.assertEqual(bpttool.RoundToMultiple(199, 10, True), 190)
    self.assertEqual(bpttool.RoundToMultiple(200, 10, True), 200)
    self.assertEqual(bpttool.RoundToMultiple(201, 10, True), 200)
    self.assertEqual(bpttool.RoundToMultiple(-18, 10, True), -20)
    self.assertEqual(bpttool.RoundToMultiple(-19, 10, True), -20)
    self.assertEqual(bpttool.RoundToMultiple(-20, 10, True), -20)
    self.assertEqual(bpttool.RoundToMultiple(-21, 10, True), -30)


class ParseSizeTest(unittest.TestCase):
  """Unit tests for the ParseSize() function."""

  def testIntegers(self):
    """Checks parsing of integers."""
    self.assertEqual(bpttool.ParseSize(123), 123)
    self.assertEqual(bpttool.ParseSize(17179869184), 1<<34)
    self.assertEqual(bpttool.ParseSize(0x1000), 4096)

  def testRawNumbers(self):
    """Checks parsing of raw numbers."""
    self.assertEqual(bpttool.ParseSize('123'), 123)
    self.assertEqual(bpttool.ParseSize('17179869184'), 1<<34)
    self.assertEqual(bpttool.ParseSize('0x1000'), 4096)

  def testDecimalUnits(self):
    """Checks that decimal size units are interpreted correctly."""
    self.assertEqual(bpttool.ParseSize('1 kB'), pow(10, 3))
    self.assertEqual(bpttool.ParseSize('1 MB'), pow(10, 6))
    self.assertEqual(bpttool.ParseSize('1 GB'), pow(10, 9))
    self.assertEqual(bpttool.ParseSize('1 TB'), pow(10, 12))
    self.assertEqual(bpttool.ParseSize('1 PB'), pow(10, 15))

  def testBinaryUnits(self):
    """Checks that binary size units are interpreted correctly."""
    self.assertEqual(bpttool.ParseSize('1 KiB'), pow(2, 10))
    self.assertEqual(bpttool.ParseSize('1 MiB'), pow(2, 20))
    self.assertEqual(bpttool.ParseSize('1 GiB'), pow(2, 30))
    self.assertEqual(bpttool.ParseSize('1 TiB'), pow(2, 40))
    self.assertEqual(bpttool.ParseSize('1 PiB'), pow(2, 50))

  def testFloatAndUnits(self):
    """Checks that floating point numbers can be used with units."""
    self.assertEqual(bpttool.ParseSize('0.5 kB'), 500)
    self.assertEqual(bpttool.ParseSize('0.5 KiB'), 512)
    self.assertEqual(bpttool.ParseSize('0.5 GB'), 500000000)
    self.assertEqual(bpttool.ParseSize('0.5 GiB'), 536870912)
    self.assertEqual(bpttool.ParseSize('0.1 MiB'), 104858)

class MakeDiskImageTest(unittest.TestCase):
  """Unit tests for 'bpttool make_disk_image'."""

  def setUp(self):
    """Set-up method."""
    self.bpt = bpttool.Bpt()

  def _BinaryPattern(self, bpt_file_name, partition_patterns):
    """Checks that a binary pattern may be written to a specified partition.

    This checks individual partion image writes to portions of a disk.  Known
    patterns are written into certain partitions and are verified after each
    pattern has been written to.

    Arguments:
      bpt_file_name: File name of bpt JSON containing partition information.
      partition_patterns: List of tuples with each tuple having partition name
                          as the first argument, and character pattern as the
                          second argument.

    """
    bpt_file = open(bpt_file_name, 'r')
    partitions_string, _ = self.bpt.make_table([bpt_file])
    bpt_tmp = tempfile.NamedTemporaryFile()
    bpt_tmp.write(partitions_string)
    bpt_tmp.seek(0)
    partitions, _ = self.bpt._read_json([bpt_tmp])

    # Declare list of partition images to be written and compared on disk.
    pattern_images = [PatternPartition(
                      char=pp[1],
                      file=tempfile.NamedTemporaryFile(),
                      partition_name=pp[0])
                      for pp in partition_patterns]

    # Store partition object and write a known character pattern image.
    for pi in pattern_images:
      pi.obj = [p for p in partitions if str(p.label) == pi.partition_name][0]
      pi.file.write(bytearray(pi.char * int(pi.obj.size)))

    # Create the disk containing the partition filled with a known character
    # pattern, seek to it's position and compare it to the supposed pattern.
    with tempfile.NamedTemporaryFile() as generated_disk_image:
      bpt_tmp.seek(0)
      self.bpt.make_disk_image(generated_disk_image,
                               bpt_tmp,
                               [p.partition_name + ':' + p.file.name
                                for p in pattern_images])

      for pi in pattern_images:
        generated_disk_image.seek(pi.obj.offset)
        pi.file.seek(0)

        self.assertEqual(generated_disk_image.read(pi.obj.size),
                    pi.file.read())
        pi.file.close()

    bpt_file.close()
    bpt_tmp.close()

  def _LargeBinary(self, bpt_file_name):
    """Helper function to write large partition images to disk images.

    This is a simple call to make_disk_image, passing a large in an image
    which exceeds the it's size as specfied in the bpt file.

    Arguments:
      bpt_file_name: File name of bpt JSON containing partition information.

    """
    with open(bpt_file_name, 'r') as bpt_file, \
         tempfile.NamedTemporaryFile() as bpt_tmp, \
         tempfile.NamedTemporaryFile() as generated_disk_image, \
         tempfile.NamedTemporaryFile() as large_partition_image:
        partitions_string, _ = self.bpt.make_table([bpt_file])
        bpt_tmp.write(partitions_string)
        bpt_tmp.seek(0)
        partitions, _ = self.bpt._read_json([bpt_tmp])

        # Create the over-sized partition image.
        large_partition_image.write(bytearray('0' *
          int(1.1*partitions[0].size + 1)))

        bpt_tmp.seek(0)

        # Expect exception here.
        self.bpt.make_disk_image(generated_disk_image, bpt_tmp,
          [p.label + ':' + large_partition_image.name for p in partitions])

  def testBinaryPattern(self):
    """Checks patterns written to partitions on disk images."""
    self._BinaryPattern('test/pattern_partition_single.bpt', [('charlie', 'c')])
    self._BinaryPattern('test/pattern_partition_multi.bpt', [('alpha', 'a'),
                        ('beta', 'b')])

  def testExceedPartitionSize(self):
    """Checks that exceedingly large partition images are not accepted."""
    try:
      self._LargeBinary('test/pattern_partition_exceed_size.bpt')
    except bpttool.BptError as e:
      assert 'exceeds the partition size' in e.message

  def testSparseImage(self):
    """Checks that sparse input is unsparsified."""
    bpt_file = open('test/test_sparse_image.bpt', 'r')
    bpt_json, _ = self.bpt.make_table([bpt_file])
    bpt_json_file = tempfile.NamedTemporaryFile()
    bpt_json_file.write(bpt_json)
    bpt_json_file.seek(0)
    partitions, _ = self.bpt._read_json([bpt_json_file])

    # Generate a disk image where one of the inputs is a sparse disk
    # image. See below for details about test/test_file.bin and
    # test/test_file.bin.sparse.
    generated_disk_image = tempfile.NamedTemporaryFile()
    bpt_json_file.seek(0)
    self.bpt.make_disk_image(generated_disk_image,
                             bpt_json_file,
                             ['sparse_data:test/test_file.bin.sparse'])

    # Get offset and size of the generated partition.
    part = json.loads(bpt_json)['partitions'][0]
    part_offset = int(part['offset'])
    part_size = int(part['size'])

    # Load the unsparsed data.
    unsparse_file = open('test/test_file.bin', 'r')
    unsparse_data = unsparse_file.read()
    unsparse_size = unsparse_file.tell()

    # Check that the unsparse image doesn't take up all the space.
    self.assertLess(unsparse_size, part_size)

    # Check that the sparse image was unsparsified correctly.
    generated_disk_image.seek(part_offset)
    disk_image_data = generated_disk_image.read(unsparse_size)
    self.assertItemsEqual(disk_image_data, unsparse_data)

    # Check that the remainder of the partition has zeroes.
    trailing_size = part_size - unsparse_size
    trailing_data = generated_disk_image.read(trailing_size)
    self.assertItemsEqual(trailing_data, '\0'*trailing_size)


class MakeTableTest(unittest.TestCase):
  """Unit tests for 'bpttool make_table'."""

  def setUp(self):
    """Reset GUID generator for every test."""
    self.fake_guid_generator = FakeGuidGenerator()

  def _MakeTable(self, input_file_names,
                 expected_json_file_name,
                 expected_gpt_file_name=None,
                 partitions_offset_begin=None,
                 disk_size=None,
                 disk_alignment=None,
                 disk_guid=None,
                 ab_suffixes=None):
    """Helper function to create partition tables.

    This is a simple wrapper for Bpt.make_table() that compares its
    output with an expected output when given a set of inputs.

    Arguments:
      input_file_names: A list of .bpt files to process.
      expected_json_file_name: File name of the file with expected JSON output.
      expected_gpt_file_name: File name of the file with expected binary
                              output or None.
      partitions_offset_begin: if not None, the size of the disk
                               partitions offset begin to use.
      disk_size: if not None, the size of the disk to use.
      disk_alignment: if not None, the disk alignment to use.
      disk_guid: if not None, the disk GUID to use.
      ab_suffixes: if not None, a list of the A/B suffixes (as a
                   comma-separated string) to use.

    """

    inputs = []
    for file_name in input_file_names:
      inputs.append(open(file_name))

    bpt = bpttool.Bpt()
    (json_str, gpt_bin) = bpt.make_table(
        inputs,
        partitions_offset_begin=partitions_offset_begin,
        disk_size=disk_size,
        disk_alignment=disk_alignment,
        disk_guid=disk_guid,
        ab_suffixes=ab_suffixes,
        guid_generator=self.fake_guid_generator)
    self.assertEqual(json_str, open(expected_json_file_name).read())

    # Check that bpttool generates same JSON if being fed output it
    # just generated without passing any options (e.g. disk size,
    # alignment, suffixes). This verifies that we include all
    # necessary information in the generated JSON.
    (json_str2, _) = bpt.make_table(
        [open(expected_json_file_name, 'r')],
        guid_generator=self.fake_guid_generator)
    self.assertEqual(json_str, json_str2)

    if expected_gpt_file_name:
      self.assertEqual(gpt_bin, open(expected_gpt_file_name).read())

  def testBase(self):
    """Checks that the basic layout is as expected."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_base.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testSize(self):
    """Checks that disk size can be changed on the command-line."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_size.bpt',
                    disk_size=bpttool.ParseSize('20 GiB'))

  def testAlignment(self):
    """Checks that disk alignment can be changed on the command-line."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_alignment.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'),
                    disk_alignment=1048576)

  def testPartitionsOffsetBegin(self):
    """Checks that disk partitions offset begin
       can be changed on the command-line."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_partitions_offset_begin.bpt',
                    partitions_offset_begin=bpttool.ParseSize('1 MiB'),
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testDiskGuid(self):
    """Checks that disk GUID can be changed on the command-line."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_disk_guid.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'),
                    disk_guid='01234567-89ab-cdef-0123-00000000002a')

  def testPersist(self):
    """Checks that persist flags are generated"""
    self._MakeTable(['test/persist.bpt'],
                    'test/expected_json_persist.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testSuffixes(self):
    """Checks that A/B-suffixes can be changed on the command-line."""
    self._MakeTable(['test/base.bpt'],
                    'test/expected_json_suffixes.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'),
                    ab_suffixes='-A,-B')

  def testStackedIgnore(self):
    """Checks that partitions can be ignored through stacking."""
    self._MakeTable(['test/base.bpt', 'test/ignore_userdata.bpt'],
                    'test/expected_json_stacked_ignore.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testStackedSize(self):
    """Checks that partition size can be changed through stacking."""
    self._MakeTable(['test/base.bpt', 'test/change_odm_size.bpt'],
                    'test/expected_json_stacked_size.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testStackedSettings(self):
    """Checks that settings can be changed through stacking."""
    self._MakeTable(['test/base.bpt', 'test/override_settings.bpt'],
                    'test/expected_json_stacked_override_settings.bpt')

  def testStackedNewPartition(self):
    """Checks that a new partition can be added through stacking."""
    self._MakeTable(['test/base.bpt', 'test/new_partition.bpt'],
                    'test/expected_json_stacked_new_partition.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testStackedChangeFlags(self):
    """Checks that we can change partition flags through stacking."""
    self._MakeTable(['test/base.bpt', 'test/change_userdata_flags.bpt'],
                    'test/expected_json_stacked_change_flags.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testStackedDisableAB(self):
    """Checks that we can change disable A/B on partitions through stacking."""
    self._MakeTable(['test/base.bpt', 'test/disable_ab.bpt'],
                    'test/expected_json_stacked_disable_ab.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testStackedNewPartitionOnTop(self):
    """Checks that we can add a new partition only given the output JSON.

    This also tests that 'userdata' is shrunk (it has a size in the
    input file 'expected_json_base.bpt') to make room for the new
    partition. This works because the 'grow' attribute is preserved.
    """
    self._MakeTable(['test/expected_json_base.bpt',
                     'test/new_partition_on_top.bpt'],
                    'test/expected_json_stacked_new_partition_on_top.bpt')

  def testStackedSizeAB(self):
    """Checks that AB partition size can be changed given output JSON.

    This verifies that we un-expand A/B partitions when importing
    output JSON. E.g. the output JSON has system_a, system_b which is
    rewritten to just system as part of the import.
    """
    self._MakeTable(['test/expected_json_base.bpt',
                     'test/change_system_size.bpt'],
                    'test/expected_json_stacked_change_ab_size.bpt')

  def testPositionAttribute(self):
    """Checks that it's possible to influence partition order."""
    self._MakeTable(['test/base.bpt',
                     'test/positions.bpt'],
                    'test/expected_json_stacked_positions.bpt',
                    disk_size=bpttool.ParseSize('10 GiB'))

  def testBinaryOutput(self):
    """Checks binary partition table output.

    This verifies that we generate the binary partition tables
    correctly.
    """
    self._MakeTable(['test/expected_json_stacked_change_flags.bpt'],
                    'test/expected_json_stacked_change_flags.bpt',
                    'test/expected_json_stacked_change_flags.bin')

  def testFileWithSyntaxErrors(self):
    """Check that we catch errors in JSON files in a structured way."""
    try:
      self._MakeTable(['test/file_with_syntax_errors.bpt'],
                      'file_with_syntax_errors.bpt')
    except bpttool.BptParsingError as e:
      self.assertEqual(e.filename, 'test/file_with_syntax_errors.bpt')


class QueryPartitionTest(unittest.TestCase):
  """Unit tests for 'bpttool query_partition'."""

  def setUp(self):
    """Set-up method."""
    self.bpt = bpttool.Bpt()

  def testQuerySize(self):
    """Checks query for size."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'userdata', 'size', False),
        '7449042944')

  def testQueryOffset(self):
    """Checks query for offset."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'userdata', 'offset', False),
        '3288354816')

  def testQueryGuid(self):
    """Checks query for GUID."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'userdata', 'guid', False),
        '01234567-89ab-cdef-0123-000000000007')

  def testQueryTypeGuid(self):
    """Checks query for type GUID."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'userdata', 'type_guid', False),
        '0bb7e6ed-4424-49c0-9372-7fbab465ab4c')

  def testQueryFlags(self):
    """Checks query for flags."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'userdata', 'flags', False),
        '0x0420000000000000')

  def testQueryPersistTrue(self):
    """Checks query for persist."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/persist.bpt'),
            'true_persist', 'persist', False), 'True')

  def testQueryPersistFalse(self):
    """Checks query for persist."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/persist.bpt'),
            'false_persist', 'persist', False), 'False')

  def testQuerySizeCollapse(self):
    """Checks query for size when collapsing A/B partitions."""
    self.assertEqual(
        self.bpt.query_partition(
            open('test/expected_json_stacked_change_flags.bpt'),
            'odm', 'size', True),
        '1073741824')


# The file test_file.bin and test_file.bin.sparse are generated using
# the following python code:
#
#  with open('test_file.bin', 'w+b') as f:
#    f.write('Barfoo43'*128*12)
#  os.system('img2simg test_file.bin test_file.bin.sparse')
#  image = bpttool.ImageHandler('test_file.bin.sparse')
#  image.append_dont_care(12*1024)
#  image.append_fill('\x01\x02\x03\x04', 12*1024)
#  image.append_raw('Foobar42'*128*12)
#  image.append_dont_care(12*1024)
#  del image
#  os.system('rm -f test_file.bin')
#  os.system('simg2img test_file.bin.sparse test_file.bin')
#
# and manually verified to be correct. The content of the raw and
# sparse files are as follows (the line with "Fill with 0x04030201" is
# a simg_dump.py bug):
#
# $ hexdump -C test_file.bin
# 00000000  42 61 72 66 6f 6f 34 33  42 61 72 66 6f 6f 34 33  |Barfoo43Barfoo43|
# *
# 00003000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
# *
# 00006000  01 02 03 04 01 02 03 04  01 02 03 04 01 02 03 04  |................|
# *
# 00009000  46 6f 6f 62 61 72 34 32  46 6f 6f 62 61 72 34 32  |Foobar42Foobar42|
# *
# 0000c000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
# *
# 0000f000
#
# $ system/core/libsparse/simg_dump.py -v test_file.bin.sparse
# test_file.bin.sparse: Total of 15 4096-byte output blocks in 5 input chunks.
#             input_bytes      output_blocks
# chunk    offset     number  offset  number
#    1         40      12288       0       3 Raw data
#    2      12340          0       3       3 Don't care
#    3      12352          4       6       3 Fill with 0x04030201
#    4      12368      12288       9       3 Raw data
#    5      24668          0      12       3 Don't care
#           24668                 15         End
#
class ImageHandler(unittest.TestCase):

  TEST_FILE_SPARSE_PATH = 'test/test_file.bin.sparse'
  TEST_FILE_PATH = 'test/test_file.bin'
  TEST_FILE_SIZE = 61440
  TEST_FILE_BLOCK_SIZE = 4096

  def _file_contents_equal(self, path1, path2, size):
    f1 = open(path1, 'r')
    f2 = open(path2, 'r')
    if f1.read(size) != f2.read(size):
      return False
    return True

  def _file_size(self, f):
    old_pos = f.tell()
    f.seek(0, os.SEEK_END)
    size = f.tell()
    f.seek(old_pos)
    return size

  def _clone_sparse_file(self):
    f = tempfile.NamedTemporaryFile()
    f.write(open(self.TEST_FILE_SPARSE_PATH).read())
    f.flush()
    return f

  def _unsparsify(self, path):
    f = tempfile.NamedTemporaryFile()
    os.system('simg2img {} {}'.format(path, f.name))
    return f

  def testRead(self):
    """Checks that reading from a sparse file works as intended."""
    ih = bpttool.ImageHandler(self.TEST_FILE_SPARSE_PATH)

    # Check that we start at offset 0.
    self.assertEqual(ih.tell(), 0)

    # Check that reading advances the cursor.
    self.assertEqual(ih.read(14), bytearray('Barfoo43Barfoo'))
    self.assertEqual(ih.tell(), 14)
    self.assertEqual(ih.read(2), bytearray('43'))
    self.assertEqual(ih.tell(), 16)

    # Check reading in the middle of a fill chunk gets the right data.
    ih.seek(0x6000 + 1)
    self.assertEqual(ih.read(4), bytearray('\x02\x03\x04\x01'))

    # Check we can cross the chunk boundary correctly.
    ih.seek(0x3000 - 10)
    self.assertEqual(ih.read(12), bytearray('43Barfoo43\x00\x00'))
    ih.seek(0x9000 - 3)
    self.assertEqual(ih.read(5), bytearray('\x02\x03\x04Fo'))

    # Check reading at end of file is a partial read.
    ih.seek(0xf000 - 2)
    self.assertEqual(ih.read(16), bytearray('\x00\x00'))

  def testTruncate(self):
    """Checks that we can truncate a sparse file correctly."""
    # Check truncation at all possible boundaries (including start and end).
    for size in range(0, self.TEST_FILE_SIZE + self.TEST_FILE_BLOCK_SIZE,
                      self.TEST_FILE_BLOCK_SIZE):
      sparse_file = self._clone_sparse_file()
      ih = bpttool.ImageHandler(sparse_file.name)
      ih.truncate(size)
      unsparse_file = self._unsparsify(sparse_file.name)
      self.assertEqual(self._file_size(unsparse_file), size)
      self.assertTrue(self._file_contents_equal(unsparse_file.name,
                                                self.TEST_FILE_PATH,
                                                size))

    # Check truncation to grow the file.
    grow_size = 8192
    sparse_file = self._clone_sparse_file()
    ih = bpttool.ImageHandler(sparse_file.name)
    ih.truncate(self.TEST_FILE_SIZE + grow_size)
    unsparse_file = self._unsparsify(sparse_file.name)
    self.assertEqual(self._file_size(unsparse_file),
                     self.TEST_FILE_SIZE + grow_size)
    self.assertTrue(self._file_contents_equal(unsparse_file.name,
                                              self.TEST_FILE_PATH,
                                              self.TEST_FILE_SIZE))
    unsparse_file.seek(self.TEST_FILE_SIZE)
    self.assertEqual(unsparse_file.read(), '\0'*grow_size)

  def testAppendRaw(self):
    """Checks that we can append raw data correctly."""
    sparse_file = self._clone_sparse_file()
    ih = bpttool.ImageHandler(sparse_file.name)
    data = 'SomeData'*4096
    ih.append_raw(data)
    unsparse_file = self._unsparsify(sparse_file.name)
    self.assertTrue(self._file_contents_equal(unsparse_file.name,
                                              self.TEST_FILE_PATH,
                                              self.TEST_FILE_SIZE))
    unsparse_file.seek(self.TEST_FILE_SIZE)
    self.assertEqual(unsparse_file.read(), data)

  def testAppendFill(self):
    """Checks that we can append fill data correctly."""
    sparse_file = self._clone_sparse_file()
    ih = bpttool.ImageHandler(sparse_file.name)
    data = 'ABCD'*4096
    ih.append_fill('ABCD', len(data))
    unsparse_file = self._unsparsify(sparse_file.name)
    self.assertTrue(self._file_contents_equal(unsparse_file.name,
                                              self.TEST_FILE_PATH,
                                              self.TEST_FILE_SIZE))
    unsparse_file.seek(self.TEST_FILE_SIZE)
    self.assertEqual(unsparse_file.read(), data)

  def testDontCare(self):
    """Checks that we can append DONT_CARE data correctly."""
    sparse_file = self._clone_sparse_file()
    ih = bpttool.ImageHandler(sparse_file.name)
    data = '\0'*40960
    ih.append_dont_care(len(data))
    unsparse_file = self._unsparsify(sparse_file.name)
    self.assertTrue(self._file_contents_equal(unsparse_file.name,
                                              self.TEST_FILE_PATH,
                                              self.TEST_FILE_SIZE))
    unsparse_file.seek(self.TEST_FILE_SIZE)
    self.assertEqual(unsparse_file.read(), data)


if __name__ == '__main__':
  unittest.main()

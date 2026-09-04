"""Independent FAT reader: verify the shipped FreeDOS tree survived migration."""
import argparse
import hashlib
import math
import struct
import zipfile
from pathlib import Path


def word(data, at):
    return struct.unpack_from('<H', data, at)[0]


def dword(data, at):
    return struct.unpack_from('<I', data, at)[0]


class Volume:
    def __init__(self, image, bits):
        self.image, self.bits = image, bits
        assert word(image, 11) == 512 and image[510:512] == b'\x55\xaa'
        self.cluster_bytes = image[13] * 512
        self.fat_start = word(image, 14) * 512
        self.fat_bytes = word(image, 22) * 512
        self.root_start = self.fat_start + image[16] * self.fat_bytes
        self.root_bytes = word(image, 17) * 32
        self.data_start = self.root_start + math.ceil(self.root_bytes / 512) * 512
        self.cluster_count = (len(image) - self.data_start) // self.cluster_bytes
        self.eoc = 0xff8 if bits == 12 else 0xfff8
        first_fat = image[self.fat_start:self.fat_start + self.fat_bytes]
        for copy in range(1, image[16]):
            start = self.fat_start + copy * self.fat_bytes
            assert image[start:start + self.fat_bytes] == first_fat, 'FAT copies differ'
        self.owners = {}

    def link(self, cluster):
        if self.bits == 16:
            return word(self.image, self.fat_start + cluster * 2)
        packed = word(self.image, self.fat_start + cluster * 3 // 2)
        return (packed >> (4 if cluster & 1 else 0)) & 0xfff

    def chain(self, first, owner):
        result = bytearray()
        while first and first < self.eoc:
            assert 2 <= first < self.cluster_count + 2, f'Invalid cluster: {owner}'
            assert first not in self.owners, f'Cross-linked/cyclic cluster: {owner}'
            self.owners[first] = owner
            start = self.data_start + (first - 2) * self.cluster_bytes
            result.extend(self.image[start:start + self.cluster_bytes])
            first = self.link(first)
            assert first >= 2, f'Unterminated FAT chain: {owner}'
        return bytes(result)

    def tree(self):
        files, directories = {}, set()

        def visit(raw, parent, cluster=0, parent_cluster=0):
            if cluster:
                assert raw[:11] == b'.          ' and word(raw, 26) == cluster
                assert raw[32:43] == b'..         ' and word(raw, 58) == parent_cluster
            for at in range(0, len(raw), 32):
                entry = raw[at:at + 32]
                if entry[0] == 0:
                    break
                if entry[0] == 0xe5 or entry[11] == 0x0f or entry[11] & 8:
                    continue
                if self.bits == 16:
                    # FreeCOM copies this timestamp when closing a D: file.
                    # Zero month/day is rejected by the real SdFat adapter.
                    date = word(entry, 24)
                    assert 1 <= (date >> 5) & 15 <= 12 and 1 <= date & 31 <= 31, 'Invalid FAT modification date'
                name = entry[:8].decode('ascii').rstrip()
                suffix = entry[8:11].decode('ascii').rstrip()
                if name in ('.', '..'):
                    continue
                if suffix:
                    name += '.' + suffix
                name = parent + '/' + name if parent else name
                assert name not in files and name not in directories, f'Duplicate {name}'
                first = word(entry, 26)
                data = self.chain(first, name)
                if entry[11] & 16:
                    directories.add(name)
                    visit(data, name, first, cluster)
                else:
                    size = dword(entry, 28)
                    assert len(data) == math.ceil(size / self.cluster_bytes) * self.cluster_bytes, f'Wrong chain length: {name}'
                    files[name] = data[:size]

        visit(self.image[self.root_start:self.root_start + self.root_bytes], '')
        allocated = {c for c in range(2, self.cluster_count + 2) if self.link(c)}
        assert allocated == set(self.owners), 'Orphaned allocated FAT clusters'
        return files, directories


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-zip', required=True, type=Path)
    parser.add_argument('--image', required=True, type=Path)
    args = parser.parse_args()
    assert hashlib.sha256(args.source_zip.read_bytes()).hexdigest() == 'ae0a074f3688da1d247b946575142164a47b00d4d8cce51aa7bf8c1aaa9a55c6'
    with zipfile.ZipFile(args.source_zip) as archive:
        original = Volume(archive.read('144m/x86BOOT.img'), 12)
    source_files, source_dirs = original.tree()
    disk = args.image.read_bytes()
    assert len(disk) == 20 * 1024 * 1024 and disk[510:512] == b'\x55\xaa'
    assert disk[446] == 0x80 and disk[450] == 4
    start, sectors = dword(disk, 454), dword(disk, 458)
    assert start == 63 and sectors == 40887 and len(disk) // 512 - start - sectors == 10
    volume = Volume(disk[start * 512:(start + sectors) * 512], 16)
    assert word(volume.image, 24) == 63 and word(volume.image, 26) == 1
    assert dword(volume.image, 28) == start and word(volume.image, 19) == sectors
    files, directories = volume.tree()
    assert source_dirs <= directories, 'Source directory omitted during migration'
    replaced = {'AUTOEXEC.BAT', 'CONFIG.SYS', 'FDCONFIG.SYS', 'COMMAND.COM', 'README.TXT'}
    checked = 0
    for name, data in source_files.items():
        if name not in replaced:
            assert name in files and files[name] == data, f'FreeDOS migration changed/omitted {name}'
            checked += 1
    for name in ('FREEDOS/BIN/MEM.EXE', 'FREEDOS/BIN/XCOPY.EXE'):
        assert files[name] == source_files[name]
        print(name, len(files[name]), hashlib.sha256(files[name]).hexdigest())
    pinned = {
        'FREEDOS/BIN/EDIT.EXE': 'e972ca9f5b25e97e2959057809a1f640123649c3da76971ec829ced6cbbe1ced',
        'FREEDOS/BIN/EDIT.HLP': '9c90eac60b8065d1d12f13af679b7895512eb76d3007e107e755f68f5b9d2265',
    }
    for name, expected in pinned.items():
        actual = hashlib.sha256(files[name]).hexdigest()
        assert actual == expected, f'Wrong official FreeDOS Edit payload: {name}'
        print(name, len(files[name]), actual)
    assert b'DOSDIR >NUL\r\n' in files['AUTOEXEC.BAT'] and files.get('DOSDIR.COM')
    assert b'PROMPT $p$g\r\n' in files['AUTOEXEC.BAT']
    assert b'EDIT filename.txt' in files['README.TXT']
    assert b'VM proof' not in files['AUTOEXEC.BAT']
    assert b'CGA80\r\nECHO Mean Hamster BIOS (C) 2026\r\n' in files['AUTOEXEC.BAT']
    assert b'ECHO Booting drive C:\r\nDOSDIR >NUL\r\nPROMPT $p$g\r\n' in files['AUTOEXEC.BAT']
    print(f'PASS 20MiB FAT16 geometry, identical FAT copies, unique complete chains, directory backlinks, valid dates; {checked} original FreeDOS files preserved')


if __name__ == '__main__':
    main()

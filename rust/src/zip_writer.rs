//! Pure Rust ZIP archive packer (Store method, compression 0) compliant with ZIP standard.

pub struct ZipEntry {
    pub name: String,
    pub data: Vec<u8>,
}

pub struct ZipArchive {
    entries: Vec<ZipEntry>,
}

impl ZipArchive {
    pub fn new() -> Self {
        Self { entries: Vec::new() }
    }

    pub fn add_file(&mut self, name: &str, data: Vec<u8>) {
        self.entries.push(ZipEntry {
            name: name.to_string(),
            data,
        });
    }

    pub fn add_str(&mut self, name: &str, text: &str) {
        self.add_file(name, text.as_bytes().to_vec());
    }

    fn crc32(data: &[u8]) -> u32 {
        let mut crc = 0xFFFF_FFFFu32;
        for &byte in data {
            crc ^= byte as u32;
            for _ in 0..8 {
                if (crc & 1) != 0 {
                    crc = (crc >> 1) ^ 0xEDB8_8320;
                } else {
                    crc >>= 1;
                }
            }
        }
        !crc
    }

    pub fn finish(&self) -> Vec<u8> {
        let mut buf = Vec::new();
        let mut cd_buf = Vec::new();

        for entry in &self.entries {
            let name_bytes = entry.name.as_bytes();
            let crc = Self::crc32(&entry.data);
            let size = entry.data.len() as u32;
            let offset = buf.len() as u32;

            // Local file header: PK\x03\x04
            buf.extend_from_slice(b"PK\x03\x04");
            buf.extend_from_slice(&20u16.to_le_bytes()); // version needed: 2.0
            buf.extend_from_slice(&0u16.to_le_bytes());  // flags
            buf.extend_from_slice(&0u16.to_le_bytes());  // compression: 0 (Store)
            buf.extend_from_slice(&0u16.to_le_bytes());  // time
            buf.extend_from_slice(&0u16.to_le_bytes());  // date
            buf.extend_from_slice(&crc.to_le_bytes());
            buf.extend_from_slice(&size.to_le_bytes());  // compressed size
            buf.extend_from_slice(&size.to_le_bytes());  // uncompressed size
            buf.extend_from_slice(&(name_bytes.len() as u16).to_le_bytes());
            buf.extend_from_slice(&0u16.to_le_bytes());  // extra field length
            buf.extend_from_slice(name_bytes);
            buf.extend_from_slice(&entry.data);

            // Central directory file header: PK\x01\x02
            cd_buf.extend_from_slice(b"PK\x01\x02");
            cd_buf.extend_from_slice(&20u16.to_le_bytes()); // version made by
            cd_buf.extend_from_slice(&20u16.to_le_bytes()); // version needed
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // flags
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // compression
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // time
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // date
            cd_buf.extend_from_slice(&crc.to_le_bytes());
            cd_buf.extend_from_slice(&size.to_le_bytes());
            cd_buf.extend_from_slice(&size.to_le_bytes());
            cd_buf.extend_from_slice(&(name_bytes.len() as u16).to_le_bytes());
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // extra field len
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // comment len
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // disk start
            cd_buf.extend_from_slice(&0u16.to_le_bytes());  // internal attrs
            cd_buf.extend_from_slice(&0u32.to_le_bytes());  // external attrs
            cd_buf.extend_from_slice(&offset.to_le_bytes()); // local header offset
            cd_buf.extend_from_slice(name_bytes);
        }

        let cd_offset = buf.len() as u32;
        let cd_len = cd_buf.len() as u32;
        buf.extend_from_slice(&cd_buf);

        // End of central directory record: PK\x05\x06
        buf.extend_from_slice(b"PK\x05\x06");
        buf.extend_from_slice(&0u16.to_le_bytes()); // disk number
        buf.extend_from_slice(&0u16.to_le_bytes()); // disk with CD
        let entry_count = self.entries.len() as u16;
        buf.extend_from_slice(&entry_count.to_le_bytes()); // total entries this disk
        buf.extend_from_slice(&entry_count.to_le_bytes()); // total entries
        buf.extend_from_slice(&cd_len.to_le_bytes());
        buf.extend_from_slice(&cd_offset.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes()); // comment length

        buf
    }
}

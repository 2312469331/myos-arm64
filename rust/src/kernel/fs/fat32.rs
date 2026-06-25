use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;
use core::cell::UnsafeCell;
use crate::kernel::fs::vfs::{self, BlockDevice, Inode, FileSystem, FileMeta, DirEntry};

#[repr(C, packed)]
struct Bpb {
    bs_jmp_boot:      [u8; 3],
    bs_oem_name:      [u8; 8],
    bpb_byts_per_sec: u16,
    bpb_sec_per_clus: u8,
    bpb_rsvd_sec_cnt: u16,
    bpb_num_fats:     u8,
    bpb_root_ent_cnt: u16,
    bpb_tot_sec16:    u16,
    bpb_media:        u8,
    bpb_fat_sz16:     u16,
    bpb_sec_per_trk:  u16,
    bpb_num_heads:    u16,
    bpb_hidd_sec:     u32,
    bpb_tot_sec32:    u32,
    bpb_fat_sz32:     u32,
    bpb_ext_flags:    u16,
    bpb_fs_ver:       u16,
    bpb_root_clus:    u32,
    bpb_fs_info:      u16,
    bpb_bk_boot_sec:  u16,
    bpb_reserved:     [u8; 12],
    bs_drv_num:       u8,
    bs_reserved1:     u8,
    bs_boot_sig:      u8,
    bs_vol_id:        u32,
    bs_vol_lab:       [u8; 11],
    bs_fil_sys_type:  [u8; 8],
}

#[repr(C, packed)]
struct ShortDirEntry {
    name:           [u8; 11],
    attr:           u8,
    nt_res:         u8,
    crt_time_tenth: u8,
    crt_time:       u16,
    crt_date:       u16,
    lst_acc_date:   u16,
    fst_clus_hi:    u16,
    wrt_time:       u16,
    wrt_date:       u16,
    fst_clus_lo:    u16,
    file_size:      u32,
}

#[repr(C, packed)]
struct LfnEntry {
    ord:         u8,
    name1:       [u8; 10],
    attr:        u8,
    typ:         u8,
    chksum:      u8,
    name2:       [u8; 12],
    fst_clus_lo: u16,
    name3:       [u8; 4],
}

const ATTR_DIRECTORY: u8 = 0x10;
const ATTR_VOLUME_ID: u8 = 0x08;
const ATTR_LONG_NAME: u8 = 0x0F;
const FAT_EOC: u32 = 0x0FFFFFF8;
const FAT_MASK: u32 = 0x0FFFFFFF;

fn is_dot_name(name: &[u8; 11]) -> bool {
    name[0] == b'.' && (name[1] == b' ' || (name[1] == b'.' && name[2] == b' '))
}

pub(crate) struct Fat32Inner {
    bdev:            Arc<dyn BlockDevice>,
    bytes_per_sec:   u16,
    sec_per_clus:    u8,
    rsvd_sec_cnt:    u16,
    num_fats:        u8,
    fat_sz32:        u32,
    root_clus:       u32,
    fat_start:       u64,
    data_start:      u64,
}

impl Fat32Inner {
    fn cluster_to_sector(&self, cluster: u32) -> u64 {
        self.data_start + (cluster as u64 - 2) * (self.sec_per_clus as u64)
    }

    fn read_fat_entry(&self, cluster: u32) -> Result<u32, &'static str> {
        let off = (cluster as u64) * 4;
        let sector = self.fat_start + off / 512;
        let idx = (off % 512) as usize;
        let mut buf = [0u8; 512];
        self.bdev.read_sector(sector, &mut buf)?;
        Ok(u32::from_le_bytes(buf[idx..idx + 4].try_into().unwrap()) & FAT_MASK)
    }

    fn write_fat_entry(&self, cluster: u32, val: u32) -> Result<(), &'static str> {
        let off = (cluster as u64) * 4;
        let sector = self.fat_start + off / 512;
        let idx = (off % 512) as usize;
        let mut buf = [0u8; 512];
        self.bdev.read_sector(sector, &mut buf)?;
        let masked = (val & FAT_MASK) as u32;
        buf[idx..idx + 4].copy_from_slice(&masked.to_le_bytes());
        self.bdev.write_sector(sector, &buf)?;
        // 写后 dummy read 刷新缓存
        let mut _tmp = [0u8; 512];
        self.bdev.read_sector(sector, &mut _tmp)?;
        // 如果有第二个 FAT，也得更新
        if self.num_fats > 1 {
            let sector2 = sector + self.fat_sz32 as u64;
            self.bdev.write_sector(sector2, &buf)?;
            self.bdev.read_sector(sector2, &mut _tmp)?;
        }
        Ok(())
    }

    fn write_cluster(&self, cluster: u32, buf: &[u8]) -> Result<(), &'static str> {
        let s = self.cluster_to_sector(cluster);
        let max = (self.sec_per_clus as usize) * 512;
        let n = buf.len().min(max);
        for i in 0..self.sec_per_clus as u64 {
            let off = (i * 512) as usize;
            if off >= n { break; }
            let end = (off + 512).min(n);
            let mut sb = [0u8; 512];
            sb[..end - off].copy_from_slice(&buf[off..end]);
            self.bdev.write_sector(s + i, &sb)?;
        }
        // 写后 dummy read 强制刷新 QEMU virtio 缓存
        let mut tmp = [0u8; 512];
        self.bdev.read_sector(s, &mut tmp)?;
        Ok(())
    }

    fn alloc_cluster(&self) -> Result<u32, &'static str> {
        let total = (self.fat_sz32 as u64 * 512) / 4;
        for c in 2..total as u32 {
            if self.read_fat_entry(c)? == 0 {
                self.write_fat_entry(c, FAT_EOC)?;
                // 清零簇
                let zero = vec![0u8; (self.sec_per_clus as usize) * 512];
                self.write_cluster(c, &zero)?;
                return Ok(c);
            }
        }
        Err("no free clusters")
    }

    fn extend_chain(&self, start: u32) -> Result<u32, &'static str> {
        // 找链尾
        let mut c = start;
        loop {
            let next = self.read_fat_entry(c)?;
            if next >= FAT_EOC { break; }
            c = next;
        }
        let newc = self.alloc_cluster()?;
        self.write_fat_entry(c, newc)?;
        Ok(newc)
    }

    fn free_chain(&self, start: u32) -> Result<(), &'static str> {
        let mut c = start;
        while c >= 2 && c < FAT_EOC {
            let next = self.read_fat_entry(c)?;
            self.write_fat_entry(c, 0)?; // 标记为空闲
            c = next;
        }
        Ok(())
    }

    /// 更新目录项中的 file_size 字段（扫描根目录匹配 first_cluster）
    fn update_entry_size(&self, cluster: u32, size: u64) -> Result<(), &'static str> {
        let root_clus = self.root_clus;
        let clus_bytes = (self.sec_per_clus as usize) * 512;
        let mut c = root_clus;
        while c >= 2 && c < FAT_EOC {
            let mut buf = vec![0u8; clus_bytes];
            self.read_cluster(c, &mut buf)?;
            let mut i = 0;
            while i + 32 <= buf.len() {
                if buf[i] == 0x00 || buf[i] == 0xE5 { i += 32; continue; }
                if buf[11] == ATTR_LONG_NAME || buf[11] & ATTR_VOLUME_ID != 0 { i += 32; continue; }
                // 检查 first_cluster 是否匹配
                let fcl = u16::from_le_bytes([buf[i + 26], buf[i + 27]]);
                let fch = u16::from_le_bytes([buf[i + 20], buf[i + 21]]);
                let fc = (fch as u32) << 16 | fcl as u32;
                if fc == cluster {
                    // 更新 file_size
                    let size32 = (size as u32).to_le_bytes();
                    buf[i + 28..i + 32].copy_from_slice(&size32);
                    self.write_cluster(c, &buf)?;
                    return Ok(());
                }
                i += 32;
            }
            if i < buf.len() && buf[i] == 0 { break; }
            c = self.read_fat_entry(c)?;
        }
        Err("entry not found")
    }

    fn read_cluster(&self, cluster: u32, buf: &mut [u8]) -> Result<(), &'static str> {
        let s = self.cluster_to_sector(cluster);
        let max = (self.sec_per_clus as usize) * 512;
        let n = buf.len().min(max);
        let mut off = 0usize;
        for i in 0..self.sec_per_clus as u64 {
            if off >= n { break; }
            let mut sb = [0u8; 512];
            self.bdev.read_sector(s + i, &mut sb)?;
            let c = 512usize.min(n - off);
            buf[off..off + c].copy_from_slice(&sb[..c]);
            off += c;
        }
        Ok(())
    }

    fn chain_size(&self, start: u32) -> Result<u64, &'static str> {
        let mut n = 0u64;
        let mut c = start;
        while c >= 2 && c < FAT_EOC {
            n += 1;
            c = self.read_fat_entry(c)?;
        }
        Ok(n * (self.sec_per_clus as u64) * 512)
    }
}

unsafe impl Send for Fat32Inner {}
unsafe impl Sync for Fat32Inner {}

pub struct Fat32Inode {
    inner: Arc<Fat32Inner>,
    first_cluster: u32,
    size: UnsafeCell<u64>,
    is_dir: bool,
}

impl Fat32Inode {
    pub(crate) fn new(inner: Arc<Fat32Inner>, first_cluster: u32, size: u64, is_dir: bool) -> Self {
        Fat32Inode { inner, first_cluster, size: UnsafeCell::new(size), is_dir }
    }

    fn encode_short_name(name: &str) -> Result<[u8; 11], &'static str> {
        let mut raw = [b' '; 11];
        let dot = name.find('.');
        let (base, ext) = if let Some(p) = dot {
            (&name[..p], &name[p + 1..])
        } else {
            (name, "")
        };
        if base.len() > 8 || ext.len() > 3 || base.is_empty() {
            return Err("bad name");
        }
        let blen = base.len().min(8);
        let elen = ext.len().min(3);
        raw[..blen].copy_from_slice(&base.as_bytes()[..blen]);
        raw[8..8 + elen].copy_from_slice(&ext.as_bytes()[..elen]);
        Ok(raw)
    }

    fn parse_short_name(raw: &[u8; 11]) -> String {
        let mut s = String::new();
        for &c in raw[0..8].iter() {
            if c == b' ' { break; }
            s.push(c as char);
        }
        let ext: String = raw[8..11].iter()
            .take_while(|&&c| c != b' ')
            .map(|&c| c as char).collect();
        if !ext.is_empty() { s.push('.'); s.push_str(&ext); }
        s
    }

    fn read_entries(&self) -> Result<Vec<DirEntry>, &'static str> {
        if !self.is_dir { return Err("not a dir"); }
        let mut entries = Vec::new();
        let clus_bytes = (self.inner.sec_per_clus as usize) * 512;
        let mut clus = self.first_cluster;
        loop {
            if clus < 2 || clus >= FAT_EOC { break; }
            let mut buf = vec![0u8; clus_bytes];
            self.inner.read_cluster(clus, &mut buf)?;
            let mut i = 0;
            while i + 32 <= buf.len() {
                let raw = &buf[i..i + 32];
                if raw[0] == 0 { break; }
                if raw[0] == 0xE5 { i += 32; continue; }
                if raw[11] == ATTR_LONG_NAME { i += 32; continue; }
                let short: &ShortDirEntry = unsafe { &*(raw.as_ptr() as *const ShortDirEntry) };
                if short.attr & ATTR_VOLUME_ID != 0 { i += 32; continue; }
                if is_dot_name(&short.name) { i += 32; continue; }
                let name = Self::parse_short_name(&short.name);
                if !name.is_empty() {
                    let fc = (u16::from_le(short.fst_clus_hi) as u32) << 16 | u16::from_le(short.fst_clus_lo) as u32;
                    entries.push(DirEntry {
                        name,
                        is_dir: (short.attr & ATTR_DIRECTORY) != 0,
                        size: u32::from_le(short.file_size) as u64,
                    });
                }
                i += 32;
            }
            if i < buf.len() && buf[i] == 0 { break; }
            clus = self.inner.read_fat_entry(clus)?;
        }
        Ok(entries)
    }
}

impl Inode for Fat32Inode {
    fn read_at(&self, offset: u64, buf: &mut [u8]) -> Result<u64, &'static str> {
        let filesize = unsafe { *self.size.get() };
        if offset >= filesize { return Ok(0); }
        let to_read = buf.len().min((filesize - offset) as usize);
        if to_read == 0 { return Ok(0); }
        let clus_bytes = (self.inner.sec_per_clus as u64) * 512;
        let skip_clus = offset / clus_bytes;
        let skip_byte = offset % clus_bytes;
        let mut total = 0usize;
        let mut clus = self.first_cluster;
        for _ in 0..skip_clus {
            if clus < 2 || clus >= FAT_EOC { return Ok(total as u64); }
            clus = self.inner.read_fat_entry(clus)?;
        }
        let mut boff = skip_byte;
        // 复用簇缓冲避免反复分配
        let mut full = vec![0u8; clus_bytes as usize];
        while clus >= 2 && clus < FAT_EOC && total < to_read {
            let remain = to_read - total;
            let clen = ((clus_bytes - boff) as usize).min(remain);
            self.inner.read_cluster(clus, &mut full)?;
            buf[total..total + clen].copy_from_slice(&full[boff as usize..boff as usize + clen]);
            total += clen;
            boff = 0;
            clus = self.inner.read_fat_entry(clus)?;
        }
        Ok(total as u64)
    }

    fn write_at(&self, offset: u64, buf: &[u8]) -> Result<u64, &'static str> {
        if buf.is_empty() { return Ok(0); }
        let clus_bytes = (self.inner.sec_per_clus as u64) * 512;
        let end_needed = offset + buf.len() as u64;
        let old_size = unsafe { *self.size.get() };

        // 如果文件需要变大，扩展簇链
        if end_needed > old_size {
            let mut current = old_size;
            let mut last = self.first_cluster;

            // 空文件：分配首个簇
            if self.first_cluster < 2 {
                return Err("empty file, not supported yet");
            }

            // 走到链尾
            while current + clus_bytes < end_needed {
                current += clus_bytes;
                let next = self.inner.read_fat_entry(last)?;
                if next >= FAT_EOC {
                    let newc = self.inner.alloc_cluster()?;
                    self.inner.write_fat_entry(last, newc)?;
                    last = newc;
                } else {
                    last = next;
                }
            }
        }

        // 写数据
        let mut written = 0usize;
        let mut pos = offset;
        while written < buf.len() {
            let clus_idx = pos / clus_bytes;
            let clus_off = pos % clus_bytes;
            // 走到目标簇
            let mut clus = self.first_cluster;
            for _ in 0..clus_idx {
                let next = self.inner.read_fat_entry(clus)?;
                if next >= FAT_EOC { break; }
                clus = next;
            }
            if clus < 2 { break; }

            let remain = (buf.len() - written).min((clus_bytes - clus_off) as usize);
            let mut full = vec![0u8; clus_bytes as usize];
            // 如果写入不是整个簇，先读原内容再部分覆盖
            if clus_off != 0 || remain as u64 != clus_bytes {
                self.inner.read_cluster(clus, &mut full)?;
            }
            full[clus_off as usize..clus_off as usize + remain].copy_from_slice(&buf[written..written + remain]);
            self.inner.write_cluster(clus, &full)?;
            written += remain;
            pos += remain as u64;
        }

        // 更新文件大小
        let new_size = if end_needed > old_size { end_needed } else { old_size };
        unsafe { *self.size.get() = new_size; }
        // 尝试写回目录项的 file_size 字段
        let _ = self.inner.update_entry_size(self.first_cluster, new_size);
        Ok(written as u64)
    }

    fn readdir(&self) -> Result<Vec<DirEntry>, &'static str> {
        self.read_entries()
    }

    fn lookup(&self, name: &str) -> Result<Arc<dyn Inode>, &'static str> {
        let clus_bytes = (self.inner.sec_per_clus as u64) * 512;
        let mut clus = self.first_cluster;
        loop {
            if clus < 2 || clus >= FAT_EOC { break; }
            let mut buf = vec![0u8; clus_bytes as usize];
            self.inner.read_cluster(clus, &mut buf)?;
            let mut i = 0;
            while i + 32 <= buf.len() {
                let raw = &buf[i..i + 32];
                if raw[0] == 0 || raw[0] == 0xE5 { i += 32; continue; }
                if raw[11] == ATTR_LONG_NAME || raw[11] & ATTR_VOLUME_ID != 0 { i += 32; continue; }
                let short: &ShortDirEntry = unsafe { &*(raw.as_ptr() as *const ShortDirEntry) };
                if is_dot_name(&short.name) { i += 32; continue; }
                let n = Self::parse_short_name(&short.name);
                if n.eq_ignore_ascii_case(name) {
                    let fc = (u16::from_le(short.fst_clus_hi) as u32) << 16 | u16::from_le(short.fst_clus_lo) as u32;
                    // 优先用 chain_size（比目录项的 file_size 更实时）
                    let sz = self.inner.chain_size(fc).unwrap_or(u32::from_le(short.file_size) as u64);
                    let d = (short.attr & ATTR_DIRECTORY) != 0;
                    return Ok(Arc::new(Fat32Inode::new(self.inner.clone(), fc, sz, d)));
                }
                i += 32;
            }
            if i < buf.len() && buf[i] == 0 { break; }
            clus = self.inner.read_fat_entry(clus)?;
        }
        Err("entry not found")
    }

    fn create(&self, name: &str, is_dir: bool) -> Result<Arc<dyn Inode>, &'static str> {
        if !self.is_dir { return Err("not a dir"); }
        let raw_name = Self::encode_short_name(name)?;
        let clus_bytes = (self.inner.sec_per_clus as usize) * 512;

        // 分配新簇给文件/目录
        let new_clus = self.inner.alloc_cluster()?;

        // 如果是目录，初始化 "." 和 ".."
        if is_dir {
            let mut zero = vec![0u8; clus_bytes];
            // "."
            zero[0] = b'.';
            zero[11] = ATTR_DIRECTORY;
            let nc_lo = new_clus as u16;
            let nc_hi = (new_clus >> 16) as u16;
            zero[20..22].copy_from_slice(&nc_hi.to_le_bytes());
            zero[26..28].copy_from_slice(&nc_lo.to_le_bytes());
            // ".."
            zero[32] = b'.';
            zero[33] = b'.';
            zero[43] = ATTR_DIRECTORY;
            let pc = self.first_cluster;
            let pc_lo = pc as u16;
            let pc_hi = (pc >> 16) as u16;
            zero[52..54].copy_from_slice(&pc_hi.to_le_bytes());
            zero[58..60].copy_from_slice(&pc_lo.to_le_bytes());
            self.inner.write_cluster(new_clus, &zero)?;
        }

        // 构造目录项（用字节数组避免 packed struct 对齐问题）
        let mut raw = [0u8; 32];
        raw[..11].copy_from_slice(&raw_name);
        raw[11] = if is_dir { ATTR_DIRECTORY } else { 0 };
        let clus_lo = new_clus as u16;
        let clus_hi = (new_clus >> 16) as u16;
        raw[20..22].copy_from_slice(&clus_hi.to_le_bytes());
        raw[26..28].copy_from_slice(&clus_lo.to_le_bytes());
        // file_size 和 time 字段保持为 0

        // 找空闲目录项（首字节 0x00 或 0xE5）
        let mut found = false;
        let mut clus = self.first_cluster;
        'outer: while clus >= 2 && clus < FAT_EOC {
            let mut buf = vec![0u8; clus_bytes];
            self.inner.read_cluster(clus, &mut buf)?;
            let mut i = 0;
            while i + 32 <= buf.len() {
                if buf[i] == 0x00 || buf[i] == 0xE5 {
                    buf[i..i + 32].copy_from_slice(&raw);
                    self.inner.write_cluster(clus, &buf)?;
                    found = true;
                    break 'outer;
                }
                i += 32;
            }
            clus = self.inner.read_fat_entry(clus)?;
        }
        if !found {
            // 扩展目录簇
            let newc = self.inner.alloc_cluster()?;
            if self.first_cluster >= 2 {
                let mut c = self.first_cluster;
                loop {
                    let n = self.inner.read_fat_entry(c)?;
                    if n >= FAT_EOC { break; }
                    c = n;
                }
                self.inner.write_fat_entry(c, newc)?;
            }
            // 在新簇开头写入条目
            let mut buf = vec![0u8; clus_bytes];
            buf[..32].copy_from_slice(&raw);
            self.inner.write_cluster(newc, &buf)?;
        }

        let sz = if is_dir { clus_bytes as u64 } else { 0 };
        Ok(Arc::new(Fat32Inode::new(self.inner.clone(), new_clus, sz, is_dir)))
    }

    fn remove(&self, name: &str) -> Result<(), &'static str> {
        if !self.is_dir { return Err("not a dir"); }
        let clus_bytes = (self.inner.sec_per_clus as usize) * 512;
        let mut clus = self.first_cluster;
        while clus >= 2 && clus < FAT_EOC {
            let mut buf = vec![0u8; clus_bytes];
            self.inner.read_cluster(clus, &mut buf)?;
            let mut i = 0;
            while i + 32 <= buf.len() {
                if buf[i] == 0x00 || buf[i] == 0xE5 { i += 32; continue; }
                if buf[11] == ATTR_LONG_NAME { i += 32; continue; }
                if buf[11] & ATTR_VOLUME_ID != 0 { i += 32; continue; }
                // 解析短文件名（直接读字节避免 packed struct）
                let mut raw_name = [b' '; 11];
                raw_name.copy_from_slice(&buf[i..i + 11]);
                let n = Self::parse_short_name(&raw_name);
                if n.eq_ignore_ascii_case(name) {
                    // 标记为已删除
                    buf[i] = 0xE5;
                    self.inner.write_cluster(clus, &buf)?;
                    // 释放该文件的簇链
                    let fcl = u16::from_le_bytes([buf[i + 26], buf[i + 27]]);
                    let fch = u16::from_le_bytes([buf[i + 20], buf[i + 21]]);
                    let fc = (fch as u32) << 16 | fcl as u32;
                    if fc >= 2 {
                        self.inner.free_chain(fc)?;
                    }
                    return Ok(());
                }
                i += 32;
            }
            clus = self.inner.read_fat_entry(clus)?;
        }
        Err("not found")
    }

    fn metadata(&self) -> Result<FileMeta, &'static str> {
        let sz = unsafe { *self.size.get() };
        Ok(FileMeta { size: sz, is_dir: self.is_dir })
    }

    fn truncate(&self) -> Result<(), &'static str> {
        if self.first_cluster >= 2 {
            self.inner.free_chain(self.first_cluster)?;
        }
        unsafe { *self.size.get() = 0; }
        Ok(())
    }
}

unsafe impl Send for Fat32Inode {}
unsafe impl Sync for Fat32Inode {}

pub struct Fat32FS {
    inner: Arc<Fat32Inner>,
}

impl Fat32FS {
    pub fn new(bdev: Arc<dyn BlockDevice>) -> Result<Self, &'static str> {
        let mut sector0 = [0u8; 512];
        bdev.read_sector(0, &mut sector0)?;
        let bpb: &Bpb = unsafe { &*(sector0.as_ptr() as *const Bpb) };
        let bps = u16::from_le(bpb.bpb_byts_per_sec);
        let spc = bpb.bpb_sec_per_clus;
        let rsvd = u16::from_le(bpb.bpb_rsvd_sec_cnt);
        let nfats = bpb.bpb_num_fats;
        let fatsz = u32::from_le(bpb.bpb_fat_sz32);
        let rootc = u32::from_le(bpb.bpb_root_clus);
        let hidd = u32::from_le(bpb.bpb_hidd_sec);
        if bps != 512 { return Err("bad bytes per sector"); }
        if spc == 0 || (spc & (spc - 1)) != 0 { return Err("bad sec per clus"); }
        if rootc < 2 { return Err("bad root cluster"); }
        let fat_start = rsvd as u64 + hidd as u64;
        let data_start = fat_start + (nfats as u64) * (fatsz as u64);
        Ok(Fat32FS {
            inner: Arc::new(Fat32Inner {
                bdev, bytes_per_sec: bps, sec_per_clus: spc,
                rsvd_sec_cnt: rsvd, num_fats: nfats, fat_sz32: fatsz,
                root_clus: rootc, fat_start, data_start,
            }),
        })
    }
}

impl FileSystem for Fat32FS {
    fn root_inode(&self) -> Arc<dyn Inode> {
        let root_size = self.inner.chain_size(self.inner.root_clus).unwrap_or(0);
        Arc::new(Fat32Inode::new(self.inner.clone(), self.inner.root_clus, root_size, true))
    }
    fn name(&self) -> &str { "fat32" }
    fn block_device(&self) -> &dyn BlockDevice { &*self.inner.bdev }
}

unsafe impl Send for Fat32FS {}
unsafe impl Sync for Fat32FS {}

use alloc::string::String;
use alloc::string::ToString;
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;
use crate::sync::KMutex;

/// 块设备 trait
pub trait BlockDevice: Send + Sync {
    fn read_sector(&self, sector: u64, buf: &mut [u8]) -> Result<(), &'static str>;
    fn write_sector(&self, sector: u64, buf: &[u8]) -> Result<(), &'static str>;
    fn sector_size(&self) -> u64 { 512 }
    fn total_sectors(&self) -> u64 { 0 }
}

/// 文件元数据
#[derive(Clone, Debug)]
pub struct FileMeta {
    pub size: u64,
    pub is_dir: bool,
}

/// 目录项
#[derive(Clone, Debug)]
pub struct DirEntry {
    pub name: String,
    pub is_dir: bool,
    pub size: u64,
}

/// Inode trait
pub trait Inode: Send + Sync {
    fn read_at(&self, offset: u64, buf: &mut [u8]) -> Result<u64, &'static str>;
    fn write_at(&self, offset: u64, buf: &[u8]) -> Result<u64, &'static str>;
    fn readdir(&self) -> Result<Vec<DirEntry>, &'static str>;
    fn lookup(&self, name: &str) -> Result<Arc<dyn Inode>, &'static str>;
    fn create(&self, name: &str, is_dir: bool) -> Result<Arc<dyn Inode>, &'static str>;
    fn remove(&self, name: &str) -> Result<(), &'static str>;
    fn metadata(&self) -> Result<FileMeta, &'static str>;
    fn truncate(&self) -> Result<(), &'static str>;
}

/// 文件系统 trait
pub trait FileSystem: Send + Sync {
    fn root_inode(&self) -> Arc<dyn Inode>;
    fn name(&self) -> &str;
    fn block_device(&self) -> &dyn BlockDevice;
}

/// 打开文件描述
pub struct OpenFile {
    pub inode: Arc<dyn Inode>,
    pub pos: u64,
    pub flags: u32,
}

impl OpenFile {
    pub fn new(inode: Arc<dyn Inode>, flags: u32) -> Self {
        OpenFile { inode, pos: 0, flags }
    }

    pub fn read(&mut self, buf: &mut [u8]) -> Result<u64, &'static str> {
        let n = self.inode.read_at(self.pos, buf)?;
        self.pos += n;
        Ok(n)
    }

    pub fn write(&mut self, buf: &[u8]) -> Result<u64, &'static str> {
        let n = self.inode.write_at(self.pos, buf)?;
        self.pos += n;
        Ok(n)
    }

    pub fn seek(&mut self, offset: i64, whence: u32) -> Result<u64, &'static str> {
        let new_pos = match whence {
            0 => offset as u64,
            1 => (self.pos as i64 + offset) as u64,
            2 => {
                let meta = self.inode.metadata()?;
                (meta.size as i64 + offset) as u64
            }
            _ => return Err("invalid whence"),
        };
        self.pos = new_pos;
        Ok(new_pos)
    }
}

/// 文件描述符表
pub struct FdTable {
    fds: Vec<Option<Arc<KMutex<OpenFile>>>>,
}

impl FdTable {
    pub fn new() -> Self {
        let mut fds = Vec::new();
        fds.resize(3, None);
        FdTable { fds }
    }

    /// 分配 fd，跳过 0/1/2（标准输入输出）
    pub fn alloc(&mut self, of: Arc<KMutex<OpenFile>>) -> usize {
        for (i, slot) in self.fds.iter_mut().enumerate().skip(3) {
            if slot.is_none() {
                *slot = Some(of);
                return i;
            }
        }
        let fd = self.fds.len();
        self.fds.push(Some(of));
        fd
    }

    pub fn get(&self, fd: usize) -> Option<&Arc<KMutex<OpenFile>>> {
        self.fds.get(fd)?.as_ref()
    }

    pub fn close(&mut self, fd: usize) -> bool {
        if fd < self.fds.len() && self.fds[fd].is_some() {
            self.fds[fd] = None;
            true
        } else {
            false
        }
    }

    pub fn dup(&mut self, fd: usize) -> Option<usize> {
        let of = self.fds.get(fd)?.as_ref()?.clone();
        Some(self.alloc(of))
    }

    /// 分配一对文件描述符（用于 pipe），返回 (read_fd, write_fd)
    pub fn alloc_pair(&mut self, read_of: Arc<KMutex<OpenFile>>, write_of: Arc<KMutex<OpenFile>>) -> (usize, usize) {
        let read_fd = self.alloc(read_of);
        let write_fd = self.alloc(write_of);
        (read_fd, write_fd)
    }

    /// 在指定位置插入一个 fd 副本（用于 dup2/dup3）
    pub fn dup_fix(&mut self, old_fd: usize, new_fd: usize) -> Option<usize> {
        let of = self.fds.get(old_fd)?.as_ref()?.clone();
        if new_fd < self.fds.len() {
            self.fds[new_fd] = Some(of);
        } else {
            self.fds.resize(new_fd + 1, None);
            self.fds[new_fd] = Some(of);
        }
        Some(new_fd)
    }

    /// 获取 FD 的就 low-level 引用（用于 fcntl 等）
    pub fn get_raw(&self, fd: usize) -> Option<&Arc<KMutex<OpenFile>>> {
        self.fds.get(fd)?.as_ref()
    }

    /// 克隆整个 fd 表（用于 fork 子进程继承父进程 fd）
    pub fn clone_from(&mut self, src: &FdTable) {
        self.fds.clear();
        self.fds.reserve(src.fds.len());
        for slot in &src.fds {
            self.fds.push(slot.as_ref().map(|arc| Arc::clone(arc)));
        }
    }
}

/// 全局挂载表
static MOUNT_TABLE: KMutex<Vec<Mount>> = KMutex::new(Vec::new());

pub struct Mount {
    pub path: String,
    pub fs: Arc<dyn FileSystem>,
}

pub fn mount(path: &str, fs: Arc<dyn FileSystem>) {
    let mut table = MOUNT_TABLE.lock();
    for m in table.iter() {
        if m.path == path { return; }
    }
    table.push(Mount { path: String::from(path), fs });
}

pub fn find_mount(path: &str) -> Option<(Arc<dyn FileSystem>, String)> {
    let table = MOUNT_TABLE.lock();
    let mut best: Option<(usize, &Mount)> = None;
    for m in table.iter() {
        if path.starts_with(&m.path) {
            let len = m.path.len();
            if best.as_ref().map_or(true, |(blen, _)| len > *blen) {
                best = Some((len, m));
            }
        }
    }
    best.map(|(_, m)| {
        let rest = if path.len() > m.path.len() {
            path[m.path.len()..].to_string()
        } else {
            String::new()
        };
        (m.fs.clone(), rest)
    })
}

pub fn root_fs() -> Option<Arc<dyn FileSystem>> {
    let table = MOUNT_TABLE.lock();
    table.iter().find(|m| m.path == "/").map(|m| m.fs.clone())
}

pub fn path_walk(path: &str) -> Result<Arc<dyn Inode>, &'static str> {
    let (fs, rel_path) = find_mount(path).ok_or("no mount for path")?;
    let inode = fs.root_inode();
    let trimmed = rel_path.trim_start_matches('/');
    if trimmed.is_empty() { return Ok(inode); }
    let mut current = inode;
    for component in trimmed.split('/') {
        if component.is_empty() { continue; }
        current = current.lookup(component)?;
    }
    Ok(current)
}

pub fn open_file(path: &str, flags: u32) -> Result<Arc<KMutex<OpenFile>>, &'static str> {
    let inode = path_walk(path)?;
    Ok(Arc::new(KMutex::new(OpenFile::new(inode, flags))))
}

/// 读取整个文件内容（供 ELF 加载使用）
pub fn read_whole_file(path: &str) -> Result<Vec<u8>, &'static str> {
    let inode = path_walk(path)?;
    let meta = inode.metadata()?;
    let size = meta.size as usize;
    if size == 0 { return Err("empty file"); }
    if size > 8 * 1024 * 1024 { return Err("file too large"); }
    let mut buf = Vec::with_capacity(size);
    unsafe { buf.set_len(size); }
    let n = inode.read_at(0, &mut buf)?;
    if n as usize != size { buf.truncate(n as usize); }
    Ok(buf)
}

/// 解析路径：相对路径拼 cwd，处理 `..` 归一化
pub fn resolve_path(cwd: &str, path: &str) -> Result<String, &'static str> {
    let joined = if path.starts_with('/') {
        path.to_string()
    } else {
        let mut s = String::with_capacity(cwd.len() + 1 + path.len());
        s.push_str(cwd);
        if !cwd.ends_with('/') { s.push('/'); }
        s.push_str(path);
        s
    };

    // 归一化：按 / 分段，处理 .. 和 .
    let mut parts: Vec<&str> = Vec::new();
    for seg in joined.split('/') {
        match seg {
            "" | "." => continue,
            ".." => { parts.pop(); },
            _ => parts.push(seg),
        }
    }

    let mut result = String::from("/");
    for (i, part) in parts.iter().enumerate() {
        if i > 0 { result.push('/'); }
        result.push_str(part);
    }
    if result.is_empty() { result.push('/'); }
    Ok(result)
}

/// 在父目录中查找某个条目的真实名称（处理 FAT32 大小写不敏感情形）
pub fn lookup_real_name(parent: &dyn Inode, name: &str) -> Result<String, &'static str> {
    let entries = parent.readdir()?;
    for e in &entries {
        if e.name.eq_ignore_ascii_case(name) {
            return Ok(e.name.clone());
        }
    }
    Err("entry not found in parent directory")
}

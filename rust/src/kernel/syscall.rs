use crate::println;
use crate::print;
use crate::ffi;
use crate::kernel::task::{self, CURRENT_TASK, do_kill};
use crate::kernel::fs::vfs;
use crate::arch::aarch64::cpu::NEED_RESCHED;
use alloc::sync::Arc;
use alloc::string::String;
use core::sync::atomic::Ordering;

// Linux ARM64 (AArch64) 标准系统调用号
pub const SYS_READ:       usize = 63;
pub const SYS_WRITE:      usize = 64;
pub const SYS_EXIT:       usize = 93;
pub const SYS_EXIT_GROUP: usize = 94;
pub const SYS_OPENAT:     usize = 56;
pub const SYS_CLOSE:      usize = 57;
pub const SYS_LSEEK:      usize = 62;
pub const SYS_BRK:        usize = 214;
pub const SYS_MMAP:       usize = 222;
pub const SYS_MUNMAP:     usize = 215;
pub const SYS_SCHED_YIELD: usize = 124;
pub const SYS_FORK:       usize = 220;
pub const SYS_WAIT4:      usize = 260;
pub const SYS_KILL:       usize = 129;
pub const SYS_EXECVE:     usize = 221;
pub const SYS_MKDIRAT:    usize = 34;
pub const SYS_UNLINKAT:   usize = 35;
pub const SYS_DUP:        usize = 23;
pub const SYS_DUP3:       usize = 24;
pub const SYS_GETPID:     usize = 172;
pub const SYS_GETPPID:    usize = 173;
pub const SYS_CHDIR:      usize = 49;
pub const SYS_GETDENTS64: usize = 217;
pub const SYS_GETCWD:     usize = 17;
pub const SYS_GETNAME:    usize = 1000;
pub const SYS_IOCTL:      usize = 29;
pub const SYS_FSTATAT:    usize = 79;
pub const SYS_UNAME:      usize = 160;

// 简易 errno（syscall 返回负值）
const ENOENT: i64 = 2;
const EBADF:  i64 = 9;
const EACCES: i64 = 13;
const EFAULT: i64 = 14;
const ENOTTY: i64 = 25;
const EINVAL: i64 = 22;

const fn neg_errno(e: i64) -> usize { (e as isize) as usize }

const fn neg1() -> usize { !0usize }

/// AT_FDCWD 表示"当前工作目录"
const AT_FDCWD: i64 = -100;

/// O_ 标志
const O_ACCMODE: u32 = 0o3;
const O_RDONLY: u32 = 0o0;
const O_WRONLY: u32 = 0o1;
const O_RDWR: u32 = 0o2;
const O_CREAT: u32 = 0o100;
const O_TRUNC: u32 = 0o1000;

/// TIOCGWINSZ = 0x5413
const TIOCGWINSZ: u64 = 0x5413;

/// struct winsize: ws_row(2) + ws_col(2) + ws_xpixel(2) + ws_ypixel(2) = 8 bytes
const WINSIZE_ROW: u16 = 24;
const WINSIZE_COL: u16 = 80;

pub fn handle_syscall(syscall_num: usize, x0: usize, x1: usize, x2: usize, x3: usize) -> usize {
    match syscall_num {
        SYS_EXIT | SYS_EXIT_GROUP => {
            crate::kernel::task::do_exit_with(x0 as i32);
            0
        }
        SYS_BRK => {
            match task::current_mm() {
                Some(mm) => mm.do_brk(x0 as u64) as usize,
                None => 0,
            }
        }
        SYS_MMAP => {
            match task::current_mm() {
                Some(mm) => mm.do_mmap(x0 as u64, x1 as u64, x2 as u64) as usize,
                None => 0,
            }
        }
        SYS_MUNMAP => {
            match task::current_mm() {
                Some(mm) => mm.do_munmap(x0 as u64, x1 as u64) as usize,
                None => 0,
            }
        }
        SYS_SCHED_YIELD => {
            NEED_RESCHED.store(true, Ordering::Relaxed);
            0
        }
        SYS_FORK => task::do_fork(x0 as *const ffi::PtRegs),
        SYS_WAIT4 => {
            let pid = x0 as i64;
            let wstatus = x1 as *mut i32;
            task::do_wait4(pid, wstatus) as usize
        }
        SYS_KILL => do_kill(x0, x1),
        SYS_READ => {
            let fd = x0;
            let buf = x1 as *mut u8;
            let len = x2;
            if buf.is_null() || len == 0 || len > 4096 { return 0; }
            if fd == 0 {
                // stdin → TTY
                let mut tmp = [0u8; 256];
                let n = crate::kernel::tty::TTY0.read_line(&mut tmp);
                let copy = n.min(len - 1);
                unsafe { core::ptr::copy_nonoverlapping(tmp.as_ptr(), buf, copy); }
                unsafe { *buf.add(copy) = 0; }
                copy
            } else {
                // 普通文件读
                match task::current_fd_table() {
                    Some(fdt) => match fdt.get(fd) {
                        Some(of_lock) => {
                            let mut of = of_lock.lock();
                            let fb = unsafe { core::slice::from_raw_parts_mut(buf, len) };
                            match of.read(fb) {
                                Ok(n) => n as usize,
                                Err(_) => 0,
                            }
                        }
                        None => neg_errno(EBADF),
                    },
                    None => 0,
                }
            }
        }
        SYS_WRITE => {
            let fd = x0;
            let buf = x1 as *const u8;
            let len = x2;
            if buf.is_null() || len == 0 || len > 4096 { return 0; }
            let slice = unsafe { core::slice::from_raw_parts(buf, len) };
            if fd <= 2 {
                // stdout / stderr → TTY
                crate::kernel::tty::TTY0.write(slice)
            } else {
                // 普通文件写
                match task::current_fd_table() {
                    Some(fdt) => match fdt.get(fd) {
                        Some(of_lock) => {
                            let mut of = of_lock.lock();
                            match of.write(slice) {
                                Ok(n) => n as usize,
                                Err(_) => 0,
                            }
                        }
                        None => neg_errno(EBADF),
                    },
                    None => 0,
                }
            }
        }
        SYS_GETPID => {
            let ptr = CURRENT_TASK.load(Ordering::Relaxed);
            if ptr.is_null() { 0 } else { unsafe { (*ptr).pid } }
        }
        SYS_GETPPID => {
            let ptr = CURRENT_TASK.load(Ordering::Relaxed);
            if ptr.is_null() { 0 } else { unsafe { (*ptr).ppid as usize } }
        }
        SYS_OPENAT => {
            let dirfd = x0 as i64;
            let path_ptr = x1 as *const u8;
            let flags = x2 as u32;
            if path_ptr.is_null() { return neg_errno(ENOENT); }
            let path = unsafe {
                let mut s = String::new();
                for i in 0..256 {
                    let c = *path_ptr.add(i);
                    if c == 0 { break; }
                    s.push(c as char);
                }
                s
            };
            // 解析路径（支持相对路径）
            let resolved = if dirfd == AT_FDCWD || path.starts_with('/') {
                let cwd = task::current_cwd().unwrap_or(String::from("/"));
                match vfs::resolve_path(&cwd, &path) {
                    Ok(p) => p,
                    Err(_) => return neg_errno(ENOENT),
                }
            } else {
                // 基于 fd 的目录暂不支持，退而用 cwd
                let cwd = task::current_cwd().unwrap_or(String::from("/"));
                match vfs::resolve_path(&cwd, &path) {
                    Ok(p) => p,
                    Err(_) => return neg_errno(ENOENT),
                }
            };
            match sys_open(&resolved, flags) {
                Ok(fd) => fd,
                Err(_) => neg_errno(ENOENT),
            }
        }
        SYS_CLOSE => {
            match task::current_fd_table() {
                Some(fdt) => if fdt.close(x0) { 0 } else { neg_errno(EBADF) },
                None => neg_errno(EBADF),
            }
        }
        SYS_DUP | SYS_DUP3 => {
            let old_fd = x0;
            match task::current_fd_table() {
                Some(fdt) => {
                    if syscall_num == SYS_DUP {
                        fdt.dup(old_fd).unwrap_or(neg_errno(EBADF))
                    } else {
                        let new_fd = x1;
                        let _flags = x2;
                        if old_fd == new_fd { new_fd }
                        else { fdt.dup_fix(old_fd, new_fd).unwrap_or(neg_errno(EBADF)) }
                    }
                }
                None => neg_errno(EBADF),
            }
        }
        SYS_LSEEK => {
            match task::current_fd_table() {
                Some(fdt) => match fdt.get(x0) {
                    Some(of) => match of.lock().seek(x1 as i64, x2 as u32) {
                        Ok(pos) => pos as usize,
                        Err(_) => neg_errno(EINVAL),
                    },
                    None => neg_errno(EBADF),
                },
                None => neg_errno(EBADF),
            }
        }
        SYS_EXECVE => {
            let path_ptr = x0 as *const u8;
            let regs_ptr = x1 as *mut ffi::PtRegs;
            if path_ptr.is_null() || regs_ptr.is_null() { return neg_errno(ENOENT); }
            let path = unsafe {
                let mut s = String::new();
                for i in 0..256 {
                    let c = *path_ptr.add(i);
                    if c == 0 { break; }
                    s.push(c as char);
                }
                s
            };
            match sys_execve(regs_ptr, &path) {
                Ok(()) => 0,
                Err(e) => {
                    println!("[EXECVE] '{}' failed: {}", path, e);
                    neg_errno(ENOENT)
                }
            }
        }
        SYS_MKDIRAT | SYS_UNLINKAT => {
            // mkdirat/unlinkat 由 shell 通过 FAT32 create/remove 处理
            let dirfd = x0 as i64;
            let path_ptr = x1 as *const u8;
            let flags_or_mode = x2 as u32; // mode for mkdirat, flags for unlinkat
            if path_ptr.is_null() { return neg_errno(EFAULT); }
            let path = unsafe {
                let mut s = String::new();
                for i in 0..256 {
                    let c = *path_ptr.add(i);
                    if c == 0 { break; }
                    s.push(c as char);
                }
                s
            };
            let resolved = if path.starts_with('/') || dirfd == AT_FDCWD {
                let cwd = task::current_cwd().unwrap_or(String::from("/"));
                match vfs::resolve_path(&cwd, &path) {
                    Ok(p) => p,
                    Err(_) => return neg_errno(ENOENT),
                }
            } else {
                let cwd = task::current_cwd().unwrap_or(String::from("/"));
                vfs::resolve_path(&cwd, &path).unwrap_or(path)
            };
            let name_s = &resolved[..];
            let (parent_path, name) = if let Some(p) = resolved.rfind('/') {
                (&resolved[..=p], &resolved[p + 1..])
            } else {
                ("/", name_s)
            };
            if name.is_empty() { return neg_errno(ENOENT); }
            match vfs::path_walk(parent_path) {
                Ok(parent_inode) => {
                    if syscall_num == SYS_MKDIRAT {
                        match parent_inode.create(name, true) {
                            Ok(_) => 0,
                            Err(_) => neg_errno(EACCES),
                        }
                    } else {
                        match parent_inode.remove(name) {
                            Ok(()) => 0,
                            Err(_) => neg_errno(ENOENT),
                        }
                    }
                }
                Err(_) => neg_errno(ENOENT),
            }
        }
        SYS_CHDIR => {
            let path_ptr = x0 as *const u8;
            if path_ptr.is_null() { return neg_errno(ENOENT); }
            let path = unsafe {
                let mut s = String::new();
                for i in 0..256 {
                    let c = *path_ptr.add(i);
                    if c == 0 { break; }
                    s.push(c as char);
                }
                s
            };
            let cwd = task::current_cwd().unwrap_or(String::from("/"));
            let resolved = match vfs::resolve_path(&cwd, &path) {
                Ok(p) => p,
                Err(_) => return neg_errno(ENOENT),
            };
            match vfs::path_walk(&resolved) {
                Ok(inode) => {
                    let meta = inode.metadata().unwrap_or(vfs::FileMeta { size: 0, is_dir: false });
                    if !meta.is_dir { return neg_errno(ENOENT); }
                    // 用真实名称
                    let real_path = if resolved == "/" {
                        String::from("/")
                    } else {
                        let pos = resolved.rfind('/').unwrap_or(0);
                        let parent_part = &resolved[..=pos];
                        let last = &resolved[pos + 1..];
                        let parent_inode = vfs::path_walk(parent_part).ok();
                        let real_name = parent_inode.and_then(|p|
                            vfs::lookup_real_name(&*p, last).ok()
                        ).unwrap_or_else(|| String::from(last));
                        let mut s = String::with_capacity(parent_part.len() + real_name.len());
                        s.push_str(parent_part);
                        s.push_str(&real_name);
                        s
                    };
                    task::set_cwd(&real_path);
                    0
                }
                Err(_) => neg_errno(ENOENT),
            }
        }
        SYS_GETDENTS64 => {
            let buf_ptr = x1 as *mut u8;
            let max_bytes = x2;
            if buf_ptr.is_null() || max_bytes < 40 { return 0; }
            let cwd = task::current_cwd().unwrap_or(String::from("/"));
            let inode = match vfs::path_walk(&cwd) {
                Ok(inode) => inode,
                Err(_) => return 0,
            };
            let entries = match inode.readdir() {
                Ok(e) => e,
                Err(_) => return 0,
            };
            let mut written = 0usize;
            for entry in &entries {
                let name_bytes = entry.name.as_bytes();
                let name_len = name_bytes.len();
                let reclen = (19 + name_len + 7) & !7;
                if written + reclen > max_bytes { break; }
                unsafe {
                    let p = buf_ptr.add(written);
                    core::ptr::write_unaligned(p as *mut u64, 1u64);
                    core::ptr::write_unaligned(p.add(8) as *mut i64, (written + reclen) as i64);
                    core::ptr::write_unaligned(p.add(16) as *mut u16, reclen as u16);
                    core::ptr::write_unaligned(p.add(18) as *mut u8, if entry.is_dir { 4u8 } else { 8u8 });
                    core::ptr::copy_nonoverlapping(name_bytes.as_ptr(), p.add(19), name_len);
                    *p.add(19 + name_len) = 0u8;
                }
                written += reclen;
            }
            written
        }
        SYS_GETCWD => {
            let buf_ptr = x0 as *mut u8;
            let max_len = x1;
            if buf_ptr.is_null() || max_len < 2 { return 0; }
            let cwd = task::current_cwd().unwrap_or(String::from("/"));
            let bytes = cwd.as_bytes();
            let copy_len = bytes.len().min(max_len - 1);
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), buf_ptr, copy_len);
                *buf_ptr.add(copy_len) = 0;
            }
            copy_len as usize
        }
        SYS_GETNAME => {
            let buf_ptr = x0 as *mut u8;
            let max_len = x1;
            if buf_ptr.is_null() || max_len < 2 { return 0; }
            let ptr = CURRENT_TASK.load(Ordering::Relaxed);
            let name = if ptr.is_null() { "unknown" } else { unsafe { (*ptr).name } };
            let bytes = name.as_bytes();
            let copy_len = bytes.len().min(max_len - 1);
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), buf_ptr, copy_len);
                *buf_ptr.add(copy_len) = 0;
            }
            copy_len as usize
        }
        SYS_IOCTL => {
            let fd = x0;
            let cmd = x1 as u64;
            let _arg = x2; // argp，暂不使用
            match cmd {
                TIOCGWINSZ => {
                    // struct winsize { u16 ws_row, ws_col, ws_xpixel, ws_ypixel; } = 8 bytes
                    let buf_ptr = x2 as *mut u8;
                    if buf_ptr.is_null() { return neg_errno(EINVAL); }
                    unsafe {
                        core::ptr::write_unaligned(buf_ptr as *mut u16, WINSIZE_ROW);
                        core::ptr::write_unaligned(buf_ptr.add(2) as *mut u16, WINSIZE_COL);
                        core::ptr::write_unaligned(buf_ptr.add(4) as *mut u16, 0);
                        core::ptr::write_unaligned(buf_ptr.add(6) as *mut u16, 0);
                    }
                    0
                }
                // TIOCGPGRP=0x540F: 返回进程组 ID（简化：返回当前 PID）
                0x540F => {
                    let buf_ptr = x2 as *mut i32;
                    if buf_ptr.is_null() { return neg_errno(EINVAL); }
                    let pid = {
                        let ptr = CURRENT_TASK.load(Ordering::Relaxed);
                        if ptr.is_null() { 0 } else { unsafe { (*ptr).pid } }
                    };
                    unsafe { *buf_ptr = pid as i32; }
                    0
                }
                _ => neg_errno(ENOTTY),
            }
        }
        SYS_FSTATAT => {
            let _dirfd = x0 as i64;
            let path_ptr = x1 as *const u8;
            let buf_ptr = x2 as *mut u8;
            let _flags = x3 as i32;
            if path_ptr.is_null() || buf_ptr.is_null() { return neg_errno(ENOENT); }
            let path = unsafe {
                let mut s = String::new();
                for i in 0..256 {
                    let c = *path_ptr.add(i);
                    if c == 0 { break; }
                    s.push(c as char);
                }
                s
            };
            // 解析路径
            let resolved = if path.starts_with('/') {
                path
            } else {
                let cwd = task::current_cwd().unwrap_or(String::from("/"));
                match vfs::resolve_path(&cwd, &path) {
                    Ok(p) => p,
                    Err(_) => path, // fallback
                }
            };
            let inode = match vfs::path_walk(&resolved) {
                Ok(inode) => inode,
                Err(_) => return neg_errno(ENOENT),
            };
            let meta = inode.metadata().unwrap_or(vfs::FileMeta { size: 0, is_dir: false });
            // AArch64 struct stat（new 64-bit variant）:
            //   st_dev(8) + st_ino(8) + st_mode(4) + st_nlink(4) +
            //   st_uid(4) + st_gid(4) + st_rdev(8) + st_size(8) +
            //   st_blksize(8) + st_blocks(8) +
            //   st_atime(8) + st_atime_nsec(8) +
            //   st_mtime(8) + st_mtime_nsec(8) +
            //   st_ctime(8) + st_ctime_nsec(8)
            // = 128 bytes
            unsafe {
                let p = buf_ptr;
                // st_dev
                core::ptr::write_unaligned(p as *mut u64, 0u64);
                // st_ino
                core::ptr::write_unaligned(p.add(8) as *mut u64, 1u64);
                // st_mode: S_IFREG=0100000, S_IFDIR=0040000
                let mode = if meta.is_dir { 0o040000u32 } else { 0o100000u32 } | 0o755u32;
                core::ptr::write_unaligned(p.add(16) as *mut u32, mode);
                // st_nlink
                core::ptr::write_unaligned(p.add(20) as *mut u32, 1u32);
                // st_uid
                core::ptr::write_unaligned(p.add(24) as *mut u32, 0u32);
                // st_gid
                core::ptr::write_unaligned(p.add(28) as *mut u32, 0u32);
                // st_rdev
                core::ptr::write_unaligned(p.add(32) as *mut u64, 0u64);
                // st_size
                core::ptr::write_unaligned(p.add(40) as *mut i64, meta.size as i64);
                // st_blksize
                core::ptr::write_unaligned(p.add(48) as *mut u64, 512u64);
                // st_blocks
                core::ptr::write_unaligned(p.add(56) as *mut u64, (meta.size + 511) / 512);
                // st_atime
                core::ptr::write_unaligned(p.add(64) as *mut u64, 0u64);
                core::ptr::write_unaligned(p.add(72) as *mut u64, 0u64);
                // st_mtime
                core::ptr::write_unaligned(p.add(80) as *mut u64, 0u64);
                core::ptr::write_unaligned(p.add(88) as *mut u64, 0u64);
                // st_ctime
                core::ptr::write_unaligned(p.add(96) as *mut u64, 0u64);
                core::ptr::write_unaligned(p.add(104) as *mut u64, 0u64);
            }
            0
        }
        SYS_UNAME => {
            // struct utsname { char sysname[65]; char nodename[65]; char release[65];
            //                   char version[65]; char machine[65]; char domainname[65]; }
            // = 390 bytes
            let buf_ptr = x0 as *mut u8;
            if buf_ptr.is_null() { return neg_errno(EINVAL); }
            let sysname  = b"MyOS\0";
            let nodename = b"myos\0";
            let release  = b"0.1.0\0";
            let version  = b"#1 SMP\0";
            let machine  = b"aarch64\0";
            let domain   = b"\0";
            unsafe {
                core::ptr::copy_nonoverlapping(sysname.as_ptr(),  buf_ptr,       sysname.len().min(65));
                core::ptr::copy_nonoverlapping(nodename.as_ptr(), buf_ptr.add(65), nodename.len().min(65));
                core::ptr::copy_nonoverlapping(release.as_ptr(),  buf_ptr.add(130), release.len().min(65));
                core::ptr::copy_nonoverlapping(version.as_ptr(),  buf_ptr.add(195), version.len().min(65));
                core::ptr::copy_nonoverlapping(machine.as_ptr(),  buf_ptr.add(260), machine.len().min(65));
                core::ptr::copy_nonoverlapping(domain.as_ptr(),   buf_ptr.add(325), domain.len().min(65));
            }
            0
        }
        _ => { println!("[SYSCALL] unknown: {}", syscall_num); 0 }
    }
}

fn sys_open(path: &str, flags: u32) -> Result<usize, ()> {
    let inode = match vfs::path_walk(path) {
        Ok(inode) => {
            if flags & O_TRUNC != 0 {
                let _ = inode.truncate();
            }
            inode
        }
        Err(_) if flags & O_CREAT != 0 => {
            // 找到父目录创建新文件
            let pos = path.rfind('/');
            let (parent_path, name) = match pos {
                None => ("/", path),           // "1.txt" → 根目录
                Some(0) => ("/", &path[1..]),   // "/1.txt" → 根目录
                Some(p) => (&path[..p], &path[p + 1..]),
            };
            if name.is_empty() { return Err(()); }
            let parent = vfs::path_walk(parent_path).map_err(|_| ())?;
            parent.create(name, false).map_err(|_| ())?
        }
        Err(_) => return Err(()),
    };
    let of = Arc::new(crate::sync::KMutex::new(vfs::OpenFile::new(inode, flags)));
    match task::current_fd_table() {
        Some(fdt) => { let fd = fdt.alloc(of); Ok(fd) }
        None => Err(()),
    }
}

fn sys_execve(regs_ptr: *mut ffi::PtRegs, path: &str) -> Result<(), &'static str> {
    let cwd = task::current_cwd().unwrap_or(String::from("/"));
    let resolved = vfs::resolve_path(&cwd, path)?;
    println!("[EXECVE] loading '{}' -> '{}'...", path, resolved);
    let elf_data = vfs::read_whole_file(&resolved)?;
    let loaded = crate::kernel::elf::load_elf(&elf_data)?;

    let task_ptr = task::CURRENT_TASK.load(Ordering::Relaxed);
    if let Some(task) = unsafe { task_ptr.as_mut() } {
        task.signals.clear_all_pending();
        let new_pgd = loaded.pgd;
        let old_pgd = task.pgd.as_pa();
        task.pgd = crate::kernel::task::PageTable::new(new_pgd);
        crate::kernel::pgtbl::switch_page_table(new_pgd);

        if old_pgd != 0 && old_pgd != new_pgd {
            unsafe { ffi::free_pgd_tree(old_pgd); }
        }
        // 栈页由 free_pgd_tree 中的 put_page 统一释放，不在此单独释放
        task.code_pa = loaded.code_pa;
        task.code_va = loaded.code_va;
        task.stack_pa = loaded.stack_pa;
        task.stack_va = loaded.stack_top as usize;
        task.stack_pages = loaded.stack_pages;
        task.user_sp = loaded.stack_top as usize;
    }

    unsafe {
        (*regs_ptr).elr_el1 = loaded.entry;
        (*regs_ptr).x0 = 0;
        // PtRegs 中寄存器按 x30→x0 递减排列，x30 是最低地址的寄存器字段
        // 从 x30 开始向后写 30 个 u64，即清零 x30..=x1
        for i in 0..30 {
            *(&mut (*regs_ptr).x30 as *mut u64).add(i) = 0;
        }
        core::arch::asm!("msr sp_el0, {}", in(reg) loaded.stack_top);
    }
    println!("[EXECVE] done, jumping to {:#x}", loaded.entry);
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_handle_syscall(num: u64, a0: u64, a1: u64, a2: u64, a3: u64) -> u64 {
    handle_syscall(num as usize, a0 as usize, a1 as usize, a2 as usize, a3 as usize) as u64
}

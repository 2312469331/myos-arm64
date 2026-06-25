use alloc::vec::Vec;
use crate::ffi;
use crate::kernel::pgtbl::{self, PAGE_USER_RX, PAGE_USER_RW};

const ET_EXEC: u16 = 2;
const EM_AARCH64: u16 = 183;
const PT_LOAD: u32 = 1;
const PT_GNU_STACK: u32 = 0x6474e551;
const PF_X: u32 = 1;
const PF_W: u32 = 2;
const PF_R: u32 = 4;

#[repr(C)]
struct Elf64_Ehdr {
    e_ident:     [u8; 16],
    e_type:      u16,
    e_machine:   u16,
    e_version:   u32,
    e_entry:     u64,
    e_phoff:     u64,
    e_shoff:     u64,
    e_flags:     u32,
    e_ehsize:    u16,
    e_phentsize: u16,
    e_phnum:     u16,
    e_shentsize: u16,
    e_shnum:     u16,
    e_shstrndx:  u16,
}

#[repr(C)]
struct Elf64_Phdr {
    p_type:   u32,
    p_flags:  u32,
    p_offset: u64,
    p_vaddr:  u64,
    p_paddr:  u64,
    p_filesz: u64,
    p_memsz:  u64,
    p_align:  u64,
}

pub struct LoadedElf {
    pub entry: u64,
    pub stack_top: u64,
    pub pgd: u64,
    pub code_pa: u64,
    pub code_va: usize,
    pub code_end: usize,
    pub stack_pa: u64,
    pub stack_va: usize,
    pub stack_pages: u32,
    pub phdr_va: u64,
    pub phnum: u16,
}

struct Segment {
    vaddr:  u64,
    memsz:  u64,
    filesz: u64,
    offset: u64,
    flags:  u32,
}

fn validate_elf(data: &[u8]) -> Result<(), &'static str> {
    if data.len() < 64 { return Err("ELF too short"); }
    if data[0] != 0x7F || data[1] != b'E' || data[2] != b'L' || data[3] != b'F' {
        return Err("bad magic");
    }
    if data[4] != 2 { return Err("not ELF64"); }
    if data[5] != 1 { return Err("not LE"); }
    let ehdr: &Elf64_Ehdr = unsafe { &*(data.as_ptr() as *const Elf64_Ehdr) };
    if u16::from_le(ehdr.e_type) != ET_EXEC { return Err("not ET_EXEC"); }
    if u16::from_le(ehdr.e_machine) != EM_AARCH64 { return Err("not AArch64"); }
    Ok(())
}

fn parse_segments(data: &[u8]) -> Result<(Vec<Segment>, u64), &'static str> {
    let ehdr: &Elf64_Ehdr = unsafe { &*(data.as_ptr() as *const Elf64_Ehdr) };
    let entry = u64::from_le(ehdr.e_entry);
    let phoff = u64::from_le(ehdr.e_phoff) as usize;
    let phnum = u16::from_le(ehdr.e_phnum) as usize;
    let phentsz = u16::from_le(ehdr.e_phentsize) as usize;
    if phoff == 0 || phnum == 0 { return Err("no phdrs"); }
    let mut segs = Vec::new();
    for i in 0..phnum {
        let phdr: &Elf64_Phdr = unsafe { &*(data.as_ptr().add(phoff + i * phentsz) as *const Elf64_Phdr) };
        if u32::from_le(phdr.p_type) == PT_LOAD {
            let p_flags = u32::from_le(phdr.p_flags);
            let p_memsz = u64::from_le(phdr.p_memsz);
            if p_memsz == 0 { continue; }
            let p_filesz = u64::from_le(phdr.p_filesz);
            segs.push(Segment {
                vaddr: u64::from_le(phdr.p_vaddr),
                memsz: p_memsz,
                filesz: p_filesz,
                offset: u64::from_le(phdr.p_offset),
                flags: p_flags,
            });
            crate::println!("[ELF]   LOAD vaddr={:#x} memsz={:#x} filesz={:#x} flags={:#x}",
                u64::from_le(phdr.p_vaddr), p_memsz, p_filesz, p_flags);
        }
    }
    if segs.is_empty() { return Err("no PT_LOAD"); }
    Ok((segs, entry))
}

fn flags_to_page_prot(p_flags: u32) -> u64 {
    if (p_flags & PF_W) != 0 { PAGE_USER_RW }
    else { PAGE_USER_RX }
}

fn alloc_and_map_page(pgd: u64, va: u64, prot: u64) -> Result<u64, &'static str> {
    let pa = unsafe { ffi::alloc_phys_pages(0, 0) };
    if pa == 0 { return Err("alloc phys page failed"); }
    let kva = pgtbl::phys_to_virt(pa);
    unsafe { core::ptr::write_bytes(kva as *mut u8, 0, 4096); }
    let old_pgd = pgtbl::current_pgd();
    pgtbl::switch_page_table(pgd);
    let ret = pgtbl::map_page(va as usize, pa, prot);
    pgtbl::switch_page_table(old_pgd);
    match ret {
        Ok(()) => Ok(pa),
        Err(_) => { unsafe { ffi::free_phys_pages(pa, 0); } Err("map failed") }
    }
}

fn find_stack_top(segments: &[Segment]) -> u64 {
    let mut max_end = 0u64;
    for seg in segments {
        let end = seg.vaddr + seg.memsz;
        if end > max_end { max_end = end; }
    }
    // 栈放在代码段上方至少 1MB
    let candidate = (max_end + 0x100000 - 1) & !0xFFFFF;
    core::cmp::max(candidate, 0x80000000)
}

pub fn load_elf(elf_data: &[u8]) -> Result<LoadedElf, &'static str> {
    validate_elf(elf_data)?;
    let ehdr: &Elf64_Ehdr = unsafe { &*(elf_data.as_ptr() as *const Elf64_Ehdr) };
    let phdr_file_off = u64::from_le(ehdr.e_phoff);
    let phnum = u16::from_le(ehdr.e_phnum);
    let (segments, entry) = parse_segments(elf_data)?;
    crate::println!("[ELF] entry={:#x}, {} segments", entry, segments.len());

    let pgd = pgtbl::create_user_pgd().ok_or("create pgd failed")?;
    let mut first_code_pa = 0u64;
    let mut first_code_va = 0usize;
    let mut last_code_end = 0usize;

    for seg in &segments {
        let vaddr_start = seg.vaddr;
        let vaddr_end = (seg.vaddr + seg.memsz + 4095) & !4095;
        let prot = flags_to_page_prot(seg.flags);
        crate::println!("[ELF]   mapping {:#x}..{:#x} prot={:#x}", vaddr_start, vaddr_end, prot);

        let mut va = vaddr_start;
        while va < vaddr_end {
            let in_seg = va - seg.vaddr; // 页在段内的偏移
            let pa = alloc_and_map_page(pgd, va, prot)?;
            if first_code_pa == 0 {
                first_code_pa = pa;
                first_code_va = va as usize;
            }

            if in_seg < seg.filesz {
                // 从文件复制数据
                let foff = seg.offset + in_seg;
                let remaining = seg.filesz - in_seg;
                let copy = remaining.min(4096).min((elf_data.len() as u64).saturating_sub(foff)) as usize;
                if copy > 0 {
                    let kva = pgtbl::phys_to_virt(pa);
                    unsafe {
                        core::ptr::copy_nonoverlapping(elf_data.as_ptr().add(foff as usize), kva as *mut u8, copy);
                    }
                }
            }
            // else: BSS 区域（已按页清零，无需操作）

            va += 4096;
        }
        let end_va = (seg.vaddr + seg.memsz + 4095) & !4095;
        if (end_va as usize) > last_code_end { last_code_end = end_va as usize; }
    }

    const STACK_PAGES: u32 = 16;
    const STACK_SIZE: usize = STACK_PAGES as usize * 4096;
    let stack_top = find_stack_top(&segments);
    let stack_bottom = stack_top - STACK_SIZE as u64;
    crate::println!("[ELF]   stack {:#x}..{:#x}", stack_bottom, stack_top);

    let stack_pa = unsafe { ffi::alloc_phys_pages(STACK_PAGES.ilog2(), 0) };
    if stack_pa == 0 { return Err("alloc stack failed"); }
    let kstack_va = pgtbl::phys_to_virt(stack_pa);
    unsafe { core::ptr::write_bytes(kstack_va as *mut u8, 0, STACK_SIZE); }

    let old_pgd = pgtbl::current_pgd();
    pgtbl::switch_page_table(pgd);
    for i in 0..STACK_PAGES {
        let page_pa = stack_pa + (i as u64) * 4096;
        let page_va = stack_bottom + (i as u64) * 4096;
        if let Err(_) = pgtbl::map_page(page_va as usize, page_pa, PAGE_USER_RW) {
            pgtbl::switch_page_table(old_pgd);
            return Err("map stack failed");
        }
    }
    pgtbl::switch_page_table(old_pgd);

    // 计算 PHDR 虚拟地址（第一个段从文件偏移 0 映射到 VA start）
    let seg0_va = segments[0].vaddr;
    let seg0_file_off = segments[0].offset;
    let phdr_va = seg0_va + phdr_file_off - seg0_file_off;

    Ok(LoadedElf {
        entry, stack_top, pgd,
        code_pa: first_code_pa, code_va: first_code_va, code_end: last_code_end,
        stack_pa, stack_va: stack_top as usize, stack_pages: STACK_PAGES,
        phdr_va, phnum,
    })
}

/// 在用户栈顶写入 Linux AArch64 启动帧（argc, argv, envp, auxv）
/// 返回调整后的初始 SP（用户态 sp_el0）
pub fn setup_user_stack(
    stack_pa: u64, stack_top: u64, stack_pages: u32,
    entry: u64, phdr_va: u64, phnum: u16,
) -> usize {
    let kva = pgtbl::phys_to_virt(stack_pa) as *mut u64;
    let stack_bottom = stack_top - (stack_pages as u64) * 4096;
    let sp = ((stack_top - 128) & !15) as usize;
    let off = (sp as u64 - stack_bottom) as usize / 8;

    unsafe {
        // argc = 1
        core::ptr::write(kva.add(off),     1u64);
        // argv[0] → points to string at sp+112
        core::ptr::write(kva.add(off + 1), (sp + 112) as u64);
        // argv[1] = NULL
        core::ptr::write(kva.add(off + 2), 0u64);
        // envp = NULL
        core::ptr::write(kva.add(off + 3), 0u64);
        // auxv: AT_PHDR = 3
        core::ptr::write(kva.add(off + 4), 3u64);
        core::ptr::write(kva.add(off + 5), phdr_va);
        // AT_PHNUM = 5
        core::ptr::write(kva.add(off + 6), 5u64);
        core::ptr::write(kva.add(off + 7), phnum as u64);
        // AT_PAGESZ = 6
        core::ptr::write(kva.add(off + 8),  6u64);
        core::ptr::write(kva.add(off + 9),  4096u64);
        // AT_ENTRY = 9
        core::ptr::write(kva.add(off + 10), 9u64);
        core::ptr::write(kva.add(off + 11), entry);
        // AT_NULL
        core::ptr::write(kva.add(off + 12), 0u64);
        core::ptr::write(kva.add(off + 13), 0u64);
        // argv[0] string "/SHELL.ELF\0"
        let str_p = kva.add(off + 14) as *mut u8;
        for (i, &b) in b"/SHELL.ELF\0".iter().enumerate() {
            core::ptr::write(str_p.add(i), b);
        }
    }
    sp
}

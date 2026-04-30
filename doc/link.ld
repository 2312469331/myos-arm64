 .debug_info :
    {
        *(.debug_info .debug_info.*)
        debug_info_end = .;
    } > BOOT_PHYS
    
    .debug_abbrev :
    {
        *(.debug_abbrev .debug_abbrev.*)
        debug_abbrev_end = .;
    } > BOOT_PHYS
    
    .debug_line :
    {
        *(.debug_line .debug_line.*)
        debug_line_end = .;
    } > BOOT_PHYS
    
    .debug_str :
    {
        *(.debug_str .debug_str.*)
        debug_str_end = .;
    } > BOOT_PHYS
    
    .debug_aranges :
    {
        *(.debug_aranges .debug_aranges.*)
        debug_aranges_end = .;
    } > BOOT_PHYS
    
    .debug_ranges :
    {
        *(.debug_ranges .debug_ranges.*)
        debug_ranges_end = .;
    } > BOOT_PHYS
    
    .debug_loc :
    {
        *(.debug_loc .debug_loc.*)
        debug_loc_end = .;
    } > BOOT_PHYS
    
    .debug_frame :
    {
        *(.debug_frame .debug_frame.*)
        debug_frame_end = .;
    } > BOOT_PHYS
    
    .debug_gdb_scripts :
    {
        *(.debug_gdb_scripts .debug_gdb_scripts.*)
    } > BOOT_PHYS
    
    .debug_pubnames :
    {
        *(.debug_pubnames .debug_pubnames.*)
    } > BOOT_PHYS
    
    .debug_pubtypes :
    {
        *(.debug_pubtypes .debug_pubtypes.*)
    } > BOOT_PHYS
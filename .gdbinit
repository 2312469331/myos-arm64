set confirm off
set print pretty on
set print asm-demangle on
set disassemble-next-line on
set output-radix 16
display/i $pc
display/x $sp
display/x $x0

display/x $x29
display/x $x30
display/x $FAR_EL1      
display/x $ESR_EL1      
fs cmd

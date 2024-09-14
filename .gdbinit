set confirm off
set print pretty on
set print asm-demangle on
set disassemble-next-line on
set output-radix 16

display/i $pc
display/x $sp
display/x $x0
display/x $x1
display/x $x2
display/x $x3
display/x $x29
display/x $x30

display/x $FAR_EL1      
display/x $ESR_EL1      
display/x $AFSR0_EL1    
display/x $AFSR1_EL1

display/x $SCTLR_EL1   
display/x $TTBR0_EL1   
display/x $TTBR1_EL1   
display/x $MairEl1     
display/x $CONTEXTIDR_EL1 
b boot/boot.S:125
b boot/boot.S:146
target remote :1234
fs cmd

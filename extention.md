All available -march extensions for AArch64

    Name                Architecture Feature(s)                                Description
    aes                 FEAT_AES, FEAT_PMULL                                   Enable AES support
    bf16                FEAT_BF16                                              Enable BFloat16 Extension
    brbe                FEAT_BRBE                                              Enable Branch Record Buffer Extension
    bti                 FEAT_BTI                                               Enable Branch Target Identification
    cmpbr               FEAT_CMPBR                                             Enable Armv9.6-A base compare and branch instructions
    fcma                FEAT_FCMA                                              Enable Armv8.3-A Floating-point complex number support
    cpa                 FEAT_CPA                                               Enable Armv9.5-A Checked Pointer Arithmetic
    crc                 FEAT_CRC32                                             Enable Armv8.0-A CRC-32 checksum instructions
    crypto              FEAT_Crypto                                            Enable cryptographic instructions
    cssc                FEAT_CSSC                                              Enable Common Short Sequence Compression (CSSC) instructions
    d128                FEAT_D128, FEAT_LVA3, FEAT_SYSREG128, FEAT_SYSINSTR128 Enable Armv9.4-A 128-bit Page Table Descriptors, System Registers and instructions
    dit                 FEAT_DIT                                               Enable Armv8.4-A Data Independent Timing instructions
    dotprod             FEAT_DotProd                                           Enable dot product support
    f32mm               FEAT_F32MM                                             Enable Matrix Multiply FP32 Extension
    f64mm               FEAT_F64MM                                             Enable Matrix Multiply FP64 Extension
    f8f16mm             FEAT_F8F16MM                                           Enable Armv9.6-A FP8 to Half-Precision Matrix Multiplication
    f8f32mm             FEAT_F8F32MM                                           Enable Armv9.6-A FP8 to Single-Precision Matrix Multiplication
    faminmax            FEAT_FAMINMAX                                          Enable FAMIN and FAMAX instructions
    flagm               FEAT_FlagM                                             Enable Armv8.4-A Flag Manipulation instructions
    fp                  FEAT_FP                                                Enable Armv8.0-A Floating Point Extensions
    fp16fml             FEAT_FHM                                               Enable FP16 FML instructions
    fp8                 FEAT_FP8                                               Enable FP8 instructions
    fp8dot2             FEAT_FP8DOT2                                           Enable FP8 2-way dot instructions
    fp8dot4             FEAT_FP8DOT4                                           Enable FP8 4-way dot instructions
    fp8fma              FEAT_FP8FMA                                            Enable Armv9.5-A FP8 multiply-add instructions
    fprcvt              FEAT_FPRCVT                                            Enable Armv9.6-A base convert instructions for SIMD&FP scalar register operands of different input and output sizes
    fp16                FEAT_FP16                                              Enable half-precision floating-point data processing
    gcs                 FEAT_GCS                                               Enable Armv9.4-A Guarded Call Stack Extension
    hbc                 FEAT_HBC                                               Enable Armv8.8-A Hinted Conditional Branches Extension
    i8mm                FEAT_I8MM                                              Enable Matrix Multiply Int8 Extension
    ite                 FEAT_ITE                                               Enable Armv9.4-A Instrumentation Extension
    jscvt               FEAT_JSCVT                                             Enable Armv8.3-A JavaScript FP conversion instructions
    ls64                FEAT_LS64, FEAT_LS64_V, FEAT_LS64_ACCDATA              Enable Armv8.7-A LD64B/ST64B Accelerator Extension
    lse                 FEAT_LSE                                               Enable Armv8.1-A Large System Extension (LSE) atomic instructions
    lse128              FEAT_LSE128                                            Enable Armv9.4-A 128-bit Atomic instructions
    lsfe                FEAT_LSFE                                              Enable Armv9.6-A base Atomic floating-point in-memory instructions
    lsui                FEAT_LSUI                                              Enable Armv9.6-A unprivileged load/store instructions
    lut                 FEAT_LUT                                               Enable Lookup Table instructions
    mops                FEAT_MOPS                                              Enable Armv8.8-A memcpy and memset acceleration instructions
    memtag              FEAT_MTE, FEAT_MTE2                                    Enable Memory Tagging Extension
    simd                FEAT_AdvSIMD                                           Enable Advanced SIMD instructions
    occmo               FEAT_OCCMO                                             Enable Armv9.6-A Outer cacheable cache maintenance operations
    pauth               FEAT_PAuth                                             Enable Armv8.3-A Pointer Authentication extension
    pauth-lr            FEAT_PAuth_LR                                          Enable Armv9.5-A PAC enhancements
    pcdphint            FEAT_PCDPHINT                                          Enable Armv9.6-A Producer Consumer Data Placement hints
    pmuv3               FEAT_PMUv3                                             Enable Armv8.0-A PMUv3 Performance Monitors extension
    pops                FEAT_PoPS                                              Enable Armv9.6-A Point Of Physical Storage (PoPS) DC instructions
    predres             FEAT_SPECRES                                           Enable Armv8.5-A execution and data prediction invalidation instructions
    rng                 FEAT_RNG                                               Enable Random Number generation instructions
    ras                 FEAT_RAS, FEAT_RASv1p1                                 Enable Armv8.0-A Reliability, Availability and Serviceability Extensions
    rasv2               FEAT_RASv2                                             Enable Armv8.9-A Reliability, Availability and Serviceability Extensions
    rcpc                FEAT_LRCPC                                             Enable support for RCPC extension
    rcpc3               FEAT_LRCPC3                                            Enable Armv8.9-A RCPC instructions for A64 and Advanced SIMD and floating-point instruction set
    rdm                 FEAT_RDM                                               Enable Armv8.1-A Rounding Double Multiply Add/Subtract instructions
    sb                  FEAT_SB                                                Enable Armv8.5-A Speculation Barrier
    sha2                FEAT_SHA1, FEAT_SHA256                                 Enable SHA1 and SHA256 support
    sha3                FEAT_SHA3, FEAT_SHA512                                 Enable SHA512 and SHA3 support
    sm4                 FEAT_SM4, FEAT_SM3                                     Enable SM3 and SM4 support
    sme                 FEAT_SME                                               Enable Scalable Matrix Extension (SME)
    sme-b16b16          FEAT_SME_B16B16                                        Enable SME2.1 ZA-targeting non-widening BFloat16 instructions
    sme-f16f16          FEAT_SME_F16F16                                        Enable SME non-widening Float16 instructions
    sme-f64f64          FEAT_SME_F64F64                                        Enable Scalable Matrix Extension (SME) F64F64 instructions
    sme-f8f16           FEAT_SME_F8F16                                         Enable Scalable Matrix Extension (SME) F8F16 instructions
    sme-f8f32           FEAT_SME_F8F32                                         Enable Scalable Matrix Extension (SME) F8F32 instructions
    sme-fa64            FEAT_SME_FA64                                          Enable the full A64 instruction set in streaming SVE mode
    sme-i16i64          FEAT_SME_I16I64                                        Enable Scalable Matrix Extension (SME) I16I64 instructions
    sme-lutv2           FEAT_SME_LUTv2                                         Enable Scalable Matrix Extension (SME) LUTv2 instructions
    sme-mop4            FEAT_SME_MOP4                                          Enable SME Quarter-tile outer product instructions
    sme-tmop            FEAT_SME_TMOP                                          Enable SME Structured sparsity outer product instructions.
    sme2                FEAT_SME2                                              Enable Scalable Matrix Extension 2 (SME2) instructions
    sme2p1              FEAT_SME2p1                                            Enable Scalable Matrix Extension 2.1 instructions
    sme2p2              FEAT_SME2p2                                            Enable Armv9.6-A Scalable Matrix Extension 2.2 instructions
    profile             FEAT_SPE                                               Enable Statistical Profiling extension
    predres2            FEAT_SPECRES2                                          Enable Speculation Restriction Instruction
    ssbs                FEAT_SSBS, FEAT_SSBS2                                  Enable Speculative Store Bypass Safe bit
    ssve-aes            FEAT_SSVE_AES                                          Enable Armv9.6-A SVE AES support in streaming SVE mode
    ssve-bitperm        FEAT_SSVE_BitPerm                                      Enable Armv9.6-A SVE BitPerm support in streaming SVE mode
    ssve-fexpa          FEAT_SSVE_FEXPA                                        Enable SVE FEXPA instruction in Streaming SVE mode
    ssve-fp8dot2        FEAT_SSVE_FP8DOT2                                      Enable SVE2 FP8 2-way dot product instructions
    ssve-fp8dot4        FEAT_SSVE_FP8DOT4                                      Enable SVE2 FP8 4-way dot product instructions
    ssve-fp8fma         FEAT_SSVE_FP8FMA                                       Enable SVE2 FP8 multiply-add instructions
    sve                 FEAT_SVE                                               Enable Scalable Vector Extension (SVE) instructions
    sve-aes             FEAT_SVE_AES, FEAT_SVE_PMULL128                        Enable SVE AES and quadword SVE polynomial multiply instructions
    sve-aes2            FEAT_SVE_AES2                                          Enable Armv9.6-A SVE multi-vector AES and multi-vector quadword polynomial multiply instructions
    sve-b16b16          FEAT_SVE_B16B16                                        Enable SVE2 non-widening and SME2 Z-targeting non-widening BFloat16 instructions
    sve-bfscale         FEAT_SVE_BFSCALE                                       Enable Armv9.6-A SVE BFloat16 scaling instructions
    sve-bitperm         FEAT_SVE_BitPerm                                       Enable bit permutation SVE2 instructions
    sve-f16f32mm        FEAT_SVE_F16F32MM                                      Enable Armv9.6-A FP16 to FP32 Matrix Multiply instructions
    sve-sha3            FEAT_SVE_SHA3                                          Enable SVE SHA3 instructions
    sve-sm4             FEAT_SVE_SM4                                           Enable SVE SM4 instructions
    sve2                FEAT_SVE2                                              Enable Scalable Vector Extension 2 (SVE2) instructions
    sve2-aes                                                                   Shorthand for +sve2+sve-aes
    sve2-bitperm                                                               Shorthand for +sve2+sve-bitperm
    sve2-sha3                                                                  Shorthand for +sve2+sve-sha3
    sve2-sm4                                                                   Shorthand for +sve2+sve-sm4
    sve2p1              FEAT_SVE2p1                                            Enable Scalable Vector Extension 2.1 instructions
    sve2p2              FEAT_SVE2p2                                            Enable Armv9.6-A Scalable Vector Extension 2.2 instructions
    the                 FEAT_THE                                               Enable Armv8.9-A Translation Hardening Extension
    tlbiw               FEAT_TLBIW                                             Enable Armv9.5-A TLBI VMALL for Dirty State
    tme                 FEAT_TME                                               Enable Transactional Memory Extension
    wfxt                FEAT_WFxT                                              Enable Armv8.7-A WFET and WFIT instruction

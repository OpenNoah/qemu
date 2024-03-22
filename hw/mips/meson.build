mips_ss = ss.source_set()
mips_ss.add(files('bootloader.c', 'mips_int.c'))
mips_ss.add(when: 'CONFIG_FW_CFG_MIPS', if_true: files('fw_cfg.c'))
mips_ss.add(when: 'CONFIG_LOONGSON3V', if_true: files('loongson3_bootp.c', 'loongson3_virt.c'))
mips_ss.add(when: 'CONFIG_MALTA', if_true: files('malta.c'))
mips_ss.add(when: 'CONFIG_IRIVER_D88', if_true: files('iriver_d88.c'))
mips_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('cps.c'))
mips_ss.add(when: 'CONFIG_INGENIC_JZ4755', if_true: files('ingenic_jz4755.c'))

if 'CONFIG_TCG' in config_all
mips_ss.add(when: 'CONFIG_JAZZ', if_true: files('jazz.c'))
mips_ss.add(when: 'CONFIG_MIPSSIM', if_true: files('mipssim.c'))
mips_ss.add(when: 'CONFIG_FULOONG', if_true: files('fuloong2e.c'))
mips_ss.add(when: 'CONFIG_MIPS_BOSTON', if_true: [files('boston.c'), fdt])
endif

hw_arch += {'mips': mips_ss}

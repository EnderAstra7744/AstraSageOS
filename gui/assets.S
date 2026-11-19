/* The images embedded in the kernel (assets folder, see assets/README.md). */
    .section .rodata
    .balign 4
    .globl asset_boot_logo_start, asset_boot_logo_end
asset_boot_logo_start:
    .incbin "assets/boot_logo.bmp"
asset_boot_logo_end:
    .balign 4
    .globl asset_ast_icon_start, asset_ast_icon_end
asset_ast_icon_start:
    .incbin "assets/ast_icon.bmp"
asset_ast_icon_end:
    .balign 4
    .globl asset_as_logo_start, asset_as_logo_end
asset_as_logo_start:
    .incbin "assets/as_logo.bmp"
asset_as_logo_end:
    .balign 4
    .balign 4
    .balign 4
    .globl asset_nhello_start, asset_nhello_end
asset_nhello_start:
    .incbin "assets/nhello.elf"
asset_nhello_end:
    .balign 4
    .globl asset_sf1_start, asset_sf1_end, asset_sf2_start, asset_sf2_end, asset_sf3_start, asset_sf3_end
asset_sf1_start:
    .incbin "assets/store_froggy1.bmp"
asset_sf1_end:
    .balign 4
asset_sf2_start:
    .incbin "assets/store_froggy2.bmp"
asset_sf2_end:
    .balign 4
asset_sf3_start:
    .incbin "assets/store_froggy3.bmp"
asset_sf3_end:
    .balign 4
    .globl asset_nlibc_start, asset_nlibc_end
asset_nlibc_start:
    .incbin "assets/nlibc.elf"
asset_nlibc_end:
    .balign 4
    .balign 4
    .globl asset_sn1_start, asset_sn1_end, asset_sn2_start, asset_sn2_end, asset_sn3_start, asset_sn3_end
asset_sn1_start:
    .incbin "assets/store_netsurf1.bmp"
asset_sn1_end:
    .balign 4
asset_sn2_start:
    .incbin "assets/store_netsurf2.bmp"
asset_sn2_end:
    .balign 4
asset_sn3_start:
    .incbin "assets/store_netsurf3.bmp"
asset_sn3_end:
    .balign 4
    .globl asset_roots_start, asset_roots_end
asset_roots_start:
    .incbin "assets/roots.bin"
asset_roots_end:

    .balign 4
    .globl asset_ic_froggy_start, asset_ic_froggy_end, asset_ic_netsurf_start, asset_ic_netsurf_end, asset_ic_script_start, asset_ic_script_end
asset_ic_froggy_start:
    .incbin "assets/froggycode_icon.bmp"
asset_ic_froggy_end:
    .balign 4
asset_ic_netsurf_start:
    .incbin "assets/netsurf_icon.bmp"
asset_ic_netsurf_end:
    .balign 4
asset_ic_script_start:
    .incbin "assets/astrascript_logo.bmp"
asset_ic_script_end:
    .balign 4
    .balign 4
    .globl asset_wp0_start, asset_wp0_end
asset_wp0_start:
    .incbin "assets/wp_0.wpz"
asset_wp0_end:
    .balign 4
    .globl asset_wpt0_start, asset_wpt0_end
asset_wpt0_start:
    .incbin "assets/wpt_0.wpz"
asset_wpt0_end:
    .balign 4
    .globl asset_wp1_start, asset_wp1_end
asset_wp1_start:
    .incbin "assets/wp_1.wpz"
asset_wp1_end:
    .balign 4
    .globl asset_wpt1_start, asset_wpt1_end
asset_wpt1_start:
    .incbin "assets/wpt_1.wpz"
asset_wpt1_end:
    .balign 4
    .globl asset_wp2_start, asset_wp2_end
asset_wp2_start:
    .incbin "assets/wp_2.wpz"
asset_wp2_end:
    .balign 4
    .globl asset_wpt2_start, asset_wpt2_end
asset_wpt2_start:
    .incbin "assets/wpt_2.wpz"
asset_wpt2_end:
    .balign 4
    .globl asset_wp3_start, asset_wp3_end
asset_wp3_start:
    .incbin "assets/wp_3.wpz"
asset_wp3_end:
    .balign 4
    .globl asset_wpt3_start, asset_wpt3_end
asset_wpt3_start:
    .incbin "assets/wpt_3.wpz"
asset_wpt3_end:
    .balign 4
    .globl asset_wp4_start, asset_wp4_end
asset_wp4_start:
    .incbin "assets/wp_4.wpz"
asset_wp4_end:
    .balign 4
    .globl asset_wpt4_start, asset_wpt4_end
asset_wpt4_start:
    .incbin "assets/wpt_4.wpz"
asset_wpt4_end:
    .balign 4
    .globl asset_wp5_start, asset_wp5_end
asset_wp5_start:
    .incbin "assets/wp_5.wpz"
asset_wp5_end:
    .balign 4
    .globl asset_wpt5_start, asset_wpt5_end
asset_wpt5_start:
    .incbin "assets/wpt_5.wpz"
asset_wpt5_end:
    .balign 4
    .globl asset_wp6_start, asset_wp6_end
asset_wp6_start:
    .incbin "assets/wp_6.wpz"
asset_wp6_end:
    .balign 4
    .globl asset_wpt6_start, asset_wpt6_end
asset_wpt6_start:
    .incbin "assets/wpt_6.wpz"
asset_wpt6_end:
    .balign 4
    .globl asset_wp7_start, asset_wp7_end
asset_wp7_start:
    .incbin "assets/wp_7.wpz"
asset_wp7_end:
    .balign 4
    .globl asset_wpt7_start, asset_wpt7_end
asset_wpt7_start:
    .incbin "assets/wpt_7.wpz"
asset_wpt7_end:
    .balign 4
    .globl asset_wp8_start, asset_wp8_end
asset_wp8_start:
    .incbin "assets/wp_8.wpz"
asset_wp8_end:
    .balign 4
    .globl asset_wpt8_start, asset_wpt8_end
asset_wpt8_start:
    .incbin "assets/wpt_8.wpz"
asset_wpt8_end:
    .balign 4
    .globl asset_wp9_start, asset_wp9_end
asset_wp9_start:
    .incbin "assets/wp_9.wpz"
asset_wp9_end:
    .balign 4
    .globl asset_wpt9_start, asset_wpt9_end
asset_wpt9_start:
    .incbin "assets/wpt_9.wpz"
asset_wpt9_end:
    .balign 4
    .globl asset_wp10_start, asset_wp10_end
asset_wp10_start:
    .incbin "assets/wp_10.wpz"
asset_wp10_end:
    .balign 4
    .globl asset_wpt10_start, asset_wpt10_end
asset_wpt10_start:
    .incbin "assets/wpt_10.wpz"
asset_wpt10_end:

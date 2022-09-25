
#pragma once

#if defined(BUILD_MONOLITHIC)

#ifdef __cplusplus
extern "C" {
#endif

extern int jpegXL_box_list_main(int argc, const char** argv);
extern int jpegXL_butteraugli_main(int argc, const char** argv);
extern int jpegXL_compress_main(int argc, const char** argv);
extern int jpegXL_cjpeg_hdr_main(int argc, const char** argv);
extern int jpegXL_dec_enc_main(int argc, const char** argv);
extern int jpegXL_decode_oneshot_main(int argc, const char** argv);
extern int jpegXL_decompress_main(int argc, const char** argv);
extern int jpegXL_emcc_main(int argc, const char** argv);
extern int jpegXL_exr_to_pq_main(int argc, const char** argv);
extern int jpegXL_encode_oneshot_main(int argc, const char** argv);
extern int jpegXL_from_tree_main(int argc, const char** argv);
extern int jpegXL_info_main(int argc, const char** argv);
extern int jpegXL_decode_EXIF_metadata_main(int argc, const char** argv);
extern int jpegXL_decode_progressive_main(int argc, const char** argv);
extern int jpegXL_ssimulacra_main(int argc, const char** argv);
extern int jpegXL_ssimulacra2_main(int argc, const char** argv);
extern int jpegXL_xyb_range_main(int argc, const char** argv);
extern int jpegXL_tone_map_main(int argc, const char** argv);
extern int jpegXL_generate_LUT_template_main(int argc, const char** argv);
extern int jpegXL_pq_to_hlg_main(int argc, const char** argv);
extern int jpegXL_render_hlg_main(int argc, const char** argv);
extern int jpegXL_texture_to_cube_main(int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif

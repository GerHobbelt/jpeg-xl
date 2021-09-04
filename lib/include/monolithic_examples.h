
#pragma once

#if defined(BUILD_MONOLITHIC)

#ifdef __cplusplus
extern "C" {
#endif

extern int jpegXL_box_list_main(int argc, const char** argv);
extern int jpegXL_butteraugli_main(int argc, const char** argv);
extern int jpegXL_C_test_main(int argc, const char** argv);
extern int jpegXL_compress_main(int argc, const char** argv);
extern int jpegXL_conformance_main(int argc, const char** argv);
extern int jpegXL_dec_enc_main(int argc, const char** argv);
extern int jpegXL_decode_oneshot_main(int argc, const char** argv);
extern int jpegXL_decompress_main(int argc, const char** argv);
extern int jpegXL_encode_oneshot_main(int argc, const char** argv);
extern int jpegXL_epf_main(int argc, const char** argv);
extern int jpegXL_from_tree_main(int argc, const char** argv);
extern int jpegXL_info_main(int argc, const char** argv);
extern int jpegXL_ssimulacra_main(int argc, const char** argv);
extern int jpegXL_ssimulacra_openCV_main(int argc, const char** argv);
extern int jpegXL_xyb_range_main(int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif

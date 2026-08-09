#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt1.o%s %D/crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%D/crtend.o%s"

#undef LIB_SPEC
#define LIB_SPEC "-lc -lssp_nonshared -lssp -lpthread -lm -lutil"

#undef LINK_SPEC
#define LINK_SPEC "-nostdlib -static -z max-page-size=0x1000"

#undef STANDARD_STARTFILE_PREFIX
#define STANDARD_STARTFILE_PREFIX "/usr/local/lib/"

#undef NATIVE_SYSTEM_HEADER_DIR
#define NATIVE_SYSTEM_HEADER_DIR "/usr/local/include"
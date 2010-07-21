# Copyright © 2010
#	Thorsten Glaser <t.glaser@tarent.de>
# This file is provided under the same terms as mksh.

LOCAL_PATH:=		$(call my-dir)


# /system/etc/mkshrc

include $(CLEAR_VARS)

LOCAL_MODULE:=		mkshrc
LOCAL_MODULE_TAGS:=	shell_mksh
LOCAL_MODULE_CLASS:=	ETC
LOCAL_MODULE_PATH:=	$(TARGET_OUT)/etc
LOCAL_SRC_FILES:=	$(LOCAL_MODULE)
include $(BUILD_PREBUILT)


# /system/bin/mksh

include $(CLEAR_VARS)

LOCAL_MODULE:=		mksh
LOCAL_MODULE_TAGS:=	shell_mksh

# mksh source files
LOCAL_SRC_FILES:=	src/lalloc.c src/edit.c src/eval.c src/exec.c \
			src/expr.c src/funcs.c src/histrap.c src/jobs.c \
			src/lex.c src/main.c src/misc.c src/shf.c \
			src/syn.c src/tree.c src/var.c

LOCAL_SYSTEM_SHARED_LIBRARIES:= libc

LOCAL_C_INCLUDES:=	$(LOCAL_PATH)/src
# additional flags first, then from Makefrag.inc: CFLAGS, CPPFLAGS
LOCAL_CFLAGS:=		-DMKSH_DEFAULT_EXECSHELL=\"/system/bin/sh\" \
			-DMKSH_DEFAULT_TMPDIR=\"/sqlite_stmt_journals\" \
			-DMKSHRC_PATH=\"/system/etc/mkshrc\" \
			-fwrapv \
			-DMKSH_ASSUME_UTF8=0 -DMKSH_NOPWNAM \
			-D_GNU_SOURCE \
			-DHAVE_ATTRIBUTE_BOUNDED=0 -DHAVE_ATTRIBUTE_FORMAT=1 \
			-DHAVE_ATTRIBUTE_NONNULL=1 -DHAVE_ATTRIBUTE_NORETURN=1 \
			-DHAVE_ATTRIBUTE_UNUSED=1 -DHAVE_ATTRIBUTE_USED=1 \
			-DHAVE_SYS_PARAM_H=1 -DHAVE_SYS_MKDEV_H=0 \
			-DHAVE_SYS_MMAN_H=1 -DHAVE_SYS_SYSMACROS_H=1 \
			-DHAVE_LIBGEN_H=1 -DHAVE_LIBUTIL_H=0 -DHAVE_PATHS_H=1 \
			-DHAVE_STDBOOL_H=1 -DHAVE_STRINGS_H=1 -DHAVE_GRP_H=1 \
			-DHAVE_ULIMIT_H=0 -DHAVE_VALUES_H=0 -DHAVE_STDINT_H=1 \
			-DHAVE_RLIM_T=1 -DHAVE_SIG_T=1 -DHAVE_SYS_SIGNAME=1 \
			-DHAVE_SYS_SIGLIST=1 -DHAVE_STRSIGNAL=0 \
			-DHAVE_GETRUSAGE=1 -DHAVE_KILLPG=1 -DHAVE_MKNOD=0 \
			-DHAVE_MKSTEMP=1 -DHAVE_NICE=1 -DHAVE_REVOKE=0 \
			-DHAVE_SETLOCALE_CTYPE=0 -DHAVE_LANGINFO_CODESET=0 \
			-DHAVE_SETMODE=1 -DHAVE_SETRESUGID=1 \
			-DHAVE_SETGROUPS=1 -DHAVE_STRCASESTR=1 \
			-DHAVE_STRLCPY=1 -DHAVE_FLOCK_DECL=1 \
			-DHAVE_REVOKE_DECL=1 -DHAVE_SYS_SIGLIST_DECL=1 \
			-DHAVE_PERSISTENT_HISTORY=0

include $(BUILD_EXECUTABLE)

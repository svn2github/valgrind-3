include $(top_srcdir)/Makefile.tool.am

noinst_PROGRAMS = 
if VGP_X86_LINUX
noinst_PROGRAMS += exp-sgcheck-x86-linux vgpreload_exp-sgcheck-x86-linux.so
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += exp-sgcheck-amd64-linux vgpreload_exp-sgcheck-amd64-linux.so
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += exp-sgcheck-ppc32-linux vgpreload_exp-sgcheck-ppc32-linux.so
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += exp-sgcheck-ppc64-linux vgpreload_exp-sgcheck-ppc64-linux.so
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += exp-sgcheck-ppc32-aix5 vgpreload_exp-sgcheck-ppc32-aix5.so
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += exp-sgcheck-ppc64-aix5 vgpreload_exp-sgcheck-ppc64-aix5.so
endif


VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON = sg_intercepts.c

vgpreload_exp_sgcheck_x86_linux_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_exp_sgcheck_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_x86_linux_so_CCASFLAGS    = $(AM_CCASFLAGS_X86_LINUX)
vgpreload_exp_sgcheck_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX)
vgpreload_exp_sgcheck_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX)

vgpreload_exp_sgcheck_amd64_linux_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_exp_sgcheck_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_amd64_linux_so_CCASFLAGS    = $(AM_CCASFLAGS_AMD64_LINUX)
vgpreload_exp_sgcheck_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX)
vgpreload_exp_sgcheck_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX)

vgpreload_exp_sgcheck_ppc32_linux_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_exp_sgcheck_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_ppc32_linux_so_CCASFLAGS    = $(AM_CCASFLAGS_PPC32_LINUX)
vgpreload_exp_sgcheck_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX)
vgpreload_exp_sgcheck_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX)

vgpreload_exp_sgcheck_ppc64_linux_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_exp_sgcheck_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_ppc64_linux_so_CCASFLAGS    = $(AM_CCASFLAGS_PPC64_LINUX)
vgpreload_exp_sgcheck_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX)
vgpreload_exp_sgcheck_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX)

vgpreload_exp_sgcheck_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_exp_sgcheck_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_ppc32_aix5_so_CCASFLAGS    = $(AM_CCASFLAGS_PPC32_AIX5)
vgpreload_exp_sgcheck_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5)
vgpreload_exp_sgcheck_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5)

vgpreload_exp_sgcheck_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_EXP_SGCHECK_SOURCES_COMMON)
vgpreload_exp_sgcheck_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_exp_sgcheck_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC) -O2
vgpreload_exp_sgcheck_ppc64_aix5_so_CCASFLAGS    = $(AM_CCASFLAGS_PPC64_AIX5)
vgpreload_exp_sgcheck_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5)
vgpreload_exp_sgcheck_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5)



EXP_SGCHECK_SOURCES_COMMON = sg_main.c

exp_sgcheck_x86_linux_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
exp_sgcheck_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX)
exp_sgcheck_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
exp_sgcheck_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
exp_sgcheck_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

exp_sgcheck_amd64_linux_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
exp_sgcheck_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX)
exp_sgcheck_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
exp_sgcheck_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
exp_sgcheck_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

exp_sgcheck_ppc32_linux_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
exp_sgcheck_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX)
exp_sgcheck_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
exp_sgcheck_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
exp_sgcheck_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

exp_sgcheck_ppc64_linux_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
exp_sgcheck_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX)
exp_sgcheck_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
exp_sgcheck_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
exp_sgcheck_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

exp_sgcheck_ppc32_aix5_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
exp_sgcheck_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5)
exp_sgcheck_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
exp_sgcheck_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
exp_sgcheck_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

exp_sgcheck_ppc64_aix5_SOURCES      = $(EXP_SGCHECK_SOURCES_COMMON)
exp_sgcheck_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
exp_sgcheck_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5)
exp_sgcheck_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
exp_sgcheck_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
exp_sgcheck_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

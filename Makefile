CC=gcc
BYTE_ORDER=__LITTLE_ENDIAN__
UNAME=Linux
#
# QuakeWorld/MVDSV and QWDTools Makefile for Linux and SunOS
#
# GNU Make required
#
# ELF only
#
#	$Id: Makefile.GNU,v 1.9 2007/03/05 17:29:07 vvd0 Exp $
#

MAINDIR=.
SV_DIR=$(MAINDIR)/source
QWDTOOLS_DIR=$(MAINDIR)/source/qwdtools

BASE_CFLAGS=-m32 -O3 -Wall -pthread -pipe -funsigned-char -DUSE_PR2 -D$(BYTE_ORDER)Q__ -DSERVERONLY -DUSE_PR2 -fno-strict-aliasing -ffast-math -funroll-loops
WITH_OPTIMIZED_CFLAGS=NO

USE_ASM=-Did386
ifeq ($(WITH_OPTIMIZED_CFLAGS),YES)
ifneq (,$(findstring 86,$(shell uname -m)))
ifeq (,$(findstring 64,$(shell uname -m)))
ifneq ($(UNAME),Darwin)
ifneq ($(UNAME),MacOSX)
ASM=$(USE_ASM)
endif
endif
endif
endif
CFLAGS=$(ASM) $(BASE_CFLAGS) -fno-strict-aliasing -ffast-math -funroll-loops
else
CFLAGS=$(BASE_CFLAGS)
endif

LDFLAGS=-Wl,--no-as-needed -lm -ldl
ifeq ($(UNAME),Linux)
LDFLAGS+=
else
ifeq ($(UNAME),SunOS)
LDFLAGS+= -lsocket -lnsl
CFLAGS+= -DBSD_COMP
endif
endif

ifeq ($(CC_BASEVERSION),4) # if gcc4 then build universal binary
ifeq ($(UNAME),Darwin)
CFLAGS+= -arch ppc
endif
ifeq ($(UNAME),MacOSX)
CFLAGS+= -arch ppc
endif
endif

#############################################################################
# SERVER
#############################################################################

SV_OBJS = \
		$(SV_DIR)/pr_cmds.o \
		$(SV_DIR)/pr_edict.o \
		$(SV_DIR)/pr_exec.o \
\
		$(SV_DIR)/pr2_cmds.o \
		$(SV_DIR)/pr2_edict.o \
		$(SV_DIR)/pr2_exec.o \
		$(SV_DIR)/pr2_vm.o \
\
		$(SV_DIR)/sv_ccmds.o \
		$(SV_DIR)/sv_demo.o \
		$(SV_DIR)/sv_demo_qtv.o \
		$(SV_DIR)/sv_demo_misc.o \
		$(SV_DIR)/sv_ents.o \
		$(SV_DIR)/sv_init.o \
		$(SV_DIR)/sv_login.o \
		$(SV_DIR)/sv_main.o \
		$(SV_DIR)/sv_master.o \
		$(SV_DIR)/sv_mod_frags.o \
		$(SV_DIR)/sv_move.o \
		$(SV_DIR)/sv_nchan.o \
		$(SV_DIR)/sv_phys.o \
		$(SV_DIR)/sv_send.o \
		$(SV_DIR)/sv_sys_unix.o \
		$(SV_DIR)/sv_user.o \
\
		$(SV_DIR)/bothtools.o \
		$(SV_DIR)/cmd.o \
		$(SV_DIR)/common.o \
		$(SV_DIR)/cmodel.o \
		$(SV_DIR)/crc.o \
		$(SV_DIR)/cvar.o \
		$(SV_DIR)/fs.o \
		$(SV_DIR)/mathlib.o \
		$(SV_DIR)/md4.o \
		$(SV_DIR)/net_chan.o \
		$(SV_DIR)/net.o \
		$(SV_DIR)/pmove.o \
		$(SV_DIR)/pmovetst.o \
		$(SV_DIR)/sha1.o \
		$(SV_DIR)/version.o \
		$(SV_DIR)/world.o \
		$(SV_DIR)/zone.o \
		$(SV_DIR)/sv_timer.o \
		$(SV_DIR)/sv_rcon.o \
\
		$(SV_DIR)/pcre/get.o \
		$(SV_DIR)/pcre/pcre.o 


ifeq ($(USE_ASM),$(ASM))
SV_ASM_OBJS = \
		$(SV_DIR)/bothtoolsa.o \
		$(SV_DIR)/math.o
endif

#############################################################################
# QWDTOOLS
#############################################################################

QWDTOOLS_OBJS = \
		$(SV_DIR)/bothtools.o \
		$(QWDTOOLS_DIR)/dem_parse.o \
		$(QWDTOOLS_DIR)/dem_send.o \
		$(QWDTOOLS_DIR)/ini.o \
		$(QWDTOOLS_DIR)/init.o \
		$(QWDTOOLS_DIR)/main.o \
		$(QWDTOOLS_DIR)/marge.o \
		$(QWDTOOLS_DIR)/qwz.o \
		$(QWDTOOLS_DIR)/sync.o \
		$(QWDTOOLS_DIR)/tools.o

ifeq ($(USE_ASM),$(ASM))
QWDTOOLS_ASM_OBJS = \
		$(SV_DIR)/bothtoolsa.o
endif

#############################################################################
# SETUP AND BUILD
#############################################################################

.c.o :
	$(CC) $(CFLAGS) -c $< -o $@

.s.o :
	$(CC) $(CFLAGS) -DELF -x assembler-with-cpp -c $< -o $@

mvdsv : $(SV_OBJS) $(SV_ASM_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o mvdsv $^

qwdtools : $(QWDTOOLS_OBJS) $(QWDTOOLS_ASM_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o qwdtools $^

clean : 
	-rm -f $(SV_DIR)/core $(SV_DIR)/*.o $(SV_DIR)/pcre/*.o $(QWDTOOLS_DIR)/*.o

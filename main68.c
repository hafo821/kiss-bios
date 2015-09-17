/* main68.c  --  main program started by booting the 68000 system  */
/*
	Copyright (C) 2011-2015 John R. Coffman.
	Licensed for hobbyist use on the N8VEM baby M68k CPU board.
***********************************************************************

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    in the file COPYING in the distribution directory along with this
    program.  If not, see <http://www.gnu.org/licenses/>.

**********************************************************************/
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "mytypes.h"
#include "packer.h"
#include "mfpic.h"
#include "dosdisk.h"
#include "ide.h"
#include "elf.h"
#include "coff.h"
#include "main68.h"
#include "bioscall.h"
#if !RETAIL
#include "debug.h"
#endif
#include "version.h"
#include "cout.h"
#include "ff.h"

void *memset(void *s, int c, size_t n);
int sio_get(void);
int _con_out(char);
void _run_us_mode(word mode, void *pc);

extern byte location_zero;
const char msg_welcome[] =
    "\r\n\r\n"
#if M68000==68030
    "        Welcome to the KISS-68030 System" "\r\n\r\n"
#else
    "        Welcome to the MINI-M68000 System" "\r\n\r\n"
#endif
    "BIOS version " VERSION_STRING " of " VERSION_DATE	"\r\n"
    "Copyright (C) 2011-2015 John R. Coffman  <johninsd@gmail.com>" "\r\n"
    "Copyright (C) 2015 William R. Sowerbutts <will@sowerbutts.com>" "\r\n"
#if RETAIL
    "\r\n"
    "This program is free software: you can redistribute it and/or modify\r\n"
    "it under the terms of the GNU General Public License as published by\r\n"
    "the Free Software Foundation, either version 3 of the License, or\r\n"
    "(at your option) any later version.\r\n"
    "\r\n"
    "This program is distributed in the hope that it will be useful,\r\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\r\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\r\n"
    "GNU General Public License for more details.\r\n"
    "\r\n"
    "You should have received a copy of the GNU General Public License\r\n"
    "in the file COPYING in the distribution directory along with this\r\n"
    "program.  If not, see <http://www.gnu.org/licenses/>.\r\n"
    "\r\n"
#endif
#if M68000==68030
    "Distributed for hobbyist use on the N8VEM KISS-68030 CPU board."
#else
    "Distributed for hobbyist use on the N8VEM Mini-M68k CPU board."
#endif
    "\r\n\r\n";

T_nv_struct nvram;

void rubout(void)
{
	_con_out('\b');
	_con_out(' ');
	_con_out('\b');
}

int getline(char *line, int linesize)
{
	int k = 0;
	signed char ch;

	do {
		do ch = sio_get();
		while (ch < 0);

		if (ch >= ' ' && ch < 0177) {
			line[k++] = ch;
			_con_out(ch);
		}
		else if (ch == '\r' || ch == '\n') {
			ch = 0;
			_con_out('\r');
			_con_out('\n');
		}
		else if ( (ch == '\b' || ch == 0177) && k>0) {
			rubout();
			--k;
		}
		else if (ch == ('X' & 037) /* Ctrl-X */) {
			while (k) { rubout(); --k; }
		}
		else _con_out('G' & 037);	/* BEL */

	} while (ch && k < linesize-1);
	line[k] = 0;
#if !RETAIL
	if (debug>=5) cprintf("\ngetline: k=%d\n", k);
#endif
	return k;
}

const char *fatfs_errmsg[20] = 
{
    /* 0  */ "Succeeded",
    /* 1  */ "A hard error occurred in the low level disk I/O layer",
    /* 2  */ "Assertion failed",
    /* 3  */ "The physical drive cannot work",
    /* 4  */ "Could not find the file",
    /* 5  */ "Could not find the path",
    /* 6  */ "The path name format is invalid",
    /* 7  */ "Access denied due to prohibited access or directory full",
    /* 8  */ "Access denied due to prohibited access",
    /* 9  */ "The file/directory object is invalid",
    /* 10 */ "The physical drive is write protected",
    /* 11 */ "The logical drive number is invalid",
    /* 12 */ "The volume has no work area",
    /* 13 */ "There is no valid FAT volume",
    /* 14 */ "The f_mkfs() aborted due to any parameter error",
    /* 15 */ "Could not get a grant to access the volume within defined period",
    /* 16 */ "The operation is rejected according to the file sharing policy",
    /* 17 */ "LFN working buffer could not be allocated",
    /* 18 */ "Number of open files > _FS_LOCK",
    /* 19 */ "Given parameter is invalid"
};

void f_perror(int errno)
{
    if(errno <= 19)
        cprintf("Error: %s\n", fatfs_errmsg[errno]);
    else
        cprintf("Error: Unknown error %d. Hold tight.\n", errno);
}

void do_dump(char *argv[], int argc)
{
    unsigned long start, count;

    start = strtoul(argv[0], NULL, 16);
    count = strtoul(argv[1], NULL, 16);

    pretty_dump_memory((void*)start, count);
}

void do_writemem(char *argv[], int argc)
{
    unsigned long value;
    unsigned char *ptr;
    int i;

    value = strtoul(argv[0], NULL, 16);
    ptr = (unsigned char*)value;

    /* the parsing could be improved here: strings > 2 chars long could be
       interpreted as a series of bytes, so 123456 = 12 34 56 then one
       could write intermixed bytes, words and dwords naturally */

    for(i=1; i<argc; i++)
        *(ptr++) = strtoul(argv[i], NULL, 16);
}

void do_execute(char *argv[], int argc)
{
    unsigned long address;
    bool usermode = true;

    address = strtoul(argv[0], NULL, 16);
    if(argc == 2){
        if(!strcasecmp(argv[1], "user"))
            usermode = true;
        else if(!strcasecmp(argv[1], "system") || !strcasecmp(argv[1], "supervisor"))
            usermode = false;
    }

    cprintf("Entry at 0x%x in %s mode\n", address, usermode ? "user" : "system");
    _run_us_mode(usermode? 0 : 1, (void*)address);
}

void do_cd(char *argv[], int argc)
{
    FRESULT r;

    r = f_chdir(argv[0]);
    if(r != FR_OK)
        f_perror(r);
}

void do_ls(char *argv[], int argc)
{
    FRESULT fr;
    const char *path;
    DIR fat_dir;
    FILINFO fat_file;
    bool left = true;
    int i;

    if(argc == 0)
        path = "";
    else
        path = argv[0];

    fr = f_opendir(&fat_dir, path);
    if(fr != FR_OK){
        cprintf("f_opendir(\"%s\"): ", path);
        f_perror(fr);
        return;
    }

    while(1){
        fr = f_readdir(&fat_dir, &fat_file);
        if(fr != FR_OK){
            cprintf("f_readdir(): ");
            f_perror(fr);
            break;
        }
        if(fat_file.fname[0] == 0) /* end of directory? */
            break;

        if(fat_file.fattrib & AM_DIR){
            /* directory */
            cprintf("          %04d-%02d-%02d %02d:%02d:%02d %s/", 
                    1980 + ((fat_file.fdate >> 9) & 0x7F),
                    (fat_file.fdate >> 5) & 0xF,
                    fat_file.fdate & 0x1F,
                    fat_file.ftime >> 11,
                    (fat_file.ftime >> 5) & 0x3F,
                    (fat_file.ftime & 0x1F) << 1,
                    fat_file.fname);
            for(i=strlen(fat_file.fname); i<12; i++)
                cprintf(" ");
        }else{
            /* regular file */
            cprintf("%9d %04d-%02d-%02d %02d:%02d:%02d %-12s ", fat_file.fsize, 
                    1980 + ((fat_file.fdate >> 9) & 0x7F),
                    (fat_file.fdate >> 5) & 0xF,
                    fat_file.fdate & 0x1F,
                    fat_file.ftime >> 11,
                    (fat_file.ftime >> 5) & 0x3F,
                    (fat_file.ftime & 0x1F) << 1,
                    fat_file.fname);
        }

        if(left)
            cprintf(" ");
        else
            cprintf("\n");
        left = !left;
    }

    if(!left)
        cprintf("\n");

    fr = f_closedir(&fat_dir);
    if(fr != FR_OK){
        cprintf("f_closedir(): ");
        f_perror(fr);
        return;
    }
}

FATFS fat_fs_workarea[_VOLUMES];

typedef struct
{
    const char *name;
    const int min_args;
    const int max_args;
    void (* function)(char *argv[], int argc);
} cmd_entry_t;

const cmd_entry_t cmd_table[] = {
    /* name         min max function */
    {"ls",          0,  1,  &do_ls},
    {"dir",         0,  1,  &do_ls},
    {"cd",          1,  1,  &do_cd},
    {"dm",          2,  2,  &do_dump},
    {"dump",        2,  2,  &do_dump},
    {"wm",          2,  0,  &do_writemem},
    {"writemem",    2,  0,  &do_writemem},  /* writemem <addr> [byte...] */
    {"execute",     1,  2,  &do_execute},   /* execute <addr> [user|system] */
    {0, 0, 0, 0} /* terminator */
};

bool handle_cmd_builtin(char *arg[], int numarg)
{
    FRESULT fr;
    const cmd_entry_t *cmd;

    if(numarg == 1 && arg[0][strlen(arg[0])-1] == ':'){
        /* change drive */
        fr = f_chdrive(arg[0]);
        if(fr)
            f_perror(fr);
        return true;
    } else {
        /* built-in command */
        for(cmd = cmd_table; cmd->name; cmd++){
            if(!strcasecmp(arg[0], cmd->name)){
                if((numarg-1) >= cmd->min_args && 
                        (cmd->max_args == 0 || (numarg-1) <= cmd->max_args)){
                    cmd->function(arg+1, numarg-1);
                }else{
                    if(cmd->min_args == cmd->max_args){
                        cprintf("%s: takes exactly %d argument%s\n", arg[0], cmd->min_args, cmd->min_args == 1 ? "" : "s");
                    }else{
                        cprintf("%s: takes %d to %d arguments\n", arg[0], cmd->min_args, cmd->max_args);
                    }
                }
                return true;
            }
        }
    }
    return false;
}

bool load_elf_executable(char *arg[], int numarg, FIL *fd)
{
    int i, proghead_num;
    unsigned int bytes_read;
    elf32_header header;
    elf32_program_header proghead;
    bool loaded = false;
    bool usermode = true;

    for(i=1; i<numarg; i++){
        if(!strcasecmp(arg[i], "user"))
            usermode = true;
        else if(!strcasecmp(arg[i], "system") || !strcasecmp(arg[i], "supervisor"))
            usermode = false;
        else{
            cprintf("Unrecognised argument \"%s\".\n", arg[i]);
            return false;
        }
    }

    if(f_read(fd, &header, sizeof(header), &bytes_read) != FR_OK || bytes_read != sizeof(header)){
        cprintf("Cannot read ELF file header\n");
        return false;
    }

    if(header.ident_magic[0] != 0x7F ||
       header.ident_magic[1] != 'E' ||
       header.ident_magic[2] != 'L' ||
       header.ident_magic[3] != 'F' ||
       header.ident_version != 1){
        cprintf("Bad ELF header\n");
        return false;
    }

    if(header.ident_class != 1 || /* 32-bit */
       header.ident_data != 2 ||  /* big-endian */
       header.ident_osabi != 0 ||
       header.ident_abiversion != 0){
        cprintf("Not a 32-bit ELF file.\n");
        return false;
    }

    if(header.type != 2){
        cprintf("ELF file is not an executable.\n");
        return false;
    }

    if(header.machine != 4){
        cprintf("ELF file is not for 68000 processor.\n");
        return false;
    }

    for(proghead_num=0; proghead_num < header.phnum; proghead_num++){
        f_lseek(fd, header.phoff + proghead_num * header.phentsize);
        if(f_read(fd, &proghead, sizeof(proghead), &bytes_read) != FR_OK || bytes_read != sizeof(proghead)){
            cprintf("Cannot read ELF program header.\n");
            return false;
        }
        switch(proghead.type){
            case PT_NULL:
            case PT_NOTE:
            case PT_PHDR:
                break;
            case PT_SHLIB: /* "reserved but has unspecified semantics" */
            case PT_DYNAMIC:
                cprintf("ELF executable is dynamically linked.\n");
                return false;
            case PT_LOAD:
                if(proghead.paddr == 0){
                    /* patch up sections which want to overwrite the processor vectors */
                    proghead.offset += 0x1000;
                    proghead.paddr += 0x1000;
                    proghead.filesz -= 0x1000;
                    proghead.memsz -= 0x1000;
                }
                cprintf("Loading %d bytes from file offset 0x%x to memory at 0x%x\n", proghead.filesz, proghead.offset, proghead.paddr);
                f_lseek(fd, proghead.offset);
                if(f_read(fd, (char*)proghead.paddr, proghead.filesz, &bytes_read) != FR_OK || 
                        bytes_read != proghead.filesz){
                    cprintf("Unable to read segment from ELF file.\n");
                    return false;
                }
                if(proghead.memsz > proghead.filesz)
                    memset((char*)proghead.paddr + proghead.filesz, 0, 
                            proghead.memsz - proghead.filesz);
                loaded = true;
                break;
            case PT_INTERP:
                cprintf("ELF executable requires an interpreter.\n");
                return false;
        }
    }

    if(loaded){
        cprintf("Entry at 0x%x in %s mode\n", header.entry, usermode ? "user" : "system");
        _run_us_mode(usermode? 0 : 1, (void*)header.entry);
    }

    return true;
}

void load_coff_executable(char *arg[], int numarg, FIL *fd)
{
    cprintf("Loading COFF executable\n");
    cprintf("[unimplemented]\n");
}

bool load_flat_executable(char *arg[], int numarg, FIL *fd)
{
    unsigned long loadaddr;
    unsigned int bytes_read;

    if(numarg != 2){
        cprintf("Please specify the load address as an argument (in hex).\n");
        return false;
    }

    loadaddr = strtoul(argv[1], NULL, 16);

    cprintf("Loading flat binary at 0x%x\n", loadaddr);

    bytes_read = f_size(fd);

    if(f_read(fd, (char*)loadaddr, bytes_read, &bytes_read) != FR_OK || bytes_read != f_size(fd)){
        cprintf("Unable to load file.\n");
        return false;
    }

    return true;
}

#define HEADER_EXAMINE_SIZE 4 /* number of bytes we need to load to determine the file type */
const char coff_header_bytes[2] = { 0x01, 0x50 };
const char elf_header_bytes[4]  = { 0x7F, 0x45, 0x4c, 0x46 };

bool handle_cmd_executable(char *arg[], int numarg)
{
    FIL fd;
    FRESULT fr;
    char buffer[HEADER_EXAMINE_SIZE];
    unsigned int br;

    fr = f_open(&fd, arg[0], FA_READ);

    if(fr == FR_NO_FILE || fr == FR_NO_PATH) // file doesn't exist?
        return false;

    if(fr != FR_OK){
        cprintf("%s: Cannot load: ", arg[0]);
        f_perror(fr);
        return true; // we tried and failed
    }

    memset(buffer, 0, HEADER_EXAMINE_SIZE);

    cprintf("%s: %d bytes, ", arg[0], f_size(&fd));

    /* sniff the first few bytes, then rewind to the start of the file */
    fr = f_read(&fd, buffer, HEADER_EXAMINE_SIZE, &br);
    f_lseek(&fd, 0);

    if(fr == FR_OK){
        if(memcmp(buffer, elf_header_bytes, sizeof(elf_header_bytes)) == 0){
            cprintf("ELF.\n");
            load_elf_executable(arg, numarg, &fd);
        }else if(memcmp(buffer, coff_header_bytes, sizeof(coff_header_bytes)) == 0){
            cprintf("COFF.\n");
            load_coff_executable(arg, numarg, &fd);
        }else{
            cprintf("unknown.\n");
            load_flat_executable(arg, numarg, &fd);
        }
    }else{
        cprintf("%s: Cannot read: ", arg[0]);
        f_perror(fr);
    }

    f_close(&fd);

    return true;
}

#define LINELEN 1024
#define MAXARG 40
char linebuffer[LINELEN];

int main68(void)
{
    char *p, *arg[MAXARG+1];
    int numarg, i;

    /* set up work areas for each volume */
    for(i=0; i<_VOLUMES; i++){
        linebuffer[0] = '0' + i;
        linebuffer[1] = ':';
        linebuffer[2] = 0;
        f_mount(&fat_fs_workarea[i], linebuffer, 0); /* permit lazy mounting */
    }

    while(true){
        f_getcwd(linebuffer, LINELEN/sizeof(TCHAR));
        cprintf("%s> ", linebuffer);
        getline(linebuffer, LINELEN);

        /* parse linebuffer into list of args */
        numarg = 0;
        p = linebuffer;
        while(true){
            if(numarg == MAXARG){
                cprintf("Limiting to %d arguments.\n", numarg);
                *p = 0;
            }
            if(!*p){ /* end of string? */
                arg[numarg] = 0;
                break;
            }
            while(isspace(*p))
                p++;
            if(!isspace(*p)){
                arg[numarg++] = p;
                while(*p && !isspace(*p))
                    p++;
                if(!*p)
                    continue;
                while(isspace(*p)){
                    *p=0;
                    p++;
                }
            }
        }

        if(numarg > 0){
            if(!handle_cmd_builtin(arg, numarg) &&
               !handle_cmd_executable(arg, numarg))
                cprintf("%s: unknown command\n", arg[0]);
        }
    }

    return 0;
}

/*
  efivars_proc.[ch] - Manipulates EFI variables as exported in /proc/efi/vars

  Copyright (C) 2001,2003 Dell Computer Corporation <Matt_Domsch@dell.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* FPM 01/21/2010 - removed functions that uefivars did not need */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/types.h>
#include <net/if.h>
#include <pci/pci.h>
#include <asm/types.h>
#include <linux/ethtool.h>

#include "efi.h"
#include "efichar.h"
#include "efibootmgr.h"
#include "list.h"


#define EFIVARFS_DIR_EFI_VARS "/sys/firmware/efi/efivars"


EFI_DEVICE_PATH *
load_option_path(EFI_LOAD_OPTION *option)
{
	char *p = (char *) option;
	return (EFI_DEVICE_PATH *)
		(p + sizeof(uint32_t) /* Attributes */
		 + sizeof(uint16_t)   /* FilePathListLength*/
		 + efichar_strsize(option->description)); /* Description */
}

char *
efi_guid_unparse(efi_guid_t *guid, char *out)
{
	sprintf(out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid->b[3], guid->b[2], guid->b[1], guid->b[0],
		guid->b[5], guid->b[4], guid->b[7], guid->b[6],
		guid->b[8], guid->b[9], guid->b[10], guid->b[11],
		guid->b[12], guid->b[13], guid->b[14], guid->b[15]);
        return out;
}

void
set_fs_kernel_calls()
{
        char filename[PATH_MAX];
        int fd;

        snprintf(filename, PATH_MAX -1, "%s/%s", EFIVARFS_DIR_EFI_VARS,
                       "ConOut-8be4df61-93ca-11d2-aa0d-00e098032b8c" );
        fd = open (filename, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "FATAL: The efivarfs filesystem does not appear to be mounted.\n");
            exit(1);
        }

        close(fd);
}


efi_status_t
read_variable(const char *name, efi_variable_t *var)
{
	char filename[PATH_MAX];
        efi_guid_t guid = EFI_GLOBAL_VARIABLE;
	int fd, len;
	size_t readsize, datasize;
	struct stat buf;
	void *bData, *bName;

	if (!name || !var) 
		return EFI_INVALID_PARAMETER;


	snprintf (filename, PATH_MAX-1, "%s/%s", EFIVARFS_DIR_EFI_VARS, name);
	fd = open (filename, O_RDONLY);
	if (fd == -1) {
		return EFI_NOT_FOUND;
	}
 
	if (fstat (fd, &buf) != 0) {
		close (fd);
		return EFI_INVALID_PARAMETER;
	}
 
	readsize = read (fd, &var->Attributes, sizeof(uint32_t));
	if (readsize != sizeof(uint32_t)) {
		close (fd);
		return EFI_INVALID_PARAMETER;
	}
 
	datasize = buf.st_size - sizeof(uint32_t);
 
	bData = malloc (datasize);
	if (bData == NULL) {
		close (fd);
		return EFI_OUT_OF_RESOURCES;
	}
 
	readsize = read (fd, bData, datasize);
	if (readsize < 1) {
		close (fd);
		free (bData);
		return EFI_INVALID_PARAMETER;
	}

        len = strlen(name) - (36 + 1);   // '-' + GUID

        bName = malloc(sizeof(efi_char16_t) *(len + 1));
        if (!bName) {
		close (fd);
		free (bData);
		return EFI_OUT_OF_RESOURCES;
        }

        efichar_from_char(bName, name, (len + 1) * 2);
        memcpy(&var->VendorGuid, &guid, sizeof(guid));
        var->Attributes = EFI_VARIABLE_NON_VOLATILE
                	| EFI_VARIABLE_BOOTSERVICE_ACCESS
                	| EFI_VARIABLE_RUNTIME_ACCESS;

        var->VariableName = bName;
	var->Data = bData;
	var->DataSize = readsize;
 
	close (fd);
	return var->Status;
}

static int
select_all_var_names(const struct dirent *d)
{
	return 1;
}

int
read_all_var_names(struct dirent ***namelist)
{
	if (!namelist) return -1;

	return scandir(EFIVARFS_DIR_EFI_VARS,
		       namelist, select_all_var_names, alphasort);
}

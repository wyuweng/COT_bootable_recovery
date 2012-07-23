#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <reboot/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "utilities/ext4_utils/make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "../../external/yaffs2/yaffs2/utils/mkyaffs2image.h"
#include "../../external/yaffs2/yaffs2/utils/unyaffs.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"

#include "settings.h"
#include "settingshandler.h"
#include "settingshandler_lang.h"

#include "eraseandformat.h"

#define ABS_MT_POSITION_X 0x35  /* Center X ellipse position */

int script_assert_enabled = 1;
static const char *SDCARD_UPDATE_FILE = "/sdcard/update.zip";

void toggle_script_asserts()
{
    script_assert_enabled = !script_assert_enabled;
    ui_print("%s %s\n", scriptasserts, script_assert_enabled ? enabled : disabled);
}

void toggle_ui_debugging()
{
	switch(UI_COLOR_DEBUG) {
		case 0: {
			ui_print("%s\n", uidebugenable);
			UI_COLOR_DEBUG = 1;
			break;
		}
		default: {
			ui_print("%s\n", uidebugdisable);
			UI_COLOR_DEBUG = 0;
			break;
		}
	}
}

int install_zip(const char* packagefilepath, int dummy)
{
    ui_print("\n-- %s: %s\n", installing, packagefilepath);
    if (device_flash_type() == MTD) {
        set_sdcard_update_bootloader_message();
    }
    int status = install_package(packagefilepath, dummy);
    ui_reset_progress();
    if (status != INSTALL_SUCCESS) {
        ui_set_background(BACKGROUND_ICON_ERROR);
#if TARGET_BOOTLOADER_BOARD_NAME != otter
        int err_i = 0;
        for ( err_i = 0; err_i < 4; err_i++ ) {
            vibrate(15);
        }
#endif
        ui_print("%s\n", installabort);
        return 1;
    }
    ui_set_background(BACKGROUND_ICON_NONE);
#if TARGET_BOOTLOADER_BOARD_NAME != otter
    vibrate(60);
#endif
    ui_print("\n%s\n", installcomplete);
    return 0;
}

void show_install_update_menu()
{
	#define ITEM_CHOOSE_ZIP       0
	#define ITEM_APPLY_SDCARD     1
	#define ITEM_ASSERTS          2

	static char* INSTALL_MENU_ITEMS[4];
	INSTALL_MENU_ITEMS[0] = zipchoosezip;
	INSTALL_MENU_ITEMS[1] = zipapplyupdatezip;
	INSTALL_MENU_ITEMS[2] = ziptoggleasserts;
	INSTALL_MENU_ITEMS[3] = NULL;

    static char* headers[2];
	headers[0] = zipinstallheader;
	headers[1] = "\n";
    headers[2] = NULL;

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, INSTALL_MENU_ITEMS, 0, 0);
        switch (chosen_item)
        {
            case ITEM_ASSERTS:
                toggle_script_asserts();
                break;
            case ITEM_APPLY_SDCARD:
            {
                if (confirm_selection(installconfirm, yesinstallupdate))
                    install_zip(SDCARD_UPDATE_FILE, 0);
                break;
            }
            case ITEM_CHOOSE_ZIP:
                show_choose_zip_menu("/sdcard/");
                break;
            default:
                return;
        }

    }
}

void free_string_array(char** array)
{
    if (array == NULL)
        return;
    char* cursor = array[0];
    int i = 0;
    while (cursor != NULL)
    {
        free(cursor);
        cursor = array[++i];
    }
    free(array);
}

char** gather_files(const char* directory, const char* fileExtensionOrDirectory, int* numFiles)
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int i;
    char** files = NULL;
    int pass;
    *numFiles = 0;
    int dirLen = strlen(directory);

    dir = opendir(directory);
    if (dir == NULL) {
        ui_print("%s\n", diropenfail);
        return NULL;
    }

    int extension_length = 0;
    if (fileExtensionOrDirectory != NULL)
        extension_length = strlen(fileExtensionOrDirectory);

    int isCounting = 1;
    i = 0;
    for (pass = 0; pass < 2; pass++) {
        while ((de=readdir(dir)) != NULL) {
            // skip hidden files
            if (de->d_name[0] == '.')
                continue;

            // NULL means that we are gathering directories, so skip this
            if (fileExtensionOrDirectory != NULL)
            {
                // make sure that we can have the desired extension (prevent seg fault)
                if (strlen(de->d_name) < extension_length)
                    continue;
                // compare the extension
                if (strcmp(de->d_name + strlen(de->d_name) - extension_length, fileExtensionOrDirectory) != 0)
                    continue;
            }
            else
            {
                struct stat info;
                char fullFileName[PATH_MAX];
                strcpy(fullFileName, directory);
                strcat(fullFileName, de->d_name);
                stat(fullFileName, &info);
                // make sure it is a directory
                if (!(S_ISDIR(info.st_mode)))
                    continue;
            }

            if (pass == 0)
            {
                total++;
                continue;
            }

            files[i] = (char*) malloc(dirLen + strlen(de->d_name) + 2);
            strcpy(files[i], directory);
            strcat(files[i], de->d_name);
            if (fileExtensionOrDirectory == NULL)
                strcat(files[i], "/");
            i++;
        }
        if (pass == 1)
            break;
        if (total == 0)
            break;
        rewinddir(dir);
        *numFiles = total;
        files = (char**) malloc((total+1)*sizeof(char*));
        files[total]=NULL;
    }

    if(closedir(dir) < 0) {
        LOGE("Failed to close directory.");
    }

    if (total==0) {
        return NULL;
    }

    // sort the result
    if (files != NULL) {
        for (i = 0; i < total; i++) {
            int curMax = -1;
            int j;
            for (j = 0; j < total - i; j++) {
                if (curMax == -1 || strcmp(files[curMax], files[j]) < 0)
                    curMax = j;
            }
            char* temp = files[curMax];
            files[curMax] = files[total - i - 1];
            files[total - i - 1] = temp;
        }
    }

    return files;
}

// pass in NULL for fileExtensionOrDirectory and you will get a directory chooser
char* choose_file_menu(const char* directory, const char* fileExtensionOrDirectory, const char* headers[])
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int numFiles = 0;
    int numDirs = 0;
    int i;
    char* return_value = NULL;
    int dir_len = strlen(directory);

    char** files = gather_files(directory, fileExtensionOrDirectory, &numFiles);
    char** dirs = NULL;
    if (fileExtensionOrDirectory != NULL)
        dirs = gather_files(directory, NULL, &numDirs);
    int total = numDirs + numFiles;
    if (total == 0)
    {
        ui_print("No files found.\n");
    }
    else
    {
        char** list = (char**) malloc((total + 1) * sizeof(char*));
        list[total] = NULL;


        for (i = 0 ; i < numDirs; i++)
        {
            list[i] = strdup(dirs[i] + dir_len);
        }

        for (i = 0 ; i < numFiles; i++)
        {
            list[numDirs + i] = strdup(files[i] + dir_len);
        }

        for (;;)
        {
            int chosen_item = get_menu_selection(headers, list, 0, 0);
            if (chosen_item == GO_BACK)
                break;
            static char ret[PATH_MAX];
            if (chosen_item < numDirs)
            {
                char* subret = choose_file_menu(dirs[chosen_item], fileExtensionOrDirectory, headers);
                if (subret != NULL)
                {
                    strcpy(ret, subret);
                    return_value = ret;
                    break;
                }
                continue;
            }
            strcpy(ret, files[chosen_item - numDirs]);
            return_value = ret;
            break;
        }
        free_string_array(list);
    }

    free_string_array(files);
    free_string_array(dirs);
    return return_value;
}

void show_view_and_delete_backups(const char *mount_point, const char *backup_path)
{
	if (ensure_path_mounted(mount_point) != 0) {
		LOGE("Can't mount %s\n", mount_point);
		return;
	}

	static char* headers[3];
	headers[0] = deletebackupheader;
	headers[1] = "\n";
	headers[2] = NULL;

	char* file = choose_file_menu(mount_point, NULL, headers);
	if(file == NULL)
		return;
	static char confirm[PATH_MAX];
	sprintf(confirm, "%s %s", yesdelete, basename(file));
	if(confirm_selection(deleteconfirm, confirm)) {
		static char* tmp[PATH_MAX];
		sprintf(tmp, "rm -rf %s", file);
		__system(tmp);
		uint64_t sdcard_free_mb = recalc_sdcard_space(backup_path);
		ui_print("%s %lluMB\n", freespacesd, sdcard_free_mb);
	}
}

void delete_old_backups(const char *mount_point)
{
	if (ensure_path_mounted(mount_point) != 0) {
		LOGE("Can't mount %s\n", mount_point);
		return;
	}

	static char* headers[3];
	headers[0] = deletebackupheader;
	headers[1] = "\n";
	headers[2] = NULL;

	char* file = choose_file_menu(mount_point, NULL, headers);
	if(file == NULL)
		return;
	static char confirm[PATH_MAX];
	sprintf(confirm, "Yes - Delete %s", basename(file));
	if(confirm_selection(deleteconfirm, confirm)) {
		static char* tmp[PATH_MAX];
        ui_print("Deleting %s\n", basename(file));
		sprintf(tmp, "rm -rf %s", file);
		__system(tmp);
        ui_print("Backup deleted!\n");
	}
}

int show_lowspace_menu(int i, const char* backup_path)
{
	static char *LOWSPACE_MENU_ITEMS[4];
	LOWSPACE_MENU_ITEMS[0] = lowspacecontinuebackup;
	LOWSPACE_MENU_ITEMS[1] = lowspaceviewdelete;
	LOWSPACE_MENU_ITEMS[2] = lowspacecancel;
	LOWSPACE_MENU_ITEMS[3] = NULL;

	#define ITEM_CONTINUE_BACKUP 0
	#define ITEM_VIEW_DELETE_BACKUPS 1
	#define ITEM_CANCEL_BACKUP 2

	static char* headers[7];
	headers[0] = lowspaceheader1;
	headers[1] = "\n";
	headers[2] = lowspaceheader2;
	headers[3] = lowspaceheader3;
	headers[4] = "\n";
	headers[5] = lowspaceheader4;
	headers[6] = NULL;

	for (;;) {
		int chosen_item = get_menu_selection(headers, LOWSPACE_MENU_ITEMS, 0, 0);
		switch(chosen_item) {
			case ITEM_CONTINUE_BACKUP: {
				static char tmp;
				ui_print("%s\n", backupproceed);
				return 0;
			}
			case ITEM_VIEW_DELETE_BACKUPS: {
				char final_backup_path[PATH_MAX];
				nandroid_get_backup_path(final_backup_path);
				show_view_and_delete_backups(final_backup_path, backup_path);
				break;
			}
			default:
				ui_print("%s\n", backupcancel);
				return 1;
		}
	}
}

void show_choose_zip_menu(const char *mount_point)
{
    if (ensure_path_mounted(mount_point) != 0) {
        LOGE ("Can't mount %s\n", mount_point);
        return;
    }

    static char *INSTALL_OR_BACKUP_ITEMS[4];
	INSTALL_OR_BACKUP_ITEMS[0] = zipchooseyesbackup;
	INSTALL_OR_BACKUP_ITEMS[1] = zipchoosenobackup;
	INSTALL_OR_BACKUP_ITEMS[2] = zipcancelinstall;
	INSTALL_OR_BACKUP_ITEMS[3] = NULL;

	#define ITEM_BACKUP_AND_INSTALL 0
	#define ITEM_INSTALL_WOUT_BACKUP 1
	#define ITEM_CANCEL_INSTALL 2

    static char* headers[3];
	headers[0] = zipchoosezip;
	headers[1] = "\n";
	headers[2] = NULL;

    char* file = choose_file_menu(mount_point, ".zip", headers);
    if (file == NULL)
        return;

    if (backupprompt == 0) {
        static char confirm[PATH_MAX];
        sprintf(confirm, "%s %s", yesinstall, basename(file));
        if (confirm_selection(installconfirm, confirm)) {
            install_zip(file, 0);
        }
    } else {
		for (;;) {
			int chosen_item = get_menu_selection(headers, INSTALL_OR_BACKUP_ITEMS, 0, 0);
			switch(chosen_item) {
				case ITEM_BACKUP_AND_INSTALL: {
					char backup_path[PATH_MAX];
					nandroid_generate_timestamp_path(backup_path);
					nandroid_backup(backup_path);
					install_zip(file, 0);
					return;
				}
				case ITEM_INSTALL_WOUT_BACKUP:
					install_zip(file, 0);
					return;
				default:
					break;
			}
			break;
		}
    }
}

void show_nandroid_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[3];
	headers[0] = nandroidrestoreheader;
	headers[1] = "\n";
	headers[2] = NULL;

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/cotrecovery/backup/", path); // Need to fix up the default backup path to not point to sdcard so that we can use a variable here...
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;

    if (confirm_selection(nandroidconfirmrestore, nandroidyesrestore))
        nandroid_restore(file, 1, 1, 1, 1, 1);
}

#ifndef BOARD_UMS_LUNFILE
#define BOARD_UMS_LUNFILE	"/sys/devices/platform/usb_mass_storage/lun0/file"
#endif

void show_mount_usb_storage_menu()
{
    int fd;
    Volume *vol = volume_for_path("/sdcard");
    if ((fd = open(BOARD_UMS_LUNFILE, O_WRONLY)) < 0) {
        LOGE("Unable to open ums lunfile (%s)", strerror(errno));
        return -1;
    }

    if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
        (!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
        LOGE("Unable to write to ums lunfile (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    static char* headers[5];
	headers[0] = usbmsheader1;
	headers[1] = usbmsheader2;
	headers[2] = usbmsheader3;
	headers[3] = "\n";
	headers[4] = NULL;

    static char* list[] = { "Unmount", NULL };
	list[0] = usbmsunmount;

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == 0)
            break;
    }

    if ((fd = open(BOARD_UMS_LUNFILE, O_WRONLY)) < 0) {
        LOGE("Unable to open ums lunfile (%s)", strerror(errno));
        return -1;
    }

    char ch = 0;
    if (write(fd, &ch, 1) < 0) {
        LOGE("Unable to write to ums lunfile (%s)", strerror(errno));
        close(fd);
        return -1;
    }
}

int confirm_selection(const char* title, const char* confirm)
{
    struct stat info;
    if (0 == stat("/sdcard/cotrecovery/.no_confirm", &info))
        return 1;

    static char* confirm_headers[2];
	confirm_headers[0] = wipedataheader2;
	confirm_headers[1] = NULL;

    static char* items[2];
	items[0] = no;
	items[1] = confirm;

    int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
    return chosen_item == 1;
}

int confirm_nandroid_backup(const char* title, const char* confirm)
{
    static char* confirm_headers[2];
    confirm_headers[0] = recommended;
    confirm_headers[1] = NULL;

    static char* items[2];
	items[0] = no;
	items[1] = confirm;

    int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
    return chosen_item == 1;
}

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"

typedef struct {
    char mount[255];
    char unmount[255];
    Volume* v;
} MountMenuEntry;

typedef struct {
    char txt[255];
    Volume* v;
} FormatMenuEntry;

int is_safe_to_format(char* name)
{
    char str[255];
    char* partition;
    /* Add /sdcard here because formatting it could cause issues, espescially
     * for dual boot systems also removing splash, haven't tested it but just
     * to be safe we'll pull it, if someone wants to format their splash and
     * confirm it's safe I'll add it back in. */
    property_get("ro.cwm.forbid_format", str, "/misc,/radio,/bootloader,/recovery,/efs,/sdcard,/splash");
    partition = strtok(str, ", ");
    while (partition != NULL) {
        if (strcmp(name, partition) == 0) {
            return 0;
        }
        partition = strtok(NULL, ", ");
    }

    return 1;
}

/* While this really shouldn't be neccessary in the fact that mounting
 * something shouldn't normally be dangerous. However unmounting the
 * sdcard does create issues, subsequently both it and splash are being
 * removed as listed in above notes */
int is_safe_to_mount(char* name) {
	char str[255];
	char* partition;
	property_get("ro.ing.forbid_mount", str, "/sdcard,/splash");
	
	partition = strtok(str, ", ");
	while (partition != NULL) {
		if (strcmp(name, partition) == 0) {
			return 0;
		}
		partition = strtok(NULL, ", ");
	}
	
	return 1;
}

void show_partition_menu()
{
    static char* headers[3];
	headers[0] = showpartitionheader;
	headers[1] = "\n";
	headers[2] = NULL;

    static MountMenuEntry* mount_menue = NULL;
    static FormatMenuEntry* format_menue = NULL;

    typedef char* string;

    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    Volume* device_volumes;

    num_volumes = get_num_volumes();
    device_volumes = get_device_volumes();

    string options[255];

    if(!device_volumes)
		return;

		mountable_volumes = 0;
		formatable_volumes = 0;

		mount_menue = malloc(num_volumes * sizeof(MountMenuEntry));
		format_menue = malloc(num_volumes * sizeof(FormatMenuEntry));

		for (i = 0; i < num_volumes; ++i) {
  			Volume* v = &device_volumes[i];
  			if(strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) != 0 && strcmp("emmc", v->fs_type) != 0 && strcmp("bml", v->fs_type) != 0) {
					if (is_safe_to_mount(v->mount_point)) {
						sprintf(&mount_menue[mountable_volumes].mount, "mount %s", v->mount_point);
						sprintf(&mount_menue[mountable_volumes].unmount, "unmount %s", v->mount_point);
						mount_menue[mountable_volumes].v = &device_volumes[i];
						++mountable_volumes;
					}
    				if (is_safe_to_format(v->mount_point)) {
      					sprintf(&format_menue[formatable_volumes].txt, "format %s", v->mount_point);
      					format_menue[formatable_volumes].v = &device_volumes[i];
      					++formatable_volumes;
    				}
  		  }
  		  else if (strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) == 0 && is_safe_to_format(v->mount_point))
  		  {
    				sprintf(&format_menue[formatable_volumes].txt, "format %s", v->mount_point);
    				format_menue[formatable_volumes].v = &device_volumes[i];
    				++formatable_volumes;
  			}
		}


    static char* confirm_format  = "Confirm format?";
    static char* confirm = "Yes - Format";
    char confirm_string[255];

    for (;;)
    {
		for (i = 0; i < mountable_volumes; i++)
		{
			MountMenuEntry* e = &mount_menue[i];
			Volume* v = e->v;
			if(is_path_mounted(v->mount_point))
				options[i] = e->unmount;
			else
				options[i] = e->mount;
		}

		for (i = 0; i < formatable_volumes; i++)
		{
			FormatMenuEntry* e = &format_menue[i];

			options[mountable_volumes+i] = e->txt;
		}

        options[mountable_volumes+formatable_volumes] = usbmsmount;
        options[mountable_volumes+formatable_volumes + 1] = NULL;

        int chosen_item = get_menu_selection(headers, &options, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        if (chosen_item == (mountable_volumes+formatable_volumes))
        {
            show_mount_usb_storage_menu();
        }
        else if (chosen_item < mountable_volumes)
        {
			MountMenuEntry* e = &mount_menue[chosen_item];
            Volume* v = e->v;

            if (is_path_mounted(v->mount_point))
            {
                if (0 != ensure_path_unmounted(v->mount_point))
                    ui_print("%s %s!\n", unmounterror, v->mount_point);
            }
            else
            {
                if (0 != ensure_path_mounted(v->mount_point))
                    ui_print("%s %s!\n", mounterror, v->mount_point);
            }
        }
        else if (chosen_item < (mountable_volumes + formatable_volumes))
        {
            chosen_item = chosen_item - mountable_volumes;
            FormatMenuEntry* e = &format_menue[chosen_item];
            Volume* v = e->v;

            sprintf(confirm_string, "%s - %s", v->mount_point, confirm_format);

            if (!confirm_selection(confirm_string, confirm))
                continue;
            ui_print("Formatting %s...\n", v->mount_point);
            if (0 != format_volume(v->mount_point))
                ui_print("Error formatting %s!\n", v->mount_point);
            else
                ui_print("%s\n", done);
        }
    }
    free(mount_menue);
    free(format_menue);
}

void show_nandroid_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE ("Can't mount %s\n", path);
        return;
    }

    static char* advancedheaders[7];
	advancedheaders[0] = advrestoreheader1;
	advancedheaders[1] = "\n";
	advancedheaders[2] = advrestoreheader1;
	advancedheaders[3] = advrestoreheader2;
	advancedheaders[4] = advrestoreheader3;
	advancedheaders[5] = "\n";
	advancedheaders[6] = NULL;

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/cotrecovery/backup/", path);
    char* file = choose_file_menu(tmp, NULL, advancedheaders);
    if (file == NULL)
        return;

    static char* headers[3];
	headers[0] = advrestoreheader11;
	headers[1] = "\n";
	headers[2] = NULL;

    static char* list[6];
	list[0] = nandroidrestoreboot;
	list[1] = nandroidrestoresys;
	list[2] = nandroidrestoredata;
	list[3] = nandroidrestorecache;
	list[4] = nandroidrestoresd;
	list[5] = NULL;

    static char* confirm_restore  = "Confirm restore?";

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
            if (confirm_selection(nandroidconfirmrestore, "Yes - Restore boot"))
                nandroid_restore(file, 1, 0, 0, 0, 0);
            break;
        case 1:
            if (confirm_selection(nandroidconfirmrestore, "Yes - Restore system"))
                nandroid_restore(file, 0, 1, 0, 0, 0);
            break;
        case 2:
            if (confirm_selection(nandroidconfirmrestore, "Yes - Restore data"))
                nandroid_restore(file, 0, 0, 1, 0, 0);
            break;
        case 3:
            if (confirm_selection(nandroidconfirmrestore, "Yes - Restore cache"))
                nandroid_restore(file, 0, 0, 0, 1, 0);
            break;
        case 4:
            if (confirm_selection(nandroidconfirmrestore, "Yes - Restore sd-ext"))
                nandroid_restore(file, 0, 0, 0, 0, 1);
            break;
    }
}

void show_nandroid_menu()
{
    static char* headers[] = {  "Nandroid",
                                "",
                                NULL
    };

    static char* list[] = { "backup",
                            "restore",
                            "advanced restore",
                            "delete old backups",
                            NULL
    };

	for (;;) {
		int chosen_item = get_menu_selection(headers, list, 0, 0);
		switch (chosen_item)
		{
			char backup_path[PATH_MAX];
			case 0:
			{
				nandroid_generate_timestamp_path(backup_path);
				nandroid_backup(backup_path);
				return;
			}
			case 1:
				show_nandroid_restore_menu("/sdcard");
				return;
			case 2:
				show_nandroid_advanced_restore_menu("/sdcard");
				return;
			case 3: 
			{
				nandroid_get_backup_path(backup_path);
				delete_old_backups(backup_path);
				break;
			}
			default:
				return;
		}
	}
}

void show_advanced_debugging_menu()
{
	static char* headers[] = { "Debugging Options",
								"",
								NULL
	};

	static char* list[] = { "Fix Permissions",
							"Report Error",
							"Show log",
							"Toggle UI Debugging",
							NULL
	};

	for (;;)
	{
		int chosen_item = get_menu_selection(headers, list, 0, 0);
		if(chosen_item == GO_BACK)
			break;
		switch(chosen_item)
		{
			case 0:
			{
				ensure_path_mounted("/system");
				ensure_path_mounted("/data");
				ui_print("Fixing permissions...\n");
				__system("fix_permissions");
				ui_print("Done!\n");
				break;
			}
			case 1:
				handle_failure(1);
				break;
			case 2:
				ui_printlogtail(12);
				break;
			case 3:
				toggle_ui_debugging();
				break;
		}
	}
}

void show_advanced_menu()
{
    static char* headers[] = {  "Advanced Options and Settings Menu",
                                "",
                                NULL
    };

    static char* list[] = {
#if TARGET_BOOTLOADER_BOARD_NAME == otter
							"Reboot Options",
#else
							"Reboot Recovery",
#endif
                            "Wipe Dalvik Cache",
#ifndef BOARD_HAS_SMALL_RECOVERY
							"Partition SD Card",
#endif
							"COT Settings",
							"Debugging Options",
                            NULL
    };

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
            {
#if TARGET_BOOTLOADER_BOARD_NAME == otter
				static char* reboot_menu[] = {"Reboot Options",
											"",
											NULL
				};
				static char* reboot_choices[] = {"reboot system now",
												"reboot recovery",
												"reboot into fastboot bootloader",
												NULL
				};
				int reboot_choice = get_menu_selection(reboot_menu, reboot_choices, 0, 0);
				switch(reboot_choice) {
					case 0:
						__system("/sbin/reboot_system");
						break;
					case 1:
						__system("/sbin/reboot_recovery");
						break;
					case 2:
						__system("/sbin/reboot_fastboot");
						break;
				}
				if(reboot_choice == GO_BACK)
					continue;
#else
				reboot_wrapper("recovery");
				break;
#endif
			}
            case 1:
            {
				erase_dalvik_cache(0);
                break;
            }
            case 2:
			{
#if TARGET_BOOTLOADER_BOARD_NAME == otter
				ui_print("Disabled for this device!\n");
#else
				static char* ext_sizes[] = { "128M",
                                             "256M",
                                             "512M",
                                             "1024M",
                                             "2048M",
                                             "4096M",
                                             NULL };

                static char* swap_sizes[] = { "0M",
                                              "32M",
                                              "64M",
                                              "128M",
                                              "256M",
                                              NULL };

                static char* ext_headers[] = { "Ext Size", "", NULL };
                static char* swap_headers[] = { "Swap Size", "", NULL };

                int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
                if (ext_size == GO_BACK)
                    continue;

                int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
                if (swap_size == GO_BACK)
                    continue;

                char sddevice[256];
                Volume *vol = volume_for_path("/sdcard");
                strcpy(sddevice, vol->device);
                // we only want the mmcblk, not the partition
                sddevice[strlen("/dev/block/mmcblkX")] = NULL;
                char cmd[PATH_MAX];
                setenv("SDPATH", sddevice, 1);
                sprintf(cmd, "sdparted -es %s -ss %s -efs ext3 -s", ext_sizes[ext_size], swap_sizes[swap_size]);
                ui_print("Partitioning SD Card... please wait...\n");
                if (0 == __system(cmd))
                    ui_print("Done!\n");
                else
                    ui_print("An error occured while partitioning your SD Card. Please see /tmp/recovery.log for more details.\n");
#endif
                break;
			}
			case 3:
			    show_settings_menu();
                break;
			case 4:
				show_advanced_debugging_menu();
				break;
			default:
				return;
        }
    }
}

void write_fstab_root(char *path, FILE *file)
{
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGW("Unable to get recovery.fstab info for %s during fstab generation!\n", path);
        return;
    }
    char device[200];
    if (vol->device[0] != '/')
        get_partition_device(vol->device, device);
    else
        strcpy(device, vol->device);

    fprintf(file, "%s ", device);
    fprintf(file, "%s ", path);
    // special case rfs cause auto will mount it as vfat on samsung.
    fprintf(file, "%s rw\n", vol->fs_type2 != NULL && strcmp(vol->fs_type, "rfs") != 0 ? "auto" : vol->fs_type);
}

void create_fstab()
{
    struct stat info;
    __system("touch /etc/mtab");
    FILE *file = fopen("/etc/fstab", "w");
    if (file == NULL) {
        LOGW("Unable to create /etc/fstab!\n");
        return;
    }
    Volume *vol = volume_for_path("/boot");
    if (NULL != vol && strcmp(vol->fs_type, "mtd") != 0 && strcmp(vol->fs_type, "emmc") != 0 && strcmp(vol->fs_type, "bml") != 0)
    write_fstab_root("/boot", file);
    write_fstab_root("/cache", file);
    write_fstab_root("/data", file);
    write_fstab_root("/datadata", file);
    write_fstab_root("/emmc", file);
    write_fstab_root("/system", file);
    write_fstab_root("/sdcard", file);
    write_fstab_root("/sd-ext", file);
    fclose(file);
    LOGI("Completed outputting fstab.\n");
}

int bml_check_volume(const char *path) {
    ui_print("Checking %s...\n", path);
    ensure_path_unmounted(path);
    if (0 == ensure_path_mounted(path)) {
        ensure_path_unmounted(path);
        return 0;
    }

    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGE("Unable process volume! Skipping...\n");
        return 0;
    }

    ui_print("%s may be rfs. Checking...\n", path);
    char tmp[PATH_MAX];
    sprintf(tmp, "mount -t rfs %s %s", vol->device, path);
    int ret = __system(tmp);
    printf("%d\n", ret);
    return ret == 0 ? 1 : 0;
}

void process_volumes() {
    create_fstab();

    if (is_data_media()) {
        setup_data_media();
    }

    return;

    // dead code.
    if (device_flash_type() != BML)
        return;

    ui_print("Checking for ext4 partitions...\n");
    int ret = 0;
    ret = bml_check_volume("/system");
    ret |= bml_check_volume("/data");
    if (has_datadata())
        ret |= bml_check_volume("/datadata");
    ret |= bml_check_volume("/cache");

    if (ret == 0) {
        ui_print("Done!\n");
        return;
    }

    char backup_path[PATH_MAX];
    time_t t = time(NULL);
    char backup_name[PATH_MAX];
    struct timeval tp;
    gettimeofday(&tp, NULL);
    sprintf(backup_name, "before-ext4-convert-%d", tp.tv_sec);
    struct stat st;
    char tmp[PATH_MAX];
	if (stat(USER_DEFINED_BACKUP_MARKER, &st) == 0) {
		FILE *file = fopen_path(USER_DEFINED_BACKUP_MARKER, "r");
		fscanf(file, "%s", &tmp);
		fclose(file);
	} else {
		sprintf(tmp, "%s", DEFAULT_BACKUP_PATH);
	}
    sprintf(backup_path, "%s%s", tmp, backup_name);

    ui_set_show_text(1);
    ui_print("Filesystems need to be converted to ext4.\n");
    ui_print("A backup and restore will now take place.\n");
    ui_print("If anything goes wrong, your backup will be\n");
    ui_print("named %s. Try restoring it\n", backup_name);
    ui_print("in case of error.\n");

    nandroid_backup(backup_path);
    //nandroid_restore(backup_path, 1, 1, 1, 1, 1, 0);
    ui_set_show_text(0);
}

void handle_failure(int ret)
{
    if (ret == 0)
        return;
    if (0 != ensure_path_mounted("/sdcard"))
        return;
    mkdir("/sdcard/cotrecovery", S_IRWXU);
    __system("cp /tmp/recovery.log /sdcard/cotrecovery/recovery.log");
    ui_print("This is the entirely wrong message and Drew is going to replace it with his inis eventually... in the meantime :P\n");
}

int is_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        return 0;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return 0;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 1;
    }
    return 0;
}

int has_datadata() {
    Volume *vol = volume_for_path("/datadata");
    return vol != NULL;
}

int volume_main(int argc, char **argv) {
    load_volume_table();
    return 0;
}

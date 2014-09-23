#include <common.h>

#define UEVENT_PATH_BLOCK_DEVICES "/sys/class/block"

/*
 * Source: http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
 */
char *copy_trim(const char *s)
{
	int start, end;
	for (start = 0; s[start] && isspace(s[start]); ++start) {
	}
	if (s[start] == '\0')
		return strdup("");
	for (end = strlen(s); end > 0 && isspace(s[end - 1]); --end) {
	}
	return strndup(s + start, end - start);
}

int getInt(const char *s)
{
	char *endptr;

	long ret = strtol(s, &endptr, 10);
	if (!endptr || *endptr != '\0') {
		return -1;
	}

	return ret;
}

static int add_uevent_entry(struct sys_block_info *info, const char *filename)
{
	FILE *fp;
	char line[128];

	// open file
	fp = fopen(filename, "r");
	if (fp == NULL) {
		ERROR("Can't open file %s!\n", filename);
		return -1;
	}
	// allocate memory
	int index = info->num_entries++;
	info->entries =
	    realloc(info->entries,
		    info->num_entries * sizeof(info->entries[0]));
	memset(&info->entries[index], 0, sizeof(info->entries[0]));

	// parse file
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *name = copy_trim(strtok(line, "="));
		char *value = copy_trim(strtok(NULL, "="));

		if (!name || !value)
			continue;

		if (!strcmp(name, "MAJOR")) {
			info->entries[index].major = getInt(value);
		} else if (!strcmp(name, "MINOR")) {
			info->entries[index].minor = getInt(value);
		} else if (!strcmp(name, "PARTN")) {
			info->entries[index].mmc_minor = getInt(value);
		} else if (!strcmp(name, "DEVNAME")) {
			info->entries[index].devname = strdup(value);
		} else if (!strcmp(name, "PARTNAME")) {
			info->entries[index].partname = strdup(value);
		} else if (!strcmp(name, "DEVTYPE")) {
			if (!strcmp(value, "disk"))
				info->entries[index].type = UEVENT_TYPE_DISK;
			else if (!strcmp(value, "partition"))
				info->entries[index].type =
				    UEVENT_TYPE_PARTITION;
			else
				info->entries[index].type = UEVENT_TYPE_UNKNOWN;
		}
	}

	unsigned mmc_major, mmc_minor;
	if (sscanf
	    (info->entries[index].devname, "mmcblk%up%u", &mmc_major,
	     &mmc_minor) == 2) {
		info->entries[index].mmc_major = mmc_major;
		info->entries[index].mmc_minor = mmc_minor;
	}
	// close file
	fclose(fp);

	return 0;
}

struct sys_block_info *get_block_devices(void)
{
	const char *path = UEVENT_PATH_BLOCK_DEVICES;
	char buf[PATH_MAX];
	struct sys_block_info *info = malloc(sizeof(struct sys_block_info));
	memset(info, 0, sizeof(info[0]));

	// find fstabs in root dir
	DIR *d = opendir(path);
	if (!d) {
		kperror("opendir");
		return NULL;
	}

	struct dirent *dt;
	while ((dt = readdir(d))) {
		if (dt->d_type != DT_LNK)
			continue;

		sprintf(buf, "%s/%s/uevent", path, dt->d_name);
		add_uevent_entry(info, buf);
	}

	if (closedir(d)) {
		kperror("closedir");
		return NULL;
	}

	return info;
}

void free_block_devices(struct sys_block_info *info)
{
	int i;

	for (i = 0; i < info->num_entries; i++) {
		struct sys_block_uevent *event = &info->entries[i];
		if (event->devname)
			free(event->devname);
		if (event->partname)
			free(event->partname);
		free(event);
	}

	free(info);
}

struct sys_block_uevent *get_blockinfo_for_path(struct sys_block_info *info,
						const char *path)
{
	char *name = NULL;
	unsigned mmc_major, mmc_minor;
	bool use_name = false;
	int i;
	struct sys_block_uevent *ret = NULL;

	if (strstr(path, "by-name") != NULL) {
		char *tmp = strdup(path);
		char *bname = basename(tmp);
		name = strdup(bname);
		free(tmp);
		use_name = true;
	} else
	    if (sscanf(path, "/dev/block/mmcblk%up%u", &mmc_major, &mmc_minor)
		!= 2) {
		printf("ERROR\n");
		return NULL;
	}

	for (i = 0; i < info->num_entries; i++) {
		struct sys_block_uevent *event = &info->entries[i];

		if (use_name && event->partname
		    && !strcmp(event->partname, name)) {
			ret = event;
			break;
		} else if (event->mmc_major == mmc_major
			   && event->mmc_minor == mmc_minor) {
			ret = event;
			break;
		}
	}

	if (name)
		free(name);

	return ret;
}

import re

with open('src/scanner.c', 'r') as f:
    content = f.read()

# Find and replace the specific pattern
old_pattern = '''#else
        drv->type = 0;
        char mount_path[MAX_PATH];
        snprintf(mount_path, sizeof(mount_path), "/mnt/%c", tolower((unsigned char)drive_letter));
        platform_strncpy_s(drv->label, sizeof(drv->label), mount_path);
#endif'''

new_pattern = '''#else
        drv->type = 0;
        /* For drive 0 (native Linux), use the subdirectory path as the mount point */
        if (drive_letter == 0) {
            platform_strncpy_s(drv->label, sizeof(drv->label), subdir_path);
        } else {
            char mount_path[MAX_PATH];
            snprintf(mount_path, sizeof(mount_path), "/mnt/%c", tolower((unsigned char)drive_letter));
            platform_strncpy_s(drv->label, sizeof(drv->label), mount_path);
        }
#endif'''

if old_pattern in content:
    content = content.replace(old_pattern, new_pattern)
    with open('src/scanner.c', 'w') as f:
        f.write(content)
    print("Fixed!")
else:
    print("Pattern not found - may already be fixed or different format")

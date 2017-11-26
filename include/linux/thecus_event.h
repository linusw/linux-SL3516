#include <linux/raid/md.h>

#define DISK_ADD "disk_add"
#define DISK_REMOVE "disk_remove"
#define DISK_FAIL "disk_fail"
#define DISK_RETRY "disk_retry"
#define DISK_IO_FAIL "disk_io_fail"
#define DEVICE_RESET "device_reset"
#define SMART_ERROR "smart_error"

#define RAID_HEALTHY "raid_healthy"
#define RAID_RECOVERY_HEALTHY "raid_healthy"
#define RAID_DEGRADE "raid_degrade"
#define RAID_DAMAGE "raid_damage"
#define RAID_RECOVERY "raid_recovery"
#define RAID_REBUILD "raid_rebuild"
#define RAID_RECOVERY_FAIL "raid_recovery_fail"
#define RAID_IO_FAIL "raid_io_fail"
#define RAID_NA "raid_na"
#define RAID_CREATE "raid_create"

#define RAID_STATUS_NA 1
#define RAID_STATUS_HEALTHY 2
#define RAID_STATUS_CREATE 3
#define RAID_STATUS_RECOVERY 4
#define RAID_STATUS_RECOVERY_HEALTHY 5
#define RAID_STATUS_RECOVERY_FAIL 6
#define RAID_STATUS_REBUILD 7
#define RAID_STATUS_DEGRADE 8
#define RAID_STATUS_DAMAGE 9
#define RAID_STATUS_IO_FAIL 10



#define BUF_MAX_RETRY 100

void normalevent_user(char *message,char *parm1);

void criticalevent_user(char *message,char *parm1);

void check_raid_status(mddev_t *mddev,int status);

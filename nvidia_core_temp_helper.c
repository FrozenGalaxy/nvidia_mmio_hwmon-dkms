#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <nvml.h>

/* ------------------------------------------------------------ */
/* Locate /sys/class/hwmon/hwmonX/nvml for nvidia_mmio           */
/* ------------------------------------------------------------ */

static int find_nvml_path(char *out, size_t len)
{
    DIR *d = opendir("/sys/class/hwmon");
    if (!d)
        return -1;

    struct dirent *ent;
    char namepath[PATH_MAX];

    while ((ent = readdir(d))) {
        if (strncmp(ent->d_name, "hwmon", 5) != 0)
            continue;

        if (snprintf(namepath, sizeof(namepath),
            "/sys/class/hwmon/%s/name",
            ent->d_name) >= (int)sizeof(namepath))
            continue;

        FILE *f = fopen(namepath, "r");
        if (!f)
            continue;

        char name[64] = {0};
        if (fgets(name, sizeof(name), f)) {
            if (strncmp(name, "nvidia_mmio", 11) == 0) {
                if (snprintf(out, len,
                    "/sys/class/hwmon/%s/nvml",
                    ent->d_name) < (int)len) {
                    fclose(f);
                closedir(d);
                return 0;
                    }
            }
        }
        fclose(f);
    }

    closedir(d);
    return -1;
}

/* ------------------------------------------------------------ */
/* NVML helpers                                                  */
/* ------------------------------------------------------------ */

static void nvml_err(const char *what, nvmlReturn_t r)
{
    fprintf(stderr, "%s: %s\n", what, nvmlErrorString(r));
}

/* ------------------------------------------------------------ */
/* main                                                         */
/* ------------------------------------------------------------ */

int main(void)
{
    nvmlDevice_t dev;
    nvmlReturn_t r;

    r = nvmlInit();
    if (r != NVML_SUCCESS) {
        nvml_err("nvmlInit", r);
        return 1;
    }

    r = nvmlDeviceGetHandleByIndex(0, &dev);
    if (r != NVML_SUCCESS) {
        nvml_err("nvmlDeviceGetHandleByIndex", r);
        nvmlShutdown();
        return 1;
    }

    unsigned int tempC = 0;
    unsigned int power_mW = 0;
    unsigned int fanPct = (unsigned int)-1;

    /* GPU core temperature (deprecated NVML API, silenced locally) */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &tempC) != NVML_SUCCESS)
        goto out;
    #pragma GCC diagnostic pop

    /* Power (mW from NVML) */
    if (nvmlDeviceGetPowerUsage(dev, &power_mW) != NVML_SUCCESS)
        goto out;

    /* Fan speed (percent; may be unsupported) */
    if (nvmlDeviceGetFanSpeed(dev, &fanPct) != NVML_SUCCESS)
        fanPct = (unsigned int)-1;

    char path[PATH_MAX];
    if (find_nvml_path(path, sizeof(path)) != 0) {
        fprintf(stderr, "nvml sysfs node not found\n");
        goto out;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        goto out;
    }

    /*
     * Kernel expects:
     *   <temp_mC> <power_uW> <fan>
     *
     * NVML power is mW → convert to µW for hwmon
     */
    fprintf(f, "%u %llu %d\n",
            tempC * 1000,
            (unsigned long long)power_mW * 1000ULL,
            (fanPct == (unsigned int)-1) ? -1 : (int)fanPct);

    fclose(f);

    out:
    nvmlShutdown();
    return 0;
}

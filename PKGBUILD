pkgname=nvidia-mmio-hwmon-dkms
pkgver=0.1
pkgrel=1
pkgdesc="Expose NVIDIA GPU Junction, VRAM and Core temperatures via hwmon (DKMS)"
arch=('x86_64')
license=('GPL')
depends=('dkms' 'nvidia-utils' 'lm_sensors')
makedepends=('cuda')
install=nvidia-mmio-hwmon.install

source=(
    'dkms.conf'
    'nvidia_mmio_hwmon.c'
    'Makefile'
    'nvidia_core_temp_helper.c'
    'nvidia-core-temp.service'
    'nvidia-core-temp.timer'
    'nvidia-core-temp.path'
    'nvidia-mmio-hwmon.modload.conf'
    'nvidia-mmio-hwmon.modprobe.conf'
)

sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

package() {
    install -d "$pkgdir/usr/src/nvidia-mmio-hwmon-0.1"

    install -Dm644 nvidia_mmio_hwmon.c \
        "$pkgdir/usr/src/nvidia-mmio-hwmon-0.1/nvidia_mmio_hwmon.c"

    install -Dm644 Makefile \
        "$pkgdir/usr/src/nvidia-mmio-hwmon-0.1/Makefile"

    install -Dm644 dkms.conf \
        "$pkgdir/usr/src/nvidia-mmio-hwmon-0.1/dkms.conf"

    install -Dm644 nvidia-mmio-hwmon.modload.conf \
        "$pkgdir/usr/lib/modules-load.d/nvidia-mmio-hwmon.conf"

    install -Dm644 nvidia-mmio-hwmon.modprobe.conf \
        "$pkgdir/usr/lib/modprobe.d/nvidia-mmio-hwmon.conf"

    # helper
    gcc -O3 -Wall -Wextra \
        -I/opt/cuda/include \
        nvidia_core_temp_helper.c \
        -lnvidia-ml \
        -o nvidia-core-temp-helper

    install -Dm755 nvidia-core-temp-helper \
        "$pkgdir/usr/libexec/nvidia-core-temp-helper"

    # systemd units
    install -Dm644 nvidia-core-temp.service \
        "$pkgdir/usr/lib/systemd/system/nvidia-core-temp.service"
    install -Dm644 nvidia-core-temp.timer \
        "$pkgdir/usr/lib/systemd/system/nvidia-core-temp.timer"
    install -Dm644 nvidia-core-temp.path \
        "$pkgdir/usr/lib/systemd/system/nvidia-core-temp.path"
}

